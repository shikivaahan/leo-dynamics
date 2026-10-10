#!/usr/bin/env python3
"""Scalar exospheric-temperature offset experiment for the official grid.

This diagnostic asks how much of the sampled residual a uniform thermal-input
perturbation can mimic. Altitude-dominant residual structure motivates the
experiment but does not establish a cause or exclude coordinate errors.

The fit adds a constant offset to DSTDTC and re-evaluates the unmodified model.
The coarse search is a scale diagnostic, not recovered storm data or proof of
an original solar-index rounding error. No fitted offset enters production.

Usage:
    jb2008_grid_scalar_tc.py --kernel PATH --astro PATH --validation-dir DIR
                             [--stride N] [--step K]
"""

import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jb2008_grid_analysis as A  # noqa: E402


def epoch_records(epoch, index, rows, astro, lons, d1950):
    """Probe records for the grid, under the driver's own reconstruction."""
    amjd = d1950 + 33281.0
    return [
        [
            amjd,
            epoch["sun_ra_printed_rad"],
            epoch["sun_dec_printed_rad"],
            astro[lons.index(lo)][2],
            la * math.pi / 180.0,
            z,
        ]
        + list(index["indices_printed"])
        + [index["dtc_printed"]]
        for z, lo, la, _tinf, _temp, _rho in rows
    ]


def stats(kernel, records, sample, shift):
    """Relative-error RMS and max for a constant DSTDTC offset."""
    shifted = [[*r] for r in records]
    for r in shifted:
        r[14] += shift
    out = A.run_probe(kernel, shifted, native=True)
    errs = [abs(o[0] - s[5]) / s[5] for o, s in zip(out, sample)]
    return {
        "rms": math.sqrt(sum(v * v for v in errs) / len(errs)),
        "max": max(errs),
        "mean_signed": sum((o[0] - s[5]) / s[5] for o, s in zip(out, sample))
        / len(errs),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--astro", required=True)
    ap.add_argument("--validation-dir", required=True)
    ap.add_argument("--stride", type=int, default=900)
    ap.add_argument("--step", type=float, default=0.05)
    ap.add_argument("--search", type=float, default=6.0)
    args = ap.parse_args()

    report = {
        "schema": "leo-dynamics/jb2008-scalar-tc-offset/1",
        "method": (
            "A constant offset is added to DSTDTC, which the model adds to the "
            "exospheric temperature as TEMP(1) = TSUBL + DSTDTC, and the offset "
            "minimising the relative density residual over the sampled grid is "
            "reported. The offset is a fitted scalar describing the published "
            "grid, not a correction applied to the model."
        ),
        "print_floor_relative_half_ulp": 0.5 / 10000.0,
        "stride": args.stride,
        "search_half_range_k": args.search,
        "search_step_k": args.step,
        "epochs": [],
    }
    for name in sorted(f for f in os.listdir(args.validation_dir) if f.endswith(".DAT")):
        path = os.path.join(args.validation_dir, name)
        epoch, index, rows = A.parse_epoch_file(path)
        d1950 = A.days_since_1950_jan0(
            epoch["year"], epoch["doy"], epoch["hour"], epoch["minute"],
            epoch["second"],
        )
        lons = sorted({r[1] for r in rows})
        astro = A.run_astro(args.astro, [(d1950, lo) for lo in lons])
        sample = rows[:: args.stride]
        records = epoch_records(epoch, index, sample, astro, lons, d1950)

        baseline = stats(args.kernel, records, sample, 0.0)
        best_shift, best = None, None
        steps = int(args.search / args.step)
        for k in range(-steps, steps + 1):
            shift = round(k * args.step, 10)
            candidate = stats(args.kernel, records, sample, shift)
            if best is None or candidate["rms"] < best["rms"]:
                best_shift, best = shift, candidate
        report["epochs"].append(
            {
                "file": name,
                "dtc_printed": index["dtc_printed"],
                "rows_sampled": len(sample),
                "baseline": baseline,
                "best_offset_k": best_shift,
                "after": best,
                "rms_removed_fraction": 1.0 - best["rms"] / baseline["rms"],
            }
        )

    shifts = [e["best_offset_k"] for e in report["epochs"]]
    report["summary"] = {
        "offset_range_k": [min(shifts), max(shifts)],
        "offset_mean_k": sum(shifts) / len(shifts),
        "all_epochs_below_1k": all(abs(s) < 1.0 for s in shifts),
        "interpretation": (
            "The per-epoch thermal offsets mimic most of the sampled density "
            "residual but leave disagreement above publication precision. "
            "This is a hypothetical input-sensitivity result, not recovered "
            "source data, a complete rounding explanation, or proof that a "
            "better fit is impossible; the search has finite resolution."
        ),
    }
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()