// Direct-reference atmospheric experiment driver for leo-dynamics.
//
// Subcommands (one process each, selected with --mode):
//   fixtures    direct model evaluation over epoch x latitude x local-time x altitude grids
//   propagate   matched CASE-A/B/C six-hour propagation per atmosphere model,
//               each with a nominal Dopri5 run and an independent RKF7(8) refinement
//   timing      repeated raw direct-evaluation and full-propagation timing distributions
//
// No atmosphere result is ever cached, memoised, interpolated or replaced by a
// surrogate: every reported density comes from a fresh call into the selected
// reference model (or, for the baseline, the existing ExponentialAtmosphere).
// Space-weather records are loaded once into an immutable WeatherDataset; that is
// immutable input data, not a density cache.
#include "bench_support.hpp"
#include "experiments/scenario.hpp"
#include "leo/atmosphere/coordinates.hpp"
#include "leo/atmosphere/jb2008.hpp"
#include "leo/atmosphere/nrlmsise00.hpp"
#include "leo/atmosphere/provider.hpp"
#include "leo/atmosphere/weather.hpp"
#include "leo/analysis/loads.hpp"
#include "leo/analysis/orbital.hpp"
#include "leo/environment/spice.hpp"
#include "leo/integration/detail/packed_state.hpp"
#include <boost/numeric/odeint/stepper/controlled_runge_kutta.hpp>
#include <boost/numeric/odeint/stepper/runge_kutta_fehlberg78.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace bench;
using Packed = leo::detail::PackedState;

namespace {

constexpr double kelvin_offset = 0.0;
const std::array<const char*, 8> number_density_names{
    "He", "O", "N2", "O2", "Ar", "H", "N", "O_anomalous"};

double positive(const std::string& text, const char* what) {
    std::size_t used = 0;
    const double parsed = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(parsed) || parsed <= 0)
        throw std::invalid_argument(std::string("invalid ") + what + ": " + text);
    return parsed;
}

std::string read_whole_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string sha256_file(const std::filesystem::path& path) {
    // Small FIPS-free SHA-256, so the executable can hash its own inputs without
    // adding a dependency. Used for evidence self-description only.
    static const std::uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    const std::string data = read_whole_file(path);
    std::vector<unsigned char> message(data.begin(), data.end());
    const std::uint64_t bits = std::uint64_t(message.size()) * 8;
    message.push_back(0x80);
    while (message.size() % 64 != 56) message.push_back(0);
    for (int index = 7; index >= 0; --index) message.push_back(static_cast<unsigned char>((bits >> (index * 8)) & 0xff));
    std::uint32_t h[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                          0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    const auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const std::size_t p = offset + std::size_t(i) * 4;
            w[i] = (std::uint32_t(message[p]) << 24) | (std::uint32_t(message[p + 1]) << 16) |
                   (std::uint32_t(message[p + 2]) << 8) | std::uint32_t(message[p + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], x = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = x + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;
            x = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += x;
    }
    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (std::uint32_t value : h) hex << std::setw(8) << value;
    return hex.str();
}

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

struct FixtureEpoch {
    std::string name, utc;
    leo::Epoch epoch{};
};

struct Config {
    std::string lsk, spk, pck, frame = "ITRF93";
    std::string weather, solfsmy, dtcfile;
    std::string epoch_utc, out, mode = "all";
    std::vector<std::string> case_files;
    std::vector<FixtureEpoch> fixture_epochs;
    double duration = 21600, cadence = 30, rtol = 1e-10, max_step = 15;
    double refine_rtol = 1e-12, refine_max_step = 5;
    std::size_t timing_calls = 2000, timing_repeats = 7, timing_propagation_repeats = 3;
    std::vector<std::size_t> timing_threads{1, 2, 4};
    bool ap_history = false;
};

// Named apart from bench::parse, which this file inherits through bench_support.hpp.
Config parse_arguments(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 == argc) throw std::invalid_argument("missing value for " + key);
            return argv[++i];
        };
        if (key == "--lsk") c.lsk = value();
        else if (key == "--spk") c.spk = value();
        else if (key == "--pck") c.pck = value();
        else if (key == "--frame") c.frame = value();
        else if (key == "--weather") c.weather = value();
        else if (key == "--solfsmy") c.solfsmy = value();
        else if (key == "--dtcfile") c.dtcfile = value();
        else if (key == "--epoch-utc") c.epoch_utc = value();
        else if (key == "--case") c.case_files.push_back(value());
        else if (key == "--out") c.out = value();
        else if (key == "--mode") c.mode = value();
        else if (key == "--fixture") {
            const std::string raw = value();
            const auto split = raw.find('=');
            if (split == std::string::npos)
                throw std::invalid_argument("--fixture expects NAME=UTC_ISO, got: " + raw);
            c.fixture_epochs.push_back({raw.substr(0, split), raw.substr(split + 1), {}});
        }
        else if (key == "--duration") c.duration = positive(value(), "--duration");
        else if (key == "--cadence") c.cadence = positive(value(), "--cadence");
        else if (key == "--rtol") c.rtol = positive(value(), "--rtol");
        else if (key == "--max-step") c.max_step = positive(value(), "--max-step");
        else if (key == "--refine-rtol") c.refine_rtol = positive(value(), "--refine-rtol");
        else if (key == "--refine-max-step") c.refine_max_step = positive(value(), "--refine-max-step");
        else if (key == "--timing-calls") c.timing_calls = static_cast<std::size_t>(positive(value(), "--timing-calls"));
        else if (key == "--timing-repeats") c.timing_repeats = static_cast<std::size_t>(positive(value(), "--timing-repeats"));
        else if (key == "--timing-propagation-repeats")
            c.timing_propagation_repeats = static_cast<std::size_t>(positive(value(), "--timing-propagation-repeats"));
        else if (key == "--timing-threads") {
            c.timing_threads.clear();
            std::istringstream rows(value());
            std::string item;
            while (std::getline(rows, item, ',')) {
                if (item.empty()) continue;
                c.timing_threads.push_back(static_cast<std::size_t>(positive(item, "--timing-threads")));
            }
            if (c.timing_threads.empty()) throw std::invalid_argument("--timing-threads needs at least one count");
        }
        else if (key == "--ap-history") c.ap_history = true;
        else throw std::invalid_argument("unknown argument: " + key);
    }
    if (c.mode != "all" && c.mode != "fixtures" && c.mode != "propagate" && c.mode != "timing" &&
        c.mode != "coordinates")
        throw std::invalid_argument("unknown --mode: " + c.mode);
    if (c.lsk.empty() || c.spk.empty())
        throw std::invalid_argument("--lsk and --spk are required; this tool has no synthetic fallback");
    if (c.out.empty()) throw std::invalid_argument("--out is required");
    if (c.epoch_utc.empty())
        throw std::invalid_argument("--epoch-utc is required for every mode");
    // The coordinates report is an observation surface: it needs the ephemeris
    // and orientation kernels only, never a weather dataset or a CASE file.
    const bool coordinates_only = c.mode == "coordinates";
    if (!coordinates_only && (c.weather.empty() || c.solfsmy.empty() || c.dtcfile.empty()))
        throw std::invalid_argument("--weather, --solfsmy and --dtcfile are required");
    const bool propagation = c.mode == "propagate" || c.mode == "all";
    const bool fixtures = c.mode == "fixtures" || c.mode == "all";
    if (propagation && c.case_files.empty())
        throw std::invalid_argument("at least one --case is required for propagation");
    if (fixtures && c.fixture_epochs.empty())
        throw std::invalid_argument("at least one --fixture NAME=UTC_ISO is required for fixtures");
    for (const auto& fixture : c.fixture_epochs)
        if (fixture.name.empty() || fixture.utc.empty())
            throw std::invalid_argument("--fixture needs a non-empty name and UTC epoch");
    return c;
}

// ---------------------------------------------------------------------------
// Output directory. Evidence is never overwritten: an existing, non-empty
// directory is a hard error rather than a silent merge.
// ---------------------------------------------------------------------------

void save(const std::filesystem::path& path, const std::string& json) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << json << '\n';
    if (!out) throw std::runtime_error("write failed for " + path.string());
}

void prepare_output_directory(const std::filesystem::path& directory) {
    if (std::filesystem::exists(directory)) {
        if (!std::filesystem::is_directory(directory))
            throw std::runtime_error("--out exists and is not a directory: " + directory.string());
        if (std::filesystem::directory_iterator(directory) != std::filesystem::directory_iterator())
            throw std::runtime_error("refusing to overwrite non-empty evidence directory: " + directory.string());
        return;
    }
    std::filesystem::create_directories(directory);
}

struct Distribution {
    double minimum = 0, median = 0, maximum = 0, mean = 0;
    std::size_t count = 0;
    static Distribution of(std::vector<double> values) {
        Distribution d;
        if (values.empty()) return d;
        std::sort(values.begin(), values.end());
        d.count = values.size();
        d.minimum = values.front();
        d.maximum = values.back();
        d.mean = std::accumulate(values.begin(), values.end(), 0.0) / double(values.size());
        const auto upper = values.begin() + long(values.size() / 2);
        d.median = values.size() % 2 ? *upper : 0.5 * (upper[-1] + *upper);
        return d;
    }
    std::string json() const {
        Json j;
        j.add("minimum", minimum); j.add("median", median); j.add("maximum", maximum);
        j.add("mean", mean); j.add("count", count);
        return j.str();
    }
};

// ---------------------------------------------------------------------------
// Shared environment
// ---------------------------------------------------------------------------

const char* model_name(leo::atmosphere::Model model) {
    switch (model) {
    case leo::atmosphere::Model::exponential: return "exponential";
    case leo::atmosphere::Model::nrlmsise00: return "nrlmsise00";
    case leo::atmosphere::Model::jb2008: return "jb2008";
    }
    throw std::invalid_argument("unknown model enumerator");
}

// Availability is decided at compile time from the backend definitions, never
// inferred from a thrown exception. A backend that is enabled but then fails
// (missing driver coverage, bad epoch, domain violation) is a run failure, not
// an excuse to report a narrower benchmark.
bool compiled_in(leo::atmosphere::Model model) {
    switch (model) {
    case leo::atmosphere::Model::exponential: return true;
#ifdef LEO_HAS_NRLMSISE00
    case leo::atmosphere::Model::nrlmsise00: return true;
#endif
#ifdef LEO_HAS_JB2008
    case leo::atmosphere::Model::jb2008: return true;
#endif
    default: return false;
    }
}

const char* availability_reason(leo::atmosphere::Model model) {
    if (compiled_in(model)) return "";
    return "backend not compiled into this build; the optional backend CMake option was OFF";
}

std::vector<leo::atmosphere::Model> requested_models() {
    std::vector<leo::atmosphere::Model> models{leo::atmosphere::Model::exponential};
    if (compiled_in(leo::atmosphere::Model::nrlmsise00)) models.push_back(leo::atmosphere::Model::nrlmsise00);
    if (compiled_in(leo::atmosphere::Model::jb2008)) models.push_back(leo::atmosphere::Model::jb2008);
    return models;
}

