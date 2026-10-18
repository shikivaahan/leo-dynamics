"""Heterogeneous batch propagation through the public API.

The native scheduler owns all concurrency: whole trajectories, bounded workers,
input ordering and failure reporting. These tests check the Python boundary for
broadcasting, per-index configuration, ordering, worker counts, failure
reporting and the absence of partial results.
"""

from __future__ import annotations

import threading

import numpy as np
import pytest

import leo_dynamics as leo
from conftest import (
    load_scenario,
    member_initials,
    scenario_arguments,
    scenario_craft,
    scenario_environment,
    scenario_model,
    scenario_options,
    scenario_spacecraft,
    scenario_state,
    truncate,
)

AU_M = 1.495978707e11


def _base(name: str = "two_body_circular") -> dict:
    return truncate(load_scenario(name))


def _times(document: dict) -> np.ndarray:
    return np.asarray(document["run"]["sample_times_s"], dtype=np.float64)


def _states(document: dict) -> np.ndarray:
    return scenario_state(document).to_array()


def _single(initial: np.ndarray, spacecraft, model, times: np.ndarray,
            environment=None, options=None) -> leo.Trajectory:
    return leo.propagate(
        initial=leo.State.from_array(initial),
        spacecraft=spacecraft,
        model=model,
        times_s=times,
        environment=environment,
        options=options,
    )


def _constant_sun(distance_m: float = AU_M) -> leo.Environment:
    return leo.Environment(sun=leo.SunEphemeris.constant([distance_m, 0.0, 0.0]),
                           atmosphere=leo.ExponentialAtmosphere())


def _short_coverage_sun(stop_et_s: float = 300.0) -> leo.SunEphemeris:
    """A tabulated Sun that covers only the first part of the window."""
    epochs = np.asarray([0.0, stop_et_s / 2.0, stop_et_s])
    position = np.tile(np.asarray([AU_M, 0.0, 0.0]), (3, 1))
    return leo.SunEphemeris.tabulated(
        et_seconds=epochs, position_m=position, velocity_m_s=np.zeros((3, 3))
    )


# --------------------------------------------------------------------------
# Broadcast configuration
# --------------------------------------------------------------------------

def test_broadcast_spacecraft_and_model_over_initial_states():
    document = _base()
    times = _times(document)
    initials = member_initials(document, 5)
    craft = scenario_spacecraft(document)
    model = scenario_model(document)
    options = scenario_options(document)
    batch = leo.propagate_batch(
        initials=initials, spacecraft=craft, model=model, times_s=times,
        options=options,
    )
    assert len(batch) == 5
    assert batch.states.shape == (5, len(times), 13)
    for index in range(5):
        single = _single(initials[index], craft, model, times, options=options)
        assert np.array_equal(batch.states[index].view(np.uint64),
                              single.states.view(np.uint64))


def test_broadcast_environment_over_initial_states():
    document = _base("srp")
    times = _times(document)
    initials = member_initials(document, 4)
    craft = scenario_spacecraft(document)
    model = scenario_model(document)
    environment = scenario_environment(document)
    options = scenario_options(document)
    batch = leo.propagate_batch(
        initials=initials, spacecraft=craft, model=model, times_s=times,
        environment=environment, options=options,
    )
    for index in range(4):
        single = _single(initials[index], craft, model, times, environment,
                         options)
        assert np.array_equal(batch.states[index].view(np.uint64),
                              single.states.view(np.uint64))


def test_batch_views_agree_with_the_owning_array():
    document = _base()
    times = _times(document)
    batch = leo.propagate_batch(
        initials=member_initials(document, 3),
        spacecraft=scenario_spacecraft(document),
        model=scenario_model(document),
        times_s=times,
    )
    states = batch.states
    assert states.shape == (3, len(times), 13)
    assert batch.position_m.shape == (3, len(times), 3)
    assert batch.velocity_m_s.shape == (3, len(times), 3)
    assert batch.quaternion_wxyz.shape == (3, len(times), 4)
    assert batch.omega_body_rad_s.shape == (3, len(times), 3)
    np.testing.assert_array_equal(batch.times_s, times)
    np.testing.assert_array_equal(batch.position_m, states[:, :, :3])
    np.testing.assert_array_equal(batch.velocity_m_s, states[:, :, 3:6])
    np.testing.assert_array_equal(batch.quaternion_wxyz, states[:, :, 6:10])
    np.testing.assert_array_equal(batch.omega_body_rad_s, states[:, :, 10:13])
    for array in (batch.states, batch.position_m, batch.velocity_m_s,
                  batch.quaternion_wxyz, batch.omega_body_rad_s, batch.times_s):
        assert array.flags["WRITEABLE"] is False
        assert array.dtype == np.float64
        assert np.isfinite(array).all()


