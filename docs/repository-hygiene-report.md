# Repository hygiene, security and release-readiness audit

> **Status update — 2026-10-26 licensing audit.** This document is preserved as the historical record of the state at its stated baseline; its licensing, packaging and release statements are superseded where they conflict with [RELEASE_LICENSE_AUDIT.md](../RELEASE_LICENSE_AUDIT.md). The numerical engine and Python interface now carry a **scoped MIT grant** ([LICENSE_SCOPE.md](../LICENSE_SCOPE.md)); all research, empirical atmosphere providers, experiments, datasets, tests and release tooling remain private and proprietary ([PROPRIETARY-NOTICE.md](../PROPRIETARY-NOTICE.md)). Publication remains **NO-GO** pending legal title/assignment review, upstream-parameter provenance review and verification of current private-repository plan support. No gate, threshold or historical result in this document was weakened or rewritten.

Audit date: 2026-10-26. Repository: `leo-dynamics` (origin `https://github.com/shikivaahan/leo-dynamics.git`).
Baseline audited: commit `fbf85a80f751b8d7f9790362a0f2d3671160ab89` (`main`, tracking `origin/main`).

Scope of this document: repository and history hygiene, secret scanning, dependency
and workflow security, repository size and evidence disposition, metadata, links and
documentation accuracy. Licensing compliance, packaging metadata and the publishing
workflow are owned by the packaging/release workstream; the licence observations below
are stated as release blockers, not resolved here.

Commands in this document are written with `<repo>` for the checkout and `<tools>` for a
scratch tool directory, so no absolute workstation path is embedded in this file.

---

## 1. Executive summary

| Area | Result |
|---|---|
| History-aware secret scan | 78 commits across all refs scanned; 161 raw findings, **0 confirmed secrets** after manual triage |
| Dependency vulnerabilities | No known vulnerabilities in the runtime, build or CI Python dependency set |
| GitHub Actions security | 23 static findings (12 High / 11 Medium) in the two workflows as they stood at baseline; **fixed in the release-readiness workstream** (SHA-pinned actions, `permissions:`, `persist-credentials: false`), with 3 low-severity advisories still outstanding |
| Tracked repository size | 926,354,590 bytes (883.4 MiB) in the working tree; 878.0 MiB of it under `docs/` |
| Git object store | 275,185,004 bytes (262.4 MiB) on disk; 248.82 MiB after an aggressive repack of a throwaway copy |
| Bulk evidence identified | 663,684,130 bytes (632.9 MiB) of raw historical output is a credible archive candidate; **nothing deleted** |
| Documentation | One obsolete README claim contradicted by repository content; platform table understated executed CI evidence |
| Release blockers | No project licence; no `LICENSE` file; repository metadata unset; repository still private |

One finding blocks a public release independently of this workstream: **no project
licence / redistribution rights** (F-09). It is not fixed here; it is listed with the
exact action required. The other baseline blocker, **no GitHub Actions `permissions:`
scoping** (F-06), has since been addressed by the release-readiness workstream: see F-05
to F-07 for what was changed and what remains outstanding.

---

## 2. Method and exact scope

Every command below was executed against the checkout at `fbf85a8`. Tools were obtained
for this audit because none were preinstalled: Gitleaks 8.28.0 (official release
archive), pip-audit 2.10.1 and zizmor 1.31.1 (isolated Python virtual environment), plus
`git` plumbing for size analysis.

### 2.1 Git and ref inventory

```
git rev-list --all --count                       -> 78
git for-each-ref --format='%(refname)'           -> 9 refs
git ls-remote origin                             -> 4 refs (HEAD, main, 2 tags)
```

| Ref | Type | Published on origin |
|---|---|---|
| `refs/heads/main` = `fbf85a8` | branch | yes |
| `refs/heads/sim` = `bbfc126` | branch | **no** (local only) |
| `refs/remotes/origin/main`, `refs/remotes/origin/HEAD` | remote-tracking | n/a |
| `refs/tags/benchmarks/opt001-candidate` = `b9b2f1f` | tag | yes |
| `refs/tags/benchmarks/opt001-final` = `a799fc6` | tag | yes |
| `refs/benchmarks/opt001-candidate`, `refs/benchmarks/opt001-final` | local convenience namespace | no |

`refs/benchmarks/*` duplicates `refs/tags/benchmarks/*` locally and makes the short
name ambiguous (`warning: refname 'benchmarks/opt001-final' is ambiguous`). The tags are
the durable, published form; the local namespace is redundant.

