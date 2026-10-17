# Proposed v0.1 Python interface — design only

No bindings, Python package, build backend, wheels or release are implemented in this phase.
The existing C++ batch API and numerical path remain authoritative.

## Public objects

| Proposed Python object | Existing C++ contract |
|---|---|
| `Earth(mu, equatorial_radius, j2)` | SI constants; fixed J2000 +Z J2 symmetry axis, not a moving ITRF pole |
| `State(position, velocity, quaternion, angular_velocity)` | Earth-centred J2000 metres / metres per second; Hamilton scalar-first active body-to-J2000 quaternion; body-frame radians per second |
| `Inertia(tensor)`, `Surface(...)`, `Spacecraft(...)` | Positive-definite symmetric 3×3 tensor, positive areas/mass, unit body normals, body-frame centres of pressure and validated material coefficients |
| `Models(...)` | Independent force/torque switches for SRP, drag and simplified albedo; gravity-gradient torque; explicit drag law and eclipse model |
| `IntegrationOptions(...)` | Existing relative and field-wise absolute tolerances, initial/max step and attempt limit; no new solver |
| `Environment(...)` | Explicit epoch and covering ephemeris where solar data are required; explicit exponential atmospheric acquisition where drag is selected |
| `Trajectory` | Owned epoch, ordered exact requested output times and complete 13-state samples |
| `propagate(...)` | Existing `leo::propagate`, no automatic backend or execution-strategy switching |
| `propagate_batch(..., workers=1)` | Existing `leo::propagate_batch`; shared epoch/options/schedule; ordered independent trajectories; one worker runs inline |
| `analyze(...)`, `export_csv(...)` | Existing orbital/load/attitude/work analysis and validated CSV export |

NRLMSISE-00 and JB2008 are v0.2 features. Keep their optional C++ code/tests; do not expose or acquire them as implicit v0.1 package dependencies.

## NumPy contracts

All numerical arrays are native-endian `float64`, with finite values required at entry. Validate shape and strides before taking references. Wrong shapes/dtypes are errors or explicit conversions, never silently reinterpreted memory.

| Data | Shape and order |
|---|---|
| Single packed initial state | `(13,)`: `r[0:3], v[3:6], q[w,x,y,z][6:10], omega[10:13]` |
| Batch initial states | `(B, 13)` |
| Requested elapsed seconds | `(N,)`, starts at exactly zero, finite and strictly increasing |
| Trajectory states | `(N, 13)` |
| Batch states | `(B, N, 13)`, input order retained |
| Inertia tensor | `(3, 3)`, kg m² |
| Surface normal / CP | `(3,)`, body axes; CP in metres |

Orientation error is the shortest SO(3) rotation, invariant under `q -> -q`; do not compare quaternion coefficients or singular orbital angles as ordinary scalar physical errors.

## Ownership and lifetimes

- Copy small initial inputs/options into an owned validated propagation request before releasing the GIL. Caller mutation must not race a native run. A zero-copy input API would require a different explicit immutability contract and is not proposed.
- Return an owning `Trajectory` with read-only NumPy views tied to that owner's lifetime, or explicitly return independent NumPy copies. Never expose a view into a temporary `std::vector`, an accepted-stage scratch buffer or a destroyed trajectory.
- Spacecraft surfaces and inertia must remain alive and immutable for every borrowed native reference. Python wrappers must keep strong references to environment/ephemeris owners until propagation and all worker joins finish.
- Avoid resizing an exported vector. Retain the complete requested output, not a lazy re-propagation iterator.

## GIL and batch semantics

Validate Python inputs and construct native owners while holding the GIL; release it around pure native propagation and batch execution. Reacquire it to construct Python results or translate errors. Do not call arbitrary Python RHS or environment callbacks from native batch workers in v0.1. Supporting such callbacks needs a separate GIL, ownership and exception design.

Workers are explicit, positive and capped internally by trajectory count. The existing bounded scheduler assigns whole trajectories. No persistent pool, process fallback, IPC, distributed cache or automatic strategy selector is proposed. Preserve the documented MT-002 distinction: gravity-only threads can help, while shared SPICE/environment infrastructure can make environment-heavy threaded batches slower.

## Errors and missing data

- Translate `std::invalid_argument` to `ValueError`, explicit supported-domain `std::out_of_range` to a documented domain/coverage error, numerical `std::runtime_error` to `RuntimeError`, and filesystem failures to `OSError` when identifiable without matching error-message text.
- Translate `BatchPropagationError` to a Python exception carrying `trajectory_index` and its translated original cause. All workers join first; report the lowest failing input index. Never return plausible partial trajectories or suppress a worker exception.
- At construction, cross-check selected forces **and torques** against available Sun and density acquisition. Missing Sun must not become a Sun at Earth's centre; disabled atmospheric acquisition must not silently mean vacuum when drag was requested. Explicit zero density remains a valid intentional vacuum.
- Custom explicit snapshot sources remain responsible for their data contract; they are not an implicit v0.1 Python callback API.
- Ephemeris validity is checked over the actual requested epoch/window; out-of-coverage errors remain errors. No synthetic geometry, guessed kernel or silent constant-density replacement.

## Minimal C++ safety changes

Qualification demonstrated that an `EarthEnvironment` without a Sun provider could be combined with SRP and yield plausible wrong loads; disabled atmospheric acquisition could silently remove drag. The focused fix cross-checks built-in source/model compatibility in `EnvironmentalDynamics` construction and rejects an explicit zero Sun vector in the SRP boundary. No valid-input force law, integrator or scheduler changes are needed.

The remaining binding work is ownership/conversion code, not a new physics API. Keep packed-state helpers, Boost stepper/error-control internals, batch failure slots, SPICE locks/generation tokens, analysis scratch structures and qualification tools private.

## Next implementation phase

After reviewing the qualification report and frozen acceptance evidence, implement bindings for the objects above, ownership/exception/GIL tests, and only then a separately scoped package build. Do not publish v0.1 or introduce packaging infrastructure as part of this qualification phase.
