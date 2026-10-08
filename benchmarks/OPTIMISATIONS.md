# Optimisation ledger

Original pre-Stage-4 main / Stage 3 baseline:
`48961a2137f87bb4a1daa80ae880ecc812dad5da`.
This reference is permanent and is not replaced by an optimisation's immediate baseline.

## OPT-001 — Interpolated Sun Ephemeris Caching

**Decision: inconclusive as a deployable optimisation; reject every evaluated
six-hour configuration.** Retain the requested optional provider as explicitly
experimental, retain all failed measurements, select **no production interval**,
and leave direct SPICE as the reference/default. Timing reductions below are
**rejected-candidate observations, not accepted performance improvements**.
The acceptance limits were not relaxed.

### Change and revisions

Hypothesis: precompute geometric Earth-to-Sun position/velocity and replace repeated
CSPICE queries with cubic Hermite interpolation, amortising construction over the
adaptive RHS calls. No other optimisation, physics, integration algorithm or
tolerance change was attempted.

- Original baseline and immediate OPT-001 before SHA:
  `48961a2137f87bb4a1daa80ae880ecc812dad5da`.
- First measured candidate SHA:
  `b9b2f1f99a6d6886dbcd654306788002c47dfb5b`
  (`refs/benchmarks/opt001-candidate`).
- Final evaluated implementation SHA:
  `a799fc6605b15b3c761a74ad446450d34ceee514`
  (`refs/benchmarks/opt001-final`).

These are actual immutable Git snapshot commits created using a temporary index;
main/HEAD and the user's index were not moved. Archive compilation inputs are
verified against the Git objects by the runner. Final documentation/evidence is
outside the earlier code snapshot; metadata also records source, driver-source
and executable hashes. The final snapshot fixes a tiny-range division-underflow
boundary defect and links cached CSV correctness to the independent accuracy
sweep; ordinary-workload numerical outputs match the first candidate exactly.
No benchmark workload or numerical acceptance threshold changed after baseline.

### Workload and environment

See [README](README.md) for exact inputs and replay commands and
[baseline.json](baseline.json) for original machine/dependency/kernel metadata.
AMD Ryzen 7 5800H, Linux x86_64 WSL2, GCC 13.3.0, C++20,
`-O3 -DNDEBUG -Wall -Wextra -Wpedantic`; Eigen 3.4.0, Boost 1.83.0,
CSPICE N0067, DE432s/NAIF0012 hashes recorded explicitly.

BENCH-03: ET=0, six-hour 400 km inclined LEO, 10 kg six-face box, J2,
SRP/conical eclipse, Sentman drag, albedo, gravity-gradient and all environmental
force/torque switches, coupled attitude. Dormand–Prince 5(4), 30 s output cadence,
original explicit tolerances (rtol 1e-10; field atols 1e-3/1e-6/1e-12/1e-12),
initial/max step 1/15 s. All direct/cached cases use identical spacecraft,
initial state, epochs, sample labels, physics, solver and tolerances.

One warm-up followed by seven measured repetitions per configuration. Runs are
sequential; comparison timings were not collected concurrently with builds or
regression suites. Host scheduling, turbo and thermal state are uncontrolled.
Reported ranges are observed variation, not confidence intervals.

### Bottleneck evidence before implementation

[Original instrumented profile](evidence/baseline/profile.json):

- 43,259 RHS evaluations and 43,259 underlying SPICE queries in six simulated hours.
- Instrumented total 137.279 ms; timed query regions 102.452 ms, **74.630%**.
- Simulated-epoch gaps: median 0.367827 s, signed mean 0.499330 s,
  minimum -7.345813 s, maximum 4.084302 s; 11,358 zero gaps and 1,162 backward gaps.
- Real query start-to-start gaps: median 2.705 µs, mean 3.173 µs.
- Empty clock-pair/loop calibration: 2.388 ms total for 43,259 iterations.

The query timer includes the SPICE provider's mutex, error handling, CSPICE call,
SI conversion and instrumentation/gap recording. It is **not** a symbol-level
measurement of `spkez_c` alone. The instrumented fraction is an overhead-biased
query-path estimate. Published baseline timings have no per-query clocks or gap
allocations. The measurements establish the query path as significant; the
remaining force/geometry/integrator time was not split by function.

### Cache architecture

`include/leo/environment/interpolated.hpp`: header-only, CSPICE-independent
`EphemerisProvider` wrapper. Construction requests Sun 10 SI position/velocity
nodes over an explicit increasing ET interval, including exact endpoints and a
shortened final segment. The source contract preserves Earth observer/J2000/NONE.
The source is not borrowed after construction; owned samples are immutable.

