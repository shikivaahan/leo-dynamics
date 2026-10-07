#include <catch2/catch_test_macros.hpp>
#include "leo/integration/propagate.hpp"
#include "leo/integration/trajectory.hpp"
#include "leo/kernel/dynamics.hpp"
#include "leo/kernel/orbital.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

using namespace leo;

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

// Sign-invariant chord metric: 4*asin(min(1, min(|qr-q|, |qr+q|)/2)).
double quaternion_error(Quaternion reference, Quaternion actual) {
    const Quaternion a = normalize_quaternion(reference);
    const Quaternion b = normalize_quaternion(actual);
    const auto length = [](double aw, double ax, double ay, double az) {
        return std::sqrt(aw * aw + ax * ax + ay * ay + az * az);
    };
    const double difference = length(a.w - b.w, a.x - b.x, a.y - b.y, a.z - b.z);
    const double sum = length(a.w + b.w, a.x + b.x, a.y + b.y, a.z + b.z);
    return 4.0 * std::asin(std::min(1.0, std::min(difference, sum) / 2.0));
}

double quaternion_norm_error(Quaternion q) {
    return std::abs(std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z) - 1.0);
}

Inertia diagonal_inertia() { return Inertia(Eigen::Vector3d(1.0, 2.0, 3.0).asDiagonal()); }

Inertia spd_inertia() {
    Eigen::Matrix3d m;
    m << 2.0, 0.2, 0.1, 0.2, 3.0, 0.3, 0.1, 0.3, 4.0;
    return Inertia(m);
}

CartesianOrbit circular_orbit(double altitude_m) {
    const Earth earth;
    const double a = earth.equatorial_radius_m + altitude_m;
    return coe_to_cartesian(ClassicalElements{a, 0.0, 0.0, 0.0, 0.0, 0.0}, earth.mu_m3_s2);
}

State leo_state(Quaternion q, BodyVector omega) {
    const CartesianOrbit orbit = circular_orbit(400000.0);
    return State{orbit.r, orbit.v, q, omega};
}

EarthDynamics point_mass_dynamics(Inertia inertia) {
    return EarthDynamics(EarthGravity(Earth{}, false), Spacecraft(10.0, inertia));
}

Trajectory run(const State& state, const std::vector<double>& times,
               const IntegrationOptions& options = {}) {
    return propagate(point_mass_dynamics(diagonal_inertia()), state, Epoch{0.0},
                     std::span<const double>(times.data(), times.size()), options);
}

// Stable decay in each field: an oversized initial step forces rejections.
// Reusing a rejected endpoint derivative corrupts the closed-form solution.
struct ExponentialDecay {
    double lambda;
    StateDerivative operator()(ElapsedTime, const State& s) const {
        return StateDerivative{s.r*(-lambda),s.v*(-lambda),
                               Quaternion{0, 0, 0, 0}, BodyVector{0, 0, 0}};
    }
};

// Non-autonomous oracle: v_dot = (0, 0, -t), integrated in closed form as
// r_z(t) = r_z0 + v_z0 t - t^3/6.
struct CubicDeceleration {
    StateDerivative operator()(ElapsedTime t, const State& s) const {
        return StateDerivative{s.v, InertialVector{0, 0, -t.seconds}, Quaternion{0, 0, 0, 0},
                               BodyVector{0, 0, 0}};
    }
};

struct MarkerError : std::logic_error {
    using std::logic_error::logic_error;
};

}  // namespace

TEST_CASE("Requested sample times are labelled exactly", "[integration]") {
    const std::vector<double> times{0.0, 1.0, 2.5, 10.0};
    const State initial = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0, 0});
    const Trajectory trajectory = run(initial, times);

    REQUIRE(trajectory.samples.size() == times.size());
    for (std::size_t i = 0; i < times.size(); ++i) {
        REQUIRE(trajectory.samples[i].t.seconds == times[i]);
        REQUIRE(trajectory.samples[i].state.q.w == 1.0);
        REQUIRE(trajectory.epoch.et_seconds == 0.0);
    }
    const State& first = trajectory.samples.front().state;
    REQUIRE(norm(first.r - initial.r) == 0.0);
    REQUIRE(norm(first.v - initial.v) == 0.0);
}