// Models present in the design but absent from this build. Reported, never
// silently dropped from the design.
std::vector<leo::atmosphere::Model> absent_models() {
    std::vector<leo::atmosphere::Model> models;
    if (!compiled_in(leo::atmosphere::Model::nrlmsise00)) models.push_back(leo::atmosphere::Model::nrlmsise00);
    if (!compiled_in(leo::atmosphere::Model::jb2008)) models.push_back(leo::atmosphere::Model::jb2008);
    return models;
}

struct Environment {
    std::vector<std::filesystem::path> kernels;
    leo::SpiceEphemerisProvider ephemeris;
    leo::atmosphere::WeatherDataset weather;
    double load_seconds = 0;

    explicit Environment(const Config& c) : kernels(), ephemeris(make_kernels(c)), weather(make_weather(c)) {}

    static std::vector<std::filesystem::path> make_kernels(const Config& c) {
        std::vector<std::filesystem::path> k{c.lsk, c.spk};
        if (!c.pck.empty()) k.emplace_back(c.pck);
        return k;
    }
    static leo::atmosphere::WeatherDataset make_weather(const Config& c) {
        return leo::atmosphere::WeatherDataset::load(c.weather, c.solfsmy, c.dtcfile);
    }
};

std::string utc_label(leo::Epoch epoch) {
    const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(epoch);
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << calendar.year << '-'
        << std::setw(2) << calendar.month << '-' << std::setw(2) << calendar.day << 'T'
        << std::setw(2) << int(calendar.seconds_of_day / 3600.0) << ':'
        << std::setw(2) << int(std::fmod(calendar.seconds_of_day / 60.0, 60.0)) << ':'
        << std::setw(2) << int(std::fmod(calendar.seconds_of_day, 60.0)) << 'Z';
    return out.str();
}

// Reported local solar time of a grid point, for interpretation only. The
// models receive their own local solar time from the shared provider; this file
// never re-derives a model input convention.
double grid_local_solar_time_hours(leo::Epoch epoch, double longitude_deg) {
    const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(epoch);
    double hours = calendar.seconds_of_day / 3600.0 + longitude_deg / 15.0;
    hours = std::fmod(hours, 24.0);
    return hours < 0 ? hours + 24.0 : hours;
}

const std::vector<double>& grid_latitudes_deg() {
    static const std::vector<double> v{-80, -60, -30, 0, 30, 60, 80};
    return v;
}
const std::vector<double>& grid_longitudes_deg() {
    // Six-hour spacing sweeps local solar time through a full 24 h cycle.
    static const std::vector<double> v{0, 60, 120, 180, -120, -60};
    return v;
}
std::vector<double> grid_altitudes_km() {
    std::vector<double> v;
    for (double altitude = 200; altitude <= 1000.0001; altitude += 50) v.push_back(altitude);
    return v;
}

// Real geodetic Earth-fixed location -> J2000 position. The parent-owned
// rotation maps a J2000 vector INTO the Earth-fixed frame, so the inverse is
// the transpose of that orthogonal matrix.
leo::InertialVector inertial_from_geodetic(leo::Epoch epoch, double latitude_rad,
                                          double longitude_rad, double altitude_m,
                                          const std::string& frame) {
    const Eigen::Vector3d fixed = leo::atmosphere::earth_fixed(
        leo::atmosphere::Geodetic{latitude_rad, longitude_rad, altitude_m});
    const Eigen::Matrix3d to_fixed =
        leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, frame);
    return InertialVector{to_fixed.transpose() * fixed};
}

// Confirms the assumed direction of earth_fixed_rotation before any grid point
// is built from it. The parent-owned rotation maps J2000 INTO the Earth-fixed
// frame, so the inverse is its transpose; this check fails loudly if that ever
// stops being true, rather than quietly evaluating every fixture at the
// antipode of the requested location.
std::string frame_direction_check(leo::Epoch epoch, const std::string& frame) {
    const Eigen::Matrix3d to_fixed = leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, frame);
    const Eigen::Matrix3d to_j2000 = to_fixed.transpose();
    double worst_position_m = 0, worst_geodetic = 0;
    for (const double latitude : grid_latitudes_deg())
        for (const double longitude : grid_longitudes_deg())
            for (const double altitude_km : {200.0, 600.0, 1000.0}) {
                const leo::atmosphere::Geodetic point{latitude * std::numbers::pi / 180.0,
                                                     longitude * std::numbers::pi / 180.0,
                                                     altitude_km * 1e3};
                const Eigen::Vector3d fixed = leo::atmosphere::earth_fixed(point);
                const Eigen::Vector3d round_trip = to_fixed * (to_j2000 * fixed);
                worst_position_m = std::max(worst_position_m, (round_trip - fixed).norm());
                const auto recovered = leo::atmosphere::geodetic(round_trip);
                worst_geodetic = std::max(worst_geodetic,
                                          std::abs(recovered.altitude_m - point.altitude_m) / point.altitude_m);
            }
    // Orthogonality of the transpose used as an inverse.
    const double orthogonality = (to_j2000 * to_fixed - Eigen::Matrix3d::Identity()).norm();
    Json j;
    j.add("rotation_direction_assumption", "earth_fixed_rotation maps J2000 into the Earth-fixed frame; "
                                          "the inverse used here is its transpose");
    j.add("max_round_trip_position_error_m", worst_position_m);
    j.add("max_round_trip_relative_altitude_error", worst_geodetic);
    j.add("transpose_orthogonality_residual", orthogonality);
    j.add("checked_points", static_cast<double>(grid_latitudes_deg().size() *
                                                 grid_longitudes_deg().size() * 3));
    if (!(worst_position_m < 1e-6 && worst_geodetic < 1e-9 && orthogonality < 1e-12))
        throw std::runtime_error("Earth-fixed rotation direction check failed; grid points would "
                                 "be built at the wrong physical locations");
    return j.str();
}

// Independent coordinate/time inspection for the requested epoch.
//
// This deliberately exposes the raw products so an external oracle (for example
// pyerfa/astropy against IERS conventions) can be compared against them. It is
// an observation surface, not a validation: agreeing with another call to the
// same SPICE wrapper is not evidence that the convention is right.
std::string coordinate_report(leo::Epoch epoch, const std::string& frame) {
    const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(epoch);
    const Eigen::Matrix3d rotation = leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, frame);

    Json j;
    j.add("kind", "coordinate-report");
    j.add("spice_toolkit_version",leo::SpiceEphemerisProvider::toolkit_version());
    j.add("purpose",
          "raw UTC calendar and Earth-fixed rotation products for external comparison against an "
          "independent SOFA/IERS oracle");
    j.add("not_a_validation",
          "these values come from the same CSPICE wrapper used by the propagator; agreement with "
          "another invocation of that wrapper is not independent validation of the convention");
    j.add("frame", frame);
    j.add("epoch_et_seconds", epoch.et_seconds);
    j.add("epoch_definition", "TDB ephemeris seconds past J2000");
    j.add("utc", utc_label(epoch));
    j.add("utc_year", static_cast<double>(calendar.year));
    j.add("utc_month", static_cast<double>(calendar.month));
    j.add("utc_day", static_cast<double>(calendar.day));
    j.add("utc_day_of_year", static_cast<double>(calendar.day_of_year));
    j.add("utc_seconds_of_day", calendar.seconds_of_day);
    j.add("utc_day_length_seconds", calendar.day_length_seconds);
    j.add("utc_leap_second_day", calendar.day_length_seconds != 86400.0);
    j.add("utc_quasi_mjd", calendar.mjd);
    j.add("utc_days_since_1970", static_cast<double>(calendar.days_since_1970));
    j.add("rotation_direction", "acts on SI J2000 positions, producing Earth-fixed positions");
    j.add("rotation_rows_are_j2000_components", true);
    std::ostringstream matrix;
    matrix << std::setprecision(17) << '[';
    for (int row = 0; row < 3; ++row) {
        if (row) matrix << ',';
        matrix << '[' << rotation(row, 0) << ',' << rotation(row, 1) << ',' << rotation(row, 2) << ']';
    }
    matrix << ']';
    j.raw("earth_fixed_rotation", matrix.str());
    // The transpose is what maps an Earth-fixed position back to J2000; both
    // are published so a consumer need not assume the direction.
    std::ostringstream inverse;
    inverse << std::setprecision(17) << '[';
    for (int row = 0; row < 3; ++row) {
        if (row) inverse << ',';
        inverse << '[' << rotation(0, row) << ',' << rotation(1, row) << ',' << rotation(2, row) << ']';
    }
    inverse << ']';
    j.raw("j2000_from_earth_fixed", inverse.str());
    j.add("transpose_orthogonality_residual",
          (rotation.transpose() * rotation - Eigen::Matrix3d::Identity()).norm());
    j.add("determinant", rotation.determinant());

    // Equatorial and polar unit vectors transformed into the Earth-fixed frame,
    // so a consumer can recover the frame's right ascension of the vernal
    // equinox and its pole direction without re-deriving them.
    const Eigen::Vector3d x_j2000{1, 0, 0}, y_j2000{0, 1, 0}, z_j2000{0, 0, 1};
    const Eigen::Vector3d x_fixed = rotation * x_j2000;
    const Eigen::Vector3d y_fixed = rotation * y_j2000;
    const Eigen::Vector3d z_fixed = rotation * z_j2000;
    j.add("j2000_x_axis_in_fixed_ra_rad", std::atan2(x_fixed.y(), x_fixed.x()));
    j.add("j2000_x_axis_in_fixed_dec_rad", std::asin(std::clamp(x_fixed.z(), -1.0, 1.0)));
    j.add("j2000_z_axis_in_fixed_dec_rad", std::asin(std::clamp(z_fixed.z(), -1.0, 1.0)));
    j.add("equinox_offset_from_fixed_x_rad", std::atan2(y_fixed.y(), y_fixed.x()));
    return j.str();
}

std::string numbers_json(const std::vector<double>& values) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    bool first = true;
    for (double value : values) {
        if (!first) out << ',';
        first = false;
        out << value;
    }
    return out.str() + ']';
}

// ---------------------------------------------------------------------------
// fixtures
// ---------------------------------------------------------------------------

