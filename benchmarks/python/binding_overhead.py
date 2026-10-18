#!/usr/bin/env python3
"""Measure the Python binding overhead of the v0.1 interface.

The scientific performance baseline is the already qualified C++ campaign; this
runner measures only what the Python layer adds:

* direct C++ propagation time (the ``leo_reference_dump`` harness, same engine);
* the full ``leo.propagate`` / ``leo.propagate_batch`` call, which includes
  argument validation, native input construction, the propagated trajectory and
  NumPy result materialisation;
* configuration-construction cost alone and an equivalent-volume NumPy copy
  (a materialisation proxy, not a measurement of native packing);
* homogeneous and heterogeneous batch overhead, worker scaling observed through
  Python, the GIL behaviour and the memory a large batch output needs.

The native propagation-only time *inside* one binding call is not separately
observable: the extension exposes no timing hook and packing happens in the same
call. The reported difference is an estimate, not an upper bound: separate runs
include timing noise and the qualification harness counts RHS calls. Those limits
are recorded rather than treating a negative difference as negative conversion cost.

Examples
--------
    cmake -S tests/reference -B build-reference -DCMAKE_BUILD_TYPE=Release
    cmake --build build-reference -j
    LEO_PYTHON_PATH=$PWD/build-python/python \\
        .venv-python/bin/python benchmarks/python/binding_overhead.py \\
        --harness build-reference/leo_reference_dump
"""

from __future__ import annotations

import argparse
import json
import platform
import statistics
import sys
import threading
import copy
import tempfile
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "tests" / "python"))

import scenario_fixtures as fixtures  # noqa: E402  (path guard lives there)

leo = fixtures.leo


# --------------------------------------------------------------------------
# Measurement helpers
# --------------------------------------------------------------------------

MATCHED_ROUTE = "matched-native-source"
NATIVE_SOURCE_FLAG = ["--native-source"]


def run_matched(binary: Path, document: dict, extra: list[str]) -> dict:
    """Direct C++ baseline through the production EarthEnvironment acquisition.

    The harness must acknowledge the flag; an older binary that silently
    ignored it would benchmark the legacy snapshot route instead.
    """
    result = fixtures.run_harness(binary, document,
                                  extra=NATIVE_SOURCE_FLAG + list(extra))
    if result.get("native_source") is not True:
        raise RuntimeError(
            f"harness {binary} did not acknowledge --native-source; "
            "rebuild tests/reference to benchmark the matched acquisition route")
    return result


def median(values) -> float | None:
    values = [float(value) for value in values]
    return statistics.median(values) if values else None


def timed(callable_, warmups: int, repeats: int):
    """Warm up, then time ``repeats`` calls; returns (samples, last result)."""
    result = None
    for index in range(warmups + repeats):
        started = time.perf_counter()
        result = callable_()
        elapsed = time.perf_counter() - started
        if index >= warmups:
            yield elapsed, result
        else:
            yield None, result


def measure(callable_, warmups: int, repeats: int) -> tuple[list[float], object]:
    samples: list[float] = []
    result = None
    for elapsed, value in timed(callable_, warmups, repeats):
        if elapsed is not None:
            samples.append(elapsed)
        result = value
    return samples, result


class Ticker:
    """Counts how often a second Python thread can run."""

    def __init__(self) -> None:
        self.ticks = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)

    def _loop(self) -> None:
        while not self._stop.is_set():
            self.ticks += 1

    def __enter__(self) -> "Ticker":
        self._thread.start()
        return self

    def __exit__(self, *exception) -> None:
        self._stop.set()
        self._thread.join(timeout=5.0)


def rss_bytes() -> int | None:
    """Resident set size of this process, when the host exposes it."""
    status = Path("/proc/self/status")
    if not status.is_file():
        return None
    for line in status.read_text().splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1]) * 1024
    return None


def build_type(directory: Path) -> str:
    cache = directory / "CMakeCache.txt"
    if not cache.is_file():
        return "unknown"
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith("CMAKE_BUILD_TYPE:"):
            return line.split("=", 1)[1] or "unspecified"
    return "unknown"


