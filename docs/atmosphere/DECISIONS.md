# Atmospheric engineering decisions

Append-only log. Source baseline: `d3f7c1410ada915db3544da1fd37f7414f0febcc`. Date: 2026-10-10. Changes are initially uncommitted; experiment manifests additionally hash the working sources.

## ATM-001 — Optional acquisition components (final)
Question: integrate empirical atmosphere into forces or environmental acquisition? Options: change kernel, replace exponential API, add optional providers. Selected: optional `leo::atmosphere` with explicit model-native input adapters and a source callable feeding existing `EnvironmentSnapshot::density_kg_m3`. Rationale: existing `EarthEnvironment` and `EnvironmentalDynamics` already separate acquisition and forces. Preserve exponential API, Sentman, geometry, torque, corotation and integrator defaults. Consequence: temperatures/composition are diagnostic only, not automatically gas-surface inputs. Limitation: existing physical force approximations remain. Evidence: `include/leo/environment/source.hpp`, `include/leo/kernel/environment.hpp` at source baseline.

## ATM-002 — JB2008 external source (final pending explicit upstream permission)
Question: may the official source be redistributed? Options: vendor, use a verified port, externally compile official Fortran. Selected: external local official Fortran file. Evidence: https://spacewx.com/jb2008/ links official `jb2008.zip`; `JB2008 Code/JB2008_ReadMe.rtf` says Copyright 2008 Space Environment Technologies, All rights reserved, points to a licence that is not supplied as an explicit grant in the archive. No redistribution permission inferred from public download. Rationale: genuine official model integration without copying restricted code into repository. Consequence: optional Fortran compiler and explicitly acquired source; no configuration downloads. Limitation: users must establish applicable use terms; public CI cannot assume licensed assets. Rejected: vendoring (unclear terms), new C++ translation (unverified equations).

## ATM-003 — Direct reference evaluation (final)
Question: optimise expensive empirical calls? Options: caching, interpolation, raw evaluation. Selected: direct raw reference calls for every request, only immutable weather records are loaded once. User requirement. No memoisation, density grids, surrogates, quantisation or trajectory precomputation. Consequence: uncached wall times are the baseline; setup measured separately.

## ATM-004 — Frames, clocks and fair comparisons (final)
Question: silently use spherical altitude/approximate rotation? Options: extrapolated rotation, SPICE ITRF93, SPICE IAU_EARTH separately labelled. Selected: explicit SPICE frame parameter, baseline ITRF93 if historical binary PCK coverage is acquired; no frame fallback. WGS84 a=6378137 m, inverse flattening=298.257223563 for geodetic height. MSISE geodetic latitude; JB2008 geocentric latitude and ellipsoidal height with inertial right ascension/Sun declination, documented separately from official driver's nominal geocentric height grids. UTC comes from CSPICE LSK, not elapsed ET. Evidence: official JB2008.f input header; existing propagator J2000 contract. Consequence: physical location shared but model-specific latitude differs. Limitation: Earth-fixed conversion requires covering frame kernels. Revised conventions, if needed after official validation, will receive superseding entries.

## ATM-005 — Weather is explicit observed archive (final)
Question: missing/proxy/predicted data? Options: climatological defaults, forecast substitution, fail. Selected: immutable explicitly supplied local records; fail on absent required records and distinguish source status. JB F/S and averages lag 1 UTC day, M 2, Y 5, from official JB2008.f. DTC is official hourly storm correction, not fabricated from Ap. MSISE uses previous UTC-day observed F10.7, current centred 81-day observed average, current daily Ap or complete seven-element history. Evidence: official input comments and NRLMSISE input specification to be pinned. Consequence: coverage must include lags/history and hourly endpoint. Limitation: archive status information must not be invented when source lacks it.

## ATM-006 — Canonical corrected NRLMSISE pin and licence (final)
Question: retain the initially inspected 20151122 GitHub mirror? Options: mirror commit `a5f81be6c8802e9014f94457b41d3484673e0bfb`, canonical corrected `bc9a2feba4344e74201281e563332688a4d09cc3`, rewrite fixes ourselves. Selected: canonical Brodowski release 20190709 from https://git.linta.de/~brodo/nrlmsise-00.git. Evidence: canonical commit fixes `alt == 32.5 km` NaN and incorporates prior compiler-warning/local-initialisation fixes. Rejected stale mirror because an existing upstream correctness fix should not be discarded. No local changes to reference equations. Consequence: source SHA-256 `a20d6523420188241963f095ad0b65df44ac0a6fa6db52d2e1df823c366f0285` enforced at configure. Upstream DOCUMENTATION only provides a warranty disclaimer, not an explicit redistribution grant; use externally acquired sources for NRLMSISE too. Underlying government Fortran provenance does not automatically establish C-translation rights. Limitation: upstream use terms remain user's responsibility; no claim of cleared redistribution. Supersedes initially inspected mirror as candidate dependency, not a published model requirement.

