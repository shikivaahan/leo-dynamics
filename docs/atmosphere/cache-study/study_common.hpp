// Shared study primitives: the authoritative native probe, the documented
// grids, the weather record reader and the error histogram. Included by
// support.hpp so every study translation unit, including the performance
// owner's, sees exactly the same reference values and the same grids.
#pragma once

namespace cache_study {

inline constexpr double kDeg = 180.0 / std::numbers::pi;
inline constexpr double kRad = std::numbers::pi / 180.0;

// ---------------------------------------------------------------------------
// Authoritative native probe
// ---------------------------------------------------------------------------

// The single source of every reference density in this study: the production
// provider, unchanged, with every ATM-OPT-001 preprocessing reuse enabled. No
// study code ever produces a reference value any other way, and a value that is
// not finite and positive is a run failure rather than a silently dropped
// sample.
class NativeProbe {
    leo::atmosphere::DirectAtmosphere atmosphere_;
    std::string frame_;

public:
    NativeProbe(const Config& c, Environment& env, leo::atmosphere::Model model, bool ap_history)
        : atmosphere_(model, env.ephemeris, env.weather, c.frame, ap_history, c.options),
          frame_(c.frame) {}

    InertialVector position(leo::Epoch epoch, double latitude_deg, double longitude_deg,
                            double altitude_km) const {
        return inertial_from_geodetic(epoch, latitude_deg * kRad, longitude_deg * kRad,
                                      altitude_km * 1e3, frame_);
    }

    double density(leo::Epoch epoch, double latitude_deg, double longitude_deg, double altitude_km,
                   const leo::SunObservation* sun = nullptr) const {
        return atmosphere_.evaluate(epoch, position(epoch, latitude_deg, longitude_deg, altitude_km),
                                    sun).density_kg_m3;
    }

    double density(const InertialVector& position_m, leo::Epoch epoch,
                   const leo::SunObservation* sun = nullptr) const {
        return atmosphere_.evaluate(epoch, position_m, sun).density_kg_m3;
    }

    leo::atmosphere::Result full(leo::Epoch epoch, double latitude_deg, double longitude_deg,
                                 double altitude_km) const {
        return atmosphere_.evaluate(epoch, position(epoch, latitude_deg, longitude_deg, altitude_km));
    }

