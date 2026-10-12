# MT-001 parameters

Engineering selections, not measured optimal settings. Full executed commands, source/build/dependency hashes and actual execution order are retained in the evidence manifest and raw documents.

## Execution

| Setting | Value | Meaning |
|---|---|---|
| API default workers | 1 | Inline, zero created threads |
| API default policy | `static_partition` | Balanced contiguous trajectories |
| Alternative policy | `dynamic` | Relaxed atomic task index, once per trajectory |
| Parallel worker bound | `min(configured_workers, trajectories)` | Caller participates only for configured worker count 1 |
| Pool lifetime | One batch call | Created and joined before return; no persistent worker service |
| Factory | Borrowed forwarding reference | Concurrent-call-safe; constructed RHS confined to its trajectory |
| Integrator/stepper/output | Per trajectory | No shared mutable numerical state |
| Failure | Continue, then lowest failing input index | Original exception retained; no partial output |
| SPICE provider | One per experiment process | Stable explicitly loaded kernels; all trajectories borrow it |
| Weather | One immutable dataset per process | Const maps shared; native input records local |
| Native models | Per-trajectory adapters, process-global locks retained | C NRLMSISE-00 and official Fortran JB2008 non-reentrant |

## Hardware and scheduling

AMD Ryzen 7 5800H, 8 physical cores / 16 logical processors. WSL2 exposes all CPUs 0–15; `cpu.max = max 100000` (no cgroup quota). Logical siblings are `(0,1), (2,3), ..., (14,15)`.

Benchmark processes use affinity `0,2,4,6,8,10,12,14`: one logical processor per physical core. Individual workers are not pinned; OS scheduling may migrate them within this set. Correctness runs use all available CPUs to exercise 16 actual workers. No realtime policy, priority adjustment, NUMA manipulation or host-wide isolation. Host/WSL scheduling, thermal and frequency variation remain limitations. CPU affinity is a selected measurement control, not a tuned optimum.

## Physics and numerical settings

Frozen CASE-A/B configs supply spacecraft mass, inertia, surfaces, orbital elements, attitude and angular rate. CASE-C supplies the separate numerical stress configuration, never a new convergence criterion. Config files and all existing evidence remain untouched. The atmospheric experiments explicitly replace the cfg's legacy ET metadata with the matched real epoch below, as the existing atmosphere experiment does.

| Parameter | Value |
|---|---:|
| Epoch UTC | `2003-10-30T00:00:00` |
| Earth-fixed frame | `ITRF93` |
| Correctness duration | 21600 s |
| Output cadence | 30 s |
| Relative tolerance | `1e-10` |
| Position absolute tolerance | `1e-3` m |
| Velocity absolute tolerance | `1e-6` m/s |
| Quaternion absolute tolerance | `1e-12` |
| Angular-rate absolute tolerance | `1e-12` rad/s |
| Initial step | 1 s |
| Maximum step | 15 s |
| Maximum attempts | 1000000 |

Stepper: existing Boost.Odeint Dormand–Prince 5(4), unchanged adaptive error control, exact sample clipping and quaternion projection. No relaxed tolerance or integration modification.

Workloads: pure point-mass two-body; point-mass + J2; full CASE dynamics with exponential, direct NRLMSISE-00 or direct JB2008 atmosphere. Full dynamics retain conical SRP force/torque, Sentman drag force/torque, albedo force/torque and gravity-gradient torque. J2 axis remains the existing fixed J2000 +Z approximation.

Production reuse switches all **on**: UTC civil-day exact reuse, compatible Sun sharing, orientation exact reuse. UTC capacity 32 FIFO civil dates; orientation 64 × 4 exact-epoch entries. No density caching/interpolation. Default NRLMSISE switches are recorded as all 24 values in correctness evidence; daily Ap is the main matched configuration, with history-mode correctness comparisons explicitly separate.

## Correctness protocol

Identical initial-state batch size 4; configured workers 1,2,4,8,16; both policies; two repeated runs per configuration. Perturbed batches use SplitMix64, seed `0x5DEECE66D0F1A7C3`, deterministic position scale 50 m, velocity 0.02 m/s and angular-rate 1e-4 rad/s. The driver records the generation algorithm, parameters and actual resulting inputs/checksums. Quaternion perturbation is not introduced.

