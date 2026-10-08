# Stage 3: analysis, CSV and headless visualization

## Architecture and API

`leo::analysis` is a separate CMake target linking `leo::kernel`, not Boost,
CSPICE or Python. Its public headers are `leo/analysis/orbital.hpp`, `loads.hpp`
and `export.hpp`. Analysis consumes `Trajectory` and explicitly supplied Earth,
spacecraft, dynamics configuration and sampled environment snapshots. The
integration adapter and physical laws are unchanged.

`EarthGravity::evaluate` exposes point-mass and selected J2 acceleration.
`EarthDynamics::evaluate` exposes selected SRP/drag/albedo wrenches,
gravity-gradient torque, total non-gravitational force, total body torque,
translational acceleration and angular acceleration. The RHS calls this same
boundary; diagnostics do not implement another force law. Accessors for gravity,
spacecraft and model selection are read-only. `non_gravitational.force_n` is the
sum of non-gravitational forces; its `torque_nm` is **total** body torque,
including gravity-gradient torque. Angular acceleration includes the gyroscopic
term, so it is not simply torque divided by inertia.

| API (`leo::analysis`) | Result |
|---|---|
| `validate_trajectory` | Finite epoch/states, nonzero radius, nonnegative strictly increasing sample times |
| `orbital_history(trajectory, earth)` | Radius/altitude, six osculating elements, peri/apo radius/altitude, specific energy and angular momentum, unwrapped angular histories |
| `unwrap_angles(span)` | Shortest-increment angular unwrapping in radians |
| `detect_apsides(trajectory, tolerance)` | Peri/apo times, sampled-radius interpolation, interpolation brackets |
| `per_orbit(trajectory, earth)` | Orbit index, boundary times, completeness, periods, endpoint elements and unwrapped changes |
| `environmental_history(trajectory, dynamics, snapshots)` | Exact selected Stage 2 loads, with snapshots retained |
| `attitude_history(trajectory, spacecraft, environment)` | Quaternion, body rates/speed, rotational energy, inertial momentum magnitude, torque, relative ZYX Euler, per-surface geometry/areas |
| `rtn_basis(state)`, `rtn_components(basis, vector)` | Right-handed RTN basis and frame-tagged RTN components |
| `srp_work(trajectory, earth, mass, environment)` | Force/acceleration, power, RTN power, cumulative work/specific work, orbital-energy change/residual, Sun-inbound/outbound transverse work |
| `work_per_orbit(orbits, work)` | Work/RTN work/specific work, energy changes and residuals over identical intervals |
| `analyze(trajectory, dynamics, snapshots)` | Complete `AnalysisResult`, with epoch, Earth, mass and model metadata |
| `export_csv(directory, trajectory, result)` | Nine deterministic CSV files |

All numerical quantities are SI; angles are radians. Position/velocity, forces,
accelerations and surface normals are Earth-centred J2000. Torques, rates and
angular acceleration are body frame. The Hamilton scalar-first quaternion is
an active body-to-J2000 rotation. `RtnVector` has its own frame tag: x=R, y=T,
z=N, and cannot be implicitly mixed with J2000/body vectors.

`orbital_history` reuses Stage 1 `cartesian_to_coe`. It accepts bound elliptic
orbits only and retains Stage 1 gauges: equatorial RAAN=0; circular argp=0;
circular anomaly is argument of latitude or orbital-direction longitude. The
Stage 1 thresholds remain e<=1e-10 and |node|/|h|<=1e-10. These are **osculating
two-body elements**, not fitted secular/mean elements. Near a gauge transition,
unwrapped argp/RAAN histories are not physically unique.

## Events, sampling and work accuracy

Apsis detection finds sign changes of radial velocity `dot(r/|r|, v)` and uses
linear interpolation of that velocity to estimate the zero time. Radius is
linearly interpolated between sampled radii, **not** a high-order extremum
estimate. Exact isolated sample zeros are counted once, including endpoint
apsides inferred from the adjacent sign. Radial velocities <=1e-12 of the largest
sampled speed are treated as numerical zero. A radius range <=1e-8 of maximum
radius has no identifiable apsides (`circular_tolerance` is caller-adjustable).
A trajectory that does not resolve a sign change cannot recover the missing
event. A zero plateau is not a uniquely identifiable isolated extremum.

