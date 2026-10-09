# Convergence assessment

**The corrected six-hour fully coupled reference is not globally certified.**
Local implementation fixes and passing regressions do not make unconverged
trajectories valid accuracy oracles. Original OPT-001 criteria remain unchanged;
there is no production tolerance relaxation or caching recommendation.

## Reproducible workload and methods

The original benchmark uses 13 states: J2000 position/velocity, body-to-inertial
unit quaternion and body angular velocity. ET=0, six-hour horizon, exact 30 s
outputs, 400 km circular inclined initial orbit, 10 kg box, diagonal inertia
(2,3,4), six 2 m² plates with CP `0.5 n + (0,0.1,0)` and original material values.
Full mode includes J2, SRP/conical eclipse, Sentman drag, radial albedo and all
selected environmental/GG torques. All parameters come from the unchanged
`benchmarks/cases/bench_support.hpp` configuration.

Production defaults: rtol1e-10, position/velocity atols1e-3/1e-6,
quaternion/omega atols1e-12/1e-12; initial/max step1/15 s. Individual sweeps vary
**only** rtol (1e-11/1e-12/1e-13), absolute-field scale (.1/.01/.001), or maximum
step (5/1/.25 s); initial step is reduced only if required by a smaller maximum.
Those are diagnostic configurations, not changed defaults.

Independent reference candidate: Boost Fehlberg7(8), a different tableau,
embedded order, default error checker and step adjuster. Variables are scaled by
physical field atols. Candidate A uses rtol1e-13, atols×.001, max1 s; candidate B
rtol1e-14, atols×.0001, max.5 s. This is a reference **candidate**, accepted only
when its own refinement and independent-method checks support it. It does not
share the production Dopri controller or accepted-step quaternion projection.
Analytical Kepler and principal-axis rotations independently validate smooth
cases. A separately implemented fixed RK4 uses prescribed binary-exact grids to
remove changes in adaptive evaluation times. Its agreement alone is not a
certificate for the fully coupled solution.

The Dopri trace mirror is diagnostic only and is required to match production
checksums bit-for-bit. It localises step histories but is never the sole oracle.

Commands and build/source selection: [README](README.md). Raw compact evidence:
[preaudit-study](evidence/preaudit-study.json),
[eclipse-only-study](evidence/eclipse-only-study.json),
[post-remediation-study](evidence/post-remediation-study.json). Each contains
configuration, RHS/attempt counts, final states, max/RMS differences, selected
perturbation growth history and all separate tolerance/step sweeps. No timing
speedup is inferred from these investigation runs.

## Post-remediation reference comparison at six hours

Differences below are between trajectories sampled at identical physical times.
Default comparison uses candidate A; refinement compares B against A. The
attitude-only mode retains the same independent translational point-mass
trajectory but has no environmental torques.

| Scenario | Default vs A position (m) | Default vs A attitude (rad) | A vs B position (m) | A vs B attitude (rad) |
|---|---:|---:|---:|---:|
| Two-body | 0.00217824 | 1.13452e-11 | 4.43152e-6 | 4.82345e-13 |
| J2 | 0.00222501 | 1.13452e-11 | 1.43270e-5 | 4.82345e-13 |
| Torque-free attitude | 0.00217824 | 1.13452e-11 | 4.43152e-6 | 4.82345e-13 |
| Principal-axis attitude | 0.00223400 | 2.28998e-13 | 4.43152e-6 | 1.25239e-14 |
| SRP force/torque only | 0.00210542 | 9.47942e-7 | 8.92045e-6 | 4.43560e-7 |
| Drag force/torque only | 31.6667 | 1.61394 | 12.5725 | 1.44984 |
| Albedo force/torque only | 0.00218961 | 6.58736e-7 | 2.53358e-6 | 3.35815e-9 |
| Gravity-gradient torque only | 0.00203072 | 9.61349e-10 | 1.43270e-5 | 1.51312e-11 |
| Fully coupled direct SPICE | 2.31751 | 0.216763 | 10.3059 | 1.69122 |

The smooth cases are consistent with numerical convergence and rounding floors;
remaining micrometre reference differences are recorded rather than declared
zero. SRP's smaller residual requires awareness of contact/non-smooth transitions.
Drag-driven and fully coupled six-hour reference candidates fail their own
refinement. Their default-vs-reference differences cannot be labelled exact
physical error because A is not truth.

A two-day J2 propagation and reference comparison are included in the raw study;
existing Stage 1–3 fourteen-day J2 and conservation regressions also remain in
the complete validation suites. No new physical law or integrator redesign was
introduced to force agreement.

## Independent observed order

Fixed RK4 with one terminal sample, compared against analytical solutions:

| Step (s) | Circular Kepler position error (m) | Principal-axis quaternion angular error (rad) |
|---:|---:|---:|
| 60 | 204.887 | 4.65960e-6 |
| 30 | 9.01268 | 2.91506e-7 |
| 15 | 0.444727 | 1.82235e-8 |
| 7.5 | 0.0240880 | 1.13904e-9 |

