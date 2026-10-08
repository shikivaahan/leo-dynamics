#!/usr/bin/env python3
"""Build one driver against the original source or current tree, preserve raw runs.

Python standard library only. No downloads. Release timings: one warm-up and at
least seven measurements per configuration. Real SPICE requires explicit kernels.
--commit on an archive verifies its compilation inputs against the Git object.
Accuracy limits are fixed in ACCURACY.md and enforced by the driver. Rejected
intervals remain evidence, not runner failures; failed normal correctness checks
or mismatching unchanged references fail the run after all evidence is saved.
"""
import argparse
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys
import tarfile
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent.parent
FLAGS = "-O3 -DNDEBUG -Wall -Wextra -Wpedantic"
COLUMNS = ["timestamp", "git_commit", "source_id", "benchmark_id", "configuration_id",
           "run_number", "wall_seconds", "preprocess_seconds", "end_to_end_seconds",
           "rhs_count", "ephemeris_queries", "underlying_queries", "correctness", "checksum",
           "rhs_per_second", "queries_per_second", "nanoseconds_per_rhs", "metrics_json"]


def run(command, **kwargs):
    result = subprocess.run([str(x) for x in command], capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f"command failed: {command}\n{result.stdout!r}\n{result.stderr!r}")
    return result.stdout


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def inputs(source):
    paths = [source / "CMakeLists.txt"]
    for directory in ("include", "src"):
        paths.extend(p for p in (source / directory).rglob("*") if p.is_file())
    return {str(p.relative_to(source)): p.read_bytes() for p in sorted(paths)}


def tree_hash(files):
    digest = hashlib.sha256()
    for name, content in sorted(files.items()):
        digest.update(name.encode() + b"\0" + content + b"\0")
    return digest.hexdigest()


def identity(source, requested):
    files = inputs(source)
    commit = requested or ""
    if (source / ".git").exists():
        head = run(["git", "-C", source, "rev-parse", "HEAD"], text=True).strip()
        if requested:
            resolved = run(["git", "-C", source, "rev-parse", requested + "^{commit}"], text=True).strip()
            if resolved != head:
                raise ValueError("working-tree --commit must identify HEAD, not a different revision")
        commit = head
    elif requested:
        commit = run(["git", "-C", ROOT, "rev-parse", requested + "^{commit}"], text=True).strip()
        archive = run(["git", "-C", ROOT, "archive", commit])
        expected = {}
        with tarfile.open(fileobj=io.BytesIO(archive)) as tree:
            for member in tree:
                if member.isfile() and (member.name == "CMakeLists.txt" or member.name.startswith(("src/", "include/"))):
                    expected[member.name] = tree.extractfile(member).read()
        if files != expected:
            raise ValueError("archived compilation inputs do not match requested commit")
    digest = tree_hash(files)
    return {"git_commit": commit, "source_id": (commit[:12] + "+" if commit else "") + "tree:" + digest[:16],
            "source_sha256": digest, "source_path": str(source)}


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def cache_value(cache, key):
    match = re.search(r"^" + re.escape(key) + r":[^=]+=(.*)$", cache, re.M)
    return match.group(1) if match else ""


def dependency_versions(cache):
    versions = {}
    for name, key, version_file in (("Eigen", "Eigen3_DIR", "Eigen3ConfigVersion.cmake"),
                                     ("Boost", "Boost_DIR", "BoostConfigVersion.cmake")):
        directory = cache_value(cache, key)
        path = Path(directory) / version_file
        match = re.search(r'set\(PACKAGE_VERSION "([^"]+)"', path.read_text()) if path.is_file() else None
        versions[name] = {"version": match.group(1) if match else None, "directory": directory}
    library = cache_value(cache, "CSPICE_LIBRARY")
    versions["CSPICE"] = {"library": library, "library_sha256": sha(library) if library else None}
    versions["CMake"] = run(["cmake", "--version"], text=True).splitlines()[0]
    return versions


