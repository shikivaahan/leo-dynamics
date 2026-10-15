// ATM-CACHE-001 shared study surface.
//
// This header is the single interface between the study's translation units.
// It reuses the *existing* direct-reference experiment driver verbatim: the
// whole driver is included with its `main` renamed, so Config, Environment,
// Json, Distribution, Checksum, execute, case_dynamics, build_native_input,
// inertial_from_geodetic, tolerances() and the CASE-A/B/C scenarios are the
// real project code, not a re-implementation. Nothing in the production
// numerical path is modified, linked differently or bypassed.
//
// Layout of the study executable:
//
//   study.cpp         dispatch: this header + propagation.hpp, one process per
//                     (kind, model, candidate); writes exactly one JSON result
//   propagation.hpp   supplied by the performance owner: propagation_study,
//                     locality_study, benchmark_study
//
// Candidate evaluators implemented here (all measured, none in production):
//
//   A "exact"     complete-input-identity memoisation, bounded FIFO/LRU
//   B "cell"      nearest-neighbour spatial-temporal cells, first-hit reuse
//   C "interp"    log altitude / log time sparse 4-D multilinear interpolation
//                 of log density, positive-density preserving
//   D "adaptive"  measured midpoint-error driven refinement, bounded storage
//   E "control"   direct preprocessing control: no density caching at all
//
// Guarantees that hold for every candidate:
//   * Native evaluation stays authoritative. Every value that is not a stored
//     native value or a documented reconstruction of stored native values comes
//     from a fresh native call.
//   * Only density is cached. DirectAtmosphere::evaluate also returns
//     temperatures and (NRLMSISE-00) eight species number densities; a
//     density-only cache can never substitute for that contract and this study
//     never pretends it does. See density_only_api_note().
//   * Keys carry model, frame, switch vector, Ap-history mode, kernel
//     generation and the exact discrete space-weather vector.
//   * Interpolation and cell reuse are refused across any change of the
//     discrete weather vector, including JB2008's integer DSTDTC transitions
//     that occur *within* an hour, and across a UTC civil day.
//   * Every reconstruction is guarded for finiteness and positivity; a value
//     that fails the guard is refused and the native value is used instead.
//
// Threading: DensityEvaluator is mutable and NOT thread safe. Every thread must
// own its own evaluator; a thread-local cache is a process-local cache.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <memory>
#include <numbers>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The driver has no include guard; this header has one, so it is included once.
#ifndef ATM_CACHE_EXPERIMENT_HEADER
#define ATM_CACHE_EXPERIMENT_HEADER "../experiment.cpp"
#endif
#define main atm_cache_experiment_main
#include ATM_CACHE_EXPERIMENT_HEADER
#undef main

namespace cache_study {

// The inherited benchmark writer does not escape JSON string values. Study
// metadata includes encoded arrays and user paths, so serialize those safely
// without changing the frozen experiment's writer.
struct Json : bench::Json {
    using bench::Json::add;
    static std::string quoted(const std::string& value) {
        std::ostringstream encoded;
        encoded << '"';
        for (unsigned char character : value) {
            switch (character) {
            case '"': encoded << "\\\""; break;
            case '\\': encoded << "\\\\"; break;
            case '\n': encoded << "\\n"; break;
            case '\r': encoded << "\\r"; break;
            case '\t': encoded << "\\t"; break;
            default:
                if (character < 0x20)
                    encoded << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(character);
                else encoded << character;
            }
        }
        encoded << '"';
        return encoded.str();
    }
    void add(const std::string& name, const std::string& value) {
        key(name);
        out << quoted(value);
    }
    void add(const std::string& name, const char* value) { add(name, std::string(value)); }
};

// ---------------------------------------------------------------------------
// Candidate and study-kind vocabulary
// ---------------------------------------------------------------------------

enum class Candidate { exact, cell, interp, adaptive, control };

inline const char* candidate_name(Candidate candidate) {
    switch (candidate) {
    case Candidate::exact: return "A-exact-memoisation";
    case Candidate::cell: return "B-nearest-neighbour-cell";
    case Candidate::interp: return "C-log-density-interpolation";
    case Candidate::adaptive: return "D-measured-adaptive-refinement";
    case Candidate::control: return "E-direct-preprocessing-control";
    }
    throw std::invalid_argument("unknown candidate enumerator");
}

inline const char* candidate_slug(Candidate candidate) {
    switch (candidate) {
    case Candidate::exact: return "exact";
    case Candidate::cell: return "cell";
    case Candidate::interp: return "interp";
    case Candidate::adaptive: return "adaptive";
    case Candidate::control: return "control";
    }
    throw std::invalid_argument("unknown candidate enumerator");
}

// Runner-facing spellings. Long names are accepted too so a written command
// line and a runner label both work.
inline Candidate parse_candidate(std::string_view text) {
    if (text == "A" || text == "a" || text == "exact" || text == "exact-memoisation") return Candidate::exact;
    if (text == "B" || text == "b" || text == "cell" || text == "cells" || text == "nearest-neighbour")
        return Candidate::cell;
    if (text == "C" || text == "c" || text == "interp" || text == "interpolation")
        return Candidate::interp;
    if (text == "D" || text == "d" || text == "adaptive" || text == "refine") return Candidate::adaptive;
    if (text == "E" || text == "e" || text == "control" || text == "direct") return Candidate::control;
    throw std::invalid_argument("unknown --study-candidate: " + std::string(text));
}

inline bool study_kind_known(std::string_view kind) {
    return kind == "sensitivity" || kind == "accuracy" || kind == "check" ||
           kind == "propagation" || kind == "locality" || kind == "benchmark";
}

inline leo::atmosphere::Model parse_study_model(std::string_view text) {
    if (text == "nrlmsise00") return leo::atmosphere::Model::nrlmsise00;
    if (text == "jb2008") return leo::atmosphere::Model::jb2008;
    if (text == "exponential") return leo::atmosphere::Model::exponential;
    throw std::invalid_argument("unknown --study-model: " + std::string(text));
}

// Which axes candidate C interpolates over. An axis that is switched off is
// served from the cell of its own quantisation, which is exactly the
// nearest-neighbour reuse of candidate B on that axis; that is what makes the
// altitude-only and time-only variants meaningful rather than accidental.
enum class InterpolationMode { altitude, time, spatial, four_dimensional };

inline const char* interpolation_mode_name(InterpolationMode mode) {
    switch (mode) {
    case InterpolationMode::altitude: return "altitude";
    case InterpolationMode::time: return "time";
    case InterpolationMode::spatial: return "spatial";
    case InterpolationMode::four_dimensional: return "4d";
    }
    throw std::invalid_argument("unknown interpolation mode enumerator");
}

inline InterpolationMode parse_interpolation_mode(std::string_view text) {
    if (text == "altitude" || text == "altitude-only" || text == "alt") return InterpolationMode::altitude;
    if (text == "time" || text == "time-only") return InterpolationMode::time;
    if (text == "spatial" || text == "spatial-only" || text == "latlon") return InterpolationMode::spatial;
    if (text == "4d" || text == "four-dimensional" || text == "full") return InterpolationMode::four_dimensional;
    throw std::invalid_argument("unknown --study-interpolation: " + std::string(text));
}

// The axis the cell grid is uniform in. The reconstructed quantity is always
// ln(rho); this only decides how the cell index advances.
enum class AxisMode { linear, logarithmic };

inline const char* axis_mode_name(AxisMode mode) {
    return mode == AxisMode::linear ? "linear" : "log";
}

// ---------------------------------------------------------------------------
// Study settings
// ---------------------------------------------------------------------------

// Documented altitude domain of this study: the 200-1000 km band the study
// was asked to cover, with particular attention to 400-900 km. Outside it every
// candidate refuses to reconstruct and calls the native model; the bounds are
// published in every result so a reader never has to guess why a fallback
// happened.
inline constexpr double domain_altitude_min_km = 200.0;
inline constexpr double domain_altitude_max_km = 1000.0;

// The native input contract of each model, which is narrower or wider than the
// study's own band. A reconstruction is refused whenever a cell it would need to
// populate lies outside its model's native altitude range, so no out-of-contract
// node is ever evaluated and no valid query is ever forced onto a cell that
// cannot be built.
inline double model_altitude_min_km(leo::atmosphere::Model model) {
    switch (model) {
    case leo::atmosphere::Model::nrlmsise00: return 0.0;
    case leo::atmosphere::Model::jb2008: return 120.0;
    case leo::atmosphere::Model::exponential: return 0.0;
    }
    return 0.0;
}
inline double model_altitude_max_km(leo::atmosphere::Model model) {
    switch (model) {
    case leo::atmosphere::Model::nrlmsise00: return 1000.0;
    case leo::atmosphere::Model::jb2008: return 1500.0;
    case leo::atmosphere::Model::exponential: return 1e9;
    }
    return 1e9;
}
inline const char* model_altitude_source(leo::atmosphere::Model model) {
    return model == leo::atmosphere::Model::nrlmsise00
               ? "NRLMSISE-00 reference input range, 0-1000 km geodetic"
           : model == leo::atmosphere::Model::jb2008
               ? "JB2008 official input range, 120-1500 km geodetic"
               : "exponential profile takes |r| - Re and has no geodetic altitude contract";
}
// Near-polar queries are served by the native model only. A uniform longitude
// cell is degenerate there and the geographic quantisation has no meaning, so
// the study refuses to reuse rather than inventing a cell boundary.
inline constexpr double domain_polar_fallback_deg = 89.999;

struct CellScheme {
    double altitude_km = 25.0;
    double latitude_deg = 5.0;
    double longitude_deg = 5.0;
    double time_s = 60.0;
    std::size_t latitude_cells() const {
        return std::max<std::size_t>(1, std::llround(180.0 / latitude_deg));
    }
    // Longitude is periodic: the cell index is wrapped onto this count, so the
    // +180/-180 seam is one cell, not two, and a query 359.99 deg east and one
    // 0.01 deg east can share it.
    std::size_t longitude_cells() const {
        return std::max<std::size_t>(1, std::llround(360.0 / longitude_deg));
    }
    std::size_t altitude_cells() const {
        return std::max<std::size_t>(1, std::llround((domain_altitude_max_km -
                                                      domain_altitude_min_km) / altitude_km));
    }
    std::size_t time_span_s(double span) const {
        return std::max<std::size_t>(1, std::llround(span / std::max(1e-9, time_s)));
    }
    std::string json() const;
};

struct StudySettings {
    // Study-kind, model and candidate selection.
    std::string kind = "sensitivity";
    std::string model = "nrlmsise00";
    std::string candidate = "A";
    std::string input_set = "direct-v2";

