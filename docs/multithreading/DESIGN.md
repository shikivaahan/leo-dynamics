# Batch execution design (MT-002)

## Starting architecture

Inspection started on `main` at `cfcf105da91d385e9748e87b69c1820c73f111f5`. The existing integration target is `leo::integration`; its C++ types/functions are in namespace `leo`. MT-001 follows this convention, rather than introducing a second namespace.

`propagate.hpp` constructs a local Dopri5 stepper, packed state arrays, adaptive-step state and trajectory sample vector. Its numerical path is unchanged. `EarthDynamics` stores value-owned gravity, spacecraft and model configuration. `EnvironmentalDynamics` stores value-owned dynamics and snapshot source. Existing profiling tools spawn threads for fixed-input atmospheric calls, not independent trajectory propagation; no reusable production batch execution framework existed.

## API

```cpp
#include "leo/integration/batch.hpp"

auto trajectories = leo::propagate_batch(
    [&](std::size_t index, const leo::State& initial) {
        // Return an independently configured RHS value for this trajectory.
        return make_rhs(index, initial);
    }, initials, epoch, sample_times, integration_options, 4);
```

`make_rhs` above is application code, not a library symbol. The factory is borrowed through a forwarding reference, invoked once per trajectory on its owning worker, and must tolerate concurrent calls. Returned RHS values may be move-only; binding their result does not copy or move them. An RHS must satisfy the existing `propagate(const Rhs&, ...)` callable contract. Mutable RHS state belongs to that trajectory alone. Returning shared mutable objects or mutating factory captures without synchronization violates the caller contract.

All initial states share one epoch, schedule and `IntegrationOptions`. States, schedule, options, factory and borrowed resources must remain alive and unmodified until return. Output is `std::vector<leo::Trajectory>` in input order, regardless of completion order. No partial result is returned on failure.

## Execution and ownership

- Default: one explicitly configured worker. No hardware-dependent implicit worker selection.
- One-worker calls execute inline without thread creation.
- Other calls create and join `min(workers, input_count)` standard C++20 `std::jthread`s. The pool lasts exactly one batch call; no background service or general task framework.
- One strategy: relaxed atomic next-index assignment once per trajectory, never once per RHS evaluation. No public scheduling enum or scheduler configuration object.
- Result slots are allocated before workers start. Each slot has a single writer. Each propagation owns its stepper, adaptive state and output sample buffer.
- Per-worker failure slots need no locks: only their owner writes them; the caller reads them after join.
- No threading-related allocation, thread/task creation or shared integration state occurs inside an RHS evaluation. Ordinary force-model computation is unchanged.

## Environmental safety

One caller-owned `SpiceEphemerisProvider` loads the explicit kernel list before execution and remains alive through all batches. Trajectory factories create their own snapshot-source and direct-atmosphere objects borrowing that provider and the immutable `WeatherDataset`. No per-trajectory kernel load/unload or configuration invalidation.

CSPICE calls, managed kernel lifecycle and exact-reuse hits retain the existing process-wide mutex. UTC civil-day reuse remains 32-entry FIFO; orientation reuse remains 64 sets × 4 ways keyed by exact binary64 epoch, frame and managed generation. JB2008 Sun-sharing retains the exact epoch/generation contract. Cache occupancy and hit ordering may differ under concurrency; computed values may not.

`WeatherDataset` const methods read immutable maps and produce local input records. NRLMSISE-00 prepares local input/flags/output, then retains its process-wide mutex around the non-reentrant C reference call. JB2008 retains its independent native mutex around the official Fortran call. These locks are not removed, narrowed or optimized. External CSPICE calls, kernel mutation and reuse invalidation during a batch are unsupported: configuration must remain stable.

## Failures

`workers == 0` is `std::invalid_argument`, even for an empty batch. A valid empty batch returns empty without invoking the factory or validating unused scientific inputs.

Factory, RHS and integration exceptions are captured per trajectory. Other trajectories continue, including tasks not yet started. After synchronization, `BatchPropagationError` reports the **lowest failing input index** via `trajectory_index()` and its original `std::exception_ptr` via `cause()`. Rethrow with `std::rethrow_exception(error.cause())` to recover the original type/payload. Higher-index failures are not aggregated. No missing output is replaced with a successful-looking placeholder.

Thread-launch/resource failures are infrastructure errors: started workers are signalled to stop between trajectories, joined, and the original exception is rethrown. In-flight propagation completes before cleanup. There is no fail-fast interruption of scientific integration.

## Concrete process isolation

The generic library API always uses in-process workers; arbitrary C++ factories
and captures are never serialized. `process_batch.py` independently launches
`mt_isolation` for concrete CASE/atmosphere workloads. Each executable constructs
its own stable SPICE provider, weather dataset and native reference-library state.
The Python parent never initializes CSPICE. Every child retains the existing locks.

Process inputs are explicit: CASE, kernels, weather, epoch, model, numerical
options, perturbation seed and global input range. Balanced contiguous ranges
reuse the existing benchmark's scientific RHS; the parent merges complete
binary64 results in ascending input order. Failures prevent result publication,
and started children are terminated/reaped on failure. This is application-level
orchestration, not another library API or a general process pool. See
[MT002.md](MT002.md) for measured selection and limitations.

## Measurement isolation

Production benchmarks link the unchanged production environment and native adapters. The diagnostic executable links private copies: existing `LEO_SPICE_REUSE_DIAGNOSTICS` enables CSPICE counters; `native_diagnostics.py` replaces only native adapter lock acquisition with a timed RAII lock in generated build-directory copies. Native scientific calls and equations are identical. Diagnostic timings include instrumentation overhead and are never used for headline speedups.