# --------------------------------------------------------------------------
# Measurement sections
# --------------------------------------------------------------------------

def native_single(document: dict, binary: Path | None, warmups: int, repeats: int):
    if binary is None:
        return {"measured": False, "reason": "no harness binary available"}
    result = run_matched(
        binary, document,
        extra=["--route", "single", "--workers", "1", "--warmups", str(warmups),
               "--repeats", str(repeats)],
    )
    return {
        "measured": True,
        "samples_s": result["repetitions_seconds"],
        "median_s": median(result["repetitions_seconds"]),
        "best_s": min(result["repetitions_seconds"]),
        "rhs_evaluations": result["rhs_evaluations"],
        "checksum": result["checksum"],
        "setup_s": result["setup_seconds"],
    }


def python_single(document: dict, warmups: int, repeats: int):
    arguments = fixtures.scenario_arguments(document)
    call = lambda: leo.propagate(**arguments)  # noqa: E731
    samples, trajectory = measure(call, warmups, repeats)
    states = np.asarray(trajectory.states)
    # Construction of the identical configuration objects, without propagating.
    samples_config, _ = measure(
        lambda: fixtures.scenario_arguments(document), warmups, repeats)
    # One full-volume NumPy copy of the owning buffer: a proxy for the single
    # native-to-NumPy packing pass the binding performs internally.
    samples_copy, _ = measure(lambda: np.array(states, copy=True), warmups, repeats)
    return {
        "measured": True,
        "samples_s": samples,
        "median_s": median(samples),
        "best_s": min(samples),
        "configuration_construction_median_s": median(samples_config),
        "result_copy_proxy_median_s": median(samples_copy),
        "checksum": fixtures.states_checksum(states),
        "samples": int(states.shape[0]),
        "output_bytes": int(states.nbytes),
    }


def gil_probe(callable_) -> dict:
    with Ticker() as ticker:
        started = time.perf_counter()
        callable_()
        elapsed = time.perf_counter() - started
    return {"call_s": elapsed, "background_thread_ticks": ticker.ticks,
            "gil_released": ticker.ticks > 0}


def native_batch(document: dict, binary: Path | None, members: int,
                 workers: list[int], warmups: int, repeats: int) -> dict:
    if binary is None:
        return {"measured": False, "reason": "no harness binary available"}
    out = {}
    for count in workers:
        result = run_matched(
            binary, document,
            extra=["--route", "batch", "--members", str(members), "--workers",
                   str(count), "--warmups", str(warmups), "--repeats", str(repeats)],
        )
        out[str(count)] = {
            "median_s": median(result["repetitions_seconds"]),
            "best_s": min(result["repetitions_seconds"]),
            "workers_actual": result["workers_actual"],
            "checksum": result["checksum"],
            "rhs_evaluations": result["rhs_evaluations"],
        }
    return out


def python_homogeneous_batch(document: dict, members: int, workers: list[int],
                             warmups: int, repeats: int) -> dict:
    arguments = fixtures.scenario_arguments(document)
    initials = fixtures.member_initials(document, members)
    out = {}
    for count in workers:
        call = lambda count=count: leo.propagate_batch(  # noqa: E731
            initials=initials, spacecraft=arguments["spacecraft"],
            model=arguments["model"], times_s=arguments["times_s"],
            environment=arguments["environment"], options=arguments["options"],
            workers=count)
        samples, batch = measure(call, warmups, repeats)
        out[str(count)] = {
            "median_s": median(samples),
            "best_s": min(samples),
            "checksum": fixtures.states_checksum(np.asarray(batch.states)),
            "output_bytes": int(batch.states.nbytes),
            "gil": gil_probe(call),
        }
    return out


