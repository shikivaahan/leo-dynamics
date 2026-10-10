# Complete parameter and convention catalogue

Values below are the selected project configuration, not an assertion that engineering choices are published scientific requirements. Machine-readable `evidence/upstream-assets.json`, native coefficient catalogues and each experiment's manifest are normative for exact source values. No third-party defaults are silently relied on. Fixed fitted coefficients are **not tunable hyperparameters**.

## Native NRLMSISE input and switches

| Exact identifier | Meaning | Selected/default value | Units | Source | Permitted range | Rationale | Alternatives | Sensitivity | Stored configuration |
|---|---|---|---|---|---|---|---|---|---|
| `NrlmsiseInput::year` | UTC calendar year; native C ignores it | Raw struct 2000; provider actual UTC year | year | Project validation, reference header says ignored | 1–9999 raw; weather1950–2099 | Validate DOY/leap calendar explicitly | Any covering historical year | Seasonal effect enters DOY, not ignored year | Native input / manifest epoch |
| `day_of_year` | UTC seasonal day | Raw1; actual calendar DOY | day | Native `doy` | 1–365/366 | Reference seasonal clock | Any valid date | Annual/semiannual density terms | Native input / manifest |
| `utc_seconds` | Seconds from UTC midnight, including leap second | Raw0; actualUTC | s | Native `sec` + ATM-008 | [0,86401), actual day checked by loader | No TDB-as-UTC substitution | Other actual times | UT/longitude terms | Native input / recorded epoch |
| `altitude_km` | WGS84 ellipsoidal height | Raw400; actualposition | km | Native `alt`; WGS84 project choice | 0–1000 supported | Intended LEO domain plus reference low-altitude fixtures | Within domain | Strong altitude dependence, no arbitrary monotonicity promise | Native input / scenario |
| `latitude_deg` | Geodetic latitude | Raw0; WGS84 actual | degree | Native `g_lat` | [-90,90] | Model-specific convention | Any latitude | Latitude/seasonal effects | Native input / comparison row |
| `longitude_deg` | East longitude | Raw0; fixed frameactual | degree | Native `g_long` | finite; wrapped[-180,180) | Stable discontinuity behavior | Any equivalent360° wrap | Longitude/UT terms | Native input / comparison row |
| `local_solar_time_hours` | Mean solar-time approximation recommended in header | Raw0; `(UTCsec/3600+longitude/15) mod24` | h | Native `lst` header | finite; wrapped[0,24) | Header-consistent triplet; no hidden equation-of-time correction | Raw explicit apparentLST permitted | Diurnal harmonics; EOT omitted in provider | Input / manifest formula |
| `f107` | Previous-day measured F10.7 at actual Earth–Sun distance | Raw150; previous observedCSSI field | sfu=10⁻²² W m⁻² Hz⁻¹ | Reference header; CSSI `Obs F10.7` | finite>0 | Not adjusted1AU/prediction | Explicit caller input | Solar activity terms | Archived file/hash / nativeinput |
| `f107_average` | Current-day centred81-day observedF10.7 average | Raw150; CSSI `Obs Ctr81` | sfu | Native `f107A` | finite>0 | Native centred window | Raw caller average; trailing not auto substituted | Background heating | Archive / input |
| `ap` | Current daily Ap | Raw4; actual observeddaily | index | Native `ap`; CSSI `Avg Ap` | [0,400] | Published input | Daily or history via switch9 | Geomagnetic response | Archive / input |
| `ap_history[0..6]` | Daily/current3h/-3h/-6h/-9h/mean12–33h/mean36–57h | Rawall4; actual completehistory when selected | index | Native `ap_array` | All[0,400] | Full published history, not replicateddaily | Daily switch9=1 | Storm memory | Archive / input; switch9 |
| `Nrlmsise00::switches_[0]` | Native output unit switch | 0 | boolean | Header standard setting | 0 or1 | Explicit cgsnative then SI conversion | 1 nativeSI also supported/tested | Units only | Constructor, manifest24values |
| `switches_[1..23]` | Published variation switches | All1 (includingdailyAp switch9) | categorical | Header standard setting | 0off,1on,2mainoff/crosson;9 additionally-1history | Retain publishedvariation set | All supported values explicit | Modifies selected physical terms | Constructor / manifest |

