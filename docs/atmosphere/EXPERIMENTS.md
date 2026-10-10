# Atmospheric direct-reference experiments

Procedure, numerical diagnostics and interpretation for the uncached
NRLMSISE-00 / JB2008 / exponential baselines.

Scope note: this document owns the *experiment harness*. Model implementation,
parameters and validation live in the sibling documents; `DECISIONS.md` is the
append-only decision log and is not edited here.

## 1. What is measured

Three independent measurements, each into its own directory:

| Directory | Command | Content |
|---|---|---|
| `coordinates/` | `--mode coordinates` | UTC calendar and Earth-fixed rotation products at the epoch, for external comparison |
| `fixtures/` | `--mode fixtures` | direct evaluation over epoch x latitude x local time x altitude grids for all three models |
| `propagate/` | `--mode propagate` | matched CASE-A/B/C six-hour propagation per model, plus an independent refinement |
| `timing/` | `--mode timing` | repeated raw direct-evaluation and full-propagation timing distributions |

`run.py` writes `manifest.json` at the top of the output directory recording
every hash, the compiler and flags, the CPU, the threading environment and the
exact commands.

## 2. No caching, by construction

Every density reported anywhere in this harness comes from a fresh call into the
selected reference model. The executable contains:

- no density cache,
- no time-keyed memoisation,
- no spatial density grid or tile,
- no interpolation between model evaluations,
- no surrogate, quantised or approximate evaluation path,
- no precomputed trajectory sampling.

The only thing loaded once is the immutable space-weather record set
(`WeatherDataset`). That is input data, not a density cache: it is a lookup of
*driver records* by date, and each density still costs one full model
evaluation. The measured `nanoseconds_per_direct_evaluation` includes that
driver lookup, so the published baseline is an upper bound on the cost of a
single model call.

No performance shortcut was introduced to improve any measured number.

## 3. Build

The experiment has its own standalone CMake root and needs no edit to the
project's root `CMakeLists.txt`:

```sh
cmake -S docs/atmosphere -B build-atmosphere \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG" \
  -DCSPICE_ROOT=/path/to/cspice \
  -DLEO_NRLMSISE00_ROOT=/path/to/nrlmsise-00 \
  -DLEO_JB2008_SOURCE=/path/to/JB2008.f \
  -DCMAKE_Fortran_COMPILER=/path/to/gfortran-13
cmake --build build-atmosphere --target atmosphere_experiment
```

Configure downloads nothing. `LEO_NRLMSISE00_ROOT` must already contain the
acquired, hash-checked upstream C sources, and `LEO_JB2008_SOURCE` must point at
the acquired official `JB2008.f`; the backend CMake fragments verify both hashes
and fail loudly on a mismatch. Note the two are different variable kinds: the
NRLMSISE option is a directory root, the JB2008 option is a single source file.
There is no `LEO_JB2008_ROOT`.

JB2008 requires GNU Fortran; its CMake fragment rejects other compiler ABIs
rather than producing an unvalidated binary. Pass it explicitly with
`-DCMAKE_Fortran_COMPILER=...`.

Backends are selectable independently. With only one enabled, the harness still
runs, and the model that was not compiled in is listed in
`models_not_compiled_in`. It is never silently omitted from the design and never
substituted.

## 4. Run

```sh
python3 docs/atmosphere/run.py \
  --binary build-atmosphere/atmosphere_experiment \
  --build-dir build-atmosphere \
  --out docs/atmosphere/evidence/direct-YYYYMMDD \
  --lsk   /path/to/naif0012.tls \
  --spk   /path/to/de432s.bsp \
  --pck   /path/to/earth_000101_270105_261009.bpc \
  --frame ITRF93 \
  --weather /path/to/SpaceWeather-All-v1.2_snapshot_20200224.txt \
  --solfsmy /path/to/SOLFSMY.TXT \
  --dtcfile /path/to/DTCFILE.TXT \
  --epoch-utc 2003-10-30T00:00:00 \
  --case docs/numerical-audit/experiments/CASE-A-v1.cfg \
  --case docs/numerical-audit/experiments/CASE-B-v1.cfg \
  --case docs/numerical-audit/experiments/CASE-C-v1.cfg \
  --build-command "cmake -S docs/atmosphere -B build-atmosphere -DCMAKE_BUILD_TYPE=Release -DCSPICE_ROOT=... -DLEO_NRLMSISE00_ROOT=... -DLEO_JB2008_SOURCE=... -DCMAKE_Fortran_COMPILER=/path/to/gfortran-13" \
  --build-command "cmake --build build-atmosphere --target atmosphere_experiment" \
  --note "storm interval, October 2003"
```

