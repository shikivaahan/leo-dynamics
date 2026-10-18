# v0.1 Python binding validation report

Qualification record for the pybind11 bindings and the `leo_dynamics` Python
package added after the C++ engine was qualified at `0110469`.

Scope: verification of the implemented v0.1 Python interface described in
[python-api-design.md](python-api-design.md) and
[python-development.md](python-development.md). The pre-Python numerical
qualification remains in [v0.1-release-readiness.md](v0.1-release-readiness.md).

## Verdict

**The v0.1 Python binding is functionally verified against the frozen
qualified goldens and the C++ engine, including under AddressSanitizer and
UndefinedBehaviorSanitizer.** No production C++ behaviour, golden, acceptance
limit or integrator setting was changed. The only native source changes are two
opt-in harness switches in `tests/reference/harness.cpp`: a per-member JSON
**input** configuration switch (`--configurations`) and a `--native-source`
route that acquires through the same production `EarthEnvironment` the binding
composes. Both exist only to build the binding-overhead comparison cases. The
numerical path and the produced trajectories on the default route are unchanged
and `--native-source` is bit-identical to it; the one default-route schema
change is additive measurement metadata — the harness JSON now always emits a
`native_source` boolean, including `false` on the default route. Frozen
goldens, acceptance thresholds and the native CSV schemas are unchanged.

Functional evidence after the final code change, with the new harness in place:
frozen native suite **17/17 PASS** with bitwise-identical native batch output at
worker counts 1/2/4/8, and the Python suite **396 passed** (2.38 s Release).

## Qualification host

| Item | Value |
|---|---|
| OS / kernel | Linux 6.18.54.1 WSL2, glibc 2.39, x86-64 |
| CPU | 16 logical CPUs |
| Compiler | GCC 13.3.0 |
| CMake / generator | 4.4.4 / Ninja |
| Python | CPython 3.12.3 |
| NumPy / pybind11 | 2.5.4 / 3.1.0 |
| Interpreter venv | `.venv-python` (repository-local, not committed) |

All Release work uses `-DCMAKE_BUILD_TYPE=Release`. The benchmark harness records
both the extension and the reference harness build type as `Release` and sets
`release_optimised: true` as run metadata; that flag is metadata, not a
separately verified assertion.

## Build

The standard, portable instructions remain in
[python-development.md](python-development.md) and apply to a normal system
`python3-dev` install. The qualification host has no system Python development
headers, so the host-specific commands were:

```sh
cmake -S . -B build-python -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLEO_BUILD_PYTHON_BINDINGS=ON -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/home/user/leo-cache-study-build-deps/headers/usr \
  -DPython3_EXECUTABLE=/home/user/leo-dynamics/.venv-python/bin/python \
  -DPython3_INCLUDE_DIR=/home/user/vellvm_feasibility_test/envs/inc/python3.12 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
env CPLUS_INCLUDE_PATH=/home/user/vellvm_feasibility_test/envs/inc \
  cmake --build build-python -j3
```

The nonstandard include tree needs **both** `Python3_INCLUDE_DIR` (the
`Python.h` directory) and the parent directory on `CPLUS_INCLUDE_PATH`, because
the architecture-specific `pyconfig.h` lives one level up. No host path is
hardcoded in the repository; `CMakeLists.txt` performs normal
Python3/pybind11 discovery.

Reference harness used for direct C++ comparison and for the frozen-golden
tests:

```sh
cmake -S tests/reference -B build-python-reference -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE=/home/user/leo-dynamics/.venv-python/bin/python
cmake --build build-python-reference -j3
```

Sanitizer build adds
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g1"`
plus `-fsanitize=address,undefined` in the executable/module linker flags, with
the same Python and pybind11 discovery inputs.

Bindings-off check: a fresh native-only configure with Python3 and pybind11
discovery disabled, `-DLEO_BUILD_PYTHON_BINDINGS=OFF` and examples enabled
configured and built successfully, confirming the bindings remain strictly
optional for native consumers. The resulting
`build-python-native/propagate_leo --model j2` prints `sample_count 1441`,
`final_radius_m 6967337.59678392`, `raan_change_rad -0.0634164558162605`.

## Test results

| Suite | Command scope | Result |
|---|---|---|
| Native CTest | Release, full suite | 119/119 passed |
| Native ASan + UBSan | Full suite | 119/119 passed, 32.29 s |
| Python pytest | Release extension, rebuilt harness | **396 passed**, 2.38 s |
| Python pytest | ASan + UBSan extension | **396 passed**, 52.46 s |

Python command (Release extension):

```sh
env PYTHONPATH=build-python/python \
  .venv-python/bin/python -m pytest tests/python -q \
  --harness build-python-reference/leo_reference_dump --tb=short
```

Sanitizer Python command:

```sh
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libasan.so.8:/usr/lib/x86_64-linux-gnu/libstdc++.so.6 \
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
PYTHONPATH=build-python-sanitize/python \
  .venv-python/bin/python -m pytest tests/python -q \
  --harness build-python-reference/leo_reference_dump --tb=short
```

