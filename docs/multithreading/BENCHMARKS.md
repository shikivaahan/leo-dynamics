# Benchmark protocol and historical MT-001 evidence

Production and diagnostic timing are separate. The benchmark uses actual `propagate()`/`propagate_batch()` calls and frozen CASE physics, never a synthetic RHS throughput proxy. The only synthetic measurement is the explicitly labelled thread-startup probe.

The measured matrices below are frozen MT-001 results. Current sources have one
internal dynamic scheduler; the commands below are adapted to that API and do
not reproduce the removed static-policy dimension. Reproduce the exact historical
matrix from revision `8a806534987d8e04a8f6d27b24ab5ee8150bab20`.
For current decisions use the compact [MT-002 protocol](MT002.md), not another
complete matrix.

The isolated startup probe (`startup.cpp`, and the `jthreads` wording in the
recorded evidence) is historical: it still creates `std::jthread`s so that the
frozen measurements remain reproducible. The production scheduler no longer does
— `batch.hpp` uses `std::thread`, since AppleClang's libc++ does not declare
`std::jthread` and that header is compiled into the macOS wheel. The probe's
numbers describe thread-startup cost, which is unchanged by that substitution;
the two are separate concerns.

## Timing boundaries

Total wall time brackets the complete solver/executor call: output-vector allocation, RHS construction, thread creation/dispatch, propagation, RHS destruction, joining and result return. Kernel/weather loading, initial-state generation, post-run checksum reduction, JSON writing and instrumentation-buffer allocation are excluded. Output sample buffers remain included.

Factory entry, RHS ready, first RHS call, RHS destructor entry and final member destruction mark each trajectory. No per-evaluation timing or scheduling task is introduced: first-call timing occurs once, subsequent calls only increment trajectory-local RHS/atmosphere counters.

- **Dispatch:** executor entry to earliest factory entry. Includes first worker readiness and scheduling; **not** time until every worker is created. Later worker startup can overlap propagation.
- **Active envelope:** earliest factory entry to latest RHS destruction. Includes construction, propagation, local destruction, idle time and overlapping later startup; not a pure warm-pool steady-state measurement.
- **Join/return:** latest destruction to executor return.
- **Per-trajectory propagation:** first RHS call to RHS destructor entry. Excludes that trajectory's startup/construction; includes adaptive integration and output publication. These series show core execution separately from startup.
- **Zero-RHS controls:** identical batch schedule `{0}`. Still construct RHS and validated initial output; therefore measure total executor + setup + allocation + dispatch/join overhead, not pure thread creation.
- **Isolated startup probe:** creates 0/1/2/4/8 `std::jthread`s, waits until every thread reaches a gate, releases and joins; 20 warm-ups and 200 recorded repetitions per count, counts interleaved. Creation/readiness and release/join costs are separately reported. Gate coordination adds overhead. Do not subtract this probe from production timings: real startup overlaps work and has a different synchronization path.

The API intentionally has no persistent executor, hence no warm persistent-pool throughput claim. Total end-to-end latency is the acceptance metric; component observations explain overhead without pretending startup is zero.

## Metrics and statistical interpretation

Raw evidence retains wall/CPU times, trajectories/s, process peak RSS, per-trajectory runtimes and evaluation counts, worker busy/span series, dispatch/active/join decomposition, and full execution order. CPU utilization is process CPU seconds / wall seconds: 1.0 means one busy logical CPU, not 100% of the host. Peak RSS is the process-lifetime high-water mark (`ru_maxrss`), not an isolated per-configuration allocation measurement. Before/after RSS includes allocator retention.

Speedup is matched sequential **median** batch wall time / parallel median wall time; sequential is an explicit loop over `propagate()`, not the batch inline path. Parallel efficiency divides by configured workers; requests greater than batch size are still reported as requested, with active-bound efficiency separately available. Five repetitions with one warm-up per configuration; median, min, max and coefficient of variation retained. No single timing difference is conclusive; min/max are observed spread, not confidence intervals.

