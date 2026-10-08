# Performance summary

**No accepted production speedup from OPT-001.** Cubic Hermite caching is faster
in timing-only measurements, but all four six-hour coupled-propagation
configurations violate the predeclared final-state/attitude/work accuracy limits.
Direct SPICE remains the default/reference. The optional provider is experimental;
no production interpolation interval is selected.

## Revisions and machine

- Original Stage 3 baseline/main: `48961a2137f87bb4a1daa80ae880ecc812dad5da`.
- Latest evaluated implementation snapshot: `a799fc6605b15b3c761a74ad446450d34ceee514`
  (`refs/benchmarks/opt001-final`), **not an accepted optimised deployment**.
- Earlier failed candidate: `b9b2f1f99a6d6886dbcd654306788002c47dfb5b`
  (`refs/benchmarks/opt001-candidate`).
- Snapshot commits preserve source without moving main/HEAD or touching the user index.
- AMD Ryzen 7 5800H, Linux x86_64 WSL2; GCC 13.3.0 Release, C++20,
  `-O3 -DNDEBUG -Wall -Wextra -Wpedantic`, no fast-math/LTO/native ISA.
- Eigen 3.4.0, Boost 1.83.0, CSPICE N0067; kernel file hashes,
  compiler/build commands and source/executable identities are preserved in metadata.

## Original baseline versus current default

Same driver/workloads, archived original versus archived final code, one warm-up
and seven measured runs. No physical model or integration tolerance changes.
Times below are wall runtime medians; ranges are minimum–maximum.

| Benchmark | Original median (ms) [range] | Current default median (ms) [range] | Original/current measured ratio | RHS count |
|---|---:|---:|---:|---:|
| BENCH-01 kernel RHS, 200000 changing states | 34.1624 [32.4663–35.4259] | 31.1212 [30.2300–33.3612] | 1.098× | 200000 |
| BENCH-02 six-hour orbital J2 | 2.1005 [1.9823–2.5816] | 2.0172 [1.9163–2.1029] | 1.041× | 10513 |
| BENCH-03 deterministic environmental fallback | 34.9188 [32.8843–38.0458] | 31.8179 [31.3264–43.5570] | 1.097× | 42764 |
| BENCH-03 real direct SPICE | 134.0898 [130.0097–161.6797] | 139.1260 [127.9343–164.7300] | 0.964× | 43259 |

All ranges overlap; these fluctuations are not attributed to an optimisation.
Original BENCH-01 throughput: **170.812 ns/RHS**, **5.854 million RHS/s**.
Original BENCH-02 throughput: **5.005 million RHS/s**.
Original real BENCH-03 throughput: **322612 RHS/s**, **43259 SPICE calls**.
All unchanged final direct/deterministic reference outputs and checksums match
original values bit-for-bit. Independent original-to-current measurements are
retained; individual candidate ratios were not added together.

## OPT-001 observations — not adopted

Original instrumented query regions occupy **74.630%** of environmental propagation
runtime. This measures the inclusive SPICE provider path, not `spkez_c` alone;
clock and gap-recording overhead is disclosed and excluded from ordinary timings.
The adaptive sequence contains 11358 repeated epochs and 1162 backward jumps.

Implemented optional owned SI/J2000 Sun state cache, cubic Hermite position plus
analytic derivative velocity, explicit bounds, no extrapolation or allocation
on evaluation. Eclipse remains a current-spacecraft calculation.

| Interval (s) | Cached propagation median (ms) | Preprocess median (ms) | End-to-end median (ms) | End-to-end timing ratio vs original | Accuracy accepted |
|---:|---:|---:|---:|---:|---|
| 60 | 32.8413 | 0.9028 | 33.7380 | 3.974× | No |
| 300 | 34.1039 | 0.3148 | 34.4247 | 3.895× | No |
| 900 | 34.4085 | 0.2098 | 34.6176 | 3.873× | No |
| 1800 | 33.7802 | 0.1785 | 33.9587 | 3.949× | No |

