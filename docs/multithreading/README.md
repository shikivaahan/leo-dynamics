# Independent-trajectory batch propagation

Optional C++20 execution of independent spacecraft trajectories, preserving the existing `leo::propagate()` numerical path. No change to force/torque equations, atmospheric routines, native/CSPICE locks, exact-cache policy or frozen scientific evidence.

Historical MT-001 verdict: 924/924 scientific bit comparisons and 184/184 regressions passed.
Gravity-only ensembles scale; all measured full-dynamics models are slower
with multiple workers on this host. Details and limits in [RESULTS.md](RESULTS.md).

The current MT-002 interface takes only an integer worker count; scheduling is
internal. [MT002.md](MT002.md) records the compact thread/process experiment and
the concrete application-level process route. MT-001 evidence below is historical
and unchanged.

- [DESIGN.md](DESIGN.md): API, ownership, thread safety, scheduling and exception contract.
- [PARAMETERS.md](PARAMETERS.md): exact engineering and experiment settings.
- [DECISIONS.md](DECISIONS.md): alternatives, rationale and limitations.
- [VALIDATION.md](VALIDATION.md): binary64 comparison coverage and regression results.
- [BENCHMARKS.md](BENCHMARKS.md): commands, timing definitions and complete scaling tables.
- [RESULTS.md](RESULTS.md): measured workload verdicts, scheduling and bottlenecks; MT-002 experiments.
- [evidence/manifest.json](evidence/manifest.json): source/dependency/toolchain/hardware/input/output hashes and executed commands.
- [evidence/correctness.json](evidence/correctness.json), [evidence/benchmarks.json](evidence/benchmarks.json), [evidence/scaling.json](evidence/scaling.json): machine-readable correctness and performance evidence.

Dependencies and weather/kernels are acquired explicitly before configure; no benchmark or library downloads data or silently substitutes a model. Use a new evidence directory when reproducing. Historical recorders/drivers refuse overwriting evidence; the concrete MT-002 route atomically replaces its requested result only after successful execution and merge.

The CMake target is `leo::integration`; C++ integration symbols remain in namespace `leo`. `include/leo/integration/batch.hpp` defines `BatchPropagationError` and the single `propagate_batch(..., integration_options, workers)` function. One worker executes inline; larger counts use dynamic assignment internally.

Measurement shortcut after configuring/building the standalone root:

```sh
python3 docs/multithreading/measure.py --evidence NEW_EVIDENCE_DIR --build build-multithreading
```

The current runner uses only dynamic scheduling. It does not recreate the old
static/dynamic matrix: check out the recorded MT-001 source revision to reproduce
that historical comparison. No regeneration is needed for MT-002. `analyse.py`
reduces raw records; `manifest.py` hashes inputs/build artifacts and outputs.
Use the compact commands in [MT002.md](MT002.md) for current scaling decisions.