The `sim` branch (`bbfc126`, "tmp: gitattributes simulation") is an unpublished scratch
branch that reverts packaging documentation and changes the batch scheduler header. It
is not reachable from `origin` and carries no release content.

### 2.2 History-aware secret scan

```
gitleaks git --redact --no-banner \
  --log-opts="--all" --report-format json --report-path <report> <repo>
```

| Property | Value |
|---|---|
| Scanner / version | Gitleaks 8.28.0 |
| Mode | `gitleaks git` (full history, not a working-tree scan) |
| Refs covered | `--all` — every commit reachable from every ref in the clone (78/78) |
| Commits scanned | 78 |
| Bytes scanned | 351,866,079 (351.87 MB) |
| Exit status | 1 (findings present) |
| Raw findings | 161, all rule `generic-api-key` |
| Distinct commits with findings | 2 (`27fcbb9`, `f23c6cf`) |
| Distinct files with findings | 2 |

### 2.3 Dependency vulnerability audit

```
pip-audit -r <runtime-requirements>      # numpy>=2.0
pip-audit -r <build-and-ci-requirements>  # scikit-build-core==0.11.6, pybind11==3.1.0,
                                          # cmake>=3.28, ninja>=1.11, pytest, pip,
                                          # cibuildwheel==4.3.1, build, twine
```

Both runs reported **"No known vulnerabilities found"** against the PyPI advisory
database. The audit covered the mandatory runtime dependency, the pinned build
requirements that determine the packaging logic and binding headers, and the CI
toolchain that produces and repairs the wheels.

### 2.4 GitHub Actions static audit

```
zizmor --format json .github/workflows/
```

23 findings across the two workflows as they stood at baseline (`ci.yml`, `wheels.yml`);
see F-05, F-06 and F-07. `qualification.yml` produced no findings of its own beyond the
shared `uses:` and `permissions:` patterns.

The same scanner was re-run after the release-readiness workstream hardened the
workflows, and the finding set changed as described in F-05 to F-07: the baseline
High and Medium findings are addressed, and what remains is three low-severity
`self-repository` advisories on the reusable-workflow references in `release.yml`. The
`self-repository` advisory prefers GitHub's `$/` syntax, which the pinned `actionlint`
rejects; the conflict and the reasoning are recorded in
`docs/release-readiness-report.md` §5.

### 2.5 Repository size analysis

```
git count-objects -vH
git rev-list --objects --all | git cat-file --batch-check='%(objecttype) %(objectname) %(objectsize) %(rest)'
du -sb .git
```

---

## 3. Findings

Severity: **Critical** blocks publication or leaks a secret; **High** materially
weakens supply-chain or release security; **Medium** is a correctness, hygiene or
presentation defect that a public reviewer would notice; **Low** is a minor
inconsistency; **Info** is recorded context.

### F-01 — Secret scan: 161 raw findings, 0 confirmed secrets — Medium (triage cost)

All 161 findings use the single rule `generic-api-key`. Manual triage:

| Count | File | Commit | Why it matched | Verdict |
|---|---|---|---|---|
| 160 | `docs/atmosphere/optimisation/evidence/manifest.json` | `27fcbb9` | The rule keys on a `key`-like token adjacent to a high-entropy string. The matches are SHA-256 content digests of tracked source and dependency files, and CMake cache entries whose values are file hashes. | False positive |
| 1 | `tests/reference/test_reference_regression.py:71` | `f23c6cf` | The Python source contains the literal assignment `key='density_kg_m3_absolute'` — a dictionary key *name* used by the acceptance checker, adjacent to a numeric tolerance. | False positive |

No credential, API token, private key, certificate, `.env` file or password was found in
the scanned scope, and no secret **values** are reproduced anywhere in this document.

**This is a triage result, not a clean-history certification.** The scanner ran with the
default rule set over all 78 reachable commits; it cannot prove absence, does not detect
PII, and does not cover unreachable or subsequently rewritten objects. It is evidence
that no *known-pattern* credential is present in reachable history, nothing stronger.

### F-02 — Personal e-mail identity in every commit — Low (user decision)

All 76 authored commits carry one personal e-mail identity; the remaining two are
machine-authored benchmark snapshot commits using a `localhost` identity. The value is
deliberately not reproduced here. Before the repository becomes public, the owner should
confirm whether that address is the intended public commit identity or whether the
history should carry a project identity instead. **User action.**

### F-03 — Absolute host filesystem paths embedded in tracked evidence — Medium

