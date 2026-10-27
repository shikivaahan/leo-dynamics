# Local licensing-audit status — final, 2026-10-26

Branch: `audit/release-licensing-v0.1.0`, created from `23df023d`.
No history rewrite, destructive git, push, upload, remote creation, workflow
dispatch or publication. The pre-existing untracked `outputs/` was preserved.

## Delivered locally

- Scoped MIT component: standard MIT terms (`LICENSE`, with an external scope
  pointer), `LICENSE_SCOPE.md` defining the licensed subject matter,
  `PROPRIETARY-NOTICE.md` for everything outside it, PEP 639 metadata, and a
  public-only `README.dist.md` used for package metadata.
- Observed final source distribution: exactly the 52 approved original files
  plus generated `PKG-INFO`. Both final wheels: 10 members each. Full member
  hashes, artifact identities, gate reports and logs are private in
  `release-audit/`.
- Direct CPython 3.13 wheel + sdist, and a clean CPython 3.12 wheel rebuilt
  from that sdist. The **CPython 3.12 rebuilt wheel** was installed and
  executed: 396 Python tests passed, runtime smoke passed (J2 orbit, SRP/drag/
  albedo/gravity-gradient environment, native batch, 9 CSV exports). The
  CPython 3.13 wheel was built and inspected, not runtime-tested.
- Native suite: fresh Release build, 120/120 tests passed. Catch2 3.7.1
  (`release-audit/native-Catch2ConfigVersion.cmake`).
- Leak gate: 15 regression tests pass, covering valid complete controls and
  manifest tampering, marker-free renamed research payloads, metadata
  channels, links, unsafe names, duplicates, wrong roots and missing
  payload/licence members. Archive checks run before every artifact upload in
  the wheel workflow and before collection/upload in the release workflow.
- Linux linkage and full binary-string evidence retained; consumed header
  comparison Eigen 170/170 and Boost 1066/1066, zero mismatches against the
  pinned archives.
- `RELEASE_LICENSE_AUDIT.md` integrates the baseline research evidence with the
  final build facts, and states the verdict.

## Exact scope

MIT component: the numerical engine sources and public headers, the three
provider-independent ephemeris interfaces, `bindings/`, `python/leo_dynamics/`,
`CMakeLists.txt`, `pyproject.toml`, `cmake/provision.cmake`, the licence
documents/notices and `README.dist.md`. Outside the grant and not distributed:
all empirical atmosphere and CSPICE adapters, the whole `docs/` tree, research,
optimisation experiments and datasets, every test suite and frozen research
fixture, examples, plotting, and packaging/CI/release/audit tooling.

## Publication verdict: NO-GO

Owner MIT intent is approved and implemented, but it is not proof of legal
title. Blocking items: copyright title/assignment (no instrument found in the
repository); upstream applicability and the ultimate provenance of the shared
40 fixed atmospheric anchors; current private-repository plan entitlement
(`/user` plan is null, so an existing environment is not proof); platform
artifacts beyond Linux; and the absence of explicit publication approval.

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
