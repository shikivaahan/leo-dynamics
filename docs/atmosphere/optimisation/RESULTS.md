# ATM-OPT-001 measured results

Single completed run, `evidence/measurements/`. Every number below is from `summary.json`; raw per-call distributions, per-batch timings, counters, commands and hashes are retained in that directory. Production speedups use only the uninstrumented unchanged-original and uninstrumented candidate executables. Counter values are from the separately identified original/candidate diagnostic processes.

Host: WSL2, GCC 13.3.0 Release `-O3 -DNDEBUG`, single-thread workloads pinned to logical CPU0, concurrent sweeps on CPUs0–3, CSPICE N0067, frozen kernels/weather/case settings. WSL2 scheduling, physical-core isolation and frequency governor are uncontrolled (no governor files exposed; no `perf`). Measurement spreads and unavailability of a statistical p99 at three propagation repeats are stated rather than smoothed.

## A. Fixed full provider (Sun already available to the candidate path)

Mean of 10,000 measured calls; five batches; 256 warm-up calls excluded. p95/p99 and batch spread retained in `summary.json`.

| Model | Configuration | Original ns | Candidate ns | Speedup | p95 ns | p99 ns | Batch spread |
|---|---|---:|---:|---:|---:|---:|---:|
| NRLMSISE-00 | off/off/off control | 30,450 | 34,168 | 0.891× | 50,827 | 69,055 | 0.066 |
| NRLMSISE-00 | UTC only | 30,450 | 11,652 | 2.613× | 16,623 | 31,134 | 0.017 |
| NRLMSISE-00 | Sun only | 30,450 | 37,536 | 0.811× | 60,321 | 89,040 | 0.060 |
| NRLMSISE-00 | Orientation only | 30,450 | 28,891 | 1.054× | 35,379 | 53,847 | 0.022 |
| NRLMSISE-00 | UTC + Sun | 30,450 | 10,133 | 3.005× | 10,249 | 22,254 | 0.024 |
| NRLMSISE-00 | UTC + Orientation | 30,450 | 8,177 | 3.724× | 12,169 | 18,729 | 0.063 |
| NRLMSISE-00 | Sun + Orientation | 30,450 | 30,227 | 1.007× | 45,049 | 62,360 | 0.071 |
| NRLMSISE-00 | **All** | 30,450 | **9,543** | **3.191×** | 14,502 | 27,980 | 0.042 |
| JB2008 | off/off/off control | 39,131 | 44,621 | 0.877× | 71,829 | 94,220 | 0.086 |
| JB2008 | UTC only | 39,131 | 19,249 | 2.033× | 29,811 | 44,470 | 0.060 |
| JB2008 | Sun only | 39,131 | 37,294 | 1.049× | 54,628 | 73,878 | 0.060 |
| JB2008 | Orientation only | 39,131 | 37,150 | 1.053× | 50,910 | 75,119 | 0.045 |
| JB2008 | UTC + Sun | 39,131 | 16,402 | 2.386× | 25,339 | 40,431 | 0.122 |
| JB2008 | UTC + Orientation | 39,131 | 13,955 | 2.804× | 14,531 | 27,646 | 0.016 |
| JB2008 | Sun + Orientation | 39,131 | 33,293 | 1.175× | 48,750 | 58,925 | 0.046 |
| JB2008 | **All** | 39,131 | **12,143** | **3.223×** | 18,331 | 25,202 | 0.092 |
| Exponential control | All | 58.7 | 68.8 | 0.853× | 74 | 74 | 0.063 |

Combined gains are measured directly; individual percentages were never summed. The exponential control touches no reuse path and remains within ~15% of the original, consistent with the native model dominating and measurement noise at 70 ns scale.

The off/off/off control is uniformly slower than the original (0.81–0.89×). That regression is the price of the new interface itself: the two-lock UTC parse path, generation rechecks and the shared-cache bookkeeping. It is not attributable to any single optimisation's disabled flag and is smaller than the gains of any enabled combination.