    const std::string& frame() const { return frame_; }
};

struct Probe {
    leo::Epoch epoch{};
    double latitude_deg = 0, longitude_deg = 0, altitude_km = 600;
};

inline double probe_density(const NativeProbe& probe, const Probe& point) {
    const double density = probe.density(point.epoch, point.latitude_deg, point.longitude_deg,
                                         point.altitude_km);
    if (!std::isfinite(density) || density <= 0)
        throw std::runtime_error("reference density is not finite and positive at "
                                 + utc_label(point.epoch));
    return density;
}

// Local solar time implied by an epoch and a longitude, for reporting only. The
// models receive their own local solar time from the shared provider; this is
// never fed back into a model input.
inline double local_solar_time_hours(leo::Epoch epoch, double longitude_deg) {
    return grid_local_solar_time_hours(epoch, longitude_deg);
}

// The longitude that places a grid point at a requested local solar time. Used
// only to choose study sample locations, never to override a model input.
inline double longitude_for_local_solar_time(leo::Epoch epoch, double target_hours) {
    const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(epoch);
    double longitude = (target_hours - calendar.seconds_of_day / 3600.0) * 15.0;
    longitude = std::fmod(longitude + 180.0, 360.0);
    if (longitude < 0) longitude += 360.0;
    return longitude - 180.0;
}

// ---------------------------------------------------------------------------
// Calendar helpers
// ---------------------------------------------------------------------------

inline std::string utc_string(int year, int month, int day, int hour, int minute = 0, int second = 0) {
    std::ostringstream text;
    text << std::setfill('0') << std::setw(4) << year << '-' << std::setw(2) << month << '-'
         << std::setw(2) << day << 'T' << std::setw(2) << hour << ':' << std::setw(2) << minute << ':'
         << std::setw(2) << second << 'Z';
    return text.str();
}

inline void civil_date(leo::Epoch epoch, int& year, int& month, int& day) {
    const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(epoch);
    year = calendar.year;
    month = calendar.month;
    day = calendar.day;
}

// Days since 1970-01-01 for a proleptic Gregorian civil date, and back. Used
// only to name candidate epochs inside the pinned records: the resulting stamp
// is still resolved through the real UTC conversion, and a day the pinned
// records do not cover simply fails there. Nothing here invents a weather
// value.
inline long long days_from_civil(int year, int month, int day) {
    year -= month <= 2;
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year =
        (153u * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2u) / 5u +
        static_cast<unsigned>(day) - 1u;
    const unsigned day_of_era = year_of_era * 365u + year_of_era / 4u - year_of_era / 100u + day_of_year;
    return era * 146097 + static_cast<long long>(day_of_era) - 719468;
}

inline void civil_from_days(long long days, int& year, int& month, int& day) {
    days += 719468;
    const long long era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned day_of_era = static_cast<unsigned>(days - era * 146097);
    const unsigned year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const long long y = static_cast<long long>(year_of_era) + era * 400;
    const unsigned day_of_year =
        day_of_era - (365u * year_of_era + year_of_era / 4u - year_of_era / 100u);
    const unsigned mp = (5u * day_of_year + 2u) / 153u;
    day = day_of_year - (153u * mp + 2u) / 5u + 1u;
    month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    year = static_cast<int>(y + (month <= 2));
}

inline void add_days(int& year, int& month, int& day, int offset) {
    civil_from_days(days_from_civil(year, month, day) + offset, year, month, day);
}

inline std::string next_day_utc(int year, int month, int day, int hour, int minute = 0, int second = 0) {
    add_days(year, month, day, 1);
    return utc_string(year, month, day, hour, minute, second);
}

// ---------------------------------------------------------------------------
// Weather record reader: the actual pinned index values, never synthesised
// ---------------------------------------------------------------------------

struct WeatherRecord {
    bool available = false;
    std::string reason;
    bool nrlmsise_requested = true, nrlmsise_read = false;
    bool jb2008_requested = true, jb2008_read = false;
    std::string utc;
    int year = 0, day_of_year = 0;
    double seconds_of_day = 0, day_length_seconds = 0;
    bool leap_second_day = false;
    double ap = 0, f107 = 0, f107_average = 0;
    std::array<double, 7> ap_history{};
    double f10 = 0, f10_average = 0, s10 = 0, s10_average = 0;
    double m10 = 0, m10_average = 0, y10 = 0, y10_average = 0, dstdtc_k = 0, mjd_utc = 0;
    std::string json() const;
};

// Only the records a study mode actually needs are read. A mode that never
// touches JB2008 must not require the JB2008 inputs to exist, and no missing
// record is ever filled in with a default or an estimate.
inline WeatherRecord read_weather(const Config& c, Environment& env, leo::Epoch epoch, bool ap_history,
                                  bool need_nrlmsise, bool need_jb2008) {
    WeatherRecord record;
    record.nrlmsise_requested = need_nrlmsise;
    record.jb2008_requested = need_jb2008;
    const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(epoch);
    record.utc = utc_label(epoch);
    record.year = calendar.year;
    record.day_of_year = calendar.day_of_year;
    record.seconds_of_day = calendar.seconds_of_day;
    record.day_length_seconds = calendar.day_length_seconds;
    record.leap_second_day = calendar.day_length_seconds != 86400.0;
    try {
        if (need_nrlmsise) {
            const auto nrlmsise = env.weather.nrlmsise(calendar, ap_history);
            record.ap = nrlmsise.ap;
            record.f107 = nrlmsise.f107;
            record.f107_average = nrlmsise.f107_average;
            record.ap_history = nrlmsise.ap_history;
            record.nrlmsise_read = true;
        }
        if (need_jb2008) {
            const auto jb2008 = env.weather.jb2008(calendar);
            record.f10 = jb2008.f10;
            record.f10_average = jb2008.f10_average;
            record.s10 = jb2008.s10;
            record.s10_average = jb2008.s10_average;
            record.m10 = jb2008.m10;
            record.m10_average = jb2008.m10_average;
            record.y10 = jb2008.y10;
            record.y10_average = jb2008.y10_average;
            record.dstdtc_k = jb2008.dstdtc_k;
            record.mjd_utc = jb2008.mjd_utc;
            record.jb2008_read = true;
        }
        record.available = true;
    } catch (const std::exception& error) {
        record.available = false;
        record.reason = error.what();
    }
    return record;
}

inline WeatherRecord read_weather(const Config& c, Environment& env, leo::Epoch epoch, bool ap_history) {
    return read_weather(c, env, epoch, ap_history, true, true);
}

inline std::string WeatherRecord::json() const {
    Json j;
    j.add("available", available);
    j.add("records_requested", [&] {
        std::vector<std::string> names;
        if (nrlmsise_requested) names.emplace_back("nrlmsise00");
        if (jb2008_requested) names.emplace_back("jb2008");
        return strings(names);
    }());
    j.add("records_read", [&] {
        std::vector<std::string> names;
        if (nrlmsise_read) names.emplace_back("nrlmsise00");
        if (jb2008_read) names.emplace_back("jb2008");
        return strings(names);
    }());
    if (!available) {
        j.add("unavailable_reason", reason);
        return j.str();
    }
    j.add("utc", utc);
    j.add("year", static_cast<double>(year));
    j.add("day_of_year", static_cast<double>(day_of_year));
    j.add("seconds_of_day", seconds_of_day);
    j.add("day_length_seconds", day_length_seconds);
    j.add("leap_second_day", leap_second_day);
    if (nrlmsise_read) {
        Json n;
        n.add("f107", f107);
        n.add("f107_average", f107_average);
        n.add("ap", ap);
        std::vector<std::string> ap_values;
        for (double value : ap_history) {
            std::ostringstream encoded;
            if (std::isfinite(value)) encoded << std::setprecision(17) << value;
            else encoded << "null";
            ap_values.push_back(encoded.str());
        }
        n.raw("ap_vector", raw_array(ap_values));
        n.add("ap_vector_null_semantics", "unused history slots in daily-Ap mode; not fabricated weather values");
        j.raw("nrlmsise00", n.str());
    }
    if (jb2008_read) {
        Json b;
        b.add("f10", f10);
        b.add("f10_average", f10_average);
        b.add("s10", s10);
        b.add("s10_average", s10_average);
        b.add("m10", m10);
        b.add("m10_average", m10_average);
        b.add("y10", y10);
        b.add("y10_average", y10_average);
        b.add("dstdtc_k", dstdtc_k);
        b.add("dstdtc_note",
              "integer-valued after truncation of the hourly interpolation, so it changes within an hour");
        b.add("mjd_utc", mjd_utc);
        j.raw("jb2008", b.str());
    }
    return j.str();
}

// ---------------------------------------------------------------------------
// The one definition of the space-weather identity
// ---------------------------------------------------------------------------

// Bitwise copy of the model's complete discrete space-weather vector at an
// epoch, plus the civil day, the leap-day flag and the Ap-history switch.
//
// This is deliberately a single free function: DensityEvaluator::build_inputs
// calls it, and any study analysis that needs to know whether two epochs may
// share a stored or interpolated value calls the same one. There is no second
// definition to keep in step, and nothing here is hashed: the words are the
// inputs' own bit patterns, compared with array ordering.
inline WeatherIdentity weather_identity(const Config& c, const Environment& env,
                                        leo::atmosphere::Model model, leo::Epoch epoch) {
    WeatherIdentity weather;
    if (model == leo::atmosphere::Model::exponential) return weather;
    const auto utc = leo::SpiceEphemerisProvider::utc_calendar(epoch, c.options.utc_reuse);
    // The civil day is discrete and is part of the identity: no value is ever
    // reused or interpolated across UTC midnight, which is also the daily solar
    // and daily-Ap record boundary.
    weather.add_word(static_cast<std::uint64_t>(utc.days_since_1970) ^ 0x9e3779b97f4a7c15ull);
    weather.add_word(static_cast<std::uint64_t>(utc.day_length_seconds == 86400.0 ? 0 : 1));
    weather.add_word(static_cast<std::uint64_t>(c.ap_history ? 1 : 0));
    if (model == leo::atmosphere::Model::nrlmsise00) {
        const auto in = env.weather.nrlmsise(utc, c.ap_history);
        weather.add_double(in.f107);
        weather.add_double(in.f107_average);
        weather.add_double(in.ap);
        // The Ap-history vector holds the current daily value, the four 3-hourly
        // bins and the two 8-bin means, and is quiet-NaN filled when the history
        // switch is off. Both are discrete, and identical bit patterns are what
        // makes the identity stable when the history switch is off.
        for (double value : in.ap_history) weather.add_double(value);
        weather.add_word(static_cast<std::uint64_t>(in.year));
        weather.add_word(static_cast<std::uint64_t>(in.day_of_year));
    } else {
        const auto in = env.weather.jb2008(utc);
        weather.add_double(in.f10);
        weather.add_double(in.f10_average);
        weather.add_double(in.s10);
        weather.add_double(in.s10_average);
        weather.add_double(in.m10);
        weather.add_double(in.m10_average);
        weather.add_double(in.y10);
        weather.add_double(in.y10_average);
        // dstdtc_k is an integer produced by truncating an hourly interpolation,
        // so it changes within an hour. Carrying it bitwise is what stops a cell
        // or an interpolation from ever spanning one of those transitions.
        weather.add_double(in.dstdtc_k);
    }
    return weather;
}

// ---------------------------------------------------------------------------
// Documented grids
// ---------------------------------------------------------------------------

inline const std::vector<double>& altitude_grid_km() {
    static const std::vector<double> v{200, 250, 300, 350, 400, 450, 500, 550, 600,
                                       650, 700, 750, 800, 850, 900, 950, 1000};
    return v;
}

// Includes the near-polar limit of the study's own fallback rule on both
// hemispheres, so a reader can see exactly where that rule engages.
inline const std::vector<double>& latitude_grid_deg() {
    static const std::vector<double> v{-89.999, -89.9, -80, -60, -45, -30, 0, 30, 45, 60, 80, 89.9, 89.999};
    return v;
}

// Includes the +180/-180 seam on both sides so a wraparound artefact is
// measurable rather than hypothetical.
inline const std::vector<double>& longitude_grid_deg() {
    static const std::vector<double> v{-180, -179.9, -120, -60, -30, 0, 30, 60, 120, 179.9, 180};
    return v;
}

// Midnight, dawn, morning, noon, afternoon, dusk, evening.
inline const std::vector<double>& local_solar_time_grid_hours() {
    static const std::vector<double> v{0.0, 6.0, 9.0, 12.0, 15.0, 18.0, 21.0};
    return v;
}

// Both far below and far above the candidate cell widths, so linearity is
// measured rather than assumed.
inline const std::vector<double>& altitude_perturbations_km() {
    static const std::vector<double> v{0.001, 0.01, 0.1, 1.0, 5.0, 10.0, 25.0, 50.0, 100.0};
    return v;
}

// Milliseconds through hours: adaptive integrator stage spacing up to a weather
// record interval.
inline const std::vector<double>& time_separations_s() {
    static const std::vector<double> v{0.001, 0.01, 0.1, 1.0, 10.0, 60.0, 300.0, 900.0,
                                       1800.0, 3600.0, 10800.0, 21600.0};
    return v;
}

// The leap-second transition the pinned IAU_EARTH/ITRF93 kernels cover.
inline std::string leap_boundary_before_utc() { return utc_string(2016, 12, 31, 23, 59, 59); }
inline std::string leap_boundary_after_utc() { return utc_string(2017, 1, 1, 0, 0, 0); }

// ---------------------------------------------------------------------------
// Error histogram
// ---------------------------------------------------------------------------

struct ErrorHistogram {
    // Bins of max/min density ratio minus one; callers supply a log-density span.
    static constexpr std::size_t bin_count = 11;
    static const std::vector<double>& edges() {
        static const std::vector<double> v{1e-6, 1e-5, 1e-4, 3e-4, 1e-3,
                                           3e-3, 1e-2, 3e-2, 1e-1, 3e-1};
        return v;
    }
    std::array<std::uint64_t, bin_count> counts{};
    std::size_t total = 0;
    std::size_t below_1e4 = 0, below_1e3 = 0, below_5e3 = 0, below_1e2 = 0, below_5e2 = 0;

