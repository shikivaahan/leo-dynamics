#!/usr/bin/env python3
"""Run the direct-reference atmospheric experiments and freeze their evidence.

Python standard library only. No downloads: every kernel and space-weather file
is supplied explicitly by the caller and hashed here.

The runner
  * verifies the source tree, dependencies and data files by SHA-256,
  * records the compiler, its exact flags, the CPU and the threading setup,
  * invokes ``atmosphere_experiment`` once per subcommand into a fresh,
    exclusive output directory,
  * writes a machine-readable ``manifest.json`` recording the exact commands,
    all hashes and every active configuration value.

Historical evidence is never overwritten: the output directory must not exist or
must be empty, and the runner refuses to write into a populated one.

Usage examples are in docs/atmosphere/EXPERIMENTS.md.
"""
import argparse
import hashlib
import json
import os
import platform
import re
import shlex
import subprocess
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

# NRLMSISE-00 flag switches as the adapter builds them: standard switches are 1
# for indices 1..23 and 0 for index 0 (the kg/m^3 unit selector); the Ap history
# selector at index 9 becomes -1 when the seven 3-hourly values are supplied.
# Recorded here so the manifest states the vector in force rather than leaving a
# reader to assume library defaults.
NRLMSISE_STANDARD_SWITCHES = [0] + [1] * 23
NRLMSISE_HISTORY_SWITCHES = [0] + [1] * 23
NRLMSISE_HISTORY_SWITCHES[9] = -1

# direct-v2 adds provenance the first manifest omitted: the compiled source
# revision, validation-tooling revision, tolerance definitions, measured test
# results, historical/preservation fingerprints and the corrected-source change
# list. The amended manifest states its own schema below.
MANIFEST_SCHEMA_VERSION = 2

# Driver index lags, in force at the time these experiments were defined. They
# are recorded as configuration, not re-derived here.
NRLMSISE_INPUT_LAGS = {
    "f107": "previous UTC day, daily observed 10.7 cm flux",
    "f107_average": "centred 81-day average of observed daily F10.7",
    "ap": "daily Ap of the current UTC day",
    "ap_history": "daily Ap; current, -3, -6, -9 h bins; means of 12..33 h and 36..57 h preceding bins",
}
JB2008_INPUT_LAGS = {
    "F10_S10": "1 day",
    "M10": "2 days",
    "Y10": "5 days",
    "F10B_S10B": "1 day; supplied centred 81-day averages",
    "M10B": "2 days; supplied centred 81-day average",
    "Y10B": "5 days; supplied centred 81-day average",
    "DSTDTC": "hourly storm correction, from the official DTCFILE",
}

# No flag value is hard-coded anywhere in this runner. Compiler identity and
# every C/C++/Fortran flag variable are read from the build tree's CMakeCache.txt
# by cmake_cache(); assuming a default here would misreport the actual build.


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def describe(path, root=None):
    entry = {"path": str(path)}
    resolved = Path(path)
    if resolved.is_file():
        entry["sha256"] = sha256(resolved)
        entry["bytes"] = resolved.stat().st_size
        try:
            entry["modified_utc"] = datetime.fromtimestamp(
                resolved.stat().st_mtime, timezone.utc).isoformat()
        except OSError:
            entry["modified_utc"] = None
        if root is not None:
            try:
                entry["repository_relative"] = str(resolved.resolve().relative_to(root))
            except ValueError:
                entry["repository_relative"] = None
    return entry


def git(*args):
    result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def source_identity():
    """Hash every source file that can change the measured result."""
    patterns = [
        "CMakeLists.txt", "cmake/*.cmake",
        "include/leo/**/*.hpp", "src/**/*.cpp", "src/**/*.hpp",
        # Fortran bridge sources are compiled into the JB2008 backend and change
        # its ABI and reentrancy behaviour, so they belong in the source hash.
        "src/**/*.f90", "src/**/*.f", "src/**/*.F90",
        "docs/atmosphere/CMakeLists.txt", "docs/atmosphere/experiment.cpp",
        "docs/atmosphere/run.py", "docs/atmosphere/EXPERIMENTS.md",
        "docs/atmosphere/DECISIONS.md",
        "docs/numerical-audit/experiments/scenario.hpp",
        "docs/numerical-audit/experiments/*.cfg",
        "benchmarks/cases/bench_support.hpp",
    ]
    files = []
    for pattern in patterns:
        files.extend(sorted(ROOT.glob(pattern)))
    unique = sorted({f for f in files if f.is_file()})
    digest = hashlib.sha256()
    per_file = {}
    for path in unique:
        rel = str(path.relative_to(ROOT))
        file_hash = sha256(path)
        per_file[rel] = file_hash
        digest.update(rel.encode())
        digest.update(file_hash.encode())
    return {"tree_sha256": digest.hexdigest(), "files": per_file,
            "git_sha": git("rev-parse", "HEAD"),
            "git_status": git("status", "--porcelain"),
            "git_describe": git("log", "-1", "--format=%H %cI %s")}