def python_heterogeneous_batch(document: dict, binary: Path | None, members: int,
                               workers: list[int], warmups: int, repeats: int) -> dict:
    arguments = fixtures.scenario_arguments(document)
    initials = fixtures.member_initials(document, members)
    spacecraft = fixtures.heterogeneous_spacecraft(document, members)
    models = fixtures.heterogeneous_models(members)
    environments = fixtures.heterogeneous_environments(document, members)
    documents = []
    for index, (craft, model, environment) in enumerate(zip(spacecraft, models, environments, strict=True)):
        item = copy.deepcopy(document)
        item["spacecraft"] = {
            "mass_kg": craft.mass_kg,
            "inertia_tensor_kg_m2": craft.inertia_kg_m2.tolist(),
            "surfaces": [{
                "area_m2": surface.area_m2, "normal": surface.normal_body.tolist(),
                "centre": surface.center_of_pressure_body_m.tolist(),
                "specular": surface.optical.specular, "diffuse": surface.optical.diffuse,
                "drag_coefficient": surface.aerodynamic.drag_coefficient,
                "accommodation": surface.aerodynamic.accommodation,
            } for surface in craft.surfaces],
        }
        item["models"].update(j2=model.gravity == "j2", gravity_gradient=model.gravity_gradient,
                              earth_albedo=model.earth_albedo, drag_model=model.drag_law,
                              eclipse_model=environment.eclipse, density_model="exponential")
        for name in ("srp", "drag", "albedo"):
            contribution = getattr(model, name) or leo.Contribution()
            item["models"][name] = contribution.enabled()
            item["models"][name + "_force"] = contribution.force
            item["models"][name + "_torque"] = contribution.torque
        # Read back exactly the same independently owned provider/config values.
        item["environment"]["sun_position_m"] = environment.sun.sample(0)[0].tolist()
        item["environment"]["atmosphere_angular_velocity_rad_s"] = environment.atmosphere_rotation_rad_s.tolist()
        item["models"]["density_scale"] = 1.0 + 0.1 * index
        documents.append(item)
    out = {}
    with tempfile.TemporaryDirectory(prefix="leo-heterogeneous-overhead-") as directory:
        path = Path(directory) / "configurations.json"
        path.write_text(json.dumps(documents, allow_nan=False))
        for count in workers:
            native = None if binary is None else run_matched(binary, document, extra=[
                "--route", "batch", "--members", str(members), "--workers", str(count),
                "--warmups", str(warmups), "--repeats", str(repeats),
                "--configurations", str(path), "--dump-batch"])
            call = lambda: leo.propagate_batch(
                initials=initials, spacecraft=spacecraft, model=models,
                times_s=arguments["times_s"], environment=environments,
                options=arguments["options"], workers=count)
            samples, batch = measure(call, warmups, repeats)
            if native is not None:
                fixtures.assert_identical(batch.states, fixtures.harness_trajectories(native),
                                          f"heterogeneous benchmark workers={count}")
            native_median = None if native is None else median(native["repetitions_seconds"])
            out[str(count)] = {
                "median_s": median(samples), "native_median_s": native_median,
                "python_minus_native_s": None if native_median is None else median(samples) - native_median,
                "native_exact_parity": native is not None,
                "output_bytes": int(batch.states.nbytes), "members": members,
            }
    return out