    // Workload sizing and reproducibility.
    std::size_t points = 200;        // random holdout/query points
    std::size_t seed = 1;            // master seed; every sub-seed derives from it
    std::size_t capacity = 4096;     // bounded cache capacity, in payload entries
    std::size_t ensemble = 4;        // trajectories in an ensemble workload
    std::size_t thread_workers = 1;  // threads inside one process (not processes)

    // Physics/coordinate probe and screening parameters.
    double h_km = 600.0;      // probe geodetic altitude
    double angle_deg = 0.0;   // probe longitude (latitude sweep is internal)
    double time_s = 1.0;      // finite-difference time separation
    double target = 0.001;    // screening relative-error target, e.g. 0.01 % = 1e-4
    double scale = 1.0;       // resolution multiplier: >1 refines, <1 coarsens
    double perturbation_scale = -1.0;

    // Binary request-trace destination (empty: no trace is written).
    std::string trace_path;

    // Resolved by parse_study_arguments; not settable on the command line.
    Candidate candidate_kind = Candidate::exact;
    leo::atmosphere::Model model_kind = leo::atmosphere::Model::nrlmsise00;
    CellScheme base_cells{};
    // Candidate C: which axes are interpolated, and how the altitude and time
    // cell indices advance. The reconstructed quantity is always ln(rho), which
    // is what preserves positivity across the study's 200-1000 km dynamic
    // range; the axis transform is a separate, explicit and published choice.
    InterpolationMode interpolation = InterpolationMode::four_dimensional;
    AxisMode axis_mode = AxisMode::linear;
    // Log-axis widths, used only when axis_mode is logarithmic. The width is
    // ln(1 + cell/reference) so a cell is `cell` wide at the reference.
    double log_altitude_width = 0.0416667;   // ln(1 + 25/600)
    double log_time_width = 0.0416667;       // ln(1 + 60/600)
    double altitude_reference_km = 600.0;
    double time_reference_s = 600.0;
    // Maximum number of native probes candidate D may spend per served query.
    std::size_t adaptive_max_depth = 4;
    // Deterministic probe divisor for candidate D: one native probe per
    // `adaptive_probe_divisor` queries is used to measure the local error.
    std::uint64_t adaptive_probe_divisor = 16;
    // Log-time origin. 0 means "first epoch this evaluator ever sees", which is
    // recorded in the result and then held fixed.
    double time_reference_epoch_s = 0.0;
    bool replace_policy_lru = true;   // false selects FIFO
    // Explicit cell widths, distinct from the sensitivity probe altitude. A
    // value of 0 means "derive from the resolution scale"; any positive value
    // overrides it for that axis alone, so a sweep can move one axis at a time.
    double cell_altitude_km = 0;
    double cell_degrees = 0;
    double cell_time_s = 0;
    // Benchmark group control, emitted by the runner.
    std::size_t repeats = 0;
    std::size_t begin = 0;
    std::size_t count = 0;
    std::string benchmark_route;
    bool refine = false;
    bool measurement = false;         // in-evaluator section timers (diagnostics)
    std::size_t fixture_scan_days = 3;
    // Set when the supplied kernels or weather records are NOT the pinned
    // direct-v2 inputs. Such a run verifies the pipeline only; it is never
    // published as scientific or performance evidence, because its numbers are
    // not comparable with the direct-v2 baseline.
    bool verification_only = false;
    std::string verification_reason;
    // The band the cell scheme is allowed to cover. It defaults to the study's
    // documented 200-1000 km band and can be moved only so a correctness check
    // can push a cell outside its model's own native range and observe the
    // refusal. Every result publishes the band that was actually used.
    double altitude_min_km = domain_altitude_min_km;
    double altitude_max_km = domain_altitude_max_km;
    // When on, sensitivity queries NRLMSISE-00 at explicit Earth-fixed geodetic
    // coordinates instead of at inertial states. That needs the UTC calendar
    // from the LSK and the pinned CSSI record, and no Earth-orientation kernel
    // at all. It reaches the real native model through the production adapter
    // with the production input contract, but it is not a provider, a
    // preprocessing, a trajectory or an end-to-end measurement, and it is
    // labelled as such wherever it appears.
    bool native_geodetic = false;

    std::string json() const;
};

// A settings copy resolved at a different resolution. `scale` greater than one
// refines every cell: the base widths shrink by that factor and the log-space
// widths are recomputed from them, so a coarse-to-fine sweep moves one
// documented knob and nothing else.
inline StudySettings settings_at_scale(const StudySettings& base, double scale) {
    StudySettings settings = base;
    settings.scale = std::clamp(scale, 0.05, 20.0);
    settings.base_cells.altitude_km = 25.0 / settings.scale;
    settings.base_cells.latitude_deg = 5.0 / settings.scale;
    settings.base_cells.longitude_deg = 5.0 / settings.scale;
    settings.base_cells.time_s = 60.0 / settings.scale;
    // An explicit per-axis width overrides the scale for that axis alone, so a
    // sweep can move one axis while the others stay exactly where they were.
    if (settings.cell_altitude_km > 0) settings.base_cells.altitude_km = settings.cell_altitude_km;
    if (settings.cell_degrees > 0) {
        settings.base_cells.latitude_deg = settings.cell_degrees;
        settings.base_cells.longitude_deg = settings.cell_degrees;
    }
    if (settings.cell_time_s > 0) settings.base_cells.time_s = settings.cell_time_s;
    settings.log_altitude_width =
        std::log1p(settings.base_cells.altitude_km / settings.altitude_reference_km);
    settings.log_time_width = std::log1p(settings.base_cells.time_s / settings.time_reference_s);
    return settings;
}

// ---------------------------------------------------------------------------
// Native-input probe: Earth-fixed geodetic queries
// ---------------------------------------------------------------------------

// NRLMSISE-00 queried directly through the production adapter at explicit
// Earth-fixed geodetic coordinates and an absolute UTC instant.
//
// This exists because a geodetic native input needs exactly two things: the UTC
// civil calendar from the LSK, and the pinned CSSI space-weather record. It does
// not need an Earth-orientation kernel, because no inertial state is ever
// formed. The input contract is identical to the one DirectAtmosphere builds,
// field for field.
//
// What it is not: it is not a provider measurement, not a preprocessing
// measurement, not a trajectory and not an end-to-end result. Every result
// produced with it says so. Availability of the Earth-orientation kernel has no
// bearing on it, and it is never compared against a direct-v2 baseline number.
#ifdef LEO_HAS_NRLMSISE00
class NativeGeodeticProbe {
    const Environment& environment_;
    bool ap_history_;
    leo::atmosphere::Nrlmsise00 model_;

public:
    NativeGeodeticProbe(const Environment& env, bool ap_history)
        : environment_(env), ap_history_(ap_history),
          model_(ap_history ? ap_history_switches()
                            : leo::atmosphere::Nrlmsise00::standard_switches()) {}