`--build-command` and `--test-command` are recorded verbatim in the manifest.
They are not executed by the runner; they record what actually produced the
binary whose results are being published.

`--out` must be absent or empty. The runner refuses a populated directory and
the executable refuses one too, so a measurement run can never overwrite
historical evidence. `--note` records why a given run was made.

Subcommand arguments can also be passed directly, which is useful when
reproducing a single measurement:

```sh
build-atmosphere/atmosphere_experiment \
  --lsk ... --spk ... --pck ... --frame ITRF93 \
  --weather ... --solfsmy ... --dtcfile ... \
  --epoch-utc 2003-10-30T00:00:00 \
  --case docs/numerical-audit/experiments/CASE-A-v1.cfg \
  --mode propagate --out /tmp/case-a-storm
```

A coordinates-only run needs no weather dataset and no CASE file:

```sh
build-atmosphere/atmosphere_experiment \
  --lsk ... --spk ... --pck ... --frame ITRF93 \
  --epoch-utc 2003-10-30T00:00:00 \
  --mode coordinates --out /tmp/coords
```

The same is available through the runner, which then omits the weather options:

```sh
python3 docs/atmosphere/run.py \
  --binary build-atmosphere/atmosphere_experiment \
  --build-dir build-atmosphere --out /tmp/coords-run \
  --lsk ... --spk ... --pck ... --modes coordinates \
  --epoch-utc 2003-10-30T00:00:00
```

## 5. Defaults, and why those defaults

| Setting | Value | Source |
|---|---|---|
| Duration | 21600 s | the representative six-hour requirement |
| Output cadence | 30 s | the historical numerical-audit cadence |
| `rtol` | 1e-10 | existing project default, unchanged |
| `max_step_s` | 15 | existing project default, unchanged |
| Position / velocity atol | 1e-3 m, 1e-6 m/s | existing project defaults, unchanged |
| Quaternion / body-rate atol | 1e-12 | existing project defaults, unchanged |
| Initial step | min(1 s, max_step) | existing project default |
| `max_attempts` | 1000000 | existing `IntegrationOptions` default, unchanged |
| Nominal integrator | Dopri5 via `leo::propagate` | production path, untouched |
| Refinement | Boost controlled RKF7(8) | independent tableau, embedded order, controller |
| Refinement tolerances | rtol 1e-12, max step 5 s | deliberately tighter than nominal |

The absolute tolerances are the project defaults. They are not tuned per
model, and they are the same for every model, so a difference between two runs
comes from the atmosphere model and not from a different integration
configuration.

**No numerical default was raised for these experiments.** `rtol`, `max_step`,
`initial_step`, all four absolute tolerances and `max_attempts` are the existing
project values, unchanged. The attempt limit is in particular *not* raised to
make a run survive: exhausting it is a failure signal, and widening it would
hide exactly that. The only differing values are the two named refinement
tolerances, and only for the RKF7(8) run. `max_attempts` is recorded in both
the manifest and each propagation record.

## 6. What is fixed across the compared runs

Only the atmosphere model and its required external drivers change between the
models inside one case. Held identical:

- spacecraft mass, inertia tensor and every surface (area, normal, centre of
  pressure, specular/diffuse fractions, drag coefficient, accommodation);
- initial position, velocity, attitude quaternion and body rate, taken from the
  same CASE definition file through `docs/numerical-audit/experiments/scenario.hpp`;
- force and torque laws: J2 gravity, conical SRP with torque, Sentman drag with
  torque, simplified albedo with torque, gravity-gradient torque;
- the atmospheric corotation vector `(0, 0, 7.292115e-5) rad/s`;
- the SPICE ephemerides, Earth-orientation kernels and frame;
- integrator configuration and absolute tolerances;
- output timestamps.

Model selection matches the numerical-audit `full` scenario exactly. Nothing in
`EarthDynamics`, `sentman_drag`, `flat_plate_drag`, `surface_wrench`,
`EarthEnvironment` or `leo::propagate` was changed for this work.

### Epoch

The historical numerical audit used `ET = 0`. This harness uses a **real
historical epoch**, because the space-weather and Earth-orientation datasets are
historical. Results are therefore **not** comparable with the older `ET = 0`
evidence, and are not claimed to be. `propagation-comparison.json` records this
explicitly in `historical_evidence_note`.

