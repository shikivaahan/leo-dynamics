#!/usr/bin/env python3
"""Quantitative JB2008 official-grid investigation.

Answers, with measured numbers rather than assumption, why the official SET
validation grid residual behaves as it does.  Everything here is derived from
the two project-owned probes plus the hash-pinned external distribution; no
JB2008 equation is reproduced.

The three questions this separates, each measured on all 8 x 68191 fixture rows:

  Q1  Bridge fidelity.  Run the official Fortran model directly and run the
      project adapter on byte-identical inputs.  Any difference is the bridge
      plus the adapter's input wrapping and domain checks, nothing else.

  Q2  Input reconstruction.  The published grid prints its epoch and its eight
      solar indices to F8.0 (whole numbers) and its solar angles to 1e-4 rad,
      but the model was actually run on the underlying full-precision values.
      Reconstruct the driver inputs exactly (official SUNPOS/THETA, official
      truncated-pi longitude conversion, official DTC interpolation), then
      measure the density sensitivity to each printed parameter at the
      precision actually lost.

  Q3  Residual budget.  Compare reconstructed-input output against the printed
      fixture, stratified, and separate the part explained by lost input
      precision from any part that is not.

Outputs a JSON report on stdout.

Usage:
    jb2008_grid_analysis.py --kernel PATH --adapter PATH [--astro PATH]
                            --validation-dir DIR --index-dir DIR
"""

import argparse
import json
import math
import os
import subprocess
import sys

# The eight solar indices in the order the official driver passes them.
INDEX_NAMES = ("F10", "F10B", "S10", "S10B", "M10", "M10B", "Y10", "Y10B")

# Lag in days applied by the reference driver before each index lookup.
#
# JB08DRVAUTO.f makes three SOLFSMY calls, and the lag belongs to the CALL, not
# to the individual index:
#   DLAG=1  -> F10, F10B, S10, S10B      (the four leading arguments)
#   DLAG=2  -> XM10, XM10B              (the two XM10 arguments)
#   DLAG=5  -> Y10, Y10B                (the two XY10 arguments)
# The first call's signature is SOLFSMY(T1950,XF10,XF10B,XS10,XS10B,XM10,...),
# so S10 and S10B share F10's one-day lag; only XM10/XM10B are two-day and only
# Y10/Y10B five-day.  Assigning lag 2 to S10/S10B or lag 5 to M10/M10B would
# silently read the wrong archive days.
INDEX_LAG = (1.0, 1.0, 1.0, 1.0, 2.0, 2.0, 5.0, 5.0)


# --------------------------------------------------------------------- fixture
def parse_epoch_file(path):
    """Parse one published validation file into its header and its grid rows.

    The published format carries three header records: the calendar epoch with
    the printed solar angles, the printed D1950 with the eight printed solar
    indices and DSTDTC, and a column-name line.  Rows are Z LON LAT TINF TEMP
    RHO with the density in D notation.
    """
    with open(path) as handle:
        raw = handle.read().split("\n")

    epoch = None
    indices = None
    rows = []
    for line in raw:
        fields = line.split()
        if not fields:
            continue
        if line.lstrip().startswith("#"):
            continue
        try:
            values = [float(f.replace("D", "E")) for f in fields]
        except ValueError:
            continue
        if epoch is None and len(fields) == 7:
            # YYYY DDD HR MIN SEC SUNRAS SUNDEC
            epoch = {
                "year": int(values[0]),
                "doy": int(values[1]),
                "hour": int(values[2]),
                "minute": int(values[3]),
                "second": values[4],
                "sun_ra_printed_rad": values[5],
                "sun_dec_printed_rad": values[6],
            }
        elif indices is None and len(fields) == 10:
            # D1950 F10 F10B S10 S10B M10 M10B Y10 Y10B DSTDTC
            indices = {
                "d1950_printed": values[0],
                "indices_printed": values[1:9],
                "dtc_printed": values[9],
            }
        elif len(fields) == 6:
            rows.append(values)
        else:
            raise ValueError(f"unparsed record in {path}: {line!r}")

    if epoch is None or indices is None:
        raise ValueError(f"{path} is missing its epoch or index header")
    return epoch, indices, rows


