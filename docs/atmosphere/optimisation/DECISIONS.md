# ATM-OPT-001 decisions

Performance values and retention verdicts are in RESULTS.md and machine-readable `evidence/measurements/summary.json`; correctness details are in VALIDATION.md. The following records describe actual implemented decisions, not speculative optimisation ceilings.

## OPT-D01 — Exact civil dates, never ET buckets

- **Problem:** every UTC request repeated two SPICE midnight conversions although metadata depends only on civil date/configuration.
- **Alternatives:** approximate ET/86400 buckets; one last-value day; bounded civil-date metadata.
- **Chosen:** exact per-request `et2utc_c("ISOC",9)` followed by a 32-date FIFO keyed by civil serial date and managed generation.
- **Rationale:** preserves leap seconds, original parsing/error behavior, quasi-MJD operation order and backward solver queries. Stores midnight ETs, rounded day length, serial date and day-of-year; never stores density or request-specific time.
- **Source:** `src/environment/spice.cpp::utc_calendar`, `CivilDay`; `include/leo/environment/spice.hpp`.
- **Performance effect:** UTC-only and UTC-combined full-provider/RHS/propagation measurements; actual str2et reduction and search timing recorded separately.
- **Correctness:** all fixtures/series exact, leap-second/day/month/year/backward/eviction/reload tests.
- **Limits:** external unreported SPICE time configuration mutation is unsupported; dates not in the bounded FIFO miss exactly.

## OPT-D02 — Shared managed generation and linearization lock

- **Problem:** CSPICE kernel/time state is process-global; stale reuse cannot survive known lifecycle mutations.
- **Alternatives:** provider-local generations; unsynchronized global last-value state; optimistic hits outside SPICE locking; shared managed generation.
- **Chosen:** invalidate UTC/orientation and observation provenance before every actual managed load/unload attempt under the existing SPICE mutex, including failed/partial loads and cleanup. Refcount-only ownership changes retain the generation. Quiescent external notification advances it explicitly.
- **Rationale:** one ownership/lifecycle convention; no obsolete hit can linearize after mutation. Clears valid entries and FIFO indices. Generation overflow terminates rather than reviving old identity.
- **Source:** `invalidate_locked`, constructor/destructor, `release_kernel`, `invalidate_reuse` in `src/environment/spice.cpp`.
- **Performance effect:** explicit invalidation microbenchmark, counts and fixed memory costs; no kernel I/O timing hidden in lookup costs.
- **Correctness:** concurrent readers with managed load/unload, last-owner unload/reload, multiple provider lifetimes and stale observation rejection.
- **Limits:** individual calls are linearizable, not a whole native evaluation/trajectory across a changing configuration. Coherent propagation requires fixed configuration. Arbitrary external mutation cannot be detected.

## OPT-D03 — Keep non-SPICE UTC work outside the critical section

- **Problem:** civil parsing/date arithmetic need no SPICE state and needlessly extend a process-global critical section.
- **Alternatives:** retain the original full-function lock; parse unlocked without checking configuration; unlock with generation retry.
- **Chosen:** unlock parsing/date formatting/final arithmetic and recheck generation before cache access/conversions; restart conversion if changed.
- **Rationale:** preserves operation-level configuration identity without locking non-SPICE work. Duplicate miss conversion is allowed when another reader fills the same day during formatting; insertion avoids duplicate entries.
- **Source:** `SpiceEphemerisProvider::utc_calendar`.
- **Performance effect:** candidate off/off/off measures changed lock-scope overhead against untouched original; 1/2/4-thread diagnostic acquisition/contention/wait counts quantify cost.
- **Correctness:** generation checks under the lock and concurrent lifecycle boundary tests; no adaptive call-order assumption.
- **Limits:** extra acquisitions on a miss; retry count can grow under sustained managed reconfiguration. Tests do not deterministically force each possible interleaving; the source review found no blocking race.

## OPT-D04 — Opaque immutable Sun observation, not an SRP dependency