## ATM-007 — Immutable archived CSSI weather mirror (final)
Question: primary CelesTrak download times out from this workstation. Options: invent defaults, drive MSISE from JB proxies with uncertain observed/adjusted definition, use archived original CSSI records. Selected: original `SpaceWeather-All-v1.2_snapshot_20200224.txt` archived in Orekit at commit `6fdf97c133c1ab8c56d7608688419bf4becacf80`; acquisition URL https://raw.githubusercontent.com/CS-SI/Orekit/6fdf97c133c1ab8c56d7608688419bf4becacf80/src/test/resources/atmosphere/SpaceWeather-All-v1.2_snapshot_20200224.txt. Original header UPDATED 2020 Feb 22; retain source fields, not Orekit's interpolation/cache logic. Selected fixed-field v1.2 with explicit OBSERVED/DAILY_PREDICTED/MONTHLY_PREDICTED sections, observed F10.7 at actual Earth distance and observed centred 81-day field. Reject non-observed records or flux quality other than measured Q=0; never substitute forecasts/gap-filled values. Consequence: experiments use historical epochs before February 2020. Limitation: supplied centred averages inherit archive construction and do not expose constituent-day quality for all 81 days; this pipeline does not recompute them.

## ATM-008 — Native weather indexing and leap-second extension (final)
Question: driver DTC is continuous or integer? Selected: official hourly DTC linear index interpolation followed by `floor(value+0.5)`; this is NOT density interpolation. Daily solar records selected by calendar day with native lags, independent of elapsed TDB. Native driver's first-hour special case and its `1e-8 day` epsilon are deliberately not inherited: they are indexing workarounds, not model equations; exact hour labels use current record. UTC day and seconds retained through leap seconds; UTC quasi-MJD divides by actual day length, and only last DTC hour stretches to 3601 s on a leap day. MSISE's final three-hour bin retains its current-day identity during the leap second. Rationale: avoid early next-day lookup and discontinuous fractional-day representations. Consequence: leap-day temporal interpretation is an explicit engineering extension because official legacy JB driver assumes 86400 s/day. Limitation: no official JB leap-second fixture is supplied. Missing hourly endpoints fail, including at file end.

## ATM-009 — JB common equator basis and support domains (final)
Question: combine fixed latitude with J2000 right ascension? Rejected incompatible axes. Selected: rotate both spacecraft and geometric Sun by the same SPICE J2000→ITRF93 matrix; supply geocentric latitude/solar declination in that common terrestrial-equator basis. The source uses its two right-ascension values only as `H = SAT(1)-SUN(1)`, so replacing both by longitudes in the common rotated basis is algebraically invariant to rotation about its z axis; no model approximation or fallback is introduced. Ellipsoidal WGS84 height remains the selected project height for JB (an explicit engineering convention, not an assertion that the published grid defines a WGS84 ellipsoid). Native fixtures bypass acquisition and reproduce their official geocentric-latitude/height inputs. Consequence: provider comparisons share the same physical position and height, with model-specific latitude. Project supported native domains: MSISE 0–1000 km, JB 120–1500 km; narrower than unrestricted extrapolation, not claimed sharp scientific validity bounds. Baseline comparisons 200–1000 km, official JB fixture grid 120–600 km. Limitation: JB official driver uses approximate Sun/GMST rather than precise SPICE; provider output need not match that approximate driver at bit level.

## ATM-011 — Documented round-off margin at the altitude boundary (final)
Question: a grid point requested at exactly 1000 km returns 1000.0000000000019 km after the J2000→Earth-fixed→geodetic round trip and trips the project domain check. Options: clamp the altitude, widen the grid, reject the point, accept a stated round-off margin. Selected: accept an explicit 1e-9 km margin (thousands of times the observed 2e-12 km residual) and evaluate the exact value received. Evidence: reproduced analytically and observed in the first fixture run. Rejected clamping because it silently evaluates a different altitude than requested, and rejection because it removes a legitimate in-domain point for floating-point representation alone. Consequence: a request genuinely above 1000 km by more than the margin still fails; nothing is rounded, quantised or approximated. Supersedes the experimental note that an endpoint rejection is the correct response. Applies to the NRLMSISE altitude guard; the JB2008 domain bounds sit far from any requested endpoint.

