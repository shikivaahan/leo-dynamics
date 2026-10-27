# Third-party notices

`leo-dynamics` 0.1.0 ships a compiled Python extension. Building that extension
compiles third-party source code into the shipped binary, so this file records
what is incorporated, under which licence, and how a recipient obtains the
corresponding source.

Two statements govern the whole document:

- **The source files of these dependencies are not shipped in the wheel or the
  source distribution.** Their *compiled* portions are incorporated into
  `_core*.so` / `_core*.pyd`. Binary redistribution is therefore the case that
  has to be handled, not mere header use.
- **This file covers the third-party components only.** The rest of the
  project is original material under the MIT License (`LICENSE`,
  `LICENSE_SCOPE.md`). The published distributions carry the core engine
  interface only; the research material and the deferred atmospheric models
  are not redistributed, so nothing in them reaches a recipient. Including
  these texts is a mechanical compliance step, not a legal conclusion; the
  remaining review items are listed at the end.

Contents:

1. [Eigen 3.4.0 — MPL-2.0](#1-eigen-340--mozilla-public-license-20)
2. [Boost 1.83.0 — Boost Software License 1.0](#2-boost-1830--boost-software-license-10)
3. [pybind11 3.1.0 — BSD 3-Clause](#3-pybind11-310--bsd-3-clause)
4. [Build-time tooling, runtime dependency and CI tooling](#4-build-time-tooling-runtime-dependency-and-ci-tooling)
5. [Platform runtimes the extension links](#5-platform-runtimes-the-extension-links)
6. [Project-Svarog-Orbital — MIT (precautionary attribution)](#6-project-svarog-orbital--mit-precautionary-attribution)
7. [Assets excluded by policy](#7-assets-excluded-by-policy)
8. [Compliance summary and remaining review](#8-compliance-summary-and-remaining-review)
9. [Appendix: full licence texts](#9-appendix-full-licence-texts)

<a id="1-eigen-340--mozilla-public-license-20"></a>
## 1. Eigen 3.4.0 — Mozilla Public License 2.0

| | |
|---|---|
| How it is used | header/template library; its templates are instantiated by the project's C++20 sources and the resulting code is compiled into `_core` |
| Copyright | Copyright (C) 2008 Gael Guennebaud `<gael.guennebaud@inria.fr>`; Copyright (C) 2007-2011 Benoit Jacob `<jacob.benoit.1@gmail.com>` (per-file headers) |
| Unmodified corresponding source | `https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz` |
| SHA-256 of that archive | `8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72` |
| Version | 3.4.0, release tag `3.4.0` |
| Licence | MPL-2.0 (verbatim text in [the appendix](#mozilla-public-license-version-20); upstream `COPYING.MPL2`) |

### 1.1 What is actually compiled

The project's C++ sources include three Eigen headers: `Eigen/Core`,
`Eigen/Geometry` and `Eigen/Cholesky`. Their templates — together with the
internal `Eigen/src/` headers they depend on — are instantiated and compiled
into `_core`. No `Eigen/unsupported/` module and no LAPACKE, BLAS or MKL
integration is enabled in this project: `EIGEN_USE_MKL`, `EIGEN_USE_LAPACKE` and
`EIGEN_USE_BLAS_TPL` are defined nowhere, so the optional integration headers
contribute no code.

Within the Eigen sources this build consumes, the following carry third-party
notices in addition to MPL-2.0, and are reproduced in
[§1.3](#13-third-party-notices-inside-the-eigen-sources):

| Header | Notice |
|---|---|
| `Eigen/src/Core/arch/Default/Half.h` | conversion routines © 2016 Fabian Giesen, BSD |
| `Eigen/src/Geometry/AlignedBox.h` | `Eigen::AlignedBox::transform` © 2011-2014 Willow Garage, Inc. and © 2014-2015 Open Source Robotics Foundation, BSD |
| `Eigen/src/Core/util/MKL_support.h` | © 2011 Intel Corporation, BSD 3-Clause (declarations; no code compiled, see above) |

The rest of the Eigen sources this build consumes carry the standard MPL-2.0
per-file header. This list covers the notices present in the pinned upstream
sources; it is not an audit of which templates an optimiser ends up
instantiating in the shipped binary.

### 1.2 MPL-2.0 obligations

The obligations that apply to this distribution are the ones in §3 of the
MPL-2.0 text, reproduced verbatim in [the appendix](#mozilla-public-license-version-20)
and available upstream as `COPYING.MPL2` in the archive named above.

- **§3.1 — Distribution of Source Form.** A source-form distribution of the
  MPL-2.0-covered Eigen files must keep those files under MPL-2.0, must tell
  recipients that the Source Code Form is governed by MPL-2.0 and how to
  obtain that licence, and must not alter or restrict the recipients' rights
  in the Source Code Form. No source-form Eigen files are embedded in either
  archive; source made available for executable distribution under §3.2
  remains MPL-governed as §3.1 requires.
- **§3.2 — Distribution of Executable Form.** The extension is distributed in
  executable form, so §3.2 applies. (a) The Covered Software must also be
  made available in Source Code Form, and recipients must be told how to
  obtain it by reasonable means, in a timely manner, at a charge no more than
  the cost of distribution. The corresponding source is the unmodified Eigen
  3.4.0 archive named above
  (`https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz`,
  SHA-256 `8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72`),
  or the upstream project at <https://gitlab.com/libeigen/eigen> at tag
  `3.4.0`; the BSD-licensed fragments listed in §1.1 are part of the same
  archive. (b) The Executable Form may be distributed under MPL-2.0 or
  sublicensed under different terms, **provided that the licence for the
  Executable Form does not attempt to limit or alter the recipients' rights
  in the Source Code Form**. MIT applies to the project's enumerated
  larger-work code; it neither restricts recipients' Eigen source rights nor
  relicenses Eigen's Source Code Form.
- **§3.3 — Distribution of a Larger Work.** The extension is a Larger Work
  that includes the Covered Software. It may be distributed under terms of the
  distributor's choice, provided the MPL-2.0 requirements for the Covered
  Software are also complied with — which §3.2 above does.
- **§3.4 — Notices.** The notices in the Eigen sources (the copyright notice,
  the patent grant, the warranty disclaimer and the liability limitation, and
  the BSD notices reproduced in §1.3) must not be removed or altered in the
  Source Code Form. The files this build consumes are **not modified**: the
  build compiles the unmodified Eigen 3.4.0 sources from the pinned archive,
  and the BSD notices are reproduced verbatim in §1.3.

No obligation in MPL-2.0 §3 prohibits distributing the executable form or the
larger work under other terms, or charging for it.

<a id="13-third-party-notices-inside-the-eigen-sources"></a>
### 1.3 Third-party notices inside the Eigen sources

Reproduced verbatim from the pinned Eigen 3.4.0 archive. Each is a BSD-style
licence requiring that the copyright notice, the conditions and the disclaimer
are retained in source redistributions and reproduced in binary
distributions' documentation or other accompanying materials; this file is that
accompanying material.

**`Eigen/src/Core/arch/Default/Half.h` — conversion routines © 2016 Fabian
Giesen**

```text
Copyright (c) Fabian Giesen, 2016
All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

**`Eigen/src/Geometry/AlignedBox.h` — `Eigen::AlignedBox::transform`**

```text
Software License Agreement (BSD License)

Copyright (c) 2011-2014, Willow Garage, Inc.
Copyright (c) 2014-2015, Open Source Robotics Foundation
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided
   with the distribution.
 * Neither the name of Open Source Robotics Foundation nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

**`Eigen/src/Core/util/MKL_support.h` — © 2011 Intel Corporation**

```text
Copyright (c) 2011, Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
* Neither the name of Intel Corporation nor the names of its contributors may
  be used to endorse or promote products derived from this software without
  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

<a id="2-boost-1830--boost-software-license-10"></a>
## 2. Boost 1.83.0 — Boost Software License 1.0

| | |
|---|---|
| How it is used | Boost.Odeint integrator (`runge_kutta_dopri5`) whose templates are instantiated and compiled into `_core`; no compiled Boost library is linked |
| Unmodified corresponding source | `https://github.com/boostorg/boost/releases/download/boost-1.83.0/boost-1.83.0.tar.xz` |
| SHA-256 of that archive | `c5a0688e1f0c05f354bbd0b32244d36085d9ffc9f932e8a18983a9908096f614` |
| Version | 1.83.0 |
| Licence | Boost Software License 1.0, shipped upstream as `LICENSE_1_0.txt` in that archive and reproduced verbatim in [the appendix](#boost-software-license-version-10) |

The project includes `<boost/numeric/odeint/stepper/runge_kutta_dopri5.hpp>`;
Odeint and its transitive Boost header dependencies are compiled from that same
pinned archive. Threading uses the platform's standard `std::thread`. No compiled
Boost binary library is linked into `_core`.

<a id="3-pybind11-310--bsd-3-clause"></a>
## 3. pybind11 3.1.0 — BSD 3-Clause

| | |
|---|---|
| How it is used | its C++ headers are compiled into `_core` to generate the Python bindings |
| Copyright | Copyright (c) 2016 Wenzel Jakob `<wenzel.jakob@epfl.ch>`, All rights reserved |
| Unmodified corresponding source | `https://files.pythonhosted.org/packages/76/f3/95b0f40b31df41dbfe6bb0857419c9442c15839cbac4796f1c26ae0b6081/pybind11-3.1.0.tar.gz` |
| SHA-256 of that archive | `a1cc06b524ab3edca51f8ad3895f9c4fa20b8b19283173dff4ae781449dc9639` |
| Upstream | <https://github.com/pybind/pybind11>, tag `v3.1.0`, file `LICENSE` |
| Licence | BSD 3-Clause, verbatim text in [the appendix](#bsd-3-clause-license-template) |

The BSD 3-Clause terms require that redistributions retain the copyright
notice, the condition list and the disclaimer, and that binary redistributions
reproduce them in their documentation or other materials. Reproducing the full
text with its copyright line in the appendix is intended to satisfy that
requirement.

<a id="4-build-time-tooling-runtime-dependency-and-ci-tooling"></a>
## 4. Build-time tooling, runtime dependency and CI tooling

None of the components in this table is compiled into, or linked with, `_core`.
The platform runtimes `_core` *does* link are covered separately in §5.

| Component | Pin | Licence | Source |
|---|---|---|---|
| scikit-build-core | `==0.11.6` | Apache-2.0 | `https://pypi.org/project/scikit-build-core/0.11.6/`; sdist SHA-256 `5982ccd839735be99cfd3b92a8847c6c196692f476c215da84b79d2ad12f9f1b` |
| CMake (PyPI wheel) | `>=3.28` | Apache-2.0 / BSD | `https://pypi.org/project/cmake/` |
| Ninja (PyPI wheel) | `>=1.11` | Apache-2.0 | `https://pypi.org/project/ninja/` |
| NumPy | runtime dependency, `>=2.0` | BSD-3-Clause (bundled: 0BSD, MIT, Zlib, CC0-1.0) | `https://github.com/numpy/numpy` — installed separately by pip, never vendored |
| cibuildwheel | CI only, `4.3.1` | BSD-2-Clause | `https://github.com/pypa/cibuildwheel` |
| auditwheel | CI only (Linux repair) | MIT | `https://github.com/pypa/auditwheel` |
| delvewheel | CI only (Windows repair) | MIT | `https://github.com/adang1345/delvewheel` |
| delocate-wheel | CI only (macOS repair) | BSD | `https://github.com/matthew-brett/delocate` |
| build | `1.6.1` | MIT | `https://pypi.org/project/build/` |
| Catch2 v3.7.1 | tests only; `BUILD_TESTING=OFF` for distribution builds | Boost Software License 1.0 | <https://github.com/catchorg/Catch2>, tag `v3.7.1` |

<a id="5-platform-runtimes-the-extension-links"></a>
## 5. Platform runtimes the extension links

`_core` is a native extension module, so it links the platform's C++ runtime.
The locally inspected Linux wheel bundles no runtime library. Runtime bundling
on macOS and Windows has not been verified by this audit.

| Platform | Linked runtime | Terms |
|---|---|---|
| Linux (x86-64, manylinux) | `libstdc++.so.6`, `libgcc_s.so.1`, `libm.so.6`, `libc.so.6` | libstdc++ and libgcc_s are distributed under the GNU General Public License version 3 with the **GCC Runtime Library Exception version 3.1**, which grants permission to propagate a work of Target Code formed by combining the Runtime Library with Independent Modules, and to convey that combination under terms of your choice, consistent with the licensing of the Independent Modules. The exception text may be copied, but is not required to be. |
| macOS (arm64, x86-64) | `libc++.1.dylib`, `libSystem` | libc++ is under the Apache License 2.0 with the LLVM Exceptions to the Apache 2.0 License, which permit linking and redistribution under other terms. |
| Windows (x86-64; configuration assessment only) | Microsoft Visual C++ runtime (toolchain-dependent) | Microsoft redistribution terms apply if runtime files are bundled; inspect each repaired Windows wheel before release. |

Observation status of this audit: the Linux linkage above was **observed** on
the artefact built for this audit (`ldd`/`readelf -d` on
`_core.cpython-313-x86_64-linux-gnu.so`). The macOS and Windows rows are a
**static configuration assessment** of the build configuration; no macOS or
Windows binary was built or inspected in this audit.

<a id="6-project-svarog-orbital--mit-precautionary-attribution"></a>
## 6. Project-Svarog-Orbital — MIT (precautionary attribution)

This section is a precaution, not a statement of derivation.

| | |
|---|---|
| Upstream project | `Project-Svarog-Orbital` — `https://github.com/ImperialSpaceSociety/Project-Svarog-Orbital` (Imperial College Space Society) |
| Licence | MIT — the upstream README at commit `58ab4d4964f9c20b7070efae48f8916e8a99784e` states "MIT – see `LICENSE`", and the upstream `LICENSE` file reads: |
| Copyright | Copyright (c) 2022 Imperial College Space Society |
| Retrieval limitation | The upstream GitHub repository returned HTTP 404 at the time of this audit. The notice above was read from the upstream README at the commit the project's own notes cite, and from the `LICENSE` file of a local checkout of that upstream project. |
The basic exponential atmosphere's fixed 40 altitude/density pairs match the
upstream `src/Constants.jl` table after converting kilometres to metres;
`include/leo/kernel/aerodynamics.hpp` acknowledges those knots. The upstream
labels its table “1976 US Standard Atmosphere”. The ultimate primary-standard
provenance and any applicable rights have not been independently established.
These fixed engine parameters are not the excluded empirical providers or
private research campaign datasets. No numerical behaviour has been removed.


The MIT licence requires that the copyright notice and the permission notice be
included in all copies or substantial portions of the software. Whether any part
of this distribution is a copy or a substantial portion of that upstream work is
a legal question this file does not decide. The notice is therefore reproduced
here, as attribution, at no cost to the distribution:

```
MIT License

Copyright (c) 2022 Imperial College Space Society

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The owner's statement is that no Imperial College or Siemens source code is
used, and that the code and constants they contributed to the Imperial College
Space Society project are their own original material, licensed under the MIT
grant of `LICENSE`. Repository notes nevertheless compare the C++ API with
that upstream Julia project, so the attribution above is retained as a
precautionary acknowledgement of the upstream work; it is not a finding that
this distribution derives from it.

<a id="7-tudat-reference-oracle--bsd-3-clause"></a>
## 7. Tudat reference oracle — BSD 3-Clause

The project uses the **Tudat** astrodynamics toolbox as an independently
installed reference oracle for part of its numerical qualification. Tudat is
distributed under the BSD 3-Clause licence; the full text is in
[the appendix](#bsd-3-clause-license-template).

| Component | Pin | Licence | Source |
|---|---|---|---|
| Tudat (native) | `2.14.0` | BSD-3-Clause | <https://github.com/tudat-team/tudat> |
| tudatpy (Python interface) | `0.9.0` (module reports `0.8.0`) | BSD-3-Clause | <https://github.com/tudat-team/tudatpy> |

Tudat itself is **not** part of this repository and is **not** redistributed
by it. The conditions and disclaimer of its BSD 3-Clause terms are those of
the template in [the appendix](#bsd-3-clause-license-template), and the
upstream copyright line is the one in the Tudat `LICENSE` file at the source
links above; anyone who obtains the upstream distribution gets that notice
with it.

The reference trajectories this project generates with that oracle are
numerical outputs of this project's own generators. They are original
material of this project under the MIT grant of `LICENSE`, and this file
makes no claim that the upstream code licence governs them.

<a id="8-assets-excluded-by-policy"></a>
## 8. Assets excluded by policy

CSPICE, NRLMSISE-00 and JB2008 model sources, SPICE kernels, external empirical
atmosphere/weather datasets and the research fixtures are not downloaded,
compiled into or embedded in either distribution. The basic exponential model's
40 fixed engine parameters are the explicit exception disclosed in §6.
The provenance rules this project applies to external
assets are in `docs/atmosphere/INPUTS.md`, which is part of the project's
research material and is not distributed with either artefact.

The research records quote a small number of published upstream reference
values for comparison — a single NRLMSISE-00 validation density
(`6.505e-14` kg/m³) in
`docs/atmosphere/evidence/validation-amendment-v1/altitude-datum.json`, and
derived ratios in `docs/atmosphere/evidence/nrlmsise00-validation.json`. They
are quoted for validation comparison, are not redistributed in either
artefact, and the upstream terms of the quoted material are unchanged.

The project's research documentation cites further external projects (the
Orekit space-weather and JB2008 references, and the Julia
`SatelliteToolboxAtmosphericModels.jl` implementation) for context. Those
citations live in the non-distributed research material, and a citation in a
document that is not distributed is not a redistribution, so no notice is owed
for them by either artefact. The Tudat toolbox is covered by §7 above.

<a id="9-compliance-summary-and-remaining-review"></a>
## 9. Compliance summary and remaining review

Concretely done:

- every dependency compiled into `_core` is named above with its version, the
  exact source URL and the SHA-256 of the unmodified input;
- the MPL-2.0 and BSL-1.0 texts are reproduced verbatim in full, and the BSD
  3-Clause text is reproduced with its copyright line, so each licence's
  retention requirements can be met from this file;
- the BSD notices carried inside the Eigen sources are reproduced verbatim in
  §1.3, with their copyright lines, rather than being swept into "Eigen is
  MPL-2.0";
- MPL-2.0 §3.2(a) source availability is addressed by pointing recipients at
  the pinned upstream archive and recording its hash; no covered file is
  modified, so §3.4's notice-retention requirement is met;
- the platform runtimes the extension links are named in §5, with the licence
  terms that permit linking them;
- the upstream MIT notice is reproduced in §6 as a precaution;
- the Tudat BSD 3-Clause terms are reproduced in §7 for the separately
  installed reference oracle that is not redistributed.

Remaining, and not settled by anything in this file:

1. **The hash proves the downloaded archive, not the consumed headers.** The
   SHA-256 values above pin the archives that `cmake/provision.cmake` downloads
   and verifies at configure time. The provisioning flow applies no patch to
   the covered files, but which headers a given binary was compiled against is
   a release-time verification (a build with
   `LEO_PROVISION_DEPENDENCIES=OFF` resolves Eigen and Boost from the host).
2. **The upstream attribution is precautionary.** §6 gives attribution as a
   precaution. The owner states that the code and constants they contributed to
   that upstream project are their own original material under the MIT grant;
   this file records the acknowledgement, not a determination about the
   upstream work.
3. **Regeneration on change.** §1.1 and §2 are specific to the components
   compiled by 0.1.0. If a future release enables another Eigen or Boost
   component, or links a compiled Boost binary library, this file must be
   regenerated.
4. **Scope of this file.** Reproducing licence texts and pinning the consumed
   archives is a mechanical step. This file records what is incorporated and
   under which terms; it makes no claim about obligations beyond those terms
   in any jurisdiction.

<a id="10-appendix-full-licence-texts"></a>
## 10. Appendix: full licence texts

The three texts below are reproduced verbatim from the sources named in their
headings.

### Mozilla Public License Version 2.0

<a id="mozilla-public-license-version-20"></a>
From <https://www.mozilla.org/media/MPL/2.0/index.txt>, identical to upstream
`COPYING.MPL2` in Eigen 3.4.0.

Mozilla Public License Version 2.0
==================================

1. Definitions
--------------

1.1. "Contributor"
    means each individual or legal entity that creates, contributes to
    the creation of, or owns Covered Software.

1.2. "Contributor Version"
    means the combination of the Contributions of others (if any) used
    by a Contributor and that particular Contributor's Contribution.

1.3. "Contribution"
    means Covered Software of a particular Contributor.

1.4. "Covered Software"
    means Source Code Form to which the initial Contributor has attached
    the notice in Exhibit A, the Executable Form of such Source Code
    Form, and Modifications of such Source Code Form, in each case
    including portions thereof.

1.5. "Incompatible With Secondary Licenses"
    means

    (a) that the initial Contributor has attached the notice described
        in Exhibit B to the Covered Software; or

    (b) that the Covered Software was made available under the terms of
        version 1.1 or earlier of the License, but not also under the
        terms of a Secondary License.

1.6. "Executable Form"
    means any form of the work other than Source Code Form.

1.7. "Larger Work"
    means a work that combines Covered Software with other material, in
    a separate file or files, that is not Covered Software.

1.8. "License"
    means this document.

1.9. "Licensable"
    means having the right to grant, to the maximum extent possible,
    whether at the time of the initial grant or subsequently, any and
    all of the rights conveyed by this License.

1.10. "Modifications"
    means any of the following:

    (a) any file in Source Code Form that results from an addition to,
        deletion from, or modification of the contents of Covered
        Software; or

    (b) any new file in Source Code Form that contains any Covered
        Software.

1.11. "Patent Claims" of a Contributor
    means any patent claim(s), including without limitation, method,
    process, and apparatus claims, in any patent Licensable by such
    Contributor that would be infringed, but for the grant of the
    License, by the making, using, selling, offering for sale, having
    made, import, or transfer of either its Contributions or its
    Contributor Version.

1.12. "Secondary License"
    means either the GNU General Public License, Version 2.0, the GNU
    Lesser General Public License, Version 2.1, the GNU Affero General
    Public License, Version 3.0, or any later versions of those
    licenses.

1.13. "Source Code Form"
    means the form of the work preferred for making modifications.

1.14. "You" (or "Your")
    means an individual or a legal entity exercising rights under this
    License. For legal entities, "You" includes any entity that
    controls, is controlled by, or is under common control with You. For
    purposes of this definition, "control" means (a) the power, direct
    or indirect, to cause the direction or management of such entity,
    whether by contract or otherwise, or (b) ownership of more than
    fifty percent (50%) of the outstanding shares or beneficial
    ownership of such entity.

2. License Grants and Conditions
--------------------------------

2.1. Grants

Each Contributor hereby grants You a world-wide, royalty-free,
non-exclusive license:

(a) under intellectual property rights (other than patent or trademark)
    Licensable by such Contributor to use, reproduce, make available,
    modify, display, perform, distribute, and otherwise exploit its
    Contributions, either on an unmodified basis, with Modifications, or
    as part of a Larger Work; and

(b) under Patent Claims of such Contributor to make, use, sell, offer
    for sale, have made, import, and otherwise transfer either its
    Contributions or its Contributor Version.

2.2. Effective Date

The licenses granted in Section 2.1 with respect to any Contribution
become effective for each Contribution on the date the Contributor first
distributes such Contribution.

2.3. Limitations on Grant Scope

The licenses granted in this Section 2 are the only rights granted under
this License. No additional rights or licenses will be implied from the
distribution or licensing of Covered Software under this License.
Notwithstanding Section 2.1(b) above, no patent license is granted by a
Contributor:

(a) for any code that a Contributor has removed from Covered Software;
    or

(b) for infringements caused by: (i) Your and any other third party's
    modifications of Covered Software, or (ii) the combination of its
    Contributions with other software (except as part of its Contributor
    Version); or

(c) under Patent Claims infringed by Covered Software in the absence of
    its Contributions.

This License does not grant any rights in the trademarks, service marks,
or logos of any Contributor (except as may be necessary to comply with
the notice requirements in Section 3.4).

2.4. Subsequent Licenses

No Contributor makes additional grants as a result of Your choice to
distribute the Covered Software under a subsequent version of this
License (see Section 10.2) or under the terms of a Secondary License (if
permitted under the terms of Section 3.3).

2.5. Representation

Each Contributor represents that the Contributor believes its
Contributions are its original creation(s) or it has sufficient rights
to grant the rights to its Contributions conveyed by this License.

2.6. Fair Use

This License is not intended to limit any rights You have under
applicable copyright doctrines of fair use, fair dealing, or other
equivalents.

2.7. Conditions

Sections 3.1, 3.2, 3.3, and 3.4 are conditions of the licenses granted
in Section 2.1.

3. Responsibilities
-------------------

3.1. Distribution of Source Form

All distribution of Covered Software in Source Code Form, including any
Modifications that You create or to which You contribute, must be under
the terms of this License. You must inform recipients that the Source
Code Form of the Covered Software is governed by the terms of this
License, and how they can obtain a copy of this License. You may not
attempt to alter or restrict the recipients' rights in the Source Code
Form.

3.2. Distribution of Executable Form

If You distribute Covered Software in Executable Form then:

(a) such Covered Software must also be made available in Source Code
    Form, as described in Section 3.1, and You must inform recipients of
    the Executable Form how they can obtain a copy of such Source Code
    Form by reasonable means in a timely manner, at a charge no more
    than the cost of distribution to the recipient; and

(b) You may distribute such Executable Form under the terms of this
    License, or sublicense it under different terms, provided that the
    license for the Executable Form does not attempt to limit or alter
    the recipients' rights in the Source Code Form under this License.

3.3. Distribution of a Larger Work

You may create and distribute a Larger Work under terms of Your choice,
provided that You also comply with the requirements of this License for
the Covered Software. If the Larger Work is a combination of Covered
Software with a work governed by one or more Secondary Licenses, and the
Covered Software is not Incompatible With Secondary Licenses, this
License permits You to additionally distribute such Covered Software
under the terms of such Secondary License(s), so that the recipient of
the Larger Work may, at their option, further distribute the Covered
Software under the terms of either this License or such Secondary
License(s).

3.4. Notices

You may not remove or alter the substance of any license notices
(including copyright notices, patent notices, disclaimers of warranty,
or limitations of liability) contained within the Source Code Form of
the Covered Software, except that You may alter any license notices to
the extent required to remedy known factual inaccuracies.

3.5. Application of Additional Terms

You may choose to offer, and to charge a fee for, warranty, support,
indemnity or liability obligations to one or more recipients of Covered
Software. However, You may do so only on Your own behalf, and not on
behalf of any Contributor. You must make it absolutely clear that any
such warranty, support, indemnity, or liability obligation is offered by
You alone, and You hereby agree to indemnify every Contributor for any
liability incurred by such Contributor as a result of warranty, support,
indemnity or liability terms You offer. You may include additional
disclaimers of warranty and limitations of liability specific to any
jurisdiction.

4. Inability to Comply Due to Statute or Regulation
---------------------------------------------------

If it is impossible for You to comply with any of the terms of this
License with respect to some or all of the Covered Software due to
statute, judicial order, or regulation then You must: (a) comply with
the terms of this License to the maximum extent possible; and (b)
describe the limitations and the code they affect. Such description must
be placed in a text file included with all distributions of the Covered
Software under this License. Except to the extent prohibited by statute
or regulation, such description must be sufficiently detailed for a
recipient of ordinary skill to be able to understand it.

5. Termination
--------------

5.1. The rights granted under this License will terminate automatically
if You fail to comply with any of its terms. However, if You become
compliant, then the rights granted under this License from a particular
Contributor are reinstated (a) provisionally, unless and until such
Contributor explicitly and finally terminates Your grants, and (b) on an
ongoing basis, if such Contributor fails to notify You of the
non-compliance by some reasonable means prior to 60 days after You have
come back into compliance. Moreover, Your grants from a particular
Contributor are reinstated on an ongoing basis if such Contributor
notifies You of the non-compliance by some reasonable means, this is the
first time You have received notice of non-compliance with this License
from such Contributor, and You become compliant prior to 30 days after
Your receipt of the notice.

5.2. If You initiate litigation against any entity by asserting a patent
infringement claim (excluding declaratory judgment actions,
counter-claims, and cross-claims) alleging that a Contributor Version
directly or indirectly infringes any patent, then the rights granted to
You by any and all Contributors for the Covered Software under Section
2.1 of this License shall terminate.

5.3. In the event of termination under Sections 5.1 or 5.2 above, all
end user license agreements (excluding distributors and resellers) which
have been validly granted by You or Your distributors under this License
prior to termination shall survive termination.

************************************************************************
*                                                                      *
*  6. Disclaimer of Warranty                                           *
*  -------------------------                                           *
*                                                                      *
*  Covered Software is provided under this License on an "as is"       *
*  basis, without warranty of any kind, either expressed, implied, or  *
*  statutory, including, without limitation, warranties that the       *
*  Covered Software is free of defects, merchantable, fit for a        *
*  particular purpose or non-infringing. The entire risk as to the     *
*  quality and performance of the Covered Software is with You.        *
*  Should any Covered Software prove defective in any respect, You     *
*  (not any Contributor) assume the cost of any necessary servicing,   *
*  repair, or correction. This disclaimer of warranty constitutes an   *
*  essential part of this License. No use of any Covered Software is   *
*  authorized under this License except under this disclaimer.         *
*                                                                      *
************************************************************************

************************************************************************
*                                                                      *
*  7. Limitation of Liability                                          *
*  --------------------------                                          *
*                                                                      *
*  Under no circumstances and under no legal theory, whether tort      *
*  (including negligence), contract, or otherwise, shall any           *
*  Contributor, or anyone who distributes Covered Software as          *
*  permitted above, be liable to You for any direct, indirect,         *
*  special, incidental, or consequential damages of any character      *
*  including, without limitation, damages for lost profits, loss of    *
*  goodwill, work stoppage, computer failure or malfunction, or any    *
*  and all other commercial damages or losses, even if such party      *
*  shall have been informed of the possibility of such damages. This   *
*  limitation of liability shall not apply to liability for death or   *
*  personal injury resulting from such party's negligence to the       *
*  extent applicable law prohibits such limitation. Some               *
*  jurisdictions do not allow the exclusion or limitation of           *
*  incidental or consequential damages, so this exclusion and          *
*  limitation may not apply to You.                                    *
*                                                                      *
************************************************************************

8. Litigation
-------------

Any litigation relating to this License may be brought only in the
courts of a jurisdiction where the defendant maintains its principal
place of business and such litigation shall be governed by laws of that
jurisdiction, without reference to its conflict-of-law provisions.
Nothing in this Section shall prevent a party's ability to bring
cross-claims or counter-claims.

9. Miscellaneous
----------------

This License represents the complete agreement concerning the subject
matter hereof. If any provision of this License is held to be
unenforceable, such provision shall be reformed only to the extent
necessary to make it enforceable. Any law or regulation which provides
that the language of a contract shall be construed against the drafter
shall not be used to construe this License against a Contributor.

10. Versions of the License
---------------------------

10.1. New Versions

Mozilla Foundation is the license steward. Except as provided in Section
10.3, no one other than the license steward has the right to modify or
publish new versions of this License. Each version will be given a
distinguishing version number.

10.2. Effect of New Versions

You may distribute the Covered Software under the terms of the version
of the License under which You originally received the Covered Software,
or under the terms of any subsequent version published by the license
steward.

10.3. Modified Versions

If you create software not governed by this License, and you want to
create a new license for such software, you may create and use a
modified version of this License if you rename the license and remove
any references to the name of the license steward (except to note that
such modified license differs from this License).

10.4. Distributing Source Code Form that is Incompatible With Secondary
Licenses

If You choose to distribute Source Code Form that is Incompatible With
Secondary Licenses under the terms of this version of the License, the
notice described in Exhibit B of this License must be attached.

Exhibit A - Source Code Form License Notice
-------------------------------------------

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.

If it is not possible or desirable to put the notice in a particular
file, then You may include the notice in a location (such as a LICENSE
file in a relevant directory) where a recipient would be likely to look
for such a notice.

You may add additional accurate notices of copyright ownership.

Exhibit B - "Incompatible With Secondary Licenses" Notice
---------------------------------------------------------

  This Source Code Form is "Incompatible With Secondary Licenses", as
  defined by the Mozilla Public License, v. 2.0.

### Boost Software License Version 1.0

<a id="boost-software-license-version-10"></a>
From <https://www.boost.org/LICENSE_1_0.txt>, identical to `LICENSE_1_0.txt` in
Boost 1.83.0 and to Catch2 v3.7.1's `LICENSE.txt`.

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

### BSD 3-Clause License Template

<a id="bsd-3-clause-license-template"></a>
From pybind11 3.1.0 `LICENSE`,
<https://raw.githubusercontent.com/pybind/pybind11/v3.1.0/LICENSE>, including its
copyright line.

Copyright (c) 2016 Wenzel Jakob <wenzel.jakob@epfl.ch>, All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Please also refer to the file .github/CONTRIBUTING.md, which clarifies licensing of
external contributions to this project including patches, pull requests, etc.