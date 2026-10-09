# Numerical defect ledger

Pre-audit source: `6a8f92a9721441623980bd47905cd22a3367f296`.
Original Stage 3 reference: `48961a2137f87bb4a1daa80ae880ecc812dad5da`.
Historical OPT-001 files/thresholds remain unchanged; their hashes and the source
archive identity are in [evidence/baseline.json](evidence/baseline.json).

## Confirmed and fixed

| ID | Severity / impact | Root cause and correction | Independent numerical evidence | Fix commit |
|---|---|---|---|---|
| NUM-001 | High numerical stability risk in conical penumbra | Large Earth-sector/lens terms cancelled while computing a small solar-area fraction; chord numerators also subtracted near-equal squared radii. Factored contact offsets, stable circular-segment series, and direct illuminated complement preserve the same angular-disk physical model. | Near internal contact: production illumination 2.185343574e-5 versus 80-digit Cartesian quadrature 1.701674513e-15. Corrected sampled maximum absolute error <1e-12; initial combined lens/centre-angle probe measured 4.186e-14. | `c196bdd0ecc75f847b0de6b186579ecc10d9f03d` |
| NUM-002 | Medium; loss of small centre angles near shadow apex | `acos(dot(unit directions))` lost separation when the dot rounded to one. Use `atan2(norm(cross),dot)` without changing geometry/frame conventions. | Nominal Earth/Sun radii near umbra apex, 1e-8 rad centre separation: old illumination 4.441e-16 versus independent quadrature 1.367674103e-6. New regression error <1e-12. | `d22507fe6623279592f3ccfc7709356e66a86ea6` |
| NUM-003 | Low; conditioning immediately outside a body's surface | `asin(R/r)` rounded the ratio before resolving the small altitude gap. Use an atan2 radius/tangent construction with `(r-R)/r` factored before division; avoid overflow in `r+R`. | One representable ULP above Earth radius: old absolute illumination error 2.703763720e-7; corrected error 1.193489751e-15 versus 80-digit Cartesian quadrature. Not the cause of the 400 km workload. | `aea356e27154f43b6c72bc4201ffc8a815d711de` |
| NUM-004 | Medium public coefficient API; no propagated-force effect | Sentman lift subtracted two O(1) normal/tangential momentum terms near full accommodation. Evaluate the algebraically factored lift `4(1-sigma) sin²(alpha) cos(alpha)`. The stable original drag sum and force implementation are unchanged. | Twelve exact-binary64 input fixtures from 60-digit incident/outgoing molecular momentum vectors: worst lift relative error 0.1413993 before, 2.140901e-16 after. Fully accommodated cold re-emission has exactly zero lift. | `bf11b1bb14f66d36b4131900f1ae5adf46725247` |
| NUM-005 | Medium API consistency; conditioning can amplify it | Near-symmetric matrices were accepted but `multiply` used the raw tensor while LLT `solve` used its lower-triangle symmetric interpretation. Canonicalise off-diagonal pairs with overflow-safe `std::midpoint`, preserve diagonals, factor that same SPD tensor. | Well-conditioned accepted asymmetry: inverse-operator relative residual 6.375299e-13 → 9.272487e-17; Euler residual 1.747083e-15 → 3.114134e-19. Physical high-aspect inertia: inverse residual 6.547546e-6 → 1.871977e-9, with remaining conditioning/roundoff. | `84de8919a86cb42845f649fac7d25789031cf2f6` |

No default integration tolerance, force-law assumption or cache code changed.
NUM-005 is a bitwise no-op for the benchmark's exact diagonal inertia. Its
canonical tensor must still be positive definite; near-singular/asymmetric data
cannot be promised the same acceptance as an inconsistent old lower-triangle
interpretation. The fix does not eliminate general ill-conditioning.

### Sensitivity-tool defects (not production-kernel defects)

`8b55f22` corrects four independently reproduced diagnostic identities:
the relative SO(3) quaternion must select the shortest arc (160° versus −160°
is 40°, not 320°); tiny vector angles use atan2 rather than acos; each
trajectory's angular momentum uses its own inertia; illumination-weighted
area uses the actual illumination integral. A response, not a derivative
with different units, is compared to numerical uncertainty.

The additional scalar-error regression has signed differences −1, −3, −2 m².
The old reported maximum was −1 m²; the corrected maximum absolute error is
3 m² while the terminal difference remains −2 m². All six independent
identities pass in `tests/sensitivity_metrics_tests.py`; CTest registers them
as `audit_metric_identities` when Python is available. The duplicate
mislabelled specific-energy-rate field and an always-undefined,
dimensionally inconsistent work/energy ratio were removed; the correctly
named `specific_energy_rate_w_kg` remains.

