#!/usr/bin/env python3
"""Concrete CASE/atmosphere batch execution, using only the Python standard library.

The parent never initializes SPICE/native libraries; Popen launches independent
mt_isolation executables with explicit inputs. Contiguous input ranges merge in
global order, and failure prevents publication and reaps all started children.
End-to-end timing includes exec, kernel/weather loading, propagation, binary
output and atomic merge. Orchestration stays outside leo::integration.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

MAGIC = b"MTISOB01"
STATE_DOUBLES = 13
# magic, epoch, global_total, global_begin, count, samples, state_doubles
HEADER = struct.Struct("<8sdQQQQQ")
HEADER_BYTES = HEADER.size


class IsolationError(RuntimeError):
    """A worker failed, produced an unusable file, or was misconfigured."""


def split_ranges(total: int, workers: int) -> list[tuple[int, int]]:
    """Balanced contiguous partition of ``total`` items over ``workers`` parts.

    Contiguous so that each part can be replayed independently and merged by
    concatenation; balanced so no worker is left with a trivially small tail that
    would dominate a short run's wall time.
    """
    if total < 0 or workers < 1:
        raise IsolationError("total must be nonnegative and workers must be positive")
    count = min(workers, total)
    if count == 0:
        return []
    base, extra = divmod(total, count)
    ranges = []
    begin = 0
    for index in range(count):
        length = base + (1 if index < extra else 0)
        ranges.append((begin, length))
        begin += length
    return ranges


def worker_command(binary, common, executor, workers, total, begin, count, out):
    command = [str(binary)]
    command += [str(item) for item in common]
    command += [
        "--isolation-executor", executor,
        "--isolation-workers", str(workers),
        "--isolation-total", str(total),
        "--isolation-begin", str(begin),
        "--isolation-count", str(count),
        "--isolation-out", str(out),
    ]
    return command


def read_header(path):
    with open(path, "rb") as handle:
        raw = handle.read(HEADER_BYTES)
    if len(raw) != HEADER_BYTES:
        raise IsolationError(f"{path}: truncated header ({len(raw)} of {HEADER_BYTES} bytes)")
    magic, epoch, total, begin, count, samples, state_doubles = HEADER.unpack(raw)
    if magic != MAGIC:
        raise IsolationError(f"{path}: bad magic {magic!r}")
    if state_doubles != STATE_DOUBLES:
        raise IsolationError(f"{path}: unexpected state width {state_doubles}")
    return {
        "epoch": epoch,
        "total": total,
        "begin": begin,
        "count": count,
        "samples": samples,
        "state_doubles": state_doubles,
    }


def payload_offset(samples):
    """Byte offset of the first state double: after the header and the sample labels."""
    return HEADER_BYTES + 8 * samples


def merge_parts(parts, destination, total, samples):
    """Concatenate part payloads in ascending global order into one result file.

    Every part must agree on epoch, schedule and width, and together they must
    cover exactly the requested global range with no gap and no overlap. The merged
    file describes the whole global range, so it is byte-comparable with the file a
    single sequential execution produces. It is written beside the destination and
    published by rename, so a failed merge never replaces a previous good result
    with a partial one.
    """
    if samples < 1:
        raise IsolationError(f"sample schedule is empty ({samples} samples)")
    destination = Path(destination)
    staging = destination.with_name(destination.name + ".partial")
    staging.parent.mkdir(parents=True, exist_ok=True)
    expected = parts[0][0]
    epoch = None
    labels = None
    written = 0
    try:
        with open(staging, "wb") as out:
            for begin, path in parts:
                header = read_header(path)
                if header["total"] != total:
                    raise IsolationError(f"{path}: global total {header['total']} != {total}")
                if header["samples"] != samples:
                    raise IsolationError(f"{path}: sample count {header['samples']} != {samples}")
                if header["begin"] != begin:
                    raise IsolationError(f"{path}: reports begin {header['begin']}, expected {begin}")
                if begin != expected:
                    raise IsolationError(f"{path}: gap or overlap at global index {begin}, expected {expected}")
                if epoch is None:
                    epoch = header["epoch"]
                elif header["epoch"] != epoch:
                    raise IsolationError(f"{path}: epoch differs between workers")
                with open(path, "rb") as handle:
                    handle.seek(HEADER_BYTES)  # labels, then the state payload
                    block = handle.read()
                part_labels = block[: 8 * samples]
                payload = block[8 * samples:]
                expected_payload = 8 * STATE_DOUBLES * samples * header["count"]
                if len(payload) != expected_payload:
                    raise IsolationError(f"{path}: payload is {len(payload)} bytes, expected {expected_payload}")
                if labels is None:
                    labels = part_labels
                    out.write(HEADER.pack(MAGIC, epoch, total, 0, total, samples, STATE_DOUBLES))
                    out.write(labels)
                elif part_labels != labels:
                    raise IsolationError(f"{path}: sample schedule differs between workers")
                out.write(payload)
                expected += header["count"]
                written += header["count"]
            out.flush()
            os.fsync(out.fileno())
    except BaseException:
        staging.unlink(missing_ok=True)
        raise
    if expected != total or written != total:
        staging.unlink(missing_ok=True)
        raise IsolationError(f"merged parts cover {written} of {total} global trajectories, ending at index {expected}")
    os.replace(staging, destination)


def run_workers(commands, scratch):
    """Launch every command, reap every child, never leave one running.

    ``Popen`` is used rather than ``run`` so all workers overlap. On any failure
    the remaining children are terminated, then killed if they do not exit, and
    always reaped, so a failed run leaks neither processes nor scratch files.
    """
    processes = []
    logs = {}
    try:
        for index, command in enumerate(commands):
            log_path = Path(scratch) / f"worker-{index}.log"
            handle = open(log_path, "wb")
            logs[index] = (handle, log_path)
            processes.append(subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT))
        for index, process in enumerate(processes):
            code = process.wait()
            if code != 0:
                handle, log_path = logs[index]
                handle.flush()
                detail = log_path.read_text(errors="replace").strip().splitlines()[-5:]
                raise IsolationError(f"worker {index} exited {code}: " + " | ".join(detail))
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        for handle, _ in logs.values():
            handle.close()


def execute_batch(binary, common, executor, workers, total, out, scratch=None, extra_meta=None):
    """Run one concrete batch and publish the merged result at ``out``.

    ``executor`` is ``sequential``, ``threads`` (one execution, many worker
    threads) or ``processes`` (one execution per contiguous subset). The returned
    dict carries the end-to-end seconds measured around launching and merging.
    """
    if total < 0 or workers < 1:
        raise IsolationError("total must be nonnegative and workers must be positive")
    if executor not in ("sequential", "threads", "processes"):
        raise IsolationError(f"unknown executor: {executor}")
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    owned_scratch = scratch is None
    if owned_scratch:
        scratch = tempfile.mkdtemp(prefix="leo-isolation-")
    scratch = Path(scratch)
    scratch.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    try:
        if executor == "processes" and total > 0:
            ranges = split_ranges(total, workers)
            commands = [worker_command(binary, common, "sequential", 1, total, begin, length,
                                       scratch / f"part-{index}.bin")
                        for index, (begin, length) in enumerate(ranges)]
            run_workers(commands, scratch)
            parts = []
            for (begin, length), command in zip(ranges, commands):
                path = Path(command[-1])
                header = read_header(path)
                if header["count"] != length:
                    raise IsolationError(f"{path}: produced {header['count']} trajectories, expected {length}")
                parts.append((begin, path))
            samples = read_header(parts[0][1])["samples"]
            merge_parts(parts, out, total, samples)
        else:
            part = scratch / "single.bin"
            mode = "sequential" if executor == "processes" else executor
            command = worker_command(binary, common, mode, workers, total, 0, total, part)
            if extra_meta is not None:
                command += ["--isolation-meta", str(extra_meta)]
            run_workers([command], scratch)
            if extra_meta is not None and not Path(extra_meta).exists():
                raise IsolationError(f"{extra_meta}: worker reported success but wrote no metadata sidecar")
            header = read_header(part)
            if header["count"] != total:
                raise IsolationError(f"{part}: produced {header['count']} trajectories, expected {total}")
            merge_parts([(0, part)], out, total, header["samples"])
    finally:
        if owned_scratch:
            shutil.rmtree(scratch, ignore_errors=True)
    elapsed = time.perf_counter() - started
    header = read_header(out)
    return {
        "executor": executor,
        "workers": workers,
        "total": total,
        "trajectories": header["count"],
        "samples": header["samples"],
        "epoch": header["epoch"],
        "seconds": elapsed,
        "out": str(out),
    }


def run_matrix(binary, common, executors, worker_counts, total, repetitions, warmups, scratch):
    """Time every (executor, workers) configuration and check the results agree.

    Equality is checked on the complete merged binary, not on a scalar summary, so
    a configuration that differs anywhere in any sample of any trajectory fails.
    The first measured configuration is the reference; it is the sequential one,
    because the executor list starts there.
    """
    records = []
    reference = None
    reference_label = None
    for executor in executors:
        for workers in ((1,) if executor == "sequential" else worker_counts):
            label = f"{executor}-w{workers}"
            for step in range(warmups + repetitions):
                out = Path(scratch) / f"{label}-{step}.bin"
                record = execute_batch(binary, common, executor, workers, total, out, scratch=scratch)
                record["step"] = step
                record["warmup"] = step < warmups
                record["label"] = label
                payload = out.read_bytes()
                if reference is None:
                    reference, reference_label = payload, label
                elif payload != reference:
                    raise IsolationError(f"{label} disagrees with {reference_label}: merged result files differ")
                out.unlink()
                if step >= warmups:
                    records.append(record)
    return records


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="process_batch.py",
        description="Run one concrete LEO batch through sequential, threads or independent processes.")
    parser.add_argument("--binary", required=True, type=Path, help="path to the built mt_isolation executable")
    parser.add_argument("--deps", type=Path, default=Path("/tmp/leo-atmosphere-deps"),
                        help="directory holding the kernels and weather files")
    parser.add_argument("--case", type=Path, required=True, help="experiment case file (exactly one)")
    parser.add_argument("--epoch-utc", default="2003-10-30T00:00:00", help="experiment epoch (default 2003-10-30T00:00:00)")
    parser.add_argument("--duration", type=float, default=21600.0, help="propagation duration, seconds (default 21600)")
    parser.add_argument("--cadence", type=float, default=30.0, help="output cadence, seconds (default 30)")
    parser.add_argument("--model", default="full_exponential",
                        help="workload name passed to --isolation-model (default full_exponential)")
    parser.add_argument("--total", type=int, required=True, help="global trajectory count, identical in every worker")
    parser.add_argument("--seed", type=int, default=20260101, help="master perturbation seed (default 20260101)")
    parser.add_argument("--executor", choices=("sequential", "threads", "processes"), default="processes",
                        help="how to execute the batch (default processes)")
    parser.add_argument("--workers", type=int, default=1, help="workers: threads for the threads executor, "
                                                               "processes for the processes executor (default 1)")
    parser.add_argument("--out", type=Path, help="merged result file; default stdout summary only")
    parser.add_argument("--meta", type=Path, help="optional JSON envelope sidecar for the threads/sequential executor")
    parser.add_argument("--benchmark", action="store_true",
                        help="time the full sequential/threads/processes x 1,4,8 matrix with equality checks")
    parser.add_argument("--repetitions", type=int, default=3, help="measured repetitions per configuration (default 3)")
    parser.add_argument("--warmups", type=int, default=1, help="warm-up runs per configuration (default 1)")
    parser.add_argument("--scratch", type=Path, help="directory for worker logs and intermediate part files")
    parser.add_argument("--report", type=Path, help="write the JSON report here as well as to stdout")
    parser.add_argument("rest", nargs=argparse.REMAINDER,
                        help="-- followed by extra arguments forwarded verbatim to mt_isolation")
    args = parser.parse_args(argv)

    if args.total < 0:
        parser.error("--total must be nonnegative")
    if args.workers < 1:
        parser.error("--workers must be at least one")
    if args.repetitions < 1:
        parser.error("--repetitions must be at least one")
    if args.warmups < 0:
        parser.error("--warmups cannot be negative")
    if not args.binary.exists():
        parser.error(f"--binary does not exist: {args.binary}")

    deps = args.deps
    common = ["--lsk", str(deps / "naif0012.tls"), "--spk", str(deps / "de432s.bsp"),
              "--pck", str(deps / "earth_000101_270105_261009.bpc"), "--frame", "ITRF93",
              "--weather", str(deps / "SpaceWeather-All-v1.2_snapshot_20200224.txt"),
              "--solfsmy", str(deps / "SOLFSMY.TXT"), "--dtcfile", str(deps / "DTCFILE.TXT"),
              "--epoch-utc", args.epoch_utc, "--case", str(args.case),
              "--duration", repr(args.duration), "--cadence", repr(args.cadence),
              "--isolation-model", args.model, "--isolation-seed", str(args.seed)]
    rest = [item for item in args.rest if item != "--"]
    common += rest

    scratch = args.scratch
    owns_scratch = scratch is None
    scratch = Path(tempfile.mkdtemp(prefix="leo-isolation-")) if owns_scratch else Path(scratch)
    scratch.mkdir(parents=True, exist_ok=True)
    try:
        if args.benchmark:
            records = run_matrix(args.binary, common, ("sequential", "threads", "processes"), (1, 4, 8),
                                 args.total, args.repetitions, args.warmups, scratch)
            report = {"kind": "MT-002 process isolation matrix", "model": args.model, "total": args.total,
                      "duration_seconds": args.duration, "repetitions": args.repetitions,
                      "warmups": args.warmups, "records": records,
                      "boundary": "seconds covers process launch, kernel and weather loading, propagation, "
                                  "output writing and merging; it is the end-to-end cost of the executor, not "
                                  "just the propagation envelope."}
            text = json.dumps(report, indent=2, sort_keys=True)
            if args.report:
                args.report.write_text(text + "\n")
            print(text)
            return 0
        if not args.out:
            parser.error("--out is required unless --benchmark is given")
        result = execute_batch(args.binary, common, args.executor, args.workers, args.total, args.out,
                               scratch=scratch, extra_meta=args.meta)
        text = json.dumps(result, indent=2, sort_keys=True)
        if args.report:
            args.report.write_text(text + "\n")
        print(text)
        return 0
    except (IsolationError, OSError) as error:
        print(f"process_batch: {error}", file=sys.stderr)
        return 1
    finally:
        if owns_scratch:
            shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())