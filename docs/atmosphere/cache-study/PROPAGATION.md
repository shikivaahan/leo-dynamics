# Coupled propagation — protocol and measured results

## Two complementary comparisons

**Fixed state.** Both densities are produced at the identical epoch and identical spacecraft state
and enter identical `EnvironmentSnapshot` values; the study asserts the snapshots are bitwise equal
outside the density field before comparing drag force, drag torque, total acceleration and angular
acceleration. Density error measured this way is reconstruction error alone.

**Free-running.** Candidate and authoritative runs are independent Dopri5 integrations from the same
initial state, compared at identical output epochs: position magnitude and RTN components, velocity,
orbital elements, specific and matched energy, specific angular momentum, quaternion orientation
angle, body rate, drag force and torque, cumulative drag work, density along each run's own path, and
RHS-call counts. Adaptive integrators are not expected to take identical steps after a density change,
so step counts are reported and never forced to match. Dopri5 attempt/rejection counters are published
as `null`: `leo::propagate` does not expose them, and no integrator behaviour was changed to obtain
them.

## Separation of error sources

An independent RKF7(8) pair (rtol 1e-12, max step 5 s) for both the reference and the candidate
separates integrator error from candidate error. A large integrator-only difference is a property of
the case, reported as such, never attributed to the cache. CASE-C convergence behaviour is reported
as measured and never presented as a cache result.

## Perturbations

Every case was propagated at nominal, small, moderate and large initial-condition perturbations,
derived from the master seed and the member index alone so each is reproducible without replaying the
ensemble. The realised rotation angle is read back off the perturbed state and published, so the
recorded perturbation is provably the one the integrator received. Member 0 is always unperturbed.

## Measured results

Worst case over CASE-A, CASE-B and CASE-C; 6 h and 24 h windows; all perturbation levels; both models.
Epoch 2003-10-29T12:00, output cadence 30 s, Dopri5 at rtol 1e-10 / max step 15 s.

| Candidate | Runs | Worst position error | Worst velocity error | Worst density error |
|---|---:|---:|---:|---:|
| A exact memoisation (JB2008) | 13 | 0 m | 0 m/s | 0 (bitwise) |
| A exact memoisation (NRLMSISE-00) | 13 | 0 m | 0 m/s | 0 (bitwise) |
| E control (both models) | 39 | 0 m | 0 m/s | 0 (bitwise) |
| B nearest-neighbour (JB2008) | 52 | 948.1 m | 1.063 m/s | 0.1512 % |
| B nearest-neighbour (NRLMSISE-00) | 52 | 926.4 m | 1.104 m/s | 0.1827 % |
| C log-density interpolation (JB2008) | 26 | 439.1 m | 0.532 m/s | 0.0126 % |
| C log-density interpolation (NRLMSISE-00) | 26 | 2173.6 m | 2.521 m/s | 0.2277 % |

Candidate D was never propagated: it never cleared accuracy screening.

## What these numbers mean

**Exact memoisation is bitwise.** Zero position, velocity and density error across every case, window
and perturbation. Its contract — reproduce the native value exactly — holds under free-running
propagation, not merely at fixed states.

**Sub-percent density error accumulates to kilometres over six hours.** The relationship is strongly
nonlinear in the density error: C on JB2008 has a 0.0126 % worst density error and still reaches
439 m, while B on NRLMSISE-00 has a 0.183 % error and reaches 926 m. Error accumulation depends on
where along the trajectory the density error occurs and how long it persists, not on its magnitude
alone.

**The largest single contributor is C on NRLMSISE-00** at 2174 m over six hours, from a
configuration whose screening worst case was 0.2966 %. This is the clearest illustration that a
density-error percentage is not a statement about trajectory accuracy: the same configuration's
*typical* error is far smaller than its worst case, and the worst case lands where it lands.

**Attitude angles of ~π in the worst-case columns are the quaternion sign convention**, not a physical
rotation; the published orientation-angle metric is the angle between orientations, which is
computed consistently across both runs.

## Disabled-cache equivalence — measured

The cache-disabled control candidate reproduces the existing driver's own six-hour Dopri5 output
**bit for bit**: all packed state components and the output label at every requested sample, compared
against `experiment::execute()` for CASE-A, CASE-B and CASE-C on both models. This certifies that the
experimental harness does not perturb the production numerical path on the inputs this process read.

## Native-domain coverage

All six model/case pairs complete the six-hour window at the study's own tolerances (rtol 1e-10,
max step 15 s), verified by `docs/atmosphere/cache-study/coverage.py` and recorded in
`study-final/case-availability.json`. An earlier apparent excursion above NRLMSISE-00's 1000 km
ceiling came from the study's own auxiliary packet generator, not from any trajectory; the trajectory
altitude range for CASE-A is 793–807 km. The defect is fixed and the gate remains so the claim is
measured rather than assumed.

## Scope

One epoch for coupled work: 2003-10-29T12:00. Sensitivity uses five archived regimes; coupled
propagation and performance use this single epoch because a 24-hour run at the study's tolerances
across every shortlisted configuration was already the dominant cost. Case diversity is covered by
CASE-A/B/C, but epoch diversity is not covered in the coupled results.