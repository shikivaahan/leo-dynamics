#pragma once
#include <filesystem>
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
// Real Sun ephemeris computed by CSPICE from kernels the caller lists explicitly.
// CSPICE is linked only by the optional leo::environment component, never by
// leo::kernel, and the force models never load kernels themselves.
//
// Semantics, all fixed by contract: target Sun (10), observer Earth (399),
// frame J2000, geometric NONE correction, output in metres and metres/second.
// Epoch is TDB seconds past J2000 (the same clock leo::Epoch carries); no
// leapsecond or UTC conversion is applied to it.
//
// CSPICE owns process-global state (kernel pool, error subsystem, units) and is
// not thread safe. This provider serializes its own calls on an internal mutex,
// sets the SPICE error action to RETURN so failures surface as exceptions
// instead of aborting, and never clears kernels it does not own. SPICE calls
// made elsewhere in the process must be serialized by their owner too.
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
    static UtcCalendar utc_calendar(Epoch);
    // Explicit frame, no Earth-rotation fallback. Matrix acts on SI J2000 positions.
    static Eigen::Matrix3d earth_fixed_rotation(Epoch,const std::string& frame);
    // Version of the actually linked toolkit, for reproducibility manifests.
    static std::string toolkit_version();
private:
    std::vector<std::string> kernel_files_;
};
}