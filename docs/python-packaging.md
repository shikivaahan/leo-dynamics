# Python packaging status and release readiness

What `leo-dynamics` 0.1.0 ships, what has actually been executed against it, and
what still blocks publication. Ordinary installation instructions are in the
[README installation section](../README.md#installation); the interface itself is
documented in [python-development.md](python-development.md) and
[python-api-design.md](python-api-design.md). Numerical evidence about the
engine is in [python-binding-validation.md](python-binding-validation.md) and
[v0.1-release-readiness.md](v0.1-release-readiness.md); nothing here re-qualifies
it.

## Evidence states

Every claim in this document is in exactly one of these three states. They are
never mixed:

1. **Verified locally** — executed on the development host, command and result
   recorded below.
2. **Executed in CI** — run by GitHub Actions on a pushed commit.
3. **Configured only** — the workflow or command exists and is lint-clean, but it
   has never been executed. Configuration is not platform validation.

## Distribution layout

| Item | Value |
|---|---|
| Distribution name | `leo-dynamics` |
| Import name | `leo_dynamics` |
| Version | `0.1.0` (Python package, CMake project and metadata agree) |
| Build backend | `scikit-build-core==0.11.6` (`build-backend = "scikit_build_core.build"`) |
| Build requirements | `scikit-build-core==0.11.6`, `pybind11==3.1.0`, `cmake>=3.28`, `ninja>=1.11` |
| C++ dependency provisioning | `cmake/provision.cmake` fetches pinned, SHA-256 verified Eigen 3.4.0 and Boost 1.83.0 archives during configure (`LEO_PROVISION_DEPENDENCIES=ON`) |
| Build type | Release, symbols stripped (`install.strip = true`) |
| Requires-Python | `>=3.10,<3.14` |
| Runtime dependencies | `numpy>=2.0` only |
| Native content | one compiled `_core` extension linking the static `leo::integration` and `leo::analysis` targets |
| Python content | `__init__.py`, `__init__.pyi`, `py.typed` |

`Build testing` is off for the distribution build, so no test binary, example or
qualification harness is compiled into a wheel. CSPICE, NRLMSISE-00 and JB2008
are not enabled for the distribution build: no kernels, atmospheric reference
sources or space-weather data are downloaded or embedded.

## Wheel and source distribution build

Build either artefact from a clean checkout. The only host requirements are a
full Python >= 3.10 (development headers included) and a C++20 compiler; CMake,
Ninja, scikit-build-core, pybind11, Eigen and Boost all come from the pinned
build requirements:

```sh
python3 -m venv .venv-build
.venv-build/bin/python -m pip install --upgrade pip build
.venv-build/bin/python -m build            # writes dist/leo_dynamics-0.1.0-*
```

## Native dependencies and bundling

Eigen and Boost are header-only *dependencies*, but their code is not absent
from the wheel: their templates are instantiated and compiled into `_core`, so
MPL-2.0 and BSL-1.0 obligations attach to the shipped binary. What makes the
wheel small is that no shared library has to be bundled: no Eigen or Boost
source is shipped, and the extension links only the C++ runtime.

**Measured on the extension as installed from a wheel**, not on a development
build:

```console
$ ldd .../site-packages/leo_dynamics/_core.cpython-313-x86_64-linux-gnu.so
	libstdc++.so.6 => /lib/x86_64-linux-gnu/libstdc++.so.6
	libm.so.6 => /lib/x86_64-linux-gnu/libm.so.6
	libgcc_s.so.1 => /lib/x86_64-linux-gnu/libgcc_s.so.1
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6
	/lib64/ld-linux-x86-64.so.2
$ readelf -d .../_core.cpython-313-x86_64-linux-gnu.so | grep -E 'NEEDED|RPATH|RUNPATH'
(NEEDED entries only for libstdc++, libm, libgcc_s and libc plus the loader;
 no RPATH, no RUNPATH)
```

The wheel's own extension is 828 512 bytes on CPython 3.13. No third-party
native library is required at runtime, so nothing has to be shipped alongside
it. The wheel CI keeps cibuildwheel's platform repair defaults, which are
`auditwheel repair -w {dest_dir} {wheel}` on Linux,
`delocate-wheel --require-archs {delocate_archs} -w {dest_dir} -v {wheel}` on
macOS and `delvewheel repair -w {dest_dir} -v {wheel}` on Windows (cibuildwheel
4.3.1 `resources/defaults.toml`; it installs `delvewheel` itself into its
Windows build environment). Only the Linux path has been exercised so far; see
the verification log for what `auditwheel` actually reported.

## Installed-wheel verification

`ci/run_installed_wheel_tests.py` is the single entry point, used by CI and
available to anyone who installs a wheel locally:

```sh
python3 -m venv /tmp/leo-wheel-test
/tmp/leo-wheel-test/bin/python -m pip install dist/leo_dynamics-0.1.0-*.whl pytest
cd /tmp
REPO=/path/to/leo-dynamics
/tmp/leo-wheel-test/bin/python "$REPO/ci/run_installed_wheel_tests.py" "$REPO" \
  --harness "$REPO/build-reference/leo_reference_dump"
```

The driver must not be invoked from inside the repository: it treats a
source-tree working directory as a failure, because that is exactly the
condition under which the suite would silently test a development build.

It fails, rather than warns, unless all of the following hold before any test
runs:

- `leo_dynamics` imports from `site-packages`, not from `<repo>/python` or a
  `build-*/python` staging directory;
- no `sys.path` entry and no working directory is inside the source tree;
  `PYTHONPATH`/`PYTHONHOME` are removed from the environment and inherited by
  pytest;
- installed metadata version equals `leo_dynamics.__version__`;
- the installed payload contains `leo_dynamics/__init__.py`,
  `leo_dynamics/__init__.pyi`, `leo_dynamics/py.typed` and the compiled
  `_core` extension, and carries no build debris;
- `pip check` reports a consistent environment (pip is required, and CI installs
  it alongside pytest because uv-created environments omit it);
- a short smoke run constructs the model and propagates one J2 arc, so a broken
  extension fails before pytest noise. Behaviour is covered by the suite, not
  duplicated here.

The origin assertions matter: `tests/python/scenario_fixtures.py` falls back to
development build directories when `leo_dynamics` is not importable, so a suite
run in the source tree can pass without proving anything about the wheel.

## Cross-platform CI

`.github/workflows/wheels.yml`, added in this phase, builds and tests:

| Job | Runner | Wheel target |
|---|---|---|
| `wheels` | `ubuntu-24.04` | `cp310`–`cp313` manylinux_2_28 x86-64 |
| `wheels` | `windows-2025` | `cp310`–`cp313` win_amd64 |
| `wheels` | `macos-15` | `cp310`–`cp313` macosx arm64 |
| `wheels` | `macos-15-intel` | `cp310`–`cp313` macosx x86-64 |
| `installed-wheel-parity` | `ubuntu-24.04` | installs the uploaded Linux wheel and compares it with the direct C++ engine |
| `sdist` | `ubuntu-24.04` | builds, inspects and rebuilds the source distribution |

Design choices that keep it economical and boring:

- **No stable ABI.** One wheel per CPython version. The pybind11/NumPy surface is
  not stable-ABI qualified, so `abi3` would assert a compatibility the bindings
  have never been validated against.
- **Sixteen wheels.** Four platform/architecture targets times four CPython
  versions. `CIBW_BUILD` pins `cp310`–`cp313` and `CIBW_SKIP` drops PyPy,
  32-bit, musllinux and free-threaded builds; the Linux job pins
  `CIBW_MANYLINUX_X86_64_IMAGE=manylinux_2_28` so the policy cannot move when
  cibuildwheel changes its default image.
- **The suite runs against every wheel**, not just one per platform, because it
  costs seconds. The expensive direct-native parity campaign is a separate job
  instead of a step in every wheel build, and the six-hour qualification
  campaign remains manual in `qualification.yml`.
- **Everything cibuildwheel needs is declared.** The C++ dependencies arrive as
  hash-verified archive downloads, so no system package manager step exists in
  any job.
- **No publishing step exists.** There is no PyPI upload, no trusted-publisher
  configuration and no release job in any workflow.

**manylinux policy.** Wheels built on a glibc 2.39 host with GCC 13.3 come out
as `manylinux_2_39_x86_64` (`auditwheel show`), because `libstdc++` needs
`GLIBCXX_3.4.32` and `libm` needs `GLIBC_2.38`. That is why the Linux job
pins `CIBW_MANYLINUX_X86_64_IMAGE=manylinux_2_28`: the container image, not
the developer host, is what produces the distributable tag. No
`manylinux_2_28` wheel has been produced or tested yet.

Failure diagnostics: compiler and pip command lines are echoed
(`CIBW_BUILD_VERBOSITY=1`), the wheel listing is printed on every outcome, and
the wheelhouse — including a partial one after a failure — is uploaded as a job
artifact.

The workflow file parses as YAML and passes `actionlint` 1.7.7 with no findings,
except one false positive: that actionlint release predates the `macos-15-intel`
runner label, which the current `actions/runner-images` table documents as the
Intel macOS 15 label.

## Dependency licences

Verified from upstream licence texts and PyPI project metadata:

| Dependency | Role | Licence | Source of the claim |
|---|---|---|---|
| NumPy | runtime dependency | BSD-3-Clause AND 0BSD AND MIT AND Zlib AND CC0-1.0 | PyPI metadata (`license_expression`) |
| pybind11 3.1.0 | build requirement, headers compiled into `_core` | BSD-3-Clause | PyPI metadata; see [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) |
| scikit-build-core 0.11.6 | build backend | Apache-2.0 | PyPI metadata |
| CMake (PyPI wheel) | build requirement | Apache-2.0 / BSD | PyPI classifiers |
| Ninja (PyPI wheel) | build requirement | Apache-2.0 | PyPI metadata |
| Eigen 3.4.0 | header-only dependency, compiled into `_core` | MPL-2.0, plus three BSD-licensed fragments inside the compiled headers | upstream `COPYING.MPL2`; see [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) |
| Boost 1.83.0 | header-only dependency, compiled into `_core` | Boost Software License 1.0 | upstream `LICENSE_1_0.txt`; see [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) |
| cibuildwheel 4.3.1 | CI only, not shipped | BSD-2-Clause | PyPI metadata |
| auditwheel / delvewheel / delocate | CI repair tools, not shipped | MIT / MIT / BSD | PyPI metadata and classifiers |
| Catch2 v3.7.1 | tests only, never in a distribution | Boost Software License 1.0 | upstream `LICENSE.txt` |

Eigen is the only copyleft dependency in the chain. Its templates are
compiled into `_core`, which makes MPL-2.0 §3.1/§3.2 relevant to the binary:
the full MPL-2.0, BSL-1.0 and BSD-3-Clause texts, the pinned upstream
sources with SHA-256 digests and the exact Eigen components compiled are
recorded in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). No Eigen or
Boost source is vendored into the repository or into either distribution.