    leo::atmosphere::NrlmsiseInput input(leo::Epoch epoch, double latitude_deg, double longitude_deg,
                                         double altitude_km) const {
        const auto utc = leo::SpiceEphemerisProvider::utc_calendar(epoch);
        auto in = environment_.weather.nrlmsise(utc, ap_history_);
        in.latitude_deg = latitude_deg;
        in.longitude_deg = longitude_deg;
        in.altitude_km = altitude_km;
        // The reference header's UT hours plus longitude/15, exactly as the
        // production provider computes it; no equation-of-time correction.
        in.local_solar_time_hours = std::fmod(utc.seconds_of_day / 3600.0 + in.longitude_deg / 15.0 + 24.0,
                                              24.0);
        return in;
    }

    double density(leo::Epoch epoch, double latitude_deg, double longitude_deg, double altitude_km) const {
        const auto result = model_.evaluate(input(epoch, latitude_deg, longitude_deg, altitude_km));
        return result.density_kg_m3;
    }

    const std::array<int, 24>& switches() const { return model_.switches(); }
};
#endif

// Parse the --study-* arguments and hand the remaining command line to the
// existing driver parser. The driver's own validation (kernels, weather, case,
// fixture requirements) still applies and is never weakened here.
struct StudyArguments {
    Config config;
    StudySettings settings;
};

inline double study_positive(const std::string& text, const char* what, double low, double high) {
    std::size_t used = 0;
    const double parsed = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(parsed) || parsed < low || parsed > high)
        throw std::invalid_argument(std::string("invalid ") + what + ": " + text);
    return parsed;
}

inline StudyArguments parse_study_arguments(int argc, char** argv) {
    std::vector<std::string> forwarded;
    forwarded.emplace_back("atm_cache_study");
    bool host_mode_given = false;
    StudySettings settings;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        const bool study = key.rfind("--study-", 0) == 0;
        if (!study) {
            if (key == "--mode") host_mode_given = true;
            forwarded.emplace_back(key);
            if (key.rfind("--", 0) == 0 && i + 1 < argc && std::string_view(argv[i + 1]).rfind("--", 0) != 0)
                forwarded.emplace_back(argv[++i]);
            continue;
        }
        if (i + 1 == argc) throw std::invalid_argument("missing value for " + key);
        const std::string value = argv[++i];
        if (key == "--study-kind") {
            settings.kind = value;
            if (!study_kind_known(settings.kind)) throw std::invalid_argument("unknown --study-kind: " + value);
        } else if (key == "--study-model") {
            settings.model = value;
        } else if (key == "--study-candidate") {
            settings.candidate = value;
        } else if (key == "--study-input-set") {
            if (value != "direct-v2" && value != "exp-solfsmy-20261011-original-pck")
                throw std::invalid_argument("unknown explicitly pinned input set: " + value);
            settings.input_set = value;
        } else if (key == "--study-points") {
            settings.points = static_cast<std::size_t>(study_positive(value, "--study-points", 1, 1e9));
        } else if (key == "--study-seed") {
            settings.seed = static_cast<std::size_t>(study_positive(value, "--study-seed", 0, 1e18));
        } else if (key == "--study-capacity") {
            settings.capacity = static_cast<std::size_t>(study_positive(value, "--study-capacity", 1, 1e9));
        } else if (key == "--study-ensemble") {
            settings.ensemble = static_cast<std::size_t>(study_positive(value, "--study-ensemble", 1, 1e6));
        } else if (key == "--study-thread-workers") {
            settings.thread_workers = static_cast<std::size_t>(study_positive(value, "--study-thread-workers", 1, 1024));
        } else if (key == "--study-h-km") {
            settings.h_km = study_positive(value, "--study-h-km", domain_altitude_min_km, domain_altitude_max_km);
        } else if (key == "--study-angle-deg") {
            settings.angle_deg = study_positive(value, "--study-angle-deg", -360.0, 360.0);
        } else if (key == "--study-time-s") {
            settings.time_s = study_positive(value, "--study-time-s", 1e-6, 86400.0);
        } else if (key == "--study-target") {
            settings.target = study_positive(value, "--study-target", 1e-5, 0.5);
        } else if (key == "--study-scale") {
            settings.scale = study_positive(value, "--study-scale", 0.05, 20.0);
        } else if (key == "--study-trace") {
            settings.trace_path = value;
        } else if (key == "--study-capacity-policy") {
            settings.replace_policy_lru = value != "fifo";
        } else if (key == "--study-time-reference-s") {
            settings.time_reference_epoch_s = study_positive(value, "--study-time-reference-s", 0.0, 1e18);
        } else if (key == "--study-cell-km") {
            settings.cell_altitude_km = study_positive(value, "--study-cell-km", 0.0, 1000.0);
        } else if (key == "--study-cell-deg") {
            settings.cell_degrees = study_positive(value, "--study-cell-deg", 0.0, 180.0);
        } else if (key == "--study-cell-s") {
            settings.cell_time_s = study_positive(value, "--study-cell-s", 0.0, 86400.0);
        } else if (key == "--study-repeats") {
            settings.repeats = static_cast<std::size_t>(study_positive(value, "--study-repeats", 0, 1e9));
        } else if (key == "--study-begin") {
            settings.begin = static_cast<std::size_t>(study_positive(value, "--study-begin", 0, 1e12));
        } else if (key == "--study-count") {
            settings.count = static_cast<std::size_t>(study_positive(value, "--study-count", 0, 1e12));
        } else if (key == "--study-benchmark-route") {
            if (value != "direct" && value != "candidate")
                throw std::invalid_argument(key + " expects direct or candidate");
            settings.benchmark_route = value;
        } else if (key == "--study-refine") {
            if (value != "on" && value != "off")
                throw std::invalid_argument(key + " expects on or off");
            settings.refine = value == "on";
        } else if (key == "--study-perturbation-scale") {
            settings.perturbation_scale = study_positive(value, "--study-perturbation-scale", 0.0, 20.0);
        } else if (key == "--study-native-geodetic") {
            if (value != "on" && value != "off")
                throw std::invalid_argument(key + " expects on or off, got: " + value);
            settings.native_geodetic = value == "on";
        } else if (key == "--study-altitude-min-km") {
            settings.altitude_min_km = study_positive(value, "--study-altitude-min-km", 1.0, 5000.0);
        } else if (key == "--study-altitude-max-km") {
            settings.altitude_max_km = study_positive(value, "--study-altitude-max-km", 2.0, 5000.0);
        } else if (key == "--study-interpolation") {
            settings.interpolation = parse_interpolation_mode(value);
        } else if (key == "--study-interpolation-axis") {
            if (value != "linear" && value != "log")
                throw std::invalid_argument(key + " expects linear or log, got: " + value);
            settings.axis_mode = value == "log" ? AxisMode::logarithmic : AxisMode::linear;
        } else if (key == "--study-verification-only") {
            if (value != "on" && value != "off")
                throw std::invalid_argument(key + " expects on or off, got: " + value);
            settings.verification_only = value == "on";
            if (i + 1 < argc && std::string_view(argv[i + 1]).rfind("--", 0) != 0)
                settings.verification_reason = argv[++i];
        } else {
            throw std::invalid_argument("unknown argument: " + key);
        }
    }
    settings.candidate_kind = parse_candidate(settings.candidate);
    settings.model_kind = parse_study_model(settings.model);
    // The host mode is only used to satisfy the existing driver's own argument
    // validation; the study never calls the driver main. `timing` is the mode
    // with the fewest extra requirements for a full run. The partial
    // native-input slice never reads a JB2008 record, so it defaults to
    // `coordinates`, which is the one mode that does not demand the SOLFSMY or
    // DTCFILE paths at all.
    if (!host_mode_given) {
        forwarded.emplace_back("--mode");
        forwarded.emplace_back(settings.native_geodetic ? "coordinates" : "timing");
    }
    // Native-only mode keeps the inherited environment's LSK alias until a
    // dedicated native environment is constructed; no arbitrary ephemeris is used.
    if (settings.native_geodetic &&
        std::find(forwarded.begin(), forwarded.end(), "--spk") == forwarded.end()) {
        // The inherited driver requires two kernel slots. Both name the same
        // verified LSK in native-only mode, which reads UTC but no Sun geometry.
        const auto lsk = std::find(forwarded.begin(), forwarded.end(), "--lsk");
        if (lsk == forwarded.end() || lsk + 1 == forwarded.end())
            throw std::invalid_argument("--lsk is required: the native slice reads UTC from the "
                                        "leapsecond kernel and uses no other ephemeris");
        const std::string lsk_path = *(lsk + 1);
        forwarded.push_back("--spk");
        forwarded.push_back(lsk_path);
    }
    std::vector<char*> raw;
    raw.reserve(forwarded.size());
    for (auto& item : forwarded) raw.push_back(item.data());
    raw.push_back(nullptr);
    StudyArguments parsed;
    parsed.config = parse_arguments(int(raw.size()) - 1, raw.data());
    if (settings.native_geodetic) {
        // The placeholder ephemeris is the leapsecond kernel itself: the shared
        // provider always furnishes an ephemeris entry, and loading the same
        // file twice is the reference-counted no-op the provider documents. No
        // planetary ephemeris content is read in this mode.
        if (parsed.config.spk.empty()) throw std::invalid_argument("internal: native slice lost its LSK");
    }
    // Cell geometry: the resolution scale sets every axis, and an explicit
    // per-axis width then overrides it for that axis alone. These are cell
    // widths, entirely separate from the probe altitude, probe longitude and
    // probe time separation, which choose where a sensitivity point is
    // measured rather than how wide a cache cell is.
    parsed.settings = settings_at_scale(settings, settings.scale);
    return parsed;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

