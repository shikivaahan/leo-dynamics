# Sensitivity and convergence experiment design (version 1, predeclared)

This document is written **before** the sweeps run. Horizons, observables,
parameter ranges, dimensional units, acceptance rules and the uncertainty-envelope
logic below are frozen at this version. Measured numbers belong in evidence
artifacts, not here; budgets live in [ACCURACY_BUDGET.md](ACCURACY_BUDGET.md).

Nothing here changes production defaults, force laws, caching, interpolation or
the immutable historical benchmark/OPT-001 evidence. Source tree identity:
leo-dynamics `0c6a9c365a5d09c2049ea5591e76bf7d99b0761e`.

## 1. Why a sensitivity study at all

The numerical audit established (see [CONVERGENCE.md](CONVERGENCE.md)) that the
six-hour fully coupled trajectory is **not** globally certified, that refinement
is nonmonotonic, and that errors are strongly horizon- and scenario-dependent.
Two questions follow, and they are different questions:

1. **Convergence**: at a given horizon, how large is the spread between
   defensible integration configurations, and does it keep shrinking?
2. **Resolution / sensitivity**: can a *physical* parameter uncertainty be told
   apart from that numerical spread? A parameter whose effect is smaller than the
   numerical envelope is **unresolved**, no matter how precise the integrator is.

Answering (2) without (1) is meaningless, so both are measured with the same
observable definitions and the same reference generation rules.

No mission requirement for attitude accuracy, pointing or coefficient confidence
is asserted. Physical perturbation ranges are exploratory spans, not probability
distributions; numerical uncertainty is measured separately from those responses.

## 2. Scenarios

Three cases, fixed for all of version 1. Config files, provenance and exact source
line ranges/hashes are in [experiments/provenance.json](experiments/provenance.json).

### CASE-A — analytically specified compact 6U-class free flyer

10 kg, 0.1 x 0.2 x 0.3 m bus, six real faces (0.06/0.03/0.02 m^2), normals on the
body axes, centre of pressure at each geometric face centre plus a uniform
(0, 0.001, 0) m corresponds to a centre-of-mass displacement of -1 mm along y.
A positive linear mass-density profile produces that centroid; its central
inertia is the uniform-bus tensor minus m*d² on Ix and Iz. Orbit semimajor axis
is Earth equatorial radius +800 km, e=0.001, i=51.6 deg, zero RAAN/argp/nu.
In-plane attitude45 deg, body-x spin0.02 rad/s about the largest principal
moment favors gyroscopic conditioning. Stability under the complete environment
is a hypothesis to test, not assumed from torque-free stability. No controller.

Optical coefficients 0.3 specular / 0.2 diffuse, Cd 2.2 and Sentman accommodation
0.9 are **explicit exploratory assumptions of this study**. No CubeSat pointing
requirement, coating specification or accommodation value is claimed. For the
record, this optical split gives a normal-incidence effective Cr of 1.43, which is
*not* the 1.7 used for CASE-B and is not a measured material property.

All force and torque terms are non-trivial: no force model or light-bounce
coefficient is quietly substituted with a simplified value.

### CASE-B — cross-braced sail (project prototype, not flight spec)

Derived from `/home/user/Project-Svarog-Orbital/examples/earth_orbit_acc.jl`
(sha256 `aeac193d5335…`), lines 15-21 and 49-89: m=10 kg, L=3 m,
I=(3.75, 3.75, 7.5) kg m^2, CP {0,0,0.05} m, 800 km, e=0.001, i=97.97 deg,
RAAN 285.5 deg, zero spin, in-plane pitch 49.7 deg.

Declared departures from that example:

- The example sets `MODEL_TORQUES=false`; **this study enables all torques**
  (SRP, gravity-gradient, drag) as a declared requirement. Conclusions about the
  example's own behaviour therefore do not transfer.
- The example's scalar `Cr = 1.7` is split into specular 0.6 / diffuse 0.15. The
  split is **non-unique**: for a flat plate at normal incidence the force
  coefficient is `1 + s + 2d/3`, so many pairs reproduce 1.70. It is a provisional
  mapping, not measured sail optics, and it is swept like any other parameter.