Unpublished sensitivity drafts also had a derivative/response unit mismatch,
discarded scalar derivatives, unmatched CASE-C variant seed options and
unsupported geometric torque estimates. They are superseded by the v1.1
amendment and newly measured evidence. These errors do not demonstrate a
defect in the propagated production equations or resolve NA-OPEN-001.


### Locations, reproductions and regressions

**NUM-001/002/003:** `src/kernel/radiation.cpp`, internal
`disk_overlap_illumination`, `segment_area`, `apparent_radius`, and public
`illumination_factor`. Focused tests in `tests/numerical_radiation_tests.cpp`:
contact/intermediate Cartesian fixtures, physical shadow-apex small angle, and
representable near-surface altitude. Sector series follows
`theta - sin(theta)cos(theta)` analytically; no fitted smoothing or arbitrary
clamp is introduced. Final fraction bounds and a nonnegative Heron radicand
handle only mathematically bounded quantities/roundoff.

The reference integrates vertical intersection widths of two Cartesian circles
at 80 decimal digits, with an endpoint-regularising sin² substitution. It does
**not** rerun the sector/lens or production segment formula. Actual binary64
Cartesian input values are converted exactly to multiprecision. The original
angular-plane, uniform-brightness approximation remains the model under test.

Reproduce: build `eclipse_probe` against the original/remediated source using
[README commands](README.md); run `eclipse_reference.py --probe BINARY --output NEW.json`.
Boundary evidence: [eclipse-reference.json](evidence/eclipse-reference.json),
[eclipse-before-after.json](evidence/eclipse-before-after.json),
[angle-physical-reference.json](evidence/angle-physical-reference.json),
[apparent-radius-before-after.json](evidence/apparent-radius-before-after.json).
Actual failing-before/passing-after Catch outputs are retained for each fix.

**NUM-004:** `src/kernel/aerodynamics.cpp::sentman_coefficients`, documentation in
`include/leo/kernel/aerodynamics.hpp`. Regression
`Sentman small lift agrees with independent high-precision momentum flux` in
`tests/numerical_environment_tests.cpp`. Cold specular molecules reverse their
normal velocity; fully accommodated cold molecules have zero mean outgoing
velocity. Incoming/outgoing momentum flux projected onto lift supplies the
independent reference. Exact input floats, not their shortest decimal strings,
are essential near `sigma=1`. Generator: `environment_reference.py`; actual C++
measurements: [sentman-before-after.json](evidence/sentman-before-after.json).
The propagated Sentman force already used a reduced exchange vector and never
called this coefficient API, so this fix cannot explain six-hour divergence.

**NUM-005:** `src/kernel/spacecraft.cpp::Inertia::Inertia`, contract documented in
`include/leo/kernel/spacecraft.hpp`. Regression
`Accepted roundoff asymmetry has one self-adjoint inertia operator`, plus large/
subnormal preservation, in `tests/numerical_dynamics_tests.cpp`. Exact repro:

```text
I = [10, 0.25, 0.05;
     0.25+9.9e-12, 8, 0.10;
     0.05, 0.10, 6]
omega = (0.01, 0.02, 0.03)
```

It lies inside the existing symmetry tolerance. Independent identities are
`I(I^-1 v)=v`, self-adjointness and `I omega_dot + omega × I omega = 0`, not a copy
of LLT's implementation. [inertia-before-after.json](evidence/inertia-before-after.json)
contains the actual C++ repro source and measured residuals. Diagonals and equal
pairs are preserved even at subnormal/large finite magnitudes; naïvely averaging
`0.5*(I+Iᵀ)` would introduce overflow, so it is not the implemented correction.

## Six-hour anomaly: supported localisation, unresolved global accuracy

The original direct 30 µm experiment reproduced exactly: 0.110523 m /
0.0133065 rad final differences. Cached 300 s also reproduced the historical
0.107147 m / 0.0125414 rad / 0.164505 J failure. See
[preaudit-reproduction.json](evidence/preaudit-reproduction.json).

The diagnostic Dopri trace is checked bit-for-bit against production output;
it is **not** used as an accuracy oracle. Original adaptive error values first
differ at t=26.925389 s, while illumination is exactly one; the next step size
differs at t=30 s. First output-state difference is at 60 s and 1 µrad attitude
difference at 7080 s. First penumbral trace entries occur around 474 s, later
than the step-sequence split. See [preaudit-study.json](evidence/preaudit-study.json).

