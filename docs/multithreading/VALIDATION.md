# MT-001 correctness validation

Acceptance is **binary64 bitwise equality**, not an orbital/attitude tolerance. No integrator tolerances, equations, atmospheric switches, scientific baseline files or exact-reuse policies are relaxed to obtain a pass.

## Methods and coverage

`validation.cpp` reuses the existing atmospheric experiment's CASE loader, `case_dynamics`, tolerance configuration, schedule generation and shared environment construction. Sequential references invoke unchanged `propagate()` in an explicit loop; concurrent runs invoke `propagate_batch()` with identical inputs. Each trajectory owns its RHS and trace buffers. One stable provider and immutable weather dataset serve the entire process.

For every trajectory, compare raw binary64 bits of epoch, every requested output label and all 13 state components: position, velocity, quaternion and body angular rate. Output ordering is checked by indexed comparison, including distinct perturbed inputs and distinct per-trajectory RHS settings in permanent tests.

Additional comparisons:

- Ordered **actual RHS-stage** traces: elapsed/absolute time, packed input state, packed derivative, Sun, illumination, density, local/exospheric temperature, constituent number densities and availability flags.
- **Reconstructed native inputs**, using the same public preprocessing helpers as the provider: UTC year/day/seconds/day length/MJD, geodetic position, native angles/altitude/LST, daily Ap/history and weather proxies. These are diagnostic recomputations, not an intercepted stream of native-call arguments. NRLMSISE degree conversion uses the provider's exact arithmetic order.
- Output-sample density/input diagnostics and directly re-evaluated drag force and torque, SRP force, total force, attitude angle, angular speed and total torque magnitude. These post-run evaluations never feed the integrated state.
- RHS/atmosphere call counts and repeatability. Checksums are supplemental; acceptance compares each value directly, not just a hash.

Protocol separation: long six-hour runs compare every trajectory/output diagnostic
and call count without per-RHS trace allocation. The 300 s actual-16-worker matrix
compares every RHS stage and reconstructed native input. Both use the same physics
and tolerances. The initial fully traced six-hour attempt timed out after 3600 s
without a summary; it is explicitly excluded from acceptance and recorded in
`evidence/correctness-traced-timeout.json`. No numeric tolerance was substituted.

CASE-A/B are the primary physically representative analytical spacecraft configs; CASE-C is explicitly labelled `numerical_stress`. No new convergence criterion is imposed on CASE-C. Exponential, direct NRLMSISE-00 and direct official JB2008 run without fallbacks. Daily-Ap and native history-Ap groups remain distinct.

## Permanent regressions (current API)

`tests/batch_tests.cpp` adds 11 consumer-visible cases, without introducing SPICE/native fixtures into the normal integration test executable:

- Complete state/time/epoch binary64 equality and original ordering.
- Different worker counts with the single internal dynamic strategy.
- Empty, single-element and oversized-worker batches.
- Zero-worker rejection, including empty input.
- Factory and RHS exception type/payload preservation.
- Deterministic lowest failing input index when several trajectories fail.
- Continue-on-failure completion and no surviving worker activity after return.
- Invalid integration options surfaced through the batch error contract.
- Independent mutable, move-only RHS state and per-trajectory call counts.

Comparison helpers retain actual bit streams, not a hash-only equality assertion. Exception assertions use `trajectory_index()` and the original cause; wrapper-message wording is not pinned.

MT-002 removes the redundant per-policy repetitions and policy-validation section.
`tests/process_batch_tests.py` additionally covers range ownership, complete binary
merge ordering, invalid/mismatched payloads, atomic publication and actual OS-child
cleanup on execution or partial-launch failure. Scientific equality is exercised
with the real executable, not synthetic atmosphere results.

Observed regression result: **184/184 CTest cases passed**, with actual pinned SPICE/native/weather fixture environment, including all existing scientific regression tests. Exact command and output: `evidence/regressions-command.json`.

## Historical MT-001 six-hour scientific run

`evidence/validation-long/correctness.json`: **624/624 comparisons passed; zero
mismatching values**. All CASE-A/B/C and five workloads, identical/perturbed inputs,
both policies, repeatability, oversized configured workers, actual 16-worker
two-body/J2 groups and separate Ap-history groups. Every run compares all 721
output samples per trajectory, epoch, labels, all state components, density/load
diagnostics and evaluation counts against its matched sequential reference.

Across the two accepted protocols: **924/924 scientific comparisons passed**.

| Workload | Accepted comparisons | Mismatching values |
|---|---:|---:|
| Two-body | 192 | 0 |
| J2 | 192 | 0 |
| Full exponential | 180 | 0 |
| Full NRLMSISE-00 | 180 | 0 |
| Full JB2008 | 180 | 0 |

## Historical MT-001 high-concurrency scientific run

`evidence/validation-wide/correctness.json`: **300/300 comparisons passed; zero mismatching values**. CASE-A/B/C × all five workloads × both policies × workers 1,2,4,8,16 × two repetitions; batch size 16, duration 300 s, output cadence 30 s. This exercises actual 8- and 16-worker atmospheric execution, rather than merely configuring a count that is clamped to a size-4 batch. Of these, 60 comparisons use 16 actual workers.

## Evidence and limits

`correctness.json` consolidates recorded scientific documents; each underlying document retains its exact configuration, kernel/weather/case hashes, native switches, worker counts, counts, mismatch details and summaries. Commands are recorded by `run.py`; the final manifest hashes sources, dependencies, outputs and prior evidence.

Preservation proof in `evidence/constraints.json`: **49 existing core source
files are byte-for-byte unchanged from the starting Git SHA**, including force,
integration, atmosphere, SPICE locking and exact-cache implementations.
**1608 historical atmospheric/numerical-audit/benchmark evidence and tool files
are unchanged** against the pre-edit hash index. New production code is confined
to the batch header; no density caching/interpolation, model fallback or
approximate ephemeris is introduced.

No ThreadSanitizer or injected OS thread-creation failure result is claimed. Partial-launch cleanup is implemented with cancellation and RAII joins and reviewed in source; runtime exception tests cover trajectory construction/evaluation failures. Generic user factories/custom borrowed resources remain the caller's thread-safety responsibility. No concurrent external CSPICE calls or kernel mutation are supported during a batch.

Validation timings include deliberately expensive trace and diagnostic work and are **not** throughput benchmarks. Production and contention benchmarks are separate processes with trace capture disabled.

## MT-002 targeted acceptance

The current API passed the existing 184-case regression suite once. The new
registered process-contract CTest passed separately (seven Python cases). The
migrated scientific validation driver passed ten short comparisons.
The real concrete runner passed 24 six-hour complete binary64 comparisons,
six empty/single/oversized comparisons and two simultaneous, independently
configured process comparisons. No scientific mismatches.
See [MT002.md](MT002.md) and [current correctness evidence](mt002-evidence/correctness.json).
The unchanged MT-001 stage/native-input evidence remains authoritative for
unchanged physics and model preprocessing; it was not regenerated.
