#include "leo/analysis/orbital.hpp"
#include "leo/kernel/gravity.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace leo::analysis {
namespace {
constexpr double tau=2*std::numbers::pi;
void finite(double value) { if(!std::isfinite(value)) throw std::invalid_argument("nonfinite orbital diagnostic"); }
bool constant_radius(const Trajectory& t,double tolerance) {
    double lo=norm(t.samples.front().state.r),hi=lo;
    for(const auto& s:t.samples) {const auto r=norm(s.state.r);lo=std::min(lo,r);hi=std::max(hi,r);}
    return hi-lo<=tolerance*hi;
}
double blend(double a,double b,double f) {return a+(b-a)*f;}
ClassicalElements interpolate(const OrbitalSample& a,const OrbitalSample& b,double f) {
    return {blend(a.elements.a_m,b.elements.a_m,f),blend(a.elements.e,b.elements.e,f),
        blend(a.elements.i_rad,b.elements.i_rad,f),blend(a.raan_unwrapped_rad,b.raan_unwrapped_rad,f),
        blend(a.arg_periapsis_unwrapped_rad,b.arg_periapsis_unwrapped_rad,f),blend(a.true_anomaly_unwrapped_rad,b.true_anomaly_unwrapped_rad,f)};
}
ClassicalElements at(const std::vector<OrbitalSample>& h,double time) {
    const auto it=std::lower_bound(h.begin(),h.end(),time,[](const auto& s,double t){return s.time_s<t;});
    if(it==h.begin()) return interpolate(*it,*it,0);
    if(it==h.end()) return interpolate(h.back(),h.back(),0);
    const auto& left=*(it-1);
    return interpolate(left,*it,(time-left.time_s)/(it->time_s-left.time_s));
}
}
void validate_trajectory(const Trajectory& t) {
    if(!std::isfinite(t.epoch.et_seconds)||t.samples.empty()) throw std::invalid_argument("analysis needs a finite epoch and nonempty trajectory");
    double previous=-1;
    for(const auto& sample:t.samples) {
        if(!std::isfinite(sample.t.seconds)||sample.t.seconds<0||sample.t.seconds<=previous)
            throw std::invalid_argument("analysis times must be nonnegative, finite and strictly increasing");
        (void)validated_state(sample.state);
        if(!std::isfinite(norm(sample.state.r))||norm(sample.state.r)<=0)
            throw std::invalid_argument("analysis needs nonzero finite orbital radius");
        previous=sample.t.seconds;
    }
}
std::vector<double> unwrap_angles(std::span<const double> angles) {
    std::vector<double> result;result.reserve(angles.size());
    for(std::size_t i=0;i<angles.size();++i) {
        finite(angles[i]);
        const double value=i==0?angles[i]:result.back()+std::remainder(angles[i]-angles[i-1],tau);
        finite(value);result.push_back(value);
    }
    return result;
}
std::vector<OrbitalSample> orbital_history(const Trajectory& t,const Earth& earth) {
    validate_trajectory(t);(void)EarthGravity(earth,false);
    std::vector<OrbitalSample> result;result.reserve(t.samples.size());
    for(const auto& s:t.samples) {
        const auto& state=s.state;
        const auto e=cartesian_to_coe({state.r,state.v},earth.mu_m3_s2);
        const auto radius=norm(state.r),peri=e.a_m*(1-e.e),apo=e.a_m*(1+e.e);
        const double kinetic=.5*dot(state.v,state.v),potential=-earth.mu_m3_s2/radius;
        OrbitalSample row{s.t.seconds,radius,radius-earth.equatorial_radius_m,e,peri,apo,
            peri-earth.equatorial_radius_m,apo-earth.equatorial_radius_m,
            kinetic+potential,cross(state.r,state.v),e.raan_rad,e.arg_periapsis_rad,e.true_anomaly_rad,kinetic,potential};
        if(!result.empty()) {
            const auto& prev=result.back();
            row.raan_unwrapped_rad=prev.raan_unwrapped_rad+std::remainder(e.raan_rad-prev.elements.raan_rad,tau);
            row.arg_periapsis_unwrapped_rad=prev.arg_periapsis_unwrapped_rad+std::remainder(e.arg_periapsis_rad-prev.elements.arg_periapsis_rad,tau);
            row.true_anomaly_unwrapped_rad=prev.true_anomaly_unwrapped_rad+std::remainder(e.true_anomaly_rad-prev.elements.true_anomaly_rad,tau);
        }
        finite(row.specific_energy_j_kg);finite(peri);finite(apo);
        if(!row.specific_angular_momentum_m2_s.as_eigen().allFinite()) throw std::invalid_argument("nonfinite angular momentum");
        result.push_back(row);
    }
    return result;
}
std::vector<ApsisEvent> detect_apsides(const Trajectory& t,double tolerance) {
    validate_trajectory(t);
    if(!std::isfinite(tolerance)||tolerance<0) throw std::invalid_argument("invalid circular event tolerance");
    std::vector<ApsisEvent> events;
    if(t.samples.size()<2||constant_radius(t,tolerance)) return events;
    std::vector<double> radial;radial.reserve(t.samples.size());
    double speed=0;
    for(const auto& s:t.samples) {radial.push_back(dot(s.state.r/norm(s.state.r),s.state.v));speed=std::max(speed,norm(s.state.v));}
    const double zero=1e-12*speed;
    for(auto& value:radial) if(std::abs(value)<=zero) value=0;
    const auto add=[&](std::size_t left,double f,ApsisKind kind) {
        const auto& a=t.samples[left];const auto& b=t.samples[left+1];
        const double time=blend(a.t.seconds,b.t.seconds,f);
        if(!events.empty()&&time==events.back().time_s) return;
        events.push_back({kind,time,blend(norm(a.state.r),norm(b.state.r),f),left,f});
    };
    for(std::size_t i=0;i+1<radial.size();++i) {
        const double a=radial[i],b=radial[i+1];
        if(a<0&&b>0) add(i,-a/(b-a),ApsisKind::periapsis);
        else if(a>0&&b<0) add(i,-a/(b-a),ApsisKind::apoapsis);
        else if(a==0&&b!=0&&(i==0||(radial[i-1]!=0&&radial[i-1]*b<0)))
            add(i,0,b>0?ApsisKind::periapsis:ApsisKind::apoapsis);
        else if(i+2==radial.size()&&b==0&&a!=0)
            add(i,1,a<0?ApsisKind::periapsis:ApsisKind::apoapsis);
    }
    return events;
}
std::vector<OrbitSummary> per_orbit(const Trajectory& t,const Earth& earth) {
    const auto h=orbital_history(t,earth);
    if(t.samples.size()<2) return {};
    // Reject known gross undersampling; this cannot detect every phase alias.
    for(std::size_t i=1;i<h.size();++i) {
        const double a=std::min(h[i-1].elements.a_m,h[i].elements.a_m);
        const double period=tau*std::sqrt(a*a*a/earth.mu_m3_s2);
        if(h[i].time_s-h[i-1].time_s>=period/2)
            throw std::invalid_argument("per-orbit analysis needs samples less than half an orbital period apart");
    }
    std::vector<double> boundaries;
    bool circular=true;
    for(const auto& row:h) if(row.elements.e>1e-8) circular=false;
    if(circular) {
        const auto r0=t.samples.front().state.r/norm(t.samples.front().state.r);
        const auto n=cross(t.samples.front().state.r,t.samples.front().state.v)/norm(cross(t.samples.front().state.r,t.samples.front().state.v));
        const auto tangent=cross(n,r0);
        std::vector<double> phase;phase.reserve(h.size());
        for(const auto& sample:t.samples) phase.push_back(std::atan2(dot(sample.state.r,tangent),dot(sample.state.r,r0)));
        phase=unwrap_angles(phase);
        const double offset=h.front().elements.true_anomaly_rad;
        for(auto& value:phase) value+=offset;
        if(std::abs(std::remainder(phase.front(),tau))<1e-10) boundaries.push_back(h.front().time_s);
        for(std::size_t i=1;i<phase.size();++i) {
            if(phase[i]<=phase[i-1]) throw std::invalid_argument("circular phase must advance with resolved sampling");
            const double next=tau*(std::floor((phase[i-1]+1e-12)/tau)+1);
            if(next<=phase[i]+1e-12) boundaries.push_back(blend(h[i-1].time_s,h[i].time_s,std::clamp((next-phase[i-1])/(phase[i]-phase[i-1]),0.,1.)));
        }
    } else {
        for(const auto& event:detect_apsides(t)) if(event.kind==ApsisKind::periapsis) boundaries.push_back(event.time_s);
    }
    const double start=h.front().time_s,end=h.back().time_s;
    std::vector<std::pair<double,bool>> cuts;
    cuts.emplace_back(start,!boundaries.empty()&&boundaries.front()==start);
    for(double time:boundaries) if(time>start&&time<end) cuts.emplace_back(time,true);
    cuts.emplace_back(end,!boundaries.empty()&&boundaries.back()==end);
    std::vector<OrbitSummary> result;result.reserve(cuts.size()-1);
    for(std::size_t i=1;i<cuts.size();++i) {
        const auto a=at(h,cuts[i-1].first),b=at(h,cuts[i].first);
        const ClassicalElements delta{b.a_m-a.a_m,b.e-a.e,b.i_rad-a.i_rad,b.raan_rad-a.raan_rad,
            b.arg_periapsis_rad-a.arg_periapsis_rad,b.true_anomaly_rad-a.true_anomaly_rad};
        const bool complete=cuts[i-1].second&&cuts[i].second;
        result.push_back({result.size(),cuts[i-1].first,cuts[i].first,complete,
            complete?cuts[i].first-cuts[i-1].first:0,a,b,delta,a.raan_rad,b.raan_rad});
    }
    return result;
}
}
