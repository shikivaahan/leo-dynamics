# Licence scope — leo-dynamics

The MIT licence text in `LICENSE` applies **only** to the files enumerated
below. This file defines the licensed subject matter; it does not grant
anything beyond it.

## The licensed subject matter (the MIT core)

| Area | Files |
|---|---|
| Numerical engine | `src/kernel/`, `src/integration/`, `src/analysis/` |
| Public headers | `include/leo/kernel/`, `include/leo/integration/`, `include/leo/analysis/`, and `include/leo/environment/ephemeris.hpp`, `include/leo/environment/interpolated.hpp`, `include/leo/environment/source.hpp` |
| Python interface | `bindings/`, `python/leo_dynamics/` |
| Build and packaging | `CMakeLists.txt`, `pyproject.toml`, `cmake/provision.cmake` |
| Licence and notices | `LICENSE`, `LICENSE_SCOPE.md`, `THIRD_PARTY_NOTICES.md` |
| Public overview | `README.dist.md` |

The exhaustive, machine-readable form of this list is
`release-audit/license-boundary.json` (class `mit_core`), which
`ci/verify_license_boundary.py` enforces.

## What is outside the licensed subject matter

- Every other file in the project repository is outside the licensed subject
  matter and is not granted under the MIT licence. That includes the
  empirical high-fidelity atmosphere providers, the research campaigns and
  their datasets, the optimisation experiments, the frozen research reference
  fixtures, the project documentation set and the packaging, CI and release
  tooling. Files outside the package are MIT licensed only where they are
  explicitly approved and enumerated above.
- Third-party components are outside the licensed subject matter. The engine
  extension is built against Eigen (MPL-2.0), Boost (BSL-1.0) and pybind11
  (BSD-3-Clause), and links the platform C++ runtime; their own terms apply
  to them, unchanged by the MIT grant, and are recorded in
  `THIRD_PARTY_NOTICES.md`.

## Redistribution

Redistribution of the enumerated files is permitted on the MIT terms, subject
to the conditions in `LICENSE` and to the third-party terms recorded in
`THIRD_PARTY_NOTICES.md`.