## Verification log

Recorded 2026-10-18 on the development host (Linux 6.18.54.1 WSL2, glibc 2.39,
x86-64, GCC 13.3.0, CMake 4.4.4, Ninja 1.13.2, uv-managed CPython
3.10.21/3.11.16/3.12.14/3.13.15, NumPy 2.5.4, pybind11 3.1.0, auditwheel 6.8.2,
patchelf 0.19.1.0, cibuildwheel 4.3.1).

### Final artefacts

| Artefact | Bytes | SHA-256 |
|---|---|---|
| `dist-310/leo_dynamics-0.1.0-cp310-cp310-linux_x86_64.whl` (CPython 3.10.21) | 374 356 | `7348da0e78f9fbaec993de64b405ba7e31fa6934aa22d1475ea5116b8a58287b` |
| `dist-311/leo_dynamics-0.1.0-cp311-cp311-linux_x86_64.whl` (CPython 3.11.16) | 374 837 | `402da2e014159cad0047ea9e5a187a0ac504237bdfb121dabe7182c5b397c24e` |
| `dist-312/leo_dynamics-0.1.0-cp312-cp312-linux_x86_64.whl` (CPython 3.12.14) | 377 449 | `4195546f0f47bd4da2d77e5eed1666a981eb93b62478efe6599cba7dc01b6f12` |
| `dist-313/leo_dynamics-0.1.0-cp313-cp313-linux_x86_64.whl` (CPython 3.13.15) | 377 555 | `4be3cd3984e324f17913b37ff22ec9cc3a25ab7bc6536f89dc73b773211945da` |
| `dist/leo_dynamics-0.1.0.tar.gz` (source distribution, 174 entries) | 1 724 697 | `c68ba0424da2a860fca1551a165d526a908c796dd3aa768831126e563731e002` |
| `dist-repaired/leo_dynamics-0.1.0-cp312-cp312-manylinux_2_39_x86_64.whl` (auditwheel repair of the cp312 wheel) | 377 849 | `36b0f5e6afa713d597dce456514b9a2a6ad63b805e347e7a994a637b5bee3a5d` |

