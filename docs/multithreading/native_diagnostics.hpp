#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace mt_diagnostics {
using Clock = std::chrono::steady_clock;
struct alignas(64) Counters {
    std::atomic<std::uint64_t> calls{0}, contended{0}, acquisition_ns{0}, held_ns{0};
};
inline Counters nrlmsise00, jb2008;
inline std::uint64_t nanoseconds(Clock::duration duration) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}
// Measurement build only: acquisition includes clock/try_lock overhead, not pure blocking time.
class NativeLock {
    std::unique_lock<std::mutex> lock_;
    Counters& counters_;
    Clock::time_point acquired_;
public:
    NativeLock(std::mutex& mutex, Counters& counters)
        : lock_(mutex, std::defer_lock), counters_(counters) {
        const auto start = Clock::now();
        const bool contended = !lock_.try_lock();
        if (contended) lock_.lock();
        acquired_ = Clock::now();
        counters_.calls.fetch_add(1, std::memory_order_relaxed);
        counters_.contended.fetch_add(contended, std::memory_order_relaxed);
        counters_.acquisition_ns.fetch_add(nanoseconds(acquired_ - start), std::memory_order_relaxed);
    }
    ~NativeLock() {
        const auto end = Clock::now();
        lock_.unlock();
        counters_.held_ns.fetch_add(nanoseconds(end - acquired_), std::memory_order_relaxed);
    }
};
}
