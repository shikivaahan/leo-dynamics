# Results, and the production recommendation

## Bottom line

**Do not implement a production atmospheric density cache.** Not because the answer is negative, but
because the measurements that would justify one cannot be made: two hash-pinned `direct-v2` inputs no
longer exist upstream and the exact baseline was kept rather than substituted. Every accuracy,
locality, coupled-error and speedup number that would drive the decision is planned, blocked and
resumable — not collected.

One measured result does constrain the design, and it is primary evidence:

| Measured (pinned inputs, NRLMSISE-00 native) | Value |
|---|---|
| Altitude log-density gradient | up to 0.21 /km (daily Ap), 0.26 /km (Ap-history) |
| ln-ρ curvature | up to 214 /km² |
| Smooth-time gradient | 8.96e-04 /s |
| Altitude cells needed for 1 % worst case | ≤ 5 km |
| Altitude cells needed for 0.1 % worst case | ≤ 5 km (6.1e-4 at 5 km; 2.6e-3 at 10 km) |
| Altitude cells needed for 0.01 % | not reached at any tested width |
| Worst region | 200–250 km, ±45° latitude |

## Evidence classes in this study

| Class | Meaning | Present here |
|---|---|---|
| `scientific-native-input-partial` | pinned inputs, real native model, no provider or trajectory claim | yes: NRLMSISE-00 sensitivity, both Ap modes |
| `pipeline-verification-only` | nonprimary inputs; proves execution, never science | yes: 71-task smoke pipeline |
| `scientific-primary` | pinned inputs, full provider path | no — blocked |

## Verified behaviour of the harness

- Direct evaluation with caching disabled is **bitwise identical** to the existing driver on
  CASE-A/B/C for both models.
- 42 correctness checks across both models pass: complete-input identity, exact-memoisation bitwise
  repeat, no cross-model or cross-configuration contamination, weather-boundary refusals, kernel
  generation invalidation, backwards and non-monotonic time, cell boundaries and polar fallback,
  bounded eviction, in-contract altitude range, and positivity preservation.
- Resume reliability: 17/17 lifecycle cases, including no re-execution of completed work, refusal on
  tampered results, changed binary, changed inputs and changed plan; clean budget stop; interrupt
  reaping; failure isolation.
- Project regressions: **185/185 pass**; all tracked source, test and frozen-evidence files hash-
  verified unchanged. The study adds only `docs/atmosphere/cache-study/` and
  `tests/cache_study_runner_tests.py`.

## Answering the eight questions

1. **NRLMSISE-00 caching?** Undetermined. Its altitude structure is measured and is stiff enough that
   coarse cells fail; its accuracy and speedups are not.
2. **JB2008 caching?** Undetermined. No JB2008 sensitivity was reachable without the pinned
   SOLFSMY/DTC records.
3. **Best candidate?** Undetermined. At smoke resolution no approximate candidate met the target; at
   pinned resolution nothing has been screened.
4. **Resolution per budget?** Altitude alone: ≤5 km for 0.1 %, none tested for 0.01 %. Other axes
   unknown.
5. **Error entering the dynamics?** Unmeasured on pinned inputs.
6. **Useful for single trajectories or batches?** Unmeasured; locality on pinned inputs is pending.
7. **Failure cases?** Known and guarded: interpolation across any discrete space-weather change,
   integer DSTDTC transitions inside an hour, UTC midnight and the leap second; nonpositive or
   nonfinite reconstructions; cold starts and eviction. Near-polar queries are served natively.
8. **Should a production cache be implemented, and with what API?** Not now, and no API should be
   added yet. The density-only seam used here is exactly the API a production cache would need —
   a density-lookup adapter alongside, never replacing, `DirectAtmosphere::evaluate()` — and even
   that should wait for the pinned-input measurements.

## To finish the study

Restore the two archived files matching `evidence/manifest.json` →
`inputs.missing_for_primary`, then:

```sh
python3 docs/atmosphere/cache-study/run.py --resume <run-directory> --workers 8
```

The run resumes at the first incomplete task, executes nothing twice, and will screen, propagate and
benchmark only configurations whose accuracy evidence is complete and passing.