## B. Six-hour CASE-A/B/C propagation (median of three repeats)

| Case/Model | Original s | All s | Wall speedup | RHS median ns | RHS speedup |
|---|---:|---:|---:|---:|---:|
| CASE-A NRLMSISE-00 | 2.092 | 0.765 | 2.734× | 32,172 → 12,288 | 2.618× |
| CASE-A JB2008 | 2.504 | 1.032 | 2.427× | 38,445 → 16,523 | 2.327× |
| CASE-B NRLMSISE-00 | 0.434 | 0.166 | 2.613× | 31,575 → 13,436 | 2.350× |
| CASE-B JB2008 | 1.733 | 0.766 | 2.262× | 41,719 → 18,074 | 2.308× |
| CASE-C NRLMSISE-00 | 2.949 | 1.102 | 2.675× | 32,586 → 12,700 | 2.566× |
| CASE-C JB2008 | 5.091 | 2.168 | 2.349× | 38,160 → 17,373 | 2.197× |

RHS medians are per model; JB2008 is consistently slower per RHS because each call also performs its Sun acquisition, exactly the work Sun sharing removes. All repeated runs of a configuration are bitwise identical to each other and to the unchanged original, and to all nine frozen direct-v2 trajectory checksums. Spread of the All-configuration batch means is 0.002–0.086.

Individual propagation configurations show the same pattern as the provider workload: UTC-only 1.76–2.74×, Sun-only 0.71–1.07×, orientation-only 0.45–1.01×, with the full matrix in `summary.json`. The exponential control is unaffected (0.89–1.08×).

Sun sharing interacts strongly with UTC reuse: measured Sun-only speedups (0.71–1.07×) are smaller than the sum with UTC, while UTC + Sun reaches 1.99–2.47×. Removing half of the Sun acquisitions shortens the remaining shared SPICE critical section that also covers every UTC conversion, so the same removed Sun work buys more once UTC work is gone. Combined results are reported only from direct measurements; this paragraph interprets the interaction, it does not derive it arithmetically.

## C. Cache statistics

Fixed cache storage is 26,376 bytes, reported by the provider as the sum of real `sizeof` expressions rather than a hand-written constant. Verified on this toolchain/ABI by compiling the exact struct definitions from `src/environment/spice.cpp`:

| Storage | Bytes | Derivation |
|---|---:|---|
| Civil-day entries | 1,536 | `sizeof(CivilDay)` 48 × 32 |
| Orientation entries | 24,576 | `sizeof(Orientation)` 96 × 256 |
| Orientation FIFO indices | 256 | `sizeof(std::array<unsigned,64>)` |
| Civil-day FIFO index | 8 | `sizeof(std::size_t)` |
| **Total** | **26,376** | matches the retained `cache_bytes` counter |

`sizeof(CivilDay)` is 48 and `sizeof(Orientation)` is 96 (9 doubles matrix plus two `uint64` keys and a padded frame flag, 8-byte alignment). The earlier claim of "26,112 bytes of orientation matrices and 256 bytes of civil-day table" was wrong and is corrected here. These figures are ABI- and compiler-specific; the code never hard-codes them, so the counter is authoritative on any platform. This is fixed cache storage only, excluding the kernel-ownership `std::map`, the SPICE mutex, diagnostic counters, and CSPICE's own state. Capacity: 32 civil dates (FIFO), 256 matrices (64 sets × 4 FIFO ways). Occupancy after every workload reached full capacity where reuse existed.

Six-hour propagation, diagnostic counters (three repeats summed):

