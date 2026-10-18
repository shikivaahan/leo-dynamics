"""Environment contracts: Sun acquisition, atmosphere, eclipse and coverage.

Every expectation is either an exact identity implied by the native model
(linear interpolation of linear samples, density scaling, shadow factors) or a
rejection required by the documented contract. No force law is re-implemented.
"""

from __future__ import annotations

import numpy as np
import pytest

import leo_dynamics as leo
from conftest import (
    diagnostics_arguments,
    load_scenario,
    scenario_environment,
    scenario_model,
    scenario_options,
    scenario_spacecraft,
    scenario_state,
    truncate,
)

AU_M = 1.495978707e11


def _document(name: str, light: bool = True) -> dict:
    document = load_scenario(name)
    return truncate(document) if light else document


def _times(document: dict) -> np.ndarray:
    return np.asarray(document["run"]["sample_times_s"], dtype=np.float64)


def _propagate(document: dict, environment=None, times=None, state=None,
               model=None, options=None) -> leo.Trajectory:
    return leo.propagate(
        initial=state if state is not None else scenario_state(document),
        spacecraft=scenario_spacecraft(document),
        model=model if model is not None else scenario_model(document),
        times_s=_times(document) if times is None else times,
        environment=environment,
        options=options if options is not None else scenario_options(document),
    )


def _diagnostics(document: dict, time_s: float = 0.0, environment=None,
                 model=None, state=None):
    arguments = diagnostics_arguments(document, time_s)
    if environment is not None:
        arguments["environment"] = environment
    if model is not None:
        arguments["model"] = model
    if state is not None:
        arguments["state"] = state
    return leo.evaluate_dynamics(**arguments)


def _drag_document() -> dict:
    return _document("drag_flat_plate")


def _radiation_document() -> dict:
    return _document("srp")


# --------------------------------------------------------------------------
# Missing acquisition is rejected at the Python boundary
# --------------------------------------------------------------------------

@pytest.mark.parametrize(
    "model",
    [
        leo.DynamicsModel(srp=leo.Contribution(force=True, torque=True)),
        leo.DynamicsModel(srp=leo.Contribution(force=True, torque=False)),
        leo.DynamicsModel(srp=leo.Contribution(force=False, torque=True)),
        leo.DynamicsModel(albedo=leo.Contribution(force=True, torque=True)),
        leo.DynamicsModel(albedo=leo.Contribution(force=False, torque=True)),
    ],
)
def test_radiation_without_a_sun_is_rejected(model):
    document = _radiation_document()
    with pytest.raises(ValueError):
        _propagate(document, environment=leo.Environment(), model=model)
    with pytest.raises(ValueError):
        _propagate(document, environment=None, model=model)
    with pytest.raises(ValueError):
        _diagnostics(document, environment=leo.Environment(), model=model)


@pytest.mark.parametrize(
    "model",
    [
        leo.DynamicsModel(drag=leo.Contribution(force=True, torque=True),
                          drag_law="flat_plate"),
        leo.DynamicsModel(drag=leo.Contribution(force=True, torque=False),
                          drag_law="flat_plate"),
        leo.DynamicsModel(drag=leo.Contribution(force=False, torque=True),
                          drag_law="sentman"),
    ],
)
def test_drag_without_atmospheric_acquisition_is_rejected(model):
    document = _drag_document()
    with pytest.raises(ValueError):
        _propagate(document, environment=leo.Environment(), model=model)
    with pytest.raises(ValueError):
        _propagate(
            document,
            environment=leo.Environment(
                sun=leo.SunEphemeris.constant([AU_M, 0.0, 0.0])
            ),
            model=model,
        )


def test_gravity_only_needs_no_environment():
    document = _document("two_body_circular")
    trajectory = _propagate(document, environment=None,
                            model=leo.DynamicsModel())
    assert trajectory.states.shape == (_times(document).size, 13)
    # An unused environment is simply not consulted.
    unused = _propagate(document, environment=leo.Environment(),
                        model=leo.DynamicsModel())
    assert np.array_equal(unused.states.view(np.uint64),
                          trajectory.states.view(np.uint64))


# --------------------------------------------------------------------------
# Explicit zero-density vacuum
# --------------------------------------------------------------------------

def test_explicit_zero_density_vacuum_is_supported():
    document = _drag_document()
    drag_model = scenario_model(document)
    vacuum = leo.Environment(
        sun=leo.SunEphemeris.constant([AU_M, 0.0, 0.0]),
        atmosphere=leo.ExponentialAtmosphere(scale=0.0),
    )
    trajectory = _propagate(document, environment=vacuum, model=drag_model)
    records = trajectory.analyze().environment
    assert np.all(records["density_kg_m3"] == 0.0)
    assert np.all(records["drag_force_n"] == 0.0)
    assert np.all(records["drag_torque_nm"] == 0.0)
    loads = _diagnostics(document, 120.0, environment=vacuum, model=drag_model)
    np.testing.assert_array_equal(loads.drag.force_n, np.zeros(3))
    np.testing.assert_array_equal(loads.drag.torque_nm, np.zeros(3))
    # A vacuum with drag selected is exactly the drag-disabled trajectory.
    without_drag = leo.DynamicsModel()
    reference = _propagate(document, environment=None, model=without_drag)
    assert np.array_equal(trajectory.states.view(np.uint64),
                          reference.states.view(np.uint64))


