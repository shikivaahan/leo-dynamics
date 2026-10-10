// Independent validation of the pinned Brodowski NRLMSISE-00 C reference adapter.
//
// Oracle: the 17 published cases of the upstream `DOCUMENTATION` file (release
// 20190709). That file is not vendored here; it is read at run time from
// LEO_NRLMSISE00_REFERENCE_DOCUMENTATION (CMake definition or environment) and
// parsed, so the expected numbers never enter this repository. When the file is
// unavailable the fixture tests SKIP explicitly instead of silently passing.
#include <catch2/catch_test_macros.hpp>
#include "leo/atmosphere/nrlmsise00.hpp"
#include "leo/atmosphere/weather.hpp"
#ifdef LEO_NRLMSISE00_TESTS_HAVE_CSPICE
#include "leo/atmosphere/coordinates.hpp"
#include "leo/atmosphere/provider.hpp"
#include "leo/environment/spice.hpp"
#include <numbers>
#endif
// The pinned C reference itself, called directly. Every Ap-mode expectation in
// this file is therefore a native gtd7d evaluation, not a second opinion from
// the adapter under test.
extern "C" {
#include "nrlmsise-00.h"
}
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
using namespace leo::atmosphere;
namespace {

// Seven-significant-digit published %E output has at most 5e-7 relative
// rounding error; 1e-6 covers that bound without claiming model-physics accuracy.
constexpr double kFixtureRtol = 1e-6;
// Pure unit-scaling identities inside the adapter; only round-off can differ.
constexpr double kUnitRtol = 1e-12;

// Constituent order of Result::number_density_m3.
constexpr std::array<int, 8> kNativeIndex{0, 1, 2, 3, 4, 6, 7, 8};
// Mass numbers the reference multiplies each native number density by (nrlmsise-00.c
// lines 1027 and 1046). Atomic oxygen/anomalous oxygen carry 16, hydrogen carries 1
// (not 1.008) exactly as published.
constexpr std::array<double, 8> kMassNumber{4, 16, 28, 32, 40, 1, 14, 16};
constexpr double kAmuGram = 1.66e-24;   // nrlmsise-00.c:1046
constexpr double kAmuKilogram = kAmuGram * 1e-3;

struct ReferenceCase {
    int doy;
    double sec, alt, lat, lon, lst, f107a, f107, ap;
};

// Exact reconstruction of nrlmsise-00_test.c: every case starts from the same
// state and cases 15/16 additionally select the seven-element Ap history
// (switches[9] = -1) with all entries set to 100 by the upstream driver.
constexpr std::array<ReferenceCase, 17> kCases{{
    {172, 29000, 400, 60, -70, 16, 150, 150, 4},   // 0  reference state
    {81, 29000, 400, 60, -70, 16, 150, 150, 4},    // 1  different season
    {172, 75000, 1000, 60, -70, 16, 150, 150, 4},  // 2  1000 km / evening UT
    {172, 29000, 100, 60, -70, 16, 150, 150, 4},   // 3  lower mesosphere
    {172, 29000, 400, 0, -70, 16, 150, 150, 4},    // 4  equator
    {172, 29000, 400, 60, 0, 16, 150, 150, 4},     // 5  prime meridian
    {172, 29000, 400, 60, -70, 4, 150, 150, 4},    // 6  local solar time
    {172, 29000, 400, 60, -70, 16, 70, 150, 4},    // 7  depressed F10.7 average
    {172, 29000, 400, 60, -70, 16, 150, 180, 4},   // 8  elevated daily F10.7
    {172, 29000, 400, 60, -70, 16, 150, 150, 40},  // 9  storm-time daily Ap
    {172, 29000, 0, 60, -70, 16, 150, 150, 4},     // 10 ground level
    {172, 29000, 10, 60, -70, 16, 150, 150, 4},    // 11 troposphere
    {172, 29000, 30, 60, -70, 16, 150, 150, 4},    // 12 upper stratosphere
    {172, 29000, 50, 60, -70, 16, 150, 150, 4},    // 13 stratopause region
    {172, 29000, 70, 60, -70, 16, 150, 150, 4},    // 14 mesopause region
    {172, 29000, 400, 60, -70, 16, 150, 150, 4},   // 15 Ap history
    {172, 29000, 100, 60, -70, 16, 150, 150, 4},   // 16 Ap history, 100 km
}};
constexpr std::array<double, 7> kApHistory{100, 100, 100, 100, 100, 100, 100};

NrlmsiseInput make_input(const ReferenceCase& c) {
    NrlmsiseInput in{};
    // The upstream driver passes year = 0 and the reference marks the field "currently
    // ignored"; nrlmsise-00.c never reads input->year. 2000 is used here because the
    // adapter requires a Gregorian year >= 1 to validate day-of-year. day 172 exists in
    // every year, so the substitution cannot change the result.
    in.year = 2000;
    in.day_of_year = c.doy;
    in.utc_seconds = c.sec;
    in.altitude_km = c.alt;
    in.latitude_deg = c.lat;
    in.longitude_deg = c.lon;
    in.local_solar_time_hours = c.lst;
    in.f107_average = c.f107a;
    in.f107 = c.f107;
    in.ap = c.ap;
    return in;
}

// switches[9] = -1 selects the Ap history; the remaining switches keep the published
// standard setting (0 = native cgs, 1 = on).
std::array<int, 24> switches_for(std::size_t index) {
    auto s = Nrlmsise00::standard_switches();
    if (index >= 15) s[9] = -1;
    return s;
}

double relative_error(double a, double b) {
    if (a == b) return 0.0;
    if (!std::isfinite(a) || !std::isfinite(b))
        return std::numeric_limits<double>::infinity();
    const double scale = std::max(std::abs(a), std::abs(b));
    return scale > 0.0 ? std::abs(a - b) / scale : 0.0;
}

struct ReferenceRow {
    std::array<double, 9> d{};  // native cm^-3 number densities, d[5] is g/cm^3
    double tinf{};
    double tg{};
};

std::filesystem::path reference_documentation() {
    const char* configured =
#ifdef LEO_NRLMSISE00_REFERENCE_DOCUMENTATION
        LEO_NRLMSISE00_REFERENCE_DOCUMENTATION;
#else
        nullptr;
#endif
    if (configured != nullptr) {
        const std::filesystem::path p{configured};
        if (std::filesystem::is_regular_file(p)) return p;
    }
    if (const char* env = std::getenv("LEO_NRLMSISE00_REFERENCE_DOCUMENTATION")) {
        const std::filesystem::path p{env};
        if (std::filesystem::is_regular_file(p)) return p;
    }
    SKIP("Upstream NRLMSISE-00 DOCUMENTATION not supplied; fixture tests cannot run. "
         "Configure with -DLEO_NRLMSISE00_REFERENCE_DOCUMENTATION=<file> or export "
         "LEO_NRLMSISE00_REFERENCE_DOCUMENTATION. No published value is assumed.");
    return {};
}

std::vector<ReferenceRow> reference_rows() {
    const auto path = reference_documentation();
    std::ifstream file{path};
    REQUIRE(file.good());
    std::vector<ReferenceRow> rows;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream stream{line};
        std::vector<std::string> tokens;
        std::string token;
        while (stream >> token) tokens.push_back(token);
        // The fixture rows are the only lines of the file holding exactly eleven
        // %E-printed numbers (d[0..8], t[0], t[1]).
        if (tokens.size() != 11 || !std::all_of(tokens.begin(),tokens.end(),
            [](const std::string& value) { return value.find('E')!=std::string::npos; })) continue;
        ReferenceRow row{};
        for (std::size_t i = 0; i < 11; ++i) {
            const std::size_t e = tokens[i].find('E');
            REQUIRE(e != std::string::npos);
            char* end = nullptr;
            const double value = std::strtod(tokens[i].c_str(), &end);
            REQUIRE((end != nullptr && *end == '\0'));
            if (i < 9) row.d[i] = value;
            else if (i == 9) row.tinf = value;
            else row.tg = value;
        }
        rows.push_back(row);
    }
    REQUIRE(rows.size() == kCases.size());
    return rows;
}

