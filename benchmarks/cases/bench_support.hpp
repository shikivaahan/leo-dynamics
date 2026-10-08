#pragma once
#include "leo/environment/source.hpp"
#include "leo/integration/propagate.hpp"
#ifndef LEO_BENCH_NO_SPICE
#include "leo/environment/spice.hpp"
#endif
#ifdef LEO_BENCH_CACHE
#include "leo/environment/interpolated.hpp"
#endif
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace bench {
using namespace leo;
using Clock=std::chrono::steady_clock;
inline double elapsed(Clock::time_point t) { return std::chrono::duration<double>(Clock::now()-t).count(); }
struct Json {
    std::ostringstream out;
    bool first=true;
    Json() { out<<std::setprecision(17)<<'{'; }
    void key(const std::string& k) { if(!first) out<<','; first=false; out<<'"'<<k<<"\":"; }
    void raw(const std::string& k,const std::string& v) { key(k); out<<v; }
    void add(const std::string& k,double v) { if(!std::isfinite(v)) throw std::runtime_error("nonfinite metric: "+k); key(k); out<<v; }
    void add(const std::string& k,std::size_t v) { key(k); out<<v; }
    void add(const std::string& k,bool v) { key(k); out<<(v?"true":"false"); }
    void add(const std::string& k,const std::string& v) { key(k); out<<'"'<<v<<'"'; }
    void add(const std::string& k,const char* v) { add(k,std::string(v)); }
    template<class F> void vector(const std::string& k,Vector3<F> v) { key(k); out<<'['<<v.x()<<','<<v.y()<<','<<v.z()<<']'; }
    std::string str() const { return out.str()+'}'; }
};
struct Checksum {
    std::uint64_t value=14695981039346656037ull;
    void add(double v) { std::uint64_t bits; std::memcpy(&bits,&v,sizeof bits); for(int i=0;i<8;++i) {value^=(bits>>(8*i))&255;value*=1099511628211ull;} }
    void add(const State& s) { for(double v:{s.r.x(),s.r.y(),s.r.z(),s.v.x(),s.v.y(),s.v.z(),s.q.w,s.q.x,s.q.y,s.q.z,s.omega.x(),s.omega.y(),s.omega.z()}) add(v); }
    void add(const EphemerisState& s) { for(double v:{s.position_m.x(),s.position_m.y(),s.position_m.z(),s.velocity_m_s.x(),s.velocity_m_s.y(),s.velocity_m_s.z()}) add(v); }
    std::string str() const {std::ostringstream o;o<<std::hex<<value;return o.str();}
};
struct Options {
    std::string id,mode,spk,lsk;
    double interval=300,duration=21600;
    bool profile=false;
};
inline Options parse(int argc,char** argv) {
    Options o;
    for(int i=1;i<argc;++i) {
        std::string a=argv[i];
        if(a=="--profile") {o.profile=true;continue;}
        if(i+1==argc) throw std::invalid_argument("missing option value");
        std::string v=argv[++i];
        if(a=="--case") o.id=v; else if(a=="--mode") o.mode=v;
        else if(a=="--spk") o.spk=v; else if(a=="--lsk") o.lsk=v;
        else if(a=="--interval"||a=="--duration") {
            std::size_t n=0; double x=std::stod(v,&n);
            if(n!=v.size()||!std::isfinite(x)||x<=0) throw std::invalid_argument("invalid time option");
            (a=="--interval"?o.interval:o.duration)=x;
        } else throw std::invalid_argument("unknown option: "+a);
    }
    if(o.mode!="direct"&&o.mode!="cached"&&o.mode!="deterministic") throw std::invalid_argument("invalid mode");
    if(o.mode!="deterministic"&&o.spk.empty()) throw std::invalid_argument("real SPICE mode requires --spk");
    return o;
}
struct AnalyticSun final:EphemerisProvider {
    EphemerisState geometric_state(int target,Epoch epoch) const override {
        if(target!=10||!std::isfinite(epoch.et_seconds)) throw std::invalid_argument("invalid analytic query");
        const double w=2*std::numbers::pi/(365.25*86400),a=w*epoch.et_seconds,c=std::cos(a),s=std::sin(a);
        return {{astronomical_unit_m*c,astronomical_unit_m*s*.917482,astronomical_unit_m*s*.397777},
                {-astronomical_unit_m*w*s,astronomical_unit_m*w*c*.917482,astronomical_unit_m*w*c*.397777}};
    }
};
// No clock reads or allocation in non-profile evaluation. Signed ET gaps expose
// adaptive retries/backtracking; wall gaps show real query frequency.
struct Meter final:EphemerisProvider {
    const EphemerisProvider& source;
    bool profile;
    mutable std::size_t calls=0;
    mutable double seconds=0;
    mutable std::vector<double> et_gaps,wall_gaps;
    mutable double last_et=0;
    mutable Clock::time_point last_wall{};
    explicit Meter(const EphemerisProvider& s,bool p=false):source(s),profile(p) {}
    EphemerisState geometric_state(int target,Epoch epoch) const override {
        Clock::time_point start{};
        if(profile) {
            start=Clock::now();
            if(calls) {et_gaps.push_back(epoch.et_seconds-last_et);wall_gaps.push_back(std::chrono::duration<double>(start-last_wall).count());}
        }
        auto result=source.geometric_state(target,epoch);
        if(profile) {seconds+=elapsed(start);last_et=epoch.et_seconds;last_wall=start;}
        ++calls;
        return result;
    }
};
inline EarthDynamics dynamics(bool environment) {
    std::vector<Surface> surfaces;
    for(BodyVector n:{BodyVector{1,0,0},BodyVector{-1,0,0},BodyVector{0,1,0},BodyVector{0,-1,0},BodyVector{0,0,1},BodyVector{0,0,-1}})
        surfaces.emplace_back(2,n,n*.5+BodyVector{0,.1,0},OpticalProperties{.3,.2});
    Spacecraft craft(10,Inertia((Eigen::Vector3d{2,3,4}).asDiagonal()),std::move(surfaces));
    EnvironmentalModels m;
    if(environment) {m.srp={true,true};m.drag={true,true};m.albedo={true,true};m.drag_model=DragModel::sentman;m.gravity_gradient=true;}
    return EarthDynamics(EarthGravity(Earth{},true),std::move(craft),m);
}
inline State initial() {
    const Earth e;
    const double r=e.equatorial_radius_m+400e3;
    return {{r,0,0},{0,std::sqrt(e.mu_m3_s2/r)*.8,std::sqrt(e.mu_m3_s2/r)*.6},
            normalize_quaternion({.9238795325112867,.3826834323650898,0,0}),{.002,.001,.0005}};
}
inline IntegrationOptions tolerances() {
    IntegrationOptions o;
    o.rtol=1e-10;o.position_atol_m=1e-3;o.velocity_atol_m_s=1e-6;
    o.quaternion_atol=1e-12;o.omega_atol_rad_s=1e-12;o.initial_step_s=1;o.max_step_s=15;
    return o;
}
inline std::vector<double> times(double duration) {
    std::vector<double> t{0};
    for(double s=30;s<duration;s+=30) t.push_back(s);
    t.push_back(duration);return t;
}
struct Stats {
    double maximum=0,sum=0; std::size_t count=0;
    void add(double e) {if(!std::isfinite(e)) throw std::runtime_error("nonfinite error");maximum=std::max(maximum,std::abs(e));sum+=e*e;++count;}
    std::string json() const {Json j;j.add("max",maximum);j.add("rms",count?std::sqrt(sum/count):0);j.add("count",count);return j.str();}
};
inline double angle(Quaternion a,Quaternion b) {
    if(a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z<0) b={-b.w,-b.x,-b.y,-b.z};
    const double d=std::sqrt(std::pow(a.w-b.w,2)+std::pow(a.x-b.x,2)+std::pow(a.y-b.y,2)+std::pow(a.z-b.z,2));
    return 4*std::asin(std::min(1.,d*.5));
}
inline Json reference(const Trajectory& tr,const EarthDynamics& rhs,const EphemerisProvider* provider) {
    EarthEnvironment source(Earth{},tr.epoch,provider,provider!=nullptr);
    Checksum checksum; double work=0,previous_power=0,previous_time=0;
    for(const auto& sample:tr.samples) {
        const State& s=sample.state;
        if(!s.r.as_eigen().allFinite()||!s.v.as_eigen().allFinite()||!s.omega.as_eigen().allFinite()||norm(s.r)<Earth{}.equatorial_radius_m)
            throw std::runtime_error("invalid trajectory");
        const double qnorm=std::sqrt(s.q.w*s.q.w+s.q.x*s.q.x+s.q.y*s.q.y+s.q.z*s.q.z);
        if(std::abs(qnorm-1)>1e-12) throw std::runtime_error("quaternion norm drift");
        checksum.add(s);
        if(provider) {
            const auto c=rhs.evaluate(sample.t,s,source(sample.t,s));
            const double power=dot(c.srp.force_n,s.v);
            if(sample.t.seconds>0) work+=(power+previous_power)*.5*(sample.t.seconds-previous_time);
            previous_power=power;previous_time=sample.t.seconds;
        }
    }
    const auto& s=tr.samples.back().state;
    Json j;j.vector("final_position_m",s.r);j.vector("final_velocity_m_s",s.v);
    std::ostringstream q;q<<std::setprecision(17)<<'['<<s.q.w<<','<<s.q.x<<','<<s.q.y<<','<<s.q.z<<']';j.raw("final_quaternion",q.str());
    j.vector("final_omega_rad_s",s.omega);j.add("cumulative_srp_work_j",work);j.add("trajectory_checksum",checksum.str());return j;
}
}
