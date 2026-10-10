# Input, time and coordinate conventions

Normative detail for every scalar that crosses the boundary into a reference
model: what the pipeline computes, which convention it uses, where the value
came from, and where it is stored for reproduction. `PARAMETERS.md` catalogues
the model-side constants and native switches; this file catalogues the input-side
conventions and their provenance. Source-level values are normative in
`evidence/upstream-assets.json`.

Sources for acquisition are `https://spacewx.com/jb2008/` (vendor index page) and
the NAIF generic-kernel archive. This project redistributes none of them.

---

## 1. Time conventions

### 1.1 Clocks

| Identifier | Meaning | Value / rule | Units | Origin | Alternatives | Reproducibility |
|---|---|---|---|---|---|---|
| `leo::Epoch::et_seconds` | Ephemeris epoch carried by the propagator | TDB seconds past J2000 | s | Existing kernel contract | None | Scenario files, unchanged |
| `SpiceEphemerisProvider::utc_calendar` | ET to UTC calendar decomposition | See 1.2 | mixed | CSPICE LSK conversion | Elapsed-time UTC, TDB-as-UTC (rejected) | `UtcCalendar` fields |
| `UtcCalendar::year/month/day` | UTC calendar date | From `et2utc_c` | — | LSK | — | Epoch manifest |
| `UtcCalendar::day_of_year` | 1-based UTC day of year | `today - Jan 1 + 1` | d | Calendar arithmetic | — | Epoch manifest |
| `UtcCalendar::days_since_1970` | Epoch day index | `sys_days` count | d | Calendar arithmetic | MJD-only (rejected) | Epoch manifest |
| `UtcCalendar::seconds_of_day` | Labelled UTC seconds since midnight | 3600h + 60min + s | s | `et2utc_c` | — | Epoch manifest |
| `UtcCalendar::day_length_seconds` | Real length of that UTC day | `round(ET(next midnight) - ET(midnight))`, accepted 86399 / 86400 / 86401 | s | LSK + rounding | Fabricated fixed 86400 (rejected) | Epoch manifest + LSK hash |
| `UtcCalendar::mjd` | UTC quasi-MJD | `40587 + days_since_1970 + seconds_of_day / day_length_seconds` | d | Project convention | TDB JD substitution (rejected) | Epoch manifest |

`seconds_of_day` reaches exactly 86400 during a positive leap second, and
`day_length_seconds` is 86401 on such a day, so the pair stays inside
`[0, day_length_seconds)` without a synthetic leap-second calendar.

**No leap second, DOY or MJD value is fabricated.** Every one of these fields
comes from the LSK-backed conversion. If the LSK is absent, `utc_calendar`
raises rather than approximating.

### 1.2 Leap-second arithmetic used by the tests

The test epochs are derived from the IERS leap-second table, independently of
SPICE: with `TT = TAI + 32.184 s`,

| UTC instant | TAI − UTC | TT − UTC |
|---|---|---|
| 2000-01-01 | 32 s | 64.184 s |
| 2016-12-31 | 36 s | 68.184 s |
| 2017-01-01 (after the leap second) | 37 s | 69.184 s |

2016-12-31 is the leap second day: 2017-01-01T00:00:00 UTC begins
`86401` ET seconds after 2016-12-31T00:00:00 UTC, and `23:59:60` labels the
instant exactly `86400` ET seconds after it.

These constants use TT where SPICE reports TDB. The periodic `TDB − TT` term is
bounded by 1.7 ms by definition, so UTC *seconds of day* are asserted to 10 ms.
Integer-valued fields (date, day of year, day count, day length, and the 1 s gap
between the leap-second label and the next midnight) are asserted exactly.

---

## 2. Coordinate conventions

### 2.1 Ellipsoid and conversions

