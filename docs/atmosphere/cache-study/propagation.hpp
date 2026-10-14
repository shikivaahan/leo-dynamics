// ATM-CACHE-001 — phases 3 (locality), 6 (coupled propagation sensitivity) and
// 7 (end-to-end performance).
//
// Included after support.hpp, so Config, Environment, Json, Distribution,
// Checksum, case_dynamics, tolerances(), matched_specific_energy(),
// reference_rkf78(), DensityEvaluator, CellScheme, cell_index_*, ErrorMetrics,
// TraceWriter and the rest of the study surface are the real project code
// already in this translation unit.
//
// What is *not* touched: the production integrator, the batch API, the force and
// torque laws, the tolerances, the native models, the weather conventions, the
// SPICE kernel lifecycle, the ATM-OPT-001 preprocessing caches, and every frozen
// evidence file. The candidate reaches the coupled dynamics through exactly one
// seam, the density_kg_m3 field of leo::EnvironmentSnapshot, and the snapshot is
// built by one shared function for the native and the candidate evaluation of the
// same (epoch, state), so a same-state comparison can only differ through
// density.
//
// Two comparisons are kept strictly apart and are never merged:
//   * same-state:  both densities are produced at one exact (epoch, state) and
//                  differ only in the density field of an otherwise identical
//                  snapshot, so the difference is the immediate physical impact
//                  of the approximation;
//   * free-running: the two integrations evolve independently from the same
//                  initial state, so the difference contains that impact *and*
//                  every feedback it produces. It is never presented as a
//                  same-state measurement.
//
// Counters that leo::propagate() does not expose (attempts, rejected steps) are
// published as JSON null. The RKF7(8) refinement loop owns its own counters and
// those are published as measured values.
#ifndef ATM_CACHE_STUDY_PROPAGATION_HPP_INCLUDED
#define ATM_CACHE_STUDY_PROPAGATION_HPP_INCLUDED

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// The public batch-propagation API, used exactly as MT-002 documents it and never
// modified.
#include "leo/integration/batch.hpp"

namespace cache_study {

// ---------------------------------------------------------------------------
// Small shared helpers
// ---------------------------------------------------------------------------

inline leo::atmosphere::Model required_model(const StudySettings& s) {
    if (!compiled_in(s.model_kind))
        throw std::runtime_error(std::string("atmosphere model ") + s.model +
                                 " is not compiled into this build");
    return s.model_kind;
}

// Independent refinement is an explicit experimental option, never a change
// to the production integration algorithm or its nominal tolerances.
inline bool refined_reference_requested(const StudySettings& s) { return s.refine; }

inline StudySettings evaluator_settings_for(const StudySettings& s) {
    StudySettings copy = s;
    // One process, one thread per evaluator: the in-evaluator section timers are
    // diagnostic only and are never enabled for a headline timing.
    copy.measurement = false;
    return copy;
}

// Self-contained deterministic generator. std::uniform_real_distribution is not
// specified across standard libraries, and an ensemble must reproduce exactly
// from its seed on any host.
inline std::uint64_t splitmix64(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Uniform in [-1, 1), from 53 exact mantissa bits.
inline double unit_draw(std::uint64_t& state) {
    return 2.0 * (static_cast<double>(splitmix64(state) >> 11) * 0x1.0p-53) - 1.0;
}

// ---------------------------------------------------------------------------
// Deterministic initial-condition perturbations
//
// Spacecraft mass, inertia, surfaces, every force and torque law, gravity, the
// integrator and the tolerances are never touched. Only the initial kinematic and
// attitude state of an ensemble member is perturbed.
//
// --study-scale selects and then continuously refines three named levels. The
// magnitude multiplier is scale/reference_scale, so the perturbation size is
// monotone in --study-scale across the whole accepted range (0.05 .. 20):
//   small    scale <= 0.5   1 m, 1 mm/s, 0.1 mrad, 1 urad/s   (at scale 0.25)
//   moderate scale <= 2.0   100 m, 0.1 m/s, 10 mrad, 1 mrad/s (at scale 1)
//   large    scale > 2.0   1 km, 1 m/s, 0.1 rad, 10 mrad/s   (at scale 4)
// Member 0 is always the unperturbed nominal state.
// ---------------------------------------------------------------------------

struct PerturbationLevel {
    const char* name;
    double reference_scale;
    double position_m;
    double velocity_m_s;
    double attitude_rad;
    double body_rate_rad_s;
};

inline PerturbationLevel perturbation_level(double scale) {
    if (scale <= 0.5) return {"small", 0.25, 1.0, 1e-3, 1e-4, 1e-6};
    if (scale <= 2.0) return {"moderate", 1.0, 100.0, 0.1, 1e-2, 1e-3};
    return {"large", 4.0, 1000.0, 1.0, 0.1, 1e-2};
}

inline double perturbation_scale(const StudySettings& s) {
    return s.perturbation_scale >= 0 ? s.perturbation_scale : 1.0;
}

struct Perturbation {
    PerturbationLevel level{"nominal", 1.0, 0, 0, 0, 0};
    double magnitude_multiplier = 0;
    double scale = 0;
    std::uint64_t seed = 0;
    std::size_t member = 0;
    std::array<double, 3> position_offset_m{};
    std::array<double, 3> velocity_offset_m_s{};
    std::array<double, 3> body_rate_offset_rad_s{};
    double attitude_angle_rad = 0;
    leo::State state{};
    std::string json() const {
        Json j;
        j.add("level", std::string(level.name));
        j.add("nominal", magnitude_multiplier == 0);
        j.add("member", static_cast<double>(member));
        j.add("seed", static_cast<double>(seed));
        j.add("study_scale", scale);
        j.add("magnitude_multiplier", magnitude_multiplier);
        j.add("base_position_offset_m", level.position_m * magnitude_multiplier);
        j.add("base_velocity_offset_m_s", level.velocity_m_s * magnitude_multiplier);
        j.add("base_attitude_offset_rad", level.attitude_rad * magnitude_multiplier);
        j.add("base_body_rate_offset_rad_s", level.body_rate_rad_s * magnitude_multiplier);
        j.add("draw_distribution",
              "uniform [-1,1) per component from splitmix64 seeded by (seed, member); "
              "reproducible on any host");
        j.raw("position_offset_m",
              numbers({position_offset_m[0], position_offset_m[1], position_offset_m[2]}));
        j.raw("velocity_offset_m_s",
              numbers({velocity_offset_m_s[0], velocity_offset_m_s[1], velocity_offset_m_s[2]}));
        j.raw("body_rate_offset_rad_s", numbers({body_rate_offset_rad_s[0],
                                                 body_rate_offset_rad_s[1],
                                                 body_rate_offset_rad_s[2]}));
        j.add("attitude_offset_angle_rad", attitude_angle_rad);
        j.add("craft_mass_inertia_surfaces_modified", false);
        j.add("force_law_gravity_tolerances_modified", false);
        return j.str();
    }
};

inline Perturbation perturb_state(const leo::State& base, std::uint64_t seed,
                                  std::size_t member, double scale) {
    if (!std::isfinite(scale) || scale < 0)
        throw std::invalid_argument("perturbation scale must be finite and nonnegative");
    Perturbation perturbation;
    perturbation.scale = scale;
    perturbation.seed = seed;
    perturbation.member = member;
    perturbation.state = base;
    if (member == 0 || scale == 0) return perturbation;
    perturbation.level = perturbation_level(scale);
    perturbation.magnitude_multiplier = scale / perturbation.level.reference_scale;

    // Every sub-seed is derived from the master seed and the member index alone,
    // so a member is reproducible without replaying the ensemble before it.
    std::uint64_t derived = seed ^ mix64(static_cast<std::uint64_t>(member) + 1);
    std::uint64_t state = splitmix64(derived);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        perturbation.position_offset_m[axis] =
            unit_draw(state) * perturbation.level.position_m * perturbation.magnitude_multiplier;
        perturbation.velocity_offset_m_s[axis] =
            unit_draw(state) * perturbation.level.velocity_m_s * perturbation.magnitude_multiplier;
        perturbation.body_rate_offset_rad_s[axis] =
            unit_draw(state) * perturbation.level.body_rate_rad_s * perturbation.magnitude_multiplier;
    }
    // One uniform rotation about a deterministic unit axis.
    const double ax = unit_draw(state), ay = unit_draw(state), az = unit_draw(state);
    const double length = std::sqrt(ax * ax + ay * ay + az * az);
    if (!(length > 0)) throw std::runtime_error("degenerate attitude perturbation axis");
    perturbation.attitude_angle_rad =
        unit_draw(state) * perturbation.level.attitude_rad * perturbation.magnitude_multiplier;
    const double half = 0.5 * perturbation.attitude_angle_rad;
    const double sine = std::sin(half);
    const Eigen::Quaterniond applied(ax / length * sine, ay / length * sine, az / length * sine,
                                     std::cos(half));
    const Eigen::Quaterniond current(base.q.w, base.q.x, base.q.y, base.q.z);
    const Eigen::Quaterniond rotated = applied * current;
    perturbation.state.q =
        leo::normalize_quaternion({rotated.w(), rotated.x(), rotated.y(), rotated.z()});
    perturbation.state.r = base.r + leo::InertialVector{perturbation.position_offset_m[0],
                                                        perturbation.position_offset_m[1],
                                                        perturbation.position_offset_m[2]};
    perturbation.state.v = base.v + leo::InertialVector{perturbation.velocity_offset_m_s[0],
                                                        perturbation.velocity_offset_m_s[1],
                                                        perturbation.velocity_offset_m_s[2]};
    perturbation.state.omega =
        base.omega + leo::BodyVector{perturbation.body_rate_offset_rad_s[0],
                                     perturbation.body_rate_offset_rad_s[1],
                                     perturbation.body_rate_offset_rad_s[2]};
    return perturbation;
}

// ---------------------------------------------------------------------------
// Error distributions
//
// Exact wherever the sample count is a trajectory output schedule (hundreds of
// samples), and a bounded 4096-bin histogram over the trace-scale deltas, whose
// quantile method is stated in every emitted object.
// ---------------------------------------------------------------------------

struct ExactDistribution {
    double minimum = 0, maximum = 0, mean = 0, rms = 0, median = 0, p95 = 0, p99 = 0;
    double minimum_absolute = 0;
    double largest_absolute = 0;
    std::size_t count = 0;
    std::size_t largest_index = 0;

    void add(double value, std::size_t index) {
        if (!std::isfinite(value)) throw std::runtime_error("nonfinite error metric");
        if (count == 0) {
            minimum = maximum = minimum_absolute = value;
            largest_absolute = std::abs(value);
            largest_index = index;
        } else {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            minimum_absolute = std::min(minimum_absolute, std::abs(value));
            if (std::abs(value) > largest_absolute) {
                largest_absolute = std::abs(value);
                largest_index = index;
            }
        }
        ++count;
        mean += value;
        rms += value * value;
    }
    void finalise(std::vector<double> samples) {
        if (samples.empty()) return;
        std::sort(samples.begin(), samples.end());
        const auto absolute = [&] {
            std::vector<double> values;
            values.reserve(samples.size());
            for (double value : samples) values.push_back(std::abs(value));
            std::sort(values.begin(), values.end());
            return values;
        }();
        mean /= double(count);
        rms = std::sqrt(rms / double(count));
        median = quantile_of_sorted(samples, 0.5);
        p95 = quantile_of_sorted(samples, 0.95);
        p99 = quantile_of_sorted(samples, 0.99);
        minimum_absolute = absolute.empty() ? 0 : absolute.front();
    }
    std::string json(const char* note = "") const {
        Json j;
        j.add("count", static_cast<double>(count));
        j.add("minimum", minimum);
        j.add("maximum", maximum);
        j.add("mean", mean);
        j.add("rms", rms);
        j.add("median", median);
        j.add("p95", p95);
        j.add("p99", p99);
        j.add("absolute_minimum", minimum_absolute);
        j.add("largest_absolute", largest_absolute);
        j.add("largest_index", static_cast<double>(largest_index));
        j.add("quantile_method", std::string("exact, over every sample"));
        if (note && *note) j.add("note", std::string(note));
        return j.str();
    }
};

inline constexpr int delta_histogram_bins = 4096;

struct DeltaDistribution {
    double minimum = 0, maximum = 0, mean = 0, rms = 0;
    std::size_t count = 0;
    std::array<std::uint64_t, delta_histogram_bins> bins{};
    bool degenerate = true;

