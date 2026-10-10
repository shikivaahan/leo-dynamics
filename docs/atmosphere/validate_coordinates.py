#!/usr/bin/env python3
"""Independent ERFA/SOFA + final IERS-B check of actual CSPICE observation output."""
import argparse
import hashlib
from importlib import metadata
import json
from pathlib import Path
import subprocess
import tempfile

import astropy.units as u
from astropy.time import Time
from astropy.utils import iers
import erfa
import numpy as np


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "lsk", "spk", "pck", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--epoch", action="append", required=True)
    args = parser.parse_args()
    iers.conf.auto_download = False
    final_eop = iers.IERS_B.open(iers.IERS_B_FILE)
    rows = []
    commands = []
    with iers.earth_orientation_table.set(final_eop), tempfile.TemporaryDirectory() as temporary:
        for index, epoch in enumerate(args.epoch):
            command = [str(args.binary.resolve()), "--mode", "coordinates", "--epoch-utc", epoch,
                       "--lsk", str(args.lsk.resolve()), "--spk", str(args.spk.resolve()),
                       "--pck", str(args.pck.resolve()), "--frame", "ITRF93",
                       "--out", str(Path(temporary) / str(index))]
            process = subprocess.run(command, capture_output=True, text=True, check=True)
            actual = json.loads(process.stdout)
            t = Time(epoch, scale="utc")
            dut1 = final_eop.ut1_utc(t).to_value(u.s)
            xp, yp = (q.to_value(u.rad) for q in final_eop.pm_xy(t))
            reference = erfa.c2t06a(t.tt.jd1, t.tt.jd2, t.ut1.jd1, t.ut1.jd2, xp, yp)
            matrix = np.asarray(actual["earth_fixed_rotation"])
            # Frobenius norm avoids arccos losing all digits for tiny rotations.
            angular_difference = float(np.linalg.norm(matrix - reference) / np.sqrt(2))
            et_reference = float(((t.tdb.jd1 - 2451545.0) + t.tdb.jd2) * 86400)
            components = t.utc.ymdhms
            expected_sod = float(3600 * components.hour + 60 * components.minute + components.second)
            day_of_year = int(t.utc.yday.split(":")[1])
            row = {"epoch_utc": epoch, "dut1_seconds": float(dut1),
                   "polar_motion_rad": [float(xp), float(yp)],
                   "rotation_difference_rad": angular_difference,
                   "rotation_position_bound_at_7000km_m": angular_difference * 7e6,
                   "et_difference_seconds": actual["epoch_et_seconds"] - et_reference,
                   "utc_seconds_of_day_error": actual["utc_seconds_of_day"] - expected_sod,
                   "utc_doy_error": actual["utc_day_of_year"] - day_of_year,
                   "quasi_mjd_error_days": actual["utc_quasi_mjd"] - float(t.utc.mjd),
                   "spice": actual, "sofa_itrf_rotation": reference.tolist()}
            # Distinct precession/nutation realizations and EOP releases have a
            # finite accuracy floor; this is a convention check, not bit equality.
            row["passed"] = (angular_difference < 1e-6 and abs(row["et_difference_seconds"]) < 5e-5
                             and abs(row["utc_seconds_of_day_error"]) < 2e-6
                             and row["utc_doy_error"] == 0 and abs(row["quasi_mjd_error_days"]) < 2e-10)
            rows.append(row)
            commands.append(command)
    report = {"schema_version": 1, "oracle": "ERFA c2t06a / IAU2006-2000A + final IERS-B; Astropy UTC/TT/TDB",
              "limitations": "SOFA celestial axes and SPICE J2000 orientation realizations and historical EOP products differ; threshold checks frame/time convention, not exact equal realizations",
              "tolerances": {"rotation_rad": 1e-6, "et_seconds": 5e-5, "utc_sod_seconds": 2e-6, "quasi_mjd_days": 2e-10},
              "dependencies": {name: metadata.version(name) for name in
                               ("astropy", "pyerfa", "astropy-iers-data", "numpy", "packaging", "pyyaml")},
              "iers_b": {"path": str(iers.IERS_B_FILE), "sha256": digest(iers.IERS_B_FILE)},
              "assets": {name: {"path": str(getattr(args, name)), "sha256": digest(getattr(args, name))}
                         for name in ("binary", "lsk", "spk", "pck")},
              "commands": commands, "rows": rows, "passed": all(row["passed"] for row in rows)}
    with args.output.open("x") as handle:
        json.dump(report, handle, indent=2, allow_nan=False)
        handle.write("\n")
    print(json.dumps({"passed": report["passed"], "rotation_max_rad": max(row["rotation_difference_rad"] for row in rows),
                      "et_max_seconds": max(abs(row["et_difference_seconds"]) for row in rows)}))
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