- **Problem:** JB2008 independently requests the same Sun state already acquired by RHS eclipse/SRP acquisition.
- **Alternatives:** pass an unlabelled vector; have JB2008 call SRP; provider-wide last-Sun state; typed immutable per-evaluation observation.
- **Chosen:** provider-only `SunObservation` with const epoch/state/generation, explicit optional input and independent fallback.
- **Rationale:** construction fixes Sun10/Earth399/J2000/NONE/SI; acquisition/stamp occur atomically under the SPICE mutex. Exact ET bits/current configuration checked. No SRP object, enabled force or integrator ordering is required. Other atmospheric backends ignore the optional Sun.
- **Source:** `SunObservation`, `observe_sun`, `compatible_sun`; `DirectAtmosphere::evaluate`; environmental acquisition in `docs/atmosphere/experiment.cpp`.
- **Performance effect:** Sun-only/combined measurements and actual original/candidate spkez counters demonstrate duplicate-call elimination; standalone evaluation remains independently measured.
- **Correctness:** byte-identical actual JB2008 bridge input/output streams; compatible/shared/standalone/stale/wrong-epoch/different-position/concurrent tests; SRP-enabled propagation and SRP-absent shared test.
- **Limits:** compatibility adds one existing-mutex acquisition. Wrong geometry is rejected structurally, not through runtime user-supplied labels. Observation lifetime must cover the call.

## OPT-D05 — Process-wide Sun provenance permits equivalent providers

- **Problem:** providers all observe one SPICE kernel pool; provider identity does not by itself establish isolated ephemeris configuration.
- **Alternatives:** pointer identity (with lifetime/reuse hazards); unique provider ID; process configuration identity.
- **Chosen:** exact global managed generation plus exact ET, independent of which provider originated the observation.
- **Rationale:** same current kernel configuration is defensible provenance across providers and survives destruction of a refcount-only owner. Loading/unloading another actual kernel makes previous observations incompatible.
- **Source:** `compatible_sun`; provider ownership/refcount code.
- **Performance effect:** no per-observation allocation, registry or ownership token; measured compatibility cost included in Sun-sharing provider/RHS timings.
- **Correctness:** cross-provider compatibility/lifetime tests and managed-reconfiguration fallback.
- **Limits:** providers do not isolate SPICE precedence; external mutation contract remains mandatory.

## OPT-D06 — Bounded four-way exact orientation cache

- **Problem:** adaptive/ensemble workloads repeat exact orientation epochs nonmonotonically; an unbounded map would retain unnecessary memory.
- **Alternatives:** one last-value matrix; unbounded hash map; per-provider/TLS caches; bounded shared cache under existing locking.
- **Chosen:** 64 sets × four FIFO ways, 256 matrices, exact ET bits/frame/fixed direction/generation, avalanche hashing.
- **Rationale:** at most four checked entries and no allocation on hit/miss; retains short adaptive-stage locality and cross-trajectory epochs. Fixed J2000-to-Earth API prevents direction aliasing. Disabled path skips hashing/insertion. Actual scope decision uses measured locality/contention plus equal-budget thread-local replay, not assumed global superiority.
- **Source:** `Orientation`, `orientation_set`, `earth_fixed_rotation` in `src/environment/spice.cpp`.
- **Performance effect:** orientation-only/combined timings, real hit/miss/eviction/pxform counts, actual hash/search cost, epoch-major versus trajectory-major schedules.
- **Correctness:** raw direct-CSPICE bitwise matrices, ITRF93/IAU_EARTH, nextafter ETs, eviction/reload/concurrency tests and full scientific comparisons.
- **Limits:** set conflicts cause exact misses, never approximate hits. Round-robin TLS replay is locality evidence, not real alternative parallel throughput. Capacity tuning for every future workload is not claimed.

## OPT-D07 — Independent small controls and separate diagnostics

- **Problem:** effects must be attributable without retaining heavy diagnostic work in production.
- **Alternatives:** global mutable flags/loggers; one combined switch; build-only irreversible cutover; immutable per-provider booleans plus separate diagnostic executables.
- **Chosen:** `EvaluationOptions` copied at construction; static helper reuse booleans; all eight configurations; separate original and candidate diagnostics. Real cache lookup timers exist only in a generated private diagnostic source.
- **Rationale:** no timer/logging/counter increment in normal production; no environment framework or model coupling. All combined effects are measured directly.
- **Source:** `include/leo/atmosphere/provider.hpp`, `docs/atmosphere/optimisation/CMakeLists.txt`, `benchmark.cpp`, `original_diagnostics.cpp`, `instrument_lookup.py`.
- **Performance effect:** published speedups use uninstrumented original/candidate only; instrumentation overhead is never called an optimisation gain.
- **Correctness:** every candidate control is checked against original scientific/native series; diagnostic result checksums also match uninstrumented original.
- **Limits:** GNU linker interposition is an experiment/Linux tool, not a portable production-library requirement; diagnostic mutex try-lock/timer behavior perturbs scheduling.

