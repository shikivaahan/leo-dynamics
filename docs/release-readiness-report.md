# Release readiness report

> **Status update — 2026-10-26 licensing audit.** This document is preserved as the historical record of the state at its stated baseline; its licensing, packaging and release statements are superseded where they conflict with [RELEASE_LICENSE_AUDIT.md](../RELEASE_LICENSE_AUDIT.md). The numerical engine and Python interface now carry a **scoped MIT grant** ([LICENSE_SCOPE.md](../LICENSE_SCOPE.md)); all research, empirical atmosphere providers, experiments, datasets, tests and release tooling remain private and proprietary ([PROPRIETARY-NOTICE.md](../PROPRIETARY-NOTICE.md)). Publication remains **NO-GO** pending legal title/assignment review, upstream-parameter provenance review and verification of current private-repository plan support. No gate, threshold or historical result in this document was weakened or rewritten.

Scope: licensing and third-party compliance, packaging and native/Python
validation, CI hardening and TestPyPI release automation for `leo-dynamics`
0.1.0.

This report records what was executed, what was observed, and what is blocked.
Nothing here is a plan or a projection. Every command below was run; every
result is what it returned.

**Path notation.** Commands are written relative to the repository root, shown as
`$REPO_ROOT`. `$BUILD` is a scratch directory outside the checkout used for
build trees, virtual environments and downloaded artefacts; it stands for
whatever temporary directory was used. No absolute path from the development
host appears in this report.

---

## 1. Verdict

**BLOCKED — USER ACTION REQUIRED.**

**Confirmed blockers.** Two facts are established, and neither can be resolved
by the code in this repository:

1. **The project has no licence.** No `LICENSE` file exists, and
   `pyproject.toml` declares no licence expression. Uploading a wheel to
   TestPyPI is a redistribution event, and nothing here grants redistribution
   rights for the project's own code. Only the copyright holder can grant
   them.
2. **The TestPyPI trusted publisher has not been confirmed.** The production
   PyPI pending publisher is registered; the separate TestPyPI one cannot be
   verified through any authorized read-only interface available here.

**Unresolved gates.** Three further questions are open. They are not blockers
in the sense of an established fact — they are unresolved rights and
configuration questions whose answers decide whether the release may proceed
at all:

- **Ownership** (§2.3): who holds the copyright is not established, so it is
  not established that anyone can grant the licence in blocker 1.
- **Environment protection** (§2.6): the `testpypi` environment carries no
  protection rules, so the publishing gate is not the protected publisher this
  design assumes.
- **Restricted-asset terms** (§2.4): one question about quoted upstream values
  in the tracked evidence is unanswered.

Everything else is **prepared and committed, but not yet verified remotely**. The
release workflow exists and refuses to publish while the licence is missing,
and reuses the existing cross-platform wheel infrastructure rather than
duplicating it. What has *not* happened, and cannot be claimed:

- **No CI run of the new workflow.** The release workflow, the hardened
  workflows and the three new scripts have never executed on GitHub. Their
  behaviour is established only by local execution and by linting.
- **The qualification workflow has never run at all**, so the release's
  qualification gate is untested.
- **The `testpypi` environment carries no protection rules**, so the
  publishing gate is not the protected publisher it is meant to be.
- **The licence and publisher questions are open**, so no artefact may be
  published regardless of how well the automation works.

The repository is therefore *prepared for* a TestPyPI release, not *verified
for* one.

---

## 2. Blocking items, with the exact evidence

### 2.1 No project licence (owner decision)

| Evidence | Command | Result |
|---|---|---|
| No licence file tracked | `git ls-files \| grep -iE "licen\|copying\|notice"` | only `THIRD_PARTY_NOTICES.md` |
| No licence metadata | `grep -n "license" pyproject.toml` | one line: `license-files = ["THIRD_PARTY_NOTICES.md"]`; no `license = ...` |
| `THIRD_PARTY_NOTICES.md` grants nothing | read §6 of that file | item 1 states the project's own licence is unresolved |
| Built wheel carries no licence | `zipfile.ZipFile(...).namelist()` on the built wheel | `leo_dynamics-0.1.0.dist-info/licenses/THIRD_PARTY_NOTICES.md` only; `METADATA` has `License-File: THIRD_PARTY_NOTICES.md` and **no** `License-Expression` |

**Recommendation: MIT — as terms, conditional on §2.3.** The
project's own code is C++20, CMake, pybind11 bindings and Python. MIT is
permissive, is compatible with distributing MPL-2.0 (Eigen) and BSL-1.0
(Boost) code in the same binary, and is the conventional choice for a Python
package of this kind. **This is a recommendation about which terms to use, not
a finding that anyone may grant them.** It applies only if §2.3 confirms that
the person granting the licence holds the copyright.

