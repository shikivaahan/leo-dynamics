"""Pointwise diagnostics: ``leo.evaluate_dynamics`` against frozen references.

The frozen pointwise goldens hold the independent accelerations, loads and
complete 13-component right-hand side at fixed instants for all 17 qualified
scenarios. They are the strongest available check that the Python-exposed
``DynamicsContributions`` reach the same native values, and they are consumed
with the frozen acceptance limits - never with a relaxed tolerance.
"""

from __future__ import annotations

import json
import numpy as np
import pytest

import leo_dynamics as leo
from conftest import (
    GOLDEN,
    SCENARIO_NAMES,
    assert_identical,
    diagnostics_arguments,
    harness_samples,
    load_scenario,
    pointwise_failures,
    pointwise_rows,
    run_harness,
    scenario_environment,
    scenario_model,
    scenario_options,
    scenario_spacecraft,
    scenario_state,
)

MASS_KEYS = ("point_mass_m_s2", "j2_m_s2")


def _loads(document: dict, time_s: float, state=None):
    arguments = diagnostics_arguments(document, time_s)
    if state is not None:
        arguments["state"] = leo.State.from_array(state)
    return leo.evaluate_dynamics(**arguments)


def _as_record(loads, time_s: float) -> dict:
    record = {
        "t": float(time_s),
        "rhs": np.asarray(loads.state_derivative, dtype=np.float64),
        "point_mass_m_s2": np.asarray(loads.point_mass_m_s2, dtype=np.float64),
        "j2_m_s2": np.asarray(loads.j2_m_s2, dtype=np.float64),
        "gravity_gradient_nm": np.asarray(loads.gravity_gradient_nm, dtype=np.float64),
        "srp_force_n": np.asarray(loads.srp.force_n, dtype=np.float64),
        "srp_torque_nm": np.asarray(loads.srp.torque_nm, dtype=np.float64),
        "drag_force_n": np.asarray(loads.drag.force_n, dtype=np.float64),
        "drag_torque_nm": np.asarray(loads.drag.torque_nm, dtype=np.float64),
        "albedo_force_n": np.asarray(loads.albedo.force_n, dtype=np.float64),
        "albedo_torque_nm": np.asarray(loads.albedo.torque_nm, dtype=np.float64),
        "total_acceleration_m_s2": np.asarray(loads.total_acceleration_m_s2,
                                              dtype=np.float64),
        "total_angular_acceleration_rad_s2": np.asarray(
            loads.total_angular_acceleration_rad_s2, dtype=np.float64),
    }
    for optional in ("illumination", "density_kg_m3"):
        value = getattr(loads, optional, None)
        if value is not None:
            record[optional] = float(np.asarray(value))
    return record


# --------------------------------------------------------------------------
# Frozen independent pointwise goldens
# --------------------------------------------------------------------------

@pytest.mark.parametrize("name", SCENARIO_NAMES)
def test_pointwise_diagnostics_match_the_frozen_reference(name):
    document = load_scenario(name)
    failures = []
    for row in pointwise_rows(name):
        record = _as_record(_loads(document, row["t"], row["state"]), row["t"])
        failures.extend(
            dict(entry, label=f"{name} t={row['t']}g")
            for entry in pointwise_failures(record, row)
        )
    assert not failures, f"pointwise reference failures: {failures}"


def test_absolute_epoch_and_elapsed_time_are_separate():
    """``time_s`` is elapsed; ``epoch_et_s`` is absolute TDB past J2000."""
    document = load_scenario("srp")
    shift = 1000.0
    au = 1.495978707e11
    position = np.asarray([[au, 0.0, 0.0], [1.01 * au, 0.0, 0.0],
                           [1.02 * au, 0.0, 0.0]])
    arguments = diagnostics_arguments(document, 0.0)
    arguments["environment"] = leo.Environment(sun=leo.SunEphemeris.tabulated(
        et_seconds=np.asarray([0.0, 300.0, 600.0]), position_m=position,
        velocity_m_s=np.zeros((3, 3))))
    at_zero = _as_record(leo.evaluate_dynamics(**arguments), 0.0)
    arguments["environment"] = leo.Environment(sun=leo.SunEphemeris.tabulated(
        et_seconds=np.asarray([shift, shift + 300.0, shift + 600.0]),
        position_m=position, velocity_m_s=np.zeros((3, 3))))
    arguments["epoch_et_s"] = shift
    at_shift = _as_record(leo.evaluate_dynamics(**arguments), 0.0)
    # The same absolute Sun state is selected through epoch and elapsed time.
    assert_identical(at_zero["srp_force_n"], at_shift["srp_force_n"], "srp_force_n")
    # Elapsed time still advances inside one absolute epoch.
    arguments["time_s"] = 300.0
    later = _as_record(leo.evaluate_dynamics(**arguments), 300.0)
    assert np.linalg.norm(later["srp_force_n"]) < .99 * np.linalg.norm(at_shift["srp_force_n"])


