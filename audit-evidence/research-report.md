# v0.1.0 licensing research — corrected evidence summary

**Baseline:** `main` at `23df023d6080c9aeb27e9ceddba1671651205584`, 1,951 tracked paths, 926,464,624 bytes. **Later changed-state review:** [`boundary-independent-review.md`](boundary-independent-review.md), with exact reviewed hashes and final negative-scenario outcomes. This report supersedes the preliminary wording; historical baseline findings below must not be presented as unresolved defects in the changed release branch.

Research was read-only outside `audit-evidence/`. No publication, workflow trigger, repository visibility change, source edit, native build or native test was performed by this researcher. Synthetic archive scenarios were later exercised under the explicit independent-review assignment; they were not actual builds. Build/dependency/linkage logs supplied by the release worker were read, not rerun.

> **Superseded 2026-10-27.** The owner has since authorised the standard MIT grant over the whole project, and the proprietary scope this record describes no longer applies. The classifications below are the dated record of the 2026-10-26 audit; the governing classification is now `release-audit/license-boundary.json`.

## 1. Owner decision versus legal-title evidence

- The **owner approved MIT for the basic numerical engine and Python interface**, reserving the empirical integrations, research, optimisation and datasets. That decision is present; it is not an unanswered licensing-intent question.
- Baseline lacked a `LICENSE`/`COPYING` file, including across the 79 then-reachable commits. GitHub's repository object reported `license: null`; `/license` returned 404. The changed branch now supplies `LICENSE`, `LICENSE_SCOPE.md` and a conservative canonical boundary. Baseline absence is therefore **historical**, not the changed state.
- Git labels show 77 commits by `Shiki Vaahan <shikivaahan@gmail.com>` and two by `LEO benchmark snapshot <benchmarks@localhost>`. The first commit contains `.gitignore` only. None of these facts establishes legal copyright ownership, absence of earlier work, or absence of imported material.
- GitHub API confirmed a **private, personal-account repository** and one returned administrator collaborator. The public account profile's free-text `company` field says `Imperial College London`. This is affiliation metadata, **not proof of employment, legal identity, assignment or institutional title**. An empty `/users/shikivaahan/orgs` result is not proof of no private memberships.
- No assignment instrument or CLA was identified in the searched repository material. Legal title/assignment remains a separate uncertainty; this audit does not invalidate or replace the owner's MIT decision.

## 2. Concrete upstream provenance

### Project Svarog / SolarSailPropagator.jl

`docs/stage3.md` cites `ImperialSpaceSociety/Project-Svarog-Orbital` at commit `58ab4d4964f9c20b7070efae48f8916e8a99784e` and maps Julia analysis functionality to C++ counterparts. Numerical-audit scenario provenance cites its `examples/earth_orbit_acc.jl`.

The GitHub URL returned 404 during this audit. The existing local clone was inspected read-only:

- README at the cited commit states **“MIT – see LICENSE.”**
- The checked-out older branch has `legacy/LICENSE`: MIT text, **Copyright (c) 2022 Imperial College Space Society**. No licence file is present in the cited commit's tree. The exact grant scope/current remote state cannot be established from a currently retrievable upstream LICENSE file.
- Three compared algorithms differ: SRP optical formulation, cold-beam drag coefficients, and apsis detection. This is evidence of different implementations in those areas, **not proof of no inherited material**.
- **Separately, all 40 fixed altitude/density anchors in public `src/kernel/aerodynamics.cpp:9-21` match the upstream Constants.jl table after km-to-m conversion.** Decimal comparison, hashes and locations are recorded in the independent review. The public aerodynamic header already acknowledges the Julia knots. A line-oriented snippet scan did not catch this reformatted numeric table.
- Numerical facts, table selection, database rights, ultimate provenance and MIT “substantial portions” applicability are not decided here. Precautionary upstream attribution was recommended and is now present in the worker's changed notices. The changed public README explicitly discloses the 40 anchors rather than claiming no atmospheric reference data exists in the binary.

The upstream history also contains 13 commits labelled with `shiki.vaahan@siemens.com`; seven are ancestors of its `origin/main`: `ac63c0c`, `2eb92fa`, `388a4b2`, `2542812`, `5a30ad7`, `7106844`, `9f3ed4a`. Email labels are not evidence that Siemens supplied or owns the material. This is an open title/authorisation question, not an infringement or ownership finding. Keep the upstream attribution label, the GitHub profile affiliation, and the corporate-email evidence distinct.

### Other referenced work

- **Orekit:** Apache-2.0 licence retrieved from its GitHub `LICENSE.txt`; cited in atmospheric research as an independent implementation/reference and as a mirror for CSSI weather records. CSSI source-data rights are not automatically granted by Orekit's code licence.
- **SatelliteToolboxAtmosphericModels.jl:** MIT text retrieved; copyright 2018–2026 Ronan Arraes Jardim Chagas. Cited for context in excluded atmospheric research.
- **Tudat/tudatpy:** used by research generators and frozen reference fixtures, not the reviewed 52-file public core. The research environment records distribution tudatpy 0.9.0, native tudat 2.14.0 and module-reported 0.8.0; those are different recorded identifiers, not interchangeable versions. Its code/data terms must be assessed separately. Final public packaging excludes tests, generators and fixtures.

## 3. Restricted or unverified scientific assets

