# NRLMSISE-00 backend

Direct, uncached evaluation of the NRLMSISE-00 empirical thermosphere through the
pinned Brodowski C reference implementation. Every requested density is evaluated by
`gtd7d` on each call; there is no caching, memoisation, interpolation, gridding,
quantisation or surrogate anywhere in this backend.

Status: **implemented, independently validated against the published 17-case reference
output.** Tests are registered as `leo_nrlmsise00_tests` and are skipped visibly when
the published oracle is not supplied.

Machine-readable companion: [`nrlmsise00-parameters.json`](nrlmsise00-parameters.json)
contains every switch, every fixed coefficient array with its pinned SHA-256 and line
span, every scalar constant with its source line, the input/output contract, the adapter
guards and the threading policy.

---

## 1. Upstream source, revision and redistribution

| Field | Value |
|---|---|
| Model | NRLMSISE-00 (model 2001), Picone, Hedin, Drob |
| Reference entry point | `gtd7d` (drag-effective total mass density) |
| Reference distribution | Brodowski C translation of the NRLMSISE-00 FORTRAN package |
| Upstream repository | `https://git.linta.de/~brodo/nrlmsise-00.git` |
| Pinned revision | `bc9a2feba4344e74201281e563332688a4d09cc3` (release 20190709) |
| Retrieval date | 2026-10-10 |
| Acquisition URL | `https://git.linta.de/~brodo/nrlmsise-00.git/plain/<file>?id=bc9a2feba4344e74201281e563332688a4d09cc3` |

| File | SHA-256 | Bytes | Used by |
|---|---|---|---|
| `nrlmsise-00.c` | `a20d6523420188241963f095ad0b65df44ac0a6fa6db52d2e1df823c366f0285` | 44170 | compiled |
| `nrlmsise-00.h` | `d5b45be690bbc6cf394f69b18c0e641a1769da0a87015704fdabf0e7560794aa` | 7930 | compiled |
| `nrlmsise-00_data.c` | `d0b3022f3c3e7ffdf703cc0e0d02339dfee01c0bf1520b29c68d5f4afd8784d5` | 48114 | compiled |
| `DOCUMENTATION` | `e7db72964c22861e446d4b61931a22a58c32eeaa7f9bb23b45d887751b4ecae3` | 11469 | test oracle only |
| `nrlmsise-00_test.c` | `5a95faa996dc265ab85a0f6db641628abe84c9aef504638b4aa465ed76c89113` | 4616 | transcribed into the tests |

**Redistribution.** No upstream code or reference data is vendored into this
repository. The package carries a warranty disclaimer rather than an explicit
redistribution grant, so the source is acquired out of tree and pinned by hash.
`cmake/nrlmsise00.cmake` verifies all three compiled files by SHA-256 at configure
time and fails hard on any mismatch. Nothing is downloaded during configuration.

### Acquisition

```sh
REV=bc9a2feba4344e74201281e563332688a4d09cc3
mkdir -p /tmp/leo-atmosphere-deps
for f in nrlmsise-00.c nrlmsise-00.h nrlmsise-00_data.c DOCUMENTATION nrlmsise-00_test.c; do
  curl -sL -o "/tmp/leo-atmosphere-deps/$f" \
    "https://git.linta.de/~brodo/nrlmsise-00.git/plain/$f?id=$REV"
done
sha256sum /tmp/leo-atmosphere-deps/*
```

---

## 2. Build

```sh
cmake -S . -B build-atmosphere \
  -DLEO_ENABLE_NRLMSISE00=ON \
  -DLEO_NRLMSISE00_ROOT=/tmp/leo-atmosphere-deps \
  -DBUILD_TESTING=ON
cmake --build build-atmosphere -j
ctest --test-dir build-atmosphere --output-on-failure
```

To enable the published fixture oracle, add
`-DLEO_NRLMSISE00_REFERENCE_DOCUMENTATION=/tmp/leo-atmosphere-deps/DOCUMENTATION`;
otherwise export `LEO_NRLMSISE00_REFERENCE_DOCUMENTATION` at run time instead. Without
either, the fixture tests **skip** — they do not pass.

