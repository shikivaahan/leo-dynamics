# ATM-CACHE-001 parameters

Every significant choice, its value, and why it was chosen. Values that were selected and then
rejected are listed with the reason, so a later run does not repeat the experiment.

## Baseline and authority

| Parameter | Value | Reason |
|---|---|---|
| Experimental scientific input set | `exp-solfsmy-20261011-original-pck` | separately pinned SOLFSMY release; historical `direct-v2` unchanged; all-five fixture equivalence is domain-limited |
| Preprocessing | UTC reuse, Sun sharing, Earth-orientation reuse all **on** | ATM-OPT-001 production configuration; the study refuses to run primary otherwise |
| Reference density | fresh `DirectAtmosphere::evaluate()` at the same epoch and position | errors must not be confounded with different spacecraft states |
| Native source | pinned Brodowski NRLMSISE-00 and official JB2008 Fortran, hash-checked | no model reimplementation |
| Weather | original CSSI 2020-02-24 and DTCFILE pins; SOLFSMY-live SHA `9bc734e66da7b5919ce6fd2b3475db82c28e500988dbb2f8d994d5ff3c074e35` | only the explicitly selected SOLFSMY release differs from historical inputs |
| Kernels | original NAIF0012 LSK, DE432s and recovered `earth_000101_270105_261009.bpc` (SHA `14deb9ab9dd49482185cc49a9c29fe56ae65d50ab94caf3197db8694836b6f81`) | original ITRF93 orientation, hash verified; no alternate Earth kernel |

## Pinned-input native sensitivity (measured)

| Parameter | Value | Reason |
|---|---|---|
| Probe coordinates | explicit Earth-fixed geodetic; no inertial state formed | needs only UTC and CSSI, so it survives the missing orientation kernel |
| Altitudes | 200–1000 km, 50 km grid | requested LEO band; NRLMSISE-00's own native range ends at 1000 km |
| Altitude perturbations | 0.001, 0.01, 0.1, 1, 5, 10, 25, 50, 100 km | spans integrator-scale probes to full interpolation cells |
| Latitudes | −80, −45, 0, 45, 80° plus ±89.9, ±89.999 | equator, mid-latitudes, near-poles, and the polar fallback limit |
| Longitudes | full 360° sweep plus seam probes at ±179.9999° | measures whether geographic quantisation invents a discontinuity |
| Local solar times | 7 phases (midnight, dawn, morning, noon, afternoon, dusk, evening) | diurnal terms dominate the altitude response |
| Time separations | 1 ms, 10 ms, 0.1, 1, 10, 60, 300, 900, 1800, 3600, 10800, 21600 s | adaptive-integrator stage spacing through hours |
| Weather regimes | quiet_june, quiet_december, moderate_march, active_october, storm_october (daily Ap); storm_october in Ap-history mode | existing frozen fixture epochs, real records, both geomagnetic configurations kept separate |
| Boundary epochs | `2016-12-31T23:59`, three-hourly bin, UTC midnight | leap second and daily/3-hourly transitions |
| One-axis holdout | 1, 5, 10, 25, 50, 100 km altitude cells, 3 independent fractions per cell | isolates altitude cost from every other axis |
| Seed | 20261014, `mix64(seed ^ mix64(purpose))` sub-seeds | any sub-experiment replays alone |

## Candidate screening

| Parameter | Value | Reason |
|---|---|---|
| Capacity | 4 096 and 262 144 entries | small and large bounds on a bounded store |
| Replacement | LRU and FIFO | eviction policy sensitivity |
| Interpolation axes | altitude, time, spatial, 4-D | separates per-axis cost |
| Grid axis | linear and logarithmic | ln ρ is close to linear in ln h |
| Coarse-to-fine | (25 km, 5°, 60 s) → (10 km, 2°, 10 s) → … | altitude refined first because it dominates the error |
| Error targets | 0.01, 0.1, 0.5, 1, 5 % | requested thresholds; satisfied only on the **maximum** observed error |
| Adaptive probe divisor | 16 | one periodic native validation per 16 queries drives refinement |
| Adaptive max depth | 4 | bounded refinement; no adaptive mesh framework |
| Holdouts | independent uniform, local and broad packets plus 684 physical-boundary/cell-interior probes | whole-domain coverage and explicit approximation-active reuse; not a fallback-only accuracy claim |
| Points | 512 per task in smoke, larger in screen/full | recorded per task |

