#!/usr/bin/env python3
"""Standalone NRLMSISE-00 geomagnetic-mode experiment: daily Ap versus Ap history.

Three independent evaluations of one and the same physical state are compared:

  native_source   the pinned Brodowski C reference compiled here from source,
                  driven through nrlmsise-00.h with an explicit seven-element
                  ap_array and switches[9] = -1;
  native_archive  the same driver linked against the prebuilt reference archive
                  the library links, so any difference is a build difference and
                  not a driver difference;
  adapter         leo::atmosphere::Nrlmsise00 from this repository.

Every driver receives byte-identical inputs, printed in the JSON output so the
comparison can be reproduced by hand. Nothing is downloaded, cached or
interpolated: each program calls gtd7d directly.

The daily-versus-history density and temperature difference this reports is a
change of the modelled geomagnetic driver, not an integration or unit error; the
unit claim is checked separately by requiring the native cgs and metric switch
selections to reproduce the adapter's SI values exactly.

Usage:
  scripts/nrlmsise00_ap_modes.py --out docs/atmosphere/evidence/validation-amendment-v1/ap-modes.json
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

NATIVE_C = r"""
#include <stdio.h>
#include <stdlib.h>
#include "nrlmsise-00.h"

int main(int argc, char **argv) {
    /* argc: doy sec alt lat lon lst f107a f107 ap mode switch0 h0..h6 */
    struct ap_array aph;
    struct nrlmsise_input in;
    struct nrlmsise_flags flags;
    struct nrlmsise_output out;
    int i, mode = atoi(argv[10]);
    in.year = 2000;
    in.doy = atoi(argv[1]);
    in.sec = atof(argv[2]);
    in.alt = atof(argv[3]);
    in.g_lat = atof(argv[4]);
    in.g_long = atof(argv[5]);
    in.lst = atof(argv[6]);
    in.f107A = atof(argv[7]);
    in.f107 = atof(argv[8]);
    in.ap = atof(argv[9]);
    for (i = 0; i < 7; i++) aph.a[i] = atof(argv[12 + i]);
    in.ap_a = (mode == -1) ? &aph : (struct ap_array *)0;
    flags.switches[0] = atoi(argv[11]);
    for (i = 1; i < 24; i++) flags.switches[i] = 1;
    flags.switches[9] = mode;
    gtd7d(&in, &flags, &out);
    printf("{\"density\":%.17g,\"temperature\":%.17g,\"tinf\":%.17g,"
           "\"n\":[%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g]}",
           out.d[5], out.t[1], out.t[0],
           out.d[0], out.d[1], out.d[2], out.d[3], out.d[4], out.d[6], out.d[7], out.d[8]);
    (void)argc;
    return 0;
}
"""

ADAPTER_CXX = r"""
#include "leo/atmosphere/nrlmsise00.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
using leo::atmosphere::Nrlmsise00;
using leo::atmosphere::NrlmsiseInput;
using leo::atmosphere::Result;

