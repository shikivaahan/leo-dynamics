# Frozen v0.1 acceptance criteria

`acceptance.json` was written after independent reference convergence and before the C++ trajectory acceptance comparison. Its SHA-256 is recorded by the acceptance runner. No threshold is selected from a failing engine result.

## Independent basis

The three DOP853 runs use relative tolerances 1e-10 / 1e-12 / 1e-13, maximum steps 30 / 10 / 2.5 s and successively tighter field-specific SI absolute tolerances. The final change, not disagreement with LEO, sets the reference uncertainty scale.

| Reference domain | Largest final refinement change |
|---|---:|
| Six-hour two-body | 2.17e-6 m; 2.24e-9 m/s |
| Six-hour J2 | 2.38e-6 m; 2.46e-9 m/s |
| Torque-free attitude | 9.43e-13 rad; 2.64e-16 rad/s |
| Gravity-gradient attitude | 2.11e-13 rad; 2.92e-16 rad/s |
| Full six-hour CASE-A/B, matched constant-Sun environment | 1.32e-5 m; 1.28e-8 m/s; 1.05e-6 rad; 1.05e-9 rad/s |
| CASE-C one-hour matched stress horizon | 2.85e-8 m; 4.15e-11 m/s; 2.61e-9 rad; 8.54e-12 rad/s |

Reference refinement must be below 5% of the scenario's acceptance limit. Otherwise that reference is not an acceptance oracle; the generator must be independently refined before comparing the engine. The final refinement change is observed numerical evidence, not a formal error bound.

## Engineering limits

- Gravity-only: 0.05 m / 5e-5 m/s over six hours. This is sub-metre precision, still orders above observed reference rounding/convergence differences; it is not arbitrary micrometre certification.
- Analytically smooth rigid-body cases: 1e-8 rad / 1e-9 rad/s. Analytical uniform rotation and symmetric-top rate solutions and inertial angular-momentum/energy invariants support the tight check.
- Gravity-gradient: 1e-7 rad / 1e-9 rad/s, with independent torque equations and native Tudat comparison.
- Environmental and full CASE-A/B: 0.1 m / 1e-4 m/s / 0.01 degrees / 1e-7 rad/s. Existing numerical audit supports 0.1 m / 0.01 degree conditional allowances on CASE-A/B at six hours; the independent reference here is comfortably below them. Rate and velocity limits are absolute engineering errors, at least 90× the observed final reference changes, rather than equal decimal tolerances on incompatible units.
- Pointwise loads: relative 5e-11 with absolute floors of 5e-15 N / 5e-16 N m, and explicit acceleration/density/illumination floors. Near-zero loads are not judged by unstable relative percentages. This is much tighter than trajectory agreement, as expected for matched equations at identical physical inputs.

Common-time position, velocity, shortest SO(3) quaternion angle and body-rate errors are checked across the entire trajectory, not only at the endpoint. Quaternion signs are equivalent. Singular orbital angles are not compared as ordinary scalars.

CASE-C's fully coupled six-hour unresolved result remains a non-blocking scientific limitation. This suite qualifies its one-hour deterministic matched-environment horizon; it does not claim to certify the historical six-hour SPICE stress trajectory or close the original audit finding.

## Scope of matched environmental inputs

CASE-A/B spacecraft and initial geometry are the existing immutable definitions. The full model includes J2, gravity-gradient, SRP, conical eclipse, cold-beam Sentman and radial-beam albedo, with forces and torques selected. The Sun position is explicitly constant in J2000 for both implementations. It is not represented as a surrogate for real DE432s ephemerides. Existing real-ephemeris validation and MT-002 contention evidence remains separate.