// gtd7d mass density reconstructed from the published constituents. The DOCUMENTATION
// RHO column is the gtd7 value (anomalous oxygen excluded, see nrlmsise-00.c:1027);
// the drag-effective value this adapter reports adds the 16 * d[8] term of line 1046.
double published_gtd7d_mass(const ReferenceRow& row) {
    double weighted = 0.0;
    for (std::size_t i = 0; i < kNativeIndex.size(); ++i)
        weighted += kMassNumber[i] * row.d[kNativeIndex[i]];
    return kAmuGram * weighted;
}

double published_gtd7_mass(const ReferenceRow& row) {
    double weighted = 0.0;
    for (std::size_t i = 0; i < 7; ++i) weighted += kMassNumber[i] * row.d[kNativeIndex[i]];
    return kAmuGram * weighted;
}

}  // namespace


TEST_CASE("NRLMSISE00 switch domains are enforced at construction", "[atmosphere][nrlmsise00][switches]") {
    std::array<int, 24> bad{};
    bad[0] = -2;
    REQUIRE_THROWS_AS(Nrlmsise00{bad}, std::invalid_argument);
    auto off = Nrlmsise00::standard_switches();
    off[9] = -2;  // only -1 selects the Ap history
    REQUIRE_THROWS_AS(Nrlmsise00{off}, std::invalid_argument);
    // Published switch value 2 (main effects off, cross terms on) is accepted for every
    // variation switch and changes the answer, so it is not silently coerced to 0 or 1.
    auto cross_terms = Nrlmsise00::standard_switches();
    cross_terms[7] = 2;
    const Nrlmsise00 model;
    REQUIRE(model.evaluate(make_input(kCases[0])).density_kg_m3 !=
            Nrlmsise00{cross_terms}.evaluate(make_input(kCases[0])).density_kg_m3);
    // Switch 0 accepts only the two unit systems, never a cross-term setting.
    std::array<int, 24> metric{};
    metric[0] = 2;
    REQUIRE_THROWS_AS(Nrlmsise00{metric}, std::invalid_argument);
}