def hash_tree(root, patterns):
    """Hash the acquired external sources that are compiled into this build."""
    root = Path(root)
    if not root.is_dir():
        return None
    files = {}
    for pattern in patterns:
        for path in sorted(root.rglob(pattern)):
            if path.is_file():
                files[str(path.relative_to(root))] = sha256(path)
    return files or None


def header_dependency(cache, includes_key, version_key, package):
    roots = []
    digest = hashlib.sha256()
    count = size = 0
    for directory in (cache.get(includes_key) or "").split(";"):
        root = Path(directory)
        if not directory or not root.is_dir():
            continue
        tree = root / package if package else root
        if not tree.is_dir():
            continue
        roots.append(str(tree))
        for path in sorted(tree.rglob("*")):
            if path.is_file():
                digest.update(str(path.relative_to(tree)).encode())
                digest.update(sha256(path).encode())
                count += 1
                size += path.stat().st_size
    if count == 0:
        raise RuntimeError(f"Resolved {includes_key} header tree unavailable; cannot freeze dependency provenance")
    return {"version": cache.get(version_key), "header_roots": roots,
            "source_tree_sha256": digest.hexdigest(), "files_hashed": count, "bytes_hashed": size}


def dependency_identity(cache):
    """Eigen, Boost and CSPICE: version evidence plus content hashes."""
    deps = {}
    libs = cache.get("CSPICE_LIBRARY")
    cspice = {"library_path": libs}
    if libs and Path(libs).is_file():
        cspice["library_sha256"] = sha256(libs)
        cspice["library_bytes"] = Path(libs).stat().st_size
        cspice["version_source"] = 'SpiceEphemerisProvider::toolkit_version / tkvrsn_c("TOOLKIT"), recorded in coordinates.json'
    else:
        cspice["note"] = "CSPICE library path not recorded in the cache"
    deps["cspice"] = cspice
    deps["eigen"] = header_dependency(cache, "LEO_EIGEN3_INCLUDE_DIRS", "LEO_EIGEN3_VERSION", "")
    deps["boost"] = header_dependency(cache, "LEO_BOOST_INCLUDE_DIRS", "LEO_BOOST_VERSION", "boost")
    return deps


def external_sources(cache):
    """Hash every acquired upstream file compiled into the binary."""
    out = {}
    nrl_root = cache.get("LEO_NRLMSISE00_ROOT")
    if nrl_root:
        out["nrlmsise00"] = {
            "root": nrl_root,
            "files": hash_tree(nrl_root, ("*.c", "*.h")),
        }
    jb_source = cache.get("LEO_JB2008_SOURCE")
    if jb_source and Path(jb_source).is_file():
        out["jb2008"] = {
            "official_source": jb_source,
            "official_source_sha256": sha256(jb_source),
            "bridge": describe(ROOT / "src/atmosphere/jb2008_bridge.f90", ROOT),
            "adapter": describe(ROOT / "src/atmosphere/jb2008.cpp", ROOT),
            "licence": "external, not redistributed in this repository",
        }
    return out


