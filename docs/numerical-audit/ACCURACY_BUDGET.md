# Accuracy and resolution budget — version 1 (predeclared)

These budgets are declared before new large sweeps. They are not Svarog flight
requirements and are not selected from achieved numerical error. Original OPT-001
limits and evidence remain immutable but are not universal requirements.

## Separate uncertainty channels

| Channel | Evidence / treatment |
|---|---|
| Numerical integration | Successive RKF78/RK4 refinements, tight production Dopri and cross-method differences at identical physical times. |
| Ephemeris approximation | Direct CSPICE only in this experiment; no interpolation error introduced. CSPICE numerical output and planetary-ephemeris/model uncertainty are not asserted zero. |
| Floating point | Record actual mantissa widths, representable perturbations, same-grid long-double-state/double-force comparisons; not a fully high-precision oracle. |
| Physical parameter uncertainty | Signed multi-size exploratory spans and resolved derivatives. No probability distribution, flight tolerances or 1-sigma values invented. |
| Model form | Cold-beam/COM-flow drag, spherical atmosphere, uniform angular-plane eclipse, fixed-pole/J2 gravity, GG torque without finite-size reciprocal orbit force, lumped surfaces and simplified albedo. Not calibrated or assigned statistical variance. |

Do not add these as independent Gaussian variances. Empirical method/cadence/
precision spreads form conservative numerical envelopes. Physical responses and
uncalibrated model effects are reported separately; when actual bounds are known,
deterministic sums may be used with explicit correlation caveats, never silent RSS.

## Numerical convergence versus physical resolution

At each fixed horizon, publish:

1. Default/all-setting spread, including coarse configurations.
2. Selected-fine empirical uncertainty `U = 2 * max(pairwise fine differences,
   cadence refinement, same-grid precision exposure)`. The factor two is a
   predeclared safety margin, not a certified bound.
3. Refinement trend and independent-method agreement. Differences must shrink
   across settings, or an independently evidenced rounding plateau must be
   small enough for the selected decision. Longer-time errors may grow; demanding
   non-increase across horizons would be scientifically incorrect.

No convergence verdict is granted merely because one fine pair agrees. A finest
trajectory is a candidate, not automatic truth. The three cases use the same
rules at every horizon, including six hours.

A physical response is quantitatively **resolved** only if:

- its magnitude is at least `10 * U` for that observable;
- central slopes agree within20% across independent/refined methods and a
  neighboring perturbation size;
- perturbations are nonzero and valid, with signed one-sided responses/asymmetry
  retained; no saturation or branch ambiguity is concealed.

Zero empirical spread is flagged, not declared proof of exactness. Normalised
sensitivity `(p/J) dJ/dp` is undefined when p=0, J is near zero relative to U,
or an observable/axis is undefined. SO(3) relative rotations are used for attitude.

## Conditional application budgets (provisional, not flight acceptance)

These illustrate explicit decisions that would justify precision. Their numbers
come from the stated decision resolution, not from the current implementation.
An actual programme must confirm the decision levels and allocate physical/model
uncertainty before these can certify a mission.

| Intended decision | Provisional decision resolution | Integration allowance | Ephemeris-approximation allowance | Roundoff allowance |
|---|---:|---:|---:|---:|
| Distinguish10 m RTN orbit displacement for engineering comparisons | 1 m | 0.1 m | 0.02 m | 0.02 m |
| Degree-bin sail-normal orientation / coarse attitude planning | 0.1 degree | 0.01 degree | 0.002 degree | 0.002 degree |
| Distinguish a10% change in mean projected area or cumulative impulse | 1% of nonzero characteristic value | 0.1% | 0.02% | 0.02% |
| Decide sign or compare10% changes in net SRP/drag work | 1% of a physically specified nonzero work scale | 0.1% of that scale | 0.02% | 0.02% |

Work near cancellation cannot use its nearly zero net value as a scale. Use
accumulated absolute power/work as an explicitly labeled conditioning scale,
retain the net work and its cadence uncertainty, and do not claim SMA gain when
the sign or magnitude is unresolved. Momentum/spin-axis requirements likewise
need a specified engineering decision; zero-spin axes are explicitly undefined.

For local parameter experiments, the response-relative 10:1 resolution rule is
usually stricter than these coarse application illustrations. Passing a coarse
provisional budget does not make a small sensitivity resolved, and high physical
uncertainty never licenses arbitrary solver error.

## Physical spans and horizons

All perturbation spans and horizons are in EXPERIMENT_DESIGN.md. Density,
accommodation, optics and CP offsets are exploratory uncertainty probes, not
measured manufacturing/atmospheric error bars. Mass and inertia may be correlated
in real hardware; OFAT independent probes are diagnostic derivatives, not a
joint probability model. CASE-B's project example lacks a verified mass budget,
membrane flexibility and pointing requirement.

