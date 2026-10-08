#!/usr/bin/env python3
"""Headless visualization of Stage 3 CSV diagnostics; no physics evaluation.

Commands:
  plot_analysis.py DATA_DIR --output DIR --format png|svg|both [--only NAME ...] [--orbit INDEX]
  plot_analysis.py compare DATA_DIR DATA_DIR ... --output DIR [--format ...]
  plot_analysis.py reduce MANIFEST.csv --output SUMMARY.csv [--sun-ra-deg DEGREES]
  plot_analysis.py heatmap TABLE.csv --x COL --y COL --value COL --output FILE
  plot_analysis.py series TABLE.csv --x COL --y COL [--y COL ...] --output FILE
"""
from __future__ import annotations
import argparse
import csv
import math
import sys
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


class PlotError(ValueError):
    pass


OPTIONAL_NAN = {"sun_incidence_rad", "projected_illuminated_area_m2",
                "projected_aerodynamic_area_m2", "orbit_normal_angle_rad"}
TRAJECTORY = "time_s,x_m,y_m,z_m,vx_m_s,vy_m_s,vz_m_s,qw,qx,qy,qz,omega_x_rad_s,omega_y_rad_s,omega_z_rad_s"
ORBITAL = "time_s,radius_m,altitude_m,a_m,e,i_rad,raan_rad,arg_periapsis_rad,true_anomaly_rad,periapsis_radius_m,apoapsis_radius_m,periapsis_altitude_m,apoapsis_altitude_m,specific_energy_j_kg,h_x_m2_s,h_y_m2_s,h_z_m2_s,raan_unwrapped_rad,arg_periapsis_unwrapped_rad,true_anomaly_unwrapped_rad,specific_kinetic_energy_j_kg,specific_potential_energy_j_kg"
ATTITUDE = "time_s,angular_speed_rad_s,rotational_energy_j,inertial_momentum_kg_m2_s,torque_x_nm,torque_y_nm,torque_z_nm,torque_magnitude_nm,relative_yaw_rad,relative_pitch_rad,relative_roll_rad,euler_singular"
SURFACES = "time_s,surface_index,normal_x,normal_y,normal_z,sun_incidence_rad,projected_illuminated_area_m2,projected_aerodynamic_area_m2,orbit_normal_angle_rad"
WORK = "time_s,srp_power_w,cumulative_srp_work_j,specific_srp_work_j_kg,orbital_energy_change_j,specific_energy_change_j_kg,energy_minus_srp_work_j,srp_force_r_n,srp_force_t_n,srp_force_n_n,srp_power_r_w,srp_power_t_w,srp_power_n_w,cumulative_work_r_j,cumulative_work_t_j,cumulative_work_n_j,sun_inbound_transverse_work_j,sun_outbound_transverse_work_j"
PER_ORBIT = "orbit_index,start_time_s,end_time_s,complete,period_s,start_a_m,start_e,start_i_rad,start_raan_unwrapped_rad,end_raan_unwrapped_rad,delta_a_m,delta_e,delta_i_rad,delta_raan_rad,delta_arg_periapsis_rad,delta_true_anomaly_rad,srp_work_j,specific_srp_work_j_kg,srp_work_r_j,srp_work_t_j,srp_work_n_j,orbital_energy_change_j,energy_minus_srp_work_j"
VECTOR_UNITS = {"point_mass": "m_s2", "j2": "m_s2", "srp_force": "n", "drag_force": "n", "albedo_force": "n",
                "total_non_gravitational_force": "n", "srp_torque": "nm", "drag_torque": "nm", "albedo_torque": "nm",
                "gravity_gradient_torque": "nm", "total_torque": "nm", "total_acceleration": "m_s2",
                "total_angular_acceleration": "rad_s2"}
ENVIRONMENT = "time_s,illumination,density_kg_m3,drag_model," + ",".join(
    f"{prefix}_{axis}_{unit}" for prefix, unit in VECTOR_UNITS.items() for axis in "xyz")
SCHEMAS = {"trajectory": TRAJECTORY, "orbital_elements": ORBITAL, "attitude": ATTITUDE,
           "surfaces": SURFACES, "environment": ENVIRONMENT, "work": WORK, "per_orbit": PER_ORBIT,
           "apsides": "time_s,kind,radius_m,left_index,fraction"}
EMPTY_ALLOWED = {"surfaces", "per_orbit", "apsides"}
DEG = 180 / math.pi