def test_density_scale_scales_density_and_drag_linearly():
    document = _drag_document()
    drag_model = scenario_model(document)
    sun = leo.SunEphemeris.constant([AU_M, 0.0, 0.0])
    single = leo.Environment(sun=sun, atmosphere=leo.ExponentialAtmosphere(scale=1.0))
    double = leo.Environment(sun=sun, atmosphere=leo.ExponentialAtmosphere(scale=2.0))
    first = _diagnostics(document, 60.0, environment=single, model=drag_model)
    second = _diagnostics(document, 60.0, environment=double, model=drag_model)
    np.testing.assert_allclose(second.drag.force_n, 2.0 * first.drag.force_n,
                               rtol=1e-14)
    altitude = np.linalg.norm(scenario_state(document).position_m) - leo.Earth().equatorial_radius_m
    assert double.density_kg_m3(altitude) == 2 * single.density_kg_m3(altitude)
    np.testing.assert_allclose(second.drag.torque_nm, 2.0 * first.drag.torque_nm, rtol=1e-14, atol=0)


def test_altitude_outside_the_atmosphere_model_is_a_domain_error():
    document = _drag_document()
    state = leo.State(
        position_m=np.asarray([6.3e6, 0.0, 0.0]),
        velocity_m_s=np.asarray([0.0, 7500.0, 0.0]),
        quaternion_wxyz=(1.0, 0.0, 0.0, 0.0),
    )
    with pytest.raises(leo.DomainError):
        _propagate(document, environment=scenario_environment(document),
                   state=state)


# --------------------------------------------------------------------------
# Constant and tabulated Sun
# --------------------------------------------------------------------------

def test_tabulated_constant_table_matches_the_constant_provider():
    document = _radiation_document()
    times = _times(document)
    constant = leo.SunEphemeris.constant([AU_M, 0.0, 0.0])
    epochs = np.asarray([0.0, 600.0, 1200.0])
    tabulated = leo.SunEphemeris.tabulated(
        et_seconds=epochs,
        position_m=np.tile(np.asarray([AU_M, 0.0, 0.0]), (3, 1)),
        velocity_m_s=np.zeros((3, 3)),
    )
    first = _propagate(document, leo.Environment(sun=constant))
    second = _propagate(document, leo.Environment(sun=tabulated))
    assert np.array_equal(first.states.view(np.uint64), second.states.view(np.uint64))


def test_tabulated_endpoints_are_returned_exactly():
    document = _radiation_document()
    epochs = np.asarray([0.0, 300.0, 600.0])
    position = np.asarray([[AU_M, 0.0, 0.0], [1.1 * AU_M, 0.0, 0.0],
                           [1.2 * AU_M, 0.0, 0.0]])
    velocity = np.zeros((3, 3))
    tabulated = leo.Environment(sun=leo.SunEphemeris.tabulated(
        et_seconds=epochs, position_m=position, velocity_m_s=velocity))
    at_start = _diagnostics(document, 0.0, environment=tabulated)
    at_end = _diagnostics(document, 600.0, environment=tabulated)
    reference_start = _diagnostics(
        document, 0.0,
        environment=leo.Environment(sun=leo.SunEphemeris.constant([AU_M, 0.0, 0.0])))
    reference_end = _diagnostics(
        document, 600.0,
        environment=leo.Environment(sun=leo.SunEphemeris.constant([1.2 * AU_M, 0.0, 0.0])))
    np.testing.assert_array_equal(at_start.srp.force_n, reference_start.srp.force_n)
    np.testing.assert_array_equal(at_end.srp.force_n, reference_end.srp.force_n)


def test_tabulated_linear_motion_is_interpolated_exactly():
    """Hermite interpolation of linear samples reproduces the line."""
    document = load_scenario("srp")
    times = _times(document)
    epochs = np.arange(0.0, times[-1] + 300.0, 300.0)
    velocity = np.asarray([0.0, 1000.0, 0.0])
    position = np.asarray([AU_M, 0.0, 0.0])[None, :] + \
        np.outer(epochs, velocity)
    environment = leo.Environment(sun=leo.SunEphemeris.tabulated(
        et_seconds=epochs, position_m=position,
        velocity_m_s=np.tile(velocity, (epochs.size, 1))))
    records = _propagate(document, environment).analyze().environment
    expected = np.asarray([AU_M, 0.0, 0.0])[None, :] + np.outer(times, velocity)
    np.testing.assert_allclose(records["sun_position_m"], expected, rtol=0.0,
                               atol=1.0e-2)
    constant = _propagate(
        document, leo.Environment(sun=leo.SunEphemeris.constant([AU_M, 0.0, 0.0])))
    assert not np.allclose(records["sun_position_m"][-1],
                           constant.analyze().environment["sun_position_m"][-1])


