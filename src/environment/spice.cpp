#include "leo/environment/spice.hpp"
#include "SpiceUsr.h"
#include <cmath>
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
}