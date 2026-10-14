# End-to-end performance — protocol and why no speedup is claimed

## Protocol

- Uninstrumented Release build (`-O3 -DNDEBUG`, GCC 13.3.0, C++20, no fast-math) linking the exact
  production libraries.
- One configuration per measured workload; 7 recorded repeats in a deterministic rotation with one
  discarded warm-up, so no configuration always runs first.
- Timed region: cache construction plus propagation. Traces, counters, checksums and cache
  statistics are read after the clock stops.
- Speedup is the median ratio of interleaved repeats of the same workload. It is never formed by
  subtracting independently measured medians.
- Benchmark groups run exclusively: a group starts only when no other child is alive, so a scientific
  sweep can never contaminate a timing.
- Process-isolated workloads reuse MT-002's orchestration — contiguous balanced ranges, one child per
  range, children concurrent within the group — and the parent measures the group's wall time
  including launch, kernel/weather loading, propagation, output and the MT-002 binary merge. Child
  times are never summed into a throughput figure.
- Each process owns its own cache. Reuse inside a process is real reuse; reuse between processes is
  reported as unrealisable for a process-local cache, never merged into an aggregate hit rate.

## Reported metrics

Provider latency, RHS latency, trajectory wall time, batch throughput, speedup versus the current
fully optimised direct provider, native-model calls avoided, hit/miss and fallback counts, cache
construction cost, cache payload bytes, process peak RSS, and per-model breakdowns.

## What the verification run produced

A complete both-model benchmark executed on nonprimary inputs with 30-second smoke propagations
(`evidence/performance-summary.json`). Individual configuration medians ranged from roughly 1 ms to
55 ms wall time and the computed ratios swung either side of 1 across repeats — the spread of a
sub-second measurement on an uncontrolled WSL2 host with no frequency or core-isolation control.

Those numbers are recorded **only** as proof that the benchmark path executes, is exclusive, is
interleaved and is hash-traced. They are labelled `pipeline-verification-only` and are not a speedup,
not a Pareto point, and not evidence for or against any cache.

## Unmeasured

The headline comparison — shortlisted candidates against the optimised direct provider for single
trajectories, sequential ensembles, threaded ensembles and MT-002 process-isolated batches, on pinned
inputs and six-hour/24-hour windows — is blocked with the primary study and recorded as pending.
Baseline provider/native-model/preprocessing costs for the current configuration were likewise
planned and not executed.