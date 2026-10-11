# ATM-PROF-001 results

**UTC conversion dominates both pipelines. Two uncached midnight `str2et_c` calls, not weather lookup or matrix multiplication, are the largest avoidable-looking repeated work.** This is attribution, not an optimisation result. No scientific/production code changed.

Evidence: `evidence/stage-timings.json`, `analysis.json`, `workload-timings.json`, `locality-statistics.json`, `concurrency-results.json`, `verification.json`; raw data retained separately, gzip-compressed where large. Every table below is derived from these records. Host/compiler/settings: manifest and DECISIONS.

## 1. Complete fixed-input attribution

Five independent batches ×2000 requests,256 warm-ups per batch. Probe:2003-10-30T00:00:00, WGS8445°N/30°E/400km, ITRF93, daily Ap. **Numbers below are same-request instrumented means**, inclusive within each top-level stage. Percentages use the instrumented provider mean, not an independently measured median or the original provider denominator.

| Stage | NRLMSISE µs | JB2008 µs | Provider share NRL / JB |
|---|---:|---:|---:|
| Earth orientation, including wrapper | 2.685 | 2.571 | 8.12% / 6.63% |
| Cartesian matrix-vector transformation | 0.024 | 0.024 | 0.073% / 0.061% |
| Geodetic conversion | 0.651 | 0.646 | 1.97% / 1.67% |
| UTC conversion, including wrapper | 25.406 | 24.544 | 76.77% / 63.28% |
| Weather lookup/preparation | 0.414 | 0.603 | 1.25% / 1.56% |
| Sun ephemeris acquisition | N/A | 2.506 | N/A / 6.46% |
| Native model, including adapter | 3.564 | 7.393 | 10.77% / 19.06% |
| Other wrapper work | 0.346 | 0.502 | 1.05% / 1.29% |
| **Total provider** | **33.091** | **38.788** | **100% / 100%** |

“Other” combines separately measured input validation and geometry construction plus the same-request residual; JB includes its additional Sun matrix multiplication. Residual alone is0.286µs NRL/0.338µs JB,0.864%/0.872%. It includes checkpoint bookkeeping, branch/return work, un-attributed RAII destruction and timer-reset gaps. Summing the exclusive segments plus that residual exactly reconciles each request's inclusive total before rounding. Outer timing adds another0.054µs/0.052µs mean around the inclusive Scope.

Tiny instrumented stages are **timer-floor dominated**. The0.024µs Cartesian row does not mean multiplication costs24ns: the prepared original microbenchmark measures a2.806ns median including dispatch/control. Weather is also substantially inflated by fine-grained instrumentation. Do not transport its instrumented share into a claim about uninstrumented production cost.

### Uninstrumented control distribution

Per-request wall times; min/median/mean/p95/p99/max retained, not trimmed. This is the direct provider, with no inner timing/checkpoint instrumentation.

| Model | Min µs | Median µs | Mean µs | p95 µs | p99 µs | Max µs |
|---|---:|---:|---:|---:|---:|---:|
| NRLMSISE daily Ap | 26.354 | 28.335 | 30.083 | 40.043 | 58.715 | 161.081 |
| NRLMSISE history diagnostic | 28.028 | 29.730 | 31.279 | 40.712 | 58.502 | 174.217 |
| JB2008 | 31.906 | 34.696 | 36.399 | 50.802 | 73.631 | 262.713 |

Historical direct-v1 medians30.308/37.451µs are not a controlled before/after comparison. direct-v2 explicitly inherited those historical timings; this experiment measures the current source, hardware state and workload afresh.

## 2. Significant nested operations

Instrumented means, µs. Parents contain children and timer gaps; **do not add nested rows to the previous table**.

