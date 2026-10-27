# Proprietary notice — leo-dynamics

**Copyright (c) 2026 Shiki Vaahan. All rights reserved.**

This notice applies to every part of the `leo-dynamics` repository that is
**not** covered by the MIT licence in `LICENSE`. The scope of the MIT grant is
described in `LICENSE_SCOPE.md`; the machine-readable, authoritative
classification of every tracked file is `release-audit/license-boundary.json`,
and `ci/verify_license_boundary.py` enforces it and fails closed.

## What is proprietary

The following material is proprietary, confidential, and **not** licensed for
redistribution, publication or sublicensing in any form:

| Area | Paths (the boundary manifest is the exhaustive list) |
|---|---|
| High-fidelity atmosphere providers and their research | `src/atmosphere/**`, `include/leo/atmosphere/**`, `cmake/nrlmsise00.cmake`, `cmake/nrlmsise00-tests.cmake`, `cmake/jb2008.cmake` |
| Third-party-provider adapters outside the MIT core (CSPICE) | `src/environment/**`, `include/leo/environment/spice.hpp`, `examples/analyze_orbit.cpp`, `tests/spice_tests.cpp` |
| Research campaigns, metrics and their notes | `docs/atmosphere/**`, `docs/multithreading/**`, `docs/numerical-audit/**` |
| Optimisation experiments and datasets | `benchmarks/**`, `scripts/**` |
| Test suites and frozen reference fixtures (research datasets) | `tests/**` |
| Qualification, validation and release reports | `docs/v0.1-qualification.md`, `docs/v0.1-release-readiness.md`, `docs/python-binding-validation.md`, `docs/python-packaging.md`, `docs/release-readiness-report.md`, `docs/repository-hygiene-report.md` |
| Packaging, CI and release tooling | `ci/**`, `.github/**`, `release-audit/**`, `audit-evidence/**` |
| Audit and release-boundary records | `RELEASE_LICENSE_AUDIT.md`, `audit-progress.md`, `PROPRIETARY-NOTICE.md` |
| The repository's internal overview document | `README.md` |

## Rules

- No file in the proprietary set may be copied into a wheel, a source
  distribution, a container image, a public mirror, or any other artefact that
  leaves the private repository.
- The MIT grant in `LICENSE` does not extend to the proprietary set. It must
  not be read as covering it, and the absence of a licence is not a grant.
- Third-party components referenced by the proprietary set — for example the
  external NRLMSISE-00 C reference implementation, the official JB2008 Fortran
  source, CSPICE and the Tudat reference oracle — are **not** present in this
  repository, are **not** redistributed by it, and remain under their own
  terms.

## Enforcement

`ci/verify_license_boundary.py` checks the source tree and any built
distribution against the boundary manifest. It fails closed: an unclassified
file, an unexpected distribution member, or a proprietary marker inside a
member is a hard failure, and the release workflow runs it before anything can
be published.
