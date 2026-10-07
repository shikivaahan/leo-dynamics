#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

#include "leo/integration/propagate.hpp"
#include "leo/kernel/dynamics.hpp"
#include "leo/kernel/earth.hpp"
#include "leo/kernel/frames.hpp"
#include "leo/kernel/gravity.hpp"
#include "leo/kernel/orbital.hpp"
#include "leo/kernel/spacecraft.hpp"
#include "leo/kernel/state.hpp"
#include "leo/kernel/time.hpp"
#include "support/analytical_orbits.hpp"
#include "support/invariants.hpp"

using namespace leo;

namespace {

constexpr double pi = std::numbers::pi_v<double>;

// Point-mass Earth dynamics with a rigid spacecraft; attitude is inert here.
Spacecraft test_spacecraft() { return Spacecraft(10.0, Inertia(Eigen::Vector3d(1.0, 2.0, 3.0).asDiagonal())); }

EarthDynamics point_mass_dynamics() {
    return EarthDynamics(EarthGravity(Earth{}, false), test_spacecraft());
}

std::vector<double> uniform_samples(double step_s, int intervals) {
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(intervals) + 1);
    for (int index = 0; index <= intervals; ++index) times.push_back(index * step_s);
    return times;
}

}  // namespace

TEST_CASE("Circular two-body orbit tracks exact analytic motion", "[two_body]") {
    const double mu = Earth{}.mu_m3_s2;
    const double radius_m = 7e6;
    const leo_test::EllipticOrbit orbit = leo_test::circular_orbit(radius_m, 0.0, 0.0, mu);
    const double period = leo_test::period_s(orbit);
    const double mean_motion = leo_test::mean_motion_rad_s(orbit);

    // Ten periods sampled at T/100, i.e. exactly the plan's output interval.
    const std::vector<double> times = uniform_samples(period / 100.0, 1000);
    const Trajectory trajectory = propagate(point_mass_dynamics(),
                                            leo_test::identity_attitude_state(leo_test::kepler_state_at(orbit, 0.0)),
                                            Epoch{0.0}, std::span<const double>(times), {});
    REQUIRE(trajectory.samples.size() == times.size());

    double worst_position = 0.0;
    double worst_velocity = 0.0;
    double worst_radius = 0.0;
    double worst_phase = 0.0;
    std::vector<double> angles;
    angles.reserve(trajectory.samples.size());
    for (std::size_t index = 0; index < trajectory.samples.size(); ++index) {
        const Sample& sample = trajectory.samples[index];
        const CartesianOrbit reference = leo_test::kepler_state_at(orbit, times[index]);
        const double t = times[index];
        INFO("sample index " << index << " at t = " << t);
        REQUIRE(sample.t.seconds == t);
        REQUIRE(std::abs(sample.state.q.w - 1.0) <= 1e-14);
        REQUIRE(std::abs(sample.state.q.x) <= 1e-14);
        REQUIRE(std::abs(sample.state.q.y) <= 1e-14);
        REQUIRE(std::abs(sample.state.q.z) <= 1e-14);
        REQUIRE(norm(sample.state.omega) <= 1e-15);
        worst_position = std::max(worst_position, norm(sample.state.r - reference.r));
        worst_velocity = std::max(worst_velocity, norm(sample.state.v - reference.v));
        worst_radius = std::max(worst_radius, std::abs(norm(sample.state.r) - radius_m));
        worst_phase = std::max(worst_phase, std::abs(std::remainder(std::atan2(sample.state.r.y(), sample.state.r.x()) - mean_motion * t, 2.0 * pi)));
        angles.push_back(std::atan2(sample.state.r.y(), sample.state.r.x()));
    }
    CAPTURE(worst_position, worst_velocity, worst_radius, worst_phase);
    REQUIRE(worst_radius <= 1.0);
    REQUIRE(worst_position <= 1.0);
    REQUIRE(worst_velocity <= 1e-3);
    REQUIRE(worst_phase <= 2e-7);

    REQUIRE(leo_test::max_relative_energy_drift(trajectory, mu) <= 1e-9);
    REQUIRE(leo_test::max_relative_angular_momentum_drift(trajectory) <= 1e-9);
    REQUIRE(leo_test::max_quaternion_norm_error(trajectory) <= 1e-14);

    // Mean motion measured from unwrapped in-plane angle, 2*pi crossings only.
    std::vector<double> unwrapped = leo_test::unwrapped_angles(angles);
    for (std::size_t index = 1; index < unwrapped.size(); ++index) REQUIRE(unwrapped[index] > unwrapped[index - 1]);
    const std::vector<double> crossings = leo_test::upward_crossing_times(times, unwrapped, 2.0 * pi);
    const double measured_period = leo_test::mean_interval_s(crossings);
    REQUIRE(std::abs(measured_period - period) <= 1e-3);
    CAPTURE(crossings.front(), crossings.back(), measured_period, period);
    REQUIRE(std::abs(crossings.front()-period)<=1e-3);
}