TEST_CASE("NRLMSISE00 reproduces the official seventeen published reference cases",
          "[atmosphere][nrlmsise00][reference]") {
    const auto rows = reference_rows();
    double max_density_error=0,max_species_error=0,max_temperature_error=0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        CAPTURE(i);
        const auto& row = rows[i];
        const Nrlmsise00 model{switches_for(i)};
        NrlmsiseInput in = make_input(kCases[i]);
        if (i >= 15) in.ap_history = kApHistory;
        const Result r = model.evaluate(in);

        // Constituent number densities: native cm^-3 scaled to m^-3.
        for (std::size_t c = 0; c < kNativeIndex.size(); ++c) {
            CAPTURE(c);
            const double expected = row.d[kNativeIndex[c]] * 1e6;
            max_species_error=std::max(max_species_error,relative_error(r.number_density_m3[c],expected));
            REQUIRE(relative_error(r.number_density_m3[c], expected) <= kFixtureRtol);
        }
        // t[0] is the exospheric temperature, t[1] the temperature at altitude.
        REQUIRE(relative_error(r.exospheric_temperature_k, row.tinf) <= kFixtureRtol);
        REQUIRE(relative_error(r.temperature_k, row.tg) <= kFixtureRtol);
        max_temperature_error=std::max({max_temperature_error,
            relative_error(r.exospheric_temperature_k,row.tinf),relative_error(r.temperature_k,row.tg)});
        // Drag-effective mass density from gtd7d, reconstructed from published constituents.
        REQUIRE(relative_error(r.density_kg_m3, published_gtd7d_mass(row) * 1e3) <= kFixtureRtol);
        max_density_error=std::max(max_density_error,
            relative_error(r.density_kg_m3,published_gtd7d_mass(row)*1e3));
    }
    std::printf("{\"model\":\"nrlmsise00\",\"fixtures\":%zu,\"max_density_relative_error\":%.17g,"
        "\"max_species_relative_error\":%.17g,\"max_temperature_relative_error\":%.17g}\n",
        rows.size(),max_density_error,max_species_error,max_temperature_error);
}

TEST_CASE("NRLMSISE00 drag density is the gtd7d quantity, not the published gtd7 RHO",
          "[atmosphere][nrlmsise00][reference]") {
    const auto rows = reference_rows();
    // Recomputing gtd7 from the published constituents reproduces the printed RHO column,
    // which confirms the oracle is the documented fixture rather than a re-run wrapper.
    for (std::size_t i = 0; i < rows.size(); ++i) {
        CAPTURE(i);
        REQUIRE(relative_error(published_gtd7_mass(rows[i]), rows[i].d[5]) <= kFixtureRtol);
    }
    // At 1000 km the anomalous-oxygen term dominates, so the two quantities must differ
    // clearly; the adapter must report the drag-effective one.
    const Nrlmsise00 model{switches_for(2)};
    const Result r = model.evaluate(make_input(kCases[2]));
    const double gtd7_mass = published_gtd7_mass(rows[2]) * 1e3;
    REQUIRE(relative_error(r.density_kg_m3, gtd7_mass) > 0.1);
    REQUIRE(r.number_density_m3[7] > 0.0);  // anomalous oxygen, index 7
}

TEST_CASE("NRLMSISE00 drag mass equals the SI constituent physical mass sum",
          "[atmosphere][nrlmsise00][units]") {
    const Nrlmsise00 model;
    const std::array<NrlmsiseInput, 4> inputs{
        make_input(kCases[0]), make_input(kCases[2]), make_input(kCases[3]),
        make_input(kCases[9])};
    for (const auto& in : inputs) {
        const Result r = model.evaluate(in);
        double weighted = 0.0;
        for (std::size_t c = 0; c < kMassNumber.size(); ++c)
            weighted += kMassNumber[c] * r.number_density_m3[c];
        CAPTURE(in.altitude_km);
        REQUIRE(relative_error(r.density_kg_m3, kAmuKilogram * weighted) <= kUnitRtol);
    }
}

TEST_CASE("NRLMSISE00 unit-system switch only rescales, native cgs and metric agree",
          "[atmosphere][nrlmsise00][units]") {
    auto metric_switches = Nrlmsise00::standard_switches();
    metric_switches[0] = 1;
    const Nrlmsise00 native;
    const Nrlmsise00 metric{metric_switches};
    for (const auto& c : kCases) {
        const NrlmsiseInput in = make_input(c);
        const Result a = native.evaluate(in);
        const Result b = metric.evaluate(in);
        CAPTURE(c.alt);
        REQUIRE(relative_error(a.density_kg_m3, b.density_kg_m3) <= kUnitRtol);
        REQUIRE(relative_error(a.temperature_k, b.temperature_k) <= kUnitRtol);
        REQUIRE(relative_error(a.exospheric_temperature_k, b.exospheric_temperature_k) <= kUnitRtol);
        for (std::size_t i = 0; i < a.number_density_m3.size(); ++i)
            REQUIRE(relative_error(a.number_density_m3[i], b.number_density_m3[i]) <= kUnitRtol);
    }
}

TEST_CASE("NRLMSISE00 wraps longitude and local solar time onto the published domain",
          "[atmosphere][nrlmsise00][coordinates]") {
    const Nrlmsise00 model;
    NrlmsiseInput base = make_input(kCases[0]);
    const Result reference = model.evaluate(base);

    for (double offset : {-720.0, -360.0, 360.0, 720.0}) {
        NrlmsiseInput in = base;
        in.longitude_deg = base.longitude_deg + offset;
        CAPTURE(in.longitude_deg);
        const Result r = model.evaluate(in);
        REQUIRE(r.density_kg_m3 == reference.density_kg_m3);
        REQUIRE(r.number_density_m3 == reference.number_density_m3);
    }
    for (double offset : {-48.0, -24.0, 24.0, 48.0}) {
        NrlmsiseInput in = base;
        in.local_solar_time_hours = base.local_solar_time_hours + offset;
        CAPTURE(in.local_solar_time_hours);
        const Result r = model.evaluate(in);
        REQUIRE(r.density_kg_m3 == reference.density_kg_m3);
        REQUIRE(r.number_density_m3 == reference.number_density_m3);
    }
    // Equivalences that must not be silently truncated into a different half turn.
    NrlmsiseInput east = base;
    east.longitude_deg = 190.0;   // wraps to -170
    NrlmsiseInput west = base;
    west.longitude_deg = -170.0;
    REQUIRE(model.evaluate(east).density_kg_m3 == model.evaluate(west).density_kg_m3);
}