def test_indexing_returns_one_trajectory():
    document = _base()
    times = _times(document)
    initials = member_initials(document, 3)
    craft = scenario_spacecraft(document)
    model = scenario_model(document)
    options = scenario_options(document)
    batch = leo.propagate_batch(
        initials=initials, spacecraft=craft, model=model, times_s=times,
        options=options,
    )
    for index in (-3, -1, 0, 2):
        trajectory = batch[index]
        assert isinstance(trajectory, leo.Trajectory)
        assert trajectory.states.shape == (len(times), 13)
        assert np.array_equal(trajectory.states.view(np.uint64),
                              batch.states[index].view(np.uint64))
        np.testing.assert_array_equal(trajectory.times_s, times)
    with pytest.raises(IndexError):
        batch[3]
    with pytest.raises(IndexError):
        batch[-4]


# --------------------------------------------------------------------------
# Per-trajectory configuration
# --------------------------------------------------------------------------

def test_heterogeneous_mass_and_inertia():
    document = _base("coupled_case_a")
    times = _times(document)
    initials = member_initials(document, 3)
    options = scenario_options(document)
    crafts = [
        scenario_craft(document, mass_kg=10.0),
        scenario_craft(document, mass_kg=250.0, inertia_scale=4.0),
        scenario_craft(document, mass_kg=1250.0, inertia_scale=9.0),
    ]
    batch = leo.propagate_batch(
        initials=initials, spacecraft=crafts, model=scenario_model(document),
        times_s=times, environment=scenario_environment(document),
        options=options,
    )
    for index, craft in enumerate(crafts):
        single = _single(initials[index], craft, scenario_model(document), times,
                         scenario_environment(document), options)
        assert np.array_equal(batch.states[index].view(np.uint64),
                              single.states.view(np.uint64))
    # Different masses must actually produce different trajectories.
    assert not np.array_equal(batch.states[0], batch.states[1])


def _panel_craft(document: dict, *, area_scale: float = 1.0, count: int | None = None):
    """Scenario spacecraft with modified one-sided surface geometry."""
    source = document["spacecraft"]
    surfaces = []
    for surface in source["surfaces"][:count]:
        surfaces.append(
            leo.Surface(
                area_m2=area_scale * surface["area_m2"],
                normal_body=[-component for component in surface["normal"]],
                center_of_pressure_body_m=[-component for component in surface["centre"]],
                optical=leo.OpticalProperties(
                    specular=surface["specular"], diffuse=surface["diffuse"]
                ),
                aerodynamic=leo.AerodynamicProperties(
                    drag_coefficient=surface["drag_coefficient"],
                    accommodation=surface["accommodation"],
                ),
            )
        )
    return leo.Spacecraft(
        mass_kg=source["mass_kg"],
        inertia_kg_m2=np.diag(np.asarray(source["inertia_kg_m2"],
                                         dtype=np.float64)),
        surfaces=surfaces,
    )


def test_heterogeneous_surface_geometry():
    document = _base("srp")
    times = _times(document)
    initials = member_initials(document, 3)
    options = scenario_options(document)
    model = scenario_model(document)
    environment = scenario_environment(document)
    crafts = [
        scenario_craft(document, surfaces=False),   # bare body: no radiation torque
        scenario_spacecraft(document),              # six panels
        _panel_craft(document, area_scale=4.0, count=2),
    ]
    batch = leo.propagate_batch(
        initials=initials, spacecraft=crafts, model=model, times_s=times,
        environment=environment, options=options,
    )
    for index, craft in enumerate(crafts):
        single = _single(initials[index], craft, model, times, environment,
                         options)
        assert np.array_equal(batch.states[index].view(np.uint64),
                              single.states.view(np.uint64))
    # A bare body feels no SRP torque, so its attitude history must differ.
    assert not np.allclose(batch.states[0][:, 6:10], batch.states[1][:, 6:10])
    assert not np.allclose(batch.states[1][:, 6:10], batch.states[2][:, 6:10])


def test_heterogeneous_dynamics_models():
    document = _base("coupled_case_a")
    times = _times(document)
    initials = member_initials(document, 4)
    options = scenario_options(document)
    craft = scenario_craft(document)
    base_environment = scenario_environment(document)
    models = [
        leo.DynamicsModel(),
        leo.DynamicsModel(gravity="j2"),
        leo.DynamicsModel(gravity_gradient=True),
        scenario_model(document),
    ]
    environments = [None, None, None, base_environment]
    batch = leo.propagate_batch(
        initials=initials, spacecraft=craft, model=models, times_s=times,
        environment=environments, options=options,
    )
    for index, model in enumerate(models):
        single = _single(initials[index], craft, model, times,
                         environments[index], options)
        assert np.array_equal(batch.states[index].view(np.uint64),
                              single.states.view(np.uint64))
    assert not np.array_equal(batch.states[0], batch.states[1])


