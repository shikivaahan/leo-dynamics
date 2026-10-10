// JB2008 direct-reference validation.
//
// Oracle: the official Space Environment Technologies validation archive
// (JB2008_AUTO_OUT_<YYYY>_<DDD>_<HH>.DAT), acquired out of tree and never
// vendored.  The loader refuses any file whose SHA-256 is not one of the pinned
// digests recorded in docs/atmosphere/JB2008.md, so a substituted or edited
// fixture fails loudly instead of silently validating.  A partially extracted
// archive is likewise an error, not a smaller validation set.
//
// Point LEO_JB2008_VALIDATION_DIR at the extracted "Validation" directory; the
// data files are read from "<dir>/expected output".  Without it these cases
// report SKIP with the acquisition instructions; they never report a pass they
// did not make.
//
// This file is compiled only into the JB2008 test target, which exists only when
// the backend is enabled.
//
// Everything here is project-owned.  No coefficient, equation or reference
// routine from the licensed SET distribution is reproduced.

#include <catch2/catch_test_macros.hpp>

#include "leo/atmosphere/jb2008.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using leo::atmosphere::Jb2008;
using leo::atmosphere::Jb2008Input;

// ---------------------------------------------------------------- tolerances
//
// PROVISIONAL.  These thresholds are derived from the *printed* resolution of
// the published fixture, which is observable in the source formats and in the
// files themselves; they are not yet calibrated against measured residuals of
// the compiled Fortran backend.  Before the JB2008 baseline is frozen, replace
// them with the measured native residuals and the sensitivity of the density to
// a +-0.5 perturbation of each printed index (both obtainable by perturbing
// inputs at run time through this same backend).  Until then a pass means "not
// grossly inconsistent with the published output", not "accurate to 1.5 %".
//
// The input uncertainty that sets the floor:
//   * the eight solar indices are written with Fortran F8.0, i.e. whole
//     numbers, although the source SOLFSMY records carry one decimal (F6.1);
//   * the solar right ascension and declination are written to 1e-4 rad;
//   * both temperatures are written to 0.05 K;
//   * the density is written with five significant digits.
constexpr double kTempAbsK = 1.5;
constexpr double kRhoRelMax = 1.5e-2;
constexpr double kRhoRelRms = 6.0e-3;

// Deterministic decimation of the 68191-row epoch files.  41 is coprime with the
// grid dimensions (97 altitudes x 37 longitudes x 19 latitudes), so the sample
// walks across all three axes instead of repeatedly hitting the same grid
// planes; both endpoints of every file are added unconditionally.
constexpr std::size_t kStride = 41;

// ------------------------------------------------------------------ SHA-256
struct Sha256 {
    std::array<std::uint32_t, 8> h{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<std::uint8_t, 64> buf{};
    std::size_t len = 0;
    std::uint64_t total = 0;

    static std::uint32_t ror(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const std::uint8_t* p) {
        static const std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
            0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
            0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
            0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) |
                   (std::uint32_t(p[4 * i + 2]) << 8) | std::uint32_t(p[4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            const std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const char* p, std::size_t n) {
        total += n;
        while (n) {
            const std::size_t take = std::min(n, 64 - len);
            std::memcpy(buf.data() + len, p, take);
            len += take; p += take; n -= take;
            if (len == 64) { block(buf.data()); len = 0; }
        }
    }

    std::string hex() {
        const std::uint64_t bits = total * 8;
        update("\x80", 1);
        while (len != 56) update("", 1);  // zero padding
        std::uint8_t be[8];
        for (int i = 0; i < 8; ++i) be[i] = std::uint8_t(bits >> (56 - 8 * i));
        update(reinterpret_cast<const char*>(be), 8);
        static const char* d = "0123456789abcdef";
        std::string out;
        for (std::uint32_t v : h)
            for (int s = 28; s >= 0; s -= 4) out += d[(v >> s) & 0xfu];
        return out;
    }
};

std::string sha256_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    Sha256 s;
    std::vector<char> buf(1 << 16);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = in.gcount();
        if (n > 0) s.update(buf.data(), static_cast<std::size_t>(n));
    }
    return s.hex();
}

// ------------------------------------------------------------ fixture model
struct Row {
    double z_km{}, lon_deg{}, lat_deg{}, t1_k{}, t2_k{}, rho{};
};

struct Epoch {
    std::string name;
    std::string sha256;
    double d1950{};
    double mjd{};
    double sun_ra_rad{}, sun_dec_rad{};
    double f10{}, f10b{}, s10{}, s10b{}, m10{}, m10b{}, y10{}, y10b{}, dtc{};
    std::vector<Row> rows;
};

