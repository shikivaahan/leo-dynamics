# LEO dynamics — Stage 3

> **Status update — 2026-10-26 licensing audit.** The v0.1.0 licensing audit is complete: the approved public component (the numerical engine and the Python interface) carries a **scoped MIT grant** ([LICENSE](LICENSE), [LICENSE_SCOPE.md](LICENSE_SCOPE.md)); all other repository material — research, empirical high-fidelity atmosphere providers, experiments, datasets, tests and release tooling — remains private and proprietary and is not distributed ([PROPRIETARY-NOTICE.md](PROPRIETARY-NOTICE.md)). The licence grant exists; publication is not authorised and remains **NO-GO** pending legal title/assignment review, upstream-parameter provenance review and verification of current private-repository plan support. The remainder of this document is the internal overview as it stood at that baseline and is retained as evidence; the current position, the open legal items and the NO-GO recommendation are in [RELEASE_LICENSE_AUDIT.md](RELEASE_LICENSE_AUDIT.md). No gate, threshold or historical result in this document was weakened or rewritten.

C++20, CPU-only, double-precision, lightweight Earth-centred 6-DoF rigid-spacecraft dynamics: point-mass/J2 gravity, panel radiation and aerodynamic forces, their torques, and gravity-gradient torque. A separate trajectory-analysis library exports CSV for optional headless Python/Matplotlib plotting. This is not a high-fidelity mission-analysis framework; no controls, third-body gravity, flexible bodies, GPU or optimisation subsystem.

## Installation

The Python distribution is `leo-dynamics`; the import name is `leo_dynamics`. It ships a binary wheel containing the compiled engine extension, so installing a compatible wheel needs **no compiler, CMake, Eigen, Boost or pybind11**. NumPy is the only mandatory runtime dependency.

The intended installation command is:

```bash
pip install leo-dynamics
```

**This command does not work yet.** Nothing has been published to PyPI and no publication has been approved. The numerical engine and Python interface now have a scoped MIT grant: see [LICENSE_SCOPE.md](LICENSE_SCOPE.md) and [LICENSE](LICENSE). All research, empirical high-fidelity atmosphere providers, experiments and datasets remain private and proprietary; see [PROPRIETARY-NOTICE.md](PROPRIETARY-NOTICE.md). Only the enumerated component may enter a distribution. The public package overview is [README.dist.md](README.dist.md), not this internal document. See [the licensing audit](RELEASE_LICENSE_AUDIT.md) for observed local checks and unresolved ownership/provenance and publication blockers.

### Installing a locally built wheel

Building needs a full Python >= 3.10 interpreter (with development headers) and a C++20 compiler on the build machine only. CMake, Ninja, scikit-build-core, pybind11, Eigen and Boost are all provisioned by the pinned build itself, so no system copies are required.

```sh
python3 -m venv .venv-build
.venv-build/bin/python -m pip install --upgrade pip build
.venv-build/bin/python -m build --wheel           # dist/leo_dynamics-0.1.0-*.whl

python3 -m venv /tmp/leo-wheel-test
/tmp/leo-wheel-test/bin/python -m pip install dist/leo_dynamics-0.1.0-*.whl
cd /tmp
/tmp/leo-wheel-test/bin/python -c "import leo_dynamics as leo; print(leo.Earth())"
```

```
leo_dynamics.Earth(mu_m3_s2=398600441800000.000000, equatorial_radius_m=6378136.300000, j2=0.001083)
```

`python -m build --sdist` produces the source distribution; rebuilding it needs the same toolchain. To prove a wheel — rather than a development build — is what got imported, install `pytest` and run the driver from outside the repository with an absolute path. It refuses to continue unless `leo_dynamics` resolves inside site-packages, then runs the existing suite from a temporary directory outside the source tree with `PYTHONPATH` removed:

```sh
/tmp/leo-wheel-test/bin/python -m pip install pytest
cd /tmp
REPO=/path/to/leo-dynamics
/tmp/leo-wheel-test/bin/python "$REPO/ci/run_installed_wheel_tests.py" "$REPO"
```

### Supported platforms

| Platform | Architecture | Python | Status |
|---|---|---|---|
| Linux | x86-64 (manylinux) | CPython 3.10–3.13 | Wheel builds and installed-wheel tests are configured in CI; see [packaging status](docs/python-packaging.md) for what has actually been executed |
| Windows | x86-64 | CPython 3.10–3.13 | Same |
| macOS | Apple Silicon (arm64) | CPython 3.10–3.13 | Same |
| macOS | Intel (x86-64) | CPython 3.10–3.13 | Same |

No interpreter or platform is claimed as supported until its wheel has been built and its installed-wheel tests have passed. Linux/macOS/macOS-Intel/Windows coverage comes from `.github/workflows/wheels.yml`; nothing in this repository publishes a package.

Both macOS architectures build against `MACOSX_DEPLOYMENT_TARGET=11.0`. One minimum is declared for both legs deliberately: the libc++ shipped with AppleClang annotates `std::filesystem` as introduced in macOS 10.15, so a lower target is a hard compile error rather than a weak link.

### What the Python interface is, and is not

