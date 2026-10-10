#!/usr/bin/env python3
"""Identify what the official-grid residual is a function of.

The grid residual is a single undifferentiated number until you ask which inputs
it moves with.  This measures, on a decimated sample of one published epoch:

  * the central-difference sensitivity of relative density to each of the eight
    printed solar indices, at step h and at h/2, so the two can be compared;
  * whether a constant per-index offset reproduces the residual, fitted by
    least squares over all eight indices jointly;
  * whether adding altitude or latitude freedom improves that fit, which is the
    test that distinguishes "the residual is the printed solar indices" from
    "the residual is something else in the geometry or the model";
  * the print-precision floor of the D15.5 density column, which is the best a
    reconstruction can do against the published number.

Degeneracy is reported, not hidden: if the fitted offsets are not identifiable
the fit still bounds the residual, but the individual offsets must not be
quoted as measured values.

Usage:
    jb2008_grid_identify.py --kernel PATH --astro PATH --validation-dir DIR
"""

import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jb2008_grid_analysis as A  # noqa: E402

# Relative half-ulp of the D15.5 density column: the mantissa is 0.ddddd, so the
# coarsest representable relative step is 0.5/99999 ~ 5.00005e-6 and the finest
# relative step, at the smallest mantissa 0.10000, is 0.5/10000 = 5e-5.
PRINT_REL_HALF_ULP_MAX = 0.5 / 10000.0
PRINT_REL_HALF_ULP_MIN = 0.5 / 99999.0


def central_difference(kernel, records, base, slot, step):
    """Relative-density central difference of one input slot.

    The numerator is the difference of the two one-sided relative changes, which
    is f(x+step) - f(x-step) with the common f(x) cancelling.  A central
    difference divides the full 2*step span, so the divisor is 2*step, not
    step; dividing by step alone doubles every sensitivity reported here.
    """
    plus = [list(r) for r in records]
    minus = [list(r) for r in records]
    for r in plus:
        r[slot] += step
    for r in minus:
        r[slot] -= step
    out_p = A.run_probe(kernel, plus, native=True)
    out_m = A.run_probe(kernel, minus, native=True)
    return [
        ((p[0] - b[0]) / b[0] - (m[0] - b[0]) / b[0]) / (2.0 * step)
        for p, m, b in zip(out_p, out_m, base)
    ]


