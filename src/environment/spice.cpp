#include "leo/environment/spice.hpp"
#include "SpiceUsr.h"
#include <array>
#include <bit>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
namespace leo {
namespace {
// CSPICE state is process global and unsynchronised, so every entry point below
// holds this mutex; SPICE calls made outside leo stay the caller's problem.
std::mutex spice_mutex;
// Kernels this library loaded, keyed by the exact path handed to furnsh_c, with
// the number of providers currently holding them.
std::map<std::string,int> loaded_kernels;
std::uint64_t configuration_generation=1;
SpiceReuseStatistics statistics;
struct CivilDay {
    long long serial=0;
    int day_of_year=0;
    double start_et=0,end_et=0,length=0;
    std::uint64_t generation=0;
};
std::array<CivilDay,32> civil_days;
std::size_t civil_next=0;
#ifdef LEO_SPICE_REUSE_DIAGNOSTICS
#define REUSE_COUNT(field) (++statistics.field)
class SpiceLock {
    std::unique_lock<std::mutex> lock_;
public:
    explicit SpiceLock(std::mutex& mutex):lock_(mutex,std::defer_lock) { lock(); }
    void lock() {
        const auto start=std::chrono::steady_clock::now();
        const bool contended=!lock_.try_lock();
        if(contended) lock_.lock();
        ++statistics.mutex_acquisitions;
        statistics.mutex_contended+=contended;
        statistics.mutex_wait_ns+=static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
    }
    void unlock() { lock_.unlock(); }
};
#else
#define REUSE_COUNT(field) ((void)0)
using SpiceLock=std::unique_lock<std::mutex>;
#endif
void invalidate_locked() {
    // Never permit generation wrap to revive old observations or cache entries.
    if(++configuration_generation==0) std::terminate();
    for(auto& entry:civil_days) entry.generation=0;
    civil_next=0;
    REUSE_COUNT(invalidations);
}

void enter_spice() {
    // RETURN keeps SPICE from aborting the process; errors are rethrown below.
    reset_c();
    SpiceChar action[]="RETURN";
    erract_c("SET",0,action);
}
[[noreturn]] void throw_spice_error(const std::string& context) {
    char message[64]={};
    getmsg_c("SHORT",static_cast<SpiceInt>(sizeof(message)),message);
    const std::string detail=message;
    reset_c();
    throw std::runtime_error("CSPICE "+context+" failed: "+detail);
}
std::string kernel_key(const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::path absolute=std::filesystem::absolute(file,ec);
    if(ec) absolute=file;
    return absolute.lexically_normal().string();
}
void release_kernel(const std::string& key) {
    const auto it=loaded_kernels.find(key);
    if(it==loaded_kernels.end()||--it->second>0) return;
    loaded_kernels.erase(it);
    invalidate_locked();
    unload_c(key.c_str()); // never fails for an unloaded file; user state is left alone
    reset_c();
}
}
SpiceEphemerisProvider::SpiceEphemerisProvider(std::vector<std::filesystem::path> kernel_files) {
    if(kernel_files.empty()) throw std::invalid_argument("no SPICE kernels supplied");
    kernel_files_.reserve(kernel_files.size());
    for(const auto& file:kernel_files) {
        std::error_code ec;
        if(!std::filesystem::exists(file,ec)||ec)
            throw std::invalid_argument("SPICE kernel file not found: "+file.string());
        kernel_files_.push_back(kernel_key(file));
    }
    SpiceLock lock{spice_mutex};
    enter_spice();
    std::size_t acquired=0;
    try {
        for(const auto& key:kernel_files_) {
            auto [it,inserted]=loaded_kernels.try_emplace(key,0);
            (void)inserted;
            if(it->second==0) {
                invalidate_locked();
                furnsh_c(key.c_str());
                if(failed_c()) {
                    // furnsh may partially load a metakernel before failing.
                    try { throw_spice_error("loading kernel '"+key+"'"); }
                    catch(...) {
                        invalidate_locked();
                        unload_c(key.c_str());
                        reset_c();
                        loaded_kernels.erase(it);
                        throw;
                    }
                }
            }
            ++it->second;
            ++acquired;
        }
    } catch(...) {
        for(std::size_t i=0;i<acquired;++i) release_kernel(kernel_files_[i]);
        throw;
    }
}
SpiceEphemerisProvider::~SpiceEphemerisProvider() {
    SpiceLock lock{spice_mutex};
    for(const auto& key:kernel_files_) release_kernel(key);
}
namespace {
EphemerisState sun_state_locked(Epoch epoch) {
    if(!std::isfinite(epoch.et_seconds))
        throw std::invalid_argument("nonfinite ephemeris epoch (TDB seconds past J2000)");
    enter_spice();
    SpiceDouble state[6]={},light_time=0;
    REUSE_COUNT(spkez_calls);
    spkez_c(sun_naif_id,epoch.et_seconds,"J2000","NONE",earth_naif_id,state,&light_time);
    if(failed_c())
        throw_spice_error("Sun ephemeris at ET="+std::to_string(epoch.et_seconds)
            +" s past J2000 (check kernel coverage)");
    constexpr double m_per_km=1e3;
    EphemerisState result{InertialVector(state[0]*m_per_km,state[1]*m_per_km,state[2]*m_per_km),
        InertialVector(state[3]*m_per_km,state[4]*m_per_km,state[5]*m_per_km)};
    if(!result.position_m.as_eigen().allFinite()||!result.velocity_m_s.as_eigen().allFinite())
        throw std::runtime_error("nonfinite Sun ephemeris state at ET="+std::to_string(epoch.et_seconds)+" s past J2000");
    return result;
}
}
EphemerisState SpiceEphemerisProvider::geometric_state(int target_naif_id,Epoch epoch) const {
    if(target_naif_id!=sun_naif_id)
        throw std::invalid_argument("unsupported SPICE target NAIF id "+std::to_string(target_naif_id)
            +"; this provider serves the Sun ("+std::to_string(sun_naif_id)+") only");
    SpiceLock lock{spice_mutex};
    return sun_state_locked(epoch);
}
SunObservation SpiceEphemerisProvider::observe_sun(Epoch epoch) const {
    SpiceLock lock{spice_mutex};
    return SunObservation{epoch,sun_state_locked(epoch),configuration_generation};
}
bool SpiceEphemerisProvider::compatible_sun(const SunObservation& observation,Epoch epoch) const {
    SpiceLock lock{spice_mutex};
    return std::bit_cast<std::uint64_t>(observation.epoch_.et_seconds)==std::bit_cast<std::uint64_t>(epoch.et_seconds)
        && observation.generation_==configuration_generation;
}
Epoch SpiceEphemerisProvider::epoch_from_utc(std::string_view utc) {
    const std::string time{utc};
    SpiceLock lock{spice_mutex};
    enter_spice();
    SpiceDouble et=0;
    REUSE_COUNT(str2et_calls);
    str2et_c(time.c_str(),&et);
    if(failed_c()) throw_spice_error("UTC to ET conversion of '"+time+"' (a leapseconds kernel is required)");
    if(!std::isfinite(et)) throw std::runtime_error("SPICE returned a nonfinite ET for '"+time+"'");
    return Epoch{et};
}
UtcCalendar SpiceEphemerisProvider::utc_calendar(Epoch epoch,bool reuse) {
    if(!std::isfinite(epoch.et_seconds)) throw std::invalid_argument("nonfinite UTC epoch");
    for(;;) {
        SpiceLock lock{spice_mutex};
        const auto generation=configuration_generation;
        enter_spice();
        char text[64]{};
        REUSE_COUNT(et2utc_calls);
        et2utc_c(epoch.et_seconds,"ISOC",9,sizeof(text),text);
        if(failed_c()) throw_spice_error("ET to UTC conversion (LSK required)");
        lock.unlock();
        int y=0,m=0,d=0,h=0,minute=0;double seconds=0;
        if(std::sscanf(text,"%d-%d-%dT%d:%d:%lf",&y,&m,&d,&h,&minute,&seconds)!=6)
            throw std::runtime_error("unsupported SPICE UTC calendar representation");
        using namespace std::chrono;
        const year_month_day date{year{y},month{static_cast<unsigned>(m)},day{static_cast<unsigned>(d)}};
        if(!date.ok()) throw std::runtime_error("invalid SPICE UTC calendar date");
        const sys_days today{date};
        const auto serial=today.time_since_epoch().count();
        lock.lock();
        // Parsing is outside the SPICE lock. A concurrent mutation requires a
        // fresh conversion, never a mixture of calendars from two generations.
        if(generation!=configuration_generation) continue;
        CivilDay metadata;
        bool hit=false;
        if(reuse) for(const auto& entry:civil_days)
            if(entry.generation==generation && entry.serial==serial) {
                metadata=entry;
                hit=true;
                break;
            }
        if(hit) {
            REUSE_COUNT(utc_hits);
        } else {
            REUSE_COUNT(utc_misses);
            // No kernel state is used in date formatting.
            lock.unlock();
            const year_month_day tomorrow{today+days{1}};
            char start[40]{},end[40]{};
            std::snprintf(start,sizeof(start),"%04d-%02d-%02dT00:00:00",y,m,d);
            std::snprintf(end,sizeof(end),"%04d-%02u-%02uT00:00:00",
                static_cast<int>(tomorrow.year()),static_cast<unsigned>(tomorrow.month()),static_cast<unsigned>(tomorrow.day()));
            metadata.serial=serial;
            metadata.day_of_year=static_cast<int>((today-sys_days{year{y}/January/1}).count())+1;
            lock.lock();
            if(generation!=configuration_generation) continue;
            enter_spice();
            REUSE_COUNT(str2et_calls);
            str2et_c(start,&metadata.start_et);
            REUSE_COUNT(str2et_calls);
            str2et_c(end,&metadata.end_et);
            if(failed_c()) throw_spice_error("UTC day-length conversion");
            // TDB rate differs from UTC by much less than 0.5 second per day.
            metadata.length=std::round(metadata.end_et-metadata.start_et);
            if(metadata.length<86399 || metadata.length>86401) throw std::runtime_error("unsupported UTC day length");
            metadata.generation=generation;
            if(reuse) {
                // Another reader may have populated this date while formatting.
                bool present=false;
                for(const auto& entry:civil_days)
                    present|=entry.generation==generation && entry.serial==serial;
                if(!present) {
                    if(civil_days[civil_next].generation!=0) REUSE_COUNT(utc_evictions);
                    civil_days[civil_next]=metadata;
                    civil_next=(civil_next+1)%civil_days.size();
                }
            }
        }
        lock.unlock();
        const double sod=3600*h+60*minute+seconds;
        return {y,m,d,metadata.day_of_year,serial,sod,metadata.length,
            40587+static_cast<double>(serial)+sod/metadata.length};
    }
}
Eigen::Matrix3d SpiceEphemerisProvider::earth_fixed_rotation(Epoch epoch,const std::string& frame,bool reuse) {
    (void)reuse; // Enabled by the separately attributable orientation change.
    if(!std::isfinite(epoch.et_seconds) || (frame!="ITRF93" && frame!="IAU_EARTH"))
        throw std::invalid_argument("Earth-fixed frame must explicitly be ITRF93 or IAU_EARTH");
    SpiceLock lock{spice_mutex};
    enter_spice();
    SpiceDouble rotation[3][3]{};
    REUSE_COUNT(pxform_calls);
    pxform_c("J2000",frame.c_str(),epoch.et_seconds,rotation);
    if(failed_c()) throw_spice_error("J2000 to "+frame+" rotation (covering orientation kernel required)");
    Eigen::Matrix3d result;
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) result(i,j)=rotation[i][j];
    return result;
}
std::string SpiceEphemerisProvider::toolkit_version() {
    SpiceLock lock{spice_mutex};
    enter_spice();
    const char* version=tkvrsn_c("TOOLKIT");
    if(failed_c()) throw_spice_error("toolkit version query");
    return version;
}
void SpiceEphemerisProvider::invalidate_reuse() {
    SpiceLock lock{spice_mutex};
    invalidate_locked();
}
SpiceReuseStatistics SpiceEphemerisProvider::reuse_statistics() {
    std::lock_guard lock{spice_mutex};
    auto result=statistics;
    result.cache_bytes=sizeof(civil_days)+sizeof(civil_next);
    for(const auto& entry:civil_days) result.utc_entries+=entry.generation!=0;
    return result;
}
void SpiceEphemerisProvider::reset_reuse_statistics() {
    std::lock_guard lock{spice_mutex};
    statistics={};
}
}