# Independent v0.1 reference provenance

## Inputs and equations

Packed states are SI `[r(3), v(3), q(wxyz), omega_body(3)]`, Earth-centred J2000, Hamilton scalar-first active body-to-J2000 rotations. `scenarios/*.json` contains all numerical inputs, selected force/torque laws, exact requested times and the qualified C++ solver configuration. The immutable CASE-A/B files supply spacecraft/input data; no C++ force or integrator is called to generate the reference.

`generators/physics.py` independently implements central/J2 gravity, Euler/quaternion dynamics, gravity-gradient torque, optical panel momentum transfer, co-rotating flat-plate/cold-beam Sentman drag, exponential density and radial-beam albedo. The density knots and constants are shared model specifications, not independent atmospheric observations. Eclipse illumination is independent adaptive quadrature of visible solar-disk strips, not a copy of the production stabilized circular-segment series.

The full matched environmental cases include SRP, **conical eclipses**, cold-beam Sentman, radial albedo, gravity-gradient and J2. Their explicit constant J2000 Sun is a deterministic matched equation input, not a claim about real planetary ephemerides. Historical SPICE numerical-audit and MT-002 results remain separate.

## Independent numerical and analytical evidence

- SciPy DOP853: relative-tolerance ladder 1e-10 / 1e-12 / 1e-13, maximum steps 30 / 10 / 2.5 s, field-wise absolute tolerances scaled 10 / 0.1 / 0.01. Final refinement changes are retained per fixture.
- Analytical elliptic Kepler state histories, uniform spherical-body rotations and symmetric-top body-rate solutions check the reference itself.
- Two-body energy/full inertial momentum, J2 energy/axial momentum, and torque-free inertial momentum/rotational energy invariants are interpreted in the correct frames. Body-frame angular-momentum components are **not** assumed constant; gravity-gradient dynamics is **not** assumed dissipative.
- Native Tudat uses explicitly configured mu, radius and normalized C20 = -J2/sqrt(5), constant zero Earth ephemeris and zero-rate identity Earth rotation. Its gravity axis is therefore the same fixed inertial +Z axis, not a default Earth-fixed/precessing pole. Native translational and rotational propagators are combined into all 13 states, with full inertia tensors and native gravity-gradient torque where selected.
- Native Tudat RKDP 8(7) higher-order output is independently refined at fixed steps 5 / 2.5 s. Requested times are actual saved grid points: no velocity finite-difference fallback, interpolation ambiguity or extrapolated terminal point. Eight native scenarios and their refinement results are retained under `golden/tudat/`.

Tudat's default atmosphere/panel/thermal/radiation laws are not substituted for the project's cold-beam or radial-beam equations. Those benchmark entries are explicitly unmatched, not counted as independent native Tudat validation.

## Versions and identity

Generation was executed with Python 3.12, NumPy 2.5.3 and SciPy 1.18.1. The separate Tudat conda environment resolves distribution `tudatpy 0.9.0`, native `tudat 2.14.0`, Python 3.10 and Boost 1.85. Its module `_version.__version__` reports **0.8.0**; both distribution and runtime identifiers must be retained rather than relabelling that discrepancy. Actual environment package records are captured in the external qualification evidence.

Each golden manifest pins scenario, trajectory and pointwise SHA-256 values. Per-fixture generation provenance is retained when an explicit subset is regenerated; ordinary tests verify hashes and the complete scenario matrix before execution. `acceptance.json` was committed as `6e8b5df` before the first full candidate comparison.

The initial CASE-B C++ configuration (1e-11 relative tolerance, 5 s maximum step) failed the frozen 0.01-degree / 1e-7-rad/s limits. A focused existing-solver refinement to 1e-12 / 2 s, and an independent confirmation at 1e-13 / 1 s, qualified the revised scenario configuration without changing either acceptance limit or any production default. The original failure and refinement results remain in external evidence.

## Explicit regeneration

Use an isolated Python environment with NumPy/SciPy; no Python dependency is introduced into the standalone production C++ build.

```sh
python3 tests/reference/generators/export_scenarios.py
python3 tests/reference/generators/generate_fixtures.py
# Only when deliberately replacing a selected fixture, preserving other records:
python3 tests/reference/generators/generate_fixtures.py --only coupled_case_b

# Separately installed, version-pinned Tudat interpreter:
/path/to/tudat-env/bin/python tests/reference/generators/qualify_tudat.py \
  --out tests/reference/golden/tudat
```

Normal CI runs `test_reference_regression.py` against committed data. It imports neither SciPy nor Tudat and never regenerates its expected results. Six-hour acceptance and performance are separately runnable qualification operations. The six-hour fully coupled CASE-C stress solution remains an explicitly unresolved, non-blocking scientific limitation; this suite qualifies only its shorter matched-environment horizon.
