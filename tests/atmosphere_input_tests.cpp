// Behavioral tests for the UTC / coordinate / immutable-weather input pipeline.
//
// Oracles are deliberately independent of the code under test:
//   * WGS84 fixtures restate the defining ellipsoid parameters published by
//     NIMA TR8350.2 (semi-major axis, inverse flattening). Every derived constant
//     here is recomputed rather than copied from the library's arithmetic.
//   * Ap and F10.7 expectations are predicted from an absolute 3-hour bin index
//     (day*8 + bin) instead of replaying the loader's wrap-around loop.
//   * UTC expectations are derived by hand from the leap-second definition
//     (TT = TAI + 32.184 s, TT - UTC = 36 s through 2016 and 37 s from
//     2017-01-01) plus the J2000 reference epoch, never from a second
//     utc_calendar call.
//   * Greenwich angle and sidereal turn use the IAU 1982 values, not SPICE.
//   * Space-weather fixtures are synthetic fixed-field files written to a private
//     temporary directory. Nothing is downloaded and no acquired archive is read.
//
// Checks that need real SPICE kernels take their paths from the environment and
// SKIP explicitly when absent, following tests/spice_tests.cpp.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "leo/atmosphere/coordinates.hpp"
#include "leo/atmosphere/weather.hpp"
#include "leo/environment/spice.hpp"
#include <Eigen/Core>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
using namespace leo;
using namespace leo::atmosphere;
namespace {

constexpr double kPi = std::numbers::pi;
// WGS84 defining parameters (NIMA TR8350.2). The library publishes a and f; all
// derived constants below are restated here from the published values so this
// file states the model independently rather than mirroring its arithmetic.
constexpr double kA = 6378137.0;
constexpr double kF = 1.0/298.257223563;
constexpr double kB = kA * (1 - kF);
constexpr double kE2 = kF * (2 - kF);

// Ellipsoid geometry at 45 deg geodetic latitude. The prime vertical radius of
// curvature there is a/sqrt(1 - e^2/2) = 6388838.290121148 m.
constexpr double kLat45 = kPi / 4;
const double kN45 = kA / std::sqrt(1 - kE2 * 0.5);
const double kX45 = kN45 * std::cos(kLat45);
const double kZ45 = kN45 * (1 - kE2) * std::sin(kLat45);
const double kR45 = std::hypot(kX45, kZ45);
// tan(geocentric) = (1 - e^2) tan(geodetic) fixes the geocentric latitude.
const double kGeocentricLat45 = std::atan((1 - kE2) * std::tan(kLat45));
// Metres of ellipsoid surface per radian of latitude, used only to convert an
// angle tolerance into a ground distance.
constexpr double kMetresPerRadian = kA;

// MJD of 1970-01-01T00:00 UTC; the library builds quasi-MJD from this constant.
constexpr double kMjdEpoch = 40587.0;

const char* env_path(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : nullptr;
}

constexpr long long serial(int y, unsigned m, unsigned d) {
    return std::chrono::sys_days{std::chrono::year{y} / std::chrono::month{m}
        / std::chrono::day{d}}.time_since_epoch().count();
}
// Reverses serial(): the calendar date of a day index since 1970-01-01.
std::chrono::year_month_day calendar_of(long long days) {
    return std::chrono::year_month_day{
        std::chrono::sys_days{std::chrono::days{static_cast<int>(days)}}};
}
int day_of_year(int year, unsigned month, unsigned day) {
    return static_cast<int>(serial(year, month, day) - serial(year, 1, 1)) + 1;
}

// Mirrors the field semantics of SpiceEphemerisProvider::utc_calendar so the
// weather tests can build a query without loading a kernel.
UtcCalendar utc(int year, unsigned month, unsigned day, double seconds_of_day,
                double day_length_seconds = 86400.0) {
    const long long days = serial(year, month, day);
    return {year, static_cast<int>(month), static_cast<int>(day),
            day_of_year(year, month, day), days, seconds_of_day, day_length_seconds,
            kMjdEpoch + static_cast<double>(days) + seconds_of_day / day_length_seconds};
}

// Every driver value is a pure function of the absolute day or bin index, so a
// test can predict any lag or averaging window without replaying loader logic.
constexpr long long kBase = serial(2016, 12, 20);
constexpr long long kFirst = kBase - 20;   // 2016-11-30
constexpr long long kLast = kBase + 15;    // 2017-01-04

int day_index(long long days) { return static_cast<int>(days - kFirst); }
int ap_bin_value(long long days, int bin) { return 50 + day_index(days) * 8 + bin; }
int ap_daily_value(long long days) { return 200 + day_index(days); }
double f107_observed(long long days) { return 100.0 + day_index(days); }
double f107_observed_center81(long long days) { return 120.0 + day_index(days); }
double solar_proxy(long long days, int column) { return 100.0 + day_index(days) * 10 + column; }
// The eight SOLFSMY columns in file order, named by what the model consumes.
enum SolarColumn { kF10 = 0, kF10Average = 1, kS10 = 2, kS10Average = 3,
                   kM10 = 4, kM10Average = 5, kY10 = 6, kY10Average = 7 };
// Both code strings mark every index as measured, derived or composite and stay
// inside the loader's accepted set; neither is a gap fill or a neighbour mean.
const char* kSolarCodes = "1B11";
const char* kSolarCodesAlt = "1E11";

// Absolute 3-hour bin index: an independent restatement of "current bin, then
// -3, -6 and -9 h, then the 12..33 h and 36..57 h means".
double ap_at_offset(long long days, int bin, int offset_hours) {
    const long long absolute = days * 8 + bin - offset_hours;
    return ap_bin_value(absolute / 8, static_cast<int>(absolute % 8));
}

// Appending fixed-width fields exactly as the CSSI and DTCFILE formats define
// them, so a fixture row is byte-identical in shape to an archived row.
void append(std::string& out, const char* format, auto... args) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), format, args...);
    out += buffer;
}

