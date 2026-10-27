# v0.1.0 pre-release licensing audit

Audit date: 2026-10-26. Private repository: `shikivaahan/leo-dynamics`.
Branch: `audit/release-licensing-v0.1.0`, based on
`23df023d6080c9aeb27e9ceddba1671651205584`. No history was rewritten and the
pre-existing untracked `outputs/` was preserved. No push, publication, remote
repository creation, workflow dispatch or upload was performed.

## Decision: NO-GO for publication; local mechanical boundary passes

The owner approved MIT intent for the basic engine/Python interface, retaining
all high-fidelity atmosphere, research, optimisation experiments/datasets and
history as proprietary. That intent is implemented, not deferred. It is not
proof of legal title. Publication remains **NO-GO** until the owner resolves the
legal-title/upstream-parameter questions below and authorises publication.
Private-repository plan support and the final supported-platform artifacts
also remain unverified. This audit does not substitute legal advice.

| Gate | Result | Evidence / qualification |
|---|---|---|
| Dedicated branch, private history/research preserved | PASS | Branch above; no destructive git operation; private files excluded, not deleted |
| Scoped standard MIT terms, not a repository-wide grant | PASS (mechanical) | `LICENSE`, `LICENSE_SCOPE.md`, `PROPRIETARY-NOTICE.md`; named author is not proof of title |
| Copyright title/assignment and upstream applicability | **FAIL (blocking)** | Not verifiable from the repository: see §Ownership and named exception E1 |
| Canonical per-file source classification | PASS | `release-audit/license-boundary.json`; complete canonical comparison, no implicit approval of new files |
| Actual sdist public-only member/content gate | PASS | 52 approved source files + generated `PKG-INFO`; every original byte compared |
| Actual direct and sdist-rebuilt wheel gates | PASS | 10 members each; all mapped originals/licenses byte-compared; metadata, RECORD and native-module boundaries checked |
| Clean wheel/sdist builds and sdist wheel rebuild | PASS locally | CPython 3.13.15 direct; CPython 3.12.14 rebuild; Linux x86-64, GCC 13.3.0 |
| Installed final rebuilt wheel Python suite | PASS | **396 passed**, 2.61 s; `final-installed-wheel-tests.log` |
| Native scientific/regression suite | PASS | **120/120**, fresh Release build; `native-build.log`, `native-tests.log` |
| Actual runtime smoke | PASS | README example, J2 orbit, SRP/drag/albedo/gravity-gradient, native batch, 9 CSV exports; `final-runtime-smoke.log` |
| Leak-gate regression suite | PASS | **15 tests**; complete synthetic controls and malicious negative cases; `gate-regressions-final.log` |
| Public package provider override refusals | PASS | Each of CSPICE, NRLMSISE-00 and JB2008 rejected with `SKBUILD=ON`; `guard-*.log` |
| Linux linkage/embedded-resource inspection | PASS within stated limits | Both final binaries: only system runtime NEEDED entries; zero named private-provider markers; whole strings retained |
| macOS/Windows/repaired-manylinux final binaries | **FAIL (blocking, not run)** | This audit built Linux `linux_x86_64` wheels, not the CI platform matrix |
| Trusted Publishing workflow configuration | PASS (static assessment only) | Manual/default-branch/TestPyPI/OIDC-only publisher; native/wheel/full qualification prerequisites retained; not executed here |
| Actual private-repository plan compatibility | **FAIL (blocking, unverified)** | `/user` plan is null; an existing environment is not proof of current entitlement |
| Publication approval | **FAIL (blocking, not granted)** | No workflow was dispatched or publishing credentials used |

## Minimal MIT component and exact boundary

The authoritative reviewable scope is `LICENSE_SCOPE.md`; the exhaustive
machine-readable classification is `release-audit/license-boundary.json`.
The source distribution has **52 approved original files**. Its only additional
member is generated `PKG-INFO`. Public components are:

- `src/kernel/` (8 C++ sources), `src/integration/` (1), `src/analysis/` (3).
- `include/leo/kernel/` (12 headers), `include/leo/integration/` including
  `detail/` (6), `include/leo/analysis/` (3), and exactly
  `include/leo/environment/{ephemeris,interpolated,source}.hpp` (3).
- `bindings/` (6 files), `python/leo_dynamics/` (3 facade/type-marker files).
- `CMakeLists.txt`, `pyproject.toml`, `cmake/provision.cmake`.
- `LICENSE`, `LICENSE_SCOPE.md`, `THIRD_PARTY_NOTICES.md`, `README.dist.md`.

Everything else is outside the MIT grant. In particular, no tests/frozen
research fixtures were silently relicensed. Tests remain private, unchanged
numerically, and are run from the private checkout against an installed wheel.
The source distribution remains rebuildable without them. `BUILD_TESTING=ON`
in that distribution fails explicitly instead of referring to missing sources.

### File-by-file findings and exceptions

Every baseline tracked file is enumerated, with hashes/provenance and scope
proposals, in `audit-evidence/source-inventory.json` (1,951 baseline files).
Its proposal is explicitly **non-binding** and its packaging projections describe
the baseline, not observed final archives. The final branch classification is
`release-audit/license-boundary.json`; actual final member hashes override all
predicted inclusion columns. The following path table explains every group and
the principal exceptions; exact individual filenames are in those JSON files.

| Path | Finding / final treatment |
|---|---|
| `src/kernel/**`, `src/integration/**`, `src/analysis/**` | Approved numerical engine; MIT; no numerical source modified |
| `include/leo/{kernel,integration,analysis}/**` | Approved public headers; MIT; no scientific API altered |
| `include/leo/environment/{ephemeris,interpolated,source}.hpp` | Basic explicit/tabulated ephemeris interfaces; MIT, needed by core |
| `src/environment/spice.cpp`, `include/leo/environment/spice.hpp` | External CSPICE adapter, outside MIT/public build; requires separately acquired CSPICE |
| `src/atmosphere/**`, `include/leo/atmosphere/**` | All empirical atmosphere code/adapters/Fortran bridge private, proprietary, excluded |
| `cmake/{nrlmsise00,nrlmsise00-tests,jb2008}.cmake` | Private provider provisioning/tests; excluded; no absent-file unconditional includes on core path |
| `cmake/provision.cmake` | Approved Eigen/Boost SHA-pinned header provisioning; MIT |
| `bindings/**`, `python/leo_dynamics/**` | Approved interface; MIT; public Python payload hashes match checkout |
| `docs/atmosphere/**` | Private high-fidelity models, acquisition/provenance, experiments, optimisation/profiling/data; excluded in full, including Markdown and nested CMake files |
| `docs/multithreading/**`, `docs/numerical-audit/**` | Research programs, measurements, metrics, datasets and history preserved privately |
| `docs/stage3.md` | Contains explicit comparison to the ImperialSpaceSociety Julia project/API at cited commit; excluded, not described as copied source |
| Other `docs/**` | Internal guides link to research fixtures/campaigns; reports contain private results; conservative exclusion of entire tree |
| `benchmarks/**`, `scripts/**` | Research/optimisation experiments and datasets preserved; proprietary, excluded |
| `tests/**`, including `tests/reference/golden/tudat/**` | Research-derived fixture provenance not approved as public core data; all excluded, no fixtures regenerated or acceptance thresholds weakened |
| `examples/**`, `plotting/**` | Helpers not inferred into owner-approved engine/interface grant; excluded |
| `README.md` | Full internal overview includes private research context; excluded and not embedded in public metadata |
| `README.dist.md` | New minimal package overview; actual executed quickstart; public metadata source |
| `LICENSE`, `LICENSE_SCOPE.md` | Standard MIT text with prominent external scope pointer; scope enumerates licensed subject matter, no precedence clause that defeats exclusions |
| `THIRD_PARTY_NOTICES.md` | Scoped dependency notices; corrected MPL clause mapping/runtime discussion and precautionary upstream notice |
| `.github/**`, `ci/**`, `release-audit/**`, `audit-evidence/**`, audit records | Private release/audit tooling/evidence; excluded; leak gate itself not represented as public engine code |
| `.gitignore`, `.gitattributes` | Repository plumbing, excluded; approved public paths forced LF so byte equality is portable without weakening checks |
| Existing `outputs/` and build/venv trees | User/local material left intact; excluded; no destructive cleanup |

