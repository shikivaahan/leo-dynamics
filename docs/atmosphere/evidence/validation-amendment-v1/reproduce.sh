#!/usr/bin/env bash
# Reproduce the JB2008 official-grid investigation end to end.
#
# Builds the three project-owned probes against the hash-pinned external official
# distribution, then runs the full-grid analysis, the index identifiability
# experiment, the constrained +-0.5 fit and the scalar temperature-offset
# experiment, and assembles the evidence record.
#
# Everything this writes lands UNDER $WORK, a fresh scratch directory.  It never
# writes into the repository, so a run can never overwrite frozen evidence; copy
# the assembled record into docs/atmosphere/evidence/ deliberately, after
# reviewing it.
#
# Nothing is downloaded, vendored or copied: JB2008.f is compiled in place from
# the external hash-pinned path, and the official SUNPOS/THETA subroutines are
# sliced byte-identically out of the hash-pinned driver with no symbol stubbed.
#
# Usage: reproduce.sh [WORKDIR]
set -euo pipefail

WORK="${1:-${TMPDIR:-/tmp}/jb2008-grid-investigation}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DEPS="${LEO_ATMOSPHERE_DEPS:-/tmp/leo-atmosphere-deps}"
BUILD="${LEO_ATMOSPHERE_BUILD:-$REPO/build-atmosphere-gcc}"
GFORTRAN="${LEO_GFORTRAN:-/tmp/leo-fortran/usr/bin/gfortran-13}"
EV="$REPO/docs/atmosphere/evidence/validation-amendment-v1"
export LD_LIBRARY_PATH="${LEO_FORTRAN_LIB:-/tmp/leo-fortran/usr/lib/x86_64-linux-gnu}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

for required in "$DEPS/JB2008.f" "$GFORTRAN" "$DEPS/jb2008.zip"; do
  [ -e "$required" ] || { echo "missing $required" >&2; exit 1; }
done
mkdir -p "$(dirname "$WORK")"
mkdir "$WORK" || { echo "refusing existing work directory: $WORK" >&2; exit 1; }
cd "$WORK"

echo "== official model, compiled in place from the hash-pinned external path =="
"$GFORTRAN" -std=legacy -O3 -c "$DEPS/JB2008.f" -o jb2008.o

echo "== official SUNPOS and THETA, sliced byte-identically from the pinned driver =="
# No link-time stub routines are used anywhere in this script.  JB2008.f carries
# no SUNPOS/THETA, and the driver's own MAIN plus its DTCVAL/SOLFSMY file
# dependencies make the whole driver unlinkable here, so the two subroutines are
# extracted verbatim from the hash-verified driver text and compiled alone.
python3 "$EV/extract_official_astronomy.py" --deps "$DEPS" --out astro_frag.f \
  --from-zip > astronomy_provenance.json
"$GFORTRAN" -std=legacy -O2 -c astro_frag.f -o astrofrag.o
"$GFORTRAN" -O2 -c "$REPO/docs/atmosphere/jb2008_official_astronomy.f90" -o astro.o
"$GFORTRAN" -O2 astro.o astrofrag.o -o jb2008_official_astronomy

echo "== native kernel and adapter probe =="
"$GFORTRAN" -std=legacy -O3 -c "$REPO/docs/atmosphere/jb2008_native_kernel.f90" -o kernel.o
"$GFORTRAN" -O3 kernel.o jb2008.o -o jb2008_native_kernel
g++ -O2 -std=c++20 -I"$REPO/include" \
    "$REPO/docs/atmosphere/jb2008_adapter_probe.cpp" \
    "$BUILD/libleo_atmosphere.a" "$BUILD/libleo_jb2008_reference.a" \
    -lpthread -o jb2008_adapter_probe

echo "== full grid analysis (8 epochs x 68191 rows) =="
python3 "$REPO/docs/atmosphere/jb2008_grid_analysis.py" \
  --kernel ./jb2008_native_kernel --adapter ./jb2008_adapter_probe \
  --astro ./jb2008_official_astronomy \
  --validation-dir "$DEPS/jb-validation" --index-dir "$DEPS" \
  --detail-json "$WORK/analysis.json"

echo "== index sensitivity, Richardson extrapolation and identifiability =="
python3 "$REPO/docs/atmosphere/jb2008_grid_identify.py" \
  --kernel ./jb2008_native_kernel --astro ./jb2008_official_astronomy \
  --validation-dir "$DEPS/jb-validation" > "$WORK/identify.json"

echo "== constrained +-0.5 fit and current-archive evaluation =="
python3 "$REPO/docs/atmosphere/jb2008_grid_bounded_fit.py" \
  --kernel ./jb2008_native_kernel --astro ./jb2008_official_astronomy \
  --validation-dir "$DEPS/jb-validation" --index-dir "$DEPS" \
  --stride 1500 > "$WORK/bounded_fit.json"

echo "== scalar exospheric-temperature offset experiment =="
python3 "$REPO/docs/atmosphere/jb2008_grid_scalar_tc.py" \
  --kernel ./jb2008_native_kernel --astro ./jb2008_official_astronomy \
  --validation-dir "$DEPS/jb-validation" > "$WORK/scalar_tc.json"

echo "== assembling evidence (into \$WORK, not the repository) =="
python3 "$EV/build_jb2008_grid_evidence.py" \
  --analysis "$WORK/analysis.json" --identify "$WORK/identify.json" \
  --scalar-tc "$WORK/scalar_tc.json" --bounded-fit "$WORK/bounded_fit.json" \
  --astronomy-provenance "$WORK/astronomy_provenance.json" \
  --envelope "$EV/altitude-datum.json" \
  --output "$WORK/jb2008-grid-investigation.json"

echo
echo "NOTHING was written into the repository; review and copy deliberately:"
echo "  $WORK/jb2008-grid-investigation.json"
echo "    -> $EV/jb2008-grid-investigation.json"