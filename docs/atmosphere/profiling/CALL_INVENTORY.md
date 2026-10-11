# Compiled density-request call inventory

Source revision `a2eebd1078cbcfd68cfd8a9692c5b5ceabe9caff`. Line references below are to unchanged originals, not generated diagnostic copies. Both optional backends and CSPICE are compiled. Valid successful-request path; error branches are listed with their triggering check, not artificially benchmarked by throwing. No constructor, weather-file parsing, kernel loading or destruction executes per request.

Notation: **T** ET/UTC time, **P** J2000 position, **W** immutable weather records, **C** model switches/frame/constants/kernel configuration. “External” means CSPICE or pinned native model; standard-library/Eigen operations are named but not treated as third-party scientific models. All densities SI kg/m³. Inclusive timings contain their nested rows; do not sum a parent and its children. Stage names match `profile.hpp`.

## Ordered NRLMSISE-00 request

| Order | Caller → operation/callee | Exact original source | Inputs → outputs / units | External; lock | Dependency | Timing |
|---|---|---|---|---|---|---|
| N1 | `DirectAtmosphere::evaluate` → `isfinite`, Eigen `allFinite` | `src/atmosphere/provider.cpp:47–49` | ET s; position m → validity, throw on failure | no; no | T,P | `validation` |
| N2 | evaluate → `earth_fixed_rotation` | `provider.cpp:52` → `src/environment/spice.cpp:147–158` | ET s, ITRF93 → dimensionless 3×3 rotation | CSPICE; process-wide SPICE mutex | T,C | `rotation`; nested inventory below |
| N3 | evaluate → Eigen matrix-vector product | `provider.cpp:53` | rotation × J2000 m → Earth-fixed m | no; no | T,P,C | `transform` |
| N4 | evaluate → `geodetic` | `provider.cpp:54` → `src/atmosphere/coordinates.cpp:16–34` | Earth-fixed m → WGS84 latitude/longitude rad, height m | no; no | T,P,C | `geodesy`; nested below |
| N5 | evaluate → `utc_calendar` | `provider.cpp:55` → `spice.cpp:117–146` | ET s → calendar, seconds/day, day length s, quasi-MJD days | CSPICE; same SPICE mutex | T,C | `utc`; nested below |
| N6 | evaluate → `WeatherDataset::nrlmsise(utc,ap_history_)` | `provider.cpp:59` → `src/atmosphere/weather.cpp:135–162` | UTC + daily/history configuration → native drivers | no; no | T,W,C | `weather`; N6a–g |
| N6a | nrlmsise → `validate_utc` → `serial`, `serial_doy` | `weather.cpp:136` → `47–54`, `11–21` | date/day-of-year/sod/daylength → consistency checks using chrono dates | no; no | T | `weather_validation` |
| N6b | nrlmsise → local `get` → `required` → `std::map::find` | `weather.cpp:137–143`, `36–40` | serial day and previous day → observed CSSI records (two tree searches) | no; no | T,W | `cssi_current`, `cssi_previous` |
| N6c | nrlmsise → `require_positive`, `require_ap` and field assignments | `weather.cpp:144–148`, `41–46` | previous-day observed F10.7 sfu; current centred-81-day F10.7 sfu; daily Ap index → input, seven-element history initialisation | no; no | T,W | `cssi_fields` |
| N6d | history branch → bin arithmetic, local `ap`, local `get`, `required` | `weather.cpp:149–159` | current three-hour bin; offsets 0–19 → 20 additional observed-record searches, four recent Ap values and two eight-bin means | no; no | T,W,C | `ap_history`, only history mode |
| N6e | daily branch | `weather.cpp:149–161` | history disabled → no three-hour lookup, ap_history[0] daily; other entries NaN | no; no | C | empty branch overhead inside `ap_history` interval, not history computation |
| N7 | evaluate → rad→deg and m→km assignments; `fmod` local solar time | `provider.cpp:60–62` | geometry, UTC hours + longitude/15 → degrees, km, local solar hours modulo 24 | no; no | T,P | `geometry` |
| N8 | evaluate → `Nrlmsise00::evaluate` | `provider.cpp:63` → `src/atmosphere/nrlmsise00.cpp:18–65` | complete native input → SI result | native C; separate NRL mutex | T,P,W,C | `native`; N8a–e |
| N8a | native adapter → `isfinite`, bounds/leap-year checks; optional history check/copy | `nrlmsise00.cpp:19–40` | 8 scalar inputs, year/doy/altitude/latitude/flux/Ap; optional 7 Ap values → validated input | no; no | T,P,W,C | `native_validation` |
| N8b | adapter → wrap lambda/`fmod`, native structs, `std::copy` | `nrlmsise00.cpp:41–47` | longitude/LST/switches → native input, flags, zeroed output | no; no | T,P,W,C | `native_construct` |
| N8c | adapter → `std::lock_guard` | `nrlmsise00.cpp:48–51` | process-wide NRL mutex → acquired/RAII release | no; NRL only | concurrency | `native_lock`; release in `native_output` |
| N8d | adapter → `gtd7d` | `nrlmsise00.cpp:50` | native cgs input/switches → densities, exospheric/local temperatures K; anomalous oxygen included in total | pinned C; NRL held | T,P,W,C | `native_execute` |
| N8e | adapter → mass/number scaling, constituent loop and result validation | `nrlmsise00.cpp:52–65` | g/cm³ ×1000 → kg/m³; cm⁻³ ×1e6 → m⁻³; positive finite temperatures K | no; no after native release | native output,C | `native_output` |
| N9 | result return/destructors/branch overhead | `provider.cpp:58–64` | Result by value | no; no | C | inclusive `provider` residual |

