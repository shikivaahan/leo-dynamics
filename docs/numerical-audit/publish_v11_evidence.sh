#!/bin/sh
# Rebuild the published v1.1 study evidence from already-measured corrected-binary runs.
# Each stage refuses to overwrite existing evidence; move or delete the old file first.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
audit=${1:?usage: publish_v11_evidence.sh /path/to/numerical_audit}
kernels=${LEO_SPICE_KERNELS:-/tmp/leo_cspice/kernels}
raw=${RAW_DIR:?set RAW_DIR to the corrected-binary raw/ and runs/ directories}
a="$repo/docs/numerical-audit"
for letter in A B C; do
  python3 "$a/qualify_cadence.py" --binary "$audit" \
    --spk "$kernels/de432s.bsp" --lsk "$kernels/naif0012.tls" \
    --input "$raw/sensitivity-$letter.json" \
    --case "$a/experiments/CASE-$letter-v1.cfg" \
    --output "$raw/qualified-$letter.json"
done
python3 "$a/summarise.py" --results "$raw/qualified-A.json" "$raw/qualified-B.json" "$raw/qualified-C.json" \
  --evidence "$a/evidence/sensitivity-results.json.gz" --output "$a/SENSITIVITY.md"
python3 "$a/spacecraft_characteristics.py" --inputs "$raw/runs/CASE-A/rkf78-half.json" \
  "$raw/runs/CASE-B/rkf78-half.json" "$raw/runs/CASE-C/rkf78-half.json" \
  --output "$a/evidence/spacecraft-characteristics.json" --overwrite
python3 "$a/orbital_diagnostics.py" --workdirs "$raw/runs/CASE-A" "$raw/runs/CASE-B" "$raw/runs/CASE-C" \
  --evidence "$a/evidence/sensitivity-results.json.gz" --output "$a/evidence/orbital-diagnostics-v11.json"
for letter in A B C; do
  horizon=21600; [ "$letter" = C ] && horizon=3600
  mv "$a/evidence/screen-case-$letter-v11.json" "$raw/screen-case-$letter-v11.json"
  python3 "$a/screen.py" --binary "$audit" --spk "$kernels/de432s.bsp" --lsk "$kernels/naif0012.tls" \
    --results "$raw/qualified-$letter.json" --horizon "$horizon" \
    --case "$a/experiments/CASE-$letter-v1.cfg" --output "$a/evidence/screen-case-$letter-v11.json"
done
python3 "$a/experiments/plot_sensitivity.py" "$a/evidence/sensitivity-results.json.gz" \
  --output "$a/evidence/figures" --horizon 21600 --observable sma_final_m
printf 'Evidence refreshed from corrected-binary measurements.\n'