Historical static and dynamic configurations used the same thread counts, epoch, physics, input states and output schedule. Configuration order was seeded-shuffled and recorded; repetitions for each configuration were contiguous, so thermal/time-order effects remain a limitation. Deterministic perturbations use a recorded SplitMix64 seed and uniform half-widths. Atmospheric results are never cached.

Diagnostics retain existing SPICE counters: UTC/orientation hits/misses/evictions, Sun acquisition, kernel invalidation and mutex acquisition/contended/acquisition-time. Private native adapter copies record acquisitions, contention, acquisition elapsed and held elapsed per model. Acquisition intervals include clock/try-lock overhead, not pure blocked time; held intervals include measurement bookkeeping. Aggregate thread wait time may exceed wall time. These counters support bottleneck ranking, not uninstrumented absolute cost claims.

`perf` is not installed on this host; hardware cache-miss, memory-bandwidth and false-sharing claims cannot be established. Memory locality and false sharing remain hypotheses.

## Reproduction entry points

Configure the standalone `docs/multithreading` CMake root with explicit locally acquired CSPICE, native source and dependency paths. Build targets `mt_validation`, `mt_benchmark`, `mt_benchmark_diagnostics`, `mt_startup`. Root normal regression tests remain available separately with `BUILD_TESTING=ON`.

`run.py --evidence NEW_DIR --label UNIQUE_LABEL -- COMMAND ...` records exact command, working directory, source revision, affinity, wall time, stdout/stderr and return code. `--test-environment` explicitly sets required regression fixture paths from `--deps`; it does not acquire or replace inputs. Existing output files are not overwritten.

Raw executed commands are authoritative in [evidence/manifest.json](evidence/manifest.json) and individual `*-command.json` records. Matrix size/worker/scheduling settings, durations, perturbations and compiler flags are listed in [PARAMETERS.md](PARAMETERS.md). Full scaling tables and workload verdicts are derived only after the recorded measurements.

### Exact workload commands

From the repository root, with pre-acquired dependencies:

```bash
ev=/tmp/mt001-reproduction
deps=/tmp/leo-atmosphere-deps
build=build-multithreading
rec=(python3 docs/multithreading/run.py --evidence "$ev")
common=(--lsk "$deps/naif0012.tls" --spk "$deps/de432s.bsp"
  --pck "$deps/earth_000101_270105_261009.bpc" --frame ITRF93
  --weather "$deps/SpaceWeather-All-v1.2_snapshot_20200224.txt"
  --solfsmy "$deps/SOLFSMY.TXT" --dtcfile "$deps/DTCFILE.TXT"
  --epoch-utc 2003-10-30T00:00:00)
A=docs/numerical-audit/experiments/CASE-A-v1.cfg
B=docs/numerical-audit/experiments/CASE-B-v1.cfg
C=docs/numerical-audit/experiments/CASE-C-v1.cfg
"${rec[@]}" --label configure -- cmake -S docs/multithreading -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  '-DCMAKE_PREFIX_PATH=/tmp/leo-deps/usr;/tmp/leo-deps/catch-install' \
  -DCSPICE_ROOT=/tmp/leo_cspice/cspice -DLEO_NRLMSISE00_ROOT="$deps" \
  -DLEO_JB2008_SOURCE="$deps/JB2008.f" \
  -DCMAKE_Fortran_COMPILER=/tmp/leo-fortran/usr/bin/gfortran-13 -DBUILD_TESTING=OFF
"${rec[@]}" --label build -- cmake --build "$build" --parallel 4

regbuild=build-multithreading-regressions
"${rec[@]}" --label regression-configure -- cmake -S . -B "$regbuild" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  '-DCMAKE_PREFIX_PATH=/tmp/leo-deps/usr;/tmp/leo-deps/catch-install' \
  -DLEO_ENABLE_CSPICE=ON -DLEO_ENABLE_NRLMSISE00=ON -DLEO_ENABLE_JB2008=ON \
  -DCSPICE_ROOT=/tmp/leo_cspice/cspice -DLEO_NRLMSISE00_ROOT="$deps" \
  -DLEO_NRLMSISE00_REFERENCE_DOCUMENTATION="$deps/NRLMSISE-DOCUMENTATION" \
  -DLEO_JB2008_SOURCE="$deps/JB2008.f" \
  -DCMAKE_Fortran_COMPILER=/tmp/leo-fortran/usr/bin/gfortran-13
"${rec[@]}" --label regression-build -- cmake --build "$regbuild" --parallel 4
"${rec[@]}" --label regressions --test-environment --deps "$deps" -- \
  ctest --test-dir "$regbuild" --output-on-failure --parallel 4

"${rec[@]}" --label correctness-long -- "$build/mt_validation" \
  --validation-out "$ev/validation-long/correctness.json" "${common[@]}" \
  --validation-traces off --validation-input-traces off \
  --case "$A" --case "$B" --case "$C" --duration 21600 \
  --cadence 30 --rtol 1e-10 --max-step 15
"${rec[@]}" --label correctness-wide -- "$build/mt_validation" \
  --validation-out "$ev/validation-wide/correctness.json" "${common[@]}" \
  --case "$A" --case "$B" --case "$C" --duration 300 --cadence 30 \
  --rtol 1e-10 --max-step 15 --validation-batch 16 \
  --validation-workers 1,2,4,8,16 --validation-wide off \
  --validation-perturbed off --validation-oversized off \
  --validation-ap-history-matrix off

affinity=0,2,4,6,8,10,12,14
"${rec[@]}" --label matrix -- taskset -c "$affinity" "$build/mt_benchmark" \
  --benchmark-out "$ev/benchmarks.json" "${common[@]}" --case "$A" \
  --benchmark-duration 300 --repetitions 5 --seed 20261012 \
  --benchmark-sizes 1,2,4,8,16,32,64 --benchmark-workers 1,2,4,8 \
  --benchmark-variants identical,perturbed
"${rec[@]}" --label confirmation -- taskset -c "$affinity" "$build/mt_benchmark" \
  --benchmark-out "$ev/confirmation.json" "${common[@]}" --case "$A" --case "$B" \
  --benchmark-duration 21600 --repetitions 5 --seed 20261012 \
  --benchmark-sizes 8 --benchmark-workers 1,2,4,8 \
  --benchmark-variants identical,perturbed
"${rec[@]}" --label diagnostics -- taskset -c "$affinity" "$build/mt_benchmark_diagnostics" \
  --benchmark-out "$ev/diagnostics.json" "${common[@]}" --case "$A" \
  --benchmark-duration 300 --repetitions 3 --seed 20261012 \
  --benchmark-sizes 8,32 --benchmark-workers 1,2,4,8 \
  --benchmark-variants identical,perturbed
"${rec[@]}" --label startup -- taskset -c "$affinity" "$build/mt_startup" "$ev/startup.json"
"${rec[@]}" --label analyse -- python3 docs/multithreading/analyse.py \
  --matrix "$ev/benchmarks.json" --confirmation "$ev/confirmation.json" \
  --diagnostics "$ev/diagnostics.json" --out "$ev/scaling.json"
python3 docs/multithreading/manifest.py --evidence "$ev" --build "$build" \
  --regression-build "$regbuild" \
  --baseline docs/multithreading/evidence/historical-hashes.json
```

Run timing commands sequentially without concurrent validation/build work. These defaults select all four compiled benchmark workloads. Reproduction on fewer available CPUs requires removing unsupported counts and choosing a valid affinity mask; record that deviation and do not compare unlike hardware masks as if identical.

Manifest generation builds fresh correctness/constraint indexes from the actual
accepted scientific documents and regression record, verifies existing indexes
against their raw hashes, and rejects failed scientific/regression inputs.
The committed pre-edit historical hash index is the preservation reference;
the starting core-source hashes are retained in `evidence/constraints.json`.
The manifest includes its exact generation command and is generated before the
documentation commit to avoid a self-referential Git/hash cycle.

The retained regression build is `build-atmosphere-gcc`. Reproduce its tests using the source root with `BUILD_TESTING=ON`, the same explicit dependency/native flags above, and `run.py --test-environment -- ... ctest --test-dir BUILD --output-on-failure --parallel 4`. Its exact configure/build cache and executed regression commands are retained in the manifest rather than implying system-installed dependencies.

