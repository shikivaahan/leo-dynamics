#!/usr/bin/env python3
"""Consumer-visible partition, binary merge and real child-lifecycle regressions."""
import importlib.util
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[1] / "docs/multithreading/process_batch.py"
spec = importlib.util.spec_from_file_location("process_batch", MODULE)
process_batch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(process_batch)


class ProcessBatchTests(unittest.TestCase):
    def test_partition_owns_every_input_once_including_edges(self):
        for total in (0, 1, 7, 64):
            for workers in (1, 4, 8, 100):
                with self.subTest(total=total, workers=workers):
                    ranges = process_batch.split_ranges(total, workers)
                    owned = [i for begin, count in ranges for i in range(begin, begin + count)]
                    self.assertEqual(owned, list(range(total)))
                    self.assertTrue(all(count > 0 for _, count in ranges))
        with self.assertRaises(process_batch.IsolationError):
            process_batch.split_ranges(0, 0)

    def write_part(self, path, total, begin, count, labels=(0.0, 1.0)):
        with path.open("wb") as out:
            out.write(process_batch.HEADER.pack(process_batch.MAGIC, 1234.5, total, begin,
                                                count, len(labels), 13))
            out.write(struct.pack(f"<{len(labels)}d", *labels))
            for i in range(begin, begin + count):
                for sample in range(len(labels)):
                    out.write(struct.pack("<13d", *[1000.0*i + 100.0*sample + k for k in range(13)]))
        return path

    def test_merge_preserves_complete_states_labels_and_input_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = self.write_part(root / "a.bin", 3, 0, 2)
            second = self.write_part(root / "b.bin", 3, 2, 1)
            out = root / "merged.bin"
            process_batch.merge_parts([(0, first), (2, second)], out, 3, 2)
            header = process_batch.read_header(out)
            self.assertEqual((header["epoch"], header["begin"], header["count"]), (1234.5, 0, 3))
            raw = out.read_bytes()
            self.assertEqual(struct.unpack_from("<2d", raw, process_batch.HEADER_BYTES), (0.0, 1.0))
            actual = struct.unpack_from("<78d", raw, process_batch.payload_offset(2))
            expected = tuple(1000.0*i + 100.0*j + k for i in range(3) for j in range(2) for k in range(13))
            self.assertEqual(actual, expected)

    def test_bad_parts_never_replace_an_existing_result(self):
        for fault in ("gap", "schedule", "truncated"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                first = self.write_part(root / "a.bin", 3, 0, 2)
                begin = 3 if fault == "gap" else 2
                labels = (0.0, 2.0) if fault == "schedule" else (0.0, 1.0)
                second = self.write_part(root / "b.bin", 3, begin, 1, labels)
                if fault == "truncated":
                    second.write_bytes(second.read_bytes()[:-8])
                out = root / "result.bin"
                out.write_bytes(b"previous result")
                with self.assertRaises(process_batch.IsolationError):
                    process_batch.merge_parts([(0, first), (begin, second)], out, 3, 2)
                self.assertEqual(out.read_bytes(), b"previous result")
                self.assertFalse(out.with_name(out.name + ".partial").exists())

    def assert_children_reaped(self, commands, expected_exception):
        launched = []
        real_popen = subprocess.Popen
        def launch(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            launched.append(child)
            return child
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(process_batch.subprocess, "Popen", side_effect=launch):
                with self.assertRaises(expected_exception):
                    process_batch.run_workers(commands, directory)
            self.assertTrue(launched)
            self.assertTrue(all(child.returncode is not None for child in launched))
            self.assertTrue(all(child.poll() is not None for child in launched))

    def test_launch_failure_reaps_the_already_started_child(self):
        sleeper = [sys.executable, "-c", "import time; time.sleep(30)"]
        with tempfile.TemporaryDirectory() as directory:
            self.assert_children_reaped([sleeper, [str(Path(directory) / "missing-executable")]], FileNotFoundError)

    def test_child_failure_terminates_and_reaps_the_remaining_child(self):
        self.assert_children_reaped([[sys.executable, "-c", "raise SystemExit(7)"],
                                     [sys.executable, "-c", "import time; time.sleep(30)"]],
                                    process_batch.IsolationError)

    def test_worker_failure_preserves_the_previous_output(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "result.bin"
            out.write_bytes(b"previous result")
            with self.assertRaises(process_batch.IsolationError):
                process_batch.execute_batch("/usr/bin/false", [], "processes", 4, 8, out)
            self.assertEqual(out.read_bytes(), b"previous result")
            self.assertFalse(out.with_name(out.name + ".partial").exists())

    def test_invalid_workers_rejected_even_for_empty_input(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(process_batch.IsolationError):
                process_batch.execute_batch("unused", [], "processes", 0, 0, Path(directory) / "result.bin")


if __name__ == "__main__":
    unittest.main()
