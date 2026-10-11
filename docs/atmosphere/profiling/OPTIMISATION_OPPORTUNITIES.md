# Opportunities — analysis only

**Next experiment: exact reuse of the UTC midnight pair and civil-day metadata, with unchanged per-request `et2utc`/seconds-of-day/MJD semantics.** No optimisation is implemented here. A lookup, memory policy, invalidation strategy and synchronisation cost have not been measured; no net cache speedup is predicted.

## Dependency and safety classes

| Operation | Class | Exact inputs/dependencies | What must remain computed |
|---|---|---|---|
| Two midnight `str2et_c` results | A: exact reuse | Current UTC civil date; unchanged leap-second/time configuration/kernel state; identical midnight strings | Per-request conversion identifying civil date/time; error behaviour and kernel lifecycle must remain correct |
| UTC serial date/day-of-year/rounded day length | A | Same civil date,valid calendar and midnight pair | UTC seconds and quasi-MJD depend on exact request time |
| CSSI current/previous immutable records | A | Dataset identity and day/current−1 keys; observed status retained | Model input UTC seconds/LST/geometry; validation contracts |
| Three-hourly Ap history | A | Dataset,day/bin and full offsets0–19; same mode | UTC/native time and geometry still vary; daily-Ap baseline must not change |
| SOLFSMY lagged record fields | A | Dataset/day−1/day−2/day−5,source-code acceptance | MJD,geometry,DTC fractional-hour interpolation/rounding |
| DTC hourly endpoints | A | Dataset,current day/hour,next day whereh23; leap-day length | Fractional interpolation and integer rounding for each query |
| Earth orientation at an identical ET | A | Exact ET bits,ITRF93/frame direction,relevant kernel state | Position-dependent matrix-vector product/geodetic conversion |
| Sun state at identical complete query | A | Exact ET,target10,observer399,J2000,NONE,kernel state | Position-dependent geometry; no interpolation |
| JB's already acquired same-RHS Sun state | A | Same complete query and kernel state as its external SRP/eclipse call | Provider still needs fixed Sun transformation and angles |
| Orientation/Sun at different nearby ET | B: approximation | Distinct exact time inputs | Requires independent accuracy/error study; near ET does not preserve exact result |
| Density over nearby position/time; rounded query keys | B | Different complete geometry,time,weather/config inputs | Requires scientific error control; not automatically safe |
| Formatting/parsing/temporary-wrapper changes | C: direct computation | Same UTC/error/format semantics; no floating-output change | Need output/error/thread-safety verification; current overhead measured, proposed net benefit not measured |
| Weather tree-search improvements | C | Same immutable records,status/source checks,lag/bin semantics | Preserve errors and all native fields; low current cost |
| SPICE error-preparation changes | C,semantic risk | Global CSPICE error state and RETURN policy | Removing reset/erract may change error isolation; do not remove mutexes or assume safety |
| Native model/required geometry on novel inputs | D: required direct evaluation | Complete native time/position/weather/config inputs | Current physical architecture needs fresh empirical-model evaluation |

Exactly repeated whole requests occur in6.37–14.01% of realistic inputs and preserve complete inputs under unchanged configuration. This is an observed input-identity statistic, **not** a simulated density-cache hit rate or a proposal to introduce atmospheric caching in this task. Native physics remains required for nonidentical inputs.

## Ranked candidates

Ranking uses measured cost, observed exact-key locality, numerical risk and interface complexity, not implementation familiarity.

| Rank | Candidate | Measured evidence | Benefit ceiling / priority | Numerical risk | Complexity | Models |
|---|---|---|---|---|---|---|
| 1 | Exact UTC midnight/day-metadata reuse | Two conversions19.707µs NRL/19.036µs JB instrumented means; independent raw9.480+9.065µs means; only1UTC date across each six-hour run | Largest measured exact-reuse candidate; fixed provider removal ceilings2.47×/1.96× | Low only with exact date/config invalidation and leap/error semantics; not a generic ET/86400 key | Moderate: civil-day identity,coverage,kernel lifecycle,thread ownership | Both |
| 2 | Share already acquired same-RHS JB Sun state | Provider Sun2.506µs/6.46%; two identical queries per JB RHS;121818/78502/242896 total queries A/B/C | Full inner-Sun removal ceiling1.069× fixed JB provider; no need to approximate any epoch | Low scientific risk if complete query identity is preserved | Moderate API/source-snapshot contract change | JB-specific; external Sun also exists in NRL RHS |
| 3 | Remove redundant UTC text formatting/parsing work without changing outputs | `sscanf`0.442/0.439µs; date/two formats0.292/0.309µs instrumented | Combined removal ceiling1.023×NRL/1.020×JB; part can overlap candidate1,so bounds not additive | Medium: UTC representation,leap-second and round-trip behaviour | Moderate | Both |
| 4 | Exact epoch orientation reuse | Orientation2.685/2.571µs;22.55–28.34% repeated epochs in real runs;87.5% ensemble repeats | Whole-stage removal ceiling1.088×/1.071×; realistic global-repeat cost opportunity much smaller than whole-stage ceiling | Low for identical ET/frame/kernel keys; different epochs require approximation | Moderate: lifetime/key/synchronisation; ordering-sensitive | Both |
| 5 | CSPICE wrapper/error-preparation work | Rotation+UTC preparation1.466µsNRL; plusSun gives2.090µsJB; prepared error control0.590µs | Complete preparation-removal ceiling1.046×/1.057×; not a safe removal proposal | High error-state/threading semantic risk relative to small benefit | Moderate/high | Both |
| 6 | Immutable weather record retrieval | Original prepared daily0.030µs,history0.221µs,JB0.083µs means; daily key never changes in real run | Low: even total instrumented weather removal ceilings1.013×/1.016× overstate fine-timer-sensitive costs | Low only with full lag/status/provenance/bin semantics | Low/moderate | Both,model-specific keys |
| 7 | Geodetic computational changes | Original midlatitude median0.337µs; full instrumented stage0.651/0.646µs | Whole-stage removal ceilings1.020×/1.017×; geometry cannot generally be removed | Medium/high if changing convergence/math; exact reuse needs identical transformed position | Moderate | Both |
| Not ranked for implementation | Native equation optimisation / removing native locks | Raw instrumented model3.247/7.101µs; isolated native throughput worsens under contention | Required science and serialization; replacing/removing protections is outside scope | High scientific/thread-safety risk | High | Different native implementations |