void run_fixtures(const Config& c, Environment& env, const std::filesystem::path& out) {
    const auto latitudes = grid_latitudes_deg();
    const auto longitudes = grid_longitudes_deg();
    const auto altitudes = grid_altitudes_km();

    struct Row {
        std::string fixture, model;
        double latitude_deg, longitude_deg, altitude_km, local_solar_time_hours;
        double density_kg_m3, temperature_k, exospheric_temperature_k;
        bool has_temperatures = false, has_composition = false;
        std::array<double, 8> number_density_m3{};
    };
    std::vector<Row> rows;
    std::map<std::string, std::size_t> evaluated;

    for (const auto& fixture : c.fixture_epochs) {
        const auto epoch = fixture.epoch;
        for (const auto model : requested_models()) {
            // No exception handling here: a compiled-in backend that fails is a
            // run failure. Recording it and continuing would silently publish a
            // benchmark over fewer models than were asked for.
            const leo::atmosphere::DirectAtmosphere atmosphere(
                model, env.ephemeris, env.weather, c.frame, c.ap_history);
            std::size_t count = 0;
            for (const double latitude : latitudes) {
                for (const double longitude : longitudes) {
                    for (const double altitude : altitudes) {
                        const auto position = inertial_from_geodetic(
                            epoch, latitude * std::numbers::pi / 180.0,
                            longitude * std::numbers::pi / 180.0, altitude * 1e3, c.frame);
                        const auto result = atmosphere.evaluate(epoch, position);
                        if (!std::isfinite(result.density_kg_m3) || result.density_kg_m3 < 0)
                            throw std::runtime_error("nonfinite or negative density");
                        Row row;
                        row.fixture = fixture.name;
                        row.model = model_name(model);
                        row.latitude_deg = latitude;
                        row.longitude_deg = longitude;
                        row.altitude_km = altitude;
                        row.local_solar_time_hours = grid_local_solar_time_hours(epoch, longitude);
                        row.density_kg_m3 = result.density_kg_m3;
                        row.temperature_k = result.temperature_k;
                        row.exospheric_temperature_k = result.exospheric_temperature_k;
                        // Native availability is reported by the model, not
                        // inferred from a zero, so an absent output is never
                        // presented as a measured zero.
                        row.has_temperatures = result.has_temperatures;
                        row.has_composition = result.has_constituents;
                        row.number_density_m3 = result.number_density_m3;
                        rows.push_back(std::move(row));
                        ++count;
                    }
                }
            }
            evaluated[model_name(model)] += count;
        }
    }

    Json j;
    j.add("kind", "atmospheric-fixtures");
    j.add("protocol",
          "Direct reference evaluation at matched physical locations, epochs and input "
          "conditions. Every density is a fresh model call; no caching, interpolation, "
          "surrogate or trajectory precomputation is used.");
    j.add("frame", c.frame);
    j.add("earth_ellipsoid_a_m", leo::atmosphere::wgs84_a_m);
    j.add("earth_flattening", leo::atmosphere::wgs84_f);
    j.add("geodetic_or_geocentric", "grid points are geodetic; each model receives its own "
                                    "published latitude convention from the shared provider");
    j.add("ap_history_enabled", c.ap_history);
    if (!c.fixture_epochs.empty())
        j.raw("frame_direction_check",
              frame_direction_check(c.fixture_epochs.front().epoch, c.frame));
    j.add("altitude_range_km", std::string("200 to 1000 inclusive in 50 km steps"));
    j.raw("latitude_values_deg", numbers_json(latitudes));
    j.raw("longitude_values_deg", numbers_json(longitudes));
    j.add("local_solar_time_note",
          "reported as UTC + longitude/15 h for interpretation only; model local solar time "
          "comes from the shared provider");
    j.add("temperature_note",
          "temperature_k is the temperature at the requested altitude; exospheric_temperature_k "
          "is the exospheric temperature. NRLMSISE-00 and JB2008 name these differently.");
    j.add("number_density_order",
          std::string(number_density_names[0]) + "," + number_density_names[1] + "," +
              number_density_names[2] + "," + number_density_names[3] + "," +
              number_density_names[4] + "," + number_density_names[5] + "," +
              number_density_names[6] + "," + number_density_names[7]);
    j.add("kelvin_offset", kelvin_offset);
    j.add("weather_inputs", "immutable observed archive records; hashes in manifest.json");
    std::ostringstream epochs; epochs << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < c.fixture_epochs.size(); ++index) {
        if (index) epochs << ',';
        Json f;
        f.add("name", c.fixture_epochs[index].name);
        f.add("utc", c.fixture_epochs[index].utc);
        f.add("epoch_et_seconds", c.fixture_epochs[index].epoch.et_seconds);
        f.add("calendar_utc", utc_label(c.fixture_epochs[index].epoch));
        const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(c.fixture_epochs[index].epoch);
        f.add("day_of_year", static_cast<double>(calendar.day_of_year));
        f.add("mjd_utc", calendar.mjd);
        epochs << f.str();
    }
    epochs << ']';
    j.raw("fixture_epochs", epochs.str());

    std::ostringstream series; series << std::setprecision(17) << '[';
    bool first = true;
    for (const auto& row : rows) {
        if (!first) series << ',';
        first = false;
        // Fixed row width: every field is always present so the array is
        // positional. A field the model does not natively produce is marked by
        // its has_* flag being 0 and must not be read as a measured value.
        series << '[' << '"' << row.fixture << '"' << ',' << '"' << row.model << '"' << ','
               << row.latitude_deg << ',' << row.longitude_deg << ',' << row.altitude_km << ','
               << row.local_solar_time_hours << ',' << row.density_kg_m3 << ','
               << (row.has_temperatures ? 1 : 0) << ','
               << (row.has_temperatures ? row.temperature_k : 0.0) << ','
               << (row.has_temperatures ? row.exospheric_temperature_k : 0.0) << ','
               << (row.has_composition ? 1 : 0);
        for (double value : row.number_density_m3)
            series << ',' << (row.has_composition ? value : 0.0);
        series << ']';
    }
    series << ']';
    j.raw("samples", series.str());
    j.raw("samples_fields",
          R"(["fixture","model","latitude_deg","longitude_deg","altitude_km",)"
          R"("local_solar_time_hours","density_kg_m3","has_temperatures",)"
          R"("temperature_k","exospheric_temperature_k","has_number_density",)"
          R"("He_m3","O_m3","N2_m3","O2_m3","Ar_m3","H_m3","N_m3","O_anomalous_m3"])");
    j.add("sample_count", static_cast<double>(rows.size()));

    // Cross-model ratios at exactly matched grid points.
    using AltitudeMap = std::map<double, std::map<double, std::map<std::string, double>>>;
    std::map<std::string, std::map<double, AltitudeMap>> index;
    for (const auto& row : rows)
        index[row.fixture][row.latitude_deg][row.longitude_deg][row.altitude_km][row.model] =
            row.density_kg_m3;
    std::map<std::string, std::vector<double>> ratio_values;
    std::map<std::string, std::size_t> ratio_unusable;
    for (const auto& fixture : index)
        for (const auto& latitude : fixture.second)
            for (const auto& longitude : latitude.second)
                for (const auto& entry : longitude.second) {
                    const auto& density = entry.second;
                    const auto reference = density.find("exponential");
                    if (reference == density.end() || reference->second <= 0) continue;
                    for (const auto& [model, value] : density) {
                        if (model == "exponential") continue;
                        const std::string key = model + "_over_exponential";
                        if (value > 0 && std::isfinite(value)) ratio_values[key].push_back(value / reference->second);
                        else ++ratio_unusable[key];
                    }
                }
    std::ostringstream ratios; ratios << std::setprecision(17) << '{';
    bool ratio_first = true;
    for (const auto& [key, values] : ratio_values) {
        if (!ratio_first) ratios << ',';
        ratio_first = false;
        const auto d = Distribution::of(values);
        ratios << '"' << key << "\":{" << "\"median\":" << d.median << ",\"minimum\":" << d.minimum
               << ",\"maximum\":" << d.maximum << ",\"mean\":" << d.mean
               << ",\"points\":" << values.size()
               << ",\"unusable_points\":" << ratio_unusable[key] << '}';
    }
    ratios << '}';
    j.raw("density_ratios_to_exponential", ratios.str());
    j.add("ratio_note",
          "model-to-model differences are model-form differences, not evidence that either "
          "implementation is wrong");
    std::ostringstream evaluated_json; evaluated_json << '{';
    bool evaluated_first = true;
    for (const auto& [model, count] : evaluated) {
        if (!evaluated_first) evaluated_json << ',';
        evaluated_first = false;
        evaluated_json << '"' << model << "\":" << count;
    }
    evaluated_json << '}';
    j.raw("evaluated_points", evaluated_json.str());
    // Absent models are absent at build time only. A compiled-in backend that
    // fails would have aborted the run above, so this list cannot contain a
    // runtime failure dressed up as unavailability.
    std::ostringstream absent_json; absent_json << '[';
    bool absent_first = true;
    for (const auto model : absent_models()) {
        if (!absent_first) absent_json << ',';
        absent_first = false;
        absent_json << "{\"model\":\"" << model_name(model) << "\",\"compiled_in\":false,\"reason\":\""
                    << availability_reason(model) << "\"}";
    }
    absent_json << ']';
    j.raw("models_not_compiled_in", absent_json.str());
    j.add("failure_policy",
          "a compiled-in backend that fails aborts the run; only a build-time absent backend "
          "is reported here, and no model is ever substituted for it");
    save(out / "fixtures.json", j.str());
}

// ---------------------------------------------------------------------------
// Propagation
// ---------------------------------------------------------------------------

// Model selection identical to the CASE-A/B/C numerical-audit "full" scenario:
// J2 gravity, conical SRP with torque, Sentman drag with torque, albedo and
// gravity-gradient torque. Only the atmosphere model changes between runs.
EarthDynamics case_dynamics(const experiment::Case& spec) {
    const Spacecraft base = spec.craft();
    std::vector<Surface> surfaces(base.surfaces().begin(), base.surfaces().end());
    const Spacecraft craft(base.mass_kg(), base.inertia(), std::move(surfaces));
    EnvironmentalModels models;
    models.srp = {true, true};
    models.drag = {true, true};
    models.drag_model = DragModel::sentman;
    models.albedo = {true, true};
    models.gravity_gradient = true;
    return EarthDynamics(EarthGravity(Earth{}, true), craft, models);
}

// K + U_central + U_J2 for the implemented fixed-J2000-Z Earth gravity law.
// Translational diagnostic only; not a closed orbit/attitude Hamiltonian.
double matched_specific_energy(const Earth& earth, const State& state) {
    const double radius = norm(state.r);
    const double z_over_r = state.r.z() / radius;
    const double ratio = earth.equatorial_radius_m / radius;
    const double central = -earth.mu_m3_s2 / radius;
    const double j2 = earth.mu_m3_s2 / radius * earth.j2 * ratio * ratio * (3 * z_over_r * z_over_r - 1) / 2;
    return 0.5 * dot(state.v, state.v) + central + j2;
}