Results will state, separately for orbit, instantaneous attitude, pointing,
projected area, impulses and work, the longest tested horizon that supports the
selected numerical/physical decision. Failure at one observable/horizon does not
automatically invalidate the numerical kernel; it also does not certify longer
horizons. No six-hour exclusion is imposed in advance.

## Historical limits and readiness

The original1 cm /1 microradian /1e-8 rad/s /1e-5 J choices were engineering
criteria for one OPT-001 experiment. They remain binding when interpreting that
historical experiment, but no documented mission budget establishes them as
universal application requirements. This study does not alter those files or
conduct caching work. Readiness for application benchmarking requires numerical
uncertainty materially below the relevant physical response and decision level;
readiness for a future ephemeris approximation additionally requires an allocated
approximation budget and a qualified direct reference.

## Measured support for the conditional decisions

The three-case v1.1 study uses direct CSPICE throughout. The following are
empirical two-times selected-fine spreads, not certified global bounds:

| Observable / allowance | CASE-A, 6 h | CASE-B, 6 h | CASE-C, 6 h | Longest supported tested CASE-C horizon |
|---|---:|---:|---:|---|
| Position, 0.1 m integration allowance | 3.97e-5 m | 1.31e-4 m | 19.0 m | 3 h (0.0361 m) |
| Point-mass SMA, conditional 0.1 m displacement allowance | 1.76e-6 m | 9.58e-6 m | 6.66 m | 3 h (0.00948 m) |
| Instantaneous orientation, 0.01° integration allowance | 1.82e-8 rad | 2.73e-5 rad | 3.97 rad formal margin | 1 h (8.48e-7 rad) |
| Angular velocity, no supplied mission allowance | 9.06e-12 rad/s | 2.71e-8 rad/s | 0.0129 rad/s | Depends on a specified rate-control decision |

CASE-C's formal safety-margin envelope can exceed π; every underlying SO(3)
distance is in [0,π]. It is not a physically possible 3.97 rad shortest-arc
rotation. CASE-C fails the conditional two-hour orientation allowance
(2.10e-4 rad versus 1.745e-4 rad). A coarse six-hour density/area/mass SMA
response may nevertheless be distinguishable from its metre-scale numerical
floor; that does not certify a six-hour pointing trajectory or a small orbit
correction. CASE-B's nominal six-hour trajectory does not certify every
physically perturbed CASE-B trajectory.

### Sampled observables are a different accuracy problem

`cadence-refinement-v11.json` measures 30, 15, 7.5 and 3.75 s output cadences on
identical RK4 0.25 s evaluation schedules. All three cases' common-time states
are bitwise identical. Thus these differences isolate output quadrature rather
than adaptive trajectory changes:

| Case | SRP-work difference 30→15 s | 15→7.5 s | 7.5→3.75 s |
|---|---:|---:|---:|
| CASE-A | 0.01793 J | 0.008528 J | 0.0007728 J |
| CASE-B | 4.9366 J | 0.9072 J | 0.002241 J |
| CASE-C | 0.1912 J | 0.006379 J | 0.09324 J |

The nonmonotonic CASE-C result does not establish quadrature convergence.
The large improvement in CASE-B does not establish fourth order across its
eclipse events. Work/impulse accuracy must include both trajectory and cadence
exposure. The sensitivity qualifier separately measures 15 s cadence in BOTH
signed perturbed trajectories; nominal cadence uncertainty alone is insufficient.
Accumulated absolute source power is retained as a work-conditioning scale,
not a claim that a nearly cancelled net work has a useful relative accuracy.
No substep interpolation is used as an independent oracle.

### Physical and model-form components

Mass, effective area, inertia, atmospheric scale, pressure-centre, material
coefficients and initial conditions have measured OFAT responses and numerical
resolution gates. Their exploratory spans are not calibrated physical error
bars. Therefore the study demonstrates integration accuracy below particular
tested physical responses, **not** below an established mission uncertainty
distribution. Parameter covariance, density/weather validation, optical/thermal
tests and a hardware mass/geometry budget remain required to quantify that
component.

The model-form budget remains unbounded by this study: cold-beam drag with one
COM wind omits panel rotational velocity and thermal re-emission; the atmosphere
is spherical/tabulated; albedo is radial; the eclipse model has angular-plane
uniform-brightness disks; GG torque has no reciprocal finite-size orbital force;
CASE-B omits flexible membranes, structural modes and a controller. Source
isolation (`force-contributions-v11.json`) measures consequences of removing
SRP or GG, not the error of these approximations. None is assigned an invented
percentage or combined by RSS.

Direct SPICE introduces no newly implemented interpolation approximation here.
Kernel truth/uncertainty is not certified by agreement with that same kernel.
Targeted signed Sun-coordinate offsets and mixed arithmetic are separate
exposure studies, not ephemeris error distributions or an independent
high-precision force model. Any future approximation needs an allocated
observable-specific bound, a qualified direct reference, and a perturbation
regime where physical response is distinguishable from numerical uncertainty.

