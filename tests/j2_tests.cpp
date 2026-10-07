#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

#include "leo/integration/options.hpp"
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
#include "support/secular_fit.hpp"

using namespace leo;

namespace {

constexpr double pi = std::numbers::pi_v<double>;

constexpr double kDegreesToRadians = pi / 180.0;

// Plan fixtures: 14 days sampled every 60 s.
constexpr double kSpanSeconds = 14.0 * 24.0 * 3600.0;
constexpr double kSampleStepSeconds = 60.0;
constexpr double kBaselineMaxStepSeconds = 30.0;  // explicit baseline, not the default.
constexpr double kRepeatMaxStepSeconds = 15.0;

Spacecraft test_spacecraft() {
    return Spacecraft(10.0, Inertia(Eigen::Vector3d(1.0, 2.0, 3.0).asDiagonal()));
}

EarthDynamics j2_dynamics() {
    return EarthDynamics(EarthGravity(Earth{}, true), test_spacecraft());
}

// J2-augmented specific potential energy, J/kg:
// U = -mu/r (1 - J2 (Re/r)^2 * 1/2 (3 (z/r)^2 - 1)).
double j2_potential_energy(const Earth& earth, InertialVector r) {
    const double radius = norm(r);
    const double z_over_r = r.z() / radius;
    const double radius_ratio = earth.equatorial_radius_m / radius;
    const double bracket =
        1.0 - earth.j2 * radius_ratio * radius_ratio * 0.5 * (3.0 * z_over_r * z_over_r - 1.0);
    return -earth.mu_m3_s2 / radius * bracket;
}

// Total specific energy under the J2 potential; the orbit is conservative, so
// this must not drift even though the full angular momentum vector does.
double j2_total_specific_energy(const Earth& earth, const State& state) {
    return 0.5 * dot(state.v, state.v) + j2_potential_energy(earth, state.r);
}

std::vector<double> uniform_samples(double step_s, double span_s) {
    const int intervals = static_cast<int>(std::llround(span_s / step_s));
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(intervals) + 1);
    for (int index = 0; index <= intervals; ++index) times.push_back(index * step_s);
    return times;
}

ClassicalElements leo_elements(double e, double inclination_deg) {
    const Earth earth{};
    return ClassicalElements{earth.equatorial_radius_m + 800000.0,
                             e,
                             inclination_deg * kDegreesToRadians,
                             30.0 * kDegreesToRadians,
                             40.0 * kDegreesToRadians,
                             0.0};
}

Trajectory propagate_j2_fixture(const ClassicalElements& elements, double max_step_s,
                                const std::vector<double>& times) {
    const Earth earth{};
    const CartesianOrbit initial = coe_to_cartesian(elements, earth.mu_m3_s2);
    return propagate(j2_dynamics(), leo_test::identity_attitude_state(initial), Epoch{0.0},
                     std::span<const double>(times),
                     IntegrationOptions{1e-10, 1e-3, 1e-6, 1e-12, 1e-12, 1.0, max_step_s, 1000000});
}

// Fit a secular drift over the retained part of a trajectory, verifying the
// fit data is finite and that the full requested sample grid came back.
leo_test::LinearFit fit_secular_angle(const Trajectory& trajectory, const Earth& earth, double a_m,
                                      leo_test::ElementAngle angle) {
    const double discard = leo_test::orbital_period_s(earth.mu_m3_s2, a_m);
    std::vector<double> fit_times;
    const std::vector<double> unwrapped =
        leo_test::osculating_angle_series(trajectory, earth.mu_m3_s2, angle, discard, &fit_times);
    REQUIRE(fit_times.size() == unwrapped.size());
    REQUIRE(fit_times.back()-fit_times.front()>13.0*86400.0);
    for (const double value : unwrapped) REQUIRE(std::isfinite(value));
    const leo_test::LinearFit fit = leo_test::fit_line(fit_times, unwrapped);
    REQUIRE(std::isfinite(fit.slope));
    REQUIRE(std::isfinite(fit.max_residual));
    return fit;
}

}  // namespace