def test_heterogeneous_environments():
    document = _base("coupled_case_a")
    times = _times(document)
    initials = member_initials(document, 3)
    options = scenario_options(document)
    craft = scenario_craft(document)
    model = scenario_model(document)
    environments = [
        _constant_sun(),
        _constant_sun(0.8 * AU_M),
        leo.Environment(
            sun=leo.SunEphemeris.constant([AU_M, 0.0, 0.0]),
            atmosphere=leo.ExponentialAtmosphere(scale=2.0),
            eclipse="cylindrical",
            atmosphere_rotation_rad_s=[0.0, 0.0, 0.0],
        ),
    ]
    batch = leo.propagate_batch(
        initials=initials, spacecraft=craft, model=model, times_s=times,
        environment=environments, options=options,
    )
    for index, environment in enumerate(environments):
        single = _single(initials[index], craft, model, times, environment,
                         options)
        assert np.array_equal(batch.states[index].view(np.uint64),
                              single.states.view(np.uint64))
    assert not np.array_equal(batch.states[0], batch.states[1])


# --------------------------------------------------------------------------
# Input forms
# --------------------------------------------------------------------------

def test_state_sequence_and_packed_array_inputs_agree():
    document = _base()
    times = _times(document)
    initials = member_initials(document, 4)
    craft = scenario_spacecraft(document)
    model = scenario_model(document)
    options = scenario_options(document)
    from_array = leo.propagate_batch(
        initials=initials, spacecraft=craft, model=model, times_s=times,
        options=options,
    )
    from_states = leo.propagate_batch(
        initials=[leo.State.from_array(row) for row in initials],
        spacecraft=craft, model=model, times_s=times, options=options,
    )
    assert np.array_equal(from_array.states.view(np.uint64),
                          from_states.states.view(np.uint64))


def test_short_configuration_sequence_is_not_padded():
    document = _base()
    times = _times(document)
    initials = member_initials(document, 3)
    craft = scenario_spacecraft(document)
    with pytest.raises(ValueError):
        leo.propagate_batch(
            initials=initials, spacecraft=[craft, craft],  # wrong length
            model=scenario_model(document), times_s=times,
        )


@pytest.mark.parametrize(
    "kwargs",
    [
        dict(spacecraft=[0, 1]),
        dict(model=[1, 2]),
        dict(environment=[None, None]),
    ],
)
def test_configuration_length_mismatch_is_rejected(kwargs):
    document = _base()
    times = _times(document)
    defaults = dict(
        initials=member_initials(document, 3),
        spacecraft=scenario_spacecraft(document),
        model=scenario_model(document),
        times_s=times,
    )
    defaults.update(kwargs)
    with pytest.raises(ValueError):
        leo.propagate_batch(**defaults)


@pytest.mark.parametrize(
    "initials",
    [
        np.zeros((3, 12)),      # wrong state width
        np.zeros((3, 14)),
        np.zeros((2, 13, 1)),   # not two-dimensional
        np.zeros((1,)),
    ],
)
def test_invalid_initial_arrays_are_rejected(initials):
    document = _base()
    with pytest.raises(ValueError):
        leo.propagate_batch(
            initials=initials, spacecraft=scenario_spacecraft(document),
            model=scenario_model(document), times_s=_times(document),
        )


# --------------------------------------------------------------------------
# Empty and singleton batches
# --------------------------------------------------------------------------

def test_empty_batch_has_documented_shape():
    document = _base()
    times = _times(document)
    batch = leo.propagate_batch(
        initials=np.zeros((0, 13)), spacecraft=scenario_spacecraft(document),
        model=scenario_model(document), times_s=times,
    )
    assert len(batch) == 0
    assert batch.states.shape == (0, len(times), 13)
    assert batch.position_m.shape == (0, len(times), 3)
    assert batch.velocity_m_s.shape == (0, len(times), 3)
    assert batch.quaternion_wxyz.shape == (0, len(times), 4)
    assert batch.omega_body_rad_s.shape == (0, len(times), 3)
    assert batch.times_s.shape == (len(times),)
    assert batch.epoch_et_s == 0.0
    with pytest.raises(IndexError):
        batch[0]