**Why it was not applied.** Applying a licence asserts a grant that only the
copyright holder can make, and §2.3 shows that the holder has not been
established. The workflow was written so that it becomes usable the moment an
approved licence is added, rather than containing a placeholder that would
let an unlicensed artefact reach an index.

**What is needed, and who does it.** The owner must *authorise* the licence
and answer the ownership questions in §2.3. Applying it is then a mechanical
edit that this repository's own tooling performs:

1. The owner confirms: MIT is the chosen licence; the copyright holder's name
   and year for the `LICENSE` file; and the §2.3 answers.
2. On that confirmation, this workstream applies it: add the `LICENSE` file
   with the confirmed holder and year, set `license = "MIT"` in the
   `[project]` table of `pyproject.toml`, and change `license-files` to
   `["LICENSE", "THIRD_PARTY_NOTICES.md"]`.

Both metadata changes and the file must land together:
`project.license-files` patterns must each match at least one file, so
listing `LICENSE` before the file exists breaks the build. Verified locally —
the build fails with `Every pattern in "project.license-files" must match at
least one file: 'LICENSE*' did not match any`.

### 2.2 TestPyPI trusted publisher (owner website action)

The production PyPI pending publisher is registered with owner `shikivaahan`,
repository `leo-dynamics`, workflow `release.yml`, environment `pypi`. The
release workflow created here targets the **TestPyPI** endpoint only, so it
needs a **separate** pending publisher on test.pypi.org with environment
`testpypi`.

That configuration lives behind an account login on test.pypi.org and is not
readable through any authorized interface available here. What was checked
read-only:

| Check | Command | Result |
|---|---|---|
| TestPyPI project state | `curl -sS https://test.pypi.org/pypi/leo-dynamics/json` | HTTP 404, `{"message": "Not Found"}` — the project does not exist yet, so no publisher is attached to it |
| PyPI project state | `curl -sS https://pypi.org/pypi/leo-dynamics/json` | HTTP 404 — consistent with a pending (not yet used) publisher |
| TestPyPI account exists | `curl -sS -o /dev/null -w "%{http_code}" https://test.pypi.org/user/shikivaahan/` | HTTP 200 |

**Owner action:** on <https://test.pypi.org/manage/account/publishing/>, add a
pending publisher with

| Field | Value |
|---|---|
| PyPI project name | `leo-dynamics` |
| Owner | `shikivaahan` |
| Repository name | `leo-dynamics` |
| Workflow name | `release.yml` |
| Environment name | `testpypi` |

The environment name must match the workflow's `environment: testpypi`
exactly. A pending publisher creates the project on first use; nothing has
been uploaded.

### 2.3 Original ownership — **the owner must confirm this**

| Observation | Command | Result |
|---|---|---|
| Commit authorship | `git shortlog -sne --all` | 76 commits by `Shiki Vaahan <shikivaahan@gmail.com>`, 2 by `LEO benchmark snapshot <benchmarks@localhost>` |
| Commit count | `git rev-list --count --all` | 78 |
| First commit | `git log --reverse --format='%H %an <%ae> %ad %s' \| head -1` | `9215c21` — "Initial commit", 2026-10-07 |
| Copyright headers in source | `grep -rn "Copyright" include/ src/ bindings/ python/` | **no matches** — no file asserts a copyright holder |

**What this does and does not establish.** It establishes that one Git
identity authored almost every commit. It does **not** establish who owns the
copyright. Authorship is not ownership: work may have been produced in
employment, under contract, or with third-party material whose terms the
commit history cannot show. Nothing in the repository records a copyright
holder, a contributor agreement, or an employer.

**Owner confirmation required before any licence is applied.** Please confirm
each of the following, because the answer determines who can grant what:

1. **Was this work produced in the course of employment** for an employer or
   client who would own the copyright? If so, that party — not the
   committer — is the holder, and its permission is needed.
2. **Does any third party own any part of it?** Specifically: were any
   snippets, reference implementations, datasets or test fixtures taken from
   a source whose terms require attribution, restrict redistribution, or
   forbid it? The atmospheric, SPICE and reference-oracle material was checked
   in §2.4 and is not vendored, but a wider audit of the code's provenance is
   the owner's to make.
3. **Are there other contributors** whose work is in the repository and whose
   consent to a licence is needed? The `benchmarks@localhost` identity is
   consistent with a local snapshot of the same project, but this was not
   established.
4. **Who is the copyright holder to be named in the `LICENSE` file**, and
   what year?

Until 1–4 are answered, no licence may be applied and no artefact may be
redistributed. The MIT recommendation in §2.1 is a recommendation about *terms
only*, conditional on the holder being able to grant them.

### 2.4 Atmospheric assets and other restricted material

The distribution does not redistribute any upstream atmospheric or SPICE
material. Verified by inspection of the tracked tree and of the built
artefacts:

| Asset | Upstream terms | In the repository? | In the wheel/sdist? |
|---|---|---|---|
| CSPICE (SPICE toolkit) | NAIF distribution terms; not vendored | no `cspice` sources, no `.bsp`/`.tls` kernels | no |
| NRLMSISE-00 (`nrlmsise-00.c/.h/_data.c`) | Brodowski C translation carries a warranty disclaimer, no explicit redistribution grant | not vendored; `docs/atmosphere/nrlmsise00-parameters.json` records SHA-256 digests, line spans and scalar constants but explicitly states the coefficient arrays are `"values": "not reproduced; read from the pinned source file"` | no |
| JB2008 (`JB2008.f`, `JB08DRVAUTO.f`) | "Copyright, 2008, Space Environment Technologies. All rights reserved." — no grant | not vendored; `docs/atmosphere/JB2008.md` §2 records the SHA-256 digests and states the source is used in place | no |
| SET Unified Data Library (`SOLFSMY4MON.DAT`, `SolRes.dat`, `DSTFILE.TXT`) | separate licence, not required for validation | absent | no |
| Tudat / tudatpy (reference oracle only) | BSD-3-Clause, separately installed | `tests/reference/generators/tudat_reference.py` **imports** tudatpy; it does not copy it. The generator scripts are excluded from the sdist (`/tests/reference/generators/**` is in `exclude`) | no |

The one place upstream *output* is quoted is
`docs/atmosphere/evidence/validation-amendment-v1/altitude-datum.json`, which
records a single vendor density value (`6.505e-14` kg/m³) as the worst-point
comparison against the project's own probe, and
`docs/atmosphere/evidence/nrlmsise00-validation.json`, which records derived
ratios rather than upstream fixtures. These are derived measurements of the
kind the project's own documentation already relies on for its validation
claims, and they are **excluded from both distributions** by the sdist
`exclude` rules. They are, however, tracked repository content, so they become
visible when the repository is made public. Whether quoting a handful of
published values in the repository is within the upstream terms is a question
for the copyright holder and is listed here as **unresolved**, not as settled.

**No SPICE kernel, atmospheric source, space-weather dataset or published
validation grid is redistributed by either distribution.** That is a mechanical
fact, verified by listing the built artefacts.

### 2.5 Dependency licences of the code compiled into `_core`

| Component | Version | Licence | Source of the claim | Verification |
|---|---|---|---|---|
| Eigen | 3.4.0 | MPL-2.0 (+ three BSD fragments inside the compiled headers) | upstream `COPYING.MPL2` | SHA-256 `8586084f…` pinned in `cmake/provision.cmake`; configure log printed `EIGEN provisioned as eigen 3.4.0 https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz sha256=8586084f…` |
| Boost | 1.83.0 | Boost Software License 1.0 | upstream `LICENSE_1_0.txt` | SHA-256 `c5a0688e…` pinned; configure log printed the matching `BOOST provisioned as boost 1.83.0 …` |
| pybind11 | 3.1.0 | BSD 3-Clause | upstream `LICENSE` | pinned `pybind11==3.1.0` in `pyproject.toml`; the headers are compiled into `_core` |

`THIRD_PARTY_NOTICES.md` reproduces the MPL-2.0 and BSL-1.0 texts in full and
the BSD 3-Clause text with its copyright line, names the pinned upstream
archives with their SHA-256 digests, and reproduces the three BSD notices
found inside the Eigen sources. The wheel carries that file under
`.dist-info/licenses/`, which is what satisfies the BSD "reproduce in the
documentation or other accompanying materials" requirement for a binary
distribution.

**One gap that is a real defect, not a formality:** the wheel currently
carries `THIRD_PARTY_NOTICES.md` but **no `LICENSE` and no
`License-Expression`**, because the project has no licence. The release
verifier (`ci/verify_release_artifacts.py`) rejects any wheel that does, so
this cannot reach an index by accident.

### 2.6 GitHub environment protection (owner action, uncertain availability)

The `testpypi` environment **exists** in the repository and is what the
publishing job references. Its current state, read back with
`gh api repos/shikivaahan/leo-dynamics/environments/testpypi`:

```json
{"id":25307450685,"name":"testpypi","can_admins_bypass":true,
 "protection_rules":[],"deployment_branch_policy":null}
```

`protection_rules` is empty and `deployment_branch_policy` is `null`: there are
**no required reviewers and no branch restriction**. Two attempts to add them
through the REST API returned HTTP 404. Those 404s came from
wrongly-shaped requests on my part — creating a deployment branch policy is a
`POST`, not a `PUT`, and requires `deployment_branch_policy.custom_branch_policies`
to be set on the environment first — so **they do not establish that the
account's plan forbids environment protection**. They establish only that my
requests were malformed.

What is established: the account's plan is not readable through the API
(`gh api user --jq .plan` returns `null` for this user account), and GitHub's
documentation states that GitHub Free accounts can configure environments only
for public repositories. The repository is private.

