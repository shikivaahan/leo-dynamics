// MT-001 batch throughput and scaling benchmark driver.
//
// One source, two link flavours:
//   mt_benchmark               production libraries, no internal lock instrumentation;
//   mt_benchmark_diagnostics   additionally linked against the private copies that
//                              compile in LEO_SPICE_REUSE_DIAGNOSTICS and
//                              MT_NATIVE_DIAGNOSTICS. Separate binary, separate
//                              process, never a mixed link.
// Both read the same command line and write the same schema. A counter that is not
// compiled into the running binary is emitted as JSON null with the reason beside
// it, never as a measured zero.
//
// Scientific configuration is reused, never re-derived: the unchanged atmosphere
// experiment translation unit (its `main` renamed, exactly as the profiling and
// optimisation harnesses do) supplies Config/parse_arguments, Environment (one
// stable, explicitly managed CSPICE provider plus one immutable WeatherDataset),
// experiment::load for the frozen CASE-A/B physics, case_dynamics for the full
// force model and bench::tolerances for the integrator tolerances. There is no
// synthetic fallback: a missing kernel, weather file, native backend or requested
// workload is a run failure, never a substituted value.
//
// What is measured, with honest boundaries. propagate_batch() exposes no timers,
// so the envelope is reconstructed from five markers the caller controls, one per
// trajectory, all stamped on the worker that owns that trajectory:
//   factory entry   first statement of the caller's factory
//   rhs ready       after that trajectory's RHS object is constructed
//   first RHS       first call of that trajectory's RHS
//   rhs end         destructor entry, i.e. after propagate() returned
//   rhs destroyed   destructor exit, i.e. after the RHS released its state
// A clock is read at those five points and nowhere else: inside an RHS evaluation
// only plain local counters are touched, with no allocation and no clock.
//
//   wall_seconds              brackets the whole executor call: worker creation,
//                              propagation, join and result return, nothing else
//   dispatch_seconds          executor entry -> earliest factory entry. It
//                              contains worker creation and scheduling latency, so
//                              it is an upper bound on startup cost, not startup.
//   active_envelope_seconds   earliest factory entry -> latest RHS destruction.
//                              Contains construction, propagation, destruction and
//                              every worker idle period inside the batch.
//   join_return_seconds       latest RHS destruction -> executor return.
//   Per trajectory: trajectory_seconds (factory entry -> destruction),
//   propagation_seconds (first RHS -> destruction), and construction/destruction
//   reported separately.
// The zero-duration control repeats the identical matrix with an output schedule
// of {0}, where propagate() returns before any RHS evaluation, so it isolates
// thread launch, dispatch and join cost for both scheduling policies.
//
// The sequential baseline is an explicit loop over propagate(), not a batch with
// one worker; batch runs with requested_workers == 1 are the library's inline path
// and are recorded separately, so the two are never conflated.
//
// Invocation:
//   mt_benchmark --benchmark-out evidence/benchmarks.json --mode propagate \
//     --lsk ... --spk ... --pck ... --weather ... --solfsmy ... --dtcfile ... \
//     --epoch-utc 2003-10-30T00:00:00Z \
//     --case docs/numerical-audit/experiments/CASE-A-v1.cfg \
//     --benchmark-duration 300 --repetitions 5 --seed 20260101
//
// Exit status is nonzero if any configuration is invalid, is skipped for want of
// host support, or fails to run. No configuration is silently dropped.
#ifndef MT_EXPERIMENT_HEADER
// Fallback for a source-tree build without the CMake-provided absolute path.
#define MT_EXPERIMENT_HEADER "../atmosphere/experiment.cpp"
#endif
#define main mt_experiment_main_renamed
#include MT_EXPERIMENT_HEADER
#undef main

#include "leo/integration/batch.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sched.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

namespace {

// ---------------------------------------------------------------------------
// JSON emission at full binary64 precision; non-finite values are explicit
// tagged strings rather than invalid JSON or a silent zero.
// ---------------------------------------------------------------------------

std::string bm_hex_bits(double value) {
    std::uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof raw);
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(16) << raw;
    return out.str();
}

std::string bm_json_double(double value) {
    if (!std::isfinite(value)) return "\"nonfinite:" + bm_hex_bits(value) + "\"";
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

std::string bm_json_text(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (const char character : value) {
        if (character == '"' || character == '\\') escaped.push_back('\\');
        escaped.push_back(character);
    }
    return '"' + escaped + '"';
}

class BmJson {
public:
    std::ostringstream out;
    bool first = true;

    BmJson() { out << std::setprecision(17) << '{'; }

    void key(const std::string& name) {
        if (!first) out << ',';
        first = false;
        out << bm_json_text(name) << ':';
    }
    void text(const std::string& name, const std::string& value) { key(name); out << bm_json_text(value); }
    void number(const std::string& name, double value) { key(name); out << bm_json_double(value); }
    void integer(const std::string& name, std::uint64_t value) { key(name); out << value; }
    void boolean(const std::string& name, bool value) { key(name); out << (value ? "true" : "false"); }
    // A null always carries its reason beside it, so an absent measurement can
    // never be read as a measured zero.
    void null(const std::string& name, const std::string& reason) {
        key(name);
        out << "null";
        text(name + "_reason", reason);
    }
    void raw(const std::string& name, const std::string& value) { key(name); out << value; }
    std::string str() { return out.str() + '}'; }
};

std::string bm_json_doubles(const std::vector<double>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) out << ',';
        out << bm_json_double(values[index]);
    }
    return out.str() + ']';
}

std::string bm_json_sizes(const std::vector<std::size_t>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) out << ',';
        out << values[index];
    }
    return out.str() + ']';
}

std::string bm_json_strings(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) out << ',';
        out << bm_json_text(values[index]);
    }
    return out.str() + ']';
}

// ---------------------------------------------------------------------------
// Host and process measurements
// ---------------------------------------------------------------------------

// Whole-process CPU time across every thread, so a run that actually overlaps
// work reports utilization above one.
double bm_process_cpu_seconds() {
    timespec now{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now) != 0)
        throw std::runtime_error("CLOCK_PROCESS_CPUTIME_ID is unavailable on this host");
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
}

// Linux ru_maxrss is in kibibytes. This is the process-lifetime high-water mark
// and never decreases, which is why it is published as peak_rss_bytes and not as
// a per-run memory cost.
double bm_peak_rss_bytes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("getrusage failed");
    return static_cast<double>(usage.ru_maxrss) * 1024.0;
}

// Current resident set, so a per-run delta exists alongside the monotone peak.
double bm_current_rss_bytes() {
    std::ifstream statm("/proc/self/statm");
    if (!statm) return 0;
    long total_pages = 0, resident_pages = 0;
    statm >> total_pages >> resident_pages;
    if (!statm) return 0;
    return static_cast<double>(resident_pages) * static_cast<double>(sysconf(_SC_PAGESIZE));
}

std::size_t bm_affinity_cpu_count() {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof set, &set) != 0) return 0;
    return static_cast<std::size_t>(CPU_COUNT(&set));
}

