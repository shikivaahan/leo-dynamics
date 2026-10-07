#pragma once
// Test-only analytical two-body oracle. Deliberately independent of the kernel's
// orbital utility: propagation here is closed form, so a mismatch means the
// numerics are wrong rather than that two implementations agree.
#include <Eigen/Geometry>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "leo/kernel/earth.hpp"
#include "leo/kernel/frames.hpp"
#include "leo/kernel/orbital.hpp"
#include "leo/kernel/state.hpp"

namespace leo_test {

inline constexpr double pi = std::numbers::pi_v<double>;

// Newton solve of Kepler's equation E - e sin E = M, initial guess E = M.
inline double solve_kepler_eccentric_anomaly(double e, double mean_anomaly_rad,
                                             double tolerance = 1e-14,
                                             int max_iterations = 50) {
    if (!(e >= 0.0 && e < 1.0) || !std::isfinite(mean_anomaly_rad))
        throw std::invalid_argument("solve_kepler_eccentric_anomaly: requires 0<=e<1 and finite mean anomaly");
    const double m = std::remainder(mean_anomaly_rad, 2.0 * pi);
    double E = m;
    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        const double residual = E - e * std::sin(E) - m;
        if (std::abs(residual) <= tolerance) return E;
        const double slope = 1.0 - e * std::cos(E);
        if (slope == 0.0) break;
        const double next = E - residual / slope;
        if (!std::isfinite(next)) break;
        E = next;
    }
    throw std::runtime_error("solve_kepler_eccentric_anomaly: Newton solve did not converge");
}

struct EllipticOrbit {
    double a_m;
    double e;
    double i_rad;
    double raan_rad;
    double arg_periapsis_rad;
    double mu_m3_s2 = leo::Earth{}.mu_m3_s2;
};

inline double period_s(const EllipticOrbit& orbit) {
    return 2.0 * pi * std::sqrt(orbit.a_m * orbit.a_m * orbit.a_m / orbit.mu_m3_s2);
}

inline double mean_motion_rad_s(const EllipticOrbit& orbit) {
    return 2.0 * pi / period_s(orbit);
}

// Mean anomaly at elapsed time, with M0 = 0 at periapsis passage t = 0.
inline double mean_anomaly_rad(const EllipticOrbit& orbit, double t) {
    return mean_motion_rad_s(orbit) * t;
}

inline double eccentric_anomaly_rad(const EllipticOrbit& orbit, double t) {
    return solve_kepler_eccentric_anomaly(orbit.e, mean_anomaly_rad(orbit, t));
}

// Circular fixture expressed as the degenerate elliptic case, so the same
// closed-form oracle serves both trajectories.
inline EllipticOrbit circular_orbit(double radius_m, double i_rad, double raan_rad,
                                    double mu_m3_s2 = leo::Earth{}.mu_m3_s2) {
    return EllipticOrbit{radius_m, 0.0, i_rad, raan_rad, 0.0, mu_m3_s2};
}

// Active perifocal -> inertial rotation: Rz(raan) Rx(i) Rz(argp).
inline Eigen::Matrix3d inertial_from_perifocal(double raan_rad, double i_rad, double argp_rad) {
    return Eigen::AngleAxisd(raan_rad, Eigen::Vector3d::UnitZ()).toRotationMatrix() *
           Eigen::AngleAxisd(i_rad, Eigen::Vector3d::UnitX()).toRotationMatrix() *
           Eigen::AngleAxisd(argp_rad, Eigen::Vector3d::UnitZ()).toRotationMatrix();
}

// Exact Keplerian state at elapsed time t; exact sin/cos motion when e = 0.
inline leo::CartesianOrbit kepler_state_at(const EllipticOrbit& orbit, double t) {
    const double root = std::sqrt(1.0 - orbit.e * orbit.e);
    const double E = eccentric_anomaly_rad(orbit, t);
    const Eigen::Vector3d position_perifocal(orbit.a_m * (std::cos(E) - orbit.e),
                                             orbit.a_m * root * std::sin(E), 0.0);
    // Perifocal velocity from the vis-viva radius: |v| = sqrt(mu a)/r with
    // r = a (1 - e cos E); scaling by sqrt(mu/p) alone drops the 1/(1 - e cos E).
    const double radius_m = orbit.a_m * (1.0 - orbit.e * std::cos(E));
    const double velocity_scale = std::sqrt(orbit.mu_m3_s2 * orbit.a_m) / radius_m;
    const Eigen::Vector3d velocity_perifocal(-velocity_scale * std::sin(E),
                                             velocity_scale * root * std::cos(E), 0.0);
    const Eigen::Matrix3d rotation =
        inertial_from_perifocal(orbit.raan_rad, orbit.i_rad, orbit.arg_periapsis_rad);
    return {leo::InertialVector(rotation * position_perifocal),
            leo::InertialVector(rotation * velocity_perifocal)};
}

// Kernel-independent initial state fixture: identity attitude, zero body rate.
inline leo::State identity_attitude_state(const leo::CartesianOrbit& orbit) {
    return {orbit.r, orbit.v, leo::Quaternion{1.0, 0.0, 0.0, 0.0}, leo::BodyVector{0.0, 0.0, 0.0}};
}

}  // namespace leo_test