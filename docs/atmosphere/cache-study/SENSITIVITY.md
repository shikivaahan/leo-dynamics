# Measured density sensitivity

All numbers here come from the pinned-input native slice
(`evidence/native-sensitivity-summary.json`, raw results in the manifest's external artefact
directory). NRLMSISE-00 is queried through the production adapter at explicit Earth-fixed geodetic
coordinates and absolute UTC instants, using the pinned CSSI archive and NAIF0012 leapsecond kernel.
No provider, Earth-orientation, trajectory or end-to-end quantity is measured, so none of these
numbers is a density-cache result and none is a `direct-v2` baseline.

## Weather regimes

Every regime is a real archive record; no index was injected, scaled or edited. Ap-history mode reads
the same records with the seven 3-hourly values.

| Regime | UTC | F10.7 (prev day) | F10.7 (81-day) | Daily Ap | Ap-history vector |
|---|---|---:|---:|---:|---|
| quiet_june | 2008-07-01 | 66.7 | 66.1 | 4 | 4, 7, 2, 5, 5, 6.125, 5.875 |
| quiet_december | 2008-12-15 | 68.8 | 69.2 | 3 | 3, 3, 0, 0, 0, 0.875, 2.75 |
| moderate_march | 2001-03-20 | 147 | 168.3 | 74 | 74, 39, 56, 111, 56, 11.25, 8.25 |
| active_october | 2003-10-28 | 257.2 | 140.5 | 25 | 25, 15, 4, 5, 9, 14.5, 9 |
| storm_october | 2003-10-30 | 279.1 | 140.1 | 191 | 191, 300, 300, 300, 179, 115.5, 18.125 |

## Altitude dominates

Finite differences at 0.001, 0.01, 0.1, 1, 5, 10, 25, 50 and 100 km over 200–1000 km, at five latitudes
(−80, −45, 0, 45, 80°) and seven local solar times, for both Ap configurations: **37 240 samples per
configuration**.

| Quantity | Daily Ap | Ap-history |
|---|---:|---:|
| max \|∂ ln ρ / ∂h\| per km | 0.2108 | 0.2597 |
| max \|∂² ln ρ / ∂h²\| per km² | 213.5 | 230.3 |

The heat maps (`evidence/sensitivity-daily-ap.svg`, `sensitivity-ap-history.svg`) show the gradient
rising monotonically with altitude and peaking near local noon; the steepest column is noon, the
flattest is around dawn.

A tenfold difference in gradient between the two Ap configurations is small compared with the
fivefold spread across local solar time, so the diurnal term dominates the geomagnetic term for
interpolation cell sizing.

## Resolution: what one altitude axis costs

Independent holdouts interpolate ln ρ between fresh native endpoints at cell corners; queried
fractions are never construction points. Worst-case absolute relative error over 240 000 (1 km) to
2 400 (100 km) samples, all regimes and all latitudes:

| Cell | Daily Ap max | Ap-history max | 0.1 % | 0.5 % | 1 % | 5 % |
|---:|---:|---:|:--:|:--:|:--:|:--:|
| 1 km | 1.71e-04 | 2.34e-04 | pass | pass | pass | pass |
| 5 km | 7.06e-04 | 7.13e-04 | pass | pass | pass | pass |
| 10 km | 2.59e-03 | 2.62e-03 | fail | pass | pass | pass |
| 25 km | 1.52e-02 | 1.54e-02 | fail | fail | fail | pass |
| 50 km | 5.29e-02 | 5.29e-02 | fail | fail | fail | fail |
| 100 km | 1.74e-01 | 1.75e-01 | fail | fail | fail | fail |

At 25 km the worst case per regime is remarkably uniform (0.0135–0.0153): the budget is spent on
altitude geometry, not on the space-weather regime. Worst points cluster at 200–250 km and ±45°
latitude, where ln ρ curvature is greatest. No tested width meets the 0.01 % budget on altitude
alone.

## Time is the easy axis

Separations from 1 ms to 6 h, forwards and (up to 600 s) backwards, at four local solar times:
**608 samples with an unchanged weather vector per configuration**. The largest smooth-time gradient
is 8.96e-04 per second in daily-Ap mode (8.9e-04 is 0.09 % per 100 s), so a one-minute cell on this
axis costs far less than a 10 km altitude cell. Samples where the weather vector changes are counted
separately and excluded from that figure.