## ATM-012 — MSISE Ap history slots absent, never defaulted (final)
Question: when switch 9 selects daily Ap, what occupies the six unused history slots? Options: raw default 4, NaN. Selected: NaN, with slot 0 carrying the real daily Ap. Rationale: a stored climatology-like default in a struct that a caller could later enable would silently become a driver value. Consequence: the native adapter validates and forwards only when the history switch is selected, so absent slots never reach the reference. Recorded because an observed fixture depends on the slots being well defined when requested, not when unused.

## ATM-013 — Provenance: host substitution recorded, content unchanged (final)
Question: primary hosts (celestrak.org, and the primary CSSI distribution) were unreachable during acquisition. Selected: acquire the byte-identical pinned archive from an available mirror, record the mirror, its commit, the original file's embedded release stamp and the content hash, and state plainly that no CelesTrak record was obtained. Rejected generating values locally or relaxing record checks. Consequence: content provenance is the original archive; transport provenance is the mirror, and both are documented. No record is described as a CelesTrak observation anywhere in this project.

## ATM-014 — Provenance records may overlap in the CSSI archive (final)
Question: the archived CSSI file repeats recent dates inside predicted and monthly-fit sections after the observed block. Selected: parse each section with its own monotonicity check and let an observed record win over any later forecast or fit record for the same day; reject duplicate observed days. Evidence: the real archive's observed block ends 2020-02-22 and its fit block starts 2020-02-23, but future files may overlap. Rejected a global monotonicity requirement, which would reject the genuine published file. Consequence: predictions remain unusable as drivers, and overlap is a defined outcome rather than a load failure.

## ATM-012a — Fixture status honesty
Both reference model suites print their measured residuals as JSON so the first real run supplies the numbers instead of pre-computed claims. Documentation of accuracy status is derived from those measured values; no numerical claim in this project originates from a transcription of either model's equations.
## ATM-010 — Preserve exponential's spherical spatial definition (final)
Question: should the new selection provider pass WGS84 ellipsoidal height to the existing spherical exponential profile? Initially considered shared-height inputs for all three. Selected: exponential retains `norm(r_J2000) - 6378137 m`, exactly the existing EarthEnvironment convention; empirical models receive their model-specific coordinates at the same physical position. Rationale: changing the exponential's spatial definition would change more than the selected atmosphere and add unnecessary SPICE cost to a lightweight density lookup. Consequence: matched physical locations need not have equal native altitude values, particularly near poles; report this as the baseline's existing spherical model-form approximation. Raw native timing remains distinct from provider acquisition. Supersedes DESIGN.md's initially considered optional shared-height exponential comparison and any common-height interpretation of ATM-009 for exponential. No production exponential API or force law changed.

## ATM-015 — Recover exact compiler configuration in new manifests (confirmed)

| Field | Record |
|---|---|
| Problem | `run.py` asked `cmake_cache` for compiler paths/flags but omitted `CMAKE_BUILD_TYPE`; `compiler.build_type` was therefore null in `direct-v1` despite a Release build. |
| Alternatives | Infer Release from the flags; edit the historical manifest; read the actual cache value in future manifests. |
| Selection | Add `CMAKE_BUILD_TYPE` to the requested cache keys. Preserve historical evidence and explain the omission in a versioned amendment. |
| Justification | Actual runner invocation before the fix produced null; the same invocation after it produced `Release`. |
| Parameters | GCC/GNU Fortran 13.3.0; C++ `-O3 -DNDEBUG -Wall -Wextra -Wpedantic`, C `-O3 -DNDEBUG`, Fortran `-O3 -std=legacy`; binary64, no fast-math. |
| Consequences | Metadata correction only. Coordinate output JSON was identical before/after; atmospheric source and input settings are untouched. |
| Validation | `evidence/validation-amendment-v1/manifest-provenance-smoke.json`; commands and observed before/after values retained. |
| Status | Confirmed. Historical null means an omitted field, not an unknown build configuration: the inspected cache and actual compiler commands establish Release. |

