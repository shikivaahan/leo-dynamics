"""Trajectory analysis: element history, loads, events, attitude and CSV export.

The analysis arrays are the native ``leo::analysis`` records. Their column
layout is checked against the native CSV schema, and every value is cross-checked
against the trajectory itself or against an independent two-body identity.
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np
import pytest

import leo_dynamics as leo
from conftest import (
    load_scenario,
    scenario_arguments,
    scenario_environment,
    scenario_model,
    scenario_spacecraft,
    scenario_state,
    truncate,
)

AU_M = 1.495978707e11

ORBITAL_COLUMNS = (
    "time_s", "radius_m", "altitude_m", "semi_major_axis_m", "eccentricity",
    "inclination_rad", "raan_rad", "arg_periapsis_rad", "true_anomaly_rad",
    "periapsis_radius_m", "apoapsis_radius_m", "periapsis_altitude_m",
    "apoapsis_altitude_m", "specific_energy_j_kg",
    "specific_angular_momentum_m2_s", "raan_unwrapped_rad",
    "arg_periapsis_unwrapped_rad", "true_anomaly_unwrapped_rad",
    "specific_kinetic_energy_j_kg", "specific_potential_energy_j_kg",
)
APSIDES_COLUMNS = ("kind", "time_s", "radius_m", "left_index", "fraction")
ELEMENTS = ("semi_major_axis_m", "eccentricity", "inclination_rad", "raan_rad",
            "arg_periapsis_rad", "true_anomaly_rad")
ORBITS_COLUMNS = (
    "index", "start_time_s", "end_time_s", "complete", "period_s",
    *(f"start_{name}" for name in ELEMENTS),
    *(f"end_{name}" for name in ELEMENTS),
    *(f"delta_{name}" for name in ELEMENTS),
    "raan_start_unwrapped_rad", "raan_end_unwrapped_rad",
)
ENVIRONMENT_COLUMNS = (
    "time_s", "epoch_et_s", "sun_position_m", "illumination", "density_kg_m3",
    "atmosphere_angular_velocity_rad_s", "point_mass_m_s2", "j2_m_s2",
    "gravity_gradient_nm", "srp_force_n", "srp_torque_nm", "drag_force_n",
    "drag_torque_nm", "albedo_force_n", "albedo_torque_nm",
    "non_gravitational_force_n", "non_gravitational_torque_nm",
    "total_acceleration_m_s2", "total_angular_acceleration_rad_s2",
)
ATTITUDE_COLUMNS = (
    "time_s", "quaternion_wxyz", "omega_body_rad_s", "angular_speed_rad_s",
    "rotational_energy_j", "inertial_momentum_kg_m2_s", "torque_nm",
    "torque_magnitude_nm", "relative_zyx_euler_rad", "euler_singular",
)
SURFACE_COLUMNS = (
    "time_s", "surface_index", "normal_j2000", "sun_incidence_rad",
    "projected_illuminated_area_m2", "projected_aerodynamic_area_m2",
    "orbit_normal_angle_rad",
)
WORK_COLUMNS = (
    "time_s", "srp_force_n", "srp_acceleration_m_s2", "force_rtn_n",
    "power_rtn_w", "cumulative_work_rtn_j", "power_w", "cumulative_work_j",
    "specific_work_j_kg", "orbital_energy_change_j",
    "specific_energy_change_j_kg", "energy_minus_srp_work_j",
    "sun_inbound_transverse_work_j", "sun_outbound_transverse_work_j", "mass_kg",
)
ORBIT_WORK_COLUMNS = (
    "index", "start_time_s", "end_time_s", "complete", "work_j",
    "specific_work_j_kg", "work_rtn_j", "orbital_energy_change_j",
    "energy_minus_srp_work_j",
)

CSV_FILES = ("metadata.csv", "trajectory.csv", "orbital_elements.csv",
             "attitude.csv", "surfaces.csv", "environment.csv", "work.csv",
             "per_orbit.csv", "apsides.csv")


def _document(name: str, light: bool = True) -> dict:
    document = load_scenario(name)
    return truncate(document) if light else document


def _propagate(name: str, light: bool = True, **overrides) -> leo.Trajectory:
    document = _document(name, light)
    arguments = scenario_arguments(document)
    arguments.update(overrides)
    return leo.propagate(**arguments)


def _rotate(quaternion: np.ndarray, vector: np.ndarray) -> np.ndarray:
    """Body-to-J2000 rotation for a Hamilton scalar-first quaternion."""
    w, x, y, z = quaternion
    return np.asarray([
        (1 - 2 * (y * y + z * z)) * vector[0] + 2 * (x * y - w * z) * vector[1]
        + 2 * (x * z + w * y) * vector[2],
        2 * (x * y + w * z) * vector[0] + (1 - 2 * (x * x + z * z)) * vector[1]
        + 2 * (y * z - w * x) * vector[2],
        2 * (x * z - w * y) * vector[0] + 2 * (y * z + w * x) * vector[1]
        + (1 - 2 * (x * x + y * y)) * vector[2],
    ])


@pytest.fixture(scope="module")
def coupled():
    document = _document("case_c_short")
    return document, leo.propagate(**scenario_arguments(document))


@pytest.fixture(scope="module")
def multi_orbit():
    """Three circular orbits, sampled every 60 s, for the per-orbit records."""
    document = load_scenario("two_body_circular")
    state = scenario_state(document)
    radius = float(np.linalg.norm(state.position_m))
    speed = float(np.linalg.norm(state.velocity_m_s))
    mu = leo.Earth().mu_m3_s2
    period = 2.0 * math.pi * math.sqrt((1.0 / (2.0 / radius - speed * speed / mu)) ** 3 / mu)
    times = np.arange(0.0, 3.0 * period, 60.0)
    arguments = scenario_arguments(document)
    arguments["times_s"] = times
    arguments["model"] = leo.DynamicsModel()
    arguments["environment"] = None
    return period, leo.propagate(**arguments)


# --------------------------------------------------------------------------
# Column layout and array ownership
# --------------------------------------------------------------------------

@pytest.mark.parametrize(
    "attribute,columns",
    [
        ("orbital", ORBITAL_COLUMNS),
        ("apsides", APSIDES_COLUMNS),
        ("orbits", ORBITS_COLUMNS),
        ("environment", ENVIRONMENT_COLUMNS),
        ("attitude", ATTITUDE_COLUMNS),
        ("surfaces", SURFACE_COLUMNS),
        ("work", WORK_COLUMNS),
        ("orbit_work", ORBIT_WORK_COLUMNS),
    ],
)
def test_analysis_column_layout(coupled, attribute, columns):
    _, trajectory = coupled
    array = getattr(trajectory.analyze(), attribute)
    assert array.ndim == 1
    assert array.dtype.names == tuple(columns)
    assert array.flags["WRITEABLE"] is False


def test_analysis_row_counts(coupled):
    document, trajectory = coupled
    count = len(document["run"]["sample_times_s"])
    surfaces = len(scenario_spacecraft(document).surfaces)
    analysis = trajectory.analyze()
    assert analysis.orbital.shape == (count,)
    assert analysis.environment.shape == (count,)
    assert analysis.attitude.shape == (count,)
    assert analysis.work.shape == (count,)
    assert analysis.surfaces.shape == (count * surfaces,)
    assert analysis.orbits.shape == analysis.orbit_work.shape


def test_analysis_arrays_own_their_memory(coupled):
    _, trajectory = coupled
    analysis = trajectory.analyze()
    orbital = analysis.orbital
    snapshot = np.array(orbital["radius_m"])
    del analysis  # the reported arrays must not point at a destroyed bundle
    np.testing.assert_array_equal(orbital["radius_m"], snapshot)
    assert np.isfinite(orbital["radius_m"]).all()
    assert orbital["radius_m"].flags["WRITEABLE"] is False


def test_subarray_columns_have_the_documented_width(coupled):
    _, trajectory = coupled
    analysis = trajectory.analyze()
    count = analysis.orbital.shape[0]
    assert analysis.orbital["specific_angular_momentum_m2_s"].shape == (count, 3)
    assert analysis.environment["sun_position_m"].shape == (count, 3)
    assert analysis.environment["total_acceleration_m_s2"].shape == (count, 3)
    assert analysis.attitude["quaternion_wxyz"].shape == (count, 4)
    assert analysis.surfaces["normal_j2000"].shape == (
        analysis.surfaces.shape[0], 3)


def test_boolean_flags_are_logical(coupled):
    _, trajectory = coupled
    analysis = trajectory.analyze()
    assert analysis.orbits.dtype["complete"] == np.dtype("bool")
    assert analysis.orbit_work.dtype["complete"] == np.dtype("bool")
    assert analysis.attitude.dtype["euler_singular"] == np.dtype("bool")


def test_trajectory_methods_match_the_analysis_arrays(coupled):
    _, trajectory = coupled
    analysis = trajectory.analyze()
    assert np.array_equal(trajectory.orbital_elements()["radius_m"],
                          analysis.orbital["radius_m"])
    assert np.array_equal(trajectory.apsides()["time_s"], analysis.apsides["time_s"])
    assert np.array_equal(trajectory.orbits()["period_s"],
                          analysis.orbits["period_s"])


# --------------------------------------------------------------------------
# Consistency with the trajectory
# --------------------------------------------------------------------------

def test_orbital_history_matches_the_states(coupled):
    _, trajectory = coupled
    analysis = trajectory.analyze()
    orbital = analysis.orbital
    states = trajectory.states
    np.testing.assert_array_equal(orbital["time_s"], trajectory.times_s)
    radius = np.linalg.norm(states[:, :3], axis=1)
    np.testing.assert_allclose(orbital["radius_m"], radius, rtol=0.0, atol=1e-6)
    np.testing.assert_allclose(orbital["altitude_m"],
                               radius - leo.Earth().equatorial_radius_m,
                               rtol=0.0, atol=1e-6)
    for index in (0, len(states) // 2, len(states) - 1):
        elements = leo.cartesian_to_keplerian(states[index, :3], states[index, 3:6])
        assert orbital["semi_major_axis_m"][index] == pytest.approx(elements.a_m,
                                                                    rel=1e-12)
        assert orbital["eccentricity"][index] == pytest.approx(elements.e, abs=1e-12)
        assert orbital["inclination_rad"][index] == pytest.approx(elements.i_rad,
                                                                  abs=1e-12)
    # Specific energy is v^2/2 - mu/r: an independent two-body identity.
    mu = leo.Earth().mu_m3_s2
    speed_squared = np.sum(states[:, 3:6] ** 2, axis=1)
    np.testing.assert_allclose(orbital["specific_energy_j_kg"],
                               0.5 * speed_squared - mu / radius, rtol=1e-12)


def test_environment_records_match_the_diagnostics_api(coupled):
    document, trajectory = coupled
    analysis = trajectory.analyze()
    records = analysis.environment
    np.testing.assert_array_equal(records["time_s"], trajectory.times_s)
    np.testing.assert_array_equal(records["epoch_et_s"], trajectory.times_s)
    sun = np.asarray(document["environment"]["sun_position_m"])
    np.testing.assert_array_equal(records["sun_position_m"],
                                  np.tile(sun, (records.size, 1)))
    assert np.all((records["illumination"] >= 0.0) & (records["illumination"] <= 1.0))
    assert np.all(records["density_kg_m3"] > 0.0)
    for index in (0, len(records) // 2, len(records) - 1):
        loads = leo.evaluate_dynamics(
            state=leo.State.from_array(trajectory.states[index]),
            time_s=float(trajectory.times_s[index]),
            spacecraft=scenario_spacecraft(document),
            model=scenario_model(document),
            environment=scenario_environment(document),
            epoch_et_s=0.0,
        )
        assert np.allclose(records["total_acceleration_m_s2"][index],
                           np.asarray(loads.total_acceleration_m_s2),
                           rtol=1e-12, atol=1e-15)
        assert np.allclose(records["srp_force_n"][index],
                           np.asarray(loads.srp.force_n), rtol=1e-12, atol=1e-18)
        assert np.allclose(records["gravity_gradient_nm"][index],
                           np.asarray(loads.gravity_gradient_nm),
                           rtol=1e-12, atol=1e-20)


def test_attitude_records_match_the_states(coupled):
    document, trajectory = coupled
    analysis = trajectory.analyze()
    attitude = analysis.attitude
    states = trajectory.states
    np.testing.assert_array_equal(attitude["time_s"], trajectory.times_s)
    np.testing.assert_allclose(attitude["quaternion_wxyz"],
                               states[:, 6:10] / np.linalg.norm(states[:, 6:10],
                                                                  axis=1)[:, None],
                               rtol=0.0, atol=1e-12)
    np.testing.assert_allclose(attitude["angular_speed_rad_s"],
                               np.linalg.norm(states[:, 10:13], axis=1),
                               rtol=1e-12, atol=1e-18)
    np.testing.assert_allclose(attitude["omega_body_rad_s"], states[:, 10:13],
                               rtol=1e-12, atol=1e-18)
    np.testing.assert_allclose(attitude["torque_magnitude_nm"],
                               np.linalg.norm(attitude["torque_nm"], axis=1),
                               rtol=1e-12, atol=1e-20)
    assert np.all(attitude["torque_magnitude_nm"] >= 0.0)


def test_surface_records_follow_the_attitude(coupled):
    document, trajectory = coupled
    analysis = trajectory.analyze()
    surfaces = analysis.surfaces
    craft = scenario_spacecraft(document)
    count = len(trajectory.times_s)
    panel_count = len(craft.surfaces)
    assert surfaces.shape[0] == count * panel_count
    np.testing.assert_array_equal(surfaces["time_s"],
                                  np.repeat(trajectory.times_s, panel_count))
    np.testing.assert_array_equal(surfaces["surface_index"],
                                  np.tile(np.arange(panel_count), count))
    for index in (0, count // 2, count - 1):
        for panel in range(panel_count):
            row = index * panel_count + panel
            expected = _rotate(trajectory.states[index, 6:10],
                               np.asarray(craft.surfaces[panel].normal_body))
            np.testing.assert_allclose(surfaces["normal_j2000"][row], expected,
                                       rtol=1e-12, atol=1e-9)


def test_work_records_are_physical(coupled):
    document, trajectory = coupled
    analysis = trajectory.analyze()
    work = analysis.work
    states = trajectory.states
    mass = scenario_spacecraft(document).mass_kg
    mu = leo.Earth().mu_m3_s2
    radius = np.linalg.norm(states[:, :3], axis=1)
    energy = 0.5 * np.sum(states[:, 3:6] ** 2, axis=1) - mu / radius
    np.testing.assert_array_equal(work["time_s"], trajectory.times_s)
    assert np.all(work["mass_kg"] == mass)
    assert work["cumulative_work_j"][0] == 0.0
    np.testing.assert_allclose(work["srp_acceleration_m_s2"],
                               work["srp_force_n"] / mass, rtol=1e-12, atol=1e-18)
    # Scalar SRP power is the inertial dot product of force and velocity.
    np.testing.assert_allclose(work["power_w"],
                               np.sum(work["srp_force_n"] * states[:, 3:6], axis=1),
                               rtol=1e-12, atol=1e-20)
    np.testing.assert_allclose(work["specific_energy_change_j_kg"],
                               energy - energy[0], rtol=1e-9, atol=1e-6)
    np.testing.assert_allclose(work["orbital_energy_change_j"],
                               mass * (energy - energy[0]), rtol=1e-9, atol=1e-4)
    np.testing.assert_allclose(work["energy_minus_srp_work_j"],
                               work["orbital_energy_change_j"]
                               - work["cumulative_work_j"], rtol=1e-9, atol=1e-16)


# --------------------------------------------------------------------------
# Events
# --------------------------------------------------------------------------

def test_apsides_are_radial_velocity_crossings():
    document = _document("two_body_eccentric", light=False)
    trajectory = leo.propagate(**scenario_arguments(document))
    radius = np.linalg.norm(trajectory.states[:, :3], axis=1)
    apsides = trajectory.apsides()
    assert apsides.shape[0] > 0
    assert np.all(np.isin(apsides["kind"], (0.0, 1.0)))
    assert np.all(apsides["left_index"] >= 0)
    assert np.all(apsides["left_index"] < radius.size - 1)
    assert np.all((apsides["fraction"] >= 0.0) & (apsides["fraction"] <= 1.0))
    for kind, time_s, event_radius, left, fraction in zip(
            apsides["kind"], apsides["time_s"], apsides["radius_m"],
            apsides["left_index"], apsides["fraction"]):
        left = int(left)
        assert trajectory.times_s[left] <= time_s <= trajectory.times_s[left + 1]
        interpolated = radius[left] + fraction * (radius[left + 1] - radius[left])
        assert event_radius == pytest.approx(interpolated, rel=1e-12)
        radial_velocity = np.sum(trajectory.position_m * trajectory.velocity_m_s, axis=1) / radius
        if kind == 0.0:
            assert radial_velocity[left] <= 0 <= radial_velocity[left + 1]
        else:
            assert radial_velocity[left] >= 0 >= radial_velocity[left + 1]


def test_circular_orbit_has_no_identifiable_apsides(multi_orbit):
    _, trajectory = multi_orbit
    assert trajectory.apsides().shape[0] == 0


def test_per_orbit_records(multi_orbit):
    period, trajectory = multi_orbit
    orbits = trajectory.orbits()
    assert orbits.shape[0] >= 2
    assert np.all(orbits["index"] == np.arange(orbits.shape[0]))
    assert orbits["start_time_s"][0] == trajectory.times_s[0]
    assert orbits["end_time_s"][-1] == trajectory.times_s[-1]
    for start, end, complete, measured in zip(
            orbits["start_time_s"], orbits["end_time_s"], orbits["complete"],
            orbits["period_s"]):
        assert end > start
        if complete:
            assert measured == pytest.approx(end - start, rel=1e-12)
            # Independent Kepler period from the osculating semi-major axis.
            assert measured == pytest.approx(period, rel=5e-3)
        else:
            assert measured == 0.0
    contiguous = np.asarray([orbits["start_time_s"][index] == orbits["end_time_s"][index - 1]
                             for index in range(1, orbits.shape[0])])
    assert np.all(contiguous)
    for index in range(1, orbits.shape[0]):
        for field in ("semi_major_axis_m", "eccentricity", "inclination_rad",
                      "raan_rad", "arg_periapsis_rad", "true_anomaly_rad"):
            assert orbits[f"delta_{field}"][index] == pytest.approx(
                orbits[f"end_{field}"][index] - orbits[f"start_{field}"][index],
                abs=1e-9)


def test_orbit_work_covers_every_orbit(multi_orbit):
    _, trajectory = multi_orbit
    analysis = trajectory.analyze()
    assert analysis.orbit_work.shape == analysis.orbits.shape
    np.testing.assert_array_equal(analysis.orbit_work["index"],
                                  analysis.orbits["index"])
    np.testing.assert_array_equal(analysis.orbit_work["start_time_s"],
                                  analysis.orbits["start_time_s"])
    np.testing.assert_array_equal(analysis.orbit_work["end_time_s"],
                                  analysis.orbits["end_time_s"])


def test_gravity_only_analysis_reports_no_fabricated_loads():
    trajectory = _propagate("two_body_circular")
    analysis = trajectory.analyze()
    assert np.all(analysis.environment["srp_force_n"] == 0.0)
    assert np.all(analysis.environment["drag_force_n"] == 0.0)
    assert np.all(analysis.environment["density_kg_m3"] == 0.0)
    assert np.all(analysis.environment["gravity_gradient_nm"] == 0.0)
    assert np.all(analysis.environment["non_gravitational_force_n"] == 0.0)
    assert np.all(np.linalg.norm(analysis.environment["point_mass_m_s2"], axis=1) > 0.0)


# --------------------------------------------------------------------------
# CSV export
# --------------------------------------------------------------------------

def _rows(path: Path) -> list[list[str]]:
    return [line.split(",") for line in path.read_text().splitlines() if line]


def test_export_csv_writes_the_native_schema(coupled, tmp_path):
    _, trajectory = coupled
    analysis = trajectory.analyze()
    target = tmp_path / "analysis"
    analysis.export_csv(target)
    assert target.is_dir()
    assert sorted(path.name for path in target.glob("*.csv")) == sorted(CSV_FILES)
    count = len(trajectory.times_s)
    surfaces = analysis.surfaces.shape[0]
    assert len(_rows(target / "trajectory.csv")) == count + 1
    assert len(_rows(target / "orbital_elements.csv")) == count + 1
    assert len(_rows(target / "attitude.csv")) == count + 1
    assert len(_rows(target / "environment.csv")) == count + 1
    assert len(_rows(target / "work.csv")) == count + 1
    assert len(_rows(target / "surfaces.csv")) == surfaces + 1
    assert len(_rows(target / "apsides.csv")) == analysis.apsides.shape[0] + 1
    assert len(_rows(target / "per_orbit.csv")) == analysis.orbits.shape[0] + 1
    assert _rows(target / "trajectory.csv")[0] == [
        "time_s", "x_m", "y_m", "z_m", "vx_m_s", "vy_m_s", "vz_m_s", "qw", "qx",
        "qy", "qz", "omega_x_rad_s", "omega_y_rad_s", "omega_z_rad_s"]
    assert _rows(target / "orbital_elements.csv")[0][:3] == [
        "time_s", "radius_m", "altitude_m"]


def test_exported_trajectory_values_match_the_arrays(coupled, tmp_path):
    _, trajectory = coupled
    target = tmp_path / "analysis"
    trajectory.analyze().export_csv(target)
    rows = _rows(target / "trajectory.csv")[1:]
    for index in (0, len(rows) // 2, len(rows) - 1):
        values = np.asarray([float(value) for value in rows[index]])
        assert values[0] == trajectory.times_s[index]
        np.testing.assert_array_equal(values[1:], trajectory.states[index])
    environment = _rows(target / "environment.csv")[1:]
    for index in (0, len(environment) // 2):
        values = np.asarray([float(value) for value in environment[index]])
        assert values[0] == trajectory.times_s[index]


def test_metadata_csv_records_the_configuration(coupled, tmp_path):
    document, trajectory = coupled
    target = tmp_path / "analysis"
    trajectory.analyze().export_csv(target)
    metadata = dict((row[0], row[1]) for row in _rows(target / "metadata.csv")[1:])
    assert metadata["schema_version"] == "1"
    assert float(metadata["initial_epoch_et_seconds"]) == 0.0
    assert float(metadata["mass_kg"]) == pytest.approx(
        scenario_spacecraft(document).mass_kg)
    assert int(metadata["surface_count"]) == len(
        scenario_spacecraft(document).surfaces)
    assert float(metadata["earth_mu_m3_s2"]) == leo.Earth().mu_m3_s2
    assert metadata["epoch_scale"] == "TDB_seconds_past_J2000"


def test_trajectory_to_csv_uses_the_native_trajectory_schema(coupled, tmp_path):
    _, trajectory = coupled
    target = tmp_path / "trajectory"
    trajectory.to_csv(target)
    candidates = [target, target / "trajectory.csv"]
    written = [path for path in candidates if path.is_file()]
    assert written, f"no trajectory CSV written for {target}"
    rows = _rows(written[0])
    assert rows[0] == ["time_s", "x_m", "y_m", "z_m", "vx_m_s", "vy_m_s", "vz_m_s",
                      "qw", "qx", "qy", "qz", "omega_x_rad_s",
                      "omega_y_rad_s", "omega_z_rad_s"]
    assert len(rows) == len(trajectory.times_s) + 1
    first = np.asarray([float(value) for value in rows[1]])
    assert first[0] == trajectory.times_s[0]
    np.testing.assert_array_equal(first[1:], trajectory.states[0])


def test_export_failures_raise_oserror(coupled, tmp_path):
    _, trajectory = coupled
    blocker = tmp_path / "blocker"
    blocker.write_text("not a directory")
    with pytest.raises(OSError):
        trajectory.analyze().export_csv(blocker / "nested")
    with pytest.raises(OSError):
        trajectory.to_csv(blocker / "nested.csv")


def test_export_rejects_a_non_directory_target(coupled, tmp_path):
    _, trajectory = coupled
    target = tmp_path / "analysis"
    target.mkdir()
    (target / "trajectory.csv").mkdir()      # not a regular file
    with pytest.raises(OSError):
        trajectory.analyze().export_csv(target)


def test_analysis_requires_a_trajectory_context():
    """Every trajectory handed back by the API can be analysed."""
    trajectory = _propagate("coupled_case_a")
    analysis = trajectory.analyze()
    assert analysis.orbital.shape == (len(trajectory.times_s),)