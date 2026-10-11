# ATM-OPT-001 — exact atmospheric preprocessing reuse

UTC civil-day metadata, compatible immutable Sun observations, and exact Earth-orientation matrices are independently controlled. No atmospheric approximation, density caching, interpolation, time quantisation, native-reference change or integrator setting change is introduced.

## Evidence map

- [DESIGN.md](DESIGN.md): interfaces, before/after call graphs, ownership and supported lifecycle.
- [DECISIONS.md](DECISIONS.md): stable decisions, alternatives, rationale and limits.
- [PARAMETERS.md](PARAMETERS.md): every cache and measurement parameter.
- [VALIDATION.md](VALIDATION.md): exact scientific comparisons and boundary/concurrency coverage.
- [BENCHMARKS.md](BENCHMARKS.md): workloads, measurement scopes and interpretation.
- [RESULTS.md](RESULTS.md): actual timings, counters and retention decisions.
- [evidence/manifest.json](evidence/manifest.json): revisions, commands, environment and hashes.
- `evidence/validation/`: raw original/candidate scientific series, actual native streams, comparisons and regression output.
- `evidence/measurements/`: original/candidate/diagnostic raw distributions, counters, summaries and benchmark manifest.
- `evidence/controls/`: complete scientific/native comparisons for all seven non-All controls, supplementing the All comparison.

Historical `direct-v1`, `direct-v2`, ATM-PROF-001 and numerical-audit evidence are read-only and not overwritten. Source baseline is revision `6a1ad59` (the profiling tooling was introduced in `4490b10`); neither changed production atmospheric calculations.

Reproduce the historical profiling harness from that unchanged worktree/revision (`generate_instrumented.py` requires the exact original function signatures and fails closed against any candidate change). Its exact-source instrumentation anchors are intentionally not rewritten for the new candidate interfaces: doing so would make a "reproduction" of ATM-PROF-001 measure code the historical evidence never contained. Historical ATM-PROF-001 conclusions therefore remain tied to its own recorded revision; all ATM-OPT-001 measurements use the original/candidate comparison builds documented here.

## Build

Dependencies must already exist. Configure never downloads data. These are the actual paths used for the published run; substitute equivalent hash-pinned dependencies on another machine.

```sh
git worktree add --detach /tmp/atm-opt-original 6a1ad59

cmake -S docs/atmosphere/optimisation -B build-atmosphere-optimisation -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DLEO_NRLMSISE00_ROOT=/tmp/leo-atmosphere-deps \
  -DLEO_JB2008_SOURCE=/tmp/leo-atmosphere-deps/JB2008.f \
  -DCMAKE_Fortran_COMPILER=/tmp/leo-fortran/usr/bin/gfortran-13 \
  -DCMAKE_PREFIX_PATH="/tmp/leo-deps/usr;/tmp/leo-deps/catch-install" \
  -DCSPICE_ROOT=/tmp/leo_cspice/cspice
cmake --build build-atmosphere-optimisation --parallel 4

cmake -S docs/atmosphere/optimisation -B build-atmosphere-optimisation-original -G Ninja \
  -DLEO_SOURCE_DIR=/tmp/atm-opt-original -DATM_OPT_CANDIDATE=OFF \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DLEO_NRLMSISE00_ROOT=/tmp/leo-atmosphere-deps \
  -DLEO_JB2008_SOURCE=/tmp/leo-atmosphere-deps/JB2008.f \
  -DCMAKE_Fortran_COMPILER=/tmp/leo-fortran/usr/bin/gfortran-13 \
  -DCMAKE_PREFIX_PATH="/tmp/leo-deps/usr;/tmp/leo-deps/catch-install" \
  -DCSPICE_ROOT=/tmp/leo_cspice/cspice
cmake --build build-atmosphere-optimisation-original --parallel 4

cmake -S . -B build-atmosphere-gcc -G Ninja \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release \
  -DLEO_ENABLE_CSPICE=ON -DLEO_ENABLE_NRLMSISE00=ON -DLEO_ENABLE_JB2008=ON \
  -DLEO_NRLMSISE00_ROOT=/tmp/leo-atmosphere-deps \
  -DLEO_JB2008_SOURCE=/tmp/leo-atmosphere-deps/JB2008.f \
  -DCMAKE_Fortran_COMPILER=/tmp/leo-fortran/usr/bin/gfortran-13 \
  -DCMAKE_PREFIX_PATH="/tmp/leo-deps/usr;/tmp/leo-deps/catch-install" \
  -DCSPICE_ROOT=/tmp/leo_cspice/cspice
cmake --build build-atmosphere-gcc --parallel 4
```