def memory_probe(document: dict, members: int, samples: int) -> dict:
    arguments = fixtures.scenario_arguments(document)
    initials = fixtures.member_initials(document, members)
    before = rss_bytes()
    batch = leo.propagate_batch(
        initials=initials, spacecraft=arguments["spacecraft"],
        model=leo.DynamicsModel(), times_s=np.linspace(0., 600., samples),
        options=arguments["options"],
        workers=1)
    after = rss_bytes()
    states = np.asarray(batch.states)
    return {
        "members": members,
        "workload": "point_mass; 600 seconds; dense output for retained-memory measurement",
        "samples": int(states.shape[1]),
        "states_shape": list(states.shape),
        "states_bytes": int(states.nbytes),
        "times_bytes": int(np.asarray(batch.times_s).nbytes),
        "rss_before_bytes": before,
        "rss_after_bytes": after,
        "rss_delta_bytes": None if before is None or after is None else after - before,
        "bytes_per_state": int(states.nbytes / states.shape[0] / states.shape[1]),
    }


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def report(results: dict) -> str:
    lines = []
    single = results["single"]
    native = single["native"]
    python = single["python"]
    lines.append("== single trajectory ({scenario}) ==".format(
        scenario=results["scenario"]))
    if native.get("measured") and python.get("measured"):
        ratio = python["median_s"] / native["median_s"]
        overhead = python["median_s"] - native["median_s"]
        lines.append(f"  native C++ (harness)        {native['median_s']:.6f} s")
        lines.append(f"  python total call           {python['median_s']:.6f} s")
        lines.append(f"  binding overhead            {overhead:+.6f} s "
                     f"({(ratio - 1.0) * 100.0:+.1f} % of native)")
        lines.append(f"    configuration construction {python['configuration_construction_median_s']:.6f} s")
        lines.append(f"    result copy proxy         {python['result_copy_proxy_median_s']:.6f} s")
        lines.append(f"  checksums agree             "
                     f"{native['checksum'] == python['checksum']}")
        lines.append("  Native-only through-binding time unavailable; difference is a noisy estimate.")
    else:
        lines.append(f"  native: {native.get('reason', 'not measured')}")
        if python.get("measured"):
            lines.append(f"  python total call           {python['median_s']:.6f} s")
    homogeneous = results["batch_homogeneous_python"]
    if isinstance(homogeneous, dict) and homogeneous:
        baseline = homogeneous.get("1", {}).get("median_s")
        lines.append("== homogeneous batch (python) ==")
        for count, entry in homogeneous.items():
            speedup = (baseline / entry["median_s"]) if baseline and entry["median_s"] else None
            lines.append(f"  workers={count:>2} {entry['median_s']:.6f} s"
                         + (f"  speedup {speedup:.2f}x" if speedup else "")
                         + f"  ticks={entry['gil']['background_thread_ticks']}")
    native_batch = results["batch_homogeneous_native"]
    if isinstance(native_batch, dict) and native_batch:
        baseline = native_batch["1"]["median_s"]
        lines.append("== homogeneous batch (native C++) ==")
        for count, entry in native_batch.items():
            lines.append(f"  workers={count:>2} {entry['median_s']:.6f} s"
                         f"  speedup {baseline / entry['median_s']:.2f}x")
    heterogeneous = results["batch_heterogeneous_python"]
    if heterogeneous:
        lines.append("== heterogeneous batch (Python / direct C++) ==")
        for count, entry in heterogeneous.items():
            native_text = "unavailable" if entry["native_median_s"] is None else f"{entry['native_median_s']:.6f}"
            lines.append(f"  workers={count} {entry['median_s']:.6f} / {native_text} s")
    memory = results["memory"]
    lines.append("== batch output memory ==")
    lines.append(f"  {memory['members']} x {memory['samples']} x 13 float64 = "
                 f"{memory['states_bytes'] / 1048576.0:.2f} MiB"
                 + (f", RSS delta {memory['rss_delta_bytes'] / 1048576.0:+.2f} MiB"
                    if memory["rss_delta_bytes"] is not None else ""))
    return "\n".join(lines)


