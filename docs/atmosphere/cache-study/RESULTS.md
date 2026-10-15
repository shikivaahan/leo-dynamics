# Results, and the production recommendation

## Bottom line

**Do not implement a production atmospheric density cache, for either model.** This time the
measurements were made, and they are negative on both axes at once:

- **Accuracy.** No approximate configuration met the 0.1 % or 0.01 % density budget. The best
  measured approximate reconstruction was 0.2966 % worst case.
- **Performance.** Every cache that met the 1 % screening threshold was *slower* than the current
  fully optimised direct provider on real trajectory work, between 0.53× and 1.12×.

The two findings are not independent. A cache can only pay for its key lookup if it reconstructs
rather than recomputes, and reconstructing accurately requires cells fine enough that the
interpolation cost exceeds the native call it replaces. That trade-off is measured here, not
assumed.

The study ran to completion on a separately pinned experimental input set: 550 tasks, 0 failures,
0 blocked.

## Measured density sensitivity

Both models, five archived weather regimes, real records, no synthesised indices:

| Model | Largest altitude ratio span (100 km step) | Largest time ratio span | Largest one-sided asymmetry (log) |
|---|---:|---:|---:|
| NRLMSISE-00 | 45.98 – 604.7 | 0.95 – 2.73 | 0.53 – 1.32 |
| JB2008 | 36.58 – 611.3 | 2.21 – 5.86 | 1.00 – 1.74 |

Density changes by more than two orders of magnitude over 100 km of altitude. That is the single
measurement that decides the design: any cache that approximates density across altitude must use
kilometre-scale cells or finer, and the finer it makes them, the more native node evaluations it must
build to answer a query.

The one-sided asymmetry column is the asymmetry across discrete space-weather transitions, measured
at real archived boundaries rather than assumed symmetric.

## Reconstruction accuracy

Screening threshold 1 %, judged on the **worst** error over four independent holdout packets and
every weather epoch. Reported budgets are met only on that worst value.

| Candidate | JB2008 worst | NRLMSISE-00 worst | Budgets met |
|---|---:|---:|---|
| A — exact memoisation | 0.0000 % | 0.0000 % | all five |
| E — preprocessing control | 0.0000 % | 0.0000 % | all five |
| C — log-density interpolation | 0.8862 % @ 0.025 km | 0.2966 % @ 10 km | 1 %, 5 % |
| B — nearest-neighbour cell | 0.6206 % @ 0.25 km | 0.4728 % @ 0.25 km | 0.5–1 %, 5 % |
| D — adaptive refinement | never met 1 % | never met 1 % | — |

Coarse-to-fine behaviour, worst case over epochs:

| Cell (km, °, s) | B / JB2008 | B / NRL | C / JB2008 | C / NRL |
|---:|---:|---:|---:|---:|
| 25, 5, 60 | 116.2 % | 115.1 % | 9.99 % | 1.65 % |
| 10, 2, 10 | 37.0 % | 36.6 % | 9.84 % | 0.308 % |
| 0.25, 0.25, 1 | 0.62 % | 0.47 % | — | — |
| 0.025, 0.025, 0.05 | — | — | 0.886 % | — |

Candidate B converges linearly in cell width; candidate C converges much faster, which is why only C
could reach the 1 % gate at a usable resolution. Neither reaches 0.1 % anywhere on the declared
seven-rung ladder.

## Coupled propagation

Worst case over CASE-A/B/C, 6 h and 24 h windows, and small/moderate/large initial-condition
perturbations, both models:

| Candidate | Worst position error | Worst velocity error | Worst density error |
|---|---:|---:|---:|
| A — exact memoisation | 0 m | 0 m/s | 0 (bitwise) |
| E — control | 0 m | 0 m/s | 0 (bitwise) |
| B — nearest-neighbour | 948 m (JB) / 926 m (NRL) | 1.10 m/s | 0.183 % |
| C — log-density interpolation | 439 m (JB) / 2174 m (NRL) | 2.52 m/s | 0.228 % |

Exact memoisation reproduces the reference trajectory bit for bit, as its contract requires. The
approximate candidates accumulate hundreds of metres to a few kilometres over six hours.

## End-to-end performance

Median speedup against the authoritative provider measured on the same route, the same ensemble and
the same thread count in this same run. Never a subtraction of independently measured medians.

| Workload | Model | A exact | B cell | C interp | E control |
|---|---|---:|---:|---:|---:|
| Single trajectory | JB2008 | 0.674× | **1.116×** | 0.071× | 0.801× |
| Single trajectory | NRLMSISE-00 | 0.629× | 0.897× | 0.861× | 0.730× |
| Sequential ensemble (shared) | JB2008 | 0.700× | 1.157× | 0.070× | 0.788× |
| Sequential ensemble (shared) | NRLMSISE-00 | 0.627× | 0.917× | 0.898× | 0.724× |
| Threaded ensemble | JB2008 | 0.588× | 0.728× | 0.065× | 0.705× |
| Threaded ensemble | NRLMSISE-00 | 0.552× | 0.696× | 0.533× | 0.677× |
| Fixed-state RHS | JB2008 | **2.091×** | 1.260× | 1.226× | 0.760× |
| Fixed-state RHS | NRLMSISE-00 | **1.587×** | 0.952× | 0.926× | 0.716× |