Noncircular segmentation uses periapsis-to-periapsis boundaries. Circular
segmentation (all sampled e<=1e-8) uses advancing longitude phase in the initial
orbital plane, with the initial Stage 1 circular angle as the origin. It does
not fabricate geometric periapsides. Strong plane evolution invalidates this
fixed-plane circular phase approximation. Partial first/last intervals are
included and flagged `complete=0`. `period_s` is the observed boundary spacing
only for a complete orbit; it is zero for a partial interval. `orbit_index`
counts exported intervals, including partial ones; count complete rows for the
number of complete observed orbits. One-sample trajectories have no intervals.
Endpoint elements are linearly interpolated sampled diagnostics; angular
changes use the unwrapped histories, not subtraction of wrapped angles.

Angular unwrapping requires changes below pi per sample. Segmentation rejects
sample intervals >=half the local two-body period, but that check cannot detect
every phase alias, especially near high-eccentricity periapsis. Use adequately
dense output, particularly at periapsis and eclipse boundaries. No integrator
event solver, dense-output event accuracy or continuous-model work accuracy is
claimed.

RTN is `R=r/|r|`, `N=(r cross v)/|r cross v|`, `T=N cross R`.
Zero/nonfinite radius or angular momentum is an error. Each power contribution
is `F_component * v_component`; full power is **`F_SRP dot v`** in W. Work is a
trapezoidal integral of sampled power using actual, possibly irregular times.
Specific work is work/mass, in J/kg. Per-orbit work uses the analytic integral
of the same piecewise-linear power at fractional boundaries (quadratic
cumulative interpolation), including cancellation cases. It therefore
partitions the same sampled integral, rather than linearly interpolating work.

Sun-inbound means positive `dot(v, unit(Sun-r))` (toward Sun), outbound negative.
A sign crossing splits a sample interval using linear interpolation of this
speed and integrates transverse power separately on each side. No solar data,
or zero projected Sun speed throughout an interval, leaves this interval
unclassified in the inbound/outbound fields. `cumulative_work_t_j` always
contains the full transverse integral. This classification is a plotting
workflow, not a new force model.

Energy fields use `specific_energy = |v|²/2 - mu/|r|` and multiply its change by
spacecraft mass. `energy_minus_srp_work_j` is **not** solely numerical error:
J2's omitted potential, drag/albedo and sampling error can all contribute.
Under point-mass gravity plus only SRP, refined sampling should make energy
change and SRP work agree. Rotational energy is `omega dot I omega / 2`;
inertial angular momentum is the magnitude of `R(q) I omega`.

Relative Euler display is `R(q(t)) R(q(0))^-1 = Rz(yaw) Ry(pitch) Rx(roll)`.
At gimbal lock (`1-|sin(pitch)|<1e-12`) roll=0 and yaw retains the observable
combination, wrapped to [-pi,pi]. `euler_singular` flags loss of uniqueness;
quaternion histories remain authoritative. Quaternion sign changes do not
change these Euler values.

Surface incidence is `acos(normal_J2000 dot unit(Sun-r))`. The illuminated
projected area of each one-sided surface is
`area * max(cos(incidence),0) * illumination`. Aerodynamic projected area uses
the Stage 2 `atmospheric_relative_velocity` direction and outward-normal
incidence; zero relative flow gives zero area. Geometric projected area is not
Sentman's force coefficient. Surface indices preserve spacecraft order.

## CSV schema, units and missing diagnostics

CSV schema version 1: UTF-8, header first, comma separator, period decimal,
classic locale, LF newlines, `max_digits10` (17 significant digits for doubles).
All trajectory rows are exported; no decimation, implicit unit conversions or
silent zip/truncation. Sample tables share exact `time_s` values. Long-format
surfaces contain one row per surface per sample. Header-only apsides/per-orbit/
surfaces files are valid when there are no events/intervals/surfaces.