def read_table(path, required=(), allow_empty=False, nan_columns=(), ordered_time=True):
    path = Path(path)
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        header = next(reader, None)
        if not header or any(not name for name in header) or len(set(header)) != len(header):
            raise PlotError(f"{path}: expected unique, nonempty CSV column names")
        missing = set(required) - set(header)
        if missing:
            raise PlotError(f"{path}: missing columns: {', '.join(sorted(missing))}")
        values = {name: [] for name in header}
        for line, row in enumerate(reader, 2):
            if len(row) != len(header):
                raise PlotError(f"{path}:{line}: expected {len(header)} fields, got {len(row)}")
            for name, text in zip(header, row):
                try:
                    value = float(text)
                except ValueError:
                    raise PlotError(f"{path}:{line}: {name}: expected a number, got {text!r}") from None
                if not math.isfinite(value) and not (math.isnan(value) and name in nan_columns):
                    raise PlotError(f"{path}:{line}: {name}: nonfinite diagnostic")
                values[name].append(value)
    result = {name: np.array(items, dtype=float) for name, items in values.items()}
    if not len(result[header[0]]) and not allow_empty:
        raise PlotError(f"{path}: no data rows")
    if ordered_time and "time_s" in result:
        times = result["time_s"]
        if np.any(times < 0) or np.any(np.diff(times) <= 0):
            raise PlotError(f"{path}: time_s must be nonnegative and strictly increasing")
    return result


def load_dataset(directory):
    directory = Path(directory)
    data = {name: read_table(directory / f"{name}.csv", schema.split(","), name in EMPTY_ALLOWED,
                             OPTIONAL_NAN if name == "surfaces" else (), name != "surfaces")
            for name, schema in SCHEMAS.items()}
    with (directory / "metadata.csv").open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        if next(reader, None) != ["key", "value"]:
            raise PlotError(f"{directory}/metadata.csv: expected key,value header")
        metadata = {}
        for line, row in enumerate(reader, 2):
            if len(row) != 2 or not row[0] or row[0] in metadata:
                raise PlotError(f"{directory}/metadata.csv:{line}: expected unique key,value rows")
            metadata[row[0]] = row[1]
    if metadata.get("schema_version") != "1":
        raise PlotError(f"{directory}/metadata.csv: expected schema_version 1")
    data["metadata"] = metadata
    data["label"] = directory.name
    times = data["trajectory"]["time_s"]
    for name in ("orbital_elements", "attitude", "environment", "work"):
        if not np.array_equal(data[name]["time_s"], times):
            raise PlotError(f"{directory}/{name}.csv: sample timestamps/count differ from trajectory.csv")
    surfaces = data["surfaces"]
    indices = surfaces["surface_index"]
    if np.any(indices < 0) or np.any(indices != np.floor(indices)):
        raise PlotError(f"{directory}/surfaces.csv: surface_index must be a nonnegative integer")
    count = int(metadata.get("surface_count", len(np.unique(indices))))
    if count < 0 or not np.array_equal(np.unique(indices), np.arange(count)):
        raise PlotError(f"{directory}/surfaces.csv: surface indices/count disagree with metadata")
    for index in range(count):
        if not np.array_equal(surfaces["time_s"][indices == index], times):
            raise PlotError(f"{directory}/surfaces.csv: surface {index} must have one row per trajectory timestamp")
    events = data["apsides"]
    if np.any(~np.isin(events["kind"], [0, 1])) or np.any((events["fraction"] < 0) | (events["fraction"] > 1)):
        raise PlotError(f"{directory}/apsides.csv: invalid kind or interpolation fraction")
    orbits = data["per_orbit"]
    starts, ends = orbits["start_time_s"], orbits["end_time_s"]
    if len(starts) and (np.any(ends <= starts) or starts[0] != times[0] or ends[-1] != times[-1]
                       or not np.array_equal(starts[1:], ends[:-1]) or np.any(~np.isin(orbits["complete"], [0, 1]))):
        raise PlotError(f"{directory}/per_orbit.csv: intervals must partition the sampled time range")
    return data


def save(fig, path):
    path = Path(path)
    if path.suffix not in (".png", ".svg"):
        raise PlotError(f"{path}: output extension must be .png or .svg")
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, dpi=140, bbox_inches="tight")
    print(path)


