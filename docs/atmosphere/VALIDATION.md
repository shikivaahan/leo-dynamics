# Validation

Independent oracles first, then boundary/metamorphic behaviour, then matched physical comparisons. A wrapper is never compared only with itself. Fixed-formatted external reference data is pinned by SHA-256 and never copied into this repository.

## 1. Independent reference fixtures

### NRLMSISE-00 — published 17-case reference output (external)

Oracle: the `DOCUMENTATION` file of Brodowski release `bc9a2feba4344e74201281e563332688a4d09cc3`, SHA-256 `e7db7296…`, parsed at run time by `tests/nrlmsise00_tests.cpp`. Cases cover quiet/solar-active/Ap-high conditions, geodetic latitudes 0/60, longitudes 0/−70, local solar time 4/16, altitudes 0/10/30/50/70/100/400/1000 km, both switch-9 paths, day 81 and 172, and F10.7 70/150/180.

Measured (GCC Release, direct `gtd7d`):

| Quantity | Worst relative error over 17 cases |
|---|---|
| Drag-effective mass density | 2.9441e-07 |
| Constituent number densities (all 8) | 3.8659e-07 |
| Both temperatures | 4.8028e-07 |

Tolerance 1e-6 relative covers the 5e-7 worst-case seven-significant-digit publication rounding with a deliberately small margin; a tighter tolerance would measure the printer, a looser one would validate nothing. The oracle itself is checked: recomputing the `gtd7` mass from the printed constituents reproduces the printed `RHO` column, and the drag quantity demonstrably differs from it (over 10 % at 1000 km).

### JB2008 — twelve higher-precision native-input reference densities

Oracle: `LegacyInputParams` / `checkLegacy.rhoRef` of `JB2008Test.java` (Apache-2.0) at pinned commit `6fdf97c133c1ab8c56d7608688419bf4becacf80`, parsed outside the repository; that file names no Fortran driver or build for these rows, so their producer origin is undocumented. Twelve cases span −85.24°…74.36° geocentric latitude, 373–399 km and DSTDTC 3–327 K. “Full precision” in the historical test name means supplied double literals rather than rounded whole-index grid headers; it does not establish unrounded original measurements. Solar proxies have tenths, solar angles generally four decimals, heights hundredths of a kilometre, and reference density literals about eight significant digits. Their earlier, higher-precision originals are unavailable.

Measured maximum unsigned `abs(computed-reference)/reference` density error **1.4717e-05**, against the unchanged inherited **2e-5** bound. All twelve references are positive, about 1.8730e-12–8.3267e-12 kg/m³; there is no zero-reference guard or near-zero floor in this test. The result establishes agreement with the supplied reference cases, not physical truth or full producer precision.

### JB2008 — official SET validation output (external)

Oracle: `jb2008validate.zip` SHA-256 `4d18c286…`, eight epochs of 2023 DOY 91, each file verified by its own pinned digest at run time. Its printed solar inputs have whole-index resolution (`F8.0`), whereas the source records use tenths (`F6.1`); solar angles have 1e-4 rad resolution. Quantitative attribution belongs to the versioned investigation below, not to a comparison of aggregate residuals from different oracles.

Historical measurement over 13 328 evaluation entries (stride 41, 120–600 km, −90…90° latitude, 0–360° longitude; the first row is evaluated twice in each epoch):

| Quantity | Measured | Threshold | Margin |
|---|---|---|---|
| RHO relative, max | 5.8606e-03 | 1.5e-2 | 2.56× |
| RHO relative, RMS | 3.2389e-03 | 6.0e-3 | 1.85× |
| Exospheric temperature, max | 0.750 K | 1.5 K | 2.00× |
| Temperature at position, max | 0.746 K | 1.5 K | 2.01× |

The gap between the two oracles' aggregate residuals is not a causal explanation. The final investigation below measures input sensitivity directly, and still leaves complete attribution unresolved. These unchanged thresholds are broad structural-consistency checks, not backend accuracy figures.