Difference-form cubic Hermite position and its analytic derivative velocity avoid
subtracting large absolute Sun coordinates in the derivative. Exact node queries
return original samples. Lookup supports arbitrary/backward times; no mutable
cursor, global cache, eviction, threads or evaluation allocation. Unsupported
targets/nonfinite epochs reject; epochs outside coverage throw `out_of_range`.
Nonfinite source samples and invalid/unrepresentable grids reject.

The cache stores **Sun state only**. `EarthEnvironment` still calculates eclipse
and atmospheric acquisition from the current spacecraft at every evaluation.
Direct `SpiceEphemerisProvider` and all Stage 1–3 physical source files are unchanged.

### Before and after timings

Original direct-SPICE BENCH-03: median **134.090 ms**, range **130.010–161.680 ms**,
43,259 RHS / SPICE calls. Final snapshot direct-SPICE rerun: median **139.126 ms**,
range **127.934–164.730 ms**, identical RHS count and bitwise numerical reference.
Those ranges overlap; no direct-path improvement is claimed.

Final cached sweep, same six-hour propagation and 30 s outputs:

| Interval (s) | Preprocess median (ms) [min–max] | Propagation median (ms) [min–max] | End-to-end median (ms) [min–max] | RHS count | SPICE construction calls | Owned bytes |
|---:|---:|---:|---:|---:|---:|---:|
| 60 | 0.9028 [0.8754–0.9481] | 32.8413 [32.3782–34.0966] | 33.7380 [33.2730–34.9994] | 43389 | 361 | 20272 |
| 300 | 0.3148 [0.3024–0.4611] | 34.1039 [32.9276–35.0377] | 34.4247 [33.3888–35.3798] | 43330 | 73 | 4144 |
| 900 | 0.2098 [0.2007–0.4288] | 34.4085 [33.1787–48.4801] | 34.6176 [33.3864–48.9089] | 43199 | 25 | 1456 |
| 1800 | 0.1785 [0.1695–0.2505] | 33.7802 [31.9892–34.9161] | 33.9587 [32.2041–35.0856] | 44262 | 13 | 784 |

Zero underlying SPICE calls occur during cached propagation; the active cache
receives one query per RHS. Postprocessing adds 721 provider requests outside
the timed propagation region and is recorded separately. Memory includes object
and vector capacity, excludes allocator bookkeeping. Preprocessing includes the
cache object construction, sampling and wrapper setup. Kernel file loading is
common explicit initialisation, separately reported, excluded for both modes.
End-to-end here means exactly cache construction + propagation, as required.

### Query throughput and amortisation (timing only)

200000 identical deterministic changing epochs, preallocated output buffer;
output consumption/checksums outside timing. Original direct batch median
2261.76 ns/query; final direct rerun **2312.28 ns/query** (432474 queries/s).

| Interval (s) | Cached ns/query median [min–max] | Queries/s | Steady-state ratio vs original | End-to-end ratio vs original | Estimated amortisation queries |
|---:|---:|---:|---:|---:|---:|
| 60 | 17.334 [15.800–20.160] | 57689024 | 4.083× | 3.974× | 394 |
| 300 | 16.097 [15.973–18.131] | 62123975 | 3.932× | 3.895× | 138 |
| 900 | 16.308 [16.146–18.621] | 61319805 | 3.897× | 3.873× | 92 |
| 1800 | 17.172 [15.452–18.198] | 58233453 | 3.969× | 3.949× | 78 |

Ratios compare full propagation, not query-only throughput. Query-only throughput
and full-propagation ratios are deliberately distinguished. Amortisation estimate:
`ceil(cache construction median / (direct ns/query − cached ns/query))`, using
final same-machine direct query measurements and the six-hour cache. It is a
cost model, not a measured short-arc break-even point; a short arc uses fewer
nodes, and the CPU/cache regime differs. It does **not** establish safe use cases
because the six-hour accuracy gate failed. The 60 s smoke is correctness evidence
only, not a speedup or production-interval selection.

### Numerical correctness: rejected despite accurate ephemerides

[ACCURACY.md](ACCURACY.md) was written before implementation and selection.
Node position and velocity errors are zero for all evaluated intervals. Both
endpoints, nonmultiple final segments, invalid epochs/targets, strict coverage
neighbours, repeated-query determinism and repeated-propagation determinism pass.
Intermediate six-hour and 30-day ephemeris limits pass for every interval.

30-day node/quarter/mid/three-quarter sampling:

| Interval (s) | Position max (m) | Position RMS (m) | Velocity max (m/s) | Velocity RMS (m/s) |
|---:|---:|---:|---:|---:|
| 60 | 5.502e-5 | 1.630e-5 | 1.262e-6 | 3.516e-7 |
| 300 | 5.709e-5 | 1.629e-5 | 2.356e-7 | 6.988e-8 |
| 900 | 4.641e-5 | 1.628e-5 | 7.653e-8 | 2.339e-8 |
| 1800 | 4.641e-5 | 1.791e-5 | 4.018e-8 | 1.794e-8 |

