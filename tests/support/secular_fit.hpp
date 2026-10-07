#pragma once
// Test-only secular-drift fitting helpers. Nothing here is a production
// analysis API: it exists so individual test files can fit a first-order line
// to an osculating angle series and compare it against an analytical
// first-order secular rate.
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "leo/integration/trajectory.hpp"
#include "leo/kernel/earth.hpp"
#include "leo/kernel/frames.hpp"
#include "leo/kernel/orbital.hpp"
#include "leo/kernel/time.hpp"
#include "invariants.hpp"

namespace leo_test {

// Which osculating angle of the classical elements to fit.
enum class ElementAngle { kRightAscensionOfAscendingNode, kArgumentOfPeriapsis };

struct LinearFit {
    double slope;          // rate, angle units per second
    double intercept;      // value at t=0
    double max_residual;   // largest |value - (slope*t + intercept)|
    double rms_residual;   // root mean square of the residuals
};

// Centered least-squares fit value = slope * t + intercept. Rejects empty,
// mismatched, non-finite or degenerate (constant-time) inputs so a caller can
// never silently fit a line to garbage.
inline LinearFit fit_line(const std::vector<double>& times_s, const std::vector<double>& values) {
    if (times_s.size() != values.size() || times_s.size() < 2)
        throw std::invalid_argument("least-squares fit needs equally sized series with at least two points");
    const std::size_t count = times_s.size();
    double mean_t = 0.0;
    double mean_y = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        if (!std::isfinite(times_s[index]) || !std::isfinite(values[index]))
            throw std::invalid_argument("least-squares fit needs finite samples");
        mean_t += times_s[index];
        mean_y += values[index];
    }
    mean_t /= static_cast<double>(count);
    mean_y /= static_cast<double>(count);
    double covariance = 0.0;
    double variance_t = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const double dt = times_s[index] - mean_t;
        covariance += dt * (values[index] - mean_y);
        variance_t += dt * dt;
    }
    if (!(variance_t > 0.0)) throw std::invalid_argument("least-squares fit needs varying sample times");
    const double slope = covariance / variance_t;
    const double intercept = mean_y - slope * mean_t;
    double max_residual = 0.0;
    double sum_squares = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const double residual = values[index] - (slope * times_s[index] + intercept);
        max_residual = std::max(max_residual, std::abs(residual));
        sum_squares += residual * residual;
    }
    return {slope, intercept, max_residual, std::sqrt(sum_squares / static_cast<double>(count))};
}

// Osculating angle series over a propagated trajectory, with samples before
// discard_before_s dropped (callers discard the first orbital period to
// suppress short-period terms) and the remainder unwrapped.
inline std::vector<double> osculating_angle_series(const leo::Trajectory& trajectory, double mu_m3_s2,
                                                   ElementAngle angle, double discard_before_s,
                                                   std::vector<double>* times_s = nullptr) {
    std::vector<double> angles;
    std::vector<double> kept_times;
    angles.reserve(trajectory.samples.size());
    kept_times.reserve(trajectory.samples.size());
    for (const auto& sample : trajectory.samples) {
        if (sample.t.seconds < discard_before_s) continue;
        const leo::ClassicalElements elements =
            leo::cartesian_to_coe({sample.state.r, sample.state.v}, mu_m3_s2);
        angles.push_back(angle == ElementAngle::kRightAscensionOfAscendingNode ? elements.raan_rad
                                                                              : elements.arg_periapsis_rad);
        kept_times.push_back(sample.t.seconds);
    }
    if (times_s != nullptr) *times_s = kept_times;
    return unwrapped_angles(angles);
}

// Mean motion, rad/s, for a circular-equivalent semi-major axis.
inline double mean_motion_rad_s(double mu_m3_s2, double a_m) {
    return std::sqrt(mu_m3_s2 / (a_m * a_m * a_m));
}

// Orbital period, s, for a circular-equivalent semi-major axis.
inline double orbital_period_s(double mu_m3_s2, double a_m) {
    return 2.0 * std::numbers::pi_v<double> / mean_motion_rad_s(mu_m3_s2, a_m);
}

// First-order J2 nodal regression of the right ascension of the ascending
// node, rad/s: -3/2 n J2 (Re/p)^2 cos i.
inline double nodal_regression_rate_rad_s(const leo::Earth& earth, double mu_m3_s2, double a_m,
                                           double e, double i_rad) {
    const double semi_latus_rectum = a_m * (1.0 - e * e);
    const double ratio = earth.equatorial_radius_m / semi_latus_rectum;
    return -1.5 * mean_motion_rad_s(mu_m3_s2, a_m) * earth.j2 * ratio * ratio * std::cos(i_rad);
}

// First-order J2 apsidal regression of the argument of perigee, rad/s:
// 3/4 n J2 (Re/p)^2 (5 cos^2 i - 1).
inline double apsidal_regression_rate_rad_s(const leo::Earth& earth, double mu_m3_s2, double a_m,
                                             double e, double i_rad) {
    const double semi_latus_rectum = a_m * (1.0 - e * e);
    const double ratio = earth.equatorial_radius_m / semi_latus_rectum;
    const double cos_i = std::cos(i_rad);
    return 0.75 * mean_motion_rad_s(mu_m3_s2, a_m) * earth.j2 * ratio * ratio * (5.0 * cos_i * cos_i - 1.0);
}

}  // namespace leo_test