**E1 — fixed exponential atmosphere parameters (retained, disclosed).** The
basic engine contains 40 fixed altitude/density anchors. All 40 pairs match
upstream Julia `Constants.jl` after converting km to m; the public C++ header
already acknowledges the Julia knots. The upstream labels its table “1976 US
Standard Atmosphere”. This is concrete shared-parameter provenance, not proof
of copyright derivation, employment ownership or ultimate primary-standard
provenance. No independent primary-standard rights verification was completed.
The engine/table remain unchanged, consistent with the approved basic numerical
engine. The public README/notices explicitly disclose this table, distinguishing
it from excluded empirical providers/private campaign datasets. The earlier
blanket “no atmosphere reference data” statement was corrected. Legal review of
applicable rights remains blocking; no unsupported public-domain conclusion is
made.

**E2 — optional-provider names in the common CMake file.** The approved build
file retains conditional private provider options for full-repository native
workflows. Package builds (`SKBUILD`) now reject any enabling of CSPICE,
NRLMSISE-00, JB2008, private examples/tests/plotting, or unpinned host dependency
resolution. Non-package full-repository research builds remain available.
Missing optional files produce explicit failures in the stripped sdist.

## Ownership and provenance: evidence versus legal conclusions

The owner approved MIT for the engine/interface and states no Imperial College
or Siemens source code is used. That statement is recorded, not contradicted by
an inference from names or matching formulas. The copyright line uses the
**evidenced author name Shiki Vaahan**, not an invented legal entity.

- Baseline release-line history has 76 commits under that author identity.
  Across all refs, research counted **79 commits**: 77 under that human identity
  and 2 under `LEO benchmark snapshot <benchmarks@localhost>`. Author identity is
  evidence of authorship, **not legal title, employment status or assignment**.
- No prior LICENSE/COPYING grant was found in the baseline history. The owner
  has now approved scoped MIT intent; this does not retrospectively establish
  employer waivers or assignments. No such instrument was found in the repo.
- The GitHub profile self-declares `company: Imperial College London`. This is
  affiliation metadata only. **Imperial College Space Society**, the distinct
  upstream project's attributed holder, must not be conflated with the
  university or an employer.
- Local `/home/user/Project-Svarog-Orbital` is an upstream Julia checkout.
  At cited commit `58ab4d4964f9c20b7070efae48f8916e8a99784e`, its README says
  “MIT – see LICENSE”. The locally retained **`legacy/LICENSE`** contains the
  standard MIT text, Copyright (c) 2022 Imperial College Space Society. The
  current remote returns 404; a root LICENSE at the cited commit was not
  independently retrieved. This limitation is material.
- Source-like comparison coverage: research compared 229 files against Eigen,
  Boost, pybind11 and the cited Julia tree; the sole normalised-line match was
  three trivial pybind11 include directives. This does **not** prove absence of
  translation/derivation. Numeric/binary payloads and 31 bulk-evidence source-like
  files were outside that comparison. The 40 engine anchors are separately
  documented by E1, not hidden by the line-comparison result.
- Algorithm inspection covered SRP, drag coefficients, apsis detection and
  additional gravity-gradient/drag/albedo torque formulations. Several have
  different implementations/formulations; gravity-gradient torque shares the
  standard physical expression. Neither similarity nor difference establishes
  copyright title or the legal “substantial portions” test.