// The study writes exactly one file: <Config.out>/study-result.json. The
// directory is created if absent; an existing study-result.json is replaced,
// because the runner owns resume and atomic publication. Nothing outside this
// directory is ever written and no frozen evidence file is touched.
inline const char* study_result_filename() { return "study-result.json"; }

inline std::filesystem::path study_result_path(const Config& c) {
    return std::filesystem::path(c.out) / study_result_filename();
}

inline void ensure_output_directory(const Config& c) {
    const std::filesystem::path directory(c.out);
    if (std::filesystem::exists(directory) && !std::filesystem::is_directory(directory))
        throw std::runtime_error("--out exists and is not a directory: " + directory.string());
    std::filesystem::create_directories(directory);
}

// Atomic publication: written to a sibling temporary and renamed, so a reader
// never observes a partial result.
inline void publish_study_result(const Config& c, const std::string& json) {
    ensure_output_directory(c);
    const std::filesystem::path target = study_result_path(c);
    const std::filesystem::path temporary = target.string() + ".partial";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + temporary.string());
        out << json << '\n';
        if (!out) throw std::runtime_error("write failed for " + temporary.string());
    }
    std::filesystem::rename(temporary, target);
}

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------

inline std::uint64_t mix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

inline std::uint64_t hash_double(std::uint64_t h, double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof bits);
    return mix64(h ^ mix64(bits));
}

inline std::uint64_t hash_int(std::uint64_t h, std::int64_t v) {
    return mix64(h ^ mix64(static_cast<std::uint64_t>(v)));
}

// A seeded generator whose every draw derives from one recorded master seed, so
// any single sub-experiment can be replayed on its own.
class Rng {
    std::mt19937_64 engine_;
public:
    explicit Rng(std::uint64_t seed) : engine_(seed) {}
    static std::uint64_t derive(std::uint64_t master, std::uint64_t purpose) {
        return mix64(master ^ mix64(purpose));
    }
    double uniform(double low, double high) {
        std::uniform_real_distribution<double> distribution(low, high);
        return distribution(engine_);
    }
    std::int64_t index(std::int64_t count) {
        if (count <= 0) return 0;
        std::uniform_int_distribution<std::int64_t> distribution(0, count - 1);
        return distribution(engine_);
    }
};

inline std::string numbers(const std::vector<double>& values) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) out << ',';
        if (!std::isfinite(values[index]))
            throw std::runtime_error("nonfinite numeric array value");
        out << values[index];
    }
    return out.str() + ']';
}

inline std::string strings(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) out << ',';
        out << '"' << values[index] << '"';
    }
    return out.str() + ']';
}

inline std::string raw_array(const std::vector<std::string>& elements) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (index) out << ',';
        out << elements[index];
    }
    return out.str() + ']';
}

inline double quantile_of_sorted(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted.front();
    const double position = std::clamp(fraction, 0.0, 1.0) * double(sorted.size() - 1);
    const std::size_t low = static_cast<std::size_t>(std::floor(position));
    const std::size_t high = std::min(sorted.size() - 1, low + 1);
    const double weight = position - double(low);
    return sorted[low] * (1.0 - weight) + sorted[high] * weight;
}

// ---------------------------------------------------------------------------
// Error metrics
// ---------------------------------------------------------------------------

struct ErrorMetrics {
    std::size_t count = 0;
    double mean_signed_relative = 0, mean_absolute_relative = 0, rms_relative = 0;
    double median_absolute_relative = 0, p95_absolute_relative = 0;
    double p99_absolute_relative = 0, p999_absolute_relative = 0;
    double maximum_absolute_relative = 0;
    double rms_log_error = 0, rms_log_density_error = 0;
    double largest_positive_bias = 0, largest_negative_bias = 0;
    std::string json() const;
};

// `signed_relative` is (reconstructed - reference)/reference at identical
// physical inputs; `log_error` is ln(reconstructed/reference).
inline ErrorMetrics error_metrics(const std::vector<double>& signed_relative,
                                  const std::vector<double>& log_error) {
    ErrorMetrics m;
    m.count = signed_relative.size();
    if (m.count == 0) return m;
    std::vector<double> absolute;
    absolute.reserve(m.count);
    double sum = 0, sum_square = 0, largest_positive = 0, largest_negative = 0;
    for (double value : signed_relative) {
        sum += value;
        sum_square += value * value;
        const double magnitude = std::abs(value);
        absolute.push_back(magnitude);
        largest_positive = std::max(largest_positive, value);
        largest_negative = std::min(largest_negative, value);
    }
    double sum_log_square = 0;
    for (double value : log_error) sum_log_square += value * value;
    std::sort(absolute.begin(), absolute.end());
    m.mean_signed_relative = sum / double(m.count);
    m.mean_absolute_relative = std::accumulate(absolute.begin(), absolute.end(), 0.0) / double(m.count);
    m.rms_relative = std::sqrt(sum_square / double(m.count));
    m.median_absolute_relative = quantile_of_sorted(absolute, 0.5);
    m.p95_absolute_relative = quantile_of_sorted(absolute, 0.95);
    m.p99_absolute_relative = quantile_of_sorted(absolute, 0.99);
    m.p999_absolute_relative = quantile_of_sorted(absolute, 0.999);
    m.maximum_absolute_relative = absolute.back();
    m.rms_log_error = std::sqrt(sum_log_square / std::max<std::size_t>(1, log_error.size()));
    m.rms_log_density_error = m.rms_log_error;
    m.largest_positive_bias = largest_positive;
    m.largest_negative_bias = largest_negative;
    return m;
}

// Threshold satisfaction is decided on the maximum observed error, never on a
// mean or a percentile. A mean that passes while the worst sample fails is not
// a passing configuration.
inline bool threshold_pass(const ErrorMetrics& m, double target) {
    return m.count > 0 && std::isfinite(m.maximum_absolute_relative) &&
           m.maximum_absolute_relative <= target;
}

inline std::string threshold_row(const ErrorMetrics& m, const std::vector<double>& targets) {
    std::ostringstream out;
    out << std::setprecision(17) << '{';
    bool first = true;
    for (double target : targets) {
        if (!first) out << ',';
        first = false;
        out << '"' << std::setprecision(6) << target << "\":" << (threshold_pass(m, target) ? "true" : "false");
    }
    out << ",\"decision_rule\":\"maximum absolute relative error over every tested sample\"}";
    return out.str();
}

// ---------------------------------------------------------------------------
// Error thresholds of the study. These are engineering sensitivity parameters,
// not acceptable physical errors.
// ---------------------------------------------------------------------------

inline const std::vector<double>& study_error_targets() {
    static const std::vector<double> v{0.0001, 0.001, 0.005, 0.01, 0.05};
    return v;
}

// ---------------------------------------------------------------------------
// Process memory
// ---------------------------------------------------------------------------

// Peak resident set of this process, read from the kernel. Reported separately
// from the payload estimate because the two answer different questions: how
// large the cache payload is, and what the process actually cost.
inline std::size_t process_peak_rss_bytes() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            std::istringstream parse(line.substr(6));
            double kilobytes = 0;
            parse >> kilobytes;
            return static_cast<std::size_t>(kilobytes * 1024.0);
        }
    }
    return 0;
}

