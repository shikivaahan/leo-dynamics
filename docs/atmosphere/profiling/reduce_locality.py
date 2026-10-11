#!/usr/bin/env python3
"""Reduce ATM-PROF-001 workload evidence into locality statistics and timings.

Input is exactly what docs/atmosphere/profiling/workloads.cpp wrote under
--profile-out. Two artefacts are produced there:

  locality-statistics.json  deterministic reuse geometry: exact repeated-epoch
                            and repeated-request fractions, consecutive signed
                            epoch gaps, position-movement distributions,
                            weather-record key changes (day / three-hour /
                            hour), Sun repeated epochs, unique day-pair and lag
                            keys, and single-trajectory versus ensemble metrics.
  workload-timings.json     min/median/mean/p95/p99/max plus batch summaries for
                            every measured series, and the original-versus-
                            instrumented equality evidence.

Nothing here simulates, approximates or models reuse. Every count is exact and
computed from the captured inputs: epochs are keyed by their raw IEEE-754 bit
pattern, not by a rounded or bucketed value, and UTC fields come from the real
utc_calendar captured by the driver, never from ephemeris seconds divided by
86400 (which is wrong across UTC midnight and on leap-second days).

Percentiles use the same estimator as the C++ driver: linear interpolation
between closest ranks at q*(n-1) of the exactly sorted sample.
"""

from __future__ import annotations

import argparse
import gzip
import json
import math
import os
import struct
import sys
from pathlib import Path
from collections import Counter

RAW_GZIP_THRESHOLD = 4 * 1024 * 1024

PROPAGATION_RAW_FIELDS = [
    "epoch_et", "x_m", "y_m", "z_m", "rhs_ns", "provider_ns", "external_sun_ns",
    "utc_year", "utc_month", "utc_day", "day_of_year", "seconds_of_day",
    "day_length_seconds", "days_since_1970", "mjd_utc",
]

LOCALITY_RAW_FIELDS = [
    "epoch_et", "x_m", "y_m", "z_m",
    "utc_year", "utc_month", "utc_day", "day_of_year",
    "seconds_of_day", "day_length_seconds", "days_since_1970", "mjd_utc",
    "density_kg_m3", "temperature_k", "exospheric_temperature_k",
    "has_temperatures", "has_number_density", "trajectory_id",
]

ENSEMBLE_MODES = {"ensemble_trajectory_major", "ensemble_epoch_major"}
SINGLE_MODES = {
    "fixed", "same_epoch_changing_positions", "same_position_changing_epochs",
    "nearby_positions_epochs", "identical_daily_records_changing_geometry",
    "weather_3h_boundary", "weather_hour_boundary", "utc_midnight",
}