Tracked files record absolute workstation paths — user home directories, external
evidence directories, dependency directories and `/tmp` build directories — inside
command logs, run manifests and documentation. Affected areas include
`benchmarks/comparative/results/measured-v01/summary.json`,
`docs/atmosphere/cache-study/` (README and evidence JSON),
`docs/atmosphere/evidence/`, `docs/atmosphere/optimisation/evidence/*/commands.json`,
`docs/atmosphere/profiling/evidence/` and `docs/v0.1-qualification.md`.

Why it was not fixed here: these strings are provenance for a specific measured run and
several of the files are hash-pinned by the campaign manifests. Rewriting them in place
would break the pinned digests and the re-verification performed by
`docs/atmosphere/optimisation/validate.py`; rewriting history to remove them is a
destructive operation that requires explicit approval. Recommended remediation, in
order of cost: (a) redact at the next manifest refresh, by publishing a **new** manifest
version with its own new SHA-256 rather than editing the existing one in place — the
current manifest's digest is pinned by the campaign record and by
`docs/atmosphere/optimisation/validate.py`, so redacting a file and keeping its
original digest would be impossible; the new manifest must retain the original
manifest's digest and provenance as its predecessor entry so the chain remains
auditable; (b) for new campaigns, record repository-relative paths and
environment-variable placeholders instead of absolute paths.

### F-04 — Untracked, unignored 116 MB generated output — Low

`outputs/` (116 MB: 103 CSV, 39 PNG, 1 ZIP, 1 HTML under `outputs/stage3-gallery/`) is
generated analysis output. It is referenced by the benchmark methodology as user-owned
and is already excluded from the source distribution, but it was **not** ignored by
`.gitignore`, so it appears as untracked noise in every `git status`.

**Status: outstanding, not fixed.** `.gitignore` has not been changed; the fix
proposed in F-14 was not applied. `outputs/` therefore still appears as untracked
output in `git status`, and it must not be staged by any commit.

### F-05 — GitHub Actions referenced by mutable tag, not by commit SHA — High

12 `uses:` references across `ci.yml` and `wheels.yml` pin a mutable tag
(`actions/checkout@v4`, `actions/setup-python@v5`, `actions/upload-artifact@v4`,
`actions/download-artifact@v4`) rather than a full commit SHA. A moved tag would change
the code that runs with repository credentials and artefact write access. The workflow
files are owned by the packaging/release workstream; the finding and its fix (pin every
`uses:` to a commit SHA) were routed there.

**Status: fixed.** The release-readiness workstream pinned every `uses:` reference in
all four workflows to a full commit SHA, with the upstream version in a trailing
comment: `actions/checkout@11d5960…` (v4), `actions/setup-python@a26af69b…` (v5),
`actions/upload-artifact@ea165f8d…` (v4), `actions/download-artifact@d3f86a10…` (v4)
and `pypa/gh-action-pypi-publish@7245906f…` (release/v1). These files are **not yet
committed and have never executed remotely**; the pinning is a local, reviewed change
only. The trade is that a pinned action no longer updates itself and needs periodic
manual review for security fixes.

### F-06 — No `permissions:` block, so the default token scope applies — High

Neither workflow declares `permissions:` at workflow or job level. The default
`GITHUB_TOKEN` scope therefore applies to every job, including jobs that only build and
upload artefacts. Least-privilege remediation: `permissions: contents: read` at workflow
level, with `id-token: write` added **only** on the publishing job that uses OIDC
trusted publishing. Routed to the workflow owner.

**Status: fixed.** `ci.yml`, `qualification.yml` and `wheels.yml` now declare
`permissions: contents: read` at workflow level; `release.yml` declares
`permissions: {}` at workflow level and each of its jobs opts in explicitly, with
`id-token: write` confined to the single publishing job. Again: not yet committed, not
yet executed remotely.

### F-07 — `actions/checkout` leaves the token persisted in `.git/config` — Medium

Five checkout steps omit `persist-credentials: false`, leaving the job token in the
local Git configuration where a later step could read it or where it could be captured
by an uploaded artefact. Remediation: add `persist-credentials: false` to every
checkout step. Routed to the workflow owner.

**Status: fixed.** Every `actions/checkout` step in all four workflows now sets
`persist-credentials: false`. Not yet committed, not yet executed remotely.

### F-08 — Repository metadata is unset — Medium

The GitHub API reports `description: null`, `topics: []` and no detected licence for the
repository, while `has_issues` and `has_wiki` are enabled. A public repository with no
description, no topics and no licence file presents as an unfinished project. Actions
required before the repository is made public: set a one-line description, add
`scientific-computing`, `astrodynamics`, `orbit-propagation`, `cpp`, `python` topics, and
disable the wiki if it will not be maintained. **User action** (repository settings).

### F-09 — No project licence and no redistribution rights — Critical (publication blocker)

