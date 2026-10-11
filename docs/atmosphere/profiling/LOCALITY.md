# Input locality: exact reuse versus proximity

Every density was freshly evaluated. This document counts recorded inputs offline; it neither implements caching nor simulates cache hits. Full rep0 sequences and all batch timings are retained. Exact request keys are the IEEE754 bits of `(ET,x,y,z)`; epoch-only keys use ET bits. +0 and −0 are distinct keys. UTC weather keys come from real `utc_calendar`, acquired outside measurement loops, not ET/86400.

## Actual CASE-A/B/C requests

Actual production Dopri5 sequences, daily-Ap NRL and unchanged JB. Each model's own coupled trajectory generates its own requests; model request counts are not assumed equal.

| Case/model | Requests | Distinct ET epochs | Repeated-epoch fraction | Exact repeated-request fraction |
|---|---:|---:|---:|---:|
| A / NRL | 60848 | 43611 | 28.328% | 13.994% |
| A / JB | 60909 | 43646 | 28.342% | 14.011% |
| B / NRL | 12409 | 8944 | 27.923% | 13.394% |
| B / JB | 39251 | 30399 | 22.552% | 6.369% |
| C / NRL | 84138 | 62532 | 25.679% | 10.745% |
| C / JB | 121448 | 92657 | 23.706% | 8.388% |

Fractions count repeats after the first occurrence over the **entire** sequence, not just adjacent pairs and not bounded-memory cache hits. They therefore do not predict a particular cache policy's performance. Exact request repetition can preserve a whole density input/output, whereas repeated epoch with differing position preserves time-only products but does not preserve density or geodetic geometry.

All six runs use one UTC calendar day:60848/60909/12409/39251/84138/121448 calls on that day respectively. Thus each run executes its two midnight conversions **2×that request count** although there is only one input date pair. Daily CSSI previous/current record identity and SOLFSMY day−1/day−2/day−5 identities never change. Each sequence crosses one three-hour boundary (two distinct Ap-bin keys) and five hourly boundaries (six DTC-hour keys). Daily record changes=0; three-hour key change fraction0.00000823–0.00008059; hourly key change fraction0.0000412–0.000403. These are record identities, not proof that full weather-model inputs are unchanged: UTC seconds, MJD, local solar time and JB fractional DTC interpolation still vary.

`calls_per_requested_epoch` retains each exact epoch bit key and its multiplicity; `epoch_multiplicity_distribution` retains the complete distribution. `calls_per_utc_day` retains per-day counts, not just a mean. These detailed arrays live in the per-variant locality evidence; top-level JSON is a navigational summary.

### Consecutive changing inputs

Signed deltas matter: adaptive integration can revisit earlier ET after trial/rejection. Negative differences must not be mistaken for a monotonic trajectory timeline.

| Case/model | Median consecutive Δt s | Minimum Δt s | p99 Δt s | Median position change m | p99 position change m |
|---|---:|---:|---:|---:|---:|
| A / NRL | 0.2559 | −1.7442 | 1.5751 | 1912.8 | 11832.8 |
| A / JB | 0.2557 | −1.7465 | 1.5746 | 1911.7 | 11833.6 |
| B / NRL | 1.3333 | −14.1167 | 7.5000 | 9930.3 | 55933.6 |
| B / JB | 0.2180 | −14.2631 | 7.5000 | 2765.1 | 70798.8 |
| C / NRL | 0.1751 | −7.0632 | 1.9059 | 1514.6 | 17015.8 |
| C / JB | 0.0348 | −5.5528 | 1.8450 | 472.2 | 16814.5 |

Minimum position delta is0 in every sequence. All raw differences, min/median/mean/p95/p99/max and full histograms are retained. “Nearby RHS calls” are frequently separated by hundreds of metres or kilometres even when epochs are subsecond apart. Nearness alone is not exact reuse and does not bound a density-interpolation error.

### Sun queries inside and outside density

NRL density itself never acquires Sun; the full RHS acquires it once for SRP/eclipse. JB RHS acquires Sun once outside density and again inside density at exactly the same epoch, target10,observer399,J2000,NONE,with unchanged kernel state.

Across full RHS Sun-query streams, repeated exact query fractions are28.328%/27.923%/25.679% for NRL A/B/C. JB counts both calls per RHS:121818/78502/242896 queries and repeated fractions64.171%/61.276%/61.853%. Half of JB queries are already same-request duplicates, before accounting for integration-stage epoch repeats. This is measured call/query identity, not a cache-hit rate under any proposed implementation.