def compiler_identity(cache):
    """Identify every language compiler actually used, with real flags.

    Nothing here is guessed: the compiler paths and all flag variables are read
    from the build tree's CMakeCache.txt. A constant would misreport the build.
    """
    out = {"languages": {}, "flags": {}}
    for language, cache_name in (("C", "CMAKE_C_COMPILER"),
                                 ("CXX", "CMAKE_CXX_COMPILER"),
                                 ("Fortran", "CMAKE_Fortran_COMPILER")):
        compiler = cache.get(cache_name)
        if not compiler:
            out["languages"][language] = {
                "used": False,
                "reason": f"{cache_name} is not set in this build",
            }
            continue
        path = Path(compiler)
        entry = {"used": True, "path": str(path), "cache_variable": cache_name}
        if path.is_file():
            entry["binary_sha256"] = sha256(path)
            for flag, key in (("--version", "version"), ("-dumpmachine", "target")):
                try:
                    result = subprocess.run([str(path), flag], capture_output=True, text=True)
                    if result.returncode == 0:
                        entry[key] = result.stdout.strip()
                except OSError as error:
                    entry[key] = f"unavailable: {error}"
        else:
            entry["available"] = False
            entry["reason"] = "compiler path from the cache does not exist"
        out["languages"][language] = entry

    # Global and per-configuration flag variables, exactly as configured.
    for name in sorted(cache):
        if re.match(r"^CMAKE_(C|CXX|Fortran)_FLAGS(_[A-Z]+)?$", name) and cache[name]:
            out["flags"][name] = cache[name]
    out["build_type"] = cache.get("CMAKE_BUILD_TYPE")
    out["note"] = ("read from CMakeCache.txt; no flag value is assumed. Target-level options "
                   "set in cmake/*.cmake are recorded under backends.")
    return out


def cpu_identity():
    entry = {"machine": platform.machine(), "processor": platform.processor(),
             "logical_cores": os.cpu_count()}
    try:
        text = Path("/proc/cpuinfo").read_text()
        models = sorted({line.split(":", 1)[1].strip()
                         for line in text.splitlines() if line.startswith("model name")})
        entry["model_names"] = models
        flags = sorted({line.split(":", 1)[1].strip()
                        for line in text.splitlines() if line.startswith("flags")})
        if flags:
            interesting = {"avx", "avx2", "avx512f", "fma", "sse4_2"}
            entry["relevant_isa_flags"] = sorted(interesting.intersection(flags[0].split()))
    except OSError:
        entry["model_names"] = None
    try:
        text = Path("/proc/meminfo").read_text()
        for line in text.splitlines():
            if line.startswith("MemTotal"):
                entry["memory_total"] = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    try:
        entry["numa_nodes"] = len([n for n in Path("/sys/devices/system/node").iterdir()
                                   if n.name.startswith("node")])
    except OSError:
        entry["numa_nodes"] = None
    entry["affinity_cpus"] = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None
    for name in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
        entry[name] = os.environ.get(name)
    entry["note"] = ("measurements are single-threaded except for the explicit concurrency "
                     "columns; the environment variables are recorded because a machine with "
                     "them set is not a clean single-thread baseline")
    return entry


def thread_settings():
    return {name: os.environ.get(name) for name in
            ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS",
             "OMP_PROC_BIND", "OMP_PLACES")}


def cmake_cache(build_dir, names):
    """Read the exact configure options and flag variables from the build tree.

    Returns every requested entry plus every C/C++/Fortran flag variable, so no
    flag value has to be assumed by the caller.
    """
    cache = Path(build_dir) / "CMakeCache.txt"
    result = {}
    if not cache.is_file():
        result["cache_present"] = False
        return result
    result["cache_present"] = True
    text = cache.read_text()
    for line in text.splitlines():
        match = re.match(r"^([A-Za-z0-9_]+):[A-Z]+=(.*)$", line)
        if not match:
            continue
        key, value = match.group(1), match.group(2)
        if key in names or re.match(r"^CMAKE_(C|CXX|Fortran)_FLAGS(_[A-Z]+)?$", key):
            result[key] = value
    for name in names:
        result.setdefault(name, None)
    return result


def require_file(path, what):
    resolved = Path(path)
    if not resolved.is_file():
        raise SystemExit(f"required {what} not found: {path}\n"
                         f"acquire it explicitly; this runner never downloads anything")
    return resolved


def require_empty_directory(path):
    target = Path(path)
    if target.exists():
        if not target.is_dir():
            raise SystemExit(f"--out exists and is not a directory: {target}")
        if any(target.iterdir()):
            raise SystemExit(f"refusing to overwrite non-empty evidence directory: {target}")
        return target
    target.mkdir(parents=True)
    return target


def execute(binary, arguments):
    command = [str(binary), *[str(a) for a in arguments]]
    print("+ " + " ".join(shlex.quote(part) for part in command), flush=True)
    result = subprocess.run(command, capture_output=True, text=True)
    record = {
        "command": command,
        "command_line": " ".join(shlex.quote(part) for part in command),
        "returncode": result.returncode,
    }
    if result.stdout.strip():
        record["stdout"] = result.stdout.strip()
    if result.stderr.strip():
        record["stderr"] = result.stderr.strip()[-4000:]
    if result.returncode:
        raise SystemExit(f"experiment failed ({result.returncode}): {record['command_line']}\n"
                         f"{result.stderr.strip()[-4000:]}")
    return record


