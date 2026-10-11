# ATM-PROF-001 experimental design

Written before measurement. Measurement-only; no production edits, caching, interpolation, quantisation, physics or numerical-configuration changes.

## Provenance

Source revision `a2eebd1078cbcfd68cfd8a9692c5b5ceabe9caff`; initial dirty state: untracked `outputs/`, excluded and preserved. Scientific baseline `direct-v2`, compiled source revision `7c11f5746cd0ff0f26f46cd81aceff677a110be8`. Its timing is explicitly inherited from `direct-v1`, not a direct-v2 measurement. Current sources and frozen external dependencies will be hashed and compared with the baseline. Historical evidence is read-only. NRLMSISE daily Ap remains the baseline; history mode is a separately labelled diagnostic experiment. Actual switch zero is 0 (native cgs converted to SI), despite contradictory historical prose about SI switching.

## Methods and attribution

1. Build an unmodified Release library and a diagnostic library from generated copies of the same six translation units. Insert only checked timing boundaries; original source remains untouched. Generation fails on source-anchor drift and records hashes. Diagnostic compilation is available only through this standalone CMake root, not the default production build.
2. Instrument inclusive provider stages and nested operations, including separate CSPICE primitive calls and acquisition intervals for the existing mutexes. Store records in preallocated thread-local storage; no I/O or JSON in timed regions. Lock-acquisition measurements include clock and uncontended acquisition overhead, not pure scheduler wait.
3. Independently benchmark operations with prepared inputs. Batch short operations, retain compiler barriers and a result sink. Run original and instrumented executables separately. Raw CSPICE experiments execute only single-threaded, with loaded kernels and error preparation outside the primitive interval; production requests retain all locks.
4. Use monotonic `std::chrono::steady_clock` wall timers; measure clock-pair, empty-call and empty-instrumentation controls. Report calibration without pretending subtraction makes each tiny stage exact. Capture process CPU time for batches/propagation where available. Wall time includes scheduling; CPU time does not directly partition lock waiting.
5. Retain minimum, median, mean, p95, p99 and maximum plus raw samples and batch summaries. Percentiles use the reducer's documented estimator. Reconcile exclusive top-level stage means against same-request inclusive totals, never differences between independently measured medians. Nested stages must not be double-counted.
6. Attempt `perf` sampling if installed. It is absent on this host (`perf --version` fails command-not-found); do not fabricate a sampling cross-check.

## Workloads and counts

| Workload | Inputs | Repetitions / warm-up |
|---|---|---|
| Fixed provider / stages | 2003-10-30T00:00:00 UTC, valid LEO WGS84 geometry; both models, plus separately labelled NRL history | 5 independent batches × 2000 calls; 256 warm-up calls per batch |
| Prepared microbenchmarks | Same epoch and precomputed native/weather/rotation inputs; equatorial, midlatitude, near-polar and exact-pole geodetic cases | Multiple batches; short operations batched; counts retained in evidence |
| Real propagation | CASE-A/B/C frozen configs; both models; actual production Dopri5 requests | 3 independent six-hour runs per model/case; provider warm-up 256 |
| Controlled locality | Fixed, changing position only, changing epoch only, nearby epoch/position, identical weather/changing geometry, hourly/three-hour/day boundaries, independent nearby trajectories in trajectory-major and epoch-major order | At least 1000 deterministic requests per distribution; batch counts retained |
| Concurrency | Both models, fixed and changing epochs | 1/2/4 threads, synchronised starts, 5 independent batches; existing native/SPICE locks retained |

No density result is reused. Offline exact-key counting is analysis only. Real propagation request capture is preallocated and separate from the provider's outer interval; its overhead still affects total propagation wall time, so paired no-capture/original comparisons and limitations must be explicit.

## Scientific and numerical settings

Existing frozen CSSI snapshot, SOLFSMY, DTCFILE; NAIF LSK, de432s SPK and historical Earth-orientation binary PCK; frame ITRF93; Sun geometric J2000/NONE relative to Earth. Duration 21600 s; output cadence 30 s; rtol 1e-10; max step 15 s; initial step 1 s; max attempts 1000000; absolute position/velocity/quaternion/body-rate tolerances 1e-3 m / 1e-6 m/s / 1e-12 / 1e-12 rad/s. CASE spacecraft, forces, torques, corotation and initial conditions unchanged. No refinement timing is substituted for the nominal workload.

## Build and host controls

Release GCC 13.3.0, GNU Fortran 13, C/C++/Fortran optimisation `-O3 -DNDEBUG` where applicable, no fast-math or scientific source edits. CMake 4.4.4. AMD Ryzen 7 5800H, 16 logical CPUs, WSL2 Linux; full host information retained in manifest. Initial affinity 0–15; single-thread commands pin to CPU 0, concurrency commands allow CPUs 0–3 (shared affinity mask, not a promise of one dedicated core per thread). Governor and current-frequency sysfs interfaces are absent under WSL2. Frequency variation and host scheduling cannot be eliminated. Initial load averages 0.0596 / 0.0566 / 0.0049; record before and after runs. Measurements are serialised across benchmark processes; no simultaneous build/tests while profiling.

## Bias and acceptance

Instrumentation inhibits inlining/reordering and adds timer calls and bookkeeping; quantify measured inflation using original full-provider batches and diagnostic-disabled/active controls. Generated instrumentation can alter register pressure and cache layout even when disabled. Fixed inputs warm CSPICE/native internal implementation state; they are not representative of changing epochs. Replay does not measure integrator cost. Contended timers perturb contention. Batch percentiles are not per-request tail estimates. WSL vCPU frequency/governor and physical-core isolation are unknown. Historical timings are contextual, not a controlled before/after speedup comparison.

Verify identical native/provider outputs, fixture records and CASE-A/B/C trajectory checksums between original and diagnostic builds and against direct-v2 where applicable. Run existing regression tests. Every difference must be reported and investigated. No optimisation is implemented. Rank measured costs and exact reuse conditions; Amdahl bounds are upper bounds only, with cache lookup/memory/synchronisation costs explicitly excluded, not claimed measured benefits.

## Exploratory supplement and publication notes

After the primary full-provider concurrency measurements, isolated native-only, rotation-only, UTC-only, Sun-only and external-RHS-Sun-plus-provider experiments were added to distinguish shared-SPICE contention from native serialization. This is an exploratory extension, not retroactively preregistered workload selection. The same original/generated libraries, timer, 256-call warm-up, 5 batches, 2000 calls/worker and 1/2/4 workers were retained. Supplemental commands/results are recorded separately in the manifest. No concurrent full six-hour propagation is claimed.

The final complete reproduction command was exercised into a new `/tmp/atm-prof-001-reproduction` directory after tooling corrections; it reruns all fixed, controlled, six-hour, fixture and regression paths. Its manifest/scientific verification summary is retained in published evidence. Diagnostic C++ formatting used isolated clang-format23.1.3; rebuilt main diagnostic executables matched the published measured executable SHA256s. Formatting never touched production/native reference sources.
