# MT-001 results and MT-002 cutover

The figures below are historical MT-001 measurements, not reruns of the simplified
API. Current thread/process measurements and recommendations are in
[MT002.md](MT002.md). The historical evidence directory is unchanged.

## MT-002 decision

Keep the generic batch API in-process and dynamic-only. Use threads for large
gravity-only ensembles and the concrete application process route for large
environmental batches. Processes materially outperform the measured shared-memory
environmental workers while every required lock remains intact. Short/small
workloads retain the explicit one-worker default; there is no automatic routing.
The compact comparison and complete six-hour correctness evidence are linked above.

## Historical MT-001 verdict

**Correct and deterministic; beneficial for gravity-only ensembles, not for the measured full-dynamics workloads.** Keep one worker for full dynamics on this host with the current CSPICE/native backends. The optional batch API is useful without claiming that every workload scales.

Two-body + J2 shows substantial throughput gains for sufficiently large/long batches. Full exponential, NRLMSISE-00 and JB2008 are consistently slower with multiple workers, including six-hour CASE-A/B confirmation. Exponential has no native-atmosphere mutex: its slowdown isolates the shared SPICE acquisition path as an important constraint, not just native reference-model serialization.

Correctness: 184/184 regression tests and 924/924 scientific comparisons passed, with zero binary64 mismatches. Complete six-hour outputs/loads are compared in 624 cases; exhaustive RHS-stage/native-input comparisons and actual workers up to 16 are covered by the separate 300-case high-concurrency run. The fully traced six-hour timeout is retained and excluded from acceptance. CASE-C remains a numerical stress test, not a convergence threshold.

49 existing core source files and 1608 historical evidence/tool files are unchanged. No force, torque, integrator, atmosphere, lock or exact-cache-policy modification.

## Recorded experiments

| Experiment | Configurations | Measured runs | Repetitions per propagation configuration | Skipped / failed |
|---|---:|---:|---:|---|
| CASE-A 300 s complete matrix | 952 | 3864 | 5 | 0 / 0 |
| CASE-A/B six-hour confirmation | 272 | 1104 | 5 | 0 / 0 |
| Separate 300 s diagnostics | 272 | 816 | 3 | 0 / 0 |

Counts include policy-specific zero-RHS controls, which have three repetitions. One warm-up per configuration. An additional 1000 startup samples cover 0/1/2/4/8 created threads, after 20 warm-ups per count. All 5784 propagation/control timing records are retained. Five repetitions establish repeatability and observed spread, not a claim of confidence intervals or host-independent optimal settings.

All repeat checksums, matched sequential final-state checksums, RHS counts and atmospheric counts passed the reduction invariants. These benchmark checks supplement, rather than replace, the complete per-field scientific comparisons.

## Best measured parallel throughput

Selections below maximize **median throughput** among actual parallel configurations (workers >1, batch size ≥2) in the 300 s matrix. They are observations, not tuned defaults; do not compare trajectory/s across different simulated durations.

| Workload | Batch | Workers / policy | Variant | Trajectories/s | Speedup | Efficiency |
|---|---:|---|---|---:|---:|---:|
| Two-body + J2 | 64 | 8 / dynamic | Perturbed | 34632.616 | 5.483 | 68.54% |
| Full exponential | 4 | 4 / dynamic | Identical | 134.885 | 0.387 | 9.69% |
| Full NRLMSISE-00 | 4 | 4 / dynamic | Identical | 31.170 | 0.400 | 10.01% |
| Full JB2008 | 16 | 8 / dynamic | Identical | 33.330 | 0.574 | 7.17% |

A best *parallel* result below 1× is still a slowdown relative to sequential propagation. One-worker inline execution is not counted as multithreading.

### Six-hour perturbed confirmation, batch 8

Best median speedup among 2/4/8 workers and both policies, per workload/case:

| Case | Workload | Workers / policy | Batch wall s | Trajectories/s | Speedup | Efficiency |
|---|---|---|---:|---:|---:|---:|
| A | Two-body + J2 | 8 / static | 0.017974 | 445.097 | 4.759 | 59.49% |
| A | Full exponential | 4 / dynamic | 4.725078 | 1.693 | 0.345 | 8.62% |
| A | Full NRLMSISE-00 | 4 / dynamic | 22.647982 | 0.353 | 0.327 | 8.19% |
| A | Full JB2008 | 4 / static | 22.784641 | 0.351 | 0.432 | 10.79% |
| B | Two-body + J2 | 8 / static | 0.002666 | 3001.189 | 5.307 | 66.34% |
| B | Full exponential | 4 / static | 0.846865 | 9.447 | 0.342 | 8.55% |
| B | Full NRLMSISE-00 | 8 / static | 4.270897 | 1.873 | 0.340 | 4.24% |
| B | Full JB2008 | 4 / dynamic | 14.544138 | 0.550 | 0.428 | 10.70% |