- The 9 m^2 sail is represented as one flat two-sided membrane (a +z and a -z
  panel sharing the centre point {0,0,0.05} m). Real membranes have distributed
  area; the idealisation is stated so that CP sensitivities are not mistaken for
  membrane flexibility results.
- Inertia is the source's own idealised cross-brace/sail lumping. No flight mass
  budget is claimed.

Source-family inconsistency (why no mission spec is frozen): `earth_orbit.jl`
gives ~84.4 m^2 with lever {0.01,0.02,0.05}; `earth_ensemble_gpu.jl` uses L=2 m
with area 36 m^2; `on_off.jl` uses L=10 m with ~490.9 m^2; `sun_orbit.jl` uses
~3.48e4 m^2 with a 2 m lever. Area does not track L^2 outside the cited file.
No verified flight-parameter/uncertainty or mission-accuracy specification was
identified in the available project sources. The mission-concept citation is
recorded, but example constants are not promoted to flight specifications.
CASE-B has nominal area/mass0.9 m²/kg; its mass distribution and membrane
stand-off remain idealized model assumptions rather than a manufactured bus.
Analytic and measured scaling is derived from these actual configurations and
recorded in `evidence/spacecraft-characteristics.json`. No unsupported generic
CubeSat uncertainty/prior range is used to certify either configuration.

### CASE-C — original 6 h stress benchmark (unchanged)

Exactly `benchmarks/cases/bench_support.hpp :: dynamics(true) / :: initial()`:
400 km, |v|=sqrt(mu/r) with direction (0, 0.8, 0.6) — a circular orbit at
i=36.87 deg — six 2 m^2 plates with CP `0.5n + (0,0.1,0)`, mass 10 kg, inertia
diag(2,3,4), initial omega (0.002, 0.001, 0.0005) rad/s. Physical parameters are
read from the factory and deliberately not duplicated in the config.

Note that this geometry is not a conventional CubeSat: six 2 m^2 faces imply a
cube edge of sqrt(2)=1.414 m, but a lever of 0.5 m from the centre implies a
1 m edge. The original benchmark is kept as-is; every force and area quantity
derived from it (drag area in particular) uses the **projected area** law, not the
total surface area. The measured form of that discrepancy is in the audit
subsection below.

### Measured physical audit

`evidence/spacecraft-characteristics.json` is generated by
`docs/numerical-audit/spacecraft_characteristics.py` from one raw nominal result
per case. The published file uses the refined `rkf78-half` nominal of each case
(RKF78, max step 0.5 s, rtol 1e-14, 30 s output cadence), and records the sha256 of
every input. Every value in it is case metadata, a measurement of that case's own
sampled load series, or a documented function of those; the file records the
formulas and the SI constants. There is no generic spacecraft prior, "typical"
dimension or "typical" coefficient anywhere in it, and none is used to classify a
case.

```sh
python3 docs/numerical-audit/spacecraft_characteristics.py \
  --inputs <raw nominal CASE-A result> <raw nominal CASE-B result> <raw nominal CASE-C result> \
  --output docs/numerical-audit/evidence/spacecraft-characteristics.json
```

**What is authoritative.** The configuration, the panel records, the principal
moments, the declared optical and aerodynamic coefficients, the initial body rate
and the initial orbital elements are exact consequences of the versioned source
and do not depend on the integration setting. Every mean, RMS, maximum and share
below is a *descriptive statistic of one recorded integration*, not a converged
physical prediction; the convergence status of each case's final state is assessed
in [CONVERGENCE.md](CONVERGENCE.md), not here.

**Area convention.** All force areas are the harness's *one-sided* projected
areas (`n.u > 0`), which is what the drag and radiation laws integrate. The
one-sided bounds are computed exactly from the recorded panel set. The two
`silhouette` diagnostic columns equal `sum_i A_i |n_i.u|` and measure **exactly
twice** the one-sided area in all three cases (measured ratio 2.000); they count
both sides of the same body and are never used as a force area.

