#!/usr/bin/env python3
"""Floating-point arithmetic, Sun-ephemeris sensitivity and event localisation.

Three measured questions, no assumed answer:

1. What arithmetic is actually in use? The binary reports its own mantissa
   widths and the precision of its force/ephemeris path. ``mixed_rk4`` keeps the
   13-component state buffer in ``long double`` while the right-hand side is
   evaluated in ``double`` and every ephemeris query is a double-precision
   CSPICE call. That is a MIXED-precision run: it widens state accumulation
   only. It is not an independent high-precision oracle, it is not a higher
   precision force model, and it is never used here as a truth value.
2. How large is the double-precision round-off floor, and how far does it sit
   from real physical responses? Sun X is perturbed by signed physical sizes
   that span the representability limit. Every run reports the perturbation it
   was asked for and the perturbation that actually reached the ephemeris
   after double rounding; a request that rounds to zero is reported as zero and
   is never used as a physical response. A response is quoted as a measured
   physical sensitivity only when it exceeds the empirical numerical envelope
   by at least 10x and the independent methods and the size ladder agree.
3. Where do adaptive step rejections actually occur? Only attempts with
   accepted=false count as rejections. Event windows are compared against the
   full attempt set, every bin carries its own denominators, and the atmosphere
   bin is an inferred lower bound on segment crossings, not a knot census.

Standard library plus the audit's own ``experiments/metrics.py``.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import math
import platform
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "experiments"))
from metrics import classify_resolution, compare_observables, envelope  # noqa: E402

#: Analysis horizons shared with the versioned sensitivity study.
HORIZONS = (60, 600, 1800, 3600, 7200, 10800, 21600)

#: Fixed-step grids for the double and mixed arithmetic ladders. Every member is
#: integrated to the same output grid, so comparisons are on identical times.
PRECISION_STEPS = (1.0, 0.5, 0.25)

#: Double-state independent methods that define the empirical numerical envelope.
#: Two fixed-step RK4 refinements and two independently controlled RKF78 runs.
ENVELOPE_METHODS = {
    "rk4-0.5": {"method": "rk4", "max_step": 0.5},
    "rk4-0.25": {"method": "rk4", "max_step": 0.25},
    "rkf78-tight": {"method": "rkf78", "rtol": 1e-14, "atol_scale": 1e-4, "max_step": 0.5},
    "rkf78-tighter": {"method": "rkf78", "rtol": 1e-14, "atol_scale": 1e-5, "max_step": 0.25},
}

#: Methods that independently measure a Sun-ephemeris response. One refined
#: fixed-step RK4 on the production grid, one independently controlled adaptive
#: RKF78, and the production adaptive Dopri at tight settings.
RESPONSE_METHODS = ("rk4-0.25", "rkf78-tight", "dopri-tight")
EXTRA_METHODS = {"dopri-tight": {"method": "dopri", "rtol": 1e-13, "atol_scale": 1e-3,
                                  "max_step": 0.5}}

#: Requested Sun X offsets. The first size is a representability probe and the
#: next three are ULP counts. The Sun X component sits near 2.65e10 m for these
#: epochs, where one double ULP is about 3.81e-6 m, so a 1e-6 m request rounds
#: to zero and the ULP counts are the smallest nonzero offsets the X component
#: can carry. The metre ladder spans three decades.
SUN_SIZES = (
    ("representability-1e-6m", "perturb", 1e-6),
    ("ulp-1", "ulps", 1),
    ("ulp-100", "ulps", 100),
    ("ulp-100000", "ulps", 100000),
    ("1m", "perturb", 1.0),
    ("100m", "perturb", 100.0),
    ("1000m", "perturb", 1000.0),
)
SIGNS = (("+", 1.0), ("-", -1.0))

#: Windows around an event at which an attempt is counted as "near" it.
EVENT_WINDOWS_S = (2.0, 10.0, 30.0, 60.0)

#: Observables carried through the resolution classification, in SI units. Every
#: key is one ``compare_observables`` maximum-block entry, so each response is a
#: maximum absolute observable difference on the shared output grid.
STATE_KEYS = ("position_m", "velocity_m_s", "attitude_angle_rad", "omega_rad_s",
              "spin_axis_angle_rad")
ORBITAL_KEYS = ("semi_major_axis_m", "inclination_rad", "raan_rad",
                "angular_momentum_norm_kg_m2_s")
OBSERVABLE_KEYS = STATE_KEYS + ORBITAL_KEYS + ("pointing_axis_angle_rad", "srp_work_j")
RESPONSE_RATIO = 10.0
CONSISTENCY_TOLERANCE = 0.20


# --------------------------------------------------------------------------- #
# Harness invocation
# --------------------------------------------------------------------------- #



class Harness:
    """Execute direct audit measurements; retain raw records, never repair JSON."""

    def __init__(self, binary, spk, lsk, case, workdir, timeout):
        self.base = [str(binary), "--spk", str(spk), "--lsk", str(lsk),
                     "--scenario", "full", "--case-file", str(case)]
        self.workdir = Path(workdir)
        self.workdir.mkdir(parents=True, exist_ok=True)
        self.timeout = timeout
        self.commands = []

    def __call__(self, method_settings, horizon, extra=()):
        cmd = list(self.base) + ["--duration", str(horizon), "--cadence", "30"]
        for key, value in sorted(method_settings.items()):
            cmd += ["--" + key.replace("_", "-"), str(value)]
        cmd += [str(x) for x in extra]
        self.commands.append(cmd)
        done = subprocess.run(cmd, check=True, text=True, capture_output=True,
                              timeout=self.timeout)
        result = json.loads(done.stdout)
        result["command"] = cmd
        path = self.workdir / (hashlib.sha256("\x1f".join(cmd).encode()).hexdigest() + ".json")
        path.write_text(json.dumps({"command": cmd, "result": result},
                                   allow_nan=False, separators=(",", ":")))
        return result

    def trace(self, horizon):
        return self({"method": "trace"}, horizon)



# --------------------------------------------------------------------------- #
# Shared measurement helpers
# --------------------------------------------------------------------------- #

def maximum_block(base, other):
    comparison = compare_observables(base, other)
    if comparison["time_alignment"] != "identical":
        raise SystemExit("observable comparison requires identical output times")
    return comparison["maximum"], comparison


def response_or_none(values):
    finite = [v for v in values if v is not None and math.isfinite(v)]
    return max(finite) if finite else None


def relative_spread(values):
    finite = [v for v in values if v is not None and math.isfinite(v)]
    if len(finite) < 2:
        return None
    low, high = min(finite), max(finite)
    if high == 0.0:
        return 0.0 if low == 0.0 else None
    return (high - low) / abs(high)


def sha256_of(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def case_id_of(path):
    return next(line.split()[1] for line in Path(path).read_text().splitlines()
                if line.startswith("id "))


def arithmetic_facts(results):
    """Mantissa widths and force-path precision, read from the runs themselves."""
    facts = {}
    for method, result in results.items():
        facts[method] = {
            "double_mantissa_bits": result["double_mantissa_bits"],
            "long_double_mantissa_bits": result["long_double_mantissa_bits"],
            "state_precision": result["state_precision"],
            "force_ephemeris_precision": result["force_ephemeris_precision"],
        }
    double_bits = {f["double_mantissa_bits"] for f in facts.values()}
    long_bits = {f["long_double_mantissa_bits"] for f in facts.values()}
    mixed = next((m for m, f in facts.items() if f["state_precision"] != "double"), None)
    return {
        "per_method": facts,
        "double_mantissa_bits_observed": sorted(double_bits),
        "long_double_mantissa_bits_observed": sorted(long_bits),
        "mantissa_widths_consistent": len(double_bits) == 1 and len(long_bits) == 1,
        "mixed_state_method": mixed,
        "mixed_precision_classification": "MIXED_PRECISION",
        "mixed_precision_statement": (
            "mixed_rk4 accumulates the 13-component state in long double "
            f"({facts[mixed]['long_double_mantissa_bits'] if mixed else 'unmeasured'} "
            "mantissa bits) but evaluates the right-hand side in double and queries "
            "double-precision CSPICE for every ephemeris and environment value. "
            "Only state accumulation is widened. This is not an independent "
            "high-precision oracle, not a higher-precision force model, and not "
            "evidence about CSPICE accuracy. It bounds state round-off alone."),
        "force_ephemeris_precision_observed": sorted(
            {f["force_ephemeris_precision"] for f in facts.values()}),
    }


def convergence_ladder(runs, precision, steps, horizons):
    """Successive-step differences and the empirical order they imply.

    For three nested grids h > h/2 > h/4 the empirical order is
    ``log2(|J(h) - J(h/2)| / |J(h/2) - J(h/4)|)``. It is reported only where both
    differences are positive, because a zero difference means the refinement has
    reached the arithmetic floor rather than a zero error.
    """
    per_horizon = {}
    for horizon in horizons:
        entries, previous = [], None
        for coarse, fine in zip(steps, steps[1:]):
            block, _ = maximum_block(runs[(precision, coarse, horizon)],
                                     runs[(precision, fine, horizon)])
            differences = {key: block[key] for key in OBSERVABLE_KEYS if key in block}
            entry = {"coarse_step_s": coarse, "fine_step_s": fine,
                     "difference_coarse_minus_fine": differences}
            if previous is not None:
                order = {}
                for key, fine_finer in differences.items():
                    coarse_fine = previous.get(key)
                    ratio = ((coarse_fine / fine_finer)
                             if (coarse_fine is not None and fine_finer not in (None, 0.0))
                             else None)
                    order[key] = {
                        "abs_difference_coarse_fine": coarse_fine,
                        "abs_difference_fine_finer": fine_finer,
                        "ratio": ratio,
                        "order": (math.log2(ratio)
                                  if (ratio is not None and ratio > 0.0) else None),
                        "reason": None if (ratio is not None and ratio > 0.0) else (
                            "zero_difference_at_this_resolution" if fine_finer == 0.0
                            else "undefined_difference_block"),
                    }
                entry["observed_order"] = {
                    "definition": "log2(|J(coarse)-J(fine)| / |J(fine)-J(finer)|); "
                                  "reported only where both differences are positive, "
                                  "because a zero difference means the refinement has "
                                  "reached the arithmetic floor, not zero error",
                    "per_observable": order}
            entries.append(entry)
            previous = differences
        per_horizon[str(horizon)] = entries
    return per_horizon


def arithmetic_ladder(harness, horizons):
    """Double RK4 against mixed RK4 on identical grids, with empirical order."""
    steps = list(PRECISION_STEPS)
    runs = {}
    for horizon in horizons:
        for step in steps:
            runs[("double", step, horizon)] = harness({"method": "rk4", "max_step": step},
                                                      horizon)
            runs[("mixed", step, horizon)] = harness({"method": "mixed_rk4",
                                                      "max_step": step}, horizon)

    ladder = {}
    double_ladder = convergence_ladder(runs, "double", steps, horizons)
    mixed_ladder = convergence_ladder(runs, "mixed", steps, horizons)
    for horizon in horizons:
        precision_effect = {}
        for step in steps:
            block, _ = maximum_block(runs[("double", step, horizon)],
                                     runs[("mixed", step, horizon)])
            precision_effect[f"step-{step}"] = {key: block[key]
                                                for key in OBSERVABLE_KEYS if key in block}
        ladder[str(horizon)] = {
            "grid": "identical output times for every member",
            "steps_s": steps,
            "double_rk4": {"arithmetic": "DOUBLE_PRECISION",
                           "successive_step_differences": double_ladder[str(horizon)]},
            "mixed_rk4": {"arithmetic": "MIXED_PRECISION",
                          "arithmetic_note": (
                              "long double state buffer, double right-hand side, "
                              "double-precision CSPICE ephemeris; not a high-precision "
                              "oracle"),
                          "successive_step_differences": mixed_ladder[str(horizon)]},
            "double_vs_mixed_same_grid": precision_effect,
            "double_vs_mixed_interpretation": (
                "MIXED_PRECISION difference, not an accuracy error: the mixed run "
                "widens state accumulation only, while force and ephemeris values "
                "stay double in both runs. The gap measures how much of the "
                "double-state difference is state round-off; it is not a bound on "
                "the total numerical error."),
        }
    return ladder, runs


# --------------------------------------------------------------------------- #
# Section 2: Sun ephemeris sensitivity
# --------------------------------------------------------------------------- #

def method_settings(name):
    """CLI settings for one declared method name."""
    return {**ENVELOPE_METHODS.get(name, {}), **EXTRA_METHODS.get(name, {})}


def load_study(path):
    """Load a measured sensitivity study, plain or gzip-compressed."""
    path = Path(path)
    if path.suffix == ".gz":
        with gzip.open(path, "rt", encoding="utf-8") as handle:
            return json.load(handle)
    return json.loads(path.read_text())


def external_envelopes(paths, case_id):
    """Per-horizon numerical envelopes already measured by the sensitivity study.

    Each convergence row carries the same-unit envelope that study used to gate
    its own responses. Several study files may be supplied; the largest value
    per observable wins, so the gate this driver applies can only get stricter.
    """
    merged, sources = {}, {}
    for path in paths or ():
        study = load_study(path)
        case = study.get("cases", {}).get(case_id)
        if case is None:
            continue
        for row in case.get("convergence", []):
            horizon = int(row["horizon_s"])
            block = row.get("uncertainty") or {}
            target = merged.setdefault(horizon, {})
            used = sources.setdefault(horizon, set())
            for key in OBSERVABLE_KEYS:
                if key not in block:
                    continue
                used.add(str(path))
                if key not in target or block[key] > target[key]:
                    target[key] = block[key]
    return merged, {horizon: sorted(paths) for horizon, paths in sources.items()}


def merge_envelope(local, supplied, source):
    """Largest same-unit envelope per observable, with its provenance."""
    keys = set(local or {}) | set(supplied or {})
    out, origin = {}, {}
    for key in sorted(keys):
        a, b = (local or {}).get(key), (supplied or {}).get(key)
        if a is None:
            out[key], origin[key] = b, source
        elif b is None:
            out[key], origin[key] = a, "measured_here"
        else:
            out[key] = max(a, b)
            origin[key] = ("measured_here_and_supplied_agree" if a == b
                           else ("measured_here" if a > b else source))
    return out, origin


def envelope_block(harness, horizon, method_names, supplied=None, supplied_source=None):
    runs = {name: harness(method_settings(name), horizon) for name in method_names}
    blocks = []
    names = sorted(runs)
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            blocks.append(maximum_block(runs[a], runs[b])[0])
    combined = envelope(*blocks)
    local = {key: combined[key] for key in OBSERVABLE_KEYS if key in combined}
    merged, origin = merge_envelope(local, supplied, supplied_source)
    return (merged, origin,
            {name: {"method": runs[name]["method"], "rtol": runs[name]["rtol"],
                    "atol_scale": runs[name]["atol_scale"],
                    "max_step_s": runs[name]["max_step_s"],
                    "checksum": runs[name]["checksum"],
                    "wall_seconds": runs[name]["wall_seconds"],
                    "rhs_calls": runs[name]["rhs_calls"]} for name in names},
            runs)


def sun_offset_measurement(nominal, perturbed, base_x):
    """Measure the Sun X offset that actually reached the force model.

    ``audit.cpp`` reports only an unsigned maximum over its ephemeris calls, so
    the signed per-sample offset is measured here from the ``loads`` series of
    the perturbed and unperturbed runs, together with how many double ULPs of
    the unperturbed Sun X each sample's offset corresponds to.
    """
    shifted = [row["sun_position_m"][0] for row in perturbed["loads"]]
    deltas = [b - a for a, b in zip(base_x, shifted)]
    if not deltas:
        return {"signed_max_m": 0.0, "max_abs_m": 0.0, "max_ulps": 0.0,
                "all_samples_zero": True}
    signed_max = max(deltas, key=abs)
    ulps = [abs(d) / math.ulp(a) for d, a in zip(deltas, base_x) if math.ulp(a) > 0.0]
    return {"signed_max_m": signed_max,
            "max_abs_m": max(abs(d) for d in deltas),
            "max_ulps": max(ulps) if ulps else 0.0,
            "all_samples_zero": all(d == 0.0 for d in deltas)}


def sun_offset_probes(harness, horizon):
    """Requested versus realised Sun X offset for every declared size and sign."""
    nominal = harness({"method": "dopri"}, horizon)
    base_x = [row["sun_position_m"][0] for row in nominal["loads"]]
    probes = []
    for label, kind, size in SUN_SIZES:
        flag = "--perturb-x" if kind == "perturb" else "--ulp-x"
        for sign_name, sign in SIGNS:
            value = sign * size
            perturbed = harness({"method": "dopri"}, horizon, (flag, repr(value)))
            measured = sun_offset_measurement(nominal, perturbed, base_x)
            applied = perturbed["actual_sun_x_perturbation_m"]
            probes.append({
                "size_label": label,
                "sign": sign_name,
                "option": flag,
                "requested": value,
                "requested_unit": ("metres" if kind == "perturb" else "ulp_count"),
                "applied_reported_by_binary_m": applied,
                "applied_measured_signed_max_m": measured["signed_max_m"],
                "applied_measured_max_abs_m": measured["max_abs_m"],
                "applied_measured_max_ulps": measured["max_ulps"],
                "applied_is_nonzero": measured["max_abs_m"] > 0.0,
                "requested_rounded_to_zero": measured["all_samples_zero"],
                "sign_preserved": (measured["max_abs_m"] == 0.0
                                   or (measured["signed_max_m"] > 0.0) == (sign > 0.0)),
                "realisation_ratio_applied_over_requested": (
                    (measured["max_abs_m"] / abs(value))
                    if kind == "perturb" and abs(value) > 0.0 else None),
                "realisation_ratio_ulps_applied_over_requested": (
                    (measured["max_ulps"] / abs(value))
                    if kind == "ulps" and abs(value) > 0.0 else None),
                "ulp_of_sun_x_at_first_sample_m": math.ulp(base_x[0]) if base_x else None,
                "command": perturbed["command"],
            })
    return probes, nominal, base_x


def ephemeris_sensitivity(harness, horizons, numeric_envelopes, nominal_runs,
                          base_x_by_method):
    """Response of each observable to a signed Sun X offset, with resolution."""
    rows = []
    for horizon in horizons:
        nominal_by_method = nominal_runs[horizon]
        env = numeric_envelopes[horizon]["envelope"]
        size_group = {}
        for label, kind, size in SUN_SIZES:
            flag = "--perturb-x" if kind == "perturb" else "--ulp-x"
            for sign_name, sign in SIGNS:
                value = sign * size
                measured, responses, reported = [], {}, []
                for name in RESPONSE_METHODS:
                    perturbed = harness(method_settings(name), horizon, (flag, repr(value)))
                    measured.append(sun_offset_measurement(
                        nominal_by_method[name], perturbed, base_x_by_method[horizon][name]))
                    reported.append(perturbed["actual_sun_x_perturbation_m"])
                    block, _ = maximum_block(nominal_by_method[name], perturbed)
                    responses[name] = {key: block[key] for key in OBSERVABLE_KEYS
                                       if key in block}
                applied = max(m["max_abs_m"] for m in measured)
                size_group.setdefault(label, {})[sign_name] = {
                    "requested": value,
                    "applied_max_abs_m": applied,
                    "applied_measured_max_ulps": max(m["max_ulps"] for m in measured),
                    "applied_reported_by_binary_m": reported,
                    "applied_is_nonzero": applied > 0.0,
                    "applied_measured_by_method_m": {
                        name: measured[i]["signed_max_m"]
                        for i, name in enumerate(RESPONSE_METHODS)},
                    "responses": responses,
                }
        ladder_keys = ("1m", "100m", "1000m")
        per_observable = {}
        for key in OBSERVABLE_KEYS:
            ladder_spreads = []
            for method in RESPONSE_METHODS:
                slopes = []
                for label in ladder_keys:
                    for sign_name, _sign in SIGNS:
                        cell = size_group[label][sign_name]
                        response = cell["responses"][method].get(key)
                        if response is not None and cell["applied_max_abs_m"] > 0.0:
                            slopes.append(response / cell["applied_max_abs_m"])
                ladder_spreads.append(relative_spread(slopes))
            ladder_spread = response_or_none(
                [s for s in ladder_spreads if s is not None]) if any(
                    s is not None for s in ladder_spreads) else None
            entries = []
            for label, _kind, _size in SUN_SIZES:
                for sign_name, _sign in SIGNS:
                    cell = size_group[label][sign_name]
                    per_method = {name: cell["responses"][name].get(key)
                                  for name in RESPONSE_METHODS}
                    method_spread = relative_spread(list(per_method.values()))
                    largest = response_or_none(list(per_method.values()))
                    # Cross-method and size-ladder consistency must both hold.
                    spreads = [s for s in (method_spread, ladder_spread) if s is not None]
                    combined = max(spreads) if spreads else None
                    verdict = classify_resolution(largest, env.get(key), RESPONSE_RATIO,
                                                  combined, CONSISTENCY_TOLERANCE)
                    entries.append({
                        "size_label": label,
                        "sign": sign_name,
                        "requested": cell["requested"],
                        "applied_max_abs_m": cell["applied_max_abs_m"],
                        "applied_measured_max_ulps": cell["applied_measured_max_ulps"],
                        "applied_is_nonzero": cell["applied_is_nonzero"],
                        "usable_as_physical_perturbation": cell["applied_is_nonzero"],
                        "response_by_method": per_method,
                        "response_max": largest,
                        "numerical_envelope": env.get(key),
                        "cross_method_relative_spread": method_spread,
                        "size_ladder_relative_spread": ladder_spread,
                        "governing_consistency_spread": combined,
                        "resolved": verdict["resolved"] and cell["applied_is_nonzero"],
                        "resolution_reason": (verdict["reason"] if verdict["resolved"]
                                              else ("requested_offset_rounded_to_zero"
                                                    if not cell["applied_is_nonzero"]
                                                    else verdict["reason"])),
                        "response_over_envelope_ratio": verdict.get("ratio"),
                    })
            per_observable[key] = entries
        rows.append({
            "horizon_s": horizon,
            "methods": list(RESPONSE_METHODS),
            "response_definitions": {
                key: "maximum over the sampled horizon of the observable difference "
                     "between the perturbed run and its own unperturbed run of the "
                     "same method and grid" for key in OBSERVABLE_KEYS},
            "resolution_policy": (
                f"a response is quoted as measured physical sensitivity only when it "
                f"is at least {RESPONSE_RATIO:g}x the same-unit empirical numerical "
                f"envelope and the independent methods agree to within "
                f"{CONSISTENCY_TOLERANCE:.0%} relative spread; every other entry is "
                f"reported unresolved with its reason"),
            "numerical_envelope": env,
            "observables": per_observable,
        })
    return rows


# --------------------------------------------------------------------------- #
# Section 3: event localisation
# --------------------------------------------------------------------------- #

def event_times(accepted):
    times = [row[0] for row in accepted]
    illumination = [row[4] for row in accepted]
    solar = [row[8] for row in accepted]
    wind = [row[9] for row in accepted]
    umbra_entry, umbra_exit, penumbra_entry, penumbra_exit = [], [], [], []
    for i in range(1, len(times)):
        if (illumination[i] <= 0) and not (illumination[i - 1] <= 0):
            umbra_entry.append(times[i])
        elif (illumination[i - 1] <= 0) and not (illumination[i] <= 0):
            umbra_exit.append(times[i])
        was_partial = 0 < illumination[i - 1] < 1
        partial = 0 < illumination[i] < 1
        if partial and not was_partial:
            penumbra_entry.append(times[i])
        elif was_partial and not partial:
            penumbra_exit.append(times[i])
    umbra = umbra_entry + umbra_exit
    penumbra = penumbra_entry + penumbra_exit
    solar_switch = [times[i] for i in range(1, len(times)) if solar[i] != solar[i - 1]]
    wind_switch = [times[i] for i in range(1, len(times)) if wind[i] != wind[i - 1]]
    face_switch = sorted(set(solar_switch) | set(wind_switch))
    return {"eclipse_umbra": umbra, "eclipse_penumbra": penumbra,
            "umbra_entry": umbra_entry, "umbra_exit": umbra_exit,
            "penumbra_entry": penumbra_entry, "penumbra_exit": penumbra_exit,
            "solar_face_switch": solar_switch, "wind_face_switch": wind_switch,
            "any_face_switch": face_switch,
            "eclipse_any": sorted(set(umbra) | set(penumbra))}


def atmosphere_crossings(accepted, tolerance):
    """Infer segment crossings of the exponential atmosphere profile.

    Inside one knot interval log density is affine in altitude, so a relative
    slope change above the tolerance marks a crossing inside the interval. A
    crossing that lands exactly on a sampled state leaves both intervals clean
    and is therefore not counted: the result is a lower bound.
    """
    crossings, examined, skipped = [], 0, 0
    for i in range(2, len(accepted)):
        a2, a1, a0 = accepted[i - 2][6], accepted[i - 1][6], accepted[i][6]
        r2, r1, r0 = accepted[i - 2][7], accepted[i - 1][7], accepted[i][7]
        if r1 > 0.0 and r0 > 0.0 and a1 != a2 and a0 != a1:
            earlier = (math.log(r1) - math.log(r2)) / (a1 - a2)
            later = (math.log(r0) - math.log(r1)) / (a0 - a1)
            scale = max(abs(earlier), abs(later), 1e-30)
            examined += 1
            if abs(later - earlier) > tolerance * scale:
                crossings.append(accepted[i - 1][0])
        else:
            skipped += 1
    return {"times_s": crossings, "intervals_examined": examined,
            "intervals_skipped": skipped}


def bin_counts(attempts, marks, window):
    """Rejections inside and outside a window, each with its own denominators."""
    inside = [row for row in attempts
              if any(abs(row[0] - m) <= window for m in marks)]
    rejected_inside = sum(1 for row in inside if row[3] == 0)
    rejected_total = sum(1 for row in attempts if row[3] == 0)
    outside_attempts = len(attempts) - len(inside)
    outside_rejected = rejected_total - rejected_inside
    rate_in = (rejected_inside / len(inside)) if inside else None
    rate_out = (outside_rejected / outside_attempts) if outside_attempts else None
    if rate_in is None:
        verdict = "undefined_no_attempts_in_window"
    elif rate_out in (None, 0.0):
        verdict = "undefined_no_rejections_outside_window"
    else:
        verdict = "measured"
    return {
        "window_s": window,
        "event_count": len(marks),
        "attempts_in_window": len(inside),
        "rejections_in_window": rejected_inside,
        "rejection_rate_in_window": rate_in,
        "attempts_outside_window": outside_attempts,
        "rejections_outside_window": outside_rejected,
        "rejection_rate_outside_window": rate_out,
        "enrichment_ratio": (rate_in / rate_out) if (rate_in is not None and rate_out) else None,
        "denominator_note": ("attempts_in_window and attempts_outside_window partition "
                             "the full attempt set; rejections are attempts with "
                             "accepted=false only"),
        "verdict": verdict,
    }


def event_localisation(harness, horizon):
    trace = harness.trace(horizon)
    production = harness({"method": "dopri"}, horizon)
    identical = trace["checksum"] == production["checksum"]
    if not identical:
        raise SystemExit("trace output differs from the production Dopri run at "
                         f"identical settings for horizon {horizon}; refusing to "
                         "interpret step diagnostics that do not describe the "
                         "production integration")
    attempts = trace["steps"]
    accepted = [row for row in attempts if row[3] == 1]
    marks = event_times(accepted)
    cpp = trace["trace_events"]
    knots = atmosphere_crossings(accepted, cpp["knot_relative_slope_tolerance"])
    features = {
        "eclipse_umbra_boundary": marks["eclipse_umbra"],
        "eclipse_penumbra_boundary": marks["eclipse_penumbra"],
        "eclipse_any_boundary": marks["eclipse_any"],
        "solar_face_incidence_change": marks["solar_face_switch"],
        "wind_face_incidence_change": marks["wind_face_switch"],
        "any_face_incidence_change": marks["any_face_switch"],
        "atmosphere_segment_crossing_inferred": knots["times_s"],
    }
    bins = {}
    for name, times in features.items():
        bins[name] = {"event_count": len(times),
                      "event_times_s": [round(t, 6) for t in times[:64]],
                      "event_time_list_truncated_at": 64 if len(times) > 64 else None,
                      "windows": [bin_counts(attempts, times, w) for w in EVENT_WINDOWS_S]}
    rejected = [row for row in attempts if row[3] == 0]
    return {
        "horizon_s": horizon,
        "trace_matches_production_checksum": identical,
        "trace_is_production_integration": (
            "the trace mirrors the production Dopri loop; its checksum is compared "
            "against the production Dopri run at identical settings and the trace "
            "is refused if they differ. It is not an accuracy oracle."),
        "denominators": {
            "attempts_total": len(attempts),
            "accepted_steps_total": len(accepted),
            "rejected_attempts_total": len(rejected),
            "rejection_rate": (len(rejected) / len(attempts)) if attempts else None,
            "definition": "a rejected attempt is a trace row with accepted=false; "
                          "re-evaluations of the same state count separately because "
                          "each is a real controller decision",
        },
        "cpp_accepted_state_diagnostics": cpp,
        "cpp_counter_parity": {
            "rejected_steps": (cpp["rejected_steps"] == len(rejected)),
            "states": (cpp["states"] == len(accepted)),
            "atmosphere_segment_changes": (cpp["atmosphere_segment_changes"]
                                           == len(knots["times_s"])),
            "atmosphere_intervals_examined": (cpp["atmosphere_intervals_examined"]
                                              == knots["intervals_examined"]),
            "atmosphere_intervals_skipped": (cpp["atmosphere_intervals_skipped"]
                                             == knots["intervals_skipped"]),
            "solar_face_switches": (cpp["solar_face_switches"]
                                    == len(marks["solar_face_switch"])),
            "wind_face_switches": (cpp["wind_face_switches"]
                                   == len(marks["wind_face_switch"])),
            "umbra_entries": (cpp["umbra_entries"] == len(marks["umbra_entry"])),
            "umbra_exits": (cpp["umbra_exits"] == len(marks["umbra_exit"])),
            "penumbra_entries": (cpp["penumbra_entries"] == len(marks["penumbra_entry"])),
            "penumbra_exits": (cpp["penumbra_exits"] == len(marks["penumbra_exit"])),
        },
        "atmosphere_bin_caveat": (
            "the atmosphere bin counts inferred segment crossings of the accepted "
            "state sequence. A crossing that lands exactly on a sampled state is not "
            "detected, so these counts are a lower bound on crossings, not a knot "
            "census; the examined and skipped interval counters state the coverage."),
        "bins": bins,
        "commands": {"trace": trace["command"], "production_dopri": production["command"]},
    }


# --------------------------------------------------------------------------- #
# Report assembly
# --------------------------------------------------------------------------- #

def case_verdict(sensitivity, numeric_envelopes):
    """Ephemeris verdict per horizon and overall, computed from the entries."""
    resolved, unresolved, reasons = [], [], set()
    per_horizon = {}
    for row in sensitivity:
        counts = {"resolved": 0, "unresolved": 0}
        for key, entries in row["observables"].items():
            for entry in entries:
                if entry["resolved"]:
                    counts["resolved"] += 1
                    resolved.append({"horizon_s": row["horizon_s"], "observable": key,
                                     "size_label": entry["size_label"], "sign": entry["sign"],
                                     "response_max": entry["response_max"],
                                     "numerical_envelope": entry["numerical_envelope"],
                                     "response_over_envelope_ratio":
                                         entry["response_over_envelope_ratio"]})
                else:
                    counts["unresolved"] += 1
                    reasons.add(entry["resolution_reason"])
                    unresolved.append({"horizon_s": row["horizon_s"], "observable": key,
                                       "size_label": entry["size_label"],
                                       "sign": entry["sign"],
                                       "response_max": entry["response_max"],
                                       "numerical_envelope": entry["numerical_envelope"],
                                       "resolution_reason": entry["resolution_reason"]})
        per_horizon[str(row["horizon_s"])] = {
            "resolved_observation_count": counts["resolved"],
            "unresolved_observation_count": counts["unresolved"],
            "status": "MEASURED_SENSITIVITY" if counts["resolved"] else "UNRESOLVED",
            "statement": (
                f"at horizon {row['horizon_s']} s "
                + (f"{counts['resolved']} observable/size/sign combinations exceed the "
                   "resolution policy, so a bounded physical sensitivity is measured"
                   if counts["resolved"] else
                   "no observable and size meets the resolution policy: the "
                   "Sun-ephemeris sensitivity at this horizon is UNRESOLVED. The "
                   "responses are measured run-to-run differences, not sensitivities.")
            ),
        }
    return {
        "resolved_observation_count": len(resolved),
        "unresolved_observation_count": len(unresolved),
        "unresolved_reasons": sorted(reasons),
        "resolved": resolved,
        "unresolved": unresolved,
        "per_horizon": per_horizon,
        "status": "MEASURED_SENSITIVITY" if resolved else "UNRESOLVED",
        "status_statement": (
            "at least one observable meets the resolution policy, so a bounded "
            "physical Sun-ephemeris sensitivity is measured"
            if resolved else
            "no observable and size meets the resolution policy, so the Sun-ephemeris "
            "sensitivity of this case is UNRESOLVED at the numerical precision "
            "reached here; the responses below are reported as measured differences, "
            "not as sensitivities"),
        "unresolved_note": ("entries whose requested Sun offset rounded to zero in "
                            "double are always unresolved: a request that never reaches "
                            "the force model carries no physical response"),
        "numerical_envelopes": {str(h): v["envelope"] for h, v in numeric_envelopes.items()},
        "envelope_sources": {str(h): v["envelope_source"]
                             for h, v in numeric_envelopes.items()},
    }


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--spk", type=Path, required=True)
    p.add_argument("--lsk", type=Path, required=True)
    p.add_argument("--cases", type=Path, nargs="+", required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--workdir", type=Path, required=True)
    p.add_argument("--horizons", type=int, nargs="+", default=list(HORIZONS))
    p.add_argument("--sensitivity-horizons", type=int, nargs="+", default=[3600, 21600])
    p.add_argument("--timeout", type=int, default=3600)
    p.add_argument("--envelope-input", type=Path, nargs="*", default=[],
                   help="measured sensitivity studies whose per-horizon uncertainty "
                        "blocks also gate this driver's responses; the largest "
                        "envelope of the two sources is always used")
    args = p.parse_args()
    if args.output.exists():
        p.error("output exists; earlier evidence is immutable, choose a fresh path")
    if not set(args.sensitivity_horizons) <= set(args.horizons):
        p.error("sensitivity horizons must be a subset of the measured horizons")

    report = {
        "schema": "leo-dynamics/precision-and-events/2",
        "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "note": __doc__,
        "provenance": {
            "binary": str(args.binary.resolve()),
            "binary_sha256": sha256_of(args.binary),
            "spk": str(args.spk.resolve()),
            "spk_sha256": sha256_of(args.spk),
            "lsk": str(args.lsk.resolve()),
            "lsk_sha256": sha256_of(args.lsk),
            "driver": str(Path(__file__).resolve()),
            "driver_sha256": sha256_of(__file__),
            "config_sha256": {str(c.resolve()): sha256_of(c) for c in args.cases},
            "envelope_inputs": [{"path": str(p.resolve()), "sha256": sha256_of(p)}
                                for p in args.envelope_input],
            "python": sys.version.split()[0],
            "platform": platform.platform(),
            "horizons_s": args.horizons,
            "sensitivity_horizons_s": args.sensitivity_horizons,
            "precision_steps_s": list(PRECISION_STEPS),
            "envelope_methods": {**ENVELOPE_METHODS, **EXTRA_METHODS},
            "response_methods": list(RESPONSE_METHODS),
            "sun_sizes": [{"label": l, "option_kind": k, "magnitude": s}
                          for l, k, s in SUN_SIZES],
            "event_windows_s": list(EVENT_WINDOWS_S),
            "response_ratio_threshold": RESPONSE_RATIO,
            "consistency_tolerance": CONSISTENCY_TOLERANCE,
            "ephemeris_query_path": "every right-hand-side evaluation queries CSPICE "
                                    "directly; no ephemeris cache or interpolation",
        },
        "cases": {},
    }

    for case in args.cases:
        identifier = case_id_of(case)
        print(f"[{identifier}] starting", flush=True)
        harness = Harness(args.binary.resolve(), args.spk.resolve(), args.lsk.resolve(),
                          case.resolve(), args.workdir / identifier, args.timeout)
        ladder, _ = arithmetic_ladder(harness, args.horizons)
        print(f"[{identifier}] arithmetic ladder done", flush=True)

        envelopes, nominal_runs, base_x_by_method = {}, {}, {}
        supplied, envelope_sources = external_envelopes(args.envelope_input, identifier)
        for horizon in args.sensitivity_horizons:
            names = sorted(set(ENVELOPE_METHODS) | set(RESPONSE_METHODS))
            paths_used = envelope_sources.get(horizon, [])
            env, origin, methods, runs = envelope_block(
                harness, horizon, names, supplied.get(horizon),
                ", ".join(paths_used) if paths_used else None)
            nominal_runs[horizon] = runs
            base_x_by_method[horizon] = {
                name: [row["sun_position_m"][0] for row in run["loads"]]
                for name, run in runs.items()}
            envelopes[horizon] = {"envelope": env, "envelope_source": origin,
                                  "members": methods,
                                  "definition": (
                                      "largest same-unit observable difference across "
                                      "all pairs of independently refined double-state "
                                      "runs at the same output grid, and the larger of "
                                      "that and any envelope supplied by the measured "
                                      "sensitivity study: an empirical numerical "
                                      "envelope, not a bound")}
        probes, _probe_nominal, _probe_base = sun_offset_probes(
            harness, args.sensitivity_horizons[-1])
        print(f"[{identifier}] envelopes and offset probes done", flush=True)

        sensitivity = ephemeris_sensitivity(harness, args.sensitivity_horizons,
                                            envelopes, nominal_runs, base_x_by_method)
        print(f"[{identifier}] ephemeris sensitivity done", flush=True)

        events = []
        for horizon in args.horizons:
            events.append(event_localisation(harness, horizon))
        print(f"[{identifier}] event localisation done", flush=True)

        short = harness({"method": "rk4", "max_step": 1.0}, min(args.horizons))
        mixed = harness({"method": "mixed_rk4", "max_step": 1.0}, min(args.horizons))
        report["cases"][identifier] = {
            "config": str(case.resolve()),
            "arithmetic": arithmetic_facts({"rk4": short, "mixed_rk4": mixed}),
            "arithmetic_ladder": ladder,
            "sun_offset_realisation": {
                "horizon_s": args.sensitivity_horizons[-1],
                "note": ("requested versus applied, measured from the Sun position "
                         "actually delivered to the force model. A request that "
                         "rounds to zero in double is reported as zero applied and "
                         "is not used as a physical perturbation."),
                "probes": probes,
            },
            "numerical_envelopes": {str(h): v for h, v in envelopes.items()},
            "ephemeris_sensitivity": sensitivity,
            "verdict": case_verdict(sensitivity, envelopes),
            "events": events,
            "physical": short["physical"],
            "run_provenance": {
                "invocations": len(harness.commands),
                "distinct_invocations": len({tuple(c) for c in harness.commands}),
                "reuse_rule": "Each invocation executes the audit binary; saved records are scientific artifacts, not reusable ephemeris/state caches.",
                "sample_commands": [list(c) for c in harness.commands[:4]],
            },
        }
        print(f"[{identifier}] complete", flush=True)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print("Saved", args.output)


if __name__ == "__main__":
    main()