TEST_CASE("NRLMSISE00 evaluation is deterministic and independent of evaluation order",
          "[atmosphere][nrlmsise00][determinism]") {
    const Nrlmsise00 model;
    NrlmsiseInput a = make_input(kCases[0]);
    NrlmsiseInput b = make_input(kCases[13]);  // much lower altitude, different branch
    const Result a_first = model.evaluate(a);
    const Result b_first = model.evaluate(b);
    REQUIRE(model.evaluate(a).density_kg_m3 == a_first.density_kg_m3);
    REQUIRE(model.evaluate(b).density_kg_m3 == b_first.density_kg_m3);
    // Repeating the identical call must reproduce the result bit for bit.
    const Result again = model.evaluate(a);
    REQUIRE(again.density_kg_m3 == a_first.density_kg_m3);
    REQUIRE(again.temperature_k == a_first.temperature_k);
    REQUIRE(again.exospheric_temperature_k == a_first.exospheric_temperature_k);
    REQUIRE(again.number_density_m3 == a_first.number_density_m3);
}

TEST_CASE("NRLMSISE00 ignores the calendar year exactly as the reference documents",
          "[atmosphere][nrlmsise00][time]") {
    const Nrlmsise00 model;
    NrlmsiseInput in = make_input(kCases[0]);
    const Result reference = model.evaluate(in);
    for (int year : {1900, 1957, 1999, 2000, 2024, 9999}) {
        NrlmsiseInput other = in;
        other.year = year;
        CAPTURE(year);
        const Result r = model.evaluate(other);
        REQUIRE(r.density_kg_m3 == reference.density_kg_m3);
        REQUIRE(r.number_density_m3 == reference.number_density_m3);
    }
}

TEST_CASE("NRLMSISE00 output at the 32.5 km lower-atmosphere node is finite",
          "[atmosphere][nrlmsise00][regression]") {
    // Release 20190709 changed `alt < zn3[0]` to `alt <= zn3[0]` and initialised the
    // spline locals; earlier releases returned non-finite values exactly at 32.5 km.
    const Nrlmsise00 model;
    for (double alt : {32.4999, 32.5, 32.5001}) {
        NrlmsiseInput in = make_input(kCases[0]);
        in.altitude_km = alt;
        CAPTURE(alt);
        const Result r = model.evaluate(in);
        REQUIRE(std::isfinite(r.density_kg_m3));
        REQUIRE(r.density_kg_m3 > 0.0);
        REQUIRE(std::isfinite(r.temperature_k));
        REQUIRE(r.temperature_k > 0.0);
        REQUIRE(std::isfinite(r.exospheric_temperature_k));
        for (double n : r.number_density_m3) {
            REQUIRE(std::isfinite(n));
            REQUIRE(n >= 0.0);
        }
    }
    // Below and at the node the published model reports no O, H, N or anomalous O.
    NrlmsiseInput in = make_input(kCases[0]);
    in.altitude_km = 32.5;
    const Result r = model.evaluate(in);
    REQUIRE(r.number_density_m3[1] == 0.0);  // O
    REQUIRE(r.number_density_m3[5] == 0.0);  // H
    REQUIRE(r.number_density_m3[6] == 0.0);  // N
    REQUIRE(r.number_density_m3[7] == 0.0);  // anomalous O
}

TEST_CASE("NRLMSISE00 Ap history is used only when switch 9 is -1", "[atmosphere][nrlmsise00][space_weather]") {
    auto history_switches = Nrlmsise00::standard_switches();
    history_switches[9] = -1;
    const Nrlmsise00 with_history{history_switches};
    const Nrlmsise00 daily_only;

    NrlmsiseInput in = make_input(kCases[15]);
    in.ap_history = kApHistory;
    const Result history_result = with_history.evaluate(in);
    // The published case 15 output is produced with exactly this configuration.
    const Result daily_result = daily_only.evaluate(in);
    REQUIRE(daily_result.density_kg_m3 != history_result.density_kg_m3);

    // sg0 (nrlmsise-00.c:594) consumes ap_a[1..6]; ap_a[0], the daily Ap slot documented
    // in nrlmsise-00.h, is only reached through the daily-index switch path. Slots 1-6
    // must therefore each reach the model, and slot 0 must not.
    for (std::size_t slot = 1; slot < 7; ++slot) {
        NrlmsiseInput probe = in;
        probe.ap_history[slot] = 250.0;
        const Result r = with_history.evaluate(probe);
        CAPTURE(slot);
        REQUIRE(r.density_kg_m3 != history_result.density_kg_m3);
    }
    NrlmsiseInput daily_slot = in;
    daily_slot.ap_history[0] = 250.0;
    REQUIRE(with_history.evaluate(daily_slot).density_kg_m3 == history_result.density_kg_m3);
    // With switch 9 back on, the whole history is ignored and the daily Ap drives the model.
    daily_slot.ap_history[1] = 250.0;
    REQUIRE(daily_only.evaluate(daily_slot).density_kg_m3 == daily_result.density_kg_m3);
    NrlmsiseInput storm = in;
    storm.ap = 40.0;
    REQUIRE(daily_only.evaluate(storm).density_kg_m3 != daily_result.density_kg_m3);
}