These are **instrumented attribution-based upper bounds**, not production speedups. Fine timing inflates very short stages, particularly weather, so these rankings are cross-checked with prepared original-library measurements. The small weather/transform results are not credible major overhead targets on this evidence.

## Amdahl bounds and six-hour wall time

For measured fraction `f`, eliminating the operation at zero replacement cost gives `S≤1/(1−f)`. Finite acceleration `s` gives `S=1/((1−f)+f/s)`. Multiple overlapping parent/child candidates cannot have their fractions added. Exact-key reuse also requires the remaining first evaluation plus lookup/storage/synchronisation; none is free by assumption in a realistic expected-benefit claim.

Fixed midnight-only fractions:59.55% NRL/49.08% JB →2.472×/1.964× ceilings. Removing **all** UTC conversion would mathematically give4.306×/2.723×, but this is not a safe/exact-reuse candidate: seconds-of-day/date identification/MJD still require per-request work.

Changing-workload estimates below use the measured diagnostic midnight fraction **multiplied by the original provider's summed per-request wall cost**, divided by original captured propagation wall. This transports a fraction between two measured contexts; it is an estimate, not an exact uninstrumented decomposition. Both raw contexts and formulas are in `evidence/analysis.json`. Correct exact reuse would preserve the same trajectories and call counts, as the profiling controls demonstrate for unchanged calculations.

| Six-hour case/model | Original captured wall s | Estimated zero-cost midnight saving ceiling s | Estimated propagation speedup ceiling |
|---|---:|---:|---:|
| A / NRL | 2.176 | 1.179 | 2.181× |
| A / JB | 2.611 | 1.176 | 1.819× |
| B / NRL | 0.451 | 0.246 | 2.202× |
| B / JB | 1.666 | 0.752 | 1.822× |
| C / NRL | 3.013 | 1.632 | 2.181× |
| C / JB | 5.070 | 2.317 | 1.841× |

One required midnight pair per day makes a tiny correction in these single-day workloads: retained pair frequency1/N, withN12409–121448. More importantly, any future lookup/invalidation/locking consumes part of the ceiling. The estimates do not account for future changes in concurrent scheduling,cache layout or synchronization. They are not measured optimized wall times. The no-capture controls differ from captured wall by run variability and capture overhead; both are retained rather than silently choosing the fastest.

Exact repeated orientation epochs could eliminate only the repeated fraction of that stage if retained indefinitely, not the whole stage. Even before lookup cost, multiplying6.63–8.12% fixed orientation share by22.55–28.34% real epoch repetition gives only about1.5–2.3% provider-cost opportunity as a cross-context estimate. Ensemble87.5% repetition is a different workload; schedule and memory lifetime determine achievable reuse. No cache capacity/replacement simulation is performed.

Whole native-adapter removal ceilings1.121×NRL/1.235×JB are mathematical comparisons only. They require changing the physical evaluation architecture; they do not justify removing calls or thread-safety guards. Density proximity,orientation interpolation and ephemeris interpolation are not classified as exact.

## Highest-value next experiment

Investigate exact civil-day metadata reuse first, in a **separate authorised optimisation task**:

1. Establish a date/kernel-configuration lifetime contract; preserve per-request ET→UTC text/date/seconds and quasi-MJD calculations. Use exact civil date,never rounded ET or ET/86400.
2. Compare midnight pair/day-length/doy outputs bit-for-bit at ordinary days,UTC midnight,year transitions and positive leap-second days. Preserve kernel reconfiguration,error behaviour and concurrent isolation.
3. Measure lookup/invalidation/storage/synchronisation separately,then original vs candidate providers under these same fixed/changing/CASE/ensemble/thread workloads.
4. Compare full fixture grids,all trajectory/scientific series and regression/reference results; report measured net provider/propagation speedup,not this zero-cost ceiling.

Priority is evidence-based: the expensive input strings are constant for all tens of thousands of same-day requests, the dominant costs are corroborated by raw CSPICE microbenchmarks, and no positional or epoch interpolation is required. This task implements none of those steps as an optimisation.