    void add(double magnitude) {
        magnitude = std::expm1(std::abs(magnitude));
        ++total;
        if (magnitude <= 1e-4) ++below_1e4;
        if (magnitude <= 1e-3) ++below_1e3;
        if (magnitude <= 5e-3) ++below_5e3;
        if (magnitude <= 1e-2) ++below_1e2;
        if (magnitude <= 5e-2) ++below_5e2;
        std::size_t bin = bin_count - 1;
        const auto& limits = edges();
        for (std::size_t index = 0; index < limits.size(); ++index)
            if (magnitude < limits[index]) { bin = index; break; }
        ++counts[bin];
    }

    std::string json() const {
        Json j;
        std::vector<std::string> bins;
        for (std::size_t index = 0; index < bin_count; ++index)
            bins.push_back(std::to_string(counts[index]));
        j.raw("bin_counts", raw_array(bins));
        std::vector<double> limits = edges();
        limits.insert(limits.begin(), 0.0);
        limits.push_back(1e9);
        j.raw("bin_edges", numbers(limits));
        j.add("total", total);
        Json fractions;
        const double count = total ? double(total) : 1.0;
        fractions.add("at_or_below_0.01_percent", double(below_1e4) / count);
        fractions.add("at_or_below_0.1_percent", double(below_1e3) / count);
        fractions.add("at_or_below_0.5_percent", double(below_5e3) / count);
        fractions.add("at_or_below_1_percent", double(below_1e2) / count);
        fractions.add("at_or_below_5_percent", double(below_5e2) / count);
        j.raw("fraction_at_or_below", fractions.str());
        return j.str();
    }
};

struct WeatherEpoch {
    std::string name, utc;
    leo::Epoch epoch{};
    WeatherRecord record;
};

// The frozen caller-supplied fixture epochs plus real additional epochs inside
// the same pinned records, read at the records' own three-hourly cadence. A day
// the pinned records do not cover fails here and is left out, never substituted.
std::vector<WeatherEpoch> build_epoch_pool(const Config& c, Environment& env, const StudySettings& s,
                                           bool need_nrlmsise = true, bool need_jb2008 = true) {
    std::vector<WeatherEpoch> pool;
    std::vector<std::pair<std::string, leo::Epoch>> anchors;
    if (c.fixture_epochs.empty()) {
        anchors.emplace_back("run-epoch", leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc));
    } else {
        for (const auto& fixture : c.fixture_epochs) anchors.emplace_back(fixture.name, fixture.epoch);
    }
    for (const auto& [name, epoch] : anchors)
        pool.push_back({name, utc_label(epoch), epoch, read_weather(c, env, epoch, c.ap_history, need_nrlmsise, need_jb2008)});

