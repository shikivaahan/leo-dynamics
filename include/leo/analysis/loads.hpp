#pragma once
#include "leo/analysis/orbital.hpp"
#include "leo/kernel/dynamics.hpp"
#include <span>
namespace leo::analysis {
struct EnvironmentalSample {
    double time_s;
    EnvironmentSnapshot snapshot;
    DynamicsContributions contributions;
};
std::vector<EnvironmentalSample> environmental_history(const Trajectory&,const EarthDynamics&,std::span<const EnvironmentSnapshot>);
struct SurfaceSample {
    std::size_t surface_index;
    InertialVector normal_j2000;
    double sun_incidence_rad,projected_illuminated_area_m2,projected_aerodynamic_area_m2,orbit_normal_angle_rad;
};
struct AttitudeSample {
    double time_s;
    Quaternion quaternion;
    BodyVector omega_rad_s;
    double angular_speed_rad_s,rotational_energy_j,inertial_momentum_kg_m2_s;
    BodyVector torque_nm;
    double torque_magnitude_nm;
    InertialVector relative_zyx_euler_rad; // x=yaw, y=pitch, z=roll; Rz Ry Rx.
    bool euler_singular;
    std::vector<SurfaceSample> surfaces;
};
// Empty environment means torque-free diagnostics and unavailable surface
// incidence/solar/aerodynamic areas (NaN); orientation remains available.
std::vector<AttitudeSample> attitude_history(const Trajectory&,const Spacecraft&,std::span<const EnvironmentalSample> = {});
struct Rtn {};
using RtnVector = Vector3<Rtn>; // x=R, y=T, z=N; distinct from J2000 and body.
struct RtnBasis { InertialVector radial,transverse,normal; };
RtnBasis rtn_basis(const State&);
RtnVector rtn_components(const RtnBasis&,InertialVector);
struct WorkSample {
    double time_s;
    InertialVector srp_force_n,srp_acceleration_m_s2;
    RtnVector force_rtn_n,power_rtn_w,cumulative_work_rtn_j;
    double power_w,cumulative_work_j,specific_work_j_kg,orbital_energy_change_j,specific_energy_change_j_kg,energy_minus_srp_work_j;
    double sun_inbound_transverse_work_j,sun_outbound_transverse_work_j;
    double mass_kg; // Carries the force/work versus specific-work scale at fractional boundaries.
};
std::vector<WorkSample> srp_work(const Trajectory&,const Earth&,double mass_kg,std::span<const EnvironmentalSample>);
struct OrbitWork {
    std::size_t index;
    double start_time_s,end_time_s;
    bool complete;
    double work_j,specific_work_j_kg;
    RtnVector work_rtn_j;
    double orbital_energy_change_j,energy_minus_srp_work_j;
};
// Integrates the same piecewise-linear power used by srp_work, including
// fractional boundary intervals. This is quadratic cumulative interpolation.
std::vector<OrbitWork> work_per_orbit(std::span<const OrbitSummary>,std::span<const WorkSample>);
}
