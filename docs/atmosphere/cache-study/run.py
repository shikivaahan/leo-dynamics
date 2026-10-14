#!/usr/bin/env python3
"""Drive the ATM-CACHE-001 atmospheric-density cache study.

Python standard library only. The runner owns scheduling, provenance and
bookkeeping; it never computes a scientific conclusion. Every task is one
independent ``atm_cache_study`` process that writes a single JSON result, which
the runner copies verbatim into ``results/`` and then aggregates mechanically
by study kind.

Reliability properties (all exercised by tests/cache_study_runner_tests.py):

  * deterministic task plans: the same configuration and seed always produce
    the same task ids, commands and order,
  * atomic publication: a result appears in ``results/`` only after the child
    exited zero and its JSON parsed, through ``os.replace`` on a temporary file,
  * a checkpointed ledger (``ledger.json``) records every task, its exact
    command line, its status and the SHA-256 of its published result,
  * ``--resume`` re-verifies the configuration, source, binary and input-data
    hashes and refuses to continue on any difference; completed tasks whose
    published result is missing or whose hash no longer matches are re-run,
    never silently trusted,
  * ``--max-hours`` stops scheduling new work and keeps everything completed;
    pending work stays honestly pending,
  * SIGINT/SIGTERM terminate and reap every started child, mark it pending
    again and still write the ledger, summaries and manifest,
  * scientific sweeps run concurrently, while benchmark groups are serialised
    and are only started when no other child is alive, so timing is not
    contaminated by a sibling sweep.

Usage examples:

  python3 docs/atmosphere/cache-study/run.py --mode smoke --workers 4 --out /tmp/cache-smoke
  python3 docs/atmosphere/cache-study/run.py --mode full --workers 8 --max-hours 8 --out /tmp/cache-study
  python3 docs/atmosphere/cache-study/run.py --resume /tmp/cache-study
"""
from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import os
import platform
import re
import shlex
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent

LEDGER_SCHEMA_VERSION = 1
# A study result larger than this is referenced by path and hash in the compact
# summaries instead of being copied into them; the verbatim copy always stays in
# results/.
SUMMARY_INLINE_BYTES = 2 << 20

EXIT_OK = 0
EXIT_USAGE = 2
EXIT_INCOMPLETE = 3
# Primary inputs missing or not matching their pinned hash: every primary task
# is recorded blocked and pending, nothing is executed, nothing is re-pinned.
EXIT_BLOCKED = 4
EXIT_INTERRUPTED = 130

# Defaults are named so a resume can recognise that the caller did not override
# them and can restore the build tree and dependency root the run actually used.
DEFAULT_BUILD_DIR = Path("/tmp/leo-cache-study")
DEFAULT_DEPS_DIR = Path("/tmp/leo-atmosphere-deps")

# Fixture epochs. The first five are the frozen ATM-CACHE-001 selection (quiet,
# quiet winter, moderate, active and storm); the boundary epochs exercise the
# named discontinuities rather than inventing weather.
FIXTURE_EPOCHS = [
    ("quiet_june", "2008-07-01T00:00:00"),
    ("quiet_december", "2008-12-15T00:00:00"),
    ("moderate_march", "2001-03-20T00:00:00"),
    ("active_october", "2003-10-28T00:00:00"),
    ("storm_october", "2003-10-30T00:00:00"),
    ("leap_before", "2016-12-31T23:59:59"),
    ("leap_after", "2017-01-01T00:00:00"),
    ("ap_bin_boundary", "2003-10-30T03:00:00"),
    ("dtc_hour_boundary", "2003-10-30T15:30:00"),
]

CASE_FILES = {
    "case-a": "docs/numerical-audit/experiments/CASE-A-v1.cfg",
    "case-b": "docs/numerical-audit/experiments/CASE-B-v1.cfg",
    "case-c": "docs/numerical-audit/experiments/CASE-C-v1.cfg",
}

HOUR = 3600.0
SIX_HOURS = 6 * HOUR
DAY = 24 * HOUR

# Coarse-to-fine exploration: steep altitude gradients dominate the error, so
# altitude spacing is refined first, then the horizontal and temporal spacing.
COARSE_TO_FINE = [
    (25.0, 5.0, 60.0),
    (10.0, 2.0, 10.0),
    (5.0, 1.0, 2.0),
]

# Candidate names agreed with the study executable owner: A|exact is the
# zero-approximation reference, B|cell and B|cells are the nearest-neighbour cell
# cache at single and multiple resolution, C|interp is log-density interpolation,
# D|adaptive the adaptive-resolution policy and E|control the preprocessing-only
# control that isolates ATM-OPT-001 reuse from density caching. The control is
# also the performance baseline: it is the current exact, fully optimised direct
# provider with no density cache.
CANDIDATE_ORDER = ["control", "exact", "cell", "cells", "interp", "adaptive"]
BENCHMARK_BASELINE = "control"
# Candidates whose accuracy depends on a cell/interpolation resolution grid.
RESOLVED_CANDIDATES = ("cell", "cells", "interp", "adaptive")
# A candidate's cell or interpolation spacing is a distinct argument from the
# native probe altitude (--study-h-km), so a screening result can never be read
# as a probe altitude and a propagated candidate is always the same
# configuration that was screened.
RESOLUTION_FLAGS = {"cell_km": "--study-cell-km",
                    "cell_deg": "--study-cell-deg",
                    "cell_s": "--study-cell-s"}
RESOLUTION_KEYS = ("cell_km", "cell_deg", "cell_s", "capacity")
# The policy knobs are part of the configuration identity, so a screened
# interpolation axis, grid axis or replacement policy is never confused with a
# different one.
POLICY_KEYS = ("interpolation", "interpolation_axis", "capacity_policy")

# The five pinned ATM-CACHE-001 epochs are always planned; the boundary epochs
# cover the named discontinuities (leap second, three-hourly Ap bin, hourly DTC
# record) and are opted into explicitly so a default run is exactly the pinned set.
PINNED_FIXTURES = [item for item in FIXTURE_EPOCHS if item[0] in
                  ("quiet_june", "quiet_december", "moderate_march", "active_october",
                   "storm_october")]
BOUNDARY_FIXTURES = [item for item in FIXTURE_EPOCHS if item not in PINNED_FIXTURES]

SUMMARY_FILES = {
    "sensitivity": "sensitivity-summary.json",
    "locality": "locality-summary.json",
    "accuracy": "accuracy-summary.json",
    "propagation": "propagation-summary.json",
    "benchmark": "performance-summary.json",
}
# Phase 9 names four summaries; locality and check keep their own so an
# incomplete locality sweep can never be hidden inside a sensitivity summary.
SUMMARY_FILES.update({"check": "check-summary.json"})

KIND_ALIASES = {"performance": "benchmark", "bench": "benchmark", "coupled": "propagation"}


class RunnerError(RuntimeError):
    """A configuration, provenance or aggregation refusal."""