`LEO_ENABLE_NRLMSISE00` defaults to `OFF`, so the lightweight kernel build is
unchanged. The target exports `LEO_HAS_NRLMSISE00=1`.

---

## 3. API

```cpp
#include "leo/atmosphere/nrlmsise00.hpp"

leo::atmosphere::Nrlmsise00 model;                       // published standard switches
leo::atmosphere::Result r = model.evaluate(input);       // one direct gtd7d call
```

`Result` fields and their exact meaning:

| Field | Units | Meaning |
|---|---|---|
| `density_kg_m3` | kg/m³ | **drag-effective** total mass density from `gtd7d`, anomalous oxygen included |
| `temperature_k` | K | reference `t[1]`, temperature at the requested altitude |
| `exospheric_temperature_k` | K | reference `t[0]` (TINF), exospheric temperature |
| `number_density_m3` | m⁻³ | `He, O, N₂, O₂, Ar, H, N, anomalous O`, in that order |

Constituents are mapped from the reference `d[0], d[1], d[2], d[3], d[4], d[6], d[7], d[8]`
(there is no `d[9]`; `d[5]` is the mass density slot). Conversions applied by the
adapter: number density × 10⁶ (cm⁻³ → m⁻³) and mass density × 10³ (g/cm³ → kg/m³).
Both factors are exact.

---

## 4. `gtd7` versus `gtd7d`

This is the single most consequential output decision in the backend.

* `gtd7` (`nrlmsise-00.c:1027`) forms the mass density from He, O, N₂, O₂, Ar, H and N
  only.
* `gtd7d` (`nrlmsise-00.c:1046`) repeats the same sum **and adds `16 × d[8]`, anomalous
  oxygen**. Anomalous oxygen is a drag-relevant species above roughly 500 km.

For satellite drag the backend therefore calls `gtd7d` and reports `d[5]` from that
entry point. The published `DOCUMENTATION` fixture table prints the **`gtd7`** mass
density in its `RHO` column, so those numbers are *not* the expected value of
`Result::density_kg_m3`. The test suite reconstructs both quantities from the published
constituent columns, uses the reconstructed `gtd7` value to prove the oracle really is
the reference output, and then requires the adapter to match the reconstructed `gtd7d`
value. The two differ substantially at the 1000 km fixture case, where anomalous
oxygen dominates the mass budget; the test asserts that separation explicitly.

---

## 5. Switches

`Nrlmsise00::standard_switches()` returns the published standard set:

* `switches[0] = 0` — **explicit** native cgs output (centimetres and grams), never an
  inherited library default.
* `switches[1..23] = 1` — every published variation on.

| Index | Meaning | Default | Permitted |
|---|---|---|---|
| 0 | output unit system: 0 = cm and g, 1 = m and kg | 0 | 0, 1 |
| 1 | F10.7 effect on mean | 1 | 0, 1, 2 |
| 2 | time independent | 1 | 0, 1, 2 |
| 3 | symmetrical annual | 1 | 0, 1, 2 |
| 4 | symmetrical semiannual | 1 | 0, 1, 2 |
| 5 | asymmetrical annual | 1 | 0, 1, 2 |
| 6 | asymmetrical semiannual | 1 | 0, 1, 2 |
| 7 | diurnal | 1 | 0, 1, 2 |
| 8 | semidiurnal | 1 | 0, 1, 2 |
| 9 | daily Ap; **-1 selects the Ap history** | 1 | 0, 1, 2, -1 |
| 10 | all UT/longitude effects | 1 | 0, 1, 2 |
| 11 | longitudinal | 1 | 0, 1, 2 |
| 12 | UT and mixed UT/longitude | 1 | 0, 1, 2 |
| 13 | mixed Ap/UT/longitude | 1 | 0, 1, 2 |
| 14 | terdiurnal | 1 | 0, 1, 2 |
| 15 | departures from diffusive equilibrium | 1 | 0, 1, 2 |
| 16 | all TINF variations | 1 | 0, 1, 2 |
| 17 | all TLB variations | 1 | 0, 1, 2 |
| 18 | all TN1 variations | 1 | 0, 1, 2 |
| 19 | all S variations | 1 | 0, 1, 2 |
| 20 | all TN2 variations | 1 | 0, 1, 2 |
| 21 | all NLB variations | 1 | 0, 1, 2 |
| 22 | all TN3 variations | 1 | 0, 1, 2 |
| 23 | turbo scale height variations | 1 | 0, 1, 2 |