Two results deserve emphasis.

**Exact memoisation wins only where states repeat.** It is 1.59–2.09× on the fixed-state RHS, which
evaluates the identical state over and over, and 0.55–0.67× on every real trajectory. An adaptive
integrator almost never re-queries an identical state, so the hit rate that would justify the lookup
cost does not arise in propagation.

**Fine 4-D interpolation is catastrophic.** JB2008 candidate C collapses to 0.065–0.071× — a 14×
slowdown. A four-dimensional interpolation must build up to 2⁴ fresh native nodes per query, and at
0.025 km cells that dominates everything. The apparently attractive "fine grid" is the worst
configuration measured.

Candidate B is the only configuration that ever exceeds break-even on a real trajectory (JB2008 warm
single, 1.116×). It does so at 0.62 % density error — more than six times the 0.1 % budget.

## Answering the eight questions

1. **How does density vary?** By >100× over 100 km of altitude; by 2–6× over hours in the diurnal
   cycle; asymmetrically across discrete weather transitions (log asymmetry up to 1.74).
2. **Which method reproduces the models best?** Only exact reproduction. Interpolating log density is
   the most accurate approximation, by a factor of ~200 over nearest-neighbour reuse.
3. **What resolution for which budget?** 0.25 km cells meet 1 % for nearest-neighbour reuse;
   10 km meets 0.3 % for altitude-only interpolation. **No tested resolution reaches 0.1 % or 0.01 %.**
4. **How does density error affect propagation?** A 0.5 % density error accumulates to ~1 km position
   error over six hours; 0.2 % to 0.4–2.2 km depending on case and model.
5. **Which candidates give end-to-end improvement?** None on trajectory work. Exact memoisation gives
   1.59–2.09× only on repeated-state evaluation.
6. **Cache ownership and reuse?** One process's own cache is the only reusable unit; cross-trajectory
   reuse requires a shared cache within a process, and process isolation removes interprocess reuse
   entirely.
7. **Where does interpolation fail?** Across any discrete space-weather change, at integer DSTDTC
   transitions inside an hour, at UTC midnight and at the leap second. All are guarded by the
   weather-identity check; none is silently crossed.
8. **Should a production cache be built, and with what API?** No. The measurements do not justify one
   for either model, and no API should be added.

## Why this is not a close call

A reader may reasonably object that 0.3 % density error is small, and that the propagation errors are
metres. Both are true, and neither rescues the cache:

- The speedup is measured, not inferred. It is **negative on every trajectory workload**, by 3–45 %
  for the configurations that passed screening.
- The configurations that would meet 0.1 % are exactly the ones whose reconstruction cost exceeds
  the native evaluation they replace.
- The one configuration that is both accurate (bitwise) and sometimes faster is only fast when the
  caller repeats states, which the production integrator does not do.

## Limitations of these results

- **Evidence class is `scientific-experimental-pinned`, not `direct-v2`.** The historical SOLFSMY
  pin is unavailable. The recovered Earth PCK is byte-identical to its historical pin. A five-epoch
  comparison over model, UTC epoch, latitude, longitude and altitude returned zero differing bits in
  3,570 densities, 7,140 temperatures and 28,560 constituents. That bounds equivalence over the
  *tested fixture domain* only; it is not a proof of byte equivalence between SOLFSMY releases.
- **One epoch for coupled work.** Propagation and performance use 2003-10-29T12:00. Sensitivity uses
  five archived regimes.
- **Threaded ensembles are measured at one worker count per ensemble size.** The thread-scaling
  behaviour of a cache is characterised only at the point where a cache is already losing.
- **Density-only seam.** These results apply to a cache serving `density_kg_m3` only. Callers needing
  temperature or constituents cannot use one at all; that limitation is unchanged by this study.

## Harness corrections made during this study

These were defects in the experimental tooling, found by running it and fixed before the reported
measurements. Each is a separate commit.

| Defect | Effect | Commit |
|---|---|---|
| Provider cost components divided a single timed operation by the batch count | understated baseline latency ~1000× | `581cfd4` |
| `--study-points` multiplied the locality packet epoch spread and along-track offset | sample count decided whether a run succeeded; auxiliary probe left the models' domain at 16384 points | `f3d8230` |
| Trace artifacts matched by prefix | an unrelated file aborted a resume | `581cfd4` |
| Budget verdicts unioned across holdouts and epochs | a configuration could claim a budget another packet met | `0f8ecd4` |
| Quaternion perturbations built with `(x,y,z,w)` | wrong rotation about a wrong axis | `0f8ecd4` |
| Benchmark results emitted prose through a raw JSON channel | every benchmark result was unparseable | `0f8ecd4` |

The native-domain finding that prompted the coverage gate turned out **not** to be a real
scenario/model limitation: re-measured at the study's own tolerances, all six model/case pairs
complete the six-hour window. The gate remains in place so the claim is measured rather than assumed.