## Discrete boundaries are real discontinuities

Forty transitions were probed across 3-hourly Ap bins, hourly DTC records, UTC midnight and the daily
record change; all forty were covered by the pinned records. Density jumps across them are reported
in the raw results and are genuine model discontinuities, not interpolation error: no candidate may
reuse or interpolate across one, and every key carries the complete vector for that reason.

At the 2016 leap second, `DSTDTC`-free NRLMSISE-00 still steps: density changes by −2.63e-05 across
`23:59:59 → 23:59:60` and by −1.13e-03 across midnight into 2017-01-01, with the day length moving
86401 → 86400 s. A leap-second-spanning cache cell would be interpolating across a step change of
1e-3, which is larger than every 0.1 % budget.

## Longitude wrap and near-polar behaviour

The ±180° seam behaves like any other longitude step (max ln-ρ span 4.87e-07 across the seam versus
5.98e-07 away from it), so any discontinuity there is a property of a cache cell, not of the model.
Near-polar samples are served natively by construction.

## Limits of this evidence

Single-point local sensitivities of one model at its own input contract. They bound what an
interpolant must reproduce; they are not themselves an interpolation error estimate for a cache.

The run that completed on the experimental pinned set measures **both** models through the full
provider path, on all five archived regimes:

| Model | Largest altitude ratio span (100 km step) | Largest time ratio span | Largest one-sided asymmetry (log) |
|---|---:|---:|---:|
| NRLMSISE-00 | 45.98 – 604.7 | 0.95 – 2.73 | 0.53 – 1.32 |
| JB2008 | 36.58 – 611.3 | 2.21 – 5.86 | 1.00 – 1.74 |

The JB2008 diurnal amplitude is markedly larger than NRLMSISE-00's: a 6-hour separation changes
density by up to 5.9× on JB2008 against 2.7× on NRLMSISE-00, and the one-sided asymmetry across
discrete weather transitions is up to 1.74 in log density. JB2008 is therefore the harder model to
cache on the time axis, not the altitude axis.

Finite differences now record, for each pair, both the first derivative and a second-order residual,
so curvature is separable from gradient rather than inferred. Weather transitions are classified by
comparing the complete discrete input vector on both sides: a span whose endpoints match that vector
is a genuine provider-response measurement, and one whose endpoints differ is a transition and is
reported as such rather than as a derivative.

## Measured provider cost structure

These components were measured on the uninstrumented build, each on its own timer, 2000 operations
per repeat, and they explain the performance outcome directly.

| Component | JB2008 | NRLMSISE-00 |
|---|---:|---:|
| Full coupled RHS, including density | 19 441 ns | 10 863 ns |
| `DirectAtmosphere::evaluate`, shared Sun | 12 571 ns | 8 047 ns |
| `DirectAtmosphere::evaluate`, own Sun lookup | 14 521 ns | 8 008 ns |
| Native model adapter | 7 326 ns | 2 984 ns |
| Provider acquisition contract | 4 798 ns | 5 801 ns |
| UTC calendar conversion | 4 160 ns | 4 755 ns |
| Space-weather record read | 77 ns | 26 ns |
| Geodetic conversion | 207 ns | 233 ns |
| Earth-fixed rotation (cached) | 15 ns | 21 ns |
| Snapshot + forces, no density | 621 ns | 597 ns |

Two things follow, and both are measured rather than assumed:

1. **ATM-OPT-001 has already taken the cheap wins.** Earth-fixed rotation is 15–21 ns and the
   space-weather record is 26–77 ns — both already cached or amortised. There is little left to
   optimise in preprocessing.
2. **The native model call is only 30–37 % of the provider call** (7 326 of 19 441 ns for JB2008;
   2 984 of 10 863 for NRLMSISE-00). The remaining 63–70 % is acquisition: UTC calendar, geodetic
   conversion, Sun geometry, frame handling. A density cache that reconstructs skips the native
   call but still pays acquisition, so its *maximum* possible saving is bounded by that fraction —
   before any lookup cost. For NRLMSISE-00 the ceiling is 0.37×, and for JB2008 it is 0.63×.

This is the structural reason no configuration in `PERFORMANCE.md` exceeds 1.16×.