# ATM-CACHE-001 — atmospheric density cache study

Status: **partial.** The tooling, the pinned-input NRLMSISE-00 native sensitivity, and the
end-to-end pipeline verification are complete. The primary direct-v2 study is **blocked** because
two pinned inputs no longer exist upstream, and the exact baseline was kept rather than replaced.

## What is measured, and what is not

| Item | State |
|---|---|
| NRLMSISE-00 native-input sensitivity, daily Ap and Ap-history | measured on pinned CSSI + LSK |
| Both-model pipeline, screening, checks, propagation and benchmark execution | verified on explicitly nonprimary inputs; never scientific evidence |
| Direct evaluation with caching disabled | bitwise identical to the current driver on CASE-A/B/C, both models |
| Primary provider/candidate accuracy, real trajectory locality, coupled propagation error, speedups | not measured; blocked on two inputs |

Two pinned `direct-v2` inputs are unavailable:

| Input | Pinned SHA-256 | Status |
|---|---|---|
| `SOLFSMY.TXT` | `891a0767…` | live file is a different release (`9bc734e6…`) |
| `earth_000101_270105_261009.bpc` | `14deb9ab…` | removed from NAIF; the rolling kernel moved to `earth_000101_270109_261013.bpc` |

Nothing was re-pinned. The runner records all 391 primary tasks as blocked and pending, prints a
resume command, and refuses to substitute either file. Alternate files were used **only** for the
`--verification-only` pipeline run, whose results are labelled
`evidence_class: pipeline-verification-only` everywhere they appear.

## Measured finding that is primary evidence

NRLMSISE-00 density is steeply altitude-dependent. One-dimensional log-density interpolation between
fresh native endpoints, on pinned inputs and independent holdout fractions:

| Altitude cell | Worst absolute relative error (daily Ap) | 0.1 % | 0.5 % | 1 % | 5 % |
|---:|---:|:--:|:--:|:--:|:--:|
| 1 km | 1.71e-04 | pass | pass | pass | pass |
| 5 km | 7.06e-04 | pass | pass | pass | pass |
| 10 km | 2.59e-03 | fail | pass | pass | pass |
| 25 km | 1.52e-02 | fail | fail | fail | pass |
| 50 km | 5.29e-02 | fail | fail | fail | fail |
| 100 km | 1.74e-01 | fail | fail | fail | fail |

Worst cases sit at 200–250 km and ±45° latitude, where log-density curvature is largest. Reaching
0.1 % on altitude alone needs roughly 5 km cells; the 0.01 % budget is not met by any tested width.
This bounds what any cache must reproduce, and it is the reason coarse altitude cells fail even when
the time axis is refined.

## Layout

```text
docs/atmosphere/cache-study/
├── README.md          this file: status, evidence classes, reproduction
├── DESIGN.md          how the harness isolates candidates and keeps native authority
├── PARAMETERS.md      every significant experiment choice and its rationale
├── SENSITIVITY.md     measured density sensitivity (pinned inputs)
├── ACCURACY.md        screening contract; what remains unmeasured
├── PROPAGATION.md     coupled protocol and the disabled-cache equivalence proof
├── PERFORMANCE.md     benchmark protocol; why no speedup is claimed
├── RESULTS.md         measured results, blocked work, and the go/no-go answer
├── DECISIONS.md       consequential decisions and rejected alternatives
├── run.py             deterministic, resumable driver (smoke/screen/full/resume)
├── analyse.py         reducer for the pinned native slices
└── evidence/          compact summaries, hashes and the manifest
```

External raw artefacts (large per-task JSON, binary traces, logs) live outside the repository and are
referenced by path and hash from `evidence/manifest.json`.

## Reproduction

```sh
# 1. Build the isolated study binary against the pinned native sources.
cmake -S docs/atmosphere/cache-study -B build-atmosphere-cache-study -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  '-DCMAKE_PREFIX_PATH=/home/user/leo-cache-study-build-deps/headers/usr' \
  -DCSPICE_ROOT=/home/user/leo-cache-study-build-deps/cspice \
  -DLEO_NRLMSISE00_ROOT=/home/user/leo-cache-study-deps \
  -DLEO_JB2008_SOURCE=/home/user/leo-cache-study-deps/JB2008.f \
  -DCMAKE_Fortran_COMPILER=/home/user/leo-cache-study-build-deps/fortran/usr/bin/gfortran-13
cmake --build build-atmosphere-cache-study --parallel 4

# 2. Pinned-input NRLMSISE-00 native sensitivity (primary evidence).
python3 docs/atmosphere/cache-study/analyse.py \
  --native /path/to/daily-ap/study-result.json \
  --native /path/to/ap-history/study-result.json \
  --out /path/to/evidence

# 3. Primary study: records the complete plan and stays blocked until the
#    two pinned inputs are restored.
python3 docs/atmosphere/cache-study/run.py --mode full --workers 8 --max-hours 8 \
  --out /path/to/run --binary build-atmosphere-cache-study/atm_cache_study

# 4. Resume after restoring the inputs; completed work is never recomputed.
python3 docs/atmosphere/cache-study/run.py --resume /path/to/run --workers 8
```

The regression suites are unchanged by this study and were run against the pinned fixtures:

```sh
python3 tests/cache_study_runner_tests.py      # 17/17 runner lifecycle cases
ctest --test-dir build-atmosphere-cache-study-regressions --output-on-failure  # 185/185
```