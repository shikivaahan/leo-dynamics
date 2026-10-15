# End-to-end performance — protocol and measured results

## Protocol

- Uninstrumented Release build (`-O3 -DNDEBUG`, GCC 13.3.0, C++20, no fast-math) linking the exact
  production libraries.
- One configuration per measured workload; recorded repeats in a deterministic rotation with one
  discarded warm-up, so no configuration always runs first.
- Timed region: cache construction plus propagation. Traces, counters, checksums and cache
  statistics are read after the clock stops.
- Speedup is the median ratio of interleaved repeats of the same workload. It is never formed by
  subtracting independently measured medians.
- A speedup is reported only against an authoritative configuration measured **on the same route,
  the same thread count and the same ensemble in this same run**. A sequential baseline is never
  aliased onto a threaded result; an unmatched route reports wall time and no ratio.
- Benchmark groups run exclusively: a group starts only when no other child is alive.
- Process-isolated workloads reuse MT-002's orchestration — contiguous balanced ranges, one child per
  range, children concurrent within the group — and the parent measures the group's wall time
  including launch, kernel/weather loading, propagation, output and the binary merge. Child times are
  never summed into a throughput figure.
- Each process owns its own cache. Reuse inside a process is real reuse; reuse between processes is
  reported as unrealisable for a process-local cache, never merged into an aggregate hit rate.

## Measured speedups

Median ratio against the authoritative provider on the same route. Six-hour propagation, 30 s
output cadence, ITRF93, all ATM-OPT-001 preprocessing reuse enabled. `n` is the number of recorded
repeats behind each median.

### Single trajectory

| Configuration | Model | n | Median speedup | Range |
|---|---|---:|---:|---|
| A exact memoisation, warm | JB2008 | 3 | 0.674× | 0.660 – 0.712 |
| A exact memoisation, cold | JB2008 | 3 | 0.687× | 0.686 – 0.687 |
| B nearest-neighbour, warm | JB2008 | 12 | **1.116×** | 0.951 – 1.217 |
| B nearest-neighbour, cold | JB2008 | 12 | 0.972× | 0.947 – 1.017 |
| C log-density interpolation | JB2008 | 6 | 0.071× | 0.067 – 0.073 |
| E preprocessing control | JB2008 | 3 | 0.801× | 0.788 – 0.816 |
| A exact memoisation, warm | NRLMSISE-00 | 3 | 0.629× | 0.605 – 0.657 |
| B nearest-neighbour, warm | NRLMSISE-00 | 12 | 0.897× | 0.862 – 0.981 |
| C log-density interpolation | NRLMSISE-00 | 6 | 0.861× | 0.823 – 0.905 |
| E preprocessing control | NRLMSISE-00 | 6 | 0.730× | 0.712 – 0.753 |

### Ensemble workloads

| Workload | Model | A | B | C | E |
|---|---|---:|---:|---:|---:|
| Sequential, shared cache | JB2008 | 0.700× | **1.157×** | 0.070× | 0.788× |
| Sequential, shared cache | NRLMSISE-00 | 0.627× | 0.917× | 0.898× | 0.724× |
| Threaded, per-trajectory cache | JB2008 | 0.588× | 0.728× | 0.065× | 0.705× |
| Threaded, per-trajectory cache | NRLMSISE-00 | 0.552× | 0.696× | 0.533× | 0.677× |

### Fixed-state RHS

Repeated evaluation of one identical state — the regime an exact memo is designed for.

| Model | A | B | C | E |
|---|---:|---:|---:|---:|
| JB2008 | **2.091×** (n=3, 1.978–2.220) | 1.260× (n=12) | 1.226× (n=6) | 0.760× |
| NRLMSISE-00 | **1.587×** (n=3, 1.581–1.607) | 0.952× (n=12) | 0.926× (n=6) | 0.716× |

## What these numbers mean

**Cache overhead is never free.** Every configuration that reconstructs less often than it computes
still pays key construction, a hash lookup and a store comparison on every query. When the hit rate
is low — which is what an adaptive integrator produces — that cost is pure loss, and it is what the
0.55–0.71× trajectory rows measure.

**The accuracy requirement and the performance requirement are in direct opposition.** Candidate B
is the only family that can exceed break-even, and only because it is the cheapest to look up: a
bounded nearest-neighbour cell. But the cell width that keeps B affordable (10–25 km) is exactly the
width at which its error is 37–116 %. Candidate C can be made accurate, but a 4-D interpolation must
build up to 2⁴ native nodes per cold query; at 0.025 km cells that is a 14× slowdown (0.065×).

**Exact memoisation is the only accurate option, and it only pays on repeated states.** 1.59–2.09×
when the caller re-queries an identical state; 0.55–0.67× on every trajectory workload. The
production integrator does not repeat states.

**A shared cache inside one process helps, and process isolation removes the benefit entirely.**
Where B was measured both ways, the shared sequential-ensemble route beat the per-trajectory route
(1.157× vs 0.990× on JB2008). Across processes there is no shared cache by construction, and the
per-process cache inherits the trajectory hit rate, which is low.

## Unmeasured and out of scope

- Thread-count scaling: the threaded ensemble is measured at one worker count per ensemble size.
  A cache that is already losing at that point cannot become winning by adding threads, but the
  full scaling curve was not measured and is not claimed.
- Memory: cache payload bytes and process peak RSS are published per configuration in
  `evidence/performance-summary.json`. No memory-versus-speed Pareto is claimed, because every
  Pareto point would be dominated by the no-cache control.
- Interprocess cache sharing: not built, and the measurements give no reason to build it.