| Case/Model | Configuration | UTC hits/misses | Orientation hits/misses | et2utc | str2et | pxform | spkez |
|---|---|---|---|---:|---:|---:|---:|
| CASE-A JB2008 | Original | — | — | 182,727 | 365,454 | 182,727 | 365,454 |
| CASE-A JB2008 | off/off/off | 0/182,727 | 0/182,727 | 182,727 | 365,454 | 182,727 | 365,454 |
| CASE-A JB2008 | UTC only | 182,727/0 | 0/182,727 | 182,727 | 0 | 182,727 | 365,454 |
| CASE-A JB2008 | Sun only | 0/182,727 | 0/182,727 | 182,727 | 365,454 | 182,727 | 182,727 |
| CASE-A JB2008 | Orientation only | 0/182,727 | 51,789/130,938 | 182,727 | 365,454 | 130,938 | 365,454 |
| CASE-A JB2008 | **All** | 182,727/0 | 51,789/130,938 | 182,727 | 0 | 130,938 | 182,727 |
| CASE-C JB2008 | **All** | 364,344/0 | 86,361/277,983 | 364,344 | 0 | 277,983 | 364,344 |
| CASE-C NRLMSISE-00 | **All** | 252,414/0 | 64,815/187,599 | 252,414 | 0 | 187,599 | 252,414 |

The exact elimination requested is demonstrated by counters, not by timing: UTC reuse removes every one of the 2N midnight `str2et` conversions (365,454 → 0 in CASE-A JB2008); Sun sharing halves `spkez` calls (365,454 → 182,727); orientation reuse removes 28.4% of `pxform` calls (182,727 → 130,938), matching the 28.4% exact-epoch repeat rate predicted by ATM-PROF-001. `et2utc` is unchanged, as required: the per-request conversion is deliberately preserved.

Orientation hit rate by schedule (JB2008, three locality batches of 1,024):

| Schedule | Hits/misses | Rate |
|---|---|---:|
| fixed, same-epoch-changing-positions | 3,072/0 | 1.000 |
| ensemble epoch-major | 2,691/381 | 0.876 |
| ensemble trajectory-major | 2,271/801 | 0.739 |
| all other schedules (nearby, weather boundaries, midnight) | 3/3,069 | 0.001 |

Eviction counts are zero for the civil-day cache across every measured workload and small for the orientation cache only on the ensemble schedules, consistent with exact nonmonotonic locality rather than working-set thrashing.

Deterministic replay of the recorded exact keys reproduces the measured UTC and orientation hit rates for all twenty schedule/model pairs at single-thread arrival; the reducer refuses to publish a summary otherwise (`max |replay − measured| = 0`).

## D. Isolated work removed, lookup cost and mutex behaviour

Mean nanoseconds per call, five batches of 2,000 calls:

| Operation | Mean ns | Note |
|---|---:|---|
| UTC, reuse hit path (full helper) | 4,537 | per-request `et2utc`, parsing, leap-aware quasi-MJD, lock, search |
| UTC, reuse disabled | 25,702 | two midnight conversions every call |
| UTC, exact distinct civil dates (miss path) | 26,587 | dates prepared outside timing by real UTC conversion |
| UTC removed work | **21,165** | miss path − hit path |
| Orientation, reuse hit path (full helper) | 32 | hash + at-most-four checks |
| Orientation, reuse disabled | 2,072 | `pxform_c` every call |
| Orientation, distinct ET (miss path) | 2,061 | |
| Orientation removed work | **2,040** | |
| `observe_sun` | 2,053 | |
| `geometric_state` | 2,042 | observation acquisition itself is not an ephemeris cache |

Actual search attribution inside the held SPICE mutex (diagnostic copy, summed durations/counts): UTC search 22–47 ns, orientation hash+search 25–40 ns. These are timer-inclusive means with the end clock read retained; they are not the isolated cost of a hit, which is why removed work above is derived from the full helper difference instead.

Sun sharing removes a downstream duplicate `spkez` call (~2 µs of provider time), not the acquisition itself.