## Complete measured scaling tables

Speedup = matched sequential median / batch median. S = static partition; D = dynamic. Counts are configured workers; when N is smaller, created workers are clamped to N. W=1 is inline, not multithreading. Five repetitions per production configuration. Full distributions, throughput, efficiency, CPU, memory, counts and timings remain in `evidence/scaling.json`.

### CASE-A, 300 s per trajectory

#### two_body_j2 / identical

| N | Sequential ms | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.148 | 0.998 | 0.995 | 0.452 | 0.516 | 0.528 | 0.539 | 0.505 | 0.420 |
| 2 | 0.312 | 1.047 | 1.014 | 0.992 | 0.945 | 0.966 | 0.647 | 0.949 | 0.872 |
| 4 | 0.600 | 0.981 | 0.950 | 1.299 | 1.215 | 1.271 | 1.183 | 1.342 | 1.348 |
| 8 | 1.197 | 0.942 | 0.987 | 1.386 | 1.263 | 2.075 | 1.990 | 1.180 | 1.977 |
| 16 | 2.393 | 0.977 | 0.978 | 1.495 | 1.686 | 2.716 | 2.639 | 2.559 | 3.261 |
| 32 | 5.726 | 1.192 | 1.012 | 1.795 | 2.095 | 3.812 | 3.444 | 4.361 | 5.169 |
| 64 | 10.256 | 0.995 | 1.013 | 2.005 | 1.754 | 3.512 | 3.606 | 3.241 | 5.263 |

#### two_body_j2 / perturbed

| N | Sequential ms | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.144 | 0.842 | 0.842 | 0.540 | 0.458 | 0.493 | 0.450 | 0.518 | 0.414 |
| 2 | 0.286 | 0.848 | 0.953 | 0.780 | 0.885 | 0.868 | 0.832 | 0.880 | 0.815 |
| 4 | 0.583 | 0.934 | 0.666 | 1.236 | 1.205 | 1.300 | 1.431 | 1.388 | 1.382 |
| 8 | 1.156 | 0.798 | 1.007 | 1.380 | 1.470 | 1.966 | 1.761 | 1.233 | 1.509 |
| 16 | 2.306 | 0.847 | 0.943 | 1.402 | 1.490 | 2.361 | 2.767 | 2.494 | 2.273 |
| 32 | 4.685 | 0.908 | 1.016 | 1.739 | 1.697 | 2.228 | 2.960 | 3.185 | 4.110 |
| 64 | 10.133 | 1.010 | 1.091 | 1.868 | 1.897 | 3.519 | 3.620 | 3.301 | 5.483 |

#### full_exponential / identical

| N | Sequential ms | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 2.944 | 1.046 | 0.899 | 0.937 | 0.997 | 0.970 | 0.943 | 0.940 | 0.937 |
| 2 | 5.671 | 0.978 | 0.808 | 0.329 | 0.245 | 0.339 | 0.355 | 0.340 | 0.374 |
| 4 | 11.489 | 1.003 | 1.015 | 0.370 | 0.330 | 0.353 | 0.387 | 0.346 | 0.374 |
| 8 | 23.224 | 1.042 | 1.039 | 0.338 | 0.344 | 0.356 | 0.343 | 0.333 | 0.344 |
| 16 | 45.616 | 1.022 | 1.012 | 0.326 | 0.330 | 0.358 | 0.347 | 0.316 | 0.306 |
| 32 | 89.783 | 1.002 | 0.986 | 0.344 | 0.327 | 0.342 | 0.339 | 0.305 | 0.308 |
| 64 | 181.463 | 1.006 | 0.981 | 0.335 | 0.328 | 0.340 | 0.346 | 0.319 | 0.317 |

#### full_exponential / perturbed

