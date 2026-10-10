#!/usr/bin/env python3
"""Constrained +-0.5 index fit against the official grid, and archive evaluation.

Two questions, both answered by running the unmodified official model rather
than by restating a number produced earlier:

1.  Can a SINGLE set of index offsets, all inside the +-0.5 half-step that the
    fixture's own F8.0 columns imply, reproduce the published density across a
    whole epoch?  This is fitted by direct evaluation of the model, not by
    linearisation: the objective is the actual probe output, so no assumption is
    made that density is linear in the indices over the interval.

2.  What do the CURRENT archive index files give if used directly?  They are a
    different data vintage from the one that produced the fixture, so this
    measures how much worse they are rather than assuming it.

The fit is reported honestly against the D15.5 print floor.  If it cannot reach
that floor the residue is reported as UNRESOLVED rather than folded into the
claim.

Usage:
    jb2008_grid_bounded_fit.py --kernel PATH --astro PATH --validation-dir DIR
                               --index-dir DIR [--stride N] [--epochs ...]
"""

import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jb2008_grid_analysis as A  # noqa: E402

# The +-0.5 the fixture's F8.0 index columns imply.
BOUND = 0.5

# Relative half-ulp of the D15.5 density column, worst case at mantissa 0.10000.
PRINT_REL_HALF_ULP_MAX = 0.5 / 10000.0


def build(epoch, index, sample, astro, lons, d1950, offsets):
    """Probe records for the sample with the given index offsets applied."""
    amjd = d1950 + 33281.0
    out = []
    for z, lo, la, _tinf, _temp, _rho in sample:
        out.append(
            [
                amjd,
                epoch["sun_ra_printed_rad"],
                epoch["sun_dec_printed_rad"],
                astro[lons.index(lo)][2],
                la * math.pi / 180.0,
                z,
            ]
            + [index["indices_printed"][k] + offsets[k] for k in range(8)]
            + [index["dtc_printed"]]
        )
    return out


def residual_stats(kernel, records, sample):
    """Signed relative residual mean and RMS of the native output vs the fixture."""
    out = A.run_probe(kernel, records, native=True)
    signed = [(o[0] - s[5]) / s[5] for o, s in zip(out, sample)]
    mean = sum(signed) / len(signed)
    return {
        "mean_signed": mean,
        "rms": math.sqrt(sum(v * v for v in signed) / len(signed)),
        "max_abs": max(abs(v) for v in signed),
    }


