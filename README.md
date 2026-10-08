# LEO dynamics — Stage 2

C++20, CPU-only, double-precision, lightweight Earth-centred 6-DoF rigid-spacecraft dynamics: point-mass/J2 gravity, panel radiation and aerodynamic forces, their torques, and gravity-gradient torque. This is a numerical LEO kernel, not a high-fidelity mission-analysis framework. No plotting, control systems, third-body gravity, flexible bodies, GPU support or optimisation subsystem.

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

`leo::kernel` links only Eigen. `leo::integration` adds Boost.Odeint. Integration defaults ON; tests and examples default OFF. The optional `leo::environment` component adds CSPICE Sun ephemerides; `leo::kernel` never links CSPICE. Environment acquisition and the solver wrapper are in `environment/source.hpp`, separate from individual force laws.

Kernel-only build:
```sh
cmake -S . -B build-kernel -DLEO_BUILD_INTEGRATION=OFF -DBUILD_TESTING=ON
cmake --build build-kernel --parallel
ctest --test-dir build-kernel --output-on-failure
```

Requesting examples with integration OFF is a configuration error. CI defines GCC/Clang Release and Clang Debug ASan/UBSan validation, full environmental propagation and kernel-only builds. Catch2 v3.7.1 and CSPICE N0067 are bootstrapped outside project configure in CI. Tests never fetch dependencies or kernels.

## Public contracts

Headers live under `include/leo`; namespace `leo`.

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

Environment compatibility is **not fully validated at construction**. `EarthDynamics` checks drag selection and albedo bounds, but `EnvironmentalDynamics` does not check whether its source supplies the data needed by enabled forces **or torques**:

| Enabled contribution | Required environment |
|---|---|
| Gravity / gravity-gradient only | No Sun provider or atmospheric density required |
| SRP | Valid Sun position and direct illumination; with `EarthEnvironment`, supply a Sun provider covering `epoch + elapsed` and select the eclipse model |
| Albedo | Valid Sun position; with `EarthEnvironment`, supply a covering Sun provider (direct-Sun illumination is not used by albedo) |
| Either drag model | Select `flat_plate` or `sentman` and supply density/rotation; with `EarthEnvironment`, enable atmospheric acquisition for nonzero model density |

An explicit/custom snapshot source can supply these fields without an ephemeris provider. With `EarthEnvironment`, a null provider leaves the Sun position at zero; atmospheric acquisition disabled leaves density at zero. Neither is automatically rejected as incompatible: drag is then muted, while SRP without a provider may fail during propagation **or use incorrect Sun geometry without an immediate error**. Albedo rejects the zero Sun position during evaluation. Runtime input/geometry failures raise `std::invalid_argument`, atmosphere-domain failures `std::out_of_range`, and CSPICE coverage failures `std::runtime_error`; propagation passes RHS exceptions through and returns no partial trajectory. Source acquisition is independent of model switches, so configured Sun/atmosphere acquisition can still fail when those contributions are disabled.

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

`atmospheric_relative_velocity(r,v,rotation)` is `v - rotation × r` at the spacecraft **centre of mass** for both drag laws; all panels use that same relative flow. Local surface velocity from body rotation, `omega × r_panel` (COM-relative panel offset), is not added. This approximation can matter for large or rapidly spinning spacecraft when panel rotational speed is appreciable relative to COM wind speed. Default atmospheric co-rotation is 7.292115e-5 rad/s about fixed J2000 +Z. Precession, nutation, winds and Earth-orientation data are not modelled.

`ExponentialAtmosphere(scale=1)` uses the Julia altitude/density anchors from 0 to 35786 km with **continuous log-linear interpolation**. Each interval is an exponential with scale height derived from adjacent densities. This preserves endpoint density fixtures while eliminating the original stitched profile's upward jumps. The default density at 400 km is 2.62e-12 kg/m³. Scale must be finite and nonnegative (zero mutes density). Unsupported finite altitudes throw `std::out_of_range`, rather than silently returning vacuum; nonfinite inputs throw `std::invalid_argument`. This is **not NRLMSISE**: spherical density only, no latitude, local-time, solar activity or temperature dependence. The sparse low-altitude anchors and non-LEO extension are numerical reference data, not fidelity claims.

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

`SpiceEphemerisProvider({kernel_paths...})` is noncopyable and loads exactly the listed files. It serves only NAIF Sun 10 relative to Earth 399, J2000, geometric `NONE`, with SI position and velocity. Nonfinite ET, unsupported targets, missing files, bad kernels and coverage gaps raise exceptions; there is no extrapolation/fallback or interpolation cache. `epoch_from_utc` is an optional explicit conversion requiring a loaded leap-seconds kernel; ET queries themselves need only appropriate SPK coverage.

CSPICE is process-global. Provider calls are mutex-serialized and set SPICE's error action to RETURN; other code using SPICE must coordinate separately. Shared providers refcount loaded file paths and unload them when the last owner dies; no `kclear`. Do not independently load a listed file elsewhere and expect it to remain loaded after provider destruction. Provider sets are not isolated ephemeris contexts: overlapping coverage follows SPICE's process-global kernel precedence. Use one explicit kernel set for a propagation.

The environmental example runs a six-face rigid box for 60 s with J2, SRP, Sentman drag, albedo and all environmental torques, printing Sun distance, illumination, density, angular acceleration and final state diagnostics.


## Propagation and errors

`propagate(rhs,initial,epoch,sample_times_s,options)` returns `Trajectory {epoch,samples}`; each sample contains typed elapsed time and named State. The RHS may be any compatible callable, without virtual dispatch or `std::function`.

Sample times must be nonempty, finite, start exactly at zero and strictly increase. A single zero returns the validated initial state. Forward propagation clips steps to exact requested samples; no interpolation or accepted-step history is returned. Output storage is reserved once; stepping uses fixed arrays.

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

Stage 2 covers the requested environmental model set with the stated approximations. Stage 3 (analysis/plotting) and Stage 4 (optimisation, benchmarks, packaging) remain out of scope. Finite-temperature Sentman, NRLMSISE, resolved Earth radiance and more accurate Earth orientation would require separately scoped fidelity work; none is claimed here.
