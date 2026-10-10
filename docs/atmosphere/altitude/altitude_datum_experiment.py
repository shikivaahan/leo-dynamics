#!/usr/bin/env python3
"""JB2008 altitude-datum investigation.

Question
--------
The upstream `JB2008.f` takes `SAT(2) = geocentric latitude (rad)` and
`SAT(3) = height of position (km)` and does no geodetic conversion of its own:
it integrates a spherical barometric/diffusion profile in the scalar `SAT(3)`
and applies gravity as `g = g0 / (1 + z / 6356.766 km)**2`. The datum the
caller must use for `SAT(3)` is therefore part of the model's input contract
and not something the code reveals.

What this experiment does
-------------------------
1.  Hashes and compiles the unmodified upstream `JB2008.f` with a project-owned
    probe driver, so every number below comes from the official Fortran.
2.  Reproduces the vendor's own published validation grid
    (`JB2008_AUTO_OUT_*.DAT` from `jb2008validate.zip`) with that same binary.
    This establishes the harness before any datum question is opened.
3.  Builds one physical point per (latitude, altitude) from an independent
    WGS84 forward conversion (Bowring, implemented here, not imported from the
    repository) and asks the reference for the density/temperature under each
    candidate Earth-centred definition of `SAT(2)`/`SAT(3)`.
4.  Quantifies the spread between candidates, at the equator, at 45 deg, and
    near the pole, over LEO altitudes.

No upstream code is redistributed: the upstream `JB2008.f` and the `THETA`
block of `JB08DRVAUTO.f` are compiled from the acquired files in place or are
copied verbatim into the build directory at run time.

Usage
-----
    python3 docs/atmosphere/altitude/altitude_datum_experiment.py \
        --jb2008 /tmp/leo-atmosphere-deps/JB2008.f \
        --vendor-driver "/path/to/JB08DRVAUTO.f" \
        --official-grid /tmp/leo-atmosphere-deps/jb-validation/JB2008_AUTO_OUT_2023_091_00.DAT \
        --fortran /tmp/leo-fortran/usr/bin/gfortran-13 \
        --out /tmp/jb2008-altitude-datum
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

# WGS84 defining and derived radii, km. 6356.766 km is the geopotential
# reference radius that the upstream XGRAV function hard-codes; the others are
# the candidate spatial datums.
WGS84_A_KM = 6378.137
WGS84_A_M = 6378137.0
WGS84_F = 1.0 / 298.257223563
WGS84_B_KM = WGS84_A_KM * (1.0 - WGS84_F)
JB2008_GRAVITY_RADIUS_KM = 6356.766
IUGG_MEAN_RADIUS_KM = 6371.0

LATITUDES_DEG = (0.0, 45.0, -45.0, 89.9, -89.9, 89.9999999)
ALTITUDES_KM = (120.0, 200.0, 400.0, 600.0, 1000.0, 1500.0)
LONGITUDES_DEG = (0.0, 90.0, 180.0, 270.0, 359.9999999)

# The JB2008 adapter's accepted domain, in kilometres.
BACKEND_MIN_ALT_KM = 120.0
BACKEND_MAX_ALT_KM = 1500.0

# Sources cited by this experiment. Both a stable locator and the content hash
# are recorded, so a moved URL cannot silently change what was read.
SOURCE_CITATIONS = [
    {
        "role": "the model source whose SAT(2)/SAT(3) contract is under investigation",
        "title": "JB2008.f (Jacchia-Bowman 2008 Fortran reference)",
        "locator": "https://sol.spacenvironment.net/JB2008/downloadables/jb2008.zip",
        "local_path": "/tmp/leo-atmosphere-deps/JB2008.f",
        "sha256": "b18c1607239db881940a7b91635a7af39edc82306cf10854ec12080397227999",
        "quotes": [
            "line 23: SAT(2) : Geocentric Latitude of Position (radians)",
            "line 24: SAT(3) : Height of Position (km)",
            "line 595: XGRAV = 9.80665D0/(1.D0 + Z/6356.766D0)**2",
        ],
    },
    {
        "role": "the vendor driver, showing what is actually fed to SAT(3)",
        "title": "JB08DRVAUTO.f, inside the same jb2008.zip",
        "locator": "https://sol.spacenvironment.net/JB2008/downloadables/jb2008.zip",
        "local_path": "/tmp/jbzip-jb2008/JB2008 Code/JB08DRVAUTO.f",
        "sha256": "061516b02b8af490b49f5745bddc44f53fd9b0ab738565753f9230e8ea00467f",
        "quotes": [
            "lines 227-232: a scalar Z grid from 120 to 1500 km is assigned "
            "straight to SAT(3); no position vector exists in the distribution",
        ],
    },
    {
        "role": "the official validation package, and the proof that it cannot "
                "discriminate an altitude datum",
        "title": "JB2008 official validation archive",
        "locator": "https://sol.spacenvironment.net/JB2008/downloadables/jb2008validate.zip",
        "local_path": "/tmp/leo-atmosphere-deps/jb2008validate.zip",
        "sha256": "4d18c286b148fc5b2271667dab71dd6a702204bb5176bc8a54200aa17e3fff98",
        "quotes": [
            "JB2008_AUTO_OUT_2023_091_00.DAT, sha256 "
            "de0f03197dd798ce5d4c9ef27124c50cc63d8e3e941fe998cebff4503897f943",
            "68191 rows of Z LON LAT TINF TEMP RHO: a scalar-height cube",
        ],
    },
    {
        "role": "the model publication; checked for a datum statement and found silent",
        "title": "Bowman, Tobiska, Marcos, Huang, Lin, Burke, AIAA 2008-6438",
        "locator": "https://sol.spacenvironment.net/JB2008/pubs/AIAA_2008-6438_JB2008_Model.pdf",
        "local_path": "/tmp/alt-src/AIAA_2008-6438_JB2008_Model.pdf",
        "sha256": "bcab52e60259046587acbd5f845b386a2ec319fa40cbf37254846cd3f84b5bf0",
        "notes": "US Government work; states the 175-1000 km validation band, "
                 "states no positional datum. F(z) is written with z = height/1000.",
    },
    {
        "role": "the vendor's index report; checked for a datum statement and found silent",
        "title": "Tobiska, Bowman, Bouwer, SET TR 2008-002",
        "locator": "https://spacewx.com/wp-content/uploads/2020/11/SET_TR2008_002.pdf",
        "local_path": "/tmp/alt-src/SET_TR2008_002.pdf",
        "sha256": "ee787a4990dff5a8a0032ed8443306ca84c4c7c75a3670195b9ebc0b16c6a572",
        "notes": "covers the solar and geomagnetic indices only.",
    },
    {
        "role": "the vendor's file-format document; contains no positional input at all",
        "title": "JB2008 file format descriptions",
        "locator": "https://sol.spacenvironment.net/JB2008/downloadables/jb2008fileformats.pdf",
        "local_path": "/tmp/leo-atmosphere-deps/jb2008/jb2008fileformats.pdf",
        "sha256": "98bd9d1583aa79167dea9b06cb5afd1abbfe1bb17c3a62e45dafc9b075cb8c4f",
    },
    {
        "role": "the sibling backend's explicit convention, for contrast",
        "title": "nrlmsise-00.h, NRLMSISE-00 reference",
        "local_path": "/tmp/leo-atmosphere-deps/nrlmsise-00.h",
        "sha256": "d5b45be690bbc6cf394f69b18c0e641a1769da0a87015704fdabf0e7560794aa",
        "quotes": [
            "double alt;    /* altitude in kilometers */",
            "double g_lat;  /* geodetic latitude */",
        ],
    },
    {
        "role": "third-party implementation, cited for context, not an oracle",
        "title": "SatelliteToolboxAtmosphericModels.jl, src/jb2008/jb2008.jl",
        "locator": "https://github.com/JuliaSpace/SatelliteToolboxAtmosphericModels.jl",
        "local_path": "/tmp/alt-src/julia_jb2008.jl",
        "sha256": "f7c20c41ce5edbf824ec8179dbdee1f099f4999e48a747e2835a774c163c5287",
        "quotes": ["documents its latitude argument as geodetic"],
    },
    {
        "role": "third-party implementation, cited for context, not an oracle",
        "title": "Orekit, AbstractJacchiaBowmanModel.java",
        "locator": "https://github.com/CS-SI/Orekit",
        "local_path": "/tmp/alt-src/AbstractJacchiaBowmanModel.java",
        "sha256": "b162285a4f3902c0908e47f9216151d2086605b8c69ebd075dfde8381cc7d799",
        "quotes": ["passes a WGS84 OneAxisEllipsoid geodetic altitude to a slot the "
                   "upstream Javadoc calls geocentric latitude"],
    },
]


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# --------------------------------------------------------------------------
# Independent WGS84 forward conversion (Bowring 1976 / Heikkinen closed form).
# Deliberately not the repository's iterative converter: the experiment must
# not measure the repository against itself.
# --------------------------------------------------------------------------
def ecef_from_geodetic(lat_deg: float, lon_deg: float, h_km: float) -> tuple[float, float, float]:
    a = WGS84_A_KM
    e2 = WGS84_F * (2.0 - WGS84_F)
    lat = math.radians(lat_deg)
    lon = math.radians(lon_deg)
    sin_lat = math.sin(lat)
    n = a / math.sqrt(1.0 - e2 * sin_lat * sin_lat)
    return (
        (n + h_km) * math.cos(lat) * math.cos(lon),
        (n + h_km) * math.cos(lat) * math.sin(lon),
        (n * (1.0 - e2) + h_km) * sin_lat,
    )


def geocentric_latitude_rad(r: tuple[float, float, float]) -> float:
    return math.atan2(r[2], math.hypot(r[0], r[1]))


def geocentric_radius_km(r: tuple[float, float, float]) -> float:
    return math.sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2])


# The candidate Earth-centred definitions of the two JB2008 scalar inputs.
# Each entry maps a candidate name to (lat_mode, alt_mode) where the modes say
# which quantity of the same physical point is handed to the reference.
CANDIDATES = {
    # Geodetic height above the WGS84 ellipsoid, geocentric latitude.
    # This is the convention the repository already implements.
    "geodetic_height_geocentric_latitude": ("geocentric", "geodetic"),
    # Spherical height above the 6356.766 km sphere that JB2008 itself uses for
    # the gravity term, geocentric latitude.
    "geocentric_minus_6356.766_geocentric_latitude": ("geocentric", "r-6356.766"),
    "geocentric_minus_6378.137_geocentric_latitude": ("geocentric", "r-6378.137"),
    "geocentric_minus_6356.752_geocentric_latitude": ("geocentric", "r-6356.752"),
    "geocentric_minus_6371.0_geocentric_latitude": ("geocentric", "r-6371.0"),
    # Latitude convention held to geodetic while the altitude stays geodetic:
    # isolates the effect of the latitude slot alone.
    "geodetic_height_geodetic_latitude": ("geodetic", "geodetic"),
}


def candidate_inputs(r, lat_deg: float, h_km: float, candidate: str):
    lat_mode, alt_mode = CANDIDATES[candidate]
    if lat_mode == "geocentric":
        lat = geocentric_latitude_rad(r)
    else:
        lat = math.radians(lat_deg)
    if alt_mode == "geodetic":
        alt = h_km
    elif alt_mode.startswith("r-"):
        alt = geocentric_radius_km(r) - float(alt_mode[2:])
    else:
        raise ValueError(alt_mode)
    return lat, alt


# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------
def extract_theta(vendor_driver: str, destination: str) -> str:
    """Copy the upstream THETA function block verbatim into the build dir."""
    with open(vendor_driver, "r", encoding="latin-1") as handle:
        lines = handle.readlines()
    start = next(i for i, l in enumerate(lines) if re.search(r"FUNCTION\s+THETA\s*\(", l, re.I))
    end = next(i for i, l in enumerate(lines[start:], start) if re.search(r"END\s+FUNCTION\s+THETA", l, re.I))
    block = lines[start:end + 1]
    with open(destination, "w", encoding="latin-1") as handle:
        handle.writelines(block)
    return hashlib.sha256("".join(block).encode("latin-1")).hexdigest()


def build(fortran: str, jb2008: str, probe_source: str, grid_source: str,
          vendor_driver: str, workdir: str) -> tuple[str, str, str, dict]:
    objects = []
    jb_object = os.path.join(workdir, "JB2008.o")
    subprocess.run([fortran, "-std=legacy", "-O2", "-c", jb2008, "-o", jb_object], check=True)
    objects.append(jb_object)

    probe_object = os.path.join(workdir, "probe.o")
    subprocess.run([fortran, "-O2", "-c", probe_source, "-o", probe_object], check=True)
    objects.append(probe_object)
    probe = os.path.join(workdir, "jb2008_altitude_probe")
    subprocess.run([fortran, "-o", probe, *objects], check=True)

    # The upstream block is fixed-form Fortran 77; the extension keeps
    # gfortran in fixed form so the copy compiles byte-for-byte as upstream.
    theta_source = os.path.join(workdir, "upstream_theta.f")
    theta_sha = extract_theta(vendor_driver, theta_source)
    theta_object = os.path.join(workdir, "theta.o")
    subprocess.run([fortran, "-O2", "-c", theta_source, "-o", theta_object], check=True)
    grid_object = os.path.join(workdir, "grid.o")
    subprocess.run([fortran, "-O2", "-c", grid_source, "-o", grid_object], check=True)
    grid = os.path.join(workdir, "jb2008_official_grid_check")
    subprocess.run([fortran, "-o", grid, jb_object, theta_object, grid_object], check=True)
    theta_probe_source = os.path.join(os.path.dirname(probe_source), "jb2008_theta_probe.f90")
    theta_probe = os.path.join(workdir, "jb2008_theta_probe")
    subprocess.run([fortran, "-O2", theta_probe_source, theta_object, "-o", theta_probe], check=True)

    provenance = {
        "fortran_compiler": fortran,
        "fortran_version": subprocess.run([fortran, "--version"], capture_output=True,
                                          text=True, check=True).stdout.splitlines()[0],
        "jb2008_source": os.path.abspath(jb2008),
        "jb2008_source_sha256": sha256(jb2008),
        "vendor_driver": os.path.abspath(vendor_driver),
        "vendor_driver_sha256": sha256(vendor_driver),
        "upstream_theta_block_sha256": theta_sha,
        "probe_source_sha256": sha256(probe_source),
        "grid_source_sha256": sha256(grid_source),
        "theta_probe_source_sha256": sha256(theta_probe_source),
        "fortran_flags": ["-O2", "-std=legacy (fixed-form model)"],
    }
    return probe, grid, theta_probe, provenance


def run_probe(probe: str, requests: list[tuple]) -> list[tuple[float, float, float]]:
    payload = "\n".join(" ".join(repr(float(v)) for v in row) for row in requests) + "\n"
    proc = subprocess.run([probe], input=payload, capture_output=True, text=True, check=True)
    rows = [line.split() for line in proc.stdout.strip().splitlines()]
    if len(rows) != len(requests):
        raise RuntimeError(f"probe returned {len(rows)} rows for {len(requests)} requests")
    return [tuple(float(v) for v in row) for row in rows]


# --------------------------------------------------------------------------
# Official grid reproduction
# --------------------------------------------------------------------------
HEADER_RE = re.compile(r"^\s*(-?\d+\.\d+)\s+(" + r"\s+".join([r"-?\d+\."] * 9) + r")\s*$")


def _as_floats(fields):
    try:
        return [float(f.replace("D", "E").replace("d", "e")) for f in fields]
    except ValueError:
        return None


def parse_official_grid(path: str, limit: int | None = None):
    """Parse one `JB2008_AUTO_OUT_*.DAT` file written by the vendor driver.

    Layout, from `JB08DRVAUTO.f` formats 101/102/103/104/105: comment lines
    start with '#'; format 102 writes YYYY DDD HR MIN SEC SUNRAS SUNDEC as
    seven fields; format 103 writes D1950 plus the nine driver values as ten
    fields; format 104 writes Z LON LAT TINF TEMP RHO as six fields, with the
    density in Fortran `D` notation.
    """
    with open(path, "r", encoding="latin-1") as handle:
        lines = handle.readlines()
    d1950 = sun = indices = None
    rows = []
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        fields = stripped.split()
        values = _as_floats(fields)
        if values is None:
            continue
        if len(values) == 10 and indices is None:
            d1950, indices = values[0], values[1:]
        elif len(values) == 7 and sun is None and indices is None:
            sun = (values[5], values[6])
        elif len(values) == 6 and indices is not None:
            rows.append(values)
            if limit is not None and len(rows) >= limit:
                break
    if d1950 is None or sun is None or indices is None:
        raise RuntimeError(f"could not parse the vendor driver header from {path}")
    return d1950, sun, indices, rows


def grid_check(grid_bin: str, d1950, sun, indices, rows) -> dict:
    # The grid program takes AMJD *and* D1950, because it recomputes the
    # Greenwich right ascension exactly as the vendor driver does.
    requests = []
    for alt, lon, lat, tinf, temp, rho in rows:
        requests.append((d1950 + 33281.0, d1950, sun[0], sun[1], lon, lat, alt, *indices))
    results = run_probe(grid_bin, requests)
    worst_rho = 0.0
    worst_temp = 0.0
    worst_index = -1
    for i, ((alt, lon, lat, tinf, temp, rho), got) in enumerate(zip(rows, results)):
        rel = abs(got[0] - rho) / rho
        dt = max(abs(got[1] - tinf), abs(got[2] - temp))
        if rel > worst_rho:
            worst_rho, worst_index = rel, i
        worst_temp = max(worst_temp, dt)
    return {
        "points": len(rows),
        "max_relative_density_difference": worst_rho,
        "max_absolute_temperature_difference_K": worst_temp,
        "worst_point": {
            "altitude_km": rows[worst_index][0],
            "longitude_deg": rows[worst_index][1],
            "latitude_deg": rows[worst_index][2],
            "vendor_density_kg_m3": rows[worst_index][5],
            "probe_density_kg_m3": results[worst_index][0],
        },
        "note": "the vendor file prints density as D15.5 (5 significant digits) and "
                "temperature as F8.1; the residual is a compatibility statement "
                "about this file, not a claim that the two harnesses agree exactly",
    }


def index_quantisation_envelope(grid_bin: str, d1950, sun, indices, rows, stride: int = 61) -> dict:
    """Measure per-row compatibility with the ±0.5 solar-index print box.

    Eight solar indices have whole-index resolution; DSTDTC remains fixed at
    its published integer kelvin. The range over 256 corners is measured
    independently for each row. Containment does not recover one shared input
    vector for the file, nor prove a complete residual attribution.
    """
    sample = rows[::stride]
    corners = []
    for mask in range(256):
        offsets = [-0.5 if (mask >> i) & 1 else 0.5 for i in range(8)]
        corners.append([v + o for v, o in zip(indices[:8], offsets)] + [indices[8]])
    requests = []
    for alt, lon, lat, tinf, temp, rho in sample:
        for corner in corners:
            requests.append((d1950 + 33281.0, d1950, sun[0], sun[1], lon, lat, alt, *corner))
    results = run_probe(grid_bin, requests)
    inside = 0
    worst_relative = 0.0
    widest = 0.0
    for i, (alt, lon, lat, tinf, temp, rho) in enumerate(sample):
        block = results[i * len(corners):(i + 1) * len(corners)]
        values = [b[0] for b in block]
        low, high = min(values), max(values)
        if low <= rho <= high:
            inside += 1
        else:
            worst_relative = max(worst_relative, min(abs(rho - low), abs(rho - high)) / rho)
        widest = max(widest, (high - low) / rho)
    return {
        "sampled_points": len(sample),
        "stride": stride,
        "index_quantisation_sfu": 0.5,
        "points_inside_quantisation_envelope": inside,
        "fraction_inside": inside / len(sample),
        "points_outside": len(sample) - inside,
        "max_relative_distance_outside": worst_relative,
        "max_corner_span_relative": widest,
        "corner_span_definition": "(maximum_corner_density-minimum_corner_density)/reference_density",
        "note": "per-row containment in the range over 2**8 solar-index corners; "
                "not a half-width, exact reconstruction, or evidence that a single "
                "index vector reproduces every row",
    }


def repository_geodetic(r) -> tuple[float, float, float]:
    """Numerical reconstruction of the project height formula.

    This Python reconstruction is not a bit-identical C++ invocation. Actual
    provider rejection/evaluation is measured separately by boundary_probe.cpp.
    """
    a = WGS84_A_M
    e2 = WGS84_F * (2.0 - WGS84_F)
    p = math.hypot(r[0], r[1])
    lat = math.atan2(r[2], p * (1.0 - e2))
    for _ in range(16):
        s = math.sin(lat)
        n = a / math.sqrt(1.0 - e2 * s * s)
        nxt = math.atan2(r[2] + e2 * n * s, p)
        if abs(nxt - lat) < 2e-15:
            lat = nxt
            break
        lat = nxt
    s = math.sin(lat)
    c = math.cos(lat)
    n = a / math.sqrt(1.0 - e2 * s * s)
    return lat, math.atan2(r[1], r[0]), p * c + r[2] * s - n * (1.0 - e2 * s * s)


def provider_pipeline_roundtrip(coordinates_json: str,
                                latitudes=(0.0, 45.0, -45.0, 89.9, -89.9, 90.0, -90.0),
                                longitudes=(0.0, 1.0, 123.456, 180.0, 270.0, 359.999, -179.0),
                                altitudes_km=(120.0, 200.0, 400.0, 1000.0, 1500.0)) -> dict:
    """J2000 -> ITRF93 -> native geodetic, driven from an independent construction.

    The point is built Earth-fixed from an exact WGS84 geodetic triple with this
    file's own forward conversion, rotated into J2000 with the transpose of the
    measured ITRF93 rotation, and handed to the repository pipeline's arithmetic.
    No repository binary is involved, so the result is not the repository
    agreeing with itself.

    The quantity that matters for the JB2008 backend is the scalar altitude in
    kilometres that the backend's domain check compares against 120 and 1500.
    """
    with open(coordinates_json, "r", encoding="utf-8") as handle:
        report = json.load(handle)
    rotation = report["earth_fixed_rotation"]

    def apply(matrix, v):
        return [sum(matrix[i][j] * v[j] for j in range(3)) for i in range(3)]

    cases = []
    worst_abs = 0.0
    rejected = []
    for lat_deg in latitudes:
        for lon_deg in longitudes:
            for alt_km in altitudes_km:
                fixed = ecef_from_geodetic(lat_deg, lon_deg, alt_km)
                # `ecef_from_geodetic` works in kilometres, the repository
                # converter works in metres.
                fixed_m = [component * 1000.0 for component in fixed]
                j2000 = [sum(rotation[i][j] * fixed_m[i] for i in range(3)) for j in range(3)]
                back = apply(rotation, j2000)
                _, _, height_m = repository_geodetic(back)
                out_km = height_m / 1000.0
                error_km = out_km - alt_km
                worst_abs = max(worst_abs, abs(error_km))
                below = out_km < 120.0
                above = out_km > 1500.0
                case = {
                    "geodetic_latitude_deg": lat_deg,
                    "geodetic_longitude_deg": lon_deg,
                    "requested_altitude_km": alt_km,
                    "pipeline_altitude_km": out_km,
                    "difference_km": error_km,
                    "rejected_by_domain_check": bool(below or above),
                }
                cases.append(case)
                if below or above:
                    rejected.append(case)
    boundary = [c for c in cases if c["requested_altitude_km"] in (120.0, 1500.0)]
    boundary_rejected = sum(1 for c in boundary if c["rejected_by_domain_check"])
    return {
        "coordinates_report": os.path.abspath(coordinates_json),
        "coordinates_report_sha256": sha256(coordinates_json),
        "frame": report.get("frame"),
        "epoch_utc": report.get("utc"),
        "earth_fixed_rotation": rotation,
        "cases": cases,
        "worst_absolute_altitude_error_km": worst_abs,
        "boundary_cases": len(boundary),
        "boundary_cases_rejected": boundary_rejected,
        "rejected_examples": rejected[:8],
        "note": "the repository pipeline's J2000 -> ITRF93 -> WGS84 height round trip is "
                f"exact to {worst_abs * 1000.0:.3e} m, but the residual is signed, so a "
                "position built to land exactly on the 120 km or 1500 km domain edge "
                f"crosses to the wrong side of the backend's strict comparison in "
                f"{boundary_rejected} of {len(boundary)} such cases",
    }


# --------------------------------------------------------------------------
def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--jb2008", default="/tmp/leo-atmosphere-deps/JB2008.f")
    parser.add_argument("--vendor-driver",
                        default="/tmp/jbzip-jb2008/JB2008 Code/JB08DRVAUTO.f")
    parser.add_argument("--official-grid",
                        default="/tmp/leo-atmosphere-deps/jb-validation/JB2008_AUTO_OUT_2023_091_00.DAT")
    parser.add_argument("--fortran", default="/tmp/leo-fortran/usr/bin/gfortran-13")
    parser.add_argument("--coordinates-json", required=True,
                        help="a coordinates.json produced by "
                             "`build-atmosphere/atmosphere_experiment --mode coordinates`; "
                             "supplies the measured ITRF93 rotation")
    parser.add_argument("--out", required=True)
    parser.add_argument("--keep-build", action="store_true")
    args = parser.parse_args()

    for path in (args.jb2008, args.vendor_driver, args.official_grid, args.fortran,
                 args.coordinates_json):
        if not os.path.exists(path):
            print(f"missing required input: {path}", file=sys.stderr)
            return 2

    if os.path.exists(args.out):
        print(f"refusing to overwrite existing output directory: {args.out}", file=sys.stderr)
        return 2
    os.makedirs(args.out)

    workdir = os.path.join(args.out, "build")
    os.makedirs(workdir)
    libpath = None
    try:
        probe, grid_bin, theta_probe, provenance = build(
            args.fortran, args.jb2008,
            os.path.join(here, "jb2008_altitude_probe.f90"),
            os.path.join(here, "jb2008_official_grid_check.f90"),
            args.vendor_driver, workdir)
        libpath = os.path.realpath(os.path.join(
            os.path.dirname(args.fortran), "..", "lib", "x86_64-linux-gnu"))
        os.environ["LD_LIBRARY_PATH"] = libpath + ":" + os.environ.get("LD_LIBRARY_PATH", "")

        d1950, sun, indices, rows = parse_official_grid(args.official_grid)
        theta = float(subprocess.run(
            [theta_probe], input=f"{d1950:.17g}\n", capture_output=True,
            text=True, check=True).stdout)
        epoch = {
            "source": os.path.abspath(args.official_grid),
            "source_sha256": sha256(args.official_grid),
            "d1950_days_since_1950_jan0": d1950,
            "amjd": d1950 + 33281.0,
            "sun_ra_rad": sun[0],
            "sun_dec_rad": sun[1],
            "indices_sfu": dict(zip(
                ["f10", "f10b", "s10", "s10b", "m10", "m10b", "y10", "y10b"], indices)),
            "dstdtc_K": indices[8],
            "greenwich_hour_angle_rad": theta,
            "candidate_spacecraft_ra": "(official_THETA(D1950)+east_longitude_rad) mod 2pi",
        }

        print(f"[1/5] reproducing the vendor validation grid ({len(rows)} points)", flush=True)
        check = grid_check(grid_bin, d1950, sun, indices, rows)

        print("[2/5] bounding the residual against the file's printed index precision", flush=True)
        envelope = index_quantisation_envelope(grid_bin, d1950, sun, indices, rows)
        check["index_quantisation_envelope"] = envelope

        print("[3/5] evaluating altitude-datum candidates", flush=True)
        requests = []
        meta = []
        for lat_deg in LATITUDES_DEG:
            for lon_deg in LONGITUDES_DEG:
                for alt_km in ALTITUDES_KM:
                    r = ecef_from_geodetic(lat_deg, lon_deg, alt_km)
                    for candidate in CANDIDATES:
                        lat, alt = candidate_inputs(r, lat_deg, alt_km, candidate)
                        requests.append((epoch["amjd"], sun[0], sun[1],
                                         (theta + lon_deg * math.pi / 180.0) % (2 * math.pi),
                                         lat, alt, *indices))
                        meta.append((lat_deg, lon_deg, alt_km, candidate, lat, alt, r))
        results = run_probe(probe, requests)

        by_key = {}
        for (lat_deg, lon_deg, alt_km, candidate, lat, alt, r), got in zip(meta, results):
            by_key[(lat_deg, lon_deg, alt_km, candidate)] = {
                "sat_lat_rad": lat,
                "sat_alt_km": alt,
                "sat_alt_minus_geodetic_km": alt - alt_km,
                "geocentric_radius_km": geocentric_radius_km(r),
                "density_kg_m3": got[0],
                "exospheric_temperature_K": got[1],
                "temperature_K": got[2],
                "inside_backend_domain": bool(
                    BACKEND_MIN_ALT_KM <= alt <= BACKEND_MAX_ALT_KM),
                "inside_backend_domain_note":
                    "the adapter rejects this scalar; a candidate reading that "
                    "lands outside [120, 1500] km is a sensitivity diagnostic of "
                    "the upstream reference itself, not a configuration this "
                    "project would ever run",
            }

        base = "geodetic_height_geocentric_latitude"
        table = []
        for lat_deg in LATITUDES_DEG:
            for lon_deg in LONGITUDES_DEG:
                for alt_km in ALTITUDES_KM:
                    reference = by_key[(lat_deg, lon_deg, alt_km, base)]
                    row = {
                        "geodetic_latitude_deg": lat_deg,
                        "geodetic_longitude_deg": lon_deg,
                        "geodetic_altitude_km": alt_km,
                        "geocentric_radius_km": reference["geocentric_radius_km"],
                        "geocentric_latitude_deg": math.degrees(reference["sat_lat_rad"]),
                        "reference_density_kg_m3": reference["density_kg_m3"],
                        "candidates": {},
                    }
                    for candidate in CANDIDATES:
                        entry = by_key[(lat_deg, lon_deg, alt_km, candidate)]
                        entry["density_ratio_to_reference"] = entry["density_kg_m3"] / reference["density_kg_m3"]
                        entry["density_percent_difference"] = 100.0 * (entry["density_ratio_to_reference"] - 1.0)
                        entry["temperature_difference_K"] = entry["temperature_K"] - reference["temperature_K"]
                        row["candidates"][candidate] = entry
                    table.append(row)

        # Separate native sensitivity diagnostics outside the adapter domain
        # from in-domain comparisons at 200 km and above.
        bands = {"all_altitudes": None, "altitude_ge_200km": 200.0,
                 "in_backend_domain_and_altitude_ge_200km": 200.0}

        def band_rows(limit):
            return [r for r in table if limit is None or r["geodetic_altitude_km"] >= limit]

        summary = {}
        for candidate in CANDIDATES:
            entry = {}
            for band, limit in bands.items():
                rows = band_rows(limit)
                if band.startswith("in_backend_domain"):
                    rows = [r for r in rows
                            if r["candidates"][candidate]["inside_backend_domain"]]
                worst = max(rows, key=lambda r: abs(r["candidates"][candidate]["density_percent_difference"]))
                entry[band] = {
                    "points_considered": len(rows),
                    "max_abs_density_percent_difference":
                        abs(worst["candidates"][candidate]["density_percent_difference"]),
                    "at": {
                        "geodetic_latitude_deg": worst["geodetic_latitude_deg"],
                        "geodetic_longitude_deg": worst["geodetic_longitude_deg"],
                        "geodetic_altitude_km": worst["geodetic_altitude_km"],
                    },
                    "reference_density_kg_m3": worst["reference_density_kg_m3"],
                }
            offset = max(table, key=lambda r: abs(r["candidates"][candidate]["sat_alt_minus_geodetic_km"]))
            entry["max_abs_altitude_offset_km"] = abs(offset["candidates"][candidate]["sat_alt_minus_geodetic_km"])
            summary[candidate] = entry

        print("[4/5] measuring the provider pipeline round trip", flush=True)
        roundtrip = provider_pipeline_roundtrip(args.coordinates_json)

        # Headline table: the in-domain points a reader actually cares about.
        # Every cell is a point the adapter would accept for both the reference
        # and the candidate, so the ratios are comparable quantities rather than
        # diagnostics of values the project would never pass.
        headline = {"note": "density ratio candidate/reference at points where "
                            "BOTH the reference geodetic height and the candidate "
                            "scalar lie inside the adapter's [120, 1500] km domain; "
                            "longitudes across the sweep are averaged because the "
                            "datum offsets are longitude-independent",
                    "rows": []}
        for alt_km in (200.0, 400.0, 600.0):
            for lat_deg in (0.0, 45.0, -45.0, 89.9, -89.9):
                cells = {}
                for candidate in CANDIDATES:
                    chosen = [r for r in table
                              if r["geodetic_altitude_km"] == alt_km
                              and r["geodetic_latitude_deg"] == lat_deg
                              and r["candidates"][candidate]["inside_backend_domain"]]
                    if not chosen:
                        cells[candidate] = None
                        continue
                    ratios = [r["candidates"][candidate]["density_ratio_to_reference"]
                              for r in chosen]
                    cells[candidate] = sum(ratios) / len(ratios)
                headline["rows"].append({
                    "geodetic_altitude_km": alt_km,
                    "geodetic_latitude_deg": lat_deg,
                    "density_ratio_candidate_over_reference": cells,
                })

        print("[5/5] writing evidence", flush=True)
        report = {
            "question": "Is JB2008 SAT(3) a geodetic height above the WGS84 ellipsoid "
                        "or a spherical height above some Earth radius, and is SAT(2) "
                        "geocentric or geodetic latitude?",
            "convention_statement": {
                "sat_2_latitude": "geocentric latitude, as the upstream source documents it",
                "sat_3_altitude": "geodetic height above the WGS84 ellipsoid",
                "status": "UNRESOLVED by primary evidence; this is the convention "
                          "retained by the implementation, not one any authoritative "
                          "artifact establishes",
            },
            "source_citations": SOURCE_CITATIONS,
            "reference": provenance,
            "vendor_epoch": epoch,
            "official_grid_reproduction": check,
            "definitions": {
                "WGS84_a_km": WGS84_A_KM,
                "WGS84_b_km": WGS84_B_KM,
                "JB2008_internal_gravity_radius_km": JB2008_GRAVITY_RADIUS_KM,
                "IUGG_mean_radius_km": IUGG_MEAN_RADIUS_KM,
                "candidates": CANDIDATES,
            },
            "candidates": table,
            "candidate_summary": summary,
            "headline_in_domain_ratios": headline,
            "provider_pipeline_roundtrip": roundtrip,
        }
        with open(os.path.join(args.out, "altitude-datum.json"), "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2, sort_keys=False)
            handle.write("\n")
        print(json.dumps({"grid": check, "written": os.path.join(args.out, "altitude-datum.json")},
                         indent=2))
    finally:
        if not args.keep_build:
            shutil.rmtree(workdir, ignore_errors=True)
            if not os.listdir(args.out):
                os.rmdir(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())