### Distribution contents

- Each wheel holds `leo_dynamics/{__init__.py, __init__.pyi, py.typed,
  _core.cpython-<XY>-x86_64-linux-gnu.so}` plus
  `leo_dynamics-0.1.0.dist-info/{METADATA,RECORD,WHEEL,licenses/THIRD_PARTY_NOTICES.md}`.
  The notices file is present in **all four** wheels, verified by listing the
  wheel members. No benchmark output, evidence archive, `outputs/`, virtual
  environment, test dataset, SPICE kernel or atmospheric source is shipped.
- The source distribution holds `CMakeLists.txt`, `pyproject.toml`, `cmake/`,
  `include/`, `src/`, `bindings/`, `python/leo_dynamics/`, `ci/`,
  `THIRD_PARTY_NOTICES.md`, `tests/python/`, `tests/reference/{acceptance.json,
  golden, scenarios}`, the guide files and `README.md`. It contains no
  `build-*`, `outputs/`, `.venv-python`, `dist*/`, `*.whl`, `*.so`, `*.bsp` or
  `*.tls`, and deliberately excludes `docs/python-packaging.md`, because this
  document quotes the sizes and hashes of the very sdist being built.

### Verification performed locally

- **Installed-wheel matrix: 396 passed on every interpreter built** — CPython
  3.10.21, 3.11.16, 3.12.14 and 3.13.15, each against its own wheel, each in a
  fresh `/tmp` virtual environment, with `pip check` reporting
  `No broken requirements found`, the working directory outside the repository,
  and `PYTHONPATH`/`LEO_PYTHON_PATH` unset. Parity runs used the native
  `leo_reference_dump` harness built from `tests/reference`
  (`cmake -S tests/reference -B /tmp/leo-native-ref -DCMAKE_BUILD_TYPE=Release`),
  not a stub.