TEST_CASE("NRLMSISE00 validates its input domain", "[atmosphere][nrlmsise00][validation]") {
    const Nrlmsise00 model;

    const auto rejects = [&model](auto mutate) {
        NrlmsiseInput in = make_input(kCases[0]);
        mutate(in);
        REQUIRE_THROWS_AS(model.evaluate(in), std::invalid_argument);
    };
    rejects([](NrlmsiseInput& in) { in.year = 0; });
    rejects([](NrlmsiseInput& in) { in.year = 10000; });
    rejects([](NrlmsiseInput& in) { in.day_of_year = 0; });
    rejects([](NrlmsiseInput& in) { in.day_of_year = 367; });  // 2000 is a leap year: 366 is valid
    rejects([](NrlmsiseInput& in) { in.day_of_year = 366; in.year = 1900; });  // not a leap year
    rejects([](NrlmsiseInput& in) { in.day_of_year = 366; in.year = 2001; });
    rejects([](NrlmsiseInput& in) { in.utc_seconds = -1.0; });
    rejects([](NrlmsiseInput& in) { in.utc_seconds = 86401.0; });
    for (double sec : {0.0, 0.5, 86399.0, 86400.0}) {
        NrlmsiseInput in = make_input(kCases[0]);
        in.utc_seconds = sec;
        CAPTURE(sec);
        REQUIRE(std::isfinite(model.evaluate(in).density_kg_m3));
    }
    rejects([](NrlmsiseInput& in) { in.altitude_km = -1.0; });
    rejects([](NrlmsiseInput& in) { in.altitude_km = 1000.5; });
    rejects([](NrlmsiseInput& in) { in.latitude_deg = 90.5; });
    rejects([](NrlmsiseInput& in) { in.latitude_deg = -90.5; });
    rejects([](NrlmsiseInput& in) { in.f107 = 0.0; });
    rejects([](NrlmsiseInput& in) { in.f107_average = -1.0; });
    rejects([](NrlmsiseInput& in) { in.ap = -1.0; });
    rejects([](NrlmsiseInput& in) { in.ap = 401.0; });
    for (double nan_value : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        CAPTURE(nan_value);
        rejects([nan_value](NrlmsiseInput& in) { in.altitude_km = nan_value; });
        rejects([nan_value](NrlmsiseInput& in) { in.latitude_deg = nan_value; });
        rejects([nan_value](NrlmsiseInput& in) { in.longitude_deg = nan_value; });
        rejects([nan_value](NrlmsiseInput& in) { in.local_solar_time_hours = nan_value; });
        rejects([nan_value](NrlmsiseInput& in) { in.utc_seconds = nan_value; });
        rejects([nan_value](NrlmsiseInput& in) { in.f107 = nan_value; });
        rejects([nan_value](NrlmsiseInput& in) { in.f107_average = nan_value; });
        rejects([nan_value](NrlmsiseInput& in) { in.ap = nan_value; });
    }

    // Longitude and local solar time are unbounded on input: only non-finite values fail.
    for (double lon : {-1e6, 1e6}) {
        NrlmsiseInput in = make_input(kCases[0]);
        in.longitude_deg = lon;
        CAPTURE(lon);
        REQUIRE(std::isfinite(model.evaluate(in).density_kg_m3));
    }

    // Ap history is only inspected when switch 9 selects it.
    auto history_switches = Nrlmsise00::standard_switches();
    history_switches[9] = -1;
    const Nrlmsise00 with_history{history_switches};
    NrlmsiseInput in = make_input(kCases[15]);
    in.ap_history[3] = -5.0;
    REQUIRE_THROWS_AS(with_history.evaluate(in), std::invalid_argument);
    in.ap_history[3] = 401.0;
    REQUIRE_THROWS_AS(with_history.evaluate(in), std::invalid_argument);
    in.ap_history[3] = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_THROWS_AS(with_history.evaluate(in), std::invalid_argument);
}