## 7. Coordinates and time (`coordinates/`)

`--mode coordinates` needs only the ephemeris and orientation kernels. It loads
no weather dataset and evaluates no atmosphere model, so it can be run and
re-inspected independently of any driver data.

It publishes the raw products:

- the UTC calendar: year, month, day, day of year, seconds of day, **day length
  in seconds**, quasi-MJD, days since 1970, and a flag identifying a leap-second
  day. A stretch of 86401 seconds is visible here rather than silently
  normalised away.
- the full `earth_fixed_rotation` 3x3 matrix, and its transpose, so a consumer
  need not assume which direction it is.
- the J2000 axes expressed in the Earth-fixed frame, and the equinox offset from
  the fixed x axis, so the frame's right-ascension convention can be read off
  directly.
- orthogonality residual and determinant of the rotation.

**This mode is an observation surface, not a validation.** Every value comes from
the same CSPICE wrapper the propagator uses. Comparing it against another
invocation of that wrapper proves nothing about the convention. Independent
qualification requires a *different* implementation — a SOFA/IERS stack such as
`astropy` + `pyerfa` — compared against these published products. Until that
comparison exists and is recorded, the frame convention is documented and
unverified, and this document does not claim otherwise.

The harness's own `frame_direction_check` (section 8, "Direct fixtures") verifies
only the *internal consistency* of the grid construction, not the correctness of
the frame convention against an outside authority.

## 8. Direct fixtures (`fixtures/`)

Grid, chosen once and identical for every model:

- longitudes `0, 60, 120, 180, -120, -60` deg. Six-hour spacing sweeps local
  solar time through a full 24 h cycle at a fixed epoch.
- latitudes `-80, -60, -30, 0, 30, 60, 80` deg.
- altitudes `200` to `1000` km in `50` km steps.
- fixture epochs supplied on the command line (`--fixture NAME=UTC_ISO`),
  covering quiet, active and storm conditions across two seasons.

Each grid point is a real geodetic Earth-fixed location converted to a J2000
position through the shared SPICE Earth-fixed rotation, and then handed to the
shared provider. **This file never re-derives a model input convention.** Each
model receives its own published latitude, local-solar-time and index convention
from the provider, so a cross-model difference at a matched physical location is
a model-form difference and not a coordinate-bookkeeping difference.

`local_solar_time_hours` is reported in the output only to make the grid
readable (UTC + longitude/15 h). It is *not* the value passed to any model.

Before building any grid point, the harness verifies its own assumption about
the direction of the Earth-fixed rotation: it round-trips every grid point
through J2000 -> Earth-fixed -> J2000 and back through the geodetic conversion,
and records the worst position, altitude and orthogonality residuals as
`frame_direction_check`. If that check fails the run aborts rather than quietly
evaluating every fixture at the antipode of the requested location.

Rows have a fixed width and are positional, matching the existing audit
convention. A model output that is not natively produced is marked by
`has_temperatures` / `has_number_density` being `0`; those columns must not be
read as measured values. A model that was not compiled into the build is listed
in `models_not_compiled_in`, never replaced by a substitute model.

### Grid endpoints are not clamped

The grid runs to 1000 km, which is exactly the upper bound of the NRLMSISE-00
supported range. The grid point is built by round-tripping through the geodetic
conversion, so the altitude handed to the model can land a few times 1e-12 km
*above* 1000 and be rejected by the adapter's domain check.

That rejection is left to happen. **No grid altitude is clamped, nudged or
rounded.** Clamping would evaluate the model at a different altitude than the
one requested and publish it as though it were the requested point, which is the
kind of quiet narrowing this harness exists to avoid. If the endpoint does
reject, the correct response is a validation guard with an explicit, documented
round-off policy — not a silent adjustment of the grid.

## 9. Propagation evidence (`propagate/`)

Per sample, at the requested cadence:

- full output state (position, velocity, quaternion, body rate);
- altitude, density, illumination, dynamic pressure;
- drag acceleration magnitude, aerodynamic drag torque magnitude, total torque
  magnitude, angular speed;
- cumulative drag work and cumulative non-gravitational work;
- matched specific energy change `K + U_central + U_J2`;
- attitude angle from the initial attitude, inertial angular-momentum norm error.

Orbital, load and attitude diagnostics come from the existing `leo::analysis`
routines (`orbital_history`, `environmental_history`, `attitude_history`), not
from a second implementation written here.

### Energy diagnostic

`matched_specific_energy` is