| Measured (6 h `rkf78-half` nominal, 30 s cadence) | CASE-A | CASE-B | CASE-C |
|---|---|---|---|
| one-sided projected area bounds [m^2] | 0.02 … 0.07 | 0 … 9.0 | 2.0 … 3.464 |
| area/mass at mean wind-facing area [m^2/kg] | 3.40e-3 | 0.588 | 0.299 |
| mean dynamic pressure [Pa] | 1.90e-7 | 2.08e-7 | 8.28e-5 |
| effective Cd, measured range (admissible [2σ, 2σ+4(1−σ)]) | 1.978 … 2.195 ([1.8, 2.2]) | 1.800 … 2.200 ([1.8, 2.2]) | 1.934 … 2.194 ([1.8, 2.2]) |
| effective Cr, measured range (admissible [1−s, 1+s+2d/3]) | 0.955 … 1.085 ([0.7, 1.43]) | 0.400 … 1.696 ([0.4, 1.70]) | 0.977 … 1.420 ([0.7, 1.43]) |
| ballistic coefficient m/(2.2·A_wind) [kg/m^2] | 133.8 | 0.774 | 1.519 |
| effective m/(Cd_eff·A_wind) [kg/m^2] | 143.4 | 0.854 | 1.665 |
| largest share of the sum of mean torque norms | gravity gradient 99.7 % | gravity gradient 95.2 % | drag 93.2 % |
| max angular acceleration [rad/s^2] | 6.25e-7 | 1.71e-6 | 3.67e-5 |
| orientation change of 1 rad under the largest sampled torque [s] | 1.11e3 | 1.08e3 | 259 |

The measured effective coefficients stay inside the analytic brackets of the
kernel's own panel laws, so no area in these runs is double counted; the recorded
non-gravitational force closes to at most 1.1e-19 N and the recorded angular
acceleration reproduces `I^-1 (tau - omega x I omega)` to at most 7.0e-21 rad/s^2.

Per case:

- **CASE-A.** The inferred cuboid is 0.1 x 0.2 x 0.3 m from the face areas, the
  recorded principal moments reproduce its uniform-bus tensor to 2.4e-4 in
  relative terms (the declared −1 mm centre-of-mass offset), and the residual
  centre-of-pressure levers equal the half-edges 0.05/0.10/0.15 m exactly. The
  configuration is gravity-gradient dominated (mean torque norm 4.30e-8 N m
  against 1.26e-10 N m of solar-radiation torque); aerodynamic torque is 1.8e-4
  of the sum of the mean norms. Spin is 0.02 rad/s about body x (period 314 s),
  and the largest measured angular acceleration would need 3.2e4 s to change that
  rate by itself, so the nominal rotation is not torque-limited over six hours.
  Optical, Cd and accommodation values remain exploratory assumptions.
- **CASE-B.** The two panel records are the two sides of one 9 m^2 membrane: the
  record sum is 18 m^2, the physical membrane area is 9 m^2, and the plate
  presents zero area edge-on. Area/mass is therefore 0.588 m^2/kg at the mean
  wind-facing area (5.876 m^2) and 0.900 m^2/kg at the largest wind-facing area
  the propagation reached (8.999 m^2) — not 1.8 m^2/kg, which is the doubled
  record sum per mass. The 0.05 m stand-off is a *common* centre-of-pressure
  offset with zero residual lever, and the source's idealised lumping
  `m L^2/24, m L^2/12` is reproduced with 0 relative residual for
  L = sqrt(9 m^2) = 3 m. The measured effective Cr reaches
  1.696 at full incidence, consistent with the example's scalar Cr = 1.7. All
  torques are enabled where the example disabled them, so the torque budget here
  (gravity gradient 95.2 % of the sum of mean norms) does not describe the
  example. The case has zero initial spin: no spin period, spin-rate change time
  or momentum change time exists, and the informative scale is the 1.08e3 s
  orientation-change scaling above.