Reference-state force/torque and deliberately dense shadow-edge checks also pass:
maximum SRP force difference **1.860e-13 N**, torque **6.172e-15 N m**,
illumination **1.200e-8** for each interval; 6724 geometries including 2367
penumbra points. RMS values and six-hour/nonmultiple/30-day sampling errors are
retained in each raw accuracy JSON, not only maxima.

But full six-hour coupled propagation fails:

| Interval (s) | Final position (m) | Final velocity (m/s) | Final attitude (rad) | Final omega (rad/s) | SRP work difference (J) | Accepted |
|---:|---:|---:|---:|---:|---:|---|
| 60 | 3.47717 | 0.00346975 | 0.465690 | 0.00157400 | 1.04874 | No |
| 300 | 0.107147 | 0.000120953 | 0.0125414 | 0.0000502334 | 0.164505 | No |
| 900 | 5.55862 | 0.00643043 | 0.916028 | 0.00337640 | 6.41524 | No |
| 1800 | 4.73457 | 0.00515515 | 0.732966 | 0.00265581 | 4.63888 | No |
| Fixed limit | 0.01 | 1e-5 | 1e-6 | 1e-8 | 1e-5 | Required |

Work uses identical 30 s trapezoidal SRP power diagnostics, not a new integrated
state. Changed adaptive RHS counts are reported rather than hidden.

[Separate sensitivity experiment](evidence/sensitivity.json): adding only
30 µm to direct Sun X produces 0.110523 m final-position and 0.0133065 rad
final-attitude differences over the unchanged six-hour workload. This directly
demonstrates workload sensitivity, without proving a Lyapunov exponent or an
integrator defect. It is not grounds to relax criteria or redesign Stage 1–3.
All unchanged direct/deterministic final references match the original baseline
bit-for-bit. Cached full trajectories are deterministic but not accurate enough.

### Verification and development corrections

- GCC 13.3 Release: **99/99** tests pass, including original Stage 1–3 analytical,
  environmental, integration, analysis and plotting smoke checks and six new cache tests.
- Clang 18.1.3 Release (`-Werror`): **99/99** pass.
- Clang 18.1.3 Debug ASan/UBSan, leak checking / halt-on-UB: **99/99** pass.
- GCC, Clang and ASan/UBSan benchmark executables built and exercised; direct/cached
  60 s smoke passes fixed absolute state/work limits. CSPICE itself is the external
  prebuilt, uninstrumented N0067 library.
- Offline CSPICE-disabled runner: three deterministic cases, seven measurements
  each; original numerical references match. No downloaded kernels or fake SPICE fallback.
- CI now builds/runs the short-arc direct/cached numerical smoke on GCC Release;
  existing GCC/Clang/sanitizer matrix exercises cache tests. Remote CI was not run here.

Initial benchmark setup failed dependency discovery before any measurements.
Harness review corrected synthetic-RHS substitution, cadence/interval coupling,
non-query timing attribution and string JSON emission before preserving the
original baseline. A test fixture `nan` naming conflict was fixed before tests
passed. Cache-grid `span/interval` underflow initially produced only one endpoint;
a throwaway repro observed that defect, the final code enforces at least one
segment, and a consumer endpoint/intermediate regression passes under sanitizers.
These are development corrections within OPT-001, not additional optimisations.

### Evidence retained and limitations

- [results.csv](results.csv): **294** individual measured rows: original 35,
  first candidate 119, final candidate 119, offline 21; warm-ups stored separately.
- [Original metadata](evidence/baseline/metadata.json),
  [original numerical references/timings](evidence/baseline/timings.json).
- [First candidate evidence](evidence/candidate/timings.json) and raw files retained
  unchanged, including failed intervals. Its BENCH-03 booleans describe finite
  trajectories; the separate ACCURACY files reject all intervals.
- [Final timings](evidence/final/timings.json), [accuracy acceptance](evidence/final/accuracy.json),
  [derived metrics/method](evidence/analysis.json); final CSV cached propagation
  correctness is false, not merely a finite-result check.
- [Regression logs/summary](evidence/validation/summary.json),
  [60 s smoke](evidence/short-arc-smoke.json), [offline replay](evidence/offline/timings.json).

No accepted end-to-end speedup and no production interpolation interval.
Do not use the rejected ratios as CV performance achievements. No subsequent
optimisation, integrator redesign, new physics, packaging or unrelated cleanup
was started. Future work must profile and baseline independently, retain this
original reference, and satisfy unchanged numerical criteria before adoption.
