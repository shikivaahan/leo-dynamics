#pragma once
#include "leo/integration/options.hpp"
#include "leo/integration/propagate.hpp"
#include "leo/integration/trajectory.hpp"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
namespace leo {

// Bounded, ordered batch propagation of independent trajectories.
//
// MT-001. The numerical path is exactly the unchanged single-trajectory
// propagate(): no integrator, stepper, force, atmosphere, SPICE or tolerance
// behaviour differs between a sequential and a batched run, and no state is
// shared between trajectories. Only the scheduling of whole trajectories is
// new.
//
// Explicit restrictions of this first interface generation:
//   * one epoch, one IntegrationOptions and one output schedule for the whole
//     batch; a per-trajectory schedule would need a wider interface, not a
//     hidden convention;
//   * one RHS object per trajectory, created by the caller's factory, so that
//     mutable integrator-facing RHS state is never shared between workers.
//
// Concurrency contract:
//   * the factory is invoked once per input trajectory, on the worker thread
//     that owns that trajectory, and must be safe to call concurrently;
//   * an RHS object is created per trajectory and is only ever dereferenced by
//     the worker that owns its trajectory, so a mutable const-callable RHS is
//     allowed (and required to be confined to that one thread);
//   * the caller keeps ownership of everything borrowed: the initial conditions,
//     the sample schedule, the IntegrationOptions and any
//     environment, ephemeris, provider or configuration reachable from the
//     factory. All of it must stay alive and unmodified until
//     propagate_batch() returns. The batch creates no SPICE provider, loads no
//     kernel and takes no lock of its own; native CSPICE and atmosphere locking
//     is untouched, so one stable provider shared by the batch is the intended
//     configuration;
//   * results are written into disjoint elements of the output vector, so no
//     result mutex exists and output ordering is the input ordering.
//
// Scheduling: workers == 1 runs the whole batch inline on the calling thread
// and creates no thread at all. Otherwise the call creates and joins a bounded
// pool of min(workers, trajectories) std::threads, which take whole
// trajectories from one relaxed atomic work index: whichever worker is free
// takes the lowest open index, so an uneven trajectory duration cannot leave a
// worker permanently idle while work remains. There is no persistent pool to
// keep warm and no per-RHS-evaluation allocation or task creation.
//
// Portability: the pool is std::thread rather than std::jthread, because
// std::jthread and std::stop_source are not declared by the libc++ shipped
// with AppleClang, and this header is compiled into the macOS wheel. Joining is
// therefore explicit and owned by an RAII pool, not by the thread destructor,
// which is what std::jthread would have provided. The cancellation flag is a
// plain relaxed atomic<bool> read between trajectories; it is not observed
// inside a trajectory, so cancelling cannot truncate a propagation.
//
// Error contract: a trajectory that throws (factory, RHS, integrator or
// environment) does not stop the batch; every remaining trajectory still runs and
// the batch never substitutes, skips or fabricates a failed trajectory. After all
// workers have joined, a BatchPropagationError is thrown for the lowest failing
// input index, carrying that trajectory's original std::exception_ptr. Failure to
// create a worker thread is an infrastructure failure, not a trajectory
// failure: the already started workers are stopped, joined and the original
// exception is rethrown unchanged.

// Thrown after a batch in which at least one trajectory failed. trajectory_index()
// is the lowest failing index of the input span and cause() is the exception that
// trajectory raised, rethrowable with std::rethrow_exception(). Trajectories with a
// higher index are not reported; their failures are dropped, never merged.
class BatchPropagationError : public std::runtime_error {
public:
    BatchPropagationError(std::size_t trajectory_index, std::exception_ptr cause)
        : std::runtime_error("batch trajectory " + std::to_string(trajectory_index) + " failed"),
          trajectory_index_(trajectory_index), cause_(std::move(cause)) {}
    std::size_t trajectory_index() const noexcept { return trajectory_index_; }
    const std::exception_ptr& cause() const noexcept { return cause_; }
private:
    std::size_t trajectory_index_;
    std::exception_ptr cause_;
};

namespace detail {

// One failure record per worker, written only by the worker that owns it and read
// only after every worker has joined, so no failure state needs atomics. Each slot
// keeps the lowest failing index that worker saw, which makes the reported
// trajectory independent of the order in which workers happen to run.
struct BatchFailure { std::size_t index = static_cast<std::size_t>(-1); std::exception_ptr cause; };

class BatchFailures {
public:
    explicit BatchFailures(std::size_t workers) : slots_(workers) {}
    void record(std::size_t worker, std::size_t index, std::exception_ptr cause) noexcept {
        BatchFailure& slot = slots_[worker];
        if (index < slot.index) {
            slot.index = index;
            slot.cause = std::move(cause);
        }
    }
    const BatchFailure* lowest() const noexcept {
        const BatchFailure* best = nullptr;
        for (const BatchFailure& slot : slots_)
            if (slot.cause && (best == nullptr || slot.index < best->index)) best = &slot;
        return best;
    }
private:
    std::vector<BatchFailure> slots_;
};

// Owns the batch's worker threads and guarantees that every thread it accepted
// is joined before the pool is destroyed, on every exit path including the
// unwinding one. std::thread's destructor calls std::terminate when the thread
// is still joinable, so the ownership that std::jthread would have supplied
// inline has to be explicit: add() is the only way in, and join() is idempotent
// so a partially constructed pool can be joined by both the caller's handler
// and this destructor.
class WorkerPool {
public:
    explicit WorkerPool(std::size_t capacity) { threads_.reserve(capacity); }
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    ~WorkerPool() { join(); }
    // Forwards the worker's own arguments straight to the std::thread
    // constructor, so a worker is launched from the caller's callable and
    // arguments rather than from a per-worker wrapper closure.
    template<class Worker, class... Args>
    void add(Worker&& worker, Args&&... args) {
        threads_.emplace_back(std::forward<Worker>(worker), std::forward<Args>(args)...);
    }
    void join() noexcept {
        for (std::thread& worker : threads_)
            if (worker.joinable()) worker.join();
        threads_.clear();
    }
private:
    std::vector<std::thread> threads_;
};

}

// Propagates count = initials.size() independent trajectories and returns them in
// input order. factory(index, initial) must return an RHS usable by propagate(),
// i.e. callable as rhs(ElapsedTime, const State&) const on a non-const object, and
// may return a move-only type; the result is bound without copying. workers must be
// positive; zero is rejected before the batch is inspected, so an invalid worker
// count is rejected even for an empty batch. All inputs are borrowed read-only for
// the duration of the call.
template<class Factory>
std::vector<Trajectory> propagate_batch(Factory&& factory, std::span<const State> initials,
                                        Epoch epoch, std::span<const double> times,
                                        const IntegrationOptions& options = {},
                                        std::size_t workers = 1) {
    if (workers == 0) throw std::invalid_argument("batch worker count must be positive");
    const std::size_t count = initials.size();
    std::vector<Trajectory> trajectories(count);
    if (count == 0) return trajectories;
    const std::size_t pool_size = std::min(workers, count);
    detail::BatchFailures failures(pool_size);
    // Shared schedule, options and epoch validation is the unchanged propagate()
    // contract, applied per trajectory inside the owning worker.
    auto attempt = [&](std::size_t worker, std::size_t index) noexcept {
        try {
            auto&& rhs = factory(index, initials[index]);
            trajectories[index] = propagate(rhs, initials[index], epoch, times, options);
        } catch (...) {
            failures.record(worker, index, std::current_exception());
        }
    };
    if (workers == 1) {
        // Inline: no thread objects, one calling-thread worker.
        for (std::size_t index = 0; index < count; ++index) attempt(0, index);
    } else {
        std::atomic<bool> cancelled{false}; // Raised only when a worker cannot be started.
        std::atomic<std::size_t> next{0};
        auto run_worker = [&](std::size_t worker) {
            // Relaxed: the index only arbitrates ownership of disjoint slots and
            // carries no synchronised data. The flag is read between trajectories
            // only, so a cancelled batch never truncates an in-flight propagation.
            for (std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                 index < count && !cancelled.load(std::memory_order_relaxed);
                 index = next.fetch_add(1, std::memory_order_relaxed)) attempt(worker, index);
        };
        detail::WorkerPool pool(pool_size);
        try {
            for (std::size_t worker = 0; worker < pool_size; ++worker) pool.add(run_worker, worker);
        } catch (...) {
            // Cancel and join what was started, then surface the original launch
            // failure unchanged; no trajectory result is reported from it. The
            // pool destructor is a second, idempotent join on this path.
            cancelled.store(true, std::memory_order_relaxed);
            pool.join();
            throw;
        }
        pool.join();
    }
    if (const detail::BatchFailure* failure = failures.lowest())
        throw BatchPropagationError(failure->index, failure->cause);
    return trajectories;
}

}