Mutex behaviour with the retained required locks: the original SPICE mutex was acquired 1.1 million times per CASE-A JB2008 six-hour run and waited 1.12 s in total; at four threads it was acquired 6.4 million times with 25.3% contention and 12.6 s total wait. The All configuration acquires it fewer times and waits far less — 30 ns per acquisition single-threaded, 6,857 ns per acquisition at four threads for JB2008 with 14.3% contention — because fewer SPICE calls occur per RHS. No mutex was removed; reduced acquisitions are a consequence of removed work.

## E. Scientific equivalence

Unchanged-original versus candidate, in all cases bitwise: all 10,710 fixture outputs, nine frozen nominal CASE series, nine frozen refined checksums, eighteen complete nominal/refined original-vs-candidate series (state, velocity, attitude, angular velocity, altitude, density, temperatures, constituents, drag force/torque, cumulative works, matched energy) and 424,025 actual JB2008 native bridge input/output streams. Every candidate control (including off/off/off) additionally reproduces the complete original series and native streams; see VALIDATION.md and `evidence/controls`. All nine six-hour trajectory checksums equal frozen direct-v2 exactly. 172 regression cases pass, including the official NRLMSISE-00 and JB2008 reference fixtures with no skip.

No tolerance was used for any changed value. No scientific setting was changed.

## F. Thread safety and lifecycle

Operation-level linearizability under the existing SPICE mutex; UTC generation recheck across unlocked parsing; orientation hits under the same lock; immutable observations shareable across trajectories. Managed load/unload (including failed loads) invalidates before mutation; refcount-only owner changes do not. Concurrent readers were exercised against managed lifecycle operations in the regression suite, and results were bitwise identical. Cross-provider observation compatibility holds while configuration is unchanged and is refused after actual reconfiguration. Explicit `invalidate_reuse()` mean cost 101.6 ns (p99 110 ns) and results are identical across invalidation.

Guarantee conditional on library-managed kernels is stated explicitly: external CSPICE mutation requires application-wide quiescence plus `invalidate_reuse()`; unreported external mutation is unsupported. Single observations are linearizable; a propagation requiring one coherent configuration must hold that configuration fixed for its duration.

## G. Verdicts

| Optimisation | Verdict | Evidence |
|---|---|---|
| UTC civil-day reuse | **Retained** | Eliminates all 2N midnight conversions; 1.95–2.74× provider/RHS/propagation gains alone; hit path 4.5 µs vs 25.7 µs miss; correct across leap seconds, month/year boundaries, backward queries, eviction, reload and concurrent lifecycle |
| JB2008 shared Sun state | **Retained** | Halves `spkez` calls exactly (365,454 → 182,727); removes a full duplicate acquisition from every JB2008 RHS call; native inputs/outputs byte-identical across shared/standalone, epochs, positions, concurrency, stale and incompatible input; no SRP dependency introduced |
| Earth orientation reuse | **Retained with measured limits** | Removes 28.4% of `pxform` calls in realistic single-trajectory work and 87.5% on epoch-major ensembles; lookup 25–40 ns vs 2,072 ns `pxform`; concurrency is neutral-to-positive at four threads. However its isolated single-trajectory contribution is within measurement spread on some cases (orientation-only propagation 0.45–1.01×) because exact-epoch reuse is workload-dependent, and it shows a mild miss-path penalty on all-distinct-epoch schedules (3/3069 hits) of about 25 ns per call |

Orientation reuse is retained rather than reverted because it is never wrong, its overhead on total miss is ~25 ns against a 2,072 ns direct call (about 1.2%), and it delivers large exact savings exactly where repeated exact epochs occur (ensembles, fixed-epoch calls, retrying integrators) with no measured contention cost. If a deployment has provably no repeated exact epochs, `orientation_reuse=false` disables it at zero scientific cost; this is a documented operational choice, not a required workaround.

Shared versus thread-local cache scope: replay shows thread-local caches would incur 22/66 extra UTC misses and 50/297 extra orientation misses at two/four threads respectively for the same total capacity, and measured shared-cache contention stays low (≤14.3% at four threads) while every configuration still gains 2.3–4.2× throughput versus its control. The shared cache is retained; replay is a locality study, not a measured alternative-throughput claim.