```
E = |v|^2/2 - mu/r + (mu/r) J2 (Re/r)^2 (3 (z/r)^2 - 1)/2
```

This is the same central + J2 potential as the implemented fixed-J2000-Z gravity
law, and the same convention as `tests/j2_tests.cpp` and
`docs/numerical-audit/experiments/energy.py`. Point-mass energy is *not*
conserved under J2 and oscillates far more than the modelled work, so the
point-mass value would misread reversible exchange with the omitted J2
potential as decay. The matched form is reported for that reason.

The reported residual is the matched energy change minus the **output-sampled**
non-gravitational work, both mass-scaled. It contains output quadrature error,
integration error and model error. It is **not** a certified physical error and
is not reported as one.

Work is accumulated by trapezoidal quadrature on the 30 s output grid. A finer
output cadence narrows the quadrature contribution; the propagation itself is
unaffected. This is stated here because a residual that does not shrink with
cadence would otherwise be misread as model error.

### CASE-C

CASE-C remains a sensitive stress test. **No convergence or attitude threshold
is asserted for it**, and none is implied by its presence in the output. The
refinement differences are reported so the sensitivity is visible and
quantified, not so a pass/fail can be read off them.

### Independent refinement

Every nominal run is repeated with Boost's controlled RKF7(8): a different
tableau, embedded order, error checker and step adjuster, at tighter tolerances.
Because the two methods integrate the same differential equations with the same
drivers, the differences between them are *numerical integration differences
only*. They are reported per case and per model as `refinement_*` fields.

If a refinement ever reproduced the nominal run bit-for-bit, that would mean the
two methods are not independent evidence. That coincidence is reported as
`refinement_bitwise_identical_to_nominal` rather than passing silently as
convergence.

A refinement that fails is recorded as `refinement_succeeded: false` with its
error. It is never dropped.

## 10. Separating the four sources of difference

The output is structured so these are not conflated:

1. **Numerical integration differences** — the `refinement_*` fields of each
   run: same equations, different method.
2. **Model-to-model differences** — the `*_vs_*` pairwise blocks of
   `propagation-comparison.json`: same integrator, same inputs, different
   atmosphere model.
3. **Space-weather input changes** — the fixture epochs, which span quiet,
   active and storm conditions and two seasons at identical grid points.
4. **Remaining physical-model uncertainty** — not measurable here. Sentman is a
   cold-beam free-molecular approximation, corotation is a fixed rate, and the
   two empirical models differ in form. None of the reported numbers bounds
   these, and none is presented as if it did.

## 11. Timing (`timing/`)

Per model, two clearly separated measurements plus the coupled propagation:

**`raw_native` — the upstream reference call alone.** The model-native input is
built once, before every timed loop, and reused unchanged; the loop calls
`Nrlmsise00::evaluate`, `Jb2008::evaluate` or
`ExponentialAtmosphere::density_kg_m3` directly. This excludes the CSPICE
Earth-fixed rotation, the geodetic conversion, the UTC calendar conversion and
the space-weather record lookup. It is the honest "cost of one reference-model
evaluation" number. The full native input — every index, lag-resolved value, the
24 NRLMSISE switches, and the JB2008 Sun and satellite angles — is written to
`raw_native.native_input` so the measurement is reproducible.

**`provider_path` — the full acquisition path.** `DirectAtmosphere::evaluate`,
timed the same way, which *does* include SPICE and the weather lookup. The gap
between the two is the acquisition overhead, and it is published as such rather
than folded into either number.

Both report a true per-evaluation distribution: each call is timed
individually, not averaged over a batch. `batch_seconds` and
`nanoseconds_per_direct_evaluation` are retained as repeated batch means for
cross-checking, and `batched_note` says so.

Also recorded:

- a warm-up pass before each measured loop, excluded from it;
- `repeated_evaluation_density_difference_kg_m3`: repeated evaluation at one
  fixed input, exposing any non-determinism in the upstream model;
- `native_vs_provider_max_density_difference_kg_m3`: both paths describe the
  same physical state and should agree; a non-zero value means they did not;
- `concurrency`: wall time and throughput at each requested thread count;
- `propagation_*`: repeated full coupled six-hour propagations with
  `wall_seconds`, `rhs_calls`, `atmosphere_calls`, `nanoseconds_per_rhs` and
  `nanoseconds_per_atmosphere_call`.

