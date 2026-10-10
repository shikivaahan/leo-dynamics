#!/usr/bin/env python3
"""Compare native -O0/-O3 and adapter outputs on identical native inputs."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path

import jb2008_grid_analysis as grid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("kernel-o0", "kernel-o3", "adapter", "astro", "validation-dir", "output"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    records = []
    fixtures = sorted(Path(args.validation_dir).glob("*.DAT"))
    if len(fixtures) != 8:
        parser.error("supply the complete eight-epoch pinned validation distribution")
    for fixture in fixtures:
        epoch, indices, rows = grid.parse_epoch_file(str(fixture))
        d1950 = grid.days_since_1950_jan0(
            epoch["year"], epoch["doy"], epoch["hour"], epoch["minute"], epoch["second"])
        sample = rows[::1000]
        longitudes = sorted({row[1] for row in sample})
        astronomy = grid.run_astro(args.astro, [(d1950, lon) for lon in longitudes])
        positions = dict(zip(longitudes, astronomy))
        for altitude, longitude, latitude, *_ in sample:
            astro = positions[longitude]
            records.append([d1950 + 33281, astro[3], astro[4], astro[2],
                            latitude * math.pi / 180, altitude,
                            *indices["indices_printed"], indices["dtc_printed"]])
    unoptimised = grid.run_probe(args.kernel_o0, records, native=True)
    optimised = grid.run_probe(args.kernel_o3, records, native=True)
    adapter = grid.run_probe(args.adapter, records, native=False)
    report = {
        "cases": len(records), "stride": 1000,
        "inputs": "Identical binary64 native inputs; official SUNPOS/THETA; no coordinate/weather pipeline",
        "input_binary64_sha256": hashlib.sha256(
            json.dumps(records, separators=(",", ":")).encode()).hexdigest(),
        "runtime_env": {"LD_LIBRARY_PATH": os.environ.get("LD_LIBRARY_PATH")},
        "arguments": vars(args),
        "binary_sha256": {name: hashlib.sha256(Path(getattr(args, name)).read_bytes()).hexdigest()
                          for name in ("kernel_o0", "kernel_o3", "adapter", "astro")},
        "max_o0_o3_relative_density_difference": max(
            abs(a[0] - b[0]) / a[0] for a, b in zip(unoptimised, optimised)),
        "max_o0_o3_absolute_temperature_difference_k": max(
            abs(a[i] - b[i]) for a, b in zip(unoptimised, optimised) for i in (1, 2)),
        "max_native_adapter_relative_density_difference": max(
            abs(a[0] - b[0]) / a[0] for a, b in zip(optimised, adapter)),
        "max_native_adapter_absolute_temperature_difference_k": max(
            abs(a[i] - b[i]) for a, b in zip(optimised, adapter) for i in (1, 2)),
        "producer_compiler_settings": "Unavailable; local agreement cannot exclude a different producer model/compiler",
    }
    with Path(args.output).open("x") as handle:
        json.dump(report, handle, indent=2, allow_nan=False)
        handle.write("\n")
    print(json.dumps({key: value for key, value in report.items() if key.startswith("max_")}))


if __name__ == "__main__":
    main()