- **The numerical engine executes in C++.** Python configures and reads results; it does not integrate anything itself.
- **NumPy is the primary data interface.** Results are read-only `float64` arrays; component views share one owning buffer with `states`.
- **Multithreading is native and controlled by `workers`.** The default worker count is **1**, which runs the batch inline and creates no thread. Higher counts use the native bounded scheduler, one whole trajectory per thread, with results bitwise identical to `workers=1`.
- **Atmospheric models NRLMSISE-00 and JB2008 are deferred** to v0.2 and are not in the wheel; the exponential atmosphere and cold-beam Sentman drag are. No SPICE kernels, atmospheric reference sources or space-weather data are downloaded or embedded.
- **No flight-qualified accuracy claim is made.** The engine is validated against frozen independent references and documented approximations, not against flight data.

### Example: one orbit

```python
from pathlib import Path

import numpy as np
import leo_dynamics as leo

earth = leo.Earth()
radius = 7.0e6                                     # ~622 km above the reference radius
speed = float(np.sqrt(earth.mu_m3_s2 / radius))    # circular speed

initial = leo.State(position_m=[radius, 0.0, 0.0],
                    velocity_m_s=[0.0, speed, 0.0])
spacecraft = leo.Spacecraft(
    mass_kg=500.0,
    inertia_kg_m2=np.diag([12.0, 14.0, 18.0]),
    surfaces=[leo.Surface(area_m2=2.0,
                          normal_body=[0.0, 1.0, 0.0],
                          center_of_pressure_body_m=[0.0, 0.4, 0.0])],
)
times_s = np.arange(0.0, 5400.0, 60.0)             # 90 samples from t = 0

trajectory = leo.propagate(initial=initial, spacecraft=spacecraft,
                           model=leo.DynamicsModel(gravity="j2"), times_s=times_s)

trajectory.states               # (90, 13) read-only float64
trajectory.position_m           # (90, 3) view into states
trajectory.orbital_elements()["altitude_m"][-1]    # metres

output = Path("/tmp/orbit"); output.mkdir(exist_ok=True)
trajectory.to_csv(output / "trajectory.csv")       # parent directory must exist
trajectory.analyze().export_csv(output)            # creates the directory, nine CSVs
```

### Example: heterogeneous batch

Per-trajectory spacecraft, dynamics model and environment on one shared schedule, epoch and solver configuration. Trajectories never interact and the batch size must match exactly.

```python
import numpy as np
import leo_dynamics as leo

count = 4
au_m = 1.495978707e11
earth = leo.Earth()
radius = 7.0e6
speed = float(np.sqrt(earth.mu_m3_s2 / radius))
initial = leo.State(position_m=[radius, 0.0, 0.0], velocity_m_s=[0.0, speed, 0.0])
times_s = np.arange(0.0, 5400.0, 60.0)

batch = leo.propagate_batch(
    initials=np.repeat(initial.to_array()[None, :], count, axis=0),   # (4, 13)
    spacecraft=[leo.Spacecraft(mass_kg=500.0 * (1.0 + i),
                               inertia_kg_m2=(1.0 + 0.5 * i) * np.diag([12.0, 14.0, 18.0]),
                               surfaces=[])
                for i in range(count)],
    model=[leo.DynamicsModel(gravity="j2" if i % 2 else "point_mass",
                             gravity_gradient=bool(i % 2))
           for i in range(count)],
    environment=[leo.Environment(sun=leo.SunEphemeris.constant([au_m * (1.0 + 0.01 * i), 0.0, 0.0]),
                                atmosphere=leo.ExponentialAtmosphere(scale=1.0 + 0.1 * i),
                                eclipse="conical" if i % 2 else "cylindrical")
                 for i in range(count)],
    times_s=times_s,
    workers=3,                       # native threads; the default is 1 (no thread)
)

batch.states                  # (4, 90, 13)
batch.position_m              # (4, 90, 3)
batch[0].analyze().export_csv("/tmp/orbit")    # nine CSV datasets
```