- Upstream history contains 13 commits authored under a Siemens email, 7
  reachable from `origin/main`; this does not prove employment or a corporate
  claim. The open question is recorded for qualified review, not asserted as
  infringement or as evidence the owner imported Siemens source.
- A precautionary MIT attribution is now reproduced for the society project.
  Its obligation's applicability remains unresolved, **not confirmed
  noncompliance**. Attribution alone cannot cure an unverified title chain.
- The earlier research claim that an NRL coefficient inventory JSON contained
  actual coefficient values was corrected: it describes shape/inventory, not
  the coefficient payload. Matching attribution-string searches likewise do
  not prove absence of third-party material.

Evidence: `audit-evidence/{research-report.md,provenance.json,snippet-scan.json,
proprietary-inventory.json,dependency-license-matrix.md}`. Research-only Orekit
(Apache-2.0), SatelliteToolboxAtmosphericModels.jl (MIT), Tudat (BSD-3-Clause),
SciPy and other oracle references remain outside the final component.

## Dependency and licence matrix

Exact resolved values below describe **this clean Linux build/test environment**,
not every interpreter/platform allowed by lower bounds. Full package metadata,
conditional extras and transitive requirements are retained in
`resolved-{build,driver,test}-licenses.log`; optional uninstalled extras are not
present dependencies.

| Dependency | Pin / actual local resolution | Licence / disposition |
|---|---|---|
| Eigen | pinned 3.4.0 | MPL-2.0 plus retained embedded BSD notices; templates compiled; source URL/hash and full notices shipped |
| Boost | pinned 1.83.0 | BSL-1.0; Odeint/header code compiled; full text shipped |
| pybind11 | pinned/resolved 3.1.0 | BSD-3-Clause; header code compiled; notice shipped |
| NumPy | declared `>=2.0`; installed **2.5.4** | BSD-3-Clause AND 0BSD AND MIT AND Zlib AND CC0-1.0; separately installed, not bundled |
| scikit-build-core | pinned/resolved **0.11.6** | Apache-2.0; build-only |
| CMake PyPI package | `>=3.28`; resolved **4.4.4** | Package metadata Apache-2.0/BSD; upstream CMake BSD, not simply Apache; build-only |
| Ninja PyPI package | `>=1.11`; resolved **1.13.2** | Apache-2.0/BSD packaging metadata; build-only |
| packaging | **26.3** | Apache-2.0 OR BSD-2-Clause; build driver/backend/tests |
| pathspec | **1.1.1** | MPL-2.0; build-only; using tool does not make generated engine MPL |
| build | **1.6.1** | MIT; build driver |
| pyproject_hooks | **1.3.3** | MIT; build driver |
| pip | **26.2.2** | MIT; installer/build driver |
| pyproject-metadata | no standalone distribution in resolved closure | Do not invent a separately resolved version; backend metadata implementation is part of its installed package |
| pytest / pluggy / iniconfig / Pygments | **9.1.1 / 1.7.0 / 2.3.1 / 2.21.0** | MIT / MIT / MIT / BSD-2-Clause; test-only |
| Catch2 | **3.7.1**, existing `/tmp/catch-install` | BSL-1.0; private tests only; exact installed version read from `release-audit/native-Catch2ConfigVersion.cmake` (`PACKAGE_VERSION "3.7.1"`), not inferred from a CI pin |
| libstdc++ / libgcc_s | host GNU runtime, observed dynamic NEEDED | GPLv3 with GCC Runtime Library Exception **3.1**; not an LGPL replacement-runtime rule |
| libc / libm / ELF loader | observed host runtime links | Not bundled in either inspected wheel |
| libc++ / MSVC runtime | static platform assessment only | macOS/Windows binary bundling and applicable redistribution set must be inspected before release |
| Society Julia upstream | cited commit and local legacy MIT notice | Precautionary attribution; applicability/title/E1 unresolved |
| cibuildwheel | configured **4.3.1** | BSD-2-Clause; not executed here |
| auditwheel / delvewheel / delocate | unpinned CI repair tooling | MIT / MIT / BSD; not locally resolved or executed in this audit |