# --------------------------------------------------------------------------
# Direct C++ parity for the same evaluations
# --------------------------------------------------------------------------

@pytest.mark.parametrize("name", SCENARIO_NAMES)
def test_pointwise_diagnostics_match_the_direct_cpp_path(name, harness_binary, tmp_path):
    document = load_scenario(name)
    # State construction applies native quaternion validation/projection. Give the
    # direct C++ evaluator those identical validated inputs, not raw oracle rows.
    references = pointwise_rows(name)
    points = tmp_path / "validated-points.json"
    points.write_text(json.dumps({"rows": [
        {**row, "state": leo.State.from_array(row["state"]).to_array().tolist()}
        for row in references
    ]}))
    native = run_harness(harness_binary, document,
                         extra=["--points-only"], points=points)
    rows = native["pointwise"]
    assert len(rows) == len(pointwise_rows(name))
    for reference, native_row in zip(pointwise_rows(name), rows, strict=True):
        record = _as_record(_loads(document, reference["t"], reference["state"]), reference["t"])
        assert_identical(record["rhs"], np.asarray(native_row["rhs"]),
                         f"{name}@{reference['t']} rhs")
        for field in (*MASS_KEYS, "gravity_gradient_nm", "total_acceleration_m_s2",
                      "total_angular_acceleration_rad_s2"):
            assert_identical(record[field], np.asarray(native_row[field]),
                             f"{name}@{reference['t']} {field}")
        for name_ in ("srp", "drag", "albedo"):
            for component in ("force_n", "torque_nm"):
                field = f"{name_}_{component}"
                assert_identical(record[field], np.asarray(native_row[field]),
                                 f"{name}@{reference['t']} {field}")


def test_native_parity_of_a_whole_trajectory(harness_binary):
    """The Python path reproduces the direct C++ trajectory exactly."""
    document = load_scenario("case_c_short")
    native = run_harness(harness_binary, document)
    trajectory = leo.propagate(
        initial=scenario_state(document),
        spacecraft=scenario_spacecraft(document),
        model=scenario_model(document),
        times_s=np.asarray(document["run"]["sample_times_s"], dtype=np.float64),
        environment=scenario_environment(document),
        options=scenario_options(document),
    )
    assert_identical(trajectory.states, harness_samples(native), "case_c_short")
    np.testing.assert_array_equal(
        trajectory.times_s,
        np.asarray([row["t"] for row in native["samples"]], dtype=np.float64),
    )


# --------------------------------------------------------------------------
# Composition of the exposed contributions
# --------------------------------------------------------------------------

def test_total_acceleration_is_the_sum_of_the_reported_parts():
    document = load_scenario("case_c_short")
    loads = _loads(document, 300.0)
    mass = scenario_spacecraft(document).mass_kg
    expected = (np.asarray(loads.point_mass_m_s2)
                + np.asarray(loads.j2_m_s2)
                + np.asarray(loads.non_gravitational.force_n) / mass)
    np.testing.assert_array_equal(np.asarray(loads.total_acceleration_m_s2),
                                  expected)


def test_total_torque_includes_gravity_gradient():
    document = load_scenario("case_c_short")
    loads = _loads(document, 300.0)
    expected = (np.asarray(loads.srp.torque_nm) + np.asarray(loads.drag.torque_nm)
                + np.asarray(loads.albedo.torque_nm)
                + np.asarray(loads.gravity_gradient_nm))
    np.testing.assert_array_equal(np.asarray(loads.non_gravitational.torque_nm),
                                  expected)


def test_angular_acceleration_is_the_inertial_torque_response():
    document = load_scenario("gravity_gradient")
    loads = _loads(document, 600.0)
    state = scenario_state(document)
    inertia = np.asarray(scenario_spacecraft(document).inertia_kg_m2)
    omega = np.asarray(state.omega_body_rad_s)
    torque = np.asarray(loads.non_gravitational.torque_nm)
    expected = np.linalg.solve(inertia, torque - np.cross(omega, inertia @ omega))
    np.testing.assert_allclose(
        np.asarray(loads.total_angular_acceleration_rad_s2), expected, rtol=1e-9,
        atol=1e-18,
    )