inline std::size_t process_current_rss_bytes() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream parse(line.substr(6));
            double kilobytes = 0;
            parse >> kilobytes;
            return static_cast<std::size_t>(kilobytes * 1024.0);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Quantisation, shared by every candidate and by the locality study
// ---------------------------------------------------------------------------

inline std::int64_t cell_index_altitude(double altitude_km, const CellScheme& scheme) {
    const double clamped = std::clamp(altitude_km, domain_altitude_min_km, domain_altitude_max_km);
    return static_cast<std::int64_t>(std::floor((clamped - domain_altitude_min_km) / scheme.altitude_km));
}

// Altitude cell index against an explicit band, so a correctness check can move
// the band and observe the model-bound refusal instead of only asserting it.
inline std::int64_t cell_index_altitude(double altitude_km, const CellScheme& scheme, double band_min_km) {
    const double clamped = std::max(altitude_km, band_min_km);
    return static_cast<std::int64_t>(std::floor((clamped - band_min_km) / std::max(1e-9, scheme.altitude_km)));
}

// The altitude extent of a cell, used to prove that no cell the scheme can
// generate lies outside its model's own native input range.
inline void cell_altitude_extent(std::int64_t index, const CellScheme& scheme, double band_min_km,
                                 double& low_km, double& high_km) {
    low_km = band_min_km + double(index) * scheme.altitude_km;
    high_km = low_km + scheme.altitude_km;
}

inline std::int64_t cell_index_latitude(double latitude_deg, const CellScheme& scheme) {
    const std::int64_t count = static_cast<std::int64_t>(scheme.latitude_cells());
    std::int64_t index = static_cast<std::int64_t>(std::floor((latitude_deg + 90.0) / scheme.latitude_deg));
    return std::clamp<std::int64_t>(index, 0, count - 1);
}

// Longitude is periodic. The index is wrapped, so the +180/-180 seam is a single
// cell boundary and no query can land outside the grid.
inline std::int64_t cell_index_longitude(double longitude_deg, const CellScheme& scheme) {
    const std::int64_t count = static_cast<std::int64_t>(scheme.longitude_cells());
    double normalised = std::fmod(longitude_deg + 180.0, 360.0);
    if (normalised < 0) normalised += 360.0;
    std::int64_t index = static_cast<std::int64_t>(std::floor(normalised / scheme.longitude_deg));
    index %= count;
    if (index < 0) index += count;
    return index;
}

inline std::int64_t cell_index_time(double et_seconds, double origin_s, const CellScheme& scheme) {
    return static_cast<std::int64_t>(std::floor((et_seconds - origin_s) / std::max(1e-9, scheme.time_s)));
}


inline bool in_study_domain(double altitude_km, double latitude_deg, double band_min_km,
                            double band_max_km) {
    if (!std::isfinite(altitude_km) || !std::isfinite(latitude_deg)) return false;
    if (altitude_km < band_min_km || altitude_km > band_max_km) return false;
    return std::abs(latitude_deg) <= domain_polar_fallback_deg;
}
inline bool in_study_domain(double altitude_km, double latitude_deg) {
    return in_study_domain(altitude_km, latitude_deg, domain_altitude_min_km, domain_altitude_max_km);
}

// ---------------------------------------------------------------------------
// Bounded store with an explicit replacement policy
// ---------------------------------------------------------------------------

inline const char* bounded_store_payload_note() {
    return "estimated lower-bound payload: entries * (sizeof(key) + sizeof(entry) + 2 pointers); "
           "excludes duplicate map keys, tree/list node overhead, allocator metadata and non-cache allocations";
}
// Exact key comparison: no hash-collision approximation is permitted anywhere in
// this study, so every store is an ordered map over the complete key.
template <class Key, class Value>
class BoundedStore {
public:
    struct Entry {
        Value value{};
        std::uint64_t sequence = 0;
    };
    using List = std::list<std::pair<Key, Entry>>;

    BoundedStore(std::size_t capacity, bool lru)
        : capacity_(std::max<std::size_t>(1, capacity)), lru_(lru), order_(), index_() {}

    const Value* find(const Key& key) {
        const auto found = index_.find(key);
        if (found == index_.end()) return nullptr;
        auto& entry = *found->second;
        if (lru_) {
            order_.splice(order_.begin(), order_, found->second);
            entry.second.sequence = ++clock_;
        }
        return &entry.second.value;
    }

    void insert(const Key& key, const Value& value, std::uint64_t& evictions) {
        const auto found = index_.find(key);
        if (found != index_.end()) {
            found->second->second.value = value;
            found->second->second.sequence = ++clock_;
            if (lru_) order_.splice(order_.begin(), order_, found->second);
            return;
        }
        order_.emplace_front(key, Entry{value, ++clock_});
        index_[key] = order_.begin();
        while (index_.size() > capacity_) {
            index_.erase(order_.back().first);
            order_.pop_back();
            ++evictions;
        }
    }

    void clear() {
        index_.clear();
        order_.clear();
    }

    bool erase(const Key& key) {
        const auto found = index_.find(key);
        if (found == index_.end()) return false;
        order_.erase(found->second);
        index_.erase(found);
        return true;
    }

    std::size_t size() const { return index_.size(); }
    std::size_t capacity() const { return capacity_; }
    // Analytical payload size from the real element sizes. This is the size of
    // the cache payload, and deliberately excludes allocator bookkeeping and
    // every other process allocation.
    std::size_t payload_bytes() const {
        return index_.size() * (sizeof(Key) + sizeof(Entry) + sizeof(void*) * 2);
    }
    static std::string payload_note() {
        return bounded_store_payload_note();
    }

private:
    std::size_t capacity_;
    bool lru_;
    std::uint64_t clock_ = 0;
    List order_;
    std::map<Key, typename List::iterator> index_;
};


// ---------------------------------------------------------------------------
// Request trace
// ---------------------------------------------------------------------------

// Compact binary record of the real request sequence, written with no per-call
// logging. Format (little-endian native doubles, no padding):
//   header  : "ATMCTRC" , u32 version , u32 model , u32 candidate , u64 count
//   record  : f64 et_seconds, f64 x, f64 y, f64 z,
//             f64 geodetic_altitude_m, f64 latitude_deg, f64 longitude_deg,
//             f64 density_kg_m3, u32 flags
// flags bits: 1 exact hit, 2 cell reuse, 4 interpolation, 8 adaptive anchor,
//             16 adaptive probe (native), 32 native call, 64 guard fallback,
//             128 native value refused as invalid
class TraceWriter {
    std::ofstream out_;
    std::uint64_t written_ = 0;
    std::uint64_t flags_seen_ = 0;
public:
    // An unopened writer is a no-op, so an evaluator can always own one.
    TraceWriter() = default;
    TraceWriter(const std::string& path, std::uint32_t model, std::uint32_t candidate) : out_() {
        if (path.empty()) return;
        const std::filesystem::path file(path);
        if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path());
        out_.open(file, std::ios::binary | std::ios::trunc);
        if (!out_) throw std::runtime_error("cannot write trace " + path);
        char magic[8] = {'A', 'T', 'M', 'C', 'T', 'R', 'C', '1'};
        out_.write(magic, 8);
        const std::uint32_t version = 1;
        out_.write(reinterpret_cast<const char*>(&version), sizeof version);
        out_.write(reinterpret_cast<const char*>(&model), sizeof model);
        out_.write(reinterpret_cast<const char*>(&candidate), sizeof candidate);
        out_.write(reinterpret_cast<const char*>(&written_), sizeof written_);
    }
    bool active() const { return out_.is_open(); }
    void record(double et, const Eigen::Vector3d& position, double altitude_m,
                double latitude_deg, double longitude_deg, double density, std::uint32_t flags) {
        if (!out_.is_open()) return;
        out_.write(reinterpret_cast<const char*>(&et), sizeof et);
        for (int axis = 0; axis < 3; ++axis) {
            const double value = position[axis];
            out_.write(reinterpret_cast<const char*>(&value), sizeof value);
        }
        out_.write(reinterpret_cast<const char*>(&altitude_m), sizeof altitude_m);
        out_.write(reinterpret_cast<const char*>(&latitude_deg), sizeof latitude_deg);
        out_.write(reinterpret_cast<const char*>(&longitude_deg), sizeof longitude_deg);
        out_.write(reinterpret_cast<const char*>(&density), sizeof density);
        out_.write(reinterpret_cast<const char*>(&flags), sizeof flags);
        ++written_;
        flags_seen_ |= flags;
    }
    std::uint64_t count() const { return written_; }
    std::string finish() {
        if (!out_.is_open()) return "{}";
        out_.flush();
        out_.close();
        Json j;
        j.add("path_written", true);
        j.add("records", written_);
        j.add("format", "ATMCTRC1, 68-byte records: et, x, y, z, geodetic_altitude_m, "
                        "latitude_deg, longitude_deg, density_kg_m3, u32 flags");
        return j.str();
    }
};