- **CASE-C.** The face areas imply a 1.4142 m cube; the residual centre-of-pressure
  lever is 0.5 m, i.e. a 1.0 m edge, a ratio of 0.7071 — the face areas and the
  centre-of-pressure plane describe different geometries and the case is kept
  unchanged. The common centre-of-pressure offset is exactly (0, 0.1, 0) m, so
  there is **no** single 0.5 m net lever arm: each panel loads 0.5 m from the
  common point and the common point itself is 0.1 m from the centre of mass. The
  declared (2, 3, 4) kg m^2 are not the uniform tensor of the inferred cuboid
  (ratios 0.60/0.90/1.20); they are factory inputs, not a derived mass
  distribution. Drag supplies 93.2 % of the sum of the mean torque norms and the
  largest measured angular acceleration, 3.67e-5 rad/s^2, would change the
  0.00229 rad/s initial rate by itself in 62 s against a 2742 s nominal rotation
  period — a directly measured statement about why this case is torque sensitive.
  **CASE-C is unresolved at six hours**, so its long-horizon numbers above are
  descriptive, grid-dependent diagnostics and not physical truth: the final
  attitude of this case differs by 1.4903 rad (SO(3)) between the RK4 0.5 s and
  0.25 s grids, by 1.5714 rad between the RK4 0.5 s grid and the refined
  `rkf78-half` grid, and by 1.4931 rad between the production Dopri default and
  `rkf78-half`, all measured with `experiments/metrics.so3_angle` on the final
  quaternions of those raw results. The load statistics nevertheless agree to
  about 0.3 % between the Dopri default and `rkf78-half` (CASE-C mean wind-facing
  area 2.9848 vs 2.9929 m^2, drag share 0.93202 vs 0.93232), so they are stable
  as characterisations of the loads while the attitude trajectory that produces
  them is not. No torque-gain factor, Lyapunov exponent or chaos statement is
  supported by any of this; the unresolved attitude divergence is neither shown
  to be physical nor shown to be numerical here.

**Superseded statements.** The earlier unpublished draft of this evidence file is
replaced, not extended. It carried invented class references (`typical` CubeSat
ballistic-coefficient and area-to-mass ranges for both CASE-B and CASE-C), an
invented 10.685 m sail half-length for CASE-B, analytic torque magnitudes for
CASE-C that exceed the measured ones by 3x (gravity gradient: 1.152e-5 N m
drafted against 2.13e-6 N m mean and 3.85e-6 N m maximum measured), 18x (solar
radiation: 2.72e-5 against 7.43e-7 mean and 1.53e-6 maximum) and 6x (drag:
3.39e-4 against 4.09e-5 mean and 5.74e-5 maximum), CASE-C's 0.5 m lever treated
as a single net lever arm acting on a common centre of pressure, and a torque
share taken from a vector sum. All of those are gone: torque magnitudes and
shares are measured, shares are of the time-averaged magnitudes (which do not sum
to one for a vector sum), the centre-of-pressure offset and the residual lever are
reported separately, and the only external comparison retained is between the
three measured cases.

## 3. Horizons, methods and settings

Horizons: 60, 600, 1800, 3600, 7200, 10800, 21600 s, ET=0, direct SPICE
(de432s, naif0012, observer 399, Sun 10, J2000, geometric NONE, SI).

Methods: `dopri` (production), `rkf78` (independent tableau/error controller),
`rk4` (fixed binary-exact grids, no adaptive evaluation times) and `mixed_rk4`
(long-double state buffers calling the double force kernel and CSPICE).
`mixed_rk4` quantifies mixed-precision exposure only. **None of these is an
independent high-precision oracle** — they share force models, environment
queries and to a large extent the same double-precision arithmetic. Mantissa and
type of every buffer are recorded in metadata.

Convergence configurations (diagnostic only; production defaults untouched):

- Baseline = production defaults: rtol 1e-10, atols 1e-3 m / 1e-6 m/s /
  1e-12 / 1e-12, initial step 1 s, max step 15 s.
- Isolated sweeps, one knob at a time: rtol 1e-11 / 1e-12 / 1e-13; absolute
  tolerance scale 0.1 / 0.01 / 0.001; max step 15 / 5 / 1 / 0.5 / 0.25 s.
- Independent RKF78 refinements: max step 2 / 1 / 0.5 s with rtol 1e-13 / 1e-14
  and absolute tolerance scale 0.001 / 0.0001.
- Fixed RK4 grids: step 1 / 0.5 / 0.25 s.

## 4. Observables

All derived from the common per-sample sample row `[t, r3, v3, q4, omega3,
illumination, srpForce3, srpTorque3, srpWork]` plus the load channels
(drag/albedo/GG force and torque, projected area, density, inertial angular
momentum, pointing axis). μ = 3.986004418e14 m^3/s^2.