template <class Rhs>
Trajectory reference_rkf78(const Rhs& rhs, const State& initial, const std::vector<double>& times,
                            const IntegrationOptions& o, std::size_t& attempts,
                            std::size_t& rejected, leo::Epoch epoch) {
    // Independent numerical method: different tableau, embedded order and
    // controller from the production Dopri5 loop. Field scaling sets physical
    // absolute tolerances so the refinement is tighter on every component.
    using namespace boost::numeric::odeint;
    const Packed scale{o.position_atol_m, o.position_atol_m, o.position_atol_m,
                       o.velocity_atol_m_s, o.velocity_atol_m_s, o.velocity_atol_m_s,
                       o.quaternion_atol, o.quaternion_atol, o.quaternion_atol, o.quaternion_atol,
                       o.omega_atol_rad_s, o.omega_atol_rad_s, o.omega_atol_rad_s};
    Packed z = leo::detail::pack(validated_state(initial));
    for (std::size_t i = 0; i < 13; ++i) z[i] /= scale[i];
    auto system = [&](const Packed& scaled, Packed& derivative, double t) {
        Packed physical{};
        for (std::size_t i = 0; i < 13; ++i) physical[i] = scaled[i] * scale[i];
        derivative = leo::detail::pack(rhs({t}, leo::detail::unpack(physical)));
        for (std::size_t i = 0; i < 13; ++i) derivative[i] /= scale[i];
    };
    runge_kutta_fehlberg78<Packed> stepper;
    array_algebra algebra;
    default_error_checker<double, array_algebra, default_operations> checker(1., o.rtol);
    default_step_adjuster<double, double> adjuster(o.max_step_s);
    Trajectory out{epoch, {{ElapsedTime{0}, validated_state(initial)}}};
    double t = 0, h = o.initial_step_s;
    for (std::size_t i = 1; i < times.size(); ++i) {
        while (t < times[i]) {
            const double remaining = times[i] - t;
            h = std::min({h, o.max_step_s, remaining});
            if (h <= 0 || t + h == t || ++attempts > o.max_attempts)
                throw std::runtime_error("RKF78 cannot advance");
            Packed dz{}, trial{}, error{};
            system(z, dz, t);
            stepper.do_step(system, z, dz, t, trial, h, error);
            const double norm = checker.error(algebra, z, dz, error, h);
            if (!std::isfinite(norm)) throw std::runtime_error("nonfinite RKF78 error");
            if (norm <= 1) {
                z = trial;
                t = h == remaining ? times[i] : t + h;
                h = adjuster.increase_step(h, norm, stepper.stepper_order());
            } else {
                ++rejected;
                h = adjuster.decrease_step(h, norm, stepper.error_order());
            }
        }
        Packed physical{};
        for (std::size_t k = 0; k < 13; ++k) physical[k] = z[k] * scale[k];
        State s = leo::detail::unpack(physical);
        s.q = normalize_quaternion(s.q);
        out.samples.push_back({ElapsedTime{times[i]}, s});
    }
    return out;
}

std::vector<double> sample_labels(const Config& c) {
    std::vector<double> labels{0};
    for (double t = c.cadence; t < c.duration; t += c.cadence) labels.push_back(t);
    labels.push_back(c.duration);
    return labels;
}

struct RunRecord {
    std::string case_id, case_file, model, method;
    bool legacy = false;
    leo::Epoch epoch{};
    std::size_t rhs_calls = 0, atmosphere_calls = 0, attempts = 0, rejected = 0;
    double wall_seconds = 0;
    Trajectory trajectory;
    std::vector<double> density, drag_acceleration, drag_torque, altitude;
    std::vector<double> cumulative_drag_work, cumulative_non_gravitational_work;
    std::vector<double> matched_energy_change, attitude_angle, angular_momentum_norm;
    std::vector<double> angular_speed, total_torque_norm;
    double energy_work_residual = 0, max_quaternion_norm_error = 0;
    double cumulative_drag_work_final = 0, cumulative_non_gravitational_work_final = 0;
    double matched_energy_change_final = 0;
};

RunRecord execute(const Config& c, Environment& env, const experiment::Case& spec,
                  leo::atmosphere::Model model, const std::string& method, leo::Epoch epoch) {
    const auto dynamics = case_dynamics(spec);
    const State start = spec.start();
    const Earth earth;
    const auto atmosphere = leo::atmosphere::DirectAtmosphere(
        model, env.ephemeris, env.weather, c.frame, c.ap_history);
    const auto labels = sample_labels(c);

    std::size_t atmosphere_calls = 0;
    auto source = [&](ElapsedTime t, const State& s) {
        const leo::Epoch at{epoch.et_seconds + t.seconds};
        leo::EnvironmentSnapshot snapshot;
        snapshot.epoch = at;
        // Existing atmospheric corotation assumption, unchanged.
        snapshot.atmosphere_angular_velocity_rad_s = InertialVector{0, 0, 7.292115e-5};
        const auto sun = env.ephemeris.geometric_state(leo::sun_naif_id, at);
        snapshot.sun_position_m = sun.position_m;
        snapshot.illumination = illumination_factor(EclipseModel::conical, s.r, sun.position_m,
                                                     earth.equatorial_radius_m);
        // One fresh reference-model call per environment query.
        const auto result = atmosphere.evaluate(at, s.r);
        if (!std::isfinite(result.density_kg_m3) || result.density_kg_m3 < 0)
            throw std::runtime_error("nonfinite or negative atmosphere density");
        snapshot.density_kg_m3 = result.density_kg_m3;
        ++atmosphere_calls;
        return snapshot;
    };

    IntegrationOptions options = tolerances();
    options.rtol = c.rtol;
    options.max_step_s = c.max_step;
    options.initial_step_s = std::min(1.0, c.max_step);
    // The existing IntegrationOptions default, stated explicitly rather than
    // raised. Every other numerical default is likewise left at the project
    // value; only the two named refinement values differ, and only for the RKF78 run.
    options.max_attempts = IntegrationOptions{}.max_attempts;
    if (method == "rkf78") {
        options.rtol = c.refine_rtol;
        options.max_step_s = c.refine_max_step;
    }

    std::size_t rhs_calls = 0, attempts = 0, rejected = 0;
    auto system = [&](ElapsedTime t, const State& s) {
        ++rhs_calls;
        return dynamics(t, s, source(t, s));
    };

    RunRecord record;
    const auto timer = Clock::now();
    if (method == "dopri") {
        record.trajectory = propagate(system, start, epoch, labels, options);
    } else if (method == "rkf78") {
        record.trajectory = reference_rkf78(system, start, labels, options, attempts, rejected, epoch);
    } else {
        throw std::invalid_argument("unknown propagation method: " + method);
    }
    record.wall_seconds = elapsed(timer);
    record.case_id = spec.id;
    record.case_file = spec.file;
    record.legacy = spec.legacy;
    record.model = model_name(model);
    record.method = method;
    record.epoch = epoch;
    record.rhs_calls = rhs_calls;
    record.atmosphere_calls = atmosphere_calls;
    record.attempts = attempts;
    record.rejected = rejected;

    // Post-run diagnostics. This pass re-evaluates the loads and the atmosphere
    // at the reported output times; it is diagnostic only and never feeds the
    // integrated state. Orbital, load and attitude diagnostics come from the
    // existing leo::analysis routines rather than from a second implementation.
    const double mass = dynamics.spacecraft().mass_kg();
    const auto& inertia = dynamics.spacecraft().inertia();
    const auto initial = record.trajectory.samples.front().state;
    const double initial_energy = matched_specific_energy(earth, initial);
    const auto initial_quaternion = initial.q;
    const double initial_angular_momentum = norm(body_to_inertial(initial_quaternion,
                                                                  inertia.multiply(initial.omega)));
    std::vector<leo::EnvironmentSnapshot> snapshots;
    snapshots.reserve(record.trajectory.samples.size());
    for (const auto& sample : record.trajectory.samples)
        snapshots.push_back(source(sample.t, sample.state));
    const auto loads = leo::analysis::environmental_history(record.trajectory, dynamics, snapshots);
    const auto attitude = leo::analysis::attitude_history(record.trajectory, dynamics.spacecraft(), loads);
    const auto orbit = leo::analysis::orbital_history(record.trajectory, earth);

    double last_drag_power = 0, last_non_gravitational_power = 0, last_time = 0;
    double cumulative_drag = 0, cumulative_non_gravitational = 0, max_residual = 0;
    const std::size_t count = record.trajectory.samples.size();
    record.density.reserve(count);
    record.drag_acceleration.reserve(count);
    record.drag_torque.reserve(count);
    record.altitude.reserve(count);
    record.cumulative_drag_work.reserve(count);
    record.cumulative_non_gravitational_work.reserve(count);
    record.matched_energy_change.reserve(count);
    record.attitude_angle.reserve(count);
    record.angular_momentum_norm.reserve(count);
    record.angular_speed.reserve(count);
    record.total_torque_norm.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& state = record.trajectory.samples[index].state;
        const double time = record.trajectory.samples[index].t.seconds;
        const auto& contributions = loads[index].contributions;
        const double drag_power = dot(contributions.drag.force_n, state.v);
        const double non_gravitational_power = dot(contributions.non_gravitational.force_n, state.v);
        const double matched = matched_specific_energy(earth, state) - initial_energy;
        if (index > 0) {
            const double dt = time - last_time;
            cumulative_drag += 0.5 * (drag_power + last_drag_power) * dt;
            cumulative_non_gravitational += 0.5 * (non_gravitational_power + last_non_gravitational_power) * dt;
            last_time = time;
        }
        max_residual = std::max(max_residual,
                                std::abs(matched - cumulative_non_gravitational / mass));
        last_drag_power = drag_power;
        last_non_gravitational_power = non_gravitational_power;
        const double qnorm = std::sqrt(state.q.w * state.q.w + state.q.x * state.q.x +
                                       state.q.y * state.q.y + state.q.z * state.q.z);
        record.max_quaternion_norm_error = std::max(record.max_quaternion_norm_error, std::abs(qnorm - 1));
        record.density.push_back(snapshots[index].density_kg_m3);
        record.drag_acceleration.push_back(norm(contributions.drag.force_n) / mass);
        record.drag_torque.push_back(norm(contributions.drag.torque_nm));
        record.total_torque_norm.push_back(attitude[index].torque_magnitude_nm);
        record.angular_speed.push_back(attitude[index].angular_speed_rad_s);
        record.altitude.push_back(orbit[index].altitude_m);
        record.cumulative_drag_work.push_back(cumulative_drag);
        record.cumulative_non_gravitational_work.push_back(cumulative_non_gravitational);
        record.matched_energy_change.push_back(matched);
        record.attitude_angle.push_back(angle(initial_quaternion, state.q));
        record.angular_momentum_norm.push_back(
            norm(body_to_inertial(state.q, inertia.multiply(state.omega))) - initial_angular_momentum);
    }
    record.energy_work_residual = max_residual;
    record.cumulative_drag_work_final = cumulative_drag;
    record.cumulative_non_gravitational_work_final = cumulative_non_gravitational;
    record.matched_energy_change_final = record.matched_energy_change.back();
    return record;
}