Two notes on the sanitizer run:

- LeakSanitizer is disabled (`detect_leaks=0`) for the CPython-instrumented
  Python run; **no leak certification of any kind is claimed from these runs**,
  native included. Worker-thread lifetime is covered instead by tests that repeat
  batch creation and forced-failure teardown under `tracemalloc`, and by a
  Linux `/proc/self/task` check asserting that no worker threads survive.
- Preloading `libasan` alone fails with `__cxa_throw` interception errors
  against the host C++ runtime. Preloading `libstdc++` as well resolves it.
  This is host toolchain interoperation, not a defect in the bindings.

## Numerical parity

Frozen full-suite native regression, unchanged harness route:

```sh
.venv-python/bin/python tests/reference/test_reference_regression.py \
  --binary build-python-reference/leo_reference_dump \
  --suite full --out build-python-reference/reference-full.json
```

=> **17/17 PASS**, with bitwise-identical output for native batch worker
counts 1, 2, 4 and 8. This was re-confirmed on the default route after the
matched-route harness switches landed. The Python suite additionally replays
all 17 full frozen timelines against the same goldens.

Parity assertions used by the Python suite:

- **Exact float equality** for trajectory and pointwise comparisons against the
  C++ harness. There is no 16-ULP-style tolerance fallback anywhere in the
  parity checks.
- **Bitwise equality** for single-versus-worker and single-versus-batch results.
- For pointwise parity, `leo.State` applies the native normalization, so the
  direct-native point inputs and the Python `State.to_array()` values are the
  same normalized values. The frozen fixture files and acceptance limits used
  by the oracle comparison are untouched; no claim is made that a raw,
  pre-normalized quaternion is bit-identical across the two paths.
- One residual limit is structural: the JSON harness serialises `-0.0` as the
  integer `0`, so **signed-zero bit parity cannot be asserted across the JSON
  boundary**. Within Python, worker/single/batch comparisons are bitwise and do
  cover signed zero.

Goldens, acceptance limits and production C++ are unchanged by this work.

## Functional smoke coverage

An end-to-end smoke run against the fresh Release extension imported from
`build-python/python` exercised:

- 6-panel spacecraft configuration;
- J2 + SRP + Sentman drag + albedo + gravity-gradient model;
- propagation over 21 samples;
- analysis of the produced arrays;
- export of all 9 native CSVs;
- a heterogeneous 2-spacecraft batch (differing masses, inertias, dynamics
  models and environments, one shared `Earth` as the contract requires) run with
  8 workers producing results exactly identical to the corresponding single
  runs;
- a `Trajectory` view outliving deletion of its parent and of its configuration
  objects.

## Binding overhead and multithreading

Source of record: `benchmarks/python/binding_overhead.py` with its recorded
results in `benchmarks/python/results/release-coupled.json` and
`release-j2.json`. Both files record
`environment.native_acquisition_route: "matched-native-source"`. All values
below are medians of 7 repeats after 2 warmups, in seconds, reproduced exactly
as recorded; every derived difference and scaling factor is computed from those
exact medians.

### Matched acquisition route

The native baseline runs with the harness's opt-in `--native-source` switch,
which composes the same production `EarthEnvironment` the binding builds
(owned constant `EphemerisProvider`, same exponential atmosphere, eclipse and
rotation) and delegates snapshots to the existing native source. The source is
constructed before timing starts. This matters because the default qualifier
route acquires environment snapshots manually and skips density acquisition
when drag is disabled, while the binding always performs native
`EarthEnvironment` acquisitions for the environment it is given; without
`--native-source` that baseline difference would be charged to the binding.
Trajectories are bit-identical between the default and `--native-source`
routes. All 17 frozen full schedules were compared between the default route
and `--native-source` and are bitwise equal across all 13 state words, with 0
differing words over 34 native invocations (JSON parsed with
`parse_int=float` so signed zeros are preserved). The default qualification
route is numerically unchanged and differs only by the additive
`native_source` metadata field described in the verdict.

Single-trajectory medians:

| Scenario | Native | Python | Python − native | Relative |
|---|---:|---:|---:|---:|
| coupled CASE-A, 121 samples | 0.011850219 | 0.011755006 | -0.000095213 | -0.80 % |
| j2_inclined, 21 samples | 0.000130470 | 0.000138124 | 0.000007654 | 5.87 % |

Binding-side per-call costs (median): coupled CASE-A configuration construction
0.000046500 s per copy and 0.000000778 s per result-copy proxy; j2_inclined
0.000031592 s per copy and 0.000000471 s per proxy. Single-trajectory checksums
match exactly between native and Python in both scenarios
(`checksum_matches_python: true`, `checksum_delta: 0.0`).

### Homogeneous 32-member batches

