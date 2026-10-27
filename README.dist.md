# leo-dynamics

Native LEO orbital dynamics: two-body and J2 gravity, panel solar-radiation
pressure, aerodynamic forces and their torques, gravity-gradient torque, and
trajectory analysis with CSV export — with a NumPy-friendly Python interface.

The numerical work runs in a compiled C++20 extension; Python configures and
reads results, it does not integrate anything itself.

## Install

This version has not been published. Install an owner-supplied local wheel
with `pip install /path/to/leo_dynamics-0.1.0-*.whl`. The intended command after
an authorised publication is:

```sh
pip install leo-dynamics
```

The wheel contains the compiled engine, so no compiler, CMake, Eigen, Boost or
pybind11 is needed on the installing machine. NumPy is the only mandatory
runtime dependency. Build configuration targets CPython 3.10–3.13 on Linux
x86-64, Windows x86-64 and macOS (Apple Silicon and Intel); a configured target
is not proof that its final wheel has been qualified.

## Quick start

```python
import numpy as np
import leo_dynamics as leo

earth = leo.Earth()
radius = 7.0e6                                     # ~622 km above the reference radius
speed = float(np.sqrt(earth.mu_m3_s2 / radius))    # circular speed

initial = leo.State(position_m=[radius, 0.0, 0.0],
                    velocity_m_s=[0.0, speed, 0.0])

spacecraft = leo.Spacecraft(
    mass_kg=500.0,
    inertia_kg_m2=np.diag([40.0, 40.0, 20.0]),
    surfaces=[
        leo.Surface(area_m2=12.0,
                    normal_body=[0.0, 0.0, 1.0],
                    center_of_pressure_body_m=[0.0, 0.0, 0.0])
    ],
)

model = leo.DynamicsModel(gravity="j2")
times_s = np.arange(0.0, 5400.0 + 5.0, 5.0)

trajectory = leo.propagate(initial=initial,
                           spacecraft=spacecraft,
                           model=model,
                           times_s=times_s,
                           earth=earth)

print(trajectory.states.shape)        # (1081, 13)
print(trajectory.position_m[-1])      # final position in metres
```

`leo.propagate` returns `float64` NumPy arrays; `position_m`, `velocity_m_s`,
`quaternion_wxyz` and `omega_body_rad_s` are read-only views into the single
owning `states` buffer. A batch of trajectories can be propagated with
`leo.propagate_batch(..., workers=4)`; results are identical to the
single-trajectory result bit for bit.

Trajectory analysis (osculating elements, loads, CSV export) is available
through `leo.Analysis` and the `leo.analysis` module.

## What this distribution contains

- Point-mass and J2 gravity, orbital-element and frame utilities.
- Panel solar-radiation pressure and its torque, including a radial-beam
  albedo model.
- An exponential atmosphere and a cold-beam Sentman drag model.
- Gravity-gradient torque for a rigid body.
- Adaptive error-controlled integration with a bounded native scheduler for
  batch propagation.
- A separate analysis target that exports trajectory CSV for optional
  headless Matplotlib plotting.

## What this distribution does not contain

- No empirical high-fidelity atmospheric model. The exponential atmosphere is
  a documented spherical approximation with no latitude, local-time, solar
  activity or temperature dependence.
- The basic exponential atmosphere compiles in a fixed 40-pair altitude/density
  parameter table, with continuous log-linear interpolation. Its upstream
  provenance is recorded in `THIRD_PARTY_NOTICES.md`.
- No SPICE kernels, external ephemeris or space-weather datasets, empirical
  atmosphere model sources, or private research fixtures are bundled or downloaded.
- No flight-qualified accuracy claim. The engine is validated against frozen
  independent references and documented approximations, not against flight data.
- No controls, third-body gravity, flexible bodies, GPU support or optimisation
  subsystem.

## Building from the source distribution

Building needs a Python >= 3.10 interpreter with development headers and a
C++20 compiler on the build machine only. CMake, Ninja, Eigen 3.4.0, Boost
1.83.0 and pybind11 are provisioned by the pinned build, so no host copies are
required.

```sh
python3 -m venv .venv-build
.venv-build/bin/python -m pip install --upgrade pip build
.venv-build/bin/python -m build --wheel
```

## Licence

This distribution is MIT licensed; see `LICENSE` for the licence text and
`LICENSE_SCOPE.md` for the exact scope of the grant, and
`THIRD_PARTY_NOTICES.md` for the terms of the third-party components the
extension is built against (Eigen, MPL-2.0; Boost, BSL-1.0; pybind11,
BSD-3-Clause) and the platform runtimes it links.

The licence covers this distribution only. It does not cover the
`leo-dynamics` repository's proprietary research material, which is not part of
this distribution and is not redistributed with it.
