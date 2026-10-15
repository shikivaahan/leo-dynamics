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
    and own the machine outright: no scientific child starts while an exclusive
    group is alive, so a timing measurement is never contaminated by a sibling
    sweep,
  * the pinned input set is explicit: direct-v2 is the immutable default and
    ``--input-set MANIFEST`` selects a separately pinned set whose id travels
    into every child command line, the plan, the identity and the manifest,
    labelled scientific-experimental-pinned and never direct-v2,
  * the accuracy stage is progressive: a family is screened coarse to fine and
    small capacity to large, and a rung behind it is only executed when the
    cheaper rung in front of it was measured and missed the threshold,
  * a coupled configuration is only propagated and benchmarked after its own
    measured screening and its own correctness checks have cleared,
  * ``--reuse-native DIR`` imports results from existing evidence only after
    their pinned bytes, native sources, switches, coordinate grids and seed
    match; an imported result never completes a planned task and a native-input
    slice never stands in for provider, JB2008, coupled or performance evidence.

Usage examples:

  python3 docs/atmosphere/cache-study/run.py --mode smoke --workers 4 --out /tmp/cache-smoke
  python3 docs/atmosphere/cache-study/run.py --mode full --workers 8 --max-hours 8 --out /tmp/cache-study
  python3 docs/atmosphere/cache-study/run.py --resume /tmp/cache-study
  python3 docs/atmosphere/cache-study/run.py --mode full --out /tmp/cache-exp \
      --input-set docs/atmosphere/cache-study/input-sets/exp-solfsmy-20261011-original-pck.json
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

# The historical primary input set. It is the default and it is immutable: no
# flag changes what direct-v2 means, a separately pinned set is selected
# explicitly by name and always keeps its own identity.
DEFAULT_INPUT_SET = "direct-v2"
INPUT_SETS_DIR = HERE / "input-sets"
# The authoritative native sources whose bytes a reused result depends on. They
# are hashed for --reuse-native so a reused measurement can never be attributed
# to a different NRLMSISE-00 or JB2008 build.
NATIVE_SOURCE_NAMES = ("nrlmsise-00.c", "nrlmsise-00.h", "nrlmsise-00_data.c", "JB2008.f")

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
# The last four rungs are the fine settings the nearest-neighbour and adaptive
# candidates need before a verdict can be called on them: a candidate is never
# written off because a coarse grid missed, only because the whole declared
# ladder was screened and missed. They are what lets the 0.1 % and 0.01 %
# budgets be answered by measured refinement instead of by extrapolation.
COARSE_TO_FINE = [
    (25.0, 5.0, 60.0),
    (10.0, 2.0, 10.0),
    (5.0, 1.0, 2.0),
    (0.25, 0.25, 1.0),
    (0.1, 0.1, 0.1),
    (0.025, 0.025, 0.05),
    (0.0025, 0.005, 0.005),
]