// SHA-256 of the eight published epoch files.  Archive SHA-256
// 4d18c286b148fc5b2271667dab71dd6a702204bb5176bc8a54200aa17e3fff98.
const std::array<std::pair<std::string_view, std::string_view>, 8> kPinned{{
    {"JB2008_AUTO_OUT_2023_091_00.DAT",
     "de0f03197dd798ce5d4c9ef27124c50cc63d8e3e941fe998cebff4503897f943"},
    {"JB2008_AUTO_OUT_2023_091_03.DAT",
     "f56540134bf6662c0b336a568a4138b57f96c3eac35b37b148e725ee0f2ca44b"},
    {"JB2008_AUTO_OUT_2023_091_06.DAT",
     "2add080c9ab1dad86d22cdbadce90b029c9669fef02a66f4c52456bfa320a7c0"},
    {"JB2008_AUTO_OUT_2023_091_09.DAT",
     "ba7a8885a5335702fbdefa949b0601f2643502e0ec10b2581eaf0dd8e6ed2b7e"},
    {"JB2008_AUTO_OUT_2023_091_12.DAT",
     "9c2da398fdb9fdbb1d8ee2e9155a949c8daed60f3231c39485ab88a2fb7fbf58"},
    {"JB2008_AUTO_OUT_2023_091_15.DAT",
     "29c5b51aed1c969a39a9bfc0e3e1b6f3c624c2132ee0893ce1f9ee7add449cc6"},
    {"JB2008_AUTO_OUT_2023_091_18.DAT",
     "9060320be794bbd06d015d197098a5bc1533140b05a8bcb9aedac7d92eb6a422"},
    {"JB2008_AUTO_OUT_2023_091_21.DAT",
     "78361c97a249991a9d36545493b7e735d7086d9a56f79f4bfdbb70c2089e88a2"},
}};

bool parse_double(std::string_view s, double& out) {
    std::string t(s);
    for (char& c : t)
        if (c == 'D' || c == 'd') c = 'E';
    try {
        std::size_t used = 0;
        const double v = std::stod(t, &used);
        if (used != t.size()) return false;
        out = v;
        return true;
    } catch (...) { return false; }
}

std::vector<std::string_view> fields(std::string_view l) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < l.size()) {
        while (i < l.size() && std::isspace(static_cast<unsigned char>(l[i]))) ++i;
        const std::size_t b = i;
        while (i < l.size() && !std::isspace(static_cast<unsigned char>(l[i]))) ++i;
        if (i > b) out.push_back(l.substr(b, i - b));
    }
    return out;
}

std::vector<Epoch> load_epochs() {
    const char* env = std::getenv("LEO_JB2008_VALIDATION_DIR");
    if (!env || !*env) return {};
    // Accept the three layouts in use: the directory holding the files, the
    // directory holding "expected output", or the one holding "jb-validation".
    const fs::path root(env);
    std::error_code ec;
    fs::path dir = root;
    for (const fs::path& candidate :
         {root / "expected output", root / "jb-validation", root}) {
        if (fs::exists(candidate / kPinned.front().first, ec)) { dir = candidate; break; }
    }
    if (!fs::exists(dir / kPinned.front().first, ec)) return {};

    std::vector<Epoch> out;
    std::vector<std::string> missing;
    for (const auto& [name, want] : kPinned) {
        const fs::path p = dir / name;
        if (!fs::exists(p, ec)) { missing.emplace_back(name); continue; }
        std::ifstream in(p);
        if (!in) { missing.emplace_back(name); continue; }
        const std::string digest = sha256_file(p);
        if (digest != want)
            throw std::runtime_error("JB2008 validation fixture hash mismatch for " +
                                     std::string(name) + ": got " + digest);
        std::string line;
        Epoch e;
        e.name = name;
        e.sha256 = digest;
        std::vector<Row> all;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto f = fields(line);
            if (f.empty()) continue;
            std::vector<double> v;
            v.reserve(f.size());
            bool numeric = true;
            for (auto s : f) {
                double d = 0;
                if (!parse_double(s, d)) { numeric = false; break; }
                v.push_back(d);
            }
            if (!numeric) continue;
            if (f.size() == 7) {  // YYYY DDD HR MIN SEC SUNRAS SUNDEC
                e.mjd = -1;
                e.sun_ra_rad = v[5];
                e.sun_dec_rad = v[6];
            } else if (f.size() == 10 && all.empty()) {  // D1950 F10..DSTDTC
                e.d1950 = v[0];
                e.mjd = e.d1950 + 33281.0;
                e.f10 = v[1]; e.f10b = v[2]; e.s10 = v[3]; e.s10b = v[4];
                e.m10 = v[5]; e.m10b = v[6]; e.y10 = v[7]; e.y10b = v[8];
                e.dtc = v[9];
            } else if (f.size() == 6) {
                all.push_back(Row{v[0], v[1], v[2], v[3], v[4], v[5]});
            }
        }
        if (e.mjd <= 0 || all.empty())
            throw std::runtime_error("JB2008 validation fixture " + std::string(name) +
                                     " is missing its epoch/index header or data rows");
        for (std::size_t i = 0; i < all.size(); i += kStride) e.rows.push_back(all[i]);
        e.rows.push_back(all.front());
        e.rows.push_back(all.back());
        out.push_back(std::move(e));
    }
    if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing) { list += m; list += ' '; }
        throw std::runtime_error("JB2008 validation archive is incomplete in " + dir.string() +
                                 "; missing " + list);
    }
    return out;
}