enum : std::uint32_t {
    trace_exact_hit = 1u << 0,
    trace_cell_reuse = 1u << 1,
    trace_interpolation = 1u << 2,
    trace_adaptive_anchor = 1u << 3,
    trace_adaptive_probe = 1u << 4,
    trace_native_call = 1u << 5,
    trace_guard_fallback = 1u << 6,
    trace_invalid_value = 1u << 7,
};

// ---------------------------------------------------------------------------
// Space-weather identity
// ---------------------------------------------------------------------------

// The complete discrete space-weather vector of one query, stored bitwise and
// compared bitwise. A hash may index it; it may never stand in for it, because a
// collision would silently permit reuse across incompatible weather. The
// vector contains the civil day, the leap-day flag and the Ap-history switch
// alongside the model's own indices, so a change of any of them is a change of
// identity and therefore a miss.
struct WeatherIdentity {
    static constexpr std::size_t word_count = 16;
    std::array<std::uint64_t, word_count> words{};
    std::size_t size = 0;

    void add_double(double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        add_word(bits);
    }
    void add_word(std::uint64_t value) {
        if (size == words.size())
            throw std::runtime_error("space-weather identity overflow");
        words[size++] = value;
    }
    bool operator<(const WeatherIdentity& o) const { return std::tie(words, size) < std::tie(o.words, o.size); }
    bool operator==(const WeatherIdentity& o) const { return words == o.words && size == o.size; }
    bool operator!=(const WeatherIdentity& o) const { return !(*this == o); }
    // A stable 64-bit summary for reporting only. Never used for a decision.
    std::uint64_t summary() const {
        std::uint64_t hash = 0;
        for (std::uint64_t word : words) hash = mix64(hash ^ word);
        return hash;
    }
};

// ---------------------------------------------------------------------------
// Query inputs
// ---------------------------------------------------------------------------

// Everything a native call for one query depends on, resolved once per query by
// the same preprocessing the production provider uses. Building it is real
// work and is timed as such; nothing here is a cache.
struct QueryInputs {
    leo::Epoch epoch{};
    InertialVector position{};
    double altitude_m = 0;              // geodetic ellipsoidal, empirical models
    double latitude_rad = 0;            // geodetic, NRLMSISE-00
    double longitude_rad = 0;
    double latitude_deg = 0;
    double longitude_deg = 0;
    double latitude_geocentric_rad = 0; // JB2008 uses geocentric latitude
    double local_solar_time_hours = 0;
    double utc_seconds_of_day = 0;
    double utc_day = 0;                 // quasi-MJD day index; never interpolated across
    bool leap_second_day = false;
    bool ap_history_mode = false;
    double sun_ra_rad = 0, sun_dec_rad = 0;
    bool sun_valid = false;
    leo::atmosphere::NrlmsiseInput nrlmsise{};
    leo::atmosphere::Jb2008Input jb2008{};

    // Identity of the *discrete* space-weather vector for this query, plus the
    // civil day and the Ap-history switch. Two queries may share stored or
    // interpolated values only when these are bitwise equal. This rejects, by
    // construction: daily solar boundaries, 3-hourly Ap boundaries, hourly
    // DTC record changes, the integer DSTDTC transitions that occur *within*
    // an hour, and every UTC midnight. Continuous time is never part of this
    // identity; it is the interpolation axis.
    WeatherIdentity weather{};

    // Identity of the complete native input for an exact memoisation: the
    // discrete weather identity plus the exact epoch and geodetic position bits.
    std::uint64_t exact_identity = 0;
};

// ---------------------------------------------------------------------------
// Density evaluator
// ---------------------------------------------------------------------------

struct DensityStatistics {
    std::uint64_t queries = 0;
    std::uint64_t exact_hits = 0;
    std::uint64_t acquisition_skips = 0;
    std::uint64_t cell_reuses = 0;
    std::uint64_t interpolations = 0;
    std::uint64_t adaptive_anchor_uses = 0;
    std::uint64_t adaptive_probes = 0;
    std::uint64_t adaptive_refinements = 0;
    std::uint64_t adaptive_degradations = 0;
    std::uint64_t native_calls = 0;
    std::uint64_t insertions = 0;
    std::uint64_t evictions = 0;
    std::uint64_t guard_fallbacks = 0;
    std::uint64_t domain_fallbacks = 0;
    std::uint64_t weather_guard_rejections = 0;
    std::uint64_t incomplete_corners = 0;
    std::uint64_t out_of_contract_nodes = 0;
    std::uint64_t negative_time_fallbacks = 0;
    std::uint64_t invalid_native_values = 0;
    std::uint64_t invalidations = 0;
    std::uint64_t construction_queries = 0;

    double construction_seconds = 0;
    double lookup_seconds = 0;
    double native_seconds = 0;

    std::size_t entries = 0;
    std::size_t capacity = 0;
    std::size_t payload_bytes = 0;
    std::size_t peak_entries = 0;

    double measured() const {
        return queries ? double(native_calls) / double(queries) : 0.0;
    }
    double avoided() const {
        return queries ? 1.0 - double(native_calls) / double(queries) : 0.0;
    }
    std::string json() const;
};

class DensityEvaluator {
public:
    // `env` and `c` must outlive the evaluator. No exception is swallowed: a
    // compiled-in backend that cannot answer is a run failure, never a silently
    // narrower result.
    DensityEvaluator(const Config& c, Environment& env, leo::atmosphere::Model model,
                     const StudySettings& settings);

    // Candidate path. Returns density in kg/m^3 only.
    double evaluate(leo::Epoch epoch, leo::InertialVector position,
                    const leo::SunObservation* sun = nullptr);

    // Authoritative reference: a fresh production DirectAtmosphere evaluation
    // through exactly the current preprocessing optimisations, with every
    // ATM-OPT-001 reuse enabled. Never cached, never reconstructed.
    double direct(leo::Epoch epoch, leo::InertialVector position,
                  const leo::SunObservation* sun = nullptr) const;

    // Complete production Result, always from a fresh native evaluation. A
    // density-only cache cannot serve temperatures or constituents, so this is
    // the only correct way to obtain them and it is never cached.
    leo::atmosphere::Result authoritative(leo::Epoch epoch, leo::InertialVector position,
                                          const leo::SunObservation* sun = nullptr) const;

    std::string statistics() const;
    DensityStatistics counts() const;
    const StudySettings& settings() const { return settings_; }
    std::uint64_t kernel_token() const { return kernel_token_; }
    std::uint64_t authoritative_calls() const { return authoritative_calls_; }

    // Clears every stored payload but keeps the counters, so cold-start and
    // eviction behaviour can be measured on one evaluator.
    void invalidate(const std::string& reason);

    // Detect managed SPICE generation changes using an opaque observation stamp.
    // File metadata cannot detect load/unload or explicit invalidate_reuse().
    bool refresh_kernel_generation();


