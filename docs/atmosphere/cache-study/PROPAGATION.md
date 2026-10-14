# Coupled propagation — protocol, proof, and pending measurements

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

An optional independent RKF7(8) pair (rtol 1e-12, max step 5 s) for both the reference and the
candidate separates integrator error from candidate error. A large integrator-only difference is a
property of the case, reported as such, never attributed to the cache. CASE-C sensitivity is reported
honestly and never presented as a cache result.

## Disabled-cache equivalence — measured

The cache-disabled control candidate reproduces the existing driver's own six-hour Dopri5 output
**bit for bit**: all 13 packed state components and the output label at every requested sample,
compared against `experiment::execute()` for CASE-A, CASE-B and CASE-C on both models
(`evidence/propagation-summary.json`).

| Model | CASE-A | CASE-B | CASE-C |
|---|---|---|---|
| NRLMSISE-00 | identical | identical | identical |
| JB2008 | identical | identical | identical |

This is the strongest available check that the experimental harness itself does not perturb the
production numerical path. It certifies wrapper-versus-current-library equivalence on the inputs this
process read; it is not the frozen `direct-v2` evidence checksum, which additionally depends on the
two pinned inputs now missing.

## Pending measurements

Six-hour and 24-hour coupled errors, RKF7(8) separation, CASE-C convergence behaviour, and initial-
condition sensitivity on pinned inputs are blocked with the rest of the primary study. The tasks are
recorded and resumable; no partial result is presented as complete.