## ATM-016 — Daily Ap is a selected baseline mode (confirmed interface)

| Field | Record |
|---|---|
| Problem | A reader must distinguish upstream geomagnetic capabilities, adapter exposure, and the mode used in historical propagation. |
| Alternatives | Daily Ap; explicit seven-slot history; silently change the historical baseline. |
| Selection | Retain `[0,1 × 23]`, switch 9 = 1 and `ap_history=false` for `direct-v1`. History remains explicitly selectable: constructor switches[9] = −1, `DirectAtmosphere(..., frame, true)`, runner `--ap-history`. |
| Justification | Integrated native header/C implementation and adapter/provider code expose both paths; the published seventeen-case oracle includes two history cases. Additional unequal-slot/timing evidence is recorded in the amendment. |
| Parameters | Switch 0 = 0 gives native cgs, converted to SI; switch 0 = 1 gives native SI. History slots: daily, current 3 h, −3/−6/−9 h, means of preceding 12–33 h and 36–57 h bins. No interpolation of Ap or atmospheric output. |
| Consequences | Selecting history can change density and temperature; it changes model inputs/response, not integration correctness. No baseline mode change. |
| Validation | Published cases 15/16 alone use equal slots and cannot establish ordering. See the separate native, unequal-slot and UTC-bin evidence linked in `VALIDATION.md`. |
| Status | Interface confirmed; numerical validation results and limits are stated in the versioned amendment, not inferred from API presence. |

## ATM-017 — Amend validation, preserve measured physical evidence (confirmed for documentary corrections)

| Field | Record |
|---|---|
| Problem | Historical source SHA identifies a dirty tree, compiler build type was omitted, and model-comparison counts/medians in prose disagree with the frozen JSON. |
| Alternatives | Rewrite `direct-v1`; regenerate all trajectories; preserve numerical files and publish a versioned validation/provenance amendment. |
| Selection | Retain `direct-v1` unchanged; use `validation-amendment-v1` for new investigation and corrected interpretation. The original manifest's source hashes identify the physical implementation. |
| Justification | All twelve atmospheric source/header hashes at inspected HEAD match the original manifest. Fresh matched-location fixture evaluation is byte-identical to the frozen file. |
| Parameters | Five epochs × seven latitudes × six longitudes × seventeen altitudes = 3 570 evaluations/model, 10 710 total; longitude spacing 60° / 4 h. Actual aggregate median ratios: NRLMSISE/exponential 1.7284952584619058, JB/exponential 1.5303465758538617. |
| Consequences | Corrects earlier prose 4 284 / 1 428 counts, 6 h spacing and 1.086 / 0.706 medians; changes no density, temperature, force, torque, trajectory or work record. History-mode diagnostics are separate experiments, never substitutions into the daily baseline. |
| Validation | `initial-inspection.json` and `physical-baseline-smoke.json` under the amendment: all 10 710 density/temperature outputs identical, fixture SHA-256 `01d16c5c9f08f3b6df5955a07fdb7d93215e7c6a45023d8d4500a9f2577d820c`. No propagation remeasurement is required for these metadata/prose corrections. |
| Status | Confirmed. Future direct comparisons must carry both the immutable physical baseline and its validation amendment. |

## ATM-018 — JB2008 boundary round-off guard and `direct-v2` (confirmed defect)

| Field | Record |
|---|---|
| Problem | Strict `height < 120` / `height > 1500` rejects exactly-on-boundary Cartesian requests after coordinate/frame round-off. Example: requested 120 km at equator/longitude 0 recovers 119.99999999999906 km. |
| Alternatives | Clamp native height; nudge or remove fixture endpoints; change the physical datum; admit a documented representation-error margin. |
| Selection | `Jb2008::evaluate` admits `1e-9 km` at both endpoints and evaluates the received altitude unchanged. No coordinate-adapter datum or reference-model equation changes. |
| Justification | Independently constructed positions and the real rotation reproduce signed height errors of a few `1e-12 km`; MSISE already applies the same margin at its upper boundary (ATM-011). A true excursion exceeding the margin still fails. |
| Parameters | Nominal native supported interval [120,1500] km; numeric guard [120−1e-9,1500+1e-9] km. Margin is 1 μm, not a density-validation tolerance. |
| Consequences | Previously rejected representation-level endpoints become evaluable; no native input is clamped. Preserve `direct-v1` and freeze a corrected-source `direct-v2` with comparison evidence rather than conceal the implementation change inside a documentation-only amendment. |
| Validation | Boundary regressions, explicit failing-before/passing-after smoke and fresh matched fixtures/CASE-A/B/C six-hour propagation are recorded in `direct-v2` and the validation amendment. |
| Status | Confirmed defect; selected correction supersedes ATM-017's amendment-only policy for the production-code fix, but not its immutable-history or documentary-statistics decisions. |

