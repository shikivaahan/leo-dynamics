# Python development guide

Practical build, import and usage notes for `leo_dynamics`. The interface
reference lives in [python-api-design.md](python-api-design.md); the C++ contracts
and scientific caveats stay in [stage3.md](stage3.md) and
[docs/v0.1-qualification.md](v0.1-qualification.md).

## Build the extension

The bindings are an **optional** CMake path. The default C++ build does not require
Python or pybind11 (native testing may optionally discover a Python interpreter):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DLEO_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

With bindings on, CMake additionally needs:

- the **Python development headers** (`Development.Module`) for the interpreter
  you will import with,
- **NumPy** for the same interpreter (the suite and examples use it),
- **pybind11** — either an install CMake can find, or a directory you pass
  explicitly with `-Dpybind11_DIR=<prefix>/share/cmake/pybind11`.

A typical development build:

```sh
python3 -m venv .venv-python
.venv-python/bin/python -m pip install --upgrade pip
.venv-python/bin/python -m pip install "numpy>=2.0" "pybind11>=2.12" pytest

cmake -S . -B build-python -DCMAKE_BUILD_TYPE=Release \
  -DLEO_BUILD_PYTHON_BINDINGS=ON \
  -DPython3_EXECUTABLE="$PWD/.venv-python/bin/python"
cmake --build build-python --parallel
```

CMake queries the selected interpreter for its pybind11 CMake directory
automatically. For an external pybind11 tree, pass
`-Dpybind11_DIR=<prefix>/share/cmake/pybind11`.

`LEO_BUILD_PYTHON_BINDINGS=ON` requires `LEO_BUILD_INTEGRATION` and
`LEO_BUILD_ANALYSIS` (both default ON), turns on position-independent code for
the static engine targets, links `leo::integration` and `leo::analysis` into the
`_core` extension and preserves C++20 and the native optimisation settings. The
kernel keeps no Python dependency.

## Import it locally

The build stages the facade and the type stubs next to the extension, so one
`PYTHONPATH` entry is enough:

```sh
export PYTHONPATH="$PWD/build-python/python"
.venv-python/bin/python -c "import leo_dynamics as leo; print(leo.Earth())"
```

Expected output:

```text
leo_dynamics.Earth(mu_m3_s2=398600441800000.000000, equatorial_radius_m=6378136.300000, j2=0.001083)
```