TEST_CASE("J2 nodal rates, step convergence and conserved quantities", "[j2]") {
    const Earth earth{};
    const auto times=uniform_samples(kSampleStepSeconds,kSpanSeconds);
    for (double inclination_deg:{60.0,120.0}) {
        const auto elements=leo_elements(.01,inclination_deg);
        const auto baseline=propagate_j2_fixture(elements,kBaselineMaxStepSeconds,times);
        const auto repeat=propagate_j2_fixture(elements,kRepeatMaxStepSeconds,times);
        const auto fit30=fit_secular_angle(baseline,earth,elements.a_m,
            leo_test::ElementAngle::kRightAscensionOfAscendingNode);
        const auto fit15=fit_secular_angle(repeat,earth,elements.a_m,
            leo_test::ElementAngle::kRightAscensionOfAscendingNode);
        const double expected=leo_test::nodal_regression_rate_rad_s(earth,earth.mu_m3_s2,
            elements.a_m,elements.e,elements.i_rad);
        CAPTURE(inclination_deg,fit30.slope,fit15.slope,expected,fit30.max_residual,fit15.max_residual);
        REQUIRE(leo_test::relative_drift(fit30.slope,expected)<=.01);
        REQUIRE(leo_test::relative_drift(fit15.slope,expected)<=.01);
        REQUIRE(leo_test::relative_drift(fit15.slope,fit30.slope)<.001);
        if (inclination_deg<90) REQUIRE(fit15.slope<0);
        else REQUIRE(fit15.slope>0);
        // Conserved quantities use the approved 15 s default accuracy.
        const double reference=j2_total_specific_energy(earth,repeat.samples.front().state);
        double worst_energy=0;
        for (const auto& sample:repeat.samples) {
            const double energy=j2_total_specific_energy(earth,sample.state);
            REQUIRE(std::isfinite(energy));
            worst_energy=std::max(worst_energy,leo_test::relative_drift(energy,reference));
        }
        const double hz_drift=leo_test::max_relative_angular_momentum_z_drift(repeat);
        CAPTURE(worst_energy,hz_drift);
        REQUIRE(worst_energy<=1e-8);
        REQUIRE(hz_drift<=1e-8);
    }
}

TEST_CASE("J2 apsidal regression matches the first-order analytic rate", "[j2]") {
    const Earth earth{};
    const std::vector<double> times = uniform_samples(kSampleStepSeconds, kSpanSeconds);
    const ClassicalElements elements = leo_elements(0.03, 40.0);

    const leo_test::LinearFit fit =
        fit_secular_angle(propagate_j2_fixture(elements, kRepeatMaxStepSeconds, times), earth,
                          elements.a_m, leo_test::ElementAngle::kArgumentOfPeriapsis);
    const double expected =
        leo_test::apsidal_regression_rate_rad_s(earth, earth.mu_m3_s2, elements.a_m, 0.03, 40.0 * kDegreesToRadians);

    INFO("fitted argp rate " << fit.slope << " rad/s, expected " << expected << " rad/s, max residual "
                            << fit.max_residual << " rad");
    REQUIRE(fit.slope > 0.0);
    REQUIRE(leo_test::relative_drift(fit.slope, expected) <= 0.03);
}

TEST_CASE("J2 apsidal regression vanishes at the critical inclination", "[j2]") {
    const Earth earth{};
    const std::vector<double> times = uniform_samples(kSampleStepSeconds, kSpanSeconds);
    const double critical_inclination = std::acos(std::sqrt(1.0 / 5.0));
    const ClassicalElements elements = leo_elements(0.03, critical_inclination * 180.0 / pi);

    const leo_test::LinearFit fit =
        fit_secular_angle(propagate_j2_fixture(elements, kRepeatMaxStepSeconds, times), earth,
                          elements.a_m, leo_test::ElementAngle::kArgumentOfPeriapsis);
    const double secular_scale =
        0.75 * leo_test::mean_motion_rad_s(earth.mu_m3_s2, elements.a_m) * earth.j2 *
        std::pow(earth.equatorial_radius_m / (elements.a_m * (1.0 - 0.03 * 0.03)), 2.0);

    INFO("critical-inclination argp rate " << fit.slope << " rad/s, scale " << secular_scale << " rad/s, max residual "
                                         << fit.max_residual << " rad");
    REQUIRE(std::abs(fit.slope) <= 0.03 * secular_scale);
}