| Workers | Coupled native | Coupled Python | J2 native | J2 Python |
|---|---:|---:|---:|---:|
| 1 | 0.378475298 | 0.376328664 | 0.004325504 | 0.004360560 |
| 2 | 0.224745719 | 0.218492001 | 0.002462730 | 0.002421909 |
| 4 | 0.127346714 | 0.123691249 | 0.001500949 | 0.001529560 |
| 8 | 0.071733279 | 0.069541787 | 0.001487401 | 0.001264349 |

Scaling from 1 to 8 workers: coupled CASE-A 5.2761× native, 5.4115× Python;
j2_inclined 2.9081× native, 3.4489× Python. Homogeneous checksums are identical
across all worker counts and both paths in each scenario, and the RHS
evaluation count is worker-count invariant (501129 coupled, 23264 j2).

### Heterogeneous 32-member batches

Per-member configurations via the harness `--configurations` switch, with
`--native-source`. `native_exact_parity: true` at every worker count in both
scenarios.

Coupled CASE-A:

| Workers | Native | Python | Python − native |
|---|---:|---:|---:|
| 1 | 0.237676563 | 0.234607357 | -0.003069206 |
| 2 | 0.140705098 | 0.139618733 | -0.001086365 |
| 4 | 0.080493528 | 0.079297893 | -0.001195635 |
| 8 | 0.048580328 | 0.046851321 | -0.001729007 |

j2_inclined:

| Workers | Native | Python | Python − native |
|---|---:|---:|---:|
| 1 | 0.012198133 | 0.011825802 | -0.000372331 |
| 2 | 0.006527438 | 0.006770103 | 0.000242665 |
| 4 | 0.004814955 | 0.004824215 | 0.000009260 |
| 8 | 0.003940030 | 0.003854819 | -0.000085211 |

Scaling from 1 to 8 workers: coupled 4.8924× native, 5.0075× Python;
j2_inclined 3.0959× native, 3.0678× Python.

### Retained memory and GIL

Retained-result measurements (point mass, 600 s, 1001 samples, 104 B per
state):

| Workload | Members | State array | Times | RSS delta |
|---|---:|---:|---:|---:|
| coupled | 1024 | 106602496 B (101.66 MiB) | 8008 B | 106479616 B |
| j2 | 64 | 6662656 B (6.35 MiB) | 8008 B | 6590464 B |

These are retained-set deltas, not peaks. GIL release is confirmed for every
Python batch call (`gil_released: true` with background-thread ticks recorded at
each worker count).

### Interpretation and limits

- **Python-minus-native is not pure packing cost, and not an upper bound.** The
  extension exposes no internal timer, so native propagation time inside a
  binding call is not separately measurable and result packing happens inside
  the same call. The difference also covers boundary validation and
  configuration materialisation on the Python side, and the harness increments
  an RHS call counter on every native evaluation, which biases the native
  baseline upward.
- **Small and negative differences are not negative overhead.** The native RHS
  counter and separate-run variability limit the resolution of these numbers.
  Several cells now land within a few hundred microseconds of zero or below it.
  They are recorded as measured and are neither clamped nor read as the binding
  being faster than native.
- An earlier short-lived j2_inclined two-worker outlier (~1.66 Python/native
  ratio) did not reproduce. The default-route run measured 0.006770103 Python
  against 0.006527438 native (≈1.037), and a follow-up probe alternating
  execution order over 31 blocks retained 899 native and 961 Python timings
  (unequal sample counts after trimming) with medians 0.006929889 native and
  0.006812868 Python, ratio 0.9831, difference -0.000117021 s. The baseline
  mismatch fix plus these reruns are the recorded
  observations; the outlier's origin is not isolated and no noise-only or
  single-cause explanation is claimed.
- No numerical speedup target is imposed; the solver, tolerances and step
  control are unchanged.
- Throughput and scaling are dominated by the native multithreading already
  qualified in [v0.1-release-readiness.md](v0.1-release-readiness.md); this
  campaign measures what the binding layer adds on top of a matched native
  route.

## Known limitations

- No distribution packaging, wheels or PyPI — out of scope by exclusion.
- No CSPICE access from Python; `SunEphemeris.constant` and
  `SunEphemeris.tabulated` only. NRLMSISE-00 and JB2008 remain native-only,
  deferred to v0.2. No Python callbacks in the integration loop.
- The scientific six-hour coupled CASE-C scenario remains unresolved; the
  historical qualification covers only the 1 h variant. The Python API inherits
  that limit.
- `SunEphemeris.constant` is for controlled experiments; it is not a real
  ephemeris.
- `Trajectory.to_csv(file)` uses the native nine-file CSV writer into a
  uniquely claimed temporary directory and then copies `trajectory.csv` to the
  destination. The extra I/O exists to preserve the exact native CSV schema.
  Full `analyze` retains native trajectory reconstruction for CSV export, while
  the trajectory arrays themselves are packed only once.
- There is no internal binding timer, so Python-minus-native overhead is an
  estimate, not an upper bound; see the overhead campaign above.