    void add(double value) {
        if (!std::isfinite(value)) throw std::runtime_error("nonfinite delta metric");
        if (count == 0) {
            minimum = maximum = value;
            degenerate = true;
        } else {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            if (!(maximum > minimum)) degenerate = true;
        }
        ++count;
        mean += value;
        rms += value * value;
        if (!degenerate) {
            const double fraction = (value - minimum) / (maximum - minimum);
            const int bin = std::clamp(static_cast<int>(fraction * (delta_histogram_bins - 1)), 0,
                                       delta_histogram_bins - 1);
            ++bins[static_cast<std::size_t>(bin)];
        }
    }
    double quantile(double q) const {
        if (count == 0) return 0;
        if (degenerate) return minimum;
        const auto wanted = static_cast<std::uint64_t>(std::ceil(q * double(count)));
        std::uint64_t cumulative = 0;
        const double width = (maximum - minimum) / (delta_histogram_bins - 1);
        for (int index = 0; index < delta_histogram_bins; ++index) {
            cumulative += bins[static_cast<std::size_t>(index)];
            if (cumulative < wanted) continue;
            const std::uint64_t in_bin = bins[static_cast<std::size_t>(index)];
            if (in_bin == 0) continue;
            const double fraction = double(wanted - (cumulative - in_bin)) / double(in_bin);
            return minimum + (double(index) + std::clamp(fraction, 0.0, 1.0)) * width;
        }
        return maximum;
    }
    std::string json(const char* note = "") const {
        if (count == 0) return std::string("{\"count\":0}");
        Json j;
        j.add("count", static_cast<double>(count));
        j.add("minimum", minimum);
        j.add("maximum", maximum);
        j.add("mean", mean / double(count));
        j.add("rms", std::sqrt(rms / double(count)));
        j.add("p50", quantile(0.5));
        j.add("p95", quantile(0.95));
        j.add("p99", quantile(0.99));
        j.add("quantile_method",
              std::string("linear histogram over the observed range, ") +
                  std::to_string(delta_histogram_bins) +
                  " bins; count, minimum, maximum, mean and RMS are exact");
        if (note && *note) j.add("note", std::string(note));
        return j.str();
    }
};

// ---------------------------------------------------------------------------
// Space-weather driver identity
//
// There is exactly one definition of "the same space-weather vector": the shared
// cache_study::weather_identity() that DensityEvaluator::build_inputs itself
// calls, which is the value every candidate key carries. The locality analysis
// reads a binary trace and so has no access to the QueryInputs the evaluator
// built internally, but it asks the same function for the same epoch rather than
// keeping a second copy of the rule that could drift from the evaluator's.
//
// Identity is the full WeatherIdentity word vector, compared in full. Its
// 64-bit summary() is a reporting digest only and decides nothing here or
// anywhere else: a collision must never be able to permit reuse across an
// incompatible weather vector.
// ---------------------------------------------------------------------------

// Distinct identities are interned, so a trace sample carries a 32-bit handle
// rather than 128 bytes of vector. A handle is canonical inside one analysis, so
// comparing handles is comparing the full vectors.
class WeatherInterner {
public:
    std::uint32_t intern(const WeatherIdentity& value) {
        const auto found = index_.find(value);
        if (found != index_.end()) return found->second;
        const auto handle = static_cast<std::uint32_t>(identities_.size());
        identities_.push_back(value);
        index_.emplace(value, handle);
        return handle;
    }
    std::size_t size() const { return identities_.size(); }
    const WeatherIdentity& at(std::uint32_t handle) const { return identities_[handle]; }
    // Reporting only. Never used to accept or refuse anything.
    std::vector<std::string> summaries() const {
        std::vector<std::string> rows;
        rows.reserve(identities_.size());
        for (const auto& identity : identities_) rows.push_back(std::to_string(identity.summary()));
        return rows;
    }

private:
    std::vector<WeatherIdentity> identities_;
    std::map<WeatherIdentity, std::uint32_t> index_;
};

// ---------------------------------------------------------------------------
// Coupled environment snapshot
//
// One function, every density source. The native and the candidate evaluation of
// the same (epoch, state) therefore differ in the density field and in nothing
// else; the fixed-state study asserts that invariant rather than assuming it.
// ---------------------------------------------------------------------------

inline leo::EnvironmentSnapshot coupled_snapshot(Environment& env, leo::Epoch at,
                                                 const leo::State& state, double density_kg_m3,
                                                 const leo::SunObservation* supplied_sun = nullptr) {
    leo::EnvironmentSnapshot snapshot;
    snapshot.epoch = at;
    // Existing atmospheric corotation assumption, unchanged.
    snapshot.atmosphere_angular_velocity_rad_s = leo::InertialVector{0, 0, 7.292115e-5};
    snapshot.sun_position_m = supplied_sun != nullptr
        ? supplied_sun->state().position_m
        : env.ephemeris.observe_sun(at).state().position_m;
    snapshot.illumination =
        leo::illumination_factor(leo::EclipseModel::conical, state.r, snapshot.sun_position_m,
                                 leo::Earth{}.equatorial_radius_m);
    snapshot.density_kg_m3 = density_kg_m3;
    return snapshot;
}

// A density source is the single seam through which a candidate enters the
// coupled dynamics. Both variants are handed the Sun observation the snapshot
// already needs, so a candidate hit and a native call are served at identical
// inputs and the provider never performs a second Sun lookup.
struct CandidateDensitySource {
    DensityEvaluator& evaluator;
    mutable std::size_t calls = 0;
    explicit CandidateDensitySource(DensityEvaluator& target) : evaluator(target) {}
    double operator()(leo::Epoch at, const leo::InertialVector& position,
                      const leo::SunObservation& sun) const {
        const double density = evaluator.evaluate(at, position, &sun);
        ++calls;
        return density;
    }
};

// The reference is the evaluator's own authoritative path: one fresh
// DirectAtmosphere evaluation with every ATM-OPT-001 preprocessing optimisation
// enabled, uncached and unreconstructed. It is a const call and writes no trace
// record, so a reference run never pollutes a locality trace.
struct AuthoritativeDensitySource {
    DensityEvaluator& evaluator;
    mutable std::size_t calls = 0;
    explicit AuthoritativeDensitySource(DensityEvaluator& target) : evaluator(target) {}
    double operator()(leo::Epoch at, const leo::InertialVector& position,
                      const leo::SunObservation& sun) const {
        const double density = evaluator.direct(at, position, &sun);
        ++calls;
        return density;
    }
};

// ---------------------------------------------------------------------------
// One coupled propagation run with the existing diagnostics
// ---------------------------------------------------------------------------

struct CoupledRun {
    leo::Epoch epoch{};
    leo::Trajectory trajectory;
    std::size_t rhs_calls = 0;
    std::size_t density_evaluations = 0;
    std::size_t rkf78_attempts = 0;
    std::size_t rkf78_rejected = 0;
    bool refined = false;
    double wall_seconds = 0;
    std::string integrator;
    std::string checksum;

    std::vector<double> used_density;
    std::vector<double> fresh_authoritative_density;
    std::vector<double> altitude;
    std::vector<double> drag_force_n;
    std::vector<double> drag_torque_nm;
    std::vector<double> total_acceleration;
    std::vector<double> angular_acceleration;
    std::vector<double> cumulative_drag_work;
    std::vector<double> matched_energy_change;
    std::vector<double> specific_energy;
    std::vector<double> attitude_angle;
    std::vector<double> angular_momentum_norm_error;
    std::vector<double> angular_momentum_norm;
    std::vector<double> angular_speed;
    std::vector<double> total_torque;
    std::vector<leo::ClassicalElements> elements;
    std::vector<leo::InertialVector> specific_momentum;

    double cumulative_drag_work_final = 0;
    double matched_energy_change_final = 0;

    void reserve(std::size_t count) {
        used_density.reserve(count);
        fresh_authoritative_density.reserve(count);
        altitude.reserve(count);
        drag_force_n.reserve(count);
        drag_torque_nm.reserve(count);
        total_acceleration.reserve(count);
        angular_acceleration.reserve(count);
        cumulative_drag_work.reserve(count);
        matched_energy_change.reserve(count);
        specific_energy.reserve(count);
        attitude_angle.reserve(count);
        angular_momentum_norm_error.reserve(count);
        angular_momentum_norm.reserve(count);
        angular_speed.reserve(count);
        total_torque.reserve(count);
        elements.reserve(count);
        specific_momentum.reserve(count);
    }
};

// `diagnostics` controls the post-run pass. It is on for every scientific run and
// off for every timed one: the pass re-evaluates the loads, the orbital and
// attitude diagnostics and one fresh authoritative density per output sample, so
// leaving it inside a timed region would measure the diagnostics rather than the
// propagation.
template <class DensitySource>
CoupledRun run_coupled(const Config& c, Environment& env, const experiment::Case& spec,
                       const leo::State& initial, DensitySource& density_source,
                       DensityEvaluator& reference, const std::vector<double>& labels,
                       leo::Epoch epoch, bool refined, bool diagnostics = true) {
    const auto dynamics = case_dynamics(spec);
    const leo::Earth earth;
    CoupledRun run;
    run.epoch = epoch;
    run.refined = refined;

    std::size_t rhs_calls = 0;
    auto source = [&](leo::ElapsedTime t, const leo::State& s) {
        const leo::Epoch at{epoch.et_seconds + t.seconds};
        const auto observation = env.ephemeris.observe_sun(at);
        const double density = density_source(at, s.r, observation);
        if (!std::isfinite(density) || density < 0)
            throw std::runtime_error("nonfinite or negative density in a coupled run");
        return coupled_snapshot(env, at, s, density, &observation);
    };
    auto system = [&](leo::ElapsedTime t, const leo::State& s) {
        ++rhs_calls;
        return dynamics(t, s, source(t, s));
    };

    leo::IntegrationOptions options = tolerances();
    options.rtol = c.rtol;
    options.max_step_s = c.max_step;
    options.initial_step_s = std::min(1.0, c.max_step);
    // Existing IntegrationOptions default, stated explicitly rather than raised.
    options.max_attempts = leo::IntegrationOptions{}.max_attempts;
    if (refined) {
        options.rtol = c.refine_rtol;
        options.max_step_s = std::min(c.refine_max_step, c.max_step);
        run.integrator = "boost odeint controlled RKF7(8), independent tableau";
    } else {
        run.integrator = "dopri5 (production leo::propagate)";
    }

    std::size_t attempts = 0, rejected = 0;
    const auto timer = Clock::now();
    if (refined)
        run.trajectory = reference_rkf78(system, initial, labels, options, attempts, rejected, epoch);
    else
        run.trajectory = leo::propagate(system, initial, epoch, labels, options);
    run.wall_seconds = elapsed(timer);
    run.rhs_calls = rhs_calls;
    run.density_evaluations = density_source.calls;
    run.rkf78_attempts = attempts;
    run.rkf78_rejected = rejected;
    if (run.trajectory.samples.size() != labels.size())
        throw std::runtime_error("coupled run lost requested output samples");

    Checksum checksum;
    for (const auto& sample : run.trajectory.samples) checksum.add(sample.state);
    run.checksum = checksum.str();

    if (!diagnostics) return run;

    // Post-run diagnostics. This pass re-evaluates the loads and the atmosphere at
    // the reported output times; it is diagnostic only and never feeds the
    // integrated state. Orbital, load and attitude diagnostics come from the
    // existing leo::analysis routines rather than from a second implementation.
    const auto& inertia = dynamics.spacecraft().inertia();
    const auto& first = run.trajectory.samples.front().state;
    const double initial_energy = matched_specific_energy(earth, first);
    const double initial_momentum =
        leo::norm(leo::body_to_inertial(first.q, inertia.multiply(first.omega)));
    run.reserve(run.trajectory.samples.size());
    std::vector<leo::EnvironmentSnapshot> snapshots;
    snapshots.reserve(run.trajectory.samples.size());
    for (const auto& sample : run.trajectory.samples)
        snapshots.push_back(source(sample.t, sample.state));
    const auto loads = leo::analysis::environmental_history(run.trajectory, dynamics, snapshots);
    const auto attitude =
        leo::analysis::attitude_history(run.trajectory, dynamics.spacecraft(), loads);
    const auto orbit = leo::analysis::orbital_history(run.trajectory, earth);

    double last_drag_power = 0, last_time = 0, cumulative_drag = 0;
    for (std::size_t index = 0; index < run.trajectory.samples.size(); ++index) {
        const auto& sample = run.trajectory.samples[index];
        const auto& state = sample.state;
        const auto& contributions = loads[index].contributions;
        const double time = sample.t.seconds;
        const double drag_power = leo::dot(contributions.drag.force_n, state.v);
        if (index > 0) {
            const double dt = time - last_time;
            cumulative_drag += 0.5 * (drag_power + last_drag_power) * dt;
            last_time = time;
        }
        last_drag_power = drag_power;

        // One fresh authoritative density at this exact output state. On a
        // candidate run this is the holdout reference at the state the candidate
        // actually visited; on a reference run it is a determinism check of the
        // authoritative path against itself.
        const leo::Epoch at{epoch.et_seconds + time};
        const auto observation = env.ephemeris.observe_sun(at);
        const double fresh = reference.direct(at, state.r, &observation);
        if (!std::isfinite(fresh) || fresh <= 0)
            throw std::runtime_error("invalid fresh authoritative density in diagnostics");

        run.used_density.push_back(snapshots[index].density_kg_m3);
        run.fresh_authoritative_density.push_back(fresh);
        run.altitude.push_back(orbit[index].altitude_m);
        run.drag_force_n.push_back(leo::norm(contributions.drag.force_n));
        run.drag_torque_nm.push_back(leo::norm(contributions.drag.torque_nm));
        run.total_acceleration.push_back(leo::norm(contributions.total_acceleration_m_s2));
        run.angular_acceleration.push_back(
            leo::norm(contributions.total_angular_acceleration_rad_s2));
        run.cumulative_drag_work.push_back(cumulative_drag);
        run.matched_energy_change.push_back(matched_specific_energy(earth, state) - initial_energy);
        run.specific_energy.push_back(orbit[index].specific_energy_j_kg);
        run.attitude_angle.push_back(bench::angle(first.q, state.q));
        run.angular_momentum_norm.push_back(
            leo::norm(leo::body_to_inertial(state.q, inertia.multiply(state.omega))));
        run.angular_momentum_norm_error.push_back(run.angular_momentum_norm.back() - initial_momentum);
        run.angular_speed.push_back(attitude[index].angular_speed_rad_s);
        run.total_torque.push_back(attitude[index].torque_magnitude_nm);
        run.elements.push_back(orbit[index].elements);
        run.specific_momentum.push_back(orbit[index].specific_angular_momentum_m2_s);
    }
    run.cumulative_drag_work_final = cumulative_drag;
    run.matched_energy_change_final = run.matched_energy_change.back();
    return run;
}

// ---------------------------------------------------------------------------
// Free-running comparison at identical output epochs
// ---------------------------------------------------------------------------

inline double wrapped_difference(double a, double b, double period) {
    double difference = std::fmod(a - b, period);
    if (difference > 0.5 * period) difference -= period;
    if (difference < -0.5 * period) difference += period;
    return difference;
}

// Differences that exist only because the two integrations visited different
// states. Reported as such; never attributed to the candidate alone.
struct RunComparison {
    ExactDistribution position, radial, along_track, cross_track, velocity;
    ExactDistribution attitude, body_rate;
    ExactDistribution element_a, element_e, element_i, element_raan, element_argp, element_nu;
    ExactDistribution specific_energy, matched_energy;
    ExactDistribution angular_momentum_vector, angular_momentum_norm;
    ExactDistribution drag_force, drag_torque, total_acceleration, angular_acceleration;
    ExactDistribution cumulative_drag_work;
    ErrorMetrics density_at_own_states;
    std::vector<double> density_log_at_own_states;
    std::vector<double> position_series, velocity_series, attitude_series;
    std::vector<double> density_series, drag_force_series, drag_work_series;