| Nested operation | NRLMSISE µs | JB2008 µs | Calls per density request |
|---|---:|---:|---:|
| Rotation mutex acquisition, uncontended | 0.026 | 0.025 | 1 |
| Rotation SPICE error preparation | 0.755 | 0.705 | 1 reset +1 erract |
| `pxform_c`, including zeroed raw matrix setup | 1.671 | 1.606 | 1 |
| Rotation failed check/matrix extraction | 0.025 | 0.026 | 1 failed check +9 copies |
| UTC mutex acquisition, uncontended | 0.025 | 0.024 | 1 |
| UTC SPICE error preparation | 0.711 | 0.677 | 1 reset +1 erract |
| `et2utc_c` | 3.779 | 3.614 | 1 |
| failed check +`sscanf` UTC parsing | 0.442 | 0.439 | 1 |
| Civil date construction/two midnight formats | 0.292 | 0.309 | 1 date +2 snprintf |
| First midnight `str2et_c` | 10.026 | 9.700 | 1 |
| Second midnight `str2et_c` | 9.681 | 9.337 | 1 |
| UTC day-length calculation/validation | 0.028 | 0.029 | 1 |
| Day serial/day-of-year arithmetic | 0.024 | 0.023 | 1 |
| Seconds-of-day/quasi-MJD/result construction | 0.024 | 0.024 | 1 |
| Sun mutex acquisition, uncontended | N/A | 0.025 | JB1 |
| Sun SPICE error preparation | N/A | 0.707 | JB1 reset +1 erract |
| `spkez_c` | N/A | 1.548 | JB1 |
| Native scalar/domain validation | 0.028 | 0.029 | 1 |
| Native struct/flags/angle wrapping | 0.055 | 0.036 | 1 |
| Native mutex acquisition, uncontended | 0.026 | 0.025 | 1 |
| Raw `gtd7d` / Fortran bridge+JB2008 | 3.247 | 7.101 | 1 |
| Native unlock/result conversion/validation | 0.032 | 0.026 | 1 |

Two midnight conversions consume19.707/19.036µs,59.55%/49.08% of the instrumented provider. Their exact input strings are unchanged throughout one UTC date; neither result is reused in this experiment.

The Fortran ABI bridge and official JB2008 model are measured as one native boundary. No separately measured bridge instruction cost, CSPICE-internal routine cost or native equation-level cost is claimed.

### Independent prepared-input cross-check

Original library; five batches ×100 timed groups,32 calls/group except short operations1024. Input preparation outside timer; all result components relevant to short operations consumed; compiler barriers retained. These are per-operation group averages, so their tails are **group-average tails**, not individual-call p99.

| Independent operation | Median µs | Mean µs | p95 µs | p99 µs |
|---|---:|---:|---:|---:|
| Raw `pxform_c` | 1.242 | 1.279 | 1.564 | 1.792 |
| Raw `et2utc_c` | 3.044 | 3.130 | 3.689 | 4.273 |
| Raw first midnight `str2et_c` | 9.229 | 9.480 | 11.000 | 13.725 |
| Raw second midnight `str2et_c` | 8.930 | 9.065 | 10.359 | 11.573 |
| Raw `spkez_c` | 1.204 | 1.263 | 1.594 | 2.140 |
| SPICE reset+erract+failed control | 0.578 | 0.590 | 0.596 | 0.933 |
| Complete rotation wrapper | 1.908 | 1.928 | 2.260 | 2.590 |
| Complete UTC wrapper | 23.139 | 23.345 | 25.525 | 28.674 |
| Complete Sun wrapper | 1.958 | 2.017 | 2.381 | 3.337 |
| NRL native adapter, daily Ap | 3.031 | 3.088 | 3.585 | 4.029 |
| JB native adapter | 6.776 | 6.813 | 7.466 | 8.495 |
| CSSI daily preparation | 0.029 | 0.030 | 0.029 | 0.031 |
| CSSI history preparation | 0.214 | 0.221 | 0.241 | 0.393 |
| SOLFSMY+DTC preparation | 0.081 | 0.083 | 0.083 | 0.084 |

Independent measurements corroborate the dominant midnight work. They are **not an exact additive decomposition**: separate batch contexts, error preparation, dispatch, cache state and timers differ. Raw primitive regions omit wrapper locks/preparation; wrapper rows include them. Adding independent medians cannot justify a precisely claimed wrapper residual.

JB adds measured Sun acquisition and its weather preparation relative to NRL. Independently prepared weather means are0.083µs JB vs0.030µs NRL; the directly measured JB weather operation remains tiny relative to the2.017µs Sun wrapper and native model. We do not interpret the subtraction of those independent means/medians as a paired exact overhead partition. The same-request JB Sun stage alone is2.506µs/6.46% of provider attribution.

### Geodetic cases and Ap modes

| Prepared400km geodetic case | Iterations | Original solver median µs |
|---|---:|---:|
| Equator | 1 | 0.0477 |
| Midlatitude45° | 6 | 0.3365 |
| Near pole89.999° | 2 | 0.1421 |
| Exact Cartesian pole | 0, shortcut | 0.0070 |

Full distributions and real-workload total iteration counts are retained. The latitude loop dominates the instrumented midlatitude solver (0.285µs mean), compared with0.068µs initialisation,0.042µs altitude projection and0.045µs longitude; all include timer floors. Fixed45° calls consistently use6 iterations. The polar shortcut is not timed as an iterative case.

