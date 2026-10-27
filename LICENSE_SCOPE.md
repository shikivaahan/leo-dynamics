# Licence and distribution scope — leo-dynamics

This file records two different things, which the earlier revision of this
repository conflated:

1. **the licence that applies to the project's own material**, and
2. **the set of files the published distributions actually carry**.

The first is a licence question. The second is a packaging decision. The MIT
grant in `LICENSE` covers the first and is not conditional on the second.

## 1. The licence grant

All original material in this repository — the numerical engine and its public
headers, the Python interface, the research campaigns and their datasets, the
optimisation experiments, the documentation set, the test suites and their
frozen reference fixtures, the examples and plotting helpers, and the
packaging, CI and release tooling — is original work of the author and is
granted under the **MIT License** (`LICENSE`, Copyright (c) 2026 Shiki
Vaahan). Code and constants the author contributed to the Imperial College
Space Society project are covered by the same grant.

No part of this repository is proprietary or reserved. The former
proprietary notice has been removed as obsolete.

## 2. Third-party software used as a reference

The project uses separately installed third-party software as a reference
oracle — the Tudat astrodynamics toolbox, SciPy, and the Orekit and
`SatelliteToolboxAtmosphericModels.jl` projects cited in the research
documentation. Those programs are **not** part of this repository, are **not**
redistributed by it, and remain under their own terms; they are recorded in
`THIRD_PARTY_NOTICES.md`.

The reference trajectories this project generates with such a tool are
numerical outputs produced by this project's own generators and are original
material of this project under the MIT grant of section 1. No upstream code
licence is extended to those outputs, and no upstream licence claim is made
about them.

## 3. External sources that are never vendored

The following are acquired and used in place by an operator. They are **not
present in this repository**, are **not** redistributed by it, and remain under
their own terms:

- the external NRLMSISE-00 C reference implementation (`nrlmsise-00.c`,
  `nrlmsise-00.h`, `nrlmsise-00_data.c` and its documentation);
- the official JB2008 Fortran source (`JB2008.f`) and the SET unified data
  library;
- CSPICE and its SPICE kernels (including the leap-second kernel);
- external empirical atmosphere and space-weather datasets (CSSI/Orekit
  mirrors, SOLFSMY releases) and the official model validation outputs.

The project's own provider wrappers and adapters are original work and are
covered by the MIT grant of section 1; they are simply not part of the
published distributions, because the deferred models they wrap are not.

## 4. The distribution allowlist (packaging policy)

The wheel and the source distribution carry the **core engine interface**
only: the numerical engine (`src/kernel/`, `src/integration/`,
`src/analysis/`), the public headers, the Python interface (`bindings/`,
`python/leo_dynamics/`), the build and packaging files, and the licence and
notice documents. The source distribution adds only its generated `PKG-INFO`.

The exhaustive, machine-readable form of that list is
`release-audit/license-boundary.json` (the `approved_sources` list), and
`ci/verify_license_boundary.py` enforces it against the source tree and
against any built distribution, failing closed.

Material outside the allowlist is **not** redistributed — that is a packaging
decision, not a licence restriction. It stays MIT-licensed in this
repository, it keeps its research value, and it is never deleted from it.
The deferred atmospheric models and their external sources are excluded from
every distribution by the same allowlist, and content markers for those
external sources are refused inside any distributed member.

## 5. Third-party components that are compiled in

The extension links Eigen (MPL-2.0), Boost (BSL-1.0) and pybind11
(BSD-3-Clause), and the platform C++ runtime. Their own terms apply to them,
unchanged by the MIT grant, and are recorded in `THIRD_PARTY_NOTICES.md`.