# The density-error budgets every screen is judged against, in percent. They are
# reported by one scoring per configuration; only the adaptive candidate's own
# reconstruction target changes what it measures, and that is a separate knob.
REPORTED_BUDGETS_PERCENT = (0.01, 0.1, 0.5, 1.0, 5.0)

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
    # Explicitly selected pinned input set. direct-v2 is the default and is
    # never relabelled; a selected set keeps its own id, manifest hash and
    # classification everywhere it is recorded.
    input_set: dict
    reuse_dirs: tuple
    reuse_provenance: tuple
    native_source_dir: Path | None
    max_screening_fallback_fraction: float
    max_replans: int
    # None means "screen every requested budget"; a value is an explicit,
    # caller-narrowed selection for the adaptive candidate only.
    adaptive_target_percent: float | None
    # None means "stop refining a family once it meets the screening threshold";
    # a value keeps the family's ladder advancing until that budget is scored or
    # the declared ladder is exhausted, so the tight budgets are answered by
    # measurement instead of by extrapolation.
    refine_to_budget_percent: float | None

    def advance_threshold_percent(self) -> float:
        """The budget a family must meet before its ladder stops being refined."""
        if self.refine_to_budget_percent is not None:
            return self.refine_to_budget_percent
        return self.shortlist_rel_error_percent

    # Everything below participates in the configuration hash that a resume must
    # reproduce. Worker count and wall-clock budget deliberately do not: raising
    # the worker count or extending the budget on a resumed run is legitimate.
    def plan_parameters(self) -> dict:
        return {
            "mode": self.mode,
            "verification_only": self.verification_only,
            "input_set": {"id": self.input_set["id"],
                          "manifest_sha256": self.input_set.get("manifest_sha256"),
                          "classification": self.input_set.get("classification")},
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
            "max_screening_fallback_fraction": self.max_screening_fallback_fraction,
            "adaptive_target_percent": self.adaptive_target_percent,
            "refine_to_budget_percent": self.refine_to_budget_percent,
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
            "input_set": dict(self.input_set),
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


def parse_expectations(entries, pinned_file, input_set=None) -> dict:
    """Collect the expected SHA-256 of every pinned scientific input.

    The runner never re-pins a dataset: the caller states the hashes of the
    pinned kernels, space-weather archive and CASE definitions, or selects a
    named input set whose manifest already pins them, and a primary smoke,
    screen or full run refuses to start unless every stated hash matches.

    A selected input set is the pin. Command-line and file overrides are
    refused next to it, because a hash the caller typed over a pinned manifest
    is a different, unrecorded experiment wearing the manifest's name.
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
    if input_set and input_set.get("explicitly_selected"):
        if entries or pinned_file:
            raise SystemExit(
                "--input-set is the pin for this run: --expect-sha256 and "
                "--pinned-input-hashes cannot override a selected input set. Select the "
                "historical direct-v2 default instead, or edit the input-set manifest.")
        for name in REQUIRED_INPUTS:
            expectations[name] = input_set["inputs"][name]["sha256"]
        # The CASE definitions are study scenarios, not model inputs: a selected
        # input set that names them wins, otherwise the direct-v2 scenario pins
        # stand and are still enforced.
        for name, entry in (input_set["inputs"] or {}).items():
            if name not in REQUIRED_INPUTS and entry.get("sha256"):
                expectations[name] = entry["sha256"]
        return expectations
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


def default_input_set() -> dict:
    """The immutable historical primary, described without inventing pins."""
    baseline = ROOT / "docs/atmosphere/evidence/direct-v2/manifest.json"
    return {"id": DEFAULT_INPUT_SET,
            "classification": "historical-primary",
            "manifest": str(baseline),
            "manifest_sha256": sha256_file(baseline) if baseline.is_file() else None,
            "inputs": {name: {"filename": None, "sha256": None}
                       for name in REQUIRED_INPUTS},
            "explicitly_selected": False,
            "rule": ("direct-v2 is the historical primary input set; it is the default, it is "
                     "never relabelled and no flag changes what it means")}


def load_input_set(path) -> dict:
    """Read an explicitly selected pinned input set.

    The manifest is the pin: it names the id the study executable verifies and
    the exact hash of every primary input. A manifest that does not pin every
    required input is refused rather than completed from another source, because
    a partially pinned set would silently measure a mixture.
    """
    path = Path(path).resolve()
    if not path.is_file():
        raise SystemExit(f"--input-set manifest not found: {path}")
    document = json.loads(path.read_text())
    identifier = str(document.get("id") or "").strip()
    if not identifier:
        raise SystemExit(f"--input-set manifest names no id: {path}")
    inputs = document.get("inputs") or {}
    if not isinstance(inputs, dict):
        raise SystemExit(f"--input-set manifest has no inputs object: {path}")
    pinned = {}
    for name in REQUIRED_INPUTS:
        entry = inputs.get(name)
        if not isinstance(entry, dict):
            raise SystemExit(f"--input-set {identifier} does not pin {name}: {path}")
        digest = str(entry.get("sha256") or "").lower()
        if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise SystemExit(f"--input-set {identifier} pins {name} with a malformed sha256: {digest!r}")
        pinned[name] = {"filename": entry.get("filename"), "sha256": digest}
    extra = sorted(set(inputs) - set(REQUIRED_INPUTS))
    for name in extra:
        entry = inputs.get(name) or {}
        pinned[name] = {"filename": entry.get("filename"),
                        "sha256": str(entry.get("sha256") or "").lower() or None}
    return {"id": identifier,
            "classification": document.get("classification") or "separately-pinned-experimental",
            "manifest": str(path),
            "manifest_sha256": sha256_file(path),
            "inputs": pinned,
            "native_sources": document.get("native_sources") or {},
            "configuration": document.get("configuration") or {},
            "historical_reference": document.get("historical_reference") or {},
            "explicitly_selected": True,
            "rule": ("an explicitly selected input set pins every primary input by hash; the study "
                     "executable verifies the same id before it measures anything, and the "
                     "evidence is labelled scientific-experimental-pinned, never direct-v2")}


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
        "input_set": dict(cfg.input_set),
        "evidence_class": evidence_class(cfg),
    }
    report["outcome"] = (
        "refused: a pinned input is missing or does not match its expected hash; every "
        "primary task is recorded blocked and pending, no input was replaced"
        if blocked else
        ("verified: every supplied pinned hash matches" if not problems else
         "verification-only: pinned inputs are missing or differ, so the outputs verify "
         "the pipeline and are never scientific or performance evidence"))
    return report


def evidence_class(cfg: RunConfig) -> str:
    """The single label every artefact of this run carries.

    A selected input set is a scientific experiment measured on its own pinned
    bytes; it is never presented as the historical direct-v2 baseline, and a
    pipeline verification run is never presented as science.
    """
    if cfg.verification_only:
        return "pipeline-verification-only"
    if cfg.input_set.get("explicitly_selected"):
        return "scientific-experimental-pinned"
    return "scientific-primary"


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


def screening_families(cfg: RunConfig) -> list[dict]:
    """The candidate families the accuracy stage screens, in a fixed order.

    A family is one candidate on one interpolation axis. The exact reference and
    the preprocessing-only control are fixed configurations: they have no
    resolution grid, so the single screen they get reports every error budget at
    once. The adaptive candidate's reconstruction depends on its numeric target,
    so the target is part of its identity and is screened per target. Every
    other candidate is screened once per family and its published thresholds
    carry all budgets, so no budget is ever a second copy of the same screen.
    """
    capacities = list(cfg.capacities)
    resolutions = list(COARSE_TO_FINE)
    if cfg.mode == "smoke":
        resolutions = resolutions[:1]
        capacities = capacities[:1]
    elif cfg.mode == "screen":
        resolutions = resolutions[:2]
        capacities = capacities[:1]
    policy = {"capacity_policy": cfg.capacity_policy, "interpolation_axis": cfg.interpolation_axis}
    families: list[dict] = []
    for candidate in cfg.candidates:
        if candidate not in RESOLVED_CANDIDATES:
            families.append({"candidate": candidate, "interpolation": None,
                             "resolutions": [None], "capacities": capacities[:1],
                             "targets": [None], "policy": policy,
                             "fixed_configuration": True,
                             "rule": ("fixed configuration: no reconstruction target applies, "
                                      "and one screen reports every error budget")})
            continue
        axes = cfg.interpolations if candidate == "interp" else cfg.interpolations[:1]
        for axis in axes:
            families.append({
                "candidate": candidate, "interpolation": axis,
                "resolutions": resolutions, "capacities": capacities,
                # The adaptive reconstruction depends on its numeric target, so
                # every requested budget is a distinct configuration of that
                # family. The ladder walks them from the strictest budget to the
                # loosest one, so a looser target is only tried when the stricter
                # one was measured and missed. --adaptive-target narrows this
                # explicitly; it is never applied silently.
                "targets": (list(cfg.targets) if cfg.adaptive_target_percent is None
                            else [cfg.adaptive_target_percent])
                           if candidate == "adaptive" else [None],
                "policy": policy, "fixed_configuration": False,
                "rule": ("the adaptive reconstruction depends on its numeric target, so the "
                         "target is part of this configuration's identity; every other "
                         "configuration is screened once and reports all budgets")})
    return families


def screening_configurations(cfg: RunConfig, family: dict) -> list[dict]:
    """One family's progressive ladder: resolution coarse to fine, capacity small to large.

    Every rung carries its own identity, so a configuration is only ever compared
    against itself across models and weather epochs. The ladder is ordered by
    cost: each rung is either a finer resolution or a larger cache, and both
    cost more than the rung in front of them.
    """
    rungs: list[dict] = []
    for resolution in family["resolutions"]:
        cell_km, cell_deg, cell_s = resolution if resolution else (None, None, None)
        for capacity in family["capacities"]:
            for target in family["targets"]:
                entry = dict(family["policy"])
                entry["capacity"] = capacity
                entry["target"] = target
                if family["interpolation"] is not None:
                    entry.update({"cell_km": cell_km, "cell_deg": cell_deg, "cell_s": cell_s,
                                  "interpolation": family["interpolation"]})
                rungs.append(entry)
    return rungs


def recorded_accuracy_screens(ledger: "Ledger", model: str, group: str) -> dict:
    """The accuracy screens already recorded for one weather configuration.

    Keyed by the configuration identity, so a completed screen is found for the
    exact settings that produced it and never for a neighbouring rung.
    """
    screens: dict[tuple, list[str]] = {}
    for task_id, entry in (ledger.data.get("tasks") or {}).items():
        if entry.get("kind") != "accuracy":
            continue
        params = ((entry.get("identity") or {}).get("params") or {})
        if params.get("model") != model or params.get("group") != group:
            continue
        screens.setdefault(configuration_key(params), []).append(task_id)
    return screens


def screen_outcome(ledger: "Ledger", key: tuple, threshold: float) -> str:
    """``unrecorded``, ``passed`` or ``failed`` for one exact configuration.

    A rung that has not been measured is not a failed rung: the rung behind it
    waits until its own screen exists, which is what keeps the ladder
    progressive instead of executing the whole product up front.
    """
    measured = False
    for task_id, entry in (ledger.data.get("tasks") or {}).items():
        if entry.get("kind") != "accuracy" or entry.get("status") != "completed":
            continue
        params = ((entry.get("identity") or {}).get("params") or {})
        if configuration_key(params) != key:
            continue
        screen = screen_accuracy(params, ledger.result_payload(task_id))
        value = screen.get("worst_relative_percent")
        measured = True
        if value is None or screen.get("problems") or value > threshold:
            return "failed"
    return "passed" if measured else "unrecorded"


def accuracy_candidate_grid(cfg: RunConfig, ledger: "Ledger | None" = None,
                            model: str | None = None, group: str | None = None) -> list[tuple[str, dict]]:
    """The screening configurations this weather configuration still has to measure.

    Progressive rather than Cartesian: a rung is executed when it has never been
    recorded, or when the cheaper rung in front of it was screened and missed
    the threshold. A rung that already meets the threshold ends its family's
    progression, so refining past it would buy accuracy nobody asked for at a
    cost nobody needs. A recorded rung always stays in the plan so its evidence
    is never dropped from a resumed run.
    """
    grid: list[tuple[str, dict]] = []
    if ledger is None:
        return [(family["candidate"], rung)
                for family in screening_families(cfg)
                for rung in screening_configurations(cfg, family)]
    recorded = recorded_accuracy_screens(ledger, model, group)
    for family in screening_families(cfg):
        rungs = screening_configurations(cfg, family)
        for index, rung in enumerate(rungs):
            key = (model, family["candidate"], group) + tuple(
                rung.get(name) for name in RESOLUTION_KEYS + POLICY_KEYS + ("target",))
            if key in recorded or index == 0:
                grid.append((family["candidate"], rung))
                continue
            previous_key = (model, family["candidate"], group) + tuple(
                rungs[index - 1].get(name) for name in RESOLUTION_KEYS + POLICY_KEYS + ("target",))
            if screen_outcome(ledger, previous_key, cfg.advance_threshold_percent()) == "failed":
                grid.append((family["candidate"], rung))
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
    if cfg.input_set.get("explicitly_selected"):
        # The executable verifies this id against the bytes it actually reads and
        # refuses anything it does not recognise, so a selected set can never be
        # measured under a different id than the one recorded here.
        command += ["--study-input-set", cfg.input_set["id"]]
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

    def __init__(self, cfg: RunConfig, ledger: "Ledger | None" = None):
        self.cfg = cfg
        self.ledger = ledger
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
        """One task per weather epoch and screened configuration.

        A configuration is identified by its model, candidate, weather
        configuration, cell spacing, capacity, interpolation policy and, for the
        adaptive candidate, its numeric target: those are the parameters that
        define the implementation, and the shortlist later selects whole
        configurations rather than candidate names.
        """
        cfg = self.cfg
        tasks: list[Task] = []
        for model, group, epochs, ap_history, accuracy_names in weather_groups(cfg):
            selected = [(name, utc) for name, utc in epochs if name in accuracy_names]
            if not selected:
                continue
            grid = accuracy_candidate_grid(cfg, self.ledger, model, group)
            for fixture_name, fixture_utc in selected:
                for candidate, resolution in grid:
                    # --study-target is the adaptive candidate's own numeric
                    # reconstruction target. For every other candidate no target
                    # is sent at all: one screen scores all five budgets against
                    # the maximum observed error, and re-screening the same grid
                    # per budget would measure the same thing five times.
                    target_percent = resolution["target"]
                    target = None if target_percent is None else target_percent / 100.0
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
                              **{key: value for key, value in resolution.items()
                                 if key not in ("target", "target_percent")}}
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
            task_id = self.unique(check_task_id(cfg, configuration))
            # The check runs in the same weather mode the configuration was
            # screened in: a candidate proved correct under daily Ap is not
            # thereby proved correct under the three-hourly Ap history.
            ap_history = bool(configuration.get("ap_history"))
            command = base_command(cfg, f"tasks/{task_id}", [], cfg.scenarios[:1],
                                   cfg.epoch_utc, cfg.duration_s, ap_history)
            params = {"kind": "check", "model": model, "candidate": candidate,
                      "group": configuration.get("group"),
                      "points": cfg.points, "seed": cfg.seed, "ap_history": ap_history,
                      # The adaptive target is part of the configuration: a check
                      # that proved a different target correct proves nothing
                      # about this one.
                      "target": configuration.get("target")
                      or cfg.shortlist_rel_error_percent / 100.0,
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
                          "ensemble": (1 if workload == "single" else
                                       min(4, ensemble_size) if workload == "sequential" else ensemble_size),
                          "thread_workers": (min(cfg.workers, ensemble_size)
                                             if workload == "ensemble" else 1),
                          "points": cfg.points, "seed": cfg.seed,
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
                            # group's global ranges never move, so a recorded
                            # partition is replayed instead of recomputed.
                            parts = recorded_partition(self.ledger, group_id,
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

        A configuration is eligible only when every one of its completed accuracy
        tasks published a maximum absolute relative error at or below the
        screening threshold and reconstructed the holdouts itself. There is no
        fallback: a configuration that has not been measured, that failed the
        threshold, or whose apparent accuracy comes from native fallbacks rather
        than from the cache, is never propagated or benchmarked.

        Among the eligible configurations the cheapest measured one is preferred:
        native calls first, then the measured construction cost, then the error
        itself. At least one configuration per candidate, and one per
        interpolation axis for the interpolating candidate, is always kept so the
        selected study still covers the A/B/C/D variants and the pure
        altitude, time and 4-D axes.
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
            native_calls = 0
            construction_seconds = 0.0
            fallback = 0.0
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
                # Cost is measured, never assumed: the native calls this
                # configuration needed to answer the holdouts, its construction
                # time and the fraction of queries it did not reconstruct.
                native_calls = max(native_calls, screen.get("native_calls") or 0)
                construction_seconds = max(construction_seconds,
                                           screen.get("construction_seconds") or 0.0)
                fallback = max(fallback, screen.get("max_fallback_fraction") or 0.0)
            entry["worst_abs_rel_error_percent"] = worst
            entry["measured_budgets_met_percent"] = [
                budget for budget in REPORTED_BUDGETS_PERCENT
                if worst is not None and not problems and not incomplete and worst <= budget]
            entry["measured_budgets_scored_percent"] = list(REPORTED_BUDGETS_PERCENT)
            entry["measured_native_calls"] = native_calls
            entry["measured_construction_seconds"] = construction_seconds
            entry["max_fallback_fraction"] = fallback
            entry["measured_diagnostics"] = {
                "native_calls": native_calls,
                "construction_seconds": construction_seconds,
                "max_fallback_fraction": fallback,
                "note": ("screening cost diagnostics, not a performance result; the headline "
                         "performance of a selected configuration is measured by the exclusive "
                         "benchmark groups of this run")}
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
            elif fallback > cfg.max_screening_fallback_fraction:
                blocked.append({**entry, "reason":
                                f"{fallback:.6f} of the screened queries were answered by a native "
                                "fallback rather than reconstructed; the apparent accuracy is the "
                                "fallback's, not the candidate's, so it is never selected "
                                f"(limit {cfg.max_screening_fallback_fraction})"})
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
                                 "measured_native_calls": 0,
                                 "measured_construction_seconds": 0.0,
                                 "max_fallback_fraction": 0.0,
                                 "selection": "preprocessing-only baseline, not a screened "
                                              "caching candidate"})

        eligible = self.select_representative(eligible, cfg)
        policy = {
            "threshold_relative_error_percent": cfg.shortlist_rel_error_percent,
            "max_screening_fallback_fraction": cfg.max_screening_fallback_fraction,
            "reported_budgets_percent": list(REPORTED_BUDGETS_PERCENT),
            "adaptive_target_percent": cfg.adaptive_target_percent,
            "budget_rule": ("every configuration is screened once per grid setting and that one "
                            "scoring answers every budget from the measured maximum; the "
                            "adaptive candidate is additionally screened per target, because "
                            "there the target changes the algorithm rather than the score"),
            "refinement_gate_percent": cfg.advance_threshold_percent(),
            "refinement_rule": ("a family's ladder advances only while the rung in front of it "
                                "was measured and missed this budget; once a rung meets it the "
                                "coarser, cheaper rung is the one carried forward and no finer "
                                "setting is executed"),
            "adaptive_targets_percent": (list(cfg.targets)
                                         if cfg.adaptive_target_percent is None
                                         else [cfg.adaptive_target_percent]),
            "metric": "maximum of all four holdout maximum_absolute_relative values of the "
                      "requested rung, over every weather epoch, in percent",
            "schema": "result.resolution_ladder[requested_rung == true]"
                      ".{holdout_uniform-random,holdout_local-query-packet,"
                      "holdout_broad-query-packet,holdout_stratified-physical-boundaries}"
                      ".results.error_metrics.maximum_absolute_relative",
            "holdout_rule": ("all four packets are required, and the selection threshold is judged "
                             "on the worst of them: the stratified physical-boundary packet "
                             "carries the per-point errors and the native, guard, domain and "
                             "node counts that a random or local packet cannot show"),
            "holdout_packets": list(HOLDOUT_PACKETS),
            "eligible": eligible,
            "not_eligible": blocked,
            "selection_rule": ("a whole configuration (model, candidate, weather mode, cell_km, "
                               "cell_deg, cell_s, capacity, interpolation policy and adaptive "
                               "target) is selected only when all of its completed accuracy tasks "
                               "published a worst-case error within the threshold and "
                               "reconstructed the holdouts themselves; among those, the one with "
                               "the fewest measured native calls wins, then the smallest measured "
                               "construction cost, then the smallest error"),
            "coverage_rule": ("at least one configuration per candidate and one per interpolation "
                              "axis of the interpolating candidate are always selected, so the A, "
                              "B, C and D variants and the pure altitude, time and 4-D axes are "
                              "never pruned away; --max-shortlist only caps the additional "
                              "configurations"),
            "selection_source": "measured accuracy and cost of this run",
        }
        return eligible, policy

    def select_representative(self, eligible: list[dict], cfg: RunConfig) -> list[dict]:
        """Keep the cheapest viable configuration of every candidate and axis.

        The representative set is what makes the study a study of A, B, C and D
        and of the altitude-only, time-only and 4-D grids: it is chosen by
        measurement, but it is never allowed to disappear because a cheaper
        configuration happened to pass somewhere else. ``--max-shortlist`` caps
        only the extra configurations beyond it.
        """
        def cost(entry):
            return (entry.get("measured_native_calls") if entry.get("measured_native_calls")
                    is not None else 0,
                    entry.get("measured_construction_seconds") or 0.0,
                    entry.get("worst_abs_rel_error_percent") or 0.0,
                    configuration_suffix(entry))

        ordered = sorted(eligible, key=lambda entry: (str(entry["model"]),
                                                      str(entry["candidate"]),
                                                      str(entry.get("interpolation")),
                                                      cost(entry)))
        representatives: list[dict] = []
        extras: list[dict] = []
        seen_candidate: set[tuple] = set()
        seen_axis: set[tuple] = set()
        for entry in ordered:
            if entry["candidate"] == BENCHMARK_BASELINE:
                representatives.append(entry)
                continue
            candidate_key = (entry["model"], entry["candidate"])
            axis_key = (entry["model"], entry["candidate"], entry.get("interpolation"))
            if candidate_key not in seen_candidate:
                seen_candidate.add(candidate_key)
                representatives.append(entry)
                continue
            if entry.get("interpolation") and axis_key not in seen_axis:
                # The pure altitude, time, spatial and 4-D grids are distinct
                # candidates of the interpolating configuration; each keeps its
                # cheapest viable setting.
                seen_axis.add(axis_key)
                representatives.append(entry)
                continue
            extras.append(entry)

        extra_counts: dict[str, int] = {}
        selected = list(representatives)
        for entry in extras:
            model = entry["model"]
            if extra_counts.get(model, 0) >= cfg.max_shortlist:
                continue
            extra_counts[model] = extra_counts.get(model, 0) + 1
            selected.append(entry)

        for entry in selected:
            entry["selection"] = (
                "preprocessing-only baseline, not a screened caching candidate"
                if entry["candidate"] == BENCHMARK_BASELINE else
                ("representative of its candidate and interpolation axis"
                 if entry in representatives else
                 "additional viable configuration within --max-shortlist"))
        selected.sort(key=lambda entry: (str(entry["model"]), bool(entry.get("ap_history")),
                                        str(entry["candidate"]),
                                        str(entry.get("interpolation")),
                                        configuration_suffix(entry)))
        return selected

    def check_state(self, configuration: dict, ledger: "Ledger") -> tuple[str, str]:
        """Whether a configuration's correctness checks have cleared the coupled stages."""
        cfg = self.cfg
        if "check" not in cfg.kinds:
            return "not-requested", ("the check stage was not requested, so this configuration "
                                    "is coupled on its measured accuracy alone")
        task_id = check_task_id(cfg, configuration)
        status = ledger.status(task_id)
        if status == "completed":
            return "passed", f"correctness checks completed: {task_id}"
        if status == "failed":
            return "failed", f"correctness checks failed: {task_id}"
        return "pending", f"correctness checks have not finished: {task_id}"

    def plan(self, ledger: "Ledger", defer_missing: bool = True) -> tuple[list[Task], dict]:
        """Build the whole stage plan from the ledger's measured state.

        With ``defer_missing`` the coupled and performance stages are recorded as
        blocked placeholders instead of being planned; the caller re-plans them
        once the screening stage has produced its measurements and the selected
        configurations' correctness checks have cleared.
        """
        cfg = self.cfg
        self.ledger = ledger
        self.used_ids.clear()
        plan = {"stages": ["sensitivity+locality", "accuracy", "check", "coupled",
                           "benchmark", "aggregate"]}
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
        ready, gated = [], []
        for entry in shortlist:
            state, reason = self.check_state(entry, ledger)
            entry["check_state"] = state
            entry["check_reason"] = reason
            (ready if state in ("passed", "not-requested") else gated).append(entry)

        if not measured and defer_missing and "accuracy" in cfg.kinds:
            reason = ("no candidate configuration passed the measured accuracy screening; "
                      "the coupled and performance stages are blocked, not executed")
            plan["blocked_stages"] = {"coupled": reason, "benchmark": reason}
            if "propagation" in cfg.kinds:
                tasks.extend(self.deferred_tasks("coupled", "propagation", reason))
            if "benchmark" in cfg.kinds:
                tasks.extend(self.deferred_tasks("coupled", "benchmark", reason))
            return tasks, plan

        if "propagation" in cfg.kinds:
            tasks.extend(self.propagation_tasks(ready))
        if "benchmark" in cfg.kinds:
            tasks.extend(self.benchmark_tasks(ready))
        if gated and defer_missing:
            for entry in gated:
                reason = ("correctness checks have not cleared this configuration: "
                          f"{entry['check_reason']}")
                plan.setdefault("blocked_stages", {})
                plan["blocked_stages"].setdefault("coupled", reason)
                if "propagation" in cfg.kinds:
                    tasks.append(self.deferred_configuration_task(
                        "coupled", "propagation", entry, reason))
                if "benchmark" in cfg.kinds:
                    tasks.append(self.deferred_configuration_task(
                        "coupled", "benchmark", entry, reason))
        return tasks, plan

    def deferred_configuration_task(self, stage: str, kind: str, configuration: dict,
                                    reason: str) -> Task:
        """A per-configuration placeholder, so a blocked configuration is named."""
        suffix = configuration_suffix(configuration)
        return Task(task_id=f"{stage}-blocked-{kind}-{configuration['model']}-"
                            f"{configuration['candidate']}{suffix}",
                    stage=stage, kind=kind, serial=True, commands=[],
                    params={"kind": kind, "model": configuration["model"],
                            "candidate": configuration["candidate"], "blocked": True,
                            **{key: configuration.get(key)
                               for key in RESOLUTION_KEYS + POLICY_KEYS + ("target",)}},
                    out_dir="", model=configuration["model"],
                    candidate=configuration["candidate"], target="blocked",
                    label=reason, deferred=reason)


def check_task_id(cfg: RunConfig, configuration: dict) -> str:
    """The deterministic id of a configuration's correctness checks."""
    return (f"chk-{configuration['model']}-{configuration['candidate']}-"
            + configuration_suffix(configuration))


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
# Every packet is required: a screen that did not score one of them, including
# the stratified physical-boundary packet that covers the poles, the longitude
# seam, the dawn/dusk terminator and the native altitude branch points, cannot
# select a configuration.
HOLDOUT_PACKETS = ("holdout_uniform-random", "holdout_local-query-packet",
                   "holdout_broad-query-packet",
                   "holdout_stratified-physical-boundaries")
NONFINITE_KEY = "nonpositive_or_nonfinite_reconstructions"


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
    reported_set = (entry.get("settings") or {}).get("input_set") \
        if isinstance(entry.get("settings"), dict) else None
    if configuration.get("input_set") and reported_set is not None \
            and reported_set != configuration["input_set"]:
        problems.append(f"settings.input_set: screened {configuration['input_set']}, "
                        f"rung {reported_set}")
    nonfinite = count_nonfinite(entry)
    if nonfinite > 0:
        problems.append(f"{nonfinite} nonpositive or nonfinite reconstruction(s): the "
                        "candidate is rejected regardless of its error")
    worst = None
    native_calls = number_or_none(entry.get("construction_native_calls")) or 0.0
    construction_seconds = number_or_none(entry.get("construction_seconds")) or 0.0
    max_fallback = 0.0
    fallback_reported = False
    fallback_unknown: list[str] = []
    for packet in HOLDOUT_PACKETS:
        node = entry.get(packet)
        metrics = (node or {}).get("results", {}).get("error_metrics") if isinstance(node, dict) else None
        value = metrics.get("maximum_absolute_relative") if isinstance(metrics, dict) else None
        if not isinstance(value, (int, float)) or isinstance(value, bool) or value != value:
            problems.append(f"holdout {packet} published no maximum_absolute_relative")
            continue
        percent = float(value) * 100.0
        worst = percent if worst is None else max(worst, percent)
        # One scoring per configuration has to answer every budget. The runner
        # recomputes each verdict from the measured maximum rather than trusting
        # a self-reported one, so a budget can never be claimed from a screen
        # that never computed it.
        results_node = node.get("results") if isinstance(node, dict) else None
        published = results_node.get("threshold_pass_by_maximum") \
            if isinstance(results_node, dict) else None
        if not isinstance(published, dict):
            problems.append(f"holdout {packet} published no threshold_pass_by_maximum, so no "
                            "error budget was scored")
        else:
            for budget in REPORTED_BUDGETS_PERCENT:
                # The study keys its budget row by the fractional threshold, not
                # by the percentage the runner reports.
                verdict = published.get("%g" % (budget / 100.0))
                if verdict is None:
                    problems.append(f"holdout {packet} did not score the {budget} percent budget")
                elif bool(verdict) != (percent <= budget):
                    problems.append(f"holdout {packet} reported {verdict} for the {budget} "
                                    f"percent budget, but its measured maximum is {percent}")
        # Measured cost of this holdout: the native calls it actually needed and
        # the share of its queries that fell back to the model instead of being
        # reconstructed. Both are read from the result, never assumed.
        native_calls += number_or_none(node.get("native_calls_on_this_holdout")) or 0.0
        fallback = number_or_none(node.get("fallback_fraction"))
        if fallback is None:
            # A packet that does not publish a fallback share cannot claim one
            # either way; it is named, and the verdict rests on the packets that
            # do publish it.
            fallback_unknown.append(packet)
        else:
            max_fallback = max(max_fallback, float(fallback))
            fallback_reported = True
    if not fallback_reported:
        problems.append("no holdout published a fallback share, so reconstructed values cannot "
                        "be told apart from native fallbacks")
    return {"worst_relative_percent": worst, "problems": problems,
            "fallback_fraction_unknown_packets": fallback_unknown,
            "nonfinite_reconstructions": nonfinite,
            "native_calls": native_calls,
            "construction_seconds": construction_seconds,
            "max_fallback_fraction": max_fallback,
            "budgets_scored_percent": list(REPORTED_BUDGETS_PERCENT),
            "budgets_met_percent": [
                budget for budget in REPORTED_BUDGETS_PERCENT
                if worst is not None and not problems and worst <= budget]}


def number_or_none(value):
    """A finite number, or None when the result published nothing usable."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    value = float(value)
    return None if value != value or value in (float("inf"), float("-inf")) else value


def plan_has_outstanding(planner: "Planner", ledger: "Ledger",
                         attempted: set) -> bool:
    """Whether re-planning would actually add executable work.

    Progressive screening and the correctness-check gate both grow the plan as
    measurements arrive. This asks the next plan what it would execute that is
    neither finished nor already attempted in this run, so a settled run stops
    re-planning instead of spinning through its remaining budget.
    """
    planner.used_ids.clear()
    upcoming, _ = planner.plan(ledger, defer_missing=True)
    for task in upcoming:
        if task.deferred or not task.commands:
            continue
        if task.task_id in attempted:
            continue
        if ledger.status(task.task_id) != "completed":
            return True
    return False


def plan_summary(tasks: list[Task], plan: dict) -> dict:
    """The task grouping of a plan, counted by kind, stage and exclusivity."""
    by_kind: dict[str, int] = {}
    by_stage: dict[str, int] = {}
    exclusive = 0
    children = 0
    for task in tasks:
        by_kind[task.kind] = by_kind.get(task.kind, 0) + 1
        by_stage[task.stage] = by_stage.get(task.stage, 0) + 1
        exclusive += 1 if task.serial else 0
        children += len(task.commands)
    return {"tasks": len(tasks), "children": children, "exclusive_groups": exclusive,
            "by_kind": dict(sorted(by_kind.items())), "by_stage": dict(sorted(by_stage.items())),
            "deferred": sum(1 for task in tasks if task.deferred),
            "blocked_stages": sorted((plan or {}).get("blocked_stages", {}))}


def print_plan_summary(tasks: list[Task], plan: dict) -> None:
    """Print the grouping of the plan, so the task contract is visible up front."""
    summary = plan_summary(tasks, plan)
    print(f"[run.py] plan: {summary['tasks']} task(s), {summary['children']} child process(es), "
          f"{summary['exclusive_groups']} exclusive group(s), "
          f"{summary['deferred']} deferred", flush=True)
    print(f"[run.py] plan by kind: {summary['by_kind']}", flush=True)
    if summary["blocked_stages"]:
        print(f"[run.py] gated stages: {summary['blocked_stages']}", flush=True)


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

    def record_result(self, task_id: str, result_path: Path, digest: str, index: int = 0) -> None:
        """One child's result, indexed, published only after it parsed and verified."""
        entry = self.data["tasks"][task_id]
        results = entry.setdefault("results", [])
        results[:] = [item for item in results if item.get("index") != index]
        results.append({"index": index, "path": str(result_path), "sha256": digest})
        results.sort(key=lambda item: item["index"])
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

    def restore_results(self) -> int:
        """Load every completed task's published result back into memory.

        Planning reads the measurements a previous run already published: the
        screening frontier, the selection and the correctness-check gate are all
        decided from real results, not from the mere presence of a task id. A
        resume that executed nothing still sees the full evidence of the run it
        continues.
        """
        restored = 0
        for task_id, entry in self.data.get("tasks", {}).items():
            if entry.get("status") != "completed" or task_id in self._results:
                continue
            for item in entry.get("results") or []:
                payload = self.load_result(item.get("path"))
                if payload is not None:
                    self._results[task_id] = payload
                    restored += 1
                    break
        return restored

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
    def publish(self, task: Task, index: int = 0) -> bool:
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
        self.ledger.record_result(task.task_id, destination, digest, index)
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
            # The log is published by rename when the child is reaped, so a
            # half-written file never appears under the name the ledger records.
            staging_path = log_path.with_name(log_path.name + f".partial-{os.getpid()}")
            handle = open(staging_path, "wb")
            started = utc_now()
            groups.setdefault(task.task_id, {"task": task, "started": time.monotonic(),
                                             "children": 0, "failures": 0})
            try:
                process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT,
                                           cwd=str(ROOT))
            except OSError as error:
                handle.write((str(error) + "\n").encode())
                handle.close()
                os.replace(staging_path, log_path)
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
                                              "log": log_path, "staging": staging_path,
                                              "started": time.monotonic()}
            groups.setdefault(task.task_id, {"task": task, "started": time.monotonic(),
                                             "children": 0, "failures": 0})
            print(f"[run.py] start {task.task_id} child {index} "
                  f"({'exclusive group' if task.serial else 'concurrent'}) pid={process.pid}",
                  flush=True)

        def publish_log(entry):
            """Move a finished child's log from its staging name to its final name."""
            staging = entry.get("staging")
            if staging is None or not Path(staging).exists():
                return
            os.replace(staging, entry["log"])

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
                publish_log(entry)

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
                publish_log(entry)
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
                if not self.publish(task, index=entry["index"]):
                    groups[task.task_id]["failures"] += 1

            for task in list(groups):
                state = groups[task]
                task_state = state["task"]
                if state["children"] >= len(task_state.commands) and not any(
                        key[0] == task for key in running):
                    finish_group(task_state)
                    finished[task] = state

            # An exclusive group owns the machine: no scientific child starts
            # while one is alive, and no scientific child of an already-running
            # group may be launched behind it. Otherwise a timing measurement
            # would share the host with the sweep it is meant to be compared
            # against, which is the contamination the exclusivity exists to stop.
            exclusive_active = any(state["task"].serial for state in groups.values())

            # Scientific work may share the machine up to the worker count.
            while (queue and not exclusive_active and len(running) < cfg.workers
                   and not self.stop["requested"]):
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


