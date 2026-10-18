# v0.1 Python interface — implemented

This document describes the interface that is **implemented and built today** by
`leo_dynamics`, not a proposal. The compiled extension is `leo_dynamics._core`;
the public names, keyword signatures and Python-friendly coercions live in
`leo_dynamics/__init__.py`, with type information in `leo_dynamics/__init__.pyi`.
No physics, integrator, tolerance, scheduling or environmental semantics are
reimplemented in Python — every numerical result comes from the same C++ code the
qualified native suites exercise.

For build steps, runnable examples and workflow, see
[python-development.md](python-development.md).

```python
import leo_dynamics as leo
print(leo.Earth())
```

## Architecture

| Layer | Location | Responsibility |
|---|---|---|
| Native extension | `bindings/*.cpp` → `_core` (private) | pybind11 classes, conversions, ownership, GIL boundaries, native `leo::` calls |
| Facade | `python/leo_dynamics/__init__.py` | Public namespace, re-exports, NumPy/coercion ergonomics for `propagate`, `propagate_batch`, `evaluate_dynamics` |
| Type information | `python/leo_dynamics/__init__.pyi` | Signatures, units and array shapes; no runtime typing dependency |

`_core` is private. Configuration objects, `propagate*`, `evaluate_dynamics`,
`Trajectory`, `BatchTrajectory`, `Analysis`, the analysis submodule and the
exception types are all re-exported from `_core`, so the facade adds only
argument coercion (`State` ↔ `(13,)` array, sequence → array). Every other public
name *is* the native class.

The kernel (`include/leo/kernel`) has no Python dependency, and the default C++
build never configures the binding path (`LEO_BUILD_PYTHON_BINDINGS` defaults OFF).

## Public objects

All classes are immutable from the caller's perspective: every property is
read-only and every constructor validates and copies its input into owned native
storage. Mutating the NumPy arrays or Python sequences you passed in afterwards
cannot change a configuration object or a running simulation.

