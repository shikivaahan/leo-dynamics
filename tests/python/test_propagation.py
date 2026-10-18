"""Single-trajectory propagation through the public API.

Every expectation comes from the frozen qualified goldens or from analytic
two-body relationships; this file checks that the Python facade reaches the same
physics for each required model class, at the frozen sample schedules.
"""

from __future__ import annotations

import math

import numpy as np
import pytest

import leo_dynamics as leo
from conftest import (
    SCENARIO_NAMES,
    golden_states,
    load_scenario,
    propagate_scenario,
    reference_helpers,
    scenario_arguments,
    scenario_craft,
    scenario_state,
    truncate,
    thresholds,
)


@pytest.fixture(scope="module")
def trajectories():
    """One propagation per qualified scenario, computed once for this module."""
    return {name: propagate_scenario(name) for name in SCENARIO_NAMES}




@pytest.mark.parametrize("name", SCENARIO_NAMES)
def test_shapes_and_dtypes(name, trajectories):
    trajectory = trajectories[name]
    count = len(load_scenario(name)["run"]["sample_times_s"])
    states = trajectory.states
    assert states.shape == (count, 13)
    assert states.dtype == np.float64
    assert states.flags["C_CONTIGUOUS"]
    assert trajectory.position_m.shape == (count, 3)
    assert trajectory.velocity_m_s.shape == (count, 3)
    assert trajectory.quaternion_wxyz.shape == (count, 4)
    assert trajectory.omega_body_rad_s.shape == (count, 3)
    assert trajectory.times_s.shape == (count,)
    assert trajectory.times_s.dtype == np.float64
    assert trajectory.epoch_et_s == 0.0
    # Views into one owning buffer, not duplicate allocations.
    np.testing.assert_array_equal(trajectory.position_m, states[:, :3])
    np.testing.assert_array_equal(trajectory.velocity_m_s, states[:, 3:6])
    np.testing.assert_array_equal(trajectory.quaternion_wxyz, states[:, 6:10])
    np.testing.assert_array_equal(trajectory.omega_body_rad_s, states[:, 10:13])
    for array in (
        states,
        trajectory.position_m,
        trajectory.velocity_m_s,
        trajectory.quaternion_wxyz,
        trajectory.omega_body_rad_s,
        trajectory.times_s,
    ):
        assert array.flags["WRITEABLE"] is False
        assert array.dtype.byteorder in ("=", "|")  # native-endian
        assert np.isfinite(array).all()


@pytest.mark.parametrize("name", SCENARIO_NAMES)
def test_sample_times_are_preserved_exactly(name, trajectories):
    requested = np.asarray(
        load_scenario(name)["run"]["sample_times_s"], dtype=np.float64
    )
    np.testing.assert_array_equal(trajectories[name].times_s, requested)


@pytest.mark.parametrize("name", SCENARIO_NAMES)
def test_matches_frozen_independent_reference(name, trajectories):
    """The Python path must stay inside the frozen acceptance limits."""
    _, expected = golden_states(name)
    actual = trajectories[name].states
    assert actual.shape == expected.shape
    error = reference_helpers.errors(actual, expected)
    limits = thresholds(name)
    for field, value in error.items():
        assert value <= limits[field], f"{name}: {field} {value} > {limits[field]}"




def test_explicit_earth_is_used():
    """A point-mass-only run is insensitive to radius and to a zero J2."""
    document = truncate(load_scenario("two_body_circular"))
    arguments = scenario_arguments(document)
    default = leo.propagate(**arguments)
    radius_only = leo.Earth(
        mu_m3_s2=leo.Earth().mu_m3_s2,
        equatorial_radius_m=1.0e7,
        j2=leo.Earth().j2,
    )
    arguments["earth"] = radius_only
    changed_radius = leo.propagate(**arguments)
    np.testing.assert_array_equal(changed_radius.states, default.states)
    arguments["earth"] = leo.Earth(j2=0.0)
    np.testing.assert_array_equal(leo.propagate(**arguments).states, default.states)
    arguments["earth"] = leo.Earth(mu_m3_s2=2.0 * leo.Earth().mu_m3_s2)
    stronger = leo.propagate(**arguments)
    assert np.linalg.norm(stronger.states[-1, :3] - default.states[-1, :3]) > 1.0