Switch 2 for index 9 is accepted because `tselec` propagates it verbatim, but the
upstream header documents only `0`, `1` and `-1` for that switch; treat it as
undocumented rather than as a supported mode.

### Exact alternatives per switch value

Every switch is a dimensionless integer flag with no unit. `tselec`
(`nrlmsise-00.c:82`) maps each entry onto two internal arrays: `sw[i]` gates main
effects and `swc[i]` gates cross terms. These are the only two behaviours the value
can have.

| Value | `sw[i]` | `swc[i]` | Exact effect |
|---|---|---|---|
| `0` | 0 | 0 | variation off; no main effect and no cross terms |
| `1` | 1 | 1 | variation on; main effect and cross terms both applied |
| `2` | 0 | 1 | main effects off but cross terms on |
| `-1` | -1 | -1 | index 9 only: selects the seven-element three-hourly Ap history |

Switch 0 is the sole exception: it is not a variation switch at all. It selects the
reference output unit system and leaves the internal arrays untouched.

| Value | Units produced by the reference |
|---|---|
| `0` | centimetres and grams (native default) |
| `1` | metres and kilograms |

Index 9 additionally has three value-specific behaviours:

| Value | Source | Exact effect |
|---|---|---|
| `0` | published | daily-Ap variation off entirely |
| `1` | published | daily Ap taken from `NrlmsiseInput::ap` |
| `2` | **undocumented** | propagated verbatim by `tselec`; the upstream header documents only `0`, `1` and `-1` for this switch, so it is accepted but not claimed as a supported mode |
| `-1` | published | `ap_a` history taken from `NrlmsiseInput::ap_history`, bypassing `ap` entirely |

The adapter rejects out-of-domain switch values at construction with
`std::invalid_argument` rather than coercing them, and the value `2` is verified by test
to actually change the result rather than being treated as a synonym for `0` or `1`.

### Unit-system switch

With `switches[0] = 1` the reference itself multiplies the number densities by 10⁶ and
divides the mass density by 1000. The adapter then applies no further scaling. The test
suite evaluates all seventeen published cases in both unit systems and requires the
SI results to agree to 1e-12 relative, which validates that the adapter's own
conversions are exact rescalings and nothing else.

### Ap-history selection

`nrlmsise00.h:72` defines the seven-element `ap_array`:

| Element | Meaning |
|---|---|
| 0 | daily Ap |
| 1 | 3-hourly Ap at the current time |
| 2 | 3-hourly Ap 3 h earlier |
| 3 | 3-hourly Ap 6 h earlier |
| 4 | 3-hourly Ap 9 h earlier |
| 5 | mean of eight 3-hourly indices 12–33 h earlier |
| 6 | mean of eight 3-hourly indices 36–57 h earlier |

Verified implementation detail: `sg0` (`nrlmsise-00.c:594`) consumes elements **1–6
only**. Element 0 is reached exclusively through the daily-index switch path, so when
switch 9 is `-1` the adapter validates all seven elements (finiteness and range) but a
change to element 0 cannot affect the result. The tests assert both halves of this
behaviour rather than assuming symmetry.

The adapter always passes a valid `ap_a` pointer, so the history path can never read a
null pointer even when switch 9 is not `-1`.

---

## 6. Coordinate, time and driver conventions

The backend is a pure model evaluator. It accepts already-reduced inputs and performs
no frame transformation of its own; frame reduction belongs to the coordinates
provider. What it does guarantee:

| Input | Convention | Adapter handling |
|---|---|---|
| `latitude_deg` | **geodetic** latitude, −90…90 | rejected outside ±90 |
| `longitude_deg` | geodetic longitude, degrees | wrapped to [−180, 180); any finite value accepted |
| `altitude_km` | **geodetic** altitude, km | rejected outside 0…1000 |
| `day_of_year` | **UTC** day of year | validated against Gregorian leap years |
| `utc_seconds` | **UTC** seconds of day | 0…86400 accepted; the reference applies no wrap |
| `local_solar_time_hours` | **local apparent solar time**, hours | wrapped to [0, 24) |
| `f107` | daily 10.7 cm flux for the **previous** UTC day (`nrlmsise-00.h:94`) | must be > 0 |
| `f107_average` | 81-day average **centred** on the current day of year (`nrlmsise-00.h:93`) | must be > 0 |
| `ap` | daily Ap | 0…400 |
| `ap_history` | see table above | validated only when switch 9 is `-1` |

Longitude and local solar time wrapping is verified to reproduce bit-identical results
for ±360° and ±720° longitude offsets and ±24 h and ±48 h local-time offsets, including
the `190° ≡ −170°` case that a naive `fmod` would get wrong.

### The year field

`nrlmsise-00.h:86` labels `year` "currently ignored", and `nrlmsise-00.c` never reads
it; the upstream driver passes `0`. The adapter requires `1 ≤ year ≤ 9999` so that
day-of-year can be validated against a real calendar, which the reference cannot do
for itself. Because the field is unused, the substitution is provably inert: the tests
evaluate the same state for years 1900 through 9999 and require bit-identical output.
The fixture tests use year 2000; day 172 exists in every year, so the fixture
reconstruction is unaffected.

### Advisory from the reference documentation

`nrlmsise-00.h:115` states that `f107`, `f107A` and `ap` effects are neither large nor
well established below 80 km, and that they should be set to 150, 150 and 4 there. The
adapter does not enforce this; it is a caller responsibility and is recorded so that
sub-80 km propagation does not imply a meaningful solar-driver dependence.

---

## 7. Fixed coefficients

All published coefficients live in `nrlmsise-00_data.c` and are **not tunable**. The
catalogue `nrlmsise00-parameters.json` pins each array by symbol, nested declared shape,
explicit versus implied-zero initialiser counts, source line span and block SHA-256.
Values are never reproduced in this repository; the pinned source file verified by its
own SHA-256 is the authoritative copy.

| Array | Declared shape | Slots | Explicit | Implied zero | Lines |
|---|---|---|---|---|---|
| `pt` | [150] | 150 | 150 | 0 | 26–57 |
| `pd` | [9][150] | 1350 | 1350 (9 rows × 150) | 0 | 59–339 |
| `ps` | [150] | 150 | 150 | 0 | 341–372 |
| `pdl` | [2][25] | 50 | 50 (2 rows × 25) | 0 | 375–386 |
| `ptm` | [50] | 50 | 10 | **40** | 388–391 |
| `pdm` | [8][10] | 80 | 80 (8 rows × 10) | 0 | 392–408 |
| `ptl` | [4][100] | 400 | 400 (4 rows × 100) | 0 | 411–496 |
| `pma` | [10][100] | 1000 | 1000 (10 rows × 100) | 0 | 498–709 |
| `sam` | [100] | 100 | 100 | 0 | 712–733 |
| `pavgm` | [10] | 10 | 10 | 0 | 737–739 |
| **total** | | **3340** | **3300** | **40** | |

`explicit` counts only the values actually written in the source, with C comments
removed first; a naive scan over the raw text inflates `pd`, `ptl` and `pma` by counting
digits in their row labels. `implied zero` is the C-initialisation remainder of the
declared slots. Regenerate this table with

```sh
scripts/extract_nrlmsise00_parameters.py "$LEO_NRLMSISE00_ROOT/nrlmsise-00_data.c" --check
```

which also verifies the pinned SHA-256 and exits non-zero on mismatch.

Published quirks recorded rather than silently repaired:

* `ptm` is declared `[50]` in the data file but `extern double ptm[10]` in
  `nrlmsise-00.c:64`. Only `ptm[0..9]` are ever written, so the remaining 40 slots are
  zero-initialised by C semantics and the mismatch is inert. The adapter does **not**
  patch upstream.

Scalar constants used by the model are listed with their exact source lines and units
in the catalogue, notably the atomic mass unit value `1.66e-24` g (deliberately not the
exact 1.66053906660e-24 g), the constituent mass numbers
`[4, 16, 28, 32, 40, 1, 14, 16]` (hydrogen carries mass number 1, not 1.008, exactly
as published), the degree-to-radian constant 0.0174533 rad/deg, and the altitude node
sets `{32.5, 20, 15, 10, 0}` km, `{72.5, 55, 45, 32.5}` km and the 62.5 km mixing
altitude.

Two constants are recorded with **no invented unit**. `rgas = 831.4`
(`nrlmsise-00.c:177, 357, 479, 1059`) appears only in
`gamm = xm * glb * zgdif / rgas`, where `xm` is the mean molecular weight from
`pdm[2][4]`, `glb = gsurf/(1+z1/re)^2` is in cm/s² and `zgdif` is in km; the upstream
sources attach no unit to it and this project applies no SI reinterpretation.
`bm = 1.3806e-19` (`nrlmsise-00.c:1058`) belongs to the `ghp7` pressure iteration, which
this adapter does not expose. Both are recorded as literal values with their use site.

---

## 8. Validation

`tests/nrlmsise00_tests.cpp` is the independent oracle suite. It never re-runs the
wrapper and compares it with itself.

| Test | What it establishes |
|---|---|
| official 17 published cases | all eight constituent number densities, both temperatures and the drag density match the published output |
| `gtd7d` versus `gtd7` | the oracle is the published output, and the adapter reports the drag-effective quantity |
| SI physical mass identity | `density_kg_m3 == 1.66e-27 kg × Σ mᵢ nᵢ` over all eight constituents including anomalous oxygen |
| unit-system switch | native cgs and metric evaluations agree to 1e-12 relative on all 17 cases |
| wrapping | longitude and local-solar-time wrapping is exact |
| determinism | repeated evaluation is bit-identical and independent of the order of previous evaluations |
| year independence | 1900…9999 give identical output, as the reference documents |
| 32.5 km regression | output is finite on and around the lower-atmosphere node |
| Ap history | slots 1–6 reach the model, slot 0 does not, and switch 9 gates the whole path |
| switch domains | out-of-domain switch values are rejected; switch value 2 changes the answer rather than being coerced |
| domain validation | every documented guard throws `std::invalid_argument`, and documented boundaries are evaluated to a finite, positive density rather than merely not throwing |
| concurrency | eight threads reproducing serial results bit for bit |

### Fixture oracle and tolerance

The 17 published cases are parsed from the pinned `DOCUMENTATION` file at run time. The
numbers are never copied into this repository. The parser selects lines containing
exactly eleven `%E`-printed numbers, which uniquely identifies the fixture block.

Tolerance is **1e-6 relative** on every compared quantity, and that number is a
deliberately tight choice rather than a comfortable one.

The oracle is printed with `%E` at **seven significant digits**, i.e. `m × 10^e` with
`1.000000 ≤ m < 10`. The relative ulp is therefore `1e-6 / m`, so the worst-case
half-ulp rounding error of any printed value is **5e-7**, attained when the leading digit
is 1 and falling below 1e-7 once the leading digit reaches 9. The drag mass is a
positive weighted sum of the constituents, so it inherits that same bound and cannot
amplify it.

The error actually observed is **3.04e-7**: recomputing the `gtd7` mass from the printed
constituent columns reproduces the printed `RHO` column to that precision across all
17 cases. So 1e-6 clears the 5e-7 worst case and the 3.04e-7 observed error with a small
margin.

