# LEO dynamics — Stage 1

C++20, CPU-only, double-precision Earth-centred propagation: point-mass gravity, additive J2 and torque-free rigid-body attitude. No other forces or torques, GPU support, runtime SPICE calls, caching, plotting or trajectory files.

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

`leo::kernel` links only Eigen. `leo::integration` adds Boost.Odeint. Integration defaults ON; tests and examples default OFF. Kernel-only build:

```sh
cmake -S . -B build-kernel -DLEO_BUILD_INTEGRATION=OFF -DBUILD_TESTING=ON
cmake --build build-kernel --parallel
ctest --test-dir build-kernel --output-on-failure
```

Requesting examples with integration OFF is a configuration error. CI defines GCC/Clang Release and Clang Debug ASan/UBSan validation, including both example modes and kernel-only builds. Catch2 v3.7.1 is explicitly bootstrapped outside project configure in CI.

## Public contracts

Headers live under `include/leo`; namespace `leo`.

- **State:** named `State {r,v,q,omega}`. Earth-relative position in metres and velocity in m/s use `InertialVector` (J2000); angular velocity in rad/s uses `BodyVector`. Minimal frame tags disallow implicit body/inertial mixing. Raw 13-scalar packing belongs only to the integration adapter.
- **Attitude:** Hamilton quaternion `{w,x,y,z}`, active body → J2000; identity `{1,0,0,0}`. Body-frame angular velocity satisfies `q_dot = 0.5*q ⊗ (0,omega)`. Quaternion derivatives and intermediate solver states are not normalized. `validated_state` normalizes input; every accepted solver step projects q and refreshes its endpoint derivative if projection changes q. Quaternion sign is not canonicalized. Nonfinite quaternions and norm <=1e-12 are errors, never replaced by identity.
- **Spacecraft:** construction requires positive finite mass and full finite symmetric positive-definite body inertia. Both are private; `double mass_kg() const noexcept` returns mass by value and `const Inertia& inertia() const noexcept` provides read-only access. There are no setters; replacing a spacecraft with another valid value preserves its invariants, so `EarthDynamics` does not revalidate mass. `Inertia` stores the matrix and an LLT factorization, not its inverse. Symmetry is checked relative to the largest matrix coefficient with tolerance 1e-12. Mass does not scale gravitational acceleration. Torque-free Euler dynamics use `I*omega_dot = -omega × (I*omega)`.
- **Earth/gravity:** `Earth` defaults to mu=3.986004418e14 m³/s², equatorial radius=6378136.3 m, J2=1.08262668e-3. `point_mass_acceleration` and `j2_acceleration` are independent functions; `EarthGravity(Earth,bool)` composes them. J2 is additive and its symmetry axis is **fixed J2000 +Z**, a Stage 1 approximation, not Earth orientation modelling. Finite interior positions are permitted; collision handling is not implemented.
- **Dynamics:** `EarthDynamics(EarthGravity,Spacecraft)` is callable as `StateDerivative(ElapsedTime,const State&)`. Pure numerical RHS, independent of solvers, ephemerides and output. Acceleration is J2000, angular acceleration body-frame. Future torque contributions must remain body-frame.
- **Time:** `ElapsedTime {seconds}` is forward elapsed dynamics time. `Epoch {et_seconds}` is TDB ephemeris seconds past J2000, retained only as trajectory metadata. No UTC/calendar conversion.
- **Environment:** `environment/ephemeris.hpp` is an external `EphemerisProvider` contract only: Earth observer, J2000, geometric NONE correction, SI position/velocity. No provider implementation or runtime dependency exists in Stage 1; future implementations must explicitly reject invalid epochs, targets and coverage failures.
- **Orbital utility:** `coe_to_cartesian`/`cartesian_to_coe` use radians and bound ellipses only. Active transform is `Rz(RAAN)*Rx(i)*Rz(argp)`. Returned angular fields other than inclination are wrapped to [0,2pi). Circular threshold e<=1e-10: argp=0 and nu is argument of latitude. Equatorial threshold |node|/|h|<=1e-10: RAAN=0. Circular equatorial orbits also set argp=0 and nu is longitude in the direction of orbital motion (negative y for retrograde i=pi). Measured a/e/i are retained. Undefined singular angles are not compared as if unique.

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

Catch2 test families: `[frames]`, `[attitude]`, `[gravity]`, `[orbital]` in `leo_kernel_tests`; `[integration]`, `[two_body]`, `[j2]` in `leo_integration_tests`. Julia is reference material, not a numerical oracle or dependency.

Validation covers frame/rotation conventions, invalid quaternion and full-inertia boundaries, gravity signs/covariance/scaling, orbital round-trips and singular gauges, ten-period circular and elliptic trajectories against an independent Kepler solve, period/apsides/energy/angular momentum, solver transitions and failures, principal-axis analytic attitude and general torque-free conservation. Fourteen-day J2 arcs validate prograde/retrograde RAAN rates, 30 s versus 15 s step convergence, total J2 potential energy and axial angular momentum, perigee rate and critical inclination. Secular fits discard the first orbit and use unwrapped angles with centred least squares; fitting and invariant helpers are test-only.

Stage 2: remaining forces/torques. Stage 3: analysis/plotting. Stage 4: optimisation, benchmarks and packaging. None is implemented here.