MPL-2.0 notices now distinguish §3.1 source form, §3.2 executable/source
availability, §3.3 larger works under chosen terms and §3.4 notice preservation.
MPL does not prohibit selling or sublicensing the executable/larger work under
other terms; recipients' covered-source rights remain intact. GCC RLE is v3.1,
not v3.3, with no invented replacement-runtime/notice §§4/5 requirements.

### Consumed dependency provenance, not just pristine download hashes

Pinned upstream archive hashes were independently verified by research. For the
**final sdist rebuild**, this audit also preserved CMakeCache, compile commands
and Ninja's actual dependency graph. **170/170 consumed Eigen headers** and
**1,066/1,066 consumed Boost headers** matched their corresponding pinned archive
bytes, with zero missing/mismatching files. See
`consumed-header-verification.json`, `consumed-compile_commands.json`,
`consumed-CMakeCache.txt`, `consumed-ninja-deps.log`. This closes the recorded
Linux consumed-tree question, not every future/other-platform build. Isolated
pybind11 build-environment paths disappear after build; its version pin and
configure output are retained, not falsely represented as a retained consumed
header-byte comparison.

## Actual artifacts, inventories and binary inspection

Final private local artifacts are retained under `release-audit/artifacts/`.
No artifact was uploaded. Exact identities:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| direct CPython 3.13 wheel | 368403 | `87df8e2428f43118694f431e3596a078f136648a4c14f1ced20480123cee8f2f` |
| source distribution | 83279 | `0906aade8fa832c1f9ad7c05f14ae62c47984787dc3a26c997a61fbb70fa5767` |
| wheel rebuilt from sdist, CPython 3.12 | 368448 | `e4e874217f2cde42bcf5fbd73d11ebed24cef9c1534bea33314b0c094803c947` |

The final archive set embeds the **current** tree bytes: the shipped
`THIRD_PARTY_NOTICES.md` is SHA-256
`d622f8acdf537bbba7391a429d2016fb6705426237a0d8e6ad2a86da8c6a0c8c` (43,737
bytes) in both the source distribution and the rebuilt wheel, identical to the
working-tree file after the last MPL-3.1/3.2 wording correction; the same
equality holds for `README.dist.md`, `LICENSE` and `LICENSE_SCOPE.md`. No file
that ships is newer than these artifacts. The earlier `5634a26d…` value is the
pre-correction review snapshot quoted by the independent reviewer, not the
shipped bytes. All evidence in this report is from this final tree.

`archive-identities.json` contains exact filenames and hashes;
`final-archive-inventory.json` enumerates **every member**, size and SHA-256.
Each wheel has the three Python facade/typing files, one `_core` extension,
METADATA/WHEEL/RECORD, and exactly LICENSE/LICENSE_SCOPE/THIRD_PARTY_NOTICES.
Only the final **CPython 3.12 rebuilt wheel** was installed and executed for the
396-test suite/runtime smoke. The CPython 3.13 wheel was built and inspected,
not represented as runtime-tested.

The sdist has exactly the 52 originals above plus generated PKG-INFO.

Both final Linux modules link `libstdc++.so.6`, `libgcc_s.so.1`, `libm.so.6`,
`libc.so.6` and the ELF loader, with no bundled additional native library.
Dynamic tables/linkage are in `final-{direct,rebuilt}-{needed,linkage}.log`.
Whole binary string inventories are retained in
`final-{direct,rebuilt}-binary-strings.txt`. Named private-provider markers
NRLMSISE/JB2008/Brodowski/CSPICE-provider/leo_atmosphere were absent; the approved
ExponentialAtmosphere names and fixed parameters are expected. Archive-member
inspection found no extra kernels/weather/research resources. String absence
alone is not proof of binary provenance: it is combined with clean builds,
SKBUILD refusals, exact public-source archive checks and consumed-tree evidence.