    for (const auto& [name, epoch] : anchors) {
        if (!read_weather(c, env, epoch, c.ap_history, need_nrlmsise, need_jb2008).available) continue;
        int year = 0, month = 0, day = 0;
        civil_date(epoch, year, month, day);
        for (int offset = 1; offset <= static_cast<int>(s.fixture_scan_days); ++offset)
            for (int sign : {1, -1})
                for (int hour : {0, 3, 6, 9, 12, 15, 18, 21}) {
                    int y = year, m = month, d = day;
                    add_days(y, m, d, sign * offset);
                    try {
                        const leo::Epoch at =
                            leo::SpiceEphemerisProvider::epoch_from_utc(utc_string(y, m, d, hour));
                        pool.push_back({name + "+" + std::to_string(sign * offset) + "d+" +
                                            std::to_string(hour) + "h",
                                        utc_label(at), at, read_weather(c, env, at, c.ap_history, need_nrlmsise, need_jb2008)});
                    } catch (const std::exception&) {
                        // Outside the pinned records: not part of the pool.
                    }
                }
    }
    return pool;
}

// The primary regimes are exactly the caller's pinned fixture epochs. They are
// never replaced by a pick from the scan.
std::vector<WeatherEpoch> select_regimes(const Config& c, Environment& env, std::vector<WeatherEpoch>& pool) {
    std::vector<WeatherEpoch> selected;
    for (const auto& fixture : c.fixture_epochs) {
        const auto found = std::find_if(pool.begin(), pool.end(), [&](const WeatherEpoch& entry) {
            return entry.name == fixture.name && entry.epoch.et_seconds == fixture.epoch.et_seconds;
        });
        if (found == pool.end() || !found->record.available)
            throw std::runtime_error("requested weather fixture unavailable: " + fixture.name);
        selected.push_back(*found);
    }
    if (selected.empty())
        for (const auto& entry : pool)
            if (entry.record.available) { selected.push_back(entry); break; }
    if (selected.empty())
        throw std::runtime_error("no pinned weather record covers any requested epoch; the study "
                                 "refuses to run without real space-weather inputs");
    (void)env;
    return selected;
}

