# Decisions, alternatives and observed tradeoffs

Only decisions that changed what the study does, with the alternative and the reason.

## Keep the exact baseline when two pinned inputs vanish

The pinned `SOLFSMY.TXT` and `earth_000101_270105_261009.bpc` are gone upstream; the live files have
different hashes and the rolling Earth kernel has a new name. Substituting either would silently
change the weather drivers or the orientation kernel that every frozen `direct-v2` number rests on.
Offered to the user, declined, and the consequence accepted: the primary study is blocked with all
391 tasks recorded pending and a resume command, rather than producing numbers that could not be
compared with any historical baseline. Recovery attempts are recorded in the manifest.

## Expose density only, never a substitute `Result`

`DirectAtmosphere::evaluate()` returns temperatures and constituents. A cache that served partial
output under that name would be a silent contract violation. The study's evaluator returns density
for candidate paths and keeps a full `authoritative()` for reference; nothing in production changed.

## Interpolate ln ρ, not ρ

ρ spans about seven decades over 200–1000 km. Interpolating ρ can go negative between nodes; ln ρ
cannot, and it is close to linear over short altitude cells. The cost is that an in-contract
reconstruction is not bitwise identical to the native value (`exp(ln x) != x`), which the correctness
checks now state explicitly instead of asserting bitwise equality for approximate candidates.

## Use a complete bitwise weather identity, not a hash

The weather guard is the only thing standing between a cache and reuse across an integer DSTDTC
transition, a 3-hourly Ap bin or UTC midnight. A hash collision would permit exactly the reuse the
guard exists to prevent, so the full vector is compared and a 64-bit summary is reported for
traceability only. Cost: a 16-word key per entry.

## Detect kernel generation by observation, not file metadata

File size and mtime cannot see a `furnsh`/`unload` or an explicit `invalidate_reuse()`. Retaining one
opaque `SunObservation` and re-checking `compatible_sun()` before each lookup uses the provider's own
generation check, so every managed invalidation path is covered.

## Detect changes through the provider's authoritative API

Two integrity defects were found this way. A locality reuse counter incremented on map insertion
rather than on an existing key, reporting 100 % reuse for an all-unique sequence. The density-only
API contract is enforced in the harness, not assumed. Neither would have been found by reading the
metric's name.

## Rebuild interpolation corner enumeration

Corner digits were consumed by parity rather than by successive bits, so full interpolation visited
two of sixteen corners and returned `exp(0) = 1 kg/m³` for time-only and spatial-only modes. A
verification run then reported a 5.8e14 relative error and zero "interpolations". Both the count and
the bit decoding are corrected, and an in-contract reconstruction is now judged on agreement.

## Keep screening strict and gate expensive stages on measured error

No fallback ranking: a configuration is propagated only when its own accuracy tasks completed, matched
the requested resolution, and passed on maximum error. The first implementation ranked the first N
screened candidates when a metric was missing; that was removed because it would have propagated
configurations that were never measured.

## Make the process-isolated benchmark a real measured workload

A benchmark that reran the in-process benchmark per child would have produced numbers that were not
process-isolated throughput. The study writes the measured MT-002 binary format from contiguous
global ranges, merges through the existing orchestration, and times the whole group in the parent.

## Keep the harness inside the study directory

Everything experimental lives in `docs/atmosphere/cache-study/` and links the production libraries
unmodified. This made the disabled-cache bitwise proof meaningful: when the control matches the
driver exactly, the wrapper is demonstrably not the source of any difference the candidates produce.

## Observed tradeoffs

- Full weather identity in every key costs memory (166 bytes per memoised entry measured in the
  verification run) and buys immunity to the discontinuities that dominate density error.
- Per-query generation checks cost a mutex round trip; correctness after a managed invalidation was
  judged worth it for a study that measures provenance.
- Screening latency diagnostics run inside the accuracy task and add two clock reads per query; they
  are labelled diagnostics and excluded from headline performance.
- A 24-hour RKF7(8) refinement pair is affordable per case but expensive per candidate; the runner
  schedules it only for shortlisted configurations.