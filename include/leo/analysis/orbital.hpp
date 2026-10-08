#pragma once
#include "leo/integration/trajectory.hpp"
#include "leo/kernel/earth.hpp"
#include "leo/kernel/orbital.hpp"
#include <span>
namespace leo::analysis {
struct OrbitalSample {
    double time_s,radius_m,altitude_m;
    ClassicalElements elements;
    double periapsis_radius_m,apoapsis_radius_m,periapsis_altitude_m,apoapsis_altitude_m,specific_energy_j_kg;
    InertialVector specific_angular_momentum_m2_s;
    double raan_unwrapped_rad,arg_periapsis_unwrapped_rad,true_anomaly_unwrapped_rad;
    double specific_kinetic_energy_j_kg,specific_potential_energy_j_kg;
};
void validate_trajectory(const Trajectory&);
std::vector<double> unwrap_angles(std::span<const double>);
std::vector<OrbitalSample> orbital_history(const Trajectory&,const Earth&);
enum class ApsisKind { periapsis,apoapsis };
struct ApsisEvent {
    ApsisKind kind;
    double time_s,radius_m;
    std::size_t left_index;
    double fraction;
};
// Linear radial-velocity zero crossing; radius linearly interpolated between
// sampled radii. Near-constant radius has no identifiable apsides.
std::vector<ApsisEvent> detect_apsides(const Trajectory&,double circular_tolerance=1e-8);
struct OrbitSummary {
    std::size_t index;
    double start_time_s,end_time_s;
    bool complete;
    double period_s; // 0 for incomplete intervals, not a period estimate.
    ClassicalElements start_elements,end_elements,delta_elements;
    double raan_start_unwrapped_rad,raan_end_unwrapped_rad;
};
// Periapsis-to-periapsis for noncircular trajectories; circular trajectories
// use longitude phase wraps in the initial orbital plane. Includes partial
// boundary intervals. Elements at boundaries interpolate sampled diagnostics.
std::vector<OrbitSummary> per_orbit(const Trajectory&,const Earth&);
}
