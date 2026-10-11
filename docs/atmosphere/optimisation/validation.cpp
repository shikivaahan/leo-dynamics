// Verification-only driver: include the selected unchanged/candidate experiment.
#define main atmosphere_experiment_main
#include ATM_OPT_EXPERIMENT_HEADER
#undef main
#include <array>

namespace {
std::ofstream* native_capture=nullptr;
std::size_t native_calls=0;
}
extern "C" void __real_leo_jb2008(const double*,const double*,const double*,
    const double*,const double*,const double*,const double*,const double*,const double*,
    const double*,const double*,const double*,double*,double*);
extern "C" void __wrap_leo_jb2008(const double* mjd,const double* sun,const double* sat,
    const double* f10,const double* f10b,const double* s10,const double* s10b,
    const double* m10,const double* m10b,const double* y10,const double* y10b,
    const double* dst,double* temp,double* rho) {
    __real_leo_jb2008(mjd,sun,sat,f10,f10b,s10,s10b,m10,m10b,y10,y10b,dst,temp,rho);
    if(native_capture) {
        const std::array<double,18> row{*mjd,sun[0],sun[1],sat[0],sat[1],sat[2],
            *f10,*f10b,*s10,*s10b,*m10,*m10b,*y10,*y10b,*dst,temp[0],temp[1],*rho};
        native_capture->write(reinterpret_cast<const char*>(row.data()),sizeof(row));
        ++native_calls;
    }
}
int main(int argc,char** argv) {
    try {
        const auto c=parse_arguments(argc,argv);
        Environment env(c);
        const auto epoch=leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
        std::filesystem::create_directories(c.out);
        for(const auto& case_file:c.case_files) {
            const auto spec=experiment::load(case_file);
            const auto dynamics=case_dynamics(spec);
            for(const auto model:requested_models()) for(const char* method:{"dopri","rkf78"}) {
                const auto tag=spec.id+"_"+model_name(model)+"_"+method;
                std::ofstream capture(std::filesystem::path(c.out)/(tag+"_native.bin"),std::ios::binary);
                if(!capture) throw std::runtime_error("cannot open native capture");
                native_capture=&capture;native_calls=0;
                const auto record=execute(c,env,spec,model,method,epoch);
                Checksum checksum;
                std::ostringstream samples,extended;
                samples<<std::setprecision(17)<<'[';
                extended<<std::setprecision(17)<<'[';
#ifdef ATM_OPT_CANDIDATE
                const leo::atmosphere::DirectAtmosphere atmosphere(model,env.ephemeris,env.weather,c.frame,c.ap_history,c.options);
#else
                const leo::atmosphere::DirectAtmosphere atmosphere(model,env.ephemeris,env.weather,c.frame,c.ap_history);
#endif
                for(std::size_t i=0;i<record.trajectory.samples.size();++i) {
                    const auto& sample=record.trajectory.samples[i];
                    const auto& state=sample.state;
                    checksum.add(state);
                    if(i) {samples<<',';extended<<',';}
                    samples<<'['<<sample.t.seconds;
                    for(double value:leo::detail::pack(state)) samples<<','<<value;
                    samples<<','<<record.altitude[i]<<','<<record.density[i]<<','
                        <<record.drag_acceleration[i]<<','<<record.drag_torque[i]<<','
                        <<record.cumulative_drag_work[i]<<','<<record.cumulative_non_gravitational_work[i]<<','
                        <<record.matched_energy_change[i]<<','<<record.attitude_angle[i]<<','
                        <<record.angular_momentum_norm[i]<<','<<record.angular_speed[i]<<','<<record.total_torque_norm[i]<<']';
                    const leo::Epoch at{epoch.et_seconds+sample.t.seconds};
                    leo::EnvironmentSnapshot snapshot;
                    snapshot.epoch=at;snapshot.atmosphere_angular_velocity_rad_s=InertialVector{0,0,7.292115e-5};
#ifdef ATM_OPT_CANDIDATE
                    const auto observation=env.ephemeris.observe_sun(at);
                    snapshot.sun_position_m=observation.state().position_m;
                    const auto result=atmosphere.evaluate(at,state.r,&observation);
#else
                    snapshot.sun_position_m=env.ephemeris.geometric_state(leo::sun_naif_id,at).position_m;
                    const auto result=atmosphere.evaluate(at,state.r);
#endif
                    snapshot.illumination=illumination_factor(EclipseModel::conical,state.r,snapshot.sun_position_m,Earth{}.equatorial_radius_m);
                    snapshot.density_kg_m3=result.density_kg_m3;
                    const auto loads=dynamics.evaluate(sample.t,state,snapshot);
                    extended<<'['<<sample.t.seconds<<','<<result.density_kg_m3<<','<<result.temperature_k<<','<<result.exospheric_temperature_k;
                    for(double value:result.number_density_m3) extended<<','<<value;
                    for(double value:{loads.drag.force_n.x(),loads.drag.force_n.y(),loads.drag.force_n.z(),
                        loads.drag.torque_nm.x(),loads.drag.torque_nm.y(),loads.drag.torque_nm.z()}) extended<<','<<value;
                    extended<<']';
                }
                samples<<']';extended<<']';
                native_capture=nullptr;capture.close();
                if(!capture) throw std::runtime_error("native capture write failed");
                Json output;
                output.add("tag",tag);output.add("trajectory_checksum",checksum.str());
                output.add("native_calls",native_calls);output.add("rhs_calls",record.rhs_calls);
                output.raw("samples",samples.str());output.raw("extended",extended.str());
                output.add("cumulative_drag_work_j",record.cumulative_drag_work_final);
                output.add("cumulative_non_gravitational_work_j",record.cumulative_non_gravitational_work_final);
                save(std::filesystem::path(c.out)/(tag+".json"),output.str());
                std::cout<<tag<<" "<<checksum.str()<<" native_calls="<<native_calls<<'\n';
            }
        }
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
