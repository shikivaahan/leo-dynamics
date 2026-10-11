#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace atm_profile {
using Clock = std::chrono::steady_clock;
enum Id : unsigned {
    provider,
    validation,
    rotation,
    transform,
    geodesy,
    utc,
    weather,
    geometry,
    native,
    sun_total,
    sun_transform,
    rot_validation,
    rot_lock,
    rot_prepare,
    pxform,
    rot_extract,
    utc_validation,
    utc_lock,
    utc_prepare,
    et2utc,
    utc_parse,
    utc_date_format,
    midnight_first,
    midnight_second,
    utc_daylength,
    utc_doy,
    utc_metadata,
    sun_validation,
    sun_lock,
    sun_prepare,
    spkez,
    sun_extract,
    geo_validation,
    geo_initial,
    geo_iteration,
    geo_altitude,
    geo_longitude,
    weather_validation,
    cssi_current,
    cssi_previous,
    cssi_fields,
    ap_history,
    solar_fs,
    solar_m,
    solar_y,
    solar_fields,
    dtc_lookup,
    dtc_interpolate,
    native_validation,
    native_construct,
    native_lock,
    native_execute,
    native_output,
    count
};
inline constexpr std::array<std::string_view, count> names{"provider",
                                                           "validation",
                                                           "rotation",
                                                           "transform",
                                                           "geodesy",
                                                           "utc",
                                                           "weather",
                                                           "geometry",
                                                           "native",
                                                           "sun_total",
                                                           "sun_transform",
                                                           "rot_validation",
                                                           "rot_lock",
                                                           "rot_prepare",
                                                           "pxform",
                                                           "rot_extract",
                                                           "utc_validation",
                                                           "utc_lock",
                                                           "utc_prepare",
                                                           "et2utc",
                                                           "utc_parse",
                                                           "utc_date_format",
                                                           "midnight_first",
                                                           "midnight_second",
                                                           "utc_daylength",
                                                           "utc_doy",
                                                           "utc_metadata",
                                                           "sun_validation",
                                                           "sun_lock",
                                                           "sun_prepare",
                                                           "spkez",
                                                           "sun_extract",
                                                           "geo_validation",
                                                           "geo_initial",
                                                           "geo_iteration",
                                                           "geo_altitude",
                                                           "geo_longitude",
                                                           "weather_validation",
                                                           "cssi_current",
                                                           "cssi_previous",
                                                           "cssi_fields",
                                                           "ap_history",
                                                           "solar_fs",
                                                           "solar_m",
                                                           "solar_y",
                                                           "solar_fields",
                                                           "dtc_lookup",
                                                           "dtc_interpolate",
                                                           "native_validation",
                                                           "native_construct",
                                                           "native_lock",
                                                           "native_execute",
                                                           "native_output"};
struct Record {
    std::array<double, count> ns{};
    std::array<unsigned, count> calls{};
    unsigned iterations = 0;
};
inline thread_local Record *active = nullptr;
inline void add(Id id, double ns) noexcept {
    if (active) {
        active->ns[id] += ns;
        ++active->calls[id];
    }
}
inline double elapsed(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::nano>(b - a).count();
}
struct Timer {
    Clock::time_point last{};
    bool enabled;
    Timer() : enabled(active != nullptr) {
        if (enabled)
            last = Clock::now();
    }
    void mark(Id id) {
        if (enabled) {
            const auto now = Clock::now();
            add(id, elapsed(last, now));
            last = Clock::now();
        }
    }
};
struct Scope {
    Id id;
    Clock::time_point start{};
    bool enabled;
    explicit Scope(Id value) : id(value), enabled(active != nullptr) {
        if (enabled)
            start = Clock::now();
    }
    ~Scope() {
        if (enabled)
            add(id, elapsed(start, Clock::now()));
    }
};
} // namespace atm_profile