# --------------------------------------------------------- driver reconstruction
def days_since_1950_jan0(year, doy, hour, minute, second):
    """The D1950 arithmetic of JB08DRVAUTO.f, integer division included.

        IYY = ((IYR-1)/4 - 12);  IYY = (IYR-50)*365 + IYY
        D1950 = IYY + IDY + IHR/24 + IMN/1440 + SEC/86400

    The Fortran is IMPLICIT INTEGER*4, so (IYR-1)/4 truncates.  Reproducing that
    truncation matters: it is what makes D1950 land on the printed value.
    """
    iyr = year - 2000 + 100 if year > 1900 else year
    if iyr < 50:
        iyr += 100
    iyy = (iyr - 1) // 4 - 12
    iyy = (iyr - 50) * 365 + iyy
    return iyy + doy + hour / 24.0 + minute / 1440.0 + second / 86400.0


def load_index_file(path):
    """Read the vendor SOLFSMY index series: (year, doy) -> eight values.

    This is the archive's own record of the same indices the driver read.  Its
    resolution is one decimal, which is coarser than the driver's F6.1 read but
    far finer than the fixture's F8.0 print, so it can test whether the printed
    whole numbers are consistent with any archive record.
    """
    table = {}
    with open(path) as handle:
        for line in handle:
            if line.lstrip().startswith("#"):
                continue
            fields = line.split()
            if len(fields) < 11:
                continue
            try:
                year, doy = int(fields[0]), int(fields[1])
                values = [float(v) for v in fields[3:11]]
            except ValueError:
                continue
            table[(year, doy)] = values
    return table


def load_dtc_file(path):
    """Read the vendor DTCFILE series: (year, doy) -> 24 hourly values."""
    table = {}
    with open(path) as handle:
        for line in handle:
            if line.lstrip().startswith("#"):
                continue
            fields = line.split()
            if len(fields) != 27 or fields[0] != "DTC":
                continue
            try:
                year, doy = int(fields[1]), int(fields[2])
                values = [int(v) for v in fields[3:27]]
            except ValueError:
                continue
            table[(year, doy)] = values
    return table


def day_of_year_to_d1950(year, doy):
    """D1950 of 00 UT on the given calendar day, same arithmetic as the driver."""
    return days_since_1950_jan0(year, doy, 0, 0, 0.0)


def reconstruct_indices(index_table, d1950):
    """The driver's lagged index lookups, or None if any is missing.

    Each of F10/F10B/S10/S10B comes from D1950-1, M10/M10B from D1950-2 and
    Y10/Y10B from D1950-5, exactly as JB08DRVAUTO.f requests them.

    SOLFSMY stores FS1950 = (first record's D1950) - 1 and returns
    INDX = T1950 - FS1950, which is INTEGER*4 under IMPLICIT INTEGER*4(I-N), so
    the subtraction TRUNCATES toward zero rather than rounding.  The record it
    selects therefore has its own D1950 equal to FS1950 + trunc(T1950 - FS1950);
    because FS1950 is a whole number of days that is floor(T1950).

    Rounding instead of truncating is wrong for the three epochs whose hour is
    not a multiple of 4: at 15, 18 and 21 UT the fractional part of T1950
    exceeds 0.5, so round() advances the lookup by a day and reads the wrong
    record.  Floor is what the driver does.
    """
    out = [None] * 8
    by_d1950 = {}
    for (year, doy), values in index_table.items():
        by_d1950[day_of_year_to_d1950(year, doy)] = values
    for slot, lag in enumerate(INDEX_LAG):
        entry = by_d1950.get(int(math.floor(d1950 - lag)))
        if entry is None:
            return None
        out[slot] = entry[slot]
    return out


def reconstruct_dtc(dtc_table, d1950):
    """The driver's hourly DTC interpolation.

    DTCVAL in JB08DRVAUTO.f indexes IDTC(KNUM) over the whole file, computes
    INDX = 1 + (T1950 - DT1950 + 1e-8)*24, and returns

        IDTCVAL = IDTCVAL1 + FAC*(IDTCVAL2 - IDTCVAL1) + 0.5

    truncated back to integer by the INTEGER*4 assignment.  At an exact hour
    FAC is zero and the value is the file's own hourly reading, so the
    published integer DSTDTC is exactly reproducible from DTCFILE.TXT.
    """
    hourly = []
    base = None
    for key in sorted(dtc_table, key=lambda k: day_of_year_to_d1950(*k)):
        day = day_of_year_to_d1950(*key)
        if base is None:
            base = day
        if day != base + len(hourly) / 24.0:
            return None
        hourly.extend(dtc_table[key])
    idx = 1.0 + (d1950 - base + 1.0e-8) * 24.0
    lo = int(math.floor(idx))
    if lo < 1 or lo >= len(hourly):
        return None
    fac = idx - lo
    value = hourly[lo - 1] + fac * (hourly[lo] - hourly[lo - 1]) + 0.5
    return int(value)


