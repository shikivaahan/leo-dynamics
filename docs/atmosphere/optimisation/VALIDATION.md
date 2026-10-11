# ATM-OPT-001 validation

## Exercised result

`evidence/validation/verification.json`: all comparisons passed. `commands.json` retains command arguments, return codes, stdout/stderr and test environment. `comparisons.json` records each exact comparison and the first difference (null for every completed comparison).

| Evidence | Coverage | Result |
|---|---:|---|
| Existing regression suite plus targeted reuse tests | 172 CTest cases | 172 passed |
| Atmospheric fixture grid | 10,710 outputs, five historical epochs, three models | Original and All candidate bitwise equal to frozen direct-v2 |
| CASE-A/B/C nominal scientific series | Nine runs, including exponential control | All fields bitwise equal to frozen direct-v2 |
| Complete nominal/refined scientific series | 18 runs, three cases × three models × two methods | Original-source and All candidate bitwise equal |
| Frozen refined trajectories | Nine checksums | Original and candidate equal to frozen direct-v2 |
| Actual JB2008 native bridge invocations | 424,025 calls | Every input/output stream byte identical |
| Independent controls (all seven non-All configurations) | 126 further complete series comparisons, 74,970 further fixture outputs, 2,968,175 further native calls | Every control bitwise identical to the original |
| Official NRLMSISE-00 reference cases | 17 cases | Existing reference assertions passed, no skip |
| Official JB2008 SET validation | 13,320 fixtures | Existing reference assertions passed, no skip |
| Full-precision independent JB2008 density references | 12 fixtures | Existing reference assertions passed, no skip |

In total every candidate configuration was checked against 126 complete nominal/refined series (All plus seven controls), 85,680 fixture outputs and 3,392,200 actual native JB2008 invocations, all bitwise equal to the unchanged original.

No changed scientific value was accepted using a tolerance. JSON numeric comparisons pack each value into binary64, including signed zero; native streams are compared in blocks byte-for-byte with a first-row/field difference locator. Scientific JSON uses the existing 17-digit output precision. Hashes accompany, but do not replace, the full comparisons.

The published independent-reference assertions retain their existing tolerances because those reference files have finite published precision and represent model-validation evidence, not optimisation equivalence. They were not changed or re-pinned. Their stdout is retained in `commands.json`.

## Complete series

`validation.cpp` includes the selected experiment translation unit, with its main renamed. The original driver includes `/tmp/atm-opt-original/docs/atmosphere/experiment.cpp` and links the original libraries from revision `6a1ad59`; the candidate includes and links this worktree. Both use the same case loader, force models, state packing, project tolerances, production Dopri5 and independent RKF7(8) refinement.

The captured 25-column `samples` arrays reproduce the frozen nominal series: time, position, velocity, quaternion, angular velocity, altitude, density, drag acceleration/torque magnitudes, cumulative drag/non-gravitational work, matched energy change and attitude diagnostics. Additional `extended` arrays capture density, both temperatures, all available constituent densities, and all three drag-force and drag-torque components at each sample. No capture value feeds the solver.

Frozen direct-v2 records refined trajectory checksums and numerical-difference diagnostics, not full refined samples or native streams. The new evidence therefore checks frozen refined checksums and additionally compares complete original-source/candidate refined series. It does not claim that unavailable frozen refined fields were compared directly. Original-source extended temperature/force/torque series supplement frozen nominal fields that were not historically recorded.

## Actual native inputs

GNU linker `--wrap=leo_jb2008` records the real official bridge call without editing the native routine, bridge source or adapter. Each row contains 18 native-endian binary64 values: MJD, Sun RA/declination, spacecraft RA/latitude/height, F10/F10B/S10/S10B/M10/M10B/Y10/Y10B, DST, exospheric temperature, local temperature and density. This captures wrapped right ascensions actually passed to Fortran, not reconstructed or mocked inputs. Capture is verification-only and never linked into the performance binary.

The streams include every JB2008 invocation during nominal/refined integration, output diagnostics and extended-series extraction. Empty streams for the two other backends are reported as zero native calls, not counted as additional JB2008 proof. All raw streams are retained gzip-compressed alongside scientific series; compression is lossless, deterministic and performed after comparison.

## Targeted boundaries

`tests/spice_tests.cpp` covers ordinary midnight, month/year and leap-year transitions, positive leap-second insertion and neighbouring labelled instants, backward/nonmonotonic requests, exact agreement with reuse-disabled UTC, missing LSK after final-owner unload, two simultaneous provider owners, reload, FIFO eviction/capacity, and four readers overlapping managed load/unload operations.

Orientation tests compare cached matrices against raw CSPICE in quiescent oracle calls, both ITRF93 and IAU_EARTH, nextafter-adjacent ETs, backward requests, eviction, capacity, final-owner unload/reload, invalid frames/nonfinite epochs and concurrent readers/lifecycle. No failed matrix is cached.

`tests/atmosphere_input_tests.cpp` covers shared versus standalone JB2008 at four epochs and three positions, nextafter mismatch rejection, stale provenance after actual managed load/unload of a copied LSK, explicit sharing disabled, and four concurrent calls using one const observation. Geometry/units cannot be fabricated through the opaque API. Exponential and NRLMSISE-00 ignore incompatible optional Sun inputs. The shared-only test constructs no SRP model; the CASE propagation exercises SRP/eclipse enabled. Standalone fixtures exercise JB2008 without any supplied Sun.

These tests do not promise correctness under unreported external CSPICE mutation. External configuration changes require application-wide quiescence and notification; full atmospheric/trajectory configuration consistency requires a fixed kernel configuration throughout that computation. See DESIGN.md.

## Reproduction

Build both optimisation roots and the regression build as documented in README.md, then use a **new** evidence directory:

```sh
python3 docs/atmosphere/optimisation/validate.py \
  --original-build build-atmosphere-optimisation-original \
  --candidate-build build-atmosphere-optimisation \
  --test-dir build-atmosphere-gcc \
  --deps /tmp/leo-atmosphere-deps --cpu 0 \
  --out /tmp/atm-opt-001-validation-reproduction
```

The runner refuses populated output directories, supplies all required real reference data, fails on a required reference skip or first numerical discrepancy, and hashes historical atmospheric/profiling/numerical-audit evidence before and after. Historical evidence remained unchanged.
