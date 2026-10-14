# Reconstruction accuracy — what was screened and what is not primary

## Screening contract

A candidate configuration is eligible for coupled and performance measurement only when **every**
accuracy task for that exact configuration — each weather epoch, each Ap mode, each requested rung —
completed and published:

- a `requested_rung` whose `settings.base_cells`, capacity, interpolation mode, axis and replacement
  policy equal what the runner requested;
- all three holdout packets (`uniform-random`, `local-query-packet`, `broad-query-packet`);
- a maximum absolute relative error at or below the screening threshold;
- zero nonpositive or nonfinite reconstructions.

A mean or a percentile never satisfies a threshold. There is no fallback: an unmeasured or failing
configuration is recorded blocked and is never propagated or benchmarked.

## What the verification run showed

The complete pipeline was executed on both models with nonprimary inputs and smoke sizing
(`evidence/accuracy-summary.json`, `evidence_class: pipeline-verification-only`). These numbers
demonstrate that screening runs end to end; they are **not** cache results and no conclusion is drawn
from them.

| Model | Candidate | Worst max relative error | Native evaluations avoided |
|---|---|---:|---|
| NRLMSISE-00 | A exact | 0 (bitwise) | 100 % |
| NRLMSISE-00 | B cell | 2.95e-01 | 100 % |
| NRLMSISE-00 | C interpolation | 1.45e-02 | 98–100 % |
| NRLMSISE-00 | D adaptive | 2.76e-01 | 51–60 % |
| NRLMSISE-00 | E control | 0 (bitwise) | 100 % |
| JB2008 | A exact | 0 (bitwise) | 100 % |
| JB2008 | B cell | 2.56e-01 | 100 % |
| JB2008 | C interpolation | 8.01e-02 | 50–99 % |
| JB2008 | D adaptive | 2.10e-01 | 51–60 % |
| JB2008 | E control | 0 (bitwise) | 100 % |

Smoke sizing uses 25 km/5°/60 s cells and a 5 % screening target, which no approximate candidate
meets. That is the intended outcome at this resolution and it is consistent with the pinned-input
altitude study in `SENSITIVITY.md`: coarse altitude cells cannot meet a tight budget regardless of
model.

## Known weaknesses of candidate D in this configuration

Adaptive refinement spent 51–60 % native calls and reduced worst-case error little at smoke
resolution, because its refinement schedule and probe divisor are tuned in the smoke range. The
primary study's coarser-to-finer sweep would decide whether it earns its cost; that sweep is pending.
A candidate that cannot meet its budget at smoke resolution is not a candidate to propagate, and none
was.

## Unmeasured on pinned inputs

Accuracy against the pinned provider, per-model per-resolution error-versus-runtime and
error-versus-memory tradeoffs, Pareto fronts, and threshold satisfaction at 0.01 %–5 % all require the
two unavailable inputs. The primary run has these tasks planned and blocked, not omitted.