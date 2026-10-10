#!/bin/sh
# Reproduce the physical sensitivity study. No downloads or production defaults change.
# Usage: sh docs/numerical-audit/run_experiments.sh /tmp/FRESH_DIRECTORY
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${1:-/tmp/leo-physical-sensitivity-study}
build=${LEO_AUDIT_BUILD:-/tmp/leo-audit-current}
cspice=${CSPICE_ROOT:-/tmp/leo_cspice/cspice}
prefix=${LEO_DEPS_PREFIX:-/tmp/leo-deps/usr}
kernels=${LEO_SPICE_KERNELS:-/tmp/leo_cspice/kernels}
if [ -e "$out" ]; then
  echo "Study output directory must be fresh: $out" >&2
  exit 2
fi
mkdir -p "$out/raw" "$out/runs"
cmake -S "$repo/docs/numerical-audit" -B "$build" -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_PREFIX_PATH=$prefix" -DCSPICE_ROOT="$cspice"
cmake --build "$build" --parallel "${LEO_BUILD_JOBS:-4}"
audit="$build/numerical_audit"
set -- "$repo/docs/numerical-audit/experiments/CASE-A-v1.cfg" \
       "$repo/docs/numerical-audit/experiments/CASE-B-v1.cfg" \
       "$repo/docs/numerical-audit/experiments/CASE-C-v1.cfg"
for letter in A B C; do
  python3 "$repo/docs/numerical-audit/experiment.py" --binary "$audit" \
    --spk "$kernels/de432s.bsp" --lsk "$kernels/naif0012.tls" \
    --cases "$repo/docs/numerical-audit/experiments/CASE-$letter-v1.cfg" \
    --output "$out/raw/sensitivity-$letter.json" --workdir "$out/runs"
  python3 "$repo/docs/numerical-audit/qualify_cadence.py" --binary "$audit" \
    --spk "$kernels/de432s.bsp" --lsk "$kernels/naif0012.tls" \
    --input "$out/raw/sensitivity-$letter.json" \
    --case "$repo/docs/numerical-audit/experiments/CASE-$letter-v1.cfg" \
    --output "$out/raw/qualified-$letter.json"
done
python3 "$repo/docs/numerical-audit/summarise.py" \
  --results "$out/raw/qualified-A.json" "$out/raw/qualified-B.json" "$out/raw/qualified-C.json" \
  --evidence "$out/sensitivity-results.json.gz" --output "$out/SENSITIVITY.md"
python3 "$repo/docs/numerical-audit/precision_probe.py" --binary "$audit" \
  --spk "$kernels/de432s.bsp" --lsk "$kernels/naif0012.tls" --cases "$@" \
  --workdir "$out/precision-runs" --envelope-input "$out/sensitivity-results.json.gz" \
  --output "$out/precision-events-v11.json"
python3 "$repo/docs/numerical-audit/case_c_probe.py" --binary "$audit" \
  --spk "$kernels/de432s.bsp" --lsk "$kernels/naif0012.tls" \
  --case "$repo/docs/numerical-audit/experiments/CASE-C-v1.cfg" \
  --output "$out/case-c-continuation-v11.json"
python3 "$repo/docs/numerical-audit/contribution_probe.py" --binary "$audit" \
  --spk "$kernels/de432s.bsp" --lsk "$kernels/naif0012.tls" --cases "$@" \
  --output "$out/force-contributions-v11.json"
python3 "$repo/docs/numerical-audit/cadence_probe.py" --binary "$audit" \
  --spk "$kernels/de432s.bsp" --lsk "$kernels/naif0012.tls" --cases "$@" \
  --output "$out/energy-balance-v12.json"
python3 "$repo/docs/numerical-audit/spacecraft_characteristics.py" \
  --inputs "$out/runs/CASE-A/rkf78-half.json" "$out/runs/CASE-B/rkf78-half.json" "$out/runs/CASE-C/rkf78-half.json" \
  --output "$out/spacecraft-characteristics.json"
python3 "$repo/docs/numerical-audit/orbital_diagnostics.py" \
  --workdirs "$out/runs/CASE-A" "$out/runs/CASE-B" "$out/runs/CASE-C" \
  --evidence "$out/sensitivity-results.json.gz" --output "$out/orbital-diagnostics-v11.json"
for letter in A B C; do
  horizon=21600
  if [ "$letter" = C ]; then horizon=3600; fi
  python3 "$repo/docs/numerical-audit/screen.py" --binary "$audit" \
    --spk "$kernels/de432s.bsp" --lsk "$kernels/naif0012.tls" \
    --results "$out/raw/qualified-$letter.json" --horizon "$horizon" \
    --case "$repo/docs/numerical-audit/experiments/CASE-$letter-v1.cfg" \
    --output "$out/screen-case-$letter-v11.json"
done
python3 "$repo/docs/numerical-audit/experiments/plot_sensitivity.py" \
  "$out/sensitivity-results.json.gz" --output "$out/figures" --horizon 21600 --observable sma_final_m
printf 'Study complete: %s\nRaw scientific trajectories stay outside committed evidence.\n' "$out"