## Fail-closed mechanics and regression coverage

`ci/verify_license_boundary.py` compares the **entire** canonical manifest,
including top-level allowlists/markers, and uses regenerated canonical data for
archive checking. It rejects unknown members, wrong roots, unsafe names,
nonregular/link members, duplicates and missing required payload/licenses.
It scans whole members, never truncating a tail. Public source/facade/licence
bytes must match approved checkout bytes; marker-free renamed research files
therefore fail. Generated metadata headers/body must match public pyproject and
README values; WHEEL fields/tags are bounded; RECORD enumerates and hashes all
wheel members. Only a narrowly named native module is allowed (no arbitrary
shared libraries or stable-ABI claim).

The automated 15-test suite exercises valid complete synthetic controls and
proprietary paths/content, renamed real research JSON, arbitrary licence
payloads, complete-manifest tampering, ZIP links/traversal directories, tar
links/duplicates/root files/multiple roots, missing package/licences, wrong
metadata and private WHEEL/Summary/X-header channels. Synthetic native bytes
are never executed and are not a claim of binary qualification.
Independent read-only review in `audit-evidence/boundary-independent-review.md`
and its JSON companion records **2 complete controls accepted, all 19 negative
archive scenarios rejected, and both canonical-manifest tamper cases rejected**.
Initial false acceptances are documented as fixed, not current vulnerabilities.
Review snapshot hashes must be read alongside the final worker gate log and
inventories after the last MPL-notice correction. Structural byte enforcement
does not establish legal rights, semantic approval or arbitrary binary purity.

**Actual packaging bug found and corrected:** scikit-build-core walks working
tree files (including untracked files) and its include rules override excludes.
Unanchored `LICENSE`/`CMakeLists.txt` patterns admitted nested private CMake files
and venv licence files. Anchoring root includes removed them. Excludes alone
are not the security boundary; actual archive byte/member gates are mandatory.

`ci/release_preflight.py` invokes the source boundary gate. Wheel/sdist workflow
steps run archive checks **before each artifact upload**, including failure-path
uploads; uploads require the gate's success. The release collection job repeats
artifact validation before collection/upload. Full existing native, wheel,
installed-wheel, sanitizer and six-hour qualification prerequisites remain.
Numerical tests/acceptance tolerances were not weakened. CI wiring was edited
locally only; no workflow execution is claimed.

## Trusted Publishing and private repository support

Read-only authenticated GET evidence is in `audit-evidence/provenance.json` and
`research-report.md`. Repo is private; default branch main, no observed branch
protection. One admin collaborator was observed. The release workflow is manual
only, refuses nondefault refs, publishes only to TestPyPI in environment
`testpypi`, and grants `id-token: write` only to the publishing job. The pinned
PyPA action uses OIDC, not a long-lived token. No production-PyPI upload job is
configured and no release workflow run was observed.

The environment exists with `protection_rules: []` and no deployment branch
policy. `/user` returned no readable plan. Environment existence can survive a
downgrade, so **current private-repository plan compatibility is UNVERIFIED**.
GitHub docs distinguish environment availability from required reviewers;
required reviewers on private repositories are not a Free/Pro/Team feature.
Do not propose that as an available protection without plan evidence, and one
collaborator would not provide independent review anyway. PyPI/TestPyPI publisher
registration/authorisation was not proven by these repo GETs. Private repository
URLs in public package metadata will be inaccessible to outsiders; any public
documentation/repository migration is a separate owner decision, not permission
to expose the original research history.

## Reproducibility, changes and limitations