Step-halving order for the spin is 3.9986, 3.9997, 3.9999, approaching four;
orbital observed order is 4.5067, 4.3410, 4.2065, approaching its asymptotic regime.
These are independent RK4 checks, **not** claims that every nonsmooth Dopri
workload achieves fifth-order global accuracy. The production exponential
rejection and arbitrary-axis spin tests validate actual consumer behaviour;
componentwise local tolerances are not global angular-error guarantees.

## Refinement by horizon: where trust breaks down

Additional same-configuration RKF78 refinement .5→.25 s at rtol1e-14,
atols×1e-4; [raw evidence](evidence/coupled-horizon-refinement.json):

| Horizon (s) | Final position difference (m) | Final attitude difference (rad) |
|---:|---:|---:|
| 1800 | 9.35091e-7 | 1.93324e-8 |
| 3600 | 8.08870e-6 | 7.25979e-8 |
| 7200 | 2.75532e-5 | 2.53275e-6 |
| 10800 | 0.000498890 | 4.04661e-5 |
| 21600 | 0.274943 | 0.0622925 |

Further .25→.125 s refinement at six hours instead differs by **0.944686 m /
0.218660 rad**, not a monotone approach to the original limits. Tight Dopri
versus independently controlled RKF78 at .25 s differs by **1.02080 m /
0.236260 rad**. These are observed failures of global certification, not a
justification to relax acceptance limits or declare the finer run exact.
Shorter horizons have substantially better agreement; they do not validate the
six-hour case.

## Physical perturbations versus adaptive history

Original direct Sun X perturbation request30 µm becomes about30.5 µm after
binary64 rounding. The 1 µm request is exactly zero on the sampled Sun-X range,
so its trajectory is bitwise identical; treating it as a nonzero experiment
would be misleading. Signed ULP, millimetre and metre perturbations and a zero
repeat are recorded.

Corrected common-time, common-state replay measures only force-model effects.
Separately, fixed RK4 holds evaluation times identical while changing only Sun X:

| Method/grid | Final position separation for 30 µm (m) | Final attitude separation (rad) |
|---|---:|---:|
| Independently adaptive production Dopri | 0.161943 | 0.0197638 |
| Identical RK4, 1 s | 2.28127e-9 | 2.96182e-10 |
| Identical RK4, .5 s | 1.67897e-9 | 1.60252e-10 |
| Identical RK4, .25 s | 1.61447e-7 | 3.15311e-8 |

The large adaptive difference is not supported as a physical response to the
micrometre Sun displacement. Error estimates/next-step sizes split before the
first eclipse transition. Disabling eclipse still gives1.4663 rad separation;
disabling environmental/GG torques yields equal state histories. The distinction
between same-state model effects and already-diverged force/torque outputs is
maintained in the evidence.

On identical fixed grids, deliberately rotating the initial physical attitude
by1e-10 rad produces final separations0.000978494 rad (.5 s) and0.00415005 rad
(.25 s): gains9.78e6 and4.15e7. Larger1e-8/1e-6 seeds are also recorded in
[attitude-conditioning.json](evidence/attitude-conditioning.json). This supports
strong forced-attitude conditioning and numerical-error amplification. The gains
are grid-dependent finite differences, **not** certified Lyapunov exponents or
proof of chaos. A detailed attribution of all remaining error-growth mechanisms
is still unresolved; C0 face/table laws and finite-precision adaptive histories
are hypotheses to investigate, not independently established implementation bugs.

## Post-remediation OPT-001 diagnostic

Existing cache implementation and original60/300/900/1800 s intervals; original
accuracy limits untouched. [Complete outputs](evidence/opt001-post-remediation.json):

| Interval (s) | Final position difference (m) | Final attitude difference (rad) | SRP work difference (J) | Original criteria satisfied |
|---:|---:|---:|---:|---|
| 60 | 2.84844 | 0.378394 | 0.437733 | No |
| 300 | 0.609564 | 0.0698552 | 1.01191 | No |
| 900 | 5.18961 | 0.848625 | 6.23051 | No |
| 1800 | 4.57599 | 0.712251 | 4.84536 | No |

Direct-SPICE benchmark was also exercised with the unchanged inputs/options;
full numerical outputs/RHS counts are retained. This is new diagnostic evidence,
not an optimisation or replacement of earlier OPT-001 rejection records.

**Caching readiness: no.** Resolve NA-OPEN-001—an independently converged global
six-hour reference/error budget—before revisiting approximate ephemerides.
Passing114 regression tests proves the exercised analytical/boundary behaviours,
not every possible numerical regime or global solution accuracy.

## Extension: versioned scenario convergence study