// --------------------------------------------------- standard astronomy ---
// Greenwich hour angle in the JB2008 sense: right ascension of the Greenwich
// meridian referred to the mean equinox of date.  The IAU 1982 expression
// (Explanatory Supplement / Meeus, Astronomical Algorithms, eq. 12.4) is a
// published standard used here only to rebuild the evaluation point from the
// fixture grid; it is not a copy of anything in the SET distribution.
//
// Note on Earth rotation: the reference driver evaluates sidereal time directly
// from UTC, i.e. it treats UT1 as equal to UTC.  For 2023 |DUT1| stayed below
// 0.9 s, an hour-angle discrepancy of at most 6.6e-5 rad.  The 37 s figure that
// is often quoted is TAI-UTC, not UT1-UTC, and does not apply here.
constexpr double kDeg2Rad = std::numbers::pi / 180.0;

double greenwich_hour_angle_rad(double mjd_utc) {
    const double jd = mjd_utc + 2400000.5;
    const double d = jd - 2451545.0;
    const double t = d / 36525.0;
    double deg = 280.46061837 + 360.98564736629 * d + 0.000387933 * t * t -
                 t * t * t / 38710000.0;
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0) deg += 360.0;
    return deg * kDeg2Rad;
}

double wrap_2pi(double a) {
    const double r = std::fmod(a, 2.0 * std::numbers::pi);
    return r < 0.0 ? r + 2.0 * std::numbers::pi : r;
}

// The published grid supplies the point altitude directly; the caller of the
// library, not the model, decides the altitude datum (see JB-D05).
Jb2008Input make_input(const Epoch& e, const Row& r) {
    Jb2008Input in;
    in.mjd_utc = e.mjd;
    in.sun_ra_dec_rad = {e.sun_ra_rad, e.sun_dec_rad};
    in.position_ra_lat_alt = {wrap_2pi(greenwich_hour_angle_rad(e.mjd) + r.lon_deg * kDeg2Rad),
                              r.lat_deg * kDeg2Rad, r.z_km};
    in.f10 = e.f10;          in.f10_average = e.f10b;
    in.s10 = e.s10;          in.s10_average = e.s10b;
    in.m10 = e.m10;          in.m10_average = e.m10b;
    in.y10 = e.y10;          in.y10_average = e.y10b;
    in.dstdtc_k = e.dtc;
    return in;
}

const std::vector<Epoch>& epochs() {
    static const std::vector<Epoch> e = load_epochs();
    return e;
}

