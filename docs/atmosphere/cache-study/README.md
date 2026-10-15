# ATM-CACHE-001 — atmospheric density cache study

Historical `direct-v2` and its blocked cache-study run remain immutable. The original SOLFSMY
bytes are unavailable; their pin has not changed. The original Earth PCK has been recovered and
verified. New work uses the separately pinned experimental input set
[`exp-solfsmy-20261011-original-pck`](input-sets/exp-solfsmy-20261011-original-pck.json).

## Input compatibility and evidence scope

Only SOLFSMY is substituted: `SOLFSMY-live.TXT`, SHA-256
`9bc734e66da7b5919ce6fd2b3475db82c28e500988dbb2f8d994d5ff3c074e35`.
The recovered original PCK SHA-256 is
`14deb9ab9dd49482185cc49a9c29fe56ae65d50ab94caf3197db8694836b6f81`.
CSSI, DTCFILE, DE432s, NAIF0012 and the scientific configuration retain their original pins.

All five historical fixture epochs were compared by model, UTC epoch, latitude, longitude and
altitude, using binary64 bit comparisons:

| Model | Identical densities | Identical temperatures | Identical constituents | Maximum relative difference |
|---|---:|---:|---:|---:|
| NRLMSISE-00 | 3,570 / 3,570 | 7,140 / 7,140 | 28,560 / 28,560 | 0 |
| JB2008 | 3,570 / 3,570 | 7,140 / 7,140 | not returned by the model | 0 |

Epochs: quiet June/December 2008, active March 2001, active October 28 and storm October 30, 2003.
The completed two-epoch smoke was reused; only the remaining three epochs were rerun.
Evidence: `/home/user/leo-cache-study-evidence/atm-cache-001-exp-solfsmy-20261011/compatibility-all-five.json`.
This proves equivalence **over the tested fixture domain**, not byte equivalence between SOLFSMY
releases or universal scientific equivalence.

The runner requires explicit `--input-set` selection, verifies every input hash and records the
definition hash in the resume identity. Neither input set nor source version may change on resume.
Compatible native-only NRLMSISE sensitivity can be imported with its original provenance; it is
never relabelled as provider, coupled, JB2008 or performance evidence.

## Status

**Complete.** The full study ran to completion on the separately pinned experimental input set:
550 tasks, 0 failures, 0 blocked, evidence class `scientific-experimental-pinned`.

**Recommendation: do not implement a production atmospheric density cache, for either model.** No
approximate configuration met the 0.1 % or 0.01 % density budget, and every configuration that did
meet the 1 % screening threshold was *slower* than the current fully optimised direct provider on
real trajectory work. See `RESULTS.md`.

## Headline measurement

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

Worst cases sit at 200–250 km and ±45° latitude, where log-density curvature is largest. This bounds
what any cache must reproduce, and it is the reason coarse altitude cells fail even when the time
axis is refined.

The full provider-path study extends this to both models and to every candidate family; the measured
screening ladder is in `ACCURACY.md` and the measured speedups in `PERFORMANCE.md`.

## Layout

```text
docs/atmosphere/cache-study/
├── README.md          this file: status, evidence classes, reproduction
├── DESIGN.md          how the harness isolates candidates and keeps native authority
├── PARAMETERS.md      every significant experiment choice and its rationale
├── SENSITIVITY.md     measured density sensitivity and provider cost structure
├── ACCURACY.md        screening contract and measured reconstruction accuracy
├── PROPAGATION.md     coupled protocol and measured propagation errors
├── PERFORMANCE.md     benchmark protocol and measured speedups
├── RESULTS.md         measured results and the production recommendation
├── DECISIONS.md       consequential decisions and rejected alternatives
├── run.py             deterministic, resumable driver (smoke/screen/full/resume)
├── coverage.py        measured native-domain coverage per (model, case)
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

# 3. Measure native-domain coverage per (model, case) at the study's own tolerances.
python3 docs/atmosphere/cache-study/coverage.py \
  --binary build-atmosphere-cache-study-final/atm_cache_study \
  --deps-dir /home/user/leo-cache-study-deps \
  --input-set-id exp-solfsmy-20261011-original-pck \
  --epoch-utc 2003-10-29T12:00:00 --duration 21600 \
  --rtol 1e-10 --max-step 15 \
  --out /home/user/leo-cache-study-evidence/atm-cache-001-exp-solfsmy-20261011/coverage/final

# 4. The full study; never resume or relabel the historical blocked run.
python3 docs/atmosphere/cache-study/run.py --mode full --workers 8 --max-hours 24 \
  --out /home/user/leo-cache-study-evidence/atm-cache-001-exp-solfsmy-20261011/study-final \
  --binary build-atmosphere-cache-study-final/atm_cache_study \
  --build-dir build-atmosphere-cache-study-final \
  --deps-dir /home/user/leo-cache-study-deps \
  --input-set docs/atmosphere/cache-study/input-sets/exp-solfsmy-20261011-original-pck.json \
  --native-source-dir /home/user/leo-cache-study-deps \
  --reuse-native /home/user/leo-cache-study-evidence/native-geodetic-pinned \
  --reuse-provenance /home/user/leo-cache-study-evidence/atm-cache-001-exp-solfsmy-20261011/native-reuse-provenance.json \
  --case-availability /home/user/leo-cache-study-evidence/atm-cache-001-exp-solfsmy-20261011/coverage/final/coverage.json \
  --refine-to-budget 0.1 --max-shortlist 0 --max-replans 128

# 5. Resume the same run with unchanged inputs, binary and study sources.
python3 docs/atmosphere/cache-study/run.py \
  --resume /home/user/leo-cache-study-evidence/atm-cache-001-exp-solfsmy-20261011/study-final --workers 8
```

The run above completed: 550 tasks, 0 failed, 0 blocked, `evidence/status.json` reports
`status: complete`. A resume with a changed binary, changed study source or changed inputs is
refused, which is how superseded runs were kept separate rather than silently mixed.