void run_propagation(const Config& c, Environment& env, const std::filesystem::path& out) {
    const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
    std::map<std::string, RunRecord> nominal;

    for (const auto& case_file : c.case_files) {
        const auto spec = experiment::load(case_file);
        for (const auto model : requested_models()) {
            const std::string tag = spec.id + "_" + model_name(model);
            // A compiled-in backend that fails aborts the run. Reporting a
            // partial comparison as if it were a complete one would substitute a
            // narrower benchmark for the requested one.
            RunRecord record = execute(c, env, spec, model, "dopri", epoch);

            Checksum checksum;
            const double mass = case_dynamics(spec).spacecraft().mass_kg();
            std::ostringstream series;
            series << std::setprecision(17) << '[';
            for (std::size_t index = 0; index < record.trajectory.samples.size(); ++index) {
                const auto& sample = record.trajectory.samples[index];
                checksum.add(sample.state);
                if (index) series << ',';
                series << '[' << sample.t.seconds;
                const auto packed = leo::detail::pack(sample.state);
                for (double value : packed) series << ',' << value;
                series << ',' << record.altitude[index] << ',' << record.density[index] << ','
                       << record.drag_acceleration[index] << ',' << record.drag_torque[index] << ','
                       << record.cumulative_drag_work[index] << ','
                       << record.cumulative_non_gravitational_work[index] << ','
                       << record.matched_energy_change[index] << ',' << record.attitude_angle[index]
                       << ',' << record.angular_momentum_norm[index] << ','
                       << record.angular_speed[index] << ',' << record.total_torque_norm[index] << ']';
            }
            series << ']';

            // Independent numerical refinement of the same differential
            // equations. No convergence threshold is asserted for any case;
            // CASE-C in particular is a sensitive stress test.
            RunRecord refined;
            bool refinement_ok = true;
            std::string refinement_error;
            try {
                refined = execute(c, env, spec, model, "rkf78", epoch);
            } catch (const std::exception& error) {
                refinement_ok = false;
                refinement_error = error.what();
            }

            const auto& dynamics = case_dynamics(spec);
            const auto inertia = experiment::tensor(dynamics.spacecraft());
            Json j;
            j.add("kind", "propagation-run");
            j.add("case_id", record.case_id);
            j.add("case_file", record.case_file);
            j.add("case_legacy_benchmark_factory", record.legacy);
            j.add("atmosphere_model", record.model);
            j.add("nominal_integrator", "dopri5 (production leo::propagate)");
            j.add("refinement_integrator", "boost odeint controlled RKF7(8), independent tableau");
            j.add("epoch_et_seconds", record.epoch.et_seconds);
            j.add("epoch_utc", utc_label(record.epoch));
            j.add("epoch_utc_input", c.epoch_utc);
            j.add("duration_s", c.duration);
            j.add("output_cadence_s", c.cadence);
            j.add("output_sample_count", static_cast<double>(record.trajectory.samples.size()));
            j.add("rtol", c.rtol);
            j.add("max_step_s", c.max_step);
            j.add("position_atol_m", 1e-3);
            j.add("velocity_atol_m_s", 1e-6);
            j.add("quaternion_atol", 1e-12);
            j.add("omega_atol_rad_s", 1e-12);
            j.add("refine_rtol", c.refine_rtol);
            j.add("refine_max_step_s", c.refine_max_step);
            j.add("max_attempts", static_cast<double>(IntegrationOptions{}.max_attempts));
            j.add("max_attempts_note",
                  "existing IntegrationOptions default, unchanged; no numerical default was "
                  "raised for these experiments");
            j.add("gravity", "J2 enabled; symmetry axis fixed J2000 +Z");
            j.add("drag_model", "sentman");
            j.add("solar_illumination_model", "conical");
            j.add("atmosphere_rotation_rad_s", std::string("0,0,7.292115e-5 (existing corotation assumption)"));
            j.add("rhs_calls", record.rhs_calls);
            j.add("atmosphere_calls_during_integration", record.atmosphere_calls);
            j.raw("integration_attempts","null"); // production step counters are not exposed
            j.raw("rejected_steps","null");
            j.add("wall_seconds", record.wall_seconds);
            j.add("mass_kg", mass);
            j.add("surface_count", static_cast<double>(dynamics.spacecraft().surfaces().size()));
            j.add("surface_area_m2", std::accumulate(
                dynamics.spacecraft().surfaces().begin(), dynamics.spacecraft().surfaces().end(), 0.0,
                [](double sum, const Surface& s) { return sum + s.area_m2(); }));
            j.vector("inertia_diagonal_kg_m2", BodyVector{inertia(0, 0), inertia(1, 1), inertia(2, 2)});
            j.add("density_min_kg_m3", *std::min_element(record.density.begin(), record.density.end()));
            j.add("density_max_kg_m3", *std::max_element(record.density.begin(), record.density.end()));
            j.add("density_mean_kg_m3", std::accumulate(record.density.begin(), record.density.end(), 0.0) /
                                            double(record.density.size()));
            j.add("drag_acceleration_max_m_s2", *std::max_element(record.drag_acceleration.begin(),
                                                                  record.drag_acceleration.end()));
            j.add("drag_torque_max_nm", *std::max_element(record.drag_torque.begin(), record.drag_torque.end()));
            j.add("altitude_min_m", *std::min_element(record.altitude.begin(), record.altitude.end()));
            j.add("altitude_max_m", *std::max_element(record.altitude.begin(), record.altitude.end()));
            j.add("cumulative_drag_work_j", record.cumulative_drag_work_final);
            j.add("cumulative_drag_specific_work_j_kg", record.cumulative_drag_work_final / mass);
            j.add("cumulative_non_gravitational_work_j", record.cumulative_non_gravitational_work_final);
            j.add("matched_specific_energy_change_j_kg", record.matched_energy_change_final);
            j.add("energy_work_residual_j_kg", record.energy_work_residual);
            j.add("energy_work_residual_note",
                  "matched K+Ucentral+UJ2 change minus output-sampled non-gravitational work; "
                  "contains quadrature, integration and model error, not only numerical error");
            j.add("max_quaternion_norm_error", record.max_quaternion_norm_error);
            j.add("attitude_angle_from_initial_final_rad", record.attitude_angle.back());
            j.add("final_angular_momentum_norm_error_kg_m2_s", record.angular_momentum_norm.back());
            j.add("trajectory_checksum", checksum.str());
            const auto& final = record.trajectory.samples.back().state;
            j.vector("final_position_m", final.r);
            j.vector("final_velocity_m_s", final.v);
            j.vector("final_omega_body_rad_s", final.omega);
            std::ostringstream quaternion;
            quaternion << std::setprecision(17) << '[' << final.q.w << ',' << final.q.x << ','
                       << final.q.y << ',' << final.q.z << ']';
            j.raw("final_quaternion", quaternion.str());

            if (refinement_ok) {
                Stats position, velocity, attitude, body_rate, density_ratio, radius;
                double drag_work_difference = 0, energy_difference = 0, max_energy_difference = 0;
                Checksum refined_checksum;
                for (std::size_t index = 0; index < record.trajectory.samples.size(); ++index) {
                    refined_checksum.add(refined.trajectory.samples[index].state);
                    const auto& a = record.trajectory.samples[index].state;
                    const auto& b = refined.trajectory.samples[index].state;
                    position.add(norm(a.r - b.r));
                    velocity.add(norm(a.v - b.v));
                    attitude.add(angle(a.q, b.q));
                    body_rate.add(norm(a.omega - b.omega));
                    radius.add(std::abs(norm(a.r) - norm(b.r)));
                    // Two independent integrations sample slightly different states,
                    // so their sampled densities differ for integration reasons
                    // alone. This is a numerical difference, not a model change.
                    if (refined.density[index] > 0)
                        density_ratio.add(std::abs(record.density[index] - refined.density[index]) /
                                          refined.density[index]);
                    if (index + 1 == record.trajectory.samples.size()) {
                        drag_work_difference = std::abs(record.cumulative_drag_work_final -
                                                        refined.cumulative_drag_work_final);
                        energy_difference = std::abs(record.matched_energy_change_final -
                                                     refined.matched_energy_change_final);
                    }
                    max_energy_difference = std::max(
                        max_energy_difference,
                        std::abs(record.matched_energy_change[index] - refined.matched_energy_change[index]));
                }
                j.add("refinement_succeeded", true);
                j.add("refinement_rhs_calls", refined.rhs_calls);
                j.add("refinement_atmosphere_calls", refined.atmosphere_calls);
                j.add("refinement_rejected_steps", refined.rejected);
                j.add("refinement_wall_seconds", refined.wall_seconds);
                j.raw("refinement_position_difference_m", position.json());
                j.raw("refinement_velocity_difference_m_s", velocity.json());
                j.raw("refinement_attitude_difference_rad", attitude.json());
                j.raw("refinement_body_rate_difference_rad_s", body_rate.json());
                j.raw("refinement_radius_difference_m", radius.json());
                j.raw("refinement_relative_density_difference", density_ratio.json());
                j.add("refinement_cumulative_drag_work_difference_j", drag_work_difference);
                j.add("refinement_matched_energy_difference_j_kg", energy_difference);
                j.add("refinement_max_matched_energy_difference_j_kg", max_energy_difference);
                j.add("refinement_trajectory_checksum", refined_checksum.str());
                j.add("refinement_note",
                      "differences measure numerical integration only: identical differential "
                      "equations, identical drivers, different tableau and tolerances. No "
                      "threshold is asserted for any case, including CASE-C.");
                // If the refinement reproduced the nominal run bit-for-bit the two
                // methods would not be independent evidence of anything, so the
                // coincidence is reported instead of passing as convergence.
                j.add("refinement_bitwise_identical_to_nominal",
                      refined_checksum.str() == checksum.str());
            } else {
                j.add("refinement_succeeded", false);
                j.add("refinement_error", refinement_error);
            }
            j.raw("samples", series.str());
            j.raw("samples_fields",
                  R"(["time_s","x_m","y_m","z_m","vx_m_s","vy_m_s","vz_m_s","qw","qx","qy","qz",)"
                  R"("omega_x_rad_s","omega_y_rad_s","omega_z_rad_s","altitude_m","density_kg_m3",)"
                  R"("drag_acceleration_m_s2","drag_torque_nm","cumulative_drag_work_j",)"
                  R"("cumulative_non_gravitational_work_j","matched_specific_energy_change_j_kg",)"
                  R"("attitude_angle_from_initial_rad","angular_momentum_norm_error_kg_m2_s",)"
                  R"("angular_speed_rad_s","total_torque_magnitude_nm"])");
            save(out / "propagation" / (tag + ".json"), j.str());
            nominal[tag] = std::move(record);
        }
    }

    // Model-to-model differences at exactly matched output times.
    std::ostringstream comparison;
    comparison << std::setprecision(17) << '{';
    bool case_first = true;
    for (const auto& case_file : c.case_files) {
        const auto spec = experiment::load(case_file);
        std::map<std::string, std::size_t> present;
        for (const auto& [tag, record] : nominal)
            if (record.case_id == spec.id) present[record.model] = record.trajectory.samples.size();
        if (present.empty()) continue;
        if (!case_first) comparison << ',';
        case_first = false;
        comparison << '"' << spec.id << "\":{";
        std::ostringstream runs;
        runs << '{';
        bool run_first = true;
        for (const auto& [tag, record] : nominal) {
            if (record.case_id != spec.id) continue;
            if (!run_first) runs << ',';
            run_first = false;
            runs << '"' << record.model << "\":{\"epoch_utc\":\"" << utc_label(record.epoch)
                 << "\",\"samples\":" << static_cast<double>(record.trajectory.samples.size())
                 << ",\"density_min_kg_m3\":"
                 << *std::min_element(record.density.begin(), record.density.end())
                 << ",\"density_max_kg_m3\":"
                 << *std::max_element(record.density.begin(), record.density.end())
                 << ",\"density_mean_kg_m3\":"
                 << (std::accumulate(record.density.begin(), record.density.end(), 0.0) /
                     double(record.density.size()))
                 << ",\"drag_acceleration_max_m_s2\":"
                 << *std::max_element(record.drag_acceleration.begin(), record.drag_acceleration.end())
                 << ",\"drag_torque_max_nm\":"
                 << *std::max_element(record.drag_torque.begin(), record.drag_torque.end())
                 << ",\"cumulative_drag_work_j\":" << record.cumulative_drag_work_final
                 << ",\"cumulative_non_gravitational_work_j\":"
                 << record.cumulative_non_gravitational_work_final
                 << ",\"matched_specific_energy_change_j_kg\":"
                 << record.matched_energy_change_final << '}';
        }
        runs << '}';
        comparison << "\"runs\":" << runs.str() << ",\"pairs\":{";
        std::vector<std::string> models;
        for (const auto& entry : present) models.push_back(entry.first);
        std::sort(models.begin(), models.end());
        bool pair_first = true;
        for (std::size_t a = 0; a + 1 < models.size(); ++a) {
            for (std::size_t b = a + 1; b < models.size(); ++b) {
                const auto& left = nominal[spec.id + "_" + models[a]];
                const auto& right = nominal[spec.id + "_" + models[b]];
                if (left.trajectory.samples.size() != right.trajectory.samples.size()) continue;
                Stats position, velocity, attitude, body_rate, relative_density;
                double work_ratio = 0;
                for (std::size_t index = 0; index < left.trajectory.samples.size(); ++index) {
                    const auto& x = left.trajectory.samples[index].state;
                    const auto& y = right.trajectory.samples[index].state;
                    position.add(norm(x.r - y.r));
                    velocity.add(norm(x.v - y.v));
                    attitude.add(angle(x.q, y.q));
                    body_rate.add(norm(x.omega - y.omega));
                    if (right.density[index] > 0)
                        relative_density.add(std::abs(left.density[index] - right.density[index]) /
                                             right.density[index]);
                }
                if (right.cumulative_drag_work_final != 0)
                    work_ratio = left.cumulative_drag_work_final / right.cumulative_drag_work_final;
                if (!pair_first) comparison << ',';
                pair_first = false;
                comparison << '"' << models[a] << "_vs_" << models[b] << "\":{"
                           << "\"position_difference_m\":" << position.json()
                           << ",\"velocity_difference_m_s\":" << velocity.json()
                           << ",\"attitude_difference_rad\":" << attitude.json()
                           << ",\"body_rate_difference_rad_s\":" << body_rate.json()
                           << ",\"relative_density_difference\":" << relative_density.json()
                           << ",\"cumulative_drag_work_ratio\":" << work_ratio << '}';
            }
        }
        comparison << "}}";
    }
    comparison << '}';

    Json summary;
    summary.add("kind", "propagation-comparison");
    summary.add("protocol",
                "Matched CASE-A/B/C reruns. Spacecraft, initial conditions, force and torque "
                "laws, gravity, radiation models, integrator configuration, SPICE ephemerides "
                "and output timestamps are identical across models; only the atmosphere model "
                "and its required external drivers change.");
    summary.add("epoch_utc", c.epoch_utc);
    summary.add("epoch_et_seconds", epoch.et_seconds);
    summary.add("duration_s", c.duration);
    summary.add("output_cadence_s", c.cadence);
    summary.add("historical_evidence_note",
                "the numerical-audit evidence used epoch ET=0; this rerun uses a real "
                "historical epoch, so results are not equivalent with those older runs");
    summary.raw("comparison", comparison.str());
    std::ostringstream missing; missing << '[';
    for (const auto model : absent_models()) {
        if (missing.tellp() > 1) missing << ',';
        missing << '"' << model_name(model) << '"';
    }
    missing << ']';
    summary.raw("models_not_compiled_in", missing.str());
    summary.add("failure_policy",
                "a compiled-in backend that fails aborts the run; every run listed here is "
                "complete for its case and model");
    summary.add("difference_categories",
                "separated per run: refinement_* fields are numerical integration differences; "
                "*_vs_* fields are model-form differences at identical inputs; changes caused "
                "by space-weather inputs are covered by the fixture epochs");
    save(out / "propagation-comparison.json", summary.str());
}