TEST_CASE("NRLMSISE00 serialises concurrent evaluation of the reference global state",
          "[atmosphere][nrlmsise00][threading]") {
    const Nrlmsise00 model;
    std::vector<Result> expected;
    for (const auto& c : kCases) expected.push_back(model.evaluate(make_input(c)));

    std::vector<std::thread> workers;
    std::vector<std::vector<Result>> got(8);
    for (std::size_t w = 0; w < 8; ++w) {
        workers.emplace_back([&, w] {
            for (std::size_t i = 0; i < kCases.size(); ++i)
                got[w].push_back(model.evaluate(make_input(kCases[i])));
        });
    }
    for (auto& worker : workers) worker.join();
    for (const auto& series : got) {
        REQUIRE(series.size() == expected.size());
        for (std::size_t i = 0; i < series.size(); ++i) {
            CAPTURE(i);
            REQUIRE(series[i].density_kg_m3 == expected[i].density_kg_m3);
            REQUIRE(series[i].number_density_m3 == expected[i].number_density_m3);
            REQUIRE(series[i].temperature_k == expected[i].temperature_k);
        }
    }
}
// ---------------------------------------------------------------------------
// Daily Ap versus the seven-slot Ap history, against the pinned C reference.
//
// The published cases 15 and 16 set every entry of the Ap history to 100, so
// they show that the -1 switch path runs but cannot distinguish one slot from
// another, cannot order them, and cannot show that slot 0 (the daily Ap) is
// unused on that path. Every expectation below is produced by calling the
// pinned gtd7d directly with an explicit seven-element ap_array, so the
// adapter is compared against the reference rather than against itself.
// ---------------------------------------------------------------------------
namespace {

// Seven distinct values, one per documented slot of struct ap_array
// (nrlmsise-00.h:72-82): daily Ap, current 3 h bin, -3 h, -6 h, -9 h, mean of
// the eight bins 12..33 h back, mean of the eight bins 36..57 h back.
constexpr std::array<double, 7> kUnequalApHistory{23, 118, 7, 156, 44, 91, 12};

struct NativeResult {
    double density_kg_m3{};
    double temperature_k{};
    double exospheric_temperature_k{};
    std::array<double, 8> number_density_m3{};
};

// Fills the reference C structs directly from NrlmsiseInput. The only unit
// handling here is the reference's own switch 0 convention restated from
// nrlmsise-00.h:41 (1 = metres and kilograms) and nrlmsise-00.c:1047.
NativeResult native_evaluate(const std::array<int, 24>& switches, const NrlmsiseInput& in,
                             const std::array<double, 7>* history) {
    ap_array ap{};
    for (std::size_t i = 0; i < 7; ++i) ap.a[i] = history != nullptr ? (*history)[i] : 0.0;
    // Compare identical model-native inputs, including the adapter's documented
    // canonical longitude/LST representation (which can round by one ulp even
    // for an already in-range coordinate recovered from Cartesian geometry).
    const auto wrap = [](double value, double period) {
        const double remainder = std::fmod(value, period);
        return remainder < 0 ? remainder + period : remainder;
    };
    nrlmsise_input native{in.year,     in.day_of_year, in.utc_seconds,
                           in.altitude_km, in.latitude_deg, wrap(in.longitude_deg + 180, 360) - 180,
                           wrap(in.local_solar_time_hours, 24), in.f107_average, in.f107, in.ap,
                           history != nullptr ? &ap : nullptr};
    nrlmsise_flags flags{};
    std::copy(switches.begin(), switches.end(), flags.switches);
    nrlmsise_output output{};
    gtd7d(&native, &flags, &output);
    const double mass = switches[0] == 0 ? 1e3 : 1.0;    // g/cm^3 -> kg/m^3
    const double number = switches[0] == 0 ? 1e6 : 1.0;  // cm^-3 -> m^-3
    NativeResult result{output.d[5] * mass, output.t[1], output.t[0], {}};
    for (std::size_t i = 0; i < kNativeIndex.size(); ++i)
        result.number_density_m3[i] = output.d[kNativeIndex[i]] * number;
    return result;
}

NrlmsiseInput unequal_history_case(const ReferenceCase& c) {
    NrlmsiseInput in = make_input(c);
    in.ap_history = kUnequalApHistory;
    return in;
}

void require_same_as_native(const Result& adapter, const NativeResult& native) {
    REQUIRE(adapter.density_kg_m3 == native.density_kg_m3);
    REQUIRE(adapter.temperature_k == native.temperature_k);
    REQUIRE(adapter.exospheric_temperature_k == native.exospheric_temperature_k);
    REQUIRE(adapter.number_density_m3 == native.number_density_m3);
}

}  // namespace

TEST_CASE("NRLMSISE00 Ap-history mode reproduces a direct native gtd7d call",
          "[atmosphere][nrlmsise00][ap_modes]") {
    auto history_switches = Nrlmsise00::standard_switches();
    history_switches[9] = -1;
    auto metric_switches = history_switches;
    metric_switches[0] = 1;
    const Nrlmsise00 history_cgs{history_switches};
    const Nrlmsise00 history_si{metric_switches};

    // Every published geometry, so the seven-slot path is checked from the
    // ground to 1000 km and through both unit systems, with values that no
    // equal-valued history could ever produce.
    for (const auto& c : kCases) {
        const NrlmsiseInput in = unequal_history_case(c);
        const NativeResult native_cgs =
            native_evaluate(history_switches, in, &in.ap_history);
        const NativeResult native_si = native_evaluate(metric_switches, in, &in.ap_history);
        CAPTURE(c.alt);
        CAPTURE(c.doy);
        require_same_as_native(history_cgs.evaluate(in), native_cgs);
        require_same_as_native(history_si.evaluate(in), native_si);
        // The two unit systems are the same physical answer, so the native
        // cgs and metric evaluations must also agree through the scaling.
        REQUIRE(relative_error(native_cgs.density_kg_m3, native_si.density_kg_m3) <= kUnitRtol);
        REQUIRE(relative_error(history_cgs.evaluate(in).density_kg_m3,
                               history_si.evaluate(in).density_kg_m3) <= kUnitRtol);
    }
}

TEST_CASE("NRLMSISE00 daily mode reproduces a native gtd7d call that never sees the Ap history",
          "[atmosphere][nrlmsise00][ap_modes]") {
    const Nrlmsise00 daily;
    // Storm-time daily Ap from published case 9, plus the unequal history the
    // adapter would forward. On the daily path the reference reads only
    // input->ap (nrlmsise-00.c:739) and never dereferences ap_a.
    NrlmsiseInput in = make_input(kCases[9]);
    in.ap_history = kUnequalApHistory;
    require_same_as_native(daily.evaluate(in),
                           native_evaluate(daily.switches(), in, nullptr));
    require_same_as_native(daily.evaluate(in),
                           native_evaluate(daily.switches(), in, &in.ap_history));
    // Same storm daily Ap without any history supplied at all.
    NrlmsiseInput plain = make_input(kCases[9]);
    require_same_as_native(daily.evaluate(plain),
                           native_evaluate(daily.switches(), plain, nullptr));
}