Both JB density metrics use unsigned `abs(computed-reference)/reference`, with density in kg/m³ and a dimensionless result; `5.8606e-3` therefore is approximately 0.58606% relative density disagreement. The grid RMS is `sqrt(sum(relative²)/N)` over every sampled evaluation entry, including duplicated first-row endpoints; it is not a density-weighted RMS or an absolute error. The historical test has no zero-reference guard or near-zero floor. All actual grid references are positive; the amendment records their range. Twelve-case metrics use the same reference denominator. Grid temperature maxima are unsigned absolute kelvin differences. Printed grid outputs use `2F8.1,D15.5`: nearest 0.1 K (half-width 0.05 K) and five significant density digits.

NRLMSISE published-case metrics instead use `abs(computed-reference)/max(abs(computed),abs(reference))`; equal values (including two zeros) return zero, non-finite unequal values return infinity, and no near-zero floor is applied. Its density oracle reconstructs `gtd7d` from printed constituent values; it is not the published `gtd7` RHO column.

Both JB2008 oracles are evaluated through the unmodified official Fortran core reached by an owned `ISO_C_BINDING` bridge; no equation was rewritten.

## 2. Coordinates, time and space weather (independent oracles)

| Property | Oracle | Result |
|---|---|---|
| WGS84 geodetic ⇄ Earth-fixed | NIMA TR8350.2 parameters restated in the test; equator, pole, 45° geometry; geocentric latitude 44.80758° ≈ 21.4 km offset | pass |
| Near-pole / anti-meridian / wrap round trip | 480-point matrix, altitude −500 m…1000 km, longitudes 0/37/179.5/180/−180/−90/270/359.9, latitude ±89.9999999° | pass |
| Pole convention | longitude 0, height `|z|−b`, including a sub-threshold transverse offset | pass |
| ITRF93 rotation | `c2t06a` (IAU 2006/2000A) with final IERS-B UT1 and polar motion, 4 epochs incl. a leap second | max 2.96145e-08 rad; position bound 0.207302 m at 7 000 km (earlier 2.2e-5 m prose was a unit-transcription error) |
| UTC epoch, TDB conversion | Astropy UTC/TT/TDB scales | max 2.47e-05 s |
| Leap second and year boundary | IERS TAI−UTC table (32/36/37 s), not SPICE | date, day of year, day count and 86 401 s day length exact; seconds of day to 10 ms (TDB−TT ≤ 1.7 ms) |
| Frame explicitness | Unsupported frame rejected; ITRF93 ≠ IAU_EARTH; both orthonormal right-handed | pass |
| CSSI lags, Ap offsets, bins, quality flags, section overlap, prediction rejection | Synthetic fixed-field archives byte-verified against real archived rows at the loader's field offsets | pass |
| SOLFSMY 1/2/5-day lags with same-lag averages, provenance rejection, malformed rows | Synthetic + real archives | pass |
| DTC hourly interpolation, rounding, hour-23 borrow, leap-day stretch | Synthetic archives | pass |

Coordinates were independently validated against ERFA/IERS-B, not against another call of the same SPICE wrapper; `evidence/coordinate-reference-v1.json` records the oracle versions and asset hashes.

## 3. Metamorphic and domain behaviour

Exact repeat evaluation and order independence; eight-thread bitwise equality for NRLMSISE-00's serialised global state; unit-system switch agreement at 1e-12; longitude and local-solar-time wrapping identity; common equator-origin shift preserving JB2008's hour angle at 1e-12; DSTDTC entering JB2008's exospheric temperature linearly; native identifier independence; finiteness and non-negativity of every density, temperature and constituent; deterministic failure for every out-of-domain, missing, predicted, interpolated or unsupported-provenance input; concurrent provider evaluation agreeing with serial results.

Received heights are never clamped. Explicit representation margins admit coordinate round-off: MSISE at 1000 km, JB2008 at both 120 and 1500 km. The native core evaluates the exact floating-point height received.

## 4. Builds and regressions exercised