- **Source-distribution rebuild:** the sdist was installed with
  `uv pip install --python /tmp/leo-sdist-venv/bin/python --reinstall --no-cache
  dist/leo_dynamics-0.1.0.tar.gz`, which reports
  `Building leo-dynamics @ file:///.../leo_dynamics-0.1.0.tar.gz` — it
  re-provisions Eigen and Boost from the pinned archives and recompiles the
  extension from the shipped sources, with no source tree present. That
  environment imports `leo_dynamics`, passes `pip check` and passes the same
  driver: **396 passed**, CPython 3.12.14, run from `/tmp`.
- **Native regression suite:** 119/119 native CTest tests pass, the frozen
  reference CTest passes, and a standalone Python-free configure and build
  (`-DLEO_PROVISION_DEPENDENCIES=ON -DBUILD_TESTING=OFF`) succeeds, producing
  `libleo_kernel.a`, `libleo_integration.a` and `libleo_analysis.a`.
- **Origin guards are real, not decorative:** the driver fails when
  `leo_dynamics` is not installed and fails when the working directory is inside
  the repository.
- **README examples:** both code blocks execute against the installed wheel from
  `/tmp` and export the nine CSV datasets.
- **Pinned dependency integrity:** every build fetched Eigen 3.4.0 and Boost
  1.83.0 from the URLs in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)
  with matching SHA-256 digests; `FetchContent URL_HASH` enforces this at
  configure time, so a successful configure is the verification.
- **Extension runtime dependencies:** measured on the extension installed from
  the CPython 3.13 wheel (828 512 bytes), `ldd` and `readelf -d` show only
  `libstdc++`, `libm`, `libgcc_s`, `libc` and the loader, with no `RPATH` and no
  `RUNPATH`. No third-party native library is needed at runtime.