def published_result_problems(cfg: RunConfig, ledger: Ledger, tasks: list[Task]) -> list[str]:
    """Every way a published result contradicts what the ledger claims.

    Aggregation is strict about this: a completed task whose result is missing,
    altered or incomplete is not quietly summarised as a completed measurement.
    The problems are recorded and the run is reported incomplete rather than
    hidden behind a status line.
    """
    problems: list[str] = []
    for task in tasks:
        entry = ledger.data["tasks"].get(task.task_id) or {}
        status = entry.get("status")
        results = entry.get("results") or []
        expected = len((entry.get("identity") or {}).get("commands") or [])
        if status == "completed":
            if expected and {item.get("index") for item in results} != set(range(expected)):
                problems.append(f"{task.task_id}: completed with {len(results)} of {expected} "
                                "published child result(s)")
            for item in results:
                path = Path(item.get("path", ""))
                if not path.is_file():
                    problems.append(f"{task.task_id}: published result is missing: {path}")
                elif sha256_file(path) != item.get("sha256"):
                    problems.append(f"{task.task_id}: published result no longer matches its "
                                    f"recorded SHA-256: {path}")
            for item in entry.get("artifacts", []):
                path = Path(item.get("path", ""))
                if not path.is_file():
                    problems.append(f"{task.task_id}: artifact is missing: {path}")
                elif sha256_file(path) != item.get("sha256"):
                    problems.append(f"{task.task_id}: artifact no longer matches its recorded "
                                    f"SHA-256: {path}")
        elif results:
            problems.append(f"{task.task_id}: status {status!r} still carries published results")
    return problems