// ---------------------------------------------------------------------------
// timing
// ---------------------------------------------------------------------------

#ifdef LEO_HAS_NRLMSISE00
// The full switch vector actually in force, recorded so a reader can reproduce
// the exact model configuration instead of assuming library defaults.
std::array<int, 24> ap_history_switches() {
    auto switches = leo::atmosphere::Nrlmsise00::standard_switches();
    switches[9] = -1; // Ap history selector: -1 means use the seven 3-hourly values
    return switches;
}
#endif

// Raw native evaluation: the upstream model call itself, with the model-native
// input built once, outside every timed loop.
//
// This is deliberately a separate measurement from the provider timings.
// DirectAtmosphere::evaluate includes the CSPICE Earth-fixed rotation, the
// geodetic conversion and the space-weather record lookup, so its wall time is
// the full acquisition path, not the cost of one reference-model evaluation.
// Both numbers are published, labelled, and never mixed.
struct NativeInput {
    bool available = false;
    std::string reason;
    leo::atmosphere::NrlmsiseInput nrlmsise{};
    leo::atmosphere::Jb2008Input jb2008{};
    // Two altitude conventions, kept separate because the models genuinely
    // differ: the empirical models take geodetic ellipsoidal height, the
    // exponential profile is spherical and takes |r| - Re.
    double geodetic_altitude_m = 0;
    double spherical_altitude_m = 0;
    std::array<int, 24> switches{};
};

// Builds the same native input the provider would build for this epoch and
// position, so the raw and provider timings describe the same physical state.
// The construction is done once and reused: this is input preparation, not a
// density cache, and it is excluded from every reported timing.
NativeInput build_native_input(leo::Epoch epoch, const State& state, const Config& c,
                               const Environment& env) {
    NativeInput native;
    const Eigen::Matrix3d to_fixed = leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, c.frame);
    const Eigen::Vector3d fixed = to_fixed * state.r.as_eigen();
    const auto g = leo::atmosphere::geodetic(fixed);
    native.geodetic_altitude_m = g.altitude_m;
    native.spherical_altitude_m = state.r.as_eigen().norm() - leo::atmosphere::wgs84_a_m;
    // The calendar is only needed by whichever empirical drivers are compiled in;
    // a build with neither must not pay for, or warn about, a conversion it
    // never uses.
#if defined(LEO_HAS_NRLMSISE00) || defined(LEO_HAS_JB2008)
    const auto utc = leo::SpiceEphemerisProvider::utc_calendar(epoch);
#else
    (void)env;
#endif
#ifdef LEO_HAS_NRLMSISE00
    native.switches = c.ap_history ? ap_history_switches() : leo::atmosphere::Nrlmsise00::standard_switches();
    auto in = env.weather.nrlmsise(utc, c.ap_history);
    constexpr double deg = 180 / std::numbers::pi;
    in.latitude_deg = g.latitude_rad * deg;
    in.longitude_deg = g.longitude_rad * deg;
    in.altitude_km = g.altitude_m / 1000;
    in.local_solar_time_hours = std::fmod(utc.seconds_of_day / 3600 + in.longitude_deg / 15 + 24, 24);
    native.nrlmsise = in;
#endif
#ifdef LEO_HAS_JB2008
    {
        auto in = env.weather.jb2008(utc);
        const auto sun = env.ephemeris.geometric_state(leo::sun_naif_id, epoch).position_m;
        const Eigen::Vector3d fixed_sun = to_fixed * sun.as_eigen();
        in.sun_ra_dec_rad = {std::atan2(fixed_sun.y(), fixed_sun.x()),
                             std::atan2(fixed_sun.z(), std::hypot(fixed_sun.x(), fixed_sun.y()))};
        in.position_ra_lat_alt = {g.longitude_rad,
                                  std::atan2(fixed.z(), std::hypot(fixed.x(), fixed.y())),
                                  g.altitude_m / 1000};
        native.jb2008 = in;
    }
#endif
#ifndef LEO_HAS_NRLMSISE00
#ifndef LEO_HAS_JB2008
    native.reason = "no empirical backend is compiled into this build";
#endif
#endif
    native.available = compiled_in(leo::atmosphere::Model::nrlmsise00) ||
                       compiled_in(leo::atmosphere::Model::jb2008);
    return native;
}

// Times one call. Clock-read overhead is retained and can dominate exponential
// evaluation; batched measurements provide a separate uncorrected cross-check.
struct TimedCall {
    double nanoseconds = 0;
    double value = 0;
};

template<class Call> TimedCall timed_call(Call&& call) {
    const auto start = Clock::now();
    const double value = call();
    return {std::chrono::duration<double, std::nano>(Clock::now() - start).count(), value};
}