TEST_CASE("NRLMSISE00 history mode ignores the daily Ap field, as the reference does",
          "[atmosphere][nrlmsise00][ap_modes]") {
    auto history_switches = Nrlmsise00::standard_switches();
    history_switches[9] = -1;
    const Nrlmsise00 history{history_switches};
    NrlmsiseInput in = unequal_history_case(kCases[0]);
    const Result reference = history.evaluate(in);
    require_same_as_native(reference, native_evaluate(history_switches, in, &in.ap_history));

    // nrlmsise-00.c reads input->ap only in the else branch of line 716, which
    // switches[9] == -1 skips; sg0 (line 594) consumes slots 1..6 only. Both
    // the daily field and slot 0 therefore leave the -1 result untouched, and
    // the native reference confirms it rather than the adapter alone.
    for (double daily_ap : {4.0, 40.0, 200.0}) {
        NrlmsiseInput probe = in;
        probe.ap = daily_ap;
        CAPTURE(daily_ap);
        REQUIRE(history.evaluate(probe).density_kg_m3 == reference.density_kg_m3);
        require_same_as_native(history.evaluate(probe),
                               native_evaluate(history_switches, probe, &probe.ap_history));
    }
    NrlmsiseInput slot0 = in;
    slot0.ap_history[0] = 200.0;
    REQUIRE(history.evaluate(slot0).density_kg_m3 == reference.density_kg_m3);
    require_same_as_native(history.evaluate(slot0),
                           native_evaluate(history_switches, slot0, &slot0.ap_history));
}

TEST_CASE("NRLMSISE00 Ap-history slots are individually identifiable, unlike equal published values",
          "[atmosphere][nrlmsise00][ap_modes]") {
    auto history_switches = Nrlmsise00::standard_switches();
    history_switches[9] = -1;
    const Nrlmsise00 history{history_switches};
    const NrlmsiseInput in = unequal_history_case(kCases[0]);
    const Result reference = history.evaluate(in);

    // Cases 15/16 cannot make this statement because all seven slots hold the
    // same number. With seven distinct values, exchanging any two of the six
    // slots the reference consumes changes the answer, so no implementation can
    // satisfy the published fixtures while getting the ordering wrong.
    for (std::size_t i = 1; i < 7; ++i) {
        for (std::size_t j = i + 1; j < 7; ++j) {
            NrlmsiseInput swapped = in;
            std::swap(swapped.ap_history[i], swapped.ap_history[j]);
            CAPTURE(i);
            CAPTURE(j);
            const Result r = history.evaluate(swapped);
            REQUIRE(r.density_kg_m3 != reference.density_kg_m3);
            require_same_as_native(r, native_evaluate(history_switches, swapped,
                                                       &swapped.ap_history));
        }
    }
}

TEST_CASE("NRLMSISE00 daily and Ap-history modes differ physically at one fixed state",
          "[atmosphere][nrlmsise00][ap_modes]") {
    auto history_switches = Nrlmsise00::standard_switches();
    history_switches[9] = -1;
    const Nrlmsise00 history{history_switches};
    const Nrlmsise00 daily;
    NrlmsiseInput in = unequal_history_case(kCases[0]);
    // Same case in both modes: the daily Ap scalar is the number in slot 0, so the
    // only thing that differs between the two evaluations is whether the model is
    // driven by the daily index or by the seven-slot history.
    in.ap = kUnequalApHistory[0];
    const Result with_history = history.evaluate(in);
    const Result with_daily = daily.evaluate(in);
    // The difference is a change of the modelled geomagnetic driver, not an
    // integration or unit error: same state, same solar drivers, same position.
    REQUIRE(with_history.density_kg_m3 != with_daily.density_kg_m3);
    REQUIRE(with_history.temperature_k != with_daily.temperature_k);
    std::printf("{\"model\":\"nrlmsise00\",\"ap_mode\":\"switch9_minus1\",\"alt_km\":%.0f,"
        "\"ap_daily\":%.0f,\"ap_history\":[%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f],"
        "\"daily_density_kg_m3\":%.17g,\"history_density_kg_m3\":%.17g,"
        "\"density_signed_change_vs_daily\":%.17g,"
        "\"density_relative_difference\":%.17g,\"daily_temperature_k\":%.17g,"
        "\"history_temperature_k\":%.17g}\n",
        in.altitude_km, in.ap, kUnequalApHistory[0], kUnequalApHistory[1],
        kUnequalApHistory[2], kUnequalApHistory[3], kUnequalApHistory[4],
        kUnequalApHistory[5], kUnequalApHistory[6],
        with_daily.density_kg_m3, with_history.density_kg_m3,
        (with_history.density_kg_m3 - with_daily.density_kg_m3) / with_daily.density_kg_m3,
        relative_error(with_history.density_kg_m3, with_daily.density_kg_m3),
        with_daily.temperature_k, with_history.temperature_k);
}

