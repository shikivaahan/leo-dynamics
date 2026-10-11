# ATM-OPT-001 parameters

## Implemented reuse policies

| Parameter | Selected value | Rationale / sensitivity |
|---|---|---|
| UTC ownership/scope | Process-owned fixed array, synchronized | The underlying SPICE time state is process-global; lets trajectories share civil metadata without a second lock or hidden last-value semantics |
| UTC capacity | 32 exact civil dates | More than a single six-hour trajectory's footprint; bounded protection for backward queries and controlled boundary schedules |
| UTC key | Civil serial date + managed uint64 generation | Civil-day identity follows the original per-request SPICE conversion; no approximate ET buckets |
| UTC stored values | Current/next midnight ET, rounded day length, serial date, day-of-year | Only day-dependent work is removed; seconds-of-day and quasi-MJD remain request-specific |
| UTC eviction | FIFO, hits do not reorder | Fixed memory, no allocation; nonmonotonic requests need not be adjacent |
| UTC request precision | `et2utc_c("ISOC", 9)` | Exactly the original parsing precision/error behavior |
| UTC supported day length | Original rounded 86399..86401 seconds | No 86400-second assumption |
| Orientation ownership/scope | Process-owned shared cache, existing CSPICE mutex | Reuses exact epochs across trajectories while linearizing hits with lifecycle; compared to benchmark-only per-thread policy replay |
| Orientation capacity | 256 matrices | Small bounded cache for realistic adaptive stages and ensemble reuse; 64 sets × four ways |
| Orientation key | Exact uint64 ET bits + requested frame + fixed J2000-to-Earth direction + generation | `nextafter` ETs and signed zeros do not alias |
| Orientation frames | `ITRF93`, `IAU_EARTH` | Original explicit-frame support; no fallback |
| Orientation hash | xor-shift/multiply avalanche, constants `0xbf58476d1ce4e5b9`, `0x94d049bb133111eb` | Distributes both high/low floating-point bits; collisions remain distinct checked keys |
| Orientation eviction | FIFO within each four-way set | At most four checked entries; fixed memory and no dynamic allocation |
| Orientation disabled path | No hash/key computation or insertion | Keeps independently disabled diagnostic overhead small |
| Kernel generation | Starts at 1; increments before every actual managed furnsh/unload attempt and explicit notification | Conservative invalidation for time, orientation and Sun provenance, including partial failed loads; refcount-only ownership changes do not mutate configuration |
| Generation wrap | Terminate before reusing generation 0 | Never revive stale provenance; practically unreachable |
| Lifecycle invalidation | Clear valid generations and reset FIFO positions | No lazily visible obsolete cache hit |
| Sun storage | Immutable per-evaluation opaque observation, no cache | Exact ET and current process configuration; no mutable last state |
| Sun fallback | Independent geometric SPICE lookup for missing/stale/wrong-epoch input or disabled sharing | Preserves standalone JB2008; incompatible geometry is unrepresentable through supported construction |
| Production defaults | UTC on, Sun sharing on, orientation on | Controls remain independently disableable; standalone JB2008 still performs its required Sun lookup |
| Synchronisation | Original process-wide SPICE mutex; unchanged native-reference mutexes | No required mutex removed; UTC non-SPICE work is outside the mutex with generation rechecks |
| External mutation | Application-wide quiescence, then `invalidate_reuse()` before resuming | Arbitrary unreported external CSPICE changes are unsupported |
| Instrumentation | Separate `LEO_SPICE_REUSE_DIAGNOSTICS` binary + generated private lookup timers | Production has no counter increments/timers/logging; diagnostic timings never define production speedups |

Fixed cache storage is 26,376 bytes on this toolchain/ABI, derived from real `sizeof` expressions at run time rather than a hard-coded constant: `sizeof(CivilDay)` 48 × 32 = 1,536, `sizeof(Orientation)` 96 × 256 = 24,576, `std::array<unsigned,64>` FIFO indices 256, `std::size_t` civil index 8. The retained `cache_bytes` counter is the authority and matches this sum exactly. These sizes are ABI/compiler dependent and would differ elsewhere; the code never assumes them. The figure covers fixed cache storage only and excludes the kernel-ownership map, the SPICE mutex, diagnostic counters and CSPICE's own state. Cache occupancy, capacity, eviction and invalidation counts are retained separately; counter resets leave entries and configuration unchanged.

Capacity sensitivity is assessed through exact-key shared/per-thread replay with the same total capacity and measured workload hit rates. Thread-local replay splits UTC32 and orientation256 across 1/2/4 threads, so per-thread capacities are UTC32/16/8 and orientation256/128/64. It is not an alternative production implementation or an independently measured TLS-throughput result. The fixed-date/six-hour workloads need very few UTC dates; all-miss epoch schedules and trajectory-major ensemble locality remain capacity/scheduling-sensitive. Runtime tuning across multiple production capacities is not claimed.

## Measurement parameters