void run_timing(const Config& c, Environment& env, const std::filesystem::path& out) {
    const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
    const auto probe = experiment::load(c.case_files.at(0));
    const auto state = probe.start();
    const auto labels = sample_labels(c);

    // ---- setup costs, reported separately from every per-evaluation timing ----
    std::vector<std::filesystem::path> kernel_paths{c.lsk, c.spk};
    if (!c.pck.empty()) kernel_paths.emplace_back(c.pck);
    std::ostringstream kernels; kernels << '[';
    for (std::size_t index = 0; index < kernel_paths.size(); ++index) {
        if (index) kernels << ',';
        Json entry;
        entry.add("path", kernel_paths[index].string());
        entry.add("sha256", sha256_file(kernel_paths[index]));
        entry.add("bytes", static_cast<double>(std::filesystem::file_size(kernel_paths[index])));
        kernels << entry.str();
    }
    kernels << ']';

    Json data;
    data.add("kind", "setup-cost");
    data.add("note",
             "one-time dependency setup, measured separately and never included in a "
             "per-evaluation or per-propagation timing");
    data.raw("kernels", kernels.str());
    for (const auto& [label, path] : std::vector<std::pair<std::string, std::string>>{
             {"cssi_weather_archive", c.weather}, {"solfsmy", c.solfsmy}, {"dtcfile", c.dtcfile}}) {
        Json entry;
        entry.add("path", path);
        entry.add("sha256", sha256_file(path));
        entry.add("bytes", static_cast<double>(std::filesystem::file_size(path)));
        data.raw(label.c_str(), entry.str());
    }

    // Timed on its own, deliberately separate from the measurement datasets. This
// is the reported data-loading cost, not a per-evaluation cost.
    const auto weather_timer = Clock::now();
    const leo::atmosphere::WeatherDataset timed_load =
        leo::atmosphere::WeatherDataset::load(c.weather, c.solfsmy, c.dtcfile);
    const double weather_seconds = elapsed(weather_timer);
    data.add("weather_dataset_load_seconds", weather_seconds);
    data.add("weather_dataset_note",
             "an explicit second load, timed on its own and discarded; the datasets used for "
             "measurement are the ones already resident in Environment");
    data.add("ephemeris_setup_note",
             "the CSPICE kernel pool is furnished once by the shared provider at construction; "
             "that furnsh cost is process setup, not part of any evaluation");
    (void)timed_load;

    // ---- per-model measurement ----
    std::ostringstream models; models << std::setprecision(17) << '[';
    std::ostringstream propagation_rows; propagation_rows << std::setprecision(17) << '[';
    bool model_first = true, row_first = true;

    for (const auto model : requested_models()) {
        const std::string name = model_name(model);
        const auto construct_timer = Clock::now();
        // No exception handling: a compiled-in backend that fails to construct
        // is a run failure, not a missing measurement.
        auto atmosphere = std::make_unique<leo::atmosphere::DirectAtmosphere>(
            model, env.ephemeris, env.weather, c.frame, c.ap_history);
        const double construct_seconds = elapsed(construct_timer);
        const auto* pointer = atmosphere.get();

        // Warm-up outside every measured batch so first-touch page faults and
        // one-time kernel effects do not enter the distribution.
        for (std::size_t warm = 0; warm < 200; ++warm) (void)pointer->evaluate(epoch, state.r);

        std::vector<double> batch_seconds, per_call_nanoseconds, per_call_results;
        for (std::size_t repeat = 0; repeat < c.timing_repeats; ++repeat) {
            const auto timer = Clock::now();
            double last = 0;
            for (std::size_t call = 0; call < c.timing_calls; ++call)
                last = pointer->evaluate(epoch, state.r).density_kg_m3;
            const double seconds = elapsed(timer);
            batch_seconds.push_back(seconds);
            per_call_nanoseconds.push_back(seconds / double(c.timing_calls) * 1e9);
            if (!std::isfinite(last) || last < 0)
                throw std::runtime_error("timed evaluation produced an invalid density");
            per_call_results.push_back(last);
        }
        // Determinism check: repeated direct evaluation at one fixed input.
        const auto first_result = pointer->evaluate(epoch, state.r);
        double repeat_error = 0;
        for (int check = 0; check < 8; ++check) {
            const auto again = pointer->evaluate(epoch, state.r);
            repeat_error = std::max(repeat_error,
                                    std::abs(again.density_kg_m3 - first_result.density_kg_m3));
        }

        // ---- provider-path per-call distribution ----
        // Each call is timed individually, so this is a real per-evaluation
        // distribution and not a repeated batch average. Warm-up ran above and
        // is excluded.
        std::vector<double> provider_call_nanoseconds, provider_call_densities;
        for (std::size_t repeat = 0; repeat < c.timing_repeats; ++repeat)
            for (std::size_t call = 0; call < c.timing_calls; ++call) {
                const auto timed = timed_call(
                    [&] { return pointer->evaluate(epoch, state.r).density_kg_m3; });
                if (!std::isfinite(timed.value) || timed.value < 0)
                    throw std::runtime_error("timed evaluation produced an invalid density");
                provider_call_nanoseconds.push_back(timed.nanoseconds);
                provider_call_densities.push_back(timed.value);
            }

        // ---- raw native model-call distribution ----
        // The upstream reference call alone, with its native input prepared once
        // before the loops. No coordinate transform, no calendar conversion and
        // no weather lookup is inside these measurements.
        const NativeInput native = build_native_input(epoch, state, c, env);
        std::vector<double> native_nanoseconds, native_densities;
        std::ostringstream native_config; native_config << std::setprecision(17) << '{';
        if (compiled_in(leo::atmosphere::Model::nrlmsise00) ||
            compiled_in(leo::atmosphere::Model::jb2008)) {
            native_config << "\"latitude_deg\":" << native.nrlmsise.latitude_deg
                          << ",\"longitude_deg\":" << native.nrlmsise.longitude_deg
                          << ",\"geodetic_altitude_m\":" << native.geodetic_altitude_m
                          << ",\"spherical_altitude_m\":" << native.spherical_altitude_m
                          << ",\"altitude_convention\":"
                             "\"empirical models take geodetic ellipsoidal height; the "
                             "exponential profile takes |r| - Re\""
                          << ",\"altitude_km\":" << native.nrlmsise.altitude_km
                          << ",\"local_solar_time_hours\":" << native.nrlmsise.local_solar_time_hours
                          << ",\"year\":" << native.nrlmsise.year
                          << ",\"day_of_year\":" << native.nrlmsise.day_of_year
                          << ",\"utc_seconds\":" << native.nrlmsise.utc_seconds
                          << ",\"f107\":" << native.nrlmsise.f107
                          << ",\"f107_average\":" << native.nrlmsise.f107_average
                          << ",\"ap\":" << native.nrlmsise.ap
                          << ",\"ap_history_enabled\":" << (c.ap_history ? 1 : 0);
            if (c.ap_history) {
                native_config << ",\"ap_history\":[";
                for (std::size_t k = 0; k < native.nrlmsise.ap_history.size(); ++k) {
                    if (k) native_config << ',';
                    native_config << native.nrlmsise.ap_history[k];
                }
                native_config << ']';
            }
#ifdef LEO_HAS_NRLMSISE00
            native_config << ",\"switches\":[";
            for (std::size_t k = 0; k < native.switches.size(); ++k) {
                if (k) native_config << ',';
                native_config << native.switches[k];
            }
            native_config << ']';
#endif
#ifdef LEO_HAS_JB2008
            const auto& jb = native.jb2008;
            native_config << ",\"jb_mjd_utc\":" << jb.mjd_utc
                          << ",\"jb_sun_ra_rad\":" << jb.sun_ra_dec_rad[0]
                          << ",\"jb_sun_dec_rad\":" << jb.sun_ra_dec_rad[1]
                          << ",\"jb_sat_ra_rad\":" << jb.position_ra_lat_alt[0]
                          << ",\"jb_sat_lat_rad\":" << jb.position_ra_lat_alt[1]
                          << ",\"jb_sat_alt_km\":" << jb.position_ra_lat_alt[2]
                          << ",\"jb_f10\":" << jb.f10 << ",\"jb_f10_average\":" << jb.f10_average
                          << ",\"jb_s10\":" << jb.s10 << ",\"jb_s10_average\":" << jb.s10_average
                          << ",\"jb_m10\":" << jb.m10 << ",\"jb_m10_average\":" << jb.m10_average
                          << ",\"jb_y10\":" << jb.y10 << ",\"jb_y10_average\":" << jb.y10_average
                          << ",\"jb_dstdtc_k\":" << jb.dstdtc_k;
#endif
        }
        native_config << '}';

#ifdef LEO_HAS_NRLMSISE00
        if (model == leo::atmosphere::Model::nrlmsise00) {
            const leo::atmosphere::Nrlmsise00 reference(native.switches);
            for (std::size_t warm = 0; warm < 200; ++warm) (void)reference.evaluate(native.nrlmsise);
            for (std::size_t repeat = 0; repeat < c.timing_repeats; ++repeat)
                for (std::size_t call = 0; call < c.timing_calls; ++call) {
                    const auto timed = timed_call(
                        [&] { return reference.evaluate(native.nrlmsise).density_kg_m3; });
                    native_nanoseconds.push_back(timed.nanoseconds);
                    native_densities.push_back(timed.value);
                }
        }
#endif
#ifdef LEO_HAS_JB2008
        if (model == leo::atmosphere::Model::jb2008) {
            const leo::atmosphere::Jb2008 reference;
            for (std::size_t warm = 0; warm < 200; ++warm) (void)reference.evaluate(native.jb2008);
            for (std::size_t repeat = 0; repeat < c.timing_repeats; ++repeat)
                for (std::size_t call = 0; call < c.timing_calls; ++call) {
                    const auto timed = timed_call(
                        [&] { return reference.evaluate(native.jb2008).density_kg_m3; });
                    native_nanoseconds.push_back(timed.nanoseconds);
                    native_densities.push_back(timed.value);
                }
        }
#endif
        if (model == leo::atmosphere::Model::exponential) {
            // The existing baseline measured the same way: the density call itself.
            // The exponential profile is spherical, so it takes the spherical
            // altitude |r| - Re, exactly as the provider now does, not the
            // geodetic height the empirical models use.
            const leo::ExponentialAtmosphere reference(1.0);
            const double altitude = native.spherical_altitude_m;
            for (std::size_t warm = 0; warm < 200; ++warm) (void)reference.density_kg_m3(altitude);
            for (std::size_t repeat = 0; repeat < c.timing_repeats; ++repeat)
                for (std::size_t call = 0; call < c.timing_calls; ++call) {
                    const auto timed = timed_call(
                        [&] { return reference.density_kg_m3(altitude); });
                    native_nanoseconds.push_back(timed.nanoseconds);
                    native_densities.push_back(timed.value);
                }
        }
        double native_vs_provider_difference = 0;
        for (std::size_t k = 0; k < native_densities.size() && k < provider_call_densities.size(); ++k)
            native_vs_provider_difference = std::max(
                native_vs_provider_difference,
                std::abs(native_densities[k] - provider_call_densities[k]));

        std::ostringstream threads; threads << std::setprecision(17) << '[';
        bool thread_first = true;
        for (const std::size_t thread_count : c.timing_threads) {
            const std::size_t per_thread =
                std::max<std::size_t>(1, c.timing_calls / std::max<std::size_t>(1, thread_count));
            const auto timer = Clock::now();
            std::vector<std::thread> workers;
            std::vector<std::string> worker_errors(thread_count);
            for (std::size_t index = 0; index < thread_count; ++index)
                workers.emplace_back([&, index] {
                    try {
                        for (std::size_t call = 0; call < per_thread; ++call)
                            (void)pointer->evaluate(epoch, state.r);
                    } catch (const std::exception& error) {
                        worker_errors[index] = error.what();
                    }
                });
            for (auto& worker : workers) worker.join();
            for (const auto& message : worker_errors)
                if (!message.empty())
                    throw std::runtime_error("concurrent evaluation failed: " + message);
            const double seconds = elapsed(timer);
            const double total = double(per_thread) * double(thread_count);
            if (!thread_first) threads << ',';
            thread_first = false;
            threads << "{\"threads\":" << thread_count << ",\"calls_per_thread\":" << per_thread
                    << ",\"total_calls\":" << total << ",\"wall_seconds\":" << seconds
                    << ",\"evaluations_per_second\":" << (total / seconds) << '}';
        }
        threads << ']';

        // ---- full coupled propagation, repeated ----
        std::vector<double> walls, nanoseconds_per_rhs, nanoseconds_per_call;
        std::vector<double> rhs_counts, atmosphere_counts;
        const auto dynamics = case_dynamics(probe);
        for (std::size_t repeat = 0; repeat < c.timing_propagation_repeats; ++repeat) {
            std::size_t calls_atmosphere = 0, calls_rhs = 0;
            auto source = [&](ElapsedTime t, const State& s) {
                const leo::Epoch at{epoch.et_seconds + t.seconds};
                leo::EnvironmentSnapshot snapshot;
                snapshot.epoch = at;
                snapshot.atmosphere_angular_velocity_rad_s = InertialVector{0, 0, 7.292115e-5};
                const auto sun = env.ephemeris.geometric_state(leo::sun_naif_id, at);
                snapshot.sun_position_m = sun.position_m;
                snapshot.illumination = illumination_factor(EclipseModel::conical, s.r,
                                                             sun.position_m, Earth{}.equatorial_radius_m);
                const auto result = pointer->evaluate(at, s.r);
                if (!std::isfinite(result.density_kg_m3) || result.density_kg_m3 < 0)
                    throw std::runtime_error("timed propagation got an invalid density");
                snapshot.density_kg_m3 = result.density_kg_m3;
                ++calls_atmosphere;
                return snapshot;
            };
            auto system = [&](ElapsedTime t, const State& s) {
                ++calls_rhs;
                return dynamics(t, s, source(t, s));
            };
            IntegrationOptions options = tolerances();
            options.rtol = c.rtol;
            options.max_step_s = c.max_step;
            options.initial_step_s = std::min(1.0, c.max_step);
            options.max_attempts = IntegrationOptions{}.max_attempts;
            const auto timer = Clock::now();
            const auto trajectory = propagate(system, state, epoch, labels, options);
            const double seconds = elapsed(timer);
            if (trajectory.samples.size() != labels.size())
                throw std::runtime_error("timed propagation lost output samples");
            Checksum checksum;
            for (const auto& sample : trajectory.samples) checksum.add(sample.state);
            const double rate_rhs = calls_rhs ? seconds / double(calls_rhs) * 1e9 : 0.0;
            const double rate_call = calls_atmosphere ? seconds / double(calls_atmosphere) * 1e9 : 0.0;
            walls.push_back(seconds);
            nanoseconds_per_rhs.push_back(rate_rhs);
            nanoseconds_per_call.push_back(rate_call);
            rhs_counts.push_back(double(calls_rhs));
            atmosphere_counts.push_back(double(calls_atmosphere));

            if (!row_first) propagation_rows << ',';
            row_first = false;
            Json row;
            row.add("atmosphere_model", name);
            row.add("case_id", probe.id);
            row.add("repeat", static_cast<double>(repeat));
            row.add("duration_s", c.duration);
            row.add("output_cadence_s", c.cadence);
            row.add("output_samples", static_cast<double>(trajectory.samples.size()));
            row.add("wall_seconds", seconds);
            row.add("rhs_calls", static_cast<double>(calls_rhs));
            row.add("atmosphere_calls", static_cast<double>(calls_atmosphere));
            row.add("nanoseconds_per_rhs", rate_rhs);
            row.add("nanoseconds_per_atmosphere_call", rate_call);
            row.add("trajectory_checksum", checksum.str());
            propagation_rows << row.str();
        }

        if (!model_first) models << ',';
        model_first = false;
        Json entry;
        entry.add("atmosphere_model", name);
        entry.add("available", true);
        entry.add("provider_construction_seconds", construct_seconds);
        entry.add("evaluations_per_batch", static_cast<double>(c.timing_calls));
        entry.add("batches", static_cast<double>(c.timing_repeats));

        // Raw native model call: the upstream reference evaluation alone.
        Json raw_native;
        raw_native.add("measures",
                       "the upstream reference model call itself; the model-native input is "
                       "built once, before every timed loop, and is reused unchanged");
        raw_native.add("excludes",
                       "Earth-fixed rotation, geodetic conversion, UTC calendar conversion and "
                       "space-weather record lookup, all of which are inside the provider-path "
                       "timings below");
        raw_native.add("units", "nanoseconds per evaluation");
        raw_native.add("timed_calls", static_cast<double>(native_nanoseconds.size()));
        raw_native.add("warmup_calls", static_cast<double>(200));
        raw_native.raw("nanoseconds_per_native_evaluation",
                       Distribution::of(native_nanoseconds).json());
        raw_native.raw("native_density_kg_m3", Distribution::of(native_densities).json());
        raw_native.raw("native_input", native_config.str());
        if (!native.available)
            raw_native.add("reason", native.reason.empty()
                                          ? "no empirical backend is compiled into this build"
                                          : native.reason);
        raw_native.add("native_vs_provider_max_density_difference_kg_m3",
                       native_vs_provider_difference);
        raw_native.add("native_vs_provider_note",
                       "the two paths describe the same physical state and should agree; a "
                       "non-zero difference means they did not describe the same evaluation");
        entry.raw("raw_native", raw_native.str());

        // Full acquisition path: provider evaluate, including SPICE and weather.
        Json provider_path;
        provider_path.add("measures",
                          "DirectAtmosphere::evaluate, including the CSPICE Earth-fixed rotation, "
                          "the geodetic conversion and the space-weather record lookup");
        provider_path.add("units", "nanoseconds per evaluation");
        provider_path.add("timed_calls", static_cast<double>(provider_call_nanoseconds.size()));
        provider_path.raw("nanoseconds_per_provider_evaluation",
                          Distribution::of(provider_call_nanoseconds).json());
        provider_path.raw("provider_density_kg_m3", Distribution::of(provider_call_densities).json());
        provider_path.add("note",
                          "distinct from raw_native; the difference between the two is the "
                          "acquisition overhead");
        entry.raw("provider_path", provider_path.str());

        // Batched totals, retained for cross-checking the per-call distributions.
        entry.raw("batch_seconds", Distribution::of(batch_seconds).json());
        entry.raw("nanoseconds_per_direct_evaluation", Distribution::of(per_call_nanoseconds).json());
        entry.raw("direct_evaluation_density_kg_m3", Distribution::of(per_call_results).json());
        entry.add("batched_note",
                  "these are repeated batch means; the per-call distributions above are the "
                  "individual measurements and the two should be consistent");
        entry.add("repeated_evaluation_density_difference_kg_m3", repeat_error);
        entry.raw("concurrency", threads.str());
        entry.add("concurrency_note",
                  "the NRLMSISE-00 and JB2008 adapters serialise native calls on a process-wide "
                  "mutex because the upstream C and Fortran models hold mutable global state; "
                  "the exponential baseline performs no synchronisation, so its scaling is a "
                  "property of this harness and not of the existing production path");
        entry.add("fixed_input_note",
                  "one fixed epoch and spacecraft position, so this measures direct model "
                  "evaluation rather than varying driver-data lookup");
        entry.add("density_kg_m3", first_result.density_kg_m3);
        entry.add("has_temperatures", first_result.has_temperatures);
        entry.add("has_constituents", first_result.has_constituents);
        if (first_result.has_temperatures) {
            entry.add("temperature_k", first_result.temperature_k);
            entry.add("exospheric_temperature_k", first_result.exospheric_temperature_k);
        } else {
            // The model does not natively produce temperatures; a zero here would
            // read as a measured value.
            entry.raw("temperature_k", "null");
            entry.raw("exospheric_temperature_k", "null");
            entry.add("temperature_absent_reason",
                      "this model has no native temperature output at this state");
        }
        entry.raw("propagation_wall_seconds", Distribution::of(walls).json());
        entry.raw("propagation_nanoseconds_per_rhs", Distribution::of(nanoseconds_per_rhs).json());
        entry.raw("propagation_nanoseconds_per_atmosphere_call",
                  Distribution::of(nanoseconds_per_call).json());
        entry.raw("propagation_rhs_calls", Distribution::of(rhs_counts).json());
        entry.raw("propagation_atmosphere_calls", Distribution::of(atmosphere_counts).json());
        models << entry.str();
    }
    models << ']';
    propagation_rows << ']';

    Json j;
    j.add("kind", "performance-baseline");
    j.add("protocol",
          "Uncached direct baseline. Every density is a fresh reference-model evaluation. "
          "One-time setup is reported separately and excluded from per-evaluation timings. "
          "No caching, interpolation, surrogate or precomputation is present in these "
          "measurements, and none was introduced to improve them.");
    j.add("epoch_utc", c.epoch_utc);
    j.add("epoch_et_seconds", epoch.et_seconds);
    j.add("case_id", probe.id);
    j.add("duration_s", c.duration);
    j.add("output_cadence_s", c.cadence);
    j.add("rtol", c.rtol);
    j.add("max_step_s", c.max_step);
    j.add("absolute_tolerances",
          "project defaults: position 1e-3 m, velocity 1e-6 m/s, quaternion 1e-12, "
          "body rate 1e-12 rad/s");
    j.raw("setup", data.str());
    j.raw("models", models.str());
    j.raw("propagation_repeats", propagation_rows.str());
    j.add("raw_repeats_note",
          "propagation_repeats holds every individual repeat: none is discarded and no "
          "percentile is dropped for being slow");
    save(out / "performance-baseline.json", j.str());
}

} // namespace

