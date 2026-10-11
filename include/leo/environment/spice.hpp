#pragma once
#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "leo/environment/ephemeris.hpp"
namespace leo {
inline constexpr int sun_naif_id=10;
inline constexpr int earth_naif_id=399;
struct UtcCalendar {
    int year,month,day,day_of_year;
    long long days_since_1970;
    double seconds_of_day,day_length_seconds,mjd;
};
// Counters are populated only in LEO_SPICE_REUSE_DIAGNOSTICS builds.
struct SpiceReuseStatistics {
    std::uint64_t utc_hits=0,utc_misses=0,utc_evictions=0;
    std::uint64_t orientation_hits=0,orientation_misses=0,orientation_evictions=0;
    std::uint64_t invalidations=0,et2utc_calls=0,str2et_calls=0,pxform_calls=0,spkez_calls=0;
    std::uint64_t mutex_acquisitions=0,mutex_contended=0,mutex_wait_ns=0;
    std::size_t utc_entries=0,orientation_entries=0,utc_capacity=32,orientation_capacity=256,cache_bytes=0;
};
// Real Sun ephemeris computed by CSPICE from kernels the caller lists explicitly.
// CSPICE is linked only by the optional leo::environment component, never by
// leo::kernel, and the force models never load kernels themselves.
//
// Semantics, all fixed by contract: target Sun (10), observer Earth (399),
// frame J2000, geometric NONE correction, output in metres and metres/second.
// Epoch is TDB seconds past J2000 (the same clock leo::Epoch carries); no
// leapsecond or UTC conversion is applied to it.
//
// CSPICE owns process-global state and is not thread safe. All library calls,
// managed kernel mutations and reuse hits serialize on the same internal mutex.
// Supported lifecycle: no external CSPICE calls/mutations while library readers
// or lifecycle operations are active. For a quiescent external reconfiguration,
// call invalidate_reuse() before resuming library calls. Arbitrary unreported
// external mutation cannot be detected and is unsupported.
//
// Kernel ownership: every provider furnsh()s its files once per shared owner.
// A listed file is unloaded when the last provider holding it is destroyed, so
// two providers on the same file stay usable until both are gone. Do not list a
// file that application code also loads and expects to stay loaded.
class SpiceEphemerisProvider final : public EphemerisProvider {
public:
    // std::filesystem::path is implicit from const char*, so brace lists of
    // string literals, arguments or paths all bind.
    // Throws std::invalid_argument when no file is listed or a file is missing,
    // std::runtime_error when a file cannot be loaded.
    explicit SpiceEphemerisProvider(std::vector<std::filesystem::path> kernel_files);
    ~SpiceEphemerisProvider() override;
    SpiceEphemerisProvider(const SpiceEphemerisProvider&)=delete;
    SpiceEphemerisProvider& operator=(const SpiceEphemerisProvider&)=delete;
    // target_naif_id must be sun_naif_id: std::invalid_argument otherwise.
    // Non-finite epochs are std::invalid_argument; missing kernel coverage,
    // SPICE failures and non-finite results are std::runtime_error.
    EphemerisState geometric_state(int target_naif_id,Epoch) const override;
    // Optional convenience for callers holding UTC strings: needs a leapseconds
    // kernel (LSK) among kernel_files, otherwise std::runtime_error.
    static Epoch epoch_from_utc(std::string_view utc);
    // UTC quasi-MJD stretches leap-second days to their actual 86401 seconds.
    // seconds_of_day retains 86400 during a positive leap second.
    static UtcCalendar utc_calendar(Epoch,bool reuse=true);
    // Explicit frame, no Earth-rotation fallback. Matrix acts on SI J2000 positions.
    static Eigen::Matrix3d earth_fixed_rotation(Epoch,const std::string& frame);
    // Version of the actually linked toolkit, for reproducibility manifests.
    static std::string toolkit_version();
    // Process-owned bounded caches; all managed furnsh/unload attempts invalidate.
    // UTC: 32 exact civil dates, FIFO eviction; per-request ISOC precision stays 9.
    // External mutation notification requires application-wide quiescence.
    static void invalidate_reuse();
    static SpiceReuseStatistics reuse_statistics();
    static void reset_reuse_statistics();
private:
    std::vector<std::string> kernel_files_;
};
}