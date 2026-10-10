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

### JB2008 — twelve full-precision reference densities

Oracle: `LegacyInputParams` / `checkLegacy.rhoRef` of `JB2008Test.java` (Apache-2.0) at pinned commit `6fdf97c133c1ab8c56d7608688419bf4becacf80`, parsed outside the repository; that file names no Fortran driver for these rows, so their upstream origin is undocumented. Twelve cases span −85.24°…74.36° geocentric latitude, 373–399 km, DSTDTC 3–327 K and full precision solar proxies.

Measured maximum relative density error **1.4717e-05**, against the upstream tolerance of 2e-5 relative (inherited unmodified, 26 % margin). This is the primary JB2008 accuracy evidence.

### JB2008 — official SET validation output (external)

Oracle: `jb2008validate.zip` SHA-256 `4d18c286…`, eight epochs of 2023 DOY 91, each file verified by its own pinned digest at run time. The printed fixture rounds its solar indices to whole sfu (source records carry one decimal) and its solar angles to 1e-4 rad, so agreement is limited by printed-input resolution, not by this implementation; the full-precision fixtures above are the sharper oracle.

Measured over 13 328 sampled rows (stride 41, 120–600 km, −90…90° latitude, 0–360° longitude):

| Quantity | Measured | Threshold | Margin |
|---|---|---|---|
| RHO relative, max | 5.8606e-03 | 1.5e-2 | 2.56× |
| RHO relative, RMS | 3.2389e-03 | 6.0e-3 | 1.85× |
| Exospheric temperature, max | 0.750 K | 1.5 K | 2.00× |
| Temperature at position, max | 0.746 K | 1.5 K | 2.01× |

The 398× gap between this residual and the full-precision residual is consistent with printed-input rounding dominating it, but is not by itself proof of that attribution: the two oracles also sample different regions. These thresholds are measured and calibrated, and stay provisional for input rounding only; they are not an accuracy figure.

Both JB2008 oracles are evaluated through the unmodified official Fortran core reached by an owned `ISO_C_BINDING` bridge; no equation was rewritten.

## 2. Coordinates, time and space weather (independent oracles)

| Property | Oracle | Result |
|---|---|---|
| WGS84 geodetic ⇄ Earth-fixed | NIMA TR8350.2 parameters restated in the test; equator, pole, 45° geometry; geocentric latitude 44.80758° ≈ 21.4 km offset | pass |
| Near-pole / anti-meridian / wrap round trip | 480-point matrix, altitude −500 m…1000 km, longitudes 0/37/179.5/180/−180/−90/270/359.9, latitude ±89.9999999° | pass |
| Pole convention | longitude 0, height `|z|−b`, including a sub-threshold transverse offset | pass |
| ITRF93 rotation | `c2t06a` (IAU 2006/2000A) with final IERS-B UT1 and polar motion, 4 epochs incl. a leap second | max 2.96e-08 rad (2.2e-5 m at 7 000 km) |
| UTC epoch, TDB conversion | Astropy UTC/TT/TDB scales | max 2.47e-05 s |
| Leap second and year boundary | IERS TAI−UTC table (32/36/37 s), not SPICE | date, day of year, day count and 86 401 s day length exact; seconds of day to 10 ms (TDB−TT ≤ 1.7 ms) |
| Frame explicitness | Unsupported frame rejected; ITRF93 ≠ IAU_EARTH; both orthonormal right-handed | pass |
| CSSI lags, Ap offsets, bins, quality flags, section overlap, prediction rejection | Synthetic fixed-field archives byte-verified against real archived rows at the loader's field offsets | pass |
| SOLFSMY 1/2/5-day lags with same-lag averages, provenance rejection, malformed rows | Synthetic + real archives | pass |
| DTC hourly interpolation, rounding, hour-23 borrow, leap-day stretch | Synthetic archives | pass |

Coordinates were independently validated against ERFA/IERS-B, not against another call of the same SPICE wrapper; `evidence/coordinate-reference-v1.json` records the oracle versions and asset hashes.

## 3. Metamorphic and domain behaviour

Exact repeat evaluation and order independence; eight-thread bitwise equality for NRLMSISE-00's serialised global state; unit-system switch agreement at 1e-12; longitude and local-solar-time wrapping identity; common equator-origin shift preserving JB2008's hour angle at 1e-12; DSTDTC entering JB2008's exospheric temperature linearly; native identifier independence; finiteness and non-negativity of every density, temperature and constituent; deterministic failure for every out-of-domain, missing, predicted, interpolated or unsupported-provenance input; concurrent provider evaluation agreeing with serial results.

Grid endpoints are not clamped: a requested 1000 km point is passed exactly, and any round-off rejection is reported rather than hidden by a silent grid adjustment.

## 4. Builds and regressions exercised

| Build | Result |
|---|---|
| GCC Release, both backends, CSPICE, tests | **153/153 pass**, including all pre-existing kernel, integration, analysis and SPICE regressions |
| Kernel-only and analysis-only configurations | preserved and unchanged |
| Clang Debug ASan/UBSan with both backends | **blocked by an external toolchain limitation, not by project code**: the private GNU Fortran prefix cannot link `-fsanitize=address,undefined` (no `libasan_preinit.o`/`-lasan` for that compiler). The clang-instrumented C++ and C reference paths were therefore *not* exercised under sanitizers, and no sanitizer claim is made for them. |

Kernel-only builds require neither the C reference nor Fortran. Existing numerical-audit and benchmark evidence were not modified.

## 5. Limitations of this validation

Empirical model-physics uncertainty is not implementation error: the 81-day averages and provenance codes are taken as supplied, `DTCFILE` carries no per-record quality flag, the official JB grid passes its rounded inputs, JB's altitude datum is undefined by the distributed source, and only 2023 DOY 91 has a published JB grid. Case-C remains a stress test with no asserted attitude threshold. Convergence of a refined run is a numerical statement, not model accuracy. Fixture SKIPs occur only when external, separately-licensed assets are absent and are always visible.