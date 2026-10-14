#!/usr/bin/env python3
"""Reduce measured native-input slices; never substitute them for the full study."""
import argparse
import hashlib
import html
import json
import math
import os
from pathlib import Path


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate result key: {key}")
        result[key] = value
    return result


def load(path):
    return json.loads(Path(path).read_text(), object_pairs_hook=unique_object,
                      parse_constant=lambda token: (_ for _ in ()).throw(ValueError(token)))


def publish(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".partial")
    with temporary.open("w") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def heatmap(data, destination, mode):
    grid = data["altitude_by_local_solar_time_heatmap"]
    altitudes = grid["rows_altitude_km"]
    hours = grid["columns_local_solar_time_hours"]
    values = grid["values"]
    valid = [v for row in values[1:-1] for v in row]
    maximum = max(valid)
    cell_w, cell_h = 72, 24
    left, top = 82, 68
    width, height = left + cell_w * len(hours) + 20, top + cell_h * len(altitudes) + 70
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}">',
             '<g font-family="sans-serif" font-size="13" fill="var(--fg, currentColor)">',
             f'<text x="12" y="22">NRLMSISE-00 {html.escape(mode)}: altitude sensitivity</text>',
             '<text x="12" y="43">Maximum |d ln ρ / dh| (km⁻¹); tested weather, latitude and probe scales</text>']
    for j, hour in enumerate(hours):
        parts.append(f'<text x="{left + j * cell_w + 24}" y="{top - 9}">{hour:g} h</text>')
    for i, altitude in enumerate(altitudes):
        y = top + i * cell_h
        parts.append(f'<text x="9" y="{y + 17}">{altitude:g} km</text>')
        for j, value in enumerate(values[i]):
            x = left + j * cell_w
            if i == 0 or i == len(altitudes) - 1:
                text = "—"
                opacity = 0
            else:
                text = f"{value:.3f}"
                opacity = 0.12 + 0.65 * value / maximum if maximum else 0
            parts.append(f'<rect x="{x}" y="{y}" width="{cell_w - 1}" height="{cell_h - 1}" '
                         f'fill="var(--accent, currentColor)" fill-opacity="{opacity:.4f}" '
                         'stroke="var(--border, currentColor)" stroke-opacity="0.2"/>')
            parts.append(f'<text x="{x + 13}" y="{y + 17}">{text}</text>')
    parts += [f'<text x="12" y="{height - 36}">— Central differences were not evaluated at the band endpoints.</text>',
              f'<text x="12" y="{height - 15}">Native-input slice only: no provider, cache-runtime or propagation claim.</text>',
              '</g></svg>']
    destination.write_text("\n".join(parts) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", action="append", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    slices = []
    for path in args.native:
        document = load(path)
        metadata, data = document["metadata"], document["result"]
        if metadata.get("verification_only") or not str(metadata.get("evidence_class", "")).startswith("scientific-native-input-partial"):
            raise ValueError(f"{path}: not a verified pinned-input native slice")
        mode = "ap-history" if metadata["ap_history_mode"] else "daily-ap"
        derivatives = data["altitude_difference"]
        smooth_time = [row for row in data["time_difference"] if not row["weather_vector_changed"]]
        row = {
            "mode": mode,
            "source": str(path.resolve()),
            "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "metadata": metadata,
            "primary_weather": [r for r in data["weather_pool"] if r["role"] == "primary weather regime"],
            "altitude_profile_count": len(data["altitude_profile"]),
            "altitude_finite_difference_count": len(derivatives),
            "max_abs_altitude_gradient_per_km": max(abs(r["dlog_density_daltitude_per_km"]) for r in derivatives),
            "max_abs_altitude_curvature_per_km2": max(abs(r["second_derivative_per_km2"]) for r in derivatives),
            "smooth_time_sample_count": len(smooth_time),
            "max_abs_smooth_time_gradient_per_s": max((abs(r["dlog_density_dt_per_s"]) for r in smooth_time), default=None),
            "altitude_interpolation": data["altitude_log_density_interpolation_holdouts"],
            "boundary_structure": data["discrete_boundary_structure"],
            "scope": data["scope"],
        }
        heatmap(data, args.out / f"sensitivity-{mode}.svg", mode)
        slices.append(row)
    modes = [row["mode"] for row in slices]
    if len(set(modes)) != len(modes):
        raise ValueError("duplicate geomagnetic configurations")
    summary = {"kind": "atm-cache-study-native-partial-summary", "study_complete": False,
               "evidence_class": "measured pinned-input NRLMSISE-00 native-geodetic slices",
               "not_measured": ["JB2008 primary sensitivity", "complete-provider candidate accuracy",
                                "real trajectory locality", "coupled propagation errors",
                                "current provider/native/preprocessing baseline costs", "end-to-end speedups"],
               "slices": slices}
    publish(args.out / "native-sensitivity-summary.json", summary)
    print(json.dumps({"slices": modes, "study_complete": False, "out": str(args.out)}, sort_keys=True))


if __name__ == "__main__":
    main()