# ---------------------------------------------------------------------------
# Small utilities
# ---------------------------------------------------------------------------


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def sha256_file(path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def canonical_json(value) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def write_json_atomic(path, payload) -> None:
    """Publish JSON through a temporary file and one atomic replace."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f"{path.name}.tmp-{os.getpid()}")
    with temporary.open("w") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def number(value) -> str:
    """Format a number for a command line without losing round-trip precision."""
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, int):
        return str(value)
    return repr(float(value))


def git(*args):
    result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def hash_paths(paths) -> dict:
    """Hash every existing input file; a missing required input is a refusal."""
    described = {}
    for name, value in sorted(paths.items()):
        if value is None:
            described[name] = None
            continue
        path = Path(value)
        if not path.is_file():
            raise RunnerError(f"required input missing: {name} = {path}")
        described[name] = {"path": str(path.resolve()), "sha256": sha256_file(path),
                           "bytes": path.stat().st_size}
    return described


def _load_mt002():
    """Import the MT-002 batch orchestration instead of reimplementing it.

    The study's process-isolated workload must reproduce the measured
    orchestration: contiguous balanced ranges and the child lifecycle both come
    from docs/multithreading/process_batch.py, not from a local copy.
    """
    import importlib.util
    path = ROOT / "docs/multithreading/process_batch.py"
    if not path.is_file():
        raise RunnerError(f"MT-002 orchestration helper is missing: {path}")
    spec = importlib.util.spec_from_file_location("leo_mt002_process_batch", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


MT002 = _load_mt002()
split_ranges = MT002.split_ranges


def recorded_partition(ledger, group_id, total, workers):
    """The partition a group was planned with, so a resume never moves its ranges."""
    entry = ledger.data["tasks"].get(group_id) or {}
    recorded = (entry.get("identity") or {}).get("params", {}).get("partition")
    if isinstance(recorded, list) and recorded:
        parts = [(int(begin), int(count)) for begin, count in recorded]
        if sum(count for _, count in parts) == total and all(count > 0 for _, count in parts):
            return parts
    return split_ranges(total, workers)


# ---------------------------------------------------------------------------
# Task model
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class Task:
    task_id: str
    stage: str            # planning stage, recorded for provenance
    kind: str             # study kind dispatched to the executable
    serial: bool          # benchmark work that must not share the machine
    commands: list        # list of argv lists; usually one child
    params: dict          # machine-readable parameters, also hashed into the id
    out_dir: str          # relative directory handed to the child as --out
    model: str | None = None
    candidate: str | None = None
    target: str | None = None
    label: str | None = None
    parallel_children: bool = False   # children of an exclusive group run together
    deferred: str | None = None       # reason this task must not be executed yet

    def identity(self) -> dict:
        return {"task_id": self.task_id, "stage": self.stage, "kind": self.kind,
                "serial": self.serial, "commands": self.commands,
                "params": self.params, "out_dir": self.out_dir,
                "model": self.model, "candidate": self.candidate,
                "target": self.target, "label": self.label,
                "parallel_children": self.parallel_children,
                "deferred": self.deferred}


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class RunConfig:
    mode: str
    out: Path
    binary: Path
    build_dir: Path | None
    deps_dir: Path | None
    lsk: Path
    spk: Path
    pck: Path | None
    frame: str
    weather: Path
    solfsmy: Path
    dtcfile: Path
    epoch_utc: str
    workers: int
    max_hours: float
    seed: int
    scenarios: tuple
    models: tuple
    kinds: tuple
    candidates: tuple
    targets: tuple
    capacities: tuple
    points: int
    duration_s: float
    cadence_s: float
    rtol: float
    max_step_s: float
    refine_rtol: float
    refine_max_step_s: float
    repeats: int
    max_shortlist: int
    shortlist_rel_error_percent: float
    extra_args: tuple
    note: str | None
    cmake_prefix_path: str | None
    cspice_root: Path | None
    nrlmsise_root: Path | None
    jb2008_source: Path | None
    build_jobs: int
    include_boundary_epochs: bool
    verification_only: bool
    expected_hashes: dict
    interpolations: tuple
    interpolation_axis: str
    capacity_policy: str

    # Everything below participates in the configuration hash that a resume must
    # reproduce. Worker count and wall-clock budget deliberately do not: raising
    # the worker count or extending the budget on a resumed run is legitimate.
    def plan_parameters(self) -> dict:
        return {
            "mode": self.mode,
            "verification_only": self.verification_only,
            "expected_hashes": dict(sorted(self.expected_hashes.items())),
            "include_boundary_epochs": self.include_boundary_epochs,
            "seed": self.seed,
            "scenarios": list(self.scenarios),
            "models": list(self.models),
            "kinds": list(self.kinds),
            "candidates": list(self.candidates),
            "targets": list(self.targets),
            "capacities": list(self.capacities),
            "interpolations": list(self.interpolations),
            "interpolation_axis": self.interpolation_axis,
            "capacity_policy": self.capacity_policy,
            "points": self.points,
            "duration_s": self.duration_s,
            "cadence_s": self.cadence_s,
            "rtol": self.rtol,
            "max_step_s": self.max_step_s,
            "refine_rtol": self.refine_rtol,
            "refine_max_step_s": self.refine_max_step_s,
            "repeats": self.repeats,
            "max_shortlist": self.max_shortlist,
            "shortlist_rel_error_percent": self.shortlist_rel_error_percent,
            "epoch_utc": self.epoch_utc,
            "frame": self.frame,
            "extra_args": list(self.extra_args),
            "fixture_epochs": [list(item) for item in FIXTURE_EPOCHS],
        }

    def identity(self) -> dict:
        return {
            "config_sha256": sha256_text(canonical_json(self.plan_parameters())),
            "binary": describe_file(self.binary),
            "build": {"build_dir": str(self.build_dir) if self.build_dir else None,
                      "deps_dir": str(self.deps_dir) if self.deps_dir else None},
            "study_sources": hash_study_sources(),
            "runner": describe_file(Path(__file__).resolve()),
            "inputs": hash_inputs(self),
        }


def describe_file(path) -> dict:
    path = Path(path)
    if not path.is_file():
        return {"path": str(path), "present": False}
    return {"path": str(path.resolve()), "sha256": sha256_file(path),
            "bytes": path.stat().st_size, "present": True}


def hash_study_sources() -> dict:
    """Hash the experimental study sources; they define the measured candidate."""
    sources = {}
    for pattern in ("*.cpp", "*.hpp", "*.py", "CMakeLists.txt"):
        for path in sorted(HERE.glob(pattern)):
            sources[str(path.relative_to(HERE))] = sha256_file(path)
    return sources


def hash_inputs(cfg: RunConfig) -> dict:
    """Describe every scientific input, present or not.

    A missing input is described rather than fatal: the primary-run gate below
    turns it into a blocked, fully recorded plan instead of an exception, so the
    pending state of the study survives.
    """
    inputs = {}
    for name, value in (("lsk", cfg.lsk), ("spk", cfg.spk), ("pck", cfg.pck),
                        ("weather", cfg.weather), ("solfsmy", cfg.solfsmy),
                        ("dtcfile", cfg.dtcfile)):
        if value is None:
            inputs[name] = {"path": None, "present": False, "sha256": None}
        else:
            inputs[name] = describe_file(value)
    inputs["cases"] = {name: describe_file(ROOT / relative)
                       for name, relative in sorted(CASE_FILES.items())}
    return inputs


def parse_expectations(entries, pinned_file) -> dict:
    """Collect the expected SHA-256 of every pinned scientific input.

    The runner never re-pins a dataset: the caller states the hashes of the
    pinned kernels, space-weather archive and CASE definitions, and a primary
    smoke, screen or full run refuses to start unless every stated hash matches.
    """
    baseline = json.loads((ROOT / "docs/atmosphere/evidence/direct-v2/manifest.json").read_text())
    expectations = {
        "lsk": baseline["kernels"]["lsk"]["sha256"],
        "spk": baseline["kernels"]["spk"]["sha256"],
        "pck": baseline["kernels"]["pck"]["sha256"],
        "weather": baseline["space_weather"]["archive"]["sha256"],
        "solfsmy": baseline["space_weather"]["solfsmy"]["sha256"],
        "dtcfile": baseline["space_weather"]["dtcfile"]["sha256"],
    }
    for case in baseline["scenarios"]["cases"]:
        expectations[Path(case["path"]).stem.split("-v1")[0].lower()] = case["sha256"]
    if pinned_file:
        path = Path(pinned_file)
        if not path.is_file():
            raise SystemExit(f"--pinned-input-hashes file not found: {path}")
        data = json.loads(path.read_text())
        for name, value in (data.get("inputs") or data).items():
            if isinstance(value, dict):
                value = value.get("sha256")
            if value:
                expectations[str(name)] = str(value).lower()
    for entry in entries or []:
        name, _, digest = str(entry).partition("=")
        name = name.strip().lower()
        digest = digest.strip().lower()
        if not name or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise SystemExit(f"--expect-sha256 expects NAME=64-hex-digest, got: {entry}")
        expectations[name] = digest
    return expectations


REQUIRED_INPUTS = ("lsk", "spk", "pck", "weather", "solfsmy", "dtcfile")


def verify_pinned_inputs(cfg: RunConfig) -> dict:
    """Enforce the pinned input hashes before any scientific task is planned.

    A missing input or a hash that does not match blocks the primary study: the
    runner plans nothing to execute, records every task as blocked and pending,
    and never substitutes another file for the pinned one.
    """
    observed = hash_inputs(cfg)
    expectations = cfg.expected_hashes
    missing = []
    mismatched = []
    unverified = []
    problems = []
    for name in REQUIRED_INPUTS:
        entry = observed.get(name) or {}
        expected = expectations.get(name)
        if not entry.get("present"):
            missing.append({"input": name, "path": entry.get("path"),
                            "expected_sha256": expected})
            problems.append(f"{name}: no input file ({entry.get('path') or 'not supplied'})")
        elif expected is None:
            unverified.append(name)
            problems.append(f"{name}: no pinned hash")
        elif entry.get("sha256") != expected:
            mismatched.append({"input": name, "path": entry["path"],
                               "expected_sha256": expected, "observed_sha256": entry["sha256"]})
            problems.append(f"{name}: expected {expected}, found {entry['sha256']} ({entry['path']})")
    for name, entry in sorted((observed.get("cases") or {}).items()):
        expected = expectations.get(name)
        if not entry.get("present"):
            missing.append({"input": name, "path": entry.get("path"), "expected_sha256": expected})
            problems.append(f"{name}: case definition missing ({entry.get('path')})")
        elif expected and entry.get("sha256") != expected:
            mismatched.append({"input": name, "path": entry["path"],
                               "expected_sha256": expected,
                               "observed_sha256": entry.get("sha256")})
            problems.append(f"{name}: expected {expected}, found {entry.get('sha256')}")
    if not expectations:
        problems.append("no pinned input hashes were supplied: pass --expect-sha256 "
                        "NAME=SHA256 (repeatable) or --pinned-input-hashes FILE")

    blocked = bool(problems) and not cfg.verification_only
    report = {
        "pinned_hashes": dict(sorted(expectations.items())),
        "observed": observed,
        "missing_inputs": missing,
        "mismatched_inputs": mismatched,
        "inputs_without_a_pinned_hash": sorted(unverified),
        "problems": problems,
        "blocked": blocked,
        "verification_only": cfg.verification_only,
    }
    report["outcome"] = (
        "refused: a pinned input is missing or does not match its expected hash; every "
        "primary task is recorded blocked and pending, no input was replaced"
        if blocked else
        ("verified: every supplied pinned hash matches" if not problems else
         "verification-only: pinned inputs are missing or differ, so the outputs verify "
         "the pipeline and are never scientific or performance evidence"))
    return report


def toolchain_identity(build_dir) -> dict:
    """Read the exact build configuration out of CMakeCache.txt; assume nothing."""
    entry = {"build_dir": str(build_dir) if build_dir else None}
    if not build_dir:
        return entry
    cache = Path(build_dir) / "CMakeCache.txt"
    if not cache.is_file():
        entry["cache_present"] = False
        return entry
    entry["cache_present"] = True
    wanted = {}
    for line in cache.read_text().splitlines():
        match = re.match(r"^([A-Za-z0-9_]+):[A-Z]+=(.*)$", line)
        if not match:
            continue
        key, value = match.group(1), match.group(2)
        if key in ("CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER",
                   "CMAKE_Fortran_COMPILER", "CSPICE_ROOT", "CSPICE_LIBRARY",
                   "LEO_SOURCE_DIR", "LEO_NRLMSISE00_ROOT", "LEO_JB2008_SOURCE",
                   "CMAKE_PREFIX_PATH", "Eigen3_DIR", "Boost_INCLUDE_DIR"):
            wanted[key] = value
        elif re.match(r"^CMAKE_(C|CXX|Fortran)_FLAGS(_[A-Z]+)?$", key) and value:
            wanted[key] = value
    entry["cache"] = wanted
    return entry


def host_identity() -> dict:
    entry = {"platform": platform.platform(), "machine": platform.machine(),
             "processor": platform.processor(), "logical_cores": os.cpu_count(),
             "affinity_cpus": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
             "python": sys.version.split()[0]}
    try:
        text = Path("/proc/cpuinfo").read_text()
        entry["model_names"] = sorted({line.split(":", 1)[1].strip()
                                       for line in text.splitlines() if line.startswith("model name")})
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
    entry["threading_environment"] = {name: os.environ.get(name) for name in
                                      ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS",
                                       "OMP_PROC_BIND", "OMP_PLACES")}
    return entry


def build_commands(cfg: RunConfig) -> list[str]:
    """The explicit standalone-CMake configure and build commands.

    Recorded in the manifest and printed by --show-build-commands; this runner
    never executes a build. The root is the cache-study CMake root, configured
    separately exactly like the ATM-OPT-001 and profiling roots.
    """
    build_dir = cfg.build_dir or (ROOT / "build-cache-study")
    command = ["cmake", "-S", str(HERE), "-B", str(build_dir),
               "-DCMAKE_BUILD_TYPE=Release"]
    if cfg.cmake_prefix_path:
        command += [f"-DCMAKE_PREFIX_PATH={cfg.cmake_prefix_path}"]
    if cfg.cspice_root:
        command += [f"-DCSPICE_ROOT={cfg.cspice_root}"]
    if cfg.nrlmsise_root:
        command += [f"-DLEO_NRLMSISE00_ROOT={cfg.nrlmsise_root}"]
    if cfg.jb2008_source:
        command += [f"-DLEO_JB2008_SOURCE={cfg.jb2008_source}"]
    command += [f"-DLEO_SOURCE_DIR={ROOT}"]
    return [" ".join(shlex.quote(part) for part in command),
            " ".join(shlex.quote(part) for part in
                     ["cmake", "--build", str(build_dir), "--target", "atm_cache_study",
                      "-j", str(cfg.build_jobs)])]


# ---------------------------------------------------------------------------
# Planning
# ---------------------------------------------------------------------------


def slug(text: str) -> str:
    return re.sub(r"[^0-9A-Za-z]+", "-", str(text)).strip("-").lower()


def number_slug(value) -> str:
    """A short, stable identifier fragment for a numeric parameter."""
    if value is None:
        return "na"
    text = ("%g" % float(value)).replace(".", "p").replace("-", "m").replace("+", "")
    return text


def accuracy_candidate_grid(cfg: RunConfig) -> list[tuple[str, dict]]:
    """Coarse-to-fine candidate/parameter combinations to screen.

    Every combination carries its own identity, so a configuration is only ever
    compared against itself across models and weather epochs.
    """
    requested = [c for c in cfg.candidates]
    grid: list[tuple[str, dict]] = []
    capacities = list(cfg.capacities)
    resolutions = COARSE_TO_FINE
    if cfg.mode == "smoke":
        resolutions = COARSE_TO_FINE[:1]
        capacities = capacities[:1]
    elif cfg.mode == "screen":
        resolutions = COARSE_TO_FINE[:2]
        capacities = capacities[:1]
    policy = {"capacity_policy": cfg.capacity_policy, "interpolation_axis": cfg.interpolation_axis}
    for candidate in requested:
        if candidate not in RESOLVED_CANDIDATES:
            # The control and the exact reference have no resolution grid; the
            # capacity is recorded but does not change what they measure.
            grid.append((candidate, {"capacity": capacities[0] if capacities else 1, **policy}))
            continue
        axes = cfg.interpolations if candidate == "interp" else cfg.interpolations[:1]
        for cell_km, cell_deg, cell_s in resolutions:
            for capacity in capacities:
                for axis in axes:
                    grid.append((candidate, {"cell_km": cell_km, "cell_deg": cell_deg,
                                             "cell_s": cell_s, "capacity": capacity,
                                             "interpolation": axis, **policy}))
    return grid


def locality_plan_sizes(cfg: RunConfig) -> tuple[tuple, tuple, tuple]:
    if cfg.mode == "smoke":
        return (1, 2), (0.0, 10.0), (1,)
    if cfg.mode == "screen":
        return (1, 4), (0.0, 1.0, 10.0), (1, 4)
    return (1, 4, 16), (0.0, 1.0, 10.0), (1, 4)


def weather_groups(cfg: RunConfig) -> list[tuple]:
    """Weather configurations; Ap history is a separate configuration, never mixed.

    Returns one tuple per (model, weather configuration): the model, the
    configuration name, its fixture epochs, whether the three-hourly Ap history
    switch is set, and the fixture names the accuracy stage screens over.
    """
    by_name = {name: utc for name, utc in FIXTURE_EPOCHS}
    selected = list(PINNED_FIXTURES)
    if cfg.include_boundary_epochs:
        selected += [item for item in BOUNDARY_FIXTURES if item[0] in by_name]
    if cfg.mode == "smoke":
        epochs = [item for item in selected if item[0] in ("quiet_june", "storm_october")]
        accuracy_epochs = ["quiet_june", "storm_october"]
        history_epochs = [item for item in epochs if item[0] == "storm_october"]
    else:
        epochs = selected
        accuracy_epochs = ["quiet_june", "moderate_march", "storm_october"]
        history_epochs = [item for item in epochs if item[0] == "storm_october"]
    groups = []
    for model in cfg.models:
        if model == "nrlmsise00":
            groups.append((model, "daily", epochs, False, accuracy_epochs))
            groups.append((model, "history", history_epochs, True, accuracy_epochs))
        else:
            groups.append((model, "standard", epochs, False, accuracy_epochs))
    return groups


def base_command(cfg: RunConfig, out_dir: str, fixtures, case_names, epoch_utc: str,
                 duration: float, ap_history: bool) -> list:
    command = [str(cfg.binary)]
    for flag, value in (("--lsk", cfg.lsk), ("--spk", cfg.spk), ("--pck", cfg.pck),
                        ("--frame", cfg.frame), ("--weather", cfg.weather),
                        ("--solfsmy", cfg.solfsmy), ("--dtcfile", cfg.dtcfile),
                        ("--epoch-utc", epoch_utc)):
        if value:
            command += [flag, str(value)]
    # Absolute paths: a child must never depend on the runner's working directory.
    command += ["--out", str(cfg.out / out_dir)]
    for name in case_names:
        command += ["--case", str(ROOT / CASE_FILES[name])]
    for name, utc in fixtures:
        command += ["--fixture", f"{name}={utc}"]
    command += ["--duration", number(duration), "--cadence", number(cfg.cadence_s),
                "--rtol", number(cfg.rtol), "--max-step", number(cfg.max_step_s)]
    if ap_history:
        command.append("--ap-history")
    if cfg.verification_only:
        command += ["--study-verification-only", "on", "explicit nonprimary pipeline verification"]
    return command


def study_arguments(cfg: RunConfig, params: dict) -> list:
    """Render the StudySettings scalar surface as command-line arguments.

    ``--study-h-km`` is the native probe altitude used by the sensitivity and
    locality probes. A candidate's cell or interpolation spacing travels in its
    own arguments so the two meanings can never be confused.
    """
    command = []
    if params.get("kind"):
        command += ["--study-kind", params["kind"]]
    if params.get("model"):
        command += ["--study-model", params["model"]]
    if params.get("candidate"):
        command += ["--study-candidate", params["candidate"]]
    for flag, key in (("--study-points", "points"), ("--study-seed", "seed"),
                      ("--study-capacity", "capacity"), ("--study-ensemble", "ensemble"),
                      ("--study-thread-workers", "thread_workers"),
                      ("--study-h-km", "h_km"), ("--study-angle-deg", "angle_deg"),
                      ("--study-time-s", "time_s"), ("--study-target", "target"),
                      ("--study-scale", "scale"),
                      ("--study-perturbation-scale", "perturbation_scale"),
                      ("--study-repeats", "repeats"),
                      ("--study-begin", "begin"), ("--study-count", "count"),
                      (RESOLUTION_FLAGS["cell_km"], "cell_km"),
                      (RESOLUTION_FLAGS["cell_deg"], "cell_deg"),
                      (RESOLUTION_FLAGS["cell_s"], "cell_s"),
                      ("--study-interpolation", "interpolation"),
                      ("--study-interpolation-axis", "interpolation_axis"),
                      ("--study-capacity-policy", "capacity_policy")):
        if params.get(key) is not None:
            value = params[key]
            command += [flag, value if isinstance(value, str) else number(value)]
    if params.get("refine"):
        command += ["--study-refine", "on"]
    if params.get("benchmark_route"):
        command += ["--study-benchmark-route", params["benchmark_route"]]
    if params.get("trace"):
        command += ["--study-trace", params["trace"]]
    return command


class Planner:
    """Builds the deterministic task list; every task id embeds its parameters."""

    def __init__(self, cfg: RunConfig):
        self.cfg = cfg
        self.used_ids: set[str] = set()

    def unique(self, task_id: str) -> str:
        candidate = task_id
        suffix = 2
        while candidate in self.used_ids:
            candidate = f"{task_id}-{suffix}"
            suffix += 1
        self.used_ids.add(candidate)
        return candidate

    # -- stage 1: sensitivity and locality ---------------------------------
    def sensitivity_tasks(self) -> list[Task]:
        cfg = self.cfg
        tasks: list[Task] = []
        for model, group, epochs, ap_history, _ in weather_groups(cfg):
            for name, utc in epochs:
                task_id = self.unique(f"sens-{group}-{model}-{name}")
                fixtures = [(name, utc)]
                command = base_command(cfg, f"tasks/{task_id}", fixtures, cfg.scenarios[:1],
                                       utc, cfg.duration_s, ap_history)
                params = {"kind": "sensitivity", "model": model, "group": group,
                          "fixture": name, "fixture_utc": utc, "ap_history": ap_history,
                          "points": cfg.points, "seed": cfg.seed, "h_km": None,
                          "angle_deg": None, "time_s": None, "scale": None, "target": None}
                command += study_arguments(cfg, params)
                command += list(cfg.extra_args)
                tasks.append(Task(task_id=task_id, stage="sensitivity", kind="sensitivity",
                                  serial=False, commands=[command], params=params,
                                  out_dir=f"tasks/{task_id}", model=model, target=group,
                                  label=f"{group} {name}"))
        return tasks

    def locality_tasks(self) -> list[Task]:
        cfg = self.cfg
        ensembles, scales, thread_counts = locality_plan_sizes(cfg)
        tasks: list[Task] = []
        for model in cfg.models:
            for ensemble in ensembles:
                for scale in scales:
                    task_id = self.unique(f"loc-seq-{model}-e{ensemble}-s{number_slug(scale)}")
                    command = base_command(cfg, f"tasks/{task_id}", [], cfg.scenarios,
                                           cfg.epoch_utc, cfg.duration_s, False)
                    params = {"kind": "locality", "model": model, "ensemble": ensemble,
                              "thread_workers": 1, "perturbation_scale": scale, "points": cfg.points,
                              "seed": cfg.seed, "capacity": cfg.capacities[0],
                              "execution": "sequential",
                              "trace": str(cfg.out / "traces" / f"{task_id}.bin")}
                    command += study_arguments(cfg, params)
                    command += list(cfg.extra_args)
                    tasks.append(Task(task_id=task_id, stage="locality", kind="locality",
                                      serial=False, commands=[command], params=params,
                                      out_dir=f"tasks/{task_id}", model=model,
                                      target="sequential", label=f"ensemble {ensemble}"))
            for threads in thread_counts:
                if threads <= 1:
                    continue
                for scale in scales:
                    task_id = self.unique(
                        f"loc-thr-{model}-e{ensemble}-t{threads}-s{number_slug(scale)}")
                    command = base_command(cfg, f"tasks/{task_id}", [], cfg.scenarios,
                                           cfg.epoch_utc, cfg.duration_s, False)
                    params = {"kind": "locality", "model": model, "ensemble": ensemble,
                              "thread_workers": threads, "perturbation_scale": scale,
                              "points": cfg.points, "seed": cfg.seed,
                              "capacity": cfg.capacities[0], "execution": "threads",
                              "trace": str(cfg.out / "traces" / f"{task_id}.bin")}
                    command += study_arguments(cfg, params)
                    command += list(cfg.extra_args)
                    tasks.append(Task(task_id=task_id, stage="locality", kind="locality",
                                      serial=False, commands=[command], params=params,
                                      out_dir=f"tasks/{task_id}", model=model,
                                      target="threads", label=f"{threads} worker threads"))
        return tasks

    # -- stage 2: accuracy screening and consistency checks -----------------
    def accuracy_tasks(self) -> list[Task]:
        """One task per weather epoch, candidate configuration and error target.

        A configuration is identified by its model, candidate, weather
        configuration, cell spacing, capacity and interpolation policy: those are
        the parameters that define the implementation, and the shortlist later
        selects whole configurations rather than candidate names.
        """
        cfg = self.cfg
        tasks: list[Task] = []
        grid = accuracy_candidate_grid(cfg)
        for model, group, epochs, ap_history, accuracy_names in weather_groups(cfg):
            selected = [(name, utc) for name, utc in epochs if name in accuracy_names]
            if not selected:
                continue
            for fixture_name, fixture_utc in selected:
                for candidate, resolution in grid:
                    for target_percent in (cfg.targets if candidate == "adaptive" else (cfg.targets[0],)):
                        # --study-target is a numeric relative-error target; the
                        # study error thresholds (0.01/0.1/0.5/1/5 %) are judged on
                        # the maximum observed error, so the runner sends the same
                        # value as a fraction and keeps the percentage in the label.
                        target = target_percent / 100.0
                        task_id = self.unique(
                            f"acc-{group}-{model}-{fixture_name}-{candidate}-"
                            + "-".join(number_slug(resolution.get(key))
                                       for key in RESOLUTION_KEYS)
                            + f"-e{number_slug(target)}")
                        # One weather epoch per task: the study anchors a task on a
                        # single fixture, so bundling epochs would silently measure
                        # only the first one.
                        command = base_command(cfg, f"tasks/{task_id}",
                                               [(fixture_name, fixture_utc)],
                                               cfg.scenarios[:1], fixture_utc,
                                               cfg.duration_s, ap_history)
                        params = {"kind": "accuracy", "model": model, "group": group,
                                  "candidate": candidate, "points": cfg.points,
                                  "seed": cfg.seed, "target": target,
                                  "target_percent": target_percent,
                                  "ap_history": ap_history,
                                  "fixture": fixture_name, "fixture_utc": fixture_utc,
                                  **resolution}
                        command += study_arguments(cfg, params)
                        command += list(cfg.extra_args)
                        tasks.append(Task(task_id=task_id, stage="accuracy", kind="accuracy",
                                          serial=False, commands=[command], params=params,
                                          out_dir=f"tasks/{task_id}", model=model,
                                          candidate=candidate, target=group,
                                          label=f"{fixture_name}, error target "
                                                f"{target_percent} percent"))
        return tasks

    def check_tasks(self, shortlist: list[dict]) -> list[Task]:
        cfg = self.cfg
        tasks: list[Task] = []
        for configuration in shortlist:
            model, candidate = configuration["model"], configuration["candidate"]
            task_id = self.unique(f"chk-{model}-{candidate}-"
                                  + configuration_suffix(configuration))
            command = base_command(cfg, f"tasks/{task_id}", [], cfg.scenarios[:1],
                                   cfg.epoch_utc, cfg.duration_s, False)
            params = {"kind": "check", "model": model, "candidate": candidate,
                      "points": cfg.points, "seed": cfg.seed,
                      **{key: configuration.get(key)
                         for key in RESOLUTION_KEYS + POLICY_KEYS}}
            command += study_arguments(cfg, params)
            command += list(cfg.extra_args)
            tasks.append(Task(task_id=task_id, stage="accuracy", kind="check",
                              serial=False, commands=[command], params=params,
                              out_dir=f"tasks/{task_id}", model=model,
                              candidate=candidate, target="consistency"))
        return tasks

    # -- stage 3: coupled propagation of the shortlist -----------------------
    def propagation_tasks(self, shortlist: list[dict]) -> list[Task]:
        cfg = self.cfg
        tasks: list[Task] = []
        long_case = cfg.scenarios[0]
        short_window = 60.0 if cfg.mode == "smoke" else SIX_HOURS
        for configuration in shortlist:
            model, candidate = configuration["model"], configuration["candidate"]
            suffix = configuration_suffix(configuration)
            ap_history = bool(configuration.get("ap_history"))
            for case_name in cfg.scenarios:
                task_id = self.unique(f"prop-{model}-{candidate}-{suffix}-{case_name}-6h")
                # The Ap history switch travels with the configuration that was
                # screened: an NRLMSISE-00 configuration is never propagated under
                # a weather configuration it was not measured in.
                command = base_command(cfg, f"tasks/{task_id}", [], [case_name],
                                       cfg.epoch_utc, short_window, ap_history)
                params = {"kind": "propagation", "model": model, "candidate": candidate,
                          "case": case_name,
                          "duration_h": round(short_window / HOUR, 6), "points": cfg.points,
                          "seed": cfg.seed, "ensemble": 1 if cfg.mode == "smoke" else 4, "ap_history": ap_history,
                          "group": configuration.get("group"),
                          "target": configuration.get("target") or cfg.shortlist_rel_error_percent / 100.0,
                          "duration_s": short_window,
                          "refine": cfg.mode == "full" and case_name == "case-c",
                          **{key: configuration.get(key)
                             for key in RESOLUTION_KEYS + POLICY_KEYS}}
                command += ["--refine-rtol", number(cfg.refine_rtol),
                            "--refine-max-step", number(cfg.refine_max_step_s)]
                command += study_arguments(cfg, params)
                command += list(cfg.extra_args)
                tasks.append(Task(task_id=task_id, stage="coupled", kind="propagation",
                                  serial=False, commands=[command], params=params,
                                  out_dir=f"tasks/{task_id}", model=model,
                                  candidate=candidate, target="6h", label=case_name))
            if "24h" in cfg.targets or cfg.mode == "full":
                task_id = self.unique(f"prop-{model}-{candidate}-{suffix}-{long_case}-24h")
                command = base_command(cfg, f"tasks/{task_id}", [], [long_case],
                                       cfg.epoch_utc, DAY, ap_history)
                params = {"kind": "propagation", "model": model, "candidate": candidate,
                          "case": long_case, "duration_h": 24, "points": cfg.points,
                          "seed": cfg.seed, "ensemble": 1, "ap_history": ap_history,
                          "group": configuration.get("group"),
                          "target": configuration.get("target") or cfg.shortlist_rel_error_percent / 100.0,
                          "duration_s": DAY,
                          **{key: configuration.get(key)
                             for key in RESOLUTION_KEYS + POLICY_KEYS}}
                command += ["--refine-rtol", number(cfg.refine_rtol),
                            "--refine-max-step", number(cfg.refine_max_step_s)]
                command += study_arguments(cfg, params)
                command += list(cfg.extra_args)
                tasks.append(Task(task_id=task_id, stage="coupled", kind="propagation",
                                  serial=False, commands=[command], params=params,
                                  out_dir=f"tasks/{task_id}", model=model,
                                  candidate=candidate, target="24h", label=long_case))
        return tasks

    # -- stage 4: exclusive end-to-end performance groups --------------------
    def benchmark_tasks(self, shortlist: list[dict]) -> list[Task]:
        """One exclusive task per measured configuration and workload.

        A benchmark task owns a whole group of children. The group is started
        only when no other child is alive, so a timing measurement is never
        contaminated by a scientific sweep or by another measurement. Within the
        group the process-isolated workload runs its children concurrently, as
        MT-002 measured, and the group's wall time is measured by the parent
        around the whole group rather than by summing child times.
        """
        cfg = self.cfg
        tasks: list[Task] = []
        workloads = ["single", "sequential", "ensemble"]
        if cfg.mode != "smoke":
            workloads.append("processes")
        repeats = cfg.repeats
        ensemble_size = 1
        if cfg.mode == "screen":
            ensemble_size = 4
        elif cfg.mode == "full":
            ensemble_size = 16
        for configuration in shortlist:
            model, candidate = configuration["model"], configuration["candidate"]
            suffix = configuration_suffix(configuration)
            ap_history = bool(configuration.get("ap_history"))
            for workload in workloads:
                task_id = self.unique(f"bench-{model}-{candidate}-{suffix}-{workload}")
                # The workload stays a runner label; --study-target carries the
                # numeric relative-error target the reconstruction must meet.
                params = {"kind": "benchmark", "model": model,
                          "candidate": candidate,
                          "target": configuration.get("target") or cfg.shortlist_rel_error_percent / 100.0,
                          "workload": workload, "repeats": repeats,
                          "ensemble": 1 if workload == "single" else ensemble_size,
                          "thread_workers": 1, "points": cfg.points, "seed": cfg.seed,
                          "isolation": workload == "processes",
                          **{key: configuration.get(key)
                             for key in RESOLUTION_KEYS + POLICY_KEYS}}
                commands = []
                if workload == "processes":
                    # MT-002 orchestration: one independent process per contiguous
                    # slice of the ensemble, using the same balanced partition the
                    # measured process-isolated batches use. Each process owns its
                    # own cache; interprocess reuse is never counted.
                    for repetition in range(-1, repeats):
                        routes = ("direct", "candidate") if repetition % 2 == 0 else ("candidate", "direct")
                        for route in routes:
                            group_id = self.unique(f"{task_id}-{route}-rep{repetition}")
                            # The partition belongs to the group's identity: a
                            # resume may change the worker count, but an existing
                            # group's global ranges never move.
                            parts = self.recorded_partition(ledger, group_id,
                                                           ensemble_size, max(cfg.workers, 1))
                            group_params = dict(params, benchmark_route=route, repetition=repetition,
                                                warmup=repetition < 0,
                                                partition=[list(part) for part in parts])
                            group_commands = []
                            for part_index, (part_begin, part_count) in enumerate(parts):
                                part_out = f"tasks/{group_id}/part{part_index}"
                                part_command = base_command(cfg, part_out, [], cfg.scenarios[:1],
                                    cfg.epoch_utc, cfg.duration_s, ap_history)
                                part_command += study_arguments(cfg, dict(group_params,
                                    begin=part_begin, count=part_count))
                                part_command += list(cfg.extra_args)
                                group_commands.append(part_command)
                            tasks.append(Task(task_id=group_id, stage="benchmark", kind="benchmark",
                                serial=True, commands=group_commands, params=group_params,
                                out_dir=f"tasks/{group_id}", model=model, candidate=candidate,
                                target="processes", label=f"{route} repeat {repetition}", parallel_children=True))
                    continue
                else:
                    # One child per concrete workload: the repetitions happen inside
                    # the study executable and are reported by it, so the runner
                    # never multiplies a repetition count by the number of children.
                    out_dir = f"tasks/{task_id}"
                    command = base_command(cfg, out_dir, [], cfg.scenarios[:1],
                                           cfg.epoch_utc, cfg.duration_s, ap_history)
                    command += study_arguments(cfg, params)
                    command += list(cfg.extra_args)
                    commands.append(command)
                tasks.append(Task(task_id=task_id, stage="benchmark", kind="benchmark",
                                  serial=True, commands=commands, params=params,
                                  out_dir=f"tasks/{task_id}", model=model,
                                  candidate=candidate, target=workload,
                                  label=f"{repeats} repetitions",
                                  parallel_children=workload == "processes"))
        return tasks

    def deferred_tasks(self, stage: str, kind: str, reason: str) -> list[Task]:
        """Placeholder records for work that must not be executed yet.

        The coupled and performance stages are blocked until a configuration has
        actually passed screening. Recording the block keeps the plan, the ledger
        and the status honest instead of quietly running an unmeasured candidate.
        """
        return [Task(task_id=f"{stage}-blocked-{kind}-{model}", stage=stage, kind=kind,
                     serial=True, commands=[], params={"kind": kind, "model": model, "blocked": True},
                     out_dir="", model=model, target="blocked", label=reason, deferred=reason)
                for model in self.cfg.models]

    # -- shortlist selection -------------------------------------------------
    def shortlist(self, accuracy_tasks: list[Task], ledger: "Ledger") -> tuple[list[dict], dict]:
        """Select the exact screened configurations the coupled stages may use.

        A configuration is eligible only when its completed accuracy tasks
        published a maximum absolute relative error at or below the screening
        threshold. There is no fallback: a configuration that has not been
        measured, or that failed the threshold, is never propagated or benchmarked.
        """
        cfg = self.cfg
        groups: dict[tuple, list[Task]] = {}
        for task in accuracy_tasks:
            key = configuration_key(task.params)
            groups.setdefault(key, []).append(task)

        eligible: list[dict] = []
        blocked: list[dict] = []
        for key, tasks in sorted(groups.items(), key=lambda item: str(item[0])):
            entry = {"model": key[0], "candidate": key[1], "group": key[2],
                     "ap_history": key[2] == "history",
                     "cell_km": key[3], "cell_deg": key[4], "cell_s": key[5],
                     "capacity": key[6], "interpolation": key[7],
                     "interpolation_axis": key[8], "capacity_policy": key[9],
                     "target": key[10],
                     "accuracy_tasks": len(tasks)}
            worst = None
            incomplete = []
            problems: list[str] = []
            for task in tasks:
                if ledger.status(task.task_id) != "completed":
                    incomplete.append(task.task_id)
                    continue
                screen = screen_accuracy(entry, ledger.result_payload(task.task_id))
                problems.extend(f"{task.task_id}: {item}" for item in screen["problems"])
                value = screen["worst_relative_percent"]
                if value is None:
                    continue
                worst = value if worst is None else max(worst, value)
            entry["worst_abs_rel_error_percent"] = worst
            # Every screening task of the configuration must have completed and
            # screened clean; a partially measured configuration is never treated
            # as a screened one.
            if incomplete or problems or worst is None:
                blocked.append({**entry,
                                "reason": "the screening stage for this configuration is "
                                          "incomplete or failed its own correctness checks",
                                "incomplete_tasks": incomplete,
                                "problems": problems[:8]})
            elif worst > cfg.shortlist_rel_error_percent:
                blocked.append({**entry, "reason": "worst measured relative error "
                                                  f"{worst} percent exceeds the "
                                                  f"{cfg.shortlist_rel_error_percent} "
                                                  "percent screening threshold"})
            else:
                eligible.append(entry)

        # The preprocessing-only control is not a caching candidate: it is always
        # propagated and benchmarked because it is the baseline that separates
        # caching from the ATM-OPT-001 reuse and the current exact provider.
        for model in cfg.models:
            if not any(entry["model"] == model and entry["candidate"] == BENCHMARK_BASELINE
                       for entry in eligible + blocked):
                eligible.append({"model": model, "candidate": BENCHMARK_BASELINE,
                                 "group": "baseline", "ap_history": False,
                                 "cell_km": None, "cell_deg": None, "cell_s": None,
                                 "capacity": cfg.capacities[-1],
                                 "interpolation": None, "interpolation_axis": None,
                                 "capacity_policy": None,
                                 "worst_abs_rel_error_percent": None,
                                 "selection": "preprocessing-only baseline, not a screened "
                                              "caching candidate"})

        eligible.sort(key=lambda entry: (entry["model"], entry.get("ap_history", False),
                                        str(entry["candidate"]),
                                        entry.get("worst_abs_rel_error_percent") or 0,
                                        configuration_suffix(entry)))
        selected = []
        selected_candidates = set()
        selected_counts = {}
        for entry in eligible:
            key = (entry["model"], entry["candidate"])
            if key in selected_candidates:
                continue
            if entry["candidate"] != BENCHMARK_BASELINE:
                if selected_counts.get(entry["model"], 0) >= cfg.max_shortlist:
                    continue
                selected_counts[entry["model"]] = selected_counts.get(entry["model"], 0) + 1
            selected.append(entry)
            selected_candidates.add(key)
        eligible = selected

        policy = {
            "threshold_relative_error_percent": cfg.shortlist_rel_error_percent,
            "metric": "maximum of the three holdout maximum_absolute_relative values of the "
                      "requested rung, over every weather epoch and error target, in percent",
            "schema": "result.resolution_ladder[requested_rung == true]"
                      ".{holdout_uniform-random,holdout_local-query-packet,"
                      "holdout_broad-query-packet}.results.error_metrics.maximum_absolute_relative",
            "holdout_packets": list(HOLDOUT_PACKETS),
            "eligible": eligible,
            "not_eligible": blocked,
            "selection_rule": ("a whole configuration (model, candidate, cell_km, cell_deg, "
                               "cell_s, capacity) is selected only when its completed accuracy "
                               "tasks published a worst-case error within the threshold; no "
                               "configuration is propagated or benchmarked otherwise"),
            "selection_source": "measured accuracy of this run",
        }
        return eligible, policy

    def plan(self, ledger: "Ledger", defer_missing: bool = True) -> tuple[list[Task], dict]:
        """Build the whole stage plan from the ledger's measured state.

        With ``defer_missing`` the coupled and performance stages are recorded as
        blocked placeholders instead of being planned; the caller re-plans them
        once the screening stage has produced its measurements.
        """
        cfg = self.cfg
        self.used_ids.clear()
        plan = {"stages": ["sensitivity+locality", "accuracy", "coupled", "benchmark", "aggregate"]}
        tasks: list[Task] = []
        if "sensitivity" in cfg.kinds:
            tasks.extend(self.sensitivity_tasks())
        if "locality" in cfg.kinds:
            tasks.extend(self.locality_tasks())
        accuracy = self.accuracy_tasks() if "accuracy" in cfg.kinds else []
        tasks.extend(accuracy)
        shortlist, policy = self.shortlist(accuracy, ledger)
        plan["shortlist"] = policy
        if "check" in cfg.kinds:
            tasks.extend(self.check_tasks(shortlist) if shortlist else
                         self.deferred_tasks("accuracy", "check", "cache configurations not screened"))
        measured = [entry for entry in shortlist
                    if entry.get("worst_abs_rel_error_percent") is not None]
        if not measured and defer_missing and "accuracy" in cfg.kinds:
            reason = ("no candidate configuration passed the measured accuracy screening; "
                      "the coupled and performance stages are blocked, not executed")
            plan["blocked_stages"] = {"coupled": reason, "benchmark": reason}
            if "propagation" in cfg.kinds:
                tasks.extend(self.deferred_tasks("coupled", "propagation", reason))
            if "benchmark" in cfg.kinds:
                tasks.extend(self.deferred_tasks("coupled", "benchmark", reason))
        else:
            if "propagation" in cfg.kinds:
                tasks.extend(self.propagation_tasks(shortlist))
            if "benchmark" in cfg.kinds:
                tasks.extend(self.benchmark_tasks(shortlist))
        return tasks, plan


def configuration_key(params: dict) -> tuple:
    """The identity of a screened configuration.

    It includes the weather configuration (including the three-hourly Ap history
    switch), the interpolation or cell policy, the actual cell widths and the
    capacity, so a propagated or benchmarked configuration is always exactly the
    configuration that was screened, never merely its candidate name.
    """
    return (params.get("model"), params.get("candidate"), params.get("group"),
            params.get("cell_km"), params.get("cell_deg"), params.get("cell_s"),
            params.get("capacity"), params.get("interpolation"),
            params.get("interpolation_axis"), params.get("capacity_policy"), params.get("target"))


def configuration_suffix(configuration: dict) -> str:
    return "-".join(number_slug(configuration.get(key)) for key in RESOLUTION_KEYS) + (
        "" if not any(configuration.get(key) for key in POLICY_KEYS) else
        "-" + "-".join(slug(str(configuration.get(key))) or "na" for key in POLICY_KEYS)) + (
        "-" + slug(configuration.get("group") or "baseline") +
        "-t" + number_slug(configuration.get("target")))


# The study executable publishes its accuracy screen under this schema; the
# runner reads exactly these fields and never guesses at an alternative name.
HOLDOUT_PACKETS = ("holdout_uniform-random", "holdout_local-query-packet",
                   "holdout_broad-query-packet")
NONFINITE_KEY = "nonpositive_or_nonfinite_reconstructions"


def screening_finished(tasks: list[Task], ledger: "Ledger") -> bool:
    """True when every accuracy task of this run has reached a terminal state."""
    screening = [task for task in tasks if task.kind == "accuracy"]
    if not screening:
        return False
    return all(ledger.status(task.task_id) in ("completed", "failed") for task in screening)


def accuracy_rung(payload) -> dict | None:
    """The requested rung of an accuracy result, or None when it is absent."""
    if not isinstance(payload, dict):
        return None
    result = payload.get("result")
    if not isinstance(result, dict):
        return None
    ladder = result.get("resolution_ladder")
    if not isinstance(ladder, list):
        return None
    for entry in ladder:
        if isinstance(entry, dict) and entry.get("requested_rung") is True:
            return entry
    return None


def count_nonfinite(node) -> int:
    """Total nonpositive or nonfinite reconstructions anywhere in a result."""
    if isinstance(node, dict):
        total = 0
        for key, value in node.items():
            if key == NONFINITE_KEY and isinstance(value, (int, float)) \
                    and not isinstance(value, bool):
                total += max(total, int(value))
            else:
                total = max(total, count_nonfinite(value))
        return total
    if isinstance(node, list):
        return max((count_nonfinite(item) for item in node), default=0)
    return 0


def expected_settings(configuration: dict) -> dict:
    return {"base_cells": {"altitude_km": configuration.get("cell_km"),
                           "latitude_deg": configuration.get("cell_deg"),
                           "longitude_deg": configuration.get("cell_deg"),
                           "time_s": configuration.get("cell_s")},
            "capacity": configuration.get("capacity"),
            "interpolation_mode": configuration.get("interpolation"),
            "interpolation_axis": configuration.get("interpolation_axis"),
            "capacity_policy": configuration.get("capacity_policy")}


def settings_match(configuration: dict, settings) -> list[str]:
    """Differences between the configuration screened and the rung reported."""
    expected = expected_settings(configuration)
    problems = []
    if configuration.get("cell_km") is None:
        # The exact reference and the control do not depend on a cell width.
        return problems
    if not isinstance(settings, dict):
        return ["the result published no settings for the requested rung"]
    cells = settings.get("base_cells") if isinstance(settings.get("base_cells"), dict) else {}
    for name, value in (("altitude_km", configuration.get("cell_km")),
                        ("latitude_deg", configuration.get("cell_deg")),
                        ("longitude_deg", configuration.get("cell_deg")),
                        ("time_s", configuration.get("cell_s"))):
        observed = cells.get(name)
        if observed != value:
            problems.append(f"settings.base_cells.{name}: screened {value}, rung {observed}")
    for name, value in (("capacity", configuration.get("capacity")),
                        ("interpolation_mode", configuration.get("interpolation")),
                        ("interpolation_axis", configuration.get("interpolation_axis")),
                        ("capacity_policy", configuration.get("capacity_policy"))):
        observed = settings.get(name)
        if observed != value:
            problems.append(f"settings.{name}: screened {value}, rung {observed}")
    return problems


def screen_accuracy(configuration: dict, payload) -> dict:
    """Screen one accuracy result against the configuration the runner requested.

    Returns the worst holdout error as a percentage together with the reasons the
    configuration cannot be selected. A configuration is never selected from a
    partial or ambiguous measurement.
    """
    problems = []
    entry = accuracy_rung(payload)
    if entry is None:
        return {"worst_relative_percent": None,
                "problems": ["the result published no requested rung"]}
    problems.extend(settings_match(configuration, entry.get("settings")))
    nonfinite = count_nonfinite(entry)
    if nonfinite > 0:
        problems.append(f"{nonfinite} nonpositive or nonfinite reconstruction(s): the "
                        "candidate is rejected regardless of its error")
    worst = None
    for packet in HOLDOUT_PACKETS:
        node = entry.get(packet)
        metrics = (node or {}).get("results", {}).get("error_metrics") if isinstance(node, dict) else None
        value = metrics.get("maximum_absolute_relative") if isinstance(metrics, dict) else None
        if not isinstance(value, (int, float)) or isinstance(value, bool) or value != value:
            problems.append(f"holdout {packet} published no maximum_absolute_relative")
            continue
        percent = float(value) * 100.0
        worst = percent if worst is None else max(worst, percent)
    return {"worst_relative_percent": worst, "problems": problems,
            "nonfinite_reconstructions": nonfinite}


# ---------------------------------------------------------------------------
# Ledger
# ---------------------------------------------------------------------------


class Ledger:
    """The checkpointed, machine-readable record of everything executed."""

    def __init__(self, path: Path):
        self.path = Path(path)
        self.data = {"kind": "atm-cache-study-ledger", "schema_version": LEDGER_SCHEMA_VERSION,
                     "created_utc": utc_now(), "updated_utc": utc_now(),
                     "identity": None, "mode": None, "plan": None,
                     "commands": [], "tasks": {}}
        self._results: dict[str, dict] = {}

    def load(self) -> None:
        if not self.path.is_file():
            return
        data = json.loads(self.path.read_text())
        if data.get("schema_version") != LEDGER_SCHEMA_VERSION:
            raise RunnerError(f"ledger schema {data.get('schema_version')} is not "
                              f"{LEDGER_SCHEMA_VERSION}; start a new --out directory")
        self.data = data

    def save(self) -> None:
        self.data["updated_utc"] = utc_now()
        write_json_atomic(self.path, self.data)

    # -- identity ----------------------------------------------------------
    def set_plan(self, mode: str, plan: dict) -> None:
        """Record the plan parameters without binding the run identity.

        A blocked primary run records its plan but deliberately leaves the
        identity unbound: the inputs were missing or wrong at that point, so the
        identity is bound by the first run that actually verifies them.
        """
        self.data["mode"] = mode
        self.data["plan"] = plan
        self.save()

    def bind(self, identity: dict, mode: str, plan: dict) -> None:
        recorded = self.data.get("identity")
        if recorded is None:
            self.data["identity"] = identity
        else:
            differences = identity_differences(recorded, identity)
            if differences:
                raise RunnerError(
                    "refusing to resume: the run identity changed since the ledger was "
                    "written. Start a new --out directory instead of mixing results.\n  "
                    + "\n  ".join(differences))
        self.data["mode"] = mode
        self.data["plan"] = plan

    # -- tasks -------------------------------------------------------------
    def record_task(self, task: Task) -> None:
        entry = self.data["tasks"].get(task.task_id)
        identity = task.identity()
        if entry is None:
            entry = {"identity": identity, "status": "pending", "attempts": 0}
            self.data["tasks"][task.task_id] = entry
        elif entry.get("identity") != identity:
            if entry.get("status") == "completed":
                raise RunnerError(f"task {task.task_id} was planned differently on this run; "
                                  "its recorded command does not match")
            # Nothing was executed for a blocked or pending task, so re-planning it
            # (typically because a previously blocked stage is now unblocked) is
            # safe and is recorded rather than silently overwritten.
            entry["replanned_utc"] = utc_now()
            entry["previous_identity"] = entry.get("identity")
            entry["identity"] = identity
        entry.update({"stage": task.stage, "kind": task.kind, "model": task.model,
                      "candidate": task.candidate, "target": task.target,
                      "label": task.label, "params": task.params})

    def status(self, task_id: str) -> str:
        return self.data["tasks"].get(task_id, {}).get("status", "pending")

    def note(self, task_id: str, note: str) -> None:
        entry = self.data["tasks"].setdefault(task_id, {"status": "pending", "attempts": 0})
        entry["note"] = note

    def record_attempt(self, task_id: str, command, started: str, index: int, count: int) -> None:
        entry = self.data["tasks"][task_id]
        entry["status"] = "running"
        entry["attempts"] = entry.get("attempts", 0) + 1
        entry["command"] = [str(part) for part in command]
        entry["command_line"] = " ".join(shlex.quote(str(part)) for part in command)
        entry["children_total"] = count
        entry["started_utc"] = started
        self.data["commands"].append({"task_id": task_id, "child": index,
                                      "command": entry["command"],
                                      "command_line": entry["command_line"],
                                      "started_utc": started})
        self.save()

    def record_child(self, task_id: str, index: int, returncode: int, finished: str,
                     log_path: Path) -> None:
        for entry in reversed(self.data["commands"]):
            if (entry["task_id"] == task_id and entry.get("child") == index
                    and "finished_utc" not in entry):
                entry["finished_utc"] = finished
                entry["returncode"] = returncode
                entry["log"] = str(log_path)
                break
        self.save()

    def record_result(self, task_id: str, result_path: Path, digest: str, index: int = 0,
                      completed: int | None = None) -> None:
        entry = self.data["tasks"][task_id]
        results = entry.setdefault("results", [])
        results[:] = [item for item in results if item.get("index") != index]
        results.append({"index": index, "path": str(result_path), "sha256": digest})
        results.sort(key=lambda item: item["index"])
        expected = len((entry.get("identity") or {}).get("commands") or [])
        entry["status"] = "running"
        self.save()

    def record_group_wall(self, task_id: str, seconds: float) -> None:
        """Wall time measured by the parent around an entire exclusive group."""
        entry = self.data["tasks"][task_id]
        entry["group_wall_seconds"] = seconds
        expected = len((entry.get("identity") or {}).get("commands") or [])
        if {item["index"] for item in entry.get("results", [])} != set(range(expected)):
            raise RunnerError(f"incomplete child result group: {task_id}")
        entry["status"] = "completed"
        entry["completed_utc"] = utc_now()
        self.save()

    def record_blocked(self, task_id: str, reason: str) -> None:
        entry = self.data["tasks"][task_id]
        entry["status"] = "blocked"
        entry["blocked_reason"] = reason
        entry.setdefault("note", "not executed: a prerequisite measurement has not passed")
        self.save()

    def record_failure(self, task_id: str, returncode: int, log_path: Path, reason: str) -> None:
        entry = self.data["tasks"][task_id]
        entry["status"] = "failed"
        entry["returncode"] = returncode
        entry["log"] = str(log_path)
        entry["failure_reason"] = reason
        self.save()

    def record_pending(self, task_id: str, reason: str) -> None:
        entry = self.data["tasks"][task_id]
        entry["status"] = "pending"
        entry["note"] = reason
        self.save()

    # -- results -----------------------------------------------------------
    def load_result(self, result_path: Path):
        try:
            return json.loads(Path(result_path).read_text())
        except (OSError, ValueError):
            return None

    def result_payload(self, task_id: str):
        return self._results.get(task_id)

    def cache_result(self, task_id: str, payload) -> None:
        self._results[task_id] = payload

    def counts(self) -> dict:
        counts: dict[str, int] = {}
        for entry in self.data["tasks"].values():
            status = entry.get("status", "pending")
            counts[status] = counts.get(status, 0) + 1
        return dict(sorted(counts.items()))


def identity_differences(recorded: dict, current: dict, prefix: str = "") -> list[str]:
    differences = []
    for key in sorted(set(recorded) | set(current)):
        left = recorded.get(key)
        right = current.get(key)
        if isinstance(left, dict) and isinstance(right, dict):
            differences.extend(identity_differences(left, right, f"{prefix}{key}."))
        elif left != right:
            differences.append(f"{prefix}{key}: recorded {left!r} != current {right!r}")
    return differences


# ---------------------------------------------------------------------------
# Scheduling
# ---------------------------------------------------------------------------


class Scheduler:
    """Concurrent scientific sweeps, exclusive serial benchmark groups."""

    def __init__(self, cfg: RunConfig, ledger: Ledger, tasks: list[Task]):
        self.cfg = cfg
        self.ledger = ledger
        self.tasks = tasks
        self.stop = {"requested": False, "signal": None}

    def install_signal_handlers(self) -> None:
        def handler(signum, _frame):
            self.stop["requested"] = True
            self.stop["signal"] = signum
            print(f"[run.py] signal {signum}: stopping scheduling and reaping children",
                  flush=True)
        for name in ("SIGINT", "SIGTERM"):
            if hasattr(signal, name):
                signal.signal(getattr(signal, name), handler)

    # -- results -----------------------------------------------------------
    def publish(self, task: Task, index: int = 0, completed: int | None = None) -> bool:
        """Publish one child's single result into results/ atomically.

        A group with several children publishes one file per child, so no
        measurement is lost and none is merged into a number nobody measured.
        """
        suffix = "" if len(task.commands) == 1 else f".part{index}"
        command = task.commands[index]
        output_directory = Path(command[command.index("--out") + 1])
        source = output_directory / "study-result.json"
        log = self.cfg.out / "logs" / f"{task.task_id}.log"
        if not source.is_file():
            self.ledger.record_failure(task.task_id, 0, log,
                                       "child exited zero but published no study-result.json")
            return False
        payload = self.ledger.load_result(source)
        if payload is None:
            self.ledger.record_failure(task.task_id, 0, log,
                                       "child published a study-result.json that is not valid JSON")
            return False
        if task.kind == "check" and (payload.get("result") or {}).get("all_checks_passed") is False:
            self.ledger.record_failure(task.task_id, 0, log, "scientific cache correctness checks failed")
            return False
        digest = sha256_file(source)
        destination = self.cfg.out / "results" / f"{task.task_id}{suffix}.json"
        destination.parent.mkdir(parents=True, exist_ok=True)
        # The child's bytes are published verbatim through a temporary file and one
        # atomic replace: re-serialising would change the bytes the hash covers and
        # could silently alter a number the study measured.
        staging = destination.with_name(f"{destination.name}.tmp-{os.getpid()}")
        staging.write_bytes(source.read_bytes())
        os.replace(staging, destination)
        if sha256_file(destination) != digest:
            raise RunnerError(f"published result hash mismatch for {task.task_id}")
        self.ledger.record_result(task.task_id, destination, digest, index, completed)
        self.ledger.cache_result(task.task_id, payload)
        return True

    def completed_is_intact(self, task: Task) -> bool:
        """A completed task is trusted only while its published hashes match."""
        entry = self.ledger.data["tasks"].get(task.task_id) or {}
        if entry.get("status") != "completed":
            return False
        published = entry.get("results") or []
        if not published:
            raise RunnerError(f"completed task has no published result: {task.task_id}")
        for item in published + entry.get("artifacts", []):
            path = Path(item.get("path", ""))
            if not path.is_file() or sha256_file(path) != item.get("sha256"):
                raise RunnerError(f"completed result missing or hash mismatch: {task.task_id}")
        return True

    # -- execution ---------------------------------------------------------
    def run(self, deadline: float) -> dict:
        cfg = self.cfg
        logs = cfg.out / "logs"
        logs.mkdir(parents=True, exist_ok=True)
        running: dict[tuple[str, int], dict] = {}
        finished: dict[str, dict] = {}
        queue = []
        for task in self.tasks:
            if task.deferred or not task.commands:
                # A blocked task is recorded, never executed.
                self.ledger.record_blocked(task.task_id, task.deferred or "not scheduled")
                continue
            queue.append(task)
        scheduled_any = False
        budget_reached = False
        interrupted = False
        groups: dict[str, dict] = {}

        def launch(task: Task, index: int):
            command = [str(part) for part in task.commands[index]]
            suffix = "" if len(task.commands) == 1 else f"-child{index}"
            log_path = logs / f"{task.task_id}{suffix}.log"
            handle = open(log_path, "wb")
            started = utc_now()
            groups.setdefault(task.task_id, {"task": task, "started": time.monotonic(),
                                             "children": 0, "failures": 0})
            try:
                process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT,
                                           cwd=str(ROOT))
            except OSError as error:
                handle.write((str(error) + "\n").encode())
                handle.close()
                self.ledger.record_attempt(task.task_id, command, started, index, len(task.commands))
                self.ledger.record_child(task.task_id, index, 127, utc_now(), log_path)
                self.ledger.record_failure(task.task_id, 127, log_path, str(error))
                state = groups.setdefault(task.task_id, {"task": task, "started": time.monotonic(),
                                                         "children": 0, "failures": 0})
                state["children"] += 1
                state["failures"] += 1
                return
            self.ledger.record_attempt(task.task_id, command, started, index,
                                       len(task.commands))
            running[(task.task_id, index)] = {"task": task, "index": index,
                                              "process": process, "handle": handle,
                                              "log": log_path, "started": time.monotonic()}
            groups.setdefault(task.task_id, {"task": task, "started": time.monotonic(),
                                             "children": 0, "failures": 0})
            print(f"[run.py] start {task.task_id} child {index} "
                  f"({'exclusive group' if task.serial else 'concurrent'}) pid={process.pid}",
                  flush=True)

        def reap_all():
            for entry in list(running.values()):
                process, handle = entry["process"], entry["handle"]
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                handle.close()

        def finish_group(task: Task):
            """Complete an exclusive group once every child has been reaped."""
            state = groups.pop(task.task_id, None)
            if state is None:
                return
            if state["failures"]:
                record = self.ledger.data["tasks"][task.task_id]
                self.ledger.record_failure(task.task_id, record.get("returncode", 1),
                    Path(record.get("log", logs / f"{task.task_id}.log")),
                    record.get("failure_reason", "one or more experiment children failed"))
                return
            if task.parallel_children and task.params.get("workload") == "processes":
                try:
                    parts = []
                    for command in task.commands:
                        directory = Path(command[command.index("--out") + 1])
                        binary = directory / "trajectories.bin"
                        header = MT002.read_header(binary)
                        parts.append((header["begin"], binary))
                    parts.sort()
                    samples = MT002.read_header(parts[0][1])["samples"]
                    merged = cfg.out / task.out_dir / "trajectories.bin"
                    MT002.merge_parts(parts, merged, task.params["ensemble"], samples)
                    self.ledger.data["tasks"][task.task_id]["artifacts"] = [
                        {"path": str(merged), "sha256": sha256_file(merged)}]
                except (OSError, MT002.IsolationError) as error:
                    self.ledger.record_failure(task.task_id, 0, logs / f"{task.task_id}.log",
                                               f"isolated binary merge failed: {error}")
                    return
            traces = sorted((cfg.out / "traces").glob(f"{task.task_id}*")) if (
                cfg.out / "traces").is_dir() else []
            if traces:
                self.ledger.data["tasks"][task.task_id].setdefault("artifacts", []).extend(
                    {"path": str(path), "sha256": sha256_file(path)} for path in traces)
            # The group's wall time is measured around the whole group by the
            # parent; child timings are never summed into a throughput figure.
            self.ledger.record_group_wall(task.task_id,
                                          time.monotonic() - state["started"])

        while queue or running:
            if self.stop["requested"]:
                interrupted = True
                break
            if queue and time.monotonic() >= deadline:
                budget_reached = True
                for pending_task in queue:
                    self.ledger.record_pending(pending_task.task_id, "not scheduled: wall-clock budget exhausted")
                queue.clear()

            # Reap finished children first so a freed slot is reused immediately.
            for key, entry in list(running.items()):
                process = entry["process"]
                if process.poll() is None:
                    continue
                del running[key]
                entry["handle"].close()
                task = entry["task"]
                self.ledger.record_child(task.task_id, entry["index"], process.returncode,
                                         utc_now(), entry["log"])
                groups.setdefault(task.task_id, {"task": task, "started": time.monotonic(),
                                                 "children": 0, "failures": 0})
                groups[task.task_id]["children"] += 1
                if process.returncode != 0:
                    tail = entry["log"].read_text(errors="replace").strip().splitlines()[-8:]
                    groups[task.task_id]["failures"] += 1
                    self.ledger.record_failure(
                        task.task_id, process.returncode, entry["log"],
                        f"exit {process.returncode}: " + " | ".join(tail))
                    print(f"[run.py] FAILED {task.task_id} exit={process.returncode} "
                          f"log={entry['log']}", flush=True)
                    continue
                if not self.publish(task, index=entry["index"],
                                    completed=entry["index"] + 1):
                    groups[task.task_id]["failures"] += 1

            for task in list(groups):
                state = groups[task]
                task_state = state["task"]
                if state["children"] >= len(task_state.commands) and not any(
                        key[0] == task for key in running):
                    finish_group(task_state)
                    finished[task] = state

            # Scientific work may share the machine up to the worker count.
            while queue and len(running) < cfg.workers and not self.stop["requested"]:
                if time.monotonic() >= deadline:
                    budget_reached = True
                    break
                index = next((i for i, task in enumerate(queue) if not task.serial), None)
                if index is None:
                    break
                task = queue.pop(index)
                scheduled_any = True
                launch(task, 0)

            # An exclusive group starts only when nothing else is alive. Within the
            # group the process-isolated children run concurrently, exactly as the
            # measured MT-002 batches do.
            if not running and not groups and queue:
                if time.monotonic() >= deadline:
                    budget_reached = True
                else:
                    index = next((i for i, task in enumerate(queue) if task.serial), None)
                    if index is not None:
                        task = queue.pop(index)
                        scheduled_any = True
                        if task.parallel_children:
                            for child in range(len(task.commands)):
                                launch(task, child)
                        else:
                            launch(task, 0)

            if running:
                time.sleep(0.05)
            elif not queue and not groups:
                break

        if interrupted:
            reap_all()
            for (task_id, _), entry in running.items():
                self.ledger.record_pending(
                    task_id, f"interrupted by signal {self.stop['signal']}")
                print(f"[run.py] interrupted {task_id}; it stays pending", flush=True)
            running.clear()
        elif budget_reached:
            for task in queue:
                self.ledger.record_pending(task.task_id,
                                           "not scheduled: the wall-clock budget was exhausted")
        return {"scheduled_any": scheduled_any, "budget_reached": budget_reached,
                "interrupted": interrupted, "signal": self.stop["signal"]}


# ---------------------------------------------------------------------------
# Aggregation
# ---------------------------------------------------------------------------


def scalars(payload) -> dict:
    if not isinstance(payload, dict):
        return {}
    return {key: value for key, value in payload.items()
            if isinstance(value, (bool, int, float, str)) and not isinstance(value, bytes)}


def sanitise(value) -> tuple:
    """Replace non-finite numbers with null so a summary stays valid JSON.

    A non-finite density or error is a result the study must report, not hide, so
    the affected entry is flagged rather than silently rounded.
    """
    replaced = 0
    if isinstance(value, dict):
        out = {}
        for key, item in value.items():
            out[key], found = sanitise(item)
            replaced += found
        return out, replaced
    if isinstance(value, list):
        out = []
        for item in value:
            item, found = sanitise(item)
            out.append(item)
            replaced += found
        return out, replaced
    if isinstance(value, float) and (value != value or value in (float("inf"), float("-inf"))):
        return None, 1
    return value, replaced


def aggregate(cfg: RunConfig, ledger: Ledger, tasks: list[Task], plan: dict,
              execution: dict) -> dict:
    """Mechanical aggregation by kind. No scientific conclusion is computed here."""
    counts = {"total": len(tasks)}
    for status in ("completed", "failed", "pending", "running", "blocked"):
        counts[status] = sum(1 for task in tasks if ledger.status(task.task_id) == status)
    complete = (counts["completed"] == counts["total"] and counts["total"] > 0
                and not execution["interrupted"] and not execution["budget_reached"])

    by_kind: dict[str, list[Task]] = {}
    for task in tasks:
        by_kind.setdefault(task.kind, []).append(task)

    summaries = {}
    for kind, kind_tasks in sorted(by_kind.items()):
        entries = []
        kind_complete = True
        for task in kind_tasks:
            entry = {"task_id": task.task_id, "stage": task.stage, "model": task.model,
                     "candidate": task.candidate, "target": task.target,
                     "label": task.label, "status": ledger.status(task.task_id),
                     "params": task.params, "children": len(task.commands),
                     "deferred": task.deferred}
            record = ledger.data["tasks"].get(task.task_id, {})
            if entry["status"] == "completed":
                published = record.get("results") or []
                entry["results"] = published
                entry["group_wall_seconds"] = record.get("group_wall_seconds")
                payload = ledger.result_payload(task.task_id)
                if payload is not None:
                    entry["result_headline"] = scalars(payload)
                for item in published:
                    path = Path(item.get("path", ""))
                    if path.is_file() and path.stat().st_size <= SUMMARY_INLINE_BYTES:
                        child = ledger.load_result(path)
                        if child is not None:
                            clean, replaced = sanitise(child)
                            if replaced:
                                item["non_finite_values_nulled"] = replaced
                            entry.setdefault("results_payload", []).append(clean)
            else:
                kind_complete = False
                entry["failure_reason"] = (record.get("failure_reason")
                                           or record.get("blocked_reason")
                                           or record.get("note"))
            entries.append(entry)
        summaries[kind] = {
            "kind": kind,
            "generated_utc": utc_now(),
            "status": "complete" if kind_complete else "incomplete",
            "scientific_evidence": not cfg.verification_only,
            "verification_only": cfg.verification_only,
            "task_count": len(kind_tasks),
            "completed": sum(1 for e in entries if e["status"] == "completed"),
            "note": ("verbatim study results, mechanically grouped by kind. The runner "
                     "computes no error, accuracy or performance conclusion; RESULTS.md "
                     "derives those from these files."),
            "tasks": entries,
        }
        write_json_atomic(cfg.out / "evidence" / SUMMARY_FILES.get(kind, f"{kind}-summary.json"),
                          summaries[kind])

    pending = [task.task_id for task in tasks if ledger.status(task.task_id) != "completed"]
    failures = {task_id: ledger.data["tasks"][task_id].get("failure_reason")
                for task_id in pending
                if ledger.status(task_id) == "failed"}

    limitations = [
        "Every density, propagation and timing number in this directory is a verbatim "
        "study-executable result; run.py computes no scientific quantity.",
        "Process-isolated benchmark children own independent caches; interprocess reuse is "
        "never counted as a hit.",
        "A candidate configuration is propagated only after a measured accuracy stage; the "
        "selection policy and the ranking it used are recorded in the plan.",
    ]
    if execution["budget_reached"]:
        limitations.append(f"The wall-clock budget stopped scheduling; {len(pending)} task(s) "
                           "remain pending and were not executed.")
    if execution["interrupted"]:
        limitations.append(f"The run was interrupted by signal {execution['signal']}; "
                           "interrupted tasks stay pending.")
    if counts["failed"]:
        limitations.append(f"{counts['failed']} task(s) failed; their diagnostics are kept in "
                           "logs/ and their results are absent, not zero.")
    if plan.get("shortlist", {}).get("fallback"):
        limitations.append(plan["shortlist"]["fallback"])
    if cfg.verification_only:
        limitations.append("This run used --verification-only: at least one pinned input was "
                           "missing or did not match its pinned hash. The results verify the "
                           "pipeline and are not direct-v2 scientific or performance evidence.")
    for note in (plan.get("shortlist") or {}).get("notes", []):
        limitations.append(note)

    verification = read_json(cfg.out / "input-verification.json") or {}
    status = {
        "kind": "atm-cache-study-status",
        "schema_version": 1,
        "generated_utc": utc_now(),
        "status": "complete" if complete else "incomplete",
        "scientific_evidence": (not cfg.verification_only) and complete,
        "verification_only": cfg.verification_only,
        "counts": counts,
        # Dependency prerequisites are counted apart from task outcomes: a missing
        # or unverified input never becomes a failed, completed or zero-valued task.
        "dependency_blockers": verification.get("problems", []),
        "missing_inputs": verification.get("missing_inputs", []),
        "mismatched_inputs": verification.get("mismatched_inputs", []),
        "pending": pending,
        "failures": failures,
        "shortlist": plan.get("shortlist"),
        "stages": plan.get("stages"),
        "limitations": limitations,
    }
    write_json_atomic(cfg.out / "evidence" / "status.json", status)
    return status


def manifest(cfg: RunConfig, ledger: Ledger, tasks: list[Task], plan: dict,
             execution: dict, status: dict) -> dict:
    """Full provenance for every published artefact."""
    outputs = {}
    for path in sorted((cfg.out / "results").glob("*.json")):
        outputs[str(path.relative_to(cfg.out))] = sha256_file(path)
    traces = {}
    trace_dir = cfg.out / "traces"
    if trace_dir.is_dir():
        for path in sorted(trace_dir.glob("*")):
            if path.is_file():
                traces[str(path.relative_to(cfg.out))] = sha256_file(path)
    logs = {}
    log_dir = cfg.out / "logs"
    if log_dir.is_dir():
        for path in sorted(log_dir.glob("*")):
            if path.is_file():
                logs[str(path.relative_to(cfg.out))] = sha256_file(path)

    identity = ledger.data["identity"] or cfg.identity()
    completed = {task.task_id for task in tasks if ledger.status(task.task_id) == "completed"}
    document = {
        "kind": "atm-cache-study-manifest",
        "schema_version": 1,
        "generated_utc": utc_now(),
        "note": cfg.note,
        "mode": cfg.mode,
        "verification_only": cfg.verification_only,
        "scientific_evidence": status.get("scientific_evidence"),
        "input_verification": read_json(cfg.out / "input-verification.json"),
        "run": {
            "workers": cfg.workers,
            "max_hours": cfg.max_hours,
            "seed": cfg.seed,
            "scenarios": list(cfg.scenarios),
            "models": list(cfg.models),
            "targets": list(cfg.targets),
            "capacities": list(cfg.capacities),
            "points": cfg.points,
            "duration_s": cfg.duration_s,
            "cadence_s": cfg.cadence_s,
            "rtol": cfg.rtol,
            "max_step_s": cfg.max_step_s,
            "refine_rtol": cfg.refine_rtol,
            "refine_max_step_s": cfg.refine_max_step_s,
            "repeats": cfg.repeats,
            "epoch_utc": cfg.epoch_utc,
            "extra_args": list(cfg.extra_args),
            "scheduled_any": execution["scheduled_any"],
            "budget_reached": execution["budget_reached"],
            "interrupted": execution["interrupted"],
            "signal": execution.get("signal"),
        },
        "identity": identity,
        "project": {
            "git_sha": git("rev-parse", "HEAD"),
            "git_status": git("status", "--porcelain"),
            "git_describe": git("log", "-1", "--format=%H %cI %s"),
            "dirty": bool(git("status", "--porcelain")),
        },
        "toolchain": toolchain_identity(cfg.build_dir),
        "host": host_identity(),
        "host_commands": [
            {"command": ["uname", "-a"], "stdout": run_fact(["uname", "-a"])},
            {"command": ["nproc"], "stdout": run_fact(["nproc"])},
            {"command": ["free", "-h"], "stdout": run_fact(["free", "-h"])},
            {"command": ["cmake", "--version"], "stdout": run_fact(["cmake", "--version"])},
        ],
        "build": {"commands": build_commands(cfg),
                  "executed_by_runner": False,
                  "note": "the runner never builds; the standalone cache-study CMake root is "
                          "configured and built by the caller with these commands"},
        "kernels_and_data": identity.get("inputs", {}),
        "fixtures": [list(item) for item in FIXTURE_EPOCHS],
        "commands": ledger.data["commands"],
        "task_counts": {"planned": len(tasks), "completed": len(completed),
                        "by_status": ledger.counts()},
        "outputs": outputs,
        "traces": traces,
        "logs": logs,
        "external_artifact_directories": {
            "results": str(cfg.out / "results"),
            "traces": str(trace_dir),
            "logs": str(log_dir),
            "note": "large raw traces stay in these external directories and are referenced "
                    "by hash here; nothing bulky is written into the documentation tree",
        },
        "known_failures": status.get("failures", {}),
        "pending": status.get("pending", []),
        "limitations": status.get("limitations", []),
        "status": status.get("status"),
    }
    write_json_atomic(cfg.out / "evidence" / "manifest.json", document)
    return document


def run_fact(command) -> str | None:
    try:
        result = subprocess.run([str(part) for part in command], capture_output=True, text=True)
    except OSError as error:
        return f"unavailable: {error}"
    return result.stdout.strip() or None


def read_json(path):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return None


# ---------------------------------------------------------------------------
# Configuration plumbing
# ---------------------------------------------------------------------------


def parse_list(text: str, cast=str) -> tuple:
    items = []
    for part in str(text).split(","):
        part = part.strip()
        if part:
            items.append(cast(part))
    return tuple(items)


def resolve_case(value: str) -> str:
    name = value.strip().lower().replace("_", "-")
    if name in CASE_FILES:
        return name
    if value in ("a", "b", "c"):
        return f"case-{value}"
    raise SystemExit(f"unknown scenario {value}; choose from "
                     + ", ".join(sorted(CASE_FILES)) + " (or A/B/C)")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=("smoke", "screen", "full"),
                        help="smoke: both models through every stage at minimum size; "
                             "screen: sensitivity, locality and broad candidate screening; "
                             "full: the complete selected study")
    parser.add_argument("--resume", metavar="DIR",
                        help="continue an existing run directory without recomputing "
                             "completed work; equivalent to --mode with --out DIR")
    parser.add_argument("--out", type=Path, help="run directory (created if absent)")
    parser.add_argument("--workers", type=int, default=4,
                        help="concurrent scientific children; benchmark groups stay exclusive")
    parser.add_argument("--max-hours", type=float, default=24.0,
                        help="wall-clock budget; scheduling stops cleanly and completed work "
                             "is retained")
    parser.add_argument("--seed", type=int, default=20261014, help="deterministic study seed")
    parser.add_argument("--scenarios", default="case-a,case-b,case-c",
                        help="comma-separated CASE scenarios used by propagation")
    parser.add_argument("--kinds", default="sensitivity,locality,accuracy,check,propagation,benchmark",
                        help="comma-separated study kinds to plan")
    parser.add_argument("--models", default="nrlmsise00,jb2008")
    parser.add_argument("--candidates", default=",".join(CANDIDATE_ORDER),
                        help="candidate names for the accuracy grid")
    parser.add_argument("--targets", default=None,
                        help="comma-separated density-error targets in percent; default "
                             "0.01,0.1,0.5,1,5 (full), 0.1,1 (screen), 5 (smoke)")
    parser.add_argument("--capacities", default="4096,262144",
                        help="comma-separated cache capacities in entries")
    parser.add_argument("--interpolations", default="4d",
                        help="comma-separated candidate C interpolation axes: "
                             "altitude, time, spatial, 4d")
    parser.add_argument("--interpolation-axis", default="linear",
                        help="candidate C grid axis: linear or log")
    parser.add_argument("--capacity-policy", default="lru", choices=("lru", "fifo"),
                        help="cache replacement policy recorded with every configuration")
    parser.add_argument("--points", type=int, default=None,
                        help="sample count per scientific task")
    parser.add_argument("--repeats", type=int, default=None,
                        help="benchmark repetitions per serial task")
    parser.add_argument("--max-shortlist", type=int, default=4,
                        help="maximum screened configurations propagated per model")
    parser.add_argument("--shortlist-rel-error-percent", type=float, default=1.0,
                        help="screening threshold on measured worst-case relative density error")
    parser.add_argument("--study-extra", action="append", default=[], metavar="ARG",
                        help="extra argument appended verbatim to every study child; repeatable")
    parser.add_argument("--include-boundary-epochs", action="store_true",
                        help="add the leap-second, three-hourly Ap and hourly DTC boundary "
                             "epochs to the pinned five")
    parser.add_argument("--expect-sha256", action="append", default=[], metavar="NAME=SHA256",
                        help="expected SHA-256 of a pinned input (lsk, spk, pck, weather, "
                             "solfsmy, dtcfile, case-a, case-b, case-c); repeatable and "
                             "enforced before any primary task is planned")
    parser.add_argument("--pinned-input-hashes", type=Path, default=None,
                        help="JSON file of the same pinned input hashes")
    parser.add_argument("--verification-only", action="store_true",
                        help="pipeline verification with unverified or alternate inputs: "
                             "results are written under <out>/verification-only and are "
                             "never scientific or performance evidence")
    parser.add_argument("--note", help="free-text note stored in the manifest")

    parser.add_argument("--binary", type=Path, default=DEFAULT_BUILD_DIR / "atm_cache_study",
                        help="path to the atm_cache_study executable")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR,
                        help="CMake build tree of the study executable")
    parser.add_argument("--deps-dir", type=Path, default=DEFAULT_DEPS_DIR,
                        help="acquired external dependency root, recorded in the manifest")
    parser.add_argument("--cmake-prefix-path", default=None)
    parser.add_argument("--cspice-root", type=Path, default=Path("/tmp/leo_cspice/cspice"))
    parser.add_argument("--nrlmsise-root", type=Path, default=None)
    parser.add_argument("--jb2008-source", type=Path, default=None)
    parser.add_argument("--build-jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2),
                        help="parallelism recorded in the build command; never executed here")

    parser.add_argument("--lsk", type=Path, default=None)
    parser.add_argument("--spk", type=Path, default=None)
    parser.add_argument("--pck", type=Path, default=None)
    parser.add_argument("--frame", default="ITRF93")
    parser.add_argument("--weather", type=Path, default=None)
    parser.add_argument("--solfsmy", type=Path, default=None)
    parser.add_argument("--dtcfile", type=Path, default=None)
    parser.add_argument("--epoch-utc", default="2003-10-29T12:00:00",
                        help="shared trajectory epoch for locality and propagation tasks")
    parser.add_argument("--duration", type=float, default=None,
                        help="duration for sensitivity, locality, propagation and benchmark "
                             f"tasks; default {SIX_HOURS} s, and 30 s in smoke mode")
    parser.add_argument("--cadence", type=float, default=30.0)
    parser.add_argument("--rtol", type=float, default=1e-10)
    parser.add_argument("--max-step", type=float, default=15.0)
    parser.add_argument("--refine-rtol", type=float, default=1e-12)
    parser.add_argument("--refine-max-step", type=float, default=5.0)
    parser.add_argument("--show-build-commands", action="store_true",
                        help="print the standalone configure and build commands and exit")
    return parser


def defaults_for_mode(mode: str) -> dict:
    if mode == "smoke":
        return {"points": 512, "repeats": 1, "targets": [5.0], "capacity": [4096]}
    if mode == "screen":
        return {"points": 4096, "repeats": 2, "targets": [0.1, 1.0], "capacity": [4096, 262144]}
    return {"points": 16384, "repeats": 3, "targets": [0.01, 0.1, 0.5, 1.0, 5.0],
            "capacity": [4096, 262144]}


def find_dataset(root: Path, patterns) -> Path | None:
    for pattern in patterns:
        for path in sorted(root.glob(pattern)) if root.is_dir() else []:
            if path.is_file():
                return path
    return None


def resolve_inputs(args, out: Path) -> None:
    """Find the kernels and space-weather files without substituting anything.

    A dataset that cannot be located is left unset rather than replaced by a
    similar file: the primary-run gate then records the study as blocked and
    pending, which is the honest state, instead of running on an unknown input.
    """
    candidates = [out, Path("/tmp/leo-atmosphere-deps"), args.deps_dir or Path("."),
                  ROOT / "docs/atmosphere/evidence"]
    missing = []

    def locate(name, patterns, explicit):
        if explicit:
            path = Path(explicit)
            if not path.is_file():
                missing.append({"input": name, "path": str(path), "reason": "not a file"})
                return None
            return path
        for root in candidates:
            found = find_dataset(root, patterns)
            if found:
                return found
        missing.append({"input": name, "path": None,
                        "reason": f"not found in {[str(c) for c in candidates]}"})
        return None

    args.lsk = locate("lsk", ("tls*.bsp", "*leap*.bsp", "naif0012.tls"), args.lsk)
    args.spk = locate("spk", ("de*.bsp",), args.spk)
    args.weather = locate("weather", ("*space*weather*", "*cssi*", "*Weather*.txt"),
                          args.weather)
    args.solfsmy = locate("solfsmy", ("*SOLFSMY*", "*solfsmy*"), args.solfsmy)
    args.dtcfile = locate("dtcfile", ("*DTCFILE*", "*dtcfile*"), args.dtcfile)
    args.pck = locate("pck", ("earth_000101_270105_261009.bpc",), args.pck)
    args.unlocated_inputs = missing


def config_from_args(args) -> RunConfig:
    if args.mode is None:
        raise SystemExit("--mode is required unless --resume names a run directory")
    defaults = defaults_for_mode(args.mode)
    if args.duration is None:
        # A smoke run must finish quickly; every other mode uses the six-hour
        # window the study requires.
        args.duration = 30.0 if args.mode == "smoke" else SIX_HOURS
    targets = parse_list(args.targets, float) if args.targets else tuple(defaults["targets"])
    capacities = parse_list(args.capacities, int)
    if args.points is None:
        points = defaults["points"]
    else:
        points = args.points
    repeats = args.repeats if args.repeats is not None else defaults["repeats"]
    kinds = tuple(KIND_ALIASES.get(k.strip().lower(), k.strip().lower())
                  for k in args.kinds.split(",") if k.strip())
    scenarios = tuple(resolve_case(item) for item in args.scenarios.split(",") if item.strip())
    extra = tuple(part for item in args.study_extra for part in shlex.split(item))
    cfg = RunConfig(
        mode=args.mode,
        out=Path(args.out).resolve(),
        binary=Path(args.binary).resolve(),
        build_dir=Path(args.build_dir).resolve() if args.build_dir else None,
        deps_dir=Path(args.deps_dir).resolve() if args.deps_dir else None,
        lsk=Path(args.lsk).resolve() if args.lsk else None,
        spk=Path(args.spk).resolve() if args.spk else None,
        pck=Path(args.pck).resolve() if args.pck else None,
        frame=args.frame,
        weather=Path(args.weather).resolve() if args.weather else None,
        solfsmy=Path(args.solfsmy).resolve() if args.solfsmy else None,
        dtcfile=Path(args.dtcfile).resolve() if args.dtcfile else None,
        epoch_utc=args.epoch_utc,
        workers=max(1, args.workers),
        max_hours=args.max_hours,
        seed=args.seed,
        scenarios=scenarios,
        models=tuple(m.strip().lower() for m in args.models.split(",") if m.strip()),
        kinds=kinds,
        candidates=tuple(c.strip().lower() for c in args.candidates.split(",") if c.strip()),
        targets=targets,
        capacities=capacities,
        interpolations=tuple(item.strip().lower() for item in args.interpolations.split(",")
                             if item.strip()) or ("4d",),
        interpolation_axis=args.interpolation_axis,
        capacity_policy=args.capacity_policy,
        points=points,
        duration_s=args.duration,
        cadence_s=args.cadence,
        rtol=args.rtol,
        max_step_s=args.max_step,
        refine_rtol=args.refine_rtol,
        refine_max_step_s=args.refine_max_step,
        repeats=repeats,
        max_shortlist=args.max_shortlist,
        shortlist_rel_error_percent=args.shortlist_rel_error_percent,
        extra_args=extra,
        note=args.note,
        cmake_prefix_path=args.cmake_prefix_path,
        cspice_root=args.cspice_root,
        nrlmsise_root=args.nrlmsise_root,
        jb2008_source=args.jb2008_source,
        build_jobs=args.build_jobs,
        include_boundary_epochs=bool(args.include_boundary_epochs),
        verification_only=bool(args.verification_only),
        expected_hashes=parse_expectations(args.expect_sha256, args.pinned_input_hashes),
    )
    unknown = [kind for kind in cfg.kinds if kind not in
               ("sensitivity", "locality", "accuracy", "check", "propagation", "benchmark")]
    if unknown:
        raise SystemExit(f"unknown study kind(s): {', '.join(unknown)}")
    return cfg


def load_resume_configuration(out: Path, args) -> dict:
    """A resumed run takes its plan parameters from the recorded ledger."""
    ledger_path = out / "ledger.json"
    if not ledger_path.is_file():
        raise RunnerError(f"--resume {out} has no ledger.json; nothing to resume")
    recorded = json.loads(ledger_path.read_text())
    parameters = (recorded.get("plan") or {}).get("parameters")
    if not parameters:
        raise RunnerError("the ledger records no plan parameters; resume cannot reproduce it")
    return recorded


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def prepare_out(path: Path, resume: bool) -> Path:
    path = Path(path).resolve()
    if resume:
        if not path.is_dir():
            raise RunnerError(f"--resume directory does not exist: {path}")
        return path
    path.mkdir(parents=True, exist_ok=True)
    return path


def resume_command(args, out: Path, recorded: dict, parser) -> RunConfig:
    parameters = (recorded.get("plan") or {})["parameters"]
    mode = recorded.get("mode") or parameters["mode"]
    args.mode = mode
    args.out = out
    if args.targets is None:
        args.targets = ",".join(number(value) for value in parameters["targets"])
    if args.points is None:
        args.points = parameters["points"]
    if args.repeats is None:
        args.repeats = parameters["repeats"]
    args.capacities = ",".join(str(value) for value in parameters["capacities"])
    args.candidates = ",".join(parameters["candidates"])
    args.models = ",".join(parameters["models"])
    args.kinds = ",".join(parameters["kinds"])
    args.scenarios = ",".join(parameters["scenarios"])
    args.seed = parameters["seed"]
    args.epoch_utc = parameters["epoch_utc"]
    args.frame = parameters["frame"]
    args.duration = parameters["duration_s"]
    args.cadence = parameters["cadence_s"]
    args.rtol = parameters["rtol"]
    args.max_step = parameters["max_step_s"]
    args.refine_rtol = parameters["refine_rtol"]
    args.refine_max_step = parameters["refine_max_step_s"]
    args.max_shortlist = parameters["max_shortlist"]
    args.shortlist_rel_error_percent = parameters["shortlist_rel_error_percent"]
    args.study_extra = [shlex.join(part) for part in parameters.get("extra_args", [])]
    args.interpolations = ",".join(parameters["interpolations"])
    args.interpolation_axis = parameters["interpolation_axis"]
    args.capacity_policy = parameters["capacity_policy"]
    args.include_boundary_epochs = parameters.get("include_boundary_epochs", False)
    args.verification_only = parameters.get("verification_only", False)
    args.expect_sha256 = [f"{name}={digest}" for name, digest
                          in sorted(parameters.get("expected_hashes", {}).items())]
    args.pinned_input_hashes = None
    # Inputs are taken from the recorded identity unless the caller overrides
    # them explicitly; a changed input then shows up as an identity difference.
    identity = recorded.get("identity") or {}
    # The binary and build tree the run actually used are part of the recorded
    # identity; a resume reuses them unless the caller overrides them.
    recorded_binary = (identity.get("binary") or {}).get("path")
    if recorded_binary and args.binary == DEFAULT_BUILD_DIR / "atm_cache_study":
        args.binary = Path(recorded_binary)
    build = identity.get("build") or {}
    if build.get("build_dir") and args.build_dir == DEFAULT_BUILD_DIR:
        args.build_dir = Path(build["build_dir"])
    if build.get("deps_dir") and args.deps_dir == DEFAULT_DEPS_DIR:
        args.deps_dir = Path(build["deps_dir"])
    recorded_inputs = identity.get("inputs") or {}
    launch = (recorded.get("plan") or {}).get("launch") or {}
    recorded_inputs = recorded_inputs or launch.get("inputs") or {}
    if launch.get("binary") and args.binary == DEFAULT_BUILD_DIR / "atm_cache_study":
        args.binary = Path(launch["binary"])
    if launch.get("build_dir") and args.build_dir == DEFAULT_BUILD_DIR:
        args.build_dir = Path(launch["build_dir"])
    if launch.get("deps_dir") and args.deps_dir == DEFAULT_DEPS_DIR:
        args.deps_dir = Path(launch["deps_dir"])
    for flag, name in (("lsk", "lsk"), ("spk", "spk"), ("pck", "pck"), ("weather", "weather"),
                       ("solfsmy", "solfsmy"), ("dtcfile", "dtcfile")):
        entry = recorded_inputs.get(name)
        if getattr(args, flag) is not None:
            continue
        if isinstance(entry, dict) and entry.get("path"):
            setattr(args, flag, Path(entry["path"]))
        elif isinstance(entry, str) and entry:
            setattr(args, flag, Path(entry))
    resolve_inputs(args, out)
    cfg = config_from_args(args)
    cfg.mode = mode
    return cfg


def verify_resume(cfg: RunConfig, ledger: Ledger, tasks: list[Task],
                  scheduler: Scheduler) -> dict:
    """Reliability check: a second planning pass must schedule nothing.

    Used by smoke mode to prove the no-repeat guarantee on the real runner with
    the real plan, without executing any child.
    """
    repeated = []
    for task in tasks:
        if not scheduler.completed_is_intact(task):
            repeated.append(task.task_id)
    return {"tasks": len(tasks), "would_reexecute": repeated,
            "passed": not repeated}


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    resume = bool(args.resume)
    if not resume and args.out is None:
        raise SystemExit("--out is required")

    if args.show_build_commands:
        cfg_probe = config_from_args(args) if args.out else None
        commands = build_commands(cfg_probe) if cfg_probe else [
            " ".join(shlex.quote(part) for part in
                     ["cmake", "-S", str(HERE), "-B", str(args.build_dir),
                      "-DCMAKE_BUILD_TYPE=Release"])]
        for command in commands:
            print(command)
        return EXIT_OK

    out = prepare_out(Path(args.resume or args.out), resume)
    recorded = load_resume_configuration(out, args) if resume else None
    if resume:
        cfg = resume_command(args, out, recorded, parser)
        args.mode = cfg.mode
    else:
        resolve_inputs(args, out)
        cfg = config_from_args(args)
        if cfg.verification_only:
            # Verification results are kept in their own namespace so they can
            # never be read, hashed or published as scientific evidence.
            cfg.out = cfg.out / "verification-only"

    verification = verify_pinned_inputs(cfg)
    cfg.out.mkdir(parents=True, exist_ok=True)
    write_json_atomic(cfg.out / "input-verification.json", verification)

    if verification["blocked"]:
        # The primary study cannot start on these inputs. The complete plan is
        # still recorded, every task is marked blocked and pending, and no input is
        # replaced and no command is executed.
        ledger = Ledger(cfg.out / "ledger.json")
        ledger.load()
        planner = Planner(cfg)
        tasks, plan = planner.plan(ledger)
        ledger.set_plan(cfg.mode, {"parameters": cfg.plan_parameters(),
                                   "stages": ["sensitivity+locality", "accuracy", "coupled",
                                              "benchmark", "aggregate"],
                                   "launch": {
                                       "binary": str(cfg.binary),
                                       "build_dir": str(cfg.build_dir) if cfg.build_dir else None,
                                       "deps_dir": str(cfg.deps_dir) if cfg.deps_dir else None,
                                       "inputs": {name: str(getattr(cfg, name))
                                                  for name in ("lsk", "spk", "pck", "weather",
                                                               "solfsmy", "dtcfile")
                                                  if getattr(cfg, name)}}})
        for task in tasks:
            ledger.record_task(task)
            entry = ledger.data["tasks"][task.task_id]
            entry["status"] = "completed" if entry.get("status") == "completed" else "blocked"
            entry["blocked_by"] = [problem.split(":")[0] for problem in verification["problems"]]
            entry["note"] = ("primary prerequisites are missing or do not match their pinned "
                             "hash; nothing was executed and the task remains pending")
        ledger.data["plan"]["shortlist"] = plan.get("shortlist")
        ledger.save()
        write_json_atomic(cfg.out / "plan.json",
                          {**plan, "mode": cfg.mode, "parameters": cfg.plan_parameters(),
                           "tasks": [task.identity() for task in tasks]})
        blocked = {
            "kind": "atm-cache-study-blocked",
            "generated_utc": utc_now(),
            "state": "blocked: the full plan is recorded and every task is pending",
            "problems": verification["problems"],
            "missing_inputs": verification["missing_inputs"],
            "mismatched_inputs": verification["mismatched_inputs"],
            "unlocated_inputs": getattr(args, "unlocated_inputs", []),
            "planned_tasks": len(tasks),
            "executed_tasks": 0,
            "previously_completed_tasks": sum(
                entry.get("status") == "completed" for entry in ledger.data["tasks"].values()),
            "input_verification": verification,
            "resume_command": (f"python3 {HERE / 'run.py'} --resume {cfg.out} "
                               f"--workers {cfg.workers} --max-hours {cfg.max_hours}"),
            "verification_command": (f"python3 {HERE / 'run.py'} --mode smoke --verification-only "
                                     f"--out {cfg.out.parent} --workers {cfg.workers}"),
            "note": ("pinned inputs were not replaced and no alternate input was substituted; "
                     "the study stays pending until the pinned files are available again. A "
                     "--verification-only run may be used to check the pipeline, and its "
                     "outputs are not scientific or performance evidence."),
        }
        write_json_atomic(cfg.out / "blocked.json", blocked)
        write_json_atomic(cfg.out / "evidence/status.json", {
            "kind": "atm-cache-study-status",
            "schema_version": 1,
            "generated_utc": utc_now(),
            "status": "blocked",
            "scientific_evidence": False,
            "verification_only": False,
            "counts": {"total": len(tasks), "completed": 0, "failed": 0,
                       "pending": 0, "running": 0, "blocked": len(tasks)},
            "pending": [task.task_id for task in tasks],
            "failures": {},
            "problems": verification["problems"],
            "dependency_blockers": verification["problems"],
            "missing_inputs": verification["missing_inputs"],
            "mismatched_inputs": verification["mismatched_inputs"],
            "shortlist": plan.get("shortlist"),
            "stages": plan.get("stages"),
            "limitations": ["no task was executed on this invocation: the pinned prerequisites are "
                            "missing or do not match their pinned hash; previously completed task "
                            "records are preserved and are not re-executed"],
        })
        print(f"[run.py] BLOCKED: {len(verification['problems'])} prerequisite problem(s); "
              f"{len(tasks)} task(s) recorded pending in {cfg.out / 'ledger.json'}",
              flush=True)
        for problem in verification["problems"]:
            print(f"[run.py]   {problem}", flush=True)
        print(f"[run.py] resume when the pinned inputs are restored:\n  "
              f"  {blocked['resume_command']}", flush=True)
        return EXIT_BLOCKED

    if not cfg.binary.is_file():
        raise SystemExit(f"study executable not found: {cfg.binary}\n"
                         f"configure and build it first; for example\n  "
                         + "\n  ".join(build_commands(cfg)))
    if cfg.verification_only:
        print("[run.py] VERIFICATION-ONLY run: results are pipeline verification, never "
              "scientific or performance evidence", flush=True)

    out = cfg.out
    ledger = Ledger(out / "ledger.json")
    ledger.load()
    ledger.bind(cfg.identity(), cfg.mode, {"parameters": cfg.plan_parameters(),
                                           "stages": ["sensitivity+locality", "accuracy",
                                                      "coupled", "benchmark", "aggregate"]})
    planner = Planner(cfg)
    deadline = time.monotonic() + cfg.max_hours * 3600.0
    scheduler = Scheduler(cfg, ledger, [])
    scheduler.install_signal_handlers()
    tasks: list[Task] = []
    plan: dict = {}
    execution = {"scheduled_any": False, "budget_reached": False, "interrupted": False,
                 "signal": None}

    # Stages are planned and scheduled in order. The coupled and performance
    # stages are re-planned once the screening stage has actually finished, so a
    # candidate is only ever propagated or benchmarked after its own measured
    # accuracy evidence exists.
    replan = True
    screening_replanned = False
    attempted_this_run = set()
    while replan:
        replan = False
        tasks, plan = planner.plan(ledger, defer_missing=True)
        planned_ids = {task.task_id for task in tasks}
        for task in tasks:
            ledger.record_task(task)
        # Placeholder records that the new plan no longer contains are dropped:
        # they were never executed and the real tasks replace them.
        for task_id, entry in list(ledger.data["tasks"].items()):
            if (entry.get("identity") or {}).get("deferred") and task_id not in planned_ids:
                del ledger.data["tasks"][task_id]
                ledger.data.setdefault("superseded", []).append(
                    {"task_id": task_id, "superseded_utc": utc_now(),
                     "reason": "the stage is no longer blocked; real tasks were planned"})
        ledger.data["plan"]["shortlist"] = plan.get("shortlist")
        ledger.save()
        write_json_atomic(out / "plan.json",
                          {**plan, "mode": cfg.mode, "parameters": cfg.plan_parameters(),
                           "tasks": [task.identity() for task in tasks]})

        # Completed work is verified, never recomputed: a missing or altered result
        # puts its task back into the queue and is reported.
        queue = []
        for task in tasks:
            entry = ledger.data["tasks"][task.task_id]
            if task.task_id in attempted_this_run and entry.get("status") == "failed":
                continue
            if task.deferred or not task.commands:
                # A blocked task is recorded as blocked and never executed.
                if entry.get("status") != "blocked":
                    ledger.record_blocked(task.task_id, task.deferred or "not scheduled")
                continue
            if scheduler.completed_is_intact(task):
                for item in entry.get("results") or []:
                    payload = ledger.load_result(item["path"])
                    if payload is not None:
                        ledger.cache_result(task.task_id, payload)
                entry["note"] = "verified complete"
            else:
                if entry.get("status") == "completed":
                    ledger.record_pending(task.task_id,
                                          "published result missing or hash mismatch; re-run")
                queue.append(task)
        ledger.save()

        if queue:
            print(f"[run.py] mode={cfg.mode} workers={cfg.workers} budget={cfg.max_hours}h "
                  f"planned={len(tasks)} outstanding={len(queue)}", flush=True)
            scheduler.tasks = queue
            attempted_this_run.update(task.task_id for task in queue)
            execution = scheduler.run(deadline)
            execution["signal"] = scheduler.stop["signal"]
            execution["scheduled_any"] = execution["scheduled_any"] or True

        replan = (not screening_replanned and not execution["interrupted"] and not execution["budget_reached"]
                  and bool(plan.get("blocked_stages"))
                  and screening_finished(tasks, ledger)
                  and time.monotonic() < deadline)
        if replan:
            screening_replanned = True
        if replan:
            print("[run.py] screening stage finished; re-planning the coupled and performance "
                  "stages from the measured results", flush=True)

    if not execution["scheduled_any"]:
        print("[run.py] nothing to execute: every planned task is complete or recorded blocked",
              flush=True)

    reliability = verify_resume(cfg, ledger, tasks, scheduler)
    write_json_atomic(out / "evidence" / "reliability.json",
                      {"generated_utc": utc_now(), "verified_utc": utc_now(),
                       "resume_no_repeat": reliability,
                       "note": "a completed task is trusted only while its published result "
                               "exists and still matches the recorded SHA-256"})
    ledger.data["plan"]["shortlist"] = plan.get("shortlist")
    ledger.save()

    status = aggregate(cfg, ledger, tasks, ledger.data["plan"], execution)
    manifest(cfg, ledger, tasks, ledger.data["plan"], execution, status)

    print(f"[run.py] status={status['status']} counts={status['counts']}", flush=True)
    if status["status"] != "complete":
        print(f"[run.py] incomplete: {len(status['pending'])} task(s) pending or blocked; "
              f"resume with\n  python3 {Path(__file__).name} --resume {out}", flush=True)
    print(f"[run.py] manifest {out / 'evidence' / 'manifest.json'}", flush=True)
    if execution["interrupted"]:
        return EXIT_INTERRUPTED
    return EXIT_OK if status["status"] == "complete" else EXIT_INCOMPLETE


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RunnerError as error:
        print(f"error: {error}", file=sys.stderr, flush=True)
        sys.exit(EXIT_USAGE)