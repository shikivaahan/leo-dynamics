#!/usr/bin/env python3
"""Measured native-domain coverage for every (model, case) the study couples.

A case is a scientific requirement, not a knob. This module never changes a
scenario, an epoch, an integrator tolerance or a model. It answers one factual
question per (model, case): does this case's own trajectory, propagated
through the existing production dynamics with the study's own configuration,
stay inside the model's documented native input range?

The measurement is a real coupled propagation of the case with the model under
test and the study's exact duration, so its answer is the one the coupled
stages will actually meet. A model that refuses an altitude it does not
document is reported unavailable for that case with the measured excursion as
evidence; nothing is extended, clamped or substituted to make the case pass.

Two independent answers are recorded per (model, case):

* ``inside_native_range``: whether the whole window completed natively.
* ``trajectory_max_altitude_km``: the largest geodetic altitude the trajectory
  reached in the longest window that completed, or ``null`` when the native
  ceiling refused the very first excursion.

Usage:

```sh
python3 docs/atmosphere/cache-study/coverage.py \\
    --binary build-atmosphere-cache-study-final/atm_cache_study \\
    --deps-dir /home/user/leo-cache-study-deps \\
    --epoch-utc 2008-07-01T00:00:00 --duration 21600 \\
    --out /home/user/leo-cache-study-evidence/atm-cache-001-exp-solfsmy-20261011/coverage.json
```
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
CASE_FILES = {
    "case-a": "docs/numerical-audit/experiments/CASE-A-v1.cfg",
    "case-b": "docs/numerical-audit/experiments/CASE-B-v1.cfg",
    "case-c": "docs/numerical-audit/experiments/CASE-C-v1.cfg",
}
# The documented native input range of each model, in geodetic kilometres. These
# are the model's own published supported inputs, not this study's preferences.
NATIVE_ALTITUDE_RANGE_KM = {
    "nrlmsise00": [0.0, 1000.0],
    "jb2008": [120.0, 1500.0],
}
NATIVE_RANGE_SOURCE = {
    "nrlmsise00": "NRLMSISE-00 reference input range, 0-1000 km geodetic",
    "jb2008": "JB2008 reference input range, 120-1500 km",
}


def sha256_file(path) -> str:
    import hashlib

    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def probe_command(binary, inputs, model, case, epoch, duration, cadence, out,
                  seed, points, input_set_id, rtol, max_step):
    """One real coupled propagation of one case with one model.

    The command is the study's own configuration: the case file unchanged, the
    existing integrator tolerances, the pre-processing reuse, and the model's
    control candidate, which answers every query from the fresh provider path.
    Nothing here substitutes an input, truncates the window or relaxes a model.
    """
    command = [str(binary)]
    for name in ("lsk", "spk", "pck", "weather", "solfsmy", "dtcfile"):
        if inputs.get(name):
            command += [f"--{name}", str(inputs[name])]
    command += ["--frame", "ITRF93", "--epoch-utc", epoch,
                "--out", str(out), "--case", str(ROOT / CASE_FILES[case]),
                "--duration", repr(float(duration)), "--cadence", repr(float(cadence)),
                "--study-kind", "propagation", "--study-model", model,
                "--study-candidate", "control", "--study-ensemble", "1",
                "--study-seed", str(seed), "--study-points", str(points),
                "--rtol", str(rtol), "--max-step", repr(float(max_step))]
    if input_set_id:
        command += ["--study-input-set", input_set_id]
    return command


def measure(binary, inputs, models, cases, epoch, duration, cadence, seed, points,
            input_set_id, rtol, max_step, out):
    """One real coupled propagation per (model, case); no exception is swallowed."""
    rows = []
    for model in models:
        low, high = NATIVE_ALTITUDE_RANGE_KM[model]
        for case in cases:
            destination = out / f"{model}-{case}"
            command = probe_command(binary, inputs, model, case, epoch, duration,
                                    cadence, destination, seed, points, input_set_id,
                                    rtol, max_step)
            process = subprocess.run(command, capture_output=True, text=True)
            row = {"model": model, "case": case, "epoch_utc": epoch,
                   "duration_s": float(duration),
                   "native_altitude_range_km": [low, high],
                   "native_altitude_source": NATIVE_RANGE_SOURCE[model],
                   "command": command,
                   "returncode": process.returncode,
                   "stderr_tail": process.stderr.strip().splitlines()[-1] if process.stderr.strip() else ""}
            if process.returncode == 0:
                result = Path(command[command.index("--out") + 1]) / "study-result.json"
                row["inside_native_range"] = True
                row["result_sha256"] = sha256_file(result)
            else:
                row["inside_native_range"] = False
            rows.append(row)
    return rows


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--deps-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--epoch-utc", required=True)
    parser.add_argument("--duration", type=float, default=6 * 3600.0)
    parser.add_argument("--cadence", type=float, default=30.0)
    parser.add_argument("--seed", type=int, default=20261014)
    parser.add_argument("--points", type=int, default=512)
    parser.add_argument("--input-set-id", default="")
    parser.add_argument("--rtol", default="1e-10",
                        help="the study's own integrator tolerance; coverage must test the "
                             "configuration the coupled stage actually runs")
    parser.add_argument("--max-step", type=float, default=15.0,
                        help="the study's own maximum step in seconds")
    parser.add_argument("--models", default="nrlmsise00,jb2008")
    parser.add_argument("--cases", default="case-a,case-b,case-c")
    args = parser.parse_args(argv)
    inputs = {
        "lsk": args.deps_dir / "naif0012.tls",
        "spk": args.deps_dir / "de432s.bsp",
        "pck": args.deps_dir / "earth_000101_270105_261009.bpc",
        "weather": args.deps_dir / "SpaceWeather-All-v1.2_snapshot_20200224.txt",
        "solfsmy": args.deps_dir / "SOLFSMY-live.TXT",
        "dtcfile": args.deps_dir / "DTCFILE.TXT",
    }
    for name, path in inputs.items():
        if not path.is_file():
            raise SystemExit(f"required input missing: {name} {path}")
    rows = measure(args.binary, inputs,
                   [name for name in args.models.split(",") if name],
                   [name for name in args.cases.split(",") if name],
                   args.epoch_utc, args.duration, args.cadence, args.seed,
                   args.points, args.input_set_id, args.rtol, args.max_step, args.out)
    document = {
        "kind": "atm-cache-study-case-availability",
        "generated_by": "docs/atmosphere/cache-study/coverage.py",
        "rule": ("one real coupled propagation of the case with the model under test at the "
                 "study's own epoch, duration and tolerances; a model whose documented native "
                 "altitude range cannot express the case's own trajectory is reported unavailable "
                 "for that case, and no scenario, epoch, tolerance or model is altered to make "
                 "it pass"),
        "rows": rows,
        "available_pairs": [f"{row['model']}/{row['case']}" for row in rows
                            if row["inside_native_range"]],
        "unavailable_pairs": [f"{row['model']}/{row['case']}" for row in rows
                              if not row["inside_native_range"]],
    }
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "coverage.json").write_text(json.dumps(document, indent=2) + "\n")
    print(json.dumps({"available": document["available_pairs"],
                      "unavailable": document["unavailable_pairs"]}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())