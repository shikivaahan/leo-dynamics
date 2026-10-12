// MT-002 concrete execution driver; not a second library batch API.
// Reuse the existing benchmark's CASE configuration, BmRhs, Environment,
// perturbation generator and unchanged propagate() numerical path.
// Each independently launched executable owns its SPICE/native state and weather.
// --isolation-total/seed define global ordered inputs; begin/count select a subset.
// The binary format contains complete epoch, schedule and trajectory binary64
// values, with no object padding. Failures carry the global trajectory index.
// Results are staged and atomically published; the Python parent owns child
// cleanup and end-to-end timing, including startup, loading, writing and merging.
#define MT_BENCHMARK_NO_MAIN
#include "benchmark.cpp"
#include <limits>

namespace {

constexpr char iso_magic[8] = {'M', 'T', 'I', 'S', 'O', 'B', '0', '1'};
// r(3) v(3) q(4) omega(3); scalar-first quaternion, written as w,x,y,z.
constexpr std::size_t iso_state_doubles = 13;

struct IsolationOptions {
    std::string out_path;              // --isolation-out
    std::string meta_path;             // --isolation-meta (optional)
    std::size_t total = 0;             // --isolation-total
    std::size_t begin = 0;             // --isolation-begin
    std::size_t count = 0;             // --isolation-count
    std::uint64_t seed = 20260101ull;  // --isolation-seed
    std::string model;                 // --isolation-model
    std::string executor = "sequential"; // --isolation-executor
    std::size_t workers = 1;           // --isolation-workers
};

void iso_print_help() {
    std::cout <<
        "mt_isolation options (every other argument is the atmosphere experiment's):\n"
        "  --isolation-out PATH              binary trajectory result file (required)\n"
        "  --isolation-meta PATH             optional JSON envelope/counter sidecar\n"
        "  --isolation-model NAME            two_body_j2, exponential, nrlmsise00, jb2008\n"
        "                                     (required; forwarded unchanged to the reused workload set)\n"
        "  --isolation-total N               global input size, identical in every execution (required)\n"
        "  --isolation-begin N               global index of the first trajectory (default 0)\n"
        "  --isolation-count N               trajectories to propagate (default total - begin)\n"
        "  --isolation-seed N                master perturbation seed (default 20260101)\n"
        "  --isolation-executor MODE         sequential | threads (default sequential)\n"
        "  --isolation-workers N              thread workers for the threads executor (default 1)\n"
        "  --isolation-help                  this text\n"
        "\n"
        "Sample schedule: the experiment's --duration and --cadence, unchanged.\n"
        "Binary format, little-endian throughout, no object padding and no host type\n"
        "names in it:\n"
        "  0   8  magic \"MTISOB01\"\n"
        "  8   8  binary64 epoch_et_seconds\n"
        " 16   8  uint64 global_total\n"
        " 24   8  uint64 global_begin\n"
        " 32   8  uint64 trajectory_count\n"
        " 40   8  uint64 samples_per_trajectory\n"
        " 48   8  uint64 state_doubles (always 13)\n"
        " 56  8s binary64 sample labels, ascending, seconds from the epoch\n"
        "      then count*s*13 binary64: r.x r.y r.z v.x v.y v.z q.w q.x q.y q.z\n"
        "                                  omega.x omega.y omega.z\n"
        "Trajectories and samples appear in ascending global order, so a process split\n"
        "merges by concatenating payloads in ascending --isolation-begin.\n";
}

std::size_t iso_parse_count(const std::string& text, const char* key) {
    std::size_t used = 0;
    if (text.empty() || text.front() == '-')
        throw std::invalid_argument(std::string("invalid value for ") + key + ": " + text);
    const unsigned long long value = std::stoull(text, &used);
    if (used != text.size() || value > std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument(std::string("invalid value for ") + key + ": " + text);
    return static_cast<std::size_t>(value);
}

// Every --isolation-* option is consumed here; everything else is forwarded
// verbatim to the unchanged experiment argument parser, with the same
// value-forwarding rule MT-001 used.
std::vector<std::string> iso_split_arguments(int argc, char** argv, IsolationOptions& iso) {
    std::vector<std::string> forwarded;
    forwarded.emplace_back(argv[0]);
    bool have_mode = false, have_out = false, have_total = false;
    const auto value_of = [&](int& index, const std::string& key) -> std::string {
        if (index + 1 >= argc) throw std::invalid_argument("missing value for " + key);
        return argv[++index];
    };
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        if (key == "--isolation-out") { iso.out_path = value_of(index, key); continue; }
        if (key == "--isolation-meta") { iso.meta_path = value_of(index, key); continue; }
        if (key == "--isolation-model") { iso.model = value_of(index, key); continue; }
        if (key == "--isolation-executor") {
            iso.executor = value_of(index, key);
            if (iso.executor != "sequential" && iso.executor != "threads")
                throw std::invalid_argument("--isolation-executor expects sequential or threads, got: " + iso.executor);
            continue;
        }
        if (key == "--isolation-total") {
            iso.total = iso_parse_count(value_of(index, key), key.c_str());
            have_total = true;
            continue;
        }
        if (key == "--isolation-begin") { iso.begin = iso_parse_count(value_of(index, key), key.c_str()); continue; }
        if (key == "--isolation-count") { iso.count = iso_parse_count(value_of(index, key), key.c_str()); continue; }
        if (key == "--isolation-workers") { iso.workers = iso_parse_count(value_of(index, key), key.c_str()); continue; }
        if (key == "--isolation-seed") { iso.seed = std::stoull(value_of(index, key), nullptr, 0); continue; }
        if (key == "--isolation-help") { iso_print_help(); std::exit(0); }
        if (key == "--mode") have_mode = true;
        if (key == "--out") have_out = true;
        forwarded.push_back(key);
        // The experiment parser's only valueless flag.
        if (key != "--ap-history" && index + 1 < argc &&
            std::string(argv[index + 1]).rfind("--", 0) != 0)
            forwarded.push_back(argv[++index]);
    }
    if (iso.out_path.empty()) throw std::invalid_argument("--isolation-out is required");
    if (iso.model.empty()) throw std::invalid_argument("--isolation-model is required");
    if (!have_total) throw std::invalid_argument("--isolation-total is required (zero means an empty batch)");
    if (iso.workers == 0) throw std::invalid_argument("--isolation-workers must be at least one");
    if (iso.begin > iso.total || (iso.begin == iso.total && iso.total != 0))
        throw std::invalid_argument("--isolation-begin " + std::to_string(iso.begin) +
                                    " is outside the global input of " + std::to_string(iso.total));
    if (iso.count == 0) iso.count = iso.total - iso.begin;
    if (iso.count > iso.total - iso.begin)
        throw std::invalid_argument("--isolation-begin " + std::to_string(iso.begin) + " plus --isolation-count " +
                                    std::to_string(iso.count) + " exceeds --isolation-total " +
                                    std::to_string(iso.total));
    if (!have_mode) { forwarded.push_back("--mode"); forwarded.push_back("propagate"); }
    if (!have_out) {
        // The experiment parser requires --out; this driver never writes through it,
        // so the result file's own directory is supplied only to satisfy that
        // contract. A bare file name has an empty parent, which the parser rejects.
        std::filesystem::path directory = std::filesystem::path(iso.out_path).parent_path();
        forwarded.push_back("--out");
        forwarded.push_back(directory.empty() ? std::string(".") : directory.string());
    }
    return forwarded;
}

// Explicit little-endian encoding: the file format is defined by this driver, not
// by the host's struct layout, byte order or floating-point type name.
void iso_put_u64(std::ofstream& out, std::uint64_t value) {
    unsigned char bytes[8];
    for (int index = 0; index < 8; ++index)
        bytes[index] = static_cast<unsigned char>((value >> (8 * index)) & 0xFFull);
    out.write(reinterpret_cast<const char*>(bytes), 8);
}

void iso_put_f64(std::ofstream& out, double value) {
    static_assert(sizeof(double) == sizeof(std::uint64_t), "binary64 double required");
    std::uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof raw);
    iso_put_u64(out, raw);
}