# ---------------------------------------------------------------------------
# Statistics
# ---------------------------------------------------------------------------
def quantile_sorted(ordered: list[float], q: float) -> float:
    if not ordered:
        return 0.0
    if len(ordered) == 1:
        return ordered[0]
    position = q * (len(ordered) - 1)
    lower = math.floor(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def summarize(values: list[float], units: str = "nanoseconds") -> dict:
    """Summary plus the raw samples: nothing measured is discarded."""
    finite = [v for v in values if math.isfinite(v)]
    if not finite:
        return {"count": 0, "minimum": 0, "median": 0, "mean": 0, "p95": 0,
                "p99": 0, "maximum": 0, "units": units, "raw": []}
    ordered = sorted(finite)
    return {
        "count": len(ordered),
        "minimum": ordered[0],
        "median": quantile_sorted(ordered, 0.5),
        "mean": math.fsum(ordered) / len(ordered),
        "p95": quantile_sorted(ordered, 0.95),
        "p99": quantile_sorted(ordered, 0.99),
        "maximum": ordered[-1],
        "units": units,
        "estimator": "exact order statistics; p95/p99 by linear interpolation "
                     "between closest ranks at q*(n-1)",
        "raw": values,
    }


def histogram(values: list[float], units: str) -> dict:
    """Full distribution of a gap series: every distinct value with its count."""
    counts: dict[float, int] = {}
    for value in values:
        if math.isfinite(value):
            counts[value] = counts.get(value, 0) + 1
    return {
        "units": units,
        "distinct_values": len(counts),
        "total": sum(counts.values()),
        "values": [{"value": value, "count": counts[value]}
                   for value in sorted(counts)],
    }


def bits(value: float) -> bytes:
    """Bitwise key. -0.0 and +0.0 are distinct requests, as they are in C++."""
    return struct.pack("<d", value)


# ---------------------------------------------------------------------------
# Raw input reading
# ---------------------------------------------------------------------------
def raw_files(directory: Path, pattern: str) -> list[Path]:
    """Raw inputs, whether or not a previous run already compressed them."""
    if not directory.exists():
        return []
    return sorted(directory.glob(pattern)) + sorted(directory.glob(pattern + ".gz"))

def read_json(path: Path):
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt") as handle:
        return json.load(handle)


def read_jsonl(path: Path) -> list[list[float]]:
    opener = gzip.open if path.suffix == ".gz" else open
    rows = []
    with opener(path, "rt") as handle:
        for line_number, line in enumerate(handle, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                rows.append([float(token) for token in json.loads(line)])
            except (ValueError, TypeError) as error:
                raise SystemExit(f"{path}:{line_number}: malformed row: {error}")
    return rows


def stem_of(path: Path) -> str:
    """Filename without any raw-input extension, plain or compressed."""
    name = path.name
    for suffix in (".jsonl.gz", ".jsonl"):
        if name.endswith(suffix):
            return name[: -len(suffix)]
    return path.stem


def rows_to_dicts(rows: list[list[float]], fields: list[str], path: Path) -> list[dict]:
    for index, row in enumerate(rows, start=1):
        if len(row) != len(fields):
            raise SystemExit(
                f"{path}: row {index} has {len(row)} fields, expected {len(fields)}")
    return [dict(zip(fields, row)) for row in rows]


# ---------------------------------------------------------------------------
# Locality statistics for one captured request sequence
# ---------------------------------------------------------------------------
def request_keys(rows: list[dict]) -> list[tuple[bytes, bytes, bytes, bytes]]:
    """Exact (epoch, x, y, z) request keys, bit for bit."""
    return [(bits(r["epoch_et"]), bits(r["x_m"]), bits(r["y_m"]), bits(r["z_m"])) for r in rows]


def locality_statistics(rows: list[dict], label: str) -> dict:
    count = len(rows)
    if count == 0:
        raise SystemExit(f"{label}: no captured requests")

    epoch_keys = [bits(r["epoch_et"]) for r in rows]
    keys = request_keys(rows)

    distinct_epochs = len(set(epoch_keys))
    distinct_requests = len(set(keys))
    repeated_epoch_calls = count - distinct_epochs
    repeated_request_calls = count - distinct_requests

    # Consecutive signed epoch gaps. Not every RHS call is a new epoch; a
    # zero gap means the integrator asked again at exactly the same ephemeris
    # second, which is what an epoch-keyed reuse structure would collapse.
    gaps = [rows[i]["epoch_et"] - rows[i - 1]["epoch_et"] for i in range(1, count)]
    zero_gaps = sum(1 for g in gaps if g == 0.0)
    repeated_adjacent_epoch_pairs = zero_gaps

    # Consecutive position movement, full distribution.
    movements = []
    for i in range(1, count):
        dx = rows[i]["x_m"] - rows[i - 1]["x_m"]
        dy = rows[i]["y_m"] - rows[i - 1]["y_m"]
        dz = rows[i]["z_m"] - rows[i - 1]["z_m"]
        movements.append(math.sqrt(dx * dx + dy * dy + dz * dz))
    distinct_positions = len({(bits(r["x_m"]), bits(r["y_m"]), bits(r["z_m"])) for r in rows})

    # Weather-record keys, derived from the captured real UTC calendar fields:
    # day index, three-hour DTC slot and hour slot. seconds_of_day is used as
    # captured; day_length_seconds is retained so a leap-second day is visible
    # rather than silently folded into 86400 s.
    day_keys = [(int(r["days_since_1970"])) for r in rows]
    three_hour_keys = [(int(r["days_since_1970"]),
                        min(7, int(math.floor(r["seconds_of_day"] / 10800.0)))) for r in rows]
    hour_keys = [(int(r["days_since_1970"]), min(23, int(math.floor(r["seconds_of_day"] / 3600.0))))
                 for r in rows]
    date_keys = [(int(r["utc_year"]), int(r["utc_month"]), int(r["utc_day"])) for r in rows]

    def changes(keys: list) -> int:
        return sum(1 for i in range(1, len(keys)) if keys[i] != keys[i - 1])

    day_changes = changes(day_keys)
    three_hour_changes = changes(three_hour_keys)
    hour_changes = changes(hour_keys)
    date_changes = changes(date_keys)

    distinct_days = len(set(day_keys))
    distinct_three_hour = len(set(three_hour_keys))
    distinct_hours = len(set(hour_keys))
    calls_per_day = count / distinct_days if distinct_days else float(count)
    calls_per_epoch = count / distinct_epochs if distinct_epochs else float(count)

    # Provider NRL does not acquire Sun; JB does. Real RHS adds one external query.
    provider_sun_queries = count if "_jb2008" in label else 0
    external_sun_queries = count if label.startswith("CASE-") else 0
    sun_queries = provider_sun_queries + external_sun_queries
    sun_distinct_epochs = distinct_epochs if sun_queries else 0
    sun_repeated_calls = sun_queries - sun_distinct_epochs
    sun_reuse_fraction = sun_repeated_calls / sun_queries if sun_queries else 0.0

    # Unique day pairs and lags between consecutive requests that fall on
    # different UTC days: how far apart, in days, the sequence jumps.
    day_pairs: dict[tuple[int, int], int] = {}
    day_lags: dict[int, int] = {}
    for i in range(1, count):
        if day_keys[i] == day_keys[i - 1]:
            continue
        pair = (day_keys[i - 1], day_keys[i])
        day_pairs[pair] = day_pairs.get(pair, 0) + 1
        lag = day_keys[i] - day_keys[i - 1]
        day_lags[lag] = day_lags.get(lag, 0) + 1

    leap_second_days = sorted({int(r["days_since_1970"]) for r in rows
                               if r["day_length_seconds"] != 86400.0})

    return {
        "label": label,
        "requests": count,
        "distinct_epochs": distinct_epochs,
        "repeated_epoch_calls": repeated_epoch_calls,
        "repeated_epoch_fraction": repeated_epoch_calls / count if count else 0.0,
        "distinct_requests": distinct_requests,
        "repeated_request_calls": repeated_request_calls,
        "repeated_request_fraction": repeated_request_calls / count if count else 0.0,
        "calls_per_requested_epoch": [{"epoch_bits_le_hex": key.hex(), "calls": value}
                                      for key, value in sorted(Counter(epoch_keys).items())],
        "epoch_multiplicity_distribution": histogram(list(Counter(epoch_keys).values()), "calls"),
        "calls_per_utc_day": [{"days_since_1970": key, "calls": value}
                             for key, value in sorted(Counter(day_keys).items())],
        "distinct_positions": distinct_positions,
        "consecutive_pairs": count - 1,
        "consecutive_repeated_epoch_pairs": repeated_adjacent_epoch_pairs,
        "consecutive_repeated_epoch_pair_fraction":
            repeated_adjacent_epoch_pairs / (count - 1) if count > 1 else 0.0,
        "consecutive_repeated_request_pairs":
            sum(1 for i in range(1, count) if keys[i] == keys[i - 1]),
        "consecutive_epoch_gap": summarize(gaps, "seconds"),
        "consecutive_epoch_gap_histogram": histogram(gaps, "seconds"),
        "consecutive_position_movement": summarize(movements, "metres"),
        "consecutive_position_movement_histogram": histogram(movements, "metres"),
        "weather_day_keys": {
            "distinct": distinct_days,
            "changes": day_changes,
            "change_fraction": day_changes / (count - 1) if count > 1 else 0.0,
            "calls_per_day": calls_per_day,
        },
        "weather_three_hour_keys": {
            "distinct": distinct_three_hour,
            "changes": three_hour_changes,
            "change_fraction": three_hour_changes / (count - 1) if count > 1 else 0.0,
        },
        "weather_hour_keys": {
            "distinct": distinct_hours,
            "changes": hour_changes,
            "change_fraction": hour_changes / (count - 1) if count > 1 else 0.0,
        },
        "utc_date_changes": date_changes,
        "calls_per_distinct_epoch": calls_per_epoch,
        "sun_reacquisition": {
            "note": "exact query epochs including the external RHS Sun call and JB provider call; NRL provider itself performs none",
            "queries": sun_queries,
            "provider_queries": provider_sun_queries,
            "external_rhs_queries": external_sun_queries,
            "distinct_epochs": sun_distinct_epochs,
            "repeated_calls": sun_repeated_calls,
            "repeated_fraction": sun_reuse_fraction,
        },
        "unique_day_pairs": {
            "distinct": len(day_pairs),
            "counts": [{"from_day": pair[0], "to_day": pair[1], "count": n}
                       for pair, n in sorted(day_pairs.items())],
        },
        "unique_day_lags": {
            "distinct": len(day_lags),
            "counts": [{"lag_days": lag, "count": n} for lag, n in sorted(day_lags.items())],
        },
        "leap_second_days_observed": leap_second_days,
        "utc_metadata_source": "real utc_calendar captured by the driver after each "
                               "timed region; never derived from ephemeris seconds",
    }


def split_trajectories(rows: list[dict]) -> list[list[dict]]:
    """Use the explicitly captured trajectory identity, never proximity clustering."""
    groups: dict[int, list[dict]] = {}
    for row in rows:
        identity = int(row["trajectory_id"])
        if identity < 0:
            raise SystemExit("ensemble request missing trajectory identity")
        groups.setdefault(identity, []).append(row)
    return [groups[key] for key in sorted(groups)]


def group_metrics(statistics: dict) -> dict:
    metrics = {
        "label": statistics["label"],
        "requests": statistics["requests"],
        "distinct_epochs": statistics["distinct_epochs"],
        "repeated_epoch_fraction": statistics["repeated_epoch_fraction"],
        "distinct_requests": statistics["distinct_requests"],
        "repeated_request_fraction": statistics["repeated_request_fraction"],
        "distinct_positions": statistics["distinct_positions"],
        "consecutive_repeated_epoch_pair_fraction":
            statistics["consecutive_repeated_epoch_pair_fraction"],
        "consecutive_epoch_gap_median": statistics["consecutive_epoch_gap"]["median"],
        "consecutive_position_movement_median":
            statistics["consecutive_position_movement"]["median"],
        "weather_day_change_fraction": statistics["weather_day_keys"]["change_fraction"],
        "weather_three_hour_change_fraction":
            statistics["weather_three_hour_keys"]["change_fraction"],
        "weather_hour_change_fraction": statistics["weather_hour_keys"]["change_fraction"],
        "calls_per_day": statistics["weather_day_keys"]["calls_per_day"],
        "calls_per_distinct_epoch": statistics["calls_per_distinct_epoch"],
        "sun_repeated_fraction": statistics["sun_reacquisition"]["repeated_fraction"],
        "unique_day_pairs": statistics["unique_day_pairs"]["distinct"],
        "unique_day_lags": statistics["unique_day_lags"]["distinct"],
    }
    if statistics["consecutive_epoch_gap"]["count"]:
        metrics["consecutive_epoch_gap_histogram"] = \
            statistics["consecutive_epoch_gap_histogram"]
    return metrics


# ---------------------------------------------------------------------------
# Reduction
# ---------------------------------------------------------------------------
def reduce_profile(root: Path) -> tuple[dict, dict]:
    manifest_path = root / "workloads-manifest.json"
    if not manifest_path.exists():
        raise SystemExit(f"{manifest_path} is missing; run the workload executable first")
    manifest = json.loads(manifest_path.read_text())

    locality_stats: dict = {
        "kind": "profiling-locality-statistics",
        "source_manifest": manifest_path.name,
        "instrumented_build": manifest.get("instrumented_build"),
        "profile_batches": manifest.get("profile_batches"),
        "frame": manifest.get("frame"),
        "epoch_utc": manifest.get("epoch_utc"),
        "key_definition": "requests are keyed by the raw IEEE-754 bit patterns of "
                          "(epoch_et, x_m, y_m, z_m); no rounding or bucketing is applied",
        "no_simulation_note": "no reuse, caching or locality is simulated; every count is "
                              "computed offline from the captured requests only",
        "modes": {},
        "single_trajectory_modes": [],
        "ensemble_modes": [],
        "propagation_request_sequences": [],
    }
    timings: dict = {
        "kind": "profiling-workload-timings",
        "source_manifest": manifest_path.name,
        "instrumented_build": manifest.get("instrumented_build"),
        "units": "nanoseconds unless stated otherwise",
        "estimator": "exact order statistics; p95/p99 by linear interpolation "
                     "between closest ranks at q*(n-1); raw samples retained",
        "timers": "std::chrono::steady_clock wall timers; CPU from "
                  "CLOCK_PROCESS_CPUTIME_ID over the same region",
        "propagation_batches": [],
        "locality_batches": [],
        "diagnostic_stages": [],
        "scientific_verification": [],
        "repetition_checks": [],
    }

    # ---- propagation ----
    propagation_dir = root / "propagation"
    raw_dir = propagation_dir / "raw"
    for path in raw_files(raw_dir, "*_requests.jsonl"):
        rows = rows_to_dicts(read_jsonl(path), PROPAGATION_RAW_FIELDS, path)
        statistics = locality_statistics(rows, stem_of(path).replace("_requests", ""))
        statistics["capture_note"] = (
            "the RHS performs its own external Sun geometric_state call in addition to any Sun "
            "acquisition inside the provider; external_sun_ns times only the external call")
        statistics["provider_ns"] = summarize([r["provider_ns"] for r in rows])
        statistics["rhs_ns"] = summarize([r["rhs_ns"] for r in rows])
        statistics["external_sun_ns"] = summarize([r["external_sun_ns"] for r in rows])
        # The raw per-request samples stay in the .jsonl inputs; the timing
        # summaries here carry the statistics only, so nothing is lost.
        statistics["provider_ns"].pop("raw", None)
        statistics["rhs_ns"].pop("raw", None)
        statistics["external_sun_ns"].pop("raw", None)
        locality_stats["propagation_request_sequences"].append(statistics)
        timings["propagation_batches"].append({
            "label": statistics["label"],
            "source": "captured request sequence (rep0)",
            "provider_call": statistics["provider_ns"],
            "rhs": statistics["rhs_ns"],
            "external_sun_call": statistics["external_sun_ns"],
            "provider_share_of_rhs_mean":
                statistics["provider_ns"]["mean"] / statistics["rhs_ns"]["mean"]
                if statistics["rhs_ns"]["mean"] else 0.0,
            "external_sun_share_of_provider_mean":
                statistics["external_sun_ns"]["mean"] / statistics["provider_ns"]["mean"]
                if statistics["provider_ns"]["mean"] else 0.0,
        })

    for path in raw_files(propagation_dir, "*_rep[0-9]*.json"):
        batch = read_json(path)
        timings["propagation_batches"].append({
            "label": f"{batch['case_id']}_{batch['atmosphere_model']}_rep{batch['batch']}",
            "source": "instrumented batch summary",
            "rhs_calls": batch["rhs_calls"],
            "provider_calls": batch["provider_calls"],
            "wall_seconds": batch["wall_seconds"],
            "cpu_seconds": batch["cpu_seconds"],
            "rhs": {k: v for k, v in batch["rhs_nanoseconds"].items() if k != "raw"},
            "provider_call": {k: v for k, v in batch["provider_call_nanoseconds"].items()
                              if k != "raw"},
            "external_sun_call": {k: v for k, v in batch["external_sun_call_nanoseconds"].items()
                                  if k != "raw"},
            "trajectory_checksum": batch["trajectory_checksum"],
            "production_trajectory_checksum": batch["production_trajectory_checksum"],
            "checksum_matches_production":
                batch["trajectory_checksum"] == batch["production_trajectory_checksum"],
            "result_hash": batch["result_hash"],
        })
        if "diagnostic_stages" in batch:
            stages = batch["diagnostic_stages"]
            timings["diagnostic_stages"].append({
                "label": f"{batch['case_id']}_{batch['atmosphere_model']}_rep{batch['batch']}",
                "source": "propagation provider calls",
                "recorded_requests": stages["recorded_requests"],
                "dropped_over_capacity": stages["dropped_over_capacity"],
                "total_iterations": stages["total_iterations"],
                "note": stages["note"],
                "stage_names": stages["stage_names"],
                "stage_calls": stages["stage_calls"],
                "stage_mean_nanoseconds": stages["stage_mean_nanoseconds"],
            })

    for path in raw_files(propagation_dir, "*_scientific.json"):
        scientific = read_json(path)
        timings["scientific_verification"].append({
            "label": f"{scientific['case_id']}_{scientific['atmosphere_model']}",
            "trajectory_checksum": scientific["trajectory_checksum"],
            "profiled_trajectory_checksum_matches_production":
                scientific["profiled_trajectory_checksum_matches_production"],
            "output_sample_count": scientific["output_sample_count"],
            "rhs_calls": scientific["rhs_calls"],
            "density_min_kg_m3": scientific["density_min_kg_m3"],
            "density_max_kg_m3": scientific["density_max_kg_m3"],
            "altitude_min_m": scientific["altitude_min_m"],
            "altitude_max_m": scientific["altitude_max_m"],
            "max_quaternion_norm_error": scientific["max_quaternion_norm_error"],
            "energy_work_residual_j_kg": scientific["energy_work_residual_j_kg"],
            "final_position_m": scientific["final_position_m"],
            "final_velocity_m_s": scientific["final_velocity_m_s"],
            "no_capture_wall_seconds": scientific.get("no_capture_wall_seconds"),
            "capture_wall_seconds": scientific.get("capture_wall_seconds"),
            "capture_wall_overhead_seconds": scientific.get("capture_wall_overhead_seconds"),
            "capture_wall_overhead_fraction": scientific.get("capture_wall_overhead_fraction"),
        })

    # ---- locality ----
    locality_dir = root / "locality"
    locality_raw_dir = locality_dir / "raw"
    for path in raw_files(locality_raw_dir, "*.jsonl"):
        rows = rows_to_dicts(read_jsonl(path), LOCALITY_RAW_FIELDS, path)
        stem = stem_of(path)
        # Mode names contain underscores, so the model suffix is stripped from
        # the right rather than the mode being split at the first underscore.
        model = None
        for candidate in ("_nrlmsise00", "_jb2008"):
            if stem.endswith(candidate):
                model = candidate[1:]
                mode = stem[: -len(candidate)]
        if model is None:
            raise SystemExit(f"{path}: filename does not end in a known model suffix")
        statistics = locality_statistics(rows, stem)
        statistics["mode"] = mode
        statistics["atmosphere_model"] = model
        statistics["density"] = summarize([r["density_kg_m3"] for r in rows], "kg/m^3")
        statistics["temperature"] = summarize([r["temperature_k"] for r in rows], "K")
        statistics["exospheric_temperature"] = summarize(
            [r["exospheric_temperature_k"] for r in rows], "K")
        statistics["density"].pop("raw", None)
        statistics["temperature"].pop("raw", None)
        statistics["exospheric_temperature"].pop("raw", None)
        if mode in ENSEMBLE_MODES:
            segments = split_trajectories(rows)
            statistics["trajectory_segments"] = len(segments)
            statistics["trajectory_segment_sizes"] = [len(s) for s in segments]
            statistics["per_trajectory"] = [
                locality_statistics(segment, f"{stem}#segment{index}")
                for index, segment in enumerate(segments)
            ]
            locality_stats["ensemble_modes"].append(group_metrics(statistics))
        else:
            locality_stats["single_trajectory_modes"].append(group_metrics(statistics))
        locality_stats["modes"][stem] = statistics

    for path in raw_files(locality_dir, "*_rep[0-9]*.json"):
        batch = read_json(path)
        call = {k: v for k, v in batch["call_nanoseconds"].items() if k != "raw"}
        timings["locality_batches"].append({
            "label": f"{batch['mode']}_{batch['atmosphere_model']}_rep{batch['batch']}",
            "mode": batch["mode"],
            "atmosphere_model": batch["atmosphere_model"],
            "requests": batch["requests"],
            "wall_seconds": batch["wall_seconds"],
            "cpu_seconds": batch["cpu_seconds"],
            "per_call": call,
            "batch_mean_ns": call["mean"],
            "mean_over_batch_ns": batch["wall_seconds"] / batch["requests"] * 1e9,
            "result_hash": batch["result_hash"],
        })
        if "diagnostic_stages" in batch:
            stages = batch["diagnostic_stages"]
            timings["diagnostic_stages"].append({
                "label": f"{batch['mode']}_{batch['atmosphere_model']}_rep{batch['batch']}",
                "source": "locality provider calls",
                "recorded_requests": stages["recorded_requests"],
                "dropped_over_capacity": stages["dropped_over_capacity"],
                "total_iterations": stages["total_iterations"],
                "note": stages["note"],
                "stage_names": stages["stage_names"],
                "stage_calls": stages["stage_calls"],
                "stage_mean_nanoseconds": stages["stage_mean_nanoseconds"],
                "stage_per_call_nanoseconds": stages.get("stage_per_call_nanoseconds", []),
            })

    for path in raw_files(locality_dir, "*_repetitions.json"):
        checks = read_json(path)
        timings["repetition_checks"].append({
            "label": path.stem.replace("_repetitions", ""),
            "repetitions": checks["repetitions"],
            "bitwise_identical": checks["repetitions_bitwise_identical"],
            "checksums": checks.get("trajectory_checksums") or checks.get("result_hashes"),
        })

    # ---- cross-series reuse summary ----
    single_reuse = [m["repeated_request_fraction"] for m in locality_stats["single_trajectory_modes"]]
    ensemble_reuse = [m["repeated_request_fraction"] for m in locality_stats["ensemble_modes"]]
    locality_stats["reuse_summary"] = {
        "note": "single-trajectory and ensemble schedules reuse different amounts by "
                "construction; the numbers are descriptions of the schedules, not "
                "predictions of a cached implementation",
        "single_trajectory_repeated_request_fraction": {
            "modes": len(single_reuse),
            "minimum": min(single_reuse) if single_reuse else None,
            "maximum": max(single_reuse) if single_reuse else None,
            "mean": math.fsum(single_reuse) / len(single_reuse) if single_reuse else None,
        },
        "ensemble_repeated_request_fraction": {
            "modes": len(ensemble_reuse),
            "minimum": min(ensemble_reuse) if ensemble_reuse else None,
            "maximum": max(ensemble_reuse) if ensemble_reuse else None,
            "mean": math.fsum(ensemble_reuse) / len(ensemble_reuse) if ensemble_reuse else None,
        },
    }
    return locality_stats, timings


def compress_large_raw(root: Path) -> list[dict]:
    """Gzip raw inputs above the threshold; small files stay readable as-is."""
    compressed = []
    for path in sorted(root.rglob("*.jsonl")):
        size = path.stat().st_size
        if size < RAW_GZIP_THRESHOLD:
            continue
        target = path.with_suffix(path.suffix + ".gz")
        with open(path, "rb") as source, gzip.open(target, "wb") as sink:
            while True:
                chunk = source.read(1 << 20)
                if not chunk:
                    break
                sink.write(chunk)
        path.unlink()
        compressed.append({"path": str(target.relative_to(root)), "bytes": size})
    return compressed


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile-out", required=True,
                        help="directory written by the profile workloads executable")
    parser.add_argument("--no-gzip", action="store_true",
                        help="keep raw .jsonl files uncompressed regardless of size")
    arguments = parser.parse_args(argv)

    root = Path(arguments.profile_out)
    if not root.is_dir():
        raise SystemExit(f"{root} is not a directory")

    locality_stats, timings = reduce_profile(root)

    compressed = [] if arguments.no_gzip else compress_large_raw(root)
    locality_stats["compressed_raw_inputs"] = compressed
    locality_stats["raw_compression_threshold_bytes"] = RAW_GZIP_THRESHOLD

    statistics_path = root / "locality-statistics.json"
    timings_path = root / "workload-timings.json"
    statistics_path.write_text(json.dumps(locality_stats, indent=1, sort_keys=True) + "\n")
    timings_path.write_text(json.dumps(timings, indent=1, sort_keys=True) + "\n")

    print(f"wrote {statistics_path}")
    print(f"wrote {timings_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))