def series_figure(table, groups, title, time_column="time_s"):
    """Groups are (axis label, [(column, legend, display multiplier), ...])."""
    fig, axes = plt.subplots(len(groups), 1, figsize=(11, 3 * len(groups)), squeeze=False, sharex=True)
    time = table[time_column] / 3600
    for ax, (ylabel, fields) in zip(axes[:, 0], groups):
        for column, label, scale in fields:
            ax.plot(time, table[column] * scale, label=label)
        ax.set_ylabel(ylabel)
        ax.grid(alpha=.3)
        ax.legend(fontsize=8, ncol=min(len(fields), 3))
    axes[-1, 0].set_xlabel("Elapsed time [h]")
    fig.suptitle(title)
    fig.tight_layout()
    return fig


def field(column, label=None, scale=1):
    return (column, label or column, scale)


def vector(table, prefix):
    return np.column_stack([table[f"{prefix}_{axis}_{VECTOR_UNITS[prefix]}"] for axis in "xyz"])


def srp_acceleration_magnitude(data):
    """Convert exported J2000 SRP force magnitude to acceleration using metadata mass."""
    try:
        mass = float(data["metadata"]["mass_kg"])
    except (KeyError, ValueError):
        raise PlotError("metadata.csv: mass_kg must be a positive finite spacecraft mass") from None
    if not math.isfinite(mass) or mass <= 0:
        raise PlotError("metadata.csv: mass_kg must be a positive finite spacecraft mass")
    return np.linalg.norm(vector(data["environment"], "srp_force"), axis=1) / mass


def selected_orbit_transverse_work(data, orbit_index):
    """Rebase exported cumulative histories; linearly interpolate boundary values."""
    orbits, work = data["per_orbit"], data["work"]
    matches = np.flatnonzero(orbits["orbit_index"] == orbit_index)
    if orbit_index < 0 or len(matches) != 1:
        raise PlotError(f"per_orbit.csv: orbit_index {orbit_index} must identify one exported interval")
    index = matches[0]
    start, end = orbits["start_time_s"][index], orbits["end_time_s"][index]
    sampled = work["time_s"]
    if not sampled[0] <= start < end <= sampled[-1]:
        raise PlotError(f"per_orbit.csv: orbit_index {orbit_index} is outside the sampled work history")
    times = np.concatenate(([start], sampled[(sampled > start) & (sampled < end)], [end]))
    result = {"time_s": times - start}
    for column in ("sun_inbound_transverse_work_j", "sun_outbound_transverse_work_j", "cumulative_work_t_j"):
        values = np.interp(times, sampled, work[column])
        result[column] = values - values[0]
    return result, bool(orbits["complete"][index])