| Build | Result |
|---|---|
| GCC Release, both backends, CSPICE, tests | Historical 153/153; final **166/166 pass, zero skips**, including all pre-existing kernel, integration, analysis and SPICE regressions |
| Kernel-only and analysis-only configurations | preserved and unchanged |
| Clang Debug ASan/UBSan with both backends | **blocked by an external toolchain limitation, not by project code**: the private GNU Fortran prefix cannot link `-fsanitize=address,undefined` (no `libasan_preinit.o`/`-lasan` for that compiler). The clang-instrumented C++ and C reference paths were therefore *not* exercised under sanitizers, and no sanitizer claim is made for them. |

Kernel-only builds require neither the C reference nor Fortran. Existing numerical-audit and benchmark evidence were not modified.

## 5. Limitations of this validation

Empirical model-physics uncertainty is not implementation error. Driver averages/provenance are taken as supplied; DTCFILE has no per-record quality flag. JB2008's complete grid attribution remains unresolved because original dated inputs/build provenance are unavailable; its physical height datum is unresolved in examined primary sources. The one-day grid cannot establish independent solar/activity trends. CASE-C remains a sensitive stress test without an attitude-convergence acceptance threshold. Optional fixture SKIPs are visible; the final run supplied every required external fixture and skipped none.

## 6. Final validation amendment and corrected-source baseline

Evidence index: [`validation-amendment-v1/manifest.json`](evidence/validation-amendment-v1/manifest.json). Physical baseline: [`direct-v2/manifest.json`](evidence/direct-v2/manifest.json); measured differences: [`comparison-to-direct-v1.json`](evidence/direct-v2/comparison-to-direct-v1.json). `direct-v1` remains byte-for-byte historical evidence.

### JB2008 discrepancy: quantified, not fully explained

| Population / experiment | Maximum relative density discrepancy | RMS relative density discrepancy |
|---|---:|---:|
| Historical 13 328 evaluation entries (eight duplicated first rows) | 0.0058605859224980525 | 0.0032388564212367911 |
| Corrected 13 320 distinct sampled rows, same inputs | 0.0058605859224980525 | 0.0032398286550430365 |
| Complete 545 528 published rows, official astronomy reconstructed | 0.005889466852069977 | 0.0032377632961419992 |

The historical duplicate removal changes weighting, not model output; maximum discrepancy is unchanged. The full-grid comparison uses reconstructed official SUNPOS/THETA, unlike the printed-angle/IAU-1982 sampled test, so sampling and input preparation are identified separately.

`jb2008-grid-investigation.json` records the twenty worst cases with epoch, native coordinates, indices, DSTDTC, reference/computed density, signed and absolute differences, relative errors, input precision and provenance. All twenty are at 600 km, at 12 UTC; strata cover altitude, latitude, longitude, hour, DSTDTC and density magnitude. These are descriptive patterns, not independent activity/cause determinations. Grid reference densities span **5.2864e-14–2.5156e-8 kg/m³**; zero-reference count is zero. The standalone metric excludes and counts zero references, with no near-zero floor; this differs from the historical unchecked denominator.

Central finite differences use **h = 1 and 0.5 index unit**, divisor `2h`, and Richardson `(4 D(h/2) − D(h))/3`. Separate nonlinear ±0.5-index fits to **46 rows/epoch** remove **92.7–97.4%** of sampled RMS but leave **9.11e-5–2.25e-4**, above the maximum density-print half-ulp **5e-5** in all eight epochs. Fits are hypothetical perturbations, not recovered source records; neither per-epoch fits nor per-row corner containment establishes one shared original daily input vector. Search failure does not prove that no explanatory vector exists.

The present SOLFSMY archive differs from the printed solar set in seven of eight fields, even after exact 1/2/5-day calendar lags. Its replacement-input sampled RMS is about **0.185–0.188**, **54–60× worse**. It is not the missing 2023 vintage. DSTDTC reconstructs exactly at all eight published hourly epochs. Original input precision/source-version and producer compiler differences remain possible; **rounding is quantitatively plausible, not a complete demonstrated explanation**.