Additional groups: batch 2 with 16 configured workers; 16 actual trajectories/workers for two-body/J2; native Ap-history enabled with workers 1 and 4. All comparisons are against a sequential reference built with exactly the same group inputs/options. Complete binary64 trajectories, RHS-stage results, atmospheric inputs and output diagnostics are compared without a numerical tolerance. Permanent tests additionally cover empty/single/invalid options, multiple failure ordering, move-only state ownership and synchronized exception cleanup.

A separate 300 s group uses batch size 16 and **actual** worker counts 1,2,4,8,16 for every workload and CASE-A/B/C, both policies and two repetitions. This distinguishes configured workers greater than input count (clamped in the six-hour size-4 group) from actual high-concurrency native/environmental execution. These short-run timings are correctness observations only, never benchmark evidence.

Long-run flags: `--validation-traces off --validation-input-traces off` (complete
output trajectories, density/load diagnostics and counts still compared). The
300 s high-worker run leaves both trace flags on. The fully traced long-run
attempt exceeded a 3600 s command deadline and produced no summary; accepted
long-run validation completed without that deadline. This is a tracing-cost
selection, not a change to physics, output schedule or numeric acceptance.

## Inputs and build

Dependencies are pre-acquired, never fetched by configure: `/tmp/leo-deps/usr` (Eigen/Boost), `/tmp/leo-deps/catch-install` (Catch2), `/tmp/leo_cspice/cspice`, `/tmp/leo-atmosphere-deps` (native sources/kernels/weather), `/tmp/leo-fortran/usr/bin/gfortran-13`.

Kernel list: `naif0012.tls`, `de432s.bsp`, `earth_000101_270105_261009.bpc`. Weather: `SpaceWeather-All-v1.2_snapshot_20200224.txt`, `SOLFSMY.TXT`, `DTCFILE.TXT`. Exact hashes in manifest; required coverage/data absence is a hard failure, never a fallback.

Release C/C++ flags `-O3 -DNDEBUG`, C++20, GNU C/C++13; GNU Fortran13 `-O3`, official fixed-format JB2008 with existing `-std=legacy`. No fast-math, LTO, architecture-specific flags or scientific-parameter tuning. Regression build also retains `-Wall -Wextra -Wpedantic`. Actual versions, cache and compilation-database hashes are recorded by `manifest.py`.

## Benchmark protocol

| Experiment | Cases | Duration | Sizes | Configured workers | Policies / variants | Measured repetitions |
|---|---|---:|---|---|---|---:|
| Complete throughput matrix | CASE-A | 300 s | 1,2,4,8,16,32,64 | 1,2,4,8 | Static + dynamic; identical + perturbed | 5 |
| Representative long-run confirmation | CASE-A/B | 21600 s | 8 | 1,2,4,8 | Static + dynamic; identical + perturbed | 5 |
| Separate contention diagnosis | CASE-A | 300 s | 8,32 | 1,2,4,8 | Static + dynamic; identical + perturbed | 3 |
| Startup probe | No scientific workload | Gate-only | 0/1/2/4/8 created threads | Explicit created counts | Interleaved counts | 200 |

All propagation benchmark configurations have one warm-up. Zero-RHS controls have three measured repetitions per policy/count/size/variant/model. Startup probe has 20 warm-ups per created-thread count. Complete matrix/confirmations use production libraries; contention run uses separately linked diagnostic copies.

All four requested benchmark workloads run in each propagation experiment: two-body + J2, full exponential, full NRLMSISE-00, full JB2008. The 300 s screening duration is an engineering selection, not a scientifically converged interval or an optimal workload duration. Six-hour CASE-A/B confirmation checks whether screening conclusions survive representative longer integrations.

Benchmark seed `20261012`; SplitMix64, uniformly bounded independent Cartesian position offsets ±50 m, velocity offsets ±0.02 m/s and body-rate offsets ±1e-4 rad/s. Quaternion unchanged. State for trajectory index i is independent of batch size, worker count or execution policy. The same seed controls the recorded seeded-shuffle configuration order. Model switches, numerical tolerances and output schedule match their sequential baseline.

No unsupported 1/2/4/8 count is omitted on this 8-core affinity set. Worker count > batch size is retained with its requested count and actual bound; efficiency against requested workers is deliberately lower in this case. No maximum-speedup target or automatic worker-count tuning.