def figures(data, orbit_index=None):
    o, a, w, env, p = (data[name] for name in ("orbital_elements", "attitude", "work", "environment", "per_orbit"))
    transverse, complete = (w, None) if orbit_index is None else selected_orbit_transverse_work(data, orbit_index)
    specs = [
        ("altitude_vs_time", o, [("Altitude [km]", [field("altitude_m", "Altitude", .001)])]),
        ("orbital_elements", o, [(label, [field(col, label, scale)]) for col, label, scale in
          [("a_m", "Semi-major axis [km]", .001), ("e", "Eccentricity", 1), ("i_rad", "Inclination [deg]", DEG),
           ("raan_rad", "RAAN [deg]", DEG), ("arg_periapsis_rad", "Argument of periapsis [deg]", DEG), ("true_anomaly_rad", "True anomaly [deg]", DEG)]]),
        ("orbital_angles_unwrapped", o, [("Unwrapped angles [deg]", [field(col, label, DEG) for col, label in
          [("raan_unwrapped_rad", "RAAN"), ("arg_periapsis_unwrapped_rad", "Argument of periapsis"), ("true_anomaly_unwrapped_rad", "True anomaly")]])]),
        ("apsides_evolution", o, [("Radius [km]", [field("periapsis_radius_m", "Periapsis", .001), field("apoapsis_radius_m", "Apoapsis", .001)]),
          ("Altitude [km]", [field("periapsis_altitude_m", "Periapsis", .001), field("apoapsis_altitude_m", "Apoapsis", .001)])]),
        ("attitude_quaternion", data["trajectory"], [("Quaternion", [field("q" + axis, axis) for axis in "wxyz"])]),
        ("attitude_angular_velocity", {**data["trajectory"], **a}, [("Body rate [rad/s]", [field(f"omega_{axis}_rad_s", axis) for axis in "xyz"]), ("Angular speed [rad/s]", [field("angular_speed_rad_s", "Magnitude")])]),
        ("attitude_rotational_state", a, [("Rotational energy [J]", [field("rotational_energy_j")]), ("Momentum [kg m²/s]", [field("inertial_momentum_kg_m2_s")]), ("Torque [N m]", [field("torque_magnitude_nm")])]),
        ("attitude_relative_euler", a, [("Relative ZYX angles [deg]", [field(f"relative_{axis}_rad", axis, DEG) for axis in ("yaw", "pitch", "roll")]), ("Gimbal lock flag", [field("euler_singular")])]),
        ("environment_illumination_density", env, [("Illuminated fraction", [field("illumination")]), ("Density [kg/m³]", [field("density_kg_m3")])]),
        ("environment_drag", env, [("Drag force [N]", [field(f"drag_force_{axis}_n", axis) for axis in "xyz"]), ("Model (0 none; 1 plate; 2 Sentman)", [field("drag_model")])]),
        ("environment_total_acceleration", env, [("J2000 acceleration [m/s²]", [field(f"total_acceleration_{axis}_m_s2", axis) for axis in "xyz"]), ("Body angular acceleration [rad/s²]", [field(f"total_angular_acceleration_{axis}_rad_s2", axis) for axis in "xyz"])]),
        ("srp_force", env, [("J2000 SRP force [N]", [field(f"srp_force_{axis}_n", axis) for axis in "xyz"])]),
        ("srp_acceleration", {"time_s": env["time_s"], "magnitude_m_s2": srp_acceleration_magnitude(data)},
         [("SRP acceleration magnitude [m/s²]", [field("magnitude_m_s2", "SRP")])]),
        ("srp_power", w, [("SRP power [W]", [field("srp_power_w", "Total")] + [field(f"srp_power_{axis}_w", axis.upper()) for axis in "rtn"])]),
        ("srp_force_rtn", w, [("RTN force [N]", [field(f"srp_force_{axis}_n", axis.upper()) for axis in ("r", "t", "n")]), ("RTN cumulative work [J]", [field(f"cumulative_work_{axis}_j", axis.upper()) for axis in "rtn"])]),
        ("srp_cumulative_work", w, [("SRP work [J]", [field("cumulative_srp_work_j", "SRP work")]), ("Two-body energy change [J]", [field("orbital_energy_change_j", "Energy change"), field("energy_minus_srp_work_j", "Residual (other physics + sampling)")])]),
        ("orbital_energy", o, [("Specific orbital energy [J/kg]", [field("specific_kinetic_energy_j_kg", "Kinetic"), field("specific_potential_energy_j_kg", "Point-mass potential"), field("specific_energy_j_kg", "Total (two body)")])]),
        ("srp_transverse_work", transverse, [("Transverse work [J]", [field("sun_inbound_transverse_work_j", "Toward Sun"), field("sun_outbound_transverse_work_j", "Away from Sun"), field("cumulative_work_t_j", "Net transverse")])]),
    ]
    fig = plt.figure(figsize=(9, 8)); ax = fig.add_subplot(111, projection="3d")
    trajectory = data["trajectory"]
    ax.plot(*(trajectory[f"{axis}_m"] * .001 for axis in "xyz"), label="Trajectory")
    radius = float(data["metadata"]["earth_equatorial_radius_m"]) * .001
    if not math.isfinite(radius) or radius <= 0:
        raise PlotError("metadata.csv: earth_equatorial_radius_m must be positive and finite")
    u, v = np.meshgrid(np.linspace(0, 2 * math.pi, 32), np.linspace(0, math.pi, 20))
    ax.plot_surface(radius * np.cos(u) * np.sin(v), radius * np.sin(u) * np.sin(v), radius * np.cos(v), alpha=.2, color="tab:blue")
    ax.set(xlabel="J2000 X [km]", ylabel="J2000 Y [km]", zlabel="J2000 Z [km]", title="Earth-centred trajectory")
    ax.set_box_aspect((1, 1, 1)); ax.legend()
    yield "orbit_3d", fig
    for name, table, groups in specs:
        fig = series_figure(table, groups, name.replace("_", " "))
        if name == "apsides_evolution":
            events = data["apsides"]
            fig.axes[0].scatter(events["time_s"] / 3600, events["radius_m"] * .001, s=12, label="Sampled events")
            fig.axes[0].legend()
        if name == "srp_transverse_work" and orbit_index is not None:
            fig.suptitle(f"Transverse SRP work — orbit {orbit_index} ({'complete' if complete else 'partial interval'})")
            fig.axes[-1].set_xlabel("Time since orbit start [h]")
        yield name, fig
    for name, prefixes, unit in [("environment_force_contributions", ("srp_force", "drag_force", "albedo_force", "total_non_gravitational_force"), "N"),
                                   ("environment_torque_contributions", ("srp_torque", "drag_torque", "albedo_torque", "gravity_gradient_torque", "total_torque"), "N m")]:
        table = {**env}
        for prefix in prefixes:
            table[prefix + "_norm"] = np.linalg.norm(vector(env, prefix), axis=1)
        groups = [(f"Magnitude [{unit}]", [field(prefix + "_norm", prefix) for prefix in prefixes])]
        groups += [(f"{prefix} components [{unit}]", [field(f"{prefix}_{axis}_{VECTOR_UNITS[prefix]}", axis) for axis in "xyz"]) for prefix in prefixes]
        yield name, series_figure(table, groups, name.replace("_", " "))
    yield "gravity_acceleration", series_figure(env, [(f"{prefix} [m/s²]", [field(f"{prefix}_{axis}_m_s2", axis) for axis in "xyz"]) for prefix in ("point_mass", "j2")], "Gravity contributions (not forces)")
    fig, ax = plt.subplots(figsize=(8, 6))
    ax.plot(w["cumulative_srp_work_j"], w["orbital_energy_change_j"], label="Sampled energy vs work")
    ax.set(xlabel="Cumulative SRP work [J]", ylabel="Two-body energy change [J]", title="Energy and SRP work")
    ax.grid(alpha=.3); ax.legend(); yield "orbital_energy_vs_srp_work", fig
    surfaces = data["surfaces"]
    if len(surfaces["time_s"]):
        for name, columns in [("surface_incidence", [("sun_incidence_rad", "Sun incidence [deg]", DEG), ("orbit_normal_angle_rad", "Surface vs orbit normal [deg]", DEG)]),
                              ("surface_projected_area", [("projected_illuminated_area_m2", "Illuminated area [m²]", 1), ("projected_aerodynamic_area_m2", "Aerodynamic area [m²]", 1)])]:
            fig, axes = plt.subplots(2, 1, figsize=(11, 7), sharex=True)
            for ax, (column, label, scale) in zip(axes, columns):
                for index in np.unique(surfaces["surface_index"]):
                    mask = surfaces["surface_index"] == index
                    ax.plot(surfaces["time_s"][mask] / 3600, surfaces[column][mask] * scale, label=f"Surface {int(index)}")
                ax.set_ylabel(label); ax.grid(alpha=.3); ax.legend(ncol=3, fontsize=8)
            axes[-1].set_xlabel("Elapsed time [h]"); fig.tight_layout(); yield name, fig
    if len(p["orbit_index"]):
        yield "per_orbit_elements", series_figure(p, [("Inclination [deg]", [field("start_i_rad", "At interval start", DEG)]),
                ("RAAN [deg]", [field("start_raan_unwrapped_rad", "Start", DEG), field("end_raan_unwrapped_rad", "End", DEG)]),
                ("Delta a [m]", [field("delta_a_m")]), ("Delta angles [deg]", [field(col, col, DEG) for col in ("delta_i_rad", "delta_raan_rad", "delta_arg_periapsis_rad")])], "Per-orbit elements (partial intervals included)", "start_time_s")
        yield "srp_work_per_orbit", series_figure(p, [("SRP work [J]", [field("srp_work_j", "Total")] + [field(f"srp_work_{axis}_j", axis.upper()) for axis in "rtn"]),
                ("Energy change [J]", [field("orbital_energy_change_j", "Two-body energy change"), field("energy_minus_srp_work_j", "Residual")]),
                ("Complete interval", [field("complete")])], "Per-orbit work (partial intervals flagged)", "start_time_s")