| Identifier | Meaning | Value / rule | Units | Origin | Alternatives | Reproducibility |
|---|---|---|---|---|---|---|
| `wgs84_a_m` | Semi-major axis | 6378137.0 | m | WGS84 / NIMA TR8350.2 | Any model ellipsoid | `coordinates.hpp` |
| `wgs84_f` | Flattening | 1/298.257223563 | — | WGS84 | — | `coordinates.hpp` |
| semi-minor `b` | Derived | `a(1-f)` = 6356752.314245179 | m | WGS84 | — | Computed in test oracle |
| first eccentricity squared `e²` | Derived | `f(2-f)` = 0.00669437999014132 | — | WGS84 | — | Computed in test oracle |
| `earth_fixed(Geodetic)` | Geodetic to Earth-fixed | `n = a/sqrt(1-e²sin²φ)`; `x=(n+h)cosφcosλ`, `y=(n+h)cosφsinλ`, `z=(n(1-e²)+h)sinφ` | m | Standard geodetic transform | Spherical (rejected) | `coordinates.cpp` |
| `geodetic(EarthFixed)` | Earth-fixed to geodetic | Latitude by fixed-point iteration; `h = p·cosφ + z·sinφ − n(1−e²sin²φ)` | rad, m | Standard inverse | Bowring-only (rejected) | `coordinates.cpp` |
| iteration limit | Non-convergence guard | 16 | iterations | Project | — | `coordinates.cpp` |
| convergence threshold | Latitude convergence | 2e-15 | rad | Project | — | `coordinates.cpp` |
| polar threshold | Longitude branch | `hypot(x,y) < 1e-9` → latitude ±90°, longitude 0 | m | Project | Undefined longitude (rejected) | `coordinates.cpp` |
| minimum altitude | Domain guard | `altitude_m > -b` | m | Project | — | `coordinates.cpp` |

Longitude is returned by `atan2`, so it always lies in (-180°, 180°]. No wrapping
is applied on input: any equivalent 360° multiple denotes the same point, and
`earth_fixed` is invariant under it. At the poles longitude is not physically
defined, so the documented convention returns 0 rather than propagating an
arbitrary value.

Geodetic and geocentric latitude are never conflated. At 45° geodetic the
geocentric latitude is 44.807576784° — an offset of 0.1924°, about 21.4 km of
ground. Both quantities are asserted in the tests.

### 2.2 Frames

| Identifier | Meaning | Value | Origin | Alternatives | Reproducibility |
|---|---|---|---|---|---|
| `SpiceEphemerisProvider::earth_fixed_rotation` | J2000 to Earth-fixed | `pxform_c("J2000", frame, et)` | CSPICE + PCK | Any other frame string → `std::invalid_argument` | PCK hash, frame name |
| default `DirectAtmosphere` frame | Baseline frame | `ITRF93` | NAIF PCK | `IAU_EARTH` (mean equator/equinox of date, lower accuracy, must be requested explicitly) | Provider argument, manifest |
| fallback behaviour | Absent Earth orientation kernel | None. Missing coverage raises | Project | IAU_EARTH-only fallback (rejected) | — |

There is **no frame fallback**: an unsupported frame name or missing kernel
coverage is an error, never a silent substitution.

### 2.3 Model-specific spatial inputs

| Model | Latitude | Height | Evidence |
|---|---|---|---|
| NRLMSISE-00 | Geodetic | WGS84 ellipsoidal | Native `g_lat`, `g_long`, `alt` |
| JB2008 | Geocentric (`atan2(z, hypot(x,y))`) | Ellipsoidal height, km | Official `JB2008.f` input header (`SAT2` geocentric latitude) |

The two models deliberately receive different latitudes for the same physical
point. That is a model-form difference, recorded per model, not an inconsistency.

### 2.4 Solar geometry

| Identifier | Meaning | Rule | Units | Origin |
|---|---|---|---|---|
| NRLMSISE `local_solar_time_hours` | Local mean solar time | `(utc_seconds/3600 + longitude_deg/15) mod 24` | h | Reference header recommendation; no hidden equation-of-time correction |
| JB2008 `sun_ra_dec_rad` | Solar right ascension and declination | Rotated geometric Sun, common equator basis | rad | Official `SUN1`/`SUN2` |
| JB2008 `position_ra_lat_alt[0]` | Spacecraft right ascension | Earth-fixed longitude in the same basis | rad | Official `SAT1`; only the hour angle `H = SAT1 − SUN1` is used |

The Sun position comes from `SpiceEphemerisProvider::geometric_state`, which is
target Sun (10), observer Earth (399), frame J2000, geometric correction NONE.

---

## 3. Weather inputs

All three archives are **immutable, explicitly supplied local files**. The loader
parses fixed-field records once and holds them; no density, memoisation or
interpolation is performed on model output.

### 3.1 CSSI v1.2 (`SpaceWeather-All-v1.2_snapshot_20200224.txt`)

Fixed-field row, 130 columns:
`I4,I3,I3,I5,I3,8I3,I4,8I4,I4,F4.1,I2,I4,F6.1,I2,5F6.1`

