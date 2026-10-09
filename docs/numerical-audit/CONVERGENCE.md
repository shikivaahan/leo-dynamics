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