## OPT-D08 — Exact scientific capture precedes performance interpretation

- **Problem:** cached reuse must not introduce untracked physical/numerical changes; frozen refined data do not contain all desired fields.
- **Alternatives:** accept a tolerance; compare only final-state checksums; change settings to recover agreement; capture complete original/candidate outputs.
- **Chosen:** IEEE-754 comparisons of all 10,710 fixture outputs and scientific series, frozen nominal fields/refined checksums, complete nominal/refined original/candidate temperatures/force/torque/work series, byte comparisons of actual native JB2008 inputs/outputs through linker wrapping.
- **Rationale:** hashes supplement full comparisons, not replace them; no native routine or solver setting is modified. Unavailable historical refined fields are explicitly distinguished from original-source recapture.
- **Source:** `validation.cpp`, `validate.py`, `validate_controls.py`; `evidence/validation` and `evidence/controls`.
- **Performance effect:** scientific capture is not linked into timed executables. All real benchmark calls still evaluate native density fresh.
- **Correctness:** completed raw comparisons and unchanged historical evidence hashes.
- **Limits:** frozen refined full-series/native streams were not historically recorded; their complete comparisons use the unchanged original-source build. Published independent-reference tests retain existing model-validation tolerances, not optimisation tolerances.

## OPT-D09 — Evidence failures are not performance results

- **Problem:** generated benchmark tooling initially failed compile/JSON/runtime checks; partially executed timing data cannot be presented as validated results.
- **Alternatives:** repair malformed raw output; ignore missing sections; accept partial measurements; correct tooling and rerun cleanly.
- **Chosen:** correct the actual code, smoke every benchmark section and both diagnostic interfaces, compare scientific checksums and replayed/measured rates, then run the full serial matrix into a fresh directory. Retain a concise failure log; remove own unusable scaffolds only after successful proof.
- **Rationale:** no fabricated results or narrowed benchmark; frozen history untouched. Counter attribution distinguishes actual original instrumentation from unavailable zeros, and UTC miss cost uses exact distinct civil dates.
- **Source:** `benchmark.cpp`, `run.py`, `analyse.py`; `evidence/tooling-failures.json`.
- **Performance effect:** only the completed identified run informs RESULTS.md.
- **Correctness:** real-section smoke, actual baseline CSPICE counts and exact output equality. Source/input/kernel/dependency/output hashes make the completed run reproducible.
- **Limits:** WSL2 variability and limited repeated wall-time samples remain documented; a descriptive p99 is not a precise population-tail estimate.

---

# Closure decisions (2026-10-11)

The three optimisations are **retained unchanged in purpose, behaviour and API**. The closure pass changed one implementation detail (OPT-D10), corrected one documented fact (OPT-D11), and resolved one measurement artefact (OPT-D12). No optimisation was reverted.

## OPT-D10 — Two explicit UTC paths instead of one shared path

- **Problem:** with reuse disabled, `utc_calendar` still paid for a two-phase lock/unlock/relock cycle, a generation sample, a generation recheck and a cache search that could never hit. The all-off control measured 0.81–0.89× of the unchanged original, which is an artefact of the new interface rather than of any optimisation.
- **Alternatives:** accept the regression and document it; keep one unified path; branch on `reuse` and give the disabled path a single-lock transcription of the original conversion; micro-optimise the shared path instead.
- **Chosen:** branch on `reuse`. The disabled path performs the original conversion under one lock with no generation sampling, no unlock/relock and no cache access. The reused path keeps its existing generation-rechecked structure. Both call one shared helper (`convert_calendar_locked`) plus one shared finisher (`finish_calendar`), so the fragile time-conversion logic, error behaviour, leap-second validation and quasi-MJD order exist in exactly one place and cannot drift.
- **Rationale:** the disabled path must be a faithful baseline for A/B measurement. Adding a branch is cheaper than carrying reuse bookkeeping in a path that cannot use reuse, and factoring the conversion avoids duplicating fragile logic. Reuse semantics, generation behaviour, leap-second handling, error types and thread safety are untouched.
- **Source:** `src/environment/spice.cpp` (`convert_calendar_locked`, `finish_calendar`, `utc_calendar`).
- **Performance effect:** measured after the change. The all-off control is now statistically indistinguishable from the unchanged original (see RESULTS.md); the all-on path is unchanged and remains a large, highly significant gain.
- **Correctness evidence:** new `[spice][reuse]` test asserting the disabled path equals the reused path bitwise, never populates the cache, repeats identically, and preserves both invalid-argument behaviours; full regression suite and all-eight-combination equivalence re-verified after the change.
- **Known limitations:** two entry paths must be maintained. The shared helper is what bounds the risk; a future change to conversion semantics must be made in the helper, not in one path.