def test_empty_batch_still_validates_shared_inputs():
    document = _base()
    craft = scenario_spacecraft(document)
    model = scenario_model(document)
    with pytest.raises(ValueError):
        leo.propagate_batch(initials=np.zeros((0, 13)), spacecraft=craft,
                            model=model, times_s=np.asarray([1.0, 2.0]))
    with pytest.raises(ValueError):
        leo.propagate_batch(initials=np.zeros((0, 13)), spacecraft=[craft],
                            model=model, times_s=_times(document))
    with pytest.raises(ValueError):
        leo.propagate_batch(initials=np.zeros((0, 13)), spacecraft=craft,
                            model=model, times_s=_times(document), workers=0)


@pytest.mark.parametrize("model", [
    leo.DynamicsModel(srp=leo.Contribution(torque=True)),
    leo.DynamicsModel(drag=leo.Contribution(torque=True), drag_law="sentman"),
])
def test_empty_batch_rejects_missing_shared_acquisition(model):
    with pytest.raises(ValueError):
        leo.propagate_batch(initials=np.empty((0, 13)),
                            spacecraft=scenario_spacecraft(_base()), model=model,
                            times_s=[0., 600.])


def test_empty_batch_validates_shared_ephemeris_coverage():
    with pytest.raises(leo.DomainError):
        leo.propagate_batch(initials=np.empty((0, 13)),
                            spacecraft=scenario_spacecraft(_base()),
                            model=leo.DynamicsModel(),
                            environment=leo.Environment(sun=_short_coverage_sun()),
                            times_s=[0., 600.])


def test_singleton_batch():
    document = _base()
    times = _times(document)
    craft = scenario_spacecraft(document)
    model = scenario_model(document)
    options = scenario_options(document)
    single = _single(_states(document), craft, model, times, options=options)
    batch = leo.propagate_batch(
        initials=np.asarray([_states(document)]), spacecraft=craft,
        model=model, times_s=times, options=options,
    )
    assert len(batch) == 1
    assert np.array_equal(batch.states[0].view(np.uint64),
                          single.states.view(np.uint64))


# --------------------------------------------------------------------------
# Workers, ordering and determinism
# --------------------------------------------------------------------------

@pytest.mark.parametrize("workers", [1, 2, 4, 8])
def test_worker_counts_are_bitwise_identical(workers):
    document = truncate(load_scenario("coupled_case_a"))
    times = _times(document)
    initials = member_initials(document, 8)
    arguments = dict(
        initials=initials, spacecraft=scenario_spacecraft(document),
        model=scenario_model(document), times_s=times,
        environment=scenario_environment(document),
        options=scenario_options(document),
    )
    reference = leo.propagate_batch(**arguments, workers=1)
    observed = leo.propagate_batch(**arguments, workers=workers)
    assert np.array_equal(observed.states.view(np.uint64),
                          reference.states.view(np.uint64))


def test_worker_count_is_bounded_by_batch_size():
    document = _base()
    times = _times(document)
    arguments = dict(
        initials=member_initials(document, 3),
        spacecraft=scenario_spacecraft(document),
        model=scenario_model(document),
        times_s=times,
    )
    small = leo.propagate_batch(**arguments, workers=1)
    huge = leo.propagate_batch(**arguments, workers=64)
    assert np.array_equal(huge.states.view(np.uint64), small.states.view(np.uint64))


def test_sequential_loop_equals_the_batch():
    document = truncate(load_scenario("coupled_case_a"))
    times = _times(document)
    initials = member_initials(document, 6)
    craft = scenario_spacecraft(document)
    model = scenario_model(document)
    environment = scenario_environment(document)
    options = scenario_options(document)
    sequential = np.stack([
        _single(row, craft, model, times, environment, options).states
        for row in initials
    ])
    for workers in (1, 2, 4):
        batch = leo.propagate_batch(
            initials=initials, spacecraft=craft, model=model, times_s=times,
            environment=environment, options=options, workers=workers,
        )
        assert np.array_equal(batch.states.view(np.uint64),
                              sequential.view(np.uint64))


def test_input_order_is_preserved():
    document = _base()
    times = _times(document)
    craft = scenario_spacecraft(document)
    model = scenario_model(document)
    options = scenario_options(document)
    base = member_initials(document, 6)
    order = np.asarray([4, 0, 5, 1, 3, 2])
    shuffled = base[order]
    batch = leo.propagate_batch(
        initials=shuffled, spacecraft=craft, model=model, times_s=times,
        options=options, workers=4,
    )
    for position, source in enumerate(order):
        single = _single(base[source], craft, model, times, options=options)
        assert np.array_equal(batch.states[position].view(np.uint64),
                              single.states.view(np.uint64))