    std::string json(bool with_series) const {
        Json j;
        j.raw("position_m", position.json("candidate minus reference at identical output epochs; "
                                         "includes the state divergence the candidate produces"));
        j.raw("radial_position_m", radial.json("RTN radial component of the reference basis"));
        j.raw("along_track_position_m", along_track.json("RTN along-track component"));
        j.raw("cross_track_position_m", cross_track.json("RTN cross-track component"));
        j.raw("velocity_m_s", velocity.json(""));
        j.raw("attitude_angle_rad", attitude.json("quaternion orientation-angle difference"));
        j.raw("body_rate_rad_s", body_rate.json(""));
        j.raw("semi_major_axis_m", element_a.json(""));
        j.raw("eccentricity", element_e.json(""));
        j.raw("inclination_rad", element_i.json(""));
        j.raw("raan_rad", element_raan.json("wrapped to (-pi, pi]"));
        j.raw("argument_of_periapsis_rad", element_argp.json("wrapped to (-pi, pi]"));
        j.raw("true_anomaly_rad", element_nu.json("wrapped to (-pi, pi]"));
        j.raw("specific_energy_j_kg",
              specific_energy.json("two-body specific energy from the existing orbital diagnostic"));
        j.raw("matched_energy_change_j_kg", matched_energy.json("K + U_central + U_J2 change"));
        j.raw("specific_angular_momentum_m2_s",
              angular_momentum_vector.json("vector difference at identical output epochs"));
        j.raw("angular_momentum_norm_error_kg_m2_s", angular_momentum_norm.json(""));
        j.raw("drag_force_magnitude_difference_n", drag_force.json(""));
        j.raw("drag_torque_magnitude_difference_nm", drag_torque.json(""));
        j.raw("total_acceleration_difference_m_s2", total_acceleration.json(""));
        j.raw("angular_acceleration_difference_rad_s2", angular_acceleration.json(""));
        j.raw("cumulative_drag_work_difference_j", cumulative_drag_work.json(""));
        j.raw("density_at_each_runs_own_state", density_at_own_states.json());
        j.add("density_at_each_runs_own_state_note",
              "candidate density against the authoritative density at each run's own state. This "
              "mixture contains both the reconstruction error and the state divergence and is not "
              "a pure density error; the same-state block is that measurement.");
        if (!with_series) return j.str();
        std::ostringstream rows;
        rows << std::setprecision(17) << '[';
        for (std::size_t index = 0; index < position_series.size(); ++index) {
            if (index) rows << ',';
            rows << '[' << position_series[index] << ',' << velocity_series[index] << ','
                 << attitude_series[index] << ',' << density_series[index] << ','
                 << drag_force_series[index] << ',' << drag_work_series[index] << ']';
        }
        rows << ']';
        Json series;
        series.raw("fields",
                   strings({"position_m", "velocity_m_s", "attitude_angle_rad",
                            "density_relative_at_own_state", "drag_force_difference_n",
                            "cumulative_drag_work_difference_j"}));
        series.raw("rows", rows.str());
        j.raw("time_series", series.str());
        return j.str();
    }
};

inline RunComparison compare_runs(const CoupledRun& reference, const CoupledRun& candidate) {
    if (reference.trajectory.samples.size() != candidate.trajectory.samples.size())
        throw std::runtime_error("runs have different output schedules");
    if (reference.trajectory.samples.empty()) throw std::runtime_error("run has no samples");
    RunComparison comparison;
    std::vector<double> position_samples, velocity_samples, attitude_samples, radial_samples,
        along_samples, cross_samples, body_rate_samples, momentum_vector_samples,
        momentum_norm_samples, drag_force_samples, drag_torque_samples, acceleration_samples,
        angular_acceleration_samples, work_samples, energy_samples, matched_samples,
        element_a_samples, element_e_samples, element_i_samples, element_raan_samples,
        element_argp_samples, element_nu_samples;
    std::vector<double> density_relative, density_log;

    for (std::size_t index = 0; index < reference.trajectory.samples.size(); ++index) {
        const auto& a = reference.trajectory.samples[index];
        const auto& b = candidate.trajectory.samples[index];
        if (a.t.seconds != b.t.seconds)
            throw std::runtime_error("runs are not sampled at identical output epochs");
        const auto& x = a.state;
        const auto& y = b.state;
        const auto basis = leo::analysis::rtn_basis(x);
        const auto rtn = leo::analysis::rtn_components(basis, y.r - x.r);
        const double position_error = leo::norm(y.r - x.r);
        const double velocity_error = leo::norm(y.v - x.v);
        const double attitude_error = bench::angle(x.q, y.q);
        const double body_rate_error = leo::norm(y.omega - x.omega);
        const double momentum_vector = leo::norm(candidate.specific_momentum[index] -
                                                 reference.specific_momentum[index]);
        const double momentum_norm = candidate.angular_momentum_norm[index] -
                                     reference.angular_momentum_norm[index];
        const double drag_force = candidate.drag_force_n[index] - reference.drag_force_n[index];
        const double drag_torque = candidate.drag_torque_nm[index] - reference.drag_torque_nm[index];
        const double acceleration =
            candidate.total_acceleration[index] - reference.total_acceleration[index];
        const double angular_acceleration =
            candidate.angular_acceleration[index] - reference.angular_acceleration[index];
        const double work =
            candidate.cumulative_drag_work[index] - reference.cumulative_drag_work[index];
        const double energy = candidate.specific_energy[index] - reference.specific_energy[index];
        const double matched =
            candidate.matched_energy_change[index] - reference.matched_energy_change[index];
        const double raan = wrapped_difference(candidate.elements[index].raan_rad,
                                               reference.elements[index].raan_rad,
                                               2 * std::numbers::pi);
        const double argp = wrapped_difference(candidate.elements[index].arg_periapsis_rad,
                                               reference.elements[index].arg_periapsis_rad,
                                               2 * std::numbers::pi);
        const double anomaly = wrapped_difference(candidate.elements[index].true_anomaly_rad,
                                                  reference.elements[index].true_anomaly_rad,
                                                  2 * std::numbers::pi);
        if (reference.used_density[index] <= 0)
            throw std::runtime_error("reference run reported a nonpositive density");
        const double relative =
            (candidate.used_density[index] - reference.used_density[index]) /
            reference.used_density[index];
        density_relative.push_back(relative);
        density_log.push_back(std::log(candidate.used_density[index]) -
                              std::log(reference.used_density[index]));

        comparison.position.add(position_error, index);
        comparison.radial.add(rtn.x(), index);
        comparison.along_track.add(rtn.y(), index);
        comparison.cross_track.add(rtn.z(), index);
        comparison.velocity.add(velocity_error, index);
        comparison.attitude.add(attitude_error, index);
        comparison.body_rate.add(body_rate_error, index);
        comparison.element_a.add(candidate.elements[index].a_m - reference.elements[index].a_m, index);
        comparison.element_e.add(candidate.elements[index].e - reference.elements[index].e, index);
        comparison.element_i.add(candidate.elements[index].i_rad - reference.elements[index].i_rad, index);
        comparison.element_raan.add(raan, index);
        comparison.element_argp.add(argp, index);
        comparison.element_nu.add(anomaly, index);
        comparison.specific_energy.add(energy, index);
        comparison.matched_energy.add(matched, index);
        comparison.angular_momentum_vector.add(momentum_vector, index);
        comparison.angular_momentum_norm.add(momentum_norm, index);
        comparison.drag_force.add(drag_force, index);
        comparison.drag_torque.add(drag_torque, index);
        comparison.total_acceleration.add(acceleration, index);
        comparison.angular_acceleration.add(angular_acceleration, index);
        comparison.cumulative_drag_work.add(work, index);

        position_samples.push_back(position_error);
        velocity_samples.push_back(velocity_error);
        attitude_samples.push_back(attitude_error);
        radial_samples.push_back(rtn.x());
        along_samples.push_back(rtn.y());
        cross_samples.push_back(rtn.z());
        body_rate_samples.push_back(body_rate_error);
        momentum_vector_samples.push_back(momentum_vector);
        momentum_norm_samples.push_back(momentum_norm);
        drag_force_samples.push_back(drag_force);
        drag_torque_samples.push_back(drag_torque);
        acceleration_samples.push_back(acceleration);
        angular_acceleration_samples.push_back(angular_acceleration);
        work_samples.push_back(work);
        energy_samples.push_back(energy);
        matched_samples.push_back(matched);
        element_a_samples.push_back(candidate.elements[index].a_m - reference.elements[index].a_m);
        element_e_samples.push_back(candidate.elements[index].e - reference.elements[index].e);
        element_i_samples.push_back(candidate.elements[index].i_rad - reference.elements[index].i_rad);
        element_raan_samples.push_back(raan);
        element_argp_samples.push_back(argp);
        element_nu_samples.push_back(anomaly);

        comparison.position_series.push_back(position_error);
        comparison.velocity_series.push_back(velocity_error);
        comparison.attitude_series.push_back(attitude_error);
        comparison.density_series.push_back(relative);
        comparison.drag_force_series.push_back(drag_force);
        comparison.drag_work_series.push_back(work);
    }
    comparison.density_at_own_states = error_metrics(density_relative, density_log);
    comparison.position.finalise(position_samples);
    comparison.radial.finalise(radial_samples);
    comparison.along_track.finalise(along_samples);
    comparison.cross_track.finalise(cross_samples);
    comparison.velocity.finalise(velocity_samples);
    comparison.attitude.finalise(attitude_samples);
    comparison.body_rate.finalise(body_rate_samples);
    comparison.element_a.finalise(element_a_samples);
    comparison.element_e.finalise(element_e_samples);
    comparison.element_i.finalise(element_i_samples);
    comparison.element_raan.finalise(element_raan_samples);
    comparison.element_argp.finalise(element_argp_samples);
    comparison.element_nu.finalise(element_nu_samples);
    comparison.specific_energy.finalise(energy_samples);
    comparison.matched_energy.finalise(matched_samples);
    comparison.angular_momentum_vector.finalise(momentum_vector_samples);
    comparison.angular_momentum_norm.finalise(momentum_norm_samples);
    comparison.drag_force.finalise(drag_force_samples);
    comparison.drag_torque.finalise(drag_torque_samples);
    comparison.total_acceleration.finalise(acceleration_samples);
    comparison.angular_acceleration.finalise(angular_acceleration_samples);
    comparison.cumulative_drag_work.finalise(work_samples);
    return comparison;
}

// ---------------------------------------------------------------------------
// Same-state evaluation
// ---------------------------------------------------------------------------

struct FixedStateComparison {
    ErrorMetrics density;
    ExactDistribution density_absolute, drag_force, drag_torque, total_acceleration,
        angular_acceleration;
    std::vector<double> relative_series;
    std::size_t points = 0;
    std::size_t snapshots_identical_outside_density = 0;
};

inline FixedStateComparison compare_fixed_state(Environment& env,
                                                const experiment::Case& spec, const CoupledRun& run,
                                                DensityEvaluator& candidate,
                                                DensityEvaluator& reference,
                                                const std::vector<std::size_t>& indices) {
    const auto dynamics = case_dynamics(spec);
    FixedStateComparison comparison;
    std::vector<double> relative, log_error, absolute;
    std::vector<double> force_samples, torque_samples, acceleration_samples, rate_samples;
    for (std::size_t position : indices) {
        const auto& sample = run.trajectory.samples[position];
        const leo::Epoch at{run.epoch.et_seconds + sample.t.seconds};
        const auto observation = env.ephemeris.observe_sun(at);

        // The same exact physical input twice: once fresh from the authoritative
        // provider, once through the candidate adapter.
        const double reference_density = reference.direct(at, sample.state.r, &observation);
        const double candidate_density = candidate.evaluate(at, sample.state.r, &observation);
        if (!std::isfinite(reference_density) || reference_density <= 0 ||
            !std::isfinite(candidate_density) || candidate_density < 0)
            throw std::runtime_error("invalid density in the fixed-state comparison");

        const auto reference_snapshot = coupled_snapshot(env, at, sample.state, reference_density, &observation);
        const auto candidate_snapshot = coupled_snapshot(env, at, sample.state, candidate_density, &observation);
        const bool identical = reference_snapshot.epoch.et_seconds == candidate_snapshot.epoch.et_seconds &&
                               reference_snapshot.sun_position_m.as_eigen() ==
                                   candidate_snapshot.sun_position_m.as_eigen() &&
                               reference_snapshot.illumination == candidate_snapshot.illumination &&
                               reference_snapshot.atmosphere_angular_velocity_rad_s.as_eigen() ==
                                   candidate_snapshot.atmosphere_angular_velocity_rad_s.as_eigen();
        if (!identical) throw std::runtime_error("fixed-state snapshots differ outside density");

        const auto reference_loads = dynamics.evaluate(sample.t, sample.state, reference_snapshot);
        const auto candidate_loads = dynamics.evaluate(sample.t, sample.state, candidate_snapshot);

        ++comparison.points;
        if (identical) ++comparison.snapshots_identical_outside_density;
        const double value = (candidate_density - reference_density) / reference_density;
        relative.push_back(value);
        log_error.push_back(std::log(candidate_density) - std::log(reference_density));
        comparison.relative_series.push_back(value);
        absolute.push_back(candidate_density - reference_density);
        comparison.density_absolute.add(candidate_density - reference_density, position);
        const double force = leo::norm(candidate_loads.drag.force_n - reference_loads.drag.force_n);
        const double torque =
            leo::norm(candidate_loads.drag.torque_nm - reference_loads.drag.torque_nm);
        const double acceleration = leo::norm(candidate_loads.total_acceleration_m_s2 -
                                              reference_loads.total_acceleration_m_s2);
        const double angular = leo::norm(candidate_loads.total_angular_acceleration_rad_s2 -
                                         reference_loads.total_angular_acceleration_rad_s2);
        comparison.drag_force.add(force, position);
        comparison.drag_torque.add(torque, position);
        comparison.total_acceleration.add(acceleration, position);
        comparison.angular_acceleration.add(angular, position);
        force_samples.push_back(force);
        torque_samples.push_back(torque);
        acceleration_samples.push_back(acceleration);
        rate_samples.push_back(angular);
    }
    comparison.density = error_metrics(relative, log_error);
    comparison.density_absolute.finalise(absolute);
    comparison.drag_force.finalise(force_samples);
    comparison.drag_torque.finalise(torque_samples);
    comparison.total_acceleration.finalise(acceleration_samples);
    comparison.angular_acceleration.finalise(rate_samples);
    return comparison;
}

inline std::string fixed_state_json(const FixedStateComparison& comparison,
                                    const std::vector<double>& targets) {
    Json j;
    j.add("protocol",
          "both densities are produced at the identical epoch and identical spacecraft state and "
          "enter identical coupled snapshots; only the density field differs");
    j.add("points", static_cast<double>(comparison.points));
    j.add("all_snapshots_identical_outside_density",
          comparison.snapshots_identical_outside_density == comparison.points);
    j.raw("density_error", comparison.density.json());
    j.raw("density_absolute_kg_m3", comparison.density_absolute.json(""));
    j.raw("drag_force_difference_n", comparison.drag_force.json(""));
    j.raw("drag_torque_difference_nm", comparison.drag_torque.json(""));
    j.raw("total_acceleration_difference_m_s2", comparison.total_acceleration.json(""));
    j.raw("angular_acceleration_difference_rad_s2", comparison.angular_acceleration.json(""));
    j.raw("error_thresholds", threshold_row(comparison.density, targets));
    j.add("threshold_rule",
          "satisfaction is decided on the maximum absolute relative error over every compared "
          "point; a mean or a percentile is never promoted to a worst-case guarantee");
    return j.str();
}

// ---------------------------------------------------------------------------
// Bitwise series comparison
//
// The existing experiment driver's own execute() propagation is the untouched
// reference. Comparing a study run against it at every requested output label,
// over all thirteen state components bit for bit, is what shows that the
// study's own wrapper, with its density cache disabled, reproduces the current
// direct library exactly rather than approximately.
//
// This equivalence is about the wrapper and the current library on the inputs
// this process actually read. It does not certify the frozen direct-v2 evidence
// checksum, which depends on the pinned kernels and space-weather archives, and
// it is never reported as if it did.
// ---------------------------------------------------------------------------

inline bool states_bitwise_equal(const leo::State& left, const leo::State& right) {
    const auto a = leo::detail::pack(left);
    const auto b = leo::detail::pack(right);
    for (std::size_t index = 0; index < 13; ++index)
        if (std::memcmp(&a[index], &b[index], sizeof(double)) != 0) return false;
    return true;
}

struct SeriesBitwise {
    bool attempted = false;
    bool equal = false;
    std::size_t samples = 0;
    std::size_t mismatched_samples = 0;
    std::size_t first_mismatch = 0;
    std::string baseline_checksum;
    std::string compared_checksum;
    std::string reason;