## OPT-D11 — Cache footprint reported from real `sizeof`, not a prose estimate

- **Problem:** RESULTS.md stated a 26,376-byte total with an incorrect breakdown (26,112 orientation + 256 civil-day), which does not sum to the total and misstated the composition.
- **Alternatives:** keep the estimate; state only the total; verify each component by compiling the actual struct definitions and correct the breakdown.
- **Chosen:** measured the real `sizeof` values by compiling the exact struct definitions from `src/environment/spice.cpp` on this toolchain, and corrected RESULTS.md and PARAMETERS.md to a verified per-component table (48 × 32 = 1,536 civil-day; 96 × 256 = 24,576 orientation; 256 FIFO indices; 8 index). The provider already reports this total from `sizeof` expressions at run time; the retained `cache_bytes` counter matches exactly.
- **Rationale:** documentation must not hard-code unverified struct sizes. Sizes are ABI/compiler dependent, so the run-time `sizeof` sum is authoritative and the prose is explicitly labelled as toolchain-specific.
- **Source:** RESULTS.md §C, PARAMETERS.md, `docs/atmosphere/optimisation/evidence/cleanup-*/…` counter evidence.
- **Performance effect:** none; documentation only. Corrects a reported fact.
- **Correctness evidence:** compiled `sizeof` probe; retained `cache_bytes` counter equals 26,376.
- **Known limitations:** the breakdown is specific to this toolchain/ABI; the total is authoritative everywhere.

## OPT-D12 — The NRLMSISE-00 Sun-toggle timing anomaly was measurement order, not code

- **Problem:** the previous run reported materially different provider times for NRLMSISE-00 across configurations that differ only in `sun_sharing` (UTC only 11,652 ns, UTC + Sun 10,133 ns, UTC + orientation 8,177 ns, All 9,543 ns), even though NRLMSISE-00 consumes no Sun. The cause was not identified in the previous pass.
- **Alternatives:** attribute the differences to a real code path; accept them as unexplained; re-measure with an order-balanced, interleaved protocol and diagnostic operation counts.
- **Chosen:** a new interleaved experiment (separate source, binaries and evidence) that warms every configuration, then executes configurations round-robin inside each repetition with a recorded seed, retains per-repetition batch means and all per-call samples, and records the actual per-call SPICE operation counts for every configuration.
- **Rationale:** the previous protocol ran each configuration's batches as one contiguous block, so a configuration difference was confounded with execution order and drift. Under the interleaved protocol, NRLMSISE-00 performs **zero** `spkez` calls in every configuration (it acquires no Sun at all) and all eight configurations return one identical result checksum. Every Sun-toggle mean difference has a Welch 95% interval spanning zero. The anomaly is therefore measurement variance, and toggling Sun sharing for NRLMSISE-00 changes no operation and no result.
- **Source:** `docs/atmosphere/optimisation/interleave.cpp`, `run_interleave.py`, `evidence/cleanup-interleave/`.
- **Performance effect:** no production change. Supplies the trustworthy provider-latency table used in RESULTS.md and confirms the disabled-path fix.
- **Correctness evidence:** interleaved run requires every configuration to share one result checksum and one checksum equal to the unchanged original; both verified.
- **Known limitations:** 12 repetitions give descriptive distributions and batch-level intervals, not a precise population p99. WSL2 scheduling remains uncontrolled.
