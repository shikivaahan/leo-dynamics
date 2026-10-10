// Independent validation of the pinned Brodowski NRLMSISE-00 C reference adapter.
//
// Oracle: the 17 published cases of the upstream `DOCUMENTATION` file (release
// 20190709). That file is not vendored here; it is read at run time from
// LEO_NRLMSISE00_REFERENCE_DOCUMENTATION (CMake definition or environment) and
// parsed, so the expected numbers never enter this repository. When the file is
// unavailable the fixture tests SKIP explicitly instead of silently passing.
#include <catch2/catch_test_macros.hpp>
#include "leo/atmosphere/nrlmsise00.hpp"
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