## ATM-019 — JB2008 height remains an explicit project convention (provisional)

| Field | Record |
|---|---|
| Problem | Upstream specifies geocentric latitude but only “Height of Position (km)” for SAT(3), without a unique height datum. |
| Alternatives | WGS84 geodetic height; radius offsets from 6378.137 km, WGS84 semi-minor radius, 6356.766 km internal gravity radius, or 6371 km mean radius; geodetic versus specified geocentric latitude. |
| Selection | Retain existing WGS84 ellipsoidal height with geocentric latitude. No physical coordinate-adapter correction or new selectable datum. |
| Justification | Source, official driver, SET index-format documentation and JB papers are scalar-height specifications, without Earth-centred positions that establish a datum. The internal gravity radius alone is insufficient. Examined primary locators/hashes are in `altitude-datum.json`. Retention avoids a material unsupported physics change. |
| Parameters | WGS84 a = 6378137 m, inverse flattening = 298.257223563; h in km; geocentric latitude atan2(z,hypot(x,y)); common Sun/spacecraft ITRF93 equator basis. |
| Consequences | Competing in-domain datums materially change density. At 400 km, longitude-averaged r−6356.766 ratios are 0.6831/0.8262/1.00024 at 0°/45°/89.9°; r−6378.137 ratios are 1/1.2121/1.4631. These are candidate sensitivity diagnostics, not production corrections. |
| Validation | Unmodified Fortran at independently calculated candidate heights; official THETA reconstructs candidate RA. Coordinate/native consistency, poles/wrapping/boundary regressions pass. See `altitude-investigation-corrections.json` for superseded omitted-THETA diagnostics. |
| Status | Latitude confirmed; physical height datum unresolved upstream; project convention provisional. Supersedes any upstream-datum interpretation of ATM-004/009, while retaining their actual implementation. |

## ATM-020 — Quantify rounding, leave complete grid attribution unresolved

| Field | Record |
|---|---|
| Problem | Reported JB grid max/RMS were not accompanied by a measured causal explanation; aggregate-oracle comparisons cannot establish rounding as the cause. |
| Alternatives | Assume printer effects; relax tolerances; replace printed inputs with current archive; reconstruct official astronomy/native calls, perturb indices and measure residuals. |
| Selection | Preserve all original thresholds. Record historical 13 328 entries, correct duplicate weighting to 13 320 sampled rows, independently evaluate all 545 528 published rows, and classify complete attribution as unresolved. |
| Justification | Density-bit-exact adapter/native agreement over the full grid; DSTDTC exact at eight published epochs; local compiler arithmetic differences ≤6.90e-15 relative density. Correctly lagged current archive differs in seven solar fields and makes sampled RMS 54–60× worse. Original dated records and producer compiler/model build are absent. |
| Parameters | Unsigned abs(rho−rho_ref)/rho_ref, kg/m³; RMS over all valid evaluation entries. Solar perturbation ±0.5 index unit; derivative h=1 and 0.5 with divisor 2h and central Richardson (4D(h/2)−D(h))/3. Bounded nonlinear fits use 46 rows/epoch. Density print maximum half-ulp 5e-5. |
| Consequences | Per-epoch hypothetical rounding offsets remove 92.7–97.4% of sampled RMS but leave 9.11e-5–2.25e-4; no searched epoch reaches print precision. Rounding is quantitatively plausible, not proven to fully explain the grid. Fits do not recover original records, rule out optimisation limits or establish one shared daily vector. |
| Validation | Full reproducible grid/sensitivity/fit pipeline and twenty worst-case records in `jb2008-grid-investigation.json`; all original native model acceptance bounds retained. Corrected sampling changes RMS 0.0032388564212367911→0.0032398286550430365, max unchanged. |
| Status | Measurements confirmed; complete causal attribution unresolved. Supersedes earlier unquantified or fully-attributed rounding claims, not the empirical model equations or twelve-case acceptance bound. |