def default_fixtures(args):
    """Quiet, active and storm epochs, each in two seasons.

    Values chosen by the caller, never derived from a climatological default.
    """
    return [
        "quiet_june=" + args.quiet_june,
        "quiet_december=" + args.quiet_december,
        "active_march=" + args.active_march,
        "active_october=" + args.active_october,
        "storm_october=" + args.storm_october,
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", required=True, help="path to atmosphere_experiment")
    parser.add_argument("--build-dir", required=True, help="CMake build tree of the experiment")
    parser.add_argument("--out", required=True, help="fresh, exclusive evidence directory")
    parser.add_argument("--lsk", required=True, help="leapseconds kernel (TLS)")
    parser.add_argument("--spk", required=True, help="planetary ephemeris kernel")
    parser.add_argument("--pck", help="Earth orientation kernel (PCK)")
    parser.add_argument("--frame", default="ITRF93", help="Earth-fixed frame name")
    parser.add_argument("--weather", help="CelesTrak/CSSI v1.2 space-weather archive")
    parser.add_argument("--solfsmy", help="official JB2008 SOLFSMY file")
    parser.add_argument("--dtcfile", help="official JB2008 DTCFILE")
    parser.add_argument("--epoch-utc", required=True,
                        help="shared real historical propagation epoch, UTC ISO")
    parser.add_argument("--case", action="append", dest="cases",
                        help="CASE definition file; repeat for CASE-A/B/C")
    parser.add_argument("--fixture", action="append", dest="fixtures",
                        help="NAME=UTC_ISO fixture epoch; overrides the default set")
    parser.add_argument("--quiet-june", default="2008-07-01T00:00:00")
    parser.add_argument("--quiet-december", default="2008-12-15T00:00:00")
    parser.add_argument("--active-march", default="2001-03-20T00:00:00")
    parser.add_argument("--active-october", default="2003-10-28T00:00:00")
    parser.add_argument("--storm-october", default="2003-10-30T00:00:00")
    parser.add_argument("--duration", type=float, default=21600.0)
    parser.add_argument("--cadence", type=float, default=30.0)
    parser.add_argument("--rtol", type=float, default=1e-10)
    parser.add_argument("--max-step", type=float, default=15.0)
    parser.add_argument("--refine-rtol", type=float, default=1e-12)
    parser.add_argument("--refine-max-step", type=float, default=5.0)
    parser.add_argument("--timing-calls", type=int, default=2000)
    parser.add_argument("--timing-repeats", type=int, default=7)
    parser.add_argument("--timing-propagation-repeats", type=int, default=3)
    parser.add_argument("--timing-threads", default="1,2,4")
    parser.add_argument("--ap-history", action="store_true",
                        help="enable the NRLMSISE-00 three-hourly Ap history")
    parser.add_argument("--modes", default="coordinates,fixtures,propagate,timing",
                        help="comma-separated subset of "
                             "coordinates,fixtures,propagate,timing")
    parser.add_argument("--build-command", action="append", dest="build_command", default=None,
                        help="exact command line used to configure/build; repeat for each step")
    parser.add_argument("--test-command", action="append", dest="test_command", default=None,
                        help="exact command line used to run the project's tests; repeat per suite")
    parser.add_argument("--note", help="free-text note stored in the manifest")
    args = parser.parse_args()
    # argparse leaves an unmatched --case as None; every later loop iterates it.
    args.cases = args.cases or []

    binary = require_file(args.binary, "experiment binary")
    lsk = require_file(args.lsk, "leapseconds kernel")
    spk = require_file(args.spk, "planetary ephemeris kernel")
    pck = require_file(args.pck, "Earth orientation kernel") if args.pck else None
    out = require_empty_directory(args.out)

    modes = [m.strip() for m in args.modes.split(",") if m.strip()]
    for mode in modes:
        if mode not in ("coordinates", "fixtures", "propagate", "timing"):
            raise SystemExit(f"unknown mode: {mode}")

    # The coordinates report is an observation surface over the ephemeris and
    # orientation kernels alone; every other mode needs driver data. Requiring
    # the files only where they are used keeps a coordinate inspection run from
    # demanding a weather archive it never reads.
    needs_weather = any(m != "coordinates" for m in modes)
    if needs_weather:
        for flag, value in (("--weather", args.weather), ("--solfsmy", args.solfsmy),
                            ("--dtcfile", args.dtcfile)):
            if not value:
                raise SystemExit(f"{flag} is required for modes {modes}; only a "
                                 f"coordinates-only run omits the weather dataset")
        weather = require_file(args.weather, "space-weather archive")
        solfsmy = require_file(args.solfsmy, "SOLFSMY file")
        dtcfile = require_file(args.dtcfile, "DTCFILE")
    else:
        weather = solfsmy = dtcfile = None
    if ("propagate" in modes or "timing" in modes) and not args.cases:
        raise SystemExit("at least one --case is required for propagate and timing")
    for case in args.cases:
        require_file(case, "CASE definition")
    fixtures = args.fixtures or default_fixtures(args)

    common = [
        "--lsk", lsk, "--spk", spk, "--frame", args.frame,
        "--epoch-utc", args.epoch_utc,
        "--duration", args.duration, "--cadence", args.cadence,
        "--rtol", args.rtol, "--max-step", args.max_step,
        "--refine-rtol", args.refine_rtol, "--refine-max-step", args.refine_max_step,
    ]
    if needs_weather:
        common += ["--weather", weather, "--solfsmy", solfsmy, "--dtcfile", dtcfile]
    if pck:
        common += ["--pck", pck]
    if args.ap_history:
        common.append("--ap-history")

    commands = []
    for mode in modes:
        arguments = list(common)
        if mode == "coordinates":
            pass
        elif mode == "fixtures":
            for value in fixtures:
                arguments += ["--fixture", value]
        elif mode == "propagate":
            for case in args.cases:
                arguments += ["--case", case]
        else:
            arguments += ["--case", args.cases[0],
                          "--timing-calls", args.timing_calls,
                          "--timing-repeats", args.timing_repeats,
                          "--timing-propagation-repeats", args.timing_propagation_repeats,
                          "--timing-threads", args.timing_threads]
        # Each mode writes into its own fresh subdirectory. The executable
        # refuses a populated --out, which is what keeps one mode from
        # overwriting another's evidence.
        arguments += ["--mode", mode, "--out", out / mode]
        commands.append(execute(binary, arguments))

    cache = cmake_cache(args.build_dir, [
        "LEO_ENABLE_CSPICE", "LEO_ENABLE_NRLMSISE00", "LEO_ENABLE_JB2008",
        "LEO_BUILD_INTEGRATION", "LEO_BUILD_ANALYSIS", "LEO_SOURCE_DIR",
        "LEO_NRLMSISE00_ROOT", "LEO_JB2008_SOURCE", "CSPICE_ROOT", "CSPICE_LIBRARY",
        "Eigen3_DIR", "Boost_INCLUDE_DIR", "Boost_VERSION",
        "LEO_EIGEN3_INCLUDE_DIRS", "LEO_EIGEN3_VERSION", "LEO_BOOST_INCLUDE_DIRS", "LEO_BOOST_VERSION",
        "CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_Fortran_COMPILER",
    ])

    backends = {
        "exponential": {
            "source": "repository src/kernel/aerodynamics.cpp (ExponentialAtmosphere)",
            "external": False,
            "note": "existing lightweight baseline, retained unchanged",
        },
    }
    if cache.get("LEO_ENABLE_NRLMSISE00") == "ON":
        backends["nrlmsise00"] = {
            "source": "pinned Brodowski C reference, gtd7d entry point",
            "external": True,
            "root": cache.get("LEO_NRLMSISE00_ROOT"),
            "density_entry_point": "gtd7d",
            "density_output": "output.d[5] = total mass density including anomalous oxygen",
            "units_switch": "switches[0] = 1 selects kg/m^3 (0 selects g/cm^3)",
            "switches_in_force": list(NRLMSISE_STANDARD_SWITCHES) if not args.ap_history
            else list(NRLMSISE_HISTORY_SWITCHES),
            "ap_history_enabled": bool(args.ap_history),
            "thread_safety": "upstream C model keeps mutable global state; the adapter "
                             "serialises native calls on a process-wide mutex",
        }
    if cache.get("LEO_ENABLE_JB2008") == "ON":
        backends["jb2008"] = {
            "source": "official external JB2008 Fortran reference",
            "external": True,
            "source_file": cache.get("LEO_JB2008_SOURCE"),
            "fortran_compiler_required": cache.get("CMAKE_Fortran_COMPILER"),
            "density_entry_point": "leo_jb2008 bridge (JB2008.f)",
            "density_output": "rho = total mass density in kg/m^3",
            "density_semantics_note": (
                "JB2008 total mass density is a model-form quantity that is not identical to "
                "NRLMSISE-00 gtd7d total density: the two models use different constituent sets, "
                "different temperature definitions and different fitted upper-boundary "
                "conditions. The two densities must be reported as model-form differences and "
                "are not evidence that either implementation is wrong."),
            "thread_safety": "upstream Fortran model keeps mutable COMMON/module state; the "
                             "adapter serialises native calls on a process-wide mutex",
        }

    manifest = {
        "kind": "atmospheric-baseline-manifest",
        "schema_version": 1,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "note": args.note,
        "project": source_identity(),
        "compiler": compiler_identity(cache),
        "dependencies": dependency_identity(cache),
        "external_sources": external_sources(cache),
        "build": {
            "build_dir": str(Path(args.build_dir).resolve()),
            "binary": describe(binary),
            "build_commands": args.build_command,
            "test_command": args.test_command,
        },
        "cpu": cpu_identity(),
        "threading": thread_settings(),
        "kernels": {
            "lsk": describe(lsk, ROOT),
            "spk": describe(spk, ROOT),
            "pck": describe(pck, ROOT) if pck else None,
        },
        "space_weather": {
            "used": needs_weather,
            "archive": describe(weather, ROOT) if needs_weather else None,
            "archive_format": "CelesTrak/CSSI v1.2 fixed-field with explicit OBSERVED sections",
            "solfsmy": describe(solfsmy, ROOT) if needs_weather else None,
            "dtcfile": describe(dtcfile, ROOT) if needs_weather else None,
            "policy": "observed records only; no observed value is silently replaced by a "
                      "prediction, and no missing value is filled with a climatological default",
        } if needs_weather else {
            "used": False,
            "reason": "coordinates-only run; no atmosphere model was evaluated, so no driver "
                      "data was read",
        },
        "scenarios": {
            "cases": [describe(c, ROOT) for c in args.cases],
            "epoch_utc": args.epoch_utc,
            "epoch_interpretation": "TDB ephemeris seconds past J2000, converted from UTC by the "
                                    "leapseconds kernel; UTC itself comes from the kernel, never "
                                    "from elapsed solver time",
            "duration_s": args.duration,
            "output_cadence_s": args.cadence,
        },
        "numerics": {
            "integrators": ["dopri5 nominal (leo::propagate)",
                            "boost odeint controlled RKF7(8) independent refinement"],
            "rtol": args.rtol,
            "max_step_s": args.max_step,
            "refine_rtol": args.refine_rtol,
            "refine_max_step_s": args.refine_max_step,
            "absolute_tolerances": {
                "position_m": 1e-3, "velocity_m_s": 1e-6,
                "quaternion": 1e-12, "body_rate_rad_s": 1e-12,
                "source": "existing project defaults, unchanged",
            },
            "initial_step_s": min(1.0, args.max_step),
            # The existing IntegrationOptions default. Recorded explicitly so a
            # reader can confirm no numerical default was raised for these runs.
            "max_attempts": 1000000,
            "max_attempts_source": "include/leo/integration/options.hpp default, unchanged",
            "defaults_changed": "none; rtol, max_step, initial_step, the four absolute "
                                 "tolerances and max_attempts are all the existing project "
                                 "values. Only the named refinement values (refine_rtol, "
                                 "refine_max_step) differ, and only for the RKF78 run.",
            "case_c_threshold": "none; CASE-C remains a sensitive stress test and no "
                                "attitude convergence threshold is asserted for it",
        },
        "frames": {
            "inertial": "J2000 (ECI), as used by the propagator",
            "earth_fixed": args.frame,
            "ellipsoid": "WGS84, a = 6378137 m, 1/f = 298.257223563",
            "latitude": "grid points are geodetic; each model receives its own published "
                        "latitude convention",
            "jb2008_altitude": "WGS84 ellipsoidal height in km (project convention; upstream datum not uniquely specified)",
            "jb2008_latitude": "geocentric latitude; spacecraft and Sun share the Earth-fixed equator basis",
            "solar_and_geomagnetic": "official archive records at their published lags",
        },
        "atmospheric_outputs": {
            "density": "total mass density in kg/m^3",
            "temperature": "temperature at the requested altitude, when the model produces one",
            "exospheric_temperature": "exospheric temperature, when the model produces one",
            "constituents": "number density in m^-3, order He, O, N2, O2, Ar, H, N, anomalous O",
            "absent_outputs": "marked by has_temperatures / has_constituents, never by a zero",
        },
        "density_definitions": {
            "nrlmsise00": {
                "entry_point": "gtd7d",
                "output_index": "d[5]",
                "meaning": "drag-effective total mass density including anomalous oxygen, adapter output kg/m^3",
                "native_units_switch": 0,
                "native_density_units": "g/cm^3",
                "density_conversion_to_kg_m3": 1000,
                "constituent_conversion_cm3_to_m3": 1000000,
                "gtd7_vs_gtd7d": "gtd7 omits the anomalous oxygen contribution; gtd7d includes "
                                 "it. The drag-effective density here is the gtd7d value. Using "
                                 "gtd7 would systematically under-report the oxygen contribution "
                                 "at high altitude.",
                "altitude_domain_km": [0, 1000],
                "altitude_domain_note": (
                    "An explicit project-supported range, not a claimed published sharp "
                    "validity cutoff. An input outside it is rejected, never clamped: "
                    "clamping would silently evaluate a different altitude than requested."),
                "ap_domain": [0, 400],
                "f107_domain": "must be positive",
            },
            "jb2008": {
                "entry_point": "JB2008.f via the leo_jb2008 bridge",
                "meaning": "total mass density in kg/m^3",
                "mjd_utc_domain": [33282, 88069],
                "mjd_utc_domain_utc": ["1950-01-01", "2100-01-01"],
                "mjd_utc_domain_note": (
                    "An explicit engineering range, not a published model validity claim. "
                    "The guard exists because the model expands its calendar-dependent "
                    "coefficients and its century terms in a way that is only characterised "
                    "over this interval. An epoch outside it is rejected with a clear error "
                    "rather than extrapolated."),
                "altitude_domain_km": [120, 1500],
                "solar_proxy_domain": [40, None],
            },
            "not_comparable_by_construction": (
                "NRLMSISE-00 gtd7d total density and JB2008 total density are different model "
                "quantities: different constituent sets, different temperature definitions and "
                "different fitted upper-boundary conditions. Their ratio is a model-form "
                "difference and is reported as such; it is not evidence that either "
                "implementation is wrong, and neither is calibrated to the other."),
        },
        "space_weather_input_lags": {
            "nrlmsise00": NRLMSISE_INPUT_LAGS,
            "jb2008": JB2008_INPUT_LAGS,
            "note": "lags are configuration of the driver data, not derived here; they must "
                    "match the WeatherDataset that produced the results",
        },
        "scalar_configuration": {
            "frame": args.frame,
            "ap_history_enabled": bool(args.ap_history),
            "nrlmsise_switches": list(NRLMSISE_HISTORY_SWITCHES) if args.ap_history
            else list(NRLMSISE_STANDARD_SWITCHES),
            "nrlmsise_switch_meaning": {
                "0": "unit system: 1 selects kg/m^3, 0 selects g/cm^3",
                "9": "Ap selector: 1 uses daily Ap; -1 uses daily/current/-3/-6/-9 h "
                     "and the means of 12..33 h / 36..57 h preceding three-hourly bins",
            },
            "density_scale_per_case": "read from each CASE definition file by "
                                      "experiments/scenario.hpp; not overridden here",
        },
        "caching_policy": "no atmospheric caching, memoisation, interpolation, surrogate, "
                          "quantisation or trajectory precomputation is present in any measurement",
        "backends": backends,
        "modes": modes,
        "fixture_epochs": fixtures,
        "performance_settings": {
            "timing_calls_per_batch": args.timing_calls,
            "timing_batches": args.timing_repeats,
            "timing_propagation_repeats": args.timing_propagation_repeats,
            "thread_counts": [int(x) for x in args.timing_threads.split(",") if x.strip()],
        },
        "commands": commands,
        "evidence": sorted(str(p.relative_to(out)) for p in out.rglob("*") if p.is_file()),
    }

    with open(out / "manifest.json", "w") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")
    print(f"wrote {out / 'manifest.json'}", flush=True)


if __name__ == "__main__":
    main()