There is no `LICENSE` or `COPYING` file, and `pyproject.toml` deliberately declares no
licence expression, with a comment stating that adding one would assert rights the
project has not granted. `THIRD_PARTY_NOTICES.md` records unresolved redistribution
questions for third-party material. A TestPyPI upload is a redistribution event and
cannot proceed until the project licence is chosen and the third-party position is
resolved. Owned by the packaging/compliance workstream. **User decision required.**

*[superseded 2026-10-26: a scoped MIT licence (`LICENSE`, `LICENSE_SCOPE.md`) now covers the enumerated public component, and the third-party position was documented for that component, with the upstream-applicability and shared-parameter questions still open; the licence names the evidenced author identity, but legal copyright-title verification is open and publication remains NO-GO — see [RELEASE_LICENSE_AUDIT.md](../RELEASE_LICENSE_AUDIT.md). The finding recorded above is retained as the baseline evidence.]*

### F-10 — Dependency vulnerability audit is clean — Info

No known vulnerabilities were reported for the runtime, build or CI dependency set
(section 2.3). The build dependencies that determine the wheel contents
(`scikit-build-core==0.11.6`, `pybind11==3.1.0`) are exactly pinned, and the C++
dependencies are provisioned as SHA-256-verified archives
(Eigen 3.4.0, Boost 1.83.0) by `cmake/provision.cmake`, so a wheel build does not depend
on host headers or a host package manager.

### F-11 — Hash-pinned evidence trees were not marked `-text` — Medium (defect)

`.gitattributes` marks the frozen golden fixtures, the acceptance limits and the
scenario files as `-text`, because a Windows checkout with `core.autocrlf=true` rewrote
LF files to CRLF and broke the pinned SHA-256 identities — the failure recorded in CI
run `39123791284` (17 failures in `tests/python/test_regression.py::test_frozen_fixture_identities`).

The same hazard exists for the campaign evidence trees: 579 tracked files are pinned by
digest in the campaign manifests, 395 of them text-like, and
`docs/atmosphere/optimisation/validate.py` re-verifies those digests on every run. A CRLF
checkout of those trees would break the identical class of check on a platform the
project targets.

**Status: outstanding, not fixed.** `.gitattributes` has not been changed; the
extension of the no-conversion rule described in F-14 was not applied. The committed
bytes are unchanged either way, but the CRLF hazard for the campaign evidence trees
remains open.

### F-12 — README contradicted repository content and understated executed evidence — Medium

Two defects in `README.md`:

1. The closing paragraph stated that "Stage 4 (optimisation and benchmarks) remains out
   of scope", while the repository ships a complete reproducible Stage 4 benchmark
   harness under `benchmarks/` (methodology, accuracy criteria, optimisation ledger and
   measured performance summary) and the README already links it. The statement was
   stale.
2. The supported-platform table described wheel builds and installed-wheel tests as
   "configured in CI", understating what has actually run: CI runs `39130183664`
   (commit `72dddd0`) and `39128373040` (commit `799de818`) each completed every wheel
   job, the source-distribution rebuild and the installed-wheel parity job successfully,
   producing 16 wheel tags plus the sdist.

**Status: outstanding, not fixed.** `README.md` has not been changed by this audit; both
defects are still present in the file as it stands. The corrections described below are
proposals, not applied edits: no numerical claim, tolerance, threshold or fixture would
change if they were applied, and no support would be asserted that has not been
measured (section 5).

### F-13 — README is embedded in the distribution metadata — Medium (coordination)

`pyproject.toml` declares `readme = { file = "README.md" }`, so every README edit
changes the `Description` field of the built wheel and sdist metadata. The artefacts
already built at `/tmp/leo-dist-312` were produced from the README as it stands, since
no README edit has been applied (F-12). The consequence is prospective rather than
current: **when the README corrections in F-12 are eventually applied, the wheel and
sdist must be rebuilt from the final README before any upload**, or the published
metadata will not match the published source. Flagged to the packaging owner.

### F-14 — Proposed `.gitignore` and `.gitattributes` improvements — Low (**not applied**)

* `.gitignore` (proposed, F-04): add `outputs/` so 116 MB of generated analysis output
  stops appearing as untracked noise.
* `.gitattributes` (proposed, F-11): extend the existing `-text` rule to the
  digest-pinned campaign evidence trees, with a comment explaining the mechanism.

**Neither change has been applied.** `.gitignore` and `.gitattributes` are unmodified
relative to the audited baseline, so F-04 and F-11 remain open. No other ignore or
attribute rule is warranted: no build product, wheel, virtual environment, cache or
compiled object is tracked in the repository, so no further suppression would be
justified.