    void set_measurement(bool enabled) { measurement_ = enabled; }
    std::size_t entries() const;
    std::size_t payload_bytes() const;
    std::size_t capacity() const { return settings_.capacity; }
    void set_capacity(std::size_t capacity);
    double time_origin() const { return time_origin_; }
    void set_time_origin(double et_seconds);
    void finish_trace();

private:
    // Candidate A keys on the *original* physical inputs, bit for bit: the
    // absolute epoch and the J2000 position actually handed to the provider. A
    // geodetic re-derivation would be a different identity, and comparing
    // floating-point keys with a total order would equate signed zeros and force
    // an arbitrary ordering on equal magnitudes. Raw integer words avoid both.
    struct ExactKey {
        std::uint64_t epoch_bits = 0, x_bits = 0, y_bits = 0, z_bits = 0;
        std::uint64_t sun_x_bits = 0, sun_y_bits = 0, sun_z_bits = 0;
        std::uint64_t kernel_token = 0;
        WeatherIdentity weather{};
        bool operator<(const ExactKey& o) const {
            return std::tie(epoch_bits, x_bits, y_bits, z_bits,
                            sun_x_bits, sun_y_bits, sun_z_bits, kernel_token, weather) <
                   std::tie(o.epoch_bits, o.x_bits, o.y_bits, o.z_bits,
                            o.sun_x_bits, o.sun_y_bits, o.sun_z_bits, o.kernel_token, o.weather);
        }
    };
    struct CellKey {
        std::int64_t altitude = 0, latitude = 0, longitude = 0, time = 0;
        WeatherIdentity weather{};
        bool operator<(const CellKey& o) const {
            return std::tie(altitude, latitude, longitude, time, weather) <
                   std::tie(o.altitude, o.latitude, o.longitude, o.time, o.weather);
        }
    };
    struct CornerKey {
        std::int64_t u = 0, v = 0, w = 0, s = 0;
        WeatherIdentity weather{};
        bool operator<(const CornerKey& o) const {
            return std::tie(u, v, w, s, weather) < std::tie(o.u, o.v, o.w, o.s, o.weather);
        }
    };
    struct CornerValue {
        double log_density = 0;
    };
    struct AdaptiveKey {
        std::int64_t altitude = 0, latitude = 0, longitude = 0, time = 0, depth = 0;
        WeatherIdentity weather{};
        bool operator<(const AdaptiveKey& o) const {
            return std::tie(altitude, latitude, longitude, time, depth, weather) <
                   std::tie(o.altitude, o.latitude, o.longitude, o.time, o.depth, o.weather);
        }
    };
    struct AdaptiveValue {
        double density = 0;
        double measured_relative_error = 0;
        std::uint64_t probes = 0;
        std::uint64_t serves = 0;
    };

    QueryInputs build_inputs(leo::Epoch epoch, const leo::InertialVector& position,
                             const leo::SunObservation* sun) const;
    ExactKey exact_key(leo::Epoch epoch, const leo::InertialVector& position,
                       const leo::SunObservation* sun) const;
    void emit_prepared(leo::Epoch epoch, const leo::InertialVector& position,
                       double density, std::uint32_t flags);
    double native_density(const QueryInputs& inputs, const leo::SunObservation* sun) const;
    [[noreturn]] void rethrow_native_failure(
        const QueryInputs& inputs, const std::exception& error) const;

    double serve_exact(const QueryInputs& inputs, const ExactKey& key);
    double serve_cell(const QueryInputs& inputs);
    double serve_interpolation(const QueryInputs& inputs);
    double serve_adaptive(const QueryInputs& inputs);
    double serve_control(const QueryInputs& inputs);

    double emit(const QueryInputs& inputs, double density, std::uint32_t flags);
    double native_call(const QueryInputs& inputs, const leo::SunObservation* sun);
    bool usable(double density) const {
        return std::isfinite(density) && density > 0.0;
    }


    Config config_;
    Environment& environment_;
    StudySettings settings_;
    leo::atmosphere::Model model_;
    std::array<int, 24> switches_{};
    leo::atmosphere::DirectAtmosphere authoritative_;
#ifdef LEO_HAS_NRLMSISE00
    leo::atmosphere::Nrlmsise00 nrlmsise_;
#endif
    bool measurement_ = false;
    bool building_ = true;

    BoundedStore<ExactKey, double> exact_;
    BoundedStore<CellKey, double> cells_;
    BoundedStore<CornerKey, CornerValue> corners_;
    BoundedStore<AdaptiveKey, AdaptiveValue> adaptive_;

    double time_origin_ = 0;
    bool time_origin_fixed_ = false;
    // The weather identity of the previous query. A miss that happens while this
    // differs from the query's own identity is a refusal by the weather guard,
    // which is the only way that counter is measured; nothing is inferred.
    WeatherIdentity previous_weather_{};
    bool has_previous_weather_ = false;
    std::uint64_t probe_counter_ = 0;
    std::uint64_t kernel_token_ = 1;
    std::optional<leo::SunObservation> generation_observation_;
    std::string last_invalidation_reason_;
    mutable DensityStatistics stats_{};
    mutable std::uint64_t authoritative_calls_ = 0;
    std::string trace_summary_ = "{}";
    bool trace_finished_ = false;
    TraceWriter trace_{};
};

// ---------------------------------------------------------------------------
// Packets: deterministic query sequences for locality and accuracy
// ---------------------------------------------------------------------------

struct QuerySample {
    leo::Epoch epoch{};
    leo::InertialVector position{};
};

// A clustered packet around one reference state: points that share an epoch
// neighbourhood and a position neighbourhood, which is what an adaptive
// integrator's stage sequence actually produces. A uniform random packet is
// generated separately so a study can measure reuse under both regimes.
inline std::vector<QuerySample> make_local_packets(leo::Epoch reference, const InertialVector& reference_position,
                                                   std::size_t packets, std::size_t per_packet,
                                                   std::uint64_t seed, double altitude_spread_km,
                                                   double time_spread_s) {
    Rng rng(seed);
    std::vector<QuerySample> samples;
    samples.reserve(packets * per_packet);
    for (std::size_t packet = 0; packet < packets; ++packet) {
        const double centre_time = reference.et_seconds +
                                   rng.uniform(-0.5, 0.5) * time_spread_s * double(per_packet);
        for (std::size_t index = 0; index < per_packet; ++index) {
            // Perturb the radius by the altitude spread and the direction inside
            // the local tangent plane, so consecutive samples are genuinely close
            // in every coordinate the models see.
            const Eigen::Vector3d radial = reference_position.as_eigen().normalized();
            Eigen::Vector3d east = radial.cross(Eigen::Vector3d{0, 0, 1});
            if (east.norm() < 1e-9) east = Eigen::Vector3d{1, 0, 0};
            east.normalize();
            const Eigen::Vector3d north = radial.cross(east).normalized();
            const double along = rng.uniform(-0.5, 0.5) * time_spread_s * double(per_packet) * 30.0;
            const double across = rng.uniform(-1.0, 1.0) * altitude_spread_km * 1e3 * 0.25;
            const double up = rng.uniform(-1.0, 1.0) * altitude_spread_km * 1e3 * 0.5;
            Eigen::Vector3d position = reference_position.as_eigen() + radial * up + east * along + north * across;
            const double radius = position.norm();
            if (radius < leo::atmosphere::wgs84_a_m) continue;  // never inside or on the Earth
            samples.push_back({leo::Epoch{centre_time + rng.uniform(-0.5, 0.5) * time_spread_s},
                               InertialVector{position}});
        }
    }
    return samples;
}

inline std::vector<QuerySample> make_random_packets(leo::Epoch reference, std::size_t count, std::uint64_t seed,
                                                   double altitude_low_km, double altitude_high_km,
                                                   double time_spread_s, const std::string& frame) {
    Rng rng(seed);
    std::vector<QuerySample> samples;
    samples.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const double altitude_km = rng.uniform(altitude_low_km, altitude_high_km);
        const double theta = rng.uniform(0.0, 2.0 * std::numbers::pi);
        const double z = rng.uniform(-1.0, 1.0);
        const leo::Epoch at{reference.et_seconds + rng.uniform(-0.5, 0.5) * time_spread_s};
        const auto position = inertial_from_geodetic(
            at, std::asin(z), theta, altitude_km * 1e3, frame);
        samples.push_back({at, position});
    }
    return samples;
}

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

// The complete key provenance every stored value carries. It is part of every
// cache key, so a stored value can never outlive the configuration that
// produced it.
inline std::string density_only_api_note() {
    return
        "these candidates cache density in kg/m^3 only. DirectAtmosphere::evaluate additionally "
        "returns temperature_k, exospheric_temperature_k and (NRLMSISE-00 only) eight species "
        "number densities, so a density-only cache cannot substitute for the production Result "
        "contract. Every caller that needs those fields must use DensityEvaluator::authoritative, "
        "which is never cached. The coupled RHS in experiment.cpp consumes density_kg_m3 only, so "
        "that one call path is preservable; the public provider contract is not.";
}

