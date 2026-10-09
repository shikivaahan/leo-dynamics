# Numerical correctness audit

Five independently reproduced numerical defects are corrected in separate
commits. **The fully coupled six-hour reference remains globally unverified;
caching work is not ready to resume.** This is a documented scientific blocker,
not a reason to loosen tolerances or claim that passing regressions certify
all trajectories.

- [DEFECTS.md](DEFECTS.md): root causes, severities, before/after evidence,
  regressions, fix SHAs and investigated hypotheses.
- [CONVERGENCE.md](CONVERGENCE.md): independent methods, isolated-model and full
  convergence studies, perturbation localisation and remaining accuracy limits.
- [evidence/baseline.json](evidence/baseline.json): pre-audit SHA and immutable
  historical evidence hashes. Original Stage3 baseline and OPT-001 are preserved.

## Scope and contracts

Pre-audit commit `6a8f92a9721441623980bd47905cd22a3367f296`; original Stage3 commit
`48961a2137f87bb4a1daa80ae880ecc812dad5da`. Original benchmark spacecraft, physics,
epoch, cadence and tolerances are read from the existing benchmark configuration.
No cache, memoisation, new ephemeris scheme, performance optimisation, default
integration tolerance or documented physical approximation changed.

Audited: actual Boost Dopri tableau/derivative overload, accepted/rejected buffers,
local error/controller, time clipping and quaternion projection; gravity/J2 and
potential identities; quaternion/Euler/inertia/frame invariants; environmental
forces/torques, atmosphere, incidence and degenerate eclipse geometry; raw direct
SPICE observer/target/J2000/NONE/SI/time conventions. Evidence distinguishes
implementation defects, physical approximations and unconfirmed conditioning
hypotheses. The diagnostic mirror is not an independent accuracy oracle.

## Build original or corrected audit binaries

Preinstall CMake, Eigen3, Boost headers and CSPICE. No downloads in project
configure. Catch2 is only required for permanent regression tests. Example paths
below are this machine's dependency locations; substitute local installations.

```sh
git archive 6a8f92a9721441623980bd47905cd22a3367f296 --format=tar -o /tmp/leo-preaudit.tar
mkdir -p /tmp/leo-preaudit
tar -xf /tmp/leo-preaudit.tar -C /tmp/leo-preaudit

cmake -S docs/numerical-audit -B /tmp/leo-audit-original \
  -DCMAKE_BUILD_TYPE=Release -DLEO_SOURCE_DIR=/tmp/leo-preaudit \
  -DCMAKE_PREFIX_PATH='/tmp/leo-deps/usr;/tmp/leo-deps/catch-install' \
  -DCSPICE_ROOT=/tmp/leo_cspice/cspice
cmake --build /tmp/leo-audit-original --parallel 4

# Omit LEO_SOURCE_DIR to compile against the current remediated tree.
cmake -S docs/numerical-audit -B /tmp/leo-audit-current \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH='/tmp/leo-deps/usr;/tmp/leo-deps/catch-install' \
  -DCSPICE_ROOT=/tmp/leo_cspice/cspice
cmake --build /tmp/leo-audit-current --parallel 4
```

The standalone project builds `numerical_audit` and `eclipse_probe`. Setting
`LEO_ENABLE_CSPICE=OFF` permits the kernel-only eclipse probe without SPICE.
Library/public APIs are unchanged; integration/conditioning instrumentation is
confined to audit tooling.

## Expensive scientific studies (not fast CI)

```sh
python3 docs/numerical-audit/run.py \
  --binary /tmp/leo-audit-current/numerical_audit \
  --spk /tmp/leo_cspice/kernels/de432s.bsp \
  --lsk /tmp/leo_cspice/kernels/naif0012.tls \
  --source-commit "$(git rev-parse HEAD)" --output /tmp/new-study.json --full

python3 docs/numerical-audit/refine.py \
  --binary /tmp/leo-audit-current/numerical_audit \
  --spk /tmp/leo_cspice/kernels/de432s.bsp \
  --lsk /tmp/leo_cspice/kernels/naif0012.tls --output /tmp/new-horizon-study.json

python3 docs/numerical-audit/condition.py \
  --binary /tmp/leo-audit-current/numerical_audit \
  --spk /tmp/leo_cspice/kernels/de432s.bsp \
  --lsk /tmp/leo_cspice/kernels/naif0012.tls --output /tmp/new-conditioning.json
```

