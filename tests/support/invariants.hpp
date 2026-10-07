#pragma once
// Test-only conserved-quantity and angle-tracking helpers. Nothing here is a
// production analysis API: it exists so individual test files can check drift
// over a whole sampled trajectory instead of at endpoints only.
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include "leo/integration/trajectory.hpp"
#include "leo/kernel/attitude.hpp"
#include "leo/kernel/earth.hpp"
#include "leo/kernel/frames.hpp"
#include "leo/kernel/orbital.hpp"
#include "leo/kernel/spacecraft.hpp"
#include "leo/kernel/state.hpp"

namespace leo_test {

inline double relative_drift(double value, double reference) {
    return std::abs(value - reference) / std::abs(reference);
}

// Specific two-body orbital energy, J/kg.
inline double specific_orbital_energy(const leo::CartesianOrbit& orbit, double mu_m3_s2) {
    return 0.5 * leo::dot(orbit.v, orbit.v) - mu_m3_s2 / leo::norm(orbit.r);
}

// Specific orbital angular momentum vector, m^2/s.
inline leo::InertialVector specific_angular_momentum(const leo::CartesianOrbit& orbit) {
    return leo::cross(orbit.r, orbit.v);
}

// Largest relative deviation of the two-body specific energy from its initial value.
inline double max_relative_energy_drift(const leo::Trajectory& trajectory, double mu_m3_s2) {
    if (trajectory.samples.empty()) return 0.0;
    const auto& first = trajectory.samples.front().state;
    const double reference = specific_orbital_energy({first.r, first.v}, mu_m3_s2);
    double worst = 0.0;
    for (const auto& sample : trajectory.samples)
        worst = std::max(worst, relative_drift(specific_orbital_energy({sample.state.r, sample.state.v}, mu_m3_s2), reference));
    return worst;
}

// Vector-norm drift against the initial total angular momentum magnitude.
inline double max_relative_angular_momentum_drift(const leo::Trajectory& trajectory) {
    if (trajectory.samples.empty()) return 0.0;
    const auto& first = trajectory.samples.front().state;
    const Eigen::Vector3d reference = specific_angular_momentum({first.r, first.v}).as_eigen();
    const double scale = reference.norm();
    double worst = 0.0;
    for (const auto& sample : trajectory.samples) {
        const Eigen::Vector3d momentum = specific_angular_momentum({sample.state.r, sample.state.v}).as_eigen();
        worst = std::max(worst, (momentum-reference).norm()/scale);
    }
    return worst;
}

// J2-conserved symmetry-axis component; useful for perturbation model checks.
inline double max_relative_angular_momentum_z_drift(const leo::Trajectory& trajectory) {
    if (trajectory.samples.empty()) return 0.0;
    const auto& first = trajectory.samples.front().state;
    const leo::InertialVector initial_momentum = specific_angular_momentum({first.r, first.v});
    const double reference = initial_momentum.z();
    double worst = 0.0;
    for (const auto& sample : trajectory.samples)
        worst = std::max(worst, relative_drift(specific_angular_momentum({sample.state.r, sample.state.v}).z(), reference));
    return worst;
}

// Largest quaternion norm deviation from unity.
inline double max_quaternion_norm_error(const leo::Trajectory& trajectory) {
    double worst = 0.0;
    for (const auto& sample : trajectory.samples) {
        const leo::Quaternion& q = sample.state.q;
        worst = std::max(worst, std::abs(std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z) - 1.0));
    }
    return worst;
}

// Remove 2*pi jumps from a sampled angle series.
inline std::vector<double> unwrapped_angles(const std::vector<double>& angles) {
    std::vector<double> unwrapped;
    unwrapped.reserve(angles.size());
    if (angles.empty()) return unwrapped;
    constexpr double full_turn = 2.0 * std::numbers::pi_v<double>;
    unwrapped.push_back(angles[0]);
    for (std::size_t index = 1; index < angles.size(); ++index) {
        const double turns = std::round((unwrapped[index - 1] - angles[index]) / full_turn);
        unwrapped.push_back(angles[index] + turns * full_turn);
    }
    return unwrapped;
}

// Times at which an unwrapped angle crosses k*step_rad upwards for k = 1, 2, ...,
// linearly interpolated between the bracketing samples. A crossing that lands
// exactly on a sample is reported at that sample.
inline std::vector<double> upward_crossing_times(const std::vector<double>& times_s,
                                                 const std::vector<double>& unwrapped,
                                                 double step_rad) {
    std::vector<double> crossings;
    if (times_s.size() != unwrapped.size() || times_s.size() < 2 || !(step_rad > 0.0)) return crossings;
    std::size_t target = 1;
    double level = step_rad;
    for (std::size_t index = 1; index < times_s.size(); ++index) {
        // The level is rebuilt as target*step_rad while the sample series is
        // accumulated by whole turns, so allow a few ulp of slack; otherwise a
        // crossing that lands exactly on the final sample could be missed.
        const double slack = 8.0 * std::numeric_limits<double>::epsilon() *
                             std::max({1.0, std::abs(level), std::abs(unwrapped[index])});
        while (level <= unwrapped[index] + slack) {
            const double span = unwrapped[index] - unwrapped[index - 1];
            if (level > unwrapped[index - 1] && span > 0.0) {
                const double fraction = (level - unwrapped[index - 1]) / span;
                crossings.push_back(times_s[index - 1] + fraction * (times_s[index] - times_s[index - 1]));
            }
            ++target;
            level = static_cast<double>(target) * step_rad;
        }
    }
    return crossings;
}

// Mean of consecutive differences of a monotone sample series.
inline double mean_interval_s(const std::vector<double>& ordered_samples) {
    if (ordered_samples.size() < 2) return 0.0;
    double total = 0.0;
    for (std::size_t index = 1; index < ordered_samples.size(); ++index)
        total += ordered_samples[index] - ordered_samples[index - 1];
    return total / static_cast<double>(ordered_samples.size() - 1);
}

}  // namespace leo_test