// One CSSI v1.2 row, 130 columns wide:
//   yy mm dd BSRN ND | 8x Kp | Sum | 8x Ap | Avg Ap | Cp | C9 | ISN | F10.7 | Q
//        | Ctr81 | Lst81 | F10.7obs | Ctr81obs | Lst81obs
std::string cssi_row(long long days, int quality, double center81 = std::numeric_limits<double>::quiet_NaN()) {
    using namespace std::chrono;
    const year_month_day date{calendar_of(days)};
    std::string row;
    append(row, "%4d%3u%3u%5d%3d", static_cast<int>(date.year()),
        static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()), 1700, 1);
    for (int kp = 0; kp < 8; ++kp) append(row, "%3d", 3);
    append(row, "%4d", 24);
    for (int bin = 0; bin < 8; ++bin) append(row, "%4d", ap_bin_value(days, bin));
    append(row, "%4d", ap_daily_value(days));
    append(row, "%4.1f%2d%4d", 1.1, 5, 236);
    append(row, "%6.1f%2d", f107_observed(days), quality);
    // Adjusted Ctr81 and Lst81 columns; the loader must ignore both.
    append(row, "%6.1f%6.1f", 250.0, 240.0);
    append(row, "%6.1f%6.1f%6.1f", f107_observed(days),
        std::isnan(center81) ? f107_observed_center81(days) : center81, 230.0);
    return row;
}

struct CssiOptions {
    int quality = 0;
    bool include_predicted_tail = false;
    bool duplicate_day = false;
    bool negate_center81 = false;
};

void write_cssi(const std::filesystem::path& path, const CssiOptions& options = {}) {
    std::string text = "DATATYPE CssiSpaceWeather\nVERSION 1.2\nUPDATED 2017 Jan 01\n";
    text += "BEGIN OBSERVED\n";
    for (long long days = kFirst; days <= kLast; ++days)
        text += cssi_row(days, options.quality,
            options.negate_center81 ? -999.0 : std::numeric_limits<double>::quiet_NaN()) + "\n";
    text += "END OBSERVED\n";
    if (options.include_predicted_tail) {
        text += "BEGIN DAILY_PREDICTED\n";
        for (long long days = kLast + 1; days <= kLast + 3; ++days) text += cssi_row(days, 0) + "\n";
        text += "END DAILY_PREDICTED\n";
    }
    if (options.duplicate_day) {
        text += "BEGIN OBSERVED\n";
        text += cssi_row(kLast + 1, 0) + "\n";
        text += cssi_row(kLast + 1, 0) + "\n";   // dates must strictly increase
        text += "END OBSERVED\n";
    }
    std::ofstream(path) << text;
}

// One DTCFILE row: "DTC " + I4 year + " " + I3 day of year + 24 four-digit
// hours, 109 columns wide.
std::string dtc_row(long long days, const std::array<int, 24>& hours) {
    using namespace std::chrono;
    const year_month_day date{calendar_of(days)};
    std::string row = "DTC ";
    append(row, "%4d %3d ", static_cast<int>(date.year()),
        day_of_year(static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
                    static_cast<unsigned>(date.day())));
    for (const int hour : hours) append(row, "%4d", hour);
    return row;
}

int ramp_hour(int base, int hour) { return base + hour; }

struct DtcOptions {
    int base = 0;
    // Overrides for the hour-23 boundary case, keyed by day serial.
    bool custom_last_hour = false;
    int last_hour_value = 0;
    int next_midnight_value = 0;
    bool truncated_tail = false;
};

void write_dtc(const std::filesystem::path& path, const DtcOptions& options = {}) {
    std::array<int, 24> hours{};
    std::string text;
    for (long long days = kFirst; days <= kLast; ++days) {
        for (int hour = 0; hour < 24; ++hour) hours[hour] = ramp_hour(options.base, hour);
        if (options.custom_last_hour && days == kLast) hours[23] = options.last_hour_value;
        text += dtc_row(days, hours) + "\n";
    }
    if (options.custom_last_hour) {
        // The day after the archive exists only so hour 23 can borrow 00:00 UT.
        for (int hour = 0; hour < 24; ++hour) hours[hour] = ramp_hour(options.base, hour);
        hours[0] = options.next_midnight_value;
        text += dtc_row(kLast + 1, hours) + "\n";
    }
    if (options.truncated_tail) text += "DTC 2017 1   1 1\n";
    std::ofstream(path) << text;
}

void write_solfsmy(const std::filesystem::path& path, std::string_view codes = kSolarCodes,
                   bool trailing_field = false) {
    std::string text;
    for (long long days = kFirst; days <= kLast; ++days) {
        using namespace std::chrono;
        const year_month_day date{calendar_of(days)};
        std::string row;
        append(row, "%d %3d %.1f", static_cast<int>(date.year()),
            day_of_year(static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
                        static_cast<unsigned>(date.day())),
            2440587.5 + static_cast<double>(days) + 0.5);
        for (int column = 0; column < 8; ++column) append(row, " %.1f", solar_proxy(days, column));
        text += row;
        text += " ";
        text += codes;
        if (trailing_field) text += " EXTRA";
        text += "\n";
    }
    std::ofstream(path) << text;
}

// Private scratch directory: every fixture file lives here and is removed on
// scope exit, so tests never read or write an acquired archive.
struct Scratch {
    std::filesystem::path dir;
    Scratch() {
        for(unsigned attempt=0;attempt<128;++attempt) {
            dir = std::filesystem::temp_directory_path() /
                ("leo_atmosphere_input_" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()) +
                    "_" + std::to_string(attempt));
            std::error_code error;
            if(std::filesystem::create_directory(dir,error)) return;
            if(error) throw std::filesystem::filesystem_error("creating fixture directory",dir,error);
        }
        throw std::runtime_error("cannot allocate isolated fixture directory");
    }
    ~Scratch() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    std::filesystem::path file(const std::string& name) const { return dir / name; }
};

// A dataset whose three archives can be rewritten independently by one test.
struct WeatherFixture {
    Scratch scratch;
    std::filesystem::path cssi, solfsmy, dtc;
    WeatherFixture() {
        cssi = scratch.file("SpaceWeather-All-v1.2.txt");
        solfsmy = scratch.file("SOLFSMY.TXT");
        dtc = scratch.file("DTCFILE.TXT");
        write_cssi(cssi);
        write_solfsmy(solfsmy);
        write_dtc(dtc);
    }
    WeatherDataset load() const { return WeatherDataset::load(cssi, solfsmy, dtc); }
};

}  // namespace