| Columns | Field | Use | Acceptance rule |
|---|---|---|---|
| 1–10 | `yy mm dd` | Record date | Strictly increasing across the file |
| 19–42 | 8 × Kp | Not consumed | — |
| 47–78 | 8 × Ap | 3-hourly Ap, 8 bins | All values `[0,400]` |
| 79–82 | `Avg Ap` | Daily Ap (`ap`, `ap_history[0]`) | `[0,400]` |
| 99–100 | `Q` | Quality flag | **Must be 0.** 1 interpolated, ≥2 missing |
| 101–112 | Adjusted `Ctr81`/`Lst81` | Not consumed | — |
| 113–118 | Observed `F10.7` | `f107` for the **next** UTC day | `> 0` |
| 119–124 | Observed `Ctr81` | `f107_average` for the record's own day | `> 0` |

Records outside a `BEGIN OBSERVED` block are predictions regardless of `Q`, and
are rejected. Only the fixed-field v1.2 layout is parsed; CSV variants are not
accepted.

### 3.2 NRLMSISE driver mapping

| Native input | Source | Lag / window | Units |
|---|---|---|---|
| `f107` | CSSI observed `F10.7` of day M−1 | 1 UTC day | sfu |
| `f107_average` | CSSI observed `Ctr81` of day M | Current day, 81-day centred | sfu |
| `ap` | CSSI `Avg Ap` of day M | Current day | index |
| `ap_history[0]` | Same as `ap` | Current day | index |
| `ap_history[1]` | 3-hour bin containing the query | 0 h | index |
| `ap_history[2..4]` | Earlier bins | −3, −6, −9 h | index |
| `ap_history[5]` | Mean of 8 bins | 12–33 h | index |
| `ap_history[6]` | Mean of 8 bins | 36–57 h | index |

Bin selection is `min(7, seconds_of_day / 10800)`. Offsets that cross midnight walk
back into previous days rather than clamping.

When the history is not requested, `ap_history[1..6]` are **NaN**. They are not
quiet Ap values and not climatology: a consumer that indexes them without
checking `std::isfinite` fails loudly instead of evaluating against a default.

### 3.3 SOLFSMY (`SOLFSMY.TXT`)

Whitespace-separated: `YYYY DDD JulianDay F10 F81c S10 S81c M10 M81c Y10 Y81c Ssrc`.

The Julian day must equal `2440587.5 + days_since_1970 + 0.5` (12 UT of the
record day), and no trailing fields may follow `Ssrc`.

**Vendor lag specification**, quoted from the file header:

> for model inputs the values should be used as a daily value between 0-24 UT for
> a given calendar date; F10 and S10 are 1-day lagged, M10 is 2-day, and Y10 is
> 5-day lagged in JB2008; the 81-day centered values are used with the same
> respective lag times.

| Native input | Record day | Units | Permitted range |
|---|---|---|---|
| `f10`, `f10_average` | M − 1 | sfu | ≥ 40 |
| `s10`, `s10_average` | M − 1 | sfu-equivalent | ≥ 40 |
| `m10`, `m10_average` | M − 2 | sfu-equivalent | ≥ 40 |
| `y10`, `y10_average` | M − 5 | sfu-equivalent | ≥ 40 |

**Provenance codes.** `Ssrc` carries one character per index (F10, S10, M10, Y10)
and is enforced, not decorative. Rejected: `0` (spline-filled or missing) and `F`
(mean of bordering values). Accepted sets are measured, derived, corrected and
composite codes. The header defines these per index; the accepted set is recorded
in `require_proxy` and is a closed list.

### 3.4 DTCFILE (`DTCFILE.TXT`)

Fixed-field row, 109 columns: `DTC ` + `I4` year + ` ` + `I3` day of year + 24 ×
`I4` hourly values.

| Identifier | Meaning | Rule | Units |
|---|---|---|---|
| `Jb2008Input::dstdtc_k` | Storm temperature correction | Hour selected as `min(23, seconds_of_day/3600)`; linearly interpolated toward the next hour's record and rounded with `floor(x + 0.5)` | K |
| next hour | Successor record | `hours[h+1]`, or `00:00 UT` of the next day when `h == 23` | K |
| hour length | Denominator | 3600 s, or `day_length_seconds − 23*3600` on a leap-second day | s |
| permitted values | Per-record integers | `[0, 2000]` | K |

Interpolating the **index** is a documented project choice; the reference driver
does the same. Atmospheric **density** is never interpolated.

### 3.5 Failure behaviour

Every one of the following raises rather than substituting a value:

- a required record absent from the archive (`std::runtime_error`);
- a required record not marked observed (predicted, interpolated or missing);
- a solar driver outside its permitted range, including a negative centred value;
- an inconsistent UTC query (date, day of year, seconds of day or day length);
- a malformed or non-monotonic archive (`std::invalid_argument`).

