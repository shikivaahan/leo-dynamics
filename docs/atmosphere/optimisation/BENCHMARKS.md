# ATM-OPT-001 benchmark protocol

## Comparison builds

The unchanged original scientific libraries and experiment come from detached revision `6a1ad59`. The driver is compiled against those original interfaces with every candidate API excluded. The candidate build uses the same dependencies, compiler, Release flags, kernels, weather, scenarios and solver settings. Source hashes include production headers/sources, the experiment, bench support, scenario loader and CASE-A/B/C configuration files.

Production speedups use only the uninstrumented original and candidate executables. Separate diagnostic processes measure call counts, cache events, actual cache lookup intervals and mutex contention. Original diagnostic interposition wraps the four real CSPICE C entry points and the original SPICE mutex using GNU linker wrapping; no original source file is changed. The mutex is identified by the last acquired pthread mutex observed at the actual protected CSPICE call, and inconsistent identity aborts rather than guessing. Candidate diagnostics use the production counter macro and a generated private source copy for lookup timing. Instrumented wall times are retained but never substituted for production timings.

The original does not have caches: zero cache occupancy/hits is real absence. Counters in an uninstrumented binary are unavailable, not measured zero. The reducer takes baseline call/wait counts from the explicitly identified original diagnostic binary; unavailable diagnostics are null in summaries.

## Configurations

| Configuration | UTC | Sun sharing | Orientation |
|---|---|---|---|
| Unchanged original | Off | Off | Off |
| Candidate off/off/off control | Off | Off | Off |
| UTC only | On | Off | Off |
| Sun only | Off | On | Off |
| Orientation only | Off | Off | On |
| UTC + Sun | On | On | Off |
| UTC + Orientation | On | Off | On |
| Sun + Orientation | Off | On | On |
| All | On | On | On |

Each candidate configuration is exercised in provider, locality, 1/2/4-thread and six-hour propagation workloads. Combined results are measured directly; independent percentages are never added. Candidate off/off/off distinguishes interface/lock-scope overhead from saved work versus the unchanged source.

## Workloads and timing scopes

| Workload | Measurement | Controls / locality |
|---|---|---|
| Fixed provider | Full atmospheric evaluation latency, raw calls and batches | Compatible Sun is prepared outside timing for candidate provider calls; original still performs its independent lookup. Sharing-disabled candidate obtains Sun internally. This measures provider latency **given already-available environmental input**, not the cost of producing that input |
| Controlled locality | Sun acquisition plus full provider latency | Ten deterministic ATM-PROF-001-style schedules; changing positions/epochs, weather boundaries, UTC midnight, epoch-major and trajectory-major eight-trajectory ensembles |
| Concurrent provider | Actual 1/2/4-thread throughput, per-call latency, wall/CPU costs | Immutable pre-acquired exact-epoch Sun shared across readers; barrier excludes warm-up from counters/timing; original and candidate retain required locks |
| CASE-A/B/C propagation | Six-hour wall time, full RHS and provider latency distributions, scientific checksums | Production Dopri5, same force models/state/settings; RHS already needs Sun for SRP/eclipse and offers the same observation to JB2008 |
| UTC hit | Full UTC hit-path latency | Includes per-request et2utc, original parsing and quasi-MJD arithmetic, locks and lookup |
| UTC miss | Full UTC miss-path latency | Civil-noon epochs on exact distinct dates prepared by real `epoch_from_utc` outside timing, advancing beyond capacity; diagnostic counts confirm misses |
| Orientation hit/miss | Full rotation-helper latency | Fixed exact ET vs unique half-second ET requests; no quantisation/interpolation |
| Sun acquisition | `observe_sun` vs `geometric_state` latency | Neither is ephemeris caching; sharing avoids the downstream duplicate call |
| Cache search attribution | Actual UTC search and orientation hash/search intervals | Private diagnostic copy, already-held SPICE mutex; end timestamp overhead retained. Summed durations/counts provide per-batch mean search cost |
| Invalidation | Explicit cache clearing/generation advancement latency and result invariance | 2,000 calls; not kernel disk I/O/loading time |

Fixed provider/micro/concurrency: five batches × 2,000 calls, 256 warm-up calls. Locality: three batches × 1,024 requests. Propagation: three repeats for every case/model/configuration in each binary. Raw full RHS/provider timings use 2^21 preallocated entries per run; overflow throws. Timer reads and preallocated assignments/checksums remain inside measured full propagation; that added capture differs from the historical profiling harness and is applied identically to the fresh original/candidate comparison. Native evaluation is fresh on every call.

Cache search attribution is distinct from saved work. The difference between no-reuse and hit-path helper timings is the removed work minus added reuse overhead, **not** an isolated lookup cost. Exact search counters are timer-inclusive diagnostic means; full hit/miss helper paths retain raw distributions, medians, means and tails. No theoretical ceiling is presented as a measured speedup.

## Locality and cache-scope decision

Locality measurements explicitly invalidate candidate caches before each batch, then warm only the first scheduled request. Key/position preparation is outside timing and cannot prepopulate the measured batch. Shared replay starts from the same first-request warm state and uses the exact recorded civil dates and ET bits, requested frame, production hash and FIFO replacement. The reducer requires measured/shared-replayed UTC **and** orientation hit rates to agree before publishing a summary.

The benchmark-only thread-local prototype splits the same total UTC32/orientation256 capacity across supported 1/2/4 threads and assigns requests round-robin. Each simulated stream warms its first request. Other thread counts are rejected to avoid unequal-capacity comparisons. Replay measures hit/miss/eviction locality, not an implemented alternative's mutex-free throughput. Epoch-major versus trajectory-major scheduling is measured separately; it is not assumed to preserve the same locality.

Production keeps the shared cache because correctness is anchored to the existing process-global SPICE mutex and the measured workloads can exploit cross-trajectory exact reuse. The final retention decision uses actual single-/multi-thread gains and contention, not an assumption that globally shared caching always wins. Different scheduling/locality can change that verdict; raw data are retained for reanalysis.

## Statistics and host limits

Per-call distributions retain mean, median, p95/p99, extrema and sample standard deviation. Percentiles interpolate at q(n−1). Batch mean/wall distributions and raw samples expose variability; the reported relative spread is SD/abs(mean), not a confidence interval. Three propagation repeats produce descriptive tails, not a statistically precise population p99.

Single-thread workloads are pinned to logical CPU0; concurrent workloads share CPUs0–3. WSL2 scheduling, physical-core placement, frequency and hypervisor load are not controlled. This is not a dedicated-core experiment. Original/candidate/diagnostic processes execute serially, after build/test completion. Kernels/weather and scientific settings match ATM-PROF-001; exact current environment is recorded rather than assumed identical to the historical run.

The runner refuses populated output directories, stops on failed commands or mismatched scientific checksums, and rechecks production/historical hashes. Large raw JSON is losslessly compressed after scientific comparison. A failed tooling run is not a valid scientific/performance result; corrected completed measurements are identified separately in the manifest. Full bitwise scientific/native-stream evidence is in VALIDATION.md, not inferred from timing checksums alone.

Reproduction commands are in README.md; exact executable arguments, raw counts, affinity, build caches, binary/dependency/kernel hashes and output hashes are in `evidence/manifest.json` and the benchmark command log.