## Coupled propagation

| Parameter | Value | Reason |
|---|---|---|
| Cases | CASE-A, CASE-B, CASE-C | representative, contrasting and stress configurations |
| Windows | 6 h; 24 h for the representative case | requested durations |
| Output cadence | 30 s | frozen configuration |
| Dopri5 | rtol 1e-10, max step 15 s, initial step 1 s | existing project tolerances, unchanged |
| Refinement | independent Boost RKF7(8), rtol 1e-12, max step 5 s | separates integrator error from candidate error |
| Absolute tolerances | position 1e-3 m, velocity 1e-6 m/s, quaternion 1e-12, body rate 1e-12 rad/s | project defaults |
| Perturbations | small/moderate/large at scales 0.25/1/4 → 1 m/100 m/1 km, 1 mm/s/0.1 m/s/1 m/s, 0.1 mrad/10 mrad/0.1 rad, 1 µrad/s/1 mrad/s/10 mrad/s | requested initial-condition spread; member 0 always unperturbed |
| Attempt/rejection counters | JSON `null` for Dopri5 | `leo::propagate` does not expose them; the RKF7(8) loop reports its own |

## Performance

| Parameter | Value | Reason |
|---|---|---|
| Build | Release `-O3 -DNDEBUG`, GCC 13.3.0, C++20, no fast-math | uninstrumented production libraries |
| Timer | `std::chrono::steady_clock`, `ru_maxrss` for memory | wall and resident memory |
| Repetitions | 7 measured repeats, order-rotated, one discarded warm-up | avoids the measurement-order bias found in ATM-OPT-001 |
| Speedup | median ratio of interleaved repeats of the same workload | never a difference of independently measured medians |
| Process isolation | MT-002 contiguous balanced ranges, one child per range, concurrent within a group, parent-measured group wall | measured MT-002 orchestration, not a reimplementation |
| Timed region | construction plus propagation; traces, counters and cache statistics read after the clock stops | no instrumentation inside a measurement |
| Benchmarks | exclusive; no other child alive | timing uncontaminated by sweeps |

## Reliability

| Parameter | Value | Reason |
|---|---|---|
| Exit codes | 0 complete, 3 incomplete, 4 blocked prerequisites, 2 provenance refusal, 130 interrupted | distinct outcomes are machine-visible |
| Resume identity | config hash, study source hashes, binary hash, input hashes, case hashes | an incompatible resume is refused |
| Completed tasks | trusted only while published result and artifacts hash correctly | tampering is detected, never assumed away |
| Budget | clean stop, completed work retained, remainder pending | no partial success presented as complete |
| Planner | deterministic; stage order sensitivity+locality → accuracy → coupled → benchmark → aggregate | candidates are propagated only after measured screening |

## Rejected choices

- **Production density cache.** Out of scope by instruction; nothing in the production path changed.
- **Generic caching framework.** Each candidate is a small evaluator with its own guard logic.
- **Relabelling or re-pinning the historical baseline.** Not permitted. New work has its own
  input-set manifest, run directory and evidence namespace; the blocked original run is preserved.
- **Adaptive mesh.** Candidate D is bounded anchors and a periodic native check, nothing more.
- **Interprocess cache sharing, IPC, distributed cache.** Not built; interprocess reuse is reported
  as unrealisable for a process-local cache.
- **New integrator, retuned tolerances, changed force laws.** None; the study reuses the existing
  Dopri5 and RKF7(8) paths.