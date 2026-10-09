#include "bench_support.hpp"
#include "experiments/scenario.hpp"
#include "leo/integration/detail/packed_state.hpp"
#include <boost/numeric/odeint/stepper/runge_kutta_fehlberg78.hpp>
#include <boost/numeric/odeint/stepper/controlled_runge_kutta.hpp>
#include "SpiceUsr.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <type_traits>
using namespace bench;
using Packed=detail::PackedState;
namespace {
struct Config {
    std::string spk,lsk,scenario="full",method="dopri",case_file;
    double duration=21600,rtol=1e-10,atol_scale=1,max_step=15,perturb=0,cadence=30,attitude_seed=0;
    int ulps=0;
    double density_factor=1,area_factor=1,mass_factor=1,inertia_factor=1,cd_factor=1,spin_factor=1;
    double inertia_x_factor=1,inertia_y_factor=1,inertia_z_factor=1;
    double cp_x=0,cp_y=0,cp_z=0,sigma_delta=0,specular_delta=0,diffuse_delta=0;
    double position_radial=0,velocity_along=0,attitude_orbit=0,omega_x=0,omega_y=0,omega_z=0;
    bool disable_srp=false,disable_gg=false,disable_drag_torque=false,disable_albedo_torque=false,disable_srp_torque=false;
};
Config parse_args(int argc,char** argv) {
    Config o;
    for(int i=1;i<argc;++i) {
        const std::string k=argv[i];if(i+1==argc) throw std::invalid_argument("missing argument");
        const std::string v=argv[++i];
        if(k=="--spk") o.spk=v;else if(k=="--lsk") o.lsk=v;
        else if(k=="--scenario") o.scenario=v;else if(k=="--method") o.method=v;
        else if(k=="--case-file") o.case_file=v;
        else if(k=="--duration") o.duration=std::stod(v);else if(k=="--rtol") o.rtol=std::stod(v);
        else if(k=="--atol-scale") o.atol_scale=std::stod(v);else if(k=="--max-step") o.max_step=std::stod(v);
        else if(k=="--perturb-x") o.perturb=std::stod(v);else if(k=="--ulp-x") o.ulps=std::stoi(v);
        else if(k=="--attitude-seed") o.attitude_seed=std::stod(v);
        else if(k=="--density-factor") o.density_factor=std::stod(v);
        else if(k=="--area-factor") o.area_factor=std::stod(v);
        else if(k=="--mass-factor") o.mass_factor=std::stod(v);
        else if(k=="--inertia-factor") o.inertia_factor=std::stod(v);
        else if(k=="--inertia-x-factor") o.inertia_x_factor=std::stod(v);
        else if(k=="--inertia-y-factor") o.inertia_y_factor=std::stod(v);
        else if(k=="--inertia-z-factor") o.inertia_z_factor=std::stod(v);
        else if(k=="--cd-factor") o.cd_factor=std::stod(v);
        else if(k=="--spin-factor") o.spin_factor=std::stod(v);
        else if(k=="--cp-x") o.cp_x=std::stod(v);else if(k=="--cp-y") o.cp_y=std::stod(v);else if(k=="--cp-z") o.cp_z=std::stod(v);
        else if(k=="--sigma-delta") o.sigma_delta=std::stod(v);
        else if(k=="--specular-delta") o.specular_delta=std::stod(v);
        else if(k=="--diffuse-delta") o.diffuse_delta=std::stod(v);
        else if(k=="--position-radial") o.position_radial=std::stod(v);
        else if(k=="--velocity-along") o.velocity_along=std::stod(v);
        else if(k=="--attitude-orbit") o.attitude_orbit=std::stod(v);
        else if(k=="--omega-x") o.omega_x=std::stod(v);else if(k=="--omega-y") o.omega_y=std::stod(v);else if(k=="--omega-z") o.omega_z=std::stod(v);
        else if(k=="--disable-srp") o.disable_srp=std::stoi(v)!=0;
        else if(k=="--disable-gg") o.disable_gg=std::stoi(v)!=0;
        else if(k=="--disable-drag-torque") o.disable_drag_torque=std::stoi(v)!=0;
        else if(k=="--disable-albedo-torque") o.disable_albedo_torque=std::stoi(v)!=0;
        else if(k=="--disable-srp-torque") o.disable_srp_torque=std::stoi(v)!=0;
        else if(k=="--cadence") o.cadence=std::stod(v);else throw std::invalid_argument("unknown argument");
    }
    return o;
}
struct PerturbedSun:EphemerisProvider {
    const EphemerisProvider& source;const Config& config;
    mutable std::size_t calls=0;mutable double max_actual=0;
    PerturbedSun(const EphemerisProvider& s,const Config& c):source(s),config(c) {}
    EphemerisState geometric_state(int target,Epoch epoch)const override {
        auto s=source.geometric_state(target,epoch);double x=s.position_m.x()+config.perturb;
        for(int i=0;i<std::abs(config.ulps);++i) x=std::nextafter(x,config.ulps>0?INFINITY:-INFINITY);
        max_actual=std::max(max_actual,std::abs(x-s.position_m.x()));++calls;
        s.position_m={x,s.position_m.y(),s.position_m.z()};return s;
    }
};
EarthDynamics configured_dynamics(const Config& o,const experiment::Case& spec) {
    auto d=dynamics(true);auto m=d.models();const auto base=spec.craft();
    const bool full=o.scenario.rfind("full",0)==0;
    if(!full) {
        m={};
        if(o.scenario=="srp") m.srp={true,true};
        else if(o.scenario=="drag") {m.drag={true,true};m.drag_model=DragModel::sentman;}
        else if(o.scenario=="albedo") m.albedo={true,true};
        else if(o.scenario=="gg") m.gravity_gradient=true;
        else if(o.scenario!="two_body"&&o.scenario!="j2"&&o.scenario!="attitude"&&o.scenario!="principal")
            throw std::invalid_argument("unknown scenario");
    }
    if(o.scenario=="full_no_torque") {m.srp.torque=false;m.drag.torque=false;m.albedo.torque=false;m.gravity_gradient=false;}
    if(o.disable_srp) m.srp={};
    if(o.disable_gg) m.gravity_gradient=false;
    // Torque-only switches: the translational contribution of the same source
    // stays enabled, so a run isolates attitude torque from orbital forcing.
    if(o.disable_drag_torque) m.drag.torque=false;
    if(o.disable_albedo_torque) m.albedo.torque=false;
    if(o.disable_srp_torque) m.srp.torque=false;
    std::vector<Surface> surfaces;
    for(const auto& s:base.surfaces())
        surfaces.emplace_back(s.area_m2()*o.area_factor,s.normal_body(),s.center_of_pressure_body()+BodyVector{o.cp_x,o.cp_y,o.cp_z},
            OpticalProperties{s.optical().specular()+o.specular_delta,s.optical().diffuse()+o.diffuse_delta},
            AerodynamicProperties{s.aerodynamic().drag_coefficient()*o.cd_factor,s.aerodynamic().accommodation()+o.sigma_delta});
    Eigen::Matrix3d tensor=experiment::tensor(base)*o.inertia_factor;
    tensor(0,0)*=o.inertia_x_factor;tensor(1,1)*=o.inertia_y_factor;tensor(2,2)*=o.inertia_z_factor;
    Spacecraft craft(base.mass_kg()*o.mass_factor,Inertia(tensor),std::move(surfaces));
    return EarthDynamics(EarthGravity(Earth{},o.scenario!="two_body"&&o.scenario!="attitude"&&o.scenario!="principal"),std::move(craft),m);
}
// First five members are the historical trace layout and keep their positions:
// run.py and precision_probe.py index rows positionally up to illumination.
struct Step {
    double t,h,error;
    bool accepted;
    double illumination;
    double proposed_h,altitude_m,density_kg_m3;
    std::uint64_t solar_mask,wind_mask;
    bool clipped_to_output,clipped_to_max_step;
    double solar_projected_area_m2,wind_projected_area_m2;
    double solar_silhouette_m2,wind_silhouette_m2;
};
// Surface-by-surface incidence along one unit direction. One-sided projected
// area reproduces the sign convention of solar_radiation_pressure (n.s > 0) and
// of both drag laws (n.v_hat > 0); the silhouette uses |n.d| and is a purely
// geometric quantity. No force law is evaluated here.
void surface_incidence(const Spacecraft& craft,BodyVector direction,std::uint64_t& mask,
                       double& projected_m2,double& silhouette_m2) {
    mask=0;projected_m2=0;silhouette_m2=0;std::size_t i=0;
    for(const auto& s:craft.surfaces()) {
        const double c=dot(s.normal_body(),direction);
        if(c>0) {mask|=std::uint64_t(1)<<i;projected_m2+=s.area_m2()*c;}
        silhouette_m2+=s.area_m2()*std::abs(c);
        ++i;
    }
}
// Counts transition events in an ordered sequence of real integration states
// (accepted trace steps or reported output samples). Nothing here decides a
// step; it only classifies changes between states the integrator produced.
struct EventProbe {
    // Relative difference between adjacent log-density slopes above which the
    // ExponentialAtmosphere piecewise-exponential profile must have crossed a
    // knot. Inside one segment log(density) is exactly affine in altitude, so a
    // mismatch far above round-off and far below any real knot-to-knot slope
    // change marks a segment boundary inside the sampled interval.
    static constexpr double knot_relative_tolerance=1e-6;
    std::size_t observations=0,solar_face_switches=0,wind_face_switches=0;
    std::size_t umbra_entries=0,umbra_exits=0,penumbra_entries=0,penumbra_exits=0;
    std::size_t atmosphere_intervals_examined=0,atmosphere_intervals_skipped=0,atmosphere_segment_changes=0;
    double min_altitude_m=0,max_altitude_m=0,min_density_kg_m3=0,max_density_kg_m3=0;
    double older_altitude_m=0,older_density_kg_m3=0;
    double altitude_m=0,density_kg_m3=0,illumination=1;
    std::uint64_t solar_mask=0,wind_mask=0;
    void observe(double next_altitude_m,double next_density_kg_m3,std::uint64_t next_solar_mask,
                 std::uint64_t next_wind_mask,double next_illumination) {
        if(observations==0) {min_altitude_m=max_altitude_m=next_altitude_m;min_density_kg_m3=max_density_kg_m3=next_density_kg_m3;}
        else {
            min_altitude_m=std::min(min_altitude_m,next_altitude_m);
            max_altitude_m=std::max(max_altitude_m,next_altitude_m);
            min_density_kg_m3=std::min(min_density_kg_m3,next_density_kg_m3);
            max_density_kg_m3=std::max(max_density_kg_m3,next_density_kg_m3);
            if(next_solar_mask!=solar_mask) ++solar_face_switches;
            if(next_wind_mask!=wind_mask) ++wind_face_switches;
            const bool was_umbra=illumination<=0,umbra=next_illumination<=0;
            if(was_umbra&&!umbra) ++umbra_exits;
            if(!was_umbra&&umbra) ++umbra_entries;
            const bool was_partial=illumination>0&&illumination<1,partial=next_illumination>0&&next_illumination<1;
            if(was_partial&&!partial) ++penumbra_exits;
            if(!was_partial&&partial) ++penumbra_entries;
        }
        // Three consecutive states span two log-density segments; a knot inside
        // either one breaks the affine-in-altitude law the model holds per knot.
        // A knot falling exactly on a sampled state leaves both intervals clean
        // and is not counted, so this is a lower bound on crossings, not an
        // exhaustive census. The examined/skipped counters expose the coverage.
        if(observations>=2) {
            if(density_kg_m3>0&&older_density_kg_m3>0&&altitude_m!=older_altitude_m&&next_altitude_m!=altitude_m) {
                const double earlier=(std::log(density_kg_m3)-std::log(older_density_kg_m3))/(altitude_m-older_altitude_m);
                const double later=(std::log(next_density_kg_m3)-std::log(density_kg_m3))/(next_altitude_m-altitude_m);
                const double scale=std::max({std::abs(earlier),std::abs(later),1e-30});
                ++atmosphere_intervals_examined;
                if(std::abs(later-earlier)>knot_relative_tolerance*scale) ++atmosphere_segment_changes;
            } else ++atmosphere_intervals_skipped;
        }
        older_altitude_m=altitude_m;older_density_kg_m3=density_kg_m3;
        altitude_m=next_altitude_m;density_kg_m3=next_density_kg_m3;
        solar_mask=next_solar_mask;wind_mask=next_wind_mask;illumination=next_illumination;
        ++observations;
    }
    // Counters that only a step-level method actually measures are passed as -1 and
    // reported as JSON null, never as a manufactured zero.
    Json json(const std::string& source,long long rejected,long long clipped_to_output,
             long long clipped_to_max_step) const {
        Json j;j.add("source",source);j.add("states",static_cast<double>(observations));
        j.add("solar_face_switches",static_cast<double>(solar_face_switches));
        j.add("wind_face_switches",static_cast<double>(wind_face_switches));
        j.add("umbra_entries",static_cast<double>(umbra_entries));
        j.add("umbra_exits",static_cast<double>(umbra_exits));
        j.add("penumbra_entries",static_cast<double>(penumbra_entries));
        j.add("penumbra_exits",static_cast<double>(penumbra_exits));
        j.add("atmosphere_segment_changes",static_cast<double>(atmosphere_segment_changes));
        j.add("atmosphere_intervals_examined",static_cast<double>(atmosphere_intervals_examined));
        j.add("atmosphere_intervals_skipped",static_cast<double>(atmosphere_intervals_skipped));
        j.add("knot_relative_slope_tolerance",knot_relative_tolerance);
        if(observations) {
            j.add("altitude_min_m",min_altitude_m);j.add("altitude_max_m",max_altitude_m);
            j.add("density_min_kg_m3",min_density_kg_m3);j.add("density_max_kg_m3",max_density_kg_m3);
        } else {
            j.raw("altitude_min_m","null");j.raw("altitude_max_m","null");
            j.raw("density_min_kg_m3","null");j.raw("density_max_kg_m3","null");
        }
        const auto count=[&](const char* key,long long value) {
            if(value<0) j.raw(key,"null");else j.add(key,static_cast<double>(value));};
        count("rejected_steps",rejected);
        count("steps_clipped_to_output",clipped_to_output);
        count("steps_clipped_to_max_step",clipped_to_max_step);
        return j;
    }
};
// Diagnostic-only mirror of the production Dopri loop. Its output is checked
// bit-for-bit against production before interpreting its trace. It is NOT an
// independent accuracy oracle; RKF78 and fixed RK4 below supply other methods.
template<class Rhs> Trajectory trace_dopri(const Rhs& rhs,const State& initial,const std::vector<double>& times,
                                         const IntegrationOptions& o,const EarthEnvironment& env,
                                         const Spacecraft& craft,std::vector<Step>& log) {
    Packed y=detail::pack(validated_state(initial)),dy{},trial{},trial_dy{},error{};
    auto system=[&](const Packed& p,Packed& d,double t) {d=detail::pack(rhs({t},detail::unpack(p)));};
    boost::numeric::odeint::runge_kutta_dopri5<Packed> stepper;
    Trajectory out{Epoch{0},{{ElapsedTime{0},validated_state(initial)}}};
    double t=0,proposed=o.initial_step_s;system(y,dy,t);
    for(std::size_t i=1;i<times.size();++i) {
        const double target=times[i];
        while(t<target) {
            if(log.size()>=o.max_attempts) throw std::runtime_error("trace attempt limit");
            const double remaining=target-t,h=std::min({proposed,o.max_step_s,remaining}),endpoint=t+h;
            stepper.do_step(system,y,dy,t,trial,trial_dy,h,error);
            const double e=detail::scaled_error(y,trial,error,o);
            const auto q=normalize_quaternion({trial[6],trial[7],trial[8],trial[9]});const bool accepted=e<=1;
            // Diagnostic-only: one extra environment query per attempt, exactly as
            // before. It does not touch the integrated state or the step decision.
            const auto snapshot=env({t},detail::unpack(y));
            const auto state=detail::unpack(y);
            std::uint64_t solar_mask=0,wind_mask=0;
            double solar_projected=0,solar_silhouette=0,wind_projected=0,wind_silhouette=0;
            const auto to_sun=snapshot.sun_position_m-state.r;const double sun_distance=norm(to_sun);
            if(sun_distance>0)
                surface_incidence(craft,inertial_to_body(state.q,to_sun/sun_distance),solar_mask,solar_projected,solar_silhouette);
            const auto wind=atmospheric_relative_velocity(state.r,state.v,snapshot.atmosphere_angular_velocity_rad_s);
            const double wind_speed=norm(wind);
            if(wind_speed>0)
                surface_incidence(craft,inertial_to_body(state.q,wind/wind_speed),wind_mask,wind_projected,wind_silhouette);
            log.push_back({t,h,e,accepted,snapshot.illumination,proposed,norm(state.r)-Earth{}.equatorial_radius_m,
                snapshot.density_kg_m3,solar_mask,wind_mask,h==remaining,h==o.max_step_s,
                solar_projected,wind_projected,solar_silhouette,wind_silhouette});
            proposed=detail::next_step(h,e,accepted,o.max_step_s);
            if(!accepted) continue;
            const bool changed=q.w!=trial[6]||q.x!=trial[7]||q.y!=trial[8]||q.z!=trial[9];
            trial[6]=q.w;trial[7]=q.x;trial[8]=q.y;trial[9]=q.z;
            const double accepted_time=h==remaining?target:endpoint;
            if(changed||accepted_time!=endpoint) system(trial,trial_dy,accepted_time);
            y=trial;dy=trial_dy;t=accepted_time;
        }
        out.samples.push_back({ElapsedTime{target},detail::unpack(y)});
    }
    return out;
}
template<class Rhs> Trajectory reference_rkf78(const Rhs& rhs,const State& initial,const std::vector<double>& times,
                                              const IntegrationOptions& o,std::size_t& attempts,std::size_t& rejected) {
    // Standard Boost controlled RKF7(8), different tableau, embedded order and
    // controller/error-checker from production. Field scaling sets physical atols.
    using namespace boost::numeric::odeint;
    const Packed scale{o.position_atol_m,o.position_atol_m,o.position_atol_m,
        o.velocity_atol_m_s,o.velocity_atol_m_s,o.velocity_atol_m_s,
        o.quaternion_atol,o.quaternion_atol,o.quaternion_atol,o.quaternion_atol,
        o.omega_atol_rad_s,o.omega_atol_rad_s,o.omega_atol_rad_s};
    Packed z=leo::detail::pack(validated_state(initial));for(std::size_t i=0;i<13;++i) z[i]/=scale[i];
    auto system=[&](const Packed& scaled,Packed& derivative,double t) {
        Packed physical{};for(std::size_t i=0;i<13;++i) physical[i]=scaled[i]*scale[i];
        derivative=leo::detail::pack(rhs({t},leo::detail::unpack(physical)));
        for(std::size_t i=0;i<13;++i) derivative[i]/=scale[i];
    };
    runge_kutta_fehlberg78<Packed> stepper;
    array_algebra algebra;
    default_error_checker<double,array_algebra,default_operations> checker(1.,o.rtol);
    default_step_adjuster<double,double> adjuster(o.max_step_s);
    Trajectory out{Epoch{0},{{ElapsedTime{0},validated_state(initial)}}};double t=0,h=o.initial_step_s;
    for(std::size_t i=1;i<times.size();++i) {
        while(t<times[i]) {
            const double remaining=times[i]-t;h=std::min({h,o.max_step_s,remaining});
            if(h<=0||t+h==t||++attempts>o.max_attempts) throw std::runtime_error("RKF78 cannot advance");
            Packed dz{},trial{},error{};system(z,dz,t);
            stepper.do_step(system,z,dz,t,trial,h,error);
            const double norm=checker.error(algebra,z,dz,error,h);
            if(!std::isfinite(norm)) throw std::runtime_error("nonfinite RKF78 error");
            if(norm<=1) {
                z=trial;t=h==remaining?times[i]:t+h;
                h=adjuster.increase_step(h,norm,stepper.stepper_order());
            } else {
                ++rejected;h=adjuster.decrease_step(h,norm,stepper.error_order());
            }
        }
        Packed physical{};for(std::size_t k=0;k<13;++k) physical[k]=z[k]*scale[k];
        State s=leo::detail::unpack(physical);s.q=normalize_quaternion(s.q);out.samples.push_back({ElapsedTime{times[i]},s});
    }
    return out;
}
template<class Real=double,class Rhs> Trajectory fixed_rk4(const Rhs& rhs,const State& initial,const std::vector<double>& times,double step) {
    using Buffer=std::array<Real,13>;
    const auto first=detail::pack(validated_state(initial));Buffer y{};for(std::size_t k=0;k<13;++k) y[k]=first[k];
    Trajectory out{Epoch{0},{{ElapsedTime{0},validated_state(initial)}}};double t=0;
    auto to_double=[](const Buffer& p){Packed d{};for(std::size_t k=0;k<13;++k) d[k]=static_cast<double>(p[k]);return d;};
    auto evaluate=[&](const Buffer& p,double time){
        const auto d=detail::pack(rhs({time},detail::unpack(to_double(p))));Buffer result{};
        for(std::size_t k=0;k<13;++k) result[k]=d[k];
        return result;
    };
    for(std::size_t i=1;i<times.size();++i) {
        while(t<times[i]) {
            const double h=std::min(step,times[i]-t);if(t+h==t) throw std::runtime_error("RK4 cannot advance");
            const auto k1=evaluate(y,t);Buffer trial{};
            for(std::size_t k=0;k<13;++k) trial[k]=y[k]+h*.5*k1[k];
            const auto k2=evaluate(trial,t+h*.5);
            for(std::size_t k=0;k<13;++k) trial[k]=y[k]+h*.5*k2[k];
            const auto k3=evaluate(trial,t+h*.5);
            for(std::size_t k=0;k<13;++k) trial[k]=y[k]+h*k3[k];
            const auto k4=evaluate(trial,t+h);
            for(std::size_t k=0;k<13;++k) y[k]+=h*(k1[k]+2*k2[k]+2*k3[k]+k4[k])/6;
            if constexpr(std::is_same_v<Real,double>) {
                const auto q=normalize_quaternion({y[6],y[7],y[8],y[9]});y[6]=q.w;y[7]=q.x;y[8]=q.y;y[9]=q.z;
            } else {
                const Real scale=std::max({std::abs(y[6]),std::abs(y[7]),std::abs(y[8]),std::abs(y[9])});
                Real n=0;for(std::size_t k=6;k<10;++k) {y[k]/=scale;n+=y[k]*y[k];}
                n=std::sqrt(n);for(std::size_t k=6;k<10;++k) y[k]/=n;
            }
            t=h==times[i]-t?times[i]:t+h;
        }
        out.samples.push_back({ElapsedTime{times[i]},detail::unpack(to_double(y))});
    }
    return out;
}
Json spice_checks(const EphemerisProvider& provider) {
    Json j;Stats position,velocity,repeat;double max_epoch_add_error=0;
    for(double t:{0.,123.25,21600.,1e8,1e8+123.25}) {
        const auto s=provider.geometric_state(10,{t}),s2=provider.geometric_state(10,{t});repeat.add(norm(s.position_m-s2.position_m));
        double raw[6],light;spkez_c(10,t,"J2000","NONE",399,raw,&light);
        position.add(norm(s.position_m-InertialVector{raw[0]*1000,raw[1]*1000,raw[2]*1000}));
        velocity.add(norm(s.velocity_m_s-InertialVector{raw[3]*1000,raw[4]*1000,raw[5]*1000}));
        const double dt=10,left=t-dt,right=t+dt;
        const auto l=provider.geometric_state(10,{left}),r=provider.geometric_state(10,{right});
        j.add("finite_difference_velocity_error_at_"+std::to_string(t),norm((r.position_m-l.position_m)/(right-left)-s.velocity_m_s));
        const double nearby=std::nextafter(t,std::numeric_limits<double>::infinity());
        j.add("nearby_epoch_delta_s_at_"+std::to_string(t),nearby-t);
        j.add("nearby_sun_position_delta_m_at_"+std::to_string(t),norm(provider.geometric_state(10,{nearby}).position_m-s.position_m));
        max_epoch_add_error=std::max(max_epoch_add_error,std::abs((t+1e-9)-t-1e-9));
    }
    j.raw("raw_cspice_position_error_m",position.json());j.raw("raw_cspice_velocity_error_m_s",velocity.json());j.raw("repeated_query_error_m",repeat.json());
    j.add("max_1ns_absolute_epoch_addition_error_s",max_epoch_add_error);return j;
}
}
int main(int argc,char** argv) {
    try {
        const auto c=parse_args(argc,argv);
        SpiceEphemerisProvider direct({c.lsk,c.spk});PerturbedSun sun(direct,c);
        if(c.method=="spice") {std::cout<<spice_checks(direct).str()<<'\n';return 0;}
        const auto spec=experiment::load(c.case_file);const auto rhs=configured_dynamics(c,spec);State start=spec.start();
        if(c.scenario=="principal") {start.q={1,0,0,0};start.omega={0,0,.002};}
        if(c.attitude_seed!=0) {
            const double a=.5*c.attitude_seed;const auto q=start.q;
            start.q=normalize_quaternion({std::cos(a)*q.w-std::sin(a)*q.z,
                std::cos(a)*q.x-std::sin(a)*q.y,
                std::cos(a)*q.y+std::sin(a)*q.x,
                std::cos(a)*q.z+std::sin(a)*q.w});
        }
        const auto radial=start.r/norm(start.r),orbit_normal=cross(start.r,start.v)/norm(cross(start.r,start.v)),along=cross(orbit_normal,radial);
        start.r=start.r+radial*c.position_radial;start.v=start.v+along*c.velocity_along;
        start.omega=start.omega*c.spin_factor+BodyVector{c.omega_x,c.omega_y,c.omega_z};
        if(c.attitude_orbit!=0) {
            const double a=.5*c.attitude_orbit;const auto q=start.q;
            const Quaternion d{std::cos(a),orbit_normal.x()*std::sin(a),orbit_normal.y()*std::sin(a),orbit_normal.z()*std::sin(a)};
            start.q=normalize_quaternion({d.w*q.w-d.x*q.x-d.y*q.y-d.z*q.z,
                d.w*q.x+d.x*q.w+d.y*q.z-d.z*q.y,
                d.w*q.y-d.x*q.z+d.y*q.w+d.z*q.x,
                d.w*q.z+d.x*q.y-d.y*q.x+d.z*q.w});
        }
        IntegrationOptions options=tolerances();options.rtol=c.rtol;
        options.position_atol_m*=c.atol_scale;options.velocity_atol_m_s*=c.atol_scale;
        options.quaternion_atol*=c.atol_scale;options.omega_atol_rad_s*=c.atol_scale;
        options.max_step_s=c.max_step;options.initial_step_s=std::min(1.,c.max_step);options.max_attempts=10000000;
        const bool environment=rhs.needs_environment();
        EarthEnvironment source(Earth{},Epoch{0},environment?&sun:nullptr,environment,c.scenario=="full_no_eclipse"?EclipseModel::none:EclipseModel::conical,ExponentialAtmosphere{spec.density_scale*c.density_factor});
        std::vector<double> labels{0};for(double t=c.cadence;t<c.duration;t+=c.cadence) labels.push_back(t);labels.push_back(c.duration);
        std::size_t rhs_calls=0,attempts=0,rejected=0;
        auto system=[&](ElapsedTime t,const State& s){++rhs_calls;return rhs(t,s,source(t,s));};
        std::vector<Step> trace;Trajectory result;
        const auto timer=Clock::now();
        if(c.method=="dopri") result=propagate(system,start,{0},labels,options);
        else if(c.method=="trace") result=trace_dopri(system,start,labels,options,source,rhs.spacecraft(),trace);
        else if(c.method=="rkf78") result=reference_rkf78(system,start,labels,options,attempts,rejected);
        else if(c.method=="rk4") result=fixed_rk4(system,start,labels,c.max_step);
        else if(c.method=="mixed_rk4") result=fixed_rk4<long double>(system,start,labels,c.max_step);
        else if(c.method=="replay") {
            result.epoch={0};double t;Packed p{};
            while(std::cin>>t) {
                for(double& value:p) if(!(std::cin>>value)) throw std::invalid_argument("incomplete replay state");
                result.samples.push_back({ElapsedTime{t},detail::unpack(p)});
            }
            if(result.samples.empty()) throw std::invalid_argument("empty replay");
        }
        else throw std::invalid_argument("unknown integrator");
        const double wall=elapsed(timer);
        Json j;j.add("scenario",c.scenario);j.add("method",c.method);j.add("duration_s",c.duration);j.add("rtol",c.rtol);j.add("atol_scale",c.atol_scale);j.add("max_step_s",c.max_step);
        j.add("perturb_x_requested_m",c.perturb);j.add("ulp_x",static_cast<double>(c.ulps));j.add("actual_sun_x_perturbation_m",sun.max_actual);
        j.add("cadence_s",c.cadence);
        j.add("initial_attitude_seed_rad",c.attitude_seed);
        j.add("double_mantissa_bits",static_cast<double>(std::numeric_limits<double>::digits));
        j.add("long_double_mantissa_bits",static_cast<double>(std::numeric_limits<long double>::digits));
        j.add("state_precision",c.method=="mixed_rk4"?"long double integration / double force model":"double");
        j.add("force_ephemeris_precision","double CSPICE and physical kernels");
        Json physical;physical.add("case_id",spec.id);physical.add("version",static_cast<double>(spec.version));physical.add("source_file",spec.file);
        physical.add("legacy_benchmark_factory",spec.legacy);
        physical.add("density_scale_case",spec.density_scale);
        physical.add("density_scale_applied",spec.density_scale*c.density_factor);
        physical.add("earth_albedo",rhs.models().earth_albedo);
        physical.add("j2_enabled",rhs.gravity().j2_enabled());
        Json models;models.add("srp_force",rhs.models().srp.force);models.add("srp_torque",rhs.models().srp.torque);
        models.add("drag_force",rhs.models().drag.force);models.add("drag_torque",rhs.models().drag.torque);
        models.add("albedo_force",rhs.models().albedo.force);models.add("albedo_torque",rhs.models().albedo.torque);
        models.add("gravity_gradient",rhs.models().gravity_gradient);
        models.add("drag_model",rhs.models().drag_model==DragModel::sentman?"sentman":rhs.models().drag_model==DragModel::flat_plate?"flat_plate":"none");
        models.add("needs_environment",rhs.needs_environment());
        models.add("eclipse_model",c.scenario=="full_no_eclipse"?"none":"conical");
        models.add("atmosphere_acquisition",environment);
        physical.raw("models",models.str());
        // Realised initial conditions after every explicit perturbation, which is
        // what the integrator actually started from.
        Json initial;initial.vector("position_m",start.r);initial.vector("velocity_m_s",start.v);
        initial.add("quaternion_norm",std::sqrt(start.q.w*start.q.w+start.q.x*start.q.x+start.q.y*start.q.y+start.q.z*start.q.z));
        initial.vector("omega_body_rad_s",start.omega);
        initial.add("omega_norm_rad_s",norm(start.omega));
        initial.add("initial_radius_m",norm(start.r));
        initial.add("initial_altitude_m",norm(start.r)-Earth{}.equatorial_radius_m);
        initial.add("initial_speed_m_s",norm(start.v));
        // Zero body rate carries no spin direction: report the vector and say so
        // rather than substituting a manufactured axis.
        if(norm(start.omega)>0) initial.vector("spin_axis_body",start.omega/norm(start.omega));
        else {initial.raw("spin_axis_body","null");initial.add("spin_axis_undefined_reason","zero initial body rate has no direction");}
        // --spin-factor multiplies the case body rate. Where that rate is zero the
        // multiplicative perturbation is identically zero and its gradient is
        // undefined; the additive --omega-* axes remain well defined there.
        initial.add("spin_factor_well_defined",norm(start.omega)>0);
        if(norm(start.omega)<=0&&c.spin_factor!=1)
            initial.add("spin_factor_degenerate_reason","multiplicative spin perturbation of a zero initial body rate has no direction");
        physical.raw("initial_state",initial.str());
        Json scales;
        scales.add("density_factor",c.density_factor);scales.add("area_factor",c.area_factor);
        scales.add("mass_factor",c.mass_factor);scales.add("inertia_factor",c.inertia_factor);
        scales.add("inertia_x_factor",c.inertia_x_factor);scales.add("inertia_y_factor",c.inertia_y_factor);scales.add("inertia_z_factor",c.inertia_z_factor);
        scales.add("cd_factor",c.cd_factor);scales.add("spin_factor",c.spin_factor);
        scales.add("cp_offset_x_m",c.cp_x);scales.add("cp_offset_y_m",c.cp_y);scales.add("cp_offset_z_m",c.cp_z);
        scales.add("sigma_delta",c.sigma_delta);scales.add("specular_delta",c.specular_delta);scales.add("diffuse_delta",c.diffuse_delta);
        scales.add("position_radial_m",c.position_radial);scales.add("velocity_along_m_s",c.velocity_along);
        scales.add("attitude_orbit_rad",c.attitude_orbit);scales.add("attitude_seed_rad",c.attitude_seed);
        scales.add("omega_x_rad_s",c.omega_x);scales.add("omega_y_rad_s",c.omega_y);scales.add("omega_z_rad_s",c.omega_z);
        scales.add("sun_position_perturbation_m",c.perturb);scales.add("sun_position_ulps",static_cast<double>(c.ulps));
        scales.add("sun_position_perturbation_applied_m",sun.max_actual);
        physical.raw("perturbation_scales",scales.str());
        physical.add("mass_kg",rhs.spacecraft().mass_kg());
        physical.add("surface_count",static_cast<double>(rhs.spacecraft().surfaces().size()));
        physical.add("total_area_m2",std::accumulate(rhs.spacecraft().surfaces().begin(),rhs.spacecraft().surfaces().end(),0.,
            [](double sum,const Surface& s){return sum+s.area_m2();}));
        const auto inertia=experiment::tensor(rhs.spacecraft());
        physical.vector("inertia_diagonal_kg_m2",BodyVector{inertia(0,0),inertia(1,1),inertia(2,2)});
        physical.add("inertia_asymmetry_ratio",inertia(2,2)>0?inertia(0,0)/inertia(2,2):0.);
        std::ostringstream surface_rows;surface_rows<<std::setprecision(17)<<'[';bool surface_first=true;
        // Surface index is the bit position used by the solar/wind incidence
        // masks reported per sample and per trace step.
        for(std::size_t index=0;index<rhs.spacecraft().surfaces().size();++index) {
            const auto& s=rhs.spacecraft().surfaces()[index];
            Json row;row.add("index",static_cast<double>(index));
            row.add("area_m2",s.area_m2());row.vector("normal_body",s.normal_body());row.vector("cp_body",s.center_of_pressure_body());
            row.add("specular",s.optical().specular());row.add("diffuse",s.optical().diffuse());row.add("cd",s.aerodynamic().drag_coefficient());row.add("accommodation",s.aerodynamic().accommodation());
            if(!surface_first) surface_rows<<',';
            surface_first=false;surface_rows<<row.str();
        }
        surface_rows<<']';physical.raw("surfaces",surface_rows.str());j.raw("physical",physical.str());
        j.add("rhs_calls",rhs_calls);j.add("wall_seconds",wall);j.add("attempts",trace.empty()?attempts:trace.size());
        if(!trace.empty()) rejected=static_cast<std::size_t>(std::count_if(trace.begin(),trace.end(),[](const auto& s){return !s.accepted;}));
        j.add("rejected",rejected);Checksum checksum;double work=0,last_power=0,last_time=0;
        double max_energy_error=0,max_momentum_error=0,max_qnorm_error=0,analytic_position_error=0,analytic_attitude_error=0;
        const auto& I=rhs.spacecraft().inertia();const double E0=.5*dot(start.omega,I.multiply(start.omega));const double L0=norm(I.multiply(start.omega));
        std::ostringstream series;series<<std::setprecision(17)<<'[';bool first=true;
        std::ostringstream load_rows;load_rows<<std::setprecision(17)<<'[';
        EventProbe sample_events;
        for(const auto& sample:result.samples) {
            const auto& s=sample.state;checksum.add(s);const auto env=source(sample.t,s);const auto load=rhs.evaluate(sample.t,s,env);
            Json ld;ld.vector("sun_position_m",env.sun_position_m);ld.vector("drag_force_n",load.drag.force_n);ld.vector("albedo_force_n",load.albedo.force_n);
            ld.vector("gravity_gradient_nm",load.gravity_gradient_nm);ld.vector("drag_torque_nm",load.drag.torque_nm);ld.vector("albedo_torque_nm",load.albedo.torque_nm);
            ld.vector("angular_momentum_inertial",body_to_inertial(s.q,I.multiply(s.omega)));
            ld.vector("pointing_axis_inertial",body_to_inertial(s.q,spec.pointing_axis/norm(spec.pointing_axis)));
            const auto wind=atmospheric_relative_velocity(s.r,s.v,env.atmosphere_angular_velocity_rad_s);
            // Solar projected area keeps its historical name and definition; the
            // wind-facing area and the two silhouettes are new diagnostics.
            std::uint64_t solar_mask=0,wind_mask=0;
            double projected=0,solar_silhouette=0,wind_projected=0,wind_silhouette=0;
            if(environment) {
                const auto to_sun=env.sun_position_m-s.r;const double sun_distance=norm(to_sun);
                if(sun_distance>0)
                    surface_incidence(rhs.spacecraft(),inertial_to_body(s.q,to_sun/sun_distance),solar_mask,projected,solar_silhouette);
            }
            const double wind_speed=norm(wind);
            if(wind_speed>0)
                surface_incidence(rhs.spacecraft(),inertial_to_body(s.q,wind/wind_speed),wind_mask,wind_projected,wind_silhouette);
            ld.add("projected_area_m2",projected);
            ld.add("projected_area_wind_m2",wind_projected);
            ld.add("projected_area_sun_silhouette_m2",solar_silhouette);
            ld.add("projected_area_wind_silhouette_m2",wind_silhouette);
            ld.add("solar_incidence_mask",static_cast<double>(solar_mask));
            ld.add("wind_incidence_mask",static_cast<double>(wind_mask));
            ld.add("density_kg_m3",env.density_kg_m3);
            ld.add("altitude_m",norm(s.r)-Earth{}.equatorial_radius_m);
            ld.add("quaternion_norm",std::sqrt(s.q.w*s.q.w+s.q.x*s.q.x+s.q.y*s.q.y+s.q.z*s.q.z));
            ld.add("dynamic_pressure_pa",.5*env.density_kg_m3*dot(wind,wind));
            ld.vector("relative_wind_m_s",wind);ld.vector("angular_acceleration_rad_s2",load.total_angular_acceleration_rad_s2);
            ld.vector("non_gravitational_force_n",load.non_gravitational.force_n);
            // Per-source torque norms and the sign of the net body torque make a
            // torque source switching observable without re-deriving it downstream.
            ld.vector("srp_torque_nm",load.srp.torque_nm);ld.vector("total_torque_nm",load.non_gravitational.torque_nm);
            ld.add("srp_torque_norm_nm",norm(load.srp.torque_nm));
            ld.add("drag_torque_norm_nm",norm(load.drag.torque_nm));
            ld.add("albedo_torque_norm_nm",norm(load.albedo.torque_nm));
            ld.add("gravity_gradient_norm_nm",norm(load.gravity_gradient_nm));
            ld.add("total_torque_norm_nm",norm(load.non_gravitational.torque_nm));
            ld.add("angular_acceleration_norm_rad_s2",norm(load.total_angular_acceleration_rad_s2));
            ld.add("angular_momentum_norm_kg_m2_s",norm(I.multiply(s.omega)));
            ld.add("rotational_energy_j",.5*dot(s.omega,I.multiply(s.omega)));
            std::uint64_t net_torque_sign=0;
            const double torque_component[3]={load.non_gravitational.torque_nm.x(),load.non_gravitational.torque_nm.y(),load.non_gravitational.torque_nm.z()};
            for(int axis=0;axis<3;++axis) if(torque_component[axis]<0) net_torque_sign|=std::uint64_t(1)<<axis;
            ld.add("net_torque_sign_mask",static_cast<double>(net_torque_sign));
            sample_events.observe(norm(s.r)-Earth{}.equatorial_radius_m,env.density_kg_m3,solar_mask,wind_mask,env.illumination);
            if(!first) load_rows<<',';
            load_rows<<ld.str();
            const double power=dot(load.srp.force_n,s.v);if(sample.t.seconds>0) work+=(power+last_power)*.5*(sample.t.seconds-last_time);
            last_power=power;last_time=sample.t.seconds;
            max_energy_error=std::max(max_energy_error,std::abs(.5*dot(s.omega,I.multiply(s.omega))-E0));
            max_momentum_error=std::max(max_momentum_error,std::abs(norm(I.multiply(s.omega))-L0));
            max_qnorm_error=std::max(max_qnorm_error,std::abs(std::sqrt(s.q.w*s.q.w+s.q.x*s.q.x+s.q.y*s.q.y+s.q.z*s.q.z)-1));
            if(c.scenario=="two_body") {const double r=norm(start.r),n=std::sqrt(Earth{}.mu_m3_s2/(r*r*r));analytic_position_error=std::max(analytic_position_error,norm(s.r-(start.r*std::cos(n*sample.t.seconds)+start.v*(std::sin(n*sample.t.seconds)/n))));}
            if(c.scenario=="principal") analytic_attitude_error=std::max(analytic_attitude_error,angle(s.q,{std::cos(.001*sample.t.seconds),0,0,std::sin(.001*sample.t.seconds)}));
            if(!first) series<<',';
            first=false;series<<'['<<sample.t.seconds;
            const auto packed=detail::pack(s);for(double v:packed) series<<','<<v;
            series<<','<<env.illumination;
            for(double v:{load.srp.force_n.x(),load.srp.force_n.y(),load.srp.force_n.z(),load.srp.torque_nm.x(),load.srp.torque_nm.y(),load.srp.torque_nm.z(),work}) series<<','<<v;
            series<<']';
        }
        series<<']';j.raw("samples",series.str());j.add("checksum",checksum.str());j.add("srp_work_j",work);
        load_rows<<']';j.raw("loads",load_rows.str());
        j.add("max_rotational_energy_error_j",max_energy_error);j.add("max_body_angular_momentum_norm_error",max_momentum_error);j.add("max_qnorm_error",max_qnorm_error);
        j.add("analytic_position_max_error_m",analytic_position_error);j.add("analytic_attitude_max_error_rad",analytic_attitude_error);
        if(!trace.empty()) {
            std::size_t clipped_to_output=0,clipped_to_max_step=0;
            EventProbe trace_events;
            std::ostringstream ts;ts<<std::setprecision(17)<<'[';bool f=true;
            for(const auto& step:trace) {
                if(!f) ts<<',';
                f=false;
                // First five fields keep the historical positional layout; the rest
                // are appended diagnostics, never reordering or replacing them.
                ts<<'['<<step.t<<','<<step.h<<','<<step.error<<','<<(step.accepted?1:0)<<','<<step.illumination
                  <<','<<step.proposed_h<<','<<step.altitude_m<<','<<step.density_kg_m3
                  <<','<<static_cast<double>(step.solar_mask)<<','<<static_cast<double>(step.wind_mask)
                  <<','<<(step.clipped_to_output?1:0)<<','<<(step.clipped_to_max_step?1:0)
                  <<','<<step.solar_projected_area_m2<<','<<step.wind_projected_area_m2
                  <<','<<step.solar_silhouette_m2<<','<<step.wind_silhouette_m2<<']';
                if(step.clipped_to_output) ++clipped_to_output;
                if(step.clipped_to_max_step) ++clipped_to_max_step;
                // Only accepted steps advance the accepted trace; a rejected
                // attempt re-evaluates the same state and is not a new state.
                if(step.accepted)
                    trace_events.observe(step.altitude_m,step.density_kg_m3,step.solar_mask,step.wind_mask,step.illumination);
            }
            ts<<']';j.raw("steps",ts.str());
            j.raw("steps_fields",R"(["time_s","step_s","scaled_error","accepted","illumination",)"
                               R"("proposed_step_s","altitude_m","density_kg_m3",)"
                               R"("solar_incidence_mask","wind_incidence_mask",)"
                               R"("clipped_to_output","clipped_to_max_step",)"
                               R"("solar_projected_area_m2","wind_projected_area_m2",)"
                               R"("solar_silhouette_m2","wind_silhouette_m2"])");
            j.raw("trace_events",trace_events.json("accepted_trace_steps",static_cast<long long>(rejected),
                static_cast<long long>(clipped_to_output),static_cast<long long>(clipped_to_max_step)).str());
        } else {
            j.raw("trace_events","null");
            j.add("trace_events_absent_reason","step-level diagnostics require --method trace");
        }
        j.raw("sample_events",sample_events.json("reported_output_samples",c.method=="trace"?static_cast<long long>(rejected):-1,-1,-1).str());
        std::cout<<j.str()<<'\n';return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