def formats(value):
    return ("png", "svg") if value == "both" else (value,)


def plot_run(directory, output, fmt, only, orbit_index=None):
    selected = set(only)
    found = set()
    for name, fig in figures(load_dataset(directory), orbit_index):
        found.add(name)
        try:
            if not selected or name in selected:
                for extension in formats(fmt):
                    save(fig, Path(output) / f"{name}.{extension}")
        finally:
            plt.close(fig)
    if selected - found:
        raise PlotError("unavailable/unknown plots: " + ", ".join(sorted(selected - found)))


def compare(directories, output, fmt):
    runs = [load_dataset(directory) for directory in directories]
    if len(runs) < 2:
        raise PlotError("compare requires at least two exported datasets")
    for name, filename, column, label, scale in [
        ("altitude", "orbital_elements", "altitude_m", "Altitude [km]", .001),
        ("semi_major_axis", "orbital_elements", "a_m", "Semi-major axis [km]", .001),
        ("eccentricity", "orbital_elements", "e", "Eccentricity", 1),
        ("inclination", "orbital_elements", "i_rad", "Inclination [deg]", DEG),
        ("raan", "orbital_elements", "raan_unwrapped_rad", "Unwrapped RAAN [deg]", DEG),
        ("arg_periapsis", "orbital_elements", "arg_periapsis_unwrapped_rad", "Unwrapped argument of periapsis [deg]", DEG),
        ("srp_power", "work", "srp_power_w", "SRP power [W]", 1),
        ("cumulative_srp_work", "work", "cumulative_srp_work_j", "SRP work [J]", 1),
        ("srp_transverse_work", "work", "cumulative_work_t_j", "Transverse work [J]", 1)]:
        fig, ax = plt.subplots(figsize=(10, 5))
        for run in runs:
            table = run[filename]
            ax.plot(table["time_s"] / 3600, table[column] * scale, label=run["label"])
        ax.set(xlabel="Elapsed time [h]", ylabel=label, title=name.replace("_", " "))
        ax.grid(alpha=.3); ax.legend()
        for extension in formats(fmt):
            save(fig, Path(output) / f"compare_{name}.{extension}")
        plt.close(fig)