def aggregate(cfg: RunConfig, ledger: Ledger, tasks: list[Task], plan: dict,
              execution: dict) -> dict:
    """Mechanical aggregation by kind. No scientific conclusion is computed here."""
    counts = {"total": len(tasks)}
    for status in ("completed", "failed", "pending", "running", "blocked"):
        counts[status] = sum(1 for task in tasks if ledger.status(task.task_id) == status)
    integrity = published_result_problems(cfg, ledger, tasks)
    complete = (counts["completed"] == counts["total"] and counts["total"] > 0
                and not integrity
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
                expected = len((record.get("identity") or {}).get("commands") or [])
                entry["children_published"] = len(published)
                if expected and len(published) != expected:
                    kind_complete = False
                    entry["integrity"] = (f"only {len(published)} of {expected} child results "
                                          "were published")
                if record.get("reused_from"):
                    entry["reused_from"] = record["reused_from"]
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
            "evidence_class": evidence_class(cfg),
            "input_set": {"id": cfg.input_set["id"],
                          "manifest_sha256": cfg.input_set.get("manifest_sha256")},
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
    if integrity:
        limitations.append(f"{len(integrity)} published result or artifact discrepancy(s) were "
                           "found while aggregating; this run is reported incomplete and the "
                           "affected tasks are listed in integrity_problems.")
    if cfg.input_set.get("explicitly_selected"):
        reference = cfg.input_set.get("historical_reference") or {}
        limitations.append(
            f"This run measured the explicitly pinned input set "
            f"'{cfg.input_set['id']}' (manifest SHA-256 "
            f"{cfg.input_set.get('manifest_sha256')}), not the historical direct-v2 baseline. "
            f"Any claim it makes is scoped to those pinned bytes"
            + (f"; the recorded reference notes: {reference.get('claim_limit')}"
               if reference.get("claim_limit") else ""))
    reuse = read_json(cfg.out / "reuse-index.json") or {}
    if reuse.get("imported"):
        limitations.append(
            f"{len(reuse['imported'])} result(s) were imported from validated existing evidence "
            "and are published in results/ with their producer hashes; an imported result never "
            "completes a planned task and is never a substitute for provider, JB2008, coupled or "
            "performance evidence.")
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
        "evidence_class": evidence_class(cfg),
        "input_set": dict(cfg.input_set),
        "counts": counts,
        "integrity_problems": integrity,
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
        "evidence_class": evidence_class(cfg),
        "input_set": dict(cfg.input_set),
        "input_verification": read_json(cfg.out / "input-verification.json"),
        "reused_evidence": read_json(cfg.out / "reuse-index.json"),
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
                        "by_status": ledger.counts(),
                        "grouping": plan_summary(tasks, plan)},
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
# Validated import of existing evidence
# ---------------------------------------------------------------------------