@pytest.mark.parametrize("workers", [0, -1, -8, 1.5, True, "2", None])
def test_invalid_worker_counts_are_rejected(workers):
    document = _base()
    with pytest.raises(ValueError):
        leo.propagate_batch(
            initials=member_initials(document, 2),
            spacecraft=scenario_spacecraft(document),
            model=scenario_model(document),
            times_s=_times(document),
            workers=workers,
        )


# --------------------------------------------------------------------------
# Failure reporting
# --------------------------------------------------------------------------

def _failing_batch(document, times, initials, fail_at, workers):
    craft = scenario_spacecraft(document)
    model = scenario_model(document)
    short_environment = leo.Environment(sun=_short_coverage_sun(300.0))
    environments = [_constant_sun()] * len(initials)
    for index in fail_at:
        environments[index] = short_environment
    return leo.propagate_batch(
        initials=initials, spacecraft=craft, model=model, times_s=times,
        environment=environments, options=scenario_options(document),
        workers=workers,
    )


@pytest.mark.parametrize("workers", [1, 2, 4])
@pytest.mark.parametrize("fail_at,expected", [([1], 1), ([0], 0), ([2], 2),
                                              ([1, 2], 1), ([0, 1, 2], 0)])
def test_failure_reports_the_lowest_failing_index(workers, fail_at, expected):
    document = _base("srp")
    times = _times(document)
    initials = member_initials(document, 3)
    with pytest.raises(leo.BatchPropagationError) as info:
        _failing_batch(document, times, initials, fail_at, workers)
    error = info.value
    assert error.trajectory_index == expected
    assert isinstance(error.cause, ValueError)
    assert error.__cause__ is not None


def test_failed_batch_returns_no_partial_results():
    document = _base("srp")
    times = _times(document)
    initials = member_initials(document, 4)
    baseline_threads = threading.active_count()
    with pytest.raises(leo.BatchPropagationError):
        _failing_batch(document, times, initials, [1], workers=4)
    # Nothing is handed back on failure and every worker was joined.
    assert threading.active_count() == baseline_threads
    healthy = leo.propagate_batch(
        initials=initials, spacecraft=scenario_spacecraft(document),
        model=scenario_model(document), times_s=times,
        environment=_constant_sun(), options=scenario_options(document),
        workers=4,
    )
    assert healthy.states.shape == (4, len(times), 13)
    assert np.isfinite(healthy.states).all()
    assert threading.active_count() == baseline_threads


def test_single_trajectory_coverage_failure_is_a_domain_error():
    document = _base("srp")
    with pytest.raises(leo.DomainError):
        leo.propagate(
            initial=scenario_state(document),
            spacecraft=scenario_spacecraft(document),
            model=scenario_model(document),
            times_s=_times(document),
            environment=leo.Environment(sun=_short_coverage_sun(300.0)),
            options=scenario_options(document),
        )


def test_batch_trajectory_is_analysable_and_indexable():
    document = _base("coupled_case_a")
    times = _times(document)
    batch = leo.propagate_batch(
        initials=member_initials(document, 2),
        spacecraft=scenario_spacecraft(document),
        model=scenario_model(document),
        times_s=times,
        environment=scenario_environment(document),
        options=scenario_options(document),
        workers=2,
    )
    for index in range(2):
        trajectory = batch[index]
        orbital = trajectory.orbital_elements()
        assert orbital.shape == (len(times),)
        np.testing.assert_array_equal(orbital["time_s"], times)


def test_single_and_batch_share_the_same_options_and_epoch():
    document = _base()
    times = _times(document)
    craft = scenario_spacecraft(document)
    model = scenario_model(document)
    options = scenario_options(document)
    initials = member_initials(document, 2)
    single = leo.propagate(
        initial=leo.State.from_array(initials[0]), spacecraft=craft,
        model=model, times_s=times, options=options, epoch_et_s=1234.5,
    )
    batch = leo.propagate_batch(
        initials=initials, spacecraft=craft, model=model, times_s=times,
        options=options, epoch_et_s=1234.5,
    )
    assert single.epoch_et_s == batch.epoch_et_s == 1234.5
    assert np.array_equal(batch.states[0].view(np.uint64),
                          single.states.view(np.uint64))


def test_scenario_arguments_helper_still_propagates():
    """The shared fixture helper keeps working for the batch suite."""
    arguments = scenario_arguments(_base())
    trajectory = leo.propagate(**arguments)
    assert trajectory.states.shape[1] == 13