# Reconstruction accuracy — screening contract and measured results

## Screening contract

A candidate configuration is eligible for coupled and performance measurement only when **every**
accuracy task for that exact configuration — each weather epoch, each Ap mode, each requested rung —
completed and published:

- a `requested_rung` whose `settings.base_cells`, capacity, interpolation mode, axis and replacement
  policy equal what the runner requested;
- **all four** holdout packets (`uniform-random`, `local-query-packet`, `broad-query-packet`,
  `stratified-physical-boundaries`);
- a maximum absolute relative error at or below the screening threshold;
- zero nonpositive or nonfinite reconstructions.

A mean or a percentile never satisfies a threshold. Budgets are derived from the **single worst**
value over every packet and every epoch, never unioned across them — a configuration cannot claim a
budget that some other packet happened to meet.

## The four holdout packets

| Packet | What it exists to catch |
|---|---|
| `uniform-random` | whole-domain coverage; mostly exercises the fallback path |
| `local-query-packet` | clustered points matching an adaptive integrator's stage sequence |
| `broad-query-packet` | wider clusters, still integrator-like |
| `stratified-physical-boundaries` | both hemispheres, poles, dawn/dusk/day/night, native altitude branch points, longitude wrap, near-polar fallback, and **explicit cell-interior pairs** |

The fourth packet is the one that makes the result meaningful. A fine grid can produce a small error
simply by never reconstructing: every query falls back to the native model and is exact by
construction. To distinguish that from genuine approximation, the packet contains fractional
cell-interior pairs (0.23 and 0.76 of each cell) whose native references are used only for scoring
and are never injected into the cache. At 0.25 km cells both models served 224/224 of those
interior queries from stored approximations with zero native calls, at a maximum error of 0.458 %
(NRLMSISE-00) and 0.432 % (JB2008).

## Measured screening results

Threshold 1 %, worst case over four packets and every weather epoch, both Ap modes where applicable.

| Candidate | Configuration | JB2008 | NRLMSISE-00 | Budgets met |
|---|---|---:|---:|---|
| A exact memoisation | — | 0.0000 % | 0.0000 % | 0.01, 0.1, 0.5, 1, 5 |
| E preprocessing control | — | 0.0000 % | 0.0000 % | 0.01, 0.1, 0.5, 1, 5 |
| B nearest-neighbour cell | 0.25 km / 0.25° / 1 s | 0.6206 % | 0.4728 % | 0.5, 1, 5 (NRL) |
| C log-density interpolation | 0.025 km / 0.025° / 0.05 s | 0.8862 % | — | 1, 5 |
| C log-density interpolation | 10 km / 2° / 10 s | — | 0.2966 % | 0.5, 1, 5 |
| D adaptive refinement | all rungs | never met 1 % | never met 1 % | — |

### Coarse-to-fine behaviour

Worst absolute relative error over all epochs, as the declared ladder advanced:

| Cell (km, °, s) | B / JB2008 | B / NRL | C / JB2008 | C / NRL |
|---:|---:|---:|---:|---:|
| 25, 5, 60 | 116.2 % | 115.1 % | 9.99 % | 1.65 % |
| 10, 2, 10 | 37.0 % | 36.6 % | 9.84 % | 0.308 % |
| 0.25, 0.25, 1 | 0.62 % | 0.47 % | — | — |
| 0.025, 0.025, 0.05 | — | — | 0.886 % | — |

Candidate B converges roughly linearly in cell width, as first-hit reuse must: the error is the true
gradient times the distance to the cell's stored point. Candidate C converges far faster because
interpolation error falls with the square of cell width while the cost of building nodes does not.

## Answering the requested budgets

| Budget | Met by any approximate configuration? |
|---|---|
| 5 % | yes — B at 0.25 km, C at 10 km |
| 1 % | yes — B at 0.25 km (both models), C at 0.025 km (JB2008) and 10 km (NRLMSISE-00) |
| 0.5 % | yes — B at 0.25 km (NRLMSISE-00 only); JB2008 best is 0.6206 % |
| 0.1 % | **no** — best measured is 0.2966 % |
| 0.01 % | **no** — not reached at any rung |

These are experimental thresholds, not predetermined acceptable physical errors. The 0.1 % and 0.01 %
budgets are simply not reachable by any cache configuration screened here.

## Why candidate D never qualified

Adaptive refinement anchors a cache at a bounded resolution and refines where a periodic native check
measures error. It spent 51–60 % of its native calls on probes and anchors while its worst-case error
tracked candidate B's, because the refinement it performed was bounded by its probe divisor and max
depth rather than by the local gradient. On the declared ladder it never reached 1 %.

It was **not** propagated or benchmarked, because it never cleared screening. That is the contract
working, not a candidate being written off: the entire ladder was screened and it missed.

## Invalid reconstructions

Zero nonpositive or nonfinite reconstructions were produced by any configuration in any task. The
guard statistics are published per configuration; a reconstruction that is not finite and positive is
counted and reported, never substituted.

## Known weakness of the ladder as an exploration

The ladder refines altitude first, then the horizontal and temporal axes together, and it advances
only while the rung in front of it missed the gate. That is a reasonable coarse-to-fine strategy but
it is not an exhaustive search: a configuration could exist outside the declared rungs that meets 0.1 %.
What the ladder does establish is that none of the seven declared rungs does, for any of the four
candidate families, on either model, across every archived weather regime.