Output paths must be fresh; do not overwrite historical evidence. Use the actual
source revision, not an arbitrary label. `run.py` runs zero/signed-ULP/1 µm/30 µm/
1 mm/1 m Sun perturbations, independent RKF78 refinements, separate rtol/atol/
maximum-step sweeps, isolated models, identical RK4 grids, exact analytic cases,
a two-day J2 comparison, step histories and same-state replay. Compact final/
max/RMS statistics and selected growth histories are saved rather than every
stage/state of every experiment. Wall time is diagnostic, not performance evidence.

`refine.py` isolates the coupled reference's self-refinement versus duration;
`condition.py` perturbs the physical initial attitude on identical grids to
separate initial-condition conditioning from adaptive-time changes. Neither
script turns an unconverged finer trajectory into ground truth or proves chaos.

Direct executable options: `--scenario` two_body/j2/attitude/principal/srp/drag/
albedo/gg/full/full_no_eclipse/full_no_torque; `--method` dopri/trace/rkf78/rk4/
replay/spice. `--rtol`, `--atol-scale`, `--max-step`, `--duration`, `--cadence`,
`--perturb-x`, `--ulp-x`, `--attitude-seed` are explicit diagnostics. Replay reads
elapsed time plus the 13 named packed-state components from stdin.

## Independent high-precision mathematics

The regression constants are checked by the regular C++ suite; CI needs no
Python multiprecision dependency. To regenerate references, install test-only
mpmath in an isolated environment:

```sh
python3 -m venv /tmp/leo-audit-reference-env
/tmp/leo-audit-reference-env/bin/pip install mpmath==1.4.1
/tmp/leo-audit-reference-env/bin/python docs/numerical-audit/eclipse_reference.py \
  --probe /tmp/leo-audit-current/eclipse_probe --output /tmp/new-eclipse-reference.json
/tmp/leo-audit-reference-env/bin/python docs/numerical-audit/environment_reference.py
```

Eclipse reference: independent Cartesian circle-intersection width quadrature at
80 digits. Sentman reference: incident/outgoing molecular momentum vectors at
60 digits. Both use exact binary64-to-multiprecision inputs, not shortest-decimal
approximations. Other permanent oracles use long-double potential gradients,
Rodrigues, Kepler identities, self-adjoint inertia/Euler identities and exact spin.

Fixed random seeds: environmental properties20251009, potential gradients20260909,
quaternion rotations13572468, Kepler orbits424242, Euler identities987654321.
Randomised inputs are bounded physically valid states; deterministic boundaries
and inverse-operator stresses are separately identified.

## Fast regression and CI validation

`BUILD_TESTING=ON` registers all numerical tests in the existing kernel/integration
executables. Root CI's GCC Release, Clang Release and Clang Debug ASan/UBSan matrix
runs them with Stage1–3 analytical/regression, cache-provider and plotting tests.
A discoverable focused command is also in the workflow:

```sh
ctest --test-dir build-stage2-gcc --output-on-failure \
  -R 'Conical disk overlap|small centre separations|Apparent body radius|Sentman small lift|self-adjoint inertia|Rejected large adaptive|Projected adaptive'
```

Observed local results: **114/114** pass in each full GCC, Clang and ASan/UBSan
suite, plus **7/7** focused numerical cases. Real DE432s/NAIF0012 and plotting
smoke are included. [Validation summary/logs](evidence/validation.json).
Remote CI was not run here. CSPICE is an external prebuilt, not itself sanitizer
instrumented. No claim is made that finite-precision extremes or all possible
numerical defects have been eliminated.

## Existing-cache post-remediation diagnostic

Build `benchmarks/cases` against the corrected source, then execute the original
`ACCURACY --mode cached` cases for60/300/900/1800 s and direct `BENCH-03` with the
same kernels. [Recorded outputs](evidence/opt001-post-remediation.json) are new
and separate: every interval still fails the original six-hour criteria.
Original OPT-001 data/rejection and thresholds remain immutable.

No caching readiness until NA-OPEN-001 has a satisfactory independently converged
six-hour reference/global error budget. Fixing the documented local defects and
passing analytical regressions are necessary, not sufficient, for that decision.