| Parameter | Value | Reason |
|---|---|---|
| Source baseline | `6a1ad59`, unchanged original worktree | Includes frozen profiling documentation, production atmospheric source still direct-v2 |
| Candidate controls | All eight combinations | Required six plus UTC+orientation and Sun+orientation interactions; combined gains measured, never added |
| Provider and micro calls | 2,000 per batch | Same scale as ATM-PROF-001 |
| Provider/micro/concurrency batches | Five | Mean/median/p95/p99, sample SD and batch variability retained |
| Warm-up | 256 operations per batch/worker | Excluded from reported timings/counters; cached inputs intentionally warmed where labels say fixed |
| Controlled locality batches | Three | Repeated deterministic schedules; raw exact epoch/date keys recorded outside timing |
| Controlled locality requests | 1,024 per schedule/batch | Same bounded locality-study scale; eight trajectories for ensemble schedules |
| Six-hour repetitions | Three per case/model/configuration in each binary | Actual wall-time distribution and identical output checksums retained |
| Propagation RHS/provider timing capacity | 2^21 samples per run | Preallocated before measurement; overflow throws, never reallocates or silently drops data |
| Thread counts | 1, 2, 4 | Actual concurrent readers, unchanged native/SPICE mutexes |
| Single-thread CPU | Logical CPU 0 | Same affinity as the profiling protocol; no dedicated-core guarantee |
| Concurrency affinity | Logical CPUs 0–3 | Shared allowed mask; worker migration within the mask is allowed |
| Compiler | GCC/G++/GFortran 13.3.0 | Original and candidate use the same compiler and frozen native dependencies |
| Optimisation | Release `-O3 -DNDEBUG`, C++20 | No fast-math; CMake caches and complete compiler commands retained |
| Timer | `std::chrono::steady_clock`; `CLOCK_PROCESS_CPUTIME_ID` per batch | Wall latency and process CPU costs kept distinct |
| Percentiles | Linear interpolation at q(n−1) | Same estimator as profiling workload evidence |
| Variability | Sample standard deviation; SD/abs(mean) | Raw samples/repeated batch means retained for alternative analysis |
| Lookup attribution | Actual search (orientation includes hashing), under held mutex, diagnostic copy only | Mutex acquisition excluded; end clock-read overhead retained; per-batch counts and summed durations retained |
| Mutex attribution | try-lock contention, acquisition count, summed wall wait | Diagnostic only; includes try-lock/timer overhead, not proof of physical-core isolation |
| Invalidation microbenchmark | 2,000 calls after warm-up | Measures cache clearing/generation advancement, not disk I/O/kernel load duration |
| Serialization | Outside measured per-call regions; raw distributions retained and gzip-compressed after verification | No hot-path JSON/logging; full propagation retains preallocated timing assignments and checksum work |

## Unchanged scientific settings

Epoch `2003-10-30T00:00:00 UTC`; frame ITRF93; duration 21,600 s; output cadence 30 s. Nominal Dopri5: rtol 1e−10, max step 15 s, initial step 1 s. Refinement RKF7(8): rtol 1e−12, max step 5 s, initial step unchanged. Absolute tolerances: position 1e−3 m, velocity 1e−6 m/s, quaternion 1e−12, body rate 1e−12 rad/s. Existing attempt limit 1,000,000. J2, Sentman drag, conical eclipse, corotation `(0,0,7.292115e−5)` and CASE-A/B/C spacecraft/forces are unchanged. NRLMSISE uses daily Ap for the main comparisons; existing Ap-history regression coverage remains active.

Kernels: NAIF0012 LSK, DE432s SPK, `earth_000101_270105_261009.bpc` for ITRF93 and `pck00011.tpc` for IAU_EARTH boundary tests. Weather: the frozen CSSI 2020-02-24 snapshot, SOLFSMY and DTCFILE. Exact file hashes and installed dependency/header hashes are in the manifest.

Host: WSL2 Linux, allowed logical CPUs 0–15. Frequency governor/physical-core isolation and hypervisor scheduling are not controlled. Current measurements use the same source/compiler/data/protocol as profiling wherever possible; added provider/RHS timing capture and diagnostic lookup clocks are explicitly scoped, not concealed as an unchanged historical harness.

---

# Closure experiment parameters

## Order-interleaved provider measurement

| Parameter | Value | Reason |
|---|---|---|
| Protocol | Round-robin interleaving of all eight configurations inside each repetition | Contiguous per-configuration blocks confound a configuration difference with execution order and drift; this was the root cause of the retired NRLMSISE-00 anomaly |
| Seed | `20251011`, recorded in the evidence | Reproducible order; every repetition's permutation is written to `execution_order_per_repetition` |
| Repetitions | 12 per configuration | Sufficient for descriptive distributions and batch-level intervals; **not** used to claim a precise population p99 |
| Calls per batch | 2,000 | Matches the earlier provider workload scale |
| Warm-up | 256 calls per configuration **before any measurement**, all configurations | Removes first-touch cold state so no configuration is penalised by position |
| Total timed calls | 24,000 per configuration per model (12 × 2,000) | All per-call samples and all 12 batch means retained |
| Statistical estimator | Welch difference of means on batch means, 95% normal interval | Batch means are the independent unit; per-call samples within a batch are not independent |
| Pinning | `taskset -c 0`; concurrency not measured in this experiment | Single-thread provider timing |
| Serial execution | Original, candidate and diagnostic binaries run one after another; nothing compiled during measurement | Avoids build-load contamination |
| Operation counts | Per-call SPICE counts from the diagnostic build, summed over all repetitions and divided by repetitions × calls | Makes "Sun sharing does nothing for NRLMSISE-00" a measured statement, not an assumption |
| Scientific equality | One result checksum required across all eight configurations and equal to the unchanged original | Timing equality is meaningless without result equality |

Interleaved measurements supersede the earlier block-order provider table for provider latency only. Propagation, RHS, concurrency and counter results come from the original protocol and remain valid, because the cleanup cannot affect the `reuse=true` path they exercise.

## Compatibility builds

| Configuration | Result |
|---|---|
| No CSPICE, no NRLMSISE-00, no JB2008 | Builds clean; the reuse code is excluded with the environment component |
| CSPICE only, no atmospheric backends | Builds clean; `spice.cpp` and all reuse code compile and link without either native backend |

Both confirm the reuse additions are confined to `leo::environment`/`leo::atmosphere` and introduce no build-time coupling between CSPICE and the native reference models.