def test_tabulated_coverage_errors_are_reported():
    document = _document("case_c_short")
    epochs = np.asarray([0.0, 100.0, 200.0])
    environment = leo.Environment(
        sun=leo.SunEphemeris.tabulated(
            et_seconds=epochs,
            position_m=np.tile(np.asarray([AU_M, 0.0, 0.0]), (3, 1)),
            velocity_m_s=np.zeros((3, 3))),
        atmosphere=leo.ExponentialAtmosphere(scale=1.0),
    )
    with pytest.raises(leo.DomainError):
        _propagate(document, environment)
    with pytest.raises(leo.DomainError):
        _diagnostics(document, 500.0, environment=environment)
    # A window inside the coverage is accepted.
    inside = leo.propagate(
        initial=scenario_state(document), spacecraft=scenario_spacecraft(document),
        model=scenario_model(document),
        times_s=np.asarray([0.0, 100.0, 200.0]),
        environment=environment, options=scenario_options(document),
    )
    assert inside.states.shape == (3, 13)


def test_tabulated_coverage_respects_the_epoch():
    document = _document("srp")
    environment = leo.Environment(sun=leo.SunEphemeris.tabulated(
        et_seconds=np.asarray([0.0, 100.0, 200.0]),
        position_m=np.tile(np.asarray([AU_M, 0.0, 0.0]), (3, 1)),
        velocity_m_s=np.zeros((3, 3))))
    with pytest.raises(leo.DomainError):
        leo.propagate(
            initial=scenario_state(document),
            spacecraft=scenario_spacecraft(document),
            model=scenario_model(document), times_s=np.asarray([0.0, 1.0]),
            environment=environment, epoch_et_s=500.0,
            options=scenario_options(document))


@pytest.mark.parametrize(
    "kwargs",
    [
        dict(et_seconds=np.asarray([0.0, 100.0, 100.0]),
             position_m=np.tile([AU_M, 0.0, 0.0], (3, 1)),
             velocity_m_s=np.zeros((3, 3))),                       # not increasing
        dict(et_seconds=np.asarray([100.0, 0.0, 200.0]),
             position_m=np.tile([AU_M, 0.0, 0.0], (3, 1)),
             velocity_m_s=np.zeros((3, 3))),                       # unordered
        dict(et_seconds=np.asarray([0.0, 100.0, 200.0]),
             position_m=np.tile([AU_M, 0.0, 0.0], (2, 1)),
             velocity_m_s=np.zeros((3, 3))),                       # shape mismatch
        dict(et_seconds=np.asarray([0.0]),
             position_m=np.asarray([[AU_M, 0.0, 0.0]]),
             velocity_m_s=np.zeros((1, 3))),                       # single sample
        dict(et_seconds=np.asarray([0.0, np.nan, 200.0]),
             position_m=np.tile([AU_M, 0.0, 0.0], (3, 1)),
             velocity_m_s=np.zeros((3, 3))),                       # non-finite epoch
        dict(et_seconds=np.asarray([0.0, 100.0, 200.0]),
             position_m=np.tile([AU_M, 0.0, np.inf], (3, 1)),
             velocity_m_s=np.zeros((3, 3))),                       # non-finite sample
        dict(et_seconds=np.asarray([0.0, 100.0, 200.0]),
             position_m=np.zeros((3, 3)),                          # Sun at the origin
             velocity_m_s=np.zeros((3, 3))),
        dict(et_seconds=np.asarray([0.0, 100.0]),
             position_m=np.asarray([[AU_M, 0.0], [1.0, 2.0]]),       # wrong width
             velocity_m_s=np.zeros((2, 3))),
        dict(et_seconds=np.asarray([[0.0, 100.0]]),                 # not one-dimensional
             position_m=np.tile([AU_M, 0.0, 0.0], (1, 1)),
             velocity_m_s=np.zeros((1, 3))),
    ],
)
def test_invalid_tabulated_sun_is_rejected(kwargs):
    with pytest.raises(ValueError):
        leo.SunEphemeris.tabulated(**kwargs)


# --------------------------------------------------------------------------
# Eclipse geometry
# --------------------------------------------------------------------------