- **JB2008:** repository research quotes the official distribution's Space Environment Technologies “All rights reserved” notice and records no redistribution grant. Official `JB2008.f` is supplied externally and hash-checked. Six tracked `.f90` wrappers were inspected; they contain calling/I/O glue rather than model equations. Earlier references to seven wrappers or an additional altitude astronomy file were incorrect and are withdrawn.
- **NRLMSISE-00 Brodowski translation:** externally supplied, pinned C/header/data files. Candidate licence filenames at the documented cgit endpoint returned 404; licence remains **unverified**, not automatically public domain. The committed parameter-inventory JSON records shapes, counts and hashes rather than coefficient values.
- **CSPICE/kernels, JB driver/weather files and CSSI records:** toolkit-code rights, data rights and mirror-host rights are distinct. No blanket redistribution clearance was established by this audit. Final public packaging excludes the proprietary adapters and research data.
- Baseline packaging included first-party empirical-provider adapters/glue through broad source inclusion even though official external model sources were not vendored. Do not confuse “external source not vendored” with “no proprietary integration source in the baseline sdist.” The changed conservative boundary remedies that distinction.

## 4. Inventory and scan coverage

`source-inventory.json` enumerates all **1,951 baseline tracked paths**, with sizes and SHA-256. Its current scope field is a **non-binding projection of the conservative reviewed release rules onto baseline paths**: 49 public-core paths, 1,900 proprietary, two repository-plumbing paths. The changed release adds three public licence/readme paths to reach 52. Current `LICENSE_SCOPE.md`/canonical manifest govern; no MIT permission is inferred for helper docs, tests or CI.

Exact verbatim-scan coverage:

| Group | Paths |
|---|---:|
| Selected source-like files compared against four upstream indexes | 229 |
| Source-extension files omitted because they are bulk evidence | 27 |
| JSON/JSONL/gzip/PNG/SVG/CSV payloads not line-compared | 1,615 |
| Other extensions, principally Markdown, not line-compared | 80 |
| **Total** | **1,951** |

The attribution-marker extension set contains **321 baseline candidate paths** and overlaps those groups. The earlier 639-path exclusion-filter listing and 327 working-index count were not baseline content-scan coverage.

The line scan found only three consecutive pybind11 include directives in `bindings/common.hpp`. Its result is limited to selected normalised lines; it is **not** whole-repository clearance, semantic derivation analysis or a claim that no transformed numerical material exists.

Packaging booleans are explicitly **declared-rule projections, not observed archive membership**. The corrected baseline projection uses scikit-build-core 0.11.6 include-first/exclude-second/default-unignored semantics, projecting 179 tracked sdist members, not the earlier erroneous 68. Real backend traversal also sees untracked files and symlink-followed directories, so tracked-only projections cannot establish actual archive contents. Final archives are separately checked by the release worker.

## 5. Dependencies, primary texts and later actual evidence

See [`dependency-license-matrix.md`](dependency-license-matrix.md). Eigen 3.4.0, Boost 1.83.0 and pybind11 3.1.0 archive hashes were independently recomputed and match their pins. A pristine downloaded archive is not, alone, evidence of consumed build bytes. Later worker evidence reports **170/170 consumed Eigen headers and 1,066/1,066 Boost headers matching**, no missing/mismatches, for the recorded Linux build.

Important corrected legal descriptions:

- MPL-2.0 **§3.1** governs source-form distribution; **§3.2(a)** requires source availability/instructions for executable distribution; **§3.2(b)** permits other executable terms without restricting source-form rights; **§3.3** permits a Larger Work under chosen terms subject to covered-code obligations; **§3.4** protects substantive notices. There is no general prohibition on selling a Larger Work or licensing the project's own code MIT.
- GCC Runtime Library Exception is **v3.1**, not v3.3. Its §1 grants additional permission for eligible combinations with Independent Modules. It does not impose the previously misattributed replacement-runtime/notice clauses.
- CMake upstream is BSD-3-Clause. The inspected PyPI distribution separately declares Apache packaging terms and bundles upstream/third-party licences. Do not flatten that distinction.
- Dynamic use of system libraries is not the same as bundling library binaries, but does **not** automatically prove every LGPL notice/linking condition inapplicable. No blanket “dynamic means no obligations” conclusion is made.

Later resolved Linux environments, from worker logs inspected rather than rerun: build scikit-build-core 0.11.6, pybind11 3.1.0, CMake 4.4.4, Ninja 1.13.2, packaging 26.3, pathspec 1.1.1; driver build 1.6.1, pyproject-hooks 1.3.3, pip 26.2.2; runtime NumPy 2.5.4. Packaging is Apache-2.0 OR BSD-2-Clause; **pathspec is MPL-2.0**; hooks are MIT. Installed licence files for those three transitives were independently opened. These are build tools, not additional vendored runtime components. Research Matplotlib/SciPy/Tudat dependencies remain separate from release requirements.

## 6. GitHub environment evidence

Authenticated read-only API confirmed private repository, configured `testpypi` environment with empty protection rules, and no release deployments/runs returned at research time. A returned configuration does **not** prove current paid-plan entitlement; the token did not expose the plan. GitHub documents private environment configuration separately from required-reviewer protection availability:

- [Managing environments](https://docs.github.com/en/actions/how-tos/deploy/configure-and-manage-deployments/manage-environments).
- [Required reviewers](https://docs.github.com/en/actions/reference/workflows-and-actions/deployments-and-environments#required-reviewers): Free/Pro/Team reviewers are public-repository features.

No environment existence, author name, profile field or unrelated repository licence is treated as proof of legal title or a current plan.

## 7. Current hand-off

**All specific independent archive false acceptances are fixed at the final reviewed verifier hash** in `boundary-independent-review.md`. The review reports valid controls, adversarial outcomes, code hashes, actual worker evidence and residual trust boundaries. Do not carry preliminary vulnerability findings into the final report as current defects.

Remaining uncertainties are legal title/assignment, underlying numeric-table rights/provenance, unverified external model/data rights, current GitHub plan, and unobserved cross-platform dependency/build outcomes. These are not decisions this research can make. Any further byte changes require the release worker's final archive check; no publication was authorised or performed.