Daily Ap performs two CSSI searches. History mode adds20 history searches spanning offsets0–19 and two eight-bin means, plus native history validation/physics. Its instrumented weather mean0.645µs and native execution mean4.263µs are separately labelled; daily means0.414µs/3.247µs remain the baseline. History-mode density differs by scientific model configuration, not by profiling; original and diagnostic history results agree exactly.

## 3. Calibration and attribution bias

- Clock-pair control:20000 samples; original min18ns,median28ns,mean24.289ns; diagnostic median27ns. VM timer granularity/latency is visible in the tiny-stage distributions.
- Batched empty compiler sink: median0.472ns/op. Empty std::function microbenchmark:median1.535ns/op. They are different controls, not an interchangeable subtraction.
- Empty all-stage enumeration:five ×10000 iterations,median2.528µs,mean2.518µs. This deliberately exercises **all** stage IDs; a particular model executes only its relevant subset, so this is a perturbation scale, not a universal correction.
- Active original-vs-diagnostic outer medians: NRL28.335→31.382µs,+10.75%; JB34.696→37.535µs,+8.18%. Diagnostic library with recording disabled:28.911/34.688µs. Disabled-code layout/TLS branches and host variability remain measurable context effects.
- No universal timer subtraction. A24ns input-validation or matrix stage is not distinguishable from the control floor as a pure computation cost. Fine weather-stage instrumentation is relatively much more intrusive than timing9–10µs midnight calls.
- Process CPU and wall batch arrays are retained, including every concurrency batch. Process CPU includes all threads; neither wall−CPU nor acquisition duration is interpreted as pure lock waiting. Acquisitions include timers, mutex overhead and possible scheduling delays.
- No `perf` executable was installed during measurement. Prepared original-library microbenchmarks, inactive diagnostic runs and original full-provider measurements are the available cross-checks; there is no fabricated sampling profile.

## 4. Real six-hour production workloads

Actual adaptive Dopri5 requests, not a fixed-input replay. Three repetitions/model/case, scientific settings unchanged. Values below are original-library per-call means and mean captured propagation wall; all individual timing samples/batches and no-capture control are retained.

| Case/model | RHS/provider calls | Provider µs | Total RHS µs | Provider share RHS | Six-hour propagation wall s |
|---|---:|---:|---:|---:|---:|
| A / NRL | 60848 | 32.179 | 35.686 | 90.17% | 2.176 |
| A / JB | 60909 | 39.591 | 42.794 | 92.51% | 2.611 |
| B / NRL | 12409 | 32.845 | 36.235 | 90.64% | 0.451 |
| B / JB | 39251 | 39.276 | 42.376 | 92.68% | 1.666 |
| C / NRL | 84138 | 32.310 | 35.724 | 90.44% | 3.013 |
| C / JB | 121448 | 38.472 | 41.669 | 92.33% | 5.070 |

RHS includes the separately timed external Sun query (mean2.22–2.50µs) before atmosphere, eclipse/source setup and force/torque evaluation. JB additionally acquires Sun inside density: two same-epoch Sun calls per RHS. Native/model-specific scientific changes produce **different adaptive request counts**, especially CASE-B; the larger JB wall time is not solely a per-call overhead difference.

No-capture controls, A/B/C respectively: NRL2.144/0.444/2.944s; JB2.544/1.678/5.042s. These are individual independent controls; negative apparent capture overhead in B/JB is run variability, not negative instrumentation cost. Captured RHS includes stage-record copying, checksum work and timestamp overhead outside the provider interval. Do not label it a perfectly uninstrumented production wall measurement. Instrumented changing-workload stage distributions are available per batch; midnight fractions remain approximately60.0–60.4% NRL/48.8–49.6% JB of diagnostic provider time.

## 5. Threading and synchronisation

All native and CSPICE mutexes retained. Five batches;2000 calls per worker; start barriers follow256 fresh warm-up calls per worker. SharedCPU0–3 affinity, not physical-core isolation. Original throughput shown as median of batch aggregate throughput.

| Original fixed workload | 1-thread calls/s | 2-thread calls/s | 4-thread calls/s |
|---|---:|---:|---:|
| Full NRL provider | 34222 | 16364 | 15206 |
| Full JB provider | 26473 | 14147 | 12185 |
| Native-only NRL | 349322 | 152682 | 155589 |
| Native-only JB | 147975 | 73150 | 80454 |
| Rotation-only SPICE wrapper | 386509 | 200343 | 141902 |
| UTC-only SPICE wrapper | 36972 | 20294 | 20812 |
| Sun-only SPICE wrapper | 407695 | 173372 | 161989 |
| External Sun +NRL provider, RHS-environment control | 27390 | 12971 | 12228 |
| External Sun +JB provider, RHS-environment control | 23596 | 12511 | 11314 |