void iso_put_state(std::ofstream& out, const State& state) {
    iso_put_f64(out, state.r.x());
    iso_put_f64(out, state.r.y());
    iso_put_f64(out, state.r.z());
    iso_put_f64(out, state.v.x());
    iso_put_f64(out, state.v.y());
    iso_put_f64(out, state.v.z());
    iso_put_f64(out, state.q.w);
    iso_put_f64(out, state.q.x);
    iso_put_f64(out, state.q.y);
    iso_put_f64(out, state.q.z);
    iso_put_f64(out, state.omega.x());
    iso_put_f64(out, state.omega.y());
    iso_put_f64(out, state.omega.z());
}

// Written to a sibling staging path and published by rename, so the destination
// never exists with partial content.
void iso_write_result(const IsolationOptions& iso, leo::Epoch epoch, const std::vector<double>& labels,
                      const std::vector<leo::Trajectory>& trajectories) {
    const std::filesystem::path destination(iso.out_path);
    if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
    std::filesystem::path staging = destination;
    staging += ".partial";
    try {
        std::ofstream out(staging, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + staging.string());
        out.write(iso_magic, sizeof iso_magic);
        iso_put_f64(out, epoch.et_seconds);
        iso_put_u64(out, iso.total);
        iso_put_u64(out, iso.begin);
        iso_put_u64(out, trajectories.size());
        iso_put_u64(out, labels.size());
        iso_put_u64(out, iso_state_doubles);
        for (const double label : labels) iso_put_f64(out, label);
        for (const leo::Trajectory& trajectory : trajectories) {
            if (trajectory.samples.size() != labels.size())
                throw std::runtime_error("trajectory has a different sample count than the schedule");
            for (const leo::Sample& sample : trajectory.samples) iso_put_state(out, sample.state);
        }
        out.close();
        if (!out) throw std::runtime_error("write failed for " + staging.string());
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(staging, ignored);
        throw;
    }
    std::error_code code;
    std::filesystem::rename(staging, destination, code);
    if (code) {
        std::filesystem::remove(staging);
        throw std::runtime_error("cannot publish " + destination.string() + ": " + code.message());
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        IsolationOptions iso;
        const std::vector<std::string> forwarded = iso_split_arguments(argc, argv, iso);
        std::vector<char*> storage;
        for (const std::string& argument : forwarded) storage.push_back(const_cast<char*>(argument.c_str()));
        storage.push_back(nullptr);
        const Config config = parse_arguments(static_cast<int>(storage.size() - 1), storage.data());
        if (config.case_files.size() != 1)
            throw std::invalid_argument("mt_isolation needs exactly one --case, got " +
                                        std::to_string(config.case_files.size()));
        if (config.cadence >= config.duration)
            throw std::invalid_argument("--cadence must be smaller than --duration");

        // One model per run, resolved through the reused workload set so a name
        // this build cannot run is rejected instead of silently substituted.
        BenchmarkOptions selection;
        selection.models = {iso.model};
        const std::vector<Workload> workloads = bm_resolve_workloads(selection);
        const Workload& work = workloads.front();
        if (work.atmospheric && !compiled_in(work.model))
            throw std::runtime_error("model " + work.name + " needs a backend this build does not contain");

        // Per-process isolation: this executable builds every kernel, weather
        // record and reference library it will ever use, with no inherited state.
        Environment env(config);
        const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(config.epoch_utc);
        const std::vector<double> labels = bm_labels(config.duration, config.cadence);
        IntegrationOptions options = tolerances();
        options.rtol = config.rtol;
        options.max_step_s = config.max_step;
        options.initial_step_s = std::min(1.0, config.max_step);
        const experiment::Case spec = experiment::load(config.case_files.front());

        // The complete global input list, then this execution's contiguous subset.
        // Perturbation is a function of the seed and the global index alone, so the
        // subset is identical however the global list is split across processes.
        const std::vector<State> global =
            bm_perturb_states(std::vector<State>(iso.total, spec.start()), iso.seed, Perturbation{});
        const std::span<const State> initials = std::span<const State>(global).subspan(iso.begin, iso.count);

        std::vector<Instrument> instruments(iso.count);
        ThreadSlots slots;
        std::vector<leo::Trajectory> trajectories;
        const double cpu_before = bm_process_cpu_seconds();
        const auto entry = Clock::now();
        try {
            if (iso.executor == "sequential") {
                trajectories.resize(iso.count);
                for (std::size_t index = 0; index < iso.count; ++index) {
                    Instrument& instrument = instruments[index];
                    instrument.worker_slot = 0;
                    instrument.factory_enter = Clock::now();
                    // A sequential failure is reported with the same global index a
                    // batch failure would carry, so one diagnostic format covers both.
                    try {
                        BmRhs rhs(work, spec, env, config, epoch, &instrument);
                        trajectories[index] = propagate(rhs, initials[index], epoch, labels, options);
                    } catch (const std::exception& cause) {
                        throw std::runtime_error("trajectory " + std::to_string(iso.begin + index) + " of " +
                                                 std::to_string(iso.total) + " (global index) failed: " +
                                                 cause.what());
                    }
                }
            } else {
                auto factory = [&](std::size_t index, const State&) {
                    Instrument& instrument = instruments[index];
                    instrument.factory_enter = Clock::now();
                    instrument.worker_slot = slots.acquire(std::this_thread::get_id());
                    return BmRhs(work, spec, env, config, epoch, &instrument);
                };
                trajectories = propagate_batch(factory, std::span<const State>(initials), epoch,
                                               std::span<const double>(labels), options, iso.workers);
            }
        } catch (const leo::BatchPropagationError& error) {
            // Report the GLOBAL index so an 8-way split is as diagnosable as a
            // single sequential run, and publish nothing.
            std::string cause;
            try {
                std::rethrow_exception(error.cause());
            } catch (const std::exception& nested) {
                cause = nested.what();
            } catch (...) {
                cause = "non-std exception";
            }
            throw std::runtime_error("trajectory " + std::to_string(iso.begin + error.trajectory_index()) +
                                     " of " + std::to_string(iso.total) + " (global index) failed: " + cause);
        }
        const auto exit = Clock::now();
        const double wall = bm_seconds_between(entry, exit);
        const double cpu = bm_process_cpu_seconds() - cpu_before;

        std::size_t rhs_calls = 0, atmosphere_calls = 0, worker_slots = 0;
        for (const Instrument& instrument : instruments) {
            rhs_calls += instrument.rhs_calls;
            atmosphere_calls += instrument.atmosphere_calls;
            worker_slots = std::max(worker_slots, instrument.worker_slot + 1);
        }

        iso_write_result(iso, epoch, labels, trajectories);

        if (!iso.meta_path.empty()) {
            BmJson meta;
            meta.text("kind", "MT-002 isolation envelope");
            meta.text("executor", iso.executor);
            meta.integer("pid", static_cast<std::uint64_t>(getpid()));
            meta.integer("workers_requested", iso.workers);
            meta.integer("workers_used", worker_slots);
            meta.text("model", work.name);
            meta.integer("global_total", iso.total);
            meta.integer("global_begin", iso.begin);
            meta.integer("trajectory_count", iso.count);
            meta.integer("seed", iso.seed);
            meta.integer("samples_per_trajectory", labels.size());
            meta.number("wall_seconds", wall);
            meta.number("cpu_seconds", cpu);
            meta.number("cpu_utilization", wall > 0 ? cpu / wall : 0.0);
            meta.integer("rhs_calls", rhs_calls);
            meta.integer("atmosphere_calls", atmosphere_calls);
            meta.number("peak_rss_bytes", bm_peak_rss_bytes());
            meta.text("boundary", "wall_seconds brackets the executor call only: worker creation, propagation and "
                                  "join. Process start-up, kernel and weather loading and output writing are "
                                  "excluded here and are inside process_batch.py's measured interval.");
            const std::filesystem::path meta_file(iso.meta_path);
            if (!meta_file.parent_path().empty()) std::filesystem::create_directories(meta_file.parent_path());
            std::ofstream out(meta_file, std::ios::trunc);
            if (!out) throw std::runtime_error("cannot write " + meta_file.string());
            out << meta.str() << '\n';
            if (!out) throw std::runtime_error("write failed for " + meta_file.string());
        }

        std::cout << "mt_isolation executor=" << iso.executor << " workers=" << iso.workers << " model=" << work.name
                  << " trajectories=" << iso.count << " begin=" << iso.begin << " total=" << iso.total
                  << " samples=" << labels.size() << " wall_seconds=" << wall << " out=" << iso.out_path << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mt_isolation: " << error.what() << '\n';
        return 2;
    }
}