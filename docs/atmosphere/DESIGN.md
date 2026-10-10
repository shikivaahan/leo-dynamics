# Direct empirical atmosphere architecture

Source baseline `d3f7c1410ada915db3544da1fd37f7414f0febcc`.

## Boundaries

`leo::kernel` remains independent of CSPICE, C atmosphere code and Fortran. Optional `leo::atmosphere` links the kernel for frame-safe vectors and types; direct native adapters accept fully explicit scalar inputs. Its CSPICE-dependent acquisition path is built only when `LEO_ENABLE_CSPICE=ON`. The existing `ExponentialAtmosphere` and `EarthEnvironment` remain available unchanged.

A provider transforms a J2000 position at explicit TDB `Epoch` into the requested SPICE Earth-fixed frame; computes WGS84 geodetic latitude/height and separately geocentric latitude; converts ET to UTC via the explicitly loaded LSK; selects exactly the necessary immutable weather records; invokes the selected model on every request. Its density enters `EnvironmentSnapshot::density_kg_m3`; existing aerodynamic force/torque laws consume that snapshot. Solar ephemeris and eclipse remain geometric Sun/Earth J2000/NONE.

Native results use kg/m³ and kelvin. MSISE constituents use m⁻³ and distinguish anomalous oxygen. JB2008 total mass density is already SI. Temperature/composition outputs do not silently alter Sentman gas-surface physics.

## Dependencies and safety

NRLMSISE uses a pinned established C reference, not rewritten equations. JB2008 calls the externally acquired official Fortran through an owned interoperable bridge; official source is not redistributed. CMake performs no downloads. Native mutable state is guarded by backend process-wide mutexes; existing CSPICE entry points share their existing mutex. Calls into those external libraries outside leo require application coordination. Baseline measurements are single-threaded, and any concurrent smoke evidence is reported separately.

No density-result caches, interpolation, spatial grids, trajectory precomputation, approximations or fallback drivers exist. Holding parsed immutable weather records is input loading, not model evaluation caching. Any interpolation required by the official DTC input convention acts on weather indices only and is documented explicitly.

JB2008's native interface specifies geocentric latitude but does not uniquely specify the height datum. The provider's WGS84 ellipsoidal height is an explicit, provisional spatial convention, not a datum established by the official scalar-height grid. `src/atmosphere/provider.cpp` applies it after J2000→ITRF93; `Jb2008::evaluate` admits a `1e-9 km` representation margin at the 120/1500 km boundaries and forwards the received height unchanged. The margin fixes a reproduced numerical rejection; it does not change the empirical equations or select a different physical datum.

## Physical limitations

Empirical density uncertainty is not numerical implementation error. The lightweight exponential profile remains spherical in its original API; matched-location comparison may explicitly give it the same height supplied to empirical models. Corotation remains the existing `{0,0,7.292115e-5}` rad/s inertial vector; no winds added. Existing Sentman molecular assumptions, plate geometry, optical radiation, torque and numerical tolerances remain unchanged. New densities change the differential equations, so the historical convergence study is not transferred to these runs. CASE-C six-hour instantaneous attitude remains a stress-test diagnostic, not an acceptance oracle.

## Evidence policy

Independent published/reference fixtures first; metamorphic and input boundary checks second; matched historical propagation and direct timings last. Frozen records identify source tree, external code/data/kernel hashes, build flags, clocks, frames, switches, lags, initial conditions and commands. Preserve all `docs/numerical-audit/evidence` and historical benchmark records. New candidate records never overwrite frozen evidence.