- **`auditwheel show` on all four wheels:** each reports *consistent with the
  following platform tag: manylinux_2_39_x86_64*. Reaching `manylinux_2_28`
  would require rebuilding on a system with earlier libraries; the binding
  requirements are `libstdc++` `GLIBCXX_3.4.32`/`CXXABI_1.3.13` and `libm`
  `GLIBC_2.38`, produced by this host's GCC 13.3 on glibc 2.39. **The host build
  floor is therefore `manylinux_2_39`; the distributable `manylinux_2_28` tag
  comes only from the CI container image.**
- **`auditwheel repair` and reinstall:** `auditwheel repair --plat
  manylinux_2_39_x86_64` relabelled `linux_x86_64` to `manylinux_2_39_x86_64`;
  the repaired wheel was installed into a fresh environment and the driver
  reported **396 passed**. Nothing needed bundling.
- **Wheel selection matrix:** with the workflow's exact environment,
  `--print-build-identifiers` yields 4 identifiers for each of
  `manylinux_x86_64`, `macosx_arm64`, `macosx_x86_64` and `win_amd64` — 16
  wheels in total; without `CIBW_SKIP` the Linux selector returns 8, so the
  skip is load-bearing.
- **Workflow lint:** `actionlint` 1.7.7 clean apart from its stale
  `macos-15-intel` label table.
- **PyPI name check:** `leo-dynamics` returns 404 on both the simple index and
  the JSON API.

Every installed-wheel run above used a fresh virtual environment, a working
directory outside the repository and `PYTHONPATH`/`PYTHONHOME` removed, so the
suite could not fall back to the development build.

**Executed in CI**

Run `39128373040`, commit `799de81`, `push`, concluded `success` — all six jobs:

| Job | Result |
| --- | --- |
| `linux-x86_64 manylinux_2_28` | success (also passed in `39123791284`) |
| `macos-arm64` | success (**failed** in `39123791284`) |
| `macos-x86_64` | success (**failed** in `39123791284`) |
| `windows-amd64` | success (**failed** in `39123791284`) |
| `Source distribution rebuild` | success |
| `Installed-wheel parity against the C++ engine` | success (**skipped** in `39123791284`, blocked by the failed jobs) |

The parity job installed
`leo_dynamics-0.1.0-cp312-cp312-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl`
into a clean venv on CPython 3.12, reported `No broken requirements found.`, ran
the compiled-extension smoke, and finished `396 passed in 3.06s` with 0 skipped
against a rebuilt reference harness. macOS wheels were produced for CPython
3.10-3.13 on both architectures, tagged `macosx_11_0_x86_64` and
`macosx_11_0_arm64`.

Everything above is anchored to `799de81`. Later commits touching only this
document carry no CI run of their own and inherit that evidence.

**Configured only**

Nothing in `wheels.yml` remains unexecuted. What is still configured without
evidence is publication: there is no upload job, no token, no tag and no
release workflow, so no wheel has been pushed to any index.

## Project licence and redistribution status

**No licence file exists.** At baseline commit `83aa05d`, `git ls-files`
reports no `LICENSE`, `COPYING` or `NOTICE` (the only tracked top-level files
were `.gitignore`, `CMakeLists.txt` and `README.md`), and the CMake cache records
an empty `CMAKE_PROJECT_SPDX_LICENSE`. Nothing in this packaging work added one,
and `THIRD_PARTY_NOTICES.md` is *not* a licence for this project: it records the
terms of the third-party code compiled into `_core` and explicitly states that it
authorises nothing. Therefore:

- no licence expression or licence classifier is asserted in `pyproject.toml`,
  because doing so would assert rights the project has not granted;
- no redistribution right is established for the project itself, which blocks
  publication regardless of build correctness;
- the same gap applies to the source distribution, not only to wheels.

This is a tracked release-readiness blocker, not an oversight. Establishing
terms is a decision for the copyright holder; until then, the wheel and source
distribution are buildable and installable but not distributable.

## PyPI name availability

Checked on 2026-10-18 against the live PyPI:

| Name | Simple index | JSON API |
|---|---|---|
| `leo-dynamics` | 404 Not Found | 404 Not Found |
| `leo_dynamics` | 404 Not Found | — |
| `numpy` (control) | 200 OK | 200 OK |

`leo-dynamics` is currently unregistered, so the name looks available. Nothing
was reserved, claimed or registered, and availability is not a reservation: a
name can be taken by anyone at any time.