def test_circular_two_body_closes_one_analytic_period():
    """Independent analytic check: a circular orbit returns to its start."""
    document = load_scenario("two_body_circular")
    state = scenario_state(document)
    radius = float(np.linalg.norm(state.position_m))
    speed = float(np.linalg.norm(state.velocity_m_s))
    mu = leo.Earth().mu_m3_s2
    semi_major = 1.0 / (2.0 / radius - speed * speed / mu)
    period = 2.0 * math.pi * math.sqrt(semi_major**3 / mu)
    arguments = scenario_arguments(document)
    arguments["times_s"] = np.asarray([0.0, period / 2.0, period])
    arguments["options"] = leo.SolverOptions(
        rtol=1e-12, max_step_s=5.0, position_atol_m=1e-6, velocity_atol_m_s=1e-9
    )
    trajectory = leo.propagate(**arguments)
    start, half, end = trajectory.states
    limits = thresholds("two_body_circular")
    # Half a period inverts the radius vector.
    assert np.linalg.norm(half[:3] + start[:3]) <= limits["position_m"]
    # One period returns to the initial state.
    assert np.linalg.norm(end[:3] - start[:3]) <= limits["position_m"]
    assert np.linalg.norm(end[3:6] - start[3:6]) <= limits["velocity_m_s"]


def test_torque_free_top_stays_constant():
    """An inertia-symmetric body with no torque keeps its body-frame rate."""
    document = load_scenario("torque_free_spherical")
    trajectory = propagate_scenario("torque_free_spherical")
    omegas = trajectory.omega_body_rad_s
    np.testing.assert_allclose(omegas, np.broadcast_to(omegas[0], omegas.shape), rtol=0.0, atol=1e-15)
    quaternions = trajectory.quaternion_wxyz
    norms = np.linalg.norm(quaternions, axis=1)
    assert np.allclose(norms, 1.0, rtol=0.0, atol=1e-12)


def test_solver_options_are_immutable_and_owned():
    options = leo.SolverOptions()
    with pytest.raises((AttributeError, TypeError)):
        options.rtol = 1e-3




@pytest.mark.parametrize(
    "times",
    [
        [1.0, 2.0, 3.0],          # does not start at zero
        [0.0, 0.0, 1.0],          # not strictly increasing
        [0.0, 2.0, 1.0],          # decreasing
        [],                       # empty schedule
        [[0.0, 1.0]],             # not one-dimensional
        [0.0, 1.0, np.nan],       # nonfinite
        [0.0, 1.0, np.inf],       # nonfinite
    ],
)
def test_invalid_schedules_are_rejected(times):
    document = truncate(load_scenario("two_body_circular"))
    arguments = scenario_arguments(document)
    arguments["times_s"] = np.asarray(times, dtype=np.float64)
    with pytest.raises(ValueError):
        leo.propagate(**arguments)


@pytest.mark.parametrize(
    "options",
    [
        dict(rtol=0.0),
        dict(position_atol_m=-1.0),
        dict(velocity_atol_m_s=0.0),
        dict(quaternion_atol=0.0),
        dict(omega_atol_rad_s=0.0),
        dict(initial_step_s=0.0),
        dict(max_step_s=0.5),          # below the initial step
        dict(max_attempts=0),
        dict(rtol=float("nan")),
    ],
)
def test_solver_contract_violations_are_rejected(options):
    with pytest.raises(ValueError):
        leo.SolverOptions(**options)


def test_integration_attempt_limit_is_a_runtime_error():
    """A finite attempt budget cannot be exhausted silently."""
    document = truncate(load_scenario("two_body_circular"))
    arguments = scenario_arguments(document)
    arguments["options"] = leo.SolverOptions(max_attempts=1, max_step_s=5.0)
    arguments["times_s"] = np.asarray([0.0, 1.0, 2.0, 3.0])
    with pytest.raises(RuntimeError):
        leo.propagate(**arguments)


def test_epoch_et_s_is_reported():
    document = truncate(load_scenario("two_body_circular"))
    arguments = scenario_arguments(document)
    arguments["epoch_et_s"] = 0.0
    assert leo.propagate(**arguments).epoch_et_s == 0.0


def test_non_zero_epoch_is_reported_and_reaches_the_environment():
    document = truncate(load_scenario("srp"))
    arguments = scenario_arguments(document)
    arguments["epoch_et_s"] = 7.5e8
    trajectory = leo.propagate(**arguments)
    assert trajectory.epoch_et_s == 7.5e8
    records = trajectory.analyze().environment
    np.testing.assert_array_equal(
        records["epoch_et_s"], 7.5e8 + trajectory.times_s
    )


def test_propagate_is_keyword_only():
    document = truncate(load_scenario("two_body_circular"))
    arguments = scenario_arguments(document)
    with pytest.raises(TypeError):
        leo.propagate(arguments["initial"], arguments["spacecraft"],
                      arguments["model"], arguments["times_s"])