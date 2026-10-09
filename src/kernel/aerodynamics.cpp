#include "leo/kernel/aerodynamics.hpp"
#include <algorithm>
#include <array>
#include <numbers>
#include <cmath>
#include <stdexcept>
namespace leo {
namespace {
struct Segment { double base_altitude_m,density_kg_m3; };
// Reference altitude/density anchors in SI; original scale heights do not join.
constexpr Segment table[] = {
    {0,1.225}, {100e3,5.25e-7}, {150e3,1.73e-9}, {200e3,2.41e-10},
    {250e3,5.97e-11}, {300e3,1.87e-11}, {350e3,6.66e-12}, {400e3,2.62e-12},
    {450e3,1.09e-12}, {500e3,4.76e-13}, {550e3,2.14e-13}, {600e3,9.89e-14},
    {650e3,4.73e-14}, {700e3,2.36e-14}, {750e3,1.24e-14}, {800e3,6.95e-15},
    {850e3,4.22e-15}, {900e3,2.78e-15}, {950e3,1.98e-15}, {1000e3,1.49e-15},
    {1250e3,5.70e-16}, {1500e3,2.79e-16}, {2000e3,9.09e-17}, {2500e3,4.23e-17},
    {3000e3,2.54e-17}, {3500e3,1.77e-17}, {4000e3,1.34e-17}, {4500e3,1.06e-17},
    {5000e3,8.62e-18}, {6000e3,6.09e-18}, {7000e3,4.56e-18}, {8000e3,3.56e-18},
    {9000e3,2.87e-18}, {10000e3,2.37e-18}, {15000e3,1.21e-18}, {20000e3,7.92e-19},
    {25000e3,5.95e-19}, {30000e3,4.83e-19}, {35000e3,4.13e-19}, {35786e3,4.04e-19}};
constexpr std::size_t segment_count=std::size(table);
const auto log_density_gradients=[] {
    std::array<double,segment_count-1> gradients{};
    for(std::size_t i=0;i<gradients.size();++i)
        gradients[i]=std::log(table[i+1].density_kg_m3/table[i].density_kg_m3)/
                     (table[i+1].base_altitude_m-table[i].base_altitude_m);
    return gradients;
}();

// Normalized attitude plus the single relative wind shared by both drag models.
struct Flow {
    Quaternion attitude;
    BodyVector direction_body;
    double dynamic_pressure_pa;
};
Flow incoming_flow(const State& state,const EnvironmentSnapshot& env) {
    if (!std::isfinite(env.density_kg_m3)||env.density_kg_m3<0)
        throw std::invalid_argument("environment density must be finite and nonnegative");
    if (!env.atmosphere_angular_velocity_rad_s.as_eigen().allFinite())
        throw std::invalid_argument("nonfinite atmosphere rotation");
    if (!state.r.as_eigen().allFinite()||!state.v.as_eigen().allFinite())
        throw std::invalid_argument("nonfinite drag state");
    Flow flow{normalize_quaternion(state.q),{},0};
    const InertialVector v_relative=atmospheric_relative_velocity(state.r,state.v,env.atmosphere_angular_velocity_rad_s);
    const double speed=norm(v_relative);
    if (speed>0) flow.direction_body=inertial_to_body(flow.attitude,v_relative/speed);
    flow.dynamic_pressure_pa=0.5*env.density_kg_m3*speed*speed;
    if (!std::isfinite(speed)||!std::isfinite(flow.dynamic_pressure_pa))
        throw std::invalid_argument("nonfinite aerodynamic speed or pressure");
    return flow;
}
}
ExponentialAtmosphere::ExponentialAtmosphere(double density_scale):density_scale_(density_scale) {
    if (!std::isfinite(density_scale)||density_scale<0)
        throw std::invalid_argument("atmospheric density scale must be finite and nonnegative");
}
double ExponentialAtmosphere::density_kg_m3(double altitude_m) const {
    if (!std::isfinite(altitude_m)) throw std::invalid_argument("nonfinite altitude");
    if (altitude_m<min_altitude_m||altitude_m>max_altitude_m)
        throw std::out_of_range("altitude outside the tabulated atmosphere range 0 .. 35786 km");
    const Segment* begin=table;
    const Segment* upper=std::upper_bound(begin,begin+segment_count,altitude_m,
        [](double value,const Segment& s){ return value<s.base_altitude_m; });
    const std::size_t i=static_cast<std::size_t>(upper-begin)-1;
    const double unscaled=i==segment_count-1?table[i].density_kg_m3:
        table[i].density_kg_m3*std::exp((altitude_m-table[i].base_altitude_m)*log_density_gradients[i]);
    const double density=unscaled*density_scale_;
    if (!std::isfinite(density)) throw std::invalid_argument("nonfinite scaled atmospheric density");
    return density;
}
InertialVector atmospheric_relative_velocity(InertialVector r,InertialVector v,InertialVector rotation) {
    if (!r.as_eigen().allFinite()||!v.as_eigen().allFinite()||!rotation.as_eigen().allFinite())
        throw std::invalid_argument("nonfinite relative velocity inputs");
    const InertialVector relative=v-cross(rotation,r);
    if (!relative.as_eigen().allFinite()) throw std::invalid_argument("nonfinite atmospheric relative velocity");
    return relative;
}
Wrench flat_plate_drag(const Spacecraft& craft,const State& state,const EnvironmentSnapshot& env) {
    const Flow flow=incoming_flow(state,env);
    Wrench result{};
    if (flow.dynamic_pressure_pa==0) return result;
    BodyVector force_body{};
    for (const Surface& surface:craft.surfaces()) {
        const double incidence=dot(surface.normal_body(),flow.direction_body);
        if (incidence<=0) continue;
        const double magnitude=flow.dynamic_pressure_pa*surface.area_m2()*
            surface.aerodynamic().drag_coefficient()*incidence;
        const BodyVector force=flow.direction_body*(-magnitude);
        force_body=force_body+force;
        result.torque_nm=result.torque_nm+surface_body_torque(surface,force);
    }
    if (force_body.as_eigen().isZero(0)) return result;
    result.force_n=body_to_inertial(flow.attitude,force_body);
    if (!result.torque_nm.as_eigen().allFinite()) throw std::invalid_argument("nonfinite drag torque");
    return result;
}
SentmanCoefficients sentman_coefficients(double grazing_angle_rad,double accommodation) {
    if (!std::isfinite(grazing_angle_rad)||grazing_angle_rad<0||grazing_angle_rad>std::numbers::pi/2)
        throw std::invalid_argument("grazing angle must lie in [0, pi/2]");
    if (!std::isfinite(accommodation)||accommodation<0||accommodation>1)
        throw std::invalid_argument("accommodation must lie in [0, 1]");
    const double s=std::sin(grazing_angle_rad),c=std::cos(grazing_angle_rad);
    const double normal=(4-2*accommodation)*s*s;
    const double tangential=2*accommodation*s*c;
    // Factor the small lift directly; subtracting two O(1) momentum terms
    // loses relative accuracy near full accommodation. The drag sum is stable.
    return {normal*s+tangential*c,4*(1-accommodation)*s*s*c};
}
Wrench sentman_drag(const Spacecraft& craft,const State& state,const EnvironmentSnapshot& env) {
    const Flow flow=incoming_flow(state,env);
    Wrench result{};
    if (flow.dynamic_pressure_pa==0) return result;
    BodyVector force_body{};
    for (const Surface& surface:craft.surfaces()) {
        const BodyVector normal=surface.normal_body();
        const double sine_alpha=std::clamp(dot(normal,flow.direction_body),0.0,1.0);
        if (sine_alpha<=0) continue;
        const double sigma=surface.aerodynamic().accommodation();
        // Cn n + Ct t_hat with t_hat = (v_hat - sin(alpha) n)/cos(alpha) reduces exactly to
        // 4(1 - sigma) sin^2(alpha) n + 2 sigma sin(alpha) v_hat, so no lift axis or division
        // by cos(alpha) is needed and the face-on limit stays finite. The flow-parallel
        // component has the same analytic value as sentman_coefficients().drag.
        const BodyVector exchange=normal*(4*(1-sigma)*sine_alpha*sine_alpha)+
                                   flow.direction_body*(2*sigma*sine_alpha);
        const BodyVector force=exchange*(-flow.dynamic_pressure_pa*surface.area_m2());
        force_body=force_body+force;
        result.torque_nm=result.torque_nm+surface_body_torque(surface,force);
    }
    if (force_body.as_eigen().isZero(0)) return result;
    result.force_n=body_to_inertial(flow.attitude,force_body);
    if (!result.torque_nm.as_eigen().allFinite()) throw std::invalid_argument("nonfinite drag torque");
    return result;
}
}