| File | Columns / conventions |
|---|---|
| `metadata.csv` | `key,value`: schema_version, initial_epoch_et_seconds, epoch_scale=TDB_seconds_past_J2000, elapsed_time_unit=s, inertial_frame=J2000, quaternion_convention, orbital_elements=osculating_two_body_not_mean, earth_mu_m3_s2, earth_equatorial_radius_m, earth_j2, j2_enabled, surface_count, mass_kg, independent force/torque enable flags, gravity_gradient_enabled, drag_model |
| `trajectory.csv` | time_s; x_m,y_m,z_m; vx_m_s,vy_m_s,vz_m_s; qw,qx,qy,qz; omega_x_rad_s,omega_y_rad_s,omega_z_rad_s |
| `orbital_elements.csv` | time_s,radius_m,altitude_m,a_m,e,i_rad,raan_rad,arg_periapsis_rad,true_anomaly_rad,periapsis_radius_m,apoapsis_radius_m,periapsis_altitude_m,apoapsis_altitude_m,specific_energy_j_kg,h_x_m2_s,h_y_m2_s,h_z_m2_s,raan_unwrapped_rad,arg_periapsis_unwrapped_rad,true_anomaly_unwrapped_rad,specific_kinetic_energy_j_kg,specific_potential_energy_j_kg |
| `attitude.csv` | time_s,angular_speed_rad_s,rotational_energy_j,inertial_momentum_kg_m2_s,torque_x_nm,torque_y_nm,torque_z_nm,torque_magnitude_nm,relative_yaw_rad,relative_pitch_rad,relative_roll_rad,euler_singular |
| `surfaces.csv` | time_s,surface_index,normal_x,normal_y,normal_z,sun_incidence_rad,projected_illuminated_area_m2,projected_aerodynamic_area_m2,orbit_normal_angle_rad |
| `environment.csv` | time_s,illumination,density_kg_m3; x/y/z triples for point_mass and j2 (`_m_s2`), srp_force/drag_force/albedo_force/total_non_gravitational_force (`_n`), srp_torque/drag_torque/albedo_torque/gravity_gradient_torque/total_torque (`_nm`), total_acceleration (`_m_s2`), total_angular_acceleration (`_rad_s2`); drag_model. Example: srp_force_x_n. Triples appear in the order listed; drag_model last. |
| `work.csv` | time_s,srp_power_w,cumulative_srp_work_j,specific_srp_work_j,orbital_energy_change_j,specific_energy_change_j_kg,energy_minus_srp_work_j; srp_force_r_n,srp_force_t_n,srp_force_n_n; srp_power_r_w,srp_power_t_w,srp_power_n_w; cumulative_work_r_j,cumulative_work_t_j,cumulative_work_n_j; sun_inbound_transverse_work_j,sun_outbound_transverse_work_j |
| `per_orbit.csv` | orbit_index,start_time_s,end_time_s,complete,period_s,start_a_m,start_e,start_i_rad,start_raan_unwrapped_rad,end_raan_unwrapped_rad,delta_a_m,delta_e,delta_i_rad,delta_raan_rad,delta_arg_periapsis_rad,delta_true_anomaly_rad,srp_work_j,specific_srp_work_j,srp_work_r_j,srp_work_t_j,srp_work_n_j,orbital_energy_change_j,energy_minus_srp_work_j |
| `apsides.csv` | time_s,kind,radius_m,left_index,fraction; kind=0 periapsis,1 apoapsis; bracket indices zero-based |

Disabled model contributions are exactly zero, never omitted columns. Drag
model values: 0=none, 1=flat plate, 2=Sentman. Booleans are 0/1. Missing solar
position (zero Earth-to-Sun position) makes surface incidence/illuminated area
`nan`. Calling attitude analysis without environment assumes torque-free loads
and gives `nan` aerodynamic area too. All other exported fields must be finite.
Environment acquisition remains caller-controlled: a supplied zero density is
not distinguishable from intentionally disabled atmospheric acquisition;
illumination is the supplied snapshot field, not recomputed by analysis.
Snapshots must have finite epoch exactly equal to trajectory epoch+sample time.
Supply real solar geometry whenever solar physics is enabled; Stage 2's
constructor does not validate source/model compatibility.