Every switch: **0** SI-native output; **1** mean F10.7; **2** time independent; **3** symmetric annual; **4** symmetric semiannual; **5** asymmetric annual; **6** asymmetric semiannual; **7** diurnal; **8** semidiurnal; **9** dailyAp/history(-1); **10** allUT/longitude; **11** longitude; **12** UT/mixedUT-longitude; **13** mixedAp/UT/longitude; **14** terdiurnal; **15** departuresfromdiffusiveequilibrium; **16** TINFvariation; **17** TLBvariation; **18** TN1variation; **19** Svariation; **20** TN2variation; **21** NLBvariation; **22** TN3variation; **23** turboscaleheightvariation.

Output `gtd7d` includes anomalousoxygen drag-effective mass; `gtd7` excludes it. With switch0=0, native `d[5]` g/cm³ ×1000→kg/m³; species cm⁻³ ×10⁶→m⁻³. With switch0=1 no conversion. Species order He,O,N2,O2,Ar,H,N,anomalousO. Both temperaturesK. Native atomicmass conversion `1.66e-24` g remains unchanged; do not replace it with a modern exact atom-mass constant. Below80km the reference recommends raw150/150/4 due weakly established driver effects; fixtures deliberately reproduce native explicit inputs, acquisition never supplies fallback values.

## JB2008 explicit input conventions

| Exact identifier | Meaning | Selected/default value | Units | Source | Permitted range | Rationale | Alternatives | Sensitivity | Stored configuration |
|---|---|---|---|---|---|---|---|---|---|
| `Jb2008Input::mjd_utc` | UTC fractionalmodifiedJulianday | Raw51544.5; UTCquasiMJDactual | d | OfficialAMJD; ATM-008leapextension | Supportedhistorical1950–2099 | Native seasonalclock | RawcallerMJD | Seasonal/semiannualterms | Input / epochmanifest |
| `sun_ra_dec_rad[0..1]` | SolarRA/declination in common equatorbasis | Raw0,0; rotatedgeometricSun | rad | OfficialSUNarray; ATM-009 | RAfinitewrapped2π;declination±π/2 | Commonbasis noJ2000/fixedmix | NativeexactdriverRA/Dec forfixtures | Solarhourangle/heating | Input / providerframe |
| `position_ra_lat_alt[0]` | SpacecraftRA in samebasis | Raw0; fixedlongitude | rad | OfficialSAT1; onlyH=SAT1−SUN1used | finitewrapped2π | Commonzero cancelsexactly | NativeRA permitted | Localsolartime | Input / position |
| `position_ra_lat_alt[1]` | Geocentriclatitude | Raw0; atan2(z,hypot(x,y)) | rad | OfficialSAT2 | ±π/2 | NotMSISEgeodeticlatitude | Nativeexplicitlatitude | Latitude/heating | Input / position |
| `position_ra_lat_alt[2]` | Height | Raw400; WGS84ellipsoidalheight | km | OfficialSAT3; ellipsoid isprojectchoice | 120–1500supported | Matchphysicalposition/height; driverclamps120 | Nativeexplicitgridheight | Densityintegration | Input / scenario |
| `f10,f10_average` | F10daily/centred81-day | Raw150each; SOLFSMYlag1UTCday | sfu | OfficialF10/F10Bcomments | ≥40finite | ActualJBtiming | Rawexplicitdrivers | Mean/dailyheating | File/hash / input |
| `s10,s10_average` | EUV26–34nmproxy/centred81-day scaledtoF10 | Raw150each; lag1day | sfu-equivalent | OfficialS10/S10B | ≥40finite | ActualJBproxy, notF10substitution | Rawexplicitdrivers | Solarheating | File/hash / input |
| `m10,m10_average` | MgIIproxy/centred81-day scaledtoF10 | Raw150each; lag2days | sfu-equivalent | OfficialXM10/XM10B | ≥40finite | Exactpublishedlag | Rawexplicitdrivers | Solarheating/seasonalterms | File/hash / input |
| `y10,y10_average` | X-ray/Lymanαproxy/centred81-day scaledtoF10 | Raw150each; lag5days | sfu-equivalent | OfficialY10/Y10B | ≥40finite | Exactpublishedlag | Rawexplicitdrivers | Solarheating | File/hash / input |
| `dstdtc_k` | Stormtemperaturecorrection, includes sourceAp/Dstprocessing | Raw0; officialhourlyDTClinearinterpolated,rounded | K | OfficialDSTDTC/DTCVAL | [0,2000] | Actualstormdriver; do notsubstituteAp | RawexplicitDTC | Stormresponse | DTCfile/hash / input |

