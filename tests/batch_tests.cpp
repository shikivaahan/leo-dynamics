// Consumer-visible contract tests for leo::propagate_batch.
//
// These tests cover only what a caller can observe: output ordering, binary64
// bitwise equality against the unchanged single-trajectory propagate(), the
// error contract (failing trajectory identity plus the preserved underlying
// exception), continue-on-failure behaviour, worker-count edge cases, argument
// validation and independent per-trajectory mutable RHS state. No scientific
// baseline is touched here; the scientific comparison against the frozen
// CASE-A/B/C fixtures lives in docs/multithreading/validation.cpp.

#include <catch2/catch_test_macros.hpp>

#include "leo/integration/batch.hpp"
#include "leo/integration/propagate.hpp"
#include "leo/kernel/attitude.hpp"
#include "leo/kernel/spacecraft.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace leo;

namespace {

constexpr double pi = std::numbers::pi_v<double>;
constexpr Epoch kEpoch{120744064.18248665};

Inertia rotor_inertia(std::size_t index) {
    const double offset = 0.25 * static_cast<double>(index);
    Eigen::Vector3d diagonal{2.0 + offset, 3.0 + offset, 4.0 + offset};
    return Inertia(diagonal.asDiagonal());
}

BodyVector rotor_rate(std::size_t index) {
    return BodyVector{0.02 + 1e-3 * static_cast<double>(index),
                      0.01 - 1e-3 * static_cast<double>(index),
                      0.005 + 5e-4 * static_cast<double>(index)};
}

State sample_state(std::size_t index) {
    const double radius = 7.0e6 + 1.0e3 * static_cast<double>(index);
    const double speed = 7.5e3 + static_cast<double>(index);
    const double angle = 0.05 * static_cast<double>(index);
    return State{InertialVector{radius, 0.0, 0.0},
                 InertialVector{-speed * std::sin(angle), speed * std::cos(angle), 0.0},
                 normalize_quaternion(Quaternion{std::cos(0.5 * angle), std::sin(0.5 * angle), 0.0, 0.0}),
                 rotor_rate(index)};
}

// Harmonic translation plus torque-free rigid rotation: every state component,
// including the quaternion and the body rate, evolves, so a bitwise comparison
// covers all thirteen integrated doubles and not only the position. The period
// differs per trajectory, so a permuted result cannot go unnoticed.
class Rotor {
    double frequency_squared_;
    Inertia inertia_;
public:
    explicit Rotor(std::size_t index)
        : frequency_squared_(std::pow(2.0 * pi / (600.0 + 30.0 * static_cast<double>(index)), 2)),
          inertia_(rotor_inertia(index)) {}
    StateDerivative operator()(ElapsedTime, const State& state) const {
        return StateDerivative{state.v, state.r * (-frequency_squared_),
                               quaternion_derivative(state.q, state.omega),
                               torque_free_angular_acceleration(inertia_, state.omega)};
    }
};

// Move-only RHS carrying its own mutable state: proves the batch neither copies
// nor shares per-trajectory integrator-facing state. The private accumulator is
// move-only, so it can only be advanced by the one worker that owns the object,
// and the destructor publishes what it accumulated.
class CountingRotor {
    std::size_t index_;
    std::atomic<std::size_t>* calls_;
    std::unique_ptr<double> owned_;
    std::vector<double>* final_owned_;
public:
    CountingRotor(std::size_t index, std::atomic<std::size_t>* calls, std::vector<double>* final_owned = nullptr)
        : index_(index), calls_(calls), owned_(std::make_unique<double>(0.0)), final_owned_(final_owned) {}
    CountingRotor(const CountingRotor&) = delete;
    CountingRotor& operator=(const CountingRotor&) = delete;
    CountingRotor(CountingRotor&&) = default;
    CountingRotor& operator=(CountingRotor&&) = default;
    ~CountingRotor() {
        if (final_owned_ != nullptr) (*final_owned_)[index_] = *owned_;
    }
    std::size_t index() const noexcept { return index_; }
    double owned_value() const { return *owned_; }
    StateDerivative operator()(ElapsedTime t, const State& state) const {
        ++*calls_;
        *owned_ += 1.0;
        Rotor rotor(index_);
        return rotor(t, state);
    }
};

// Fails on a chosen output label, so the captured cause comes from the
// integrator's derivative evaluation rather than from object construction.
class FailingRotor {
    std::size_t index_;
    Rotor rotor_;
public:
    explicit FailingRotor(std::size_t index) : index_(index), rotor_(index) {}
    StateDerivative operator()(ElapsedTime t, const State& state) const {
        if (index_ == 3 && t.seconds == 43.0) throw std::overflow_error("rhs overflow");
        return rotor_(t, state);
    }
};

std::vector<double> output_labels() {
    // Six outputs, deliberately not a multiple of any worker count used below.
    return {0.0, 7.0, 19.0, 43.0, 61.0, 97.0};
}

IntegrationOptions test_options() {
    IntegrationOptions options;
    options.rtol = 1e-12;
    options.initial_step_s = 1.0;
    options.max_step_s = 20.0;
    return options;
}

std::vector<State> initial_states(std::size_t count) {
    std::vector<State> states;
    states.reserve(count);
    for (std::size_t index = 0; index < count; ++index) states.push_back(sample_state(index));
    return states;
}

std::vector<Trajectory> sequential_reference(const std::vector<State>& initials,
                                             const std::vector<double>& labels,
                                             const IntegrationOptions& options) {
    std::vector<Trajectory> trajectories;
    trajectories.reserve(initials.size());
    for (std::size_t index = 0; index < initials.size(); ++index)
        trajectories.push_back(propagate(Rotor(index), initials[index], kEpoch,
                                         std::span<const double>(labels), options));
    return trajectories;
}

std::vector<Trajectory> run_batch(const std::vector<State>& initials, const std::vector<double>& labels,
                                  const IntegrationOptions& options, std::size_t workers) {
    const auto factory = [](std::size_t index, const State&) { return Rotor(index); };
    return propagate_batch(factory, std::span<const State>(initials), kEpoch,
                           std::span<const double>(labels), options, workers);
}

std::uint64_t bits(double value) {
    std::uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof raw);
    return raw;
}