int main(int argc, char** argv) {
    try {
        Config c = parse_arguments(argc, argv);
        const std::filesystem::path out(c.out);
        prepare_output_directory(out);

        // The coordinates report needs ephemeris and orientation kernels only;
        // it deliberately does not load a weather dataset.
        if (c.mode == "coordinates") {
            std::vector<std::filesystem::path> kernels{c.lsk, c.spk};
            if (!c.pck.empty()) kernels.emplace_back(c.pck);
            const leo::SpiceEphemerisProvider ephemeris(kernels);
            const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
            save(out / "coordinates.json", coordinate_report(epoch, c.frame));
            std::cout << coordinate_report(epoch, c.frame) << '\n';
            return 0;
        }

        const auto load_timer = Clock::now();
        Environment env(c);
        // UTC conversion requires the LSK owned by env.ephemeris to be loaded first.
        for(auto& fixture:c.fixture_epochs)
            fixture.epoch=leo::SpiceEphemerisProvider::epoch_from_utc(fixture.utc);
        const double environment_seconds = elapsed(load_timer);

        Json manifest;
        manifest.add("kind", "run-manifest-partial");
        manifest.add("mode", c.mode);
        manifest.add("frame", c.frame);
        manifest.add("earth_fixed_ellipsoid_a_m", leo::atmosphere::wgs84_a_m);
        manifest.add("earth_fixed_flattening", leo::atmosphere::wgs84_f);
        manifest.add("kernel_setup_and_weather_load_seconds", environment_seconds);
        manifest.add("epoch_utc", c.epoch_utc);
        manifest.raw("case_files", [&] {
            std::ostringstream text; text << '[';
            for (std::size_t index = 0; index < c.case_files.size(); ++index) {
                if (index) text << ',';
                text << '"' << c.case_files[index] << '"';
            }
            return text.str() + ']';
        }().c_str());
        manifest.add("propagation_duration_s", c.duration);
        manifest.add("output_cadence_s", c.cadence);
        manifest.add("rtol", c.rtol);
        manifest.add("max_step_s", c.max_step);
        manifest.add("absolute_tolerances",
                     "project defaults: position 1e-3 m, velocity 1e-6 m/s, quaternion 1e-12, "
                     "body rate 1e-12 rad/s");
        manifest.add("threading", "single-threaded measurements; concurrent columns are measured, not assumed");
        manifest.add("integrators", "dopri5 nominal, boost odeint controlled RKF7(8) refinement");
        save(out / "environment.json", manifest.str());

        const bool fixtures = c.mode == "fixtures" || c.mode == "all";
        const bool propagation = c.mode == "propagate" || c.mode == "all";
        const bool timing = c.mode == "timing" || c.mode == "all";
        if (fixtures) run_fixtures(c, env, out);
        if (propagation) run_propagation(c, env, out);
        if (timing) run_timing(c, env, out);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}