def native_source_hashes(cfg: RunConfig) -> dict:
    """Hash the authoritative native sources this build is made of.

    NRLMSISE-00 and JB2008 are Fortran and C sources outside this repository.
    A measurement made from different bytes of them is a different
    measurement, so they are hashed by name and reported even when they are
    absent, and an absent source is never silently assumed identical.
    """
    roots = [Path(root) for root in (cfg.native_source_dir, cfg.deps_dir) if root]
    described = {}
    for name in NATIVE_SOURCE_NAMES:
        found = next((root / name for root in roots if (root / name).is_file()), None)
        described[name] = ({"path": str(found.resolve()), "sha256": sha256_file(found),
                            "bytes": found.stat().st_size}
                           if found else {"path": None, "sha256": None, "present": False})
    return described


def reuse_identity(cfg: RunConfig) -> dict:
    """Everything a reused measurement has to agree with before it is imported.

    The identity is the pinned input bytes (including the leapsecond kernel and
    the CSSI weather archive), the authoritative native sources, the
    preprocessing switches and frame, and the coordinate grids and seed that
    produced the samples. Two runs that agree on all of it measured the same
    thing; anything else is a refusal, not an approximation.
    """
    inputs = hash_inputs(cfg)
    observed = {}
    for name, entry in inputs.items():
        if name == "cases":
            for case, case_entry in (entry or {}).items():
                observed[f"case:{case}"] = case_entry.get("sha256")
        else:
            observed[name] = entry.get("sha256")
    return {
        "input_set": cfg.input_set["id"],
        "input_set_manifest_sha256": cfg.input_set.get("manifest_sha256"),
        "inputs": dict(sorted(observed.items())),
        "native_sources": {name: entry.get("sha256")
                           for name, entry in sorted(native_source_hashes(cfg).items())},
        "switches": {"utc_reuse": True, "orientation_reuse": True, "sun_sharing": True,
                     "frame": cfg.frame, "ap_history_modes": ["daily", "history"]},
        "coordinate_grids": {"fixture_epochs": [list(item) for item in FIXTURE_EPOCHS],
                             "altitude_domain_km": [200, 1000],
                             "longitude_wrap": True, "polar_fallback_deg": 89.999},
        "seed": cfg.seed,
    }