## Controlled schedules

Both models,three batches,1024 requests per schedule,256 warm-ups. Original/diagnostic result hashes match in every batch. Inputs use real WGS84 geometry and real SPICE orientation outside provider timers. Same-input scientific computations are never skipped.

| Schedule | Distinct epochs | Exact epoch repeat fraction | Exact request repeat fraction | Distinct daily /3h /hour keys |
|---|---:|---:|---:|---:|
| Fixed epoch and position | 1 | 99.902% | 99.902% | 1 /1 /1 |
| Same epoch, changing grid positions | 1 | 99.902% | 0% | 1 /1 /1 |
| Same J2000 position,10s epoch steps | 1024 | 0% | 0% | 1 /1 /3 |
| Nearby epoch/position,1s steps | 1024 | 0% | 0% | 1 /1 /1 |
| Identical daily records,changing geometry+epoch | 1024 | 0% | 0% | 1 /1 /1 |
| Three-hour-boundary sweeps | 1024 | 0% | 0% | 8 /16 /16 |
| Hour-boundary sweeps | 1024 | 0% | 0% | 8 /8 /16 |
| UTC-midnight sweeps | 1024 | 0% | 0% | 9 /16 /32 |
| Eight trajectories,trajectory-major | 128 | 87.5% | 0% | 1 /1 /1 |
| Eight trajectories,epoch-major | 128 | 87.5% | 0% | 1 /1 /1 |

Boundary sweeps traverse eight date-separated windows, so daily changes between windows are expected even for the hour/3h experiments. The actual calendar metadata validates16 distinct three-hour bins for3h sweeps,16 distinct hours for hour sweeps and9 dates for midnight sweeps. No leap-second day was encountered; the experiment **does not claim measured leap-second reuse behaviour**. Existing UTC/leap-second regression tests remain part of scientific verification.

Same J2000 position is not constant Earth-fixed/geodetic geometry at changing epochs: rotation still changes. Identical daily records do not imply identical Sun geometry, MJD, seconds-of-day or interpolated DTC. Changing Earth orientation or ephemeris at distinct ET requires new evaluation or an explicitly error-controlled approximation; neither is performed here.

## Single trajectory versus ensemble

The two ensemble schedules have the **same request set**,eight explicitly identified trajectories ×128 epochs,128 requests per trajectory. Each trajectory individually has128 distinct epochs,zero repeated requests and zero repeated epochs. Combined ensemble has128 distinct epochs for1024 requests,87.5% exact epoch repeats but0% exact density-request repeats because trajectories have different positions.

Epoch-major order puts seven equal-epoch adjacent pairs in each group of eight:896/1023 adjacent pairs,87.5855%. Trajectory-major order has0 equal-epoch adjacent pairs but the same global87.5% repeated-epoch fraction. Therefore exact time-product reuse across ensembles depends strongly on scheduling and memory lifetime, while the global input identity alone does not identify a bounded cache's hit rate. Explicit trajectory IDs, not proximity clustering, determine membership.

## Exact reuse conditions

- **UTC civil-day metadata:** same civil date and unchanged LSK/time-conversion configuration; midnight ET pair, rounded day length, serial date and day-of-year are exact products. `et2utc` timestamp text, seconds-of-day and quasi-MJD are not constant through the day.
- **Immutable weather records:** same dataset identity and required day/bin keys. CSSI daily/lagged SOLFSMY record fields are exact; history mode needs its full20-bin Ap history; DTC must retain hourly indices and fractional interpolation/rounding.
- **Orientation:** identical ET bit pattern,frame direction/name and unchanged relevant kernel state; position may differ. Different ET is not exact orientation reuse.
- **Sun ephemeris:** identical target,observer,frame,aberration mode,ET and kernel state; independent of spacecraft position. Reusing the already acquired same-RHS JB Sun state is exact under these conditions.
- **Geometry/native density:** identical complete input geometry/time/weather/configuration is exact; nearby coordinates or rounded epochs are not. Even bitwise identical ET alone does not preserve full native input.

No numerical safety inference is drawn from time/position proximity alone. See OPTIMISATION_OPPORTUNITIES for measured ceilings and the next controlled experiment.