def _eclipse_records(model_name: str, eclipse: str) -> np.ndarray:
    document = load_scenario(model_name)
    environment = leo.Environment(
        sun=leo.SunEphemeris.constant(document["environment"]["sun_position_m"]),
        eclipse=eclipse,
    )
    return _propagate(document, environment,
                      model=scenario_model(document)).analyze().environment[
        "illumination"]


def test_eclipse_models_produce_different_shadow_histories():
    none = _eclipse_records("eclipse_conical", "none")
    conical = _eclipse_records("eclipse_conical", "conical")
    cylindrical = _eclipse_records("eclipse_conical", "cylindrical")
    assert np.all(none == 1.0)
    assert np.all((conical >= 0.0) & (conical <= 1.0))
    assert np.all((cylindrical >= 0.0) & (cylindrical <= 1.0))
    assert conical.min() < 1.0
    assert cylindrical.min() < 1.0
    assert not np.array_equal(conical, cylindrical)


def test_default_eclipse_model_is_conical():
    document = load_scenario("eclipse_conical")
    sun = leo.SunEphemeris.constant(document["environment"]["sun_position_m"])
    default = _propagate(document, leo.Environment(sun=sun)).analyze().environment[
        "illumination"]
    conical = _propagate(
        document, leo.Environment(sun=sun, eclipse="conical")
    ).analyze().environment["illumination"]
    np.testing.assert_array_equal(default, conical)


def test_atmosphere_rotation_affects_drag_force_and_torque():
    document = _drag_document()
    drag_model = scenario_model(document)
    sun = leo.SunEphemeris.constant([AU_M, 0.0, 0.0])
    atmosphere = leo.ExponentialAtmosphere(scale=1.0)
    co_rotating = leo.Environment(sun=sun, atmosphere=atmosphere)
    inertial_frame = leo.Environment(
        sun=sun, atmosphere=leo.ExponentialAtmosphere(scale=1.0),
        atmosphere_rotation_rad_s=[0.0, 0.0, 0.0])
    default = leo.Environment(sun=sun, atmosphere=leo.ExponentialAtmosphere(scale=1.0))
    first = _diagnostics(document, 30.0, environment=co_rotating, model=drag_model)
    second = _diagnostics(document, 30.0, environment=inertial_frame,
                          model=drag_model)
    implicit = _diagnostics(document, 30.0, environment=default, model=drag_model)
    assert np.linalg.norm(first.drag.force_n - second.drag.force_n) > 0
    assert np.linalg.norm(first.drag.torque_nm) > 0.0
    assert np.linalg.norm(first.drag.torque_nm - second.drag.torque_nm) > 0.0
    np.testing.assert_array_equal(first.drag.torque_nm, implicit.drag.torque_nm)


def test_environment_samples_report_absolute_epochs():
    document = _radiation_document()
    sun_position = np.asarray(document["environment"]["sun_position_m"])
    trajectory = _propagate(document, scenario_environment(document))
    records = trajectory.analyze().environment
    np.testing.assert_array_equal(records["epoch_et_s"], trajectory.times_s)
    np.testing.assert_array_equal(records["time_s"], trajectory.times_s)
    np.testing.assert_array_equal(records["sun_position_m"],
                                  np.tile(sun_position, (records.size, 1)))


def test_j2_is_independent_of_the_solar_environment():
    document = _document("j2_inclined")
    with_sun = _propagate(
        document, leo.Environment(sun=leo.SunEphemeris.constant([AU_M, 0.0, 0.0])))
    without = _propagate(document, environment=None)
    assert np.array_equal(with_sun.states.view(np.uint64),
                          without.states.view(np.uint64))


def test_irregular_sun_table_reproduces_cubic_position_and_derivative():
    epochs = np.array([1000., 1002., 1005.])
    def polynomial(et):
        t = np.asarray(et) - 1000.
        return np.stack((AU_M + 2*t**3, t**3 - 3*t**2, 4*t + t**2), axis=-1)
    def derivative(et):
        t = np.asarray(et) - 1000.
        return np.stack((6*t**2, 3*t**2 - 6*t, 4 + 2*t), axis=-1)
    positions, velocities = polynomial(epochs), derivative(epochs)
    table = leo.SunEphemeris.tabulated(et_seconds=epochs, position_m=positions,
                                       velocity_m_s=velocities)
    positions[...] = 0
    velocities[...] = 0
    for et in [1000., 1000.5, 1002., 1003.25, 1005.]:
        r, v = table.sample(et)
        np.testing.assert_allclose(r[0], polynomial(et)[0], rtol=0, atol=np.spacing(AU_M))
        np.testing.assert_allclose(r[1:], polynomial(et)[1:], rtol=0, atol=1e-12)
        np.testing.assert_allclose(v, derivative(et), rtol=0, atol=1e-12)
    for et in [np.nextafter(1000., -np.inf), np.nextafter(1005., np.inf)]:
        with pytest.raises(leo.DomainError):
            table.sample(et)