Model switches show the effect persists without eclipse (1.466299 rad final
separation), while disabling environmental torques/gravity-gradient gives
bitwise equal state trajectories for the Sun perturbation. Pure drag and pure
GG do not depend on the perturbed Sun in their force equations; their repeated
runs remain bitwise identical.

After all fixes, independently adaptive direct 30 µm runs still differ by
0.161943 m / 0.0197638 rad. At prescribed fixed RK4 evaluation times the same
perturbation instead produces final separations of 2.28e-9 m / 2.96e-10 rad
(1 s), 1.68e-9 m / 1.60e-10 rad (0.5 s), and 1.61e-7 m / 3.15e-8 rad (0.25 s).
Same-state/time replay gives maximum SRP/illumination differences in the compact
evidence; those must not be confused with loads evaluated on already-diverged
trajectories. Thus the original adaptive comparison is **not evidence that
physical 30 µm Sun uncertainty intrinsically causes centimetre-scale trajectory
or substantial attitude changes**. Different numerical evaluation/acceptance histories matter.

A separate physical initial-attitude perturbation on common fixed grids shows
large finite-difference gains, confirming strong conditioning of this forced
attitude workload. Gains themselves differ between grids and are not certified
Lyapunov exponents or proof of chaos. No smoothing surrogate or new physics was
used for the final assessment. [CONVERGENCE.md](CONVERGENCE.md) records why neither
the default nor the refined six-hour reference is currently globally certified.

**NA-OPEN-001 remains open pending the versioned physical sensitivity study.**
The historical six-hour data above is preserved. The new study separates
same-grid physical responses from adaptive and cross-method numerical spread,
using CASE-A/B and the unchanged CASE-C. Classification B/C/D may coexist;
an implementation defect is not inferred from disagreement, nor excluded solely
because a torque-free control converges.

Draft amplification interpretations were withdrawn after independent checks
found analysis-tool errors: SO(3) distances above pi, gradients compared with
same-observable uncertainty despite incompatible units, and seeded runs that
did not retain the matching parameter variant. These are new experimental-tool
defects, not evidence of a defect in the physical kernel. Their regressions are
in `tests/sensitivity_metrics_tests.py`; corrected measurements will replace
only this task's unpublished draft outputs, not historical audit or OPT-001 data.

## Investigated hypotheses and model limitations

- **Dopri tableau/FSAL/rejection/projection/controller bug:** not confirmed.
  Actual Boost stage definitions and explicit derivative overload were reviewed;
  rejection leaves accepted buffers untouched and accepted projection refreshes
  derivatives. Exact exponential and arbitrary-axis spin regressions pass.
- **Gravity/J2 sign/frame/scale defect:** not found in the audited range;
  independent potential gradients, Kepler invariants and long-arc tests pass.
- **Quaternion convention/frame defect:** not found; independent Rodrigues and
  exact spin references, large representatives and tiny angles pass.
- **Atmospheric discontinuity as the six-hour cause:** not demonstrated. The
  table is joined/log-linear and only C0 at its knots; dense numerical tests and
  geometric-mean references pass. C0 table/face laws remain documented model
  features, not bugs that justify changing physics.
- **Drag force law, lever arms or covariance:** independent dissipation,
  lever-arm-shift and scene-rotation properties pass with fixed seed 20251009.
  This does not certify every geometry or full global propagation accuracy.
- **SPICE observer/frame/units/clock bug:** not found. Direct raw CSPICE comparison,
  exact repeated queries and SI derivative checks pass. A 1 µm X perturbation
  rounds to zero on this workload; one X ULP is about 3.8 µm. Absolute ET=1e8 has
  ~1.49e-8 s resolution, so adding 1 ns can be lost: an explicit binary64
  representation limit, not evidence of a wrong TDB/UTC conversion.
- **Unvalidated surrogate chaos claims:** discarded. A supplementary independent
  replica omitted `3 mu/r³` from GG torque and used half the proper quaternion
  angle. It could not support a physical chaos/step-threshold conclusion and
  was removed rather than committed as evidence. Only actual library-backed
  model-switch/common-time experiments are used here.

Physical approximations left intact: Newtonian/J2 Earth field with fixed pole,
point-mass translational gravity plus GG torque (no finite-size reciprocal
quadrupole force), cold-beam Sentman with one COM wind (no panel rotational
velocity/thermal re-emission), spherical tabulated atmosphere, radial albedo,
angular-plane eclipse disks with uniform brightness. These are not equivalent
to a complete closed conservative orbit/attitude model. Extreme unresolved
angular geometry and ill-conditioned inertia still require application-level
conditioning checks. This audit does not claim every possible defect eliminated.