TEST_CASE("Elliptic two-body orbit matches Keplerian motion over ten periods", "[two_body]") {
    const Earth earth;
    const double mu = earth.mu_m3_s2;
    const double periapsis_m = earth.equatorial_radius_m + 400000.0;
    const double apoapsis_m = earth.equatorial_radius_m + 900000.0;
    const double a_m = 0.5 * (periapsis_m + apoapsis_m);
    const double e = (apoapsis_m - periapsis_m) / (apoapsis_m + periapsis_m);
    const double i_rad = 40.0 * pi / 180.0;
    const double raan_rad = 20.0 * pi / 180.0;
    const double argp_rad = 30.0 * pi / 180.0;
    const leo_test::EllipticOrbit orbit{a_m, e, i_rad, raan_rad, argp_rad, mu};
    const double period = leo_test::period_s(orbit);

    // T/10 sampling places every half period explicitly on a requested sample.
    const std::vector<double> times = uniform_samples(period / 10.0, 100);
    const Trajectory trajectory = propagate(point_mass_dynamics(),
                                            leo_test::identity_attitude_state(leo_test::kepler_state_at(orbit, 0.0)),
                                            Epoch{0.0}, std::span<const double>(times), {});
    REQUIRE(trajectory.samples.size() == times.size());

    double worst_position = 0.0;
    double worst_velocity = 0.0;
    double worst_periapsis = std::numeric_limits<double>::infinity();
    double worst_apoapsis = 0.0;
    double worst_a = 0.0;
    double worst_e = 0.0;
    for (std::size_t index = 0; index < trajectory.samples.size(); ++index) {
        const Sample& sample = trajectory.samples[index];
        const CartesianOrbit reference = leo_test::kepler_state_at(orbit, times[index]);
        INFO("sample index " << index << " at t = " << times[index]);
        REQUIRE(sample.t.seconds == times[index]);
        const Quaternion& q = sample.state.q;
        REQUIRE(std::abs(std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z) - 1.0) <= 1e-14);
        worst_position = std::max(worst_position, norm(sample.state.r - reference.r));
        worst_velocity = std::max(worst_velocity, norm(sample.state.v - reference.v));
        worst_periapsis = std::min(worst_periapsis, norm(sample.state.r));
        worst_apoapsis = std::max(worst_apoapsis, norm(sample.state.r));
        const ClassicalElements osculating = cartesian_to_coe({sample.state.r, sample.state.v}, mu);
        worst_a = std::max(worst_a, std::abs(osculating.a_m / a_m - 1.0));
        worst_e = std::max(worst_e, std::abs(osculating.e - e));
    }
    CAPTURE(worst_position, worst_velocity, worst_periapsis, worst_apoapsis, worst_a, worst_e);
    REQUIRE(std::abs(worst_periapsis - periapsis_m) <= 1.0);
    REQUIRE(std::abs(worst_apoapsis - apoapsis_m) <= 1.0);
    REQUIRE(worst_position <= 2.0);
    REQUIRE(worst_velocity <= 2e-3);
    REQUIRE(worst_a <= 1e-9);
    REQUIRE(worst_e <= 1e-9);

    REQUIRE(leo_test::max_relative_energy_drift(trajectory, mu) <= 1e-9);
    REQUIRE(leo_test::max_relative_angular_momentum_drift(trajectory) <= 1e-9);
    REQUIRE(leo_test::max_quaternion_norm_error(trajectory) <= 1e-14);
}