def reduce_sweep(manifest, output, sun_ra=None, baseline=None):
    if sun_ra is not None and not math.isfinite(sun_ra):
        raise PlotError("Sun RA must be finite degrees")
    if baseline is not None and (not math.isfinite(baseline) or baseline <= 0):
        raise PlotError("baseline semi-major axis must be positive and finite")
    manifest = Path(manifest)
    with manifest.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames or not {"data_dir", "raan_deg", "pitch_deg"}.issubset(reader.fieldnames) or len(set(reader.fieldnames)) != len(reader.fieldnames):
            raise PlotError(f"{manifest}: expected data_dir,raan_deg,pitch_deg[,ltan_h]")
        axes = ["raan_deg", "pitch_deg"]
        if "ltan_h" in reader.fieldnames or sun_ra is not None:
            axes.append("ltan_h")
        rows = []
        for line, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()) or not row["data_dir"]:
                raise PlotError(f"{manifest}:{line}: ragged or empty manifest row")
            try:
                coordinates = [float(row[name]) for name in axes if name in row]
            except ValueError:
                raise PlotError(f"{manifest}:{line}: expected numeric sweep coordinates") from None
            if not all(math.isfinite(value) for value in coordinates):
                raise PlotError(f"{manifest}:{line}: nonfinite coordinates")
            if "ltan_h" in axes and "ltan_h" not in row:
                coordinates.append((12 + (coordinates[0] - sun_ra) / 15) % 24)
            if "ltan_h" in axes and not 0 <= coordinates[-1] < 24:
                raise PlotError(f"{manifest}:{line}: ltan_h must be in [0,24)")
            directory = manifest.parent / row["data_dir"]
            table = read_table(directory / "orbital_elements.csv", ("time_s", "a_m"))
            if np.any(table["a_m"] <= 0):
                raise PlotError(f"{directory}: a_m must be positive")
            index = int(np.argmax(table["a_m"]))
            rows.append(coordinates + [table["a_m"][index] - (baseline if baseline is not None else table["a_m"][0]), table["time_s"][index]])
    if not rows:
        raise PlotError(f"{manifest}: no sweep datasets")
    output = Path(output); output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(axes + ["max_delta_a_m", "time_to_max_s"]); writer.writerows(rows)
    print(output)


def edges(values):
    if len(values) == 1:
        return np.array([values[0] - .5, values[0] + .5])
    middle = (values[:-1] + values[1:]) / 2
    return np.concatenate(([values[0] - (middle[0] - values[0])], middle, [values[-1] + (values[-1] - middle[-1])]))