// Independent full-precision reference densities, not the rounded official
// validation grid. Source: LegacyInputParams / checkLegacy.rhoRef in
// src/test/java/org/orekit/models/earth/atmosphere/JB2008Test.java at commit
// 6fdf97c133c1ab8c56d7608688419bf4becacf80 (Apache-2.0), fetched and parsed
// outside this repository; that file names no Fortran driver for these rows,
// so their origin is undocumented upstream. Fields, in order: MJD(UTC),
// Sun RA, Sun declination, spacecraft RA, geocentric latitude, native height km,
// F10, F10B, S10, S10B, M10, M10B, Y10, Y10B, DSTDTC, reference density.
struct LegacyReference {
    double mjd_utc, sun_ra, sun_dec, sat_ra, sat_geocentric_latitude, height_km;
    double f10, f10b, s10, s10b, m10, m10b, y10, y10b, dtc, rho;
};
const std::array<LegacyReference,12> kLegacyReferences{{
    {53316.004548611112, 3.8826000000000001, -0.28470000000000001, 2.1229646062101786, -1.4877186543999663, 398.91000000000003, 128.80000000000001, 105.59999999999999, 103.5, 103.8, 110.90000000000001, 106.90000000000001, 127.90000000000001, 112.90000000000001, 3, 1.8730056000000002e-12},
    {53316.503620000003, 3.8914, -0.2873, 1.5186898778985838, -0.32550390549694241, 376.75, 128.80000000000001, 105.59999999999999, 103.5, 103.8, 110.90000000000001, 106.90000000000001, 127.90000000000001, 112.90000000000001, 80, 2.5650338999999999e-12},
    {53317.004679999998, 3.9001000000000001, -0.2898, 1.4618875870566432, 0.65816366092706169, 373.44999999999999, 129.59999999999999, 105.59999999999999, 110.2, 103.8, 115.59999999999999, 106.90000000000001, 125.90000000000001, 112.90000000000001, 240, 5.7428913000000003e-12},
    {53317.503750000003, 3.9089, -0.2923, 4.8104257981427914, 1.2978268317829835, 380.61000000000001, 129.59999999999999, 105.59999999999999, 110.2, 103.8, 115.59999999999999, 106.90000000000001, 125.90000000000001, 112.90000000000001, 307, 8.3266893000000003e-12},
    {53318, 3.9176000000000002, -0.29480000000000001, 4.6262783341429072, -0.15446163880149816, 374.02999999999997, 124.09999999999999, 105.59999999999999, 109.40000000000001, 103.8, 110, 106.90000000000001, 127.7, 113, 132, 8.2238725999999997e-12},
    {53318.505559999998, 3.9264999999999999, -0.29730000000000001, 4.5903228739801305, -0.69184851549055226, 385.05000000000001, 124.09999999999999, 105.59999999999999, 109.40000000000001, 103.8, 110, 106.90000000000001, 127.7, 113, 40, 4.8686457000000002e-12},
    {53319.00088, 3.9352, -0.29980000000000001, 1.5470517879213195, -0.90634948056065534, 389.82999999999998, 140.90000000000001, 105.59999999999999, 108.59999999999999, 103.7, 110, 107, 125.59999999999999, 113.2, 327, 6.7210914000000004e-12},
    {53319.506439999997, 3.9441000000000002, -0.30220000000000002, 1.5006243522465486, -0.37088246604879505, 376.98000000000002, 140.90000000000001, 105.59999999999999, 108.59999999999999, 103.7, 110, 107, 125.59999999999999, 113.2, 327, 7.4215570999999993e-12},
    {53320.005429999997, 3.9529000000000001, -0.30459999999999998, 1.4278131983427818, 0.81035637170096719, 374.56, 104.59999999999999, 105.7, 107.40000000000001, 103.7, 106.90000000000001, 107.09999999999999, 126.59999999999999, 113.2, 118, 3.1821074999999998e-12},
    {53320.504500000003, 3.9618000000000002, -0.307, 4.7248641964950959, 1.1513937075406593, 378.97000000000003, 104.59999999999999, 105.7, 107.40000000000001, 103.7, 106.90000000000001, 107.09999999999999, 126.59999999999999, 113.2, 25, 2.9553578e-12},
    {53321, 3.9706000000000001, -0.30940000000000001, 4.5944599713675602, -0.37192966359999158, 377.75999999999999, 94.900000000000006, 105.90000000000001, 110.90000000000001, 103.7, 102.2, 107.09999999999999, 126.8, 113.3, 85, 6.4122627000000003e-12},
    {53321.505559999998, 3.9794999999999998, -0.31169999999999998, 4.5483816015432161, -0.9053022830094587, 390.08999999999997, 94.900000000000006, 105.90000000000001, 110.90000000000001, 103.7, 102.2, 107.09999999999999, 126.8, 113.3, 251, 7.9559726999999992e-12}}};
// Upstream asserts these against rhoRef at 2e-5 relative; the tolerance is
// inherited, not fitted to this implementation.
constexpr double kLegacyRtol = 2e-5;

constexpr const char* kNoFixture =
    "LEO_JB2008_VALIDATION_DIR unset or no official validation archive extracted; "
    "see docs/atmosphere/acquire.py and docs/atmosphere/JB2008.md";

}  // namespace