All audit work is committed locally on `audit/release-licensing-v0.1.0`; the
branch's audit commits run from `2bb5ccb` to its latest commit. Nothing was
pushed, uploaded, published or dispatched; `origin/main` still points at
`23df023d`, and the pre-existing untracked `outputs/` is untouched.

Full final commands, working directories, exit codes and persistent log names
are in `release-audit/commands.json`. This includes final direct/sdist builds,
clean installed tests, runtime smoke, dependency closure, provider refusals and
binary inspection. Earlier exploratory failures are not hidden: system Python
3.12 lacked development headers, initial uv venvs lacked pip, an initial
quickstart draft used the wrong API (corrected and executed), and the initial
unanchored sdist leaked nested files. Those artifacts are superseded by the
hashed final set, not described as passing.

Modified/new implementation paths: `.gitattributes`,
`.github/workflows/{wheels,release}.yml`, `CMakeLists.txt`, `pyproject.toml`,
`ci/release_preflight.py`, new `ci/verify_license_boundary.py`, new
`tests/license_boundary_tests.py`, `LICENSE`, `LICENSE_SCOPE.md`,
`PROPRIETARY-NOTICE.md`, `README.dist.md`, `README.md`,
`THIRD_PARTY_NOTICES.md`, `docs/python-packaging.md`,
`docs/release-readiness-report.md`, `docs/repository-hygiene-report.md`,
`docs/v0.1-release-readiness.md`, this report, `audit-progress.md`, the
researcher-written `audit-evidence/` tree including
`audit-evidence/docs-licensing-sweep.md` and
`audit-evidence/docs-licensing-sweep.json`, and the private
`release-audit/` manifest/generator/evidence.
The parallel researcher exclusively wrote `audit-evidence/`; its contents are
research evidence, not licence grants. Exact changed paths are additionally
recorded in `release-audit/changed-paths.txt` (81 paths: the implementation,
licence, packaging, workflow, documentation, research-evidence and audit-evidence
changes; it excludes the pre-existing untracked `outputs/` and the local
`dist*`/`build*` trees, and no evidence log is inside either public artefact).

The canonical generator is permanent boundary-maintenance tooling: after a
reviewed source addition, explicitly classify it, update packaging/scope if
approved, regenerate the manifest and rerun source/archive gates. Do not widen
rules to silence a failure. No temporary scaffolding was added to numerical
code. Evidence artifacts remain private and excluded from public distributions.

### Documentation status banners

A parallel read-only documentation sweep (2026-10-26, evidence in
`audit-evidence/docs-licensing-sweep.md` and `audit-evidence/docs-licensing-sweep.json`)
marked the internal documents whose licensing statements predated the scoped
grant. Five documents are affected: `README.md`,
`docs/release-readiness-report.md`, `docs/python-packaging.md`,
`docs/repository-hygiene-report.md` and `docs/v0.1-release-readiness.md`. The
annotation style is additive only: a dated status banner immediately after each
document's title, and dated inline `*[superseded 2026-10-26: …]*` notes placed
directly after the stale statement they qualify. The sweep found **32 stale
statements: 13 annotated, 19 banner-covered**. A line-level diff of the four
modified tracked documents shows insertions only and **zero removed lines**, so
no measurement, baseline date, hash, count, gate, blocker, severity or test
tolerance was deleted, softened or rewritten. The sweep's own record states
that no annotation claims a release, legal clearance or verified ownership.
During consolidation two annotations that could have been read as granting
redistribution outright were tightened in the same additive style, so that they
describe the owner's approval subject to the still-open copyright-title
verification; no count, gate or historical statement changed as a result.

### Non-actions and preserved material

Stated explicitly, because the audit's value depends on them being checkable:

- **Nothing was pushed.** `origin/main` still reads
  `23df023d6080c9aeb27e9ceddba1671651205584`; the audit work exists only as
  local commits on `audit/release-licensing-v0.1.0`.