def heatmap(path, x, y, value, output, allow_missing, cmap, log_scale):
    table = read_table(path, (x, y, value))
    xs, ys = np.unique(table[x]), np.unique(table[y])
    grid = np.full((len(ys), len(xs)), np.nan)
    seen = set()
    for xx, yy, zz in zip(table[x], table[y], table[value]):
        if (xx, yy) in seen:
            raise PlotError(f"{path}: duplicate sweep coordinate ({xx}, {yy})")
        seen.add((xx, yy)); grid[np.searchsorted(ys, yy), np.searchsorted(xs, xx)] = zz
    missing = np.isnan(grid)
    if np.any(missing) and not allow_missing:
        raise PlotError(f"{path}: incomplete sweep grid; use --allow-missing to show hatched cells")
    norm = None
    if log_scale:
        if np.any(grid[~missing] <= 0):
            raise PlotError(f"{path}: log scale requires strictly positive values")
        norm = matplotlib.colors.LogNorm()
    fig, ax = plt.subplots(figsize=(9, 7)); xe, ye = edges(xs), edges(ys)
    mesh = ax.pcolormesh(xe, ye, np.ma.masked_invalid(grid), cmap=cmap, norm=norm)
    for row, col in np.argwhere(missing):
        ax.add_patch(plt.Rectangle((xe[col], ye[row]), xe[col+1]-xe[col], ye[row+1]-ye[row], hatch="///", fill=False))
    fig.colorbar(mesh, ax=ax, label=value)
    ax.set(xlabel=x, ylabel=y, title=f"{value}: supplied parameter sweep")
    save(fig, output); plt.close(fig)


def supplied_series(path, x, columns, output, xlabel, ylabel, title, x_scale, y_scale):
    table = read_table(path, (x, *columns))
    if not all(math.isfinite(value) for value in (x_scale, y_scale)):
        raise PlotError("display scales must be finite")
    fig, ax = plt.subplots(figsize=(10, 5))
    for column in columns:
        ax.plot(table[x] * x_scale, table[column] * y_scale, label=column)
    ax.set(xlabel=xlabel or x, ylabel=ylabel or ", ".join(columns), title=title or "Supplied diagnostic series")
    ax.grid(alpha=.3); ax.legend(); save(fig, output); plt.close(fig)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    command = argv.pop(0) if argv and argv[0] in ("compare", "reduce", "heatmap", "series") else "plots"
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", nargs="+" if command == "compare" else None)
    parser.add_argument("--output", required=True)
    if command in ("plots", "compare"):
        parser.add_argument("--format", choices=("png", "svg", "both"), default="png")
    if command == "plots":
        parser.add_argument("--only", action="append", default=[])
        parser.add_argument("--orbit", type=int, help="exported orbit_index for transverse SRP work; includes flagged partial intervals")
    elif command == "reduce":
        parser.add_argument("--sun-ra-deg", type=float)
        parser.add_argument("--baseline-a-m", type=float)
    elif command in ("heatmap", "series"):
        parser.add_argument("--x", required=True)
        parser.add_argument("--y", required=True, action="append" if command == "series" else "store")
        if command == "heatmap":
            parser.add_argument("--value", required=True)
            parser.add_argument("--allow-missing", action="store_true")
            parser.add_argument("--cmap", default="viridis")
            parser.add_argument("--log-scale", action="store_true")
        else:
            parser.add_argument("--xlabel"); parser.add_argument("--ylabel"); parser.add_argument("--title")
            parser.add_argument("--x-scale", type=float, default=1); parser.add_argument("--y-scale", type=float, default=1)
    args = parser.parse_args(argv)
    try:
        if command == "plots":
            plot_run(args.input, args.output, args.format, args.only, args.orbit)
        elif command == "compare":
            compare(args.input, args.output, args.format)
        elif command == "reduce":
            reduce_sweep(args.input, args.output, args.sun_ra_deg, args.baseline_a_m)
        elif command == "heatmap":
            heatmap(args.input, args.x, args.y, args.value, args.output, args.allow_missing, args.cmap, args.log_scale)
        else:
            supplied_series(args.input, args.x, args.y, args.output, args.xlabel, args.ylabel, args.title, args.x_scale, args.y_scale)
    except (PlotError, OSError, UnicodeError, ValueError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr); return 2
    finally:
        plt.close("all")
    return 0


if __name__ == "__main__":
    sys.exit(main())