No individual optimisation was reverted. No optimisation was found that regressed relevant workloads once its own control is excluded, except the noted orientation miss-path overhead on all-distinct-epoch schedules and the uniform interface-level off/off/off control cost.

## H. Independent speedups are not additive

The All configuration is measured directly in every table (provider 3.19×/3.22×, propagation 2.26–2.73×, RHS 2.20–2.33×). Individual figures (UTC 2.03–2.61×, Sun 0.81–1.05×, orientation 1.05–1.05×) illustrate attribution and interaction; their sum exceeds the measured combination because the removed SPICE calls share the same critical section. Amdahl reasoning supports interpretation only; every speedup in this report is a runtime measurement.
---

# Closure measurements (2026-10-11)

Everything below is a **new** experiment in `evidence/cleanup-interleave/`, measured after the disabled-path fix with an order-interleaved protocol. The earlier tables in this document (sections A–H) remain the original evidence in `evidence/measurements/` and are unmodified. Where the two disagree, the earlier block-order measurement is the one known to confound configuration with execution order; the interleaved numbers are the trustworthy provider-latency figures. Propagation, concurrency and counter results were not re-measured because the code change cannot affect them: it is confined to the `reuse=false` UTC branch, which production never takes.

## Protocol

Every configuration is warmed (256 calls) before any measurement. Configurations are then executed round-robin inside each of 12 repetitions, with the order shuffled per repetition from recorded seed `20251011` and the order written to the evidence. Each configuration contributes 2,000 timed calls per repetition; all 24,000 per-call samples and all 12 batch means are retained per configuration. Comparisons use Welch intervals on batch means (95%, normal approximation). Three binaries ran serially on CPU0: unchanged original, uninstrumented candidate, diagnostic candidate. Nothing was compiled during measurement.

## Provider latency, order-interleaved (mean of 24,000 calls)

| Model | Configuration | Mean ns | Median ns | p95 ns | p99 ns | Batch SD ns |
|---|---|---:|---:|---:|---:|---:|
| NRLMSISE-00 | Unchanged original | 29,265 | 27,938 | — | — | 720 |
| NRLMSISE-00 | **All off (control)** | 31,756 | 28,655 | 50,925 | 66,742 | 2,996 |
| NRLMSISE-00 | Sun only | 31,176 | 29,013 | 46,875 | 61,360 | 1,738 |
| NRLMSISE-00 | Orientation only | 28,548 | 26,460 | 44,414 | 52,946 | 1,289 |
| NRLMSISE-00 | UTC only | 10,598 | 9,717 | 12,142 | 24,494 | 896 |
| NRLMSISE-00 | UTC + Sun | 10,717 | 9,772 | 12,730 | 23,410 | 1,290 |
| NRLMSISE-00 | UTC + Orientation | 8,356 | 7,687 | 12,021 | 23,777 | 1,029 |
| NRLMSISE-00 | **All on** | 8,198 | 7,669 | 11,634 | 23,229 | 504 |
| JB2008 | Unchanged original | 36,482 | 34,193 | — | — | 1,866 |
| JB2008 | **All off (control)** | 36,603 | 34,293 | 50,180 | 66,529 | 2,402 |
| JB2008 | Sun only | 36,182 | 34,293 | 49,336 | 68,635 | 725 |
| JB2008 | Orientation only | 34,028 | 32,227 | 45,140 | 55,033 | 567 |
| JB2008 | UTC only | 16,273 | 15,466 | 22,545 | 36,018 | 438 |
| JB2008 | UTC + Sun | 16,282 | 15,457 | 22,041 | 33,706 | 346 |
| JB2008 | UTC + Orientation | 14,025 | 13,464 | 19,829 | 31,353 | 282 |
| JB2008 | **All on** | 14,052 | 13,473 | 19,377 | 30,494 | 222 |