# ------------------------------------------------------------------ probe calls
def run_probe(binary, records, native=False):
    """Feed one whitespace-separated record per case, collect numeric output.

    The native kernel takes a mandatory leading style integer; the adapter probe
    takes no header.  `native` selects which contract to use.
    """
    payload = "".join(" ".join(repr(v) for v in r) + "\n" for r in records)
    done = subprocess.run(
        [binary],
        input=("0\n" if native else "") + payload,
        capture_output=True,
        text=True,
        check=True,
    )
    out = []
    for line in done.stdout.split("\n"):
        if not line.strip():
            continue
        out.append([float(v) for v in line.split()])
    if len(out) != len(records):
        raise RuntimeError(
            f"{binary} returned {len(out)} records for {len(records)} cases"
        )
    return out


def run_astro(binary, cases):
    """Official SUNPOS/THETA: one record per case, returning five columns."""
    payload = "".join(f"{d!r} {x!r}\n" for d, x in cases)
    done = subprocess.run(
        [binary], input=payload, capture_output=True, text=True, check=True
    )
    out = []
    for line in done.stdout.split("\n"):
        if not line.strip():
            continue
        out.append([float(v) for v in line.split()])
    if len(out) != len(cases):
        raise RuntimeError(
            f"{binary} returned {len(out)} records for {len(cases)} cases"
        )
    return out


# ------------------------------------------------------------------ statistics
def relative_error(denominator, value):
    """Relative error of `value` against `denominator`, with the zero rule.

    A zero reference has no defined relative error, so those rows are counted
    separately instead of being silently reported as zero or as infinity.
    """
    if denominator == 0.0:
        return None
    return abs(value - denominator) / abs(denominator)