Setup is measured separately in `setup`: kernel file hashes and sizes, weather
file hashes, and an explicit timed `WeatherDataset::load`. **No setup cost is
included in any per-evaluation or per-propagation timing**, and the native input
construction is likewise outside every timed loop. Reusing one prepared input
across timed calls is input preparation, not a density cache: no density result
is ever reused.

Every individual repeat is written to `propagation_repeats`. No repeat is
discarded and no percentile is dropped for being slow.

Concurrency is *measured*, not assumed. The NRLMSISE-00 and JB2008 adapters
serialise native calls on a process-wide mutex, because the upstream C and
Fortran models hold mutable global state; this is a correctness requirement, not
an optimisation choice. The exponential baseline performs no synchronisation, so
its scaling is a property of this harness rather than of the existing
production path, and is labelled as such.

## 12. Failure policy

A backend is unavailable only when it is **not compiled into the build**, which
is decided at compile time from `LEO_HAS_NRLMSISE00` / `LEO_HAS_JB2008` and
recorded in `models_not_compiled_in`.

A compiled-in backend that fails — missing driver coverage, an epoch outside
the data, a domain violation — **aborts the run**. It is never recorded as
"unavailable" and the run never continues without it, because a partial
comparison presented as a complete one silently substitutes a narrower
benchmark for the requested one. No model is ever substituted for a failing one.

## 13. Density definitions, recorded not assumed

The manifest records the density entry point and meaning for each model:

- **NRLMSISE-00**: `gtd7d`, output `d[5]`, total mass density *including*
  anomalous oxygen, in kg/m³ when `switches[0] = 1`. The `gtd7` variant omits
  anomalous oxygen and is not used here.
- **JB2008**: total mass density in kg/m³.

These are different model quantities: different constituent sets, different
temperature definitions, different fitted upper-boundary conditions. Their
ratio is a model-form difference, recorded as such, and is not evidence that
either implementation is wrong. Neither model is calibrated to the other.

Driver index lags (F10.7 previous UTC day, 81-day centred average, daily Ap,
three-hourly Ap history; JB2008 F/S 1 day, M 2 days, Y 5 days, hourly DSTDTC)
are recorded in the manifest as configuration, so a reader does not have to
infer them from the code.

### Altitude conventions differ per model, deliberately

This is a real model difference, not a bookkeeping slip, and it must not be
silently unified:

- **Exponential baseline**: the profile is spherically symmetric, so it takes
  the *spherical* altitude `|r| − R_e`. This preserves the actual piecewise
  exponential profile and keeps the lightweight baseline free of any SPICE
  transformation — the exponential branch needs no Earth-fixed rotation at all.
- **NRLMSISE-00 and JB2008**: the shared *geodetic* ellipsoidal height on
  WGS84. For JB2008 this is combined with its geocentric latitude.

The matched physical *positions* are identical across all three; only the
altitude each model is handed differs, because that is what each model defines.
Both altitudes are recorded side by side in
`raw_native.native_input.geodetic_altitude_m` and
`raw_native.native_input.spherical_altitude_m`, together with an explicit
`altitude_convention` string, so no reader can assume they are the same number.

The spherical-height-plus-UT1-minus-UTC-37s variant sometimes attributed to the
JB2008 reference driver is **not** used here: it is unsupported by the source
comments and inconsistent with the model input, and adopting it would silently
shift both the altitude and the time.

## 14. Reproducing a published result

1. Check `manifest.json` for the git SHA and the source tree hash.
2. Confirm the kernel, weather, external model source and case file hashes match
   your local copies (`external_sources` holds the acquired upstream files).
3. Confirm `compiler` (all three languages, with the real CMake flag variables),
   `dependencies`, `cpu` and `threading` match.
4. Rebuild using the recorded `build.build_commands`, then re-run the recorded
   `commands` into a fresh `--out`.

No flag value is hard-coded in the runner: compiler identity and every
C/C++/Fortran flag variable are read from the build tree's `CMakeCache.txt`, so
the manifest describes the build that actually ran rather than an assumed one.

Timings are machine-specific and will not reproduce on different hardware;
densities, ratios and propagation diagnostics are hardware-independent for a
fixed compiler and library.

## 15. Files

| Path | Owner | Role |
|---|---|---|
| `docs/atmosphere/experiment.cpp` | experiments | the executable |
| `docs/atmosphere/CMakeLists.txt` | experiments | standalone CMake root |
| `docs/atmosphere/run.py` | experiments | runner and manifest writer |
| `docs/atmosphere/EXPERIMENTS.md` | experiments | this document |
| `docs/atmosphere/DECISIONS.md` | parent | append-only decision log |