This is the developer path. Ordinary installation is a wheel: see the
[README installation section](../README.md#installation), and
[python-packaging.md](python-packaging.md) for what has actually been built,
installed and tested. If the import fails with a "has not been built" message,
either the extension is missing from the directory on `PYTHONPATH` or the
invoked interpreter is not the one the build targeted.

## Units, frames and conventions

- SI: metres, seconds, radians, kilograms, newtons, newton-metres.
- Positions, velocities, Sun geometry and accelerations: Earth-centred J2000.
- Torques, angular rates and surface geometry: body frame.
- Attitude: Hamilton scalar-first **active body-to-J2000** quaternion
  `(w, x, y, z)`.
- Packed 13-state order: `[rx, ry, rz, vx, vy, vz, qw, qx, qy, qz, wx, wy, wz]`.
- `epoch_et_s` is **TDB ephemeris seconds past J2000**, never UTC or a Unix
  timestamp. `times_s` are elapsed seconds from that epoch and must start at
  exactly `0.0`, be finite and be strictly increasing.
- Analysis work components named `_rtn_` are ordered radial, transverse, normal.

## First orbital propagation

```python
import numpy as np
import leo_dynamics as leo

earth = leo.Earth()                       # mu, equatorial radius, J2
radius = 7.0e6                            # about 622 km above the reference radius
speed = np.sqrt(earth.mu_m3_s2 / radius)  # circular speed

initial = leo.State(
    position_m=[radius, 0.0, 0.0],
    velocity_m_s=[0.0, speed, 0.0],
)

spacecraft = leo.Spacecraft(
    mass_kg=500.0,
    inertia_kg_m2=np.diag([12.0, 14.0, 18.0]),   # kg m^2, symmetric positive definite
    surfaces=[
        leo.Surface(
            area_m2=2.0,
            normal_body=[0.0, 1.0, 0.0],        # unit vector, body frame
            center_of_pressure_body_m=[0.0, 0.4, 0.0],
            optical=leo.OpticalProperties(specular=0.25, diffuse=0.10),
            aerodynamic=leo.AerodynamicProperties(drag_coefficient=2.2,
                                                  accommodation=0.9),
        ),
    ],
)

times_s = np.arange(0.0, 5400.0, 60.0)          # 90 samples, starts at 0

trajectory = leo.propagate(
    initial=initial,
    spacecraft=spacecraft,
    model=leo.DynamicsModel(gravity="j2"),
    times_s=times_s,
)

trajectory.states            # (90, 13) float64, read-only
trajectory.position_m        # (90, 3)  read-only view into `states`
trajectory.quaternion_wxyz   # (90, 4)
trajectory.times_s           # (90,)   the exact requested schedule
trajectory.epoch_et_s        # 0.0

elements = trajectory.orbital_elements()        # structured array, (90,)
elements["altitude_m"][-1]                      # metres
trajectory.apsides()["kind"]                    # 0.0 periapsis, 1.0 apoapsis
trajectory.orbits()["period_s"]                 # 0.0 when the orbit is incomplete
```

`propagate` also accepts a `(13,)` array or sequence wherever a `State` is
expected, so `leo.State.from_array(...)` is optional.

## Full environmental example

SRP, drag and albedo each need their own acquisition, and drag needs an explicit
law. A tabulated Sun covers the whole requested window; `epoch_et_s` is TDB
seconds past J2000.

```python
import numpy as np
import leo_dynamics as leo

AU_M = 1.495978707e11

# Sun samples covering the run: strictly increasing TDB epochs plus (M, 3)
# positions and velocities. Nothing is extrapolated outside this range.
et = np.arange(0.0, 21600.0 + 600.0, 600.0)
sun_position = np.column_stack([AU_M + 3.0e8 * et, np.zeros(et.size), np.zeros(et.size)])
sun_velocity = np.tile([3.0e8, 0.0, 0.0], (et.size, 1))
sun = leo.SunEphemeris.tabulated(et_seconds=et,
                                position_m=sun_position,
                                velocity_m_s=sun_velocity)

environment = leo.Environment(
    sun=sun,
    atmosphere=leo.ExponentialAtmosphere(scale=1.0),
    eclipse="conical",                        # "none" | "cylindrical" | "conical"
    atmosphere_rotation_rad_s=(0.0, 0.0, 7.292115e-5),
)

model = leo.DynamicsModel(
    gravity="j2",
    srp=leo.Contribution(force=True, torque=True),
    drag=leo.Contribution(force=True, torque=True),
    drag_law="sentman",                       # or "flat_plate"
    albedo=leo.Contribution(force=True, torque=True),
    earth_albedo=0.3,
    gravity_gradient=True,
)

trajectory = leo.propagate(
    initial=initial,
    spacecraft=spacecraft,
    model=model,
    times_s=times_s,
    environment=environment,
    epoch_et_s=0.0,
)

records = trajectory.analyze().environment
records["illumination"][:3]        # 1.0 in sunlight, 0.0 in umbra
records["density_kg_m3"][:3]
records["srp_force_n"]             # (N, 3) newtons
records["non_gravitational_torque_nm"]

from pathlib import Path

Path("/tmp/orbit").mkdir(exist_ok=True)
trajectory.to_csv("/tmp/orbit/trajectory.csv")   # trajectory.csv only; parent must exist
trajectory.analyze().export_csv("/tmp/orbit")    # all nine CSV files; creates the directory
```

CSV export is native I/O: `to_csv(path)` writes one file and requires its parent
directory to exist, while `export_csv(directory)` creates the directory. Any file
system failure raises `OSError`.

Constant-Sun provider for controlled experiments (no CSPICE required):

```python
environment = leo.Environment(
    sun=leo.SunEphemeris.constant([AU_M, 0.0, 0.0]),
    atmosphere=leo.ExponentialAtmosphere(scale=1.0),
)
```

An explicitly quiet atmosphere is valid and intentional:

```python
vacuum = leo.Environment(atmosphere=leo.ExponentialAtmosphere(scale=0.0))
vacuum.density_kg_m3(400e3)   # 0.0
```

Omitting the atmosphere entirely while drag is selected is rejected — it never
silently means vacuum. A model that needs no acquisition propagates with
`environment=None`.

## Homogeneous batch

One spacecraft, one model, many initial states:

```python
initials = np.repeat(initial.to_array()[None, :], 8, axis=0)
initials[:, 0] += np.arange(8) * 1.0e5       # fan the starting positions out

batch = leo.propagate_batch(
    initials=initials,                        # (8, 13)
    spacecraft=spacecraft,                    # broadcast to all 8
    model=leo.DynamicsModel(gravity="j2"),     # broadcast to all 8
    times_s=times_s,                          # one shared schedule
    workers=4,
)

len(batch)             # 8
batch.states           # (8, 90, 13)
batch.position_m       # (8, 90, 3)
batch.times_s          # (90,)
first = batch[0]       # a Trajectory; supports the same analysis and export API
first.states           # (90, 13)
batch[-1]              # negative indices work; out of range raises IndexError
```

`initials` may equally be a sequence of `State` objects. Batch results are
bitwise identical to running each trajectory through `propagate`, and identical
across worker counts.

## Heterogeneous spacecraft batch

Per-trajectory spacecraft, models and environments, one shared schedule, epoch and
solver configuration:

```python
count = 4
AU_M = 1.495978707e11

spacecrafts = [
    leo.Spacecraft(
        mass_kg=500.0 * (1.0 + i),
        inertia_kg_m2=(1.0 + 0.5 * i) * np.diag([12.0, 14.0, 18.0]),
        surfaces=[
            leo.Surface(
                area_m2=2.0 * (1.0 + 0.25 * i),
                normal_body=[0.0, 1.0, 0.0],
                center_of_pressure_body_m=[0.0, 0.4, 0.0],
                optical=leo.OpticalProperties(specular=0.25, diffuse=0.10),
                aerodynamic=leo.AerodynamicProperties(2.2, 0.9),
            ),
        ],
    )
    for i in range(count)
]

models = [
    leo.DynamicsModel(
        gravity="j2" if i % 2 else "point_mass",
        srp=leo.Contribution(force=True, torque=bool(i % 2)),
        drag=leo.Contribution(force=True, torque=True) if i % 3 == 0 else None,
        drag_law="flat_plate",
        gravity_gradient=bool(i % 2),
    )
    for i in range(count)
]

environments = [
    leo.Environment(
        sun=leo.SunEphemeris.constant([AU_M * (1.0 + 0.01 * i), 0.0, 0.0]),
        atmosphere=leo.ExponentialAtmosphere(scale=1.0 + 0.1 * i),
        eclipse="conical" if i % 2 else "cylindrical",
    )
    for i in range(count)
]

initials = np.repeat(initial.to_array()[None, :], count, axis=0)   # (B, 13)

batch = leo.propagate_batch(
    initials=initials,
    spacecraft=spacecrafts,      # length must be exactly B
    model=models,                # length must be exactly B
    environment=environments,    # length must be exactly B
    times_s=times_s,
    workers=3,
)
batch.states                     # (4, 90, 13)
```

Trajectories never interact: no mutual gravity, no collisions. Configuration
sequences are never cycled, truncated or padded — a wrong length raises
`ValueError`.

## Multithreading

- `workers=1` (default) runs the whole batch inline and creates **no** thread.
- Otherwise the native bounded scheduler creates and joins
  `min(workers, trajectories)` threads, each taking whole trajectories.
- `workers` must be a positive integer; `0`, negatives, floats, booleans and
  strings raise `ValueError`.
- The GIL is released for the whole native run, so other Python threads keep
  making progress during a long propagation.
- Results keep input order regardless of which worker finished first.
- A failure joins every worker, returns nothing and raises
  `BatchPropagationError` with the lowest failing `trajectory_index`.

Caveats carried over from the native MT-002 measurements still apply: gravity-only
batches scale well on native workers, while shared ephemeris/environment
infrastructure can make environment-heavy threaded batches slower. Choose
`workers` by measurement for your workload; there is no automatic selection.

```python
# Same heterogeneous batch, now on four native workers.
try:
    leo.propagate_batch(initials=initials, spacecraft=spacecrafts, model=models,
                        environment=environments, times_s=times_s, workers=4)
except leo.BatchPropagationError as error:
    print(error.trajectory_index, repr(error.cause))
```

## NumPy shapes and memory ownership

| Result | Shape |
|---|---|
| `Trajectory.states` | `(N, 13)` |
| `Trajectory.position_m` / `velocity_m_s` / `omega_body_rad_s` | `(N, 3)` |
| `Trajectory.quaternion_wxyz` | `(N, 4)` |
| `BatchTrajectory.states` | `(B, N, 13)` |
| `BatchTrajectory.position_m` / `velocity_m_s` / `omega_body_rad_s` | `(B, N, 3)` |
| `BatchTrajectory.quaternion_wxyz` | `(B, N, 4)` |
| `times_s` (single and batch) | `(N,)` |
| Empty batch | `(0, N, 13)`, `len(batch) == 0`, indexing raises `IndexError` |

- All outputs are native-endian `float64` and **read-only**; copy with
  `np.array(x)` if you need to mutate one.
- Component views share one owning buffer with `states` — no duplicate
  allocation.
- Results own their memory and stay valid after the input objects, the model or
  the trajectory itself are deleted. An individual `batch[i]` keeps its own
  context and survives deletion of the parent batch.
- Configuration objects copy your arrays on construction, so mutating an input
  array during or after a run cannot change the native simulation.
- Integer arrays, lists and non-contiguous views are converted safely; complex,
  object, string and structured dtypes are rejected.
- Memory: a `(B, N, 13)` `float64` batch costs `B · N · 104` bytes of output, so
  plan the member count and sample count together.

## Error handling

| Situation | Exception |
|---|---|
| Bad shape, dtype, nonfinite value, invalid configuration or model name | `ValueError` |
| Outside the supported domain: Sun coverage window, atmosphere altitude range | `leo.DomainError` (subclass of `ValueError`) |
| Unbound or invalid elliptic elements | `ValueError` |
| Integration failure | `RuntimeError` |
| Failed batch trajectory | `leo.BatchPropagationError` (subclass of `RuntimeError`) |
| CSV export file system failure | `OSError` |

```python
try:
    trajectory = leo.propagate(initial=initial, spacecraft=spacecraft,
                               model=model, times_s=times_s, environment=environment)
except leo.DomainError as error:      # also catches plain ValueError cases
    print("out of coverage:", error)
```

Common mistakes and their messages:

- `times_s` not starting at `0.0`, not increasing, or nonfinite →
  `times_s must start at exactly zero` / `times_s must be finite and strictly increasing`.
- `surface` normal that is not a unit vector → `surface requires positive finite area, unit normal and finite application point`.
- Non positive-definite inertia → `inertia must be positive definite`.
- Optical fractions summing above one → `optical fractions must be finite, nonnegative and sum to at most one`.
- `drag_law="none"` with drag enabled → `enabled drag needs an explicit drag model`.
- Configuration sequence length ≠ batch size → `<name> sequence length must equal batch size`.
- Non-positive or non-integer `workers` / `max_attempts` → `ValueError`; each field must be a positive integer.

## Running the tests and benchmarks

```sh
cmake -S tests/reference -B build-python-reference -DCMAKE_BUILD_TYPE=Release
cmake --build build-python-reference --parallel
PYTHONPATH="$PWD/build-python/python" \
  "$PWD/.venv-python/bin/python" -m pytest tests/python -q \
  --harness build-python-reference/leo_reference_dump
```

The suite covers the public API surface, single propagation, heterogeneous
batches, environment validation, analysis and CSV export, ownership/GIL
behaviour and numerical parity against the frozen qualified goldens in
`tests/reference`.

Full numerical parity requires the direct C++ harness above. Without it,
pytest skips the direct-native comparisons while still running the frozen-golden
checks. `LEO_REFERENCE_DUMP` is an alternative to `--harness`.

Binding-overhead benchmark (no SciPy/TudatPy campaign is repeated):

```sh
cmake -S tests/reference -B build-reference -DCMAKE_BUILD_TYPE=Release
cmake --build build-reference --parallel
LEO_PYTHON_PATH="$PWD/build-python/python" \
  "$PWD/.venv-python/bin/python" benchmarks/python/binding_overhead.py \
  --harness build-reference/leo_reference_dump
```

Observed results, sanitizer commands and the nonstandard dependency/header paths
used on the qualification host are in
[python-binding-validation.md](python-binding-validation.md).

## Supported v0.1 physics and limitations

Supported: point-mass and J2 gravity (J2 axis fixed to J2000 +Z); the full
13-state orbit/rigid-body dynamics with the qualified adaptive Dopri5 settings;
independent force/torque selection for SRP, drag and simplified albedo;
gravity-gradient torque; flat-plate and cold-beam Sentman drag with the native
aerodynamic co-rotation and one-sided panel conventions; cylindrical/conical
eclipse; the exponential atmosphere over 0 … 35 786 km; orbital/attitude/work
analysis and the nine-file CSV export.

Limitations to keep in mind:

- **No publication yet.** Wheels and a source distribution build from this
  repository, but nothing is on PyPI and no licence is established, so
  `pip install leo-dynamics` does not work. See
  [python-packaging.md](python-packaging.md).
- **No CSPICE from Python.** The optional native provider and its tests are
  unchanged; use `SunEphemeris.constant` or `SunEphemeris.tabulated`.
- **No NRLMSISE-00 or JB2008** in v0.1 (deferred to v0.2).
- **No Python callbacks** in the integration loop, no Python thread/process
  pools, no automatic concurrency or backend selection, no GPU.
- **No interacting multi-spacecraft dynamics**, no time-varying mass or inertia,
  no per-trajectory sampling schedules.
- Analysis re-evaluates the trajectory's own environment at every sample; a
  diagnostic the native model cannot supply is `NaN` (for example Sun incidence
  without a Sun provider), never a substituted value.
- Attitude error should be compared as the shortest SO(3) rotation (invariant
  under `q → -q`), and singular orbital angles are not ordinary scalar errors.
- The exponential atmosphere is spherical only, Sentman is cold-beam, and the
  albedo term is a radial-beam approximation — see
  [python-api-design.md](python-api-design.md#scientific-caveats-preserved) and
  [stage3.md](stage3.md).