| N | Sequential ms | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 2.960 | 1.061 | 1.056 | 0.949 | 0.795 | 0.947 | 0.969 | 0.946 | 0.958 |
| 2 | 6.194 | 1.095 | 1.009 | 0.379 | 0.410 | 0.358 | 0.367 | 0.342 | 0.355 |
| 4 | 11.290 | 0.999 | 0.981 | 0.328 | 0.335 | 0.347 | 0.358 | 0.362 | 0.336 |
| 8 | 23.739 | 1.028 | 1.028 | 0.339 | 0.364 | 0.358 | 0.356 | 0.334 | 0.335 |
| 16 | 45.148 | 0.912 | 1.008 | 0.329 | 0.319 | 0.347 | 0.342 | 0.311 | 0.309 |
| 32 | 92.356 | 1.023 | 1.017 | 0.341 | 0.333 | 0.344 | 0.353 | 0.297 | 0.320 |
| 64 | 182.721 | 1.002 | 1.009 | 0.336 | 0.328 | 0.347 | 0.346 | 0.307 | 0.316 |

#### full_nrlmsise00 / identical

| N | Sequential ms | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 12.741 | 0.996 | 0.913 | 0.951 | 0.975 | 0.958 | 0.966 | 0.975 | 0.972 |
| 2 | 25.716 | 1.004 | 1.011 | 0.318 | 0.309 | 0.327 | 0.350 | 0.349 | 0.345 |
| 4 | 51.361 | 0.981 | 0.989 | 0.342 | 0.310 | 0.387 | 0.400 | 0.375 | 0.362 |
| 8 | 104.898 | 0.973 | 1.016 | 0.328 | 0.334 | 0.393 | 0.391 | 0.391 | 0.383 |
| 16 | 209.755 | 1.012 | 1.008 | 0.315 | 0.321 | 0.379 | 0.393 | 0.384 | 0.392 |
| 32 | 415.867 | 0.985 | 1.010 | 0.312 | 0.317 | 0.362 | 0.365 | 0.381 | 0.379 |
| 64 | 828.825 | 0.996 | 0.998 | 0.313 | 0.312 | 0.346 | 0.354 | 0.377 | 0.378 |

#### full_nrlmsise00 / perturbed

| N | Sequential ms | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 13.074 | 1.021 | 1.015 | 1.000 | 0.995 | 1.008 | 1.004 | 0.900 | 0.983 |
| 2 | 26.441 | 1.048 | 1.034 | 0.323 | 0.329 | 0.329 | 0.332 | 0.286 | 0.314 |
| 4 | 50.945 | 0.975 | 0.974 | 0.313 | 0.295 | 0.323 | 0.349 | 0.323 | 0.316 |
| 8 | 103.445 | 0.980 | 1.006 | 0.309 | 0.311 | 0.331 | 0.323 | 0.339 | 0.336 |
| 16 | 205.859 | 1.009 | 1.014 | 0.307 | 0.321 | 0.330 | 0.319 | 0.329 | 0.328 |
| 32 | 408.019 | 0.993 | 1.000 | 0.318 | 0.299 | 0.322 | 0.324 | 0.325 | 0.324 |
| 64 | 826.963 | 1.011 | 1.010 | 0.306 | 0.306 | 0.329 | 0.326 | 0.327 | 0.325 |

#### full_jb2008 / identical

| N | Sequential ms | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 17.138 | 0.996 | 1.008 | 0.979 | 0.911 | 0.913 | 0.980 | 0.983 | 0.936 |
| 2 | 34.504 | 1.011 | 0.885 | 0.404 | 0.428 | 0.437 | 0.449 | 0.440 | 0.429 |
| 4 | 70.795 | 1.035 | 1.034 | 0.433 | 0.441 | 0.527 | 0.508 | 0.517 | 0.510 |
| 8 | 139.252 | 0.985 | 1.010 | 0.440 | 0.444 | 0.510 | 0.500 | 0.471 | 0.466 |
| 16 | 275.373 | 1.000 | 1.005 | 0.435 | 0.436 | 0.501 | 0.502 | 0.459 | 0.574 |
| 32 | 556.832 | 1.005 | 1.000 | 0.432 | 0.453 | 0.510 | 0.512 | 0.471 | 0.472 |
| 64 | 1108.714 | 1.002 | 0.998 | 0.432 | 0.438 | 0.506 | 0.505 | 0.469 | 0.465 |