def observed_input_hashes(cfg: RunConfig) -> dict:
    """Name to pinned hash for every input file this run supplies."""
    observed = {}
    for name, entry in hash_inputs(cfg).items():
        if name == "cases":
            for case, case_entry in (entry or {}).items():
                observed[f"case:{case}"] = case_entry.get("sha256")
        else:
            observed[name] = entry.get("sha256")
    return observed


def donor_results(directory: Path) -> list[Path]:
    """Every published study result under an evidence directory."""
    directory = Path(directory)
    found = []
    if (directory / "study-result.json").is_file():
        found.append(directory / "study-result.json")
    if directory.is_dir():
        found.extend(sorted(path for path in directory.rglob("study-result.json")
                            if path.is_file() and path != directory / "study-result.json"))
    return found


def donor_producer_sources(directory: Path, provenance=()) -> dict:
    """The authoritative native sources a donor recorded when it produced results.

    A manifest's source map and a reuse-provenance record are both accepted; they
    are merged and must agree, because a donor that names two different sets of
    native sources has not established what it measured.
    """
    directory = Path(directory)
    sources: dict[str, str] = {}
    conflicting: set[str] = set()
    for candidate in (directory / "manifest.json", directory / "evidence" / "manifest.json",
                      directory.parent / "manifest.json"):
        document = read_json(candidate)
        if isinstance(document, dict) and isinstance(document.get("sources"), dict):
            for name, digest in document["sources"].items():
                base = Path(name).name
                if base in NATIVE_SOURCE_NAMES:
                    if sources.get(base) not in (None, digest):
                        conflicting.add(base)
                    sources[base] = digest
    for path in provenance or ():
        document = read_json(path)
        for entry in ((document or {}).get("entries") or []):
            declared = ((entry.get("source_dependencies") or {}).get("native_C_and_data") or {})
            for name, digest in declared.items():
                base = Path(name).name
                if base in NATIVE_SOURCE_NAMES:
                    if sources.get(base) not in (None, digest):
                        conflicting.add(base)
                    sources[base] = digest
    for name in conflicting:
        sources.pop(name, None)
    return sources


def study_value(value):
    """A study-published scalar or array, however the executable serialised it.

    The metadata writer emits raw JSON arrays through a string field, so an
    altitude domain arrives as the text "[200,1000]". Parsing it here keeps the
    comparison on values instead of on formatting.
    """
    if isinstance(value, str):
        try:
            return json.loads(value)
        except ValueError:
            return value
    return value


def donor_read_inputs(metadata: dict) -> list[tuple[str, str | None]]:
    """The input files a donor result actually read, with their hashes."""
    files: list[tuple[str, str | None]] = []
    for kernel in metadata.get("kernels") or []:
        if isinstance(kernel, dict):
            files.append((Path(str(kernel.get("path") or "?")).name, kernel.get("sha256")))
    for key, label in (("cssi_weather_archive", "CSSI weather archive"),
                       ("solfsmy", "solfsmy"), ("dtcfile", "dtcfile")):
        node = metadata.get(key)
        if isinstance(node, dict):
            files.append((node.get("path") or label, node.get("sha256")))
    return files