NRL switches are created once, not per request (`provider.cpp:16–19,23–28`; `include/leo/atmosphere/nrlmsise00.hpp:9–13`). Actual standard switches: zero=0, 1–23=1; daily Ap switch9=1, diagnostic history switch9=-1. The historical manifest's prose saying SI switch1 disagrees with its own switch array and the compiled code. We retain the actual baseline array and adapter conversion.

## Ordered JB2008 request

| Order | Caller → operation/callee | Exact original source | Inputs → outputs / units | External; lock | Dependency | Timing |
|---|---|---|---|---|---|---|
| J1–J5 | same validation, orientation, transform, geodetic, UTC sequence as N1–N5 | `provider.cpp:47–55` | same shared inputs/products | same SPICE mutex, otherwise no | T,P,C | same shared stages |
| J6 | evaluate → `WeatherDataset::jb2008` | `provider.cpp:68` → `weather.cpp:163–182` | UTC/W → MJD, solar proxies, DTC K | no; no | T,W | `weather` |
| J6a | weather → `validate_utc` | `weather.cpp:164`, `47–54`, `11–21` | same consistency validation as N6a | no; no | T | `weather_validation` |
| J6b | weather → `required` → map searches, lags | `weather.cpp:165–167`, `36–40` | day−1 F10/S10; day−2 M10; day−5 Y10 → three SOLFSMY records | no; no | T,W | `solar_fs`, `solar_m`, `solar_y` |
| J6c | weather → four `require_proxy` calls, assignment, 8 scalar checks | `weather.cpp:168–173`, `55–61` | per-proxy provenance codes; proxy/averages sfu-equivalent → native fields (must be ≥40) | no; no | T,W | `solar_fields` |
| J6d | weather → `required`, hourly bin, optional tomorrow search | `weather.cpp:174–176` | day, h=min(23,sod/3600) → two DTC hourly indices; h23 obtains next-day hour0 | no; no | T,W | `dtc_lookup` |
| J6e | weather → final-hour leap-day stretch, interpolation, `floor(x+.5)` | `weather.cpp:177–182` | hourly K values + fractional hour → integer-valued dstdtc K | no; no | T,W | `dtc_interpolate` |
| J7 | evaluate → `SpiceEphemerisProvider::geometric_state(Sun,epoch)` | `provider.cpp:69` → `spice.cpp:87–106` | target10, ET s → geometric Earth-relative J2000 Sun position m and velocity m/s | CSPICE; SPICE mutex | T,C | `sun_total`; nested below |
| J8 | evaluate → rotation × Sun position | `provider.cpp:70` | matrix × m → Earth-fixed Sun m | no; no | T,C | `sun_transform` |
| J9 | evaluate → `atan2`, `hypot`, angular/native field assignment | `provider.cpp:73–75` | fixed Sun vector → Sun longitude/declination rad; fixed spacecraft → longitude/geocentric latitude rad; WGS84 height km | no; no | T,P,C | `geometry` |
| J10 | evaluate → `Jb2008{}.evaluate` | `provider.cpp:76` → `src/atmosphere/jb2008.cpp:11–44` | native input → Result | external Fortran; JB-only mutex | T,P,W | `native` |
| J10a | adapter → 15 `isfinite` checks and domain bounds | `jb2008.cpp:12–29` | MJD, angles, altitude km, proxies, DTC K → validated inputs | no; no | T,P,W | `native_validation` |
| J10b | adapter → wrap lambda/`fmod`, array construction | `jb2008.cpp:30–34` | longitude-like angles → [0,2π), stack arrays, zeroed output | no; no | T,P | `native_construct` |
| J10c | adapter → `std::lock_guard` | `jb2008.cpp:35–39` | JB mutex → acquire/release | no; JB held for bridge/model only | concurrency | `native_lock`; release in output interval |
| J10d | adapter → `leo_jb2008` → `JB2008` | `jb2008.cpp:37–38` → `src/atmosphere/jb2008_bridge.f90:2–19` | C pointers, `c_double` arrays → official Fortran model; rho kg/m³, temp K | Fortran reference; JB held | T,P,W | `native_execute` includes ABI bridge (not independently separable at this scale) |
| J10e | adapter → finite/nonnegative density and positive temperature checks, Result construction | `jb2008.cpp:40–44` | rho/temp → SI Result; no constituent outputs | no; after native release | native output | `native_output` |
| J11 | result return/temporary/destructor/branch overhead | `provider.cpp:67–77` | Result by value | no; no | C | inclusive `provider` residual |

