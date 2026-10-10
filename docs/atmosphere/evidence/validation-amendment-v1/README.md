# Final atmospheric validation amendment

Physical reference baseline: [`../direct-v2/manifest.json`](../direct-v2/manifest.json), compiled source revision `7c11f57`. Historical `direct-v1` is unchanged. This directory records scientific interpretation, native-model comparisons, and the confirmed boundary regression; it is not a replacement for the frozen physical runs.

## Findings and limits

| Question | Evidence | Outcome |
|---|---|---|
| JB2008 grid disagreement | `jb2008-grid-investigation.json` | Full 545,528-row native/adapter density agreement is bit-exact. Printed-input disagreement is not fully explained: bounded per-epoch perturbations remove 92.7–97.4% of sampled RMS, but all eight searched epochs remain above density-print precision. Original dated solar inputs and producer build provenance are unavailable. |
| JB2008 altitude datum | `altitude-datum.json` | Examined primary sources specify geocentric latitude and scalar height in kilometres, without a unique physical height datum. Retain WGS84 ellipsoidal height as a provisional project convention. |
| Actual implementation defect | `boundary-before.json`, `boundary-after.json` | Real provider: 10/22 exact-boundary rejections before, 0/22 after the 1e-9 km guard margin; 3/3 interior controls evaluate in both. Received height is never clamped. |
| NRL geomagnetic modes | `ap-modes.json` | Daily Ap and unequal-slot history independently agree with the pinned native C core. History is already explicitly selectable; the baseline remains daily Ap. |
| Compiler/ABI arithmetic | `compiler-arithmetic.json` | 552 identical native-input cases: adapter density and both temperatures match exactly; GNU Fortran -O0/-O3 differ by at most 6.90e-15 relative density and 5.68e-13 K. This does not identify the unpublished producer toolchain. |
| Coordinates/time | `coordinates.json` | ERFA/IERS-B/Astropy comparison passes: max rotation difference 2.96145e-8 rad, ET difference 2.47061e-5 s. |
| Required regressions | `regression-suite.json`, `published-model-fixtures.json` | 166/166 pass, zero skips; original model tolerances unchanged. |
| Before/after physical runs | `../direct-v2/comparison-to-direct-v1.json` | All 10,710 matched-location outputs and all nine six-hour runs, including nominal/refined checksums and non-timing convergence diagnostics, are identical. |

`initial-inspection.json` records starting revision, source/data/compiler fingerprints and historical/output preservation fingerprints. `manifest-provenance-smoke.json` demonstrates the corrected missing compiler-build-type field; `physical-baseline-smoke.json` records the pre-guard confirmation of existing fixtures. `altitude-investigation-corrections.json` explicitly supersedes the earlier candidate experiment that omitted official THETA from spacecraft RA and mislabeled a full corner span as a half-width.

### Metric populations

Density is kg/m³. JB fixture relative error is unsigned `abs(computed-reference)/reference`, with a dimensionless result; RMS includes every evaluated entry. Historical 13,328 entries contain eight duplicated first rows. The corrected test evaluates 13,320 distinct sampled rows; the independent investigation evaluates all 545,528 published rows. Printed longitude 0/360 endpoints remain separate published labels despite physical equivalence. Different populations and reconstructed astronomy are identified separately, never silently compared as identical inputs.

The full-grid record distinguishes signed density difference, absolute density error, signed/unsigned relative error, signed/absolute temperature differences and zero-reference exclusion/counting. References are all positive (5.2864e-14–2.5156e-8 kg/m³); no near-zero floor is applied. Every worst-twenty record carries its epoch, row identifier, native coordinates, eight indices, DSTDTC, reference/computed values, input precision and provenance.

Input-rounding diagnostics are **hypothetical**, not recovered weather. Separate bounded fits use 46 sampled rows/epoch. Their failure to reach print precision does not prove no better fit exists; neither those separate fits nor per-row containment establishes one shared daily input vector. The altitude experiment's 1,118/1,118 containment result concerns the range over 256 solar-index corners. Maximum **full corner span** is `(high-low)/rho_ref = 0.04929275138411617`; it is not a half-width or a complete causal explanation.

## Height evidence

Primary locators, content hashes and inspected scope are recorded in `altitude-datum.json/source_citations`:

- [Official JB2008 distribution](https://sol.spacenvironment.net/JB2008/downloadables/jb2008.zip): `JB2008.f` specifies SAT(2) geocentric latitude and SAT(3) height in km; `JB08DRVAUTO.f` supplies scalar heights, not Earth-centred positions.
- [Official validation archive](https://sol.spacenvironment.net/JB2008/downloadables/jb2008validate.zip): a latitude/longitude/scalar-height cube; it cannot independently select the height datum.
- [SET input-format documentation](https://sol.spacenvironment.net/JB2008/downloadables/jb2008fileformats.pdf): driver-index formats, not a unique positional datum.
- [AIAA 2008-6438](https://sol.spacenvironment.net/JB2008/pubs/AIAA_2008-6438_JB2008_Model.pdf) and [SET TR 2008-002](https://spacewx.com/wp-content/uploads/2020/11/SET_TR2008_002.pdf): examined without finding a uniquely defined height datum or known-Cartesian datum fixture.

The internal 6356.766 km gravity constant does not by itself establish a spatial datum. Spherical and ellipsoidal coordinates are both reversible; reversibility does not select the convention. NRL's native header specifies geodetic latitude and altitude in kilometres; WGS84 itself is the project's explicit ellipsoid. JB's required geocentric latitude is retained, not silently replaced with geodetic latitude.

Candidate heights are independently calculated from the same Cartesian points, with official THETA added to native spacecraft RA. At 400 km, longitude-averaged density ratio to the retained convention:

| Candidate | Equator | 45° | 89.9° |
|---|---:|---:|---:|
| WGS84 ellipsoidal h | 1 | 1 | 1 |
| r − 6356.766 km | 0.6831 | 0.8262 | 1.00024 |
| r − 6378.137 km | 1 | 1.2121 | 1.4631 |
| r − 6371 km | 0.8795 | 1.0652 | 1.2869 |

Full equatorial/midlatitude/near-polar and multi-altitude results, including temperatures, are in the JSON. Candidates outside the adapter's nominal [120,1500] km domain are marked explicitly and excluded from in-domain claims. The official examples lack the known Earth-centred positions needed to settle the datum; the convention remains provisional despite the pipeline regression tests.

## Reproduce

External model source and raw SET fixtures stay outside the repository. Inspect upstream terms before acquisition. Pinned content hashes, not mutable URLs alone, define the assets. All commands below require a **fresh** output directory; retained evidence is never overwritten by reproduction.

Build/test configuration and exact executed native, kernel, driver, CASE and propagation commands are in this manifest and `../direct-v2/manifest.json`. On the exercised workstation:

```sh
cmake --build build-atmosphere-gcc --parallel
cmake --build build-atmosphere --parallel

LEO_JB2008_VALIDATION_DIR=/tmp/leo-atmosphere-deps/jb-validation \
LEO_SPICE_TEST_LSK=/tmp/leo-atmosphere-deps/naif0012.tls \
LEO_SPICE_TEST_SPK=/tmp/leo-atmosphere-deps/de432s.bsp \
LEO_ATMOSPHERE_TEST_PCK=/tmp/leo-atmosphere-deps/earth_000101_270105_261009.bpc \
LEO_ATMOSPHERE_TEST_LOW_PRECISION_PCK=/tmp/leo-atmosphere-deps/pck00011.tpc \
LEO_NRLMSISE00_TEST_CSSI=/tmp/leo-atmosphere-deps/SpaceWeather-All-v1.2_snapshot_20200224.txt \
ctest --test-dir build-atmosphere-gcc --output-on-failure -j 4

bash docs/atmosphere/evidence/validation-amendment-v1/reproduce.sh \
  /tmp/leo-jb-grid-reproduction

python3 scripts/nrlmsise00_ap_modes.py \
  --nrlmsise-root /tmp/leo-atmosphere-deps --build-dir build-atmosphere-gcc \
  --out /tmp/leo-ap-reproduction.json

python3 docs/atmosphere/altitude/altitude_datum_experiment.py \
  --jb2008 /tmp/leo-atmosphere-deps/JB2008.f \
  --vendor-driver '/tmp/jbzip-jb2008/JB2008 Code/JB08DRVAUTO.f' \
  --official-grid /tmp/leo-atmosphere-deps/jb-validation/JB2008_AUTO_OUT_2023_091_00.DAT \
  --fortran /tmp/leo-fortran/usr/bin/gfortran-13 \
  --coordinates-json docs/atmosphere/evidence/direct-v2/coordinates/coordinates.json \
  --out /tmp/leo-altitude-reproduction
```

The atmospheric acquisition helper extracts the validation files. Extract `JB2008 Code/JB08DRVAUTO.f` byte-identically from the pinned external ZIP for the altitude command; no licensed source is installed in the repository. The grid command reads/extracts official astronomy from that ZIP automatically, with hash verification and no no-op link stubs.

For the independent coordinate/time check, use `docs/atmosphere/validate_coordinates.py` with the recorded Astropy/ERFA/IERS-B versions; `coordinates.json` and the manifest contain exact arguments and asset hashes. The reusable real-provider boundary driver is `docs/atmosphere/altitude/boundary_probe.cpp`; before/after evidence includes link/run details and kernel/weather hashes.

No atmospheric output cache, interpolation, surrogate, memoisation, new force law or performance optimisation was introduced. Historical timings remain pinned in direct-v1 and are not described as newly measured direct-v2 performance.