#### full_jb2008 / perturbed

| N | Sequential ms | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 18.302 | 0.990 | 1.079 | 1.037 | 0.989 | 1.017 | 1.049 | 1.048 | 1.038 |
| 2 | 35.661 | 1.046 | 1.049 | 0.416 | 0.391 | 0.410 | 0.399 | 0.414 | 0.410 |
| 4 | 67.925 | 0.963 | 0.954 | 0.402 | 0.386 | 0.434 | 0.426 | 0.444 | 0.422 |
| 8 | 138.313 | 1.016 | 1.010 | 0.394 | 0.398 | 0.438 | 0.429 | 0.389 | 0.403 |
| 16 | 275.105 | 0.983 | 1.010 | 0.394 | 0.402 | 0.435 | 0.437 | 0.403 | 0.406 |
| 32 | 549.261 | 0.995 | 0.997 | 0.396 | 0.394 | 0.434 | 0.433 | 0.395 | 0.395 |
| 64 | 1106.683 | 0.999 | 0.995 | 0.395 | 0.398 | 0.438 | 0.435 | 0.393 | 0.402 |

### Six-hour confirmation, N=8

#### CASE-A / identical

| Workload | Sequential s | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| two_body_j2 | 0.085427 | 1.011 | 1.007 | 1.940 | 1.980 | 3.520 | 3.343 | 4.692 | 5.519 |
| full_exponential | 1.640906 | 0.997 | 1.004 | 0.329 | 0.335 | 0.345 | 0.344 | 0.313 | 0.312 |
| full_nrlmsise00 | 7.423286 | 1.003 | 0.997 | 0.309 | 0.304 | 0.329 | 0.328 | 0.355 | 0.356 |
| full_jb2008 | 9.953187 | 1.005 | 0.999 | 0.432 | 0.436 | 0.491 | 0.494 | 0.448 | 0.448 |

#### CASE-A / perturbed

| Workload | Sequential s | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| two_body_j2 | 0.085534 | 1.021 | 0.979 | 1.980 | 1.904 | 3.432 | 3.061 | 4.759 | 4.690 |
| full_exponential | 1.629962 | 1.004 | 0.990 | 0.330 | 0.333 | 0.343 | 0.345 | 0.311 | 0.310 |
| full_nrlmsise00 | 7.415637 | 0.997 | 0.990 | 0.307 | 0.307 | 0.326 | 0.327 | 0.323 | 0.324 |
| full_jb2008 | 9.834437 | 0.940 | 0.996 | 0.388 | 0.387 | 0.432 | 0.431 | 0.395 | 0.395 |

#### CASE-B / identical

| Workload | Sequential s | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| two_body_j2 | 0.013301 | 1.066 | 1.065 | 1.984 | 1.958 | 3.369 | 3.724 | 4.803 | 3.600 |
| full_exponential | 0.312187 | 0.998 | 1.046 | 0.349 | 0.342 | 0.370 | 0.359 | 0.326 | 0.322 |
| full_nrlmsise00 | 1.501332 | 0.993 | 0.987 | 0.305 | 0.307 | 0.337 | 0.336 | 0.374 | 0.364 |
| full_jb2008 | 6.409540 | 0.998 | 0.997 | 0.431 | 0.430 | 0.497 | 0.494 | 0.457 | 0.459 |

#### CASE-B / perturbed

| Workload | Sequential s | W1 S | W1 D | W2 S | W2 D | W4 S | W4 D | W8 S | W8 D |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| two_body_j2 | 0.014147 | 1.025 | 1.010 | 1.871 | 1.938 | 3.115 | 3.274 | 5.307 | 3.602 |
| full_exponential | 0.289748 | 1.003 | 1.001 | 0.330 | 0.326 | 0.342 | 0.340 | 0.310 | 0.306 |
| full_nrlmsise00 | 1.450184 | 0.935 | 0.992 | 0.307 | 0.282 | 0.335 | 0.333 | 0.340 | 0.339 |
| full_jb2008 | 6.226192 | 0.999 | 0.998 | 0.387 | 0.388 | 0.428 | 0.428 | 0.395 | 0.395 |