def bounded_fit(kernel, records, base_indices, sample,
                rounds=60, steps=(0.5, 0.25, 0.1, 0.05, 0.02, 0.01)):
    """Coordinate descent minimising the true RMS, clamped to |offset| <= BOUND.

    `records` are the probe records with the printed indices already in place;
    `base_indices` are those printed values, so a trial offset set is applied by
    overwriting slots 6..13.  The objective is recomputed from the model at every
    trial, so this is a nonlinear fit in the model's own terms rather than a
    linear least-squares surrogate.
    """

    def with_offsets(offsets):
        out = []
        for r in records:
            c = list(r)
            for k in range(8):
                c[6 + k] = base_indices[k] + offsets[k]
            out.append(c)
        return out

    current = [0.0] * 8
    best = residual_stats(kernel, records, sample)
    for _ in range(rounds):
        improved = False
        for k in range(8):
            for step in steps:
                for sign in (1.0, -1.0):
                    candidate = current[k] + sign * step
                    if abs(candidate) > BOUND or abs(candidate - current[k]) < 1e-12:
                        continue
                    trial = list(current)
                    trial[k] = candidate
                    stats = residual_stats(kernel, with_offsets(trial), sample)
                    if stats["rms"] < best["rms"] - 1e-15:
                        current, best, improved = trial, stats, True
        if not improved:
            break
    return current, best


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--astro", required=True)
    ap.add_argument("--validation-dir", required=True)
    ap.add_argument("--index-dir")
    ap.add_argument("--stride", type=int, default=300)
    ap.add_argument("--epochs", nargs="*", default=None)
    args = ap.parse_args()

    names = args.epochs or sorted(
        f for f in os.listdir(args.validation_dir) if f.endswith(".DAT")
    )

    index_table = dtc_table = None
    if args.index_dir:
        index_table = A.load_index_file(os.path.join(args.index_dir, "SOLFSMY.TXT"))
        dtc_table = A.load_dtc_file(os.path.join(args.index_dir, "DTCFILE.TXT"))

    report = {
        "schema": "leo-dynamics/jb2008-bounded-fit/1",
        "bound": BOUND,
        "bound_meaning": (
            "half-step of the fixture's F8.0 index columns; an offset inside "
            "this box is one the published precision cannot distinguish from "
            "the printed value"
        ),
        "fit_method": (
            "coordinate descent on the true model output, every trial point "
            "re-evaluated through the unmodified official Fortran kernel; no "
            "linearisation of density in the indices is assumed"
        ),
        "print_floor_relative_half_ulp_max": PRINT_REL_HALF_ULP_MAX,
        "stride": args.stride,
        "epochs": [],
    }

    for name in names:
        path = os.path.join(args.validation_dir, name)
        epoch, index, rows = A.parse_epoch_file(path)
        d1950 = A.days_since_1950_jan0(
            epoch["year"], epoch["doy"], epoch["hour"], epoch["minute"],
            epoch["second"],
        )
        lons = sorted({r[1] for r in rows})
        astro = A.run_astro(args.astro, [(d1950, lo) for lo in lons])
        sample = rows[:: args.stride]
        base_indices = list(index["indices_printed"])
        records = build(epoch, index, sample, astro, lons, d1950, [0.0] * 8)

        offsets, fitted = bounded_fit(
            args.kernel, records, base_indices, sample
        )
        baseline = residual_stats(args.kernel, records, sample)

        entry = {
            "file": name,
            "dtc_printed": index["dtc_printed"],
            "hour_utc": epoch["hour"],
            "rows_sampled": len(sample),
            "indices_printed": base_indices,
            "baseline": baseline,
            "fitted_offsets": {A.INDEX_NAMES[k]: offsets[k] for k in range(8)},
            "after_bounded_fit": fitted,
            "rms_removed_fraction": 1.0 - fitted["rms"] / baseline["rms"],
            "reaches_print_floor": fitted["rms"] <= PRINT_REL_HALF_ULP_MAX,
            "offset_at_bound": {
                A.INDEX_NAMES[k]: abs(offsets[k]) >= BOUND - 1e-9
                for k in range(8)
            },
        }

        if index_table is not None:
            archive = A.reconstruct_indices(index_table, d1950)
            archive_dtc = A.reconstruct_dtc(dtc_table, d1950)
            if archive is not None:
                entry["current_archive"] = {
                    "indices": archive,
                    "indices_minus_printed": {
                        A.INDEX_NAMES[k]: archive[k] - base_indices[k]
                        for k in range(8)
                    },
                    "within_print_bound": sum(
                        1 for k in range(8)
                        if abs(archive[k] - base_indices[k]) <= BOUND
                    ),
                    "indices_total": 8,
                    "dtc_from_archive": archive_dtc,
                    "dtc_matches_printed": archive_dtc == index["dtc_printed"],
                    "stats": residual_stats(
                        args.kernel,
                        build(epoch, index, sample, astro, lons, d1950,
                              [archive[k] - base_indices[k] for k in range(8)]),
                        sample,
                    ),
                }

        report["epochs"].append(entry)

    fit_rms = [e["after_bounded_fit"]["rms"] for e in report["epochs"]]
    base_rms = [e["baseline"]["rms"] for e in report["epochs"]]
    report["summary"] = {
        "baseline_rms_range": [min(base_rms), max(base_rms)],
        "bounded_fit_rms_range": [min(fit_rms), max(fit_rms)],
        "rms_removed_fraction_range": [
            min(e["rms_removed_fraction"] for e in report["epochs"]),
            max(e["rms_removed_fraction"] for e in report["epochs"]),
        ],
        "epochs_reaching_print_floor": sum(
            1 for e in report["epochs"] if e["reaches_print_floor"]
        ),
        "epochs_total": len(report["epochs"]),
        "classification": (
            "PARTIAL"
            if any(not e["reaches_print_floor"] for e in report["epochs"])
            else "COMPLETE_AT_PRINT_PRECISION"
        ),
    }
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()