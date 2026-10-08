#include "leo/analysis/export.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string_view>
#include <type_traits>
namespace leo::analysis {
AnalysisResult analyze(const Trajectory& trajectory,const EarthDynamics& dynamics,
                       std::span<const EnvironmentSnapshot> snapshots) {
    validate_trajectory(trajectory);
    AnalysisResult result{trajectory.epoch,dynamics.gravity().earth(),dynamics.spacecraft().mass_kg(),dynamics.models(),dynamics.gravity().j2_enabled(),{},{},{},{},{},{},{}};
    result.orbital=orbital_history(trajectory,result.earth);
    result.apsides=detect_apsides(trajectory);
    result.orbits=per_orbit(trajectory,result.earth);
    result.environment=environmental_history(trajectory,dynamics,snapshots);
    result.attitude=attitude_history(trajectory,dynamics.spacecraft(),result.environment);
    result.work=srp_work(trajectory,result.earth,result.mass_kg,result.environment);
    result.orbit_work=work_per_orbit(result.orbits,result.work);
    return result;
}
namespace {
constexpr std::array<std::string_view,9> filenames{
    "metadata.csv","trajectory.csv","orbital_elements.csv","attitude.csv","surfaces.csv",
    "environment.csv","work.csv","per_orbit.csv","apsides.csv"};
class Csv {
    std::ostream* out_;
    bool first_=true;
public:
    explicit Csv(std::ostream* out):out_(out) {}
    void header(std::string_view text) { if(out_) *out_<<text<<'\n'; }
    template<class T> void value(T value) {
        if constexpr(std::is_floating_point_v<T>)
            if(!std::isfinite(value)) throw std::invalid_argument("nonfinite CSV diagnostic");
        separator(); if(out_) *out_<<value;
    }
    void optional(double value) {
        if(std::isinf(value)) throw std::invalid_argument("infinite surface diagnostic");
        separator(); if(out_) { if(std::isnan(value)) *out_<<"nan"; else *out_<<value; }
    }
    void separator() { if(out_&&!first_) *out_<<','; first_=false; }
    template<class... T> void row(T... values) { (value(values),...); end(); }
    template<class Vector> void vector(Vector v) { value(v.x());value(v.y());value(v.z()); }
    void end() { if(out_) *out_<<'\n'; first_=true; }
};
void emit(std::size_t file,std::ostream* stream,const Trajectory& t,const AnalysisResult& a) {
    Csv c(stream);
    switch(file) {
    case 0:
        c.header("key,value");
        c.row("schema_version",1);
        c.row("initial_epoch_et_seconds",a.epoch.et_seconds);
        c.row("epoch_scale","TDB_seconds_past_J2000");
        c.row("elapsed_time_unit","s"); c.row("inertial_frame","J2000");
        c.row("quaternion_convention","Hamilton_scalar_first_body_to_J2000");
        c.row("orbital_elements","osculating_two_body_not_mean");
        c.row("earth_mu_m3_s2",a.earth.mu_m3_s2);
        c.row("earth_equatorial_radius_m",a.earth.equatorial_radius_m);
        c.row("earth_j2",a.earth.j2);c.row("j2_enabled",a.j2_enabled);
        c.row("surface_count",a.attitude.front().surfaces.size());
        c.row("mass_kg",a.mass_kg);
        c.row("srp_force_enabled",a.models.srp.force); c.row("srp_torque_enabled",a.models.srp.torque);
        c.row("drag_force_enabled",a.models.drag.force); c.row("drag_torque_enabled",a.models.drag.torque);
        c.row("albedo_force_enabled",a.models.albedo.force); c.row("albedo_torque_enabled",a.models.albedo.torque);
        c.row("gravity_gradient_enabled",a.models.gravity_gradient);
        c.row("drag_model",static_cast<int>(a.models.drag_model));
        break;
    case 1:
        c.header("time_s,x_m,y_m,z_m,vx_m_s,vy_m_s,vz_m_s,qw,qx,qy,qz,omega_x_rad_s,omega_y_rad_s,omega_z_rad_s");
        for(const auto& s:t.samples) {
            c.value(s.t.seconds);c.vector(s.state.r);c.vector(s.state.v);
            c.value(s.state.q.w);c.value(s.state.q.x);c.value(s.state.q.y);c.value(s.state.q.z);
            c.vector(s.state.omega);c.end();
        } break;
    case 2:
        c.header("time_s,radius_m,altitude_m,a_m,e,i_rad,raan_rad,arg_periapsis_rad,true_anomaly_rad,periapsis_radius_m,apoapsis_radius_m,periapsis_altitude_m,apoapsis_altitude_m,specific_energy_j_kg,h_x_m2_s,h_y_m2_s,h_z_m2_s,raan_unwrapped_rad,arg_periapsis_unwrapped_rad,true_anomaly_unwrapped_rad,specific_kinetic_energy_j_kg,specific_potential_energy_j_kg");
        for(const auto& s:a.orbital) {
            c.value(s.time_s);c.value(s.radius_m);c.value(s.altitude_m);
            const auto& e=s.elements;
            c.value(e.a_m);c.value(e.e);c.value(e.i_rad);c.value(e.raan_rad);c.value(e.arg_periapsis_rad);c.value(e.true_anomaly_rad);
            c.value(s.periapsis_radius_m);c.value(s.apoapsis_radius_m);c.value(s.periapsis_altitude_m);c.value(s.apoapsis_altitude_m);
            c.value(s.specific_energy_j_kg);c.vector(s.specific_angular_momentum_m2_s);
            c.value(s.raan_unwrapped_rad);c.value(s.arg_periapsis_unwrapped_rad);c.value(s.true_anomaly_unwrapped_rad);
            c.value(s.specific_kinetic_energy_j_kg);c.value(s.specific_potential_energy_j_kg);c.end();
        } break;
    case 3:
        c.header("time_s,angular_speed_rad_s,rotational_energy_j,inertial_momentum_kg_m2_s,torque_x_nm,torque_y_nm,torque_z_nm,torque_magnitude_nm,relative_yaw_rad,relative_pitch_rad,relative_roll_rad,euler_singular");
        for(const auto& s:a.attitude) {
            c.value(s.time_s);c.value(s.angular_speed_rad_s);c.value(s.rotational_energy_j);c.value(s.inertial_momentum_kg_m2_s);
            c.vector(s.torque_nm);c.value(s.torque_magnitude_nm);c.vector(s.relative_zyx_euler_rad);c.value(s.euler_singular);c.end();
        } break;
    case 4:
        c.header("time_s,surface_index,normal_x,normal_y,normal_z,sun_incidence_rad,projected_illuminated_area_m2,projected_aerodynamic_area_m2,orbit_normal_angle_rad");
        for(const auto& s:a.attitude) for(const auto& surface:s.surfaces) {
            c.value(s.time_s);c.value(surface.surface_index);c.vector(surface.normal_j2000);
            c.optional(surface.sun_incidence_rad);c.optional(surface.projected_illuminated_area_m2);
            c.optional(surface.projected_aerodynamic_area_m2);c.optional(surface.orbit_normal_angle_rad);c.end();
        }
        break;
    case 5:
        c.header("time_s,illumination,density_kg_m3,point_mass_x_m_s2,point_mass_y_m_s2,point_mass_z_m_s2,j2_x_m_s2,j2_y_m_s2,j2_z_m_s2,srp_force_x_n,srp_force_y_n,srp_force_z_n,drag_force_x_n,drag_force_y_n,drag_force_z_n,albedo_force_x_n,albedo_force_y_n,albedo_force_z_n,total_non_gravitational_force_x_n,total_non_gravitational_force_y_n,total_non_gravitational_force_z_n,srp_torque_x_nm,srp_torque_y_nm,srp_torque_z_nm,drag_torque_x_nm,drag_torque_y_nm,drag_torque_z_nm,albedo_torque_x_nm,albedo_torque_y_nm,albedo_torque_z_nm,gravity_gradient_torque_x_nm,gravity_gradient_torque_y_nm,gravity_gradient_torque_z_nm,total_torque_x_nm,total_torque_y_nm,total_torque_z_nm,total_acceleration_x_m_s2,total_acceleration_y_m_s2,total_acceleration_z_m_s2,total_angular_acceleration_x_rad_s2,total_angular_acceleration_y_rad_s2,total_angular_acceleration_z_rad_s2,drag_model");
        for(const auto& s:a.environment) {
            c.value(s.time_s);c.value(s.snapshot.illumination);c.value(s.snapshot.density_kg_m3);
            const auto& d=s.contributions;
            c.vector(d.point_mass_m_s2);c.vector(d.j2_m_s2);c.vector(d.srp.force_n);c.vector(d.drag.force_n);c.vector(d.albedo.force_n);
            c.vector(d.non_gravitational.force_n);c.vector(d.srp.torque_nm);c.vector(d.drag.torque_nm);c.vector(d.albedo.torque_nm);
            c.vector(d.gravity_gradient_nm);c.vector(d.non_gravitational.torque_nm);c.vector(d.total_acceleration_m_s2);
            c.vector(d.total_angular_acceleration_rad_s2);c.value(static_cast<int>(a.models.drag_model));c.end();
        } break;
    case 6:
        c.header("time_s,srp_power_w,cumulative_srp_work_j,specific_srp_work_j,orbital_energy_change_j,specific_energy_change_j_kg,energy_minus_srp_work_j,srp_force_r_n,srp_force_t_n,srp_force_n_n,srp_power_r_w,srp_power_t_w,srp_power_n_w,cumulative_work_r_j,cumulative_work_t_j,cumulative_work_n_j,sun_inbound_transverse_work_j,sun_outbound_transverse_work_j");
        for(const auto& s:a.work) {
            c.value(s.time_s);c.value(s.power_w);c.value(s.cumulative_work_j);c.value(s.specific_work_j);
            c.value(s.orbital_energy_change_j);c.value(s.specific_energy_change_j_kg);c.value(s.energy_minus_srp_work_j);
            c.vector(s.force_rtn_n);c.vector(s.power_rtn_w);c.vector(s.cumulative_work_rtn_j);
            c.value(s.sun_inbound_transverse_work_j);c.value(s.sun_outbound_transverse_work_j);c.end();
        } break;
    case 7:
        c.header("orbit_index,start_time_s,end_time_s,complete,period_s,start_a_m,start_e,start_i_rad,start_raan_unwrapped_rad,end_raan_unwrapped_rad,delta_a_m,delta_e,delta_i_rad,delta_raan_rad,delta_arg_periapsis_rad,delta_true_anomaly_rad,srp_work_j,specific_srp_work_j,srp_work_r_j,srp_work_t_j,srp_work_n_j,orbital_energy_change_j,energy_minus_srp_work_j");
        for(std::size_t i=0;i<a.orbits.size();++i) {
            const auto& s=a.orbits[i];const auto& w=a.orbit_work[i];
            c.value(s.index);c.value(s.start_time_s);c.value(s.end_time_s);c.value(s.complete);c.value(s.period_s);
            c.value(s.start_elements.a_m);c.value(s.start_elements.e);c.value(s.start_elements.i_rad);
            c.value(s.raan_start_unwrapped_rad);c.value(s.raan_end_unwrapped_rad);
            c.value(s.delta_elements.a_m);c.value(s.delta_elements.e);c.value(s.delta_elements.i_rad);
            c.value(s.delta_elements.raan_rad);c.value(s.delta_elements.arg_periapsis_rad);c.value(s.delta_elements.true_anomaly_rad);
            c.value(w.work_j);c.value(w.specific_work_j);c.vector(w.work_rtn_j);c.value(w.orbital_energy_change_j);c.value(w.energy_minus_srp_work_j);c.end();
        } break;
    case 8:
        c.header("time_s,kind,radius_m,left_index,fraction");
        for(const auto& s:a.apsides) c.row(s.time_s,static_cast<int>(s.kind),s.radius_m,s.left_index,s.fraction);
        break;
    }
}
void validate(const Trajectory& t,const AnalysisResult& a) {
    validate_trajectory(t);
    (void)EarthGravity(a.earth,a.j2_enabled);
    if(a.epoch.et_seconds!=t.epoch.et_seconds||!std::isfinite(a.mass_kg)||a.mass_kg<=0)
        throw std::invalid_argument("analysis metadata does not match trajectory");
    const auto n=t.samples.size();
    if(a.orbital.size()!=n||a.environment.size()!=n||a.attitude.size()!=n||a.work.size()!=n||a.orbits.size()!=a.orbit_work.size())
        throw std::invalid_argument("analysis row counts do not match trajectory/orbits");
    for(std::size_t i=0;i<n;++i) {
        const auto time=t.samples[i].t.seconds;
        if(a.orbital[i].time_s!=time||a.environment[i].time_s!=time||a.attitude[i].time_s!=time||a.work[i].time_s!=time)
            throw std::invalid_argument("analysis timestamps do not match trajectory");
        if(a.work[i].mass_kg!=a.mass_kg||!std::isfinite(a.environment[i].snapshot.epoch.et_seconds)||
           a.environment[i].snapshot.epoch.et_seconds!=a.epoch.et_seconds+time)
            throw std::invalid_argument("analysis mass or snapshot epoch mismatch");
        if(a.attitude[i].surfaces.size()!=a.attitude.front().surfaces.size())
            throw std::invalid_argument("surface row counts change over trajectory");
        for(std::size_t j=0;j<a.attitude[i].surfaces.size();++j)
            if(a.attitude[i].surfaces[j].surface_index!=j) throw std::invalid_argument("surface indices must be ordered and contiguous");
    }
    if((n==1&&!a.orbits.empty())||(n>1&&(a.orbits.empty()||
       a.orbits.front().start_time_s!=t.samples.front().t.seconds||
       a.orbits.back().end_time_s!=t.samples.back().t.seconds)))
        throw std::invalid_argument("per-orbit intervals must cover the trajectory");
    for(std::size_t i=0;i<a.orbits.size();++i) {
        const auto& s=a.orbits[i];const auto& w=a.orbit_work[i];
        if(s.index!=i||w.index!=s.index||w.start_time_s!=s.start_time_s||w.end_time_s!=s.end_time_s||w.complete!=s.complete||s.end_time_s<=s.start_time_s||s.start_time_s<t.samples.front().t.seconds||s.end_time_s>t.samples.back().t.seconds)
            throw std::invalid_argument("malformed per-orbit interval");
        if((i>0&&s.start_time_s!=a.orbits[i-1].end_time_s)||
           s.period_s!=(s.complete?s.end_time_s-s.start_time_s:0))
            throw std::invalid_argument("per-orbit intervals must be contiguous with consistent periods");
    }
    for(const auto& event:a.apsides)
        if(event.left_index>=n-1||event.fraction<0||event.fraction>1||
           (event.kind!=ApsisKind::periapsis&&event.kind!=ApsisKind::apoapsis)||
           event.time_s<t.samples[event.left_index].t.seconds||event.time_s>t.samples[event.left_index+1].t.seconds)
            throw std::invalid_argument("malformed apsis interpolation bracket");
    for(std::size_t file=0;file<filenames.size();++file) emit(file,nullptr,t,a);
}
}
void export_csv(const std::filesystem::path& directory,const Trajectory& t,const AnalysisResult& a) {
    validate(t,a);
    if(directory.empty()) throw std::invalid_argument("CSV output directory must not be empty");
    std::error_code error;
    std::filesystem::create_directories(directory,error);
    if(error||!std::filesystem::is_directory(directory)) throw std::runtime_error("cannot create CSV output directory: "+directory.string()+": "+error.message());
    for(const auto name:filenames) {
        const auto path=directory/name;
        if(std::filesystem::exists(path)&&!std::filesystem::is_regular_file(path))
            throw std::runtime_error("CSV output is not a regular file: "+path.string());
    }
    for(std::size_t file=0;file<filenames.size();++file) {
        const auto path=directory/filenames[file];
        std::ofstream stream(path);
        if(!stream) throw std::runtime_error("cannot open CSV output: "+path.string());
        stream.imbue(std::locale::classic());
        stream<<std::setprecision(std::numeric_limits<double>::max_digits10);
        emit(file,&stream,t,a);stream.close();
        if(!stream) throw std::runtime_error("failed writing CSV output: "+path.string());
    }
}
}