The exporter validates counts, timestamps, finite outputs and event/interval
brackets before opening files, then creates the directory and replaces these
nine named files in deterministic order. Invalid data raises
`std::invalid_argument`; filesystem/write failures raise exceptions with paths.
Writes are not transactional: an I/O failure may leave a partial export. The
plotter rejects missing/malformed columns, unexpected NaN/infinity, mismatched
sample grids and missing/duplicate per-surface samples rather than dropping rows.

## Reproducible end-to-end commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DLEO_BUILD_EXAMPLES=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/analyze_orbit /tmp/leo-analysis

python3 -m venv .venv
.venv/bin/python -m pip install -r plotting/requirements.txt
.venv/bin/python plotting/plot_analysis.py /tmp/leo-analysis \
  --output /tmp/leo-analysis/plots --format both
.venv/bin/python tests/plotting_smoke.py
```

The deterministic example uses a constructed stationary Sun, conical eclipse,
700 km semi-major-axis altitude, e=.02, inclined orbit and a six-face spacecraft.
It enables J2, SRP, Sentman drag, albedo, their torques and gravity-gradient torque,
propagates 2.5 nominal periods (751 samples), analyzes and exports. Its stationary
Sun is **not a mission ephemeris**. No SPICE kernels or downloads are required.

For caller-provided real Sun ephemerides:

```sh
cmake -S . -B build-spice -DCMAKE_BUILD_TYPE=Release \
  -DLEO_BUILD_EXAMPLES=ON -DLEO_ENABLE_CSPICE=ON -DCSPICE_ROOT=/path/to/cspice
cmake --build build-spice --parallel
./build-spice/analyze_orbit /tmp/leo-spice-analysis \
  --spice /path/to/naif0012.tls /path/to/de432s.bsp 0
.venv/bin/python plotting/plot_analysis.py /tmp/leo-spice-analysis \
  --output /tmp/leo-spice-analysis/plots --format png
```

The final argument is TDB ET seconds, not UTC/Unix seconds. Coverage failures
propagate; no fallback ephemeris is used. `LEO_BUILD_ANALYSIS` defaults ON and may
be disabled for kernel-only builds. `LEO_TEST_PLOTTING` defaults OFF; set ON with
`BUILD_TESTING` to register the Python smoke test in CTest. Python is not located
or required by CMake unless that optional test is requested.

## Plot inventory and supplied datasets

The default command produces 26 PNG figures (52 files with `--format both`) for
a trajectory containing surfaces and orbit intervals. Names are stable stems:
`orbit_3d`, `altitude_vs_time`, `orbital_elements`, `orbital_angles_unwrapped`,
`apsides_evolution`, `per_orbit_elements`, `attitude_quaternion`,
`attitude_angular_velocity`, `attitude_rotational_state`, `attitude_relative_euler`,
`surface_incidence`, `surface_projected_area`, `environment_force_contributions`,
`environment_torque_contributions`, `gravity_acceleration`,
`environment_total_acceleration`, `environment_drag`,
`environment_illumination_density`, `srp_force`, `srp_power`, `srp_force_rtn`,
`srp_cumulative_work`, `orbital_energy`, `orbital_energy_vs_srp_work`, `srp_transverse_work`,
`srp_work_per_orbit`. With no surfaces/intervals only applicable figures are
produced; explicitly selecting an unavailable figure errors. Use repeated
`--only NAME` options to select figures. Gravity accelerations and environmental
forces have separate plots/units; torque plots are body frame. No plotting path
calls physical models or silently thins data.

`compare DIR1 DIR2 ... --output DIR --format both` overlays supplied runs for
altitude, a, e, i, unwrapped RAAN/argp, SRP power/work and transverse work. This
supports model-on/off experiments without adding a propagation/ensemble engine.

A sweep manifest is caller-supplied CSV:

```csv
data_dir,raan_deg,pitch_deg
run-000,0,0
run-001,90,0
run-002,0,45
run-003,90,45
```

Paths resolve relative to the manifest. The reducer reads exported
`orbital_elements.csv` and emits a **numeric-only** summary:
`raan_deg,pitch_deg[,ltan_h],max_delta_a_m,time_to_max_s`. It uses all samples,
`max(a)-a_initial`, the first maximum in case of ties and its actual `time_s`.
An optional `--baseline-a-m` uses an explicitly supplied baseline. Without
`--sun-ra-deg`, RAAN heatmaps need no solar conversion; with it, LTAN is
`(12+(RAAN-SunRA)/15) mod 24`. A manifest-supplied `ltan_h` is retained instead.
This is a mean local-solar-time geometric convention; no equation-of-time or
Earth-orientation corrections are inferred.

```sh
.venv/bin/python plotting/plot_analysis.py reduce sweep.csv --output summary.csv --sun-ra-deg 281.70
.venv/bin/python plotting/plot_analysis.py heatmap summary.csv \
  --x ltan_h --y pitch_deg --value max_delta_a_m --output gain.png