    std::string json(const char* role) const {
        Json j;
        j.add("role", std::string(role));
        j.add("attempted", attempted);
        j.add("compared", static_cast<double>(samples));
        j.add("bitwise_equal", attempted ? equal : false);
        j.add("mismatched_samples", static_cast<double>(mismatched_samples));
        j.add("first_mismatch_index", static_cast<double>(first_mismatch));
        j.add("driver_baseline_checksum", baseline_checksum);
        j.add("compared_checksum", compared_checksum);
        j.add("method",
              "all thirteen packed state components and the requested output label are compared "
              "bit for bit at every sample");
        j.add("scope",
              "equivalence of this study's wrapper with the current direct library on the inputs "
              "this process read; it does not certify the frozen direct-v2 evidence checksum, "
              "which depends on the pinned kernels and space-weather archives");
        if (!reason.empty()) j.add("reason", reason);
        return j.str();
    }
};

inline SeriesBitwise compare_series_bitwise(const leo::Trajectory& baseline,
                                            const leo::Trajectory& compared) {
    SeriesBitwise result;
    result.attempted = true;
    Checksum baseline_checksum, compared_checksum;
    for (const auto& sample : baseline.samples) baseline_checksum.add(sample.state);
    for (const auto& sample : compared.samples) compared_checksum.add(sample.state);
    result.baseline_checksum = baseline_checksum.str();
    result.compared_checksum = compared_checksum.str();
    result.samples = std::min(baseline.samples.size(), compared.samples.size());
    bool equal = baseline.samples.size() == compared.samples.size();
    for (std::size_t index = 0; index < result.samples; ++index) {
        const auto& a = baseline.samples[index];
        const auto& b = compared.samples[index];
        if (std::memcmp(&a.t.seconds, &b.t.seconds, sizeof(double)) != 0 ||
            !states_bitwise_equal(a.state, b.state)) {
            equal = false;
            ++result.mismatched_samples;
            if (result.mismatched_samples == 1) result.first_mismatch = index;
        }
    }
    result.equal = equal;
    return result;
}

// ---------------------------------------------------------------------------
// propagation_study
// ---------------------------------------------------------------------------

inline std::vector<std::size_t> selected_sample_indices(std::size_t available,
                                                        std::size_t requested) {
    std::vector<std::size_t> indices;
    if (available == 0) return indices;
    if (requested <= 1 || requested >= available) {
        indices.reserve(available);
        for (std::size_t index = 0; index < available; ++index) indices.push_back(index);
        return indices;
    }
    // Evenly spaced, endpoints included, deterministic for a given request count.
    for (std::size_t index = 0; index < requested; ++index)
        indices.push_back(static_cast<std::size_t>(
            std::llround(double(index) * double(available - 1) / double(requested - 1))));
    return indices;
}

inline void emit_run_metadata(Json& j, const CoupledRun& run) {
    j.add("integrator", run.integrator);
    j.add("output_samples", static_cast<double>(run.trajectory.samples.size()));
    j.add("rhs_calls", static_cast<double>(run.rhs_calls));
    j.add("density_evaluations", static_cast<double>(run.density_evaluations));
    j.add("trajectory_checksum", run.checksum);
    if (run.refined) {
        // The refinement loop owns its counters, so these are measured values.
        j.add("rkf78_attempts", static_cast<double>(run.rkf78_attempts));
        j.add("rkf78_rejected_steps", static_cast<double>(run.rkf78_rejected));
    } else {
        // The production dopri5 loop exposes neither counter and the integrator is
        // not modified to obtain them. These are null, not zero, and are not
        // inferred from rhs_calls.
        j.raw("integration_attempts", "null");
        j.raw("rejected_steps", "null");
        j.add("integration_counters_note",
              "leo::propagate() exposes neither counter; the values are not estimated");
    }
}

inline std::string propagation_study(const Config& c, Environment& env, const StudySettings& s) {
    if (c.case_files.empty()) throw std::invalid_argument("propagation study needs a --case");
    const auto model = required_model(s);
    const bool refined = refined_reference_requested(s);
    const auto settings = evaluator_settings_for(s);
    const std::size_t members = std::max<std::size_t>(1, s.ensemble);
    const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
    const auto labels = sample_labels(c);
    const auto targets = study_error_targets();

    Json header;
    header.add("study_kind", std::string("propagation"));
    header.add("protocol",
               "Phase 6 coupled sensitivity. A same-state comparison isolates the immediate "
               "physical impact of the density approximation; a free-running comparison adds "
               "every feedback that approximation produces. The reference is one fresh "
               "authoritative DirectAtmosphere evaluation per query with the ATM-OPT-001 "
               "preprocessing optimisations enabled. No integrator, force law, tolerance, native "
               "model or weather convention is modified.");
    header.add("model", model_name(model));
    header.add("candidate", candidate_name(s.candidate_kind));
    header.add("candidate_selection", s.candidate);
    header.add("refined_reference_requested", refined);
    header.add("epoch_utc", utc_label(epoch));
    header.add("epoch_et_seconds", epoch.et_seconds);
    header.add("duration_s", c.duration);
    header.add("output_cadence_s", c.cadence);
    header.add("output_samples", static_cast<double>(labels.size()));
    header.add("rtol", c.rtol);
    header.add("max_step_s", c.max_step);
    header.add("absolute_tolerances",
               "project defaults: position 1e-3 m, velocity 1e-6 m/s, quaternion 1e-12, body rate "
               "1e-12 rad/s");
    header.add("perturbation_level", "nominal plus small/moderate/large members when no explicit scale is supplied");
    header.add("perturbation_scale", s.perturbation_scale);
    header.add("perturbation_seed", static_cast<double>(s.seed));
    header.add("ensemble_members", static_cast<double>(members));
    header.add("same_state_points_requested", static_cast<double>(s.points));
    header.add("screening_target", s.target);
    header.add("density_only_adapter", true);
    header.add("density_only_api_contract", density_only_api_note());
    header.add("gravity", "J2 enabled; symmetry axis fixed J2000 +Z");
    header.add("drag_model", "sentman");
    header.add("solar_illumination_model", "conical");
    header.add("atmosphere_rotation_rad_s",
               std::string("0,0,7.292115e-5 (existing corotation assumption)"));

    std::ostringstream runs;
    runs << std::setprecision(17) << '[';
    bool run_first = true;
    for (std::size_t member = 0; member < members; ++member) {
        for (const auto& case_file : c.case_files) {
            const auto spec = experiment::load(case_file);
            const auto dynamics = case_dynamics(spec);
            const double level_scales[]{0.25, 1.0, 4.0};
            const double initial_scale = s.perturbation_scale >= 0
                ? s.perturbation_scale : level_scales[member == 0 ? 0 : (member - 1) % 3];
            const auto perturbation = perturb_state(spec.start(), s.seed, member, initial_scale);

            // One evaluator per query role. It owns the authoritative provider used
            // by the reference runs and the candidate store used by the candidate
            // run; the two paths never share a stored value.
            DensityEvaluator evaluator(c, env, model, settings);
            AuthoritativeDensitySource reference_source(evaluator);
            CandidateDensitySource candidate_source(evaluator);

            const auto reference_run =
                run_coupled(c, env, spec, perturbation.state, reference_source, evaluator, labels,
                            epoch, false);
            const auto candidate_run =
                run_coupled(c, env, spec, perturbation.state, candidate_source, evaluator, labels,
                            epoch, false);

            Json entry;
            entry.add("case_id", spec.id);
            entry.add("case_file", case_file);
            entry.add("spacecraft_mass_kg", dynamics.spacecraft().mass_kg());
            entry.add("spacecraft_surface_count",
                      static_cast<double>(dynamics.spacecraft().surfaces().size()));
            entry.raw("initial_condition", perturbation.json());

            const auto comparison = compare_runs(reference_run, candidate_run);
            Json free_running;
            free_running.raw("comparison", comparison.json(member == 0));
            free_running.raw("reference_run", [&] {
                Json run_block;
                emit_run_metadata(run_block, reference_run);
                run_block.add("wall_seconds", reference_run.wall_seconds);
                return run_block.str();
            }());
            free_running.raw("candidate_run", [&] {
                Json run_block;
                emit_run_metadata(run_block, candidate_run);
                run_block.add("wall_seconds", candidate_run.wall_seconds);
                return run_block.str();
            }());
            free_running.add("rhs_call_ratio",
                             candidate_run.rhs_calls
                                 ? double(reference_run.rhs_calls) /
                                       double(candidate_run.rhs_calls)
                                 : 0.0);
            free_running.add("interpretation",
                             "after the first step the two integrations are independent, so these "
                             "differences contain the density approximation and the feedback it "
                             "produces; this is not a same-state measurement");
            entry.raw("free_running_comparison", free_running.str());

            const auto fixed = compare_fixed_state(
                env, spec, reference_run, evaluator, evaluator,
                selected_sample_indices(reference_run.trajectory.samples.size(), s.points));
            entry.raw("same_state_comparison", fixed_state_json(fixed, targets));

            // Wrapper-disabled equivalence to the current direct library. The
            // driver's own execute() is the untouched baseline and is run only for
            // the unperturbed nominal member, because execute() always starts from
            // the case's own initial state.
            if (member == 0) {
                const RunRecord driver_baseline = execute(c, env, spec, model, "dopri", epoch);
                const auto authoritative_series =
                    compare_series_bitwise(driver_baseline.trajectory, reference_run.trajectory);
                SeriesBitwise candidate_series;
                if (s.candidate_kind == Candidate::control) {
                    candidate_series = compare_series_bitwise(driver_baseline.trajectory,
                                                              candidate_run.trajectory);
                } else {
                    candidate_series.reason =
                        "the candidate under test reconstructs density by design, so its series is "
                        "not expected to be bitwise equal; the cache-disabled control candidate is "
                        "the configuration this proof is about";
                }
                Json proof;
                proof.add("direct_series_bitwise_equal",
                          candidate_series.attempted && candidate_series.equal);
                proof.add("direct_series_bitwise_check_attempted", candidate_series.attempted);
                proof.add("driver_baseline", std::string("experiment execute() dopri5, unperturbed "
                                                          "nominal member, identical options"));
                proof.raw("authoritative_run",
                          authoritative_series.json("study authoritative run against driver execute()"));
                proof.raw("candidate_run", candidate_series.json(
                                                 "study candidate run against driver execute()"));
                proof.add("density_cache_disabled",
                          "the control candidate performs exact preprocessing with no density "
                          "caching at all, so an exact series match shows that the study wrapper "
                          "itself introduces no change to the current direct library");
                proof.add("does_not_certify",
                          "the frozen direct-v2 evidence checksum, which additionally depends on "
                          "the pinned kernels and space-weather archives this process may or may "
                          "not have read; see evidence_class in the shared metadata");
                entry.raw("direct_series_bitwise_proof", proof.str());
            }

            Json budget;
            budget.add("screening_target", s.target);
            budget.add("screening_target_percent", 100 * s.target);
            budget.add("screening_target_mean_passes",
                       std::abs(fixed.density.mean_absolute_relative) <= s.target);
            budget.add("screening_target_max_passes", threshold_pass(fixed.density, s.target));
            budget.add("note",
                       "the screening target is an experimental comparison parameter supplied by "
                       "the runner; it is not a predetermined acceptable physical error and the "
                       "decision is always made on the worst compared point");
            entry.raw("screening_target_assessment", budget.str());

            // Optional independent tightened reference, so that error introduced by
            // the candidate is separated from error introduced by the integrator.
            if (refined) {
                const auto refined_reference =
                    run_coupled(c, env, spec, perturbation.state, reference_source, evaluator,
                                labels, epoch, true);
                const auto refined_candidate =
                    run_coupled(c, env, spec, perturbation.state, candidate_source, evaluator,
                                labels, epoch, true);
                const auto integrator_only = compare_runs(reference_run, refined_reference);
                const auto cache_only = compare_runs(refined_reference, refined_candidate);
                const auto combined = compare_runs(reference_run, refined_candidate);
                Json separation;
                separation.raw("integrator_only_dopri5_vs_rkf78", integrator_only.json(false));
                separation.raw("candidate_only_rkf78", cache_only.json(false));
                separation.raw("combined_dopri5_reference_vs_rkf78_candidate", combined.json(false));
                separation.add("integrator_only_max_position_m", integrator_only.position.maximum);
                separation.add("candidate_only_max_position_m", cache_only.position.maximum);
                separation.add("candidate_effect_exceeds_integrator_noise",
                               cache_only.position.maximum > integrator_only.position.maximum);
                separation.add("same_state_density_vs_rkf78_reference",
                               "the same-state block is measured at output states of the dopri5 "
                               "reference; the rkf78 pair re-measures it at the rkf78 reference's "
                               "own states, so it is a second independent sample of the same "
                               "quantity and not a tighter bound on the first");
                separation.add("interpretation",
                               "candidate_only is measured with one integrator, so it is free of "
                               "integrator error; integrator_only is measured with identical "
                               "density, so it is free of candidate error. Comparing the two "
                               "magnitudes is what makes a candidate-induced effect "
                               "distinguishable from numerical noise. A large integrator_only "
                               "value is a pre-existing convergence property of the case and is "
                               "never attributed to the candidate.");
                entry.raw("numerical_reference_separation", separation.str());
            }

            // Cache accounting, read outside every timed region.
            entry.raw("evaluator_counters", evaluator.counts().json());

            if (!run_first) runs << ',';
            run_first = false;
            runs << entry.str();
        }
    }
    runs << ']';
    header.raw("runs", runs.str());
    return header.str();
}

// ---------------------------------------------------------------------------
// Locality: the real request traces
//
// One evaluator per thread, each with its own trace file, so a threaded ensemble
// never has to impose an order on records whose interleaving is not
// deterministic, and a process-isolated chunk analyses only the files its own
// process wrote. The traces are the study's own binary stream, fixed width,
// written without accumulating anything in memory and never as text.
// ---------------------------------------------------------------------------

inline constexpr std::size_t trace_header_bytes = 28;   // magic, u32 version, u32 model, u32 candidate, u64
inline constexpr std::size_t trace_record_bytes = 68;   // et, x, y, z, altitude, lat, lon, density, u32 flags
// Bounded analysis. A trace longer than this is reported as truncated rather
// than silently summarised; the bound is a memory bound, not a sampling choice.
// Explicit, published bound on how many trace records one analysis materialises.
// Roughly 48 bytes of sample state per record, so the cap is a memory bound, not
// a sampling choice: a longer trace is reported as truncated with its true
// record count beside it.
inline constexpr std::size_t trace_analysis_record_cap = 2000000;
inline constexpr std::size_t identity_table_cap = 1000000;
inline constexpr std::size_t cell_table_cap = 600000;

struct TraceSample {
    double et = 0;
    leo::InertialVector position{};
    double altitude_m = 0;
    double latitude_deg = 0;
    double longitude_deg = 0;
    double density = 0;
    std::uint32_t flags = 0;
    // Interned handle into the analysis's WeatherInterner. The handle stands for
    // the full WeatherIdentity vector, never for a hash of it.
    std::uint32_t weather = 0;
};

struct TraceFile {
    std::filesystem::path path;
    std::uint64_t records_on_disk = 0;
    bool truncated_for_analysis = false;
    std::size_t bytes = 0;
    std::vector<TraceSample> samples;
};

struct TraceSummary {
    std::filesystem::path path;
    std::uint64_t records_on_disk = 0;
    std::size_t records_analysed = 0;
    bool truncated_for_analysis = false;
    std::size_t bytes = 0;
};

// Reads one binary trace file produced by the study TraceWriter. The weather
// identity is recomputed from the epoch with exactly the sequence
// DensityEvaluator::build_inputs uses, so the weather grouping in the locality
// statistics is the weather grouping the candidates themselves enforce.
inline TraceFile read_trace(const Config& c, const Environment& env, leo::atmosphere::Model model,
                            const std::filesystem::path& path, WeatherInterner& interner) {
    std::ifstream header_stream(path, std::ios::binary);
    if (!header_stream) throw std::runtime_error("cannot read trace " + path.string());
    char magic[8] = {};
    header_stream.read(magic, sizeof magic);
    std::uint32_t version = 0;
    header_stream.read(reinterpret_cast<char*>(&version), sizeof version);
    if (!header_stream) throw std::runtime_error("truncated trace header " + path.string());
    if (std::memcmp(magic, "ATMCTRC1", 8) != 0)
        throw std::runtime_error("bad trace magic in " + path.string());
    if (version != 1) throw std::runtime_error("unsupported trace version");

    // The file is read one fixed-width record at a time, never as a whole, and at
    // most analysis_record_cap records are materialised. Memory is therefore
    // bounded by that cap however long the trace is; a trace longer than the cap
    // is reported as truncated rather than silently summarised, and the shortfall
    // is published with the file's true record count.
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("cannot read trace " + path.string());
    TraceFile trace;
    trace.path = path;
    const auto size = static_cast<std::size_t>(stream.tellg());
    const std::size_t available =
        size > trace_header_bytes ? (size - trace_header_bytes) / trace_record_bytes : 0;
    const std::size_t records =
        available > trace_analysis_record_cap ? trace_analysis_record_cap : available;
    trace.records_on_disk = available;
    trace.truncated_for_analysis = records < available;
    trace.bytes = size;
    trace.samples.reserve(records);
    stream.seekg(static_cast<std::streamoff>(trace_header_bytes));
    std::vector<unsigned char> record_bytes(trace_record_bytes);
    for (std::size_t index = 0; index < records; ++index) {
        stream.read(reinterpret_cast<char*>(record_bytes.data()),
                    static_cast<std::streamsize>(trace_record_bytes));
        if (!stream) throw std::runtime_error("truncated trace body " + path.string());
        const auto* base = record_bytes.data();
        TraceSample sample;
        std::memcpy(&sample.et, base + 0, sizeof sample.et);
        std::memcpy(&sample.altitude_m, base + 32, sizeof sample.altitude_m);
        std::memcpy(&sample.latitude_deg, base + 40, sizeof sample.latitude_deg);
        std::memcpy(&sample.longitude_deg, base + 48, sizeof sample.longitude_deg);
        std::memcpy(&sample.density, base + 56, sizeof sample.density);
        std::memcpy(&sample.flags, base + 64, sizeof sample.flags);
        double x = 0, y = 0, z = 0;
        std::memcpy(&x, base + 8, sizeof x);
        std::memcpy(&y, base + 16, sizeof y);
        std::memcpy(&z, base + 24, sizeof z);
        sample.position = leo::InertialVector{x, y, z};
        sample.weather = interner.intern(weather_identity(c, env, model, leo::Epoch{sample.et}));
        trace.samples.push_back(sample);
    }
    return trace;
}

// The complete native input identity of a request: the exact epoch bits, the
// three original inertial position bits the provider receives, and the interned
// discrete space-weather vector. That is exactly what candidate A's complete
// identity is built from, so a locality "exact repeat" here means the same thing
// it means to the memoising candidate, and nothing weaker counts as reuse: no
// cell, no memo and no interpolation ever spans a different weather vector.
struct RequestIdentity {
    std::uint64_t et_bits = 0;
    std::array<std::uint64_t, 3> position_bits{};
    std::uint32_t weather = 0;
    bool operator<(const RequestIdentity& other) const noexcept {
        return std::tie(weather, et_bits, position_bits) <
               std::tie(other.weather, other.et_bits, other.position_bits);
    }
};

inline RequestIdentity request_identity(const TraceSample& sample) {
    RequestIdentity identity;
    std::memcpy(&identity.et_bits, &sample.et, sizeof identity.et_bits);
    const double coordinates[]{sample.position.x(), sample.position.y(), sample.position.z()};
    for (std::size_t axis = 0; axis < 3; ++axis)
        std::memcpy(&identity.position_bits[axis], &coordinates[axis], sizeof(double));
    identity.weather = sample.weather;
    return identity;
}

// A quantised cell plus its weather vector. The weather vector is part of the
// key, not a filter applied afterwards, so a cell can never straddle a weather
// record change even when both records fall in the same hour.
struct QuantisedCellKey {
    std::int64_t altitude = 0;
    std::int64_t latitude = 0;
    std::int64_t longitude = 0;
    std::int64_t time = 0;
    std::uint32_t weather = 0;
    bool operator<(const QuantisedCellKey& other) const noexcept {
        return std::tie(altitude, latitude, longitude, time, weather) <
               std::tie(other.altitude, other.latitude, other.longitude, other.time, other.weather);
    }
};

inline double longitude_gap(double a, double b) { return std::fmod(a - b + 540.0, 360.0) - 180.0; }

struct ResolutionOutcome {
    std::size_t requests = 0;
    std::size_t repeat_hits = 0;
    std::size_t cross_trajectory_hits = 0;
    std::size_t requests_skipped_at_cap = 0;
    bool cap_reached = false;
};

// Cell geometry is exactly the candidates' own quantisation, through the shared
// CellScheme helpers, so a locality number can never describe a different cell
// from the one a candidate would have used.
inline std::vector<CellScheme> locality_cell_schemes(const CellScheme& base) {
    auto scaled = [&](double altitude_factor, double latitude_factor, double longitude_factor,
                      double time_factor) {
        CellScheme scheme;
        scheme.altitude_km = base.altitude_km * altitude_factor;
        scheme.latitude_deg = std::max(1e-3, base.latitude_deg * latitude_factor);
        scheme.longitude_deg = std::max(1e-3, base.longitude_deg * longitude_factor);
        scheme.time_s = std::max(1e-6, base.time_s * time_factor);
        return scheme;
    };
    return {scaled(1.0, 1.0, 1.0, 1.0), scaled(0.5, 1.0, 1.0, 1.0),
            scaled(2.0, 1.0, 1.0, 1.0),   scaled(1.0, 2.0, 2.0, 1.0),
            scaled(1.0, 0.5, 0.5, 1.0),   scaled(0.25, 0.25, 0.25, 0.1)};
}

// A shared-cache run writes one continuous trace for the whole ensemble. The
// evaluator's own cumulative query count gives the exact record boundary of each
// member, so the shared trace is split into one sub-trace per member and analysed
// as separate trajectories: within-member order and cross-member reuse are then
// measured correctly instead of being read off a single concatenated sequence.
inline TraceFile split_trace(const TraceFile& source, std::size_t begin, std::size_t end) {
    TraceFile piece;
    piece.path = source.path;
    piece.records_on_disk = end - begin;
    piece.bytes = (end - begin) * trace_record_bytes;
    piece.truncated_for_analysis = source.truncated_for_analysis;
    piece.samples.assign(source.samples.begin() + static_cast<std::ptrdiff_t>(begin),
                         source.samples.begin() + static_cast<std::ptrdiff_t>(end));
    return piece;
}

struct LocalityOutcome {
    std::size_t trajectories = 0;
    std::size_t requests = 0;
    std::size_t distinct_requests = 0;
    std::size_t exact_repeat_hits = 0;
    std::size_t cross_trajectory_exact_repeats = 0;
    bool identity_cap_reached = false;
    std::size_t distinct_weather_identities = 0;
    std::size_t weather_transitions_within_trajectory = 0;
    std::size_t requests_with_changed_weather_since_trajectory_start = 0;
    DeltaDistribution epoch_delta, altitude_delta, latitude_delta, longitude_delta;
    DeltaDistribution altitude, latitude, longitude;
    std::vector<ResolutionOutcome> resolutions;
    std::vector<TraceSummary> traces;