def check_reusable(metadata: dict, producer_sources: dict, cfg: RunConfig) -> list[dict]:
    """Compare one donor result against this run, field by field.

    Compatibility is decided on the bytes the donor actually read. A provider
    measurement reads every pinned input, so it must also carry this run's
    input-set id. A native-input slice reads only the leapsecond kernel and the
    CSSI weather archive, so it is compatible exactly when those bytes match,
    and it stays labelled as a native slice that covers no provider, JB2008,
    coupled or performance evidence.
    """
    observed = observed_input_hashes(cfg)
    hashes = {digest for digest in observed.values() if digest}
    sources = {entry.get("sha256") for entry in native_source_hashes(cfg).values()}
    identity = cfg.input_set["id"]
    mode = metadata.get("measurement_mode") or "provider"
    checks = []

    def record(name, expected, found, ok):
        checks.append({"check": name, "expected": expected, "found": found, "passed": bool(ok)})

    verification = metadata.get("verification_only")
    record("not a verification-only result", False, verification, verification is False)
    reported_set = metadata.get("input_set") or DEFAULT_INPUT_SET
    if mode == "native-geodetic-partial":
        record("input set (native slice reads only the leapsecond kernel and the CSSI archive)",
               identity, reported_set, True)
    else:
        record("input set", identity, reported_set, reported_set == identity)
    record("seed", cfg.seed, metadata.get("master_seed"), metadata.get("master_seed") == cfg.seed)
    record("frame", cfg.frame, metadata.get("frame"), metadata.get("frame") == cfg.frame)
    domain = study_value(metadata.get("altitude_domain_km"))
    record("coordinate grid: altitude domain", [200, 1000], domain, domain == [200, 1000])
    polar = study_value(metadata.get("polar_fallback_deg"))
    record("coordinate grid: polar fallback", 89.999, polar, polar == 89.999)
    for name in ("utc_reuse", "orientation_reuse", "sun_sharing"):
        record(f"preprocessing switch {name}", True, metadata.get(name), metadata.get(name) is True)
    read_files = donor_read_inputs(metadata)
    record("the result recorded which inputs it read", True,
           [name for name, _ in read_files], bool(read_files))
    for name, digest in read_files:
        record(f"input bytes this run also has: {name}", "pinned by this run",
               digest, bool(digest) and digest in hashes)
    if producer_sources:
        for name, digest in sorted(producer_sources.items()):
            record(f"native source {name}", "matches this build",
                   digest, bool(sources) and digest in sources)
    else:
        record("native sources", "recorded by the producer", None, False)
    return checks


def import_reusable_evidence(cfg: RunConfig) -> dict:
    """Import results from validated existing evidence directories.

    Every candidate is checked against this run's pinned bytes, native sources,
    preprocessing switches, coordinate grids and seed before a single byte is
    published. An imported result is evidence with its producer's hashes
    attached; it never completes a planned task, and a native-input slice can
    never stand in for provider, JB2008, coupled or performance evidence.
    """
    index = {"kind": "atm-cache-study-reuse-index", "generated_utc": utc_now(),
             "compatibility": reuse_identity(cfg),
             "requested_directories": [str(directory) for directory in cfg.reuse_dirs],
             "imported": [], "refused": [],
             "rule": ("an imported result is published verbatim with its producer hashes and is "
                      "never a substitute for a planned task, for provider or JB2008 "
                      "performance, or for coupled evidence")}
    if not cfg.reuse_dirs:
        return index
    results_dir = cfg.out / "results"
    results_dir.mkdir(parents=True, exist_ok=True)
    index["declared_provenance"] = [str(path) for path in cfg.reuse_provenance]
    for directory in cfg.reuse_dirs:
        sources = donor_producer_sources(Path(directory), cfg.reuse_provenance)
        candidates = donor_results(Path(directory))
        if not candidates:
            index["refused"].append({"donor": str(directory),
                                     "reason": "no study-result.json was found under it"})
            continue
        for path in candidates:
            document = read_json(path)
            if not isinstance(document, dict):
                index["refused"].append({"donor": str(path),
                                         "reason": "the published result is not valid JSON"})
                continue
            metadata = document.get("metadata") or {}
            checks = check_reusable(metadata, sources, cfg)
            failed = [check["check"] for check in checks if not check["passed"]]
            mode = metadata.get("measurement_mode") or "provider"
            allowed = ("native-input sensitivity only: never a substitute for provider, "
                       "JB2008, coupled or performance evidence"
                       if mode == "native-geodetic-partial"
                       else "provider measurement on the same pinned bytes")
            if failed:
                index["refused"].append({"donor": str(path), "reason": "incompatible evidence",
                                         "failed_checks": failed, "checks": checks})
                continue
            name = f"reused-{slug(path.parent.name)}-{slug(metadata.get('study_kind') or 'study')}"
            destination = results_dir / f"{name}.json"
            digest = sha256_file(path)
            staging = destination.with_name(f"{destination.name}.tmp-{os.getpid()}")
            staging.write_bytes(Path(path).read_bytes())
            os.replace(staging, destination)
            if sha256_file(destination) != digest:
                raise RunnerError(f"imported result hash mismatch: {path}")
            index["imported"].append({
                "name": name, "donor": str(path.resolve()), "donor_sha256": digest,
                "published": str(destination), "published_sha256": digest,
                "study_kind": metadata.get("study_kind"), "model": metadata.get("model"),
                "measurement_mode": mode, "input_set": metadata.get("input_set"),
                "ap_history_mode": metadata.get("ap_history_mode"),
                "master_seed": metadata.get("master_seed"),
                "producer_sources": sources, "checks": checks,
                "allowed_use": allowed,
                "completes_a_planned_task": False})
    write_json_atomic(cfg.out / "reuse-index.json", index)
    return index


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
    parser.add_argument("--input-set", type=Path, default=None, metavar="MANIFEST",
                        help="explicitly select a pinned input set by its JSON manifest; the id, "
                             "manifest hash and classification are recorded in the plan, the "
                             "identity, the manifest and every child command line, and the "
                             "evidence is labelled scientific-experimental-pinned. The default "
                             f"is the immutable {DEFAULT_INPUT_SET} set")
    parser.add_argument("--reuse-native", action="append", default=[], type=Path, metavar="DIR",
                        help="import results from an existing evidence directory after checking "
                             "its pinned input bytes, native sources, preprocessing switches, "
                             "coordinate grids and seed against this run; repeatable")
    parser.add_argument("--reuse-provenance", action="append", default=[], type=Path,
                        metavar="FILE",
                        help="a reuse-provenance record naming the authoritative native "
                             "sources its donor was built from; used by --reuse-native when the "
                             "donor directory has no manifest of its own")
    parser.add_argument("--native-source-dir", type=Path, default=None,
                        help="directory holding nrlmsise-00.c/.h/_data.c and JB2008.f, hashed "
                             "for --reuse-native compatibility")
    parser.add_argument("--adaptive-target", type=float, default=None, metavar="PERCENT",
                        help="screen the adaptive candidate at this reconstruction target only, "
                             "instead of at every requested budget. The default is no narrowing: "
                             "the adaptive family is screened at each requested budget, because "
                             "its target changes the algorithm, while every other candidate is "
                             "screened once and reports all budgets from that one scoring")
    parser.add_argument("--refine-to-budget", type=float, default=None, metavar="PERCENT",
                        help="keep a family's resolution ladder advancing until this error budget "
                             "is scored, instead of stopping as soon as it meets the screening "
                             "threshold. Use it to answer the tight budgets by measurement; the "
                             "default never refines past a rung that already passes")
    parser.add_argument("--max-screening-fallback-fraction", type=float, default=0.5,
                        help="a screened configuration whose holdouts were mostly answered by a "
                             "native fallback is never selected, however accurate it looks")
    parser.add_argument("--max-replans", type=int, default=12,
                        help="planning passes allowed while progressive screening and the "
                             "correctness-check gate advance the coupled stages")
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


def resolve_inputs(args, out: Path, input_set=None) -> None:
    """Find the kernels and space-weather files without substituting anything.

    A selected input set names the exact file each input must be, so that name
    is tried first and the hash gate decides whether the located file is the
    pinned one. A dataset that cannot be located is left unset rather than
    replaced by a similar file: the primary-run gate then records the study as
    blocked and pending, which is the honest state, instead of running on an
    unknown input.
    """
    candidates = [out, Path("/tmp/leo-atmosphere-deps"), args.deps_dir or Path("."),
                  ROOT / "docs/atmosphere/evidence"]
    missing = []
    pinned = (input_set or {}).get("inputs") or {}

    def locate(name, patterns, explicit):
        if explicit:
            path = Path(explicit)
            if not path.is_file():
                missing.append({"input": name, "path": str(path), "reason": "not a file"})
                return None
            return path
        filename = (pinned.get(name) or {}).get("filename")
        if filename:
            for root in candidates:
                candidate = root / filename
                if candidate.is_file():
                    return candidate
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


def select_input_set(args) -> dict:
    """Resolve the pinned input set this run measures.

    direct-v2 stays the immutable default and is never relabelled. Selecting a
    set is an explicit, named act: the manifest is the pin, the id travels into
    every child command line, and a scientific experiment on separately pinned
    bytes can never be labelled as the historical primary.
    """
    cached = getattr(args, "_resolved_input_set", None)
    if cached is not None:
        return cached
    if args.input_set is None:
        args._resolved_input_set = default_input_set()
        return args._resolved_input_set
    manifest = load_input_set(args.input_set)
    if args.verification_only:
        raise SystemExit(
            f"--input-set {manifest['id']} is a scientifically pinned experiment; "
            "--verification-only would label the same measurement as a pipeline check, which "
            "is exactly the mislabelling the pinned set exists to prevent")
    known = {path.stem: path for path in sorted(INPUT_SETS_DIR.glob("*.json"))} \
        if INPUT_SETS_DIR.is_dir() else {}
    print(f"[run.py] input set {manifest['id']} selected from {manifest['manifest']} "
          f"(sha256 {manifest['manifest_sha256']}); evidence class "
          f"scientific-experimental-pinned, never direct-v2", flush=True)
    if known and manifest["id"] not in known:
        print(f"[run.py]   note: {manifest['id']} is not one of the manifests in "
              f"{INPUT_SETS_DIR} ({', '.join(sorted(known))}); the study executable still "
              "refuses any id it does not recognise", flush=True)
    args._resolved_input_set = manifest
    return manifest