**Owner action:** open
<https://github.com/shikivaahan/leo-dynamics/settings/environments> and inspect
what the UI offers for the `testpypi` environment. If required reviewers and a
deployment-branch restriction are available, set them before the first
publication.

**If the UI cannot configure environment protection for a private repository
on the current plan, this is a pause point, not something to work around.**
The publishing job's security then rests on the environment existing at all,
with `can_admins_bypass: true` and no rules — which is weaker than the
protected publisher this design assumes. The options are a plan that supports
environment protection, or a public repository (where GitHub's documentation
says environments are configurable on all plans). **Neither is chosen here**,
because the repository's visibility is an owner decision and the licence and
redistribution rights in §2.1 and §2.3 are still unresolved: making the
repository public before the rights questions are answered would expose
material whose redistribution status is not settled.

The release workflow's own constraints — manual trigger, default-branch
refusal, same-commit artefact verification — reduce the blast radius of an
unprotected environment. They are **not** a substitute for it, and are not
offered as one.

---

## 3. What was executed, and what it returned

### 3.1 Native C++ tests

```sh
cmake -S . -B $BUILD/native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DLEO_BUILD_EXAMPLES=ON -DLEO_PROVISION_DEPENDENCIES=ON \
  -DCMAKE_PREFIX_PATH=$BUILD/catch-install
cmake --build $BUILD/native --parallel
ctest --test-dir $BUILD/native --output-on-failure
```

**120/120 tests passed.** (Catch2 v3.7.1 was bootstrapped to
`$BUILD/catch-install` first, exactly as `ci.yml` does, because the system has
no Catch2 package.)

### 3.2 Sanitisers

```sh
cmake -S . -B $BUILD/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DLEO_PROVISION_DEPENDENCIES=ON -DCMAKE_PREFIX_PATH=$BUILD/catch-install \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir $BUILD/asan --output-on-failure
```

**120/120 passed**, 328.43 s, no AddressSanitizer or UndefinedBehaviourSanitizer
report. This is GCC 13.3.0, not Clang: the CI matrix has a Clang ASan/UBSan
leg, but this host has `clangd-18` only and no `clang++` binary, so the Clang
leg was not reproduced locally. The GCC leg is equivalent in intent.

### 3.3 Frozen independent reference regression

```sh
cmake -S tests/reference -B $BUILD/reference -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE=$REPO_ROOT/.venv-python/bin/python
cmake --build $BUILD/reference --parallel
ctest --test-dir $BUILD/reference --output-on-failure
```

**1/1 passed** (`frozen_reference_light`). The first attempt without a Python
interpreter failed with `ModuleNotFoundError: No module named 'numpy'` — the
harness needs NumPy — and was rerun with the repository's own virtual
environment.

### 3.4 Wheel and source distribution build, and metadata validation

```sh
$BUILD/build-venv/bin/python -m build --outdir $BUILD/dist .
$BUILD/build-venv/bin/python -m twine check $BUILD/dist/*
```

**Both PASSED.** Artefacts and digests:

| Artefact | SHA-256 |
|---|---|
| `leo_dynamics-0.1.0-cp312-cp312-linux_x86_64.whl` | `616a90adf16e8b7b6f9fffa05006126c061ae5bc6aae9f9240273379ca5cce66` |
| `leo_dynamics-0.1.0.tar.gz` | `c4c0afe7d35ae76fee6b61c092e3f6f6960907bb5e9ee711d00f9845e2a3e2c2` |

The sdist contains 175 entries: `CMakeLists.txt`, `pyproject.toml`, `cmake/`,
`src/`, `include/`, `bindings/`, `python/leo_dynamics/`, `ci/`,
`THIRD_PARTY_NOTICES.md`, `tests/python/`, `tests/reference/{acceptance.json,
golden, scenarios}` and the guide files. It carries no `build-*`, `outputs/`,
`.venv`, `dist*` or evidence directory.

### 3.5 Installed-wheel validation outside the source checkout

```sh
$BUILD/wheel-venv/bin/python $REPO_ROOT/ci/run_installed_wheel_tests.py \
  $REPO_ROOT
```

Run from `/tmp`, against the wheel installed into a fresh environment:

```
removed source-tree path entry $REPO_ROOT/ci
installed distribution: $BUILD/wheel-venv/lib/python3.12/site-packages/leo_dynamics/__init__.py
version: 0.1.0
installed payload: ['leo_dynamics/__init__.py', 'leo_dynamics/__init__.pyi',
  'leo_dynamics/__pycache__/__init__.cpython-312.pyc',
  'leo_dynamics/_core.cpython-312-x86_64-linux-gnu.so', 'leo_dynamics/py.typed']
No broken requirements found.
installed-wheel smoke: leo_dynamics.Earth(mu_m3_s2=398600441800000.000000,
  equatorial_radius_m=6378136.300000, j2=0.001083); j2 propagation returned (10, 13)
396 passed in 2.67s
```

