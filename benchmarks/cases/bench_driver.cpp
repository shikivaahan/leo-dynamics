#include "bench_support.hpp"
#include <algorithm>
#include <limits>
using namespace bench;
namespace {
constexpr std::size_t iterations=200000;
struct Source {
    std::unique_ptr<EphemerisProvider> real;
    std::unique_ptr<Meter> underlying;
#ifdef LEO_BENCH_CACHE
    std::unique_ptr<InterpolatedEphemerisProvider> cache;
#endif
    std::unique_ptr<Meter> active;
    double kernel_load=0,preprocess=0;
    std::size_t nodes=0,bytes=0,construction_calls=0;
    Source(const Options& o) {
        auto start=Clock::now();
        if(o.mode=="deterministic") real=std::make_unique<AnalyticSun>();
        else {
#ifndef LEO_BENCH_NO_SPICE
            std::vector<std::filesystem::path> files;
            if(!o.lsk.empty()) files.emplace_back(o.lsk);
            files.emplace_back(o.spk);
            real=std::make_unique<SpiceEphemerisProvider>(std::move(files));
#else
            throw std::invalid_argument("CSPICE not enabled");
#endif
        }
        kernel_load=elapsed(start);
        underlying=std::make_unique<Meter>(*real,o.profile);
        const EphemerisProvider* provider=underlying.get();
        if(o.mode=="cached") {
#ifdef LEO_BENCH_CACHE
            start=Clock::now();
            cache=std::make_unique<InterpolatedEphemerisProvider>(*underlying,Epoch{0},Epoch{o.duration},o.interval);
            preprocess=elapsed(start);nodes=cache->sample_count();bytes=cache->memory_bytes();provider=cache.get();
#else
            throw std::invalid_argument("cache unavailable in selected source");
#endif
        }
        construction_calls=underlying->calls;
        active=std::make_unique<Meter>(*provider);
    }
};
Json base(const Options& o,double wall,double preprocessing,std::size_t rhs,std::size_t queries,std::size_t underlying,const std::string& checksum,bool correct=true) {
    Json j;j.add("benchmark_id",o.id);j.add("mode",o.mode);j.add("interval_s",o.interval);j.add("duration_s",o.duration);
    j.add("wall_seconds",wall);j.add("preprocess_seconds",preprocessing);j.add("rhs_count",rhs);
    j.add("ephemeris_queries",queries);j.add("underlying_queries",underlying);j.add("checksum",checksum);j.add("correctness",correct);
    return j;
}
void gaps(Json& j,const std::string& name,std::vector<double> values) {
    if(values.empty()) return;
    std::sort(values.begin(),values.end());double sum=0;std::size_t zeros=0,negative=0;
    for(double v:values) {sum+=v;zeros+=v==0;negative+=v<0;}
    Json s;s.add("min",values.front());s.add("median",values[values.size()/2]);s.add("max",values.back());s.add("mean",sum/values.size());
    s.add("zero_count",zeros);s.add("negative_count",negative);s.add("count",values.size());j.raw(name,s.str());
}
std::string kernel(const Options& o) {
    const auto rhs=dynamics(false);
    const State start=initial();
    std::vector<State> states(iterations);
    std::vector<StateDerivative> output(iterations);
    for(std::size_t i=0;i<iterations;++i) {
        const double a=static_cast<double>(i)*.001;
        states[i]=start;
        states[i].r={start.r.x()*std::cos(a),start.r.x()*std::sin(a)*.8,start.r.x()*std::sin(a)*.6};
        states[i].v={-norm(start.v)*std::sin(a),norm(start.v)*std::cos(a)*.8,norm(start.v)*std::cos(a)*.6};
        states[i].omega={.002+1e-4*std::sin(a),.001+1e-4*std::cos(a),.0005};
    }
    auto t=Clock::now();
    for(std::size_t i=0;i<iterations;++i) output[i]=rhs(ElapsedTime{static_cast<double>(i)},states[i]);
    const double wall=elapsed(t);
    Checksum checksum;
    for(const auto& d:output) {
        for(double v:{d.r_dot.x(),d.r_dot.y(),d.r_dot.z(),d.v_dot.x(),d.v_dot.y(),d.v_dot.z(),d.q_dot.w,d.q_dot.x,d.q_dot.y,d.q_dot.z,d.omega_dot.x(),d.omega_dot.y(),d.omega_dot.z()}) {
            if(!std::isfinite(v)) throw std::runtime_error("nonfinite RHS");
            checksum.add(v);
        }
    }
    Json j=base(o,wall,0,iterations,0,0,checksum.str());j.add("rhs_nanoseconds_per_call",wall*1e9/iterations);
    Json ref;ref.vector("first_acceleration_m_s2",output.front().v_dot);ref.vector("last_acceleration_m_s2",output.back().v_dot);j.raw("reference",ref.str());
    return j.str();
}
std::string propagation(const Options& o) {
    const bool environmental=o.id=="BENCH-03";
    const auto rhs=dynamics(environmental);const auto samples=times(o.duration);
    std::unique_ptr<Source> source;
    if(environmental) source=std::make_unique<Source>(o);
    const EphemerisProvider* provider=source?source->active.get():nullptr;
    EarthEnvironment environment(Earth{},Epoch{0},provider,environmental);
    std::size_t calls=0;
    auto counted=[&](ElapsedTime t,const State& s) {++calls;return environmental?rhs(t,s,environment(t,s)):rhs(t,s);};
    auto start=Clock::now();
    const auto trajectory=propagate(counted,initial(),Epoch{0},samples,tolerances());
    const double wall=elapsed(start);
    const std::size_t queries=source?source->active->calls:0,underlying=source?source->underlying->calls:0;
    const double query_seconds=source?source->underlying->seconds:0;
    start=Clock::now();
    Json ref=reference(trajectory,rhs,provider);
    Checksum checksum;for(const auto& sample:trajectory.samples) checksum.add(sample.state);
    Json j=base(o,wall,source?source->preprocess:0,calls,queries,underlying,checksum.str());
    j.raw("reference",ref.str());j.add("postprocess_seconds",elapsed(start));j.add("sample_count",samples.size());
    if(source) {
        j.add("kernel_load_seconds",source->kernel_load);j.add("cache_memory_bytes",source->bytes);j.add("cache_sample_count",source->nodes);
        j.add("construction_queries",source->construction_calls);j.add("postprocess_queries",source->active->calls-queries);
        if(o.profile) {
            j.add("query_seconds",query_seconds);j.add("query_count",queries);j.add("query_runtime_fraction",query_seconds/wall);
            // Remove diagnostic queries added after the timed propagation.
            auto et=source->underlying->et_gaps,real=source->underlying->wall_gaps;
            if(et.size()>=queries&&queries) {et.resize(queries-1);real.resize(queries-1);}
            gaps(j,"epoch_query_gaps_s",std::move(et));gaps(j,"wall_query_gaps_s",std::move(real));
            double sink=0;start=Clock::now();for(std::size_t i=0;i<queries;++i) {const auto a=Clock::now();sink+=elapsed(a);}
            j.add("clock_pair_calibration_seconds",elapsed(start));j.add("clock_calibration_checksum",sink);
        }
    }
    return j.str();
}
std::string query(const Options& o) {
    Source source(o);
    std::vector<double> epochs(iterations);std::vector<EphemerisState> output(iterations);
    for(std::size_t i=0;i<iterations;++i) epochs[i]=o.duration*static_cast<double>((i*7919)%iterations)/iterations;
    auto start=Clock::now();
    for(std::size_t i=0;i<iterations;++i) output[i]=source.active->geometric_state(10,{epochs[i]});
    const double wall=elapsed(start);Checksum checksum;
    for(auto s:output) {if(!s.position_m.as_eigen().allFinite()||!s.velocity_m_s.as_eigen().allFinite()) throw std::runtime_error("invalid Sun state");checksum.add(s);}
    Json j=base(o,wall,source.preprocess,0,iterations,source.underlying->calls,checksum.str());
    j.add("queries_per_second",iterations/wall);j.add("nanoseconds_per_query",wall*1e9/iterations);
    j.add("cache_memory_bytes",source.bytes);j.add("cache_sample_count",source.nodes);j.add("construction_queries",source.construction_calls);
    Json ref;ref.vector("first_position_m",output.front().position_m);ref.vector("last_position_m",output.back().position_m);j.raw("reference",ref.str());return j.str();
}
#ifdef LEO_BENCH_CACHE
bool rejected(const std::function<void()>& f) {try {f();return false;} catch(const std::exception&) {return true;}}
struct EphErrors {
    Stats position,velocity,node_position,node_velocity;
    double build=0;std::size_t nodes=0,bytes=0,calls=0;
    bool boundaries=false,deterministic=true;
    bool accepted() const {return position.maximum<=1&&velocity.maximum<=1e-3&&node_position.maximum<=1e-3&&node_velocity.maximum<=1e-6&&boundaries&&deterministic;}
    Json json() const {Json j;j.raw("position_m",position.json());j.raw("velocity_m_s",velocity.json());j.raw("node_position_m",node_position.json());j.raw("node_velocity_m_s",node_velocity.json());j.add("build_seconds",build);j.add("nodes",nodes);j.add("memory_bytes",bytes);j.add("underlying_queries",calls);j.add("boundaries",boundaries);j.add("deterministic",deterministic);j.add("passed",accepted());return j;}
};
EphErrors ephemeris_errors(const EphemerisProvider& direct,double interval,double duration) {
    Meter meter(direct);auto start=Clock::now();InterpolatedEphemerisProvider cache(meter,{0},{duration},interval);
    EphErrors e;e.build=elapsed(start);e.nodes=cache.sample_count();e.bytes=cache.memory_bytes();e.calls=meter.calls;
    auto compare=[&](double t,bool node) {
        const auto a=direct.geometric_state(10,{t}),b=cache.geometric_state(10,{t}),repeat=cache.geometric_state(10,{t});
        (node?e.node_position:e.position).add(norm(a.position_m-b.position_m));
        (node?e.node_velocity:e.velocity).add(norm(a.velocity_m_s-b.velocity_m_s));
        e.deterministic=e.deterministic&&(b.position_m.as_eigen()==repeat.position_m.as_eigen())&&(b.velocity_m_s.as_eigen()==repeat.velocity_m_s.as_eigen());
    };
    double t=0;
    while(t<duration) {compare(t,true);const double next=std::min(duration,t+interval);for(double u:{.25,.5,.75}) compare(t+(next-t)*u,false);t=next;}
    compare(duration,true);
    e.boundaries=rejected([&]{cache.geometric_state(10,{std::nextafter(0.,-1.)});})&&rejected([&]{cache.geometric_state(10,{std::nextafter(duration,std::numeric_limits<double>::infinity())});})&&
        rejected([&]{cache.geometric_state(399,{0});})&&rejected([&]{cache.geometric_state(10,{std::numeric_limits<double>::quiet_NaN()});})&&rejected([&]{cache.geometric_state(10,{std::numeric_limits<double>::infinity()});});
    return e;
}
std::string accuracy(const Options& o) {
    Options direct_options=o;direct_options.mode="direct";Source source(direct_options);
    const auto& direct=*source.real;
    auto start=Clock::now();
    auto short_errors=ephemeris_errors(direct,o.interval,21600);
    auto long_errors=ephemeris_errors(direct,o.interval,30*86400);
    auto partial_errors=ephemeris_errors(direct,o.interval,21600+17);
    Meter construction(direct);
    auto build_start=Clock::now();InterpolatedEphemerisProvider cache(construction,{0},{21600},o.interval);const double build=elapsed(build_start);
    const auto rhs=dynamics(true);const auto samples=times(21600);
    EarthEnvironment a(Earth{},Epoch{0},&direct,true),b(Earth{},Epoch{0},&cache,true);
    std::size_t direct_rhs=0,cached_rhs=0;
    auto rd=[&](ElapsedTime t,const State& s){++direct_rhs;return rhs(t,s,a(t,s));};
    auto rc=[&](ElapsedTime t,const State& s){++cached_rhs;return rhs(t,s,b(t,s));};
    auto timer=Clock::now();const auto td=propagate(rd,initial(),{0},samples,tolerances());const double direct_wall=elapsed(timer);
    timer=Clock::now();const auto tc=propagate(rc,initial(),{0},samples,tolerances());const double cached_wall=elapsed(timer);
    const auto repeat=propagate(rc,initial(),{0},samples,tolerances());
    Checksum hash,repeat_hash;
    Stats force,torque,illumination,position,velocity,attitude,omega;
    double wd=0,wc=0,pd=0,pc=0,last_t=0;
    for(std::size_t i=0;i<td.samples.size();++i) {
        const auto& x=td.samples[i];const auto& y=tc.samples[i];
        hash.add(y.state);repeat_hash.add(repeat.samples[i].state);
        const auto envd=a(x.t,x.state),envc=b(x.t,x.state);
        const auto cd=rhs.evaluate(x.t,x.state,envd),cc=rhs.evaluate(x.t,x.state,envc);
        force.add(norm(cd.srp.force_n-cc.srp.force_n));torque.add(norm(cd.srp.torque_nm-cc.srp.torque_nm));illumination.add(envd.illumination-envc.illumination);
        position.add(norm(x.state.r-y.state.r));velocity.add(norm(x.state.v-y.state.v));attitude.add(angle(x.state.q,y.state.q));omega.add(norm(x.state.omega-y.state.omega));
        const double powerd=dot(cd.srp.force_n,x.state.v),powerc=dot(rhs.evaluate(y.t,y.state,b(y.t,y.state)).srp.force_n,y.state.v);
        wd+=(powerd+pd)*.5*(x.t.seconds-last_t);wc+=(powerc+pc)*.5*(x.t.seconds-last_t);
        pd=powerd;pc=powerc;last_t=x.t.seconds;
    }
    std::size_t penumbra_points=0;
    // Sample the shadow edge at three changing epochs on the same spacecraft
    // geometries for both providers, independent of trajectory event sampling.
    for(double et:{0.,10813.,21599.}) {
        auto sun=direct.geometric_state(10,{et}).position_m;const auto u=sun/norm(sun);
        auto axis=cross(u,InertialVector{0,0,1});axis=axis/norm(axis);
        const double r=norm(initial().r),edge=std::asin(Earth{}.equatorial_radius_m/r);
        for(int i=0;i<=2000;++i) {
            const double theta=edge-.012+.024*i/2000;
            State s=initial();s.r=(-u*std::cos(theta)+axis*std::sin(theta))*r;
            const auto ea=a({et},s),eb=b({et},s);
            illumination.add(ea.illumination-eb.illumination);
            penumbra_points+=ea.illumination>0&&ea.illumination<1;
            const auto fa=rhs.evaluate({et},s,ea).srp,fb=rhs.evaluate({et},s,eb).srp;
            force.add(norm(fa.force_n-fb.force_n));torque.add(norm(fa.torque_nm-fb.torque_nm));
        }
    }
    const auto& x=td.samples.back().state;const auto& y=tc.samples.back().state;
    const double final_position=norm(x.r-y.r),final_velocity=norm(x.v-y.v),final_attitude=angle(x.q,y.q),final_omega=norm(x.omega-y.omega);
    const bool deterministic=hash.str()==repeat_hash.str();
    const bool accepted=short_errors.accepted()&&long_errors.accepted()&&partial_errors.accepted()&&deterministic&&penumbra_points>0&&
        final_position<=1e-2&&final_velocity<=1e-5&&final_attitude<=1e-6&&final_omega<=1e-8&&force.maximum<=1e-10&&torque.maximum<=1e-10&&illumination.maximum<=1e-6&&std::abs(wd-wc)<=1e-5;
    Json j=base(o,elapsed(start),build,direct_rhs+cached_rhs,0,construction.calls,hash.str(),accepted);
    Json report;report.add("passed",accepted);report.add("deterministic",deterministic);j.raw("accuracy",report.str());
    j.raw("ephemeris_6h",short_errors.json().str());j.raw("ephemeris_30d",long_errors.json().str());j.raw("nonmultiple_boundary",partial_errors.json().str());
    j.raw("srp_force_n",force.json());j.raw("srp_torque_nm",torque.json());j.raw("illumination",illumination.json());
    j.raw("trajectory_position_m",position.json());j.raw("trajectory_velocity_m_s",velocity.json());j.raw("trajectory_attitude_rad",attitude.json());j.raw("trajectory_omega_rad_s",omega.json());
    j.add("final_position_m",final_position);j.add("final_velocity_m_s",final_velocity);j.add("final_attitude_rad",final_attitude);j.add("final_omega_rad_s",final_omega);
    j.add("srp_work_direct_j",wd);j.add("srp_work_cached_j",wc);j.add("srp_work_difference_j",std::abs(wd-wc));j.add("penumbra_points",penumbra_points);
    j.add("direct_rhs_count",direct_rhs);j.add("cached_rhs_count",cached_rhs/2);j.add("direct_wall_seconds",direct_wall);j.add("cached_wall_seconds",cached_wall);
    j.raw("reference",reference(tc,rhs,&cache).str());return j.str();
}
#endif
}
int main(int argc,char** argv) {
    try {
        const auto o=parse(argc,argv);std::string result;
        if(o.id=="BENCH-01") result=kernel(o);
        else if(o.id=="BENCH-02"||o.id=="BENCH-03") result=propagation(o);
        else if(o.id=="QUERY") result=query(o);
#ifdef LEO_BENCH_CACHE
        else if(o.id=="ACCURACY"&&o.mode=="cached") result=accuracy(o);
#endif
        else throw std::invalid_argument("unsupported benchmark/configuration");
        std::cout<<result<<'\n';return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