## What eventual publication would require

None of the following has been done, and none is configured anywhere in this
repository:

1. **A licence decision** by the copyright holder, a `LICENSE` file, and
   matching `license` metadata. This is the blocking item.
2. **Confirmation that every shipped file may be redistributed** — the C++ and
   pybind11 sources under this repository's control, plus the transitive
   dependency review in the table above.
3. **A PyPI account or organisation**, then either an API token (classic upload)
   or a GitHub Actions trusted publisher for `shikivaahan/leo-dynamics`.
4. **A publication workflow** with an explicit release trigger, a version bump,
   and artefact checksums. Nothing uploads today.
5. **Executed CI evidence** for every platform in the matrix above — obtained in
   run `39128373040` at commit `799de81`, where all six jobs succeeded.
6. **A v0.1.0 tag**, cut only after the above.

## Wheel repair: run 39123791284

The first executed run of this workflow (`39123791284`, commit `016af3a`) failed
on `windows-amd64`, `macos-arm64` and `macos-x86_64`; only `linux-x86_64` and the
source distribution job passed. The failures were diagnosed from the actual job
logs, not inferred.

**Windows.** `core.autocrlf=true` rewrote the byte-pinned frozen reference
fixtures from LF to CRLF, so every SHA-256 pinned in
`tests/reference/golden/manifest.json` stopped matching the committed bytes and
`tests/python/test_regression.py::test_frozen_fixture_identities` reported 17
failures. Fixed by `.gitattributes`, marking the pinned fixtures `-text` so the
committed bytes survive checkout verbatim on every platform. No fixture, hash or
threshold was changed.

**macOS, both legs.** `include/leo/integration/batch.hpp` used `std::jthread`
and `std::stop_source`. The libc++ shipped with AppleClang 16.4 does not
declare either name, so `bindings/integration.cpp` failed to compile and no wheel
was produced. Replaced with `std::thread` plus an explicitly RAII-owned worker
pool that joins on every exit path, preserving the whole scheduling contract:
relaxed lowest-open-index dispatch, a bounded `min(workers, trajectories)` pool,
the inline single-worker path, input-ordered output, deterministic lowest-failure
reporting, and launch failure still treated as an infrastructure error (started
workers cancelled and joined, original exception rethrown unchanged).

**macOS, Intel leg only.** `std::filesystem` was rejected outright:
`"'path' is unavailable: introduced in macOS 10.15"`. cibuildwheel defaults
`macosx_x86_64` to a `MACOSX_DEPLOYMENT_TARGET` of 10.9, below libc++'s 10.15
availability floor. The arm64 leg defaults to 11.0, which is why only the Intel
leg failed this way. Fixed by declaring one minimum, `MACOSX_DEPLOYMENT_TARGET=11.0`,
for both macOS legs.

No physics, integrator, force model, tolerance or default value changed, and
`propagate()` remains the same code path.

### Local verification actually executed

- **Full native suite, `ctest`: 100% of 120 passed.** This is the 119-test
  baseline plus one added regression covering the batch scheduler
  (exactly one work claim per trajectory index across worker counts 2, 4 and 11).
  Includes `leo_analysis_tests`. The focused scheduler suite is 229 assertions
  across 12 cases.
- **Native batch determinism, frozen CASE-A/B/C, rebuilt scheduler: passed.**
  Bitwise binary64 comparison of complete trajectories from `propagate_batch()`
  at workers 1, 2, 4 and 8 against single-threaded `propagate()`, with no
  tolerance applied: 3 cases x 4 worker counts x 12 trajectories, 10095 doubles
  compared per trajectory.
  *Scope of this check:* the case files are the genuine frozen scenarios,
  loaded through the repository's own `experiment::load`, but the optional
  atmospheric datasets the historic campaign consumes (`--weather`, `--solfsmy`,
  `--dtcfile`) and the recorded CSPICE kernels are absent from this environment,
  so the harness supplies an explicit snapshot from the frozen deterministic
  `AnalyticSun` instead. This is therefore **not** a rerun of the full historic
  optional-atmosphere campaign, and **no scientific requalification is claimed
  from it**. It demonstrates only that batching is bitwise-order-preserving and
  worker-count-independent for these cases. Recreating the absent datasets is
  deliberately out of scope.