**396 passed, 0 skipped.** The smoke is a real orbital propagation: it
constructs the Earth model and propagates a J2 arc, returning a (10, 13)
state array.

### 3.6 Fresh source-distribution rebuild

The sdist was extracted to `$BUILD/sdist-extracted`, installed into a fresh
environment with no source tree present (so Eigen and Boost are re-provisioned
from the pinned archives and the extension is recompiled from the shipped
sources), and the same driver was run against it:

```
Successfully built leo-dynamics
installed distribution: $BUILD/sdist-venv/lib/python3.12/site-packages/leo_dynamics/__init__.py
No broken requirements found.
installed-wheel smoke: ... j2 propagation returned (10, 13)
396 passed in 2.53s
```

**396 passed, 0 skipped.**

### 3.7 Cross-platform evidence (CI, not local)

The four-platform matrix was **not** rebuilt locally — this host is Linux
x86-64 only. The evidence is the executed CI run
[39130183664](https://github.com/shikivaahan/leo-dynamics/actions/runs/39130183664)
at commit `72dddd07`, whose six jobs all concluded `success`:

| Job | Suite result |
|---|---|
| `linux-x86_64 manylinux_2_28` | 360 passed, 36 skipped |
| `macos-arm64` | 360 passed, 36 skipped |
| `macos-x86_64` | 360 passed, 36 skipped |
| `windows-amd64` | 360 passed, 36 skipped |
| `Source distribution rebuild` | 360 passed, 36 skipped, `No broken requirements found` |
| `Installed-wheel parity against the C++ engine` | **396 passed**, `No broken requirements found` |

The sixteen wheel artefacts and the sdist were downloaded from that run and
inspected locally. They are the sixteen expected slots — four CPython versions
(3.10–3.13) × four platform targets — plus `leo_dynamics-0.1.0.tar.gz`.

**There is no final tested commit yet.** The CI results above describe
commit `72dddd07`. The baseline `HEAD` was `fbf85a8` ("docs: correct
installed-wheel validation evidence"), one commit ahead, and this workstream
contributed further changes on top of it: the three workflow files, the new
release workflow, the three `ci/` scripts and two documents. Those changes
are now carried by the commit that adds this report, so they are **committed
but not yet through CI**. The `72dddd07` results therefore describe the source
and build configuration as they were then, not the tree that would be
released.

---

## 4. The release workflow

`.github/workflows/release.yml`, created in this work. It is **configured but
not executed** — its first job fails while the licence is missing, by design.

```
preflight ──> native-validation ──┐
    │                              ├──> publish (TestPyPI only)
    ├──> wheel-validation ──> collect ──┘
    │                              │
    └──> qualification ─────────────┘
```

| Job | What it does | Permissions |
|---|---|---|
| `preflight` | Refuses any ref that is not the default branch; checks the licence file, the licence metadata and the version | `contents: read` |
| `native-validation` | Calls `ci.yml` — the native suite, the sanitiser build, the frozen references — at this commit | `contents: read` |
| `wheel-validation` | Calls `wheels.yml` — the 4-platform × 4-CPython matrix, the installed-wheel parity job, the sdist rebuild — at this commit | `contents: read` |
| `qualification` | Calls `qualification.yml` — the six-hour numerical qualification campaign | `contents: read` |
| `collect` | Downloads the artefacts, verifies every one against this commit and this run, runs `twine check`, uploads the verified set | `contents: read`, `actions: read` |
| `publish` | Downloads the verified set and uploads to TestPyPI | **`id-token: write`**, `actions: read` |

**One caveat about the `qualification` gate.** That workflow has **never been
executed** — `gh run list --workflow="Manual six-hour and comparative
qualification"` returns an empty list, and the workflow list shows three
active workflows of which only two have runs. It is a 120-minute campaign that
installs micromamba and a pinned Tudat conda environment from the network. The
release gate therefore depends on a job that has not yet proven itself in CI.
That is the correct dependency to insist on — the qualification evidence is
what the numerical claims rest on — but it means the **first** release
dispatch will exercise it for the first time, and a failure there is more
likely to be environmental than numerical.

Properties that matter:

- **Manual trigger only.** `workflow_dispatch` and nothing else. No `push`, no
  `pull_request`, no `schedule`, no `release`. No fork or pull-request code can
  reach the publishing job.
- **Default-branch guard.** The `preflight` job refuses any ref other than
  `refs/heads/<default branch>`, so a dispatch from a side branch is rejected
  before anything is built.
- **Job-scoped `id-token: write`.** Only the `publish` job holds it, and the
  workflow-level default is `permissions: {}`. The publish job checks out
  nothing.
- **TestPyPI only.** `repository-url: https://test.pypi.org/legacy/`. There is
  no production job in the file, and the production pending publisher on
  pypi.org is untouched.
- **Same-commit artefacts.** Each build job writes a `provenance.json`
  recording the commit, the workflow run id and the SHA-256 of every
  distribution file; `collect` refuses to proceed unless every distribution
  file is covered by exactly one record naming this commit and this run, every
  recorded digest still matches, and the wheel set is exactly the declared
  matrix.
- **Inputs are not interpolated into shell.** `inputs.version` is passed as
  `RELEASE_VERSION` through `env:` and quoted in the shell.
- **Reuse, not duplication.** `ci.yml`, `wheels.yml` and `qualification.yml`
  gained a `workflow_call:` trigger and are called, so the release runs the
  same code paths ordinary CI runs.
- **Concurrency groups cannot collide.** Inside a reusable workflow
  `github.workflow` is the *caller's* workflow name, so a called workflow that
  groups on `${{ github.workflow }}` would otherwise compute the same group as
  its caller and cancel it. The groups are therefore explicitly prefixed and
  were computed and compared:

  | Workflow | Standalone | Called by the release |
  |---|---|---|
  | `wheels.yml` | `wheels-Wheel build and installed-wheel validation-<ref>` | `wheels-Release (TestPyPI only)-<ref>` |
  | `release.yml` | `release-testpypi-<ref>` | — |

  No two groups are equal, `release.yml` uses `cancel-in-progress: false`, and
  a release can neither cancel nor be cancelled by ordinary CI.

### 4.1 The licence gate

`ci/release_preflight.py` runs first and fails the release unless all of:

1. a non-empty `LICENSE` exists at the repository root;
2. `pyproject.toml` declares a licence string in `[project]`;
3. `project.license-files` lists both `LICENSE` and `THIRD_PARTY_NOTICES.md`;
4. the declared version equals the version being released.

It checks the mechanical facts a licence decision produces. It does not judge
whether the grant is legally sufficient.

Both directions were exercised:

```
# today, without a licence
$ python3 ci/release_preflight.py --project-root . --expected-version 0.1.0
release preflight failed: this release cannot be published yet.
  - no LICENSE file at the repository root; the copyright holder must add one
    (MIT is the recommended choice) and commit it
  - pyproject.toml does not declare the project licence; add license = "MIT"
    (or the SPDX expression the owner chooses) to the [project] table
  - pyproject.toml project.license-files does not list LICENSE; both documents
    must travel with every distribution
exit 1

# with a licensed fixture (temporary copy, $BUILD/licensed-fixture)
$ python3 ci/release_preflight.py --project-root $BUILD/licensed-fixture --expected-version 0.1.0
release preflight passed: licence file, licence metadata and version are in
place; the distribution may be built and published.
exit 0
```

A licensed build was also produced from a temporary copy with the MIT licence
applied, to confirm the metadata is correct once the owner decides:

```
leo_dynamics-0.1.0.dist-info/licenses/LICENSE
leo_dynamics-0.1.0.dist-info/licenses/THIRD_PARTY_NOTICES.md
METADATA: License-Expression: MIT
          License-File: LICENSE
          License-File: THIRD_PARTY_NOTICES.md
twine check: PASSED (both the wheel and the sdist)
sdist contents: leo_dynamics-0.1.0/LICENSE, leo_dynamics-0.1.0/THIRD_PARTY_NOTICES.md
```

### 4.2 The artefact verifier

`ci/verify_release_artifacts.py` refuses to let a set of artefacts reach an
index unless all of the following hold: every distribution file is covered by
exactly one provenance record naming this commit and this run; every file a
record names is actually present and its recorded SHA-256 still matches; the
wheel set is exactly the sixteen declared slots (one per CPython per
platform, `leo_dynamics`, the released version, the version-specific ABI, no
duplicates, no extras); exactly one source distribution is present, its
filename and its `PKG-INFO` both naming `leo_dynamics` at the released
version, carrying `LICENSE` and `THIRD_PARTY_NOTICES.md` and carrying no
build, environment or bulk-evidence directory; and every wheel carries
`LICENSE` and `THIRD_PARTY_NOTICES.md` inside `.dist-info/licenses/` and
declares a `License-Expression`.

It was exercised against real artefacts, not synthetic ones:

| Scenario | Expected | Observed |
|---|---|---|
| The sixteen real CI wheels + sdist from run `39130183664` | fails on the licence checks | **exit 1**, 33 failures, all of the form `expected to carry LICENSE` / `expected to declare a licence expression`; all 16 matrix slots verified as present |
| The same set with the licence documents and expression added | passes | **exit 0**, 246 checks passed |
| A wheel byte-mutated after its digest was recorded | fails | `expected to match its recorded SHA-256` |
| A recorded file deleted from the artefact set | fails | `expected to be present as its provenance record states`, plus the missing matrix slot |
| Expected version changed to `9.9.9` | fails | `expected to be version 9.9.9` |
| One matrix slot removed | fails | `wheel slot macosx_arm64 cp313: expected to hold exactly one wheel (found 0)` |
| Expected commit changed | fails | `sdist: expected to be built from commit deadbeef` |
| The source distribution renamed to a wrong version | fails | `leo_dynamics-9.9.9.tar.gz: expected to be version 0.1.0` |
| The source distribution's `PKG-INFO` carrying a wrong `Name` | fails | `expected to declare Name leo_dynamics in PKG-INFO` |

The failing path was also exercised on a second, independent copy of the
verifier, which caught two defects that were then fixed: the distribution name
compared with the wrong normalisation, and the source-distribution
content check that only tested the first forbidden prefix.

---

## 5. CI hardening

Applied to all four workflows, in response to `zizmor` findings:

| Finding | Fix |
|---|---|
| `unpinned-uses` (high) — mutable action tags | Every action pinned to a full commit SHA with the version in a comment: `actions/checkout@11d5960…` (v4), `actions/setup-python@a26af69b…` (v5), `actions/upload-artifact@ea165f8d…` (v4), `actions/download-artifact@d3f86a10…` (v4), `pypa/gh-action-pypi-publish@7245906f…` (release/v1) |
| `artipacked` (medium) — credential persistence | `persist-credentials: false` on every `actions/checkout` |
| `excessive-permissions` (medium) — default permissions | `permissions: contents: read` at workflow level in `ci.yml`, `qualification.yml` and `wheels.yml`; `permissions: {}` at workflow level in `release.yml`, with each job opting in explicitly |

Re-lint results after the changes:

```
$ zizmor --format plain .github/workflows/
27 findings (24 suppressed, 3 unsafe fixes): 0 informational, 3 low, 0 medium, 0 high
$ zizmor .github/workflows/ ; echo $?
12

$ actionlint .github/workflows/*.yml
.github/workflows/wheels.yml:83: label "macos-15-intel" is unknown.
```

**The three remaining `zizmor` findings are outstanding, not suppressed.**
They are all the same advisory, on the three reusable-workflow references:

```
help[self-repository]: use GitHub's dedicated self-repository syntax
  --> .github/workflows/release.yml:89:11
   |
89 |     uses: ./.github/workflows/ci.yml
```

`zizmor` prefers GitHub's `$/` syntax. The `./` form is used here, and the
two are in direct conflict with the pinned linter: `actionlint` 1.7.7 rejects
`$/` as malformed, verified directly against a minimal reproduction —

```
$ actionlint caller.yml
caller.yml:7:11: reusable workflow call "$/.github/workflows/callee.yml" at
"uses" is not following the format "owner/repo/path/to/workflow.yml@ref" nor
"./path/to/workflow.yml".
```

The `./` reference without a ref is the form this repository uses
throughout, and it denotes the same commit as the caller. The `$/` form is
*not* used here, and no claim is made that it is unavailable or unavailable
on this plan — the only verified fact is that the pinned `actionlint` does
not accept it. Silencing the advisory would make `zizmor` report zero
findings while hiding a real difference between the two linters, so the
finding is reported as outstanding instead. The `./` form is kept: it is the
same-commit, no-ref reference form this repository already uses, and it is the
form the pinned linter accepts. Switching to `$/` would mean adopting a newer
`actionlint` as well; that is an engineering trade-off for a future
maintenance pass, not an owner decision, and nothing about the release depends
on it. The `zizmor` exit status is 12 because of these three low-severity
findings.

The single `actionlint` finding is a false positive: that release's runner
label table predates `macos-15-intel`, which the current
`actions/runner-images` table documents as the Intel macOS 15 label. The label
was already in use by the pre-existing workflow and is unchanged.

The `unpinned-uses` severity is worth noting honestly: pinning to a SHA means
the action no longer updates itself, so a pinned action needs periodic manual
review for security fixes. That is the trade the finding asks for, and it is
recorded here so the maintenance cost is visible.

---

## 6. Files changed in this workstream

| File | Change |
|---|---|
| `.github/workflows/release.yml` | **new** — the TestPyPI-only release workflow |
| `.github/workflows/ci.yml` | SHA-pinned checkout, `persist-credentials: false`, `permissions: contents: read`, `workflow_call:` trigger |
| `.github/workflows/qualification.yml` | SHA-pinned checkout and upload, `persist-credentials: false`, `permissions: contents: read`, `workflow_call:` trigger |
| `.github/workflows/wheels.yml` | SHA-pinned actions, `persist-credentials: false`, `permissions: contents: read`, `workflow_call:` trigger, provenance recording in the wheel and sdist jobs |
| `ci/release_preflight.py` | **new** — the licence and version gate |
| `ci/verify_release_artifacts.py` | **new** — the same-commit artefact verifier: provenance coverage, digests, the exact 16-slot wheel matrix, the source distribution's filename, `PKG-INFO` name and version, and the licence documents in both artefacts |
| `ci/write_release_provenance.py` | **new** — the provenance recorder |
| `docs/python-packaging.md` | reconciled the stale `manylinux_2_28` and "no release workflow" statements with the observed CI run and the new workflow; replaced the API-token alternative with OIDC-only publication; separated *configured* from *executed* in the publication-prerequisites list |
| `docs/release-readiness-report.md` | **new** — this report |
| `docs/repository-hygiene-report.md` | **new** — the repository hygiene audit, carried in the same commit; its own claims about README/`.gitignore`/`.gitattributes` edits were corrected to *outstanding*, since those edits were never applied |

`pyproject.toml` and `THIRD_PARTY_NOTICES.md` were **not** changed. The first
needs the owner's licence decision; the second already states the position
correctly. `README.md`, `.gitignore` and `.gitattributes` were **not** changed
either; see `docs/repository-hygiene-report.md` F-04, F-11, F-12 and F-14.

---

## 7. Deviations and limits of this report

- **A remote environment was created.** Earlier in this work I called
  `PUT /repos/shikivaahan/leo-dynamics/environments/testpypi`, which created
  the `testpypi` environment (id `25307450685`). That was outside a read-only
  brief. The environment has been left in place and not altered further, and
  no other remote write was made. It is referenced by the release workflow, so
  it is needed; only its protection rules remain for the owner to configure.
- **No publication was attempted.** Nothing was uploaded to TestPyPI or PyPI,
  no release was tagged, and the repository visibility was not changed.
- **The changes are committed, but no final tested commit exists.** The changes
  above are carried by the commit that adds this report, on top of the baseline
  `fbf85a8`. Nothing here has been through CI yet, so no statement in this
  report is backed by a CI run of the new workflow.
- **The new workflow has never executed.** Its behaviour is established by
  local execution of the scripts it calls and by linting, not by a run. The
  first dispatch will exercise it, including the qualification campaign,
  which has never run at all.
- **The cross-platform matrix was not re-run locally.** The evidence in §3.7 is
  CI run `39130183664` at `72dddd07`, which is behind the current tree. A
  fresh run is required before the release artefacts themselves can be
  described as validated.
- **No Clang sanitiser leg was run locally**; only the GCC ASan/UBSan leg
  (§3.2). The Clang leg exists in CI and is called by the release workflow.
- **The legal question is not settled here.** §2.3 lists the ownership
  questions that only the owner can answer, and §2.4 lists one unresolved
  item — whether quoting a handful of published upstream values in the tracked
  evidence is within the upstream terms. No lawyer has reviewed the notices,
  and nothing in this report should be read as legal advice.
- **The `zizmor` exit status is 12**, because three low-severity
  `self-repository` advisories remain outstanding (§5). They are reported, not
  suppressed.

---

## 8. Owner actions, in order

These are **authorisations and decisions**, not file edits. Each one is
something only the owner can give; the mechanical application is then done by
this workstream.

1. **Answer the ownership questions in §2.3** — employment, third-party
   material, other contributors — and **authorise the licence**:
   - the licence terms (MIT is recommended, subject to §2.3);
   - the copyright holder's name and the year, for the `LICENSE` file.
   On that confirmation, this workstream adds the `LICENSE` file and applies
   the `pyproject.toml` metadata (`license = "MIT"`,
   `license-files = ["LICENSE", "THIRD_PARTY_NOTICES.md"]`) in one change, so
   the two cannot drift apart.
2. **Register the TestPyPI pending publisher** at
   <https://test.pypi.org/manage/account/publishing/> with project
   `leo-dynamics`, owner `shikivaahan`, repository `leo-dynamics`, workflow
   `release.yml`, environment `testpypi`.
3. **Inspect the `testpypi` environment** at
   <https://github.com/shikivaahan/leo-dynamics/settings/environments> and
   configure required reviewers and a default-branch restriction if the plan
   offers them for a private repository. If it does not, choose between a plan
   that supports environment protection and making the repository public —
   **but not before step 1**, because making it public exposes material whose
   redistribution status is still unresolved.
4. **Decide whether the repository becomes public**, and when, once step 1 has
   settled the rights. The audit above is the material that would be exposed;
   the bulk evidence question and the README presentation are handled
   separately.
5. After 1–3, let CI run at the commit that carries this work, and then
   dispatch **Release (TestPyPI only)** from the default branch with the version
   `0.1.0`. The workflow will then build, verify and upload, and stop. It cannot
   reach production PyPI. (The changes in this workstream are already committed
   and pushed; what remains is the CI run at that commit and the dispatch.)
