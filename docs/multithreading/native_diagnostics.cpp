#include <initializer_list>
#include "native_diagnostics.hpp"
extern "C" void mt_native_statistics(std::uint64_t* values) {
    std::size_t offset = 0;
    for (const auto* counters : {&mt_diagnostics::nrlmsise00, &mt_diagnostics::jb2008}) {
        values[offset++] = counters->calls.load(std::memory_order_relaxed);
        values[offset++] = counters->contended.load(std::memory_order_relaxed);
        values[offset++] = counters->acquisition_ns.load(std::memory_order_relaxed);
        values[offset++] = counters->held_ns.load(std::memory_order_relaxed);
    }
}
