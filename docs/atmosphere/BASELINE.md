# Frozen direct-reference baseline `direct-v1`

Configuration `direct-reference-v1`, repository baseline `d3f7c1410ada915db3544da1fd37f7414f0febcc`. Machine-readable manifest: [`evidence/direct-v1/manifest.json`](evidence/direct-v1/manifest.json). Every value below was measured; nothing is transcribed from either model's equations. Historical numerical-audit and benchmark evidence are untouched, and this baseline does not overwrite them.

## Configuration actually in force

| Item | Value |
|---|---|
| Atmosphere models | Exponential (existing), NRLMSISE-00 `gtd7d` (Brodowski `bc9a2feb…`, release 20190709), JB2008 (official unmodified `JB2008.f`, SHA-256 `b18c1607…`) |
| NRLMSISE switches | `[0,1,1,…,1]` — switch 0 = 0 (native cgs, converted to SI by the adapter), all variation switches 1, daily Ap |
| Density definition | NRLMSISE drag-effective mass including anomalous O, ×10³ g→kg cm⁻³; JB2008 native kg/m³; Exponential spherical profile |
| Weather | CSSI v1.2 observed-only (Q = 0), official `SOLFSMY.TXT` and `DTCFILE.TXT`; JB lags F/S 1 d, M 2 d, Y 5 d, same-lag averages; DTC hourly interpolation + rounding |
| Frames | J2000 → ITRF93 via `earth_000101_270105_261009.bpc`; WGS84 a = 6 378 137 m, 1/f = 298.257223563 |
| Clocks | `naif0012.tls`; UTC quasi-MJD with real day lengths; epicycle geometry handled by CSPICE N0067 |
| Physical models | unchanged: central + J2, cold-beam Sentman drag/torque, conical-eclipse SRP, albedo 0.3, gravity gradient, corotation 7.292115e-5 rad/s about J2000 +Z |
| Integrator | `leo::propagate` adaptive Dopri5, rtol 1e-10, atols 1e-3 m / 1e-6 m s⁻¹ / 1e-12 / 1e-12, initial step 1 s, max step 15 s, 1 000 000 attempts; refinement = Boost controlled RKF7(8), rtol 1e-12, max step 5 s, same atols |
| Scenario | CASE-A-v1, CASE-B-v1, CASE-C-v1 unchanged; epoch 2003-10-30T00:00:00 UTC (ET = 120 744 064.182 s), 21 600 s duration, 30 s cadence |
| Build | GCC 13.3.0 Release `-O3 -DNDEBUG -Wall -Wextra -Wpedantic`; GNU Fortran 13.3.0 `-std=legacy`; no fast-math, no atmosphere-specific optimisation |
| Threading | single-threaded baseline; native calls serialised; 1/2/4-thread provider columns measured |

Caching: none. No density-result cache, memoisation, interpolation, surrogate, quantisation or trajectory precomputation exists anywhere in these measurements.

## Independent accuracy (measured)

| Oracle | Result |
|---|---|
| NRLMSISE-00, 17 published cases | density 2.944e-07, species 3.866e-07, temperatures 4.803e-07 (worst relative) |
| JB2008, 12 full-precision cases | 1.4717e-05 worst relative (upstream tolerance 2e-5) |
| JB2008, official SET grid, 13 328 rows | density 5.861e-03 max / 3.239e-03 RMS; temperatures 0.750 K / 0.746 K max |
| Coordinates vs ERFA `c2t06a` + final IERS-B | 2.96e-08 rad worst rotation; 2.47e-05 s worst ET |

## Matched model comparison

7 latitudes (−80…80°), 6 longitudes (6 h spacing, sweeping local solar time through 24 h), 17 altitudes (200–1000 km by 50 km), five epochs: quiet (2008-07-01, 2008-12-15), active (2001-03-20, 2003-10-28), storm (2003-10-30). 4 284 evaluated points, 1 428 per model, zero unusable.

Density ratio to the Exponential baseline (median / min / max over all points and epochs): NRLMSISE-00 **1.086 / 0.0331 / 25.16**; JB2008 **0.706 / 0.0301 / 45.80**. The wide spread is the exponential profile's spherical model-form limitation — it has no latitude, local-time or solar dependence — at the same physical locations, not an implementation difference. Both empirical models agree with each other far more closely than either agrees with a profile that carries none of the drivers they depend on. Ratio changes across altitude, latitude, local time, season and quiet→active→storm conditions exactly as expected for the drivers each model uses.

## Matched six-hour propagation (mean over 721 samples, storm epoch)