TEST_CASE("JB2008 reproduces the official SET validation epochs", "[atmosphere][jb2008]") {
    if (epochs().empty()) SKIP(kNoFixture);
    const Jb2008 model;
    double worst_rel = 0.0, worst_t1 = 0.0, worst_t2 = 0.0, sumsq = 0.0;
    std::size_t n = 0;
    for (const auto& e : epochs()) {
        CAPTURE(e.name, e.sha256, e.mjd, e.dtc);
        for (const auto& r : e.rows) {
            CAPTURE(r.z_km, r.lon_deg, r.lat_deg);
            const auto got = model.evaluate(make_input(e, r));
            const double rel = std::abs(got.density_kg_m3 - r.rho) / r.rho;
            worst_rel = std::max(worst_rel, rel);
            worst_t1 = std::max(worst_t1, std::abs(got.exospheric_temperature_k - r.t1_k));
            worst_t2 = std::max(worst_t2, std::abs(got.temperature_k - r.t2_k));
            sumsq += rel * rel;
            ++n;
            REQUIRE(got.density_kg_m3 > 0.0);
            REQUIRE(got.temperature_k > 0.0);
            REQUIRE(got.exospheric_temperature_k > 0.0);
        }
    }
    const double rms = std::sqrt(sumsq / static_cast<double>(n));
    // Report the observed residuals so the first native run supplies the numbers
    // that should replace the provisional thresholds above.
    INFO("samples=" << n << " rho_rel_max=" << worst_rel << " rho_rel_rms=" << rms
                   << " dTEMP1_max=" << worst_t1 << "K dTEMP2_max=" << worst_t2 << "K");
    std::printf("{\"model\":\"jb2008\",\"fixtures\":%zu,\"max_density_relative_error\":%.17g,"
        "\"rms_density_relative_error\":%.17g,\"max_exospheric_temperature_error_k\":%.17g,"
        "\"max_local_temperature_error_k\":%.17g}\n",n,worst_rel,rms,worst_t1,worst_t2);
    REQUIRE(worst_rel <= kRhoRelMax);
    REQUIRE(rms <= kRhoRelRms);
    REQUIRE(worst_t1 <= kTempAbsK);
    REQUIRE(worst_t2 <= kTempAbsK);
}

TEST_CASE("JB2008 reproduces twelve full-precision independent reference densities",
          "[atmosphere][jb2008]") {
    const Jb2008 model;
    double worst = 0.0;
    for (std::size_t i = 0; i < kLegacyReferences.size(); ++i) {
        const auto& r = kLegacyReferences[i];
        Jb2008Input in;
        in.mjd_utc = r.mjd_utc;
        in.sun_ra_dec_rad = {r.sun_ra, r.sun_dec};
        in.position_ra_lat_alt = {r.sat_ra, r.sat_geocentric_latitude, r.height_km};
        in.f10 = r.f10; in.f10_average = r.f10b; in.s10 = r.s10; in.s10_average = r.s10b;
        in.m10 = r.m10; in.m10_average = r.m10b; in.y10 = r.y10; in.y10_average = r.y10b;
        in.dstdtc_k = r.dtc;
        const double relative = std::abs(model.evaluate(in).density_kg_m3 - r.rho) / r.rho;
        worst = std::max(worst, relative);
        CAPTURE(i, relative);
        REQUIRE(relative <= kLegacyRtol);
    }
    std::printf("{\"model\":\"jb2008\",\"full_precision_fixtures\":%zu,\"max_relative_error\":%.17g}\n",
                kLegacyReferences.size(), worst);
}

TEST_CASE("JB2008 storm index enters the exospheric temperature exactly", "[atmosphere][jb2008]") {
    if (epochs().empty()) SKIP(kNoFixture);
    const Jb2008 model;
    const Epoch& e = epochs().front();
    const Row r{400.0, 45.0, 25.0, 0.0, 0.0, 0.0};
    const auto base = model.evaluate(make_input(e, r));
    for (double d : {0.0, 13.0, 47.0, 115.0}) {
        Jb2008Input in = make_input(e, r);
        in.dstdtc_k = d;
        const auto got = model.evaluate(in);
        REQUIRE(std::abs((got.exospheric_temperature_k - base.exospheric_temperature_k) - (d-e.dtc)) <=
                1e-9);
    }
}

