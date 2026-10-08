#include "leo/kernel/dynamics.hpp"
#include "leo/kernel/aerodynamics.hpp"
#include "leo/kernel/radiation.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>
namespace leo {
EarthDynamics::EarthDynamics(EarthGravity gravity,Spacecraft spacecraft,EnvironmentalModels models)
    :gravity_(std::move(gravity)),spacecraft_(std::move(spacecraft)),models_(models) {
    if (!std::isfinite(models.earth_albedo)||models.earth_albedo<0||models.earth_albedo>1)
        throw std::invalid_argument("Earth albedo must be in [0,1]");
    if (models.drag_model!=DragModel::none&&models.drag_model!=DragModel::flat_plate&&models.drag_model!=DragModel::sentman)
        throw std::invalid_argument("unsupported drag model");
    if (models.drag.enabled()&&models.drag_model==DragModel::none)
        throw std::invalid_argument("enabled drag needs an explicit drag model");
}
bool EarthDynamics::needs_environment() const noexcept {
    return models_.srp.enabled()||models_.drag.enabled()||models_.albedo.enabled();
}
BodyVector gravity_gradient_torque(const Earth& earth,const State& s,const Inertia& inertia) {
    if (!std::isfinite(earth.mu_m3_s2)||earth.mu_m3_s2<=0||!s.r.as_eigen().allFinite())
        throw std::invalid_argument("invalid gravity-gradient input");
    const double radius=norm(s.r);
    if (!std::isfinite(radius)||radius<=0) throw std::invalid_argument("gravity-gradient needs nonzero radius");
    const BodyVector radial=inertial_to_body(s.q,s.r/radius);
    const BodyVector torque=cross(radial,inertia.multiply(radial))*(3*(earth.mu_m3_s2/radius)/radius/radius);
    if (!torque.as_eigen().allFinite()) throw std::invalid_argument("nonfinite gravity-gradient torque");
    return torque;
}
StateDerivative EarthDynamics::operator()(ElapsedTime t,const State& s) const {
    if (needs_environment()) throw std::invalid_argument("enabled environmental models need an explicit snapshot");
    return (*this)(t,s,EnvironmentSnapshot{});
}
DynamicsContributions EarthDynamics::evaluate(ElapsedTime t,const State& s,const EnvironmentSnapshot& env) const {
    if (!std::isfinite(t.seconds)||!s.r.as_eigen().allFinite()||!s.v.as_eigen().allFinite()||!s.omega.as_eigen().allFinite())
        throw std::invalid_argument("nonfinite dynamics input");
    (void)normalize_quaternion(s.q); // Validate only; evolving q must remain raw.
    DynamicsContributions result;
    const auto select=[](Wrench contribution,SurfaceContribution selection) {
        if (!selection.force) contribution.force_n={};
        if (!selection.torque) contribution.torque_nm={};
        return contribution;
    };
    if (models_.srp.enabled()) result.srp=select(solar_radiation_pressure(spacecraft_,s,env),models_.srp);
    if (models_.drag.enabled())
        result.drag=select(models_.drag_model==DragModel::flat_plate?flat_plate_drag(spacecraft_,s,env):
            sentman_drag(spacecraft_,s,env),models_.drag);
    if (models_.albedo.enabled())
        result.albedo=select(simplified_albedo(spacecraft_,s,env,gravity_.earth().equatorial_radius_m,models_.earth_albedo),models_.albedo);
    result.non_gravitational+=result.srp;
    result.non_gravitational+=result.drag;
    result.non_gravitational+=result.albedo;
    if (models_.gravity_gradient)
        result.gravity_gradient_nm=gravity_gradient_torque(gravity_.earth(),s,spacecraft_.inertia());
    result.non_gravitational.torque_nm=result.non_gravitational.torque_nm+result.gravity_gradient_nm;
    const auto gravity=gravity_.evaluate(s.r);
    result.point_mass_m_s2=gravity.point_mass_m_s2;
    result.j2_m_s2=gravity.j2_m_s2;
    result.total_acceleration_m_s2=result.point_mass_m_s2+result.j2_m_s2+result.non_gravitational.force_n/spacecraft_.mass_kg();
    if (!result.total_acceleration_m_s2.as_eigen().allFinite()) throw std::invalid_argument("nonfinite total acceleration");
    result.total_angular_acceleration_rad_s2=spacecraft_.inertia().solve(
        result.non_gravitational.torque_nm-cross(s.omega,spacecraft_.inertia().multiply(s.omega)));
    return result;
}
StateDerivative EarthDynamics::operator()(ElapsedTime t,const State& s,const EnvironmentSnapshot& env) const {
    const auto result=evaluate(t,s,env);
    return {s.v,result.total_acceleration_m_s2,quaternion_derivative(s.q,s.omega),
            result.total_angular_acceleration_rad_s2};
}
} // namespace leo
