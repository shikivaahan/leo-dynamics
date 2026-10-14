# ATM-CACHE-001 design

## The one seam a candidate is allowed to cross

`leo::EnvironmentSnapshot::density_kg_m3` is the only field a candidate touches. The coupled dynamics,
force and torque laws, attitude kinematics, the integrator, tolerances, native models, weather
conventions, SPICE kernel lifecycle and the ATM-OPT-001 preprocessing caches are untouched.

`DirectAtmosphere::evaluate()` also returns temperatures and, for NRLMSISE-00, eight constituent
number densities. A density-only cache therefore cannot stand in for that API: the study's
`DensityEvaluator` exposes density for candidate paths and keeps `authoritative()` returning a full
fresh `Result`. No caller of the production API is affected.

## Input identity

A candidate answers a query only from storage whose key matches on:

- the exact bit pattern of the epoch and of the three J2000 position components;
- the model's **complete discrete space-weather vector** compared bitwise, never by hash: NRLMSISE-00
  previous-day F10.7, centred-81-day average, daily Ap, the seven 3-hourly Ap values, year and day of
  year; JB2008 the eight SOLFSMY lag proxies and the interpolated DSTDTC;
- the UTC civil day, the leap-day flag and the Ap-history switch.

Interpolated cells additionally require every corner's weather identity to match the query's, which is
what refuses reuse across an integer DSTDTC transition inside an hour, across a 3-hourly Ap bin, and
across UTC midnight. Model, frame, switch vector, Ap-history mode and the managed SPICE generation are
evaluator ownership, not per-query keys.

## Kernel-generation provenance

Cached values carry a token. An opaque `SunObservation` is retained and re-checked with
`compatible_sun()` before every lookup; that call compares the process-wide managed generation, so
load, unload and `invalidate_reuse()` are all detected even when the kernel bytes do not change. Any
change discards every stored value. A synthetic-token check exercises the same path deterministically
in the correctness study.

## Candidates

| Candidate | Mechanism | Approximation |
|---|---|---|
| A exact | bounded FIFO/LRU memoisation on the complete input bits | none |
| B cell | bounded nearest-neighbour spatial-temporal cells, first hit wins | yes |
| C interpolation | sparse multilinear interpolation of ln ρ; selectable axes (altitude, time, spatial, 4-D); linear or logarithmic grid axes | yes |
| D adaptive | bounded anchors refined when periodic native validation exceeds the target | yes |
| E control | exact preprocessing, no density caching | none |

Every candidate refuses a reconstruction that is non-finite or nonpositive and answers natively
instead, counting the refusal. Interpolation builds missing nodes from fresh native evaluations at
those nodes rather than scattering values from other spacecraft states. Near-polar queries
(|lat| > 89.999°) are served natively because a uniform longitude cell is degenerate there.

## Why `exp(ln ρ)`

Density spans roughly seven decades over 200–1000 km. Interpolating ln ρ keeps the reconstruction
positive by construction and makes the altitude dependence close to linear over short cells; the
measured curvature is what sets the usable cell widths (see `SENSITIVITY.md`). Exact memoisation is
bitwise; approximate candidates are judged on measured error, never on bitwise equality.

## Threading and process ownership

`DensityEvaluator` is mutable and **not** thread safe. Each thread and each trajectory owns its own
evaluator. An evaluator's storage is process-local: reuse between processes is real reuse potential
only within one process, and the study never publishes an aggregate interprocess hit rate. The
process-isolated workload reuses MT-002's orchestration (`split_ranges`, `merge_parts`) through
`docs/multithreading/process_batch.py` and writes the measured MT-002 binary format.

## Runner reliability

One process per task; each publishes exactly one `study-result.json` through a temporary file and an
atomic rename. The runner copies the child's bytes verbatim, records a SHA-256, and refuses a resume
whose configuration, sources, binary or input hashes differ. A completed task is trusted only while
its published result and artifacts still hash correctly; anything else is re-queued or refused, never
silently reused. Scientific sweeps run concurrently; benchmark groups are exclusive so a timing is
never contaminated by a sibling sweep. A budget stop, an interrupt or a single failure retains
completed work and records the rest honestly.