- **Launch-failure smoke: passed** at three injection points. A throwaway
  `LD_PRELOAD` shim makes `pthread_create` return `EAGAIN` after N launches,
  driving the public `propagate_batch()`: the original `std::system_error`
  reaches the caller unchanged, is never reported as `BatchPropagationError`, no
  partial batch is returned, and already-started workers are joined before the
  exception leaves.
- The two Python-driven ctest entries (`audit_metric_identities`,
  `process_batch_contracts`) pass locally. Multi-interpreter installed-wheel
  validation against CPython 3.10–3.13 is CI's job and is exercised per wheel by
  the `wheels` matrix.
- **Installed-wheel validation against the repaired tree: 396 passed, 0 skipped.**
  A CPython 3.12.14 wheel was built after the final scheduler correction —
  every artefact postdates the settled `batch.hpp`, confirmed by a compiled-source
  digest over `src`, `include`, `bindings`, `CMakeLists.txt` and `cmake` that was
  unchanged across the rebuild. `ci/run_installed_wheel_tests.py` was run from
  outside the repository against the installed distribution, not the source
  tree: `pip check` reported `No broken requirements found.`, the extension smoke
  propagated a j2 case, and all 396 tests passed with none skipped. Passing the
  reference harness converted the 36 parity skips seen in `39123791284` into real
  assertions, which is why 396/0 is the expected result rather than 360/36. That
  includes all 17 frozen fixture identities, `test_python_reproduces_the_direct_cpp_path`
  across all 17 scenarios, and the 8-member batch-determinism contract at workers
  1, 2, 4 and 8 through the Python surface, so the scheduler change preserved
  bitwise reproducibility. Full command log: `/tmp/leo-wheel-repair-python.md`.
- **Python-side Windows diagnosis.** The Windows leg failed on byte-pinned
  fixture line endings, not on the scheduler; the `.gitattributes` correction and
  its controlled-clone proof are recorded in `/tmp/leo-wheel-failure-windows.md`.

### CI rerun

Run `39128373040` at commit `799de81` (`push`, 2026-10-18, concluded `success`)
executed the repair on real runners. All six jobs passed: the three that failed
in `39123791284` — `macos-arm64`, `macos-x86_64` and `windows-amd64` — are now
green, and the installed-wheel parity job, previously skipped because those jobs
failed, ran and reported `396 passed` with 0 skipped, `No broken requirements
found.`, and a successful compiled-extension smoke. Both macOS jobs built with
`MACOSX_DEPLOYMENT_TARGET=11.0` and emitted wheels tagged `macosx_11_0_x86_64`
and `macosx_11_0_arm64` for CPython 3.10-3.13; the previously failing `std::jthread`
and `std::stop_source` diagnostics and the `std::filesystem` availability error
do not appear. See the CI table in the verification log above.

### What remains outstanding

Publication only. No licence decision, no `LICENSE` file, no index credentials
and no release tag exist, so although every wheel now builds and installs, no
wheel has been uploaded anywhere. The macOS wheels are additionally built but
not yet exercised by an installed-wheel test on a macOS interpreter — the
installed-wheel parity job installs the manylinux wheel.

## Open risks

- Linux wheels built on this host are `manylinux_2_39`, produced and tested
  locally; CI additionally built and installed the pinned
  `manylinux_2_28`-compatible wheel in run `39128373040`. Licensing,
  independently, still blocks public distribution of any of these wheels.
- The build downloads Eigen and Boost during configure. The archives are pinned
  and SHA-256 verified, so a build is reproducible, but it is not offline.
- Windows and macOS compilation of the C++20 engine now has successful execution:
  run `39128373040` built the full CPython 3.10-3.13 wheel matrix on both macOS
  architectures and on Windows. The residual risk is coverage, not buildability —
  the Windows and macOS wheels are not exercised by an installed-wheel test on
  their own interpreters, since that job installs the manylinux wheel.
- `requires-python` excludes 3.14 by design; no 3.14 claim is made.
- The exponential atmosphere is spherical and Sentman is cold-beam; NRLMSISE-00
  and JB2008 are deferred to v0.2 and are absent from the distribution.
- No flight-qualified accuracy claim is made for this software.