TEST_CASE("NRLMSISE00 rejects an absent Ap history before any native call",
          "[atmosphere][nrlmsise00][ap_modes]") {
    auto history_switches = Nrlmsise00::standard_switches();
    history_switches[9] = -1;
    const Nrlmsise00 history{history_switches};
    // The default NrlmsiseInput carries a quiet Ap of 4 in all seven slots, so a
    // caller that forgets to fill the history is silently evaluated against
    // climatology by the reference unless the adapter refuses it first. A NaN
    // slot is how WeatherDataset::nrlmsise reports "this record is absent".
    NrlmsiseInput in = make_input(kCases[0]);
    for (std::size_t slot = 1; slot < 7; ++slot) {
        NrlmsiseInput absent = in;
        absent.ap_history[slot] = std::numeric_limits<double>::quiet_NaN();
        CAPTURE(slot);
        REQUIRE_THROWS_AS(history.evaluate(absent), std::invalid_argument);
    }
    // The same NaN slots are ignored entirely on the daily path, which is why
    // the provider's history flag, not the record set, decides the mode.
    NrlmsiseInput daily_only = in;
    for (std::size_t slot = 1; slot < 7; ++slot)
        daily_only.ap_history[slot] = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(Nrlmsise00{}.evaluate(daily_only).density_kg_m3 ==
            Nrlmsise00{}.evaluate(in).density_kg_m3);
}

#ifdef LEO_NRLMSISE00_TESTS_HAVE_CSPICE
// ---------------------------------------------------------------------------
// Provider-level Ap mode: the boolean that selects the seven-slot loader must
// reach the reference, and the answer must be the native gtd7d evaluation of
// the very records the provider loaded.
// ---------------------------------------------------------------------------
namespace {

const char* optional_path(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : nullptr;
}

}  // namespace

TEST_CASE("DirectAtmosphere selects the NRLMSISE Ap-history mode and matches a native evaluation",
          "[atmosphere][nrlmsise00][ap_modes][provider]") {
    const char* spk = optional_path("LEO_SPICE_TEST_SPK");
    const char* lsk = optional_path("LEO_SPICE_TEST_LSK");
    const char* cssi = optional_path("LEO_NRLMSISE00_TEST_CSSI");
    const char* pck = optional_path("LEO_ATMOSPHERE_TEST_PCK");
    if (spk == nullptr || lsk == nullptr || cssi == nullptr || pck == nullptr)
        SKIP("Provider Ap-mode test needs LEO_SPICE_TEST_SPK, LEO_SPICE_TEST_LSK, "
             "LEO_ATMOSPHERE_TEST_PCK and LEO_NRLMSISE00_TEST_CSSI. No value is assumed.");

    using namespace leo;
    const SpiceEphemerisProvider spice{std::vector<std::filesystem::path>{spk, lsk, pck}};
    const WeatherDataset weather = WeatherDataset::load(cssi, {}, {});
    const DirectAtmosphere daily{Model::nrlmsise00, spice, weather, "ITRF93", false};
    const DirectAtmosphere history{Model::nrlmsise00, spice, weather, "ITRF93", true};

    // Real observed storm records, including the complete preceding 57 h tail.
    const Epoch epoch = SpiceEphemerisProvider::epoch_from_utc("2003-10-30 T10:00:00");
    // A geodetic site is chosen first and then taken to J2000 through the same
    // documented rotation the provider uses in reverse, so the provider's own
    // Earth-fixed geodetic recovery is what gets tested.
    const Geodetic site{60.0 * std::numbers::pi / 180.0, -70.0 * std::numbers::pi / 180.0,
                        400e3};
    const auto rotation = SpiceEphemerisProvider::earth_fixed_rotation(epoch, "ITRF93");
    const InertialVector inertial{rotation.transpose() * earth_fixed(site)};

    const Result with_daily = daily.evaluate(epoch, inertial);
    const Result with_history = history.evaluate(epoch, inertial);
    REQUIRE(with_history.density_kg_m3 != with_daily.density_kg_m3);

    // Independent oracle: the same immutable records, the same geodetic site and
    // the same local-solar-time convention, fed straight to the pinned reference.
    const UtcCalendar utc = SpiceEphemerisProvider::utc_calendar(epoch);
    NrlmsiseInput in = weather.nrlmsise(utc, true);
    const Geodetic recovered = geodetic(rotation * inertial.as_eigen());
    // Match the provider's binary64 input construction, not an algebraically
    // equivalent reassociation that changes the native latitude by one ulp.
    constexpr double degrees_per_radian = 180.0 / std::numbers::pi;
    in.latitude_deg = recovered.latitude_rad * degrees_per_radian;
    in.longitude_deg = recovered.longitude_rad * degrees_per_radian;
    in.altitude_km = recovered.altitude_m / 1000.0;
    in.local_solar_time_hours =
        std::fmod(utc.seconds_of_day / 3600.0 + in.longitude_deg / 15.0 + 24.0, 24.0);

    auto history_switches = Nrlmsise00::standard_switches();
    history_switches[9] = -1;
    require_same_as_native(with_history,
                           native_evaluate(history_switches, in, &in.ap_history));
    require_same_as_native(with_daily,
                           native_evaluate(Nrlmsise00::standard_switches(), in, nullptr));
    std::printf("{\"provider\":\"nrlmsise00\",\"epoch_utc\":\"2003-10-30T10:00:00\","
        "\"ap_history\":[%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g],"
        "\"daily_density_kg_m3\":%.17g,\"history_density_kg_m3\":%.17g}\n",
        in.ap_history[0], in.ap_history[1], in.ap_history[2], in.ap_history[3],
        in.ap_history[4], in.ap_history[5], in.ap_history[6],
        with_daily.density_kg_m3, with_history.density_kg_m3);
}
#endif