| Case | Model | Mean density (kg/m³) | Max drag accel (m/s²) | Max drag torque (N·m) | Cumulative drag work (J) | Matched specific energy change (J/kg) |
|---|---|---|---|---|---|---|
| A | Exponential | 7.480e-15 | 1.537e-09 | 1.201e-11 | −2.134 | −0.4404 |
| A | NRLMSISE-00 | 9.403e-14 | 3.741e-08 | 2.891e-10 | −26.79 | −2.905 |
| A | JB2008 | 1.137e-13 | 4.753e-08 | 3.661e-10 | −32.42 | −3.468 |
| B | Exponential | 7.336e-15 | 4.238e-07 | 8.723e-08 | −414.5 | −62.09 |
| B | NRLMSISE-00 | 1.017e-13 | 9.751e-06 | 2.010e-06 | −4052 | −415.1 |
| B | JB2008 | 1.127e-13 | 1.161e-05 | 3.340e-06 | −4978 | −506 |
| C | Exponential | 3.120e-12 | 6.331e-05 | 5.761e-05 | −8.253e+04 | −8253 |
| C | NRLMSISE-00 | 1.334e-11 | 3.496e-04 | 3.183e-04 | −3.521e+05 | −3.522e+04 |
| C | JB2008 | 1.230e-11 | 3.262e-04 | 3.004e-04 | −3.257e+05 | −3.257e+04 |

Density, drag acceleration, aerodynamic torque, cumulative drag work and the J2-consistent energy diagnostic scale coherently across models; the larger differences in CASE-B/C follow the model's own speed sensitivity. Cumulative non-gravitational work is recorded per run.

### Numerically separated differences

`refinement_*` fields in each run isolate integration differences (independent RKF7(8) at rtol 1e-12 / max step 5 s versus nominal Dopri5); `*_vs_*` fields isolate model-form differences at identical inputs. Worst refinement position difference after six hours: 2.2e-5 m (A), 4.7e-2 m (B), 6.5e+02 m (C); worst refinement energy difference 5.9e-6, 3.2e-2 and 4.1e+02 J/kg respectively. CASE-C remains the documented sensitive stress test: its refinement residual energy-work difference (0.66–13.3 J/kg against ~8 000–33 000 J/kg accumulated) is reported, not asserted against any threshold, and no attitude convergence threshold is claimed for it. Historical audit runs used ET = 0; this baseline uses a real historical epoch, so those results are not transferable.

## Direct performance baseline (uncached)

Median nanoseconds per call, 14 000 individually timed calls per model after 200 warm-up calls:

| Model | Raw native model call | Through `DirectAtmosphere` (includes SPICE rotation, geodetic conversion, weather lookup) | Acquisition overhead |
|---|---|---|---|
| Exponential | 40 | 60 | 20 |
| NRLMSISE-00 | 2 876 | 30 308 | 27 432 |
| JB2008 | 7 595 | 37 451 | 29 856 |

Native call ranges: NRLMSISE-00 2 826–228 886 ns, JB2008 7 514–197 838 ns; the exponential maximum (351 ns) is dominated by the clock-read overhead that dominates its 40 ns body, which is why batched means are reported alongside. Setup is excluded from every per-evaluation figure and reported separately: weather loading and CSPICE `furnsh` are process setup.

Full six-hour coupled propagation, three repeats each, identical to the runs above: wall 0.190 s (Exponential), 2.412 s (NRLMSISE-00), 2.962 s (JB2008); ~60 850 RHS evaluations and the same number of atmosphere evaluations per run (one direct evaluation per RHS call, never amortised); 3.18 µs per RHS/atmospheric call for NRLMSISE-00 and 4.86 µs for JB2008 against 3.13 µs total for the exponential profile. Concurrent provider throughput is recorded for 1, 2 and 4 threads; native serialisation means neither empirical backend scales with thread count.

These numbers are the reference point for any future caching proposal. No performance shortcut was introduced to obtain them.

## Reproduce

```sh
python3 docs/atmosphere/acquire.py --destination /tmp/leo-atmosphere-deps \
  --groups nrlmsise00 jb2008 weather kernels --acknowledge-external-terms

cmake -S docs/atmosphere -B build-atmosphere -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLEO_ENABLE_NRLMSISE00=ON -DLEO_ENABLE_JB2008=ON \
  -DLEO_NRLMSISE00_ROOT=/tmp/leo-atmosphere-deps \
  -DLEO_JB2008_SOURCE=/tmp/leo-atmosphere-deps/JB2008.f \
  -DCSPICE_ROOT=/path/to/cspice -DCMAKE_PREFIX_PATH='/path/to/eigen;/path/to/boost'
cmake --build build-atmosphere --parallel

python3 docs/atmosphere/run.py --binary build-atmosphere/atmosphere_experiment \
  --build-dir build-atmosphere --out docs/atmosphere/evidence/direct-v1 \
  --lsk .../naif0012.tls --spk .../de432s.bsp --pck .../earth_000101_270105_261009.bpc \
  --weather .../SpaceWeather-All-v1.2_snapshot_20200224.txt \
  --solfsmy .../SOLFSMY.TXT --dtcfile .../DTCFILE.TXT \
  --epoch-utc 2003-10-30T00:00:00 \
  --case docs/numerical-audit/experiments/CASE-{A,B,C}-v1.cfg
```

The runner refuses a non-empty output directory, so a rerun can never overwrite this record; write a new directory instead. The manifest records every source, kernel, data and binary hash, the resolved compilers and flags, Eigen/Boost header-tree hashes, the CSPICE library hash and runtime version, all switch and driver values, the exact command lines and the CPU and threading configuration.