def config_from_args(args) -> RunConfig:
    if args.mode is None:
        raise SystemExit("--mode is required unless --resume names a run directory")
    input_set = select_input_set(args)
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
        expected_hashes=parse_expectations(args.expect_sha256, args.pinned_input_hashes,
                                           input_set),
        input_set=input_set,
        reuse_dirs=tuple(Path(item).resolve() for item in (args.reuse_native or [])),
        reuse_provenance=tuple(Path(item).resolve()
                               for item in (args.reuse_provenance or [])),
        native_source_dir=(Path(args.native_source_dir).resolve()
                           if args.native_source_dir else
                           (Path(args.deps_dir).resolve() if args.deps_dir else None)),
        max_screening_fallback_fraction=float(args.max_screening_fallback_fraction),
        max_replans=max(1, int(args.max_replans)),
        adaptive_target_percent=(float(args.adaptive_target)
                                 if args.adaptive_target is not None else None),
        refine_to_budget_percent=(float(args.refine_to_budget)
                                  if args.refine_to_budget is not None else None),
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


def resume_input_set_manifest(out: Path, recorded: dict, recorded_set: dict) -> Path | None:
    """The manifest of the input set a previous run of this directory measured.

    The historical primary needs no manifest: the default is direct-v2 and the
    recorded identity is compared against it exactly as before. A separately
    pinned set is restored from the manifest the run recorded, so the resume
    verifies the same bytes rather than a look-alike.
    """
    identifier = (recorded_set or {}).get("id") or DEFAULT_INPUT_SET
    if identifier == DEFAULT_INPUT_SET:
        return None
    recorded_manifest = ((recorded.get("identity") or {}).get("input_set") or {}).get("manifest")
    candidates = [Path(recorded_manifest)] if recorded_manifest else []
    candidates.append(INPUT_SETS_DIR / f"{identifier}.json")
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    raise RunnerError(
        f"this run directory measured the pinned input set '{identifier}', but its manifest is "
        f"not available ({[str(c) for c in candidates]}); a resume may not re-pin it by name")


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
    # A resumed run measures the same pinned bytes it started on. The selected
    # input set is restored from the plan, never re-chosen, so a resume can
    # never quietly slide onto a different set or back to direct-v2.
    recorded_set = parameters.get("input_set") or {}
    args.input_set = resume_input_set_manifest(out, recorded, recorded_set)
    # A selected input set is the pin, so a resume must not re-supply the
    # recorded hashes as command-line overrides on top of it: that would be the
    # runner quietly re-pinning a manifest it just verified.
    args.expect_sha256 = ([] if args.input_set is not None else
                          [f"{name}={digest}" for name, digest
                           in sorted(parameters.get("expected_hashes", {}).items())])
    args.reuse_native = [str(path) for path in (recorded.get("plan") or {}).get(
        "reuse_directories", [])]
    args.reuse_provenance = [str(path) for path in (recorded.get("plan") or {}).get(
        "reuse_provenance", [])]
    args.max_screening_fallback_fraction = parameters.get(
        "max_screening_fallback_fraction", args.max_screening_fallback_fraction)
    args.max_replans = parameters.get("max_replans", args.max_replans)
    args.adaptive_target = parameters.get("adaptive_target_percent", args.adaptive_target)
    args.refine_to_budget = parameters.get("refine_to_budget_percent", args.refine_to_budget)
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
    resolve_inputs(args, out, select_input_set(args))
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
        resolve_inputs(args, out, select_input_set(args))
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
                                   "stages": ["sensitivity+locality", "accuracy", "check",
                                              "coupled", "benchmark", "aggregate"],
                                   "reuse_directories": [str(path) for path in cfg.reuse_dirs],
                                   "reuse_provenance": [str(path) for path in cfg.reuse_provenance],
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
            "input_set": dict(cfg.input_set),
            "evidence_class": evidence_class(cfg),
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
            "evidence_class": evidence_class(cfg),
            "input_set": dict(cfg.input_set),
            "counts": {"total": len(tasks),
                       "completed": sum(entry.get("status") == "completed"
                                       for entry in ledger.data["tasks"].values()),
                       "failed": 0,
                       "pending": 0, "running": 0,
                       "blocked": sum(entry.get("status") != "completed"
                                     for entry in ledger.data["tasks"].values())},
            "pending": [task.task_id for task in tasks
                        if ledger.data["tasks"][task.task_id].get("status") != "completed"],
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
    restored = ledger.restore_results()
    if restored:
        print(f"[run.py] {restored} completed result(s) restored from this run directory",
              flush=True)
    plan_record = {"parameters": cfg.plan_parameters(),
                   "stages": ["sensitivity+locality", "accuracy", "check", "coupled",
                              "benchmark", "aggregate"],
                   "reuse_directories": [str(path) for path in cfg.reuse_dirs],
                   "reuse_provenance": [str(path) for path in cfg.reuse_provenance]}
    ledger.bind(cfg.identity(), cfg.mode, plan_record)
    # Validated import happens before planning: a reuse that cannot be verified
    # is recorded as a refusal and never reaches results/.
    reuse = import_reusable_evidence(cfg)
    if reuse["imported"] or reuse["refused"]:
        print(f"[run.py] reused evidence: {len(reuse['imported'])} imported, "
              f"{len(reuse['refused'])} refused", flush=True)
        for entry in reuse["refused"]:
            print(f"[run.py]   refused {entry.get('donor')}: "
                  f"{entry.get('reason', '')} "
                  f"{', '.join(entry.get('failed_checks', []))}", flush=True)
    planner = Planner(cfg, ledger)
    deadline = time.monotonic() + cfg.max_hours * 3600.0
    scheduler = Scheduler(cfg, ledger, [])
    scheduler.install_signal_handlers()
    tasks: list[Task] = []
    plan: dict = {}
    execution = {"scheduled_any": False, "budget_reached": False, "interrupted": False,
                 "signal": None}

    # Stages are planned and scheduled in order. The plan is re-derived after
    # every pass that executed something: progressive screening adds the next
    # rung only where the cheaper one missed, and the coupled stages open only
    # for configurations whose own correctness checks have cleared. A candidate
    # is therefore never propagated or benchmarked before the measurements that
    # justify it exist.
    replan = True
    replans = 0
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
            print_plan_summary(tasks, plan)
            scheduler.tasks = queue
            attempted_this_run.update(task.task_id for task in queue)
            execution = scheduler.run(deadline)
            execution["signal"] = scheduler.stop["signal"]
            execution["scheduled_any"] = execution["scheduled_any"] or True

        replan = (replans < cfg.max_replans and execution["scheduled_any"]
                  and not execution["interrupted"] and not execution["budget_reached"]
                  and time.monotonic() < deadline
                  and plan_has_outstanding(planner, ledger, attempted_this_run))
        if replan:
            replans += 1
            reasons = []
            if plan.get("blocked_stages"):
                reasons.append("a gated stage is waiting on its measurements")
            reasons.append("the next plan has executable work the ledger has not settled")
            print(f"[run.py] re-planning after this pass ({replans}/{cfg.max_replans}): "
                  f"{'; '.join(reasons)}", flush=True)

    # The recorded plan is re-derived from the final ledger state, so the plan,
    # the shortlist it carries and the summaries describe the run as it actually
    # ended rather than the plan as it stood before the last execution.
    tasks, plan = planner.plan(ledger, defer_missing=True)
    for task in tasks:
        ledger.record_task(task)
    ledger.data["plan"]["shortlist"] = plan.get("shortlist")
    ledger.save()
    write_json_atomic(out / "plan.json",
                      {**plan, "mode": cfg.mode, "parameters": cfg.plan_parameters(),
                       "grouping": plan_summary(tasks, plan),
                       "tasks": [task.identity() for task in tasks]})

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