inline std::string study_metadata(const Config& c, Environment& env, const StudySettings& s) {
    Json j;
    j.add("kind", "atm-cache-study-result");
    j.add("study_kind", s.kind);
    j.add("measurement_mode", s.native_geodetic ? "native-geodetic-partial" : "provider");
    if (s.native_geodetic) {
        j.add("measurement_scope",
              "PARTIAL: NRLMSISE-00 is queried through the production adapter at explicit Earth-fixed "
              "geodetic coordinates and absolute UTC instants. No provider, no Earth-orientation "
              "preprocessing, no trajectory, no coupled RHS and no end-to-end quantity is measured. "
              "These numbers are not comparable with a direct-v2 baseline and must not be read as "
              "provider, preprocessing or performance evidence.");
        j.add("required_inputs", strings({"leapseconds kernel (UTC calendar)", "pinned CSSI weather archive"}));
        j.add("not_used_inputs",
              strings({"planetary ephemeris", "Earth orientation kernel", "JB2008 SOLFSMY and DTCFILE records"}));
    } else {
        j.add("measurement_scope",
              "every reference density is a fresh production DirectAtmosphere evaluation through the "
              "full acquisition path with all ATM-OPT-001 preprocessing reuse enabled");
    }
    j.add("evidence_class",
          s.verification_only
              ? "pipeline-verification-only: not pinned direct-v2 scientific or performance evidence"
              : s.native_geodetic
                    ? "scientific-native-input-partial: pinned NRLMSISE-00/CSSI/UTC, no provider or trajectory claims"
                    : s.input_set == "direct-v2"
                        ? "scientific-primary: pinned direct-v2 input set"
                        : "scientific-experimental-pinned: separately pinned SOLFSMY release and original Earth PCK");
    j.add("verification_only", s.verification_only);
    j.add("verification_reason", s.verification_reason);
    j.add("input_set", s.input_set);
    j.add("historical_reference", "direct-v2");
    j.add("input_pinning_rule",
          "all input bytes must match the explicitly selected input set; a separately pinned "
          "experimental input set is never relabelled as the historical direct-v2 input set");
    j.add("model", model_name(s.model_kind));
    j.add("candidate", candidate_name(s.candidate_kind));
    j.add("candidate_slug", candidate_slug(s.candidate_kind));
    j.raw("arguments", s.json());
    j.add("master_seed", static_cast<double>(s.seed));
    j.add("seed_derivation", "every sub-seed is mix64(master_seed ^ mix64(purpose))");
    j.add("density_only_api_contract", density_only_api_note());
    j.add("native_authority",
          "every non-reconstructed value is a fresh native evaluation; reconstructed values are "
          "documented combinations of stored native values only");
    j.add("preprocessing_options",
          "UTC civil-day reuse, exact-epoch Earth orientation reuse and shared Sun observation are "
          "all enabled, exactly as in the direct-v2 baseline");
    j.add("utc_reuse", c.options.utc_reuse);
    j.add("orientation_reuse", c.options.orientation_reuse);
    j.add("sun_sharing", c.options.sun_sharing);
    j.add("frame", c.frame);
    j.add("ap_history_mode", c.ap_history);
    j.add("duration_s", c.duration);
    j.add("cadence_s", c.cadence);
    j.add("rtol", c.rtol);
    j.add("max_step_s", c.max_step);
    j.add("earth_fixed_ellipsoid_a_m", leo::atmosphere::wgs84_a_m);
    j.add("earth_fixed_flattening", leo::atmosphere::wgs84_f);
    j.add("altitude_domain_km", numbers({domain_altitude_min_km, domain_altitude_max_km}));
    j.add("model_native_altitude_km",
          numbers({model_altitude_min_km(s.model_kind), model_altitude_max_km(s.model_kind)}));
    j.add("model_native_altitude_source", model_altitude_source(s.model_kind));
    j.add("out_of_contract_node_rule",
          "a reconstruction is refused when any cell it would need lies outside the model's own native "
          "altitude range, so no out-of-contract node is ever evaluated and no valid query is forced onto "
          "a cell that cannot be built");
    j.add("polar_fallback_deg", domain_polar_fallback_deg);
    j.add("polar_fallback_rule",
          "a query with |latitude| above the fallback limit is served by the native model only; a "
          "uniform longitude cell is degenerate there, so no cell boundary is invented");
    j.add("longitude_wrap_rule",
          "longitude cell indices are wrapped onto the periodic grid, so the +180/-180 seam is a "
          "single cell boundary and never a duplicated cell");
    j.add("weather_guard_rule",
          "every cache key carries the model's complete discrete space-weather vector bitwise, not a "
          "hash of it, together with the UTC civil day, the leap-day flag and the Ap-history switch. A "
          "single differing bit is a different key, so no stored, cell-reused or interpolated value can "
          "cross a daily solar or Ap boundary, a 3-hourly Ap bin, an hourly DTC record, an integer "
          "DSTDTC transition inside an hour, a switch-vector change or UTC midnight");
    j.add("weather_identity_comparison",
          "bitwise equality of the full vector; a 64-bit summary is reported for convenience and is "
          "never used to decide whether a value may be reused");
    j.add("interpolation_mode", interpolation_mode_name(s.interpolation));
    j.add("interpolation_axis", axis_mode_name(s.axis_mode));
    j.add("reconstructed_quantity", "ln(density)");
    j.add("native_definition_of_invalid", "a native density that is not finite and positive is "
                                          "counted and reported, never replaced or concealed");
    j.add("kernel_invalidation_rule",
          "an opaque Sun observation stamps the managed SPICE generation; each cache query checks "
          "its compatibility and discards all stored values after managed invalidation");
    j.add("threading", "one evaluator per thread; DensityEvaluator is process-local and not thread safe");
    j.add("weather_identity_definition",
          "one free function, cache_study::weather_identity(config, env, model, epoch), called by "
          "DensityEvaluator::build_inputs and available to any analysis that must decide whether two "
          "epochs may share a stored or interpolated value; it copies the model's complete discrete "
          "space-weather vector bitwise and is never hashed for a decision");
    j.raw("fixture_epochs", [&] {
        std::vector<std::string> rows;
        for (const auto& fixture : c.fixture_epochs) {
            Json row;
            row.add("name", fixture.name);
            row.add("utc", fixture.utc);
            row.add("epoch_et_seconds", fixture.epoch.et_seconds);
            rows.push_back(row.str());
        }
        return raw_array(rows);
    }().c_str());
    j.raw("case_files", [&] {
        std::vector<std::string> rows;
        for (const auto& file : c.case_files) rows.push_back("\"" + file + "\"");
        return raw_array(rows);
    }().c_str());
    // Input provenance: the exact bytes the measurements read. Each furnished
    // kernel appears once even where the provider lists a file under two slots.
    std::vector<std::string> kernel_rows;
    std::vector<std::filesystem::path> unique_kernels;
    for (const auto& kernel : Environment::make_kernels(c))
        if (std::find(unique_kernels.begin(), unique_kernels.end(), kernel) == unique_kernels.end())
            unique_kernels.push_back(kernel);
    for (const auto& kernel : unique_kernels) {
        Json row;
        row.add("path", kernel.string());
        row.add("sha256", sha256_file(kernel));
        row.add("bytes", static_cast<double>(std::filesystem::file_size(kernel)));
        kernel_rows.push_back(row.str());
    }
    j.raw("kernels", raw_array(kernel_rows));
    for (const auto& entry : std::vector<std::pair<std::string, std::string>>{
             {"cssi_weather_archive", c.weather}, {"solfsmy", c.solfsmy}, {"dtcfile", c.dtcfile}}) {
        if (entry.second.empty()) continue;
        Json row;
        row.add("path", entry.second);
        row.add("sha256", sha256_file(entry.second));
        row.add("bytes", static_cast<double>(std::filesystem::file_size(entry.second)));
        j.raw(entry.first.c_str(), row.str());
    }
    j.add("spice_toolkit_version", leo::SpiceEphemerisProvider::toolkit_version());
    j.add("weather_load_seconds", env.load_seconds);
    j.add("compiler", __VERSION__);
    j.add("compiled_in_models", strings({
        std::string(model_name(leo::atmosphere::Model::nrlmsise00)) + (compiled_in(leo::atmosphere::Model::nrlmsise00) ? ":on" : ":off"),
        std::string(model_name(leo::atmosphere::Model::jb2008)) + (compiled_in(leo::atmosphere::Model::jb2008) ? ":on" : ":off")}));
    j.add("process_peak_rss_bytes", static_cast<double>(process_peak_rss_bytes()));
    j.add("process_current_rss_bytes", static_cast<double>(process_current_rss_bytes()));
    j.add("payload_estimate_note",
          "cache payload bytes are reported separately from the process peak RSS: the payload is "
          "the cache's own cost, the RSS is everything the process used");
    return j.str();
}

// ---------------------------------------------------------------------------
// Study entry points supplied by the performance owner (propagation.hpp)
// ---------------------------------------------------------------------------

std::string propagation_study(const Config& c, Environment& env, const StudySettings& s);
std::string locality_study(const Config& c, Environment& env, const StudySettings& s);
std::string benchmark_study(const Config& c, Environment& env, const StudySettings& s);
inline std::string process_benchmark_study(const Config& c, Environment& env, const StudySettings& s);

} // namespace cache_study

#include "study_common.hpp"
#include "study_impl.hpp"