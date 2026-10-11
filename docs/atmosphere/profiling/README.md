# ATM-PROF-001 — uncached atmospheric pipeline profiling

Measurement and analysis only. Production source is unchanged. No caching, memoisation, interpolation, quantisation, optimisation, scientific configuration change or mutex removal. `direct-v1`, `direct-v2` and numerical-audit evidence are not overwritten.

## Read

- [RESULTS.md](RESULTS.md): function-level breakdown, cross-checks, measurement inflation, threading and scientific verification.
- [LOCALITY.md](LOCALITY.md): actual RHS request reuse and controlled schedules.
- [OPTIMISATION_OPPORTUNITIES.md](OPTIMISATION_OPPORTUNITIES.md): dependency classes and Amdahl ceilings, not measured speedups.
- [CALL_INVENTORY.md](CALL_INVENTORY.md): separate ordered native-provider call inventories, nested CSPICE operations and exact original source ranges.
- [EXPERIMENT_DESIGN.md](EXPERIMENT_DESIGN.md): preregistered methodology, workloads and bias.
- [DECISIONS.md](DECISIONS.md): actual values, alternatives and consequential choices.
- [evidence/manifest.json](evidence/manifest.json): revisions, dirty state, hashes, tools, host, commands, numerical/model settings and verification.

## Build

Dependencies must already be acquired and hash-pinned. No network acquisition is performed by configure or measurement. Paths below are the paths used for the published experiment; override them for another machine.

```sh
cmake -S docs/atmosphere/profiling -B build-atmosphere-profiling -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG" \
  -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG" \
  -DCMAKE_Fortran_FLAGS_RELEASE="-O3 -DNDEBUG" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DLEO_NRLMSISE00_ROOT=/tmp/leo-atmosphere-deps \
  -DLEO_JB2008_SOURCE=/tmp/leo-atmosphere-deps/JB2008.f \
  -DCMAKE_Fortran_COMPILER=/tmp/leo-fortran/usr/bin/gfortran-13 \
  -DCMAKE_PREFIX_PATH="/tmp/leo-deps/usr;/tmp/leo-deps/catch-install" \
  -DCSPICE_ROOT=/tmp/leo_cspice/cspice
cmake --build build-atmosphere-profiling --parallel 4
```

This standalone root builds original libraries and generated diagnostic copies side by side. The normal project build has no profiling option, timer, logging or allocation added. Generation checks exact unique source anchors, writes original-source hashes and leaves the originals untouched. C++ calculations, native reference sources, CSPICE library and protected mutex scopes remain the same.

## Run

Run from the repository root. `--out` must be absent or empty; use a **new** directory for reproduction, never the published `evidence/`.

```sh
python3 docs/atmosphere/profiling/run.py \
  --build-dir build-atmosphere-profiling \
  --deps /tmp/leo-atmosphere-deps \
  --test-dir build-atmosphere-gcc \
  --single-cpu 0 --concurrency-cpus 0-3 \
  --out /tmp/atm-prof-001-reproduction
```

The runner executes original/diagnostic stage, locality and six-hour CASE-A/B/C workloads; reruns the five-epoch fixture grid; supplies real kernels and official native-reference fixtures to the existing regression suite; verifies scientific equality and historical/source preservation; retains raw data and hashes outputs. Benchmarks execute serially, not simultaneously with build/tests. It records stdout/stderr and failing return codes in `command-log.json` before stopping on a failed scientific check. Do not interpret a partial run as complete evidence.

`profile_stages_{original,instrumented}` pin the main thread to the first allowed CPU for microbenchmarks and restore the supplied mask before 1/2/4-thread experiments. `profile_workloads_*` are externally pinned to CPU0. Shared 0–3 affinity permits migrations within that mask during concurrency; it is not a dedicated-core guarantee under WSL2.

`profile_concurrency_{original,instrumented}` additionally isolate native-only, rotation-only, UTC-only, Sun-only and full RHS-environment Sun+provider contention. Reproduction commands and exact inputs are recorded in the manifest.

## Evidence and reanalysis

Top-level `stage-timings.json`, `workload-timings.json`, `locality-statistics.json`, `concurrency-results.json` are navigational summaries. Raw stage/concurrency records and large per-batch files are gzip-compressed, **not discarded**. Original and diagnostic workload directories contain trajectory checksums, full scientific series, outer timing samples, nested stage distributions and complete rep0 request JSONL. JSONL rows are arrays; field order is declared in `reduce_locality.py`. UTC metadata is acquired by the real wrapper outside measured loops. No ET/86400 calendar approximation is used.

```sh
python3 docs/atmosphere/profiling/reduce_locality.py \
  --profile-out /tmp/atm-prof-001-reproduction/original
python3 docs/atmosphere/profiling/reduce_locality.py \
  --profile-out /tmp/atm-prof-001-reproduction/instrumented
python3 docs/atmosphere/profiling/analyse.py \
  --out /tmp/atm-prof-001-reproduction
```

Stage percentiles use nearest-rank tails; workload percentiles use linear interpolation at q(n−1). Raw data permit either estimator. Inclusive stages contain nested children. Do not sum parent+child costs or treat the difference between independent medians as a decomposition. The published breakdown labels timer-inclusive attribution separately from the uninstrumented control.

Host limitations: WSL2 scheduling, unknown physical-core isolation/frequency governor, no installed `perf`. Native and CSPICE state can have its own internal warmed behaviour even though the atmospheric provider remains uncached. Amdahl estimates exclude the cost of any future lookup, memory management and synchronisation; no cache-performance prediction is claimed.