Adapter versus unmodified standalone Fortran is **density-bit-exact over all 545 528 rows**. A 552-case native-input smoke also verifies both temperatures exactly; local GNU Fortran `-O0` versus `-O3` differs by at most **6.90e-15 relative density / 5.68e-13 K** (`compiler-arithmetic.json`). That does not identify the unpublished producer toolchain. No equation, unit, lag or native ABI defect was demonstrated.

### Height convention and actual boundary fix

Examined primary source/document hashes and locators are in `altitude-datum.json`. `SAT(2)` is explicitly geocentric latitude; `SAT(3)` is height in kilometres without a unique datum. The official scalar-height cube has no Earth-centred positions and cannot choose a datum.

Retain **WGS84 ellipsoidal height plus geocentric latitude**, explicitly provisional as an upstream-datum interpretation. Independently built equatorial, midlatitude and near-polar points at several heights were evaluated with the unmodified native core. At 400 km, longitude-averaged density ratios for `r−6356.766 km` are **0.6831 / 0.8262 / 1.00024** at 0° / 45° / 89.9°; for `r−6378.137 km`, **1 / 1.2121 / 1.4631**. The ambiguity is physically material. Diagnostic candidates outside [120,1500] km are explicitly flagged and excluded from in-domain claims. Candidate RA includes official THETA; the earlier omitted-THETA experiment is explicitly superseded in `altitude-investigation-corrections.json`.

`src/atmosphere/jb2008.cpp` now admits **1e-9 km** at each height boundary, without clamping. Real provider smoke: **10/22 boundary rejections before → 0/22 after**, with **3/3** interior controls evaluated both times. Permanent regressions cover equator, both midlatitudes, near poles/poles, longitude wrapping, native-coordinate consistency, exact endpoints and genuinely out-of-domain heights.

### NRLMSISE geomagnetic modes

Daily Ap is **the selected baseline mode, not the only upstream or adapter mode**. Upstream and adapter expose switch 9 = 1 daily and −1 history; provider `ap_history=true` and runner `--ap-history` select history explicitly. Baseline remains `[0,1 × 23]`, `ap_history=false`. Seven-slot order: daily/current/−3/−6/−9 h/mean 12–33 h/mean 36–57 h. Loader timing, tail coverage, leap-second/year crossings, unequal-slot native agreement, absent-history errors, unit systems and the real provider are tested.

`ap-modes.json` independently compiles the pinned C source, links the built native archive, and calls the adapter. Native SI and adapter agree exactly; cgs/SI density rounding differs by at most **3.43e-16** relative. Its deliberately synthetic 400 km input set uses daily Ap **23** and unequal history **[23,118,7,156,44,91,12]**; density changes **4.71101023967667e-12 → 5.5668950247596604e-12 kg/m³** (about **+18.17%**) and local temperature about **+4.91%**. These independent diagnostic fields are not claimed as a recovered historical weather sequence.

The real observed storm-provider case is separately recorded in `ap-provider-historical-case.json`: **2003-10-30T10:00:00 UTC**, WGS84 **60° / −70° / 400 km**, same archive and physical position/time in both modes. Daily Ap **191**, history **[191,39,56,154,300,203.875,24.875]**; density **7.49848838776156e-12 → 6.478659717899017e-12 kg/m³**, signed change **−13.60046%**. Both outputs match direct native evaluation. Mode differences need not have the same sign; neither result is an integration error or a baseline-mode change.

### Before/after physical experiments

All **10 710 matched-location outputs** and all nine **six-hour CASE-A/B/C × model** reruns match `direct-v1`: zero stored density, temperature, drag-force-magnitude, drag-torque-magnitude, position, attitude and cumulative-work difference. Every nominal/refined trajectory checksum and every non-timing convergence diagnostic is identical. Timing fields are not physical outputs; historical direct-v1 performance remains separately pinned, not relabelled as newly measured.