### F-15 — No compiled binaries, wheels, virtual environments or archives are tracked — Info

Tracked file types are 1212 JSON, 351 gzip, 83 Python, 73 C++, 68 Markdown, 45 headers,
42 JSONL, 36 text, 6 PNG, 6 Fortran, 4 shell, 4 CMake, 3 YAML, 3 SVG, 3 config, plus
single files. Every `.gz` is a compressed evidence artefact under `docs/`; the PNG/SVG
files are generated figures. No `.whl`, `.tar.gz`, `.so`, `.dll`, `.exe`, `.o` or `.a`
file is tracked, and no virtual environment directory is tracked.

### F-16 — Local markdown links and anchors resolve — Info

All tracked Markdown files were parsed for inline links and headings: 0 broken relative
file links and 0 broken in-page or cross-document anchors, using GitHub's anchor
algorithm. One apparent hit in `docs/multithreading/DESIGN.md` was a false positive — a
C++ lambda signature inside a fenced code block.

**Uncertainty, stated explicitly:** external URLs were not machine-verified. Each
outbound link was reviewed by reading, but no HTTP request was made to every external
target, so a rotated or dead third-party URL (for example a kernel or tooling download
host) would not have been detected by this audit.

### F-17 — Ambiguous benchmark tag name and a stale local branch — Low