JB uses WGS84 ellipsoidal height provisionally and geocentric latitude, exactly as direct-v2. This task does not resolve its documented upstream altitude-datum uncertainty or alter the science.

## Shared nested SPICE calls (ordered within each parent)

| Parent; order | Caller → operation | Exact source | Inputs → outputs; units | External; lock scope | Dependency | Timing |
|---|---|---|---|---|---|---|
| rotation 1 | wrapper → finite ET and allowed frame check | `spice.cpp:148–149` | ET s, string → validity | no; before lock | T,C | `rot_validation` |
| rotation 2 | wrapper → mutex acquisition | `spice.cpp:150` | process-wide mutex → acquired | no; acquired until function return | concurrency | `rot_lock` |
| rotation 3 | wrapper → `enter_spice` → `reset_c`, `erract_c("SET",RETURN)` | `spice.cpp:151`, `19–24` | CSPICE global error state → reset/RETURN action | CSPICE; held | C/global state | `rot_prepare` |
| rotation 4 | wrapper → `pxform_c("J2000",frame,ET)` | `spice.cpp:152–153` | ET s + frame → matrix | CSPICE; held | T,C | `pxform` (zero-initialisation included) |
| rotation 5 | wrapper → `failed_c`, nested row/column copy, return | `spice.cpp:154–157` | raw matrix → Eigen matrix | failed_c CSPICE; held until return | output | `rot_extract`; unlock included in outer residual |
| UTC 1–3 | finite check, mutex, `enter_spice` | `spice.cpp:118–120`, `19–24` | ET s/error state → valid locked entry | CSPICE prep; held to return | T,C/concurrency | `utc_validation`, `utc_lock`, `utc_prepare` |
| UTC 4 | wrapper → `et2utc_c(ET,"ISOC",9,64,text)` | `spice.cpp:121–123` | ET s → nanosecond-resolution UTC text | CSPICE; held | T,C | `et2utc`; failed_c subsequently in parsing interval |
| UTC 5 | wrapper → `sscanf` | `spice.cpp:124–126` | ISO text → y,m,d,h,min,sec | standard C; held | T | `utc_parse` |
| UTC 6 | wrapper → chrono date construction/validation, today/tomorrow, two `snprintf` | `spice.cpp:127–136` | date → midnight ISO strings + civil-day values | standard library; held | UTC date | `utc_date_format` |
| UTC 7 | wrapper → first `str2et_c` | `spice.cpp:137` first call | current midnight ISO → start ET s | CSPICE; held | UTC date,C | `midnight_first` |
| UTC 8 | wrapper → second `str2et_c` | `spice.cpp:137` second call | tomorrow midnight ISO → end ET s | CSPICE; held | UTC date,C | `midnight_second` |
| UTC 9 | wrapper → failed check, subtraction/`round`, length bounds | `spice.cpp:138–141` | end−start ET → UTC day length 86399–86401 s | failed_c CSPICE; held | UTC date,C | `utc_daylength` |
| UTC 10 | wrapper → day serial and chrono day-of-year | `spice.cpp:142–143` | today−Jan1 → serial day/doy | no; held | UTC date | `utc_doy` |
| UTC 11 | wrapper → sod and quasi-MJD construction/return | `spice.cpp:144–145` | h,min,sec,date,length → sod s, MJD days | no; held until return | T,C | `utc_metadata` |
| Sun 1–3 | target/finite validation, mutex, `enter_spice` | `spice.cpp:88–95`, `19–24` | target10/ET → valid locked entry, zeroed buffers | CSPICE prep; held to return | T,C/concurrency | `sun_validation`, `sun_lock`, `sun_prepare` |
| Sun 4 | wrapper → `spkez_c(10,ET,"J2000","NONE",399)` | `spice.cpp:96` | ET → geometric position km, velocity km/s, light time s | CSPICE; held | T,C | `spkez` |
| Sun 5 | wrapper → `failed_c`, ×1000 SI conversion, finite validation, return | `spice.cpp:97–105` | km/km/s → m/m/s EphemerisState | failed_c CSPICE; held until return | output | `sun_extract`; unlock outer residual |

