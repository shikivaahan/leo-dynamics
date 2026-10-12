// MT-001 multithreaded correctness validation driver.
//
// It answers one question with evidence: does leo::propagate_batch
// reproduce, bit for bit, what the unchanged single-trajectory propagate()
// produces for the same work, and does it do so for every worker count and both
// scheduling policies, for the frozen CASE-A/B/C fixtures, for every compiled-in
// atmospheric backend?
//
// Scientific configuration comes from the unchanged atmosphere experiment
// translation unit (its `main` renamed), exactly as the profiling and
// optimisation harnesses do: experiment::load, case_dynamics, bench::tolerances
// and sample_labels supply the cases, the force models and the output schedule,
// and Environment supplies one stable, explicitly managed SPICE provider plus
// one immutable WeatherDataset. Nothing here re-implements the physics, and
// there is no synthetic fallback: a missing kernel, weather file or native
// backend is a run failure, never a substitute value.
//
// Compared for every trajectory, against the sequential reference:
//   * the complete trajectory: epoch, every requested output label and all
//     thirteen integrated doubles (position, velocity, quaternion, body rate),
//     compared as raw binary64 bit patterns, not as tolerances;
//   * the complete per-RHS-stage trace recorded by the trajectory's own RHS
//     while it ran: elapsed time, epoch, packed input state, packed derivative,
//     Sun vector, illumination, density, both temperatures and the eight number
//     densities, in call order;
//   * the derived native-model inputs of every stage (UTC calendar, geodetic
//     coordinates, local solar time, weather scalars and ap history) when
//     --validation-input-traces is on;
//   * the output-sample diagnostics: atmospheric inputs and results, drag force
//     and torque, SRP force, altitude, attitude angle, angular speed and total
//     torque magnitude.
// Nothing is accepted on a checksum alone: checksums are published as a
// cross-check, the comparison itself is element by element.
//
// Invocation (see VALIDATION.md for the full command line):
//   mt_validation --validation-out <dir>/correctness.json --mode propagate \
//     --lsk <lsk> --spk <spk> --pck <pck> --weather <cssi> --solfsmy <sol> \
//     --dtcfile <dtc> --epoch-utc 2003-10-30T00:00:00Z \
//     --case docs/numerical-audit/experiments/CASE-A-v1.cfg \
//     --case docs/numerical-audit/experiments/CASE-B-v1.cfg \
//     --case docs/numerical-audit/experiments/CASE-C-v1.cfg \
//     --duration 21600 --cadence 30 --rtol 1e-10 --max-step 15
// --mode defaults to "propagate" and --out defaults to the directory of
// --validation-out, so only the paths above are strictly required. Every other
// option is listed by --validation-help.
//
// Groups run for every case and workload, each against its own sequential
// reference and each repeated --validation-repeats times:
//   identical_initial_states   batch --validation-batch, every --validation-workers
//   perturbed_initial_states   deterministic perturbed initial states
//   more_workers_than_trajectories  small batch, more workers than trajectories
//   every_host_worker          batch == --validation-wide-batch workers, sixteen
//                              independent trajectories in flight at once
//   ap_history_enabled         the ap-history native switch on (skipped when the
//                              whole run already uses --ap-history)
// Both scheduling policies run for every configuration, so the matrix is
// (case x workload x group x worker count x policy x repeat).
//
// Exit status is nonzero if any comparison fails or any configuration is
// invalid. Failed comparisons are printed in full and also recorded in the
// machine-readable document.
#ifndef MT_EXPERIMENT_HEADER
// Fallback for a source-tree build without the CMake-provided absolute path.
#define MT_EXPERIMENT_HEADER "../atmosphere/experiment.cpp"
#endif
#define main mt_experiment_main_renamed
#include MT_EXPERIMENT_HEADER
#undef main

#include "leo/analysis/loads.hpp"
#include "leo/analysis/orbital.hpp"
#include "leo/atmosphere/coordinates.hpp"
#include "leo/integration/batch.hpp"
#include "leo/integration/detail/packed_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace leo;

namespace {


// ---------------------------------------------------------------------------
// JSON emission. Every number is written at full binary64 precision; a non-finite
// value is emitted as an explicit tagged string rather than as invalid JSON or a
// silent zero.
// ---------------------------------------------------------------------------

std::string hex_bits(double value) {
    std::uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof raw);
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(16) << raw;
    return out.str();
}

bool bit_identical(double expected, double actual) {
    return std::memcmp(&expected, &actual, sizeof(double)) == 0;
}

std::string json_double(double value) {
    if (!std::isfinite(value)) return "\"nonfinite:" + hex_bits(value) + "\"";
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

std::string json_text(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (const char character : value) {
        if (character == '"' || character == '\\') escaped.push_back('\\');
        escaped.push_back(character);
    }
    return '"' + escaped + '"';
}

std::string json_sizes(const std::vector<std::size_t>& values) {
    std::string out = "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) out += ',';
        out += std::to_string(values[index]);
    }
    return out + ']';
}

std::string json_strings(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) out += ',';
        out += json_text(values[index]);
    }
    return out + ']';
}

class JsonWriter {
public:
    std::ostringstream out;
    bool first_ = true;

    JsonWriter() { out << std::setprecision(17) << '{'; }

    void key(const std::string& name) {
        if (!first_) out << ',';
        first_ = false;
        out << json_text(name) << ':';
    }
    void number(const std::string& name, double value) { key(name); out << json_double(value); }
    void integer(const std::string& name, long long value) { key(name); out << value; }
    void unsigned_field(const std::string& name, std::size_t value) { integer(name, static_cast<long long>(value)); }
    void flag(const std::string& name, bool value) { key(name); out << (value ? "true" : "false"); }
    void text(const std::string& name, const std::string& value) { key(name); out << json_text(value); }
    void raw(const std::string& name, const std::string& value) { key(name); out << value; }
    std::string str() const { return out.str() + '}'; }
};

std::string json_array_of(const std::vector<std::string>& elements) {
    std::string out = "[";
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (index) out += ',';
        out += elements[index];
    }
    return out + ']';
}

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

struct ValidationOptions {
    std::string json_path;               // --validation-out
    std::size_t batch = 4;               // --validation-batch
    std::size_t repeats = 2;             // --validation-repeats
    std::size_t oversized_batch = 2;     // --validation-oversized-batch (workers > batch)
    std::size_t wide_batch = 16;         // --validation-wide-batch (exercise every host worker)
    std::vector<std::size_t> workers{1, 2, 4, 8, 16};
    std::vector<std::size_t> perturbed_workers{4, 16};
    std::vector<std::size_t> oversized_workers{16};
    std::vector<std::size_t> ap_history_workers{1, 4};
    // The 16-task, 16-worker group is the only way to prove that sixteen
    // independent trajectories really are in flight at once; it is scoped by
    // name because running it on every six-hour workload dominates the run
    // time. "*" selects every workload.
    std::vector<std::string> wide_workloads{"two_body", "j2"};
    bool perturbed = true;
    bool oversized = true;
    bool wide = true;
    bool ap_history_phase = true;
    bool core_traces = true;
    bool input_traces = true;
    std::uint64_t seed = 0x5DEECE66D0F1A7C3ull;  // --validation-seed
    double position_sigma_m = 50.0;               // --validation-position-sigma
    double velocity_sigma_m_s = 0.02;            // --validation-velocity-sigma
    double omega_sigma_rad_s = 1e-4;             // --validation-omega-sigma
    std::size_t mismatch_limit = 25;             // --validation-mismatch-limit
};

std::size_t parse_count(const std::string& text, const char* key) {
    std::size_t used = 0;
    const unsigned long long parsed = std::stoull(text, &used);
    if (used != text.size()) throw std::invalid_argument(std::string("invalid value for ") + key + ": " + text);
    return static_cast<std::size_t>(parsed);
}

std::vector<std::size_t> parse_counts(const std::string& text, const char* key) {
    std::vector<std::size_t> values;
    std::istringstream rows(text);
    std::string item;
    while (std::getline(rows, item, ',')) {
        if (item.empty()) continue;
        values.push_back(parse_count(item, key));
    }
    if (values.empty()) throw std::invalid_argument(std::string(key) + " needs at least one count");
    return values;
}

bool parse_switch(const std::string& text, const char* key) {
    if (text == "on" || text == "true" || text == "1") return true;
    if (text == "off" || text == "false" || text == "0") return false;
    throw std::invalid_argument(std::string(key) + " expects on or off, got: " + text);
}

const char* scheduling_name(BatchScheduling scheduling) {
    switch (scheduling) {
    case BatchScheduling::static_partition: return "static_partition";
    case BatchScheduling::dynamic: return "dynamic";
    }
    return "unknown";
}

const std::vector<BatchScheduling> all_schedulings() {
    return {BatchScheduling::static_partition, BatchScheduling::dynamic};
}

