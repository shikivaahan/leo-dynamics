# Local licensing-audit status — final, 2026-10-26

Branch: `audit/release-licensing-v0.1.0`, created from `23df023d`.
No history rewrite, destructive git, push, upload, remote creation, workflow
dispatch or publication. All audit work is committed locally on this branch
(`2bb5ccb` through the latest commit); `origin/main` still points at
`23df023d`. The pre-existing untracked `outputs/` and the local `dist*`/`build*`
trees were preserved untouched, and every research and historical artefact is
still present: the regenerated boundary manifest classifies **1,965 tracked
files as proprietary** (plus 2 repository-plumbing files) out of **2,019
classified tracked files**, of which **52** are the MIT core that may be
distributed.

## Local audit commits

```
3eb9703 audit: keep the licensing audit evidence logs in the branch
7a3b3ee audit: finalise the boundary manifest, changed-path record and report counts
5516344 docs: qualify the proprietary notice and record the documentation sweep in the audit report
3798ad6 docs: annotate stale licensing statements in the internal documentation sweep
bbd94fa docs: state the shipped notice bytes and that all evidence is from the final tree
5413b21 audit: label the superseded first gate-regression run and close evidence logs
3bf5f08 docs: record the local audit commit in the audit report and progress note
2bb5ccb audit: scope the MIT core, enforce the proprietary boundary, verify v0.1.0 packaging
```

## Six green checks (exit codes)

```
{
  "generate_boundary.py (regenerate)": 0,
  "generate_boundary.py --check": 0,
  "verify_license_boundary.py (source)": 0,
  "verify_license_boundary.py --artifacts": 0,
  "release_preflight.py --expected-version 0.1.0": 0,
  "tests/license_boundary_tests.py": 0
}
```

## Final artefact identities (private, never uploaded)

```
release-audit/artifacts/direct/leo_dynamics-0.1.0-cp313-cp313-linux_x86_64.whl  bytes=368403  sha256=87df8e2428f43118694f431e3596a078f136648a4c14f1ced20480123cee8f2f
release-audit/artifacts/direct/leo_dynamics-0.1.0.tar.gz  bytes=83279  sha256=0906aade8fa832c1f9ad7c05f14ae62c47984787dc3a26c997a61fbb70fa5767
release-audit/artifacts/rebuilt/leo_dynamics-0.1.0-cp312-cp312-linux_x86_64.whl  bytes=368448  sha256=e4e874217f2cde42bcf5fbd73d11ebed24cef9c1534bea33314b0c094803c947
```

The source distribution holds exactly the 52 approved original files plus the
generated `PKG-INFO` (53 members); each wheel holds 10 members. The archive
gate re-run after this final step proves the public contents are unchanged: all
three artefact SHA-256 values are identical to the recorded ones, and no
evidence log is inside any artefact.

## Exact scope

MIT component: the numerical engine sources and public headers, the three
provider-independent ephemeris interfaces, `bindings/`, `python/leo_dynamics/`,
`CMakeLists.txt`, `pyproject.toml`, `cmake/provision.cmake`, the licence
documents/notices and `README.dist.md`. Outside the grant and not distributed:
all empirical atmosphere and CSPICE adapters, the whole `docs/` tree, research,
optimisation experiments and datasets, every test suite and frozen research
fixture, examples, plotting, and packaging/CI/release/audit tooling. The audit
evidence itself (`release-audit/`) is proprietary and excluded from both
distributions.

## Publication verdict: NO-GO

Owner MIT intent is approved and implemented, but it is not proof of legal
title. Blocking items: copyright title/assignment (no instrument found in the
repository); upstream applicability and the ultimate provenance of the shared
40 fixed atmospheric anchors; current private-repository plan entitlement
(`/user` plan is null, so an existing environment is not proof); platform
artifacts beyond Linux; and the absence of explicit publication approval.
Nothing was published, pushed or dispatched.

Ownership evidence, stated precisely: the release-line history has 76 commits
and all refs 79 — 77 under the human author identity
`Shiki Vaahan <shikivaahan@gmail.com>` and 2 under the local machine identity
`LEO benchmark snapshot <benchmarks@localhost>`. Author identity is evidence
of authorship, not of legal title, employment status or assignment. The
copyright line uses the evidenced author name rather than an invented entity.

The owner states that no Imperial College or Siemens source code is used. A
repository-wide search found no matching third-party attribution strings, and
that negative search is not proof of absence: it cannot exclude material that
carries no such string, and the shared 40-parameter atmospheric table is a
concrete example of provenance that string searches do not surface. Nothing
here contradicts the owner's statement, and no affiliation metadata, formula
similarity or name coincidence is treated as evidence against it. Imperial
College Space Society (the distinct upstream Julia project, MIT, Copyright
(c) 2022, notice reproduced precautionarily) must not be conflated with the
university or an employer.