    std::string json(const std::string& scope, const std::vector<CellScheme>& schemes,
                     const std::string& sharing_note, const std::vector<std::string>& summaries) const {
        Json j;
        j.add("scope", scope);
        j.add("sharing_note", sharing_note);
        j.add("trajectories", static_cast<double>(trajectories));
        j.add("requests", static_cast<double>(requests));
        j.raw("consecutive_epoch_difference_s",
              epoch_delta.json("signed, in the real request order inside one trajectory; a zero "
                               "entry is a repeated or retried evaluation, which the adaptive "
                               "integrator does produce"));
        j.raw("consecutive_altitude_difference_m", altitude_delta.json(""));
        j.raw("consecutive_latitude_difference_deg", latitude_delta.json(""));
        j.raw("consecutive_longitude_difference_deg",
              longitude_delta.json("signed, shortest way across the longitude wrap"));
        j.raw("altitude_m", altitude.json(""));
        j.raw("latitude_deg", latitude.json(""));
        j.raw("longitude_deg", longitude.json(""));
        j.add("distinct_exact_request_identities", static_cast<double>(distinct_requests));
        j.add("exact_repeat_hits", static_cast<double>(exact_repeat_hits));
        j.add("exact_repeat_fraction",
              requests ? double(exact_repeat_hits) / double(requests) : 0.0);
        j.add("exact_repeat_definition",
              "an earlier request with bitwise identical epoch, model-received geodetic "
              "coordinates and discrete space-weather vector; nothing weaker counts as reuse");
        j.add("cross_trajectory_exact_repeats",
              static_cast<double>(cross_trajectory_exact_repeats));
        j.add("identity_table_cap", static_cast<double>(identity_table_cap));
        j.add("identity_table_cap_reached", identity_cap_reached);
        j.add("identity_table_note",
              identity_cap_reached
                  ? "the exact-identity table reached its bound, so distinct counts are a lower "
                    "bound and repeat fractions cover the requests actually recorded"
                  : "every recorded request was inserted into the exact-identity table");

        Json weather;
        weather.add("distinct_weather_driver_identities",
                    static_cast<double>(distinct_weather_identities));
        weather.add("identity_is_full_vector",
                    "every identity is the shared WeatherIdentity word vector compared in full; a "
                    "64-bit summary is reported below for traceability only and never decides "
                    "acceptance, reuse or refusal");
        weather.add("identity_summaries_report_only", strings(summaries));
        weather.add("transitions_within_a_trajectory",
                    static_cast<double>(weather_transitions_within_trajectory));
        weather.add("requests_with_weather_different_from_their_trajectory_start",
                    static_cast<double>(requests_with_changed_weather_since_trajectory_start));
        weather.add("identity_content",
                    "civil day, leap-second flag and Ap-history switch, then the archive drivers "
                    "the backend consumes: NRLMSISE-00 previous-day f107, centred-81-day f107 "
                    "average, current daily Ap, the seven 3-hourly Ap values, year and day of "
                    "year; JB2008 the eight SOLFSMY lag proxies and DSTDTC");
        weather.add("dsttc_transition_rule",
                    "DSTDTC is floor(hourly interpolation + 0.5), so it changes inside an hour; it "
                    "is part of the weather identity and every cell key below includes the "
                    "weather identity, so no cell and no interpolation can span one of those "
                    "transitions");
        j.raw("weather_input_identity", weather.str());

        std::ostringstream cells;
        cells << std::setprecision(17) << '[';
        for (std::size_t index = 0; index < schemes.size(); ++index) {
            if (index) cells << ',';
            Json row;
            row.raw("scheme", schemes[index].json());
            row.add("requests", static_cast<double>(resolutions[index].requests));
            row.add("repeat_hits", static_cast<double>(resolutions[index].repeat_hits));
            row.add("repeat_fraction",
                    requests ? double(resolutions[index].repeat_hits) / double(requests) : 0.0);
            row.add("cross_trajectory_hits",
                    static_cast<double>(resolutions[index].cross_trajectory_hits));
            row.add("cross_trajectory_fraction",
                    requests ? double(resolutions[index].cross_trajectory_hits) / double(requests)
                             : 0.0);
                row.add("requests_skipped_at_cell_cap",
                    static_cast<double>(resolutions[index].requests_skipped_at_cap));
            row.add("requests_note",
                    "every recorded request is counted at every resolution, including the ones "
                    "whose cell could not be stored once the table was full");
            row.add("cell_table_cap", static_cast<double>(cell_table_cap));
            row.add("cell_table_cap_reached", resolutions[index].cap_reached);
            row.add("note",
                    "reuse is measured against the requests actually recorded; a cap hit means a "
                    "lower bound, never an estimate");
            cells << row.str();
        }
        cells << ']';
        j.raw("quantised_cell_reuse", cells.str());

        std::vector<std::string> file_rows;
        for (const auto& trace : traces) {
            Json row;
            row.add("path", trace.path.string());
            row.add("bytes", static_cast<double>(trace.bytes));
            row.add("records_on_disk", static_cast<double>(trace.records_on_disk));
            row.add("records_analysed", static_cast<double>(trace.records_analysed));
            row.add("truncated_for_analysis", trace.truncated_for_analysis);
            row.add("record_bytes", static_cast<double>(trace_record_bytes));
            file_rows.push_back(row.str());
        }
        j.raw("traces", raw_array(file_rows));
        j.add("trace_note",
              "the traces are the study's own fixed-width binary stream; they are read one "
              "record at a time and never loaded as a whole, up to the published "
              "analysis_record_cap; nothing is logged as text");
        return j.str();
    }
};

inline LocalityOutcome analyse_traces(const std::vector<TraceFile>& files,
                                      const std::vector<CellScheme>& schemes) {
    LocalityOutcome outcome;
    for (const auto& file : files)
        outcome.traces.push_back(TraceSummary{file.path, file.records_on_disk,
                                              file.samples.size(), file.truncated_for_analysis,
                                              file.bytes});
    outcome.resolutions.assign(schemes.size(), ResolutionOutcome{});
    // Ordered maps over the complete keys. No hash is involved in any accept or
    // refuse decision, so a collision can never merge two distinct identities.
    std::map<RequestIdentity, std::uint32_t> identity_owner;
    std::vector<std::map<QuantisedCellKey, std::uint32_t>> cell_owner(schemes.size());
    std::set<std::uint32_t> weather_identities;

    // One time origin for the whole analysed group keeps the time axis comparable
    // between the trajectories of an ensemble.
    double origin = 0.0;
    bool have_origin = false;
    for (const auto& file : files)
        if (!file.samples.empty() && (!have_origin || file.samples.front().et < origin)) {
            origin = file.samples.front().et;
            have_origin = true;
        }

    // Files are analysed in ascending trajectory order. Within a process that
    // order is real for a sequential run; for a threaded run it is imposed, which
    // is why a threaded scope reports reuse potential and never achieved reuse.
    for (std::size_t file_index = 0; file_index < files.size(); ++file_index) {
        const auto& file = files[file_index];
        const auto trajectory = static_cast<std::uint32_t>(file_index);
        ++outcome.trajectories;
        outcome.requests += file.samples.size();
        std::uint64_t trajectory_weather = 0;
        bool has_trajectory_weather = false;
        for (std::size_t index = 0; index < file.samples.size(); ++index) {
            const auto& sample = file.samples[index];
            weather_identities.insert(sample.weather);
            outcome.altitude.add(sample.altitude_m);
            outcome.latitude.add(sample.latitude_deg);
            outcome.longitude.add(sample.longitude_deg);
            if (index == 0) {
                trajectory_weather = sample.weather;
                has_trajectory_weather = true;
            } else {
                const auto& previous = file.samples[index - 1];
                outcome.epoch_delta.add(sample.et - previous.et);
                outcome.altitude_delta.add(sample.altitude_m - previous.altitude_m);
                outcome.latitude_delta.add(sample.latitude_deg - previous.latitude_deg);
                outcome.longitude_delta.add(longitude_gap(sample.longitude_deg,
                                                          previous.longitude_deg));
                if (sample.weather != previous.weather) ++outcome.weather_transitions_within_trajectory;
            }
            if (!has_trajectory_weather) {
                trajectory_weather = sample.weather;
                has_trajectory_weather = true;
            } else if (sample.weather != trajectory_weather) {
                ++outcome.requests_with_changed_weather_since_trajectory_start;
            }

            const auto identity = request_identity(sample);
            const auto entry = identity_owner.find(identity);
            if (entry != identity_owner.end()) {
                ++outcome.exact_repeat_hits;
                if (entry->second != trajectory) ++outcome.cross_trajectory_exact_repeats;
            } else if (identity_owner.size() < identity_table_cap) {
                identity_owner.emplace(identity, trajectory);
                ++outcome.distinct_requests;
            } else {
                outcome.identity_cap_reached = true;
            }

            for (std::size_t r = 0; r < schemes.size(); ++r) {
                auto& table = cell_owner[r];
                const CellScheme& scheme = schemes[r];
                const QuantisedCellKey key{cell_index_altitude(sample.altitude_m / 1000.0, scheme),
                                           cell_index_latitude(sample.latitude_deg, scheme),
                                           cell_index_longitude(sample.longitude_deg, scheme),
                                           cell_index_time(sample.et, origin, scheme),
                                           sample.weather};
                // Every recorded request counts against this resolution, whether
                // or not it produced a new cell.
                ++outcome.resolutions[r].requests;
                const auto cell = table.find(key);
                if (cell != table.end()) {
                    ++outcome.resolutions[r].repeat_hits;
                    if (cell->second != trajectory)
                        ++outcome.resolutions[r].cross_trajectory_hits;
                } else if (table.size() < cell_table_cap) {
                    table.emplace(key, trajectory);
                } else {
                    ++outcome.resolutions[r].requests_skipped_at_cap;
                    outcome.resolutions[r].cap_reached = true;
                }
            }
        }
    }
    outcome.distinct_weather_identities = weather_identities.size();
    return outcome;
}

// One file per trajectory and per scope. The name carries the seed, so a
// process-isolated chunk's traces are distinct from every other chunk's and two
// chunks can never overwrite one another's evidence.
inline std::filesystem::path locality_trace_path(const Config& c, const StudySettings& s,
                                                 std::size_t trajectory, const char* tag,
                                                 const std::string& case_id) {
    if (!s.trace_path.empty())
        return std::filesystem::path(s.trace_path + "-" + case_id + "-" + tag + "-" +
                                    std::to_string(trajectory) + ".trace");
    std::ostringstream name;
    name << "locality-" << candidate_slug(s.candidate_kind) << '-' << model_name(s.model_kind)
         << "-seed" << s.seed << '-' << case_id << '-' << tag << '-' << std::to_string(trajectory) << ".trace";
    return std::filesystem::path(c.out) / "traces" / name.str();
}

// Auxiliary locality probe: deterministic query sequences that are NOT a coupled
// RHS sequence. They exist to characterise reuse under a clustered regime and a
// uniform random regime at a known resolution, and are labelled as such wherever
// they appear.
inline std::string packet_probe(const Config& c, Environment& env, const StudySettings& s,
                                const std::vector<QuerySample>& samples, const std::string& regime,
                                const std::vector<CellScheme>& schemes) {
    const auto model = required_model(s);
    StudySettings probe_settings = evaluator_settings_for(s);
    probe_settings.trace_path.clear();
    DensityEvaluator evaluator(c, env, model, probe_settings);
    const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
    WeatherInterner interner;
    std::map<RequestIdentity, std::uint32_t> identity_owner;
    std::vector<std::map<QuantisedCellKey, std::uint32_t>> cell_owner(schemes.size());
    std::size_t exact_repeats = 0, requests = 0, capped = 0;
    for (const auto& sample : samples) {
        const double density = evaluator.evaluate(sample.epoch, sample.position, nullptr);
        if (!std::isfinite(density) || density <= 0)
            throw std::runtime_error("auxiliary probe produced an invalid density");
        ++requests;
        const auto rotation =
            leo::SpiceEphemerisProvider::earth_fixed_rotation(sample.epoch, c.frame);
        const auto geodetic = leo::atmosphere::geodetic(rotation * sample.position.as_eigen());
        TraceSample record;
        record.et = sample.epoch.et_seconds;
        record.altitude_m = geodetic.altitude_m;
        record.latitude_deg = geodetic.latitude_rad * 180 / std::numbers::pi;
        record.longitude_deg = geodetic.longitude_rad * 180 / std::numbers::pi;
        record.weather = interner.intern(weather_identity(c, env, model, sample.epoch));
        const auto identity = request_identity(record);
        const auto inserted = identity_owner.emplace(identity, 0u);
        if (!inserted.second) ++exact_repeats;
        for (std::size_t r = 0; r < schemes.size(); ++r) {
            const CellScheme& scheme = schemes[r];
            const QuantisedCellKey key{cell_index_altitude(record.altitude_m / 1000.0, scheme),
                                       cell_index_latitude(record.latitude_deg, scheme),
                                       cell_index_longitude(record.longitude_deg, scheme),
                                       cell_index_time(record.et, epoch.et_seconds, scheme),
                                       record.weather};
            auto& table = cell_owner[r];
            if (table.emplace(key, 0u).second) {
                if (table.size() >= cell_table_cap) {
                    ++capped;
                    table.erase(key);
                }
            }
        }
    }
    Json j;
    j.add("regime", regime);
    j.add("is_coupled_rhs_sequence", false);
    j.add("note",
          "a deterministic synthetic query sequence generated by the study's own packet "
          "generator; it is not produced by the coupled RHS and is never mixed with the real "
          "trajectory request counts");
    j.add("requests", static_cast<double>(requests));
    j.add("exact_repeat_hits", static_cast<double>(exact_repeats));
    j.add("exact_repeat_fraction",
          requests ? double(exact_repeats) / double(requests) : 0.0);
    j.add("requests_skipped_at_cell_cap", static_cast<double>(capped));
    std::ostringstream cells;
    cells << std::setprecision(17) << '[';
    bool first = true;
    for (std::size_t r = 0; r < schemes.size(); ++r) {
        if (!first) cells << ',';
        first = false;
        Json row;
        row.raw("scheme", schemes[r].json());
        row.add("distinct_cells", static_cast<double>(cell_owner[r].size()));
        row.add("reuse_fraction",
                requests ? 1.0 - double(cell_owner[r].size()) / double(requests) : 0.0);
        cells << row.str();
    }
    cells << ']';
    j.raw("cell_reuse", cells.str());
    j.raw("evaluator_counters", evaluator.counts().json());
    return j.str();
}

inline std::string locality_study(const Config& c, Environment& env, const StudySettings& s) {
    if (c.case_files.empty()) throw std::invalid_argument("locality study needs a --case");
    const auto model = required_model(s);
    const auto settings = evaluator_settings_for(s);
    const std::size_t members = std::max<std::size_t>(1, s.ensemble);
    const std::size_t workers = std::max<std::size_t>(1, s.thread_workers);
    const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
    const auto labels = sample_labels(c);
    const auto schemes = locality_cell_schemes(s.base_cells);

    Json header;
    header.add("study_kind", std::string("locality"));
    header.add("protocol",
               "Phase 3 real trajectory-query locality. The traced sequences are the atmospheric "
               "requests the production coupled RHS actually makes during the production "
               "propagation, one fixed-width binary record per request, streamed to disk with no "
               "in-memory accumulation and never as text.");
    header.add("model", model_name(model));
    header.add("candidate", candidate_name(s.candidate_kind));
    header.add("epoch_utc", utc_label(epoch));
    header.add("duration_s", c.duration);
    header.add("output_cadence_s", c.cadence);
    header.add("ensemble_members", static_cast<double>(members));
    header.add("thread_workers", static_cast<double>(workers));
    header.add("perturbation_level", std::string(perturbation_level(perturbation_scale(s)).name));
    header.add("perturbation_seed", static_cast<double>(s.seed));
    header.add("trace_record_bytes", static_cast<double>(trace_record_bytes));
    header.add("trace_header_bytes", static_cast<double>(trace_header_bytes));
    header.add("analysis_record_cap", static_cast<double>(trace_analysis_record_cap));
    header.add("trace_scope_rule",
               "one evaluator and one trace file per trajectory, so a threaded ensemble never "
               "imposes an order on records whose interleaving is not deterministic and a "
               "process-isolated chunk analyses only the files its own process wrote");
    header.add("cell_geometry",
               "every reuse figure below uses the study's shared cell_index_* helpers and the "
               "same CellScheme the candidates themselves use, so a locality number can never "
               "describe a different cell from the one a candidate would have used");
    header.raw("base_cell_scheme", s.base_cells.json());

    // One interner per result: a handle stands for the full WeatherIdentity
    // vector across every scope, and every comparison below is a comparison of
    // those vectors.
    WeatherInterner interner;
    std::vector<std::string> summaries;
    std::vector<std::string> scope_rows;
    std::size_t traces_written = 0;
    // Every trace this process reads is one it wrote itself. Nothing else is
    // loaded, so a chunk can never absorb another chunk's requests.
    const auto load = [&](const std::filesystem::path& trace_path) {
        ++traces_written;
        return read_trace(c, env, model, trace_path, interner);
    };
    for (const auto& case_file : c.case_files) {
        const auto spec = experiment::load(case_file);
        std::vector<leo::State> initials;
        std::vector<Perturbation> perturbations;
        initials.reserve(members);
        for (std::size_t member = 0; member < members; ++member) {
            perturbations.push_back(perturb_state(spec.start(), s.seed, member, perturbation_scale(s)));
            initials.push_back(perturbations.back().state);
        }

        // ---- single trajectory ----
        {
            auto trace_settings = settings;
            trace_settings.trace_path = locality_trace_path(c, s, 0, "single", spec.id);
            DensityEvaluator evaluator(c, env, model, trace_settings);
            CandidateDensitySource source(evaluator);
            run_coupled(c, env, spec, initials.front(), source, evaluator, labels, epoch, false);
            evaluator.finish_trace();
            const auto trace = load(trace_settings.trace_path);
            const auto outcome = analyse_traces({trace}, schemes);
            scope_rows.push_back(outcome.json("single_trajectory", schemes,
                                              "one trajectory, one evaluator", summaries));
        }

        // ---- sequential ensemble, one shared cache ----
        // This is the configuration whose cross-trajectory reuse is actually
        // achievable for a process-local cache.
        if (members > 1) {
            auto trace_settings = settings;
            trace_settings.trace_path = locality_trace_path(c, s, 0, "shared", spec.id);
            DensityEvaluator evaluator(c, env, model, trace_settings);
            CandidateDensitySource source(evaluator);
            std::vector<std::size_t> boundaries{0};
            for (std::size_t member = 0; member < members; ++member) {
                run_coupled(c, env, spec, initials[member], source, evaluator, labels, epoch, false, false);
                boundaries.push_back(evaluator.counts().queries);
            }
            evaluator.finish_trace();
            auto trace = load(trace_settings.trace_path);
            std::vector<TraceFile> member_traces;
            for (std::size_t member = 0; member < members; ++member) {
                TraceFile part;
                part.path = trace.path;
                const auto begin = std::min(boundaries[member], trace.samples.size());
                const auto end = std::min(boundaries[member + 1], trace.samples.size());
                part.records_on_disk = boundaries[member + 1] - boundaries[member];
                part.truncated_for_analysis = end - begin < part.records_on_disk;
                part.samples.insert(part.samples.end(), trace.samples.begin() + begin, trace.samples.begin() + end);
                member_traces.push_back(std::move(part));
            }
            trace.samples.clear();
            trace.samples.shrink_to_fit();
            const auto outcome = analyse_traces(member_traces, schemes);
            scope_rows.push_back(outcome.json("sequential_ensemble_shared_cache", schemes,
                                              "one evaluator and one trace for the whole "
                                              "ensemble, evaluated sequentially: the cross-"
                                              "trajectory reuse here is achieved reuse",
                                              summaries));
        }

        // ---- sequential ensemble, one cache per trajectory ----
        // The requests are the same; only the ownership differs, so comparing
        // this scope with the shared one measures exactly what cache ownership
        // buys inside one process.
        if (members > 1) {
            std::vector<TraceFile> traces;
            for (std::size_t member = 0; member < members; ++member) {
                auto trace_settings = settings;
                trace_settings.trace_path = locality_trace_path(c, s, member, "private", spec.id);
                DensityEvaluator evaluator(c, env, model, trace_settings);
                CandidateDensitySource source(evaluator);
                run_coupled(c, env, spec, initials[member], source, evaluator, labels, epoch, false);
                evaluator.finish_trace();
                traces.push_back(load(trace_settings.trace_path));
            }
            const auto outcome = analyse_traces(traces, schemes);
            scope_rows.push_back(outcome.json("sequential_ensemble_per_trajectory_cache", schemes,
                                              "one evaluator and one trace per trajectory, "
                                              "evaluated sequentially: cross_trajectory_* "
                                              "figures are reuse potential if the caches were "
                                              "shared, not achieved reuse", summaries));
        }

        // ---- threaded ensemble, one cache per worker thread ----
        // DensityEvaluator is mutable and not thread safe, so every trajectory owns
        // its evaluator and they are all constructed on this thread. The public
        // batch API is used exactly as MT-002 documents: the factory is called on
        // the worker that owns the trajectory and the returned RHS object is only
        // dereferenced by that worker.
        if (members > 1 && workers > 1) {
            std::vector<std::unique_ptr<DensityEvaluator>> evaluators;
            std::vector<std::string> paths;
            for (std::size_t member = 0; member < members; ++member) {
                auto trace_settings = settings;
                trace_settings.thread_workers = 1;
                trace_settings.trace_path = locality_trace_path(c, s, member, "threaded", spec.id);
                evaluators.push_back(
                    std::make_unique<DensityEvaluator>(c, env, model, trace_settings));
                paths.push_back(trace_settings.trace_path);
            }
            const auto dynamics = case_dynamics(spec);
            const leo::EarthDynamics& rhs_dynamics = dynamics;
            // The batch hands the initial state to propagate() itself, not to the
            // RHS, so the RHS only needs the trajectory index to pick its own
            // evaluator, trace writer and dynamics object.
            const auto factory = [&](std::size_t member, const leo::State&) {
                DensityEvaluator& evaluator = *evaluators[member];
                return [&](leo::ElapsedTime t, const leo::State& state) {
                    const leo::Epoch at{epoch.et_seconds + t.seconds};
                    const auto observation = env.ephemeris.observe_sun(at);
                    const double density = evaluator.evaluate(at, state.r, &observation);
                    if (!std::isfinite(density) || density < 0)
                        throw std::runtime_error("invalid density in a threaded locality run");
                    return rhs_dynamics(t, state, coupled_snapshot(env, at, state, density, &observation));
                };
            };
            leo::IntegrationOptions options = tolerances();
            options.rtol = c.rtol;
            options.max_step_s = c.max_step;
            options.initial_step_s = std::min(1.0, c.max_step);
            options.max_attempts = leo::IntegrationOptions{}.max_attempts;
            leo::propagate_batch(factory, initials, epoch, labels, options, workers);
            std::vector<TraceFile> traces;
            for (std::size_t member = 0; member < members; ++member) {
                evaluators[member]->finish_trace();
                traces.push_back(load(paths[member]));
            }
            const auto outcome = analyse_traces(traces, schemes);
            scope_rows.push_back(outcome.json("threaded_ensemble_per_trajectory_cache", schemes,
                                              "one evaluator per worker thread; records from "
                                              "different trajectories are not ordered relative to "
                                              "each other, so only within-trajectory order is "
                                              "measured and cross_trajectory_* figures are reuse "
                                              "potential, never achieved reuse", summaries));
        }

        // ---- the authoritative native request sequence ----
        // The control candidate performs one fresh native evaluation per query
        // through the same acquisition path, so this trace is the request
        // sequence the authoritative models actually receive, and it is the
        // reference against which a candidate's own request sequence is read.
        {
            auto trace_settings = settings;
            trace_settings.candidate_kind = Candidate::control;
            trace_settings.candidate = "E";
            trace_settings.trace_path = locality_trace_path(c, s, 0, "native", spec.id);
            DensityEvaluator evaluator(c, env, model, trace_settings);
            CandidateDensitySource source(evaluator);
            run_coupled(c, env, spec, initials.front(), source, evaluator, labels, epoch, false);
            evaluator.finish_trace();
            const auto trace = load(trace_settings.trace_path);
            const auto outcome = analyse_traces({trace}, schemes);
            scope_rows.push_back(outcome.json("authoritative_native_request_sequence", schemes,
                                              "one trajectory evaluated by the control candidate: "
                                              "every query is one fresh native evaluation, so this "
                                              "is the native request sequence", summaries));
        }

        // ---- auxiliary synthetic regimes ----
        {
            const std::size_t per_packet = std::max<std::size_t>(2, s.points / 8);
            const std::size_t packet_count = std::max<std::size_t>(1, s.points / per_packet);
            const auto reference_state = spec.start();
            const auto clustered = make_local_packets(epoch, reference_state.r, packet_count,
                                                      per_packet, mix64(s.seed), 5.0, 60.0);
            const auto uniform = make_random_packets(epoch, clustered.size(), mix64(s.seed), 400.0,
                                                     900.0, 60.0, c.frame);
            scope_rows.push_back(packet_probe(c, env, s, clustered, "clustered_packets", schemes));
            scope_rows.push_back(packet_probe(c, env, s, uniform, "uniform_random_packets", schemes));
        }
    }
    header.raw("scopes", raw_array(scope_rows));
    {
        summaries = interner.summaries();
        header.raw("weather_identity_vectors", strings(summaries));
        header.add("weather_identity_vector_count", static_cast<double>(interner.size()));
        header.add("weather_identity_rule",
                   "every identity above is the shared WeatherIdentity word vector, compared in "
                   "full; the 64-bit summaries are reported for traceability only and no accept, "
                   "reuse or refuse decision anywhere in this study is taken on a hash");
    }

    // Process-local chunk identity. A process-isolated chunk is one (seed,
    // ensemble) pair; each process writes its own traces and reads only its own.
    Json chunk;
    chunk.add("chunk_identity", std::string("(study seed, ensemble size, model, candidate)"));
    chunk.add("chunk_seed", static_cast<double>(s.seed));
    chunk.add("chunk_ensemble", static_cast<double>(members));
    // Global chunk window and repeat group, exactly as the runner issued them.
    // They identify this process's slice of the global work; nothing outside the
    // window is read and nothing inside another chunk's window is counted here.
    chunk.add("global_chunk_begin", static_cast<double>(s.begin));
    chunk.add("global_chunk_count", static_cast<double>(s.count));
    chunk.add("global_chunk_repeats", static_cast<double>(s.repeats));
    chunk.add("chunk_window_rule",
               "begin and count address the runner's global work range; this process only ever "
               "reads the trace files it wrote, so a chunk can never contribute to or consume "
               "another chunk's reuse counts");
    chunk.add("chunk_model", model_name(model));
    chunk.add("chunk_candidate", candidate_name(s.candidate_kind));
    chunk.add("traces_written_by_this_process", static_cast<double>(traces_written));
    chunk.add("other_chunks_data_read", false);
    chunk.add("aggregate_hit_rate_across_chunks_published", false);
    chunk.add("why",
              "a process-local cache is not shared between processes, so reuse between chunks is "
              "not an achievable hit rate. This result covers only what this process itself "
              "requested and never merges another chunk's requests into its own counts.");
    header.raw("process_chunk", chunk.str());

    Json isolation;
    isolation.add("cross_process_reuse_counted_as_achievable", false);
    isolation.add("why",
                  "an ordinary process-local cache is not shared between processes, so reuse "
                  "between two processes is not an achievable hit rate. No aggregate or merged "
                  "hit rate for it is reported anywhere in this result.");
    isolation.add("how_it_is_assessed",
                  "the runner launches independent chunks with distinct --study-seed values; each "
                  "chunk writes its own trace files and this process analyses only those files. "
                  "Reuse inside one chunk is real and is reported; reuse between chunks is never "
                  "inferred from a merged trace.");
    isolation.add("chunk_identity", "the (seed, ensemble) pair");
    isolation.add("shared_memory_or_distributed_cache_considered", false);
    header.raw("process_isolation", isolation.str());
    return header.str();
}

// ---------------------------------------------------------------------------
// benchmark_study
//
// Timed regions contain the production propagation and nothing else: no trace
// writer, no clock read, no cache-statistics call, no diagnostic allocation. The
// per-RHS counter is one integer increment, and every counter and every cache
// statistic is read after the timed region has closed.
// ---------------------------------------------------------------------------

struct BenchmarkRow {
    std::string configuration;
    std::size_t repeat = 0;
    std::size_t order_position = 0;
    double wall_seconds = 0;
    double construction_seconds = 0;
    std::size_t trajectories = 0;
    std::size_t rhs_calls = 0;
    std::size_t density_evaluations = 0;
    std::size_t output_samples = 0;
    std::string checksum;
    std::string cache_statistics;
};

struct BenchmarkConfiguration {
    std::string name;
    std::string workload;  // single | sequential_ensemble | threaded_ensemble | fixed_state_rhs
    bool authoritative = false;  // authoritative direct path instead of a candidate store
    bool cold = false;           // a fresh cache-owning evaluator per trajectory
    bool share_cache = false;    // one evaluator owned by the whole workload
    std::size_t ensemble = 1;
    std::size_t workers = 1;
    std::size_t rhs_evaluations = 0;
};

inline BenchmarkRow run_benchmark_configuration(const Config& c, Environment& env,
                                                 const StudySettings& settings,
                                                 const BenchmarkConfiguration& configuration,
                                                 DensityEvaluator* shared, std::size_t repeat,
                                                 std::size_t order_position) {
    const auto model = required_model(settings);
    const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
    const auto labels = sample_labels(c);
    const auto spec = experiment::load(c.case_files.at(0));
    const auto dynamics = case_dynamics(spec);
    const auto states = [&] {
        std::vector<leo::State> initials;
        const std::size_t members = std::max<std::size_t>(1, configuration.ensemble);
        initials.reserve(members);
        for (std::size_t member = 0; member < members; ++member)
            initials.push_back(perturb_state(spec.start(), settings.seed, member, perturbation_scale(settings)).state);
        return initials;
    }();

    leo::IntegrationOptions options = tolerances();
    options.rtol = c.rtol;
    options.max_step_s = c.max_step;
    options.initial_step_s = std::min(1.0, c.max_step);
    options.max_attempts = leo::IntegrationOptions{}.max_attempts;

    BenchmarkRow row;
    row.configuration = configuration.name;
    row.repeat = repeat;
    row.order_position = order_position;
    Checksum checksum;

    if (configuration.workload == "fixed_state_rhs") {
        // One fixed epoch and one fixed state, so this measures the coupled RHS
        // cost itself and not a mixture of many physical states.
        const auto state = states.front();
        const auto timer_construction = Clock::now();
        std::unique_ptr<DensityEvaluator> owned;
        DensityEvaluator* evaluator = shared;
        if (!evaluator) {
            owned = std::make_unique<DensityEvaluator>(c, env, model, settings);
            evaluator = owned.get();
        }
        row.construction_seconds = elapsed(timer_construction);

        double sink = 0;
        for (std::size_t call = 0; call < configuration.rhs_evaluations; ++call) {
            const auto observation = env.ephemeris.observe_sun(epoch);
            const double density = configuration.authoritative
                ? evaluator->direct(epoch, state.r, &observation)
                : evaluator->evaluate(epoch, state.r, &observation);
            const auto snapshot = coupled_snapshot(env, epoch, state, density, &observation);
            sink += dynamics(leo::ElapsedTime{0}, state, snapshot).v_dot.x();
        }
        row.wall_seconds = elapsed(timer_construction);
        row.rhs_calls = configuration.rhs_evaluations;
        row.density_evaluations = configuration.rhs_evaluations;
        row.trajectories = 1;
        row.output_samples = 1;
        row.checksum = std::to_string(sink);
        if (!configuration.authoritative) row.cache_statistics = evaluator->statistics();
        return row;
    }

    if (configuration.workload == "threaded_ensemble") {
        // One evaluator per trajectory, all constructed on this thread. Each is
        // dereferenced by exactly one worker, which is the ownership contract the
        // public batch API documents. No mutable cache is shared between workers,
        // so no cross-thread hit rate is claimed from this configuration.
        std::vector<std::unique_ptr<DensityEvaluator>> evaluators;
        const auto timer_construction = Clock::now();
        for (std::size_t member = 0; member < states.size(); ++member)
            evaluators.push_back(std::make_unique<DensityEvaluator>(c, env, model, settings));
        row.construction_seconds = elapsed(timer_construction);
        std::vector<std::size_t> rhs_counts(states.size(), 0);
        std::vector<std::size_t> density_counts(states.size(), 0);
        const auto factory = [&](std::size_t member, const leo::State&) {
            DensityEvaluator& evaluator = *evaluators[member];
            std::size_t* rhs = &rhs_counts[member];
            std::size_t* density_count = &density_counts[member];
            return [&, rhs, density_count](leo::ElapsedTime t, const leo::State& state) {
                const leo::Epoch at{epoch.et_seconds + t.seconds};
                const auto observation = env.ephemeris.observe_sun(at);
                const double density = evaluator.evaluate(at, state.r, &observation);
                ++(*density_count);
                ++(*rhs);
                return dynamics(t, state, coupled_snapshot(env, at, state, density, &observation));
            };
        };
        const auto trajectories =
            leo::propagate_batch(factory, states, epoch, labels, options, configuration.workers);
        row.wall_seconds = elapsed(timer_construction);
        for (std::size_t member = 0; member < states.size(); ++member) {
            row.rhs_calls += rhs_counts[member];
            row.density_evaluations += density_counts[member];
            row.output_samples += trajectories[member].samples.size();
            for (const auto& sample : trajectories[member].samples) checksum.add(sample.state);
        }
        row.trajectories = states.size();
        row.checksum = checksum.str();
        return row;
    }

    if (configuration.workload == "sequential_ensemble") {
        const auto timer_construction = Clock::now();
        std::unique_ptr<DensityEvaluator> owned;
        DensityEvaluator* evaluator = shared;
        if (!evaluator && configuration.share_cache) {
            owned = std::make_unique<DensityEvaluator>(c, env, model, settings);
            evaluator = owned.get();
        }
        row.construction_seconds = elapsed(timer_construction);
        DensityEvaluator reference(c, env, model, settings);
        std::vector<DensityStatistics> per_trajectory_statistics;
        per_trajectory_statistics.reserve(states.size());
        for (const auto& state : states) {
            std::unique_ptr<DensityEvaluator> private_evaluator;
            if (!configuration.share_cache) {
                private_evaluator = std::make_unique<DensityEvaluator>(c, env, model, settings);
                evaluator = private_evaluator.get();
            }
            CoupledRun run;
            if (configuration.authoritative) {
                AuthoritativeDensitySource source(*evaluator);
                run = run_coupled(c, env, spec, state, source, reference, labels, epoch, false, false);
            } else {
                CandidateDensitySource source(*evaluator);
                run = run_coupled(c, env, spec, state, source, reference, labels, epoch, false, false);
            }
            row.rhs_calls += run.rhs_calls;
            row.density_evaluations += run.density_evaluations;
            row.output_samples += run.trajectory.samples.size();
            for (const auto& sample : run.trajectory.samples) checksum.add(sample.state);
            if (!configuration.authoritative && !configuration.share_cache)
                per_trajectory_statistics.push_back(evaluator->counts());
        }
        row.wall_seconds = elapsed(timer_construction);
        row.trajectories = states.size();
        row.checksum = checksum.str();
        if (!configuration.authoritative && configuration.share_cache)
            row.cache_statistics = evaluator->statistics();
        if (!configuration.authoritative && !configuration.share_cache) {
            std::vector<std::string> rows;
            for (const auto& statistics : per_trajectory_statistics) rows.push_back(statistics.json());
            Json statistics;
            statistics.raw("per_trajectory", raw_array(rows));
            statistics.add("scope", "independent per-trajectory caches; counters are not shared");
            row.cache_statistics = statistics.str();
        }
        return row;
    }

    // single trajectory
    const auto timer_construction = Clock::now();
    std::unique_ptr<DensityEvaluator> owned;
    DensityEvaluator* evaluator = shared;
    if (!evaluator || configuration.cold) {
        owned = std::make_unique<DensityEvaluator>(c, env, model, settings);
        evaluator = owned.get();
    }
    row.construction_seconds = elapsed(timer_construction);
    DensityEvaluator reference(c, env, model, settings);
    CoupledRun run;
    if (configuration.authoritative) {
        // The authoritative path is the production provider, evaluated afresh for
        // every query exactly as the current baseline does.
        AuthoritativeDensitySource source(reference);
        run = run_coupled(c, env, spec, states.front(), source, reference, labels, epoch, false,
                          false);
    } else {
        CandidateDensitySource source(*evaluator);
        run = run_coupled(c, env, spec, states.front(), source, reference, labels, epoch, false,
                          false);
    }
    row.wall_seconds = elapsed(timer_construction);
    row.trajectories = 1;
    row.rhs_calls = run.rhs_calls;
    row.density_evaluations = run.density_evaluations;
    row.output_samples = run.trajectory.samples.size();
    row.checksum = run.checksum;
    if (!configuration.authoritative) row.cache_statistics = evaluator->statistics();
    return row;
}

inline std::string benchmark_study(const Config& c, Environment& env, const StudySettings& s) {
    if (c.case_files.empty()) throw std::invalid_argument("benchmark study needs a --case");
    if (!s.benchmark_route.empty()) return process_benchmark_study(c, env, s);
    const auto model = required_model(s);
    const auto settings = evaluator_settings_for(s);
    const std::size_t members = std::max<std::size_t>(1, s.ensemble);
    const std::size_t workers = std::max<std::size_t>(1, s.thread_workers);
    // --timing-calls sizes the fixed-state workload; --study-points is a
    // scientific sizing and does not change a timing.
    const std::size_t fixed_state_calls = std::max<std::size_t>(1, c.timing_calls);
    // --study-repeats overrides the driver's --timing-repeats for the whole
    // repeat group, so the runner controls the benchmark group size directly.
    const std::size_t repeats = s.repeats > 0 ? s.repeats : std::max<std::size_t>(1, c.timing_repeats);

    Json header;
    header.add("study_kind", std::string("benchmark"));
    header.add("protocol",
               "Phase 7 end-to-end performance. Every headline number is wall time over a real "
               "production propagation with the candidate supplying the coupled density. The "
               "authoritative configuration is the current fully optimised direct provider and is "
               "the baseline every speedup is measured against.");
    header.add("model", model_name(model));
    header.add("candidate", candidate_name(s.candidate_kind));
    header.add("case_id", experiment::load(c.case_files.at(0)).id);
    header.add("epoch_utc", c.epoch_utc);
    header.add("duration_s", c.duration);
    header.add("output_cadence_s", c.cadence);
    header.add("ensemble_members", static_cast<double>(members));
    header.add("thread_workers", static_cast<double>(workers));
    header.add("fixed_state_rhs_evaluations", static_cast<double>(fixed_state_calls));
    header.add("fixed_state_rhs_sizing", std::string("--timing-calls"));
    header.add("construction_included_in_wall_seconds", true);
    header.add("construction_seconds_reported_separately", true);
    header.add("instrumentation_inside_timed_regions",
               "one integer increment per RHS call and nothing else: no clock read, no trace "
               "writer, no cache-statistics call, no diagnostic allocation");
    header.add("counters_and_cache_statistics_read_outside_every_timed_region", true);
    header.add("in_evaluator_timers_enabled", false);
    header.add("measurement_order",
               "deterministic rotation: repeat r runs configuration ((r + k) mod n), so no "
               "configuration is always measured first and measurement order cannot favour one");
    header.add("warm_up", "one discarded execution of every configuration before any recorded "
                          "repeat, so first-touch and one-time effects are excluded");
    header.add("recorded_repeats", static_cast<double>(repeats));
    header.add("repeats_source", std::string(s.repeats > 0 ? "--study-repeats" : "--timing-repeats"));
    header.add("global_chunk_begin", static_cast<double>(s.begin));
    header.add("global_chunk_count", static_cast<double>(s.count));
    header.add("speedup_rule",
               "a speedup is the ratio of the medians of interleaved repeats of the same "
               "workload, never a difference of independently measured medians");

    std::vector<BenchmarkConfiguration> configurations;
    configurations.push_back({"authoritative_single", "single", true, true, false, 1, 1, 0});
    configurations.push_back({"candidate_single_cold", "single", false, true, false, 1, 1, 0});
    configurations.push_back({"candidate_single_warm", "single", false, false, false, 1, 1, 0});
    if (members > 1) {
        configurations.push_back(
            {"authoritative_sequential_ensemble", "sequential_ensemble", true, true, true, members, 1, 0});
        configurations.push_back({"candidate_sequential_ensemble_shared_cache",
                                  "sequential_ensemble", false, false, true, members, 1, 0});
        configurations.push_back({"candidate_sequential_ensemble_per_trajectory_cache",
                                  "sequential_ensemble", false, true, false, members, 1, 0});
        if (workers > 1)
            configurations.push_back({"candidate_threaded_ensemble_per_trajectory_cache",
                                      "threaded_ensemble", false, true, false, members, workers, 0});
    }
    configurations.push_back({"authoritative_fixed_state_rhs", "fixed_state_rhs", true, true, false,
                              1, 1, fixed_state_calls});
    configurations.push_back({"candidate_fixed_state_rhs", "fixed_state_rhs", false, true, false, 1,
                              1, fixed_state_calls});

    // Warm configurations own one evaluator for the whole benchmark; cold ones
    // construct a fresh one on every execution. The difference between them is
    // therefore exactly cache ownership and nothing else. Both objects are built
    // before any timed region.
    auto warm_single = std::make_unique<DensityEvaluator>(c, env, model, settings);
    auto warm_ensemble = std::make_unique<DensityEvaluator>(c, env, model, settings);
    auto warm_rhs = std::make_unique<DensityEvaluator>(c, env, model, settings);
    const auto cache_for = [&](const BenchmarkConfiguration& configuration) -> DensityEvaluator* {
        if (configuration.cold) return nullptr;
        if (configuration.workload == "fixed_state_rhs") return warm_rhs.get();
        if (configuration.workload == "single") return warm_single.get();
        return warm_ensemble.get();
    };

    for (const auto& configuration : configurations)
        (void)run_benchmark_configuration(c, env, settings, configuration, cache_for(configuration),
                                          0, 0);

    std::vector<BenchmarkRow> rows;
    for (std::size_t repeat = 0; repeat < repeats; ++repeat)
        for (std::size_t step = 0; step < configurations.size(); ++step) {
            const std::size_t index = (repeat + step) % configurations.size();
            rows.push_back(run_benchmark_configuration(c, env, settings, configurations[index],
                                                       cache_for(configurations[index]), repeat,
                                                       step));
        }

    std::map<std::string, std::vector<double>> baseline_walls;
    for (const auto& row : rows)
        if (row.configuration == "authoritative_single")
            baseline_walls["single"].push_back(row.wall_seconds);
    for (const auto& row : rows)
        if (row.configuration == "authoritative_sequential_ensemble")
            baseline_walls["sequential_ensemble"].push_back(row.wall_seconds);
    for (const auto& row : rows)
        if (row.configuration == "authoritative_fixed_state_rhs")
            baseline_walls["fixed_state_rhs"].push_back(row.wall_seconds);
    // The threaded workload has no authoritative ensemble counterpart in this
    // protocol: a threaded candidate result is reported as its own wall time,
    // never as a speedup against a configuration that was not measured.

    std::vector<std::string> configuration_rows;
    for (const auto& configuration : configurations) {
        std::vector<double> walls, constructions, per_rhs, per_density;
        std::size_t rhs_calls = 0, density_evaluations = 0, trajectories = 0, samples = 0;
        std::string checksum;
        std::string statistics;
        for (const auto& row : rows) {
            if (row.configuration != configuration.name) continue;
            walls.push_back(row.wall_seconds);
            constructions.push_back(row.construction_seconds);
            rhs_calls += row.rhs_calls;
            density_evaluations += row.density_evaluations;
            trajectories += row.trajectories;
            samples += row.output_samples;
            if (checksum.empty()) checksum = row.checksum;
            if (statistics.empty()) statistics = row.cache_statistics;
            // Per-repeat rates use that repeat's own counts, never a running total.
            if (row.rhs_calls)
                per_rhs.push_back(row.wall_seconds / double(row.rhs_calls) * 1e9);
            if (row.density_evaluations)
                per_density.push_back(row.wall_seconds / double(row.density_evaluations) * 1e9);
        }
        if (walls.empty()) continue;
        const auto wall_distribution = Distribution::of(walls);
        const double median_wall = wall_distribution.median;
        Json entry;
        entry.add("configuration", configuration.name);
        entry.add("workload", configuration.workload);
        entry.add("authoritative_direct_path", configuration.authoritative);
        entry.add("cache_cold", configuration.cold);
        entry.add("cache_shared_across_trajectories", configuration.share_cache);
        entry.add("ensemble_members", static_cast<double>(configuration.ensemble));
        entry.add("thread_workers", static_cast<double>(configuration.workers));
        entry.add("recorded_repeats", static_cast<double>(walls.size()));
        entry.raw("wall_seconds", wall_distribution.json());
        entry.raw("construction_seconds", Distribution::of(constructions).json());
        entry.add("total_rhs_calls", static_cast<double>(rhs_calls));
        entry.add("total_density_evaluations", static_cast<double>(density_evaluations));
        entry.add("total_trajectories", static_cast<double>(trajectories));
        entry.add("total_output_samples", static_cast<double>(samples));
        entry.raw("nanoseconds_per_rhs", Distribution::of(per_rhs).json());
        entry.raw("nanoseconds_per_density_evaluation", Distribution::of(per_density).json());
        entry.add("trajectory_checksum", checksum);
        const auto baseline = baseline_walls.find(configuration.workload);
        if (!configuration.authoritative && baseline != baseline_walls.end() &&
            !baseline->second.empty()) {
            const auto baseline_distribution = Distribution::of(baseline->second);
            entry.add("authoritative_baseline_median_seconds", baseline_distribution.median);
            entry.add("speedup_versus_authoritative",
                      baseline_distribution.median > 0 ? baseline_distribution.median / median_wall
                                                       : 0.0);
            entry.add("speedup_note",
                      "ratio of the medians of interleaved repeats of the same workload");
        } else if (configuration.authoritative) {
            entry.add("authoritative_baseline_median_seconds", median_wall);
            entry.add("speedup_versus_authoritative", 1.0);
        }
        if (configuration.authoritative)
            entry.raw("cache_statistics", "null");
        else if (statistics.empty())
            entry.raw("cache_statistics", "null");
        else
            entry.raw("cache_statistics", statistics);
        configuration_rows.push_back(entry.str());
    }
    header.raw("configurations", raw_array(configuration_rows));

    std::vector<std::string> repeat_rows;
    for (const auto& row : rows) {
        Json entry;
        entry.add("configuration", row.configuration);
        entry.add("repeat", static_cast<double>(row.repeat));
        entry.add("order_position", static_cast<double>(row.order_position));
        entry.add("wall_seconds", row.wall_seconds);
        entry.add("construction_seconds", row.construction_seconds);
        entry.add("trajectories", static_cast<double>(row.trajectories));
        entry.add("rhs_calls", static_cast<double>(row.rhs_calls));
        entry.add("density_evaluations", static_cast<double>(row.density_evaluations));
        entry.add("output_samples", static_cast<double>(row.output_samples));
        entry.add("trajectory_checksum", row.checksum);
        repeat_rows.push_back(entry.str());
    }
    header.raw("repeats", raw_array(repeat_rows));

    Json memory;
    memory.add("process_peak_rss_bytes", static_cast<double>(process_peak_rss_bytes()));
    memory.add("process_current_rss_bytes", static_cast<double>(process_current_rss_bytes()));
    memory.add("cache_payload_bytes",
               static_cast<double>(warm_single->payload_bytes() + warm_ensemble->payload_bytes() +
                                   warm_rhs->payload_bytes()));
    memory.add("cache_entries",
               static_cast<double>(warm_single->entries() + warm_ensemble->entries() +
                                   warm_rhs->entries()));
    memory.add("note",
               "the payload is the three warm evaluators' own cost; the resident set is the whole "
               "process, including kernels, weather data and every configuration");
    header.raw("memory", memory.str());

    header.add("process_isolation_note",
               "process-isolated batches are separate processes, each owning its own cache; only "
               "reuse inside one process is a cache benefit and no shared interprocess cache is "
               "assumed or built");
    header.add("concurrency_warning",
               "these are wall-time measurements: two of them running at once contaminate each "
               "other. Scientific sweeps may run concurrently only because they are not timed.");
    return header.str();
}

} // namespace cache_study
#endif