`refs/benchmarks/*` duplicates `refs/tags/benchmarks/*` locally and makes the short
name ambiguous in Git output; the published tags are the durable form and the local
namespace can be dropped. The unpublished `sim` branch (`bbfc126`, "tmp: gitattributes
simulation") is a disposable scratch branch based on `016af3a` that carries the
`.gitattributes` rule as an experiment; it is not reachable from `origin` and carries no
release content. Both are clean-up candidates that require no history rewrite; **no ref
was deleted by this audit** because deletion of published refs and branch removal are
release decisions.

### F-18 — Repository is still private — Info

`visibility: private`, `default_branch: main`, `archived: false`, GitHub-reported
repository size 257,473 KB. One GitHub Actions environment exists, named `testpypi`,
which is the environment the OIDC trusted publisher will need. Visibility was **not**
changed by this audit.

---

## 4. Repository size analysis

### 4.1 Exact figures

| Quantity | Bytes | MiB | Definition |
|---|---:|---:|---|
| Tracked working-tree content | 926,354,590 | 883.4 | sum of the sizes of all 1,945 tracked files |
| Unique content within the working tree | 369,649,827 | 352.5 | distinct file contents (SHA-256) among those files |
| Duplicate overhead inside the working tree | 556,704,763 | 530.9 | tracked bytes − tree-unique bytes; 98 duplicate groups, 701 extra copies |
| Unique blobs across all reachable history | 381,073,660 | 363.4 | 1,473 distinct blobs reachable from all refs |
| History-only content (superseded revisions) | 11,423,833 | 10.9 | history-unique − tree-unique |
| `.git` directory on disk | 275,185,004 | 262.4 | 2,041 loose objects, 267.60 MiB, plus a 2.30 KiB pack |
| `.git` after aggressive repack of a throwaway copy | 261,389,580 | 249.3 | 2 packs, `size-pack` 248.82 MiB |

### 4.2 Reconciling the two "duplicate" figures

Two different subtractions appear in discussions of this repository and they measure
different things:

* **530.9 MiB** = 926,354,590 − **369,649,827**. Both terms are from the *current
  working tree*: total tracked bytes minus distinct content in that tree. This is the
  genuine duplicate overhead of the checkout — the same bytes stored many times under
  different paths.
* **520.0 MiB** = 926,354,590 − **381,073,660**. The subtrahend is the unique blob set
  of *all reachable history*, which includes superseded revisions that no longer exist
  in the tree. Subtracting a history-wide quantity from a tree-wide quantity mixes two
  universes, so 520.0 MiB is not a meaningful duplicate figure for the tree.

The 10.9 MiB gap between the two unique-content numbers is exactly the history-only
content listed above. The defensible statement is therefore: **530.9 MiB of the tracked
tree is duplicate content; the tree holds 352.5 MiB of distinct data.**

### 4.3 Where the bytes are

| Path | Bytes | MiB | Share of tracked bytes |
|---|---:|---:|---:|
| `docs/` (all) | 920,657,152 | 878.0 | 99.4% |
| `docs/atmosphere/optimisation/` | 697,865,920 | 665.6 | 75.3% |
| `docs/atmosphere/profiling/` | 165,304,576 | 157.6 | 17.8% |
| `docs/multithreading/` | 31,131,008 | 29.7 | 3.4% |
| `docs/atmosphere/evidence/` | 12,255,232 | 11.7 | 1.3% |
| `docs/numerical-audit/` | 8,912,896 | 8.5 | 1.0% |
| `docs/atmosphere/cache-study/` | 7,667,712 | 7.3 | 0.8% |
| `tests/` | 3,981,312 | 3.8 | 0.4% |
| `benchmarks/` | 1,363,456 | 1.3 | 0.1% |
| `src/`, `include/`, `bindings/`, `python/`, `cmake/`, `examples/`, `ci/`, `scripts/`, `plotting/`, `.github/` | 1,277,952 | 1.2 | 0.1% |

Within the two dominant trees:

| Directory | Files | MiB |
|---|---:|---:|
| `docs/atmosphere/optimisation/evidence/cleanup-controls/` | 270 | 213.5 |
| `docs/atmosphere/optimisation/evidence/controls/` | 270 | 213.5 |
| `docs/atmosphere/optimisation/evidence/measurements/` | 15 | 112.6 |
| `docs/atmosphere/optimisation/evidence/cleanup-validation/` | 80 | 61.1 |
| `docs/atmosphere/optimisation/evidence/validation/` | 80 | 61.1 |
| `docs/atmosphere/optimisation/evidence/manifest.json` | 1 | 2.3 |
| `docs/atmosphere/profiling/evidence/instrumented/` | 145 | 69.6 |
| `docs/atmosphere/profiling/evidence/original/` | 145 | 57.0 |
| `docs/atmosphere/profiling/evidence/stages-instrumented-raw.json.gz` | 1 | 11.4 |
| `docs/atmosphere/profiling/evidence/isolated-concurrency-instrumented-raw.json.gz` | 1 | 7.9 |
| `docs/multithreading/evidence/benchmarks.json` | 1 | 10.9 |
| `docs/multithreading/evidence/scaling.json` | 1 | 7.0 |

### 4.4 Duplicate content

701 tracked files are byte-identical to another tracked file (98 duplicate groups). The
largest group is 18 identical copies of each control-condition native trajectory dump
(`CASE-C_jb2008_dopri_native.bin.gz`, 5.74 MiB each), repeated across the
`off_off_off`, `sun_only`, `orientation_only`, `utc_only`, `utc_sun`,
`utc_orientation`, `sun_orientation` and `cleanup-controls` control permutations. Git
already stores each of these blobs once; the duplication is checkout size, not history
size.

---

## 5. Evidence disposition: essential versus bulk

### 5.1 What must stay in the repository

| Path | Why it is essential |
|---|---|
| `tests/reference/golden/**` (44 files) | Frozen golden fixtures. Every SHA-256 in `tests/reference/golden/manifest.json` and the native Tudat manifest is computed over the exact committed bytes; they are the numerical contract. |
| `tests/reference/acceptance.json`, `tests/reference/ACCEPTANCE.md` | The frozen acceptance limits and their justification. |
| `tests/reference/scenarios/**` | Input scenarios for the acceptance protocol. |
| `tests/reference/golden/tudat/**` | Independent-oracle fixtures; needed to show the engine was checked against something other than itself. |
| `docs/atmosphere/optimisation/evidence/manifest.json` | The campaign manifest: source, dependency, tool and output digests plus the historical-hash set. This is the index for everything below. |
| `docs/atmosphere/profiling/evidence/manifest.json`, `docs/multithreading/evidence/manifest.json` | Same role for the profiling and multithreading campaigns. |
| `docs/**/evidence/**/verification.json`, `commands.json`, `summary.json`, `summary.md`, `analysis.json`, `comparisons.json`, `output-hashes.json` | Compact provenance and comparison records — the run identity, the commands, the verdicts. |
| `docs/atmosphere/optimisation/{DESIGN,DECISIONS,PARAMETERS,RESULTS,VALIDATION,BENCHMARKS}.md` and the equivalent sets under `docs/atmosphere/`, `docs/atmosphere/profiling/`, `docs/atmosphere/cache-study/`, `docs/multithreading/`, `docs/numerical-audit/` | Scientific methodology, decisions, acceptance criteria and conclusions. These are the human-readable record; the raw dumps support them but do not replace them. |
| `docs/numerical-audit/evidence/*.json` (non-figure) | The audit's numerical deltas and invariant checks, referenced by the defect ledger. |

### 5.2 Bulk candidate set (measured)

| Set | Path | Files | Bytes | MiB |
|---|---|---:|---:|---:|
| A | `docs/atmosphere/optimisation/evidence/**/*_native.bin.gz` | 324 | 369,782,370 | 352.7 |
| B | `docs/atmosphere/optimisation/evidence/**/CASE-*.json` (per-case result payloads) | 365 | 207,981,667 | 198.3 |
| C | `docs/atmosphere/profiling/evidence/**/{propagation,locality}/raw/*` | 52 | 47,522,596 | 45.3 |
| D | `docs/atmosphere/profiling/evidence/**/locality/*.json` (per-scenario locality dumps) | 160 | 11,835,289 | 11.3 |
| E | `docs/multithreading/evidence/*.json` larger than 1 MiB | 5 | 26,562,208 | 25.3 |
| **Total** | | **906** | **663,684,130** | **632.9** |

These are the raw per-case numerical payloads and per-request dumps: historical output
of completed campaigns, reproducible only with the external atmosphere and SPICE inputs
that are deliberately not redistributable, and not consumed by any test, workflow or
distribution build. The compact summaries, manifests, verification records, thresholds
and methodology listed in 5.1 stay in the repository.

### 5.3 Archive proposal (requires approval; nothing has been deleted)

Because the campaign manifests pin the bulk files by SHA-256, relocation must preserve
the digest contract. A workable archive mechanism, in increasing order of durability:

1. **GitHub Release asset archive (recommended).** A single `.tar.zst` per campaign
   (`optimisation-evidence.tar.zst`, `profiling-evidence.tar.zst`,
   `multithreading-evidence.tar.zst`) attached to a `v0.1.0` release, plus a SHA-256
   index file kept in the repository next to the manifest. The archive stores the files
   under their **exact current repository-relative paths**, so the existing manifest
   digests still verify after extraction, and `git archive` of the release tag keeps a
   second durable copy.
2. **Separate research dataset repository.** Same path-preserving requirement, with the
   dataset DOI cited from the campaign README.
3. **LFS-backed pointers.** Not recommended here: it moves the bytes into a separate
   store that a `git clone` does not reproduce, which weakens the "one clone reproduces
   the evidence" property the repository currently has.

Size to expect: the 632.9 MiB candidate compresses further because 530.9 MiB of the
tracked tree is duplicate content — a deduplicated archive of the candidate sets is
substantially smaller than the byte sum above.

**No file was deleted, moved or rewritten by this audit.** A durable archive must exist
and be verifiable *before* any removal, and the removal itself needs explicit approval.

### 5.4 History-rewrite proposal (not performed on the shared repository)

History rewriting would materially reduce the clone size only for the bulk sets, because
Git already stores the duplicated blobs once: the reachable history holds 381,073,660
bytes of unique blob content in total, and the aggressive repack of a throwaway copy
produced a 248.82 MiB pack. The realistic ceiling for a rewrite that removes the
candidate sets of 5.2 is the packed contribution of those ~633 MiB of raw output, not
their uncompressed size.

If a rewrite is ever approved, the constraints are: it must preserve every digest in
every manifest, every fixture, the acceptance limits and the published benchmark tags;
and it must be executed with `git filter-repo`, not `filter-branch`. It also invalidates
the published tag object IDs and every recorded commit SHA — including the commit
references in this document, in `docs/v0.1-release-readiness.md` and in the campaign
manifests — so it must happen **before** the first public release, never after.

**What was and was not done.** The history of the shared repository — the commits
reachable from `origin/main` and from the published tags — was not rewritten, no ref
was deleted, nothing was force-pushed, and the repository visibility was not changed.

A rewrite **experiment** was attempted on disposable material only, and it did not
result in a rewrite of anything: a `git filter-branch` invocation was run against a
throwaway copy and aborted (it refused to run in a bare work tree and then refused to
rewrite branches because of unstaged changes), and the local `sim` scratch branch
(F-17) carries a simulated variant of the `.gitattributes` rule. Neither touched
`origin/main` or the published tags; both are local and disposable. The phrase "no
history was rewritten" is therefore a statement about the shared repository and its
published refs, not a claim that no rewrite was ever attempted anywhere.

---

## 6. Changes applied by this audit

This audit produced exactly one file: the report itself.

| File | Change |
|---|---|
| `docs/repository-hygiene-report.md` | This report (new). |

**No other file was modified by this audit.** In particular, `README.md`,
`.gitignore` and `.gitattributes` are **unchanged** relative to the audited baseline,
despite the proposals in F-12 and F-14; those proposals are recorded here as
outstanding work, not as applied fixes. An earlier draft of this report listed them as
applied changes; that was wrong and has been corrected.

Files deliberately **not** touched: `README.md`, `.gitignore`, `.gitattributes`,
`pyproject.toml`, `.github/workflows/*`, `THIRD_PARTY_NOTICES.md`, `LICENSE` (does not
exist), `docs/python-packaging.md`, `docs/v0.1-release-readiness.md`,
`docs/v0.1-qualification.md` and every file under `docs/**/evidence/**`. The packaging
and workflow changes belong to the packaging/release workstream and are described in
`docs/release-readiness-report.md`; the evidence tree is the numerical record.

*[superseded 2026-10-26: `LICENSE` now exists, together with `LICENSE_SCOPE.md` and `PROPRIETARY-NOTICE.md`; they were added by the licensing audit, not by the hygiene audit recorded here, and the list above is retained as the baseline record.]*

---

## 7. Validation performed for these changes

* **README Python examples, executed against an installed wheel outside the source
  tree.** A CPython 3.12 Linux x86-64 wheel (`leo_dynamics-0.1.0-cp312-cp312-linux_x86_64.whl`,
  built by the packaging workstream at `/tmp/leo-dist-312`) was installed into an
  isolated environment and imported from `site-packages` with the working directory
  outside the checkout. Both documented examples were run verbatim:
  * one-orbit example: `states` shape `(90, 13)` `float64`, `position_m` shape `(90, 3)`,
    final altitude 620,624.683 m, and the documented nine CSV datasets written;
  * heterogeneous batch example with `workers=3`: `batch.states` shape `(4, 90, 13)`,
    `batch.position_m` shape `(4, 90, 3)`, nine CSV datasets exported.
  This exercises Linux x86-64 CPython 3.12 only. It is not platform coverage and is not
  a substitute for the installed-wheel CI job. It validates the README **as it stands**;
  since the README was not edited (F-12), this run is a baseline check of the current
  text, not a check of a proposed correction.
* **Link and anchor audit** over the tracked Markdown, including the README as it
  stands: 0 broken relative links, 0 broken anchors (F-16).
* **Markdown structure of this report** verified for heading/anchor integrity.
* No build, test or lint run was executed by this workstream beyond the above; the
  native C++ suite, the sanitiser matrix, the wheel matrix and the sdist rebuild are
  owned and reported by the packaging workstream.

---

## 8. Remaining limitations of this audit

1. The secret scan is a triage of one scanner's default rules over reachable history.
   It is not a proof of absence and it does not cover PII.
2. External URLs were reviewed by reading only; no automated HTTP reachability check
   was performed.
3. Size figures for a *hypothetical* archive or history rewrite are computed from the
   current tree and packed object store. They are estimates until an archive is built.
4. The bulk/essential boundary in section 5.2 is a proposal based on what the test
   suite, workflows and distribution build actually consume. The maintainer may know of
   additional consumers of the raw dumps.
5. The licence, third-party redistribution position and the packaging metadata are not
   assessed here beyond identifying the blocker.

---

## 9. Actions required before the repository becomes public

| # | Action | Owner | Severity |
|---|---|---|---|
| 1 | Choose and add the project licence; resolve the third-party redistribution questions; then update `pyproject.toml` licence metadata. | User decision + compliance | Critical |
| 2 | Pin every GitHub Actions `uses:` to a full commit SHA; add `permissions: contents: read` at workflow level and job-scoped `id-token: write` only on the publishing job; add `persist-credentials: false` to every checkout. **Done in the release-readiness workstream** (F-05 to F-07); three low-severity `self-repository` advisories remain outstanding there. | Packaging / release | High |
| 3 | Set the repository description and topics; decide on the wiki; confirm the public commit identity (F-02). | User | Medium |
| 4 | Rebuild the wheel and sdist from the final README so the embedded metadata matches the repository (F-13). Required once the README corrections (F-12) are applied. | Packaging | Medium |
| 5 | Decide the archive mechanism for the 632.9 MiB bulk candidate set and approve or decline the removal; a durable, digest-verifiable archive must exist first. | User | Medium |
| 6 | Redact absolute host paths from tracked evidence at the next manifest refresh, as a new manifest version that retains the previous manifest's digest and provenance (F-03). | Numerical / evidence | Medium |
| 7 | Drop the redundant local `refs/benchmarks/*` namespace and decide the fate of the unpublished `sim` branch (F-17). | User | Low |
| 8 | Apply the outstanding `.gitignore` and `.gitattributes` proposals (F-04, F-11, F-14) and the README corrections (F-12); the `.gitignore` change is what stops `outputs/` appearing as untracked noise. | Repository hygiene | Low |

**No secret values are reproduced in this document.**