# --------------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--harness", default="",
                        help="leo_reference_dump binary (default: search build dirs)")
    parser.add_argument("--scenario", default="coupled_case_a",
                        help="qualified scenario used for the single and batch runs")
    parser.add_argument("--members", type=int, default=8)
    parser.add_argument("--workers", default="1,2,4,8")
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--memory-members", type=int, default=64)
    parser.add_argument("--memory-samples", type=int, default=1001)
    parser.add_argument("--limit-s", type=float, default=None,
                        help="truncate the scenario schedule (default: full)")
    parser.add_argument("--skip-native", action="store_true")
    parser.add_argument("--out", type=Path,
                        default=HERE / "results" / "binding_overhead.json")
    arguments = parser.parse_args(argv)

    if arguments.repeats < 1 or arguments.warmups < 0:
        parser.error("--repeats must be positive and --warmups non-negative")
    workers = [int(value) for value in arguments.workers.split(",") if value.strip()]
    if not workers or min(workers) < 1 or arguments.members < 1 or arguments.memory_members < 1 or arguments.memory_samples < 2:
        parser.error("positive workers/member counts and at least two memory samples required")

    try:
        binary = None if arguments.skip_native else fixtures.resolve_harness(
            arguments.harness or None)
    except FileNotFoundError as error:
        if not arguments.skip_native:
            parser.error(str(error))
        binary = None

    document = fixtures.load_scenario(arguments.scenario)
    if arguments.limit_s is not None:
        document = fixtures.truncate(document, arguments.limit_s)

    single_native = native_single(document, binary, arguments.warmups,
                                  arguments.repeats)
    single_python = python_single(document, arguments.warmups, arguments.repeats)
    if single_native.get("measured") and single_python.get("measured"):
        difference = abs(single_native["checksum"] - single_python["checksum"])
        single_native["checksum_delta"] = difference
        single_native["checksum_matches_python"] = difference == 0.0

    batch_native = {} if binary is None else native_batch(
        document, binary, arguments.members, workers, arguments.warmups,
        arguments.repeats)
    batch_python = python_homogeneous_batch(document, arguments.members, workers,
                                            arguments.warmups, arguments.repeats)
    heterogeneous = python_heterogeneous_batch(
        document, binary, arguments.members, workers, arguments.warmups,
        arguments.repeats)
    memory = memory_probe(document, arguments.memory_members, arguments.memory_samples)

    package_build = next((root for root in fixtures.package_roots()
                          if (root / "leo_dynamics" / "__init__.py").is_file()), None)
    results = {
        "kind": "v0.1-python-binding-overhead",
        "scenario": arguments.scenario,
        "samples_per_trajectory": len(document["run"]["sample_times_s"]),
        "members": arguments.members,
        "workers": workers,
        "repeats": arguments.repeats,
        "warmups": arguments.warmups,
        "environment": {
            "python": platform.python_version(),
            "numpy": np.__version__,
            "platform": platform.platform(),
            "usable_cores": fixtures.thread_count(),
            "extension": str((package_build or "installed") / "leo_dynamics"),
            "extension_build_type": build_type(
                (package_build.parent if package_build else Path("."))),
            "harness": str(binary) if binary else None,
            "harness_build_type": build_type(
                binary.parent if binary else Path(".")),
            "release_optimised": None,
            "native_acquisition_route": MATCHED_ROUTE,
        },
        "single": {"native": single_native, "python": single_python},
        "batch_homogeneous_native": batch_native,
        "batch_homogeneous_python": batch_python,
        "batch_heterogeneous_python": heterogeneous,
        "memory": memory,
        "limitations": [
            "Native propagation-only time inside a single binding call is not "
            "separately measurable: the extension exposes no internal timer and "
            "packs results inside the same call. Python-minus-native is an estimate, "
            "not an upper bound; separate runs have noise and the harness counts RHS calls.",
            "The native baseline runs with --native-source, acquiring through the same "
            "production EarthEnvironment the Python binding composes (owned constant "
            "EphemerisProvider, same exponential atmosphere, eclipse and rotation). "
            "Trajectories are bit-identical to the default acquisition route.",
            "Python-minus-native is NOT pure packing cost: it also covers boundary "
            "validation and configuration materialisation on the Python side, and an "
            "RHS call counter the harness increments on every native evaluation, which "
            "biases the native baseline upward.",
            "No numerical speedup target is imposed; the solver is unchanged.",
        ],
    }
    build_types = [results["environment"]["extension_build_type"],
                   results["environment"]["harness_build_type"]]
    results["environment"]["release_optimised"] = all(
        value.lower() == "release" for value in build_types) if binary else None

    results["single"]["overhead"] = (
        {
            "python_minus_native_s": single_python["median_s"] - single_native["median_s"],
            "relative": single_python["median_s"] / single_native["median_s"] - 1.0,
        }
        if single_native.get("measured") and single_python.get("measured") else None
    )

    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    arguments.out.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
    print(report(results))
    print(f"\nresults written to {arguments.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())