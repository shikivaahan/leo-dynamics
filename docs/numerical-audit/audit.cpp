#include "bench_support.hpp"
#include "leo/integration/detail/packed_state.hpp"
#include <boost/numeric/odeint/stepper/runge_kutta_fehlberg78.hpp>
#include <boost/numeric/odeint/stepper/controlled_runge_kutta.hpp>
#include "SpiceUsr.h"
#include <algorithm>
#include <limits>
using namespace bench;
using Packed=detail::PackedState;
namespace {
struct Config {
    std::string spk,lsk,scenario="full",method="dopri";
    double duration=21600,rtol=1e-10,atol_scale=1,max_step=15,perturb=0,cadence=30,attitude_seed=0;
    int ulps=0;
};
Config parse_args(int argc,char** argv) {
    Config o;
    for(int i=1;i<argc;++i) {
        const std::string k=argv[i];if(i+1==argc) throw std::invalid_argument("missing argument");
        const std::string v=argv[++i];
        if(k=="--spk") o.spk=v;else if(k=="--lsk") o.lsk=v;
        else if(k=="--scenario") o.scenario=v;else if(k=="--method") o.method=v;
        else if(k=="--duration") o.duration=std::stod(v);else if(k=="--rtol") o.rtol=std::stod(v);
        else if(k=="--atol-scale") o.atol_scale=std::stod(v);else if(k=="--max-step") o.max_step=std::stod(v);
        else if(k=="--perturb-x") o.perturb=std::stod(v);else if(k=="--ulp-x") o.ulps=std::stoi(v);
        else if(k=="--attitude-seed") o.attitude_seed=std::stod(v);
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
EarthDynamics configured_dynamics(const Config& o) {
    auto d=dynamics(true);auto m=d.models();
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
    return EarthDynamics(EarthGravity(Earth{},o.scenario!="two_body"&&o.scenario!="attitude"&&o.scenario!="principal"),d.spacecraft(),m);
}
struct Step { double t,h,error;bool accepted;double illumination; };
// Diagnostic-only mirror of the production Dopri loop. Its output is checked
// bit-for-bit against production before interpreting its trace. It is NOT an
// independent accuracy oracle; RKF78 and fixed RK4 below supply other methods.
template<class Rhs> Trajectory trace_dopri(const Rhs& rhs,const State& initial,const std::vector<double>& times,
                                         const IntegrationOptions& o,const EarthEnvironment& env,std::vector<Step>& log) {
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
            log.push_back({t,h,e,accepted,env({t},detail::unpack(y)).illumination});
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
template<class Rhs> Trajectory fixed_rk4(const Rhs& rhs,const State& initial,const std::vector<double>& times,double step) {
    Packed y=detail::pack(validated_state(initial));Trajectory out{Epoch{0},{{ElapsedTime{0},validated_state(initial)}}};
    double t=0;
    auto evaluate=[&](const Packed& p,double time){return detail::pack(rhs({time},detail::unpack(p)));};
    for(std::size_t i=1;i<times.size();++i) {
        while(t<times[i]) {
            const double h=std::min(step,times[i]-t);if(t+h==t) throw std::runtime_error("RK4 cannot advance");
            const auto k1=evaluate(y,t);Packed trial{};
            for(std::size_t k=0;k<13;++k) trial[k]=y[k]+h*.5*k1[k];
            const auto k2=evaluate(trial,t+h*.5);
            for(std::size_t k=0;k<13;++k) trial[k]=y[k]+h*.5*k2[k];
            const auto k3=evaluate(trial,t+h*.5);
            for(std::size_t k=0;k<13;++k) trial[k]=y[k]+h*k3[k];
            const auto k4=evaluate(trial,t+h);
            for(std::size_t k=0;k<13;++k) y[k]+=h*(k1[k]+2*k2[k]+2*k3[k]+k4[k])/6;
            const auto q=normalize_quaternion({y[6],y[7],y[8],y[9]});y[6]=q.w;y[7]=q.x;y[8]=q.y;y[9]=q.z;
            t=h==times[i]-t?times[i]:t+h;
        }
        out.samples.push_back({ElapsedTime{times[i]},detail::unpack(y)});
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
        const auto rhs=configured_dynamics(c);State start=initial();
        if(c.scenario=="principal") {start.q={1,0,0,0};start.omega={0,0,.002};}
        if(c.attitude_seed!=0) {
            const double a=.5*c.attitude_seed;const auto q=start.q;
            start.q=normalize_quaternion({std::cos(a)*q.w-std::sin(a)*q.z,
                std::cos(a)*q.x-std::sin(a)*q.y,
                std::cos(a)*q.y+std::sin(a)*q.x,
                std::cos(a)*q.z+std::sin(a)*q.w});
        }
        IntegrationOptions options=tolerances();options.rtol=c.rtol;
        options.position_atol_m*=c.atol_scale;options.velocity_atol_m_s*=c.atol_scale;
        options.quaternion_atol*=c.atol_scale;options.omega_atol_rad_s*=c.atol_scale;
        options.max_step_s=c.max_step;options.initial_step_s=std::min(1.,c.max_step);options.max_attempts=10000000;
        const bool environment=rhs.needs_environment();
        EarthEnvironment source(Earth{},Epoch{0},environment?&sun:nullptr,environment,c.scenario=="full_no_eclipse"?EclipseModel::none:EclipseModel::conical);
        std::vector<double> labels{0};for(double t=c.cadence;t<c.duration;t+=c.cadence) labels.push_back(t);labels.push_back(c.duration);
        std::size_t rhs_calls=0,attempts=0,rejected=0;
        auto system=[&](ElapsedTime t,const State& s){++rhs_calls;return rhs(t,s,source(t,s));};
        std::vector<Step> trace;Trajectory result;
        const auto timer=Clock::now();
        if(c.method=="dopri") result=propagate(system,start,{0},labels,options);
        else if(c.method=="trace") result=trace_dopri(system,start,labels,options,source,trace);
        else if(c.method=="rkf78") result=reference_rkf78(system,start,labels,options,attempts,rejected);
        else if(c.method=="rk4") result=fixed_rk4(system,start,labels,c.max_step);
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
        j.add("initial_attitude_seed_rad",c.attitude_seed);
        j.add("rhs_calls",rhs_calls);j.add("wall_seconds",wall);j.add("attempts",trace.empty()?attempts:trace.size());
        if(!trace.empty()) rejected=static_cast<std::size_t>(std::count_if(trace.begin(),trace.end(),[](const auto& s){return !s.accepted;}));
        j.add("rejected",rejected);Checksum checksum;double work=0,last_power=0,last_time=0;
        double max_energy_error=0,max_momentum_error=0,max_qnorm_error=0,analytic_position_error=0,analytic_attitude_error=0;
        const auto& I=rhs.spacecraft().inertia();const double E0=.5*dot(start.omega,I.multiply(start.omega));const double L0=norm(I.multiply(start.omega));
        std::ostringstream series;series<<std::setprecision(17)<<'[';bool first=true;
        for(const auto& sample:result.samples) {
            const auto& s=sample.state;checksum.add(s);const auto env=source(sample.t,s);const auto load=rhs.evaluate(sample.t,s,env);
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
        j.add("max_rotational_energy_error_j",max_energy_error);j.add("max_body_angular_momentum_norm_error",max_momentum_error);j.add("max_qnorm_error",max_qnorm_error);
        j.add("analytic_position_max_error_m",analytic_position_error);j.add("analytic_attitude_max_error_rad",analytic_attitude_error);
        if(!trace.empty()) {
            std::ostringstream ts;ts<<std::setprecision(17)<<'[';bool f=true;
            for(const auto& step:trace) {if(!f) ts<<',';f=false;ts<<'['<<step.t<<','<<step.h<<','<<step.error<<','<<(step.accepted?1:0)<<','<<step.illumination<<']';}
            ts<<']';j.raw("steps",ts.str());
        }
        std::cout<<j.str()<<'\n';return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
