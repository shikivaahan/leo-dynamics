# Reproducible Stage 4 benchmarks

The immutable Stage 3 reference is `48961a2137f87bb4a1daa80ae880ecc812dad5da`.
`baseline.json` records the original main/HEAD, hardware, toolchain and external
kernel identities before performance code changes. User-owned `outputs/` is
not benchmark input and is untouched.

## Methodology

The standalone CMake project in `cases/` compiles the **same driver** against a
selected `LEO_SOURCE_DIR`. This permits benchmarking the original commit without
backporting benchmark scaffolding into that commit. Build flags are Release
`-O3 -DNDEBUG -Wall -Wextra -Wpedantic`, C++20, without fast-math, LTO or native ISA.
Use the same compiler, dependency versions, machine and workload for comparisons.
Do not compare sanitizer or Debug timing with Release timing.

Each workload gets at least one warm-up and seven measured repetitions. Raw
individual JSON measurements, numerical references, metadata and individual CSV
rows are retained. Summaries use median/minimum/maximum, never summed speedup
percentages. Wall time uses a monotonic clock. Driver outputs include consumed
checksums and final states. Ephemeris preprocessing and propagation are timed
separately; end-to-end time is their sum. Output diagnostics/correctness checks
are outside the timed propagation region. Profile instrumentation is a separate
run, not the published performance sample; its clock overhead is disclosed.

| ID | Workload | Metric |
|---|---|---|
| BENCH-01 | 200000 changing deterministic spacecraft states; kernel gravity/J2 and rigid-body RHS | ns/RHS, RHS/s |
| BENCH-02 | 6 h fixed LEO orbit, J2 and coupled rigid body | wall s, RHS count, RHS/s |
| BENCH-03 | 6 h fixed LEO box, J2, SRP, conical eclipse, Sentman drag, albedo, environmental torques, coupled attitude | wall s, RHS count, provider/underlying query counts |
| QUERY | Changing Sun epochs, direct or interpolated | queries/s, ns/query |
| ACCURACY | Node/intermediate/boundary/30-day direct-reference comparisons, force/torque/eclipse and full trajectory | numerical errors, pass/fail |

Fixed ET=0, 30 s outputs, Dormand–Prince 5(4); integration defaults are specified
explicitly by the driver: rtol=1e-10, position atol=1e-3 m, velocity atol=1e-6 m/s,
quaternion/omega atol=1e-12, initial/max step=1/15 s. No tolerances are relaxed.
Detailed spacecraft, initial state and Sun fallback are recorded by the driver
and raw numerical reference outputs. The analytic fallback is deliberately not
advertised as SPICE; it supports reproducibility without external kernels.
Real direct mode invokes the unchanged CSPICE provider on every request.

`ACCURACY.md` fixes criteria **before cache implementation/interval selection**.
The source wrapper acquires only Sun position/velocity. Eclipse geometry is
recomputed from each current spacecraft state, never cached.

## Preserving and replaying the original baseline

```sh
mkdir -p /tmp/leo-stage3-baseline
git archive 48961a2137f87bb4a1daa80ae880ecc812dad5da | tar -x -C /tmp/leo-stage3-baseline
```

Point the runner at that source directory, not at a modified working tree.
The stored original archive SHA256 is
`66e9623eaee8359776c33f6320b5be60a09b633edae39437b5b8633ebc72d231`
(for the `git archive --format=tar` artifact). Git object history is the durable
source of truth; external temporary build/source directories are disposable.

The runner uses Python 3 standard library and a POSIX shell entrypoint, no
benchmark framework. Run `sh benchmarks/run.sh --help` for the exact options.
Dependencies are preinstalled; nothing downloads in configure or measurements.
Supply SPK/LSK paths explicitly for real SPICE runs, otherwise only deterministic
benchmarks run. Use `--reference` to compare against preserved original numerical
outputs. A candidate without a commit is identified by the parent commit **and
source content hash**, not invented candidate SHA. Build/toolchain/kernel
metadata is captured with each evidence bundle.

Kernel identities from this machine:

| File | SHA256 |
|---|---|
| DE432s `de432s.bsp` | `363f32e14f5255359ac32c4d38080cf28ab55564a5e16696a75f63394b666e9b` |
| NAIF0012 `naif0012.tls` | `678e32bdb5a744117a467cd9601cd6b373f0e9bc9bbde1371d5eee39600a039b` |

CSPICE N0067 library SHA256:
`22ac28890f746526217cc8a7d242887c2e78c5a83380eb8db34465c2a1092e08`.
Kernel paths are environment-specific; file hashes, not paths, establish identity.
The runner captures paths and hashes together. NAIF0012 is loaded explicitly even
though geometric ET queries need only SPK coverage.

## Ledger rules

Every later optimisation must profile, preserve its immediate baseline, state a
hypothesis, implement one isolated change, measure before/after, validate numerical
behavior, and record keep/revert/inconclusive in `OPTIMISATIONS.md`. Failed or
inconclusive experiments stay in the ledger. Preserve original-baseline evidence
separately. Do not start any other optimisation as part of OPT-001.

`results.csv` holds individual measurements, not just favourable medians.
`PERFORMANCE_SUMMARY.md` is for measured engineering/CV evidence, not predictions.
WSL scheduling, host activity, turbo and thermal state are uncontrolled; runtime
ranges represent observed variation, not confidence intervals or universal speedups.