CSPICE is a prebuilt static N0067 library. No internal SPICE routine call-tree instrumentation is claimed: the measured externally visible primitive boundary is `pxform_c`, `et2utc_c`, each `str2et_c`, and `spkez_c`. Native internals are similarly attributed to `gtd7d` and official `JB2008`, not invented as separately measured equation costs.

Error-only nested `throw_spice_error` (`spice.cpp:25–31`) uses `getmsg_c`, string construction, `reset_c`, exception construction. No invocation on valid measured requests. `required` errors and native validation failures also do not run successfully timed requests. Mutex unlocking occurs via guard destruction; acquisition intervals include uncontended overhead and waiting, not a pure-wait metric.

## Nested geodetic solver

| Order | Operation / source | Input → output units | Dependencies / lock | Timing |
|---|---|---|---|---|
| G1 | Eigen `allFinite`, `squaredNorm` / `coordinates.cpp:17–18` | fixed m → validity | P transformed with T,C; no lock | `geo_validation` |
| G2 | `hypot(x,y)`, p<1e−9 polar shortcut (`copysign`, `abs`) or initial `atan2(z,p(1−e²))` / `19–22` | fixed m → p m and latitude rad; exact-pole altitude m | geometry only; no lock | `geo_initial`; pole skips G3–G5 |
| G3 | up to16 iterations: `sin`, `sqrt`, `atan2`, convergence abs(delta)<2e−15 / `23–29` | prior rad → next rad, ellipsoid normal radius m | geometry/C; no lock | `geo_iteration`; count captured |
| G4 | `sin`, `cos`, `sqrt`, ellipsoid-normal projection / `30–32` | p/z m, latitude → height m | geometry/C; no lock | `geo_altitude` |
| G5 | `atan2(y,x)`, Result / `33` | fixed x/y m → longitude rad | geometry; no lock | `geo_longitude` |

## Full RHS calls outside atmospheric provider

`docs/atmosphere/experiment.cpp:826–842` calls `geometric_state` once before **every** provider call for SRP/eclipse (`832–835`), then calls density (`837`). NRL therefore uses one Sun query per RHS, but zero inside density. JB uses two identical Sun-query epochs per RHS, one outside and one inside density. Source executes these independently; no reuse occurs. Its source/dynamics preparation is outside the atmospheric/native locks, except each CSPICE wrapper separately locks. Dynamics execution `859–867` includes the source snapshot and environmental forces/torques. Diagnostic output-time reevaluations (`885–899`) are not integrator requests and are excluded from request locality capture.

Immutable weather load parses files once (`weather.cpp:63–133`); dataset maps are read-only during measurement. SPICE kernels are loaded once (`spice.cpp:46–82`) and managed by reference count, not loaded per query. Provider construction selects switches once; no model-specific mutex encompasses weather or geometry. SPICE UTC keeps its mutex across parsing, formatting and both midnight conversions, unlike independent primitive microbenchmarks.