Developer builds — the optional CMake bindings path, the test suite and the benchmarks — stay in [Build and run](#build-and-run) and [docs/python-development.md](docs/python-development.md); they are not needed to install a wheel.

## Build and run

Dependencies: CMake >=3.24, Eigen >=3.4, Boost >=1.74 headers; Catch2 v3 only for tests. Dependencies must be installed before configure; CMake does not download them. Set `CMAKE_PREFIX_PATH` for non-system installs. Boost package-config installs and upstream header-only trees (via `Boost_ROOT`/`BOOST_ROOT`) are supported.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DLEO_BUILD_EXAMPLES=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/propagate_leo --model point-mass
./build/propagate_leo --model j2
```

The example propagates one day at 60 s output intervals (1441 samples). No arguments selects point-mass; only `--model point-mass|j2` is accepted. It prints model, sample count, final radius in metres and RAAN change in radians.

`leo::kernel` links only Eigen. `leo::integration` adds Boost.Odeint and the platform thread support for optional C++20 batch execution. `leo::analysis` links only the kernel and consumes trajectories, spacecraft/model information and explicit environment snapshots; it adds orbital, attitude, per-orbit and work diagnostics plus CSV export. Integration/analysis default ON; tests/examples default OFF. The optional `leo::environment` component adds CSPICE Sun ephemerides; the kernel never links CSPICE or Python. Environment acquisition and the solver wrapper remain separate from individual force laws.

Kernel-only build:
```sh
cmake -S . -B build-kernel -DLEO_BUILD_INTEGRATION=OFF -DLEO_BUILD_ANALYSIS=OFF -DBUILD_TESTING=ON
cmake --build build-kernel --parallel
ctest --test-dir build-kernel --output-on-failure
```

Requesting examples with integration OFF is a configuration error. CI defines GCC/Clang Release and Clang Debug ASan/UBSan validation, full environmental propagation and kernel-only builds. Catch2 v3.7.1 and CSPICE N0067 are bootstrapped outside project configure in CI. Tests never fetch dependencies or kernels.

An optional development build compiles the private pybind11 extension for local use (the kernel keeps no Python dependency and the default build never needs Python):

```sh
python3 -m venv .venv-python
.venv-python/bin/python -m pip install numpy pybind11 pytest
cmake -S . -B build-python -DCMAKE_BUILD_TYPE=Release -DLEO_BUILD_PYTHON_BINDINGS=ON \
  -DPython3_EXECUTABLE="$PWD/.venv-python/bin/python"
cmake --build build-python --parallel
PYTHONPATH="$PWD/build-python/python" .venv-python/bin/python -c "import leo_dynamics as leo; print(leo.Earth())"
```

This needs the Python development headers, NumPy and pybind11 for the target interpreter; pass `-Dpybind11_DIR=<prefix>/share/cmake/pybind11` when pybind11 is only inside that environment. It is a developer path for iterating on the bindings; ordinary installation uses the wheel in [Installation](#installation). See the [Python development guide](docs/python-development.md) for imports, examples, batching, multithreading, shapes, ownership and errors, the [implemented v0.1 Python interface](docs/python-api-design.md) for the full object and analysis reference, the [binding validation report](docs/python-binding-validation.md) for the qualification evidence, and [packaging status](docs/python-packaging.md) for distribution state and blockers.

## Analysis and plotting workflow

```sh
./build/analyze_orbit /tmp/leo-analysis
python3 -m venv .venv
.venv/bin/python -m pip install -r plotting/requirements.txt
.venv/bin/python plotting/plot_analysis.py /tmp/leo-analysis --output /tmp/leo-analysis/plots --format both
.venv/bin/python tests/plotting_smoke.py
```

The example propagates 751 samples across 2.5 nominal periods with J2, SRP,
Sentman drag, albedo and environmental torques, using a deterministic constructed
Sun. It analyzes every sample and exports nine CSV datasets. Python is optional
and not required to configure/build/use C++ targets. `LEO_TEST_PLOTTING=ON`
registers the Python smoke test only when explicitly requested.

For real ephemerides, configure with `LEO_ENABLE_CSPICE=ON` and use
`analyze_orbit OUTPUT_DIR --spice leap-seconds.tls planets.bsp epoch_et_seconds`.
Kernels are caller-provided; no hidden downloads or fallback ephemerides.
The plotting CLI also supports supplied-run comparisons, parameter heatmaps,
sweep-manifest reduction and externally computed scalar series.

See [Stage 3 contracts and feature inventory](docs/stage3.md) for APIs, full CSV
schemas/units, sampled event/work interpolation, missing-diagnostic behavior,
all plotting commands, Julia correctness corrections and deliberate omissions.


## Public contracts

Headers live under `include/leo`; physics/integration/environment use namespace `leo`, postprocessing uses `leo::analysis`.

- **State:** named `State {r,v,q,omega}`. Earth-relative position in metres and velocity in m/s use `InertialVector` (J2000); angular velocity in rad/s uses `BodyVector`. Minimal frame tags disallow implicit body/inertial mixing. Raw 13-scalar packing belongs only to the integration adapter.
- **Attitude:** Hamilton quaternion `{w,x,y,z}`, active body → J2000; identity `{1,0,0,0}`. Body-frame angular velocity satisfies `q_dot = 0.5*q ⊗ (0,omega)`. Quaternion derivatives and intermediate solver states are not normalized. `validated_state` normalizes input; every accepted solver step projects q and refreshes its endpoint derivative if projection changes q. Quaternion sign is not canonicalized. Nonfinite quaternions and norm <=1e-12 are errors, never replaced by identity.
- **Spacecraft:** positive finite mass and full finite symmetric positive-definite body inertia, with optional `std::vector<Surface>`. `mass_kg()`, `inertia()` and `surfaces()` give read-only access; `surfaces()` is a `std::span<const Surface>`. No setters; valid-value replacement preserves invariants. `Inertia` stores the matrix and an LLT factorization, not its inverse; symmetry tolerance is 1e-12 relative to the largest coefficient. Mass does not scale gravitational acceleration. A `Surface(area_m2,normal_body,center_of_pressure_body,optical,aerodynamic)` is a one-sided plate with positive finite area, finite COM-relative application point and finite unit normal (norm tolerance 1e-12). Invalid normals are rejected, not normalized. Two opposing surfaces represent two sides of a panel. Geometry and material coefficients are private validated values shared by all surface models.
- **Earth/gravity:** `Earth` defaults to mu=3.986004418e14 m³/s², equatorial radius=6378136.3 m, J2=1.08262668e-3. `point_mass_acceleration` and `j2_acceleration` are independent functions; `EarthGravity(Earth,bool)` composes them. J2 is additive and its symmetry axis is **fixed J2000 +Z**, a Stage 1 approximation, not Earth orientation modelling. Finite interior positions are permitted; collision handling is not implemented.
- **Dynamics:** `EarthDynamics(EarthGravity,Spacecraft,EnvironmentalModels={})` retains the Stage 1 `(ElapsedTime,const State&)` callable for gravity and optional gravity-gradient torque. Enabled SRP/drag/albedo requires the explicit `(ElapsedTime,const State&,const EnvironmentSnapshot&)` overload; omitting the snapshot is an error. `v_dot = gravity + total_force/mass`; `omega_dot = I.solve(total_body_torque - omega × I*omega)`. `Wrench` carries J2000 force in N and body torque in N m. All surface torques use the shared `surface_body_torque = center_of_pressure_body × force_body`; force and torque are never evaluated with different laws.
- **Time:** `ElapsedTime {seconds}` is elapsed dynamics time. `Epoch {et_seconds}` is TDB ephemeris seconds past J2000 (2000-01-01 12:00:00 TDB). The integration adapter retains epoch as metadata; `EarthEnvironment` explicitly evaluates ephemerides at `epoch.et_seconds + elapsed.seconds`, without truncating fractional seconds. Do not pass Unix/UTC seconds as ET.
- **Environment:** `EnvironmentSnapshot` contains explicit epoch, Earth-to-Sun J2000 position in m, direct illumination [0,1], density in kg/m³ and atmospheric rotation vector in rad/s. Physics models do not acquire environment data. `EarthEnvironment(Earth,Epoch,provider_pointer,enable_atmosphere,eclipse,density,rotation)` borrows the provider (which must outlive it); a null pointer disables solar acquisition. `EnvironmentalDynamics(EarthDynamics,source)` wraps any compatible snapshot source for the unchanged generic `propagate` adapter.
- **Orbital utility:** `coe_to_cartesian`/`cartesian_to_coe` use radians and bound ellipses only. Active transform is `Rz(RAAN)*Rx(i)*Rz(argp)`. Returned angular fields other than inclination are wrapped to [0,2pi). Circular threshold e<=1e-10: argp=0 and nu is argument of latitude. Equatorial threshold |node|/|h|<=1e-10: RAAN=0. Circular equatorial orbits also set argp=0 and nu is longitude in the direction of orbital motion (negative y for retrograde i=pi). Measured a/e/i are retained. Undefined singular angles are not compared as if unique.

## Environmental models and selection

`EnvironmentalModels` contains `srp`, `drag` and `albedo` selections, each `{force,torque}`. All default disabled; forces and torques can be selected independently without reimplementing their physics. `drag_model` explicitly selects `DragModel::none`, `flat_plate` or `sentman`; enabled drag with `none` is rejected. `gravity_gradient` selects the independent inertia-based torque, and `earth_albedo` is a finite [0,1] radial-beam coefficient. Point-mass/J2 selection remains `EarthGravity(earth,enable_j2)`.

`EarthDynamics` validates drag selection and albedo bounds. `EnvironmentalDynamics` additionally checks a built-in `EarthEnvironment` at construction: enabled SRP/albedo force **or torque** requires a Sun provider, and enabled drag requires atmospheric acquisition. Custom snapshot sources retain their explicit caller-owned data contract:

| Enabled contribution | Required environment |
|---|---|
| Gravity / gravity-gradient only | No Sun provider or atmospheric density required |
| SRP | Valid Sun position and direct illumination; with `EarthEnvironment`, supply a Sun provider covering `epoch + elapsed` and select the eclipse model |
| Albedo | Valid Sun position; with `EarthEnvironment`, supply a covering Sun provider (direct-Sun illumination is not used by albedo) |
| Either drag model | Select `flat_plate` or `sentman` and supply density/rotation; with `EarthEnvironment`, enable atmospheric acquisition for nonzero model density |

An explicit/custom snapshot source can supply these fields without an ephemeris provider. A built-in `EarthEnvironment` that disables required acquisition is rejected when combined through `EnvironmentalDynamics`; standalone acquisition without selected forces remains valid. Explicit zero-density vacuum is supported by enabling acquisition with `ExponentialAtmosphere(0)`. Both SRP and albedo reject a zero Sun position during evaluation. Runtime input/geometry failures raise `std::invalid_argument`, atmosphere-domain failures `std::out_of_range`, and CSPICE coverage failures `std::runtime_error`; propagation passes RHS exceptions through and returns no partial trajectory. Source acquisition remains independent of model switches.

```cpp
leo::Spacecraft craft(10, leo::Inertia(Eigen::Matrix3d::Identity()),
    {leo::Surface(2, {1,0,0}, {0,0.1,0},
                  leo::OpticalProperties{0.3,0.2},
                  leo::AerodynamicProperties{2.2,0.9})});
leo::EnvironmentalModels models;
models.srp = {true,true};
models.drag = {true,true};
models.drag_model = leo::DragModel::sentman;
models.albedo = {true,true};
models.gravity_gradient = true;
leo::EarthDynamics rhs(leo::EarthGravity(leo::Earth{},true),
                      std::move(craft),models);
// Explicit snapshot: rhs(leo::ElapsedTime{t}, state, snapshot).
// Or wrap rhs with EnvironmentalDynamics(rhs, EarthEnvironment(...)).
```

### Radiation and illumination

`OpticalProperties(specular,diffuse)` specifies fractions of incident power, both nonnegative with sum <=1; the remainder is absorption. For unit source direction `s` toward the Sun in body coordinates, outward normal `n`, incidence `c=n·s>0`, pressure `P` and area `A`:

```text
F_body = -P A c [(1-specular)s + (2 specular c + 2 diffuse/3)n]
```

The incident-momentum coefficient includes absorption **and diffuse reflection**. The 2/3 diffuse recoil assumes Lambertian reflection. Face-on absorbing, specular and diffuse surfaces give `PA`, `2PA` and `5PA/3`, respectively, away from the source. Grazing/back-facing surfaces give zero. `solar_radiation_pressure` uses `P=(1361 W/m² / 299792458 m/s)*(AU/|sun-r|)²*illumination`, with AU=149597870700 m. It returns force and torque from one calculation.

`illumination_factor` is separate from SRP: `none` returns 1 without needing geometry; `cylindrical` is a hard-edged anti-solar cylinder; `conical` uses apparent angular radii `asin(R_sun/|sun-r|)` and `asin(R_earth/|r|)` and solar/Earth disk overlap, reaching exact sunlight and umbra endpoints with continuous penumbra. It assumes a small uniform-brightness solar disk and angular-plane overlap; no limb darkening, atmospheric refraction or scattered umbral light. Shadowed models require finite geometry outside Earth (and outside the solar photosphere).

### Atmosphere and aerodynamics

`atmospheric_relative_velocity(r,v,rotation)` is `v - rotation × r` at the spacecraft **centre of mass** for both drag laws; all panels use that same relative flow. Local surface velocity from body rotation, `omega × r_panel` (COM-relative panel offset), is not added. This approximation can matter for large or rapidly spinning spacecraft when panel rotational speed is appreciable relative to COM wind speed. Default atmospheric co-rotation is 7.292115e-5 rad/s about fixed J2000 +Z. Its wind/corotation law still does not model precession, nutation or winds; the optional empirical density acquisition below separately uses explicit Earth-orientation kernels.

`ExponentialAtmosphere(scale=1)` uses the Julia altitude/density anchors from 0 to 35786 km with **continuous log-linear interpolation**. Each interval is an exponential with scale height derived from adjacent densities. This preserves endpoint density fixtures while eliminating the original stitched profile's upward jumps. The default density at 400 km is 2.62e-12 kg/m³. Scale must be finite and nonnegative (zero mutes density). Unsupported finite altitudes throw `std::out_of_range`, rather than silently returning vacuum; nonfinite inputs throw `std::invalid_argument`. This is **not NRLMSISE**: spherical density only, no latitude, local-time, solar activity or temperature dependence. The sparse low-altitude anchors and non-LEO extension are numerical reference data, not fidelity claims.

Optional `leo::atmosphere` adds direct, pinned `leo::atmosphere::Nrlmsise00` and `Jb2008` reference adapters, selected with `LEO_ENABLE_NRLMSISE00` / `LEO_ENABLE_JB2008` (both default OFF). External source paths are `LEO_NRLMSISE00_ROOT` and `LEO_JB2008_SOURCE`; JB additionally needs GNU Fortran. No sources/data are downloaded by CMake and no redistribution permission is inferred from public download. The kernel remains independent of both empirical models.

With CSPICE enabled, `DirectAtmosphere(Model, spice, weather, "ITRF93")::evaluate(Epoch, position_J2000)` supplies density, temperatures and available constituent diagnostics. It uses model-specific latitude, explicit WGS84 height, LSK-based UTC and strict archived weather, including JB's 1/2/5-day proxy lags. Missing drivers/orientation fail rather than falling back. Exponential selection retains the original spherical height. Feed the returned density into the existing `EnvironmentSnapshot`; force laws, torque, corotation and solver defaults remain unchanged. No atmospheric result caching or interpolation is implemented. [Atmospheric reference documentation, validation and direct benchmarks](docs/atmosphere/README.md).

For JB2008, an already-acquired `const auto sun = spice.observe_sun(epoch)` may be supplied as `atmosphere.evaluate(epoch, position, &sun)`. `SunObservation` has immutable fields and provider-only construction fixing Sun/Earth/J2000/NONE/SI geometry. Exact ET bits and the managed process-wide kernel generation are checked; mismatches trigger the independent Sun lookup. Two-argument evaluation remains standalone. NRLMSISE-00 and exponential ignore the optional Sun. This shares environmental input, not SRP: no SRP object or enabled force is required. `EvaluationOptions` independently controls UTC reuse, Sun sharing and orientation reuse.

`AerodynamicProperties(Cd=2.2,accommodation=0.9)` attaches nonnegative finite flat-plate Cd and accommodation in [0,1] to each surface.

- `flat_plate_drag`: `F=-0.5 rho |v_rel|² Cd A max(0,n·vhat) vhat`; projected area and force opposing relative velocity. Normal faces incoming molecules when `n·vhat>0`.
- `sentman_drag`: a **high-speed, cold-beam free-molecular approximation**, **not the full finite-temperature Sentman model**. It assumes collisionless flow with negligible incident thermal velocity spread relative to bulk COM wind and negligible re-emission speed. Specular fraction is `1-sigma`, diffuse fraction `sigma`. It omits the incident Maxwellian velocity distribution, finite-temperature re-emission recoil, and gas/surface-temperature, molecular-mass and speed-ratio dependence; no such inputs are supplied. For grazing angle `alpha` (0 parallel, pi/2 face-on):

```text
Cn = (4-2 sigma) sin(alpha)^2
Ct = 2 sigma sin(alpha) cos(alpha)
Cd = Cn sin(alpha) + Ct cos(alpha)
Cl = Cn cos(alpha) - Ct sin(alpha)
```

`sentman_coefficients` independently exposes Cd/Cl. Per-surface force is `-0.5 rho |v_rel|² A (Cn n + Ct t_hat)` with `t_hat` along the tangential incoming velocity; evaluation uses an algebraic form with no singular lift-axis normalization. Fully diffuse cold re-emission gives Cd=`2 sin(alpha)` and no lift; fully specular interaction is normal to the plate. Both models have exact zero-density/zero-wind limits and derive torque from that same force.

### Gravity gradient and simplified albedo

`gravity_gradient_torque` computes `3 mu/|r|³ * (rhat_body × I*rhat_body)` directly in body coordinates, independent of panels. Spherical inertia or a principal-axis radial vector gives zero.

`simplified_albedo` is explicitly a **radial-beam approximation**, not resolved Earth radiance. Pressure is

```text
P_reflected = P_1AU (AU/|sun|)^2 earth_albedo (R_earth/|r|)^2 max(0,rhat·sunhat)
```

The source direction for panel incidence is toward Earth, `-rhat`; the exact same optical law and torque calculation as SRP applies. `earth_albedo` is an effective coefficient for this simplified beam, not a calibrated Bond/geometric albedo conversion. The local-day cosine switches off the night side; direct-Sun eclipse is not multiplied into Earth-reflected radiation. Units are pressure, not solar flux: solar irradiance is divided by light speed once. No Earth disk integration, cloud/land maps, infrared emission, seasonal reflectance or panel self-occlusion.

### CSPICE Sun provider

Install CSPICE separately, then configure:

```sh
cmake -S . -B build-spice -DBUILD_TESTING=ON -DLEO_BUILD_EXAMPLES=ON \
  -DLEO_ENABLE_CSPICE=ON -DCSPICE_ROOT=/path/to/cspice
cmake --build build-spice --parallel
ctest --test-dir build-spice --output-on-failure
./build-spice/propagate_environment /path/to/naif0012.tls /path/to/de432s.bsp 0
```

`CSPICE_ROOT` contains `include/SpiceUsr.h` and `lib/` with `cspice.a`, `libcspice.a` or a shared CSPICE library; explicit `CSPICE_INCLUDE_DIR` and `CSPICE_LIBRARY` also work. The [NAIF N0067 Linux distribution](https://naif.jpl.nasa.gov/pub/naif/toolkit/C/PC_Linux_GCC_64bit/packages/) supplies a prebuilt library. Kernel files are supplied by the caller, never downloaded or loaded by a force model.

`SpiceEphemerisProvider({kernel_paths...})` is noncopyable and loads exactly the listed files. It serves only NAIF Sun 10 relative to Earth 399, J2000, geometric `NONE`, with SI position and velocity. Nonfinite ET, unsupported targets, missing files, bad kernels and coverage gaps raise exceptions; Sun queries never extrapolate, fall back or cache. `epoch_from_utc` is an optional explicit conversion requiring a loaded leap-seconds kernel; ET queries themselves need only appropriate SPK coverage.

CSPICE is process-global. Provider calls, reuse hits and library-managed kernel mutations share one mutex and set SPICE's error action to RETURN. External CSPICE calls require application-wide quiescence; external reconfiguration must be followed by `SpiceEphemerisProvider::invalidate_reuse()` before library calls resume. Arbitrary unreported external mutation is unsupported. Shared providers refcount loaded file paths and unload them when the last owner dies; no `kclear`. Do not independently load a listed file elsewhere and expect it to remain loaded after provider destruction. Provider sets are not isolated ephemeris contexts: overlapping coverage follows SPICE's process-global kernel precedence. Use one explicit kernel set for a propagation.

`utc_calendar(epoch, reuse=true)` retains each request's `et2utc_c("ISOC",9)` conversion, seconds-of-day and leap-aware quasi-MJD. A process-owned 32-date FIFO cache reuses only exact civil-day metadata (midnight ETs, rounded day length, serial date and day-of-year). Every managed load/unload attempt invalidates reuse under the SPICE mutex, including partial-load failure. Parsing and date arithmetic run outside that mutex; a generation change retries the conversion. Pass `false` for independent diagnostic evaluation.

`earth_fixed_rotation(epoch, frame, reuse=true)` uses a process-owned 256-matrix cache (64 sets, four FIFO ways). Keys contain exact ET bits, `ITRF93`/`IAU_EARTH`, fixed J2000-to-Earth direction and managed kernel generation. No rounding or interpolation; misses call `pxform_c` directly. Hits use the existing SPICE mutex, so a managed mutation cannot race an obsolete hit. Pass `false` to disable this cache independently.

The environmental example runs a six-face rigid box for 60 s with J2, SRP, Sentman drag, albedo and all environmental torques, printing Sun distance, illumination, density, angular acceleration and final state diagnostics.

### Optional interpolated Sun provider (experimental)

`leo/environment/interpolated.hpp` supplies a CSPICE-independent, header-only
`InterpolatedEphemerisProvider(source, start_epoch, end_epoch, interval_seconds)`.
It owns SI position/velocity samples, including both endpoints and a shortened
last interval, and uses cubic Hermite interpolation with analytic derivative
velocity. The source is needed only during construction. Queries allocate
nothing and have no mutable cursor or global state; backward adaptive queries
are supported. Unsupported targets/nonfinite epochs raise `invalid_argument`;
out-of-coverage epochs raise `out_of_range`, never extrapolation. Invalid ranges,
unrepresentable grids, source coverage failures and nonfinite samples are rejected.
`sample_count()` and `memory_bytes()` expose preprocessing storage (object plus
owned vector capacity, excluding allocator bookkeeping).

```cpp
#include "leo/environment/interpolated.hpp"
// Explicitly opt in; the original direct provider remains unchanged.
leo::InterpolatedEphemerisProvider cached(sun, epoch,
    leo::Epoch{epoch.et_seconds + duration_s}, 300.0);
leo::EarthEnvironment source(earth, epoch, &cached, true);
```

The interval above illustrates the API, **not a production recommendation**.
Stage 4 evaluation rejected all tested 60/300/900/1800 s intervals for the fixed
six-hour coupled-attitude workload: small ephemeris errors did not guarantee
acceptable final trajectory, attitude or cumulative SRP work differences.
No production interval is selected, no default changes, and no accepted
performance improvement is claimed. Use direct SPICE unless the complete
application workload passes its own fixed correctness criteria.
Eclipse geometry still uses the current spacecraft state on every RHS call.

See [benchmark methodology](benchmarks/README.md),
[OPT-001 evidence and decision](benchmarks/OPTIMISATIONS.md) and
[performance summary](benchmarks/PERFORMANCE_SUMMARY.md).

## Numerical correctness audit

The numerical audit fixes independently reproduced eclipse cancellation,
small-angle/apparent-radius conditioning, near-full-accommodation Sentman lift
and inconsistent accepted inertia operators. It does **not** establish a
converged six-hour fully coupled reference: independently refined solutions
still disagree, and the original cache intervals remain rejected.
Default tolerances, physical approximations and the cache implementation are
unchanged. See [audit scope and reproduction](docs/numerical-audit/README.md),
[defect ledger](docs/numerical-audit/DEFECTS.md) and
[convergence assessment](docs/numerical-audit/CONVERGENCE.md).


## Propagation and errors

`propagate(rhs,initial,epoch,sample_times_s,options)` returns `Trajectory {epoch,samples}`; each sample contains typed elapsed time and named State. The RHS may be any compatible callable, without virtual dispatch or `std::function`.

Sample times must be nonempty, finite, start exactly at zero and strictly increase. A single zero returns the validated initial state. Forward propagation clips steps to exact requested samples; no interpolation or accepted-step history is returned. Output storage is reserved once; stepping uses fixed arrays.

Optional independent-trajectory concurrency: include `leo/integration/batch.hpp`
and call `leo::propagate_batch(factory, initials, epoch, sample_times, options,
workers)`. The factory receives `(index, initial)`
and creates a trajectory-owned RHS on its worker; concurrent factory calls must
be safe. One shared epoch/options/schedule, original input ordering, independent
solver/output state. Default one worker runs inline; parallel calls create and
join bounded C++20 workers using dynamic atomic-index assignment internally.
There is no selectable scheduling policy. Existing `propagate()` is unchanged.
All trajectories continue after a trajectory failure; after joining,
`BatchPropagationError` reports the lowest failing input index and original
exception pointer. Borrowed environmental resources must outlive the call;
keep one stable SPICE configuration and retain native-model locks. See
[batch design and scaling evidence](docs/multithreading/README.md).
For SPICE-heavy application workloads, [MT-002](docs/multithreading/MT002.md)
compares independently launched processes without extending the generic C++ API.

The adapter uses Boost.Odeint Dormand–Prince 5(4) and explicitly owns current/trial derivative buffers, including rejection and quaternion projection. Maximum component error is scaled by `atol_field + rtol*max(abs(current),abs(trial))`. Defaults:

|Parameter|Default|
|---|---:|
|Relative tolerance|1e-10|
|Position absolute tolerance|1e-3 m|
|Velocity absolute tolerance|1e-6 m/s|
|Quaternion absolute tolerance|1e-12|
|Angular-rate absolute tolerance|1e-12 rad/s|
|Initial step|1 s|
|Maximum step|15 s|
|Total attempted-step limit|1000000|

The 15 s maximum was selected during analytical validation: the proposed 30 s default slightly exceeded the 1e-9 ten-orbit conservation bound; 15 s reduced measured energy drift below 3.9e-11 without relaxing that bound. Tolerances and steps must be finite and positive, maximum step >= initial step, attempted-step limit nonzero.

Invalid caller inputs throw `std::invalid_argument`. Detected nonfinite stage/state/derivative/error, invalid trial quaternion (even on a rejected attempt), stalled step or attempt exhaustion throws `std::runtime_error`; no partial trajectory is returned. Exceptions explicitly thrown by user RHS code propagate unchanged.

Near-zero trial quaternions remain an intentional hard numerical failure, even when the error estimate would otherwise reject the candidate. Their attitude is undefined, while torque-free quaternion kinematics preserve nonzero norm. No Stage 1 physical case has demonstrated a need to recover from this failure; adaptive retries are not used to mask it. Invalid initial quaternions still fail input validation, and nonfinite states/derivatives still fail explicitly. Rejected valid candidates retain the original state and derivative; accepted candidates are normalized and refresh the derivative when needed.

## Analytical validation

Catch2 test families: Stage 1 `[frames]`, `[attitude]`, `[gravity]`, `[orbital]`, `[integration]`, `[two_body]`, `[j2]`; Stage 2 `[geometry]`, `[environment]`, `[radiation]`, `[atmosphere]`, `[flatplate]`, `[sentman]`, `[gravity_gradient]`, `[environment_integration]` and optional `[spice]`. Julia is reference material, not a numerical oracle or dependency.

Validation covers frame/rotation conventions, invalid quaternion and full-inertia boundaries, gravity signs/covariance/scaling, orbital round-trips and singular gauges, ten-period circular and elliptic trajectories against an independent Kepler solve, period/apsides/energy/angular momentum, solver transitions and failures, principal-axis analytic attitude and general torque-free conservation. Fourteen-day J2 arcs validate prograde/retrograde RAAN rates, 30 s versus 15 s step convergence, total J2 potential energy and axial angular momentum, perigee rate and critical inclination. Secular fits discard the first orbit and use unwrapped angles with centred least squares; fitting and invariant helpers are test-only.

Stage 2 additionally validates geometric/material invariants, epoch/coverage handling, atmospheric wind signs and co-rotation, illumination endpoints/penumbra continuity/symmetry, optical momentum fixtures (including the fully diffuse `5PA/3` regression), pressure/density/speed/radius scaling, COM/lever-arm torques, body-frame covariance, Sentman coefficient limits and continuous monotonic density. Short deterministic propagation covers shadowed/front/back SRP, drag attitude/co-rotation, off-centre attitude feedback, gravity-gradient rotation and independent force/torque selection.

CSPICE tests create a deterministic local SPK to validate real kernel interpolation, SI units, J2000 orientation, ownership and coverage errors offline. Optional real-kernel checks explicitly skip unless `LEO_SPICE_TEST_SPK`/`LEO_SPICE_TEST_LSK` paths are set; CI supplies pre-fetched DE432s/NAIF0012 and runs the environmental executable. CSPICE itself is an external prebuilt dependency, not sanitizer-instrumented by this project.

## Deliberate corrections relative to Julia

- SRP now pushes away from the Sun; its torque includes the same illumination and optical response as force.
- Environmental torques are all body-frame. Julia's RHS rotates accumulated torques even though gravity-gradient torque is already body-frame.
- Both aerodynamic models use one co-rotating wind definition and outward-normal incidence. Julia's grazing-angle mapping/coefficient approximation makes face-on drag vanish; this is replaced by the documented free-molecular limit. Its unused Cd parameter is now meaningful only for flat-plate drag.
- Atmosphere naming is honest, and log interpolation eliminates inconsistent stitched scale-height jumps.
- Penumbra overlap replaces tanh smoothing with discontinuous endpoints.
- Albedo force/torque now share one dimensionally valid law. Julia's torque used W/m² as pressure and incorrectly scaled solar irradiance with Earth-spacecraft distance, unlike its force.
- Explicit TDB epoch plus elapsed seconds replaces ambiguous Unix-time handling, date-string truncation and hidden kernel downloads. Direct SPICE interpolation replaces Julia's optional daily position cache.

Stages 1–3 cover propagation, the requested environmental model set, reusable analysis, CSV export and headless plotting with the documented approximations. Stage 4 (optimisation and benchmarks) remains out of scope; Python distribution ships as described under [Installation](#installation). Finite-temperature Sentman, NRLMSISE, resolved Earth radiance and more accurate Earth orientation require separately scoped fidelity work; none is claimed here.