TEST_CASE("A single requested sample returns the validated initial state", "[integration]") {
    const State initial = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0, 0});
    const Trajectory trajectory = run(initial, std::vector<double>{0.0});
    REQUIRE(trajectory.samples.size() == 1);
    const State& sample = trajectory.samples.front().state;
    REQUIRE(trajectory.samples.front().t.seconds == 0.0);
    REQUIRE(norm(sample.r - initial.r) == 0.0);
    REQUIRE(norm(sample.v - initial.v) == 0.0);
}

TEST_CASE("Caller-invalid sample times are rejected", "[integration]") {
    const State initial = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0, 0});
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const std::vector<std::vector<double>> invalid{
        {},          {1.0},        {0.0, 2.0, 1.0}, {0.0, 1.0, 1.0}, {-1.0},
        {0.0, nan},  {0.0, inf},   {-0.0, -1.0},   {1.0, 0.0},
    };
    for (const auto& times : invalid) {
        CAPTURE(times.size());
        REQUIRE_THROWS_AS(run(initial, times), std::invalid_argument);
    }
}

TEST_CASE("Caller-invalid state and epoch are rejected", "[integration]") {
    const std::vector<double> times{0.0, 1.0};
    State nonfinite = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0, 0});
    nonfinite.v = InertialVector{std::numeric_limits<double>::quiet_NaN(), 0, 0};
    REQUIRE_THROWS_AS(run(nonfinite, times), std::invalid_argument);

    State tiny = leo_state(Quaternion{1e-13, 0, 0, 0}, BodyVector{0, 0, 0});
    REQUIRE_THROWS_AS(run(tiny, times), std::invalid_argument);

    State bad_rate = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0,
                                                                  std::numeric_limits<double>::infinity()});
    REQUIRE_THROWS_AS(run(bad_rate, times), std::invalid_argument);

    const State initial = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0, 0});
    REQUIRE_THROWS_AS(propagate(point_mass_dynamics(diagonal_inertia()), initial,
                                Epoch{std::numeric_limits<double>::quiet_NaN()},
                                std::span<const double>(times.data(), times.size())),
                      std::invalid_argument);
}

TEST_CASE("Caller-invalid options are rejected", "[integration]") {
    const State initial = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0, 0});
    const std::vector<double> times{0.0, 1.0};
    const double nan = std::numeric_limits<double>::quiet_NaN();

    IntegrationOptions options;
    options.rtol = 0.0;
    REQUIRE_THROWS_AS(run(initial, times, options), std::invalid_argument);

    options = IntegrationOptions{};
    options.position_atol_m = -1.0;
    REQUIRE_THROWS_AS(run(initial, times, options), std::invalid_argument);

    options = IntegrationOptions{};
    options.velocity_atol_m_s = nan;
    REQUIRE_THROWS_AS(run(initial, times, options), std::invalid_argument);

    options = IntegrationOptions{};
    options.quaternion_atol = 0.0;
    REQUIRE_THROWS_AS(run(initial, times, options), std::invalid_argument);

    options = IntegrationOptions{};
    options.omega_atol_rad_s = -1e-12;
    REQUIRE_THROWS_AS(run(initial, times, options), std::invalid_argument);

    options = IntegrationOptions{};
    options.initial_step_s = 0.0;
    REQUIRE_THROWS_AS(run(initial, times, options), std::invalid_argument);

    options = IntegrationOptions{};
    options.max_step_s = 0.5;  // below default initial_step_s of 1 s
    REQUIRE_THROWS_AS(run(initial, times, options), std::invalid_argument);

    options = IntegrationOptions{};
    options.max_step_s = nan;
    REQUIRE_THROWS_AS(run(initial, times, options), std::invalid_argument);

    options = IntegrationOptions{};
    options.max_attempts = 0;
    REQUIRE_THROWS_AS(run(initial, times, options), std::invalid_argument);
}