// ---------------------------------------------------------------------------
// A canonical study state, derived from a real case when one is supplied
// ---------------------------------------------------------------------------

struct StudyState {
    leo::Epoch epoch{};
    InertialVector position{};
    std::string origin;
};

// The study state is a real CASE start state when --case is supplied, and
// otherwise a documented circular orbit at the study altitude. It is never a
// synthetic value injected into a model.
inline StudyState study_state(const Config& c, const StudySettings& s) {
    StudyState state;
    state.epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
    if (!c.case_files.empty()) {
        const auto spec = experiment::load(c.case_files.front());
        const State start = spec.start();
        state.position = start.r;
        state.origin = "start state of case " + spec.id + " (" + c.case_files.front() + ")";
        return state;
    }
    const double radius = leo::atmosphere::wgs84_a_m + s.h_km * 1e3;
    const double speed = std::sqrt(3.986004418e14 / radius);
    const double inclination = 51.64 * kRad;
    Eigen::Vector3d position{radius * std::cos(inclination), 0.0, radius * std::sin(inclination)};
    Eigen::Vector3d velocity{0.0, speed * std::sin(inclination), speed * std::cos(inclination)};
    state.position = InertialVector{position};
    (void)velocity;
    state.origin = "documented circular state at " + std::to_string(s.h_km) +
                   " km, 51.64 degree inclination, position only";
    return state;
}

} // namespace cache_study