End-to-end includes cache construction + propagation; common kernel loading and
postprocessing are reported separately. Owned storage is 784–20272 bytes for the
six-hour caches, 13–361 underlying construction calls and none during propagation.
Timing-only amortisation estimates span 78–394 queries for the six-hour cache;
they do not establish safe deployment or measured short-arc break-even.

### Numerical evidence and unsuccessful experiments

All intervals reproduce cached nodes exactly; quarter/mid/three-quarter samples
over 30 days have maximum position error below **5.710e-5 m** and velocity error
below **1.262e-6 m/s**. Maximum reference-geometry SRP force/torque differences
are **1.860e-13 N / 6.172e-15 N m**, illumination difference **1.200e-8**.
Coverage boundaries, shortened intervals, invalid inputs and repeated determinism pass.

Those accurate ephemerides do not meet the full six-hour application criteria.
Even the least divergent 300 s candidate has **0.107147 m** final-position,
**0.0125414 rad** final-attitude and **0.164505 J** SRP-work differences; limits
are 0.01 m, 1e-6 rad and 1e-5 J. Every interval is rejected, not re-pinned to
looser thresholds. A separate constant 30 µm direct-Sun-X perturbation produces
0.110523 m / 0.0133065 rad final differences, directly demonstrating sensitivity
of the fixed workload. No integrator defect or chaos exponent is inferred as fact.

A 60 s direct/cached smoke agrees in final state on GCC (work difference
8.882e-16 J); that is a smoke proof, not a substitute for six-hour acceptance.

## Verification and reproducibility

- GCC Release: **99/99** analytical/regression tests pass.
- Clang 18.1.3 Release (`-Werror`): **99/99** pass.
- Clang Debug ASan/UBSan with leak checking/halt-on-UB: **99/99** pass.
- Clang Release and sanitizer benchmark binaries build and execute the cache path.
- Real DE432s/NAIF0012 checks and plotting smoke included in all full suites.
- CSPICE-disabled runner executes all three deterministic cases with original
  numerical references preserved; no external kernel dependency for that mode.
- **294 measured rows** plus warm-ups, profiles, raw errors and complete test logs
  preserved. The first unsuccessful candidate and final rejected configurations remain.
- CI workflow includes cache tests and a short-arc direct/cached smoke; remote CI
  execution is not claimed. External CSPICE is prebuilt, not sanitizer-instrumented.

Full configurations, interval ranges, RMS errors, adaptive counts and decisions:
[OPTIMISATIONS.md](OPTIMISATIONS.md). Replay commands:
[README.md](README.md). Fixed accuracy limits: [ACCURACY.md](ACCURACY.md).
Raw original/final data: [results.csv](results.csv),
[evidence/baseline](evidence/baseline/), [evidence/final](evidence/final/),
[derived analysis](evidence/analysis.json), [validation](evidence/validation/summary.json).

## Potential CV/interview evidence

- Established content-addressed, baseline-preserving C++ orbital/environmental
  benchmarks with seven measured repetitions, numerical references, kernel hashes
  and 294 raw measurements across original and experimental configurations.
- Profiled 43259 direct SPICE queries in a six-hour coupled LEO workload, measured
  a 74.6% inclusive query-path share and implemented allocation-free cubic Hermite
  Sun-state lookup with deterministic bounds and exact node reproduction.
- Rejected approximately 4× timing-only cache gains when application accuracy
  failed, despite sampled 30-day ephemeris position errors below 58 µm; retained
  failed experiments and validated GCC, Clang and ASan/UBSan regressions.

Do not claim an accepted propagation speedup, production interval, or improved
trajectory accuracy from this experiment.

## Subsequent Stage 4 investigation candidates (not implemented)

The unchanged direct-SPICE query path remains dominant. Separately profile
force/eclipse/atmosphere and integrator work before optimising those regions.
Investigate workload sensitivity/global error budgets before adopting any
approximate ephemeris. These are future investigations, not changes in OPT-001.
No unrelated optimisation or OSS packaging was begun.