def test_state_derivative_packing_is_consistent():
    document = load_scenario("case_c_short")
    loads = _loads(document, 0.0)
    derivative = np.asarray(loads.state_derivative)
    assert derivative.shape == (13,)
    state = scenario_state(document)
    np.testing.assert_array_equal(derivative[0:3], np.asarray(state.velocity_m_s))
    np.testing.assert_array_equal(derivative[3:6],
                                  np.asarray(loads.total_acceleration_m_s2))
    np.testing.assert_array_equal(derivative[10:13],
                                  np.asarray(loads.total_angular_acceleration_rad_s2))


def test_contributions_view_returns_the_same_values():
    document = load_scenario("case_c_short")
    loads = _loads(document, 0.0)
    contributions = loads.contributions
    np.testing.assert_array_equal(np.asarray(contributions.total_acceleration_m_s2),
                                  np.asarray(loads.total_acceleration_m_s2))
    np.testing.assert_array_equal(np.asarray(contributions.srp.force_n),
                                  np.asarray(loads.srp.force_n))


# --------------------------------------------------------------------------
# Independent force and torque switches
# --------------------------------------------------------------------------

def test_disabled_models_report_exact_zeros():
    document = load_scenario("two_body_circular")
    loads = _loads(document, 0.0)
    assert np.all(np.asarray(loads.srp.force_n) == 0.0)
    assert np.all(np.asarray(loads.srp.torque_nm) == 0.0)
    assert np.all(np.asarray(loads.drag.force_n) == 0.0)
    assert np.all(np.asarray(loads.drag.torque_nm) == 0.0)
    assert np.all(np.asarray(loads.albedo.force_n) == 0.0)
    assert np.all(np.asarray(loads.albedo.torque_nm) == 0.0)
    assert np.all(np.asarray(loads.gravity_gradient_nm) == 0.0)
    assert np.all(np.asarray(loads.j2_m_s2) == 0.0)


@pytest.mark.parametrize("force,torque", [(True, False), (False, True)])
def test_force_and_torque_switches_are_independent(force, torque):
    document = load_scenario("srp")
    arguments = diagnostics_arguments(document, 0.0)
    arguments["model"] = leo.DynamicsModel(
        srp=leo.Contribution(force=force, torque=torque))
    loads = leo.evaluate_dynamics(**arguments)
    assert bool(np.any(np.asarray(loads.srp.force_n) != 0.0)) is force
    assert bool(np.any(np.asarray(loads.srp.torque_nm) != 0.0)) is torque
    np.testing.assert_array_equal(np.asarray(loads.drag.force_n), np.zeros(3))


def test_gravity_gradient_switch_is_independent():
    document = load_scenario("gravity_gradient")
    arguments = diagnostics_arguments(document, 0.0)
    arguments["model"] = leo.DynamicsModel(gravity_gradient=True)
    enabled = leo.evaluate_dynamics(**arguments)
    arguments["model"] = leo.DynamicsModel(gravity_gradient=False)
    disabled = leo.evaluate_dynamics(**arguments)
    assert np.any(np.asarray(disabled.gravity_gradient_nm) == 0.0)
    assert np.any(np.asarray(enabled.gravity_gradient_nm) != 0.0)
    np.testing.assert_array_equal(
        np.asarray(enabled.point_mass_m_s2),
        np.asarray(disabled.point_mass_m_s2))


# --------------------------------------------------------------------------
# Input validation
# --------------------------------------------------------------------------

@pytest.mark.parametrize("time_s", [float("nan"), float("inf")])
def test_nonfinite_time_is_rejected(time_s):
    document = load_scenario("two_body_circular")
    with pytest.raises(ValueError):
        _loads(document, time_s)


@pytest.mark.parametrize(
    "position,velocity",
    [
        ([float("nan"), 0.0, 0.0], [0.0, 7500.0, 0.0]),
        ([0.0, 0.0, 0.0], [0.0, 7500.0, 0.0]),
        ([7.0e6, 0.0, 0.0], [0.0, float("inf"), 0.0]),
    ],
)
def test_invalid_states_are_rejected(position, velocity):
    document = load_scenario("two_body_circular")
    arguments = diagnostics_arguments(document, 0.0)
    with pytest.raises(ValueError):
        arguments["state"] = leo.State(position_m=position, velocity_m_s=velocity)
        leo.evaluate_dynamics(**arguments)