std::array<std::uint64_t, 13> bits(const State& state) {
    return {bits(state.r.x()), bits(state.r.y()), bits(state.r.z()),
            bits(state.v.x()), bits(state.v.y()), bits(state.v.z()),
            bits(state.q.w), bits(state.q.x), bits(state.q.y), bits(state.q.z),
            bits(state.omega.x()), bits(state.omega.y()), bits(state.omega.z())};
}

std::vector<std::uint64_t> bits(const Trajectory& trajectory) {
    std::vector<std::uint64_t> values{bits(trajectory.epoch.et_seconds)};
    values.reserve(1 + 14 * trajectory.samples.size());
    for (const Sample& sample : trajectory.samples) {
        values.push_back(bits(sample.t.seconds));
        const auto state = bits(sample.state);
        values.insert(values.end(), state.begin(), state.end());
    }
    return values;
}

// First difference found, in the caller's own terms. Used only for messages.
std::string describe_difference(const Trajectory& expected, const Trajectory& actual) {
    if (expected.epoch.et_seconds != actual.epoch.et_seconds)
        return "epoch.et_seconds differs: reference " + std::to_string(expected.epoch.et_seconds) +
               " batch " + std::to_string(actual.epoch.et_seconds);
    if (expected.samples.size() != actual.samples.size())
        return "sample count differs: reference " + std::to_string(expected.samples.size()) + " batch " +
               std::to_string(actual.samples.size());
    for (std::size_t index = 0; index < expected.samples.size(); ++index) {
        const Sample& reference = expected.samples[index];
        const Sample& batch = actual.samples[index];
        if (reference.t.seconds != batch.t.seconds)
            return "sample " + std::to_string(index) + " label differs: reference " +
                   std::to_string(reference.t.seconds) + " batch " + std::to_string(batch.t.seconds);
        if (bits(reference.state) != bits(batch.state))
            return "sample " + std::to_string(index) + " state is not bitwise equal";
    }
    return {};
}

}  // namespace