- **No workflow was run or dispatched**, and no publishing credential was used.
  The GitHub access during this audit was read-only GET.
- **No index upload** of any wheel or source distribution occurred, and **no
  repository was created or made public**. The repository remains private.
- **No proprietary file was deleted, moved or modified.** The private tree still
  contains every research and historical artefact: the regenerated canonical
  manifest classifies **1,965 tracked files as proprietary**,
  plus 2 repository-plumbing files, out of
  2,019 classified tracked files, of which 52 are the
  MIT core that may be distributed. The research campaigns, empirical
  atmosphere providers and adapters, optimisation experiments and datasets,
  frozen reference fixtures, qualification reports and release tooling are all
  still present and still classified as non-distributable.
- **The proprietary notice itself is not distributed.** `PROPRIETARY-NOTICE.md`
  is absent from both final artifacts (sdist 53 members, wheel 10 members;
  classification `proprietary`, `in_sdist: false`), so its dated title
  clarification required no rebuild; the archive and source gates were re-run
  and pass.
- **No numerical source, test, fixture or acceptance threshold was changed.**
  The native suite and the Python suite pass unchanged against the final
  artefacts.

### How to re-verify this audit

Run from the private checkout. These are the exact commands behind the results
above; each writes or refreshes a log under `release-audit/`.

```sh
# 1. Source classification and the mechanical release preflight.
python3 release-audit/generate_boundary.py --check
python3 ci/verify_license_boundary.py --project-root .
python3 ci/release_preflight.py --project-root . --expected-version 0.1.0

# 2. Leak-gate regressions (valid controls must pass, attacks must fail).
python3 tests/license_boundary_tests.py

# 3. Clean build of the final tree (isolated environment, pinned backend).
python3 -m build --sdist --wheel --outdir release-audit/artifacts/direct .

# 4. Rebuild a wheel from that source distribution.
mkdir -p /tmp/leo-sdist-source && tar xzf release-audit/artifacts/direct/leo_dynamics-0.1.0.tar.gz -C /tmp/leo-sdist-source
cd /tmp/leo-sdist-source/leo_dynamics-0.1.0 && python3 -m build --wheel --outdir <repo>/release-audit/artifacts/rebuilt .

# 5. Archive boundary gate over both artifacts.
python3 ci/verify_license_boundary.py --project-root . \
    --artifacts release-audit/artifacts \
    --json-report release-audit/final-archive-inventory.json

# 6. Installed-wheel suite and runtime smoke against the rebuilt wheel.
python3 -m venv /tmp/leo-check && /tmp/leo-check/bin/python -m pip install <rebuilt wheel> pytest
cd /tmp && /tmp/leo-check/bin/python <repo>/ci/run_installed_wheel_tests.py <repo>

# 7. Native suite (needs Catch2 3 and pinned dependency provisioning).
cmake -S . -B /tmp/leo-native -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
      -DLEO_PROVISION_DEPENDENCIES=ON -DCMAKE_PREFIX_PATH=<catch2 prefix>
cmake --build /tmp/leo-native --parallel && ctest --test-dir /tmp/leo-native --output-on-failure

# 8. Binary linkage and embedded-string inspection of a built extension.
readelf -d <_core*.so> ; ldd <_core*.so> ; strings -a <_core*.so> > strings.txt
```

Steps 1–6 are the licence-boundary evidence; step 7 is the unchanged scientific
gate; step 8 supports, but never replaces, the source and archive gates.

This is not a six-hour campaign rerun or a new accuracy qualification; that
scientific gate remains required in the release workflow. No macOS/Windows
binary, repaired manylinux artifact, production publisher or current private
account entitlement was exercised. Legal title/assignment, the shared table's
ultimate provenance and upstream substantial-portions applicability remain open.
Only the owner, with appropriate rights review and explicit publication approval,
can turn this **NO-GO** into a release decision.
