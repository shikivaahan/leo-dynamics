#include "leo/analysis/loads.hpp"
#include "leo/kernel/aerodynamics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace leo::analysis {
namespace {
constexpr double missing=std::numeric_limits<double>::quiet_NaN();
void finite(double value,const char* what) {if(!std::isfinite(value)) throw std::invalid_argument(what);}
void check_environment(const Trajectory& t,std::span<const EnvironmentalSample> samples) {
    if(samples.size()!=t.samples.size()) throw std::invalid_argument("environment diagnostics must match every trajectory sample");
    for(std::size_t i=0;i<samples.size();++i)
        if(samples[i].time_s!=t.samples[i].t.seconds) throw std::invalid_argument("environment diagnostic time mismatch");
}
Quaternion multiply(Quaternion a,Quaternion b) {
    return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
double scalar_integral(double p0,double p1,double dt,double f) {return dt*(p0*f+.5*(p1-p0)*f*f);}
RtnVector vector_integral(RtnVector p0,RtnVector p1,double dt,double f) {return (p0*f+(p1-p0)*(.5*f*f))*dt;}
struct BoundaryWork {double work,specific,energy;RtnVector rtn;};
BoundaryWork boundary(std::span<const WorkSample> samples,double time) {
    if(!std::isfinite(time)||time<samples.front().time_s||time>samples.back().time_s)
        throw std::invalid_argument("orbit work interval outside sampled work history");
    const auto it=std::lower_bound(samples.begin(),samples.end(),time,[](const auto& s,double t){return s.time_s<t;});
    if(it==samples.begin()||it->time_s==time)
        return {it->cumulative_work_j,it->specific_work_j_kg,it->orbital_energy_change_j,it->cumulative_work_rtn_j};
    const auto& a=*(it-1);const auto& b=*it;
    const double dt=b.time_s-a.time_s,f=(time-a.time_s)/dt;
    const double work=a.cumulative_work_j+scalar_integral(a.power_w,b.power_w,dt,f);
    const double specific=work/a.mass_kg;
    return {work,specific,a.orbital_energy_change_j+(b.orbital_energy_change_j-a.orbital_energy_change_j)*f,
        a.cumulative_work_rtn_j+vector_integral(a.power_rtn_w,b.power_rtn_w,dt,f)};
}
}
std::vector<EnvironmentalSample> environmental_history(const Trajectory& t,const EarthDynamics& dynamics,std::span<const EnvironmentSnapshot> snapshots) {
    validate_trajectory(t);
    if(snapshots.size()!=t.samples.size()) throw std::invalid_argument("snapshots must match every trajectory sample");
    std::vector<EnvironmentalSample> result;result.reserve(snapshots.size());
    for(std::size_t i=0;i<snapshots.size();++i) {
        const auto& env=snapshots[i];const auto& s=t.samples[i];
        if(!std::isfinite(env.epoch.et_seconds)||env.epoch.et_seconds!=t.epoch.et_seconds+s.t.seconds)
            throw std::invalid_argument("snapshot epoch must be finite and equal trajectory epoch plus elapsed time");
        if(!std::isfinite(env.illumination)||env.illumination<0||env.illumination>1||!std::isfinite(env.density_kg_m3)||env.density_kg_m3<0||
           !env.sun_position_m.as_eigen().allFinite()||!env.atmosphere_angular_velocity_rad_s.as_eigen().allFinite())
            throw std::invalid_argument("invalid analysis environment snapshot");
        result.push_back({s.t.seconds,env,dynamics.evaluate(s.t,s.state,env)});
    }
    return result;
}
RtnBasis rtn_basis(const State& state) {
    const double radius=norm(state.r);const auto h=cross(state.r,state.v);const double hn=norm(h);
    if(!state.r.as_eigen().allFinite()||!state.v.as_eigen().allFinite()||!std::isfinite(radius)||radius<=0||!std::isfinite(hn)||hn<=0)
        throw std::invalid_argument("RTN basis needs nonzero finite radius and angular momentum");
    const auto radial=state.r/radius,normal=h/hn;
    return {radial,cross(normal,radial),normal};
}
RtnVector rtn_components(const RtnBasis& basis,InertialVector vector) {
    if(!vector.as_eigen().allFinite()) throw std::invalid_argument("nonfinite RTN input");
    return {dot(vector,basis.radial),dot(vector,basis.transverse),dot(vector,basis.normal)};
}
std::vector<AttitudeSample> attitude_history(const Trajectory& t,const Spacecraft& craft,std::span<const EnvironmentalSample> environment) {
    validate_trajectory(t);if(!environment.empty()) check_environment(t,environment);
    const auto reference=normalize_quaternion(t.samples.front().state.q);
    const Quaternion inverse{reference.w,-reference.x,-reference.y,-reference.z};
    std::vector<AttitudeSample> result;result.reserve(t.samples.size());
    for(std::size_t i=0;i<t.samples.size();++i) {
        const auto& sample=t.samples[i];const auto& s=sample.state;
        const auto q=normalize_quaternion(s.q);const auto momentum=craft.inertia().multiply(s.omega);
        const auto torque=environment.empty()?BodyVector{}:environment[i].contributions.non_gravitational.torque_nm;
        const auto relative=normalize_quaternion(multiply(q,inverse));
        const double sine=std::clamp(2*(relative.w*relative.y-relative.z*relative.x),-1.,1.);
        const double pitch=std::asin(sine);const bool singular=1-std::abs(sine)<1e-12;
        double yaw=std::atan2(2*(relative.w*relative.z+relative.x*relative.y),1-2*(relative.y*relative.y+relative.z*relative.z));
        double roll=std::atan2(2*(relative.w*relative.x+relative.y*relative.z),1-2*(relative.x*relative.x+relative.y*relative.y));
        // At gimbal lock choose roll=0; yaw retains the observable combination.
        if(singular) {yaw=std::remainder(2*std::atan2(relative.z,relative.w),2*std::numbers::pi);roll=0;}
        AttitudeSample row{sample.t.seconds,q,s.omega,norm(s.omega),.5*dot(s.omega,momentum),norm(body_to_inertial(q,momentum)),
            torque,norm(torque),{yaw,pitch,roll},singular,{}};
        finite(row.rotational_energy_j,"nonfinite rotational energy");finite(row.inertial_momentum_kg_m2_s,"nonfinite angular momentum");
        const auto basis=rtn_basis(s);
        InertialVector toward_sun{},flow{};bool solar=false,aero=false;
        double illumination=0;
        if(!environment.empty()) {
            const auto& env=environment[i].snapshot;
            if(norm(env.sun_position_m)>0) {
                const auto direction=env.sun_position_m-s.r;const double length=norm(direction);
                if(!std::isfinite(length)||length<=0) throw std::invalid_argument("surface incidence needs distinct Sun position");
                toward_sun=direction/length;solar=true;illumination=env.illumination;
            }
            const auto velocity=atmospheric_relative_velocity(s.r,s.v,env.atmosphere_angular_velocity_rad_s);
            const double speed=norm(velocity);
            if(speed>0) flow=velocity/speed;
            aero=true;
        }
        row.surfaces.reserve(craft.surfaces().size());
        for(std::size_t j=0;j<craft.surfaces().size();++j) {
            const auto& surface=craft.surfaces()[j];const auto normal=body_to_inertial(q,surface.normal_body());
            const double cosine=solar?std::clamp(dot(normal,toward_sun),-1.,1.):0;
            row.surfaces.push_back({j,normal,solar?std::acos(cosine):missing,
                solar?surface.area_m2()*std::max(0.,cosine)*illumination:missing,
                aero?surface.area_m2()*std::max(0.,dot(normal,flow)):missing,
                std::acos(std::clamp(dot(normal,basis.normal),-1.,1.))});
        }
        result.push_back(std::move(row));
    }
    return result;
}
std::vector<WorkSample> srp_work(const Trajectory& t,const Earth& earth,double mass,std::span<const EnvironmentalSample> environment) {
    validate_trajectory(t);check_environment(t,environment);(void)EarthGravity(earth,false);
    if(!std::isfinite(mass)||mass<=0) throw std::invalid_argument("work analysis needs positive finite mass");
    const auto energy=[&](const State& s){return .5*dot(s.v,s.v)-earth.mu_m3_s2/norm(s.r);};
    const double initial_energy=energy(t.samples.front().state);
    std::vector<WorkSample> result;result.reserve(t.samples.size());
    std::vector<double> sun_speed;sun_speed.reserve(t.samples.size());
    for(std::size_t i=0;i<t.samples.size();++i) {
        const auto& sample=t.samples[i];const auto& env=environment[i];const auto& s=sample.state;
        const auto force=env.contributions.srp.force_n;
        const auto basis=rtn_basis(s);const auto force_rtn=rtn_components(basis,force),velocity_rtn=rtn_components(basis,s.v);
        const RtnVector powers{force_rtn.x()*velocity_rtn.x(),force_rtn.y()*velocity_rtn.y(),force_rtn.z()*velocity_rtn.z()};
        const double power=dot(force,s.v),delta=energy(s)-initial_energy;
        finite(power,"nonfinite SRP power");finite(delta,"nonfinite orbital energy change");
        WorkSample row{sample.t.seconds,force,force/mass,force_rtn,powers,{},power,0,0,mass*delta,delta,mass*delta,0,0,mass};
        double speed=0;
        if(norm(env.snapshot.sun_position_m)>0) {
            const auto direction=env.snapshot.sun_position_m-s.r;const double distance=norm(direction);
            if(!std::isfinite(distance)||distance<=0) throw std::invalid_argument("Sun work classification needs distinct Sun position");
            speed=dot(s.v,direction/distance); // positive means toward Sun: inbound.
        }
        sun_speed.push_back(speed);
        if(i>0) {
            const auto& a=result.back();const double dt=row.time_s-a.time_s;
            row.cumulative_work_j=a.cumulative_work_j+scalar_integral(a.power_w,power,dt,1);
            row.cumulative_work_rtn_j=a.cumulative_work_rtn_j+vector_integral(a.power_rtn_w,powers,dt,1);
            row.sun_inbound_transverse_work_j=a.sun_inbound_transverse_work_j;
            row.sun_outbound_transverse_work_j=a.sun_outbound_transverse_work_j;
            const double left=sun_speed[i-1],right=speed;
            const auto assign=[&](double work,double direction) {
                if(direction>0) row.sun_inbound_transverse_work_j+=work;
                else if(direction<0) row.sun_outbound_transverse_work_j+=work;
            };
            if(left*right<0) {
                const double f=-left/(right-left),first=scalar_integral(a.power_rtn_w.y(),powers.y(),dt,f);
                assign(first,left);assign(scalar_integral(a.power_rtn_w.y(),powers.y(),dt,1)-first,right);
            } else assign(scalar_integral(a.power_rtn_w.y(),powers.y(),dt,1),left!=0?left:right);
        }
        row.specific_work_j_kg=row.cumulative_work_j/mass;
        row.energy_minus_srp_work_j=row.orbital_energy_change_j-row.cumulative_work_j;
        finite(row.cumulative_work_j,"nonfinite cumulative SRP work");
        result.push_back(row);
    }
    return result;
}
std::vector<OrbitWork> work_per_orbit(std::span<const OrbitSummary> orbits,std::span<const WorkSample> work) {
    if(work.empty()) {if(orbits.empty()) return {};throw std::invalid_argument("orbit work needs sampled work history");}
    double previous=-1;
    for(const auto& row:work) {
        if(!std::isfinite(row.time_s)||row.time_s<0||row.time_s<=previous) throw std::invalid_argument("work times must be finite and increasing");
        finite(row.power_w,"nonfinite sampled power");finite(row.cumulative_work_j,"nonfinite sampled work");
        if(!std::isfinite(row.mass_kg)||row.mass_kg<=0||row.mass_kg!=work.front().mass_kg)
            throw std::invalid_argument("work history mass must be positive and constant");
        previous=row.time_s;
    }
    std::vector<OrbitWork> result;result.reserve(orbits.size());
    for(const auto& orbit:orbits) {
        if(orbit.end_time_s<=orbit.start_time_s) throw std::invalid_argument("invalid orbit work interval");
        const auto a=boundary(work,orbit.start_time_s),b=boundary(work,orbit.end_time_s);
        const double total=b.work-a.work,energy=b.energy-a.energy;
        result.push_back({orbit.index,orbit.start_time_s,orbit.end_time_s,orbit.complete,total,b.specific-a.specific,b.rtn-a.rtn,energy,energy-total});
    }
    return result;
}
}