std::string bm_cpu_model() {
    std::ifstream info("/proc/cpuinfo");
    std::string line;
    while (std::getline(info, line)) {
        if (line.rfind("model name", 0) != 0) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
        while (!value.empty() && (value.back() == ' ' || value.back() == '\n')) value.pop_back();
        return value;
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Deterministic initial-state perturbation
// ---------------------------------------------------------------------------

std::uint64_t bm_splitmix64(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Uniform in [-sigma, sigma] drawn from the top 53 bits, so the mapping is exact
// and identical on every conforming implementation.
double bm_uniform_offset(std::uint64_t& state, double sigma) {
    const double unit = static_cast<double>(bm_splitmix64(state) >> 11) * 0x1.0p-53;
    return sigma * (2.0 * unit - 1.0);
}

struct Perturbation {
    double position_sigma_m = 50.0;
    double velocity_sigma_m_s = 0.02;
    double omega_sigma_rad_s = 1e-4;
};

// One perturbed initial state per case and trajectory index, derived from the
// recorded master seed and that index alone, so the same trajectory is perturbed
// identically regardless of batch size, worker count, policy or execution order.
std::vector<State> bm_perturb_states(const std::vector<State>& base, std::uint64_t seed,
                                     const Perturbation& perturbation) {
    std::vector<State> perturbed = base;
    for (std::size_t index = 0; index < perturbed.size(); ++index) {
        std::uint64_t state = seed + 0x9E3779B97F4A7C15ull * (index + 1);
        State& value = perturbed[index];
        value.r = value.r + InertialVector{bm_uniform_offset(state, perturbation.position_sigma_m),
                                           bm_uniform_offset(state, perturbation.position_sigma_m),
                                           bm_uniform_offset(state, perturbation.position_sigma_m)};
        value.v = value.v + InertialVector{bm_uniform_offset(state, perturbation.velocity_sigma_m_s),
                                           bm_uniform_offset(state, perturbation.velocity_sigma_m_s),
                                           bm_uniform_offset(state, perturbation.velocity_sigma_m_s)};
        value.omega = value.omega + BodyVector{bm_uniform_offset(state, perturbation.omega_sigma_rad_s),
                                               bm_uniform_offset(state, perturbation.omega_sigma_rad_s),
                                               bm_uniform_offset(state, perturbation.omega_sigma_rad_s)};
        value = validated_state(value);
    }
    return perturbed;
}

// ---------------------------------------------------------------------------
// Workloads
// ---------------------------------------------------------------------------

enum class WorkloadKind { two_body_j2, full };

struct Workload {
    std::string name;
    WorkloadKind kind = WorkloadKind::two_body_j2;
    bool atmospheric = false;
    leo::atmosphere::Model model = leo::atmosphere::Model::exponential;
};

// two_body_j2 is the pure integrator: the CASE spacecraft and initial state with
// J2 gravity and every environmental contribution switched off, so no SPICE call,
// no atmosphere call and no shared mutable resource is on the path. The three full
// workloads are the frozen CASE-A/B force model whose only free parameter is the
// atmospheric backend.
EarthDynamics bm_workload_dynamics(const Workload& work, const experiment::Case& spec) {
    if (work.kind == WorkloadKind::full) return case_dynamics(spec);
    const Spacecraft base = spec.craft();
    std::vector<Surface> surfaces(base.surfaces().begin(), base.surfaces().end());
    const Spacecraft craft(base.mass_kg(), base.inertia(), std::move(surfaces));
    return EarthDynamics(EarthGravity(Earth{}, true), craft);
}

// One process-wide slot assignment, acquired once per trajectory and never per RHS
// evaluation, so per-worker workload can be attributed after the join.
class ThreadSlots {
public:
    std::size_t acquire(std::thread::id id) {
        std::lock_guard<std::mutex> guard(mutex_);
        for (std::size_t index = 0; index < ids_.size(); ++index)
            if (ids_[index] == id) return index;
        ids_.push_back(id);
        return ids_.size() - 1;
    }
    void reset() {
        std::lock_guard<std::mutex> guard(mutex_);
        ids_.clear();
    }
private:
    std::mutex mutex_;
    std::vector<std::thread::id> ids_;
};

// Per-trajectory markers, written only by the owning worker and read only after the
// executor has returned, so no field is ever concurrently accessed.
struct Instrument {
    std::size_t worker_slot = 0;
    Clock::time_point factory_enter{}, rhs_ready{}, first_rhs{}, rhs_end{}, destroyed{};
    std::size_t rhs_calls = 0, atmosphere_calls = 0;
    bool rhs_entered = false;
};

// Declared as the FIRST member of BmRhs, so it is constructed first and therefore
// destroyed last: rhs end is stamped in the destructor body, every other member is
// released, and only then is the destroyed marker written.
struct DestructionMarker {
    Instrument* instrument;
    ~DestructionMarker() { instrument->destroyed = Clock::now(); }
};

// One RHS per trajectory, created by the factory on the owning worker. It owns its
// own dynamics object and its own DirectAtmosphere; it borrows the single shared
// provider and the immutable weather dataset. Only the owning worker ever calls it.
class BmRhs {
public:
    BmRhs(const Workload& work, const experiment::Case& spec, const Environment& env, const Config& config,
          leo::Epoch epoch, Instrument* instrument)
        : marker_{instrument}, work_(work), env_(env), epoch_(epoch),
          dynamics_(bm_workload_dynamics(work, spec)), instrument_(instrument) {
        if (work_.atmospheric)
            atmosphere_ = std::make_unique<leo::atmosphere::DirectAtmosphere>(
                work_.model, env_.ephemeris, env_.weather, config.frame, config.ap_history, config.options);
        // rhs ready: this trajectory's RHS object, including its own atmospheric
        // object, is fully constructed. Stamped here rather than by the caller so
        // the marker cannot drift from the construction it measures.
        instrument->rhs_ready = Clock::now();
    }

    BmRhs(const BmRhs&) = delete;
    BmRhs& operator=(const BmRhs&) = delete;

    ~BmRhs() { instrument_->rhs_end = Clock::now(); }

    StateDerivative operator()(ElapsedTime t, const State& s) const {
        Instrument& instrument = *instrument_;
        if (!instrument.rhs_entered) {
            instrument.first_rhs = Clock::now();
            instrument.rhs_entered = true;
        }
        ++instrument.rhs_calls;
        if (!work_.atmospheric) return dynamics_(t, s);
        const leo::Epoch at{epoch_.et_seconds + t.seconds};
        leo::EnvironmentSnapshot snapshot;
        snapshot.epoch = at;
        // Existing atmospheric corotation assumption, unchanged.
        snapshot.atmosphere_angular_velocity_rad_s = InertialVector{0, 0, 7.292115e-5};
        const auto observation = env_.ephemeris.observe_sun(at);
        const InertialVector sun_position = observation.state().position_m;
        snapshot.sun_position_m = sun_position;
        snapshot.illumination = illumination_factor(EclipseModel::conical, s.r, sun_position,
                                                     Earth{}.equatorial_radius_m);
        const auto result = atmosphere_->evaluate(at, s.r, &observation);
        if (!std::isfinite(result.density_kg_m3) || result.density_kg_m3 < 0)
            throw std::runtime_error("nonfinite or negative atmosphere density");
        snapshot.density_kg_m3 = result.density_kg_m3;
        ++instrument.atmosphere_calls;
        return dynamics_(t, s, snapshot);
    }

private:
    DestructionMarker marker_;
    const Workload& work_;
    const Environment& env_;
    leo::Epoch epoch_;
    EarthDynamics dynamics_;
    std::unique_ptr<leo::atmosphere::DirectAtmosphere> atmosphere_;
    Instrument* instrument_;
};

// ---------------------------------------------------------------------------
// Executors
// ---------------------------------------------------------------------------

enum class Executor { sequential, batch, control };

std::string bm_executor_name(Executor executor) {
    switch (executor) {
    case Executor::sequential: return "sequential";
    case Executor::batch: return "batch";
    case Executor::control: return "control_no_rhs";
    }
    return "unknown";
}

std::string bm_scheduling_name(BatchScheduling scheduling) {
    return scheduling == BatchScheduling::dynamic ? "dynamic" : "static_partition";
}

struct ExecutorResult {
    double wall_seconds = 0, cpu_seconds = 0, cpu_utilization = 0;
    double dispatch_seconds = 0, active_envelope_seconds = 0, join_return_seconds = 0;
    double construction_seconds = 0, destruction_seconds = 0;
    double peak_rss_bytes = 0, rss_before_bytes = 0, rss_after_bytes = 0;
    std::size_t rhs_calls = 0, atmosphere_calls = 0, worker_slots = 0;
    std::vector<State> final_states;
};

double bm_seconds_between(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double>(to - from).count();
}

// Envelope reconstruction from the per-trajectory markers only; the returned
// trajectories are not traversed, so this never costs anything in output size.
void bm_fold_envelope(const std::vector<Instrument>& instruments, Clock::time_point entry,
                      Clock::time_point exit, ExecutorResult& result) {
    if (instruments.empty()) {
        result.dispatch_seconds = bm_seconds_between(entry, exit);
        result.active_envelope_seconds = result.dispatch_seconds;
        result.join_return_seconds = 0;
        return;
    }
    Clock::time_point first = instruments.front().factory_enter;
    Clock::time_point last = instruments.front().destroyed;
    for (const Instrument& instrument : instruments) {
        first = std::min(first, instrument.factory_enter);
        last = std::max(last, instrument.destroyed);
        result.construction_seconds += bm_seconds_between(instrument.factory_enter, instrument.rhs_ready);
        result.destruction_seconds += bm_seconds_between(instrument.rhs_end, instrument.destroyed);
        result.rhs_calls += instrument.rhs_calls;
        result.atmosphere_calls += instrument.atmosphere_calls;
        result.worker_slots = std::max(result.worker_slots, instrument.worker_slot + 1);
    }
    result.dispatch_seconds = bm_seconds_between(entry, first);
    result.active_envelope_seconds = bm_seconds_between(first, last);
    result.join_return_seconds = bm_seconds_between(last, exit);
}

ExecutorResult bm_run_sequential(const std::vector<State>& initials, const Workload& work,
                                 const experiment::Case& spec, const Environment& env, const Config& config,
                                 leo::Epoch epoch, std::span<const double> labels,
                                 const IntegrationOptions& options, std::vector<Instrument>& instruments) {
    instruments.assign(initials.size(), Instrument{});
    ExecutorResult result;
    result.rss_before_bytes = bm_current_rss_bytes();
    const double cpu_before = bm_process_cpu_seconds();
    const auto entry = Clock::now();
    std::vector<leo::Trajectory> trajectories(initials.size());
    for (std::size_t index = 0; index < initials.size(); ++index) {
        Instrument& instrument = instruments[index];
        instrument.worker_slot = 0;
        instrument.factory_enter = Clock::now();
        BmRhs rhs(work, spec, env, config, epoch, &instrument);
        trajectories[index] = propagate(rhs, initials[index], epoch, labels, options);
    }
    const auto exit = Clock::now();
    result.wall_seconds = bm_seconds_between(entry, exit);
    result.cpu_seconds = bm_process_cpu_seconds() - cpu_before;
    result.cpu_utilization = result.wall_seconds > 0 ? result.cpu_seconds / result.wall_seconds : 0;
    result.rss_after_bytes = bm_current_rss_bytes();
    result.peak_rss_bytes = bm_peak_rss_bytes();
    bm_fold_envelope(instruments, entry, exit, result);
    if (!initials.empty()) result.worker_slots = 1;
    result.final_states.reserve(trajectories.size());
    for (const leo::Trajectory& trajectory : trajectories)
        result.final_states.push_back(trajectory.samples.empty() ? State{} : trajectory.samples.back().state);
    return result;
}

ExecutorResult bm_run_batch(const std::vector<State>& initials, const Workload& work,
                            const experiment::Case& spec, const Environment& env, const Config& config,
                            leo::Epoch epoch, std::span<const double> labels,
                            const IntegrationOptions& options, BatchOptions batch, ThreadSlots& slots,
                            std::vector<Instrument>& instruments) {
    instruments.assign(initials.size(), Instrument{});
    ExecutorResult result;
    slots.reset();
    result.rss_before_bytes = bm_current_rss_bytes();
    const double cpu_before = bm_process_cpu_seconds();
    const auto entry = Clock::now();
    // Runs on the owning worker, once per trajectory. It stamps the factory-entry
    // marker, assigns that worker's slot and returns the trajectory's own RHS by
    // prvalue, so it is constructed in place on the owning worker and destroyed
    // there when the batch releases it, with no extra allocation and no copy. It
    // touches no shared mutable propagation state and is safe to call concurrently.
    auto factory = [&](std::size_t index, const State&) {
        Instrument& instrument = instruments[index];
        instrument.factory_enter = Clock::now();
        instrument.worker_slot = slots.acquire(std::this_thread::get_id());
        return BmRhs(work, spec, env, config, epoch, &instrument);
    };
    std::vector<leo::Trajectory> trajectories =
        propagate_batch(factory, std::span<const State>(initials), epoch, labels, options, batch);
    const auto exit = Clock::now();
    result.wall_seconds = bm_seconds_between(entry, exit);
    result.cpu_seconds = bm_process_cpu_seconds() - cpu_before;
    result.cpu_utilization = result.wall_seconds > 0 ? result.cpu_seconds / result.wall_seconds : 0;
    result.rss_after_bytes = bm_current_rss_bytes();
    result.peak_rss_bytes = bm_peak_rss_bytes();
    bm_fold_envelope(instruments, entry, exit, result);
    result.final_states.reserve(trajectories.size());
    for (const leo::Trajectory& trajectory : trajectories)
        result.final_states.push_back(trajectory.samples.empty() ? State{} : trajectory.samples.back().state);
    return result;
}

// ---------------------------------------------------------------------------
// Diagnostic-only counters
// ---------------------------------------------------------------------------

struct NativeMutexSnapshot {
    std::uint64_t calls = 0, contended = 0, acquisition_ns = 0, held_ns = 0;
};

struct NativeMutexDelta {
    NativeMutexSnapshot nrlmsise00, jb2008;
};

#ifdef MT_NATIVE_DIAGNOSTICS
extern "C" void mt_native_statistics(std::uint64_t* values);
#endif

bool bm_native_diagnostics_available() {
#ifdef MT_NATIVE_DIAGNOSTICS
    return true;
#else
    return false;
#endif
}

NativeMutexSnapshot bm_native_snapshot(bool nrlmsise00) {
    NativeMutexSnapshot snapshot;
#ifdef MT_NATIVE_DIAGNOSTICS
    std::uint64_t values[8] = {};
    mt_native_statistics(values);
    const std::size_t offset = nrlmsise00 ? 0 : 4;
    snapshot.calls = values[offset];
    snapshot.contended = values[offset + 1];
    snapshot.acquisition_ns = values[offset + 2];
    snapshot.held_ns = values[offset + 3];
#else
    (void)nrlmsise00;
#endif
    return snapshot;
}

NativeMutexDelta bm_native_delta(const NativeMutexSnapshot& nrl_before, const NativeMutexSnapshot& nrl_after,
                                 const NativeMutexSnapshot& jb_before, const NativeMutexSnapshot& jb_after) {
    NativeMutexDelta delta;
    delta.nrlmsise00 = {nrl_after.calls - nrl_before.calls, nrl_after.contended - nrl_before.contended,
                        nrl_after.acquisition_ns - nrl_before.acquisition_ns,
                        nrl_after.held_ns - nrl_before.held_ns};
    delta.jb2008 = {jb_after.calls - jb_before.calls, jb_after.contended - jb_before.contended,
                    jb_after.acquisition_ns - jb_before.acquisition_ns, jb_after.held_ns - jb_before.held_ns};
    return delta;
}

bool bm_spice_diagnostics_available() {
#ifdef LEO_SPICE_REUSE_DIAGNOSTICS
    return true;
#else
    return false;
#endif
}

void bm_emit_spice_delta(BmJson& json, const leo::SpiceReuseStatistics& before,
                         const leo::SpiceReuseStatistics& after) {
    BmJson delta;
    delta.integer("utc_hits", after.utc_hits - before.utc_hits);
    delta.integer("utc_misses", after.utc_misses - before.utc_misses);
    delta.integer("utc_evictions", after.utc_evictions - before.utc_evictions);
    delta.integer("orientation_hits", after.orientation_hits - before.orientation_hits);
    delta.integer("orientation_misses", after.orientation_misses - before.orientation_misses);
    delta.integer("orientation_evictions", after.orientation_evictions - before.orientation_evictions);
    delta.integer("invalidations", after.invalidations - before.invalidations);
    delta.integer("et2utc_calls", after.et2utc_calls - before.et2utc_calls);
    delta.integer("str2et_calls", after.str2et_calls - before.str2et_calls);
    delta.integer("pxform_calls", after.pxform_calls - before.pxform_calls);
    delta.integer("spkez_calls", after.spkez_calls - before.spkez_calls);
    delta.integer("spice_mutex_acquisitions", after.mutex_acquisitions - before.mutex_acquisitions);
    delta.integer("spice_mutex_contended", after.mutex_contended - before.mutex_contended);
    delta.integer("spice_mutex_wait_ns", after.mutex_wait_ns - before.mutex_wait_ns);
    delta.integer("utc_cache_entries_after", after.utc_entries);
    delta.integer("orientation_cache_entries_after", after.orientation_entries);
    delta.integer("utc_cache_capacity", after.utc_capacity);
    delta.integer("orientation_cache_capacity", after.orientation_capacity);
    delta.integer("reuse_cache_bytes", after.cache_bytes);
    json.raw("spice_reuse_delta", delta.str());
}

std::string bm_native_entry_json(const NativeMutexSnapshot& snapshot) {
    BmJson entry;
    entry.integer("calls", snapshot.calls);
    entry.integer("contended", snapshot.contended);
    entry.integer("acquisition_ns", snapshot.acquisition_ns);
    entry.integer("held_ns", snapshot.held_ns);
    entry.number("mean_acquisition_ns",
                 snapshot.calls ? static_cast<double>(snapshot.acquisition_ns) / double(snapshot.calls) : 0.0);
    entry.number("mean_held_ns",
                 snapshot.calls ? static_cast<double>(snapshot.held_ns) / double(snapshot.calls) : 0.0);
    entry.number("contended_fraction",
                 snapshot.calls ? static_cast<double>(snapshot.contended) / double(snapshot.calls) : 0.0);
    return entry.str();
}

void bm_emit_native_delta(BmJson& json, const NativeMutexDelta& delta) {
    BmJson native;
    native.raw("nrlmsise00", bm_native_entry_json(delta.nrlmsise00));
    native.raw("jb2008", bm_native_entry_json(delta.jb2008));
    native.text("definition",
                "held_ns is the total time spent inside the native adapter mutex. acquisition_ns is a timed "
                "try_lock/lock pair and therefore contains clock and try_lock overhead in addition to any "
                "blocking, so it is an upper bound on wait time, not pure blocking time. The native models are "
                "unchanged: the same mutexes are entered exactly once per native evaluation as before.");
    json.raw("native_mutex_delta", native.str());
}

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

struct BenchmarkOptions {
    std::string json_path;                                        // --benchmark-out
    double duration = 0;                                          // --benchmark-duration (0 -> Config)
    double cadence = 30;                                          // --benchmark-cadence
    std::size_t repetitions = 5;                                  // --repetitions
    std::size_t warmups = 1;                                      // --benchmark-warmups
    std::size_t control_repetitions = 3;                          // --benchmark-control-repetitions
    std::uint64_t seed = 20260101ull;                             // --seed
    bool shuffle_order = true;                                    // --benchmark-order
    bool controls = true;                                         // --benchmark-controls
    bool sequential = true;                                       // --benchmark-sequential
    bool progress = true;                                         // --benchmark-progress
    bool ap_history = false;                                      // --benchmark-ap-history
    Perturbation perturbation;                                    // --benchmark-*-sigma
    std::vector<std::size_t> sizes{1, 2, 4, 8, 16, 32, 64};       // --benchmark-sizes
    std::vector<std::size_t> workers{1, 2, 4, 8};                 // --benchmark-workers
    std::vector<BatchScheduling> policies{BatchScheduling::static_partition, BatchScheduling::dynamic};
    std::vector<std::string> models;                              // --benchmark-models
    std::vector<std::string> variants{"identical", "perturbed"}; // --benchmark-variants
    std::vector<std::string> case_ids;                            // --benchmark-cases
};

std::size_t bm_parse_count(const std::string& text, const char* key) {
    std::size_t used = 0;
    const unsigned long long value = std::stoull(text, &used);
    if (used != text.size()) throw std::invalid_argument(std::string("invalid value for ") + key + ": " + text);
    return static_cast<std::size_t>(value);
}

std::vector<std::size_t> bm_parse_counts(const std::string& text, const char* key) {
    std::vector<std::size_t> counts;
    std::istringstream rows(text);
    std::string item;
    while (std::getline(rows, item, ',')) {
        if (!item.empty()) counts.push_back(bm_parse_count(item, key));
    }
    if (counts.empty()) throw std::invalid_argument(std::string(key) + " needs at least one count");
    return counts;
}

bool bm_parse_switch(const std::string& text, const char* key) {
    if (text == "on" || text == "true") return true;
    if (text == "off" || text == "false") return false;
    throw std::invalid_argument(std::string(key) + " expects on or off, got: " + text);
}

std::vector<std::string> bm_split_strings(const std::string& text, const char* key) {
    std::vector<std::string> items;
    std::istringstream rows(text);
    std::string item;
    while (std::getline(rows, item, ','))
        if (!item.empty()) items.push_back(item);
    if (items.empty()) throw std::invalid_argument(std::string(key) + " needs at least one entry");
    return items;
}

void bm_print_help() {
    std::cout <<
        "mt_benchmark options (every other argument is the atmosphere experiment's):\n"
        "  --benchmark-out PATH              machine-readable benchmarks.json (required)\n"
        "  --benchmark-duration S            per-trajectory propagation duration, seconds\n"
        "                                     (default: the experiment --duration)\n"
        "  --benchmark-cadence S             output sample spacing, seconds (default 30)\n"
        "  --repetitions N                   measured runs per configuration (default 5)\n"
        "  --benchmark-warmups N             warm-up runs per configuration (default 1)\n"
        "  --benchmark-control-repetitions N repetitions per zero-duration control (default 3)\n"
        "  --seed N                          master seed: perturbations and run order (default 20260101)\n"
        "  --benchmark-order seeded|sequential  seeded shuffle of the execution order (default seeded)\n"
        "  --benchmark-sizes LIST            batch sizes (default 1,2,4,8,16,32,64)\n"
        "  --benchmark-workers LIST          worker counts (default 1,2,4,8)\n"
        "  --benchmark-policies LIST         static,dynamic (default both)\n"
        "  --benchmark-models LIST           two_body_j2,exponential,nrlmsise00,jb2008\n"
        "                                     (default two_body_j2 plus every compiled-in backend)\n"
        "  --benchmark-variants LIST         identical,perturbed (default both)\n"
        "  --benchmark-cases LIST            CASE ids to keep from the --case files\n"
        "  --benchmark-position-sigma M      perturbation position half width, m (default 50)\n"
        "  --benchmark-velocity-sigma M/S    perturbation velocity half width (default 0.02)\n"
        "  --benchmark-omega-sigma RAD/S     perturbation body-rate half width (default 1e-4)\n"
        "  --benchmark-sequential on|off     explicit propagate() loop baseline (default on)\n"
        "  --benchmark-controls on|off       zero-duration, zero-RHS scheduling controls (default on)\n"
        "  --benchmark-ap-history on|off     ap-history switch for every workload (default off)\n"
        "  --benchmark-progress on|off       progress lines on stderr (default on)\n"
        "  --benchmark-help                  this text\n";
}

// Every --benchmark-* option is consumed here; everything else is forwarded
// verbatim to the unchanged experiment argument parser.
std::vector<std::string> bm_split_arguments(int argc, char** argv, BenchmarkOptions& benchmark) {
    std::vector<std::string> forwarded;
    forwarded.emplace_back(argv[0]);
    bool have_mode = false, have_out = false;
    const auto value_of = [&](int& index, const std::string& key) -> std::string {
        if (index + 1 >= argc) throw std::invalid_argument("missing value for " + key);
        return argv[++index];
    };
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        if (key == "--benchmark-out") { benchmark.json_path = value_of(index, key); continue; }
        if (key == "--benchmark-duration") { benchmark.duration = positive(value_of(index, key), "--benchmark-duration"); continue; }
        if (key == "--benchmark-cadence") { benchmark.cadence = positive(value_of(index, key), "--benchmark-cadence"); continue; }
        if (key == "--repetitions") { benchmark.repetitions = bm_parse_count(value_of(index, key), "--repetitions"); continue; }
        if (key == "--benchmark-warmups") { benchmark.warmups = bm_parse_count(value_of(index, key), "--benchmark-warmups"); continue; }
        if (key == "--benchmark-control-repetitions") {
            benchmark.control_repetitions = bm_parse_count(value_of(index, key), "--benchmark-control-repetitions");
            continue;
        }
        if (key == "--seed") { benchmark.seed = std::stoull(value_of(index, key), nullptr, 0); continue; }
        if (key == "--benchmark-order") {
            const std::string value = value_of(index, key);
            if (value != "seeded" && value != "sequential")
                throw std::invalid_argument("--benchmark-order expects seeded or sequential, got: " + value);
            benchmark.shuffle_order = value == "seeded";
            continue;
        }
        if (key == "--benchmark-controls") { benchmark.controls = bm_parse_switch(value_of(index, key), "--benchmark-controls"); continue; }
        if (key == "--benchmark-sequential") { benchmark.sequential = bm_parse_switch(value_of(index, key), "--benchmark-sequential"); continue; }
        if (key == "--benchmark-progress") { benchmark.progress = bm_parse_switch(value_of(index, key), "--benchmark-progress"); continue; }
        if (key == "--benchmark-ap-history") { benchmark.ap_history = bm_parse_switch(value_of(index, key), "--benchmark-ap-history"); continue; }
        if (key == "--benchmark-sizes") { benchmark.sizes = bm_parse_counts(value_of(index, key), "--benchmark-sizes"); continue; }
        if (key == "--benchmark-workers") { benchmark.workers = bm_parse_counts(value_of(index, key), "--benchmark-workers"); continue; }
        if (key == "--benchmark-policies") {
            benchmark.policies.clear();
            for (const std::string& name : bm_split_strings(value_of(index, key), "--benchmark-policies")) {
                if (name == "static" || name == "static_partition")
                    benchmark.policies.push_back(BatchScheduling::static_partition);
                else if (name == "dynamic")
                    benchmark.policies.push_back(BatchScheduling::dynamic);
                else throw std::invalid_argument("unknown --benchmark-policies entry: " + name);
            }
            continue;
        }
        if (key == "--benchmark-models") { benchmark.models = bm_split_strings(value_of(index, key), "--benchmark-models"); continue; }
        if (key == "--benchmark-variants") { benchmark.variants = bm_split_strings(value_of(index, key), "--benchmark-variants"); continue; }
        if (key == "--benchmark-cases") { benchmark.case_ids = bm_split_strings(value_of(index, key), "--benchmark-cases"); continue; }
        if (key == "--benchmark-position-sigma") { benchmark.perturbation.position_sigma_m = positive(value_of(index, key), "--benchmark-position-sigma"); continue; }
        if (key == "--benchmark-velocity-sigma") { benchmark.perturbation.velocity_sigma_m_s = positive(value_of(index, key), "--benchmark-velocity-sigma"); continue; }
        if (key == "--benchmark-omega-sigma") { benchmark.perturbation.omega_sigma_rad_s = positive(value_of(index, key), "--benchmark-omega-sigma"); continue; }
        if (key == "--benchmark-help") { bm_print_help(); std::exit(0); }
        if (key == "--mode") have_mode = true;
        if (key == "--out") have_out = true;
        forwarded.push_back(key);
        // The experiment parser's only valueless flag; every other experiment
        // option takes exactly one value, which is forwarded with its key so a
        // value that happens to start with a dash is never silently split.
        if (key != "--ap-history" && index + 1 < argc &&
            std::string(argv[index + 1]).rfind("--", 0) != 0)
            forwarded.push_back(argv[++index]);
    }
    if (benchmark.repetitions == 0) throw std::invalid_argument("--repetitions must be at least one");
    if (benchmark.control_repetitions == 0)
        throw std::invalid_argument("--benchmark-control-repetitions must be at least one");
    if (benchmark.sizes.empty() || benchmark.workers.empty() || benchmark.policies.empty() ||
        benchmark.variants.empty())
        throw std::invalid_argument("sizes, workers, policies and variants must each name at least one value");
    for (const std::size_t size : benchmark.sizes)
        if (size == 0) throw std::invalid_argument("--benchmark-sizes entries must be at least one");
    for (const std::size_t workers : benchmark.workers)
        if (workers == 0) throw std::invalid_argument("--benchmark-workers entries must be at least one");
    for (const std::string& variant : benchmark.variants)
        if (variant != "identical" && variant != "perturbed")
            throw std::invalid_argument("--benchmark-variants entries must be identical or perturbed, got: " + variant);
    if (benchmark.json_path.empty())
        throw std::invalid_argument("--benchmark-out is required (or --out, defaulting benchmarks.json)");
    if (!have_mode) { forwarded.push_back("--mode"); forwarded.push_back("propagate"); }
    if (!have_out) {
        // The experiment parser requires --out; this driver never writes through
        // it, so the benchmark file's own directory is supplied only to satisfy
        // that contract. A bare file name has an empty parent, which the parser
        // rejects, so it becomes the current directory.
        std::filesystem::path directory = std::filesystem::path(benchmark.json_path).parent_path();
        forwarded.push_back("--out");
        forwarded.push_back(directory.empty() ? std::string(".") : directory.string());
    }
    return forwarded;
}

// ---------------------------------------------------------------------------
// Matrix
// ---------------------------------------------------------------------------

std::vector<Workload> bm_resolve_workloads(const BenchmarkOptions& benchmark) {
    std::vector<Workload> available;
    available.push_back({"two_body_j2", WorkloadKind::two_body_j2, false, leo::atmosphere::Model::exponential});
    for (const auto model : requested_models())
        available.push_back({std::string("full_") + model_name(model), WorkloadKind::full, true, model});
    if (benchmark.models.empty()) return available;
    std::vector<Workload> selected;
    for (const std::string& name : benchmark.models) {
        const auto found = std::find_if(available.begin(), available.end(), [&](const Workload& work) {
            if (name == work.name) return true;
            if (work.kind == WorkloadKind::two_body_j2 && (name == "two_body" || name == "j2")) return true;
            return work.kind == WorkloadKind::full && name == model_name(work.model);
        });
        if (found == available.end()) {
            std::string list;
            for (const Workload& work : available) list += ", " + work.name;
            throw std::invalid_argument("--benchmark-models names a workload this build cannot run: " + name +
                                        " (available:" + list + ")");
        }
        selected.push_back(*found);
    }
    return selected;
}

std::vector<double> bm_labels(double duration, double cadence) {
    std::vector<double> labels{0};
    for (double time = cadence; time < duration; time += cadence) labels.push_back(time);
    labels.push_back(duration);
    return labels;
}

struct Job {
    Executor executor = Executor::batch;
    std::size_t case_index = 0;
    const Workload* work = nullptr;
    std::string variant, case_id;
    std::size_t batch_size = 1, workers = 1, repetition = 0;
    BatchScheduling scheduling = BatchScheduling::static_partition;
    bool ap_history = false;
    std::string configuration_id;
};

Job bm_make_job(Executor executor, std::size_t case_index, const Workload& work, const std::string& variant,
                const std::string& case_id, std::size_t batch_size, std::size_t workers,
                BatchScheduling scheduling, bool ap_history) {
    Job job;
    job.executor = executor;
    job.case_index = case_index;
    job.work = &work;
    job.variant = variant;
    job.case_id = case_id;
    job.batch_size = batch_size;
    job.workers = workers;
    job.scheduling = scheduling;
    job.ap_history = ap_history;
    std::ostringstream id;
    id << bm_executor_name(executor) << '|' << case_id << '|' << work.name << '|' << variant
       << "|size=" << batch_size << "|workers=" << workers;
    if (executor != Executor::sequential) id << '|' << bm_scheduling_name(scheduling);
    if (ap_history) id << "|ap_history";
    job.configuration_id = id.str();
    return job;
}

// Named apart from experiment.cpp's own RunRecord, which the included
// translation unit already defines in the same unnamed namespace.
struct BmRunRecord {
    Job job;
    std::string configuration_id;
    double wall_seconds = 0, cpu_seconds = 0, cpu_utilization = 0;
    double dispatch_seconds = 0, active_envelope_seconds = 0, join_return_seconds = 0;
    double construction_seconds = 0, destruction_seconds = 0;
    double peak_rss_bytes = 0, rss_before_bytes = 0, rss_after_bytes = 0;
    std::size_t rhs_calls = 0, atmosphere_calls = 0, worker_count = 0;
    double trajectories_per_second = 0;
    double worker_busy_max_over_span = 0, worker_busy_cv = 0;
    std::string checksum;
    std::vector<double> trajectory_seconds, propagation_seconds, construction_seconds_each;
    std::vector<double> worker_busy_seconds, worker_span_seconds;
    std::vector<std::size_t> trajectory_rhs_calls, trajectory_atmosphere_calls, trajectory_slots;
    int sequential_reference = -1;
    bool has_spice = false, has_native = false;
    leo::SpiceReuseStatistics spice_before{}, spice_after{};
    NativeMutexSnapshot native_before_nrl{}, native_after_nrl{}, native_before_jb{}, native_after_jb{};
};

std::string bm_checksum(const std::vector<State>& states) {
    Checksum checksum;
    for (const State& state : states) checksum.add(state);
    return checksum.str();
}

void bm_fold_worker_workload(const std::vector<Instrument>& instruments, std::size_t slots,
                             BmRunRecord& record) {
    record.worker_busy_seconds.assign(slots, 0.0);
    record.worker_span_seconds.assign(slots, 0.0);
    std::vector<Clock::time_point> first(slots), last(slots);
    std::vector<bool> seen(slots, false);
    for (const Instrument& instrument : instruments) {
        if (instrument.worker_slot >= slots) continue;
        record.worker_busy_seconds[instrument.worker_slot] +=
            bm_seconds_between(instrument.factory_enter, instrument.destroyed);
        if (!seen[instrument.worker_slot]) {
            first[instrument.worker_slot] = instrument.factory_enter;
            last[instrument.worker_slot] = instrument.destroyed;
            seen[instrument.worker_slot] = true;
        } else {
            first[instrument.worker_slot] = std::min(first[instrument.worker_slot], instrument.factory_enter);
            last[instrument.worker_slot] = std::max(last[instrument.worker_slot], instrument.destroyed);
        }
    }
    std::vector<double> busy;
    for (std::size_t slot = 0; slot < slots; ++slot) {
        if (!seen[slot]) continue;
        record.worker_span_seconds[slot] = bm_seconds_between(first[slot], last[slot]);
        busy.push_back(record.worker_busy_seconds[slot]);
        if (record.worker_span_seconds[slot] > 0)
            record.worker_busy_max_over_span = std::max(
                record.worker_busy_max_over_span,
                record.worker_busy_seconds[slot] / record.worker_span_seconds[slot]);
    }
    if (busy.size() > 1) {
        const double mean = std::accumulate(busy.begin(), busy.end(), 0.0) / double(busy.size());
        double variance = 0;
        for (const double value : busy) variance += (value - mean) * (value - mean);
        record.worker_busy_cv = mean > 0 ? std::sqrt(variance / double(busy.size())) / mean : 0;
    }
}

BmRunRecord bm_measure(const Job& job, const std::vector<experiment::Case>& specs, const Environment& env,
                     const Config& config, leo::Epoch epoch, const IntegrationOptions& options,
                     const std::vector<double>& labels, const std::vector<double>& control_labels,
                     const std::vector<std::vector<State>>& identical_initials,
                     const std::vector<std::vector<State>>& perturbed_initials, ThreadSlots& slots) {
    const experiment::Case& spec = specs[job.case_index];
    // One initial-state list per case, each of the largest requested batch size,
    // so a batch of N trajectories is always the first N entries of its own case.
    const std::vector<std::vector<State>>& pool =
        job.variant == "perturbed" ? perturbed_initials : identical_initials;
    const std::vector<State>& initials = pool[job.case_index];
    if (initials.size() < job.batch_size)
        throw std::runtime_error("internal error: initial-state pool shorter than the requested batch size");
    const std::vector<State> selected(initials.begin(),
                                      initials.begin() + static_cast<std::ptrdiff_t>(job.batch_size));

    BmRunRecord record;
    record.job = job;
    record.configuration_id = job.configuration_id;
    record.has_spice = bm_spice_diagnostics_available();
    record.has_native = bm_native_diagnostics_available();

    // Counters are sampled outside the timed region on both sides.
    if (record.has_spice) record.spice_before = leo::SpiceEphemerisProvider::reuse_statistics();
    if (record.has_native) {
        record.native_before_nrl = bm_native_snapshot(true);
        record.native_before_jb = bm_native_snapshot(false);
    }

    Config scoped = config;
    scoped.ap_history = job.ap_history;

    std::vector<Instrument> instruments;
    ExecutorResult result;
    if (job.executor == Executor::sequential)
        result = bm_run_sequential(selected, *job.work, spec, env, scoped, epoch, labels, options, instruments);
    else
        result = bm_run_batch(selected, *job.work, spec, env, scoped, epoch,
                              job.executor == Executor::control ? control_labels : labels, options,
                              BatchOptions{job.workers, job.scheduling}, slots, instruments);

    if (record.has_spice) record.spice_after = leo::SpiceEphemerisProvider::reuse_statistics();
    if (record.has_native) {
        record.native_after_nrl = bm_native_snapshot(true);
        record.native_after_jb = bm_native_snapshot(false);
    }

    record.wall_seconds = result.wall_seconds;
    record.cpu_seconds = result.cpu_seconds;
    record.cpu_utilization = result.cpu_utilization;
    record.dispatch_seconds = result.dispatch_seconds;
    record.active_envelope_seconds = result.active_envelope_seconds;
    record.join_return_seconds = result.join_return_seconds;
    record.construction_seconds = result.construction_seconds;
    record.destruction_seconds = result.destruction_seconds;
    record.peak_rss_bytes = result.peak_rss_bytes;
    record.rss_before_bytes = result.rss_before_bytes;
    record.rss_after_bytes = result.rss_after_bytes;
    record.rhs_calls = result.rhs_calls;
    record.atmosphere_calls = result.atmosphere_calls;
    record.worker_count = result.worker_slots;
    record.trajectories_per_second =
        result.wall_seconds > 0 ? static_cast<double>(job.batch_size) / result.wall_seconds : 0.0;
    record.checksum = bm_checksum(result.final_states);

    const std::size_t count = instruments.size();
    record.trajectory_seconds.reserve(count);
    record.propagation_seconds.reserve(count);
    record.construction_seconds_each.reserve(count);
    record.trajectory_rhs_calls.reserve(count);
    record.trajectory_atmosphere_calls.reserve(count);
    record.trajectory_slots.reserve(count);
    for (const Instrument& instrument : instruments) {
        record.trajectory_seconds.push_back(bm_seconds_between(instrument.factory_enter, instrument.destroyed));
        record.propagation_seconds.push_back(
            instrument.rhs_entered ? bm_seconds_between(instrument.first_rhs, instrument.rhs_end) : 0.0);
        record.construction_seconds_each.push_back(bm_seconds_between(instrument.factory_enter, instrument.rhs_ready));
        record.trajectory_rhs_calls.push_back(instrument.rhs_calls);
        record.trajectory_atmosphere_calls.push_back(instrument.atmosphere_calls);
        record.trajectory_slots.push_back(instrument.worker_slot);
    }
    bm_fold_worker_workload(instruments, std::max<std::size_t>(result.worker_slots, 1), record);
    return record;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        BenchmarkOptions benchmark;
        const std::vector<std::string> forwarded = bm_split_arguments(argc, argv, benchmark);
        std::vector<char*> storage;
        for (const std::string& argument : forwarded) storage.push_back(const_cast<char*>(argument.c_str()));
        storage.push_back(nullptr);
        const Config config = parse_arguments(static_cast<int>(storage.size() - 1), storage.data());

        if (benchmark.duration <= 0) benchmark.duration = config.duration;
        if (benchmark.duration <= 0) throw std::invalid_argument("benchmark duration must be positive");
        if (benchmark.cadence >= benchmark.duration)
            throw std::invalid_argument("--benchmark-cadence must be smaller than --benchmark-duration");

        Environment env(config);
        const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(config.epoch_utc);
        const std::vector<double> labels = bm_labels(benchmark.duration, benchmark.cadence);
        const std::vector<double> control_labels{0};
        IntegrationOptions options = tolerances();
        options.rtol = config.rtol;
        options.max_step_s = config.max_step;
        options.initial_step_s = std::min(1.0, config.max_step);

        std::vector<experiment::Case> specs;
        for (const std::string& file : config.case_files) {
            experiment::Case spec = experiment::load(file);
            if (!benchmark.case_ids.empty() &&
                std::find(benchmark.case_ids.begin(), benchmark.case_ids.end(), spec.id) == benchmark.case_ids.end())
                continue;
            specs.push_back(spec);
        }
        if (specs.empty())
            throw std::invalid_argument("--benchmark-cases matched none of the --case files; refusing to run an "
                                        "empty matrix");

        const std::vector<Workload> workloads = bm_resolve_workloads(benchmark);
        for (const Workload& work : workloads)
            if (work.atmospheric && !compiled_in(work.model))
                throw std::runtime_error(std::string("workload ") + work.name +
                                         " needs a backend this build does not contain");

        const std::size_t affinity_cpus = bm_affinity_cpu_count();
        const std::size_t hardware_cpus = std::thread::hardware_concurrency();
        const std::size_t worker_limit = affinity_cpus ? affinity_cpus : hardware_cpus;
        const std::size_t largest_size =
            *std::max_element(benchmark.sizes.begin(), benchmark.sizes.end());

        // One initial-state list per case, each of the largest requested batch
        // size: the identical variant repeats the case start state, the perturbed
        // variant perturbs entry i from the seed and index i alone.
        std::vector<std::vector<State>> identical_initials;
        std::vector<std::vector<State>> perturbed_initials;
        for (const experiment::Case& spec : specs) {
            std::vector<State> initials(largest_size, spec.start());
            identical_initials.push_back(initials);
            perturbed_initials.push_back(bm_perturb_states(initials, benchmark.seed, benchmark.perturbation));
        }

        // ---- matrix ------------------------------------------------------
        std::vector<Job> jobs;
        std::vector<std::string> skipped;
        const auto skip_reason = [&](const Job& job) {
            return job.configuration_id + ": skipped, " + std::to_string(job.workers) +
                   " workers exceed the " + std::to_string(worker_limit) +
                   " logical CPUs available to this process";
        };
        for (std::size_t case_index = 0; case_index < specs.size(); ++case_index) {
            const std::string& case_id = specs[case_index].id;
            for (const Workload& work : workloads)
                for (const std::string& variant : benchmark.variants) {
                    if (benchmark.sequential)
                        for (const std::size_t size : benchmark.sizes)
                            jobs.push_back(bm_make_job(Executor::sequential, case_index, work, variant, case_id,
                                                       size, 1, BatchScheduling::static_partition,
                                                       benchmark.ap_history));
                    for (const std::size_t size : benchmark.sizes)
                        for (const std::size_t workers : benchmark.workers)
                            for (const BatchScheduling policy : benchmark.policies) {
                                const Job job = bm_make_job(Executor::batch, case_index, work, variant, case_id,
                                                            size, workers, policy, benchmark.ap_history);
                                if (workers > worker_limit) {
                                    skipped.push_back(skip_reason(job));
                                    continue;
                                }
                                jobs.push_back(job);
                            }
                    if (!benchmark.controls) continue;
                    for (const std::size_t size : benchmark.sizes)
                        for (const std::size_t workers : benchmark.workers)
                            for (const BatchScheduling policy : benchmark.policies) {
                                const Job job = bm_make_job(Executor::control, case_index, work, variant, case_id,
                                                            size, workers, policy, benchmark.ap_history);
                                if (workers > worker_limit) {
                                    skipped.push_back(skip_reason(job));
                                    continue;
                                }
                                jobs.push_back(job);
                            }
                }
        }

        std::vector<std::size_t> order(jobs.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        if (benchmark.shuffle_order) {
            std::mt19937_64 generator(benchmark.seed);
            std::shuffle(order.begin(), order.end(), generator);
        }

        // ---- execution ---------------------------------------------------
        std::vector<BmRunRecord> records;
        std::vector<std::string> execution_order;
        ThreadSlots slots;
        std::size_t failed = 0;
        const auto progress = [&](std::size_t position, const Job& job, std::size_t step, std::size_t steps,
                                  double seconds, const char* phase) {
            if (!benchmark.progress) return;
            std::cerr << '[' << (position + 1) << '/' << jobs.size() << "] " << phase << ' ' << job.case_id << ' '
                      << job.work->name << ' ' << job.variant << " size=" << job.batch_size
                      << " workers=" << job.workers
                      << (job.executor == Executor::batch ? " " + bm_scheduling_name(job.scheduling) : "")
                      << ' ' << bm_executor_name(job.executor) << ' ' << step << '/' << steps << ' '
                      << std::fixed << std::setprecision(6) << seconds << "s\n";
        };

        for (std::size_t position = 0; position < order.size(); ++position) {
            const Job& job = jobs[order[position]];
            // A configuration that cannot even warm up is reported and then left
            // alone: its repetitions are not attempted, and the remaining
            // configurations still run.
            bool warm = true;
            for (std::size_t step = 0; step < benchmark.warmups; ++step) {
                try {
                    const BmRunRecord record = bm_measure(job, specs, env, config, epoch, options, labels,
                                                          control_labels, identical_initials, perturbed_initials,
                                                          slots);
                    progress(position, job, step + 1, benchmark.warmups, record.wall_seconds, "warmup");
                    execution_order.push_back(job.configuration_id + "|warmup=" + std::to_string(step) +
                                              "|seconds=" + std::to_string(record.wall_seconds));
                } catch (const std::exception& error) {
                    ++failed;
                    warm = false;
                    std::cerr << "configuration warm-up FAILED: " << job.configuration_id << ": " << error.what()
                              << '\n';
                    execution_order.push_back(job.configuration_id + "|warmup=" + std::to_string(step) +
                                              "|failed|" + error.what());
                    break;
                }
            }
            if (!warm) continue;
            const std::size_t steps =
                job.executor == Executor::control ? benchmark.control_repetitions : benchmark.repetitions;
            for (std::size_t repetition = 0; repetition < steps; ++repetition) {
                Job measured = job;
                measured.repetition = repetition;
                try {
                    BmRunRecord record = bm_measure(measured, specs, env, config, epoch, options, labels,
                                                  control_labels, identical_initials, perturbed_initials, slots);
                    execution_order.push_back(measured.configuration_id + "|repetition=" +
                                              std::to_string(repetition) + "|seconds=" +
                                              std::to_string(record.wall_seconds));
                    progress(position, measured, repetition + 1, steps, record.wall_seconds, "run");
                    records.push_back(std::move(record));
                } catch (const std::exception& error) {
                    ++failed;
                    std::cerr << "configuration FAILED: " << measured.configuration_id << ": " << error.what()
                              << '\n';
                    execution_order.push_back(measured.configuration_id + "|repetition=" +
                                              std::to_string(repetition) + "|failed|" + error.what());
                }
            }
        }

        // Link every batch run to the sequential baseline of the same case,
        // workload, variant, batch size and ap-history switch, so speedup and
        // parallel efficiency are derivable without re-running anything.
        for (BmRunRecord& record : records) {
            if (record.job.executor != Executor::batch) continue;
            for (std::size_t index = 0; index < records.size(); ++index) {
                const BmRunRecord& other = records[index];
                if (other.job.executor == Executor::sequential && other.job.case_id == record.job.case_id &&
                    other.job.work == record.job.work && other.job.variant == record.job.variant &&
                    other.job.batch_size == record.job.batch_size &&
                    other.job.ap_history == record.job.ap_history) {
                    record.sequential_reference = static_cast<int>(index);
                    break;
                }
            }
        }

        // ---- document ----------------------------------------------------
        BmJson document;
        document.text("kind", "mt001-batch-benchmark");
        document.text("tool", "mt_benchmark");
        document.text("protocol",
                      "Real propagation workloads. One executor call per measured run. The sequential baseline is an "
                      "explicit loop over propagate(), never a batch with one worker. Each configuration is warmed "
                      "up and then repeated; every repetition is recorded and none is discarded.");
        document.text("timing_boundaries",
                      "propagate_batch exposes no timers. wall_seconds brackets the whole executor call. "
                      "dispatch_seconds runs from executor entry to the earliest per-trajectory factory entry and "
                      "therefore contains worker creation and scheduling latency: an upper bound on startup cost, "
                      "not startup alone. active_envelope_seconds runs from that earliest factory entry to the "
                      "latest RHS destruction and contains construction, propagation, destruction and every idle "
                      "period inside the batch. join_return_seconds runs from that latest destruction to executor "
                      "return. Per trajectory, trajectory_seconds spans factory entry to RHS destruction, "
                      "propagation_seconds spans the first RHS call to RHS destruction, and construction and "
                      "destruction are reported separately.");
        document.text("counter_instrumentation",
                      "RHS and atmosphere counts are plain local fields of each trajectory's own RHS object, "
                      "incremented by its owning worker. No clock is read and nothing is allocated inside an RHS "
                      "evaluation; the only clock reads are the five per-trajectory markers and the process CPU and "
                      "RSS samples that bracket the run.");
        document.text("sequential_baseline_definition",
                      "executor \"sequential\" is a plain for loop calling propagate() once per trajectory on the "
                      "calling thread. executor \"batch\" with requested_workers == 1 is the library's inline "
                      "single-worker path, recorded separately so the two are never conflated.");
        document.text("control_definition",
                      "executor \"control_no_rhs\" repeats the matrix with an output schedule of {0}, where "
                      "propagate() returns before evaluating the RHS at all, so it isolates thread launch, "
                      "dispatch and join for both scheduling policies.");
        document.text("speedup_and_efficiency",
                      "Not computed here. Each batch run carries sequential_reference, the index of a sequential "
                      "run of the same case, workload, variant, batch size and ap-history switch, so speedup is "
                      "that run's wall_seconds over this run's wall_seconds and parallel efficiency is that "
                      "speedup divided by requested_workers.");
        document.text("worker_workload_definition",
                      "worker_busy_seconds is the sum over that worker's trajectories of factory-entry to "
                      "destruction; worker_span_seconds is that worker's first factory entry to its last RHS "
                      "destruction. worker_busy_max_over_span is the largest busy/span ratio, so a value near one "
                      "means no idle gap inside any worker and a smaller value means one; worker_busy_cv is the "
                      "coefficient of variation of busy time across workers and measures load imbalance. Slots are "
                      "assigned by first factory call on each thread, so a slot index is an execution order label, "
                      "not a fixed worker identity across runs.");
        document.text("skipped_policy",
                      "A configuration is skipped only when the host cannot support its worker count, and every "
                      "skip is listed with its reason. No workload, model or configuration is silently dropped.");
        document.text("spice_reuse_counters",
                      "Delta of SpiceEphemerisProvider::reuse_statistics() across the timed region, sampled "
                      "outside it. Present only in a build whose environment library was compiled with "
                      "LEO_SPICE_REUSE_DIAGNOSTICS; otherwise null with the reason.");
        document.text("native_mutex_counters",
                      "Total time serialized inside the native NRLMSISE-00 and JB2008 adapter mutexes and the "
                      "contended share of acquisitions, from private measurement-only copies of those two adapter "
                      "sources. Present only in a build compiled with MT_NATIVE_DIAGNOSTICS; otherwise null with "
                      "the reason. No production lock, model or SPICE internal was modified.");
        document.text("memory_definitions",
                      "peak_rss_bytes is the process-lifetime high-water mark (ru_maxrss) and never decreases; "
                      "rss_bytes_before and rss_bytes_after are instantaneous samples around the run, so "
                      "rss_delta_bytes is the per-run change.");
        document.integer("seed", benchmark.seed);
        document.integer("repetitions", benchmark.repetitions);
        document.integer("warmups_per_configuration", benchmark.warmups);
        document.integer("control_repetitions", benchmark.control_repetitions);
        document.text("execution_order_policy", benchmark.shuffle_order ? "seeded shuffle of the matrix" : "matrix order");
        document.text("epoch_utc", config.epoch_utc);
        document.number("epoch_et_seconds", epoch.et_seconds);
        document.text("frame", config.frame);
        document.number("duration_s", benchmark.duration);
        document.number("output_cadence_s", benchmark.cadence);
        document.integer("output_sample_count", labels.size());
        document.number("rtol", options.rtol);
        document.number("max_step_s", options.max_step_s);
        document.number("initial_step_s", options.initial_step_s);
        document.text("absolute_tolerances",
                      "project defaults: position 1e-3 m, velocity 1e-6 m/s, quaternion 1e-12, body rate 1e-12 rad/s");
        document.text("ap_history", benchmark.ap_history ? "enabled" : "disabled");
        document.text("warmup_note",
                      "Warm-up runs are executed and their wall time recorded in execution_order, but they are not "
                      "part of runs, so no summary can accidentally include a warm cache.");

        BmJson host;
        host.integer("logical_cpus_reported", hardware_cpus);
        host.integer("logical_cpus_available_to_process", affinity_cpus);
        host.integer("worker_limit_applied", worker_limit);
        host.text("cpu_model", bm_cpu_model());
        host.text("worker_affinity",
                  "no individual worker is bound; the whole process inherits the caller's affinity, so runs are "
                  "only comparable with the same taskset mask");
        document.raw("host", host.str());

        BmJson build;
        build.text("compiler", __VERSION__);
        build.text("cxx_standard", std::to_string(__cplusplus));
        build.boolean("spice_reuse_diagnostics_compiled_in", bm_spice_diagnostics_available());
        build.boolean("native_mutex_diagnostics_compiled_in", bm_native_diagnostics_available());
        document.raw("build", build.str());

        std::vector<std::string> policy_names;
        for (const BatchScheduling policy : benchmark.policies) policy_names.push_back(bm_scheduling_name(policy));
        std::vector<std::string> workload_names;
        for (const Workload& work : workloads) workload_names.push_back(work.name);
        std::vector<std::string> model_names;
        for (const Workload& work : workloads)
            model_names.push_back(work.atmospheric ? model_name(work.model) : "none");

        document.raw("batch_sizes", bm_json_sizes(benchmark.sizes));
        document.raw("worker_counts", bm_json_sizes(benchmark.workers));
        document.raw("scheduling_policies", bm_json_strings(policy_names));
        document.raw("workloads", bm_json_strings(workload_names));
        document.raw("atmosphere_models", bm_json_strings(model_names));
        document.raw("variants", bm_json_strings(benchmark.variants));

        BmJson matrix;
        matrix.integer("configurations_requested", jobs.size());
        matrix.integer("measured_runs", records.size());
        matrix.integer("configurations_skipped", skipped.size());
        matrix.integer("configurations_failed", failed);
        document.raw("matrix", matrix.str());

        BmJson perturb;
        perturb.text("method",
                     "splitmix64 stream seeded with seed + 0x9E3779B97F4A7C15 * (trajectory_index + 1), uniform in "
                     "[-sigma, sigma] from the top 53 bits, applied to position, velocity and body rate");
        perturb.integer("seed", benchmark.seed);
        perturb.number("position_sigma_m", benchmark.perturbation.position_sigma_m);
        perturb.number("velocity_sigma_m_s", benchmark.perturbation.velocity_sigma_m_s);
        perturb.number("omega_sigma_rad_s", benchmark.perturbation.omega_sigma_rad_s);
        document.raw("perturbation", perturb.str());

        std::ostringstream cases_json;
        cases_json << '[';
        for (std::size_t index = 0; index < specs.size(); ++index) {
            if (index) cases_json << ',';
            BmJson entry;
            entry.text("id", specs[index].id);
            entry.text("file", specs[index].file);
            entry.boolean("legacy", specs[index].legacy);
            entry.text("file_sha256",
                       specs[index].file.empty() ? "unavailable" : sha256_file(specs[index].file));
            cases_json << entry.str();
        }
        cases_json << ']';

        std::ostringstream kernels_json;
        kernels_json << '[';
        for (std::size_t index = 0; index < env.kernels.size(); ++index) {
            if (index) kernels_json << ',';
            BmJson entry;
            entry.text("path", env.kernels[index].string());
            entry.text("sha256", sha256_file(env.kernels[index]));
            kernels_json << entry.str();
        }
        kernels_json << ']';

        const std::vector<std::string> weather_files{config.weather, config.solfsmy, config.dtcfile};
        std::ostringstream weather_json;
        weather_json << '[';
        for (std::size_t index = 0; index < weather_files.size(); ++index) {
            if (index) weather_json << ',';
            BmJson entry;
            entry.text("path", weather_files[index]);
            entry.text("sha256", sha256_file(weather_files[index]));
            weather_json << entry.str();
        }
        weather_json << ']';

        BmJson inputs;
        inputs.raw("cases", cases_json.str());
        inputs.raw("kernels", kernels_json.str());
        inputs.raw("weather_inputs", weather_json.str());
        document.raw("inputs", inputs.str());

        document.raw("skipped_configurations", bm_json_strings(skipped));
        document.raw("execution_order", bm_json_strings(execution_order));

        std::ostringstream runs;
        runs << '[';
        for (std::size_t index = 0; index < records.size(); ++index) {
            if (index) runs << ',';
            const BmRunRecord& record = records[index];
            BmJson json;
            json.integer("run_index", index);
            json.text("configuration_id", record.configuration_id);
            json.text("executor", bm_executor_name(record.job.executor));
            json.text("case_id", record.job.case_id);
            json.text("workload", record.job.work->name);
            json.text("atmosphere_model",
                      record.job.work->atmospheric ? model_name(record.job.work->model) : "none");
            json.text("variant", record.job.variant);
            json.boolean("ap_history", record.job.ap_history);
            json.integer("batch_size", record.job.batch_size);
            json.integer("requested_workers", record.job.workers);
            json.integer("worker_threads_used", record.worker_count);
            if (record.job.executor != Executor::sequential)
                json.text("scheduling", bm_scheduling_name(record.job.scheduling));
            json.integer("repetition", record.job.repetition);
            json.integer("sequential_reference", record.sequential_reference);
            json.number("wall_seconds", record.wall_seconds);
            json.number("trajectories_per_second", record.trajectories_per_second);
            json.number("cpu_seconds", record.cpu_seconds);
            json.number("cpu_utilization", record.cpu_utilization);
            json.number("peak_rss_bytes", record.peak_rss_bytes);
            json.number("rss_bytes_before", record.rss_before_bytes);
            json.number("rss_bytes_after", record.rss_after_bytes);
            json.number("rss_delta_bytes", record.rss_after_bytes - record.rss_before_bytes);
            json.number("dispatch_seconds", record.dispatch_seconds);
            json.number("active_envelope_seconds", record.active_envelope_seconds);
            json.number("join_return_seconds", record.join_return_seconds);
            json.number("rhs_construction_seconds_total", record.construction_seconds);
            json.number("rhs_destruction_seconds_total", record.destruction_seconds);
            json.integer("rhs_calls", record.rhs_calls);
            json.integer("atmosphere_calls", record.atmosphere_calls);
            json.text("final_state_checksum", record.checksum);
            json.raw("trajectory_seconds", Distribution::of(record.trajectory_seconds).json());
            json.raw("propagation_seconds", Distribution::of(record.propagation_seconds).json());
            json.raw("rhs_construction_seconds", Distribution::of(record.construction_seconds_each).json());
            json.raw("trajectory_seconds_series", bm_json_doubles(record.trajectory_seconds));
            json.raw("propagation_seconds_series", bm_json_doubles(record.propagation_seconds));
            json.raw("rhs_calls_per_trajectory", bm_json_sizes(record.trajectory_rhs_calls));
            json.raw("atmosphere_calls_per_trajectory", bm_json_sizes(record.trajectory_atmosphere_calls));
            json.raw("worker_slot_per_trajectory", bm_json_sizes(record.trajectory_slots));
            json.raw("worker_busy_seconds", bm_json_doubles(record.worker_busy_seconds));
            json.raw("worker_span_seconds", bm_json_doubles(record.worker_span_seconds));
            json.number("worker_busy_max_over_span", record.worker_busy_max_over_span);
            json.number("worker_busy_cv", record.worker_busy_cv);
            if (record.has_spice) {
                bm_emit_spice_delta(json, record.spice_before, record.spice_after);
            } else {
                json.null("spice_reuse_delta",
                          "this binary was not compiled against LEO_SPICE_REUSE_DIAGNOSTICS, so reuse and mutex "
                          "counters are unmeasured rather than zero");
            }
            if (record.has_native) {
                bm_emit_native_delta(
                    json, bm_native_delta(record.native_before_nrl, record.native_after_nrl, record.native_before_jb,
                                          record.native_after_jb));
            } else {
                json.null("native_mutex_delta",
                          "this binary was not compiled with MT_NATIVE_DIAGNOSTICS, so native adapter lock time is "
                          "unmeasured rather than zero");
            }
            runs << json.str();
        }
        runs << ']';
        document.raw("runs", runs.str());

        // save() creates the parent directory, which requires a non-empty one.
        const std::filesystem::path path(
            std::filesystem::path(benchmark.json_path).parent_path().empty()
                ? std::filesystem::path(".") / std::filesystem::path(benchmark.json_path).filename()
                : std::filesystem::path(benchmark.json_path));
        // Evidence is never overwritten: an existing benchmark document is a hard
        // error rather than a silent replacement of a previous run.
        if (std::filesystem::exists(path))
            throw std::runtime_error("refusing to overwrite existing benchmark evidence: " + path.string());
        save(path, document.str());
        std::cerr << "wrote " << records.size() << " measured runs, " << skipped.size() << " skipped, " << failed
                  << " failed to " << path.string() << '\n';
        return (failed == 0 && skipped.empty()) ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}