// Diagnostic-only GNU linker interposition. Original source remains unchanged.
#include "SpiceUsr.h"
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <pthread.h>

namespace {
// ET->UTC, UTC->ET, rotation, Sun, SPICE acquisitions, contended, wait ns.
std::array<std::atomic<std::uint64_t>,7> counters{};
std::atomic<pthread_mutex_t*> identified_spice_mutex{nullptr};
thread_local pthread_mutex_t* last_mutex=nullptr;
void identify_spice_mutex() {
    auto* expected=static_cast<pthread_mutex_t*>(nullptr);
    if(!last_mutex) std::abort();
    if(!identified_spice_mutex.compare_exchange_strong(expected,last_mutex,std::memory_order_relaxed)
        && expected!=last_mutex) std::abort();
}
}
extern "C" int __real_pthread_mutex_lock(pthread_mutex_t*);
extern "C" int __wrap_pthread_mutex_lock(pthread_mutex_t* mutex) {
    last_mutex=mutex;
    if(mutex!=identified_spice_mutex.load(std::memory_order_relaxed))
        return __real_pthread_mutex_lock(mutex);
    const auto start=std::chrono::steady_clock::now();
    int status=pthread_mutex_trylock(mutex);
    const bool contended=status==EBUSY;
    if(status!=0) status=__real_pthread_mutex_lock(mutex);
    counters[4].fetch_add(1,std::memory_order_relaxed);
    counters[5].fetch_add(contended,std::memory_order_relaxed);
    counters[6].fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-start).count()),std::memory_order_relaxed);
    return status;
}
extern "C" void __real_et2utc_c(SpiceDouble,ConstSpiceChar*,SpiceInt,SpiceInt,SpiceChar*);
extern "C" void __wrap_et2utc_c(SpiceDouble et,ConstSpiceChar* format,SpiceInt precision,SpiceInt length,SpiceChar* text) {
    identify_spice_mutex();counters[0].fetch_add(1,std::memory_order_relaxed);
    __real_et2utc_c(et,format,precision,length,text);
}
extern "C" void __real_str2et_c(ConstSpiceChar*,SpiceDouble*);
extern "C" void __wrap_str2et_c(ConstSpiceChar* text,SpiceDouble* et) {
    identify_spice_mutex();counters[1].fetch_add(1,std::memory_order_relaxed);
    __real_str2et_c(text,et);
}
extern "C" void __real_pxform_c(ConstSpiceChar*,ConstSpiceChar*,SpiceDouble,SpiceDouble[3][3]);
extern "C" void __wrap_pxform_c(ConstSpiceChar* from,ConstSpiceChar* to,SpiceDouble et,SpiceDouble matrix[3][3]) {
    identify_spice_mutex();counters[2].fetch_add(1,std::memory_order_relaxed);
    __real_pxform_c(from,to,et,matrix);
}
extern "C" void __real_spkez_c(SpiceInt,SpiceDouble,ConstSpiceChar*,ConstSpiceChar*,SpiceInt,SpiceDouble[6],SpiceDouble*);
extern "C" void __wrap_spkez_c(SpiceInt target,SpiceDouble et,ConstSpiceChar* frame,ConstSpiceChar* aberration,
    SpiceInt observer,SpiceDouble state[6],SpiceDouble* light_time) {
    identify_spice_mutex();counters[3].fetch_add(1,std::memory_order_relaxed);
    __real_spkez_c(target,et,frame,aberration,observer,state,light_time);
}
extern "C" void atm_opt_original_counts(std::uint64_t* result) {
    for(std::size_t i=0;i<counters.size();++i) result[i]=counters[i].load(std::memory_order_relaxed);
}
extern "C" void atm_opt_original_reset() {
    for(auto& counter:counters) counter.store(0,std::memory_order_relaxed);
}
