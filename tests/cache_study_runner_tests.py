#!/usr/bin/env python3
"""ATM-CACHE-001 runner reliability regressions: resume, budget, interrupt, hashing.

Consumer-visible behaviour of docs/atmosphere/cache-study/run.py, exercised
against a stub study executable: a real child process per task that publishes
one study-result.json, so the ledger, the atomic publication and the scheduler
are tested exactly as the study uses them, without the scientific binary.
"""
import importlib.util
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "docs/atmosphere/cache-study/run.py"
spec = importlib.util.spec_from_file_location("cache_study_run", RUNNER)
runner = importlib.util.module_from_spec(spec)
# The runner declares dataclasses; they resolve annotations through the module
# entry in sys.modules, so it is registered before the module body executes.
sys.modules[spec.name] = runner
spec.loader.exec_module(runner)

STUB = """#!/usr/bin/env python3
""" + """import json, os, sys, time


def option(name, default=None):
    for index, token in enumerate(sys.argv):
        if token == name and index + 1 < len(sys.argv):
            return sys.argv[index + 1]
    return default


out = option("--out")
kind = option("--study-kind")
trace = option("--study-trace")
time.sleep(float(os.environ.get("STUB_SLEEP", "0.02")))
if os.environ.get("STUB_FAIL_MATCH") and os.environ["STUB_FAIL_MATCH"] in " ".join(sys.argv):
    print("stub failure requested", file=sys.stderr)
    sys.exit(7)
os.makedirs(out, exist_ok=True)
record = {"kind": kind, "study_result": True, "pid": os.getpid(), "start": time.time()}
error = float(os.environ.get("STUB_ERROR", "0.0005"))
if kind == "accuracy":
    # The study's published accuracy schema: the requested rung of the
    # resolution ladder with all three holdout packets, with the measured cost
    # and fallback share the runner reads when it ranks configurations.
    number = lambda name, default=None: (float(option(name)) if option(name) else default)
    fallback = float(os.environ.get("STUB_FALLBACK", "0.0"))
    rung = {"requested_rung": True,
            "settings": {"capacity": int(number("--study-capacity", 1)),
                         "scale": number("--study-scale", 1.0),
                         "input_set": option("--study-input-set", "direct-v2"),
                         "base_cells": {"altitude_km": number("--study-cell-km"),
                                        "latitude_deg": number("--study-cell-deg"),
                                        "longitude_deg": number("--study-cell-deg"),
                                        "time_s": number("--study-cell-s")},
                         "interpolation_mode": option("--study-interpolation"),
                         "interpolation_axis": option("--study-interpolation-axis"),
                         "capacity_policy": option("--study-capacity-policy")},
            "construction_native_calls": float(os.environ.get("STUB_NATIVE_CALLS", "100")),
            "construction_seconds": float(os.environ.get("STUB_CONSTRUCTION_SECONDS", "1")),
            "nonpositive_or_nonfinite_reconstructions": 0}
    # One scoring per configuration has to answer every budget: the runner
    # recomputes each verdict from the measured maximum and refuses a screen
    # that did not score one.
    budgets = {"%g" % b: error <= b for b in (0.0001, 0.001, 0.005, 0.01, 0.05)}
    for packet in ("holdout_uniform-random", "holdout_local-query-packet",
                   "holdout_broad-query-packet",
                   "holdout_stratified-physical-boundaries"):
        rung[packet] = {"queries_on_this_holdout": 10,
                        "native_calls_on_this_holdout": float(
                            os.environ.get("STUB_NATIVE_CALLS", "100")),
                        "fallback_fraction": fallback,
                        "results": {"error_metrics": {"maximum_absolute_relative": error},
                                    "threshold_pass_by_maximum": dict(
                                        budgets,
                                        decision_rule="maximum absolute relative error over "
                                                      "every tested sample")}}
    payload = {"metadata": {"study_kind": kind}, "result": {"resolution_ladder": [rung]}}
elif kind == "check":
    # The correctness stage publishes a single verdict; the runner gates the
    # coupled stages on it and never on the mere existence of the task.
    payload = {"metadata": {"study_kind": kind},
              "result": {"all_checks_passed": os.environ.get("STUB_CHECK_FAIL") != "1"}}
else:
    payload = dict(record)
payload["end"] = time.time()
with open(os.path.join(out, "study-result.json"), "w") as handle:
    json.dump(payload, handle)
if os.environ.get("STUB_LOG"):
    with open(os.environ["STUB_LOG"], "a") as log:
        log.write(json.dumps({"pid": os.getpid(), "out": out, "start": record["start"],
                              "end": payload["end"]}) + "\\n")
if trace:
    os.makedirs(os.path.dirname(trace) or ".", exist_ok=True)
    with open(trace, "wb") as handle:
        handle.write(b"trace")
"""