Combined speedups against the unchanged original, measured directly: NRLMSISE-00 3.57×, JB2008 2.60× on mean provider latency. Interleaved ratios are modestly better than the earlier block-order figures (3.19×/3.22×), because the earlier all-on batches were measured in the thermally and cache-worst position of their sequence.

## Disabled-path regression: resolved

The all-off control previously measured 34,168 ns (NRLMSISE-00) and 44,621 ns (JB2008), an 0.81–0.89× regression against the unchanged original. After the two-path change (OPT-D10) it measures 31,756 ns and 36,603 ns. Against the unchanged original, measured in the same interleaved run:

| Model | Configuration | Mean difference | t | 95% interval | Distinguishable? |
|---|---|---:|---:|---:|---|
| NRLMSISE-00 | All off | +581.7 ns | +1.57 | [−144.5, +1,307.9] ns | No |
| JB2008 | All off | −309.9 ns | −0.45 | [−1,661.0, +1,041.2] ns | No |
| NRLMSISE-00 | All on | −20,982.1 ns | −39.68 | [−22,018.6, −19,945.6] ns | Yes (gain) |
| JB2008 | All on | −22,544.7 ns | −39.47 | [−23,664.3, −21,425.2] ns | Yes (gain) |

The disabled path is now statistically indistinguishable from the unchanged original (both intervals contain zero), and the default optimised path is unchanged and remains a large, highly significant gain (|t| ≈ 40). Residual difference is within WSL2 run-to-run variation: 12 repetitions on one shared CPU, no frequency control, no dedicated cores.

## Benchmark consistency: the NRLMSISE-00 anomaly resolved

The apparent inconsistency was **measurement order, not code**. Under the interleaved protocol, with actual per-call operation counts from the diagnostic build:

| Model | Configuration pair | str2et/call | spkez/call | Result checksum | Mean difference | t | 95% interval | Distinguishable? |
|---|---|---|---|---|---:|---:|---:|---|
| NRLMSISE-00 | UTC only vs UTC + Sun | 0.00 / 0.00 | **0.00 / 0.00** | identical | −118.8 ns | −0.26 | [−1,007.4, +769.7] | No |
| NRLMSISE-00 | Orientation only vs Sun + Orientation | 2.00 / 2.00 | **0.00 / 0.00** | identical | −1,150.1 ns | −0.75 | [−4,166.7, +1,866.4] | No |
| JB2008 | UTC only vs UTC + Sun | 0.00 / 0.00 | 1.00 / 1.00 | identical | −9.1 ns | −0.06 | [−324.9, +306.7] | No |
| JB2008 | Orientation only vs Sun + Orientation | 2.00 / 2.00 | 1.00 / 1.00 | identical | +156.8 ns | +0.69 | [−288.1, +601.7] | No |

NRLMSISE-00 performs **zero** Sun lookups per call in every configuration — it acquires no Sun at all — and all eight configurations return one identical result checksum, equal to the unchanged original. Every Sun-toggle difference has a 95% interval spanning zero. The earlier 11,652 / 10,133 / 8,177 / 9,543 ns spread was an artefact of contiguous, non-randomised execution order. JB2008, which does consume a Sun, shows the same pattern here because at a *fixed* epoch both its UTC-only and UTC+Sun variants still perform exactly one Sun lookup; Sun sharing only removes the duplicate when a caller supplies an observation, which the propagation workload does and this fixed-epoch provider workload does not.

## Untouched by the cleanup

Disabled-path overhead is the only performance dimension the code change can affect, and it is confined to `utc_calendar(reuse=false)`, which production never takes. The six-hour propagation, RHS latency, concurrency throughput, cache counters and SPICE-call elimination in sections B–G above were re-verified for scientific equality (all bitwise identical) but are not re-timed; the timing values there stand.
