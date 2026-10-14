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
    # resolution ladder with all three holdout packets.
    number = lambda name, default=None: (float(option(name)) if option(name) else default)
    rung = {"requested_rung": True,
            "settings": {"capacity": int(number("--study-capacity", 1)),
                         "scale": number("--study-scale", 1.0),
                         "base_cells": {"altitude_km": number("--study-cell-km"),
                                        "latitude_deg": number("--study-cell-deg"),
                                        "longitude_deg": number("--study-cell-deg"),
                                        "time_s": number("--study-cell-s")},
                         "interpolation_mode": option("--study-interpolation"),
                         "interpolation_axis": option("--study-interpolation-axis"),
                         "capacity_policy": option("--study-capacity-policy")},
            "nonpositive_or_nonfinite_reconstructions": 0}
    for packet in ("holdout_uniform-random", "holdout_local-query-packet",
                   "holdout_broad-query-packet"):
        rung[packet] = {"results": {"error_metrics": {"maximum_absolute_relative": error}}}
    payload = {"metadata": {"study_kind": kind}, "result": {"resolution_ladder": [rung]}}
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

    def run_runner(self, *extra, env=None, expect_code=None):
        environment = dict(os.environ)
        environment.update(env or {})
        environment.setdefault("STUB_LOG", str(self.log))
        process = subprocess.run(self.argv(*extra), capture_output=True, text=True,
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


if __name__ == "__main__":
    unittest.main()