| Python object | Native contract |
|---|---|
| `Earth(mu_m3_s2=3.986004418e14, equatorial_radius_m=6378136.3, j2=1.08262668e-3)` | SI constants; the J2 symmetry axis is fixed to J2000 +Z, not a moving ITRF pole. Invalid constants are rejected at construction. |
| `KeplerianElements(*, a_m, e, i_rad, raan_rad, arg_periapsis_rad, true_anomaly_rad)` | **Bound elliptic only**; constructed elements are domain-validated by the native conversion. |
| `keplerian_to_cartesian(*, a_m, e, i_rad, raan_rad, arg_periapsis_rad, true_anomaly_rad, earth=None)` | Returns `(position_m, velocity_m_s)` as two `(3,)` read-only arrays. |
| `cartesian_to_keplerian(position_m, velocity_m_s, *, earth=None)` | Native singular-gauge conventions are retained. Bound elliptic input only: unbound (energy ≥ 0) or otherwise invalid elements raise `ValueError`; no hyperbolic/parabolic support is added in Python. |
| `State(*, position_m, velocity_m_s, quaternion_wxyz=(1,0,0,0), omega_body_rad_s=(0,0,0))` | Earth-centred J2000 m and m/s; Hamilton scalar-first **active body-to-J2000** quaternion; body-frame rad/s. `to_array()` / `State.from_array(x)` pack/unpack the 13-state. Construction uses native quaternion normalization, including when reconstructing a solver sample; arbitrary quaternion coefficients are not promised bitwise round trips. |
| `OpticalProperties(specular=0.0, diffuse=0.0)` | Nonnegative fractions of incident power with sum ≤ 1; the remainder is absorption. |
| `AerodynamicProperties(drag_coefficient=2.2, accommodation=0.9)` | Nonnegative finite flat-plate Cd; accommodation in `[0, 1]`. |
| `Surface(*, area_m2, normal_body, center_of_pressure_body_m, optical=None, aerodynamic=None)` | One-sided plate: positive finite area, unit body normal, finite body-frame application point. `optical`/`aerodynamic` read back as native defaults when omitted. |
| `Spacecraft(*, mass_kg, inertia_kg_m2, surfaces=())` | Positive mass; symmetric **positive-definite** `(3, 3)` inertia tensor. `inertia_kg_m2` accepts any `(3, 3)` NumPy-compatible matrix, so no separate native `Inertia` object is needed. `surfaces` reads back as a list of `Surface`. |
| `Contribution(force=False, torque=False)` | Independent force/torque switches; `enabled()` is true when either is selected. |
| `DynamicsModel(*, gravity="point_mass", srp=None, drag=None, drag_law="none", albedo=None, earth_albedo=0.3, gravity_gradient=False)` | `gravity` ∈ `"point_mass"`, `"j2"`; `drag_law` ∈ `"none"`, `"flat_plate"`, `"sentman"`; `earth_albedo` finite in `[0, 1]`; enabled drag with `drag_law="none"` is rejected. `srp`/`drag`/`albedo` read back as `None` when absent or fully disabled. `needs_environment()` is true when a selected model requires Sun or atmospheric acquisition. |
| `SunEphemeris.constant(position_m)` | Constant Earth-centred J2000 Sun position with zero relative velocity, for controlled experiments only. No CSPICE needed. |
| `SunEphemeris.tabulated(*, et_seconds, position_m, velocity_m_s)` | Independently owned samples with the native cubic Hermite scheme. `et_seconds` is a finite, strictly increasing `(M,)` array of at least two TDB epochs; positions/velocities are matching `(M, 3)` arrays. Interior knots return the supplied sample. Queries outside coverage raise `DomainError`; nothing is extrapolated. |
| `ExponentialAtmosphere(scale=1.0)` | Spherically symmetric exponential profile, no latitude/local-time/solar dependence. `min_altitude_m` / `max_altitude_m` (0 … 35 786 km) are class attributes; `density_kg_m3(altitude_m)` raises `DomainError` outside that range. `scale=0.0` is an explicit, valid vacuum. |
| `Environment(*, sun=None, atmosphere=None, eclipse="conical", atmosphere_rotation_rad_s=(0, 0, 7.292115e-5))` | Owns its Sun provider for the whole simulation. `eclipse` ∈ `"none"`, `"cylindrical"`, `"conical"`. `has_atmosphere` reports whether density is acquired; `density_kg_m3` raises `ValueError` when it is not. |
| `SolverOptions(*, rtol=1e-10, position_atol_m=1e-3, velocity_atol_m_s=1e-6, quaternion_atol=1e-12, omega_atol_rad_s=1e-12, initial_step_s=1.0, max_step_s=15.0, max_attempts=1_000_000)` | The qualified adaptive Dopri5 settings. Tolerances and steps must be finite and positive, `max_step_s >= initial_step_s`, and `max_attempts` must be a positive integer (booleans and floats are rejected, never truncated). |
| `propagate(...)` / `propagate_batch(...)` / `evaluate_dynamics(...)` | Keyword-only facades over `leo::propagate`, `leo::propagate_batch` and the native `EarthDynamics`/`EnvironmentalDynamics` composition. |
| `Trajectory` / `BatchTrajectory` | Owning results; read-only NumPy views into one owned buffer. |
| `Analysis` | Complete native `leo::analysis` diagnostics for one trajectory. |
| `analysis` (module) | `orbital`, `apsides`, `orbits`, `environment`, `attitude`, `work`, `export_csv` mirroring `leo::analysis`. |
| `Wrench`, `DynamicsContributions`, `DynamicsEvaluation` | Native load and pointwise-evaluation records. |
| `DomainError`, `BatchPropagationError` | See [Errors](#errors-and-missing-data). |

NRLMSISE-00 and JB2008 remain v0.2 features. Their optional C++ code and tests are
unchanged and they are not exposed, acquired, or implied anywhere in this package.

## NumPy contracts

Every numerical array is native-endian `float64`. Inputs are converted to a
contiguous `float64` buffer (copying when needed), so non-contiguous views,
integer arrays and lists are accepted safely; complex, object, string and
structured dtypes are rejected rather than reinterpreted. Shapes and finiteness
are validated before the GIL is released.

| Data | Shape and order |
|---|---|
| Single packed initial state | `(13,)`: `r[0:3], v[3:6], q[w,x,y,z][6:10], omega[10:13]` |
| Batch initial states | `(B, 13)` |
| Requested elapsed seconds | `(N,)`, finite, strictly increasing, starting at exactly `0.0` |
| Inertia tensor | `(3, 3)`, kg·m² |
| Surface normal / centre of pressure | `(3,)` body frame; centre in metres |

Trajectory results:

| Property | Single | Batch |
|---|---|---|
| `states` | `(N, 13)` | `(B, N, 13)` |
| `position_m` | `(N, 3)` | `(B, N, 3)` |
| `velocity_m_s` | `(N, 3)` | `(B, N, 3)` |
| `quaternion_wxyz` | `(N, 4)` | `(B, N, 4)` |
| `omega_body_rad_s` | `(N, 3)` | `(B, N, 3)` |
| `times_s` | `(N,)` | `(N,)` |
| `epoch_et_s` | TDB seconds past J2000 | same value for every trajectory |

`position_m`, `velocity_m_s`, `quaternion_wxyz` and `omega_body_rad_s` are
strided views into the single owning `states` buffer (no second allocation) and
are all read-only. `states` holds the native-endian `float64` samples in the exact
requested order.

**Empty batches** are supported and still validate the shared inputs. With
`initials` of length `B = 0` and a valid `(N,)` schedule:

| Property | Value |
|---|---|
| `len(batch)` | `0` |
| `states` / `position_m` / … | `(0, N, 13)` / `(0, N, 3)` / … |
| `times_s` | `(N,)` |
| `epoch_et_s` | the requested epoch |
| iteration | yields nothing |
| `batch[i]` | `IndexError` |

`workers` must still be a positive integer for an empty batch, and a
configuration *sequence* must still have length exactly `B` (i.e. `0`).

Orientation error is the shortest SO(3) rotation, invariant under `q -> -q`; do
not compare quaternion coefficients or singular orbital angles as ordinary
scalar physical errors.

## Units, frames and epochs

- SI throughout: metres, seconds, radians, kilograms, newtons, newton-metres.
- Positions, velocities, Sun geometry, accelerations and surface normals are
  Earth-centred J2000.
- Torques, angular rates, angular accelerations and surface geometry are body
  frame. R/T/N work vectors carry their own frame tag and never mix implicitly
  with J2000 or body vectors.
- Attitude is the Hamilton scalar-first active body-to-J2000 quaternion
  `(w, x, y, z)`.
- `epoch_et_s` is TDB ephemeris seconds past J2000 — never UTC and never a Unix
  timestamp. `times_s` are elapsed seconds from that epoch.
- Absolute epochs (`epoch_et_s + times_s[-1]`) must be finite.

## Batch propagation

```python
leo.propagate_batch(
    *, initials, spacecraft, model, times_s,
    earth=None, environment=None, epoch_et_s=0.0, options=None, workers=1,
)
```

`initials` is a `(B, 13)` array or a sequence of `State` / `(13,)` arrays.
`spacecraft`, `model` and `environment` each accept one object (broadcast to
every trajectory) or a sequence of exactly `B` objects; `environment` also accepts
`None`. Sequences are never cycled, truncated or padded — a wrong length raises
`ValueError`. `earth`, `epoch_et_s`, `options` and `times_s` are shared by the
batch.

Each trajectory may differ in initial state and attitude, mass, inertia tensor,
surface geometry and optical/aerodynamic properties, selected force/torque
models and environmental configuration. Trajectories are fully independent:
there is no mutual gravity and no collision handling.

Scheduling is the existing native bounded scheduler
(`leo::propagate_batch`), which is handed one `(index, initial)` factory that
builds each trajectory's own RHS:

- `workers=1` runs the whole batch inline on the calling thread and creates no
  thread at all.
- Otherwise the call creates and joins a bounded pool of
  `min(workers, trajectories)` `std::threads`, each taking whole trajectories
  from one relaxed atomic work index.
- Results retain input order regardless of completion order.
- A failed batch returns nothing; the lowest failing trajectory index is
  reported.
- Every worker is joined before returning or raising.
- `workers` must be a positive integer — `0`, negatives, floats, `True` and
  strings are rejected with `ValueError`.

There is no Python thread pool, process pool, persistent pool, fallback,
automatic backend selection or per-step Python callback in v0.1.

## Analysis and CSV export

`Trajectory` exposes `orbital_elements()`, `apsides()`, `orbits()`, `analyze()`
and `to_csv(path)`. `Analysis` adds `surfaces` and `orbit_work`, and
`export_csv(directory)`.

Analysis reuses the bound context: the trajectory's own `EarthDynamics` plus one
real environment snapshot per sample, re-evaluated from the trajectory's own
`Environment`. No environmental history is invented and no unavailable model is
substituted with zero loads — when a required acquisition is missing the
propagation that produced the trajectory was already rejected.

Every array is a one-dimensional, read-only, owning NumPy structured array.
Frames: J2000 for inertial vectors, body frame for torques and angular rates,
R/T/N for work vectors. Times are elapsed seconds since the initial epoch;
`epoch_et_s` columns are TDB seconds past J2000.

| Attribute | Rows | Columns |
|---|---|---|
| `orbital` | `(N,)` | `time_s, radius_m, altitude_m, semi_major_axis_m, eccentricity, inclination_rad, raan_rad, arg_periapsis_rad, true_anomaly_rad, periapsis_radius_m, apoapsis_radius_m, periapsis_altitude_m, apoapsis_altitude_m, specific_energy_j_kg, specific_angular_momentum_m2_s(3), raan_unwrapped_rad, arg_periapsis_unwrapped_rad, true_anomaly_unwrapped_rad, specific_kinetic_energy_j_kg, specific_potential_energy_j_kg` |
| `apsides` | `(M,)` | `kind, time_s, radius_m, left_index, fraction` — `kind` is `0.0` periapsis, `1.0` apoapsis; `left_index` is the zero-based sample bracket |
| `orbits` | `(K,)` | `index, start_time_s, end_time_s, complete(bool), period_s, start_*/end_*/delta_*` for the six elements, `raan_start_unwrapped_rad, raan_end_unwrapped_rad` |
| `environment` | `(N,)` | `time_s, epoch_et_s, sun_position_m(3), illumination, density_kg_m3, atmosphere_angular_velocity_rad_s(3), point_mass_m_s2(3), j2_m_s2(3), gravity_gradient_nm(3), srp/drag/albedo_force_n(3), srp/drag/albedo_torque_nm(3), non_gravitational_force_n(3), non_gravitational_torque_nm(3), total_acceleration_m_s2(3), total_angular_acceleration_rad_s2(3)` |
| `attitude` | `(N,)` | `time_s, quaternion_wxyz(4), omega_body_rad_s(3), angular_speed_rad_s, rotational_energy_j, inertial_momentum_kg_m2_s, torque_nm(3), torque_magnitude_nm, relative_zyx_euler_rad(3), euler_singular(bool)` |
| `surfaces` | `(N·S,)` | `time_s, surface_index, normal_j2000(3), sun_incidence_rad, projected_illuminated_area_m2, projected_aerodynamic_area_m2, orbit_normal_angle_rad` |
| `work` | `(N,)` | `time_s, srp_force_n(3), srp_acceleration_m_s2(3), force_rtn_n(3), power_rtn_w(3), cumulative_work_rtn_j(3), power_w, cumulative_work_j, specific_work_j_kg, orbital_energy_change_j, specific_energy_change_j_kg, energy_minus_srp_work_j, sun_inbound_transverse_work_j, sun_outbound_transverse_work_j, mass_kg` |
| `orbit_work` | `(K,)` | `index, start_time_s, end_time_s, complete(bool), work_j, specific_work_j_kg, work_rtn_j(3), orbital_energy_change_j, energy_minus_srp_work_j` |

`(3,)` and `(4,)` columns are fixed-width subarrays, so `record["sun_position_m"]`
has shape `(N, 3)`. Boolean columns are real `bool` dtype. Elements are
osculating two-body values, not fitted secular elements; near a gauge transition
the unwrapped histories are not physically unique.

Missing diagnostics are `NaN` rather than invented values. The binding always
supplies one real environment snapshot per sample, so the Sun-geometry columns
are the ones that can be unavailable: without a Sun provider,
`sun_incidence_rad` and `projected_illuminated_area_m2` are `NaN`.
`projected_aerodynamic_area_m2` is always present — it depends on the
atmospheric relative flow, not on density. Disabled models are exactly zero,
never omitted columns, and attitude `torque_nm` is zero when there is no
non-gravitational contribution.

`Analysis.export_csv(directory)` writes the nine native CSV files —
`metadata.csv`, `trajectory.csv`, `orbital_elements.csv`, `attitude.csv`,
`surfaces.csv`, `environment.csv`, `work.csv`, `per_orbit.csv`, `apsides.csv` —
creating the directory if needed and preserving the existing schemas and
disabled-but-present columns exactly. `Trajectory.to_csv(path)` writes only
`trajectory.csv`, in the native schema, at the requested path.

A trajectory obtained from `BatchTrajectory[i]` supports the same analysis and
export API: it keeps its own native context and never re-runs the propagator.

## Pointwise diagnostics

```python
leo.evaluate_dynamics(*, state, time_s, spacecraft, model,
                      earth=None, environment=None, epoch_et_s=0.0)
```

`time_s` is elapsed seconds since `epoch_et_s`. The result is a
`DynamicsEvaluation` exposing the same selected contributions as native
`DynamicsContributions` — `point_mass_m_s2`, `j2_m_s2`, `gravity_gradient_nm`,
the `srp`/`drag`/`albedo`/`non_gravitational` `Wrench` records (each `force_n`
and `torque_nm`), `total_acceleration_m_s2`, `total_angular_acceleration_rad_s2`
and the packed 13-element `state_derivative`. Each contribution is reachable
directly on the evaluation object as well as through `.contributions`. Forces are
never recomputed in Python.

## Ownership, memory and GIL

- Configuration constructors validate and copy every mutable array/sequence into
  owned native storage before anything else runs.
- Results are owning: `Trajectory.states`, `BatchTrajectory.states` and every
  `Analysis` array own their buffer and stay valid after the trajectory, batch,
  model, spacecraft, environment or initial inputs are deleted. The strided
  component views share that one owning buffer.
- The batch result keeps one `std::shared_ptr` context per trajectory, so an
  ephemeris provider — and its Sun samples — outlive every native worker and all
  worker joins. Indexing a batch returns a `Trajectory` that holds its own
  context and a view into the parent buffer; deleting the parent does not
  invalidate it.
- Outputs are explicitly read-only; assignment raises `ValueError`.
- The GIL is held while inputs are validated and native owners are constructed,
  released across the entire native single or batch propagation (including the
  shared-environment coverage checks), and reacquired before NumPy results or
  Python exceptions are built. Native workers touch no Python objects at all.
- `Analysis` reuses the already-bound context; it does not re-propagate.

## Errors and missing data

| Condition | Python exception |
|---|---|
| Invalid input, shape, dtype, configuration, or unsupported model name | `ValueError` |
| Outside the supported domain: ephemeris coverage window, atmosphere altitude range | `DomainError` (a `ValueError` subclass) |
| Unbound or invalid elliptic elements (`keplerian_to_cartesian`, `cartesian_to_keplerian`) | `ValueError` |
| Integration failure (nonfinite stage/state, stalled step, attempt exhaustion, near-zero trial quaternion) | `RuntimeError` |
| Failed batch trajectory | `BatchPropagationError` (a `RuntimeError` subclass) |
| File system failure during CSV export | `OSError` |

`BatchPropagationError` carries `trajectory_index` (the lowest failing input
index) and `cause` (the translated original exception), and also chains through
`__cause__`. No partial results are returned and no worker exception is
suppressed.

At construction, the selected forces **and torques** are cross-checked against
available Sun and density acquisition. A radiation model without a Sun provider
and drag without atmospheric acquisition are both rejected — including
torque-only configurations. An explicit zero-density or `scale=0.0` atmosphere
remains a valid intentional vacuum and is not confused with disabled
acquisition. Ephemeris coverage is checked over the actual requested epoch and
window; there is no synthetic geometry, guessed kernel or silent
constant-density replacement.

## Supported physics and v0.1 limits

Supported, with unchanged native definitions and documented approximations:

- Point-mass and J2 gravity, J2 axis fixed to J2000 +Z.
- Full 13-state orbit/rigid-body propagation with the qualified adaptive Dopri5
  settings and step limits.
- Independent force/torque selection for SRP, drag and the simplified albedo
  model, plus the independent inertia-based gravity-gradient torque.
- Cold-beam Sentman and flat-plate drag with the native aerodynamic co-rotation
  and one-sided panel conventions.
- Simplified radial-beam albedo.
- Cylindrical/conical eclipse geometry.
- Exponential atmosphere with the spherically symmetric log-linear profile over
  0 … 35 786 km.

Deliberately absent in v0.1: NRLMSISE-00 and JB2008 exposure (deferred to v0.2);
CSPICE ephemerides from Python (the optional native provider is unchanged);
distribution packaging, `pyproject.toml`, wheels or PyPI infrastructure; Python
numerical integration; arbitrary Python RHS/environment callbacks in the
integration loop; Python thread or process pools; automatic concurrency or
backend selection; GPU support; new physical models; interacting multi-spacecraft
dynamics; time-varying mass or inertia; and per-trajectory sampling schedules.

## Scientific caveats preserved

These remain the repository's position and are unchanged by the bindings:

- Orbital elements are osculating two-body values with the native singular
  gauges (equatorial RAAN = 0; circular argp = 0; circular anomaly reported as
  argument of latitude / orbital-direction longitude).
- Illumination is a small uniform-brightness solar disk with angular-plane
  overlap; no limb darkening, refraction or scattered umbral light.
- The albedo term is a radial-beam approximation, not resolved Earth radiance,
  and direct-Sun eclipse is not multiplied into Earth-reflected radiation.
- Sentman is a cold-beam free-molecular approximation, not finite-temperature
  Sentman.
- The exponential atmosphere is spherical only — no latitude, local-time, solar
  activity or temperature dependence.
- Attitude analysis without a non-gravitational environment assumes torque-free
  loads; the exported `NaN` diagnostics mark exactly what is unavailable.
- The numerically unresolved six-hour fully coupled CASE-C stress result is a
  scientific limitation recorded in `docs/v0.1-qualification.md`, not a release
  gate for the bindings.

## Numerically related repository documents

- [v0.1 pre-Python qualification](v0.1-qualification.md)
- [v0.1 release readiness](v0.1-release-readiness.md)
- [Stage 3 contracts and feature inventory](stage3.md) — full C++ CSV schemas
- [Reference acceptance protocol](../tests/reference/ACCEPTANCE.md)

Packaging (wheels, `pyproject.toml`, PyPI) is deliberately a separate, later
phase; nothing in this document depends on it.