// ---------------------------------------------------------------------------
// WGS84 geodetic <-> Earth-fixed conversion
// ---------------------------------------------------------------------------
TEST_CASE("WGS84 reproduces the defining ellipsoid at the equator and pole",
          "[atmosphere][coordinates]") {
    const Eigen::Vector3d equator = earth_fixed({0.0, 0.0, 0.0});
    REQUIRE(equator.x() == Catch::Approx(kA).margin(1e-9));
    REQUIRE(equator.y() == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(equator.z() == Catch::Approx(0.0).margin(1e-9));

    const Eigen::Vector3d north = earth_fixed({kPi / 2, 0.0, 0.0});
    REQUIRE(north.z() == Catch::Approx(kB).margin(1e-9));
    REQUIRE(std::hypot(north.x(), north.y()) == Catch::Approx(0.0).margin(1e-6));

    // On the reference ellipsoid the recovered geodetic height vanishes.
    for (const double latitude : {-90.0, -45.0, -0.001, 0.0, 0.001, 45.0, 90.0}) {
        const double radians = latitude * kPi / 180.0;
        REQUIRE(std::abs(geodetic(earth_fixed({radians, 0.0, 0.0})).altitude_m)
            == Catch::Approx(0.0).margin(1e-6));
    }
}

TEST_CASE("WGS84 45 degree fixture matches the published ellipsoid geometry",
          "[atmosphere][coordinates]") {
    const Eigen::Vector3d point = earth_fixed({kLat45, 0.0, 0.0});
    REQUIRE(point.x() == Catch::Approx(kX45).margin(1e-9));
    REQUIRE(point.z() == Catch::Approx(kZ45).margin(1e-9));
    REQUIRE(point.norm() == Catch::Approx(kR45).margin(1e-9));
    // Geodetic and geocentric latitude are genuinely different quantities: at
    // 45 deg geodetic the published geocentric value is 44.80758 deg, an offset
    // of roughly 0.192 deg or 21 km of ground.
    const double geocentric = std::atan2(point.z(), point.x());
    REQUIRE(std::abs(geocentric - kLat45) * kMetresPerRadian == Catch::Approx(21400.0).margin(200.0));
    REQUIRE(geocentric == Catch::Approx(kGeocentricLat45).margin(1e-12));
    REQUIRE(geocentric * 180.0 / kPi == Catch::Approx(44.80758).margin(1e-4));
    // What the model consumes is the geodetic latitude that was requested.
    REQUIRE(geodetic(point).latitude_rad == Catch::Approx(kLat45).margin(1e-12));
}

TEST_CASE("WGS84 round trips near the poles and across the longitude wrap",
          "[atmosphere][coordinates]") {
    const double latitude_degrees[] = {0.0, 30.0, 45.0, 60.0, 89.0, 89.99999, 89.9999999,
                                       -89.9999999, -60.0, -12.5};
    const double longitude_degrees[] = {0.0, 37.0, 179.5, 180.0, -180.0, -90.0, 270.0, 359.9};
    const double altitudes_m[] = {0.0, 1.0, 200e3, 400e3, 1000e3, -500.0};
    for (const double latitude : latitude_degrees)
        for (const double longitude : longitude_degrees)
            for (const double altitude : altitudes_m) {
                const Geodetic input{latitude * kPi / 180.0, longitude * kPi / 180.0, altitude};
                const Geodetic back = geodetic(earth_fixed(input));
                // One millimetre of ground: the tightest statement the near-pole
                // and near-antipode cases support in double precision.
                REQUIRE(std::abs(back.latitude_rad - input.latitude_rad) * kMetresPerRadian
                    == Catch::Approx(0.0).margin(1e-3));
                REQUIRE(back.altitude_m == Catch::Approx(altitude).margin(1e-6));
                if (std::abs(latitude) == 90.0) continue;   // longitude is undefined there
                // Longitude is compared modulo a full turn.
                const double difference =
                    std::remainder(back.longitude_rad - input.longitude_rad, 2 * kPi);
                REQUIRE(std::abs(difference) * kMetresPerRadian
                        * std::cos(latitude * kPi / 180.0) == Catch::Approx(0.0).margin(1e-3));
            }
    // Longitude wrapping describes the same physical point.
    REQUIRE(earth_fixed({kLat45, 370.0 * kPi / 180.0, 0.0})
        .isApprox(earth_fixed({kLat45, 10.0 * kPi / 180.0, 0.0}), 1e-9));
}

TEST_CASE("WGS84 pole convention returns a defined longitude and height",
          "[atmosphere][coordinates]") {
    const Geodetic north = geodetic({0.0, 0.0, kB + 400e3});
    REQUIRE(north.latitude_rad == Catch::Approx(kPi / 2).margin(1e-12));
    REQUIRE(north.longitude_rad == 0.0);
    REQUIRE(north.altitude_m == Catch::Approx(400e3).margin(1e-6));
    const Geodetic south = geodetic({0.0, 0.0, -(kB + 400e3)});
    REQUIRE(south.latitude_rad == Catch::Approx(-kPi / 2).margin(1e-12));
    REQUIRE(south.altitude_m == Catch::Approx(400e3).margin(1e-6));
    // Any residual transverse offset inside the documented 1e-9 m threshold takes
    // the same branch, so the convention does not depend on x and y being zero.
    const Geodetic near = geodetic({1e-10, -1e-10, kB});
    REQUIRE(near.longitude_rad == 0.0);
    REQUIRE(near.latitude_rad == Catch::Approx(kPi / 2).margin(1e-12));
    REQUIRE(near.altitude_m == Catch::Approx(0.0).margin(1e-9));
}

TEST_CASE("WGS84 conversion rejects invalid coordinates", "[atmosphere][coordinates]") {
    REQUIRE_THROWS_AS(earth_fixed({kPi / 2 + 1e-6, 0.0, 0.0}), std::invalid_argument);
    REQUIRE_THROWS_AS(earth_fixed({-kPi / 2 - 1e-6, 0.0, 0.0}), std::invalid_argument);
    REQUIRE_THROWS_AS(earth_fixed({std::nan(""), 0.0, 0.0}), std::invalid_argument);
    REQUIRE_THROWS_AS(earth_fixed({0.0, std::nan(""), 0.0}), std::invalid_argument);
    REQUIRE_THROWS_AS(earth_fixed({0.0, 0.0, std::numeric_limits<double>::infinity()}),
        std::invalid_argument);
    // At or below the geocentre the geodetic solution does not exist.
    REQUIRE_THROWS_AS(earth_fixed({0.0, 0.0, -kB}), std::invalid_argument);
    REQUIRE_THROWS_AS(earth_fixed({0.0, 0.0, -kB - 1.0}), std::invalid_argument);
    REQUIRE_THROWS_AS(geodetic({0.0, 0.0, 0.0}), std::invalid_argument);
    REQUIRE_THROWS_AS(geodetic({std::nan(""), 0.0, kB + 1.0}), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Immutable CSSI / SOLFSMY / DTCFILE weather records
// ---------------------------------------------------------------------------
TEST_CASE("NRLMSISE daily drivers come from the CSSI observed records",
          "[atmosphere][weather]") {
    const WeatherFixture fixture;
    const WeatherDataset data = fixture.load();
    // 2017-01-01 sits inside the synthetic archive. Its previous day supplies the
    // daily F10.7 while the current day supplies the 81-day centered value.
    const long long current = serial(2017, 1, 1);
    const NrlmsiseInput input = data.nrlmsise(utc(2017, 1, 1, 36000.0));
    REQUIRE(input.year == 2017);
    REQUIRE(input.day_of_year == 1);
    REQUIRE(input.utc_seconds == 36000.0);
    REQUIRE(input.f107 == Catch::Approx(f107_observed(current - 1)));
    REQUIRE(input.f107_average == Catch::Approx(f107_observed_center81(current)));
    REQUIRE(input.ap == Catch::Approx(ap_daily_value(current)));
    REQUIRE(input.ap_history[0] == Catch::Approx(ap_daily_value(current)));
    // Coverage outside the archive is an explicit failure, never climatology.
    REQUIRE_THROWS_AS(data.nrlmsise(utc(2017, 1, 20, 36000.0)), std::runtime_error);
    REQUIRE_THROWS_AS(data.nrlmsise(utc(2016, 11, 25, 36000.0)), std::runtime_error);
}

TEST_CASE("NRLMSISE Ap history uses exact three hour offsets and two averaging windows",
          "[atmosphere][weather]") {
    const WeatherFixture fixture;
    const WeatherDataset data = fixture.load();
    const long long current = serial(2017, 1, 1);
    const int bin = 3;                        // 10:00 UTC, i.e. the 09:00-12:00 bin
    const NrlmsiseInput input = data.nrlmsise(utc(2017, 1, 1, 10.0 * 3600.0));
    REQUIRE(input.ap_history[1] == Catch::Approx(ap_at_offset(current, bin, 0)));
    REQUIRE(input.ap_history[2] == Catch::Approx(ap_at_offset(current, bin, 1)));
    REQUIRE(input.ap_history[3] == Catch::Approx(ap_at_offset(current, bin, 2)));
    REQUIRE(input.ap_history[4] == Catch::Approx(ap_at_offset(current, bin, 3)));
    double early = 0.0, late = 0.0;
    for (int offset = 4; offset <= 11; ++offset) early += ap_at_offset(current, bin, offset);
    for (int offset = 12; offset <= 19; ++offset) late += ap_at_offset(current, bin, offset);
    REQUIRE(input.ap_history[5] == Catch::Approx(early / 8.0).margin(1e-12));
    REQUIRE(input.ap_history[6] == Catch::Approx(late / 8.0).margin(1e-12));
    // The two windows really are 12..33 h and 36..57 h: three hours separate their
    // upper edges, and each mean sits within five percent of its own upper edge.
    REQUIRE(input.ap_history[5] == Catch::Approx(ap_at_offset(current, bin, 11)).epsilon(0.05));
    REQUIRE(input.ap_history[6] == Catch::Approx(ap_at_offset(current, bin, 19)).epsilon(0.05));
}

TEST_CASE("NRLMSISE three hour bin selection is exact at every boundary",
          "[atmosphere][weather]") {
    const WeatherFixture fixture;
    const WeatherDataset data = fixture.load();
    const long long current = serial(2017, 1, 1);
    struct Case { double seconds; int bin; };
    const Case cases[] = {{0.0, 0}, {10799.0, 0}, {10800.0, 1}, {21599.0, 1}, {21600.0, 2},
                          {32399.0, 2}, {32400.0, 3}, {43199.0, 3}, {43200.0, 4},
                          {53999.0, 4}, {54000.0, 5}, {64799.0, 5}, {64800.0, 6},
                          {75599.0, 6}, {75600.0, 7}, {82800.0, 7}, {86399.0, 7}};
    for (const Case& test : cases) {
        const NrlmsiseInput input = data.nrlmsise(utc(2017, 1, 1, test.seconds));
        REQUIRE(input.ap_history[1] == Catch::Approx(ap_at_offset(current, test.bin, 0)));
        REQUIRE(input.ap_history[4] == Catch::Approx(ap_at_offset(current, test.bin, 3)));
        REQUIRE(input.utc_seconds == test.seconds);
    }
    // Crossing midnight walks back into the previous day instead of clamping: at
    // 00:00 UT the -3 h record is the last bin of 2016-12-31.
    const NrlmsiseInput midnight = data.nrlmsise(utc(2017, 1, 1, 0.0));
    REQUIRE(midnight.ap_history[2] == Catch::Approx(ap_bin_value(current - 1, 7)));
    REQUIRE(midnight.ap_history[4] == Catch::Approx(ap_bin_value(current - 1, 5)));
}

TEST_CASE("NRLMSISE history selection leaves unused slots absent rather than defaulted",
          "[atmosphere][weather]") {
    const WeatherFixture fixture;
    const WeatherDataset data = fixture.load();
    const long long current = serial(2017, 1, 1);
    const NrlmsiseInput with = data.nrlmsise(utc(2017, 1, 1, 36000.0), true);
    const NrlmsiseInput without = data.nrlmsise(utc(2017, 1, 1, 36000.0), false);
    // The daily mean is always present and is independent of the history option.
    REQUIRE(without.ap_history[0] == Catch::Approx(ap_daily_value(current)));
    REQUIRE(with.ap_history[0] == Catch::Approx(ap_daily_value(current)));
    // Without history the remaining six slots carry no data at all. They are NaN,
    // not a quiet Ap of 4, so any consumer that indexes them without checking
    // fails loudly instead of silently evaluating against a climatology.
    for (int i = 1; i < 7; ++i) REQUIRE(std::isnan(without.ap_history[i]));
    // With history every slot is a real record: the daily mean, the current bin,
    // -3, -6, -9 h, and the two averaging windows.
    REQUIRE(with.ap_history[1] == Catch::Approx(ap_at_offset(current, 3, 0)));
    REQUIRE(with.ap_history[2] == Catch::Approx(ap_at_offset(current, 3, 1)));
    REQUIRE(with.ap_history[3] == Catch::Approx(ap_at_offset(current, 3, 2)));
    REQUIRE(with.ap_history[4] == Catch::Approx(ap_at_offset(current, 3, 3)));
    double early = 0.0, late = 0.0;
    for (int offset = 4; offset <= 11; ++offset) early += ap_at_offset(current, 3, offset);
    for (int offset = 12; offset <= 19; ++offset) late += ap_at_offset(current, 3, offset);
    REQUIRE(with.ap_history[5] == Catch::Approx(early / 8.0).margin(1e-12));
    REQUIRE(with.ap_history[6] == Catch::Approx(late / 8.0).margin(1e-12));
    // The daily solar and geomagnetic drivers do not depend on the option.
    REQUIRE(with.f107 == Catch::Approx(without.f107));
    REQUIRE(with.f107_average == Catch::Approx(without.f107_average));
    REQUIRE(with.ap == Catch::Approx(without.ap));
}

TEST_CASE("CSSI quality flags and predicted sections are rejected", "[atmosphere][weather]") {
    WeatherFixture fixture;
    const UtcCalendar query = utc(2017, 1, 1, 36000.0);
    // Quality 0 marks an observed record, and it is this record's numbers that
    // reach the model, not merely the absence of an exception.
    write_cssi(fixture.cssi);
    const WeatherDataset observed = fixture.load();
    const long long current = serial(2017, 1, 1);
    const NrlmsiseInput accepted = observed.nrlmsise(query);
    REQUIRE(accepted.f107 == Catch::Approx(f107_observed(current - 1)));
    REQUIRE(accepted.f107_average == Catch::Approx(f107_observed_center81(current)));
    REQUIRE(accepted.ap == Catch::Approx(ap_daily_value(current)));
    REQUIRE(accepted.ap_history[1] == Catch::Approx(ap_at_offset(current, 3, 0)));
    // Quality 1 marks an interpolated centre value, quality 2 a missing one.
    write_cssi(fixture.cssi, CssiOptions{.quality = 1});
    REQUIRE_THROWS_AS(fixture.load().nrlmsise(query), std::runtime_error);
    write_cssi(fixture.cssi, CssiOptions{.quality = 2});
    REQUIRE_THROWS_AS(fixture.load().nrlmsise(query), std::runtime_error);
    // A negative centred value is a missing measurement whatever the quality flag.
    write_cssi(fixture.cssi, CssiOptions{.negate_center81 = true});
    REQUIRE_THROWS_AS(fixture.load().nrlmsise(query), std::runtime_error);
    // Records inside a prediction section are never observed, even at quality 0.
    // Days before the predicted tail are unaffected and still resolve.
    write_cssi(fixture.cssi, CssiOptions{.include_predicted_tail = true});
    const WeatherDataset with_tail = fixture.load();
    const NrlmsiseInput before_tail = with_tail.nrlmsise(query);
    REQUIRE(before_tail.f107 == Catch::Approx(f107_observed(current - 1)));
    REQUIRE(before_tail.ap_history[1] == Catch::Approx(ap_at_offset(current, 3, 0)));
    // The first predicted day and its lagged neighbour are both refused.
    REQUIRE_THROWS_AS(with_tail.nrlmsise(utc(2017, 1, 7, 36000.0)), std::runtime_error);
    REQUIRE_THROWS_AS(with_tail.nrlmsise(utc(2017, 1, 6, 36000.0)), std::runtime_error);
    // Dates must strictly increase, so a duplicated day is a load error.
    write_cssi(fixture.cssi, CssiOptions{.duplicate_day = true});
    REQUIRE_THROWS_AS(fixture.load(), std::invalid_argument);
}

TEST_CASE("CSSI forecast and fitted sections can overlap without replacing observations",
          "[atmosphere][weather]") {
    WeatherFixture fixture;
    write_cssi(fixture.cssi,CssiOptions{.include_predicted_tail=true});
    {
        std::ofstream out(fixture.cssi,std::ios::app);
        out << "BEGIN MONTHLY_FIT\n"
            << cssi_row(serial(2017,1,1),0,399.0) << '\n'
            << cssi_row(kLast+1,0,398.0) << "\nEND MONTHLY_FIT\n";
    }
    const auto data=fixture.load();
    const auto current=serial(2017,1,1);
    const auto input=data.nrlmsise(utc(2017,1,1,36000.0));
    REQUIRE(input.f107_average==Catch::Approx(f107_observed_center81(current)));
    REQUIRE(input.ap_history[1]==Catch::Approx(ap_at_offset(current,3,0)));
    REQUIRE_THROWS_AS(data.nrlmsise(utc(2017,1,6,36000.0)),std::runtime_error);
}

TEST_CASE("JB2008 applies the documented minus one, two and five day solar lags",
          "[atmosphere][weather]") {
    const WeatherFixture fixture;
    const WeatherDataset data = fixture.load();
    const long long current = serial(2017, 1, 1);
    const Jb2008Input input = data.jb2008(utc(2017, 1, 1, 43200.0));
    // F10 and S10 lag one day, M10 two days, Y10 five days, and every 81-day
    // centered average carries the same lag as its own index.
    REQUIRE(input.f10 == Catch::Approx(solar_proxy(current - 1, kF10)));
    REQUIRE(input.f10_average == Catch::Approx(solar_proxy(current - 1, kF10Average)));
    REQUIRE(input.s10 == Catch::Approx(solar_proxy(current - 1, kS10)));
    REQUIRE(input.s10_average == Catch::Approx(solar_proxy(current - 1, kS10Average)));
    REQUIRE(input.m10 == Catch::Approx(solar_proxy(current - 2, kM10)));
    REQUIRE(input.m10_average == Catch::Approx(solar_proxy(current - 2, kM10Average)));
    REQUIRE(input.y10 == Catch::Approx(solar_proxy(current - 5, kY10)));
    REQUIRE(input.y10_average == Catch::Approx(solar_proxy(current - 5, kY10Average)));
    REQUIRE(input.mjd_utc == Catch::Approx(kMjdEpoch + static_cast<double>(current) + 0.5));
    // The lags cross a year boundary: 2016 is a leap year, so day 362 is
    // 2016-12-27, which is exactly the day the Y10 column is read from.
    REQUIRE(day_of_year(2016, 12, 27) == 362);
    // Five days of lagged coverage must exist before any JB2008 query succeeds.
    REQUIRE_THROWS_AS(data.jb2008(utc(2016, 11, 24, 43200.0)), std::runtime_error);
}

TEST_CASE("JB2008 rejects gap filled and unsupported SOLFSMY provenance", "[atmosphere][weather]") {
    WeatherFixture fixture;
    const UtcCalendar query = utc(2017, 1, 1, 43200.0);
    // "0" marks a spline-filled or missing index and "F" a mean of bordering
    // values; neither is an observed measurement.
    write_solfsmy(fixture.solfsmy, "0B11");
    REQUIRE_THROWS_AS(fixture.load().jb2008(query), std::runtime_error);
    write_solfsmy(fixture.solfsmy, "1B1F");
    REQUIRE_THROWS_AS(fixture.load().jb2008(query), std::runtime_error);
    // Measured and composite provenance are both accepted, and both resolve to the
    // same lagged records because only the provenance code differs.
    const long long current = serial(2017, 1, 1);
    write_solfsmy(fixture.solfsmy, kSolarCodes);
    const Jb2008Input measured = fixture.load().jb2008(query);
    REQUIRE(measured.f10 == Catch::Approx(solar_proxy(current - 1, kF10)));
    REQUIRE(measured.y10 == Catch::Approx(solar_proxy(current - 5, kY10)));
    REQUIRE(measured.m10_average == Catch::Approx(solar_proxy(current - 2, kM10Average)));
    write_solfsmy(fixture.solfsmy, kSolarCodesAlt);
    const Jb2008Input composite = fixture.load().jb2008(query);
    REQUIRE(composite.f10 == Catch::Approx(solar_proxy(current - 1, kF10)));
    REQUIRE(composite.s10_average == Catch::Approx(solar_proxy(current - 1, kS10Average)));
    REQUIRE(composite.y10_average == Catch::Approx(solar_proxy(current - 5, kY10Average)));
    // Every accepted code must be one the model actually consumed, never a
    // neighbouring value: the eight inputs are eight distinct day-lagged columns.
    REQUIRE(measured.f10 != measured.s10);
    REQUIRE(measured.s10 != measured.m10);
    REQUIRE(measured.m10 != measured.y10);
    // Malformed rows are load errors, not silently skipped records.
    write_solfsmy(fixture.solfsmy, kSolarCodes, /*trailing_field=*/true);
    REQUIRE_THROWS_AS(fixture.load(), std::invalid_argument);
}

TEST_CASE("JB2008 dTc interpolates within the hour and rounds half up", "[atmosphere][weather]") {
    WeatherFixture fixture;
    write_dtc(fixture.dtc);
    const WeatherDataset data = fixture.load();
    // Hour h holds base+h, so the interpolated value is base+h+fraction and the
    // vendor driver rounds with floor(x+0.5), which this test states explicitly.
    for (const int hour : {0, 5, 13, 22}) {
        const double midpoint = ramp_hour(0, hour) + 0.5;
        REQUIRE(data.jb2008(utc(2017, 1, 1, hour * 3600.0 + 1800.0)).dstdtc_k
            == Catch::Approx(std::floor(midpoint + 0.5)).margin(1e-12));
        REQUIRE(data.jb2008(utc(2017, 1, 1, hour * 3600.0)).dstdtc_k
            == Catch::Approx(std::floor(static_cast<double>(ramp_hour(0, hour)) + 0.5)).margin(1e-12));
    }
    // A missing hourly record is an error, never a smoothed-over zero.
    REQUIRE_THROWS_AS(data.jb2008(utc(2017, 1, 20, 43200.0)), std::runtime_error);
    write_dtc(fixture.dtc, DtcOptions{.truncated_tail = true});
    REQUIRE_THROWS_AS(fixture.load(), std::invalid_argument);
}

TEST_CASE("JB2008 hour 23 borrows the next day and the leap second stretches it",
          "[atmosphere][weather]") {
    WeatherFixture fixture;
    // A steep hour-23 step makes the effect of a one-second stretch unambiguous
    // under the driver's floor(x+0.5) rounding.
    const int last_hour = 1000;
    const int next_midnight = 0;
    write_dtc(fixture.dtc, DtcOptions{.custom_last_hour = true,
        .last_hour_value = last_hour, .next_midnight_value = next_midnight});
    const WeatherDataset data = fixture.load();
    const double seconds_into_hour = 3599.0;
    const double wall_clock = 23 * 3600.0 + seconds_into_hour;
    // A normal day divides the final hour into 3600 s; a positive leap second
    // makes it 3601 s, which lowers the fraction and therefore the value.
    const double ordinary_fraction = seconds_into_hour / 3600.0;
    const double stretched_fraction = seconds_into_hour / 3601.0;
    const double ordinary = std::floor(
        last_hour - ordinary_fraction * (last_hour - next_midnight) + 0.5);
    const double stretched = std::floor(
        last_hour - stretched_fraction * (last_hour - next_midnight) + 0.5);
    REQUIRE(ordinary != stretched);
    REQUIRE(data.jb2008(utc(2017, 1, 4, wall_clock, 86400.0)).dstdtc_k
        == Catch::Approx(ordinary).margin(1e-12));
    REQUIRE(data.jb2008(utc(2017, 1, 4, wall_clock, 86401.0)).dstdtc_k
        == Catch::Approx(stretched).margin(1e-12));
    // Exactly at 23:00 UT the current hour applies with zero fraction.
    REQUIRE(data.jb2008(utc(2017, 1, 4, 23 * 3600.0)).dstdtc_k
        == Catch::Approx(std::floor(static_cast<double>(last_hour) + 0.5)).margin(1e-12));
    // The day before the leap second is a normal 86400 s day and is unaffected.
    REQUIRE(data.jb2008(utc(2017, 1, 3, wall_clock)).dstdtc_k
        == Catch::Approx(std::floor(ramp_hour(0, 23)
            - ordinary_fraction * (ramp_hour(0, 23) - ramp_hour(0, 0)) + 0.5)).margin(1e-12));
}

TEST_CASE("Weather queries reject inconsistent UTC calendars", "[atmosphere][weather]") {
    const WeatherFixture fixture;
    const WeatherDataset data = fixture.load();
    UtcCalendar broken = utc(2017, 1, 1, 43200.0);
    broken.day_of_year = 2;                    // disagrees with the calendar date
    REQUIRE_THROWS_AS(data.jb2008(broken), std::invalid_argument);
    broken = utc(2017, 1, 1, 43200.0);
    broken.days_since_1970 -= 1;
    REQUIRE_THROWS_AS(data.jb2008(broken), std::invalid_argument);
    // Seconds of day must lie inside the day, and the day length must be a real
    // UTC day, possibly stretched by one leap second.
    REQUIRE_THROWS_AS(data.nrlmsise(utc(2017, 1, 1, 86400.0)), std::invalid_argument);
    REQUIRE_THROWS_AS(data.nrlmsise(utc(2017, 1, 1, -1.0)), std::invalid_argument);
    REQUIRE_THROWS_AS(data.nrlmsise(utc(2017, 1, 1, std::nan(""))), std::invalid_argument);
    REQUIRE_THROWS_AS(data.nrlmsise(utc(2017, 1, 1, 43200.0, 86402.0)), std::invalid_argument);
    REQUIRE_THROWS_AS(data.nrlmsise(utc(2017, 1, 1, 43200.0, 86398.0)), std::invalid_argument);
    REQUIRE_THROWS_AS(data.nrlmsise(utc(2017, 1, 1, std::nan(""), 86400.0)), std::invalid_argument);
    // 2016 is a leap year, so day 366 exists and an 86401 s day is representable.
    // The stretched day resolves to the same records as a 86400 s day would,
    // because the extra second only widens the final hour.
    REQUIRE(day_of_year(2016, 12, 31) == 366);
    const long long leap_day = serial(2016, 12, 31);
    const UtcCalendar stretched = utc(2016, 12, 31, 43200.0, 86401.0);
    REQUIRE(stretched.day_length_seconds == Catch::Approx(86401.0));
    REQUIRE(stretched.days_since_1970 == leap_day);
    const NrlmsiseInput leap_nrlmsise = data.nrlmsise(stretched);
    REQUIRE(leap_nrlmsise.day_of_year == 366);
    REQUIRE(leap_nrlmsise.year == 2016);
    REQUIRE(leap_nrlmsise.f107 == Catch::Approx(f107_observed(leap_day - 1)));
    REQUIRE(leap_nrlmsise.ap_history[1] == Catch::Approx(ap_at_offset(leap_day, 4, 0)));
    const Jb2008Input leap_jb2008 = data.jb2008(stretched);
    REQUIRE(leap_jb2008.f10 == Catch::Approx(solar_proxy(leap_day - 1, kF10)));
    REQUIRE(leap_jb2008.y10 == Catch::Approx(solar_proxy(leap_day - 5, kY10)));
}

TEST_CASE("Weather loading rejects absent and partially supplied archives",
          "[atmosphere][weather]") {
    const WeatherFixture fixture;
    REQUIRE_THROWS_AS(WeatherDataset::load(fixture.scratch.file("absent.txt"), {}, {}),
        std::invalid_argument);
    // Empty paths mean a dataset is intentionally absent, not corrupt.
    const WeatherDataset none = WeatherDataset::load({}, {}, {});
    REQUIRE_THROWS_AS(none.nrlmsise(utc(2017, 1, 1, 0.0)), std::runtime_error);
    REQUIRE_THROWS_AS(none.jb2008(utc(2017, 1, 1, 0.0)), std::runtime_error);
    // A dataset holding only one archive cannot serve the other model's inputs.
    const long long current = serial(2017, 1, 1);
    const WeatherDataset cssi_only = WeatherDataset::load(fixture.cssi, {}, {});
    const NrlmsiseInput from_cssi = cssi_only.nrlmsise(utc(2017, 1, 1, 36000.0));
    REQUIRE(from_cssi.f107 == Catch::Approx(f107_observed(current - 1)));
    REQUIRE_THROWS_AS(cssi_only.jb2008(utc(2017, 1, 1, 43200.0)), std::runtime_error);
    const WeatherDataset solar_only = WeatherDataset::load({}, fixture.solfsmy, fixture.dtc);
    const Jb2008Input from_solar = solar_only.jb2008(utc(2017, 1, 1, 43200.0));
    REQUIRE(from_solar.f10 == Catch::Approx(solar_proxy(current - 1, kF10)));
    REQUIRE_THROWS_AS(solar_only.nrlmsise(utc(2017, 1, 1, 36000.0)), std::runtime_error);
}

// ---------------------------------------------------------------------------
// UTC calendar and Earth-fixed frame, against real SPICE kernels
// ---------------------------------------------------------------------------
TEST_CASE("UTC calendar resolves the 2016-12-31 leap second and year boundary",
          "[atmosphere][spice][external]") {
    const char* lsk = env_path("LEO_SPICE_TEST_LSK");
    if (lsk == nullptr) SKIP("Set LEO_SPICE_TEST_LSK to exercise real leapsecond conversion");
    const SpiceEphemerisProvider provider{{lsk}};
    // Epochs are derived from the IERS leap-second table, not from SPICE: with
    // TT = TAI + 32.184 s, TT - UTC is 64.184 s at 2000-01-01, 68.184 s through
    // 2016, and 69.184 s from 2017-01-01. Counting 6209 days from the J2000
    // reference instant 2000-01-01T12:00:00 gives
    //   ET(2016-12-31T00:00:00 UTC) = 6209*86400 - 43200 + 68.184 s.
    // That arithmetic uses TT where SPICE returns TDB. The periodic TDB - TT term
    // is bounded by 1.7 ms by definition, so the UTC fields derived from these
    // epochs are asserted to 10 ms rather than to a fictitious exactness. The
    // calendar date, day of year, day count and day length are exact because they
    // are integer-valued and the leap second changes the day length, not them.
    const double et_2016_start = 6209.0 * 86400.0 + 68.184 - 43200.0;
    const double et_leap_second = et_2016_start + 86400.0;   // 23:59:60 UTC
    const double et_2017_start = et_2016_start + 86401.0;   // 2017-01-01T00:00:00
    const UtcCalendar start = SpiceEphemerisProvider::utc_calendar({et_2016_start});
    REQUIRE(start.year == 2016);
    REQUIRE(start.month == 12);
    REQUIRE(start.day == 31);
    REQUIRE(start.day_of_year == 366);
    REQUIRE(start.days_since_1970 == serial(2016, 12, 31));
    REQUIRE(start.seconds_of_day == Catch::Approx(0.0).margin(0.01));
    // 2016-12-31 carries the leap second, so the UTC day is 86401 s long.
    REQUIRE(start.day_length_seconds == Catch::Approx(86401.0).margin(0.01));

    const UtcCalendar leap = SpiceEphemerisProvider::utc_calendar({et_leap_second});
    REQUIRE(leap.year == 2016);
    REQUIRE(leap.month == 12);
    REQUIRE(leap.day == 31);
    REQUIRE(leap.day_of_year == 366);
    // The positive leap second reaches exactly 86400 s of labelled UTC.
    REQUIRE(leap.seconds_of_day == Catch::Approx(86400.0).margin(0.01));
    REQUIRE(leap.mjd == Catch::Approx(kMjdEpoch + static_cast<double>(serial(2016, 12, 31))
        + 86400.0 / 86401.0).margin(1e-4));

    const UtcCalendar next = SpiceEphemerisProvider::utc_calendar({et_2017_start});
    REQUIRE(next.year == 2017);
    REQUIRE(next.month == 1);
    REQUIRE(next.day == 1);
    REQUIRE(next.day_of_year == 1);
    REQUIRE(next.seconds_of_day == Catch::Approx(0.0).margin(1e-3));
    REQUIRE(next.day_length_seconds == Catch::Approx(86400.0).margin(0.01));
    REQUIRE(next.days_since_1970 == serial(2017, 1, 1));
    REQUIRE(next.mjd == Catch::Approx(kMjdEpoch + static_cast<double>(serial(2017, 1, 1)))
        .margin(1e-9));
}

TEST_CASE("UTC calendar matches the standard Modified Julian Day epoch",
          "[atmosphere][spice][external]") {
    const char* lsk = env_path("LEO_SPICE_TEST_LSK");
    if (lsk == nullptr) SKIP("Set LEO_SPICE_TEST_LSK to exercise real leapsecond conversion");
    const SpiceEphemerisProvider provider{{lsk}};
    // MJD 51544.0 is 2000-01-01T00:00:00 UTC, the day before the J2000 epoch.
    // TT - UTC is 64.184 s then, and TDB - TT is at most 1.7 ms, so the seconds of
    // day carry the same 10 ms tolerance used in the leap-second test.
    const UtcCalendar new_year = SpiceEphemerisProvider::utc_calendar({-43200.0 + 64.184});
    REQUIRE(new_year.year == 2000);
    REQUIRE(new_year.month == 1);
    REQUIRE(new_year.day == 1);
    REQUIRE(new_year.day_of_year == 1);
    REQUIRE(new_year.seconds_of_day == Catch::Approx(0.0).margin(0.01));
    REQUIRE(new_year.days_since_1970 == serial(2000, 1, 1));
    REQUIRE(new_year.mjd == Catch::Approx(51544.0).margin(1e-4));
    // Midday on the same day is MJD 51544.5 and 43200 s into the UTC day.
    const UtcCalendar noon = SpiceEphemerisProvider::utc_calendar({64.184});
    REQUIRE(noon.seconds_of_day == Catch::Approx(43200.0).margin(0.01));
    REQUIRE(noon.mjd == Catch::Approx(51544.5).margin(1e-4));
    REQUIRE_THROWS_AS(SpiceEphemerisProvider::utc_calendar({std::nan("")}), std::invalid_argument);
    REQUIRE_THROWS_AS(
        SpiceEphemerisProvider::utc_calendar({std::numeric_limits<double>::infinity()}),
        std::invalid_argument);
}

TEST_CASE("Earth-fixed rotation is an explicit frame with no fallback",
          "[atmosphere][spice][external]") {
    // Frame selection is rejected before any kernel is touched, so these hold
    // without an orientation kernel.
    REQUIRE_THROWS_AS(SpiceEphemerisProvider::earth_fixed_rotation({0.0}, "ICRF"), std::invalid_argument);
    REQUIRE_THROWS_AS(SpiceEphemerisProvider::earth_fixed_rotation({0.0}, "ITRF2008"), std::invalid_argument);
    REQUIRE_THROWS_AS(SpiceEphemerisProvider::earth_fixed_rotation({0.0}, ""), std::invalid_argument);
    REQUIRE_THROWS_AS(SpiceEphemerisProvider::earth_fixed_rotation({std::nan("")}, "ITRF93"),
        std::invalid_argument);

    const char* pck = env_path("LEO_ATMOSPHERE_TEST_PCK");
    if (pck == nullptr) SKIP("Set LEO_ATMOSPHERE_TEST_PCK to exercise a real ITRF93 orientation kernel");
    const SpiceEphemerisProvider provider{{pck}};
    // UTC noon, not ET=0 (which is TDB noon and approximately 64 s earlier in UTC).
    const Eigen::Matrix3d fixed = SpiceEphemerisProvider::earth_fixed_rotation({64.184}, "ITRF93");
    // Any frame transformation must be a right-handed orthonormal rotation.
    REQUIRE((fixed * fixed.transpose()).isApprox(Eigen::Matrix3d::Identity(), 1e-12));
    REQUIRE(fixed.determinant() == Catch::Approx(1.0).margin(1e-12));
    // The J2000 pole maps to within a few hundred arcseconds of the ITRF93 pole;
    // the residual is the frame bias plus polar motion.
    const Eigen::Vector3d pole = fixed * Eigen::Vector3d::UnitZ();
    REQUIRE(pole.z() == Catch::Approx(1.0).margin(1e-8));
    REQUIRE(std::hypot(pole.x(), pole.y()) == Catch::Approx(0.0).margin(1e-4));
    // IAU1982 GMST at UTC/UT1 J2000 noon is 280.46061837 degrees.
    // Nutation/equation-of-equinoxes and the UTC–UT1 offset are within 0.01 degree.
    const Eigen::Vector3d x_axis = fixed.transpose() * Eigen::Vector3d::UnitX();
    const double greenwich = std::atan2(x_axis.y(), x_axis.x()) * 180.0 / kPi;
    REQUIRE(std::abs(std::remainder(greenwich - 280.46061837,360.0)) < 0.01);
    // An active J2000->fixed rotation advances clockwise by one sidereal turn.
    const Eigen::Matrix3d next_day = SpiceEphemerisProvider::earth_fixed_rotation({86464.184}, "ITRF93");
    const Eigen::Vector3d advance = next_day * fixed.transpose() * Eigen::Vector3d::UnitX();
    const double angle = std::atan2(advance.y(), advance.x()) * 180.0 / kPi;
    REQUIRE(angle == Catch::Approx(-0.98564736629).margin(0.01));
    // ITRF93 and the IAU mean-equator-of-date frame are different conventions and
    // must not be silently interchangeable.
    const char* low_pck=env_path("LEO_ATMOSPHERE_TEST_LOW_PRECISION_PCK");
    if(!low_pck) return; // IAU_EARTH is optional and requires its own text PCK.
    const SpiceEphemerisProvider low_precision_kernels{{low_pck}};
    const Eigen::Matrix3d mean_of_date =
        SpiceEphemerisProvider::earth_fixed_rotation({64.184}, "IAU_EARTH");
    REQUIRE(!fixed.isApprox(mean_of_date, 1e-9));
    REQUIRE((mean_of_date * mean_of_date.transpose()).isApprox(Eigen::Matrix3d::Identity(), 1e-12));
    // A rotation one day apart must not be degenerate: a zero-length advance
    // would indicate a stale or transposed matrix.
    REQUIRE(advance.norm() == Catch::Approx(1.0).margin(1e-12));
}