TEST_CASE("Attempt exhaustion fails without a partial trajectory", "[integration]") {
    const State initial = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0, 0});
    const double period = kTwoPi * std::sqrt(std::pow(initial.r.x(), 3.0) / Earth{}.mu_m3_s2);
    const std::vector<double> times{0.0, period};
    IntegrationOptions options;
    options.max_attempts = 1;
    REQUIRE_THROWS_AS(run(initial, times, options), std::runtime_error);
}

TEST_CASE("Nonfinite stage derivative is a detected step failure", "[integration]") {
    const State initial = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0, 0});
    const auto rhs = [](ElapsedTime t, const State&) {
        return StateDerivative{InertialVector{t.seconds==0?0:std::numeric_limits<double>::quiet_NaN(), 0, 0},
                               InertialVector{0, 0, 0}, Quaternion{0, 0, 0, 0}, BodyVector{0, 0, 0}};
    };
    const std::vector<double> times{0.0, 10.0};
    REQUIRE_THROWS_AS(propagate(rhs, initial, Epoch{0.0},
                                std::span<const double>(times.data(), times.size())),
                      std::runtime_error);
}

TEST_CASE("A near-zero trial quaternion is a detected step failure", "[integration]") {
    // Linear raw q_w reaches zero exactly in a single accepted step.
    // No physical RHS input validation can mask the projection failure.
    const auto rhs = [](ElapsedTime, const State& s) {
        return StateDerivative{s.v, InertialVector{0, 0, 0},
                               Quaternion{-1, 0, 0, 0}, BodyVector{0, 0, 0}};
    };
    const State initial{circular_orbit(400000.0).r, circular_orbit(400000.0).v, Quaternion{1, 0, 0, 0},
                        BodyVector{0, 0, 0}};
    const std::vector<double> times{0.0, 1.0};
    REQUIRE_THROWS_AS(propagate(rhs, initial, Epoch{0.0},
                                std::span<const double>(times.data(), times.size())),
                      std::runtime_error);
}

TEST_CASE("A near-zero quaternion also fails on a rejected trial", "[integration]") {
    const auto rhs=[](ElapsedTime,const State& s) -> StateDerivative {
        return {s.r*(-10),{},{-1,0,0,0},{}};
    };
    const State initial{{1e6,0,0},{},{1,0,0,0},{}};
    const double times[]{0,1};
    REQUIRE_THROWS_AS(propagate(rhs,initial,Epoch{0},times),std::runtime_error);
}

TEST_CASE("RHS exceptions propagate unchanged", "[integration]") {
    const State initial = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0, 0});
    const auto rhs = [](ElapsedTime t, const State&) -> StateDerivative {
        if(t.seconds>0) throw MarkerError("user rhs failure");
        return {{},{},{0,0,0,0},{}};
    };
    const std::vector<double> times{0.0, 10.0};
    REQUIRE_THROWS_AS(propagate(rhs, initial, Epoch{0.0},
                                std::span<const double>(times.data(), times.size())),
                      MarkerError);
}

TEST_CASE("Rejected steps below time resolution fail explicitly", "[integration]") {
    const State initial{{},{},{1,0,0,0},{}};
    const auto rhs=[](ElapsedTime t,const State&) -> StateDerivative {
        return {{t.seconds<=1?0:1e10,0,0},{},{0,0,0,0},{}};
    };
    const std::vector<double> times{0,1,std::nextafter(1.0,2.0)};
    IntegrationOptions options;
    options.position_atol_m=1e-30;
    REQUIRE_THROWS_AS(propagate(rhs,initial,Epoch{0},times,options),std::runtime_error);
}

TEST_CASE("Rejections converge on the exponential-decay oracle", "[integration]") {
    const double lambda = 20.0;
    const double r0 = 7.0e6;
    State initial;
    initial.r = InertialVector{r0, 0, 0};
    initial.v = InertialVector{-lambda * r0, 0, 0};
    initial.q = Quaternion{1, 0, 0, 0};
    initial.omega = BodyVector{0, 0, 0};

    IntegrationOptions options;
    options.position_atol_m = 1.0e-9;
    options.velocity_atol_m_s = 1.0e-9;
    options.max_step_s = 60.0;
    options.initial_step_s = 60.0;  // forces rejection of the first attempt

    const std::vector<double> times{0.0, 0.1, 0.25, 0.5, 0.75, 1.0};
    const Trajectory trajectory =
        propagate(ExponentialDecay{lambda}, initial, Epoch{0.0},
                  std::span<const double>(times.data(), times.size()), options);

    REQUIRE(trajectory.samples.size() == times.size());
    for (std::size_t i = 0; i < times.size(); ++i) {
        CAPTURE(times[i]);
        const double decay = std::exp(-lambda * times[i]);
        REQUIRE(trajectory.samples[i].t.seconds == times[i]);
        REQUIRE(std::abs(trajectory.samples[i].state.r.x() - r0 * decay) <=
                options.position_atol_m + 1.0e-7 * std::abs(r0 * decay));
        REQUIRE(std::abs(trajectory.samples[i].state.r.y()) <=
                options.position_atol_m + 1.0e-7 * r0 * decay);
        REQUIRE(std::abs(trajectory.samples[i].state.v.x() + lambda * r0 * decay) <=
                options.velocity_atol_m_s + 1.0e-7 * lambda * r0 * decay);
    }
}

TEST_CASE("Non-autonomous RHS is evaluated at the exact requested times", "[integration]") {
    State initial;
    initial.r = InertialVector{0, 0, 1000.0};
    initial.v = InertialVector{0, 0, 0};
    initial.q = Quaternion{1, 0, 0, 0};
    initial.omega = BodyVector{0, 0, 0};

    IntegrationOptions options;
    options.position_atol_m = 1.0e-9;
    options.velocity_atol_m_s = 1.0e-12;

    const std::vector<double> times{0.0, 1.0, 2.5, 10.0};
    const Trajectory trajectory = propagate(CubicDeceleration{}, initial, Epoch{0.0},
                                            std::span<const double>(times.data(), times.size()),
                                            options);

    for (std::size_t i = 0; i < times.size(); ++i) {
        CAPTURE(times[i]);
        const double t = times[i];
        REQUIRE(trajectory.samples[i].t.seconds == t);
        REQUIRE(std::abs(trajectory.samples[i].state.r.z() - (1000.0 - t * t * t / 6.0)) <= 1.0e-6);
        REQUIRE(std::abs(trajectory.samples[i].state.v.z() + t * t / 2.0) <= 1.0e-9);
    }
}

TEST_CASE("Zero attitude stays fixed under EarthDynamics", "[integration]") {
    const State initial = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0, 0});
    std::vector<double> times;
    for (double t = 0.0; t <= 1000.0; t += 5.0) times.push_back(t);

    const Trajectory trajectory = run(initial, times);
    REQUIRE(trajectory.samples.size() == times.size());
    for (std::size_t i = 0; i < times.size(); ++i) {
        CAPTURE(times[i]);
        const State& s = trajectory.samples[i].state;
        REQUIRE(quaternion_norm_error(s.q) <= 1e-14);
        REQUIRE(quaternion_error(Quaternion{1, 0, 0, 0}, s.q) <= 1e-12);
        REQUIRE(norm(s.omega) == 0.0);
    }
}

TEST_CASE("Principal-axis rotation with an unnormalized initial quaternion", "[integration]") {
    // q = (2,0,0,0) must be normalized once at input; the evolved attitude then
    // follows the exact principal-axis solution for omega_z = 0.1 rad/s. The
    // per-step projection on q changes the trial, so a stale endpoint
    // derivative would show up as a growing angular error.
    const double rate = 0.1;
    const State initial = leo_state(Quaternion{2, 0, 0, 0}, BodyVector{0, 0, rate});
    std::vector<double> times{0.0, 1.0, 2.5};
    for (double t = 5.0; t <= 1000.0; t += 5.0) times.push_back(t);

    const Trajectory trajectory = run(initial, times);
    REQUIRE(trajectory.samples.front().state.q.w == 1.0);
    for (std::size_t i = 0; i < trajectory.samples.size(); ++i) {
        CAPTURE(times[i]);
        const State& s = trajectory.samples[i].state;
        const double half = 0.5 * rate * times[i];
        const Quaternion expected{std::cos(half), 0, 0, std::sin(half)};
        REQUIRE(quaternion_error(expected, s.q) <= 1e-7);
        REQUIRE(quaternion_norm_error(s.q) <= 1e-14);
        REQUIRE(std::abs(s.omega.z() - rate) <= 1e-14);
        REQUIRE(std::abs(s.omega.x()) <= 1e-15);
        REQUIRE(std::abs(s.omega.y()) <= 1e-15);
    }
}

TEST_CASE("Constant body rate about a principal axis stays analytic", "[integration]") {
    const double rate = 0.01;
    const State initial = leo_state(Quaternion{1, 0, 0, 0}, BodyVector{0, 0, rate});
    std::vector<double> times;
    for (double t = 0.0; t <= 1000.0; t += 5.0) times.push_back(t);

    const Trajectory trajectory = run(initial, times);
    for (std::size_t i = 0; i < trajectory.samples.size(); ++i) {
        CAPTURE(times[i]);
        const State& s = trajectory.samples[i].state;
        const double half = 0.5 * rate * times[i];
        REQUIRE(quaternion_error(Quaternion{std::cos(half), 0, 0, std::sin(half)}, s.q) <= 1e-8);
        REQUIRE(quaternion_norm_error(s.q) <= 1e-14);
        REQUIRE(std::abs(s.omega.z() - rate) <= 1e-14);
    }
}

TEST_CASE("General inertia torque-free motion conserves energy and angular momentum",
          "[integration]") {
    const Inertia inertia = spd_inertia();
    const State initial = leo_state(normalize_quaternion(Quaternion{1.0, 0.2, -0.3, 0.4}),
                                    BodyVector{0.01, 0.02, 0.03});

    IntegrationOptions options;
    options.max_step_s = 1.0;
    options.initial_step_s = 1.0;

    std::vector<double> times;
    for (double t = 0.0; t <= 1000.0; t += 10.0) times.push_back(t);

    const Trajectory trajectory = propagate(point_mass_dynamics(inertia), initial, Epoch{0.0},
                                            std::span<const double>(times.data(), times.size()),
                                            options);

    const auto energy = [&](const State& s) {
        return 0.5 * dot(s.omega, inertia.multiply(s.omega));
    };
    const auto momentum = [&](const State& s) {
        return body_to_inertial(s.q, inertia.multiply(s.omega));
    };

    const double energy0 = energy(initial);
    const Eigen::Vector3d momentum0 = momentum(initial).as_eigen();

    double energy_drift = 0.0;
    double momentum_drift = 0.0;
    for (std::size_t i = 0; i < trajectory.samples.size(); ++i) {
        CAPTURE(times[i]);
        const State& s = trajectory.samples[i].state;
        REQUIRE(trajectory.samples[i].t.seconds == times[i]);
        REQUIRE(quaternion_norm_error(s.q) <= 1e-14);
        energy_drift = std::max(energy_drift, std::abs(energy(s) - energy0) / energy0);
        momentum_drift =
            std::max(momentum_drift, (momentum(s).as_eigen() - momentum0).norm() / momentum0.norm());
    }
    REQUIRE(energy_drift <= 1e-9);
    REQUIRE(momentum_drift <= 1e-8);
}