def stats(values):
    return {"median": statistics.median(values), "min": min(values), "max": max(values)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, default=ROOT)
    p.add_argument("--build", type=Path, default=ROOT / "build-bench")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--label", required=True)
    p.add_argument("--mode", choices=["baseline", "candidate"], default="baseline")
    p.add_argument("--repetitions", type=int, default=7)
    p.add_argument("--warmup", type=int, default=1)
    p.add_argument("--intervals", type=float, nargs="+", default=[60, 300, 900, 1800])
    p.add_argument("--duration", type=float, default=21600)
    p.add_argument("--spk", type=Path)
    p.add_argument("--lsk", type=Path)
    p.add_argument("--cspice-root", type=Path)
    p.add_argument("--commit")
    p.add_argument("--reference", type=Path, help="original evidence/timings.json")
    p.add_argument("--evidence-dir", type=Path)
    p.add_argument("--compiler", default="c++")
    p.add_argument("--jobs", type=int, default=4)
    a = p.parse_args()
    if a.repetitions < 7 or a.warmup < 1 or a.jobs < 1:
        p.error("require >=7 repetitions, >=1 warm-up, and positive build jobs")
    if any(not math.isfinite(x) or x <= 0 for x in [a.duration] + a.intervals):
        p.error("duration/intervals must be finite and positive")
    if bool(a.spk) != bool(a.lsk) or (a.mode == "candidate" and not a.spk):
        p.error("supply both SPK and LSK; candidate evaluation requires real SPICE")
    source, build, output = a.source.resolve(), a.build.resolve(), a.output.resolve()
    evidence = (a.evidence_dir or output.parent / "evidence" / a.label).resolve()
    if evidence.exists() and any(evidence.iterdir()):
        p.error("evidence directory is nonempty; use a new directory to preserve existing evidence")
    if output.exists():
        with output.open() as stream:
            if any(row["configuration_id"].startswith(a.label + ":") for row in csv.DictReader(stream)):
                p.error("configuration label already exists in CSV; use a new label")
    revision = identity(source, a.commit)
    cases = ROOT / "benchmarks" / "cases"
    configure = ["cmake", "-S", cases, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
                 "-DCMAKE_CXX_FLAGS=", "-DCMAKE_CXX_FLAGS_RELEASE=" + FLAGS,
                 "-DCMAKE_CXX_COMPILER=" + a.compiler, "-DLEO_SOURCE_DIR=" + str(source),
                 "-DLEO_ENABLE_CSPICE=" + ("ON" if a.spk else "OFF")]
    if a.spk:
        spice_root = a.cspice_root or os.environ.get("CSPICE_ROOT")
        if spice_root:
            configure.append("-DCSPICE_ROOT=" + str(spice_root))
    print("Building:", " ".join(map(str, configure)), file=sys.stderr)
    run(configure)
    run(["cmake", "--build", build, "--target", "leo_bench", "--parallel", a.jobs])
    driver = build / "leo_bench"
    cache = (build / "CMakeCache.txt").read_text()
    compiler = cache_value(cache, "CMAKE_CXX_COMPILER")
    cpuinfo = Path("/proc/cpuinfo").read_text() if Path("/proc/cpuinfo").exists() else ""
    cpu = re.search(r"model name\s*:\s*(.*)", cpuinfo)
    metadata = {"timestamp": datetime.now(timezone.utc).isoformat(), "revision": revision,
                "driver_source_sha256": tree_hash({str(f.relative_to(cases)): f.read_bytes() for f in cases.rglob("*") if f.is_file()}),
                "driver_binary_sha256": sha(driver), "cpu": cpu.group(1) if cpu else platform.processor(),
                "os": platform.platform(), "compiler": run([compiler, "--version"], text=True).splitlines()[0],
                "flags": FLAGS, "configure_command": list(map(str, configure)), "dependencies": dependency_versions(cache),
                "repetitions": a.repetitions, "warmup": a.warmup, "duration_s": a.duration,
                "output_cadence_s": 30, "intervals_s": a.intervals,
                "kernels": {"spk": {"path": str(a.spk.resolve()), "sha256": sha(a.spk)},
                            "lsk": {"path": str(a.lsk.resolve()), "sha256": sha(a.lsk)}} if a.spk else {}}
    save(evidence / "metadata.json", metadata)
    plan = [("BENCH-01", "deterministic", 30), ("BENCH-02", "deterministic", 30), ("BENCH-03", "deterministic", 30)]
    if a.spk:
        plan += [("BENCH-03", "direct", 30), ("QUERY", "direct", 30)]
    if a.mode == "candidate":
        plan += [(case, "cached", interval) for interval in a.intervals for case in ("BENCH-03", "QUERY", "ACCURACY")]
    summaries, rows, failures = {}, [], []
    reference = json.loads(a.reference.read_text()) if a.reference else {}
    for case, mode, interval in plan:
        group = f"{case}|{mode}|{interval:g}"
        payloads = []
        for i in range(a.warmup + a.repetitions):
            command = [driver, "--case", case, "--mode", mode, "--interval", interval, "--duration", a.duration]
            if a.spk:
                command += ["--spk", a.spk.resolve(), "--lsk", a.lsk.resolve()]
            payload = json.loads(run(command, text=True))
            for field in ("wall_seconds", "preprocess_seconds", "rhs_count", "ephemeris_queries", "underlying_queries", "checksum", "correctness", "reference"):
                if field not in payload:
                    raise ValueError("driver missing " + field)
            warmup = i < a.warmup
            number = i + 1 if warmup else i - a.warmup + 1
            save(evidence / ("warmup" if warmup else "raw") / f"{group.replace('|', '_')}_{number}.json", payload)
            print(f"{group} {'warmup' if warmup else number}: {payload['wall_seconds']:.6f}s correctness={payload['correctness']}", file=sys.stderr)
            if warmup:
                continue
            payloads.append(payload)
            wall, pre = payload["wall_seconds"], payload["preprocess_seconds"]
            rows.append(dict(timestamp=datetime.now(timezone.utc).isoformat(), git_commit=revision["git_commit"],
                             source_id=revision["source_id"], benchmark_id=case, configuration_id=a.label + ":" + group,
                             run_number=number, wall_seconds=wall, preprocess_seconds=pre, end_to_end_seconds=wall + pre,
                             rhs_count=payload["rhs_count"], ephemeris_queries=payload["ephemeris_queries"], underlying_queries=payload["underlying_queries"],
                             correctness=payload["correctness"], checksum=payload["checksum"],
                             rhs_per_second=payload["rhs_count"] / wall, queries_per_second=payload["ephemeris_queries"] / wall,
                             nanoseconds_per_rhs=wall * 1e9 / payload["rhs_count"] if payload["rhs_count"] else "",
                             metrics_json=json.dumps(payload, separators=(",", ":"))))
        s = {metric: stats([r[metric] for r in payloads]) for metric in ("wall_seconds", "preprocess_seconds", "rhs_count", "ephemeris_queries", "underlying_queries")}
        s["end_to_end_seconds"] = stats([r["wall_seconds"] + r["preprocess_seconds"] for r in payloads])
        s["reference"], s["checksum"] = payloads[0]["reference"], payloads[0]["checksum"]
        s["correctness"] = all(r["correctness"] for r in payloads)
        s["deterministic"] = len({r["checksum"] for r in payloads}) == 1
        old = reference.get(group)
        if old:
            s["baseline_speedup"] = old["wall_seconds"]["median"] / s["wall_seconds"]["median"]
            s["matches_baseline_reference"] = old["reference"] == s["reference"] and old["checksum"] == s["checksum"]
            if not s["matches_baseline_reference"]:
                failures.append(group + " differs from unchanged original numerical reference")
        if not s["deterministic"] or (case != "ACCURACY" and not s["correctness"]):
            failures.append(group + " failed correctness/determinism")
        summaries[group] = s
    # A finite trajectory is not proof of agreement with direct SPICE. Link each
    # cached propagation's correctness status to its separate accuracy sweep.
    acceptance = {}
    for interval in a.intervals if a.mode == "candidate" else []:
        accuracy_group = f"ACCURACY|cached|{interval:g}"
        propagation_group = f"BENCH-03|cached|{interval:g}"
        accepted = summaries[accuracy_group]["correctness"]
        acceptance[propagation_group] = accepted
        summaries[propagation_group]["finite_correctness"] = summaries[propagation_group]["correctness"]
        summaries[propagation_group]["accuracy_accepted"] = accepted
        summaries[propagation_group]["correctness"] = accepted
        for row in rows:
            if row["configuration_id"] == a.label + ":" + propagation_group:
                row["correctness"] = accepted
    save(evidence / "accuracy.json", acceptance)
    if a.spk:
        command = [driver, "--case", "BENCH-03", "--mode", "direct", "--duration", a.duration,
                   "--interval", 30, "--spk", a.spk.resolve(), "--lsk", a.lsk.resolve(), "--profile"]
        profile = json.loads(run(command, text=True))
        profile["limitation"] = "Clock reads and ET/wall-gap vector writes add overhead. Query timer includes provider/mutex/error/SI conversion, not only spkez_c. Normal timings have no per-query clocks."
        save(evidence / "profile.json", profile)
    save(evidence / "timings.json", summaries)
    save(evidence / "comparison.json", {"reference": str(a.reference), "failures": failures})
    output.parent.mkdir(parents=True, exist_ok=True)
    existed = output.exists()
    with output.open("a", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=COLUMNS)
        if not existed:
            writer.writeheader()
        writer.writerows(rows)
    for group, s in summaries.items():
        t = s["wall_seconds"]
        print(f"{group}: median={t['median']:.6f}s min={t['min']:.6f}s max={t['max']:.6f}s pass={s['correctness']}")
    if failures:
        raise RuntimeError("; ".join(failures))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError) as error:
        print("benchmark error:", error, file=sys.stderr)
        sys.exit(1)