.venv/bin/python plotting/plot_analysis.py heatmap summary.csv \
  --x raan_deg --y pitch_deg --value time_to_max_s --output time-to-maximum.svg
```

Any supplied numeric CSV parameter table works with `heatmap`. Duplicate
coordinates error; incomplete rectangular grids error unless `--allow-missing`
explicitly requests hatched cells. Nonuniform coordinates are rendered with
midpoint cell edges. `--log-scale` requires positive finite values.

`series INPUT.csv --x COLUMN --y COLUMN [--y COLUMN ...] --output FILE`
plots supplied diagnostic columns without deriving physics. Optional title,
axis labels and `--x-scale`/`--y-scale` support already-computed legacy thermal,
energy or scalar experiment data. No thermal/optimisation model is ported.

## Julia reconnaissance and feature mapping

This checkout has `src/Plotting.jl`, `Propagator.jl`, `SolarSailPropagator.jl`,
`OrbitState.jl`, `examples/*.jl`, root `test.jl`, and `legacy/` analyses.
`Analysis.jl`, `EnsembleIO.jl` and the advertised experiments directories are
absent; the nested `Project-Svarog-Orbital/` directory is empty. Useful analysis
is embedded in plotting functions/root scripts; export is in the propagator.
No Julia runtime is required for C++ or Python validation.

| Julia functionality | C++ analysis / Python equivalent |
|---|---|
| `rv2coe` in `OrbitState.jl`; embedded elements in `plot_orbital_elements` | Stage 1 conversion reused by `orbital_history`; `orbital_elements` + unwrapped plots |
| `plot_orbit_3d`, `plot_altitude` | `trajectory.csv`, `orbital_history`; `orbit_3d`, `altitude_vs_time` |
| `_peri_plot`, `find_periapsis_indices`, inclination/RAAN at periapsis | `detect_apsides`, `per_orbit`; `apsides_evolution`, `per_orbit_elements` |
| `plot_attitude` quaternion-relative yaw/pitch/roll and body rates | `attitude_history`; quaternion/rates/relative ZYX Euler plots |
| `plot_sail_plane_angle` | Per-surface `orbit_normal_angle_rad`; `surface_incidence` |
| `plot_force_contributions`, `plot_torque_contributions` | Shared `EarthDynamics::evaluate`, `environmental_history`; distinct force/gravity/torque plots |
| `plot_srp_contribution` | Selected SRP force and acceleration; `srp_force` and RTN plot |
| `plot_srp_transverse_work` (one-orbit inbound/outbound classification) | Full `srp_work` transverse histories/classification, exact `work_per_orbit`; transverse work plot. Per-orbit rows identify intervals for downstream selection rather than silently falling back to another orbit. |
| `plot_srp_net_work_per_orbit` | `work_per_orbit`; total and RTN per-orbit work plot |
| Root `test.jl` max delta-a/time-to-maximum RAAN or LTAN/pitch maps | `reduce` supplied manifest, `heatmap` numeric summary |
| `examples/on_off.jl`, `earth_orbit*.jl`, `visualisation.jl` | General `compare` supplied runs and deterministic/caller-SPICE example |
| `earth_ensemble_gpu.jl` trajectory grid and JLD2 names | Supplied manifest/parameter table; no ensemble generation or GPU port |
| `Propagator.jl` `TrajectoryData`, JLD2/chunked output | Existing C++ `Trajectory` + deterministic CSV export; no JLD2/custom binary dependency |
| `legacy/statusOrbitalEnergy.jl`, `plotEnergy`, MATLAB energy plots | Two-body kinetic/potential/total specific energy via `orbital_history` and `orbital_energy` plot, mass-scaled energy change/residual. Multi-body potentials remain outside the Earth-bound kernel; `series` also displays externally supplied energy columns. |
| Legacy temperature plots/MAT export, heliocentric planet overlays/live animation | Supplied scalar series supported; thermal physics, planet propagation, MAT/JLD2 readers and interactive/live views deliberately not ported (outside current Earth-bound Stage 1/2 models and headless CSV architecture) |
| Legacy sensitivity/genetic optimisation plots | Already-computed numeric series supported; generation, sensitivities, fitness optimisation and mission controls are Stage 4/out of scope |

## Correctness issues in the Julia analysis

- `Plotting.jl:277` pads the last `dt` and integrates through the final point,
  adding a non-existent interval. `:346` uses a fixed 100-second multiplier
  instead of actual timestamps; thinning (`:31-39`) makes this worse.
- `:289-292` / `:342-346` calculate **transverse-only** power, not full SRP
  work. The port labels transverse work separately from `F dot v` total work.
- `:298-305` calls positive velocity toward the Sun "outbound" and negative
  "inbound"; the port uses the physical directional meaning.
- `:201` and `:231-242` detect wraps with hard degree thresholds and fixed
  4000/5000-second exclusions. They can miss coarse-sampled crossings, impose
  LEO-specific periods and are unsuitable for singular circular anomaly.
  The port has explicit sampled events and circular phase segmentation.
- `OrbitState.jl:101-105` divides by eccentricity when obtaining argp/anomaly,
  without a circular gauge. Stage 1's deterministic circular handling is reused.
- `test.jl:19,57` omits the +12-hour local-noon offset in RAAN-to-LTAN mapping.
  `:72` silently limits processing to 9,000,000 seconds and `:80` reconstructs
  time from a fixed step instead of exported `times`.
- `legacy/statusOrbitalEnergy.jl:74-82` reduces spacecraft position to scalar
  radius before subtracting a planet position vector in `calcEnergy` (the
  vectorized `calcEnergyVec` does retain the full position). That mission-specific
  multi-body potential is not reproduced by the Earth-bound energy diagnostic.

## Validation and remaining limitations

Analytical C++ tests cover circular/elliptic invariants, propagated two-body
constancy, J2 nodal trends, singular gauges/unwrapping, exact/interpolated apsides,
periods, partial/circular segments, real-model load reconstruction/switches,
frame transforms, projected areas, rotational invariants, quaternion-sign
invariance at Euler lock, zero/eclipsed SRP, irregular constant-force work,
fractional-boundary cancellation, RTN power/work sums and energy-work balance.
CSV tests check precision, values, ordering, complete row counts, deterministic
bytes, missing surface diagnostics and malformed data/paths. Plot smoke fixtures
exercise all 26 PNG/SVG outputs headlessly, circular/no-surface/single-sample
exports, malformed/truncated tables, surface completeness, manifest reduction,
first-maximum timestamps, LTAN, holes/duplicates/log-scale errors and comparisons.

CI retains GCC, Clang and ASan/UBSan, all previous tests, kernel-only builds and
real CSPICE tests, adding Python fixtures and both end-to-end plotting paths.
Hosted CI status is not inferred from local runs. CSPICE is an external prebuilt
library, not sanitizer-instrumented here.

Sampling-limited events/work, fixed-initial-plane circular segmentation,
nontransactional CSV I/O and in-memory trajectories/analysis remain deliberate
limitations. Plots keep every exported row; extremely large datasets can consume
substantial memory/rendering time. Existing environmental fidelity limits remain
unchanged. No optimisation, benchmarks, controls, ephemeris caching, parallel
propagation, GUI or packaging cutover is included. Stage 4 has not started.