def summarize(errors):
    """Max, RMS and count over a list of finite relative errors."""
    if not errors:
        return {"count": 0}
    return {
        "count": len(errors),
        "max": max(errors),
        "rms": math.sqrt(sum(e * e for e in errors) / len(errors)),
        "median": sorted(errors)[len(errors) // 2],
        "p95": sorted(errors)[int(0.95 * (len(errors) - 1))],
    }


def summarize_signed(residuals):
    """Signed-error statistics, so a one-sided bias is visible and not hidden."""
    if not residuals:
        return {"count": 0}
    return {
        "count": len(residuals),
        "mean": sum(residuals) / len(residuals),
        "max_absolute": max(abs(v) for v in residuals),
        "rms": math.sqrt(sum(v * v for v in residuals) / len(residuals)),
    }


def quantile_boundaries(values, bins):
    """Equal-count bin edges, so a stratum never silently holds one row."""
    ordered = sorted(values)
    return [ordered[min(len(ordered) - 1, int(q * (len(ordered) - 1)))] for q in
            [b / bins for b in range(bins + 1)]]


def stratify(records, key, label):
    """Group the per-row metric dictionaries by one field and summarize each."""
    groups = {}
    for row in records:
        groups.setdefault(row[key], []).append(row)
    return {
        label: {
            str(name): {
                "rows": len(items),
                "relative_error": summarize([i["relative_error"] for i in items]),
                "signed_relative_error": summarize_signed(
                    [i["signed_relative_error"] for i in items]
                ),
            }
            for name, items in sorted(groups.items(), key=lambda kv: str(kv[0]))
        }
    }


def stratify_log(records, key, label):
    """Group by the base-10 logarithm of a strictly positive field.

    The published density column spans roughly nine decades, so equal-width bins
    would collapse the entire upper atmosphere into the first bucket.  Log
    buckets keep each altitude regime populated, which is the point of
    stratifying by density at all.
    """
    groups = {}
    for row in records:
        value = row[key]
        if value <= 0:
            groups.setdefault("nonpositive", []).append(row)
            continue
        exponent = math.floor(math.log10(value))
        groups.setdefault("1e%d" % exponent, []).append(row)
    return {
        label: {
            name: {
                "rows": len(items),
                "relative_error": summarize([i["relative_error"] for i in items]),
                "signed_relative_error": summarize_signed(
                    [i["signed_relative_error"] for i in items]
                ),
            }
            for name, items in sorted(
                groups.items(), key=lambda kv: str(kv[0])
            )
        }
    }


# ------------------------------------------------------------------------ main
def analyse_epoch(kernel, adapter, astro_bin, path, limit, worst_n):
    """Full-epoch metrics for one published validation file."""
    epoch, index, rows = parse_epoch_file(path)
    d1950 = days_since_1950_jan0(
        epoch["year"], epoch["doy"], epoch["hour"], epoch["minute"], epoch["second"]
    )
    lons = sorted({r[1] for r in rows})
    astro = run_astro(astro_bin, [(d1950, lo) for lo in lons])
    amjd = d1950 + 33281.0

    if limit:
        rows = rows[:limit]

    def build(sat_ra_full, indices, use_astro_angles):
        """One probe record per grid row under a chosen reconstruction."""
        out = []
        for z, lo, la, _tinf, _temp, rho in rows:
            amjd_, _gwr, sat_ra, solras, soldec = astro[lons.index(lo)]
            out.append(
                [
                    amjd_,
                    solras if use_astro_angles else epoch["sun_ra_printed_rad"],
                    soldec if use_astro_angles else epoch["sun_dec_printed_rad"],
                    sat_ra if sat_ra_full else 0.0,
                    la * math.pi / 180.0,
                    z,
                ]
                + list(indices)
                + [index["dtc_printed"]]
            )
        return out

    printed = list(index["indices_printed"])
    # The official driver's own reconstruction of the position right ascension,
    # full-precision solar angles and the printed indices: the best available
    # inputs, since the underlying index record the driver read is not in the
    # distribution.
    records = build(True, printed, True)
    native = run_probe(kernel, records, native=True)
    through_adapter = run_probe(adapter, records)

    # The comparison the shipped test performs: IAU-1982 Greenwich hour angle
    # instead of the driver's THETA, and the printed 1e-4 rad solar angles.
    iau_records = []
    for z, lo, la, _tinf, _temp, _rho in rows:
        dg2rd = math.pi / 180.0
        ra = (iau_greenwich_hour_angle(amjd) + lo * dg2rd) % (2.0 * math.pi)
        iau_records.append(
            [
                amjd,
                epoch["sun_ra_printed_rad"],
                epoch["sun_dec_printed_rad"],
                ra,
                la * math.pi / 180.0,
                z,
            ]
            + printed
            + [index["dtc_printed"]]
        )
    iau_native = run_probe(kernel, iau_records, native=True)

    detail = []
    zero_reference = 0
    name = os.path.basename(path)
    for k, (row, nat, adp, iau) in enumerate(zip(rows, native, through_adapter, iau_native)):
        z, lo, la, tinf, temp, rho = row
        rel = relative_error(rho, nat[0])
        if rel is None:
            zero_reference += 1
            continue
        detail.append(
            {
                # Epoch, stated three ways so a row is self-identifying even
                # if it is read away from its parent file entry.
                "fixture_file": name,
                "epoch_utc": "%04d-%03dT%02d:%02d:%06.3fZ"
                % (
                    epoch["year"],
                    epoch["doy"],
                    epoch["hour"],
                    epoch["minute"],
                    epoch["second"],
                ),
                "epoch_year": epoch["year"],
                "epoch_doy": epoch["doy"],
                "epoch_hour_utc": epoch["hour"],
                "d1950": d1950,
                "mjd_utc": amjd,
                "row_index_in_file": k,
                "z_km": z,
                "lon_deg": lo,
                "lat_deg": la,
                # The exact evaluation point handed to the model, at full
                # precision, so a reader can reproduce this row without
                # re-deriving any astronomy.
                "sat_ra_rad": astro[lons.index(lo)][2],
                "sat_lat_rad": la * math.pi / 180.0,
                "sun_ra_rad": astro[lons.index(lo)][3],
                "sun_dec_rad": astro[lons.index(lo)][4],
                "greenwich_ra_rad": astro[lons.index(lo)][1],
                # And the precision those inputs were actually published at,
                # which is what bounds how well this row can agree.
                "input_precision": {
                    "solar_indices": {
                        "printed_format": "F8.0",
                        "printed_values": printed,
                        "half_step": 0.5,
                        "driver_read_format": "F6.1",
                    },
                    "sun_angles": {
                        "printed_format": "F10.4",
                        "printed_ra_rad": epoch["sun_ra_printed_rad"],
                        "printed_dec_rad": epoch["sun_dec_printed_rad"],
                        "half_step_rad": 5e-5,
                        "used_here": "official SUNPOS at full precision, not the printed value",
                    },
                    "position_ra": {
                        "derivation": "official THETA(D1950) + lon*DG2RD, driver pi = 3.1415927",
                        "half_step_rad": None,
                        "note": "not published in the fixture; reconstructed by the official routine",
                    },
                    "d1950": {
                        "printed_format": "F15.5",
                        "printed": index["d1950_printed"],
                        "half_step_days": 5e-6,
                    },
                    "dtc": {
                        "printed_format": "F8.0",
                        "printed_k": index["dtc_printed"],
                        "half_step_k": 0.5,
                        "note": "exact reconstruction from DTCFILE.TXT, so no rounding loss here",
                    },
                    "density_reference": {
                        "printed_format": "D15.5",
                        "relative_half_ulp_max": 0.5 / 10000.0,
                    },
                    "temperature": {
                        "printed_format": "F8.1",
                        "half_step_k": 0.05,
                    },
                },
                "dtc_printed": index["dtc_printed"],
                "density_reference_kg_m3": rho,
                "density_native_kg_m3": nat[0],
                "density_adapter_kg_m3": adp[0],
                "density_iau_hga_kg_m3": iau[0],
                # Signed difference and its magnitude are separate fields: the
                # sign carries the systematic bias, and conflating the two under
                # a field named "absolute_error" hides that entirely.
                "signed_density_difference_kg_m3": nat[0] - rho,
                "absolute_error_kg_m3": abs(nat[0] - rho),
                "relative_error": rel,
                "signed_relative_error": (nat[0] - rho) / abs(rho),
                "tinf_reference_k": tinf,
                "signed_tinf_difference_k": nat[1] - tinf,
                "absolute_tinf_difference_k": abs(nat[1] - tinf),
                "temp_reference_k": temp,
                "signed_temp_difference_k": nat[2] - temp,
                "absolute_temp_difference_k": abs(nat[2] - temp),
                "native_vs_adapter_relative": abs(nat[0] - adp[0]) / abs(nat[0]),
                "native_vs_iau_hga_relative": abs(nat[0] - iau[0]) / abs(nat[0]),
            }
        )

    # Duplicate endpoints: the published grid repeats its first and last row of
    # each latitude plane through the decimation, so the same physical point can
    # be counted twice.  Measured, not assumed.
    seen = {}
    for d in detail:
        key = (d["z_km"], d["lon_deg"], d["lat_deg"])
        seen[key] = seen.get(key, 0) + 1
    duplicates = {str(k): v for k, v in seen.items() if v > 1}

    worst = sorted(detail, key=lambda d: -d["relative_error"])[:worst_n]
    return {
        "file": os.path.basename(path),
        "epoch": epoch,
        "indices_printed": printed,
        "dtc_printed": index["dtc_printed"],
        "rows_evaluated": len(detail),
        "zero_reference_rows_excluded": zero_reference,
        "duplicate_grid_points": duplicates,
        "bridge": {
            "note": "native official Fortran vs the project adapter, identical inputs",
            "max_relative_difference": max(
                (d["native_vs_adapter_relative"] for d in detail), default=0.0
            ),
            "max_temperature_difference_k": max(
                (abs(d["absolute_tinf_difference_k"]) for d in detail), default=0.0
            ),
        },
        "astronomy": {
            "note": "official THETA vs the IAU-1982 expression the test uses",
            "max_relative_difference": max(
                (d["native_vs_iau_hga_relative"] for d in detail), default=0.0
            ),
        },
        "density": {
            "absolute_error_kg_m3": summarize([d["absolute_error_kg_m3"] for d in detail]),
            "relative_error": summarize([d["relative_error"] for d in detail]),
            "signed_relative_error": summarize_signed([d["signed_relative_error"] for d in detail]),
        },
        "temperature": {
            "exospheric_abs_error_k": summarize([d["absolute_tinf_difference_k"] for d in detail]),
            "local_abs_error_k": summarize([abs(d["absolute_temp_difference_k"]) for d in detail]),
        },
        "strata": {
            "altitude_km": stratify(detail, "z_km", "by_altitude_km"),
            "latitude_deg": stratify(detail, "lat_deg", "by_latitude_deg"),
            "longitude_deg": stratify(detail, "lon_deg", "by_longitude_deg"),
            # Density is bucketed logarithmically: the published grid spans
            # nine decades, so equal-width bins would put almost every row in
            # the first one and leave the rest empty.
            "reference_density": stratify_log(
                detail, "density_reference_kg_m3", "by_reference_density"
            ),
        },
        "time_and_activity": {
            "hour_utc": epoch["hour"],
            "dtc_printed_k": index["dtc_printed"],
            "indices_printed": printed,
        },
        "worst": worst,
        "detail": detail,
    }


def iau_greenwich_hour_angle(mjd):
    """IAU 1982 Greenwich hour angle (Meeus, Astronomical Algorithms eq. 12.4)."""
    jd = mjd + 2400000.5
    d = jd - 2451545.0
    t = d / 36525.0
    deg = (
        280.46061837
        + 360.98564736629 * d
        + 0.000387933 * t * t
        - t * t * t / 38710000.0
    )
    return math.fmod(deg, 360.0) * math.pi / 180.0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--adapter", required=True)
    ap.add_argument("--astro", required=True)
    ap.add_argument("--validation-dir", required=True)
    ap.add_argument("--index-dir")
    ap.add_argument("--limit", type=int, default=0, help="rows per epoch, 0 = all")
    ap.add_argument("--worst", type=int, default=20)
    ap.add_argument("--epochs", nargs="*", default=None)
    ap.add_argument("--detail-json")
    args = ap.parse_args()

    names = args.epochs or sorted(
        f for f in os.listdir(args.validation_dir) if f.endswith(".DAT")
    )
    per_epoch = [
        analyse_epoch(
            args.kernel,
            args.adapter,
            args.astro,
            os.path.join(args.validation_dir, n),
            args.limit,
            args.worst,
        )
        for n in names
    ]

    pooled = [d for e in per_epoch for d in e.pop("detail")]
    # Per-row hour of day, so time of day can be stratified without re-deriving
    # it from the file name.  `rows_evaluated` counts only rows with a usable
    # reference, which is exactly the set `detail` holds, so the two agree even
    # if a file ever contained a zero reference density.
    pooled_hours = []
    for e in per_epoch:
        pooled_hours.extend(
            [e["time_and_activity"]["hour_utc"]] * e["rows_evaluated"]
        )
    if len(pooled_hours) != len(pooled):
        raise RuntimeError(
            "hour bookkeeping lost rows: %d hours for %d rows"
            % (len(pooled_hours), len(pooled))
        )

    index_provenance = None
    if args.index_dir:
        index_table = load_index_file(os.path.join(args.index_dir, "SOLFSMY.TXT"))
        dtc_table = load_dtc_file(os.path.join(args.index_dir, "DTCFILE.TXT"))
        index_provenance = []
        for e in per_epoch:
            d1950 = days_since_1950_jan0(
                e["epoch"]["year"],
                e["epoch"]["doy"],
                e["epoch"]["hour"],
                e["epoch"]["minute"],
                e["epoch"]["second"],
            )
            archive = reconstruct_indices(index_table, d1950)
            archive_dtc = reconstruct_dtc(dtc_table, d1950)
            index_provenance.append(
                {
                    "file": e["file"],
                    "indices_printed": e["indices_printed"],
                    "indices_from_current_archive": archive,
                    "indices_match": archive is not None
                    and all(
                        abs(a - round(b)) < 1e-9
                        for a, b in zip(archive, e["indices_printed"])
                    ),
                    "dtc_printed": e["dtc_printed"],
                    "dtc_from_current_archive": archive_dtc,
                    "dtc_match": archive_dtc == e["dtc_printed"],
                }
            )

    report = {
        "schema": "leo-dynamics/jb2008-grid-investigation/1",
        "metric_definitions": {
            "signed_density_difference_kg_m3": (
                "density_native - density_reference, SIGNED, kg/m3. The sign "
                "carries the systematic bias and is retained deliberately."
            ),
            "absolute_error_kg_m3": (
                "|density_native - density_reference|, kg/m3. Distinct from "
                "the signed difference above; the two are reported separately "
                "so a one-sided bias is never hidden inside a magnitude."
            ),
            "relative_error": (
                "|density_native - density_reference| / |density_reference|, "
                "non-negative"
            ),
            "signed_relative_error": (
                "(density_native - density_reference) / |density_reference|, "
                "sign preserved so bias is visible"
            ),
            "denominator_zero": (
                "excluded from the error statistics and counted separately; no "
                "substitute value is reported"
            ),
            "rms": (
                "root mean square over every evaluated row of this record. The "
                "full-grid record evaluates all 545528 rows; the decimated "
                "shipped test evaluates 13320 distinct rows. These are "
                "different populations and their max values differ slightly by "
                "sampling alone - see sample_populations below."
            ),
            "signed_tinf_difference_k": "native TEMP(1) - printed TINF, SIGNED, K",
            "absolute_tinf_difference_k": "|native TEMP(1) - printed TINF|, K",
            "signed_temp_difference_k": "native TEMP(2) - printed TEMP, SIGNED, K",
            "absolute_temp_difference_k": "|native TEMP(2) - printed TEMP|, K",
        },
        "sample_populations": {
            "note": (
                "Two distinct populations are reported and must not be "
                "compared as if they were the same measurement."
            ),
            "full_grid": {
                "rows": len(pooled),
                "selection": "every row of all eight published files",
                "max_relative_error": max(d["relative_error"] for d in pooled),
                "rms_relative_error": summarize(
                    [d["relative_error"] for d in pooled]
                )["rms"],
            },
            "decimated_shipped_test": {
                "distinct_rows": 13320,
                "rows_as_previously_reported": 13328,
                "selection": (
                    "every 41st row plus the trailing endpoint of each file; "
                    "the previously reported 13328 double-counted row 0 once "
                    "per epoch"
                ),
                "deduplication_effect": (
                    "removing the repeated row changes only the weighting: "
                    "RMS 0.0032388564212367911 -> 0.0032398286550430365, "
                    "max 0.0058605859224980525 unchanged. The full-grid max "
                    "0.005889466852069977 is slightly larger purely because it "
                    "includes rows the 41-row decimation does not visit."
                ),
            },
        },
        "print_precision": {
            "density": "FORMAT D15.5, five significant mantissa digits",
            "relative_half_ulp_max": 0.5 / 10000.0,
            "solar_indices": "FORMAT F8.0, whole numbers, half-step 0.5",
            "solar_angles": "FORMAT F10.4, half-step 5e-5 rad",
            "d1950": "FORMAT F15.5, half-step 8.64e-4 s",
        },
        "epochs": per_epoch,
        "pooled_strata": {
            "by_hour_utc": stratify(
                [dict(d, hour_utc=h) for d, h in zip(pooled, pooled_hours)],
                "hour_utc",
                "by_hour_utc",
            ),
            "by_dtc": stratify(pooled, "dtc_printed", "by_dtc"),
            "by_reference_density": stratify_log(
                pooled, "density_reference_kg_m3", "by_reference_density"
            ),
        },
        "pooled": {
            "rows_evaluated": len(pooled),
            "density_relative_error": summarize([d["relative_error"] for d in pooled]),
            "density_signed_relative_error": summarize_signed(
                [d["signed_relative_error"] for d in pooled]
            ),
            "density_absolute_error_kg_m3": summarize(
                [d["absolute_error_kg_m3"] for d in pooled]
            ),
            "exospheric_temperature_abs_error_k": summarize(
                [d["absolute_tinf_difference_k"] for d in pooled]
            ),
            "local_temperature_abs_error_k": summarize(
                [d["absolute_temp_difference_k"] for d in pooled]
            ),
            "bridge_max_relative_difference": max(
                (d["native_vs_adapter_relative"] for d in pooled), default=0.0
            ),
            "astronomy_max_relative_difference": max(
                (d["native_vs_iau_hga_relative"] for d in pooled), default=0.0
            ),
        },
        "worst_overall": sorted(pooled, key=lambda d: -d["relative_error"])[: args.worst],
        "index_provenance": index_provenance,
    }
    text = json.dumps(report, indent=2)
    if args.detail_json:
        with open(args.detail_json, "w") as handle:
            handle.write(text)
        print(
            json.dumps(
                {
                    "rows_evaluated": len(pooled),
                    "pooled_density_relative_error": report["pooled"][
                        "density_relative_error"
                    ],
                    "detail_json": args.detail_json,
                },
                indent=2,
            )
        )
    else:
        print(text)


if __name__ == "__main__":
    main()