def solve(columns, y):
    """Least squares by Gauss-Jordan with full pivoting; returns x and residual rms."""
    m = len(columns)
    n = len(y)
    aug = [
        [
            sum(columns[a][r] * columns[b][r] for r in range(n))
            + (1e-15 if a == b else 0.0)
            for b in range(m)
        ]
        + [sum(columns[a][r] * y[r] for r in range(n))]
        for a in range(m)
    ]
    for c in range(m):
        p = max(range(c, m), key=lambda rr: abs(aug[rr][c]))
        aug[c], aug[p] = aug[p], aug[c]
        if abs(aug[c][c]) < 1e-300:
            continue
        for r in range(m):
            if r != c:
                f = aug[r][c] / aug[c][c]
                for cc in range(c, m + 1):
                    aug[r][cc] -= f * aug[c][cc]
    x = [aug[k][m] / aug[k][k] if abs(aug[k][k]) > 1e-300 else 0.0 for k in range(m)]
    residual = [
        y[r] - sum(columns[a][r] * x[a] for a in range(m)) for r in range(n)
    ]
    rms = math.sqrt(sum(v * v for v in residual) / n)
    return x, rms


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--astro", required=True)
    ap.add_argument("--validation-dir", required=True)
    ap.add_argument("--epoch", default="JB2008_AUTO_OUT_2023_091_00.DAT")
    ap.add_argument("--stride", type=int, default=1000)
    ap.add_argument("--h", type=float, default=1.0)
    args = ap.parse_args()

    path = os.path.join(args.validation_dir, args.epoch)
    epoch, index, rows = A.parse_epoch_file(path)
    d1950 = A.days_since_1950_jan0(
        epoch["year"], epoch["doy"], epoch["hour"], epoch["minute"], epoch["second"]
    )
    lons = sorted({r[1] for r in rows})
    astro = A.run_astro(args.astro, [(d1950, lo) for lo in lons])
    amjd = d1950 + 33281.0
    sample = rows[:: args.stride]
    n = len(sample)

    # Slot layout matches the probe contract: 0 amjd, 1-2 sun, 3-5 sat, 6..13
    # indices, 14 dtc.  Satellite latitude enters as radians, the printed grid
    # being in degrees.
    records = []
    for z, lo, la, _tinf, _temp, _rho in sample:
        records.append(
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
        )
    base = A.run_probe(args.kernel, records, native=True)
    y = [(b[0] - r[5]) / r[5] for b, r in zip(base, sample)]

    report = {
        "epoch": args.epoch,
        "sample_rows": n,
        "stride": args.stride,
        "baseline_relative_rms": math.sqrt(sum(v * v for v in y) / n),
        "print_precision": {
            "column": "RHO, FORMAT D15.5",
            "relative_half_ulp_max": PRINT_REL_HALF_ULP_MAX,
            "relative_half_ulp_min": PRINT_REL_HALF_ULP_MIN,
        },
        "index_sensitivity": {},
        "fits": {},
    }

    columns_by_step = {}
    for step in (args.h, args.h / 2.0):
        cols = [
            central_difference(args.kernel, records, base, 6 + k, step)
            for k in range(8)
        ]
        columns_by_step[step] = cols
        x, rms = solve(cols, y)
        report["fits"][f"indices_h_{step}"] = {
            "fitted_offsets": {
                A.INDEX_NAMES[k]: x[k] for k in range(8)
            },
            "residual_rms_after": rms,
        }
        report["fits"][f"indices_h_{step}"]["note"] = (
            "Offsets are joint least-squares estimates at one epoch and are "
            "reported for the residual they remove, not as measured values: the "
            "eight indices are strongly collinear in their effect on density, so "
            "individual offsets are not identifiable.  They are not bounded by "
            "the F8.0 print either; several exceed 0.5 by orders of magnitude, "
            "which is itself evidence that the fit is absorbing collinearity."
        )

    h1 = args.h
    h2 = args.h / 2.0
    for k in range(8):
        d1 = columns_by_step[h1][k]
        d2 = columns_by_step[h2][k]
        mean_h = sum(d1) / n
        mean_half = sum(d2) / n
        # Richardson extrapolation for a CENTRAL difference, whose leading error
        # term is proportional to h^2: D(h/2) = f' + c2 h^2/4, so
        #   f' = (4*D(h/2) - D(h)) / 3.
        # The (2*D(h/2) - D(h)) form is the rule for a FORWARD difference, whose
        # error term is proportional to h; using it here would leave a residual
        # O(h^2) bias in the extrapolated value.
        report["index_sensitivity"][A.INDEX_NAMES[k]] = {
            "relative_derivative_h": mean_h,
            "relative_derivative_h_half": mean_half,
            "richardson_extrapolated": (4.0 * mean_half - mean_h) / 3.0,
            "richardson_form": "(4*D(h/2) - D(h))/3, central-difference rule",
            "h_vs_h_half_relative_gap": (
                abs(mean_h - mean_half) / abs(mean_h) if mean_h else None
            ),
            "relative_half_ulp_share": (
                PRINT_REL_HALF_ULP_MAX / abs(sum(d1) / n) if sum(d1) else None
            ),
        }

    # Does geometry freedom explain the residual better than the indices do?
    # Probed on the printed altitude (probe slot 5) and on the latitude the
    # caller supplies (slot 4), both of which the fixture fixes exactly.  These
    # use the same 2*step central difference as the index derivatives; in a
    # scale-invariant least-squares fit the divisor would cancel, but the columns
    # are built identically so the reported coefficients are comparable.
    alt_col = central_difference(args.kernel, records, base, 5, 10.0)
    lat_col = central_difference(args.kernel, records, base, 4, 0.01)
    _x, rms_geo = solve(columns_by_step[h1] + [alt_col, lat_col], y)
    report["fits"]["indices_plus_altitude_plus_latitude_h_%s" % h1] = {
        "residual_rms_after": rms_geo,
        "note": (
            "If geometry freedom materially improved on the index-only fit, the "
            "residual would not be attributable to the printed solar indices.  "
            "Compare with indices_h_%s." % h1
        ),
    }
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()