`atm_opt_benchmark_original` links only the unchanged original libraries and includes the original experiment translation unit. `atm_opt_benchmark` links uninstrumented candidate libraries. `atm_opt_benchmark_diagnostics` links separate diagnostic copies, including a generated private SPICE source with lookup timers; no diagnostic timer/logging reaches production. `optimisation_validation{,_original}` use GNU linker wrapping to capture real native JB2008 inputs/outputs without editing native sources. Both builds also retain an original/candidate `atmosphere_experiment` executable.

## Run serially

Use new output directories; both runners refuse populated directories. Do not benchmark concurrently with builds/tests or unrelated work.

```sh
python3 docs/atmosphere/optimisation/validate.py \
  --original-build build-atmosphere-optimisation-original \
  --candidate-build build-atmosphere-optimisation \
  --test-dir build-atmosphere-gcc --deps /tmp/leo-atmosphere-deps \
  --cpu 0 --out /tmp/atm-opt-001-validation-reproduction

python3 docs/atmosphere/optimisation/validate_controls.py \
  --baseline-evidence /tmp/atm-opt-001-validation-reproduction \
  --candidate-build build-atmosphere-optimisation --deps /tmp/leo-atmosphere-deps \
  --cpu 0 --out /tmp/atm-opt-001-controls-reproduction

python3 docs/atmosphere/optimisation/run.py \
  --build-original build-atmosphere-optimisation-original \
  --build-candidate build-atmosphere-optimisation \
  --original-source /tmp/atm-opt-original --deps /tmp/leo-atmosphere-deps \
  --single-cpu 0 --concurrency-cpus 0-3 \
  --out /tmp/atm-opt-001-benchmark-reproduction
```

Defaults execute original, all eight independent/combined candidate configurations and separate diagnostic runs. The six-hour CASE-A/B/C sweep repeats each configuration three times; fixed provider/concurrency/micro measurements have five batches. Complete raw distributions are retained, gzip-compressed where large. `analyse.py` exposes `analyse(path)` for lossless reanalysis of an existing benchmark directory.

## Library controls

```cpp
const leo::SunObservation sun = spice.observe_sun(epoch);
const leo::atmosphere::EvaluationOptions options{true, true, true};
const leo::atmosphere::DirectAtmosphere atmosphere(
    leo::atmosphere::Model::jb2008, spice, weather, "ITRF93", false, options);
const auto standalone = atmosphere.evaluate(epoch, position);
const auto shared = atmosphere.evaluate(epoch, position, &sun);
```

The option order is UTC reuse, Sun sharing, orientation reuse. The experiment accepts `--utc-reuse on|off`, `--sun-sharing on|off`, `--orientation-reuse on|off`; standalone evaluation still works with sharing enabled. Static SPICE helpers independently accept a reuse boolean. Cache ownership is process-wide and synchronised, not hidden last-value storage.

Kernel contract: managed loads/unloads invalidate under the existing SPICE mutex. External mutation is supported only at application-wide quiescence followed by `SpiceEphemerisProvider::invalidate_reuse()`. Unreported external mutation is unsupported. Keep one scientific configuration fixed throughout each propagation.