OfficialJB density is alreadykg/m³; TEMP1exospheric, TEMP2localK. Composition is not exported by reference interface (`has_constituents=false`). Exponential has neither temperature nor species (`has_temperatures=false`, `has_constituents=false`); unused numeric output slots are not scientific zero-density/zero-temperature claims. JB has no caller-selectable modelvariation switches. Nativefitted coefficients, molecularmasses, sea-levelfractions, roundedπ, integrationbands/quadrature and empirical correction coefficients remain byte-for-byte upstream. Their complete source-indexed catalogues preserve values and units where source documents them; undocumented coefficientunits/sensitivities are explicitly unknown, not invented.

## Coordinate/time/engineering and benchmark settings

| Exact identifier | Meaning | Value | Units/source | Range/alternatives | Rationale and sensitivity | Reproducibility |
|---|---|---|---|---|---|---|
| `frame_` | SPICEfixedframe | BaselineITRF93 | NAIFbinaryEarthPCK | IAU_EARTH explicitloweraccuracyoption; nofallback | IncludesEarthorientation, poles/UT1; IAUoption requiresPCK and hasloweraccuracy | Provider/manifestframe + kernelhash |
| `wgs84_a_m` / `wgs84_f` | Ellipsoid | 6378137 / 1÷298.257223563 | m /dimensionless; WGS84 | Fixednotfitted | Independentgeodetic/geocentricdistinction | coordinates.hpp |
| `geodetic` iterationlimit/convergencethreshold/polethreshold | Numericalinversionguards | 16 / 2e-15 / 1e-9 | iterations/rad/m; project | Fixedfailure-on-nonconvergence | Notdensityapproximation; avoidpoledivision | coordinates.cpp |
| `UtcCalendar::day_length_seconds` | ActualUTCdaylength | 86399,86400,86401asLSKdetermines | s; roundedmidnightETdifference | No fabricatedleapcalendar | UTC/TDBrates differmuchless0.5s/day | LSKhash + utc_calendar |
| `UtcCalendar::mjd` | UTCquasiMJD | 40587+dayssince1970+sod/daylength | d; explicitprojectleapconvention | NoTDBJDsubstitution | Continuouswithinleapday; extensionnotnativeJBrequirement | Epochmanifest |
| `RecordStatus` / sourceflags | Weatherquality | ObservedonlyCSSI/Q0; allowednativeSOLprovenancecodes documentedINPUTS | Categoricalsource-native | Missing/interpolated/predictedfail | Avoidclimatology/forecasts | Inputfiles/hash + loader |
| `ExponentialAtmosphere::density_scale_` | Legacyprofilescale | 1; CASEconfigurationretainedwhereapplicable | dimensionless/project | ≥0finite | Scalarprofileuncertainty; notempiricalmodelparameter | Casefile + experimentmanifest |
| `EnvironmentSnapshot::atmosphere_angular_velocity_rad_s` | Existingcorotation | [0,0,7.292115e-5] | rad/s/J2000 | Unchanged | No winds; forceapproximationretained | Kerneldefault + manifest |
| `IntegrationOptions` | NominaladaptiveDopri5 | rtol1e-10; positionatol1e-3; velocityatol1e-6; qatol1e-12; omegaatol1e-12; initialstep1; maxstep15; maxattempts1000000 | dimensionless,m,m/s,dimensionless,rad/s,s,s,count | Explicitrefinementsseparaterecords | NewdensitychangesODE, convergencecannottransfer | options.hpp + manifest |
| Outputcadence/duration | Coupledphysicalcomparison | 30 /21600 | s/s | Exactcommandselectable | Fixedtimestamps; workquadraturecadenceuncertaintyreported | command/manifest |
| Threading | Nativeentrypolicy | singlethreadbaseline; serializednative/SPICEcalls | process-widemutex | Concurrentcallssupportedwithserialization | ProtectCstaticstate, conservativeFortranpolicy | sources + tests + manifest |
| Compileroptimisation | Ordinarybuild | Release-O3-NDEBUG, nofast-math | GCC/ClangC++; GNUFortranlegacy | DebugASan/UBSanseparate | Noatmosphereperformanceoptimisation | CMakecache/compilerrecord |

Raw struct moderate-input defaults are convenience values explicitly listed above, **never missing-data fallbacks**. Production acquisition overwrites required weather fields or throws. Source constants and code hashes plus scenario files record every inherited physical spacecraft, gravity, radiation and aerodynamic parameter without silently modifying them. The original exponential knot table and mathematical log-linear interpolation remain unchanged; the no-interpolation restriction applies to the two newly integrated empirical models, not deletion of the historical lightweight baseline.
