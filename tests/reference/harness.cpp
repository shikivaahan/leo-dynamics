// Measurement only: production EarthDynamics, propagate and propagate_batch.
#include "leo/integration/propagate.hpp"
#include "leo/integration/batch.hpp"
#include "leo/kernel/dynamics.hpp"
#include "leo/kernel/aerodynamics.hpp"
#include "leo/kernel/radiation.hpp"
#include "leo/environment/ephemeris.hpp"
#include "leo/environment/source.hpp"
#include <boost/property_tree/json_parser.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

namespace {
using Tree = boost::property_tree::ptree;
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double>(Clock::now()-start).count(); }

std::vector<double> array(const Tree& root,const std::string& path,std::size_t required=0) {
    std::vector<double> values;
    for(const auto& child:root.get_child(path)) {
        const double value=child.second.get_value<double>();
        if(!std::isfinite(value)) throw std::invalid_argument("nonfinite "+path);
        values.push_back(value);
    }
    if(required && values.size()!=required) throw std::invalid_argument("wrong length: "+path);
    return values;
}
leo::InertialVector inertial(const std::vector<double>& a) { return {a[0],a[1],a[2]}; }
leo::BodyVector body(const std::vector<double>& a) { return {a[0],a[1],a[2]}; }
leo::State state(const std::vector<double>& a) {
    if(a.size()!=13) throw std::invalid_argument("13 state components required");
    return {{a[0],a[1],a[2]},{a[3],a[4],a[5]},
            {a[6],a[7],a[8],a[9]},{a[10],a[11],a[12]}};
}
leo::State initial(const Tree& d) {
    auto r=array(d,"initial_state.position_m",3),v=array(d,"initial_state.velocity_m_s",3);
    auto q=array(d,"initial_state.quaternion",4),w=array(d,"initial_state.omega_body_rad_s",3);
    return {inertial(r),inertial(v),{q[0],q[1],q[2],q[3]},body(w)};
}
leo::Spacecraft spacecraft(const Tree& d) {
    Eigen::Matrix3d tensor;
    if(auto full=d.get_child_optional("spacecraft.inertia_tensor_kg_m2")) {
        if(full->size()!=3) throw std::invalid_argument("inertia requires 3 rows");
        int i=0;
        for(const auto& row:*full) {
            const auto values=array(row.second,"",3);
            for(int j=0;j<3;++j) tensor(i,j)=values[j];
            ++i;
        }
    } else {
        const auto values=array(d,"spacecraft.inertia_kg_m2",3);
        tensor=Eigen::Vector3d(values[0],values[1],values[2]).asDiagonal();
    }
    std::vector<leo::Surface> surfaces;
    for(const auto& entry:d.get_child("spacecraft.surfaces")) {
        const auto& s=entry.second;
        surfaces.emplace_back(s.get<double>("area_m2"),body(array(s,"normal",3)),body(array(s,"centre",3)),
                              leo::OpticalProperties(s.get<double>("specular"),s.get<double>("diffuse")),
                              leo::AerodynamicProperties(s.get<double>("drag_coefficient"),s.get<double>("accommodation")));
    }
    return leo::Spacecraft(d.get<double>("spacecraft.mass_kg"),leo::Inertia(tensor),std::move(surfaces));
}
// Owned constant Sun for the --native-source measurement route: the production
// EphemerisProvider interface, filled from the frozen scenario document. This
// is acquisition plumbing only - it contains no physics and no interpolation.
struct NativeFixedSun final : public leo::EphemerisProvider {
    leo::InertialVector position_m;
    explicit NativeFixedSun(leo::InertialVector position):position_m(position) {
        if(!position_m.as_eigen().allFinite()||leo::norm(position_m)<=0)
            throw std::invalid_argument("native-source constant Sun must be finite and nonzero");
    }
    leo::EphemerisState geometric_state(int target_naif_id,leo::Epoch epoch) const override {
        if(target_naif_id!=10||!std::isfinite(epoch.et_seconds))
            throw std::invalid_argument("Sun target and finite TDB epoch required");
        return {position_m,{}};
    }
};
leo::EnvironmentalModels models(const Tree& d) {
    leo::EnvironmentalModels result;
    const auto select=[&](const std::string& name) {
        const bool enabled=d.get<bool>("models."+name);
        return leo::SurfaceContribution{d.get<bool>("models."+name+"_force",enabled),
                                        d.get<bool>("models."+name+"_torque",enabled)};
    };
    result.srp=select("srp"); result.drag=select("drag"); result.albedo=select("albedo");
    const auto drag=d.get<std::string>("models.drag_model");
    if(drag!="flat_plate" && drag!="sentman") throw std::invalid_argument("unknown drag law");
    result.drag_model=result.drag.enabled() ? (drag=="sentman"?leo::DragModel::sentman:leo::DragModel::flat_plate):leo::DragModel::none;
    result.gravity_gradient=d.get<bool>("models.gravity_gradient");
    result.earth_albedo=d.get<double>("models.earth_albedo");
    return result;
}
struct Measurement {
    Tree document;
    leo::Earth earth;
    leo::EnvironmentalModels selected;
    leo::EarthDynamics dynamics;
    leo::ExponentialAtmosphere exponential;
    leo::EnvironmentSnapshot environment;
    leo::EclipseModel eclipse=leo::EclipseModel::none;
    bool constant_density=false;
    // --native-source route only. The provider is owned here and reached
    // through a heap-stable address, so it stays valid while Measurement
    // objects move inside per_member and outlives every worker join.
    std::unique_ptr<NativeFixedSun> owned_sun;
    std::optional<leo::EarthEnvironment> source;
    std::vector<double> times;
    leo::IntegrationOptions options;
    explicit Measurement(Tree input):document(std::move(input)),selected(models(document)),
        dynamics(leo::EarthGravity(earth,document.get<bool>("models.j2")),spacecraft(document),selected),
        exponential(document.get<double>("models.density_scale")) {
        const auto model=document.get<std::string>("models.eclipse_model","none");
        if(model=="conical") eclipse=leo::EclipseModel::conical;
        else if(model=="cylindrical") eclipse=leo::EclipseModel::cylindrical;
        else if(model!="none") throw std::invalid_argument("unknown eclipse model");
        if(selected.srp.enabled()||selected.albedo.enabled()) {
            environment.sun_position_m=inertial(array(document,"environment.sun_position_m",3));
            if(leo::norm(environment.sun_position_m)==0) throw std::invalid_argument("explicit nonzero Sun required");
        }
        environment.illumination=document.get<double>("environment.illumination",1.);
        if(auto rotation=document.get_child_optional("environment.atmosphere_angular_velocity_rad_s"))
            environment.atmosphere_angular_velocity_rad_s=inertial(array(*rotation,"",3));
        const auto density=document.get<std::string>("models.density_model","exponential");
        if(density!="exponential" && density!="constant") throw std::invalid_argument("unknown density model");
        constant_density=density=="constant";
        if(selected.drag.enabled() && constant_density) {
            environment.density_kg_m3=document.get<double>("environment.density_kg_m3");
            if(!std::isfinite(environment.density_kg_m3)||environment.density_kg_m3<0)
                throw std::invalid_argument("invalid explicit density");
        }
        times=array(document,"run.sample_times_s");
        const auto& t=document.get_child("run.tolerances");
        options.rtol=t.get<double>("rtol"); options.position_atol_m=t.get<double>("position_atol_m");
        options.velocity_atol_m_s=t.get<double>("velocity_atol_m_s"); options.quaternion_atol=t.get<double>("quaternion_atol");
        options.omega_atol_rad_s=t.get<double>("omega_atol_rad_s"); options.initial_step_s=t.get<double>("initial_step_s");
        options.max_step_s=t.get<double>("max_step_s");
    }
    // Opt-in measurement route: acquire through the production
    // EarthEnvironment the Python binding composes, instead of this
    // harness-local snapshot. Same forces, same numbers; only the
    // acquisition path differs. Constant-density fixtures have no
    // EarthEnvironment equivalent and are rejected rather than approximated.
    void enable_native_source() {
        if(constant_density) throw std::invalid_argument("native-source route does not support constant-density fixtures");
        const bool acquisition=dynamics.needs_environment();
        // Read the document directly: the constructor only caches the Sun for
        // radiation fixtures, while a drag-only fixture still acquires one.
        if(acquisition) owned_sun=std::make_unique<NativeFixedSun>(inertial(array(document,"environment.sun_position_m",3)));
        source.emplace(earth,leo::Epoch{0},owned_sun.get(),acquisition,eclipse,exponential,environment.atmosphere_angular_velocity_rad_s);
        source->validate_for(selected);
    }
    leo::EnvironmentSnapshot snapshot(leo::ElapsedTime t,const leo::State& s) const {
        if(source) return (*source)(t,s);
        auto result=environment; result.epoch={t.seconds};
        if(selected.drag.enabled() && !constant_density)
            result.density_kg_m3=exponential.density_kg_m3(leo::norm(s.r)-earth.equatorial_radius_m);
        if((selected.srp.enabled()||selected.albedo.enabled()) && eclipse!=leo::EclipseModel::none)
            result.illumination=leo::illumination_factor(eclipse,s.r,result.sun_position_m,earth.equatorial_radius_m);
        return result;
    }
};
struct alignas(64) Counter { std::size_t calls=0; };
struct Rhs {
    const Measurement* measurement;
    Counter* counter;
    leo::StateDerivative operator()(leo::ElapsedTime t,const leo::State& s) const {
        ++counter->calls;
        return measurement->dynamics(t,s,measurement->snapshot(t,s));
    }
};
void numbers(std::ostream& out,const auto& values) {
    out<<'['; bool first=true;
    for(double x:values) {
        if(!std::isfinite(x)) throw std::runtime_error("nonfinite output");
        if(!first) out<<','; first=false; out<<x;
    }
    out<<']';
}
void samples(std::ostream& out,const leo::Trajectory& trajectory) {
    out<<'[';
    for(std::size_t i=0;i<trajectory.samples.size();++i) {
        if(i) out<<',';
        const auto& sample=trajectory.samples[i]; const auto& s=sample.state;
        out<<"{\"t\":"<<sample.t.seconds<<",\"r\":"; numbers(out,s.r.as_eigen());
        out<<",\"v\":"; numbers(out,s.v.as_eigen());
        out<<",\"q\":"; numbers(out,std::array{s.q.w,s.q.x,s.q.y,s.q.z});
        out<<",\"omega\":"; numbers(out,s.omega.as_eigen()); out<<'}';
    }
    out<<']';
}
void pointwise(std::ostream& out,const Measurement& m,const std::string& path) {
    Tree points; boost::property_tree::read_json(path,points);
    out<<'['; bool first=true;
    for(const auto& entry:points.get_child("rows")) {
        if(!first) out<<','; first=false;
        const auto& row=entry.second;
        const auto s=state(array(row,"state",13)); const leo::ElapsedTime t{row.get<double>("t")};
        const auto env=m.snapshot(t,s);
        const auto loads=m.dynamics.evaluate(t,s,env);
        const auto rhs=m.dynamics(t,s,env);
        out<<"{\"t\":"<<t.seconds;
        auto field=[&](const char* name,const auto& value) { out<<",\""<<name<<"\":"; numbers(out,value); };
        field("rhs",leo::detail::pack(rhs));
        field("point_mass_m_s2",loads.point_mass_m_s2.as_eigen()); field("j2_m_s2",loads.j2_m_s2.as_eigen());
        field("gravity_gradient_nm",loads.gravity_gradient_nm.as_eigen());
        field("srp_force_n",loads.srp.force_n.as_eigen()); field("srp_torque_nm",loads.srp.torque_nm.as_eigen());
        field("drag_force_n",loads.drag.force_n.as_eigen()); field("drag_torque_nm",loads.drag.torque_nm.as_eigen());
        field("albedo_force_n",loads.albedo.force_n.as_eigen()); field("albedo_torque_nm",loads.albedo.torque_nm.as_eigen());
        field("total_acceleration_m_s2",loads.total_acceleration_m_s2.as_eigen());
        field("total_angular_acceleration_rad_s2",loads.total_angular_acceleration_rad_s2.as_eigen());
        out<<",\"illumination\":"<<env.illumination<<",\"density_kg_m3\":"<<env.density_kg_m3<<'}';
    }
    out<<']';
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc<3) throw std::invalid_argument("usage: leo_reference_dump SCENARIO OUTPUT [--configurations JSON] [--points JSON] [--points-only] [--route single|sequential|batch] [--members N] [--workers N] [--repeats N] [--warmups N] [--dump-batch] [--native-source]");
        std::string route="single",points,configurations; std::size_t members=1,workers=1,repeats=1,warmups=0;
        bool dump_batch=false,points_only=false,native_source=false;
        for(int i=3;i<argc;++i) {
            const std::string key=argv[i];
            if(key=="--dump-batch") { dump_batch=true; continue; }
            if(key=="--points-only") { points_only=true; continue; }
            if(key=="--native-source") { native_source=true; continue; }
            if(i+1==argc) throw std::invalid_argument("missing option value");
            const std::string value=argv[++i];
            if(key=="--route") route=value;
            else if(key=="--points") points=value;
            else if(key=="--configurations") configurations=value;
            else if(key=="--members") members=std::stoull(value);
            else if(key=="--workers") workers=std::stoull(value);
            else if(key=="--repeats") repeats=std::stoull(value);
            else if(key=="--warmups") warmups=std::stoull(value);
            else throw std::invalid_argument("unknown option "+key);
        }
        if(!members||!workers||!repeats||(route!="single"&&route!="sequential"&&route!="batch"))
            throw std::invalid_argument("invalid route/count");
        if(route=="single" && members!=1) throw std::invalid_argument("single route requires one member");
        const auto setup=Clock::now(); Tree input; boost::property_tree::read_json(argv[1],input);
        Measurement m(std::move(input));
        // Optional heterogeneous configurations for binding-overhead measurement.
        // The frozen qualification route does not use this option.
        std::vector<Measurement> per_member;
        if(!configurations.empty()) {
            Tree documents;boost::property_tree::read_json(configurations,documents);
            if(documents.size()!=members) throw std::invalid_argument("configuration count must equal members");
            per_member.reserve(members);
            for(auto& entry:documents) per_member.emplace_back(std::move(entry.second));
        }
        // Acquisition route is fixed before any timed region; workers join
        // inside propagate_batch, long after these sources are built.
        if(native_source) {
            m.enable_native_source();
            for(Measurement& member:per_member) member.enable_native_source();
        }
        std::vector<leo::State> initials(members,initial(m.document));
        for(std::size_t i=0;i<members;++i) {
            if(!per_member.empty()) initials[i]=initial(per_member[i].document);
            initials[i].r=initials[i].r+leo::InertialVector{.25*i,0.,0.};
            initials[i].v=initials[i].v+leo::InertialVector{0.,1e-4*i,0.};
        }
        const double setup_seconds=elapsed(setup);
        std::vector<double> durations; std::vector<std::size_t> calls;
        std::vector<leo::Trajectory> trajectories;
        for(std::size_t iteration=0;!points_only && iteration<warmups+repeats;++iteration) {
            std::vector<Counter> counters(members);
            trajectories.clear(); // Previous-repeat destruction is outside every route's clock.
            const auto begin=Clock::now();
            auto factory=[&](std::size_t index,const leo::State&) {
                return Rhs{per_member.empty()?&m:&per_member[index],&counters[index]};
            };
            if(route=="batch") trajectories=leo::propagate_batch(factory,initials,leo::Epoch{0},m.times,m.options,workers);
            else {
                std::vector<leo::Trajectory> output(members);
                for(std::size_t i=0;i<members;++i)
                    output[i]=leo::propagate(factory(i,initials[i]),initials[i],leo::Epoch{0},m.times,m.options);
                trajectories=std::move(output);
            }
            const double wall=elapsed(begin);
            if(iteration>=warmups) {
                durations.push_back(wall);
                calls.push_back(std::accumulate(counters.begin(),counters.end(),std::size_t{},
                    [](std::size_t count,const Counter& c) { return count+c.calls; }));
            }
        }
        double checksum=0;
        for(const auto& trajectory:trajectories)
            for(const auto& sample:trajectory.samples) {
                const auto packed=leo::detail::pack(sample.state);
                for(std::size_t i=0;i<packed.size();++i) checksum+=packed[i]*(i+1);
            }
        const auto publication=Clock::now();
        std::ofstream out(argv[2]); if(!out) throw std::runtime_error("cannot open output");
        out<<std::setprecision(17)<<"{\"name\":\""<<m.document.get<std::string>("name")<<"\",\"setup_seconds\":"<<setup_seconds
           <<",\"native_source\":"<<(native_source?"true":"false")
           <<",\"route\":\""<<route<<"\",\"members\":"<<members<<",\"workers_requested\":"<<workers
           <<",\"workers_actual\":"<<(route=="batch"?std::min(workers,members):1)<<",\"repetitions_seconds\":";
        numbers(out,durations); out<<",\"rhs_evaluations_per_repeat\":"; numbers(out,calls);
        out<<",\"rhs_evaluations\":"<<(calls.empty()?0:calls.back())
           <<",\"wall_seconds\":"<<(durations.empty()?0:durations.back())<<",\"checksum\":"<<checksum;
        if(!points.empty()) { out<<",\"pointwise\":"; pointwise(out,m,points); }
        if(!trajectories.empty()) { out<<",\"samples\":"; samples(out,trajectories.front()); }
        if(dump_batch) {
            out<<",\"trajectories\":[";
            for(std::size_t i=0;i<trajectories.size();++i) { if(i) out<<','; samples(out,trajectories[i]); }
            out<<']';
        }
        out<<",\"publication_seconds_before_final_flush\":"<<elapsed(publication)<<"}\n";
        out.flush(); if(!out) throw std::runtime_error("output publication failed");
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"qualification harness: "<<error.what()<<'\n'; return 1;
    }
}
