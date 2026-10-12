# MT-001 — multithreaded batch propagation

Optional C++20 execution of independent spacecraft trajectories, preserving the existing `leo::propagate()` numerical path. No change to force/torque equations, atmospheric routines, native/CSPICE locks, exact-cache policy or frozen scientific evidence.

Verdict: 924/924 scientific bit comparisons and 184/184 regressions pass.
Gravity-only ensembles scale; all measured full-dynamics models are slower
with multiple workers on this host. Details and limits in [RESULTS.md](RESULTS.md).

- [DESIGN.md](DESIGN.md): API, ownership, thread safety, scheduling and exception contract.
- [PARAMETERS.md](PARAMETERS.md): exact engineering and experiment settings.
- [DECISIONS.md](DECISIONS.md): alternatives, rationale and limitations.
- [VALIDATION.md](VALIDATION.md): binary64 comparison coverage and regression results.
- [BENCHMARKS.md](BENCHMARKS.md): commands, timing definitions and complete scaling tables.
- [RESULTS.md](RESULTS.md): measured workload verdicts, scheduling and bottlenecks; MT-002 experiments.
- [evidence/manifest.json](evidence/manifest.json): source/dependency/toolchain/hardware/input/output hashes and executed commands.
- [evidence/correctness.json](evidence/correctness.json), [evidence/benchmarks.json](evidence/benchmarks.json), [evidence/scaling.json](evidence/scaling.json): machine-readable correctness and performance evidence.

Start at [BENCHMARKS.md](BENCHMARKS.md) for reproduction commands. Dependencies and weather/kernels are acquired explicitly before configure; no benchmark or library downloads data or silently substitutes a model. Use a new evidence directory when reproducing: the command recorder and experiment drivers refuse overwriting existing output files.

The CMake target is `leo::integration`; C++ integration symbols remain in namespace `leo`, matching existing project conventions. `include/leo/integration/batch.hpp` defines `BatchOptions`, `BatchScheduling`, `BatchPropagationError` and `propagate_batch()`.

Measurement shortcut after configuring/building the standalone root:

```sh
python3 docs/multithreading/measure.py --evidence NEW_EVIDENCE_DIR --build build-multithreading
```

This runs the full matrix, six-hour confirmation, diagnostic matrix and startup
probe sequentially under the recorded physical-core affinity. It does not fetch
dependencies or replace correctness validation. `analyse.py` reduces the raw
records; `manifest.py` hashes actual inputs, build artifacts, commands and outputs.