int main(int argc, char **argv) {
    /* argv: doy sec alt lat lon lst f107a f107 ap mode switch0 h0..h6 */
    auto switches = Nrlmsise00::standard_switches();
    switches[0] = std::atoi(argv[11]);
    switches[9] = std::atoi(argv[10]);
    NrlmsiseInput in{};
    in.year = 2000;
    in.day_of_year = std::atoi(argv[1]);
    in.utc_seconds = std::atof(argv[2]);
    in.altitude_km = std::atof(argv[3]);
    in.latitude_deg = std::atof(argv[4]);
    in.longitude_deg = std::atof(argv[5]);
    in.local_solar_time_hours = std::atof(argv[6]);
    in.f107_average = std::atof(argv[7]);
    in.f107 = std::atof(argv[8]);
    in.ap = std::atof(argv[9]);
    for (int i = 0; i < 7; i++) in.ap_history[static_cast<std::size_t>(i)] = std::atof(argv[12 + i]);
    const Result r = Nrlmsise00{switches}.evaluate(in);
    printf("{\"density\":%.17g,\"temperature\":%.17g,\"tinf\":%.17g,"
           "\"n\":[%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g]}",
           r.density_kg_m3, r.temperature_k, r.exospheric_temperature_k,
           r.number_density_m3[0], r.number_density_m3[1], r.number_density_m3[2],
           r.number_density_m3[3], r.number_density_m3[4], r.number_density_m3[5],
           r.number_density_m3[6], r.number_density_m3[7]);
    (void)argc;
    return 0;
}
"""

# The physical state. Only the geomagnetic driver and the switch that selects it
# change between the two modes; solar drivers, position and time are identical,
# and the daily Ap scalar is the same number as history slot 0 (the documented
# daily-Ap slot), so the two modes describe the same case rather than two.
BASE = {"doy": 172, "sec": 29000.0, "alt_km": 400.0, "lat_deg": 60.0,
        "lon_deg": -70.0, "lst_hours": 16.0, "f107_average": 150.0, "f107": 150.0,
        "daily_ap": 23.0}
# Seven distinct Ap values in the slot order of nrlmsise-00.h: daily, current
# 3 h bin, -3 h, -6 h, -9 h, mean of 12..33 h, mean of 36..57 h.
HISTORY = [23.0, 118.0, 7.0, 156.0, 44.0, 91.0, 12.0]
ALTITUDES_KM = [100.0, 200.0, 400.0, 600.0, 1000.0]


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def cache_value(cache, key):
    for line in Path(cache).read_text().splitlines():
        if line.startswith(key + ":"):
            return line.split("=", 1)[1]
    raise SystemExit(f"{key} not found in {cache}")


def run(command):
    done = subprocess.run(command, capture_output=True, text=True)
    if done.returncode != 0:
        raise SystemExit("command failed: " + " ".join(command) + "\n" + done.stderr)
    return done


def relative(a, b):
    if a == b:
        return 0.0
    scale = max(abs(a), abs(b))
    return abs(a - b) / scale if scale > 0.0 else 0.0


def evaluate(program, mode, switch0, altitude_km, history):
    args = [program, str(BASE["doy"]), repr(BASE["sec"]), repr(altitude_km),
            repr(BASE["lat_deg"]), repr(BASE["lon_deg"]), repr(BASE["lst_hours"]),
            repr(BASE["f107_average"]), repr(BASE["f107"]), repr(BASE["daily_ap"]),
            str(mode), str(switch0)] + [repr(v) for v in history]
    return json.loads(run(args).stdout)


def main():
    if BASE["daily_ap"] != HISTORY[0]:
        raise SystemExit("the two modes must describe the same case: the daily Ap scalar "
                         "has to equal history slot 0")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nrlmsise-root", default=os.environ.get(
        "LEO_NRLMSISE00_ROOT", "/tmp/leo-atmosphere-deps"),
        help="pinned Brodowski C reference source directory")
    parser.add_argument("--build-dir", default=str(ROOT / "build-atmosphere-gcc"),
                        help="existing build tree providing libleo_atmosphere.a")
    parser.add_argument("--out", help="write the evidence JSON here")
    options = parser.parse_args()

    source = Path(options.nrlmsise_root).resolve()
    build = Path(options.build_dir).resolve()
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        raise SystemExit(f"{cache} not found: pass --build-dir for an existing build tree")
    eigen = (cache_value(cache, "LEO_EIGEN3_INCLUDE_DIRS")
             if "LEO_EIGEN3_INCLUDE_DIRS" in cache.read_text()
             else cache_value(cache, "EIGEN3_INCLUDE_DIR"))
    cxx_flags = [f for f in cache_value(cache, "CMAKE_CXX_FLAGS_RELEASE").split()
                 if f != "-DNDEBUG"]
    c_flags = [f for f in cache_value(cache, "CMAKE_C_FLAGS_RELEASE").split()
               if f != "-DNDEBUG"]
    reference_archive = build / "libleo_nrlmsise00_reference.a"
    atmosphere_archive = build / "libleo_atmosphere.a"

    for required in (source / "nrlmsise-00.c", source / "nrlmsise-00_data.c",
                     source / "nrlmsise-00.h", reference_archive, atmosphere_archive):
        if not required.is_file():
            raise SystemExit(f"missing input {required}")

    with tempfile.TemporaryDirectory(prefix="nrlmsise-ap-modes-") as work:
        work = Path(work)
        (work / "native.c").write_text(NATIVE_C)
        (work / "adapter.cpp").write_text(ADAPTER_CXX)
        gcc, gxx = shutil.which("gcc") or "cc", shutil.which("g++") or "c++"
        native_source = work / "native_source"
        native_archive = work / "native_archive"
        adapter = work / "adapter"
        commands = {
            "native_source": [gcc, "-std=c99", *c_flags, f"-I{source}", "-o", str(native_source),
                              str(work / "native.c"), str(source / "nrlmsise-00.c"),
                              str(source / "nrlmsise-00_data.c"), "-lm"],
            "native_archive": [gcc, "-std=c99", *c_flags, f"-I{source}", "-o", str(native_archive),
                               str(work / "native.c"), str(reference_archive), "-lm"],
            "adapter": [gxx, "-std=c++20", *cxx_flags, f"-I{ROOT / 'include'}", f"-I{eigen}",
                        "-o", str(adapter), str(work / "adapter.cpp"),
                        str(atmosphere_archive), str(reference_archive), "-lpthread"],
        }
        for name, command in commands.items():
            run(command)

        cases = []
        worst = {"native_cgs_vs_archive_cgs_density": 0.0,
                 "native_si_vs_archive_si_density": 0.0,
                 "native_si_vs_archive_si_temperature": 0.0,
                 "native_si_vs_archive_si_constituent": 0.0,
                 "native_si_vs_adapter_density": 0.0,
                 "native_si_vs_adapter_temperature": 0.0,
                 "native_si_vs_adapter_constituent": 0.0,
                 "cgs_scaled_to_si_vs_adapter_density": 0.0,
                 "cgs_vs_metric_adapter_density": 0.0}
        for altitude in ALTITUDES_KM:
            for mode in (1, -1):
                history = HISTORY if mode == -1 else [0.0] * 7
                native_cgs = evaluate(str(native_source), mode, 0, altitude, history)
                archive_cgs = evaluate(str(native_archive), mode, 0, altitude, history)
                # switch 0 = 1 asks the reference itself for SI output, so the
                # adapter is compared against like-for-like numbers rather than
                # against a conversion this script would have to invent.
                native_si = evaluate(str(native_source), mode, 1, altitude, history)
                archive_si = evaluate(str(native_archive), mode, 1, altitude, history)
                adapter_si = evaluate(str(adapter), mode, 1, altitude, history)
                adapter_cgs = evaluate(str(adapter), mode, 0, altitude, history)
                scaled = {"density": native_cgs["density"] * 1e3,
                          "n": [value * 1e6 for value in native_cgs["n"]]}
                worst["native_cgs_vs_archive_cgs_density"] = max(
                    worst["native_cgs_vs_archive_cgs_density"],
                    relative(native_cgs["density"], archive_cgs["density"]))
                for key, other in (("native_si_vs_archive_si_density", "density"),
                                   ("native_si_vs_archive_si_temperature", "temperature"),
                                   ("native_si_vs_adapter_density", "density"),
                                   ("native_si_vs_adapter_temperature", "temperature")):
                    left = native_si[other]
                    right = (archive_si if "archive" in key else adapter_si)[other]
                    worst[key] = max(worst[key], relative(left, right))
                worst["native_si_vs_archive_si_constituent"] = max(
                    worst["native_si_vs_archive_si_constituent"],
                    max(relative(a, b) for a, b in zip(native_si["n"], archive_si["n"])))
                worst["native_si_vs_adapter_constituent"] = max(
                    worst["native_si_vs_adapter_constituent"],
                    max(relative(a, b) for a, b in zip(native_si["n"], adapter_si["n"])))
                worst["cgs_scaled_to_si_vs_adapter_density"] = max(
                    worst["cgs_scaled_to_si_vs_adapter_density"],
                    relative(scaled["density"], adapter_cgs["density"]))
                worst["cgs_vs_metric_adapter_density"] = max(
                    worst["cgs_vs_metric_adapter_density"],
                    relative(adapter_cgs["density"], adapter_si["density"]))
                cases.append({
                    "altitude_km": altitude,
                    "switches_9": mode,
                    "mode": "ap_history" if mode == -1 else "daily_ap",
                    "ap_history_slots": history,
                    "native_source_cgs": native_cgs,
                    "native_archive_cgs": archive_cgs,
                    "native_source_si": native_si,
                    "native_archive_si": archive_si,
                    "adapter_si": adapter_si,
                    "adapter_from_cgs_switch0_0": adapter_cgs,
                })
        by_key = {(c["altitude_km"], c["switches_9"]): c for c in cases}
        differences = []
        for altitude in ALTITUDES_KM:
            daily = by_key[(altitude, 1)]["adapter_si"]
            history_case = by_key[(altitude, -1)]["adapter_si"]
            differences.append({
                "altitude_km": altitude,
                "daily_density_kg_m3": daily["density"],
                "history_density_kg_m3": history_case["density"],
                # Signed mode change, daily Ap mode as the denominator:
                # (history - daily) / daily. A negative value means the
                # three-hourly history gives a lower density than the daily index.
                "density_signed_change_vs_daily": (history_case["density"] - daily["density"]) / daily["density"],
                "daily_temperature_k": daily["temperature"],
                "history_temperature_k": history_case["temperature"],
                "temperature_signed_change_vs_daily":
                    (history_case["temperature"] - daily["temperature"]) / daily["temperature"],
                "daily_tinf_k": daily["tinf"],
                "history_tinf_k": history_case["tinf"],
                "tinf_signed_change_vs_daily":
                    (history_case["tinf"] - daily["tinf"]) / daily["tinf"],
                # Symmetric, unsigned diagnostic of the same pair; it says how
                # large the mode change is, not whether it raises the density.
                "density_relative_difference": relative(history_case["density"], daily["density"]),
            })

    evidence = {
        "experiment": "nrlmsise00-ap-modes",
        "question": "does switches[9] = -1 feed the seven documented Ap slots to the "
                    "reference, and what does the mode change physically?",
        "state": BASE,
        "daily_ap_equals_history_slot_0": BASE["daily_ap"] == HISTORY[0],
        "difference_definitions": {
            "density_signed_change_vs_daily": "(history - daily) / daily; daily Ap mode is the denominator",
            "density_relative_difference": "|history - daily| / max(|history|, |daily|); unsigned magnitude only",
        },
        "history_slot_meaning": ["daily Ap", "3 h bin at the evaluation time",
                                 "3 h bin 3 h earlier", "3 h bin 6 h earlier",
                                 "3 h bin 9 h earlier", "mean of the eight bins 12..33 h earlier",
                                 "mean of the eight bins 36..57 h earlier"],
        "history_slot_source": "nrlmsise-00.h lines 69-82",
        "toolchain": {
            "gcc": run([gcc, "--version"]).stdout.splitlines()[0],
            "gpp": run([gxx, "--version"]).stdout.splitlines()[0],
            "c_flags": c_flags,
            "cxx_flags": cxx_flags,
            "platform": platform.platform(),
        },
        "inputs": {
            "nrlmsise-00.c": sha256(source / "nrlmsise-00.c"),
            "nrlmsise-00_data.c": sha256(source / "nrlmsise-00_data.c"),
            "nrlmsise-00.h": sha256(source / "nrlmsise-00.h"),
            "libleo_nrlmsise00_reference.a": sha256(reference_archive),
            "libleo_atmosphere.a": sha256(atmosphere_archive),
        },
        "commands": {name: " ".join(command) for name, command in commands.items()},
        "worst_relative_differences": worst,
        "mode_differences": differences,
        "cases": cases,
    }
    text = json.dumps(evidence, indent=2, sort_keys=False) + "\n"
    if options.out:
        out = Path(options.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text)
    else:
        sys.stdout.write(text)
    print(json.dumps(worst, indent=2))
    for difference in differences:
        print(f"{difference['altitude_km']:7.0f} km  daily {difference['daily_density_kg_m3']:.6e} "
              f"kg/m^3  history {difference['history_density_kg_m3']:.6e} kg/m^3  "
              f"signed change vs daily {difference['density_signed_change_vs_daily']:+.2%}  "
              f"T {difference['temperature_signed_change_vs_daily']:+.2%}")
    if max(worst.values()) > 1e-12:
        raise SystemExit("evaluations disagree beyond 1e-12 relative")
    if min(d["density_relative_difference"] for d in differences) == 0.0:
        raise SystemExit("the two modes produced identical densities; the experiment proves nothing")


if __name__ == "__main__":
    main()