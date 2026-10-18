// Measurement-only C++20 thread creation/readiness and release/join probe.
// No production worker hook, no scientific workload, no persistent executor.
//
// HISTORICAL TOOL, NOT THE CURRENT SCHEDULER. This probe deliberately still uses
// std::jthread because it reproduces a frozen MT-001 measurement whose recorded
// evidence strings describe "create jthreads". The production batch scheduler
// (include/leo/integration/batch.hpp) now uses std::thread, because AppleClang's
// libc++ does not declare std::jthread and the header is compiled into the macOS
// wheel. Do not read this file as a description of the runtime scheduler; see
// docs/multithreading/DESIGN.md for that. This probe is not compiled by the
// native test suite or by the wheel build.
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
struct Timing { double creation_ready, release_join, total; };
double seconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}
Timing probe(std::size_t count) {
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t ready = 0;
    bool release = false;
    std::vector<std::jthread> threads;
    threads.reserve(count);
    const auto begin = Clock::now();
    try {
        for (std::size_t i = 0; i < count; ++i) {
            threads.emplace_back([&] {
                std::unique_lock lock(mutex);
                ++ready;
                changed.notify_all();
                changed.wait(lock, [&] { return release; });
            });
        }
    } catch (...) {
        { std::lock_guard lock(mutex); release = true; }
        changed.notify_all();
        threads.clear();
        throw;
    }
    Clock::time_point all_ready;
    {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return ready == count; });
        all_ready = Clock::now();
        release = true;
    }
    changed.notify_all();
    for (auto& thread : threads) thread.join();
    const auto end = Clock::now();
    return {seconds(begin, all_ready), seconds(all_ready, end), seconds(begin, end)};
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument("usage: mt_startup NEW_OUTPUT.json");
        const std::filesystem::path path(argv[1]);
        if (std::filesystem::exists(path)) throw std::invalid_argument("refusing to overwrite startup evidence");
        std::vector<std::pair<std::size_t, Timing>> rows;
        constexpr std::size_t warmups = 20, repetitions = 200;
        // Interleave counts rather than measuring each count in a separate thermal window.
        const std::vector<std::size_t> counts{0, 1, 2, 4, 8};
        for (std::size_t repeat = 0; repeat < warmups + repetitions; ++repeat)
            for (const auto count : counts) {
                const auto timing = probe(count);
                if (repeat >= warmups) rows.emplace_back(count, timing);
            }
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        if (!out) throw std::runtime_error("cannot write startup evidence");
        out << std::setprecision(17)
            << "{\"kind\":\"MT-001 isolated startup\",\"warmups_per_count\":" << warmups
            << ",\"repetitions_per_count\":" << repetitions
            << ",\"method\":\"create jthreads, wait until every thread reaches a condition-variable gate, release, join; includes gate coordination and scheduler latency; not a subtractable estimate of overlapping production startup\",\"execution_order\":\"0,1,2,4,8 repeated\",\"runs\":[";
        bool first = true;
        for (const auto& [count, t] : rows) {
            if (!first) out << ',';
            first = false;
            out << "{\"threads_created\":" << count << ",\"creation_ready_s\":" << t.creation_ready
                << ",\"release_join_s\":" << t.release_join << ",\"total_s\":" << t.total << '}';
        }
        out << "]}\n";
        if (!out) throw std::runtime_error("startup evidence write failed");
        std::cout << "wrote " << rows.size() << " startup samples to " << path << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
