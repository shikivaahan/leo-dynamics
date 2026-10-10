#include "leo/environment/spice.hpp"
#include "SpiceUsr.h"
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
    std::lock_guard lock{spice_mutex};
    enter_spice();
    std::size_t acquired=0;
    try {
        for(const auto& key:kernel_files_) {
            auto [it,inserted]=loaded_kernels.try_emplace(key,0);
            (void)inserted;
            if(it->second==0) {
                furnsh_c(key.c_str());
                if(failed_c()) {
                    // furnsh may partially load a metakernel before failing.
                    try { throw_spice_error("loading kernel '"+key+"'"); }
                    catch(...) {
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
    std::lock_guard lock{spice_mutex};
    for(const auto& key:kernel_files_) release_kernel(key);
}
EphemerisState SpiceEphemerisProvider::geometric_state(int target_naif_id,Epoch epoch) const {
    if(target_naif_id!=sun_naif_id)
        throw std::invalid_argument("unsupported SPICE target NAIF id "+std::to_string(target_naif_id)
            +"; this provider serves the Sun ("+std::to_string(sun_naif_id)+") only");
    if(!std::isfinite(epoch.et_seconds))
        throw std::invalid_argument("nonfinite ephemeris epoch (TDB seconds past J2000)");
    std::lock_guard lock{spice_mutex};
    enter_spice();
    SpiceDouble state[6]={},light_time=0;
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
Epoch SpiceEphemerisProvider::epoch_from_utc(std::string_view utc) {
    std::lock_guard lock{spice_mutex};
    enter_spice();
    const std::string time{utc};
    SpiceDouble et=0;
    str2et_c(time.c_str(),&et);
    if(failed_c()) throw_spice_error("UTC to ET conversion of '"+time+"' (a leapseconds kernel is required)");
    if(!std::isfinite(et)) throw std::runtime_error("SPICE returned a nonfinite ET for '"+time+"'");
    return Epoch{et};
}
UtcCalendar SpiceEphemerisProvider::utc_calendar(Epoch epoch) {
    if(!std::isfinite(epoch.et_seconds)) throw std::invalid_argument("nonfinite UTC epoch");
    std::lock_guard lock{spice_mutex};
    enter_spice();
    char text[64]{};
    et2utc_c(epoch.et_seconds,"ISOC",9,sizeof(text),text);
    if(failed_c()) throw_spice_error("ET to UTC conversion (LSK required)");
    int y=0,m=0,d=0,h=0,minute=0;double seconds=0;
    if(std::sscanf(text,"%d-%d-%dT%d:%d:%lf",&y,&m,&d,&h,&minute,&seconds)!=6)
        throw std::runtime_error("unsupported SPICE UTC calendar representation");
    using namespace std::chrono;
    const year_month_day date{year{y},month{static_cast<unsigned>(m)},day{static_cast<unsigned>(d)}};
    if(!date.ok()) throw std::runtime_error("invalid SPICE UTC calendar date");
    const sys_days today{date};
    const year_month_day tomorrow{today+days{1}};
    char start[40]{},end[40]{};
    std::snprintf(start,sizeof(start),"%04d-%02d-%02dT00:00:00",y,m,d);
    std::snprintf(end,sizeof(end),"%04d-%02u-%02uT00:00:00",
        static_cast<int>(tomorrow.year()),static_cast<unsigned>(tomorrow.month()),static_cast<unsigned>(tomorrow.day()));
    double start_et=0,end_et=0;
    str2et_c(start,&start_et);str2et_c(end,&end_et);
    if(failed_c()) throw_spice_error("UTC day-length conversion");
    // TDB rate differs from UTC by much less than 0.5 second per day.
    const double length=std::round(end_et-start_et);
    if(length<86399 || length>86401) throw std::runtime_error("unsupported UTC day length");
    const auto serial=today.time_since_epoch().count();
    const int doy=static_cast<int>((today-sys_days{year{y}/January/1}).count())+1;
    const double sod=3600*h+60*minute+seconds;
    return {y,m,d,doy,serial,sod,length,40587+static_cast<double>(serial)+sod/length};
}
Eigen::Matrix3d SpiceEphemerisProvider::earth_fixed_rotation(Epoch epoch,const std::string& frame) {
    if(!std::isfinite(epoch.et_seconds) || (frame!="ITRF93" && frame!="IAU_EARTH"))
        throw std::invalid_argument("Earth-fixed frame must explicitly be ITRF93 or IAU_EARTH");
    std::lock_guard lock{spice_mutex};
    enter_spice();
    SpiceDouble rotation[3][3]{};
    pxform_c("J2000",frame.c_str(),epoch.et_seconds,rotation);
    if(failed_c()) throw_spice_error("J2000 to "+frame+" rotation (covering orientation kernel required)");
    Eigen::Matrix3d result;
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) result(i,j)=rotation[i][j];
    return result;
}
std::string SpiceEphemerisProvider::toolkit_version() {
    std::lock_guard lock{spice_mutex};
    enter_spice();
    const char* version=tkvrsn_c("TOOLKIT");
    if(failed_c()) throw_spice_error("toolkit version query");
    return version;
}
}