class RunnerHarness(unittest.TestCase):
    """A run directory, a stub study executable and every runner contract.

    Each test drives the real runner as a subprocess against a stub study
    executable, so the ledger, the atomic publication, the scheduler, the
    progressive screening, the correctness gate and the pinned input sets are
    exercised exactly as the study uses them, without the scientific binary.
    """

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.out = self.root / "study"
        self.data = self.root / "data"
        self.data.mkdir()
        for name in ("tls.bsp", "de.bsp", "earth.bpc", "space-weather.txt", "SOLFSMY.txt", "DTCFILE.txt"):
            (self.data / name).write_text(name + " pinned content\n")
        self.binary = self.root / "atm_cache_study"
        self.binary.write_text(STUB)
        self.binary.chmod(0o755)
        self.log = self.root / "children.jsonl"

    def tearDown(self):
        self.directory.cleanup()

    def argv(self, *extra, out=None, verification_only=True):
        command = [sys.executable, RUNNER, "--out", str(out or self.out),
                   "--binary", self.binary, "--deps-dir", self.data,
                   "--pck", self.data / "earth.bpc",
                   "--lsk", self.data / "tls.bsp", "--spk", self.data / "de.bsp",
                   "--weather", self.data / "space-weather.txt",
                   "--solfsmy", self.data / "SOLFSMY.txt",
                   "--dtcfile", self.data / "DTCFILE.txt", "--workers", "4"]
        if verification_only:
            command.append("--verification-only")
        return [str(part) for part in command + list(extra)]

    def expectations(self):
        digests = {}
        for key, name in (("lsk", "tls.bsp"), ("spk", "de.bsp"), ("pck", "earth.bpc"),
                          ("weather", "space-weather.txt"), ("solfsmy", "SOLFSMY.txt"),
                          ("dtcfile", "DTCFILE.txt")):
            digests[key] = f"{key}={runner.sha256_file(self.data / name)}"
        return [part for value in digests.values() for part in ("--expect-sha256", value)]

    def run_runner(self, *extra, env=None, expect_code=None, verification_only=None):
        environment = dict(os.environ)
        environment.update(env or {})
        environment.setdefault("STUB_LOG", str(self.log))
        command = self.argv(*extra) if verification_only is None else \
            self.argv(*extra, verification_only=verification_only)
        process = subprocess.run(command, capture_output=True, text=True,
                                 env=environment)
        if expect_code is not None:
            self.assertEqual(process.returncode, expect_code,
                             f"stdout:\n{process.stdout}\nstderr:\n{process.stderr}")
        return process

    def children(self):
        if not self.log.is_file():
            return []
        return [json.loads(line) for line in self.log.read_text().splitlines() if line.strip()]

    def run_dir(self):
        return self.out / "verification-only"

    def test_smoke_completes_every_stage_for_both_models(self):
        self.run_runner("--mode", "smoke", expect_code=runner.EXIT_OK)
        status = json.loads((self.run_dir() / "evidence/status.json").read_text())
        self.assertEqual(status["status"], "complete")
        self.assertTrue(status["counts"]["failed"] == 0)
        summary_files = {path.name for path in (self.run_dir() / "evidence").iterdir()}
        for name in ("sensitivity-summary.json", "locality-summary.json",
                     "accuracy-summary.json", "propagation-summary.json",
                     "performance-summary.json", "manifest.json", "reliability.json"):
            self.assertIn(name, summary_files)
        manifest = json.loads((self.run_dir() / "evidence/manifest.json").read_text())
        self.assertTrue(manifest["verification_only"])
        self.assertFalse(manifest["scientific_evidence"])
        self.assertTrue(manifest["commands"])
        for entry in manifest["commands"]:
            self.assertIn("--study-kind", entry["command"])
        self.assertIn("build", manifest)
        self.assertEqual(manifest["build"]["executed_by_runner"], False)

    def test_plan_is_deterministic(self):
        first_out = self.out
        second_out = self.root / "second"
        for target in (first_out, second_out):
            process = subprocess.run(
                self.argv("--mode", "smoke", "--kinds", "sensitivity", "--models", "nrlmsise00",
                          out=target),
                capture_output=True, text=True, env=dict(os.environ, STUB_LOG=str(self.log)))
            self.assertEqual(process.returncode, runner.EXIT_OK, process.stderr)
        first = json.loads((first_out / "verification-only/plan.json").read_text())
        second = json.loads((second_out / "verification-only/plan.json").read_text())
        self.assertEqual([task["task_id"] for task in first["tasks"]],
                         [task["task_id"] for task in second["tasks"]])
        def without_output_paths(tasks):
            stripped = []
            for task in tasks:
                for command in task["commands"]:
                    parts = []
                    skip = False
                    for part in command:
                        if skip:
                            skip = False
                            continue
                        if part == "--out":
                            skip = True
                            continue
                        parts.append(part)
                    stripped.append(parts)
            return stripped
        self.assertEqual(without_output_paths(first["tasks"]),
                         without_output_paths(second["tasks"]))

    def test_resume_executes_nothing_already_complete(self):
        self.run_runner("--mode", "smoke", expect_code=runner.EXIT_OK)
        executed = len(self.children())
        process = subprocess.run([sys.executable, RUNNER, "--resume", str(self.run_dir()),
                                  "--workers", "2"],
                                 capture_output=True, text=True,
                                 env=dict(os.environ, STUB_LOG=str(self.log)))
        self.assertEqual(process.returncode, runner.EXIT_OK, process.stderr)
        self.assertEqual(len(self.children()), executed,
                         "a resume re-executed work that was already complete")

    def test_altered_completed_result_refuses_resume(self):
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity", "--models", "nrlmsise00",
                        expect_code=runner.EXIT_OK)
        executed = len(self.children())
        victim = sorted((self.run_dir() / "results").iterdir())[0]
        victim.write_text('{"kind": "tampered"}\n')
        process = subprocess.run([sys.executable, RUNNER, "--resume", str(self.run_dir())],
                                 capture_output=True, text=True,
                                 env=dict(os.environ, STUB_LOG=str(self.log)))
        self.assertEqual(process.returncode, runner.EXIT_USAGE, process.stderr)
        self.assertEqual(len(self.children()), executed,
                         "a corrupted completed result must not silently repeat scientific work")

    def test_changed_binary_refuses_resume(self):
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity", "--models", "nrlmsise00",
                        expect_code=runner.EXIT_OK)
        self.binary.write_text(STUB + "# rebuilt\n")
        process = subprocess.run([sys.executable, RUNNER, "--resume", str(self.run_dir())],
                                 capture_output=True, text=True,
                                 env=dict(os.environ, STUB_LOG=str(self.log)))
        self.assertEqual(process.returncode, runner.EXIT_USAGE)
        self.assertIn("refusing to resume", process.stderr)

    def test_budget_stops_scheduling_and_keeps_completed_work(self):
        process = self.run_runner("--mode", "smoke", "--max-hours", "0.0", "--kinds", "sensitivity")
        self.assertEqual(process.returncode, runner.EXIT_INCOMPLETE)
        status = json.loads((self.run_dir() / "evidence/status.json").read_text())
        self.assertEqual(status["status"], "incomplete")
        self.assertTrue(status["pending"])
        self.assertEqual(self.children(), [])
        self.assertTrue(any("budget" in limitation for limitation in status["limitations"]))
        # The pending work is retained and finishes on resume.
        process = self.run_runner("--resume", str(self.run_dir()))
        self.assertEqual(process.returncode, runner.EXIT_OK, process.stderr)
        status = json.loads((self.run_dir() / "evidence/status.json").read_text())
        self.assertEqual(status["status"], "complete")

    def test_failure_preserves_diagnostics_and_lets_others_finish(self):
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity", "--models", "nrlmsise00",
                        env={"STUB_FAIL_MATCH": "quiet_june"}, expect_code=runner.EXIT_INCOMPLETE)
        status = json.loads((self.run_dir() / "evidence/status.json").read_text())
        self.assertEqual(status["status"], "incomplete")
        self.assertTrue(status["failures"])
        failed_id = sorted(status["failures"])[0]
        ledger = json.loads((self.run_dir() / "ledger.json").read_text())
        record = ledger["tasks"][failed_id]
        self.assertEqual(record["status"], "failed")
        log = Path(record["log"])
        self.assertTrue(log.is_file())
        self.assertIn("stub failure requested", log.read_text())
        self.assertFalse(record.get("results"))
        completed = [task for task in ledger["tasks"].values() if task["status"] == "completed"]
        self.assertTrue(completed, "independent work must continue after one failure")
        # The failed task is retried, not skipped.
        process = self.run_runner("--resume", str(self.run_dir()))
        self.assertEqual(process.returncode, runner.EXIT_OK, process.stderr)

    def test_interrupt_reaps_children_and_keeps_work_pending(self):
        process = subprocess.Popen(self.argv("--mode", "smoke"), stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True,
                                   env=dict(os.environ, STUB_LOG=str(self.log),
                                            STUB_SLEEP="0.4"))
        time.sleep(1.5)
        process.send_signal(signal.SIGINT)
        process.communicate(timeout=60)
        self.assertEqual(process.returncode, runner.EXIT_INTERRUPTED)
        time.sleep(0.2)
        for child in self.children():
            with self.assertRaises(ProcessLookupError):
                os.kill(child["pid"], 0)
        ledger = json.loads((self.run_dir() / "ledger.json").read_text())
        self.assertNotIn("running", {task["status"] for task in ledger["tasks"].values()})
        status = json.loads((self.run_dir() / "evidence/status.json").read_text())
        self.assertEqual(status["status"], "incomplete")
        process = self.run_runner("--resume", str(self.run_dir()), env={"STUB_SLEEP": "0.02"})
        self.assertEqual(process.returncode, runner.EXIT_OK, process.stderr)

    def test_benchmarks_never_overlap_another_child(self):
        self.run_runner("--mode", "smoke", "--workers", "4", env={"STUB_SLEEP": "0.05"},
                        expect_code=runner.EXIT_OK)
        children = self.children()
        self.assertGreater(len(children), 10)
        for index, child in enumerate(children):
            if "/bench-" not in child["out"]:
                continue
            for other in children[:index]:
                self.assertFalse(child["start"] < other["end"] and other["start"] < child["end"],
                                 f"benchmark child {child['out']} overlapped {other['out']}")

    def test_primary_run_refuses_unverified_or_mismatched_inputs(self):
        # No pinned hashes at all.
        environment = dict(os.environ, STUB_LOG=str(self.log))
        command = self.argv("--mode", "smoke", "--kinds", "sensitivity",
                            verification_only=False)
        process = subprocess.run(command, capture_output=True, text=True, env=environment)
        self.assertEqual(process.returncode, runner.EXIT_BLOCKED, process.stderr)
        blocked = json.loads((self.out / "blocked.json").read_text())
        self.assertIn("lsk: expected", " ".join(blocked["problems"]))
        # A pinned hash that does not match the file on disk.
        command = command + ["--expect-sha256", "lsk=" + "0" * 64]
        process = subprocess.run(command, capture_output=True, text=True, env=environment)
        self.assertEqual(process.returncode, runner.EXIT_BLOCKED)
        self.assertIn("lsk: expected", process.stdout + process.stderr)
        self.assertEqual(self.children(), [])
        # Matching hashes start the primary run.
        command = command[:-2] + self.expectations()
        process = subprocess.run(command, capture_output=True, text=True, env=environment,
                                timeout=600)
        self.assertEqual(process.returncode, runner.EXIT_OK, process.stderr)
        self.assertTrue((self.out / "ledger.json").is_file())
        self.assertFalse((self.out / "verification-only").exists())
        manifest = json.loads((self.out / "evidence/manifest.json").read_text())
        self.assertFalse(manifest["verification_only"])
        self.assertTrue(manifest["scientific_evidence"])

    def test_full_mode_records_a_complete_blocked_plan_without_executing(self):
        (self.data / "SOLFSMY.txt").unlink()
        # No pinned hashes and, deliberately, an unusable dataset directory: the
        # full study must still initialise its complete plan and ledger.
        command = self.argv("--mode", "full", "--deps-dir", self.root / "absent",
                            verification_only=False)
        process = subprocess.run(command, capture_output=True, text=True,
                                 env=dict(os.environ, STUB_LOG=str(self.log)))
        self.assertEqual(process.returncode, runner.EXIT_BLOCKED, process.stderr)
        self.assertEqual(self.children(), [])
        ledger = json.loads((self.out / "ledger.json").read_text())
        self.assertIsNone(ledger["identity"], "a blocked run must not bind its identity")
        statuses = {task["status"] for task in ledger["tasks"].values()}
        self.assertEqual(statuses, {"blocked"})
        self.assertEqual((ledger["plan"]["parameters"])["mode"], "full")
        plan = json.loads((self.out / "plan.json").read_text())
        kinds = {task["kind"] for task in plan["tasks"]}
        self.assertEqual(kinds, {"sensitivity", "locality", "accuracy", "check",
                                 "propagation", "benchmark"})
        self.assertTrue(all(task["model"] in ("nrlmsise00", "jb2008") for task in plan["tasks"]))
        blocked = json.loads((self.out / "blocked.json").read_text())
        self.assertEqual(blocked["executed_tasks"], 0)
        self.assertEqual(blocked["planned_tasks"], len(plan["tasks"]))
        self.assertTrue(blocked["missing_inputs"])
        self.assertIn("--resume", blocked["resume_command"])
        status = json.loads((self.out / "evidence/status.json").read_text())
        self.assertEqual(status["status"], "blocked")
        self.assertEqual(status["counts"]["blocked"], len(plan["tasks"]))
        self.assertEqual(status["counts"]["failed"], 0)
        self.assertEqual(status["counts"]["completed"], 0)
        self.assertTrue(status["dependency_blockers"])

    def test_changed_input_data_refuses_resume(self):
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity", "--models", "nrlmsise00",
                        expect_code=runner.EXIT_OK)
        executed = len(self.children())
        (self.data / "space-weather.txt").write_text("different pinned content\n")
        process = subprocess.run([sys.executable, RUNNER, "--resume", str(self.run_dir())],
                                 capture_output=True, text=True,
                                 env=dict(os.environ, STUB_LOG=str(self.log)))
        self.assertEqual(process.returncode, runner.EXIT_USAGE)
        self.assertIn("refusing to resume", process.stderr)
        self.assertEqual(len(self.children()), executed)

    def test_failed_retry_preserves_published_results_of_other_tasks(self):
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity", "--models", "nrlmsise00",
                        env={"STUB_FAIL_MATCH": "quiet_june"}, expect_code=runner.EXIT_INCOMPLETE)
        published = {path.name: path.read_bytes()
                     for path in (self.run_dir() / "results").iterdir()}
        self.run_runner("--resume", str(self.run_dir()),
                        env={"STUB_FAIL_MATCH": "quiet_june"}, expect_code=runner.EXIT_INCOMPLETE)
        for name, content in published.items():
            self.assertEqual((self.run_dir() / "results" / name).read_bytes(), content,
                             "a failing retry must preserve independently completed results")

    def test_verification_only_results_are_never_primary_evidence(self):
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity", "--models", "jb2008",
                        expect_code=runner.EXIT_OK)
        status = json.loads((self.run_dir() / "evidence/status.json").read_text())
        self.assertFalse(status["scientific_evidence"])
        self.assertTrue(status["verification_only"])
        summary = json.loads((self.run_dir() / "evidence/sensitivity-summary.json").read_text())
        self.assertFalse(summary["scientific_evidence"])
        # A resume of a verification run stays verification: it cannot be promoted.
        process = subprocess.run([sys.executable, RUNNER, "--resume", str(self.run_dir())],
                                 capture_output=True, text=True,
                                 env=dict(os.environ, STUB_LOG=str(self.log)))
        self.assertEqual(process.returncode, runner.EXIT_OK, process.stderr)
        manifest = json.loads((self.run_dir() / "evidence/manifest.json").read_text())
        self.assertTrue(manifest["verification_only"])
        self.assertFalse(manifest["scientific_evidence"])
        self.assertTrue(any("verification-only" in item for item in manifest["limitations"]))

    def test_shortlist_selects_only_measured_passing_configurations(self):
        self.run_runner("--mode", "smoke", "--kinds", "accuracy", "--models", "nrlmsise00",
                        env={"STUB_ERROR": "0.0005"}, expect_code=runner.EXIT_OK)
        plan = json.loads((self.run_dir() / "plan.json").read_text())
        policy = plan["shortlist"]
        eligible = [entry for entry in policy["eligible"]
                    if entry.get("worst_abs_rel_error_percent") is not None]
        self.assertTrue(eligible)
        for entry in eligible:
            self.assertLessEqual(entry["worst_abs_rel_error_percent"],
                                 policy["threshold_relative_error_percent"])
            self.assertTrue(entry["cell_km"] is None or entry["cell_km"] > 0)
        # The exact configuration, not merely the candidate name, is recorded.
        self.assertIn("cell_km", eligible[0])
        self.assertIn("capacity_policy", eligible[0])

    def test_unscreened_configurations_are_never_coupled_or_benchmarked(self):
        # A result with no requested rung and no holdout packets screens nothing,
        # so nothing may be propagated or benchmarked.
        process = self.run_runner("--mode", "smoke", "--models", "jb2008",
                                  env={"STUB_ERROR": "nan"})
        self.assertEqual(process.returncode, runner.EXIT_INCOMPLETE)
        ledger = json.loads((self.run_dir() / "ledger.json").read_text())
        coupled = [task for task in ledger["tasks"].values()
                   if task["kind"] in ("propagation", "benchmark")]
        self.assertTrue(coupled)
        for task in coupled:
            self.assertEqual(task["status"], "blocked")
            self.assertFalse(task.get("results"))
        plan = json.loads((self.run_dir() / "plan.json").read_text())
        self.assertIn("coupled", plan["blocked_stages"])

    def test_failing_screening_blocks_the_coupled_stages(self):
        # 50 percent relative error is far above the screening threshold.
        process = self.run_runner("--mode", "smoke", "--models", "jb2008",
                                  env={"STUB_ERROR": "50.0"})
        self.assertEqual(process.returncode, runner.EXIT_INCOMPLETE)
        ledger = json.loads((self.run_dir() / "ledger.json").read_text())
        for task in ledger["tasks"].values():
            if task["kind"] in ("propagation", "benchmark"):
                self.assertEqual(task["status"], "blocked")


    def manifest(self, solver=None):
        document = {"schema_version": 1, "id": "exp-test-pins",
                    "classification": "separately-pinned-experimental",
                    "inputs": {}}
        names = {"lsk": "tls.bsp", "spk": "de.bsp", "pck": "earth.bpc",
                 "weather": "space-weather.txt", "solfsmy": "SOLFSMY.txt",
                 "dtcfile": "DTCFILE.txt"}
        for key, name in names.items():
            document["inputs"][key] = {"filename": name,
                                       "sha256": runner.sha256_file(self.data / name)}
        path = self.root / "input-set.json"
        path.write_text(json.dumps(document, indent=2))
        return path

    def test_default_input_set_is_direct_v2_and_is_never_relabelled(self):
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity",
                        expect_code=runner.EXIT_OK)
        manifest = json.loads((self.run_dir() / "evidence/manifest.json").read_text())
        self.assertEqual(manifest["input_set"]["id"], "direct-v2")
        self.assertFalse(manifest["input_set"]["explicitly_selected"])
        for entry in manifest["commands"]:
            self.assertNotIn("--study-input-set", entry["command"])
        summary = json.loads((self.run_dir() / "evidence/sensitivity-summary.json").read_text())
        self.assertEqual(summary["input_set"]["id"], "direct-v2")

    def test_selected_input_set_is_recorded_and_forwarded_to_every_child(self):
        path = self.manifest()
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity",
                        "--input-set", str(path), verification_only=False,
                        expect_code=runner.EXIT_OK)
        run_dir = self.out
        manifest = json.loads((run_dir / "evidence/manifest.json").read_text())
        self.assertEqual(manifest["input_set"]["id"], "exp-test-pins")
        self.assertEqual(manifest["input_set"]["manifest_sha256"], runner.sha256_file(path))
        self.assertEqual(manifest["evidence_class"], "scientific-experimental-pinned")
        self.assertFalse(manifest["verification_only"])
        self.assertTrue(manifest["scientific_evidence"])
        self.assertTrue(manifest["commands"])
        for entry in manifest["commands"]:
            command = entry["command"]
            self.assertIn("--study-input-set", command)
            self.assertEqual(command[command.index("--study-input-set") + 1], "exp-test-pins")
        plan = json.loads((run_dir / "plan.json").read_text())
        self.assertEqual(plan["parameters"]["input_set"]["id"], "exp-test-pins")

    def test_selected_input_set_refuses_hash_overrides(self):
        path = self.manifest()
        process = self.run_runner("--mode", "smoke", "--kinds", "sensitivity",
                                  "--input-set", str(path),
                                  "--expect-sha256", "lsk=" + "0" * 64,
                                  verification_only=False)
        self.assertNotEqual(process.returncode, runner.EXIT_OK)
        self.assertIn("--input-set is the pin for this run", process.stderr + process.stdout)
        self.assertEqual(self.children(), [])

    def test_selected_input_set_refuses_the_verification_only_label(self):
        path = self.manifest()
        process = self.run_runner("--mode", "smoke", "--kinds", "sensitivity",
                                  "--input-set", str(path))
        self.assertNotEqual(process.returncode, runner.EXIT_OK)
        self.assertIn("scientifically pinned experiment",
                      process.stderr + process.stdout)

    def test_input_set_that_does_not_pin_every_input_is_refused(self):
        path = self.root / "partial.json"
        path.write_text(json.dumps({"id": "partial", "inputs": {"lsk": {
            "filename": "tls.bsp", "sha256": runner.sha256_file(self.data / "tls.bsp")}}}))
        process = self.run_runner("--mode", "smoke", "--input-set", str(path))
        self.assertNotEqual(process.returncode, runner.EXIT_OK)
        self.assertIn("does not pin", process.stderr + process.stdout)

    def test_changed_input_set_manifest_refuses_resume(self):
        path = self.manifest()
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity",
                        "--input-set", str(path), verification_only=False,
                        expect_code=runner.EXIT_OK)
        executed = len(self.children())
        document = json.loads(path.read_text())
        document["inputs"]["solfsmy"]["sha256"] = "0" * 64
        path.write_text(json.dumps(document, indent=2))
        process = subprocess.run([sys.executable, RUNNER, "--resume", str(self.out)],
                                 capture_output=True, text=True,
                                 env=dict(os.environ, STUB_LOG=str(self.log)))
        # The pin gate refuses first: a manifest whose hash no longer matches the
        # bytes on disk blocks the run rather than re-pinning anything.
        self.assertIn(process.returncode, (runner.EXIT_BLOCKED, runner.EXIT_USAGE), process.stderr)
        self.assertIn("solfsmy", process.stdout + process.stderr)
        self.assertEqual(len(self.children()), executed)
    """The screening ladder, the correctness gate and the selection policy."""

    def test_passing_rung_stops_the_family_from_being_refined(self):
        # 0.05 % is inside the 1 % screening threshold, so the next rung of the
        # family is never executed.
        self.run_runner("--mode", "screen", "--kinds", "accuracy", "--models", "nrlmsise00",
                        "--capacities", "4096,262144",
                        env={"STUB_ERROR": "0.0005"}, expect_code=runner.EXIT_OK)
        plan = json.loads((self.run_dir() / "plan.json").read_text())
        widths = {task["params"].get("cell_km")
                  for task in plan["tasks"] if task["kind"] == "accuracy"}
        self.assertIn(25.0, widths)
        self.assertNotIn(10.0, widths, "a family that already meets the threshold must not "
                                      "be refined further")

    def test_missing_rung_opens_the_next_one(self):
        # 2 % misses the 1 % threshold, so the finer rung is screened as well.
        self.run_runner("--mode", "screen", "--kinds", "accuracy", "--models", "nrlmsise00",
                        env={"STUB_ERROR": "0.02"}, expect_code=runner.EXIT_OK)
        plan = json.loads((self.run_dir() / "plan.json").read_text())
        widths = {task["params"].get("cell_km")
                  for task in plan["tasks"] if task["kind"] == "accuracy"}
        self.assertIn(10.0, widths)

    def test_failed_correctness_checks_block_the_coupled_stages(self):
        self.run_runner("--mode", "smoke", env={"STUB_CHECK_FAIL": "1"},
                        expect_code=runner.EXIT_INCOMPLETE)
        ledger = json.loads((self.run_dir() / "ledger.json").read_text())
        coupled = [task for task in ledger["tasks"].values()
                   if task["kind"] in ("propagation", "benchmark")]
        self.assertTrue(coupled)
        for task in coupled:
            self.assertEqual(task["status"], "blocked")
            self.assertFalse(task.get("results"))
        plan = json.loads((self.run_dir() / "plan.json").read_text())
        self.assertIn("coupled", plan["blocked_stages"])

    def test_error_budget_requires_every_independent_holdout_to_pass(self):
        rung = {"requested_rung": True}
        for index, packet in enumerate(runner.HOLDOUT_PACKETS):
            maximum = 0.02 if index == 3 else 0.00005
            rung[packet] = {
                "fallback_fraction": 0.0,
                "results": {
                    "error_metrics": {"maximum_absolute_relative": maximum},
                    "threshold_pass_by_maximum": {
                        "%g" % (budget / 100.0): maximum * 100.0 <= budget
                        for budget in runner.REPORTED_BUDGETS_PERCENT}}}
        verdict = runner.screen_accuracy(
            {"cell_km": None}, {"result": {"resolution_ladder": [rung]}})
        self.assertEqual(verdict["problems"], [])
        self.assertEqual(verdict["worst_relative_percent"], 2.0)
        self.assertEqual(verdict["budgets_met_percent"], [5.0])

    def test_fallback_only_accuracy_is_never_selected(self):
        self.run_runner("--mode", "smoke", env={"STUB_ERROR": "0.0005",
                                                "STUB_FALLBACK": "0.9"},
                        expect_code=runner.EXIT_INCOMPLETE)
        plan = json.loads((self.run_dir() / "plan.json").read_text())
        policy = plan["shortlist"]
        for entry in policy["eligible"]:
            if entry["candidate"] != "control":
                self.assertLessEqual(entry["max_fallback_fraction"],
                                     policy["max_screening_fallback_fraction"])
        ledger = json.loads((self.run_dir() / "ledger.json").read_text())
        for task in ledger["tasks"].values():
            if task["kind"] in ("propagation", "benchmark"):
                self.assertEqual(task["status"], "blocked")

    def test_every_candidate_keeps_a_representative_configuration(self):
        self.run_runner("--mode", "smoke", "--kinds", "accuracy,check,propagation,benchmark",
                        env={"STUB_ERROR": "0.0005"}, expect_code=runner.EXIT_OK)
        plan = json.loads((self.run_dir() / "plan.json").read_text())
        candidates = {entry["candidate"] for entry in plan["shortlist"]["eligible"]}
        for name in ("exact", "cell", "cells", "interp", "adaptive", "control"):
            self.assertIn(name, candidates)
    """Validated import of existing evidence."""

    def donor(self, compatible=True):
        directory = self.root / "donor" / "daily-ap"
        directory.mkdir(parents=True)
        for name in ("nrlmsise-00.c", "nrlmsise-00.h", "nrlmsise-00_data.c", "JB2008.f"):
            (self.data / name).write_text(name + " source\n")
        sources = {name: runner.sha256_file(self.data / name)
                   for name in ("nrlmsise-00.c", "nrlmsise-00.h", "nrlmsise-00_data.c",
                                "JB2008.f")}
        (self.root / "donor" / "manifest.json").write_text(json.dumps(
            {"kind": "atm-cache-study-manifest", "sources": sources}))
        metadata = {
            "kind": "atm-cache-study-result", "study_kind": "sensitivity",
            "measurement_mode": "native-geodetic-partial", "verification_only": False,
            "input_set": "direct-v2", "model": "nrlmsise00", "master_seed": 20261014,
            "frame": "ITRF93", "utc_reuse": True, "orientation_reuse": True,
            "sun_sharing": True, "altitude_domain_km": [200, 1000],
            "polar_fallback_deg": 89.999,
            "fixture_epochs": [{"name": name, "utc": utc}
                               for name, utc in runner.FIXTURE_EPOCHS],
            "kernels": [{"path": str(self.data / "tls.bsp"),
                         "sha256": runner.sha256_file(self.data / "tls.bsp")}],
            "cssi_weather_archive": {"path": str(self.data / "space-weather.txt"),
                                     "sha256": runner.sha256_file(self.data / "space-weather.txt")},
        }
        if not compatible:
            metadata["cssi_weather_archive"]["sha256"] = "0" * 64
        (directory / "study-result.json").write_text(json.dumps(
            {"metadata": metadata, "result": {"weather_pool": []}}))
        return self.root / "donor"

    def test_compatible_evidence_is_imported_with_its_producer_hashes(self):
        donor = self.donor()
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity",
                        "--reuse-native", str(donor), "--native-source-dir", str(self.data),
                        expect_code=runner.EXIT_OK)
        index = json.loads((self.run_dir() / "reuse-index.json").read_text())
        self.assertEqual(len(index["imported"]), 1)
        entry = index["imported"][0]
        self.assertTrue(Path(entry["published"]).is_file())
        self.assertEqual(entry["donor_sha256"],
                         runner.sha256_file(self.root / "donor/daily-ap/study-result.json"))
        self.assertFalse(entry["completes_a_planned_task"])
        self.assertIn("never a substitute", entry["allowed_use"])
        manifest = json.loads((self.run_dir() / "evidence/manifest.json").read_text())
        self.assertTrue(manifest["reused_evidence"]["imported"])
        # An imported result is evidence, not a completed task: it never appears
        # in the ledger or in the plan.
        ledger = json.loads((self.run_dir() / "ledger.json").read_text())
        self.assertNotIn(entry["name"], ledger["tasks"])
        plan = json.loads((self.run_dir() / "plan.json").read_text())
        self.assertNotIn(entry["name"], {task["task_id"] for task in plan["tasks"]})

    def test_declared_provenance_supplies_the_producer_sources(self):
        # A donor directory with no manifest of its own is still validated when
        # the caller supplies the reuse-provenance record that names the
        # authoritative native sources it was built from.
        directory = self.root / "sourceless" / "daily-ap"
        directory.mkdir(parents=True)
        for name in ("nrlmsise-00.c", "nrlmsise-00.h", "nrlmsise-00_data.c", "JB2008.f"):
            (self.data / name).write_text(name + " source\n")
        sources = {name: runner.sha256_file(self.data / name)
                   for name in ("nrlmsise-00.c", "nrlmsise-00.h", "nrlmsise-00_data.c",
                                "JB2008.f")}
        provenance = self.root / "native-reuse-provenance.json"
        provenance.write_text(json.dumps(
            {"entries": [{"mode": "daily-ap",
                          "source_dependencies": {"native_C_and_data": sources}}]}))
        # The study writes raw JSON arrays through string fields, so the grid the
        # runner compares is parsed, not formatted.
        metadata = {
            "study_kind": "sensitivity", "measurement_mode": "native-geodetic-partial",
            "verification_only": False, "input_set": "direct-v2", "model": "nrlmsise00",
            "master_seed": 20261014, "frame": "ITRF93", "utc_reuse": True,
            "orientation_reuse": True, "sun_sharing": True,
            "altitude_domain_km": "[200,1000]", "polar_fallback_deg": 89.999,
            "kernels": [{"path": str(self.data / "tls.bsp"),
                         "sha256": runner.sha256_file(self.data / "tls.bsp")}],
            "cssi_weather_archive": {"path": str(self.data / "space-weather.txt"),
                                     "sha256": runner.sha256_file(self.data / "space-weather.txt")},
        }
        (directory / "study-result.json").write_text(json.dumps(
            {"metadata": metadata, "result": {"weather_pool": []}}))
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity",
                        "--reuse-native", str(self.root / "sourceless"),
                        "--reuse-provenance", str(provenance),
                        "--native-source-dir", str(self.data),
                        expect_code=runner.EXIT_OK)
        index = json.loads((self.run_dir() / "reuse-index.json").read_text())
        self.assertEqual(len(index["imported"]), 1)
        self.assertEqual(index["declared_provenance"], [str(provenance)])
        self.assertTrue(all(check["passed"] for check in index["imported"][0]["checks"]))

    def test_provenance_that_names_other_native_sources_is_refused(self):
        donor = self.donor(compatible=False)
        (self.root / "donor" / "manifest.json").unlink()
        for name in ("nrlmsise-00.c", "nrlmsise-00.h", "nrlmsise-00_data.c", "JB2008.f"):
            (self.data / name).write_text(name + " a different source\n")
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity",
                        "--reuse-native", str(donor), "--native-source-dir", str(self.data),
                        expect_code=runner.EXIT_OK)
        index = json.loads((self.run_dir() / "reuse-index.json").read_text())
        self.assertFalse(index["imported"])
        self.assertTrue(any("native sources" in check
                            for check in index["refused"][0]["failed_checks"]))

    def test_incompatible_evidence_is_refused(self):
        donor = self.donor(compatible=False)
        self.run_runner("--mode", "smoke", "--kinds", "sensitivity",
                        "--reuse-native", str(donor), "--native-source-dir", str(self.data),
                        expect_code=runner.EXIT_OK)
        index = json.loads((self.run_dir() / "reuse-index.json").read_text())
        self.assertFalse(index["imported"])
        self.assertEqual(len(index["refused"]), 1)
        self.assertTrue(any("input bytes" in check
                            for check in index["refused"][0]["failed_checks"]))


if __name__ == "__main__":
    unittest.main()