- **State**: final position [m] and velocity [m/s] in inertial J2000; their RTN
  components; Kepler point-mass osculating elements from the instantaneous
  J2-propagated state. These are valid diagnostics but not conserved under J2;
  no J2 mean-element transformation or long-term decay accuracy is claimed.
- **Attitude**: SO(3) relative rotation angle between propagated and reference
  attitude [rad], and final |omega| difference [rad/s].
- **Orbital energy**: specific orbital energy [J/kg] (magnitude/label stated
  explicitly; never confused with total J).
- **Loads**: SRP force [N], SRP body torque [N m], cumulative SRP work [J] by
  trapezoid on the sampled series, drag force [N], albedo force [N], GG torque
  [N m], projected area [m^2], density [kg/m^3]. Work and impulses are computed
  from the sampled data with the documented cadence; any cadence-refinement
  effect is measured (cadence halving) and reported with the number, not assumed away.
- **Angular momentum**: inertial angular momentum magnitude [kg m^2/s] and its
  direction (relative SO(3) angle [rad]).

## 5. Perturbations

Each parameter is perturbed about the case baseline. Signed central differences
(`-d`, `+d`); the reported slope is the central estimate, and the two one-sided
estimates are stored separately so asymmetry is visible.

State/attitude perturbations (deterministic, physically tiny, exploratory — *not*
claimed to represent any real initial-condition knowledge):