// Every --validation-* option is consumed here; everything else is forwarded
// verbatim to the unchanged experiment argument parser.
std::vector<std::string> split_arguments(int argc, char** argv, ValidationOptions& validation) {
    const auto value_of = [&](int& index, const std::string& key) -> std::string {
        if (index + 1 == argc) throw std::invalid_argument("missing value for " + key);
        return argv[++index];
    };
    const std::vector<std::string> valued{"--lsk", "--spk", "--pck", "--frame", "--weather", "--solfsmy",
                                          "--dtcfile", "--epoch-utc", "--case", "--out", "--mode", "--fixture",
                                          "--duration", "--cadence", "--rtol", "--max-step", "--refine-rtol",
                                          "--refine-max-step", "--timing-calls", "--timing-repeats",
                                          "--timing-propagation-repeats", "--timing-threads", "--utc-reuse",
                                          "--sun-sharing", "--orientation-reuse"};
    const std::vector<std::string> flags{"--ap-history"};
    std::vector<std::string> forwarded{argv[0]};
    bool have_out = false, have_mode = false;
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        if (key == "--validation-out") { validation.json_path = value_of(index, key); continue; }
        if (key == "--validation-batch") { validation.batch = parse_count(value_of(index, key), "--validation-batch"); continue; }
        if (key == "--validation-repeats") { validation.repeats = parse_count(value_of(index, key), "--validation-repeats"); continue; }
        if (key == "--validation-oversized-batch") { validation.oversized_batch = parse_count(value_of(index, key), "--validation-oversized-batch"); continue; }
        if (key == "--validation-wide-batch") { validation.wide_batch = parse_count(value_of(index, key), "--validation-wide-batch"); continue; }
        if (key == "--validation-wide-workloads") {
            const std::string raw = value_of(index, key);
            validation.wide_workloads.clear();
            std::istringstream rows(raw);
            std::string item;
            while (std::getline(rows, item, ',')) if (!item.empty()) validation.wide_workloads.push_back(item);
            if (validation.wide_workloads.empty())
                throw std::invalid_argument("--validation-wide-workloads needs at least one name");
            continue;
        }
        if (key == "--validation-workers") { validation.workers = parse_counts(value_of(index, key), "--validation-workers"); continue; }
        if (key == "--validation-perturbed-workers") { validation.perturbed_workers = parse_counts(value_of(index, key), "--validation-perturbed-workers"); continue; }
        if (key == "--validation-oversized-workers") { validation.oversized_workers = parse_counts(value_of(index, key), "--validation-oversized-workers"); continue; }
        if (key == "--validation-ap-history-workers") { validation.ap_history_workers = parse_counts(value_of(index, key), "--validation-ap-history-workers"); continue; }
        if (key == "--validation-perturbed") { validation.perturbed = parse_switch(value_of(index, key), "--validation-perturbed"); continue; }
        if (key == "--validation-oversized") { validation.oversized = parse_switch(value_of(index, key), "--validation-oversized"); continue; }
        if (key == "--validation-wide") { validation.wide = parse_switch(value_of(index, key), "--validation-wide"); continue; }
        if (key == "--validation-ap-history-matrix") { validation.ap_history_phase = parse_switch(value_of(index, key), "--validation-ap-history-matrix"); continue; }
        if (key == "--validation-traces") { validation.core_traces = parse_switch(value_of(index, key), "--validation-traces"); continue; }
        if (key == "--validation-input-traces") { validation.input_traces = parse_switch(value_of(index, key), "--validation-input-traces"); continue; }
        if (key == "--validation-seed") {
            std::size_t used = 0;
            validation.seed = std::stoull(value_of(index, key), &used, 0);
            continue;
        }
        if (key == "--validation-position-sigma") { validation.position_sigma_m = std::stod(value_of(index, key)); continue; }
        if (key == "--validation-velocity-sigma") { validation.velocity_sigma_m_s = std::stod(value_of(index, key)); continue; }
        if (key == "--validation-omega-sigma") { validation.omega_sigma_rad_s = std::stod(value_of(index, key)); continue; }
        if (key == "--validation-mismatch-limit") { validation.mismatch_limit = parse_count(value_of(index, key), "--validation-mismatch-limit"); continue; }
        if (key == "--validation-help") {
            std::cout << "mt_validation options (all other arguments are the atmosphere experiment's):\n"
                      << "  --validation-out PATH            machine-readable correctness.json (required)\n"
                      << "  --validation-batch N             trajectories per batch (default 4)\n"
                      << "  --validation-repeats N           identical repetitions of every batch (default 2)\n"
                      << "  --validation-workers LIST        worker counts, comma separated (default 1,2,4,8,16)\n"
                      << "  --validation-perturbed-workers L worker counts for the perturbed-initial-state batches\n"
                      << "  --validation-perturbed on|off    deterministic perturbed batches (default on)\n"
                      << "  --validation-oversized on|off    small batch with more workers than trajectories\n"
                      << "  --validation-oversized-batch N   trajectories for that group (default 2)\n"
                      << "  --validation-oversized-workers L worker counts for that group (default 16)\n"
                      << "  --validation-wide on|off        exercise every host worker (two-body and J2 only)\n"
                      << "  --validation-wide-batch N        trajectories for that group (default 16)\n"
                      << "  --validation-wide-workloads LIST workloads that run it (default two_body,j2)\n"
                      << "  --validation-ap-history-matrix on|off  extra group with the ap-history switch on\n"
                      << "  --validation-traces on|off       per-RHS-stage trace comparison (default on)\n"
                      << "  --validation-input-traces on|off native-model input trace comparison (default on)\n"
                      << "  --validation-seed N              perturbation seed (default 0x5DEECE66D0F1A7C3)\n"
                      << "  --validation-position-sigma M    position perturbation half width (default 50 m)\n"
                      << "  --validation-velocity-sigma M/S  velocity perturbation half width (default 0.02 m/s)\n"
                      << "  --validation-omega-sigma RAD/S   body-rate perturbation half width (default 1e-4)\n"
                      << "  --validation-mismatch-limit N    mismatches recorded per comparison (default 25)\n";
            std::exit(0);
        }
        forwarded.push_back(key);
        if (key == "--out") have_out = true;
        if (key == "--mode") have_mode = true;
        const bool takes_value = std::find(valued.begin(), valued.end(), key) != valued.end();
        const bool is_flag = std::find(flags.begin(), flags.end(), key) != flags.end();
        if (takes_value && index + 1 < argc) forwarded.push_back(argv[++index]);
        else if (!takes_value && !is_flag && index + 1 < argc && key.rfind("--", 0) == 0)
            // An argument the unchanged parser does not know: forward the value
            // too, so its own message is the one the user sees.
            forwarded.push_back(argv[++index]);
    }
    if (validation.repeats == 0) throw std::invalid_argument("--validation-repeats must be at least one");
    if (validation.batch == 0 || validation.oversized_batch == 0 || validation.wide_batch == 0)
        throw std::invalid_argument("batch sizes must be at least one");
    if (!have_mode) { forwarded.push_back("--mode"); forwarded.push_back("propagate"); }
    if (!have_out) {
        if (validation.json_path.empty())
            throw std::invalid_argument("--validation-out is required (or --out, defaulting correctness.json)");
        forwarded.push_back("--out");
        const std::filesystem::path parent = std::filesystem::path(validation.json_path).parent_path();
        forwarded.push_back(parent.empty() ? std::string(".") : parent.string());
    }
    return forwarded;
}

// ---------------------------------------------------------------------------
// Deterministic initial-state perturbation
// ---------------------------------------------------------------------------