Changing epochs also fail to scale: NRL33008/15950/15117calls/s; JB26874/14111/12465. This is a concurrency limitation, not a single-thread computational bottleneck estimate. Both independent native locks serialize their own model; all SPICE wrappers share one mutex. Moving to two or four callers on this host worsens throughput instead of hiding preparation work.

Instrumented fixed four-worker acquisition **means**, NRL:rotation52.642µs,UTC156.285µs,native0.074µs; JB:rotation197.940µs,UTC34.392µs,Sun0.770µs,native0.041µs. Uncontended acquisition medians are about27–28ns (timer floor included). Large wait assignment varies with arrival order; the same process-wide SPICE lock may be waited for at rotation or UTC, so do not infer a separate independent lock from each row. p95/p99/max retained; NRL four-thread provider p99 is2.484ms original, JB0.946ms. Rare starvation/scheduling tails can make means exceed p99 in isolated UTC tests; no slow sample was removed.

The native lock is only around `gtd7d`/Fortran bridge+model; weather, input construction and conversion largely lie outside it. UTC's SPICE lock remains held across text parsing, formatting, both midnight conversions and date arithmetic. Contention is predominantly at that shared subsystem in full providers; native-only controls prove native serialization is a separate limitation. The RHS-environment controls add the external Sun call under the same SPICE mutex and demonstrate additional concurrent traffic from outside density; they are not full concurrent six-hour propagations. Instrumented lock timers extend critical sections, so original throughput is the authoritative concurrency control.

## 6. Scientific verification and retained limitations

- Original native vs original provider fixed outputs agree exactly; diagnostic outputs agree exactly for daily NRL, history NRL and JB, including temperatures/constituents.
- All10710 fixture samples over five frozen epochs agree exactly between original, diagnostic and direct-v2, not merely within tolerance.
- Every full CASE-A/B/C scientific output series agrees exactly with direct-v2; all six original/diagnostic profiled repetitions per case/model match the baseline trajectory checksum.
- All60 controlled-workload batch result hashes match original vs diagnostic; all21 isolated concurrency workload/worker-count output-hash arrays match.
- Existing regression suite:166/166 passed, real SPICE kernels, CSSI, official NRL documentation and JB validation data supplied. Direct native-reference invocations additionally prove the three fixture checks executed assertions rather than skipping:563/39964/12 assertions, all passed (`reference-checks.json`).
- Original production sources, direct-v1/v2 and validation-amendment evidence preserved by SHA256 comparison. Historical timing and numerical-audit evidence never written.
- A post-run raw-compression naming defect in the diagnostic reducer was corrected: gzip bytes had been stored under a `.jsonl` suffix and subsequently wrapped again. Normalisation changed compression wrappers only; uncompressed request SHA256 records are retained in `compression-repair.json`. Reanalysis exercises the corrected gzip-reader/writer path.
- Final complete reproduction ran successfully into a new directory with the corrected reducer and isolated controls; its manifest and scientific verification are retained. Original fixed provider medians were28.288µsNRL/34.446µsJB, compared with published28.335/34.696µs; this confirms modest run variability, not a controlled speedup.

Native reference residuals remain the existing baseline limitations, not instrumentation differences:

| Independent reference check | Fixture count | Observed residual |
|---|---:|---|
| NRL published rounded table | 17 | Maximum density relative error2.944e−7; species3.866e−7; temperature4.803e−7 |
| Official JB SET validation grid | 13320 | Maximum density relative error0.005861 (0.5861%); RMS0.003240; maximum exospheric/local-temperature error0.7502/0.7461K |
| JB twelve full-precision independent densities | 12 | Maximum density relative error1.472e−5 |

All pass existing reference-test bounds. The official JB grid's residual/cause and provisional altitude datum remain documented direct-v2 scientific limitations; this profiling task neither resolves nor suppresses them. Original-versus-diagnostic and current-versus-direct-v2 physical fixture/trajectory differences are exactly zero.

Timing limitations remain: five batches on one WSL2 host, no governor/frequency control, unknown physical-core scheduling, fixed-order original/diagnostic runs, timer-floor small stages, additional capture work in RHS/wall measurements, group-average microbenchmark tails and opt-in diagnostic critical-section inflation. Results identify the pipeline bottleneck; they do not establish a portable latency guarantee or measured future cache speedup.