The complete size/worker/model/policy/variant scaling tables are in [BENCHMARKS.md](BENCHMARKS.md#complete-measured-scaling-tables). Total times, throughput, requested/active-bound efficiency, per-trajectory timing series/distributions, CPU, memory and evaluation counts are in `evidence/scaling.json` and the raw documents.

## Scheduling comparison

Twelve matched six-hour pairs per workload: CASE-A/B × identical/perturbed × 2/4/8 workers. Ratio is static median wall / dynamic median wall; above 1 favors dynamic.

| Workload | Median ratio | Minimum ratio | Maximum ratio |
|---|---:|---:|---:|
| Two-body + J2 | 0.986 | 0.679 | 1.176 |
| Full exponential | 0.994 | 0.971 | 1.019 |
| Full NRLMSISE-00 | 0.999 | 0.918 | 1.007 |
| Full JB2008 | 1.000 | 0.995 | 1.010 |

No universal dynamic-scheduling win was established. Dynamic won the largest short perturbed J2 batch; static won the six-hour perturbed gravity configurations. Full-model policy differences were small compared with their parallel slowdown. MT-001 retained static by default; MT-002 removes the policy dimension and uses dynamic assignment internally for one maintainable API, not as a claim of universally optimal performance.

### Intrinsic work variation versus wall-time imbalance

CASE-A, 300 s, perturbed batch 64, eight workers:

| Workload | Per-trajectory RHS-count CV | Static worker busy-time CV | Dynamic worker busy-time CV |
|---|---:|---:|---:|
| Two-body + J2 | 1.26% | 12.84% | 11.30% |
| Full exponential | 0.61% | 1.11% | 0.63% |
| Full NRLMSISE-00 | 0.51% | 0.54% | 0.48% |
| Full JB2008 | 0.51% | 0.42% | 0.22% |

Adaptive work counts vary, but little for these perturbations. The large full-model slowdown is not explained by a large adaptive-step imbalance. Gravity wall variation exceeds intrinsic count variation; its precise scheduling/placement/accounting causes are not isolated.

## Ranked bottlenecks

### 1. Shared SPICE serialization and contention — measured

Representative diagnostic selection: CASE-A, 300 s, perturbed batch 32, eight static workers; medians over three runs. Acquisition intervals include timing/try-lock overhead. Aggregate acquisition time sums across threads and may exceed elapsed batch wall time; it is **not** a wall-time percentage.

| Workload | Diagnostic wall s | SPICE contended acquisitions | Aggregate SPICE acquisition s | Native contended acquisitions | Aggregate native acquisition s | Aggregate native held s |
|---|---:|---:|---:|---:|---:|---:|
| Full exponential | 0.262106 | 44.06% | 1.417553 | — | — | — |
| Full NRLMSISE-00 | 1.200456 | 39.81% | 6.659615 | 12.96% | 0.135358 | 0.177427 |
| Full JB2008 | 1.320072 | 39.10% | 6.745612 | 26.24% | 0.451347 | 0.341660 |

Exponential's native-call counts are zero. It still suffers a full-dynamics slowdown, with substantial SPICE contention. All full models make exactly one Sun acquisition per RHS; JB2008 compatible Sun sharing avoids a second one, but repeated per-trajectory Sun acquisition remains.

Native workloads show four SPICE mutex acquisitions per RHS for NRLMSISE-00 and five for JB2008, versus one for exponential. Existing UTC metadata reuse hits do not remove `et2utc` or the shared lock; orientation hits also serialize. Zero kernel-configuration invalidations are observed during measured runs: no per-trajectory kernel lifecycle churn.

### Exact-cache observations

Same representative batch/worker/policy; compare identical and perturbed inputs:

| Model | Variant | UTC metadata hit rate | Orientation hit rate |
|---|---|---:|---:|
| NRLMSISE-00 | Identical | 100% | 90.35% |
| NRLMSISE-00 | Perturbed | 100% | 29.71% |
| JB2008 | Identical | 100% | 90.97% |
| JB2008 | Perturbed | 100% | 29.75% |

Exact reuse depends on stage-key locality. It helps identical ensembles more than perturbed ones, but does not remove the shared synchronization bottleneck. Cache capacity, eviction, epoch/generation keys and flags remain unchanged; no density reuse is introduced.

### 2. Native reference-model serialization — measured, secondary in this selection

Both native mutexes remain necessary because the reference models are non-reentrant. Acquisition/held intervals and contention are measured separately above. JB2008 has more native contention than NRLMSISE-00 in this selection. SPICE aggregate acquisition intervals are substantially larger than native acquisition intervals; native locking is not the only explanation for poor scaling. These are diagnostic observations, not a claim that instrumented elapsed times equal production costs.

### 3. Startup and scheduling/setup costs for short gravity workloads — measured

Separate gate probe, 200 samples per count. Medians are computed independently for each interval, so component medians need not sum to the total median.

| Created threads | Creation + all-thread readiness µs | Release + join µs | Total µs |
|---:|---:|---:|---:|
| 0 | 0.12 | 0.25 | 0.38 |
| 1 | 115.05 | 75.98 | 190.82 |
| 2 | 173.82 | 62.93 | 241.63 |
| 4 | 370.94 | 103.89 | 474.49 |
| 8 | 806.95 | 216.89 | 1027.39 |

The probe includes gate/scheduler overhead and is not subtractable from overlapping production startup. Zero-RHS controls and production dispatch/active/join markers retain the actual executor/setup overhead separately. A 300 s one-trajectory J2 job takes roughly 0.14–0.15 ms sequentially; starting even one worker can dominate it. The API default one worker executes inline without that thread cost. Longer/larger gravity ensembles amortize startup; the eight-thread pool is recreated on every call.

### 4. Workload imbalance — measured but not dominant here

RHS-count CV is small in the selected Monte Carlo perturbations. Dynamic assignment changes load balance and short-run performance, but does not rescue full-model scaling. Broader perturbation populations need separate measurement; no claim of optimal scheduling for all ensembles.

### Unranked hypotheses

Memory allocation/locality and false sharing lack hardware-counter or allocator-profile evidence. Process peak RSS was 32.16 MiB for the matrix, 28.32 MiB for confirmation and 24.89 MiB for diagnostics; these include loaded weather/kernels and allocator retention and do not establish a memory bottleneck. No per-RHS task allocation is introduced by the batch executor. `perf` is not installed; no cache-miss, bandwidth or false-sharing attribution is claimed.

## CPU/accounting and scope limits

Six-hour CASE-A perturbed gravity execution reports about 5.57 process-CPU-seconds/wall-second at eight static workers, consistent with actual parallel work. Full models use about 3.0/2.6/2.9 CPU equivalents at eight workers for exponential/NRLMSISE-00/JB2008 while still reducing throughput: greater CPU consumption is not a speedup.

Some short guest-thread measurements report process CPU/wall below one despite observed wall speedup; the cause is not isolated. Raw clocks are retained, but short-run CPU utilization is not used as proof of concurrency. No claim of Windows host-wide utilization, fixed frequency, thermal isolation or optimal affinity. Only one WSL2 host and the recorded physical-core mask were measured.

No ThreadSanitizer or injected OS thread-launch failure result is claimed. Trajectory factory/RHS failures, deterministic failure ordering and synchronized cleanup are exercised by permanent tests. Intermediate six-hour RHS traces are not part of acceptance; full output diagnostics and separate exhaustive short-stage validation are.

## Experiments proposed by MT-001

1. **Process-level independent propagation**: now tested and implemented by [MT-002](MT002.md) for concrete application workloads; numerical locks remain unchanged.
2. **Exact shared time-key preprocessing** for Sun/UTC/orientation acquisition across trajectories. Measure lock-call reduction, key locality and generation/lifetime safety, especially perturbed batches. No density caching/interpolation, approximate transforms or hidden fallback.
3. **Native state-context feasibility** only after identifying every mutable upstream C/Fortran global. Validate identical native inputs/results before considering independent contexts; retain the current mutex until proven safe. SPICE may remain the limiting path even if native calls become independent.
4. **Persistent bounded batch workers** for repeated short gravity ensembles. Separate constructor/startup from warm execution and end-to-end lifetime costs; preserve error/ownership/order contracts. Do not infer that a persistent pool fixes SPICE contention.
5. **Scheduling/affinity and realistic heterogeneity sweeps**, plus longer CPU-accounting probes. Vary only explicit worker policies/placement and physically motivated perturbations, not tolerances or force equations. Use profiler/hardware evidence before padding buffers or blaming false sharing.

Remaining proposals are future work, not implemented optimizations. They must preserve frozen numerical acceptance and explicit kernel/weather/model configurations. Density caching remains deferred.