That margin is intentional. A looser tolerance would be meaningless because the oracle
itself is only good to about five parts in 10⁷, and a tighter one would be testing the
printer rather than the model. Exact-equality assertions (1e-12 relative or bitwise) are
reserved for identities that *must* be exact — unit rescaling, angle wrapping and repeat
evaluation — never for the published fixture.

### The 32.5 km fix

Release 20190709 is pinned specifically because it changes
`if (input->alt < zn3[0])` to `<=` (`nrlmsise-00.c:979`) and initialises the spline
locals `x`, `z1`, `t1`, `zgdif` and `mn` in `densm`. Earlier releases left them
uninitialised at exactly 32.5 km and returned non-finite values. The regression test
evaluates 32.4999, 32.5 and 32.5001 km and additionally requires that O, H, N and
anomalous O are exactly zero on and below the node, which is the published lower-atmosphere
behaviour.

---

## 9. Threading

The upstream reference is **not reentrant**. It keeps file-scope mutable state in
`nrlmsise-00.c:37-74`: `gsurf`, `re`, `dd`, `dm04`, `dm16`, `dm28`, `dm32`, `dm40`,
`dm01`, `dm14`, `meso_tn1`, `meso_tn2`, `meso_tn3`, `meso_tgn1`, `meso_tgn2`, `meso_tgn3`,
`dfa`, `plg`, `ctloc`, `stloc`, `c2tloc`, `s2tloc`, `s3tloc`, `c3tloc`, `apdf` and `apt`.

The adapter holds a single process-wide `std::mutex` across the whole `gtd7d` call, so
concurrent use of `Nrlmsise00` is safe but **serialised**. This is a correctness measure,
not a performance claim; it must be revisited only together with a measured timing
baseline, never removed on assumption.

Inspection shows every one of those globals is assigned before it is read within a
single `gtd7d` call, and the guarded reads (`apdf` only in the daily path, `apt` only in
the history path) cannot observe another call's leftovers. The tests verify this
empirically through order independence and eight-thread concurrency.

---

## 10. Adapter guards and limits

| Guard | Rule | Units | Class |
|---|---|---|---|
| non-finite input | every floating-point input must be finite | applies to all fields | guard |
| latitude | \|latitude\| ≤ 90 | degrees, geodetic | guard |
| longitude, local solar time | any finite value; wrapped | degrees / hours | guard |
| UTC seconds | 0 … 86400 | seconds of UTC day | guard |
| day of year | 1…365, or 1…366 in a Gregorian leap year | dimensionless calendar index | guard |
| year | 1…9999 | dimensionless calendar index | guard |
| solar flux | `f107 > 0`, `f107_average > 0` | sfu | guard |
| Ap | 0…400, history elements too when switch 9 is `-1` | dimensionless index | guard |
| switches | switch 0 in {0,1}; 1…23 in {0,1,2}; 9 additionally −1 | dimensionless integer flag | guard |
| reference output | non-finite/negative constituents, non-positive temperatures → `std::runtime_error` | m⁻³, kg/m³, K | guard |
| **altitude** | **0 … 1000** | **km, geodetic** | **project support limit, not a published cutoff** |

All guards are enforced in `src/atmosphere/nrlmsise00.cpp`; the machine-readable
catalogue records the same rules with their classification
(`engineering_guard` versus `engineering_limit`).

The altitude bound is the one limit this project chose rather than inherited. It spans
the intended LEO and upper-LEO domain and covers the published 1000 km fixture. The
NRLMSISE-00 model itself is published from the ground to the lower exosphere; this
backend does not claim the model degrades gracefully outside the bound, only that
requests beyond it fail loudly instead of returning a number nobody validated.

---

## 11. Known limitations

1. **No redistribution grant.** The backend cannot be built without an out-of-tree
   acquisition of the pinned files. This is a licensing consequence, not a technical one.
2. **Serialised evaluation.** Concurrency is safe but not parallel; per-evaluation
   throughput is bounded by the mutex.
3. **Domain guard, not a validity study.** The 1000 km ceiling is a support limit. No
   independent study of the model's accuracy limits is claimed here.