There is **no climatological default, no forecast substitution and no
interpolation of a missing index**.

---

## 4. Asset provenance

Machine-readable normative list: `evidence/upstream-assets.json`. Acquisition is
explicit and outside CMake: `docs/atmosphere/acquire.py --destination <outside
repo> --groups ... --acknowledge-external-terms`. Every asset is verified by
SHA-256; a mismatch aborts rather than re-pinning.

| Asset | Version / revision | Source | Provenance status |
|---|---|---|---|
| `nrlmsise-00.c`, `nrlmsise-00.h`, `nrlmsise-00_data.c`, `DOCUMENTATION`, `nrlmsise-00_test.c` | 20190709, commit `bc9a2feba4344e74201281e563332688a4d09cc3` | `git.linta.de/~brodo/nrlmsise-00.git` | Published reference; used unmodified |
| `jb2008.zip` → `JB2008.f` | official distribution README 2023-05-24 | `sol.spacenvironment.net/JB2008/downloadables/` | **All rights reserved**; acquired externally, not vendored |
| `jb2008validate.zip` | 2023-09-19 published output | same | Official validation cases |
| `SpaceWeather-All-v1.2_snapshot_20200224.txt` | CSSI 1.2, UPDATED 2020 Feb 22 | Orekit mirror, commit `6fdf97c133c1ab8c56d7608688419bf4becacf80` | Original observed/predicted sections preserved; `Q=0` only accepted |
| `SOLFSMY.TXT` | release 8_1_0, 2026-10-10 | `sol.spacenvironment.net` | Native per-proxy provenance codes preserved |
| `DTCFILE.TXT` | retrieved 2026-10-10 | `sol.spacenvironment.net` | **No per-record quality flag is supplied** |
| `earth_000101_270105_261009.bpc` | 2026-10-09 high-precision Earth orientation | NAIF generic kernels | ITRF93 |
| `naif0012.tls` | NAIF0012 | NAIF generic kernels | Leap seconds |
| `de432s.bsp` | DE432s | NAIF generic kernels | Planetary ephemeris |

### 4.1 Stated limitations of provenance

1. **CSSI mirror, not the primary host.** The archive is byte-identical to the
   pinned Orekit mirror of the original CSSI file. It is used because
   `celestrak.org` space-weather endpoints were unreachable at acquisition time
   (see 4.2). The content is the original; only the host differs.
2. **No CelesTrak archive was obtained.** `celestrak.org` returns HTTP 200 for
   its home page but 404s or times out on every space-weather archive endpoint
   (`/SpaceWeather/`, all `fileseason.php?FILE={F10.7,F10.7cm,Ap,ApID,Ap3h}`,
   all `/pub/SpaceWeather/` variants, `radio_flux.json`, `Ap3h.json`). NOAA SWPC
   serves only a current 1-minute Kp feed. Therefore **no file in this project is
   a CelesTrak observation**; the CSSI archive above is the NRLMSISE driver source
   and it is identified as such everywhere it is used. It is not described as
   CelesTrak data.
3. **`DTCFILE.TXT` carries no per-record status.** The file has no quality flag,
   so observed, interpolated and missing hours cannot be distinguished from the
   record itself. The loader validates structure and range, and applies the same
   interpolation the reference driver applies. Provenance beyond "the official
   file, as distributed" is **unknown**, not assumed.
4. **`SOLFSMY.TXT` records derived values as first-class data.** The provenance
   codes identify how each index was produced; codes outside the accepted
   measured/derived/corrected/composite set are rejected rather than silently
   consumed.
5. **Redistribution terms are not inferred.** Public availability is not a
   licence. External code and assets stay outside the repository.
6. **Kernel versions are pinned, not bundled.** Orientation, leap-second and
   planetary kernels are acquired separately; no build downloads anything.

---

## 5. Reproducibility summary

A result is reproducible from: project git SHA; the model name, version and
upstream revision; the asset SHA-256 values in `evidence/upstream-assets.json`;
the epoch and its exact time interpretation (§1); the coordinate frame, ellipsoid
and per-model latitude convention (§2); the solar and geomagnetic input
conventions (§3); the model switches and parameter values in `PARAMETERS.md`; and
the scenario, integrator and tolerances in `DECISIONS.md` / `BASELINE.md`. Each
experiment manifest records the same set together with compiler, build flags and
command lines.

Weather records are the only data held in memory: they are parsed once into
immutable maps and never used as a density cache.