TEST_CASE("JB2008 is a pure function of its arguments", "[atmosphere][jb2008]") {
    if (epochs().empty()) SKIP(kNoFixture);
    const Jb2008 model;
    const Jb2008Input in = make_input(epochs().front(), Row{412.5, 133.0, -37.0, 0, 0, 0});
    const auto a = model.evaluate(in);
    for (int i = 0; i < 8; ++i) {
        const auto b = model.evaluate(in);
        REQUIRE(b.density_kg_m3 == a.density_kg_m3);
        REQUIRE(b.temperature_k == a.temperature_k);
        REQUIRE(b.exospheric_temperature_k == a.exospheric_temperature_k);
    }
}

TEST_CASE("JB2008 common equator-origin shift preserves the solar hour angle", "[atmosphere][jb2008]") {
    if (epochs().empty()) SKIP(kNoFixture);
    const Jb2008 model;
    const Epoch& e = epochs().front();
    const Row r{430.0, 200.0, -15.0, 0.0, 0.0, 0.0};
    const auto base = model.evaluate(make_input(e, r));
    for (double shift_deg : {-5.0, 0.0, 10.0}) {
        Jb2008Input in = make_input(e, r);
        in.position_ra_lat_alt[0] = wrap_2pi(in.position_ra_lat_alt[0] + shift_deg * kDeg2Rad);
        in.sun_ra_dec_rad[0] = wrap_2pi(in.sun_ra_dec_rad[0] + shift_deg * kDeg2Rad);
        // Both longitudes rotate together; H=SAT(1)-SUN(1) is unchanged.
        const auto got = model.evaluate(in);
        REQUIRE(std::abs(got.density_kg_m3-base.density_kg_m3)/base.density_kg_m3 <= 1e-12);
        REQUIRE(std::abs(got.temperature_k-base.temperature_k) <= 1e-9);
        REQUIRE(std::abs(got.exospheric_temperature_k-base.exospheric_temperature_k) <= 1e-9);
    }
}

TEST_CASE("JB2008 right ascension wrapping is irrelevant", "[atmosphere][jb2008]") {
    if (epochs().empty()) SKIP(kNoFixture);
    const Jb2008 model;
    const Jb2008Input in = make_input(epochs().front(), Row{380.0, 25.0, 55.0, 0, 0, 0});
    const auto base = model.evaluate(in);
    for (double turn : {-6.0, -4.0, 4.0, 10.0}) {
        Jb2008Input w = in;
        w.position_ra_lat_alt[0] += turn * 2.0 * std::numbers::pi;
        const auto got = model.evaluate(w);
        REQUIRE(std::abs(got.density_kg_m3 - base.density_kg_m3) / base.density_kg_m3 <= 1e-12);
        REQUIRE(std::abs(got.temperature_k - base.temperature_k) <= 1e-9);
    }
}


TEST_CASE("JB2008 rejects inputs outside the documented domain", "[atmosphere][jb2008]") {
    const Jb2008 model;
    Jb2008Input in;
    in.mjd_utc = 60035.0;
    in.sun_ra_dec_rad = {0.1, 0.07};
    in.position_ra_lat_alt = {1.0, 0.2, 400.0};
    REQUIRE(model.evaluate(in).density_kg_m3 > 0.0);
    for (double alt : {0.0, 60.0, 119.0, 1501.0, 5000.0}) {
        Jb2008Input bad = in;
        bad.position_ra_lat_alt[2] = alt;
        REQUIRE_THROWS_AS(model.evaluate(bad), std::invalid_argument);
    }
    for (double lat : {-91.0, 91.0, 1000.0}) {
        Jb2008Input bad = in;
        bad.position_ra_lat_alt[1] = lat * kDeg2Rad;
        REQUIRE_THROWS_AS(model.evaluate(bad), std::invalid_argument);
    }
    for (double flux : {0.0, 39.0, -1.0}) {
        Jb2008Input bad = in;
        bad.f10 = flux;
        REQUIRE_THROWS_AS(model.evaluate(bad), std::invalid_argument);
    }
    for (double bad_dtc : {-1.0, 5000.0}) {
        Jb2008Input bad = in;
        bad.dstdtc_k = bad_dtc;
        REQUIRE_THROWS_AS(model.evaluate(bad), std::invalid_argument);
    }
    Jb2008Input nf = in;
    nf.mjd_utc = std::nan("");
    REQUIRE_THROWS_AS(model.evaluate(nf), std::invalid_argument);
    nf = in;
    nf.sun_ra_dec_rad[0] = std::numeric_limits<double>::infinity();
    REQUIRE_THROWS_AS(model.evaluate(nf), std::invalid_argument);
}