4. **Drivers are the caller's responsibility.** Observed-versus-predicted status,
   temporal centring of the 81-day average and the previous-day convention for `f107`
   must be honoured upstream; this backend takes the values as given.
5. **Frame reduction is external.** Geodetic vs geocentric latitude, ellipsoidal vs
   spherical altitude and UTC conversion all happen before `evaluate` is called.
6. **`ghp7` is not exposed.** Only `gtd7d` is adapted; pressure-level queries are out of
   scope and unused.

---

## 12. Dated decisions recorded for this backend

These are recorded here for the project decision log; `docs/atmosphere/DECISIONS.md` is
maintained centrally and is not edited by this component.

**2026-10-10 — pin Brodowski release 20190709 (`bc9a2feb…`) rather than the 2004-era
copy.** *Options:* the widely mirrored 20041227 copy, or the maintained revision.
*Selected:* the maintained revision. *Justification:* it contains the 32.5 km
lower-atmosphere fix and the localised-variable initialisation that the older copy lacks;
using the older copy would reproduce a known non-finite-output defect. *Consequence:* the
`nrlmsise-00.c` hash differs from older mirrors and the CMake hash check must match the
20190709 file. *Final.*

**2026-10-10 — report `gtd7d`, not `gtd7`.** *Options:* the mass density printed in the
published fixture table (`gtd7`) or the drag-effective quantity (`gtd7d`).
*Selected:* `gtd7d`. *Justification:* the project baseline exists to feed aerodynamic
drag, and anomalous oxygen materially changes the mass budget in the upper LEO. The
fixture table documents `gtd7`, so the validation suite reconstructs both from the
published constituent columns instead of comparing against the wrong column.
*Consequence:* expected values must be the anomalous-oxygen-inclusive mass.
*Final.*

**2026-10-10 — keep switch 0 at the explicit native cgs value 0.** *Options:* leave the
reference in native cgs and convert in the adapter, or let switch 0 do the conversion.
*Selected:* native cgs. *Justification:* the conversion becomes visible, testable code
owned by this project instead of a side effect buried in the reference; both paths are
tested to agree exactly. *Consequence:* two explicit multiplication factors in the
adapter. *Final.*

**2026-10-10 — substitute year 2000 for the reference driver's ignored `year = 0`.**
*Options:* pass 0 through, or substitute a real year. *Selected:* substitute a real
year. *Justification:* day-of-year cannot be validated without a calendar, and the field
is provably unread by the reference, so the substitution is inert and testable. *Final.*

**2026-10-10 — require a real Ap history even though `ap_a[0]` is unused.**
*Options:* pass a zero-filled struct, or always pass the caller's seven elements.
*Selected:* always pass the caller's seven elements and validate all seven.
*Justification:* a zero-filled history would be a silent fabricated driver value when
switch 9 is later set to −1. *Consequence:* callers must supply plausible values even
when the history is not selected. *Final.*

**2026-10-10 — serialize all reference calls behind one process-wide mutex.**
*Options:* assume reentrancy, use thread-local state, or serialize. *Selected:*
serialize. *Justification:* the reference holds documented file-scope mutable state and
provides no reentrancy guarantee; verifying every write-before-read by inspection alone
would be a weaker guarantee than a mutex. *Consequence:* evaluation does not scale with
threads. *Provisional* — revisit only if a measured baseline makes it worthwhile, never
on assumption.

**2026-10-10 — parse the published oracle from the external `DOCUMENTATION` file rather
than embedding expected values.** *Options:* embed the numbers in the test source, or
parse them from the pinned upstream file. *Selected:* parse. *Justification:* the
reference distribution carries no redistribution grant, so the repository must not
contain a copy of its published output; the test also SKIPs visibly when the oracle is
absent instead of pretending to validate. *Consequence:* fixture tests require the
external file to be meaningful. *Final.*

**2026-10-10 — 1000 km altitude ceiling is a project support limit, not a published
validity boundary.** *Justification:* it covers the intended domain and the published
fixture while failing loudly rather than extrapolating. *Provisional* until an accuracy
study justifies a different bound.