std::uint64_t splitmix64(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Uniform in [-sigma, sigma], drawn from the top 53 bits so the mapping is exact
// and reproducible on any conforming implementation.
double uniform_offset(std::uint64_t& state, double sigma) {
    const double unit = static_cast<double>(splitmix64(state) >> 11) * 0x1.0p-53;
    return sigma * (2.0 * unit - 1.0);
}

std::vector<State> perturbed_initials(const std::vector<State>& base, const ValidationOptions& validation) {
    std::vector<State> perturbed = base;
    for (std::size_t index = 0; index < perturbed.size(); ++index) {
        std::uint64_t state = validation.seed + 0x9E3779B97F4A7C15ull * (index + 1);
        State& state_value = perturbed[index];
        state_value.r = state_value.r + InertialVector{uniform_offset(state, validation.position_sigma_m),
                                                       uniform_offset(state, validation.position_sigma_m),
                                                       uniform_offset(state, validation.position_sigma_m)};
        state_value.v = state_value.v + InertialVector{uniform_offset(state, validation.velocity_sigma_m_s),
                                                       uniform_offset(state, validation.velocity_sigma_m_s),
                                                       uniform_offset(state, validation.velocity_sigma_m_s)};
        state_value.omega = state_value.omega + BodyVector{uniform_offset(state, validation.omega_sigma_rad_s),
                                                           uniform_offset(state, validation.omega_sigma_rad_s),
                                                           uniform_offset(state, validation.omega_sigma_rad_s)};
        state_value = validated_state(state_value);
    }
    return perturbed;
}

// ---------------------------------------------------------------------------
// Workloads
// ---------------------------------------------------------------------------

enum class WorkloadKind { two_body, j2, full };

struct WorkloadSpec {
    std::string name;
    WorkloadKind kind;
    bool atmospheric = false;
    leo::atmosphere::Model model = leo::atmosphere::Model::exponential;
};

std::vector<WorkloadSpec> validation_workloads() {
    std::vector<WorkloadSpec> list;
    list.push_back({"two_body", WorkloadKind::two_body, false, leo::atmosphere::Model::exponential});
    list.push_back({"j2", WorkloadKind::j2, false, leo::atmosphere::Model::exponential});
    for (const auto model : requested_models())
        list.push_back({std::string("full_") + model_name(model), WorkloadKind::full, true, model});
    return list;
}

// Two-body and J2 use the CASE spacecraft with every environmental contribution
// disabled, so the same craft, attitude and initial state are exercised without
// an atmosphere. The full workload is the frozen CASE-A/B/C configuration, whose
// only free parameter is the atmospheric backend.
EarthDynamics workload_dynamics(const WorkloadSpec& work, const experiment::Case& spec) {
    if (work.kind == WorkloadKind::full) return case_dynamics(spec);
    const Spacecraft base = spec.craft();
    std::vector<Surface> surfaces(base.surfaces().begin(), base.surfaces().end());
    const Spacecraft craft(base.mass_kg(), base.inertia(), std::move(surfaces));
    const bool j2 = work.kind == WorkloadKind::j2;
    return EarthDynamics(EarthGravity(Earth{}, j2), craft);
}

// ---------------------------------------------------------------------------
// Recorded evidence
// ---------------------------------------------------------------------------

struct TraceRow {
    double elapsed_s = 0, epoch_et_s = 0, illumination = 0;
    std::array<double, 3> sun_m{};
    double density = 0, temperature = 0, exospheric = 0;
    std::array<double, 8> number_density{};
    std::array<double, 13> input{};
    std::array<double, 13> derivative{};
    std::uint32_t has_temperatures = 0, has_constituents = 0;
};

// The values the native models actually receive, recomputed from the same
// public helpers the provider uses, so a shared-resource disagreement would show
// up here even when the density happened to round to the same double.
struct InputRow {
    double utc_year = 0, utc_day_of_year = 0, utc_seconds_of_day = 0, mjd_utc = 0, utc_day_length_s = 0;
    double latitude_rad = 0, longitude_rad = 0, altitude_m = 0, local_solar_time_hours = 0;
    double sun_ra_rad = 0, sun_dec_rad = 0, position_ra_rad = 0, position_lat_rad = 0, position_alt_km = 0;
    double f10 = 0, f10_average = 0, s10 = 0, s10_average = 0;
    double m10 = 0, m10_average = 0, y10 = 0, y10_average = 0, dstdtc = 0;
    double f107 = 0, f107_average = 0, ap = 0, latitude_deg = 0, longitude_deg = 0;
    std::array<double, 7> ap_history{};
};

struct SampleDiagnostics {
    double epoch_et_s = 0, illumination = 0, density = 0, temperature = 0, exospheric = 0;
    double altitude_m = 0, attitude_angle = 0, angular_speed = 0, total_torque = 0;
    std::array<double, 3> sun_m{}, drag_force_n{}, drag_torque_nm{}, srp_force_n{}, total_force_n{};
    std::array<double, 8> number_density{};
    std::uint32_t has_temperatures = 0, has_constituents = 0;
};

struct Record {
    leo::Trajectory trajectory;
    std::vector<TraceRow> trace;
    std::vector<InputRow> inputs;
    std::vector<SampleDiagnostics> diagnostics;
    std::size_t rhs_calls = 0, atmosphere_calls = 0;
    bool core_traces = true, input_traces = true;
    std::uint64_t trajectory_checksum = 0, trace_checksum = 0, diagnostics_checksum = 0;
};

// ---------------------------------------------------------------------------
// Per-trajectory RHS. One instance per trajectory, created by the batch factory
// on the owning worker (or inline for the sequential reference); it shares the
// single stable provider and the immutable weather dataset, and owns its own
// dynamics object, its own DirectAtmosphere and its own recording buffer.
// ---------------------------------------------------------------------------

class TrajectoryRhs {
    const WorkloadSpec& work_;
    const Environment& env_;
    const Config& config_;
    leo::Epoch epoch_;
    EarthDynamics dynamics_;
    std::unique_ptr<leo::atmosphere::DirectAtmosphere> atmosphere_;
    Record* record_ = nullptr;

public:
    TrajectoryRhs(const WorkloadSpec& work, const experiment::Case& spec, const Environment& env,
                  const Config& config, leo::Epoch epoch, Record* record)
        : work_(work), env_(env), config_(config), epoch_(epoch), dynamics_(workload_dynamics(work, spec)),
          record_(record) {
        if (work_.atmospheric)
            atmosphere_ = std::make_unique<leo::atmosphere::DirectAtmosphere>(
                work_.model, env_.ephemeris, env_.weather, config_.frame, config_.ap_history, config_.options);
    }

    StateDerivative operator()(ElapsedTime t, const State& s) const {
        ++record_->rhs_calls;
        if (!work_.atmospheric) {
            const StateDerivative derivative = dynamics_(t, s);
            if (record_->core_traces) record_stage(t, epoch_.et_seconds + t.seconds, s, derivative);
            return derivative;
        }
        const leo::Epoch at{epoch_.et_seconds + t.seconds};
        leo::EnvironmentSnapshot snapshot;
        snapshot.epoch = at;
        // Existing atmospheric corotation assumption, unchanged.
        snapshot.atmosphere_angular_velocity_rad_s = InertialVector{0, 0, 7.292115e-5};
        const auto observation = env_.ephemeris.observe_sun(at);
        const InertialVector sun = observation.state().position_m;
        snapshot.sun_position_m = sun;
        snapshot.illumination =
            illumination_factor(EclipseModel::conical, s.r, sun, Earth{}.equatorial_radius_m);
        const auto result = atmosphere_->evaluate(at, s.r, &observation);
        if (!std::isfinite(result.density_kg_m3) || result.density_kg_m3 < 0)
            throw std::runtime_error("nonfinite or negative atmosphere density");
        snapshot.density_kg_m3 = result.density_kg_m3;
        ++record_->atmosphere_calls;
        const StateDerivative derivative = dynamics_(t, s, snapshot);
        if (record_->core_traces) {
            record_stage(t, at.et_seconds, s, derivative, sun, snapshot.illumination, result);
            if (record_->input_traces) record_->inputs.push_back(native_inputs(at, s.r, sun));
        }
        return derivative;
    }

private:
    // One recorded RHS stage. Without an atmosphere the environmental fields stay
    // at their zero value on both sides of the comparison; the stage order, the
    // packed input state and the packed derivative are still compared element by
    // element, which is what proves that no worker interleaved or reordered the
    // adaptive step sequence of another trajectory.
    void record_stage(ElapsedTime t, double epoch_et_s, const State& s, const StateDerivative& derivative,
                      InertialVector sun = InertialVector{0, 0, 0}, double illumination = 0,
                      const leo::atmosphere::Result& result = leo::atmosphere::Result{}) const {
        TraceRow row;
        row.elapsed_s = t.seconds;
        row.epoch_et_s = epoch_et_s;
        row.illumination = illumination;
        row.sun_m = {sun.x(), sun.y(), sun.z()};
        row.density = result.density_kg_m3;
        row.temperature = result.temperature_k;
        row.exospheric = result.exospheric_temperature_k;
        row.number_density = result.number_density_m3;
        row.has_temperatures = result.has_temperatures ? 1u : 0u;
        row.has_constituents = result.has_constituents ? 1u : 0u;
        const Packed packed_input = leo::detail::pack(s);
        const Packed packed_derivative = leo::detail::pack(derivative);
        std::copy(packed_input.begin(), packed_input.end(), row.input.begin());
        std::copy(packed_derivative.begin(), packed_derivative.end(), row.derivative.begin());
        record_->trace.push_back(row);
    }

    // Reproduces, with the same public helpers the provider uses internally, the
    // coordinates and weather scalars handed to the native backends.
    InputRow native_inputs(leo::Epoch at, InertialVector position, InertialVector sun) const {
        InputRow row;
        const auto utc = leo::SpiceEphemerisProvider::utc_calendar(at, config_.options.utc_reuse);
        const auto rotation =
            leo::SpiceEphemerisProvider::earth_fixed_rotation(at, config_.frame, config_.options.orientation_reuse);
        const Eigen::Vector3d fixed = rotation * position.as_eigen();
        const auto geodetic = leo::atmosphere::geodetic(fixed);
        row.utc_year = utc.year;
        row.utc_day_of_year = utc.day_of_year;
        row.utc_seconds_of_day = utc.seconds_of_day;
        row.mjd_utc = utc.mjd;
        row.utc_day_length_s = utc.day_length_seconds;
        row.latitude_rad = geodetic.latitude_rad;
        row.longitude_rad = geodetic.longitude_rad;
        row.altitude_m = geodetic.altitude_m;
        constexpr double degrees = 180 / std::numbers::pi;
        const double longitude_deg = geodetic.longitude_rad * degrees;
        row.latitude_deg = geodetic.latitude_rad * degrees;
        row.longitude_deg = longitude_deg;
        row.local_solar_time_hours =
            std::fmod(utc.seconds_of_day / 3600 + longitude_deg / 15 + 24, 24);
        const Eigen::Vector3d fixed_sun = rotation * sun.as_eigen();
        row.sun_ra_rad = std::atan2(fixed_sun.y(), fixed_sun.x());
        row.sun_dec_rad = std::atan2(fixed_sun.z(), std::hypot(fixed_sun.x(), fixed_sun.y()));
        row.position_ra_rad = geodetic.longitude_rad;
        row.position_lat_rad = std::atan2(fixed.z(), std::hypot(fixed.x(), fixed.y()));
        row.position_alt_km = geodetic.altitude_m / 1000;
        if (work_.model == leo::atmosphere::Model::nrlmsise00) {
            const auto in = env_.weather.nrlmsise(utc, config_.ap_history);
            row.f107 = in.f107;
            row.f107_average = in.f107_average;
            row.ap = in.ap;
            row.ap_history = in.ap_history;
        } else if (work_.model == leo::atmosphere::Model::jb2008) {
            const auto in = env_.weather.jb2008(utc);
            row.f10 = in.f10;
            row.f10_average = in.f10_average;
            row.s10 = in.s10;
            row.s10_average = in.s10_average;
            row.m10 = in.m10;
            row.m10_average = in.m10_average;
            row.y10 = in.y10;
            row.y10_average = in.y10_average;
            row.dstdtc = in.dstdtc_k;
        }
        return row;
    }
};

// ---------------------------------------------------------------------------
// Output-sample diagnostics. Diagnostic only: it re-evaluates the loads and the
// atmosphere at the reported output times after propagation and never feeds the
// integrated state. It runs identically for the reference and for every batch.
// ---------------------------------------------------------------------------

void collect_diagnostics(Record& record, const WorkloadSpec& work, const experiment::Case& spec,
                         const Environment& env, const Config& config, leo::Epoch epoch) {
    const EarthDynamics dynamics = workload_dynamics(work, spec);
    const Earth earth;
    std::unique_ptr<leo::atmosphere::DirectAtmosphere> atmosphere;
    if (work.atmospheric)
        atmosphere = std::make_unique<leo::atmosphere::DirectAtmosphere>(
            work.model, env.ephemeris, env.weather, config.frame, config.ap_history, config.options);

    std::vector<leo::EnvironmentSnapshot> snapshots;
    snapshots.reserve(record.trajectory.samples.size());
    for (const auto& sample : record.trajectory.samples) {
        const leo::Epoch at{epoch.et_seconds + sample.t.seconds};
        leo::EnvironmentSnapshot snapshot;
        snapshot.epoch = at;
        snapshot.atmosphere_angular_velocity_rad_s = InertialVector{0, 0, 7.292115e-5};
        if (work.atmospheric) {
            const auto observation = env.ephemeris.observe_sun(at);
            const InertialVector sun = observation.state().position_m;
            snapshot.sun_position_m = sun;
            snapshot.illumination =
                illumination_factor(EclipseModel::conical, sample.state.r, sun, earth.equatorial_radius_m);
            snapshot.density_kg_m3 = atmosphere->evaluate(at, sample.state.r, &observation).density_kg_m3;
        }
        snapshots.push_back(snapshot);
    }
    const auto loads = leo::analysis::environmental_history(record.trajectory, dynamics, snapshots);
    const auto attitude = leo::analysis::attitude_history(record.trajectory, dynamics.spacecraft(), loads);
    const auto orbit = leo::analysis::orbital_history(record.trajectory, earth);
    const Quaternion initial = record.trajectory.samples.front().state.q;

    record.diagnostics.reserve(record.trajectory.samples.size());
    for (std::size_t index = 0; index < record.trajectory.samples.size(); ++index) {
        const auto& sample = record.trajectory.samples[index];
        SampleDiagnostics entry;
        entry.epoch_et_s = snapshots[index].epoch.et_seconds;
        entry.illumination = snapshots[index].illumination;
        entry.density = snapshots[index].density_kg_m3;
        entry.altitude_m = orbit[index].altitude_m;
        entry.attitude_angle = angle(initial, sample.state.q);
        entry.angular_speed = attitude[index].angular_speed_rad_s;
        entry.total_torque = attitude[index].torque_magnitude_nm;
        entry.sun_m = {snapshots[index].sun_position_m.x(), snapshots[index].sun_position_m.y(),
                       snapshots[index].sun_position_m.z()};
        if (work.atmospheric) {
            const auto result = atmosphere->evaluate(snapshots[index].epoch, sample.state.r);
            entry.temperature = result.temperature_k;
            entry.exospheric = result.exospheric_temperature_k;
            entry.number_density = result.number_density_m3;
            entry.has_temperatures = result.has_temperatures ? 1u : 0u;
            entry.has_constituents = result.has_constituents ? 1u : 0u;
        }
        const auto contributions = dynamics.evaluate(sample.t, sample.state, snapshots[index]);
        entry.drag_force_n = {contributions.drag.force_n.x(), contributions.drag.force_n.y(),
                              contributions.drag.force_n.z()};
        entry.drag_torque_nm = {contributions.drag.torque_nm.x(), contributions.drag.torque_nm.y(),
                                contributions.drag.torque_nm.z()};
        entry.srp_force_n = {contributions.srp.force_n.x(), contributions.srp.force_n.y(),
                             contributions.srp.force_n.z()};
        const InertialVector total = contributions.srp.force_n + contributions.albedo.force_n + contributions.drag.force_n;
        entry.total_force_n = {total.x(), total.y(), total.z()};
        record.diagnostics.push_back(entry);
    }
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

struct MismatchSink {
    std::vector<std::string> details;
    std::size_t total = 0;
    std::size_t limit = 25;

    void add(std::string detail) {
        ++total;
        if (details.size() < limit) details.push_back(std::move(detail));
    }
    bool empty() const { return total == 0; }
};

void note_double(double expected, double actual, const std::string& where, const std::string& field,
                 MismatchSink& sink) {
    if (bit_identical(expected, actual)) return;
    sink.add(where + ": " + field + " differs bitwise, reference " + hex_bits(expected) + " batch " +
             hex_bits(actual));
}

void note_unsigned(long long expected, long long actual, const std::string& where, const std::string& field,
                   MismatchSink& sink) {
    if (expected == actual) return;
    sink.add(where + ": " + field + " differs, reference " + std::to_string(expected) + " batch " +
             std::to_string(actual));
}

template <std::size_t N>
void note_doubles(const std::array<double, N>& expected, const std::array<double, N>& actual,
                  const std::string& where, const std::string& field, MismatchSink& sink) {
    for (std::size_t index = 0; index < N; ++index)
        if (!bit_identical(expected[index], actual[index]))
            note_double(expected[index], actual[index], where, field + "[" + std::to_string(index) + "]", sink);
}

void note_trace(const TraceRow& expected, const TraceRow& actual, const std::string& where, MismatchSink& sink) {
    note_double(expected.elapsed_s, actual.elapsed_s, where, "elapsed_s", sink);
    note_double(expected.epoch_et_s, actual.epoch_et_s, where, "epoch_et_s", sink);
    note_double(expected.illumination, actual.illumination, where, "illumination", sink);
    note_doubles(expected.sun_m, actual.sun_m, where, "sun_m", sink);
    note_double(expected.density, actual.density, where, "density_kg_m3", sink);
    note_double(expected.temperature, actual.temperature, where, "temperature_k", sink);
    note_double(expected.exospheric, actual.exospheric, where, "exospheric_temperature_k", sink);
    note_doubles(expected.number_density, actual.number_density, where, "number_density_m3", sink);
    note_unsigned(expected.has_temperatures, actual.has_temperatures, where, "has_temperatures", sink);
    note_unsigned(expected.has_constituents, actual.has_constituents, where, "has_constituents", sink);
    note_doubles(expected.input, actual.input, where, "input", sink);
    note_doubles(expected.derivative, actual.derivative, where, "derivative", sink);
}

void note_inputs(const InputRow& expected, const InputRow& actual, const std::string& where, MismatchSink& sink) {
    note_double(expected.utc_year, actual.utc_year, where, "utc_year", sink);
    note_double(expected.utc_day_of_year, actual.utc_day_of_year, where, "utc_day_of_year", sink);
    note_double(expected.utc_seconds_of_day, actual.utc_seconds_of_day, where, "utc_seconds_of_day", sink);
    note_double(expected.mjd_utc, actual.mjd_utc, where, "mjd_utc", sink);
    note_double(expected.utc_day_length_s, actual.utc_day_length_s, where, "utc_day_length_s", sink);
    note_double(expected.latitude_rad, actual.latitude_rad, where, "latitude_rad", sink);
    note_double(expected.longitude_rad, actual.longitude_rad, where, "longitude_rad", sink);
    note_double(expected.altitude_m, actual.altitude_m, where, "altitude_m", sink);
    note_double(expected.local_solar_time_hours, actual.local_solar_time_hours, where, "local_solar_time_hours", sink);
    note_double(expected.sun_ra_rad, actual.sun_ra_rad, where, "sun_ra_rad", sink);
    note_double(expected.sun_dec_rad, actual.sun_dec_rad, where, "sun_dec_rad", sink);
    note_double(expected.position_ra_rad, actual.position_ra_rad, where, "position_ra_rad", sink);
    note_double(expected.position_lat_rad, actual.position_lat_rad, where, "position_lat_rad", sink);
    note_double(expected.position_alt_km, actual.position_alt_km, where, "position_alt_km", sink);
    note_double(expected.f10, actual.f10, where, "f10", sink);
    note_double(expected.f10_average, actual.f10_average, where, "f10_average", sink);
    note_double(expected.s10, actual.s10, where, "s10", sink);
    note_double(expected.s10_average, actual.s10_average, where, "s10_average", sink);
    note_double(expected.m10, actual.m10, where, "m10", sink);
    note_double(expected.m10_average, actual.m10_average, where, "m10_average", sink);
    note_double(expected.y10, actual.y10, where, "y10", sink);
    note_double(expected.y10_average, actual.y10_average, where, "y10_average", sink);
    note_double(expected.dstdtc, actual.dstdtc, where, "dstdtc_k", sink);
    note_double(expected.f107, actual.f107, where, "f107", sink);
    note_double(expected.f107_average, actual.f107_average, where, "f107_average", sink);
    note_double(expected.ap, actual.ap, where, "ap_daily", sink);
    note_double(expected.latitude_deg, actual.latitude_deg, where, "latitude_deg", sink);
    note_double(expected.longitude_deg, actual.longitude_deg, where, "longitude_deg", sink);
    note_doubles(expected.ap_history, actual.ap_history, where, "ap_history", sink);
}

void note_diagnostics(const SampleDiagnostics& expected, const SampleDiagnostics& actual,
                      const std::string& where, MismatchSink& sink) {
    note_double(expected.epoch_et_s, actual.epoch_et_s, where, "epoch_et_s", sink);
    note_double(expected.illumination, actual.illumination, where, "illumination", sink);
    note_double(expected.density, actual.density, where, "density_kg_m3", sink);
    note_double(expected.temperature, actual.temperature, where, "temperature_k", sink);
    note_double(expected.exospheric, actual.exospheric, where, "exospheric_temperature_k", sink);
    note_double(expected.altitude_m, actual.altitude_m, where, "altitude_m", sink);
    note_double(expected.attitude_angle, actual.attitude_angle, where, "attitude_angle_rad", sink);
    note_double(expected.angular_speed, actual.angular_speed, where, "angular_speed_rad_s", sink);
    note_double(expected.total_torque, actual.total_torque, where, "total_torque_magnitude_nm", sink);
    note_doubles(expected.sun_m, actual.sun_m, where, "sun_m", sink);
    note_doubles(expected.drag_force_n, actual.drag_force_n, where, "drag_force_n", sink);
    note_doubles(expected.drag_torque_nm, actual.drag_torque_nm, where, "drag_torque_nm", sink);
    note_doubles(expected.srp_force_n, actual.srp_force_n, where, "srp_force_n", sink);
    note_doubles(expected.total_force_n, actual.total_force_n, where, "total_force_n", sink);
    note_doubles(expected.number_density, actual.number_density, where, "number_density_m3", sink);
    note_unsigned(expected.has_temperatures, actual.has_temperatures, where, "has_temperatures", sink);
    note_unsigned(expected.has_constituents, actual.has_constituents, where, "has_constituents", sink);
}

std::uint64_t fold(std::uint64_t accumulator, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    accumulator ^= bits;
    return accumulator * 1099511628211ull;
}

std::uint64_t fold(std::uint64_t accumulator, std::uint64_t value) {
    accumulator ^= value;
    return accumulator * 1099511628211ull;
}

void finalize_checksums(Record& record) {
    Checksum trajectory;
    trajectory.add(record.trajectory.epoch.et_seconds);
    for (const auto& sample : record.trajectory.samples) {
        trajectory.add(sample.t.seconds);
        trajectory.add(sample.state);
    }
    record.trajectory_checksum = std::stoull(trajectory.str(), nullptr, 16);
    std::uint64_t trace = 14695981039346656037ull;
    for (const auto& row : record.trace) {
        trace = fold(trace, row.elapsed_s);
        trace = fold(trace, row.epoch_et_s);
        trace = fold(trace, row.illumination);
        for (const double value : row.sun_m) trace = fold(trace, value);
        trace = fold(trace, row.density);
        trace = fold(trace, row.temperature);
        trace = fold(trace, row.exospheric);
        for (const double value : row.number_density) trace = fold(trace, value);
        for (const double value : row.input) trace = fold(trace, value);
        for (const double value : row.derivative) trace = fold(trace, value);
    }
    record.trace_checksum = trace;
    std::uint64_t diagnostics = 14695981039346656037ull;
    for (const auto& entry : record.diagnostics) {
        diagnostics = fold(diagnostics, entry.epoch_et_s);
        diagnostics = fold(diagnostics, entry.illumination);
        diagnostics = fold(diagnostics, entry.density);
        diagnostics = fold(diagnostics, entry.temperature);
        diagnostics = fold(diagnostics, entry.exospheric);
        diagnostics = fold(diagnostics, entry.altitude_m);
        diagnostics = fold(diagnostics, entry.attitude_angle);
        diagnostics = fold(diagnostics, entry.angular_speed);
        diagnostics = fold(diagnostics, entry.total_torque);
        for (const double value : entry.sun_m) diagnostics = fold(diagnostics, value);
        for (const double value : entry.drag_force_n) diagnostics = fold(diagnostics, value);
        for (const double value : entry.drag_torque_nm) diagnostics = fold(diagnostics, value);
        for (const double value : entry.srp_force_n) diagnostics = fold(diagnostics, value);
        for (const double value : entry.total_force_n) diagnostics = fold(diagnostics, value);
        for (const double value : entry.number_density) diagnostics = fold(diagnostics, value);
    }
    record.diagnostics_checksum = diagnostics;
}

void compare_records(const Record& expected, const Record& actual, std::size_t index, MismatchSink& sink) {
    const std::string prefix = "trajectory " + std::to_string(index);
    note_unsigned(static_cast<long long>(expected.rhs_calls), static_cast<long long>(actual.rhs_calls), prefix,
                  "rhs_calls", sink);
    note_unsigned(static_cast<long long>(expected.atmosphere_calls),
                  static_cast<long long>(actual.atmosphere_calls), prefix, "atmosphere_calls", sink);
    note_double(expected.trajectory.epoch.et_seconds, actual.trajectory.epoch.et_seconds, prefix,
                "epoch.et_seconds", sink);
    if (expected.trajectory.samples.size() != actual.trajectory.samples.size()) {
        sink.add(prefix + ": output sample count differs, reference " +
                 std::to_string(expected.trajectory.samples.size()) + " batch " +
                 std::to_string(actual.trajectory.samples.size()));
        return;
    }
    for (std::size_t sample = 0; sample < expected.trajectory.samples.size(); ++sample) {
        const std::string where = prefix + " sample " + std::to_string(sample);
        const auto& reference_sample = expected.trajectory.samples[sample];
        const auto& batch_sample = actual.trajectory.samples[sample];
        note_double(reference_sample.t.seconds, batch_sample.t.seconds, where, "output label", sink);
        const Packed reference_state = leo::detail::pack(reference_sample.state);
        const Packed batch_state = leo::detail::pack(batch_sample.state);
        static const char* const components[13] = {"r.x", "r.y", "r.z", "v.x", "v.y", "v.z",
                                                   "q.w", "q.x", "q.y", "q.z", "omega.x", "omega.y", "omega.z"};
        for (std::size_t component = 0; component < 13; ++component)
            note_double(reference_state[component], batch_state[component], where, components[component], sink);
    }
    if (expected.trace.size() != actual.trace.size()) {
        sink.add(prefix + ": RHS stage trace length differs, reference " + std::to_string(expected.trace.size()) +
                 " batch " + std::to_string(actual.trace.size()));
    } else {
        for (std::size_t row = 0; row < expected.trace.size(); ++row)
            note_trace(expected.trace[row], actual.trace[row], prefix + " RHS stage " + std::to_string(row), sink);
    }
    if (expected.inputs.size() != actual.inputs.size()) {
        sink.add(prefix + ": native input trace length differs, reference " + std::to_string(expected.inputs.size()) +
                 " batch " + std::to_string(actual.inputs.size()));
    } else {
        for (std::size_t row = 0; row < expected.inputs.size(); ++row)
            note_inputs(expected.inputs[row], actual.inputs[row], prefix + " native input " + std::to_string(row), sink);
    }
    if (expected.diagnostics.size() != actual.diagnostics.size()) {
        sink.add(prefix + ": output diagnostic length differs, reference " +
                 std::to_string(expected.diagnostics.size()) + " batch " + std::to_string(actual.diagnostics.size()));
        return;
    }
    for (std::size_t index_sample = 0; index_sample < expected.diagnostics.size(); ++index_sample)
        note_diagnostics(expected.diagnostics[index_sample], actual.diagnostics[index_sample],
                         prefix + " diagnostics " + std::to_string(index_sample), sink);
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

struct RunOutcome {
    std::vector<Record> records;
    double wall_seconds = 0;
    std::string error;
    bool failed = false;
};

struct Group {
    std::string name;
    std::vector<State> initials;
    std::vector<std::size_t> workers;
    std::size_t batch = 0;
    bool ap_history = false;
    bool core_traces = true;
    bool input_traces = true;
};

RunOutcome run_sequential(const Group& group, const WorkloadSpec& work, const experiment::Case& spec,
                          const Environment& env, const Config& config, leo::Epoch epoch,
                          const std::vector<double>& labels, const IntegrationOptions& options) {
    RunOutcome outcome;
    outcome.records.resize(group.initials.size());
    const auto timer = Clock::now();
    try {
        for (std::size_t index = 0; index < group.initials.size(); ++index) {
            Record& record = outcome.records[index];
            record.core_traces = group.core_traces;
            record.input_traces = group.input_traces;
            const TrajectoryRhs rhs(work, spec, env, config, epoch, &record);
            record.trajectory = propagate(rhs, group.initials[index], epoch,
                                          std::span<const double>(labels), options);
        }
    } catch (const std::exception& failure) {
        outcome.failed = true;
        outcome.error = failure.what();
    }
    outcome.wall_seconds = elapsed(timer);
    if (outcome.failed) return outcome;
    for (std::size_t index = 0; index < outcome.records.size(); ++index) {
        collect_diagnostics(outcome.records[index], work, spec, env, config, epoch);
        finalize_checksums(outcome.records[index]);
    }
    return outcome;
}

RunOutcome run_concurrent(const Group& group, const WorkloadSpec& work, const experiment::Case& spec,
                          const Environment& env, const Config& config, leo::Epoch epoch,
                          const std::vector<double>& labels, const IntegrationOptions& options,
                          std::size_t workers, BatchScheduling scheduling) {
    RunOutcome outcome;
    outcome.records.resize(group.initials.size());
    for (auto& record : outcome.records) {
        record.core_traces = group.core_traces;
        record.input_traces = group.input_traces;
    }
    // The factory is concurrent-call-safe: it only stores into the record slot
    // that belongs to the trajectory it is building, and it borrows the single
    // shared provider and the immutable weather dataset.
    const auto factory = [&](std::size_t index, const State&) {
        return TrajectoryRhs(work, spec, env, config, epoch, &outcome.records[index]);
    };
    const auto timer = Clock::now();
    try {
        std::vector<leo::Trajectory> trajectories =
            propagate_batch(factory, std::span<const State>(group.initials), epoch,
                            std::span<const double>(labels), options, BatchOptions{workers, scheduling});
        for (std::size_t index = 0; index < trajectories.size() && index < outcome.records.size(); ++index)
            outcome.records[index].trajectory = std::move(trajectories[index]);
    } catch (const leo::BatchPropagationError& failure) {
        outcome.failed = true;
        outcome.error = "BatchPropagationError at trajectory " + std::to_string(failure.trajectory_index()) +
                        ": " + failure.what();
        try {
            std::rethrow_exception(failure.cause());
        } catch (const std::exception& cause) {
            outcome.error += " | cause: " + std::string(cause.what());
        } catch (...) {
            outcome.error += " | cause: non-standard exception";
        }
        return outcome;
    } catch (const std::exception& failure) {
        outcome.failed = true;
        outcome.error = failure.what();
        return outcome;
    }
    outcome.wall_seconds = elapsed(timer);
    for (std::size_t index = 0; index < outcome.records.size(); ++index) {
        collect_diagnostics(outcome.records[index], work, spec, env, config, epoch);
        finalize_checksums(outcome.records[index]);
    }
    return outcome;
}

// Final output state of one trajectory, published so a reader can check the
// evidence without rerunning anything.
std::string final_state_json(const Record& record) {
    if (record.trajectory.samples.empty()) return "null";
    const auto& sample = record.trajectory.samples.back();
    std::ostringstream out;
    out << '[' << sample.t.seconds;
    const Packed packed = leo::detail::pack(sample.state);
    for (const double value : packed) out << ',' << value;
    return out.str() + ']';
}

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

struct Comparison {
    std::string phase, case_id, case_role, case_file, workload, atmosphere_model;
    std::size_t batch = 0, requested_workers = 0, effective_workers = 0, repeat = 0;
    bool ap_history = false;
    std::string scheduling, status, error;
    std::size_t mismatches = 0;
    std::vector<std::string> details;
    double batch_wall_seconds = 0, sequential_wall_seconds = 0;
    std::uint64_t trajectory_checksum = 0, trace_checksum = 0, diagnostics_checksum = 0;
    std::uint64_t repeat_reference_checksum = 0;
    bool repeat_identical = true;
    std::size_t rhs_calls = 0, atmosphere_calls = 0, rhs_stage_rows = 0;
    bool traces_compared = false, input_traces_compared = false;
    std::string final_state = "null";
};

std::uint64_t combined_checksum(const std::vector<Record>& records, std::uint64_t (*field)(const Record&)) {
    std::uint64_t accumulator = 14695981039346656037ull;
    for (const auto& record : records) accumulator = fold(accumulator, field(record));
    return accumulator;
}

std::uint64_t trajectory_checksum_of(const Record& record) { return record.trajectory_checksum; }
std::uint64_t trace_checksum_of(const Record& record) { return record.trace_checksum; }
std::uint64_t diagnostics_checksum_of(const Record& record) { return record.diagnostics_checksum; }

std::vector<State> repeated_start(const experiment::Case& spec, std::size_t count) {
    return std::vector<State>(count, spec.start());
}

// One batch definition. Every group shares the frozen scientific configuration
// and differs only in batch size, worker counts, initial-state determinism and
// the ap-history switch, so a failure can be attributed to exactly one of them.
std::vector<Group> build_groups(const std::vector<State>& base_initials, const ValidationOptions& validation,
                                bool ap_history, const WorkloadSpec& work) {
    if (validation.oversized_batch > validation.batch)
        throw std::invalid_argument("--validation-oversized-batch cannot exceed --validation-batch");
    const bool traces = validation.core_traces, inputs = validation.input_traces;
    std::vector<Group> groups;
    groups.push_back({"identical_initial_states", base_initials, validation.workers, validation.batch,
                      ap_history, traces, inputs});
    if (validation.perturbed)
        groups.push_back({"perturbed_initial_states", perturbed_initials(base_initials, validation),
                          validation.perturbed_workers, validation.batch, ap_history, traces, inputs});
    if (validation.oversized)
        groups.push_back({"more_workers_than_trajectories",
                          std::vector<State>(base_initials.begin(),
                                             base_initials.begin() +
                                                 static_cast<std::ptrdiff_t>(validation.oversized_batch)),
                          validation.oversized_workers, validation.oversized_batch, ap_history, traces, inputs});
    const bool wide_selected = std::find(validation.wide_workloads.begin(), validation.wide_workloads.end(), work.name) !=
                                   validation.wide_workloads.end() ||
                               std::find(validation.wide_workloads.begin(), validation.wide_workloads.end(), "*") !=
                                   validation.wide_workloads.end();
    if (validation.wide && wide_selected)
        groups.push_back({"every_host_worker", std::vector<State>(validation.wide_batch, base_initials.front()),
                          {validation.wide_batch}, validation.wide_batch, ap_history, traces, inputs});
    if (validation.ap_history_phase && !ap_history)
        groups.push_back({"ap_history_enabled", base_initials, validation.ap_history_workers, validation.batch,
                          true, traces, inputs});
    return groups;
}

void write_document(const std::filesystem::path& path, const Config& config, const ValidationOptions& validation,
                    const Environment& env, leo::Epoch epoch, const std::vector<double>& labels,
                    const IntegrationOptions& options, const std::vector<experiment::Case>& specs,
                    const std::vector<WorkloadSpec>& workloads, const std::vector<Comparison>& comparisons,
                    std::size_t passed, std::size_t failed, std::size_t mismatches) {
    JsonWriter document;
    document.text("kind", "multithreading-correctness");
    document.text("tool", "mt_validation");
    document.text("comparison", "byte-level binary64 comparison of complete trajectories, per-RHS-stage "
                                 "traces, native atmospheric inputs and output-sample diagnostics against "
                                 "single-threaded propagate(); no tolerance is applied anywhere");
    document.text("epoch_utc", config.epoch_utc);
    document.number("epoch_et_seconds", epoch.et_seconds);
    document.number("duration_s", config.duration);
    document.number("output_cadence_s", config.cadence);
    document.unsigned_field("output_sample_count", labels.size());
    document.number("rtol", options.rtol);
    document.number("max_step_s", options.max_step_s);
    document.number("initial_step_s", options.initial_step_s);
    document.number("position_atol_m", options.position_atol_m);
    document.number("velocity_atol_m_s", options.velocity_atol_m_s);
    document.number("quaternion_atol", options.quaternion_atol);
    document.number("omega_atol_rad_s", options.omega_atol_rad_s);
    document.unsigned_field("max_attempts", options.max_attempts);
    document.integer("hardware_concurrency", static_cast<long long>(std::thread::hardware_concurrency()));
    document.text("spice_toolkit", leo::SpiceEphemerisProvider::toolkit_version());
    document.integer("spice_provider_instances", 1);
    document.integer("spice_provider_identity",
                     static_cast<long long>(reinterpret_cast<std::uintptr_t>(&env.ephemeris)));
    document.text("spice_provider_lifetime",
                  "one Environment constructed before any batch and destroyed after the last one; every "
                  "per-trajectory DirectAtmosphere borrows it by reference");
    document.flag("utc_reuse", config.options.utc_reuse);
    document.flag("sun_sharing", config.options.sun_sharing);
    document.flag("orientation_reuse", config.options.orientation_reuse);
    {
        const auto switches = ap_history_switches();
        std::ostringstream values;
        values << '[';
        for (std::size_t index = 0; index < switches.size(); ++index) {
            if (index) values << ',';
            values << switches[index];
        }
        values << ']';
        document.raw("nrlmsise_standard_switches", values.str());
    }
    {
        std::vector<std::string> kernels;
        for (const auto& kernel : env.kernels) kernels.push_back(kernel.string() + " " + sha256_file(kernel));
        document.raw("kernel_sha256", json_strings(kernels));
        std::vector<std::string> weather;
        for (const auto& file : {config.weather, config.solfsmy, config.dtcfile})
            weather.push_back(file + " " + sha256_file(file));
        document.raw("weather_sha256", json_strings(weather));
        std::vector<std::string> case_hashes;
        for (const auto& spec : specs) case_hashes.push_back(spec.file + " " + sha256_file(spec.file));
        document.raw("case_sha256", json_strings(case_hashes));
    }
    {
        const auto statistics = leo::SpiceEphemerisProvider::reuse_statistics();
        JsonWriter reuse;
        reuse.unsigned_field("utc_hits", statistics.utc_hits);
        reuse.unsigned_field("utc_misses", statistics.utc_misses);
        reuse.unsigned_field("utc_evictions", statistics.utc_evictions);
        reuse.unsigned_field("orientation_hits", statistics.orientation_hits);
        reuse.unsigned_field("orientation_misses", statistics.orientation_misses);
        reuse.unsigned_field("orientation_evictions", statistics.orientation_evictions);
        reuse.text("note", "counters are populated only in LEO_SPICE_REUSE_DIAGNOSTICS builds");
        document.raw("spice_reuse_statistics", reuse.str());
    }
    {
        JsonWriter settings;
        settings.unsigned_field("batch_size", validation.batch);
        settings.unsigned_field("repeats", validation.repeats);
        settings.raw("workers", json_sizes(validation.workers));
        settings.raw("perturbed_workers", json_sizes(validation.perturbed_workers));
        settings.raw("oversized_workers", json_sizes(validation.oversized_workers));
        settings.raw("ap_history_workers", json_sizes(validation.ap_history_workers));
        settings.unsigned_field("oversized_batch_size", validation.oversized_batch);
        settings.unsigned_field("wide_batch_size", validation.wide_batch);
        settings.flag("perturbed_group", validation.perturbed);
        settings.flag("oversized_group", validation.oversized);
        settings.flag("wide_group", validation.wide);
        settings.raw("wide_group_workloads", json_strings(validation.wide_workloads));
        settings.flag("ap_history_group", validation.ap_history_phase && !config.ap_history);
        settings.flag("rhs_stage_traces", validation.core_traces);
        settings.flag("native_input_traces", validation.core_traces && validation.input_traces);
        {
            std::ostringstream seed_text;
            seed_text << "0x" << std::hex << validation.seed;
            settings.text("perturbation_seed", seed_text.str());
        }
        settings.number("position_perturbation_half_width_m", validation.position_sigma_m);
        settings.number("velocity_perturbation_half_width_m_s", validation.velocity_sigma_m_s);
        settings.number("body_rate_perturbation_half_width_rad_s", validation.omega_sigma_rad_s);
        settings.text("perturbation_generator", "splitmix64, uniform in [-sigma, sigma], stream seeded with "
                                                 "seed + 0x9E3779B97F4A7C15 * (trajectory index + 1)");
        settings.unsigned_field("mismatch_limit_per_comparison", validation.mismatch_limit);
        document.raw("validation_settings", settings.str());
    }
    {
        std::vector<std::string> entries;
        for (const auto& spec : specs) {
            JsonWriter entry;
            entry.text("id", spec.id);
            entry.text("file", spec.file);
            entry.flag("legacy_benchmark_factory", spec.legacy);
            entry.text("role", spec.id == "CASE-C" ? "numerical_stress" : "primary_representative");
            entries.push_back(entry.str());
        }
        document.raw("cases", json_array_of(entries));
        std::vector<std::string> names, absent;
        for (const auto& work : workloads) names.push_back(work.name);
        for (const auto& model : absent_models()) absent.push_back(model_name(model));
        document.raw("workloads", json_strings(names));
        document.raw("atmosphere_models_not_compiled_in", json_strings(absent));
    }
    {
        std::vector<std::string> entries;
        for (const auto& comparison : comparisons) {
            JsonWriter entry;
            entry.text("phase", comparison.phase);
            entry.text("case", comparison.case_id);
            entry.text("case_role", comparison.case_role);
            entry.text("case_file", comparison.case_file);
            entry.text("workload", comparison.workload);
            entry.text("atmosphere_model", comparison.atmosphere_model);
            entry.flag("ap_history", comparison.ap_history);
            entry.unsigned_field("batch_size", comparison.batch);
            entry.unsigned_field("requested_workers", comparison.requested_workers);
            entry.unsigned_field("effective_workers", comparison.effective_workers);
            entry.text("scheduling", comparison.scheduling);
            entry.unsigned_field("repeat", comparison.repeat);
            entry.text("status", comparison.status);
            entry.unsigned_field("mismatches", comparison.mismatches);
            entry.raw("mismatch_details", json_strings(comparison.details));
            if (!comparison.error.empty()) entry.text("error", comparison.error);
            entry.flag("repeat_identical_to_first_run", comparison.repeat_identical);
            entry.number("batch_wall_seconds", comparison.batch_wall_seconds);
            entry.number("sequential_wall_seconds", comparison.sequential_wall_seconds);
            entry.unsigned_field("rhs_calls", comparison.rhs_calls);
            entry.unsigned_field("atmosphere_calls", comparison.atmosphere_calls);
            entry.unsigned_field("rhs_stage_rows_compared", comparison.rhs_stage_rows);
            entry.flag("rhs_stage_traces_compared", comparison.traces_compared);
            entry.flag("native_input_traces_compared", comparison.input_traces_compared);
            entry.text("trajectory_checksum", std::to_string(comparison.trajectory_checksum));
            entry.text("sequential_trajectory_checksum", std::to_string(comparison.repeat_reference_checksum));
            entry.text("rhs_trace_checksum", std::to_string(comparison.trace_checksum));
            entry.text("output_diagnostics_checksum", std::to_string(comparison.diagnostics_checksum));
            entry.raw("final_state", comparison.final_state);
            entries.push_back(entry.str());
        }
        document.raw("comparisons", json_array_of(entries));
    }
    {
        JsonWriter summary;
        summary.unsigned_field("comparisons", passed + failed);
        summary.unsigned_field("passed", passed);
        summary.unsigned_field("failed", failed);
        summary.unsigned_field("mismatching_values", mismatches);
        summary.flag("bitwise_equal_everywhere", failed == 0);
        summary.text("final_state_format", "trajectory elapsed_s, then r.x r.y r.z v.x v.y v.z q.w q.x q.y q.z "
                                           "omega.x omega.y omega.z at the last requested output");
        document.raw("summary", summary.str());
    }
    save(path, document.str());
}

int run(int argc, char** argv) {
    ValidationOptions validation;
    const std::vector<std::string> forwarded = split_arguments(argc, argv, validation);
    std::vector<std::string> storage = forwarded;
    std::vector<char*> arguments;
    arguments.reserve(storage.size() + 1);
    for (auto& item : storage) arguments.push_back(item.data());
    arguments.push_back(nullptr);
    const Config config = parse_arguments(static_cast<int>(storage.size()), arguments.data());
    if (validation.json_path.empty()) validation.json_path = (std::filesystem::path(config.out) / "correctness.json").string();

    const std::filesystem::path json_path(validation.json_path);
    const std::filesystem::path directory = json_path.has_parent_path() ? json_path.parent_path()
                                                                        : std::filesystem::path(".");
    prepare_output_directory(directory);

    // One stable, explicitly managed provider and one immutable weather dataset
    // for the whole process: every trajectory borrows these two objects.
    Environment env(config);
    const leo::Epoch epoch = leo::SpiceEphemerisProvider::epoch_from_utc(config.epoch_utc);
    const std::vector<double> labels = sample_labels(config);

    IntegrationOptions options = tolerances();
    options.rtol = config.rtol;
    options.max_step_s = config.max_step;
    options.initial_step_s = std::min(1.0, config.max_step);
    options.max_attempts = IntegrationOptions{}.max_attempts;

    const auto workloads = validation_workloads();
    std::vector<experiment::Case> specs;
    for (const auto& file : config.case_files) specs.push_back(experiment::load(file));

    std::vector<Comparison> comparisons;
    std::size_t passed = 0, failed = 0, mismatches = 0;

    for (const auto& spec : specs) {
        const std::string role = spec.id == "CASE-C" ? "numerical_stress" : "primary_representative";
        const std::vector<State> base_initials = repeated_start(spec, validation.batch);
        for (const auto& work : workloads) {
            const auto wall_start = Clock::now();
            const auto groups = build_groups(base_initials, validation, config.ap_history, work);
            for (const auto& group : groups) {
                Config group_config = config;
                group_config.ap_history = group.ap_history;
                const auto sequential = run_sequential(group, work, spec, env, group_config, epoch, labels, options);
                if (sequential.failed) {
                    Comparison comparison;
                    comparison.phase = group.name;
                    comparison.case_id = spec.id;
                    comparison.case_role = role;
                    comparison.case_file = spec.file;
                    comparison.workload = work.name;
                    comparison.status = "error";
                    comparison.error = "sequential reference failed: " + sequential.error;
                    ++failed;
                    comparisons.push_back(comparison);
                    std::cerr << "ERROR " << group.name << ' ' << spec.id << ' ' << work.name
                              << ": sequential reference failed: " << sequential.error << '\n';
                    continue;
                }
                for (const std::size_t workers : group.workers) {
                    for (const BatchScheduling scheduling : all_schedulings()) {
                        std::uint64_t first_run_checksum = 0;
                        for (std::size_t repeat = 0; repeat < validation.repeats; ++repeat) {
                            const auto batch = run_concurrent(group, work, spec, env, group_config, epoch, labels,
                                                             options, workers, scheduling);
                            Comparison comparison;
                            comparison.phase = group.name;
                            comparison.case_id = spec.id;
                            comparison.case_role = role;
                            comparison.case_file = spec.file;
                            comparison.workload = work.name;
                            comparison.atmosphere_model = work.atmospheric ? model_name(work.model) : "none";
                            comparison.batch = group.batch;
                            comparison.requested_workers = workers;
                            comparison.effective_workers = std::min(workers, group.batch);
                            comparison.repeat = repeat;
                            comparison.ap_history = group.ap_history;
                            comparison.scheduling = scheduling_name(scheduling);
                            comparison.batch_wall_seconds = batch.wall_seconds;
                            comparison.sequential_wall_seconds = sequential.wall_seconds;
                            comparison.traces_compared = group.core_traces;
                            comparison.input_traces_compared = group.core_traces && group.input_traces;
                            if (batch.failed) {
                                comparison.status = "error";
                                comparison.error = batch.error;
                                ++failed;
                                std::cerr << "ERROR " << group.name << ' ' << spec.id << ' ' << work.name
                                          << " workers=" << workers << " scheduling=" << comparison.scheduling
                                          << ": " << batch.error << '\n';
                                comparisons.push_back(comparison);
                                continue;
                            }
                            MismatchSink sink;
                            sink.limit = validation.mismatch_limit;
                            for (std::size_t index = 0; index < sequential.records.size(); ++index)
                                compare_records(sequential.records[index], batch.records[index], index, sink);
                            const std::uint64_t trajectory_checksum =
                                combined_checksum(batch.records, &trajectory_checksum_of);
                            const std::uint64_t trace_checksum = combined_checksum(batch.records, &trace_checksum_of);
                            const std::uint64_t diagnostics_checksum =
                                combined_checksum(batch.records, &diagnostics_checksum_of);
                            if (repeat == 0) {
                                first_run_checksum = trajectory_checksum;
                            } else if (trajectory_checksum != first_run_checksum) {
                                comparison.repeat_identical = false;
                                sink.add("repeated identical batch differs from its first run: trajectory checksum " +
                                         std::to_string(first_run_checksum) + " vs " +
                                         std::to_string(trajectory_checksum));
                            }
                            comparison.trajectory_checksum = trajectory_checksum;
                            comparison.trace_checksum = trace_checksum;
                            comparison.diagnostics_checksum = diagnostics_checksum;
                            comparison.repeat_reference_checksum =
                                combined_checksum(sequential.records, &trajectory_checksum_of);
                            for (const auto& record : batch.records) {
                                comparison.rhs_calls += record.rhs_calls;
                                comparison.atmosphere_calls += record.atmosphere_calls;
                                comparison.rhs_stage_rows += record.trace.size();
                            }
                            comparison.mismatches = sink.total;
                            comparison.details = sink.details;
                            comparison.final_state = final_state_json(batch.records.front());
                            comparison.status = sink.empty() ? "pass" : "fail";
                            if (sink.empty()) {
                                ++passed;
                                std::cout << "pass " << group.name << ' ' << spec.id << ' ' << work.name
                                          << " workers=" << workers << " effective=" << comparison.effective_workers
                                          << " scheduling=" << comparison.scheduling << " repeat=" << repeat
                                          << " batch=" << group.batch << " wall=" << batch.wall_seconds
                                          << "s sequential=" << sequential.wall_seconds << "s rhs="
                                          << comparison.rhs_calls << '\n';
                            } else {
                                ++failed;
                                mismatches += sink.total;
                                std::cerr << "FAIL " << group.name << ' ' << spec.id << ' ' << work.name
                                          << " workers=" << workers << " scheduling=" << comparison.scheduling
                                          << " repeat=" << repeat << ": " << sink.total
                                          << " mismatching values\n";
                                for (const auto& detail : sink.details) std::cerr << "  " << detail << '\n';
                                if (sink.total > sink.details.size())
                                    std::cerr << "  ... " << (sink.total - sink.details.size())
                                              << " further mismatches suppressed by --validation-mismatch-limit\n";
                            }
                            comparisons.push_back(comparison);
                        }
                    }
                }
            }
            std::cout << "workload " << work.name << ' ' << spec.id << " complete in " << elapsed(wall_start)
                      << "s\n";
        }
    }

    write_document(json_path, config, validation, env, epoch, labels, options, specs, workloads, comparisons,
                   passed, failed, mismatches);
    std::cout << "multithreading correctness: " << (passed + failed) << " comparisons, " << passed
              << " passed, " << failed << " failed, " << mismatches << " mismatching values\n";
    std::cout << "wrote " << json_path.string() << '\n';
    return failed == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "mt_validation: " << error.what() << '\n';
        return 2;
    }
}