TEST_CASE("Batch output is bitwise identical to sequential propagation", "[batch][ordering]") {
    const std::vector<double> labels = output_labels();
    const IntegrationOptions options = test_options();
    const std::vector<State> initials = initial_states(7);
    const std::vector<Trajectory> reference = sequential_reference(initials, labels, options);
    REQUIRE(reference.size() == 7);

    const std::vector<Trajectory> batch = run_batch(initials, labels, options, 4);

    REQUIRE(batch.size() == initials.size());
    for (std::size_t index = 0; index < initials.size(); ++index) {
        CAPTURE(index);
        REQUIRE(batch[index].samples.size() == reference[index].samples.size());
        for (std::size_t sample = 0; sample < reference[index].samples.size(); ++sample) {
            CAPTURE(sample);
            REQUIRE(bits(batch[index].samples[sample].state) == bits(reference[index].samples[sample].state));
        }
        INFO(describe_difference(reference[index], batch[index]));
        REQUIRE(describe_difference(reference[index], batch[index]).empty());
    }
}

TEST_CASE("Batch results are ordered by input index, not by worker", "[batch][ordering]") {
    const std::vector<double> labels = output_labels();
    const IntegrationOptions options = test_options();
    const std::vector<State> initials = initial_states(5);
    const std::vector<Trajectory> reference = sequential_reference(initials, labels, options);
    const std::vector<Trajectory> batch = run_batch(initials, labels, options, 4);

    REQUIRE(batch.size() == initials.size());
    for (std::size_t index = 0; index < initials.size(); ++index) {
        REQUIRE(bits(batch[index]) == bits(reference[index]));
        for (std::size_t other = index + 1; other < initials.size(); ++other)
            REQUIRE(bits(batch[index]) != bits(batch[other]));
    }
}

TEST_CASE("Results do not depend on the worker count", "[batch][workers]") {
    const std::vector<double> labels = output_labels();
    const IntegrationOptions options = test_options();
    const std::vector<State> initials = initial_states(6);
    const std::vector<Trajectory> reference = sequential_reference(initials, labels, options);

    for (const std::size_t workers : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{5},
                                      std::size_t{6}, std::size_t{8}}) {
        const std::vector<Trajectory> batch = run_batch(initials, labels, options, workers);
        CAPTURE(workers);
        REQUIRE(batch.size() == reference.size());
        for (std::size_t index = 0; index < reference.size(); ++index)
            REQUIRE(bits(batch[index]) == bits(reference[index]));
    }
}

TEST_CASE("Empty, single-element and oversized worker batches behave", "[batch][edges]") {
    const std::vector<double> labels = output_labels();
    const IntegrationOptions options = test_options();

    SECTION("empty batch returns no trajectories") {
        for (const std::size_t workers : {std::size_t{1}, std::size_t{4}}) {
            const auto factory = [](std::size_t index, const State&) { return Rotor(index); };
            const std::vector<Trajectory> batch =
                propagate_batch(factory, std::span<const State>{}, kEpoch,
                                std::span<const double>(labels), options, workers);
            CAPTURE(workers);
            REQUIRE(batch.empty());
        }
    }

    SECTION("single trajectory matches sequential propagation") {
        const std::vector<State> initials = initial_states(1);
        const std::vector<Trajectory> reference = sequential_reference(initials, labels, options);
        for (const std::size_t workers : {std::size_t{1}, std::size_t{2}, std::size_t{9}}) {
            const std::vector<Trajectory> batch = run_batch(initials, labels, options, workers);
            CAPTURE(workers);
            REQUIRE(batch.size() == 1);
            REQUIRE(bits(batch.front()) == bits(reference.front()));
        }
    }

    SECTION("worker count greater than the trajectory count") {
        const std::vector<State> initials = initial_states(3);
        const std::vector<Trajectory> reference = sequential_reference(initials, labels, options);
        const std::vector<Trajectory> batch = run_batch(initials, labels, options, 16);
        REQUIRE(batch.size() == 3);
        for (std::size_t index = 0; index < 3; ++index)
            REQUIRE(bits(batch[index]) == bits(reference[index]));
    }
}

TEST_CASE("A zero worker count is rejected before any work", "[batch][validation]") {
    const std::vector<double> labels = output_labels();
    const IntegrationOptions options = test_options();
    const auto factory = [](std::size_t index, const State&) { return Rotor(index); };
    const std::vector<State> initials = initial_states(3);

    REQUIRE_THROWS_AS(propagate_batch(factory, std::span<const State>(initials), kEpoch,
                                      std::span<const double>(labels), options, 0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(propagate_batch(factory, std::span<const State>{}, kEpoch,
                                      std::span<const double>(labels), options, 0),
                      std::invalid_argument);
}

TEST_CASE("A factory failure identifies its trajectory and preserves the cause", "[batch][errors]") {
    const std::vector<double> labels = output_labels();
    const IntegrationOptions options = test_options();
    const std::vector<State> initials = initial_states(5);
    const auto factory = [](std::size_t index, const State&) -> Rotor {
        if (index == 2) throw std::runtime_error("factory refused trajectory 2");
        return Rotor(index);
    };

    try {
        const auto batch = propagate_batch(factory, std::span<const State>(initials), kEpoch,
                                           std::span<const double>(labels), options, 4);
        FAIL("expected BatchPropagationError, got " << batch.size() << " trajectories");
    } catch (const BatchPropagationError& error) {
        REQUIRE(error.trajectory_index() == 2);
        REQUIRE(error.cause() != nullptr);
        REQUIRE_THROWS_AS(std::rethrow_exception(error.cause()), std::runtime_error);
        try {
            std::rethrow_exception(error.cause());
            FAIL("cause did not rethrow");
        } catch (const std::runtime_error& cause) {
            REQUIRE(std::string(cause.what()) == "factory refused trajectory 2");
        }
    }
}

TEST_CASE("An RHS failure identifies its trajectory and preserves the cause", "[batch][errors]") {
    const std::vector<double> labels = output_labels();
    const IntegrationOptions options = test_options();
    const std::vector<State> initials = initial_states(5);
    const auto factory = [](std::size_t index, const State&) { return FailingRotor(index); };

    try {
        propagate_batch(factory, std::span<const State>(initials), kEpoch,
                        std::span<const double>(labels), options, 4);
        FAIL("expected BatchPropagationError");
    } catch (const BatchPropagationError& error) {
        REQUIRE(error.trajectory_index() == 3);
        REQUIRE_THROWS_AS(std::rethrow_exception(error.cause()), std::overflow_error);
        try {
            std::rethrow_exception(error.cause());
            FAIL("cause did not rethrow");
        } catch (const std::overflow_error& cause) {
            REQUIRE(std::string(cause.what()) == "rhs overflow");
        }
    }
}

TEST_CASE("Several failures report the lowest failing input index", "[batch][errors]") {
    const std::vector<double> labels = output_labels();
    const IntegrationOptions options = test_options();
    const std::vector<State> initials = initial_states(8);
    const auto factory = [](std::size_t index, const State&) -> Rotor {
        if (index == 1 || index == 4 || index == 7)
            throw std::runtime_error("factory refused " + std::to_string(index));
        return Rotor(index);
    };

    for (const std::size_t workers : {std::size_t{1}, std::size_t{3}, std::size_t{8}}) {
        try {
            propagate_batch(factory, std::span<const State>(initials), kEpoch,
                            std::span<const double>(labels), options, workers);
            FAIL("expected BatchPropagationError");
        } catch (const BatchPropagationError& error) {
            CAPTURE(workers);
            REQUIRE(error.trajectory_index() == 1);
            try {
                std::rethrow_exception(error.cause());
                FAIL("cause did not rethrow");
            } catch (const std::runtime_error& cause) {
                REQUIRE(std::string(cause.what()) == "factory refused 1");
            }
        }
    }
}

TEST_CASE("Every other trajectory runs to completion when one fails",
          "[batch][errors][cleanup]") {
    const std::vector<double> labels = output_labels();
    const IntegrationOptions options = test_options();
    const std::vector<State> initials = initial_states(6);

    std::vector<std::atomic<std::size_t>> batch_calls(initials.size());
    std::vector<std::atomic<std::size_t>> reference_calls(initials.size());
    for (auto& counter : batch_calls) counter.store(0);
    for (auto& counter : reference_calls) counter.store(0);
    std::atomic<std::size_t> factory_calls{0};

    // Sequential reference first, so every survivor has an expected RHS count.
    std::vector<Trajectory> reference;
    reference.reserve(initials.size());
    for (std::size_t index = 0; index < initials.size(); ++index) {
        if (index == 2) {
            reference.emplace_back();
            continue;
        }
        reference.push_back(propagate(CountingRotor{index, &reference_calls[index]}, initials[index], kEpoch,
                                      std::span<const double>(labels), options));
    }

    auto factory = [&](std::size_t index, const State&) {
        ++factory_calls;
        if (index == 2) throw std::runtime_error("factory refused trajectory 2");
        return CountingRotor{index, &batch_calls[index]};
    };

    try {
        propagate_batch(factory, std::span<const State>(initials), kEpoch,
                        std::span<const double>(labels), options, 4);
        FAIL("expected BatchPropagationError");
    } catch (const BatchPropagationError& error) {
        REQUIRE(error.trajectory_index() == 2);
    }

    // The failing trajectory still consumed its factory call, and no other
    // trajectory was abandoned or truncated: every survivor ran exactly as many
    // RHS evaluations as its own sequential run.
    REQUIRE(factory_calls.load() == initials.size());
    std::vector<std::size_t> counts_after_return(initials.size());
    for (std::size_t index = 0; index < initials.size(); ++index) {
        CAPTURE(index);
        counts_after_return[index] = batch_calls[index].load();
        if (index == 2) {
            REQUIRE(counts_after_return[index] == 0);
            continue;
        }
        const std::size_t expected = reference_calls[index].load();
        REQUIRE(expected > 0);
        REQUIRE(counts_after_return[index] == expected);
    }
    // Every worker is joined before the call returns: no counter may still move.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (std::size_t index = 0; index < initials.size(); ++index)
        REQUIRE(batch_calls[index].load() == counts_after_return[index]);
}

TEST_CASE("Invalid shared integration options surface as the lowest failing trajectory",
          "[batch][errors]") {
    const std::vector<double> labels = output_labels();
    IntegrationOptions options = test_options();
    options.max_step_s = 0.0; // rejected by propagate() itself
    const std::vector<State> initials = initial_states(3);
    const auto factory = [](std::size_t index, const State&) { return Rotor(index); };

    try {
        propagate_batch(factory, std::span<const State>(initials), kEpoch,
                        std::span<const double>(labels), options, 2);
        FAIL("expected BatchPropagationError");
    } catch (const BatchPropagationError& error) {
        REQUIRE(error.trajectory_index() == 0);
        REQUIRE_THROWS_AS(std::rethrow_exception(error.cause()), std::invalid_argument);
    }
}

TEST_CASE("Each trajectory gets independent move-only mutable RHS state", "[batch][state]") {
    const std::vector<double> labels = output_labels();
    const IntegrationOptions options = test_options();
    const std::vector<State> initials = initial_states(4);

    std::vector<std::atomic<std::size_t>> batch_calls(initials.size());
    std::vector<std::atomic<std::size_t>> reference_calls(initials.size());
    std::vector<double> batch_owned(initials.size(), -1.0);
    std::vector<double> reference_owned(initials.size(), -1.0);
    for (auto& counter : batch_calls) counter.store(0);
    for (auto& counter : reference_calls) counter.store(0);

    const auto factory = [&](std::size_t index, const State&) {
        return CountingRotor{index, &batch_calls[index], &batch_owned};
    };
    const std::vector<Trajectory> batch =
        propagate_batch(factory, std::span<const State>(initials), kEpoch,
                        std::span<const double>(labels), options, 4);

    std::vector<Trajectory> reference;
    reference.reserve(initials.size());
    for (std::size_t index = 0; index < initials.size(); ++index) {
        CountingRotor rotor{index, &reference_calls[index], &reference_owned};
        REQUIRE(rotor.index() == index);
        REQUIRE(rotor.owned_value() == 0.0);
        reference.push_back(propagate(rotor, initials[index], kEpoch,
                                      std::span<const double>(labels), options));
    }

    std::size_t batch_total = 0, reference_total = 0;
    for (std::size_t index = 0; index < initials.size(); ++index) {
        CAPTURE(index);
        REQUIRE(bits(batch[index]) == bits(reference[index]));
        REQUIRE(batch_calls[index].load() == reference_calls[index].load());
        REQUIRE(batch_calls[index].load() > 0);
        // Each move-only object accumulated exactly its own trajectory's calls.
        REQUIRE(batch_owned[index] == static_cast<double>(batch_calls[index].load()));
        REQUIRE(reference_owned[index] == batch_owned[index]);
        batch_total += batch_calls[index].load();
        reference_total += reference_calls[index].load();
    }
    // Independent per-trajectory counters: no trajectory's count was merged.
    REQUIRE(batch_total == reference_total);
}