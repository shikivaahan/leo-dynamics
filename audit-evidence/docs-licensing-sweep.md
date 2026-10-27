# Documentation licensing sweep — v0.1.0 pre-release licensing audit

> **Superseded 2026-10-27.** The owner has since authorised the standard MIT grant over the whole project, and the proprietary scope this record describes no longer applies. The classifications below are the dated record of the 2026-10-26 audit; the governing classification is now `release-audit/license-boundary.json`.

**Date:** 2026-10-26 · **Branch:** `audit/release-licensing-v0.1.0` ·
**Baseline:** `2bb5ccb` ("audit: scope the MIT core, enforce the proprietary
boundary, verify v0.1.0 packaging") · **Authoritative report:**
[`RELEASE_LICENSE_AUDIT.md`](../RELEASE_LICENSE_AUDIT.md)

> Research evidence only. This note records which internal documentation
> statements were found to be stale and what was done about them. It is not a
> licence grant, not legal advice, not a clearance and not an authorisation to
> publish. The licence grant is [`LICENSE`](../LICENSE) scoped by
> [`LICENSE_SCOPE.md`](../LICENSE_SCOPE.md); the current position, the open legal
> items and the NO-GO recommendation are in
> [`RELEASE_LICENSE_AUDIT.md`](../RELEASE_LICENSE_AUDIT.md).

## What was done

Every statement in the internal documentation that asserted "no licence", "no
redistribution right" or "the licence file does not exist" was either annotated
in place with a dated `[superseded 2026-10-26: …]` note, or left exactly as
written and covered by a dated status banner at the top of its document. No
measurement, baseline date, hash, count, gate, blocker or test-tolerance
statement was deleted, softened or rewritten.

| Document | Banner | Stale statements found | Annotated | Banner-only |
|---|---|---:|---:|---:|
| `docs/release-readiness-report.md` | present (line 3) | 13 | 5 | 8 |
| `docs/v0.1-release-readiness.md` | present (line 3) | 1 | 0 | 1 |
| `docs/python-packaging.md` | present (line 3) | 8 | 6 | 2 |
| `docs/repository-hygiene-report.md` | present (line 3) | 8 | 2 | 6 |
| `README.md` | present (line 3) | 2 | 0 | 2 |
| **Total** | | **32** | **13** | **19** |

Machine-readable detail, including every quoted statement with its baseline
line number and the action taken, is in
[`docs-licensing-sweep.json`](docs-licensing-sweep.json).

## Banners

All five documents start with a dated status banner of the form:

> **Status update — 2026-10-26 licensing audit.** … the scoped MIT grant exists
> … all other repository material remains proprietary and is not distributed …
> publication is not authorised and remains **NO-GO** … the current position,
> the open legal items and the NO-GO recommendation are in
> `RELEASE_LICENSE_AUDIT.md` … no gate, threshold or historical result was
> weakened or rewritten.

Banners were **added by this sweep** to `README.md` and
`docs/python-packaging.md`. The banners in `docs/release-readiness-report.md`,
`docs/v0.1-release-readiness.md` and `docs/repository-hygiene-report.md` were
already added by the concurrent licensing commit `2bb5ccb`; they were left
byte-for-byte unchanged and were not reverted or reworded. The more detailed
"Licensing-audit update (2026-10-26)" section in `docs/python-packaging.md` was
preserved verbatim.

## Annotated statements (13, in 11 dated notes)

- `docs/release-readiness-report.md` — verdict blocker 1 ("The project has no
  licence"); §2.1 "Why it was not applied"; §2.3 "Until 1–4 are answered, no
  licence may be applied and no artefact may be redistributed"; §2.5 "One gap
  that is a real defect, not a formality"; §8 owner action 1.
- `docs/python-packaging.md` — CI-artefact licence statement; the whole
  "Project licence and redistribution status" section; publication
  prerequisite 1; "What remains outstanding".
- `docs/repository-hygiene-report.md` — finding F-09 body; the "`LICENSE`
  (does not exist)" parenthetical in the "files not touched" list.

Each annotation is additive and dated. None of them claims a release, legal
clearance or verified ownership; the ownership wording is limited to "the
licence names the evidenced author identity; legal title verification is
open", and each annotation keeps the publication NO-GO explicit.

## Deliberately left as historical

Recorded command output, artefact sizes and SHA-256 digests, CI run identifiers,
test counts, the baseline sdist member list, the baseline summary tables, the
recorded GitHub API observation, and every gate, blocker and acceptance limit
were kept exactly as written. In particular:

- the §2.1 evidence table rows of `docs/release-readiness-report.md` (the
  commands and their results at the time of the baseline run) are evidence and
  were not edited;
- the ownership questions 1–4, F-09's Critical severity and the release
  workflow's refusal to publish without a licence remain open and unchanged;
- `docs/v0.1-release-readiness.md` contains no licensing statement; its
  numerical verdict, the 17 frozen acceptance limits and the defect record are
  untouched.

## Changed paths

```
README.md
docs/python-packaging.md
docs/release-readiness-report.md
docs/repository-hygiene-report.md
audit-evidence/docs-licensing-sweep.json
audit-evidence/docs-licensing-sweep.md
```

No other file was modified: no push, publication, index upload, workflow
dispatch or repository creation was performed, the branch was never switched,
no history was rewritten, and the pre-existing untracked `outputs/` tree was
left untouched. `RELEASE_LICENSE_AUDIT.md`, `THIRD_PARTY_NOTICES.md`,
`audit-progress.md`, `release-audit/`, `.github/workflows/`, `LICENSE`,
`LICENSE_SCOPE.md`, `PROPRIETARY-NOTICE.md`, `README.dist.md`, `pyproject.toml`,
`CMakeLists.txt`, `ci/`, `tests/` and the other `audit-evidence/` files were
read only.