### Measured arithmetic exposure (not a pure roundoff bound)

The binary records 53-bit double and 64-bit long-double mantissas. The extended
state buffers still call double force kernels and double CSPICE. On the same
RK4 0.25 s schedule over six hours:

| Case | Maximum position exposure | Maximum SO(3) exposure | Maximum rate exposure |
|---|---:|---:|---:|
| CASE-A | 6.40e-6 m | 3.51e-12 rad | 6.25e-16 rad/s |
| CASE-B | 3.36e-6 m | 5.89e-11 rad | 5.85e-14 rad/s |
| CASE-C | 4.05e-5 m | 9.93e-6 rad | 3.16e-8 rad/s |

These measured mixed-state exposures are much smaller than CASE-C's cross-step
orientation spread. They show that changing state accumulation alone does not
resolve its six-hour reference. They do not separate force arithmetic, time
representation, projection and dynamical amplification into independent errors.
No purely higher-precision physical force calculation or exact SPICE truth
oracle was used.

### Ephemeris sensitivity is unresolved, not small

`precision-events-v11.json` perturbs the direct Sun X coordinate by signed
representable sizes and measures what actually reached the force model from the
reported load series. The Sun X component's ULP is 3.8147e-6 m, so a requested
1e-6 m offset applies exactly zero and is permanently unresolved; 1 ULP, 1e5
ULPs, 1 m, 100 m and 1000 m all realise exactly as requested with sign preserved.

At six hours the largest response/envelope ratio over every observable, sign and
size is 1.34 (inclination, CASE-A); the greatest ratio anywhere in the study is
1.34 against a required 10. **Zero of 154 entries per case per horizon resolve.**
Every reported Sun-offset difference is a measured run-to-run difference, not an
ephemeris sensitivity. This is the direct answer to the original suspicion that a
30 µm Sun perturbation drove the six-hour anomaly: it is not separable from
numerical uncertainty at any tested size in any case, while the initial-condition
and parameter sensitivities of the same cases are clearly resolvable.

### Point-mass energy change is not a decay measurement

`orbital-diagnostics-v11.json` compares the terminal point-mass specific-energy
difference with the accumulated trapezoidal work of the modelled sources over
the same six hours:

| Case | Point-mass Δε [J/kg] | Non-gravitational specific work [J/kg] | Ratio | Dominant source |
|---|---:|---:|---:|---|
| CASE-A | −9166.1 | −0.2221 | 41271 | drag (−0.2134 J/kg) |
| CASE-B | −13225.2 | −59.07 | 223.9 | SRP (−23.74) and drag (−40.08) J/kg |
| CASE-C | −18305.1 | −8226.6 | 2.2 | drag (−8220.7 J/kg) |

Point-mass energy is not conserved under J2; it oscillates by 3.8–7.0e4 J/kg
within a single case's own run. For CASE-A the naive diagnostic exceeds the
total modelled work by a factor of 4.1e4. Reporting that number as orbital
decay would be wrong by four orders of magnitude. Only accumulated source work
is attributable to the modelled forces, and even that requires the force model
to be qualified. **A semi-major-axis or energy decay prediction is therefore not
supported by this study for any case**, and the `energy_change_j_kg` sensitivity
channel is retained solely as a J2-contaminated diagnostic. Computing decay in
an osculating Kepler element set under J2 requires a J2-consistent element
transformation, which this audit does not implement.

### Mission-observable horizons are not interchangeable

The following are longest **tested nominal horizons** meeting the declared
integration allowance in the present 30 s sampled evidence. Pointing RMS uses
0.01°; mean area and impulse use 0.1% of the nonzero nominal value; work uses
0.1% of the accumulated absolute source power from the 3.75 s cadence study.
They are conditional numerical agreements, not calibrated mission predictions:

| Observable | CASE-A | CASE-B | CASE-C |
|---|---|---|---|
| Sun-pointing-axis RMS | 6 h | 6 h | 3 h |
| Mean projected solar area | 6 h | 6 h | 3 h |
| Cumulative SRP impulse norm | 6 h | 2 h | 60 s |
| Cumulative drag impulse norm | 6 h | 6 h | 1 h |
| Net SRP work, absolute-power conditioning scale | No tested horizon | No tested horizon | 60 s |
| Net drag work, absolute-power conditioning scale | 6 h | 6 h | 3 h |

An averaged pointing observable can agree when instantaneous SO(3) state
does not. Conversely, accurate state integration does not ensure accurately
sampled work. Scalar/cadence envelopes can be nonmonotonic: the longest
supported scalar horizon does not certify every shorter interval (for example,
CASE-A's 60 s mean-area quadrature is poorly sampled). Earlier/horizon-specific
values remain in the machine-readable evidence. These entries do not promote
an unresolved physical derivative; each derivative still passes its own
perturbed-trajectory, method, size and cadence gates.