| Parameter | Unit | Deltas |
|---|---|---|
| Position, radial | m | ±0.1, ±1, ±10 |
| Velocity, along-track | m/s | ±1e-4, ±1e-3, ±1e-2 |
| Attitude about orbital normal | rad | ±1e-6, ±1e-5, ±1e-4 |
| Body rate, spin axis | rad/s | ±1e-6, ±1e-5, ±1e-4 (additive on CASE-B's zero spin) |

Physical parameter perturbations (multiplicative unless stated):

| Parameter | Deltas |
|---|---|
| Density scale | ×(1 ± 0.01, 0.05, 0.1) |
| Area scale | ×(1 ± 0.01, 0.05, 0.1) |
| Mass | ×(1 ± 0.01, 0.05, 0.1) |
| Inertia (all three diagonals) | ×(1 ± 0.01, 0.05, 0.1) |
| CP tangential shift | ±0.0001, ±0.001, ±0.005 m |
| Aerodynamic accommodation σ | ±0.01, ±0.03, ±0.05 |
| Cd | relative ×(1 ± 0.01, 0.05, 0.1) |
| Spin scale | ×(1 ± 0.01, 0.05, 0.1) |
| Specular / diffuse coefficients | absolute ±0.01, ±0.03, ±0.05 |

For a multiplicative span the harness receives the scale factor (1 ± delta) and the
derivative divides by the declared **relative** delta, so gradient units are per
unit relative change. For example a density-scale gradient is reported per unit
of fractional density change, not per kg/m³. Additive spans (position, velocity,
attitude, body rate, CP, σ, optics) divide by their absolute delta.

All declared optical spans remain inside specular/diffuse positivity and
sum≤1 constraints. The kernel rejects invalid material inputs explicitly;
the study never silently clamps them or invents a skipped-result measurement.

**Contribution switches** (SRP, albedo, drag, GG on/off) are *diagnostic
decompositions*, not uncertainties. They are reported in a separate block and are
excluded from slope tables, because "how much does X contribute" and "how much do I
not know about X" are different questions.

## 6. Numerical envelope, slopes and the resolution rule

At each horizon/observable report two predeclared envelopes: the maximum
pairwise spread of ALL settings (including the production default) and a
selected-fine numerical envelope from tight Dopri, RKF78 refinements,
finest fixed RK4, same-grid mixed arithmetic and output-cadence refinement.
The fine envelope is multiplied by two as an empirical safety margin.
The coarse spread measures default-setting exposure, not the uncertainty of a
qualified finer result. Neither envelope is a certified error bound.
Convergence is assessed across refinements at the SAME horizon, not by demanding
errors decrease with propagation time. A fine regime requires shrinking successive
differences or an independently evidenced roundoff plateau, cross-method agreement,
and compliance with the chosen provisional decision budget. This rule applies to
all horizons/cases, including six hours; there is no automatic long-horizon veto.

A perturbation `d` on parameter `p` is **resolved** at horizon `h` when both hold:

1. `|R(p, +d)| ≥ 10 · U(h)` for the observable `R` (physical response is at least
   ten times the empirical numerical envelope), and
2. the central slope is **stable**: central slopes computed at the refined methods
   and at the smaller delta agree within 20 % relative. Where the relative
   difference is ill-conditioned (`|response|` near zero or slope near zero), the
   normalisation condition number is reported and the cell is marked
   indeterminate rather than resolved.

Anything failing (1) or (2) is reported as **unresolved at that horizon**, which is
a legitimate and expected result. In particular, agreement between a coarse and a
fine configuration is *not* by itself a resolution certificate — nonmonotonic
refinement is exactly the behaviour already documented for CASE-C.

Where the response is dominated by intermittently switched channels
(attitude-dependent drag or radiation near incidence/eclipse transitions), `U(h)` includes
those samples; no outlier removal is performed. Any exclusion is declared with
its reason and its effect on the verdict.

## 7. Global screening

Global parameters receive a controlled screening pass with seed20261009 only
after local sensitivity is trustworthy at the selected horizon. Use12 samples
of the three locally highest-ranked resolved parameters in their declared spans,
check refined methods, and report rejected/unresolved samples explicitly.
Screening is exploratory, not a probability distribution or Sobol decomposition.

## 8. What this study does not claim

- No accuracy requirement of any mission. No CubeSat/nadir/in-plane pointing
  tolerance is invented here.
- No probability distribution, 1σ/3σ confidence or uncertainty budget for any
  physical parameter. Deltas are declared exploration spans.
- No certified global error bound. `U(h)` is empirical and configuration-relative.
- No caching, ephemeris-approximation, tolerance-relaxation or performance claim.
- Historical benchmark results and OPT-001 thresholds are untouched.

## 9. Executed analysis details

The pre-sweep v1.1 amendment defines the signed-pair response as
`max(|J+−J0|,|J−−J0|)` and requires a neighboring-size check even for the
smallest tested magnitude. The fine state envelope includes both perturbed
trajectories; sampled scalar responses additionally use actual 15 s cadence
qualification in each sign on an identical RK4 0.25 s RHS schedule.
The independent cadence study uses 30/15/7.5/3.75 s samples and records
bitwise equality of common-time states. Local cubic quadrature diagnostics
are not an independent refinement oracle.

Uniform mass/area/inertia/spin derivatives are stored per fraction and
converted for qualified reporting to kg, physical area, inertia trace at
fixed ratios, and rate magnitude at fixed direction. Relative elasticities
require a nonzero physical parameter origin and an observable baseline at
least 10U from zero. CASE-B's zero multiplicative spin is degenerate;
its additive body-rate perturbation is explicitly body-z.

The controlled screening uses terminal point-mass SMA as a common
observable, the three largest resolved tested-span responses, and 12
stratified draws with seed 20261009. CASE-A/B use six hours and CASE-C
its independently converged one-hour regime. Ranges remain exploratory;
differences smaller than numerical envelopes do not establish rank order.

Additional source-isolation evidence separately removes SRP force/torque,
SRP torque only, and GG in all cases. CASE-C continuation preserves each
variant's options in both nominal and seeded runs, includes correct
0/0.05/0.1/0.2/−0.1 m common CP offsets, Izz factors 0.9/1.1 (physically
valid inertia triangles), and retains translational forces when removing
individual source torques. No production equation/default is changed.

## 10. Post-correction evidence identity

AUDIT-JSON-001 (invalid `steps_fields` JSON in `--method trace`) was found
while reviewing the first full sweeps. It changes only the emitted column-name
array, never states, loads or event counters, but a diagnostic that a strict
consumer cannot parse is not trustworthy output. The published v1.1 evidence is
therefore measured entirely by the corrected binary, and every driver parses
harness stdout with plain `json.loads`. No tolerant recovery path,
premeasured-run reuse or pre-fix measurement enters the published files; their
`binary_sha256` identifies the corrected executable. Historical OPT-001 and
pre-audit evidence remain byte-identical and unchanged.