The original workload is retained above as the historical record. The version 1
sensitivity study adds three physically documented cases (see
[EXPERIMENT_DESIGN.md](EXPERIMENT_DESIGN.md) and
`experiments/provenance.json`): CASE-A (analytically specified compact 6U-class
free flyer), CASE-B (project-inspired deployed sail, provisional values) and
CASE-C (this unchanged stress workload). Per case and horizon the study measures
a fine numerical envelope over tight Dopri, independently refined RKF78, fixed
RK4, output-cadence refinement and a mixed-arithmetic state channel, then reports
which physical sensitivities are resolved against it. Measured envelopes,
verdicts and reliable horizons are generated from
`evidence/sensitivity-results.json.gz` into [SENSITIVITY.md](SENSITIVITY.md) and
[evidence/figures/](evidence/figures/) by `summarise.py` and
`experiments/plot_sensitivity.py`. Historical sections retain their original data.

### Refinement regimes, not a finest-step oracle

The new study independently varies rtol, absolute scales and maximum step.
Observed RK4 orders below are `log2(difference(1,0.5)/difference(0.5,0.25))`
using maximum common-time differences. They are descriptive pairwise orders;
neither a shared force implementation nor a saturated SO(3) difference is an
independent accuracy reference.

| Case / horizon | Position observed order | SO(3) observed order |
|---|---:|---:|
| CASE-A / 60 s | 1.43 | 4.20 |
| CASE-A / 1 h | −0.16 | 7.12 |
| CASE-A / 6 h | −2.64 | 6.47 |
| CASE-B / 60 s | −0.40 | 0.82 |
| CASE-B / 1 h | −0.54 | 2.78 |
| CASE-B / 6 h | 0.94 | 2.78 |
| CASE-C / 60 s | 0.99 | 3.01 |
| CASE-C / 1 h | 4.80 | 2.71 |
| CASE-C / 6 h | 1.61 | 0.78 |

There is no demonstrated global fourth-order regime for every observable.
Nanometre/micrometre position spreads often stop improving under halving;
mixed long-double state arithmetic with double forces measures arithmetic
exposure but cannot certify a pure roundoff floor. Incidence/eclipse events
also invalidate a universal smooth-RHS order expectation. CASE-A/B
cross-method agreement is small relative to the predeclared conditional
decision levels despite these floors. CASE-C's six-hour attitude differences
are order-one and its small instantaneous sensitivities remain unresolved.

### Measured physical source isolation

`force-contributions-v11.json` compares each case with SRP force+torque removed,
with SRP torque alone removed (the translational force stays on), and with
gravity-gradient removed. Maximum common-time differences over the horizon,
with the same resolution gate as every other claim:

| Case / 6 h | Remove SRP (force+torque) | Remove SRP torque only | Remove GG |
|---|---|---|---|
| CASE-A position | 0.9419 m | 6e-6 m (unresolved) | 1.129e-3 m |
| CASE-A orientation | 3.956e-3 rad | 3.956e-3 rad | 5.302e-3 rad |
| CASE-B position | 364.3 m | 93.0 m | 532.8 m |
| CASE-B orientation | 3.127 rad | 3.127 rad | 3.134 rad |
| CASE-C position | 335 m (unresolved) | 378 m (unresolved) | 326 m (unresolved) |

CASE-A behaves as its geometry predicts: its common CP offset is 1 mm, so SRP
acts essentially through the centre of mass and its whole attitude effect is
torque-mediated (identical 3.956e-3 rad whether or not the force stays) while
its orbital effect is force-mediated (0.94 m versus an unresolved 6e-6 m).
CASE-B's sail couples the other way: its 0.05 m common CP offset lets attitude
torques feed back into the orbit, so removing SRP torque alone still moves the
position by 93 m. Every CASE-C six-hour isolation is unresolved, consistent
with its unresolved baseline rather than evidence that these sources are
unimportant. These are finite model-isolation changes over six hours, not local
derivatives and not uncertainty bounds.

### Demonstrated numerical configurations and cost

The selected fine comparison includes DP5 rtol 1e-13 / absolute-scale 0.001 /
maximum step 0.5 s; RKF78 rtol 1e-14 / absolute-scale 0.0001 / caps 1 and 0.5 s;
RK4 0.5 and 0.25 s; mixed-state RK4 0.25 s; and actual 15 s output cadence.
These are audit configurations, **not changed production defaults**.

| Case, 6 h | Default DP5 RHS calls | Tight DP5 RHS calls | RKF78 0.5 s RHS calls | RK4 0.25 s RHS calls |
|---|---:|---:|---:|---:|
| CASE-A | 60887 | 298266 | 561600 | 345600 |
| CASE-B | 12080 | 294428 | 561600 | 345600 |
| CASE-C | 43416 | 309696 | 562536 | 345600 |

RHS counts state the scientific cost of independent refinement, not a
performance optimisation or a speedup claim. Tightening the original
six-hour CASE-C settings does not establish an accurate instantaneous
reference. The conditional 0.1 m orbit allowance is supported through the
tested three-hour CASE-C horizon; 0.01° orientation through one hour.
CASE-A/B support both at six hours. Application requirements remain provisional.

Actual cadence refinement isolates a second limitation: identical RK4
quarter-second schedules give bitwise identical common-time states at
30/15/7.5/3.75 s output intervals, while work and averages change.
`cadence-refinement-v11.json` and independent cadence qualification of both
signed perturbations prevent that sampling error being called physics.

