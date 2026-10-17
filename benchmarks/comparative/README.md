# Comparative performance: measurement boundary

Qualification-only. This is not a public batch API, execution-strategy switch, or
replacement for `benchmarks/run.sh` or the MT-001/MT-002 evidence.

## What is measured

Release-optimised GCC `-O3 -DNDEBUG` C++20 `leo_reference_dump` (production
`EarthDynamics`, `propagate`, `propagate_batch`), an isolated SciPy DOP853
process, and a separately installed native Tudat process. Headline clocks cover
propagation plus generation and retention of every requested output sample.
Import, model/body construction, outer fresh-process wall time (warmup plus all
repeats plus publication) and RHS counters are reported separately. Per-member
Python preparation happens once per repeat loop in one process; the concurrent
multiprocess MT-002 protocol is not reimplemented here.

## Fairness rules

1. Identical scenario JSON feeds all three tools: same SI constants, explicit
   fixed inertial +Z J2 axis, same spacecraft/input data, same output times.
2. Each actual duration/cadence is verified against an independent reference
   *before* it is timed. A configuration that is not accuracy-qualified is not
   benchmarked.
3. Identical nominal tolerances are not assumed to mean identical accuracy.
   Achieved position/velocity/SO(3)/rate errors are published per row.
4. One benchmark child runs at a time; one warmup is discarded; every repetition
   is retained. Speedups are ratios of medians measured in this same run, never
   differences of independently measured medians.
5. Reference-fixture generation is a separate explicit operation and never runs
   inside an ordinary timing clock.
6. Only physically equivalent models are compared. Native Tudat's panel/thermal
   atmosphere and radiation equations are not the project's cold-beam or
   radial-beam laws, so those rows are marked `UNMATCHED` and excluded from
   ratios.

## Scope limits

The matched constant-Sun environment acquires no SPICE and creates no
atmosphere-provider contention. Strong-scaling numbers therefore measure the
scheduler and shared state of *this* configuration and do not supersede, extend
or contradict the MT-002 real-environment measurements: environment-heavy
threaded batches were measured there as slower than sequential. Report slowdowns
at equal prominence to speedups. No automatic process fallback is introduced or
justified by these results.

Python peak RSS is a fresh isolated process measurement including interpreter,
imports and setup, not incremental trajectory memory; only logical retained
`float64` output bytes are directly comparable. SciPy's DOP853 uses Python
stage/control logic with compiled NumPy algebra; there is no separately
isolatable compiled DOP853 engine to time, and Python-level RHS latency is a
diagnostic only, never subtracted from solver medians.

There is no arbitrary performance pass/fail threshold for v0.1. A Python tool
that is faster than C++ is reported as such.

## Usage

```sh
python3 benchmarks/comparative/run.py \
  --binary build-reference/leo_reference_dump \
  --tudat-python /path/to/tudat-env/bin/python \
  --out results/full --repeats 3 --warmups 1 [--resume]
python3 benchmarks/comparative/analyse.py --input results/full/summary.json --out results/full/report
```

`--quick` produces short-horizon tooling smoke output and is labelled as such,
never as six-hour qualification. Workloads: two-body, J2, torque-free rigid body,
flat-plate drag, and matched fully coupled CASE-A/B; single trajectories, 8 and
64-member sequential ensembles, and native 1/2/4/8-worker strong scaling at 64.