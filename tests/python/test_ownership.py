"""Ownership, memory and GIL contracts.

Inputs must be copied into validated native storage before the GIL is released,
results must own their memory, read-only views must not duplicate the owning
buffer, native workers must run without the GIL, and repeated construction,
destruction and failure must not leak references or threads.
"""

from __future__ import annotations

import gc
import threading
import time
import tracemalloc
from pathlib import Path

import numpy as np
import pytest

import leo_dynamics as leo
from conftest import (
    load_scenario,
    member_initials,
    scenario_environment,
    scenario_model,
    scenario_options,
    scenario_spacecraft,
    scenario_state,
    truncate,
)

AU_M = 1.495978707e11


def _document(name: str = "two_body_circular") -> dict:
    return truncate(load_scenario(name))


def _times(document: dict) -> np.ndarray:
    return np.asarray(document["run"]["sample_times_s"], dtype=np.float64)


def _craft(document: dict) -> leo.Spacecraft:
    return scenario_spacecraft(document)


def _propagate(document: dict, **overrides) -> leo.Trajectory:
    arguments = dict(
        initial=scenario_state(document),
        spacecraft=_craft(document),
        model=scenario_model(document),
        times_s=_times(document),
        environment=scenario_environment(document),
        options=scenario_options(document),
    )
    arguments.update(overrides)
    return leo.propagate(**arguments)


def _run(horizon_s: float = 21600.0, steps: int = 40000) -> float:
    """A deliberately long native propagation; returns its wall time."""
    document = load_scenario("two_body_circular")
    times = np.arange(0.0, horizon_s, 60.0)
    started = time.perf_counter()
    leo.propagate(
        initial=scenario_state(document), spacecraft=_craft(document),
        model=leo.DynamicsModel(), times_s=times,
        options=leo.SolverOptions(rtol=1e-12, initial_step_s=horizon_s / steps, max_step_s=horizon_s / steps,
                                  position_atol_m=1e-6, velocity_atol_m_s=1e-9),
    )
    return time.perf_counter() - started


class _Ticker:
    """Counts how often a second Python thread can run."""

    def __init__(self) -> None:
        self.ticks = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)

    def _loop(self) -> None:
        while not self._stop.is_set():
            self.ticks += 1
            time.sleep(0)

    def __enter__(self) -> "_Ticker":
        self._thread.start()
        return self

    def __exit__(self, *exception) -> None:
        self._stop.set()
        self._thread.join(timeout=5.0)


# --------------------------------------------------------------------------
# Input ownership
# --------------------------------------------------------------------------

def test_configuration_copies_mutable_inputs():
    inertia = np.diag([4.0, 6.0, 9.0])
    normals = np.eye(3)
    centres = np.zeros((3, 3))
    craft = leo.Spacecraft(
        mass_kg=5.0, inertia_kg_m2=inertia,
        surfaces=[
            leo.Surface(area_m2=1.0, normal_body=normals[row],
                        center_of_pressure_body_m=centres[row])
            for row in range(3)
        ],
    )
    before = np.array(craft.inertia_kg_m2, copy=True)
    inertia[...] = 0.0
    normals[...] = 0.0
    centres[...] = 99.0
    np.testing.assert_array_equal(craft.inertia_kg_m2, before)
    for index, surface in enumerate(craft.surfaces):
        np.testing.assert_array_equal(surface.normal_body, np.eye(3)[index])
        np.testing.assert_array_equal(surface.center_of_pressure_body_m, np.zeros(3))


def test_state_copies_its_arrays():
    position = np.array([7.0e6, 0.0, 0.0])
    velocity = np.array([0.0, 7500.0, 0.0])
    state = leo.State(position_m=position, velocity_m_s=velocity)
    packed = state.to_array().copy()
    position[...] = 0.0
    velocity[...] = 0.0
    np.testing.assert_array_equal(state.to_array(), packed)


def test_mutating_inputs_during_propagation_cannot_change_the_run():
    """Caller mutation while native work runs is a data race, not an input."""
    document = _document("coupled_case_a")
    initials = member_initials(document, 4)
    times = _times(document)
    arguments = dict(
        spacecraft=_craft(document), model=scenario_model(document),
        times_s=times, environment=scenario_environment(document),
        options=scenario_options(document),
    )
    reference = leo.propagate_batch(initials=np.array(initials), **arguments)
    mutated = np.array(initials)
    schedule = np.array(times)
    import sys
    ready, go, changed = threading.Event(), threading.Event(), threading.Event()
    def scribble() -> None:
        ready.set()
        go.wait()
        mutated[...] = 1.0e9
        schedule[...] = 0.0
        changed.set()
    thread = threading.Thread(target=scribble)
    thread.start()
    ready.wait()
    previous_interval = sys.getswitchinterval()
    try:
        # No Python thread switch between releasing this gate and the extension
        # releasing the GIL after native input capture.
        sys.setswitchinterval(1.0)
        go.set()
        observed = leo.propagate_batch(initials=mutated, **{**arguments,
                                                            "times_s": schedule})
    finally:
        sys.setswitchinterval(previous_interval)
        thread.join(timeout=5.0)
    assert changed.is_set()
    assert np.array_equal(observed.states.view(np.uint64),
                          reference.states.view(np.uint64))


def test_non_contiguous_inputs_are_converted():
    document = _document()
    dense = member_initials(document, 6)
    view = np.repeat(dense, 2, axis=0)[::2]        # same values, strided
    assert not view.flags["C_CONTIGUOUS"]
    arguments = dict(spacecraft=_craft(document), model=scenario_model(document),
                     times_s=_times(document), options=scenario_options(document))
    from_view = leo.propagate_batch(initials=view, **arguments)
    from_copy = leo.propagate_batch(initials=np.ascontiguousarray(view), **arguments)
    assert np.array_equal(from_view.states.view(np.uint64),
                          from_copy.states.view(np.uint64))
    # The same holds for the per-trajectory state inputs.
    big = np.zeros(6)
    big[::2] = dense[0, :3]
    strided = big[::2]
    assert not strided.flags["C_CONTIGUOUS"]
    state = leo.State(position_m=strided, velocity_m_s=dense[0, 3:6])
    np.testing.assert_array_equal(state.position_m, dense[0, :3])


def test_integer_and_list_inputs_are_accepted():
    document = _document()
    arguments = dict(model=scenario_model(document), times_s=_times(document),
                     options=scenario_options(document))
    initial = member_initials(document, 2).astype(np.int64)
    floats = leo.propagate_batch(
        initials=initial.astype(np.float64), spacecraft=_craft(document),
        **arguments)
    integers = leo.propagate_batch(
        initials=initial, spacecraft=_craft(document), **arguments)
    assert np.array_equal(floats.states.view(np.uint64),
                          integers.states.view(np.uint64))


@pytest.mark.parametrize(
    "initials",
    [
        np.full((2, 13), "not-a-number"),
        np.full((2, 13), np.nan),
        np.full((2, 13), 1, dtype=np.complex128),
        np.zeros((2, 13), dtype=object),
    ],
)
def test_invalid_dtypes_fail_cleanly(initials):
    document = _document()
    with pytest.raises((TypeError, ValueError)):
        leo.propagate_batch(initials=initials, spacecraft=_craft(document),
                            model=scenario_model(document),
                            times_s=_times(document))


# --------------------------------------------------------------------------
# Result ownership
# --------------------------------------------------------------------------

def test_results_outlive_their_sources():
    document = _document()
    expected_times = _times(document)
    trajectory = _propagate(document)
    states = trajectory.states
    times = trajectory.times_s
    snapshot = np.array(states, copy=True)
    del trajectory, document
    gc.collect()
    np.testing.assert_array_equal(states, snapshot)
    np.testing.assert_array_equal(times, expected_times)
    assert states.flags["WRITEABLE"] is False


def test_batch_members_outlive_the_batch():
    document = _document()
    batch = leo.propagate_batch(
        initials=member_initials(document, 3), spacecraft=_craft(document),
        model=scenario_model(document), times_s=_times(document))
    first, second = batch[0], batch[-1]
    snapshots = (np.array(first.states, copy=True), np.array(second.states, copy=True))
    del batch
    gc.collect()
    np.testing.assert_array_equal(first.states, snapshots[0])
    np.testing.assert_array_equal(second.states, snapshots[1])


def test_configuration_objects_can_be_deleted_after_propagation():
    document = _document("srp")
    trajectory = _propagate(document)
    # _propagate's initial/config/provider temporaries have already gone away.
    gc.collect()
    retained = trajectory.analyze().environment
    direct = leo.evaluate_dynamics(state=leo.State.from_array(trajectory.states[0]),
        time_s=0., spacecraft=_craft(document), model=scenario_model(document),
        environment=scenario_environment(document))
    np.testing.assert_array_equal(retained["srp_force_n"][0], direct.srp.force_n)
    np.testing.assert_array_equal(retained["srp_torque_nm"][0], direct.srp.torque_nm)


def test_views_share_one_owning_buffer():
    document = _document()
    trajectory = _propagate(document)
    states = trajectory.states
    base = states.__array_interface__["data"][0]
    for view, offset in ((trajectory.position_m, 0), (trajectory.velocity_m_s, 3),
                         (trajectory.quaternion_wxyz, 6), (trajectory.omega_body_rad_s, 10)):
        assert np.shares_memory(view, states)
        assert view.__array_interface__["data"][0] == base + offset * 8
        assert view.flags["WRITEABLE"] is False
    assert states.itemsize * states.size == states.nbytes


def test_batch_output_memory_matches_the_declared_shape():
    document = _document()
    members, count = 8, len(_times(document))
    batch = leo.propagate_batch(
        initials=member_initials(document, members), spacecraft=_craft(document),
        model=scenario_model(document), times_s=_times(document))
    assert batch.states.nbytes == members * count * 13 * 8
    assert batch.states.size == members * count * 13
    member = batch[0]
    # Members are views into the single owning (B, N, 13) buffer.
    assert member.states.__array_interface__["data"][0] == \
        batch.states.__array_interface__["data"][0]
    assert member.states.nbytes == count * 13 * 8


# --------------------------------------------------------------------------
# GIL
# --------------------------------------------------------------------------

def test_gil_is_released_during_single_propagation():
    import sys
    previous = sys.getswitchinterval()
    with _Ticker() as ticker:
        try:
            sys.setswitchinterval(1.0)
            ticker.ticks = 0
            _run(steps=40000)
        finally:
            sys.setswitchinterval(previous)
    assert ticker.ticks > 0, "no Python thread progress during native propagation"


def test_gil_is_released_during_batch_propagation():
    document = load_scenario("two_body_circular")
    initials = member_initials(document, 4)
    times = np.arange(0.0, 21600.0, 60.0)
    options = leo.SolverOptions(rtol=1e-12, max_step_s=1.0,
                                position_atol_m=1e-6, velocity_atol_m_s=1e-9)
    arguments = dict(initials=initials, spacecraft=_craft(document),
                     model=leo.DynamicsModel(), times_s=times, options=options,
                     workers=2)
    import sys
    previous = sys.getswitchinterval()
    with _Ticker() as ticker:
        try:
            sys.setswitchinterval(1.0)
            ticker.ticks = 0
            leo.propagate_batch(**arguments)
        finally:
            sys.setswitchinterval(previous)
    assert ticker.ticks > 0, "no Python thread progress during native batch work"




# --------------------------------------------------------------------------
# Repeated construction, destruction and failure
# --------------------------------------------------------------------------

def test_repeated_construction_does_not_accumulate_memory():
    document = _document()
    for _ in range(20):                      # warm up caches and one-off costs
        _propagate(document)
    gc.collect()
    tracemalloc.start()
    before = tracemalloc.take_snapshot()
    for _ in range(200):
        trajectory = _propagate(document)
        del trajectory
    gc.collect()
    after = tracemalloc.take_snapshot()
    tracemalloc.stop()
    growth = sum(stat.size_diff for stat in after.compare_to(before, "filename"))
    assert growth < 1_000_000, f"python-side growth {growth} bytes over 200 runs"


def test_repeated_failures_leave_no_threads_or_references():
    document = _document("srp")
    craft = _craft(document)
    model = scenario_model(document)
    times = _times(document)
    short = leo.Environment(sun=leo.SunEphemeris.tabulated(
        et_seconds=np.asarray([0.0, 100.0, 200.0]),
        position_m=np.tile(np.asarray([AU_M, 0.0, 0.0]), (3, 1)),
        velocity_m_s=np.zeros((3, 3))))
    healthy = leo.Environment(sun=leo.SunEphemeris.constant([AU_M, 0.0, 0.0]))
    initials = member_initials(document, 4)
    baseline = threading.active_count()
    tasks = Path("/proc/self/task")
    native_threads = {p.name for p in tasks.iterdir()} if tasks.exists() else None
    for _ in range(5):                       # warm up
        try:
            leo.propagate_batch(initials=initials, spacecraft=craft, model=model,
                                times_s=times,
                                environment=[healthy, short, healthy, healthy],
                                workers=4)
        except leo.BatchPropagationError:
            pass
    gc.collect()
    tracemalloc.start()
    snapshot = tracemalloc.take_snapshot()
    for _ in range(50):
        with pytest.raises(leo.BatchPropagationError):
            leo.propagate_batch(initials=initials, spacecraft=craft, model=model,
                                times_s=times,
                                environment=[healthy, short, healthy, healthy],
                                workers=4)
    gc.collect()
    after = tracemalloc.take_snapshot()
    tracemalloc.stop()
    assert threading.active_count() == baseline
    if native_threads is not None:
        assert {p.name for p in tasks.iterdir()} <= native_threads
    growth = sum(stat.size_diff for stat in after.compare_to(snapshot, "filename"))
    assert growth < 500_000, f"python-side growth {growth} bytes over 50 failures"
    recovered = leo.propagate_batch(initials=initials, spacecraft=craft,
                                    model=model, times_s=times,
                                    environment=healthy, workers=4)
    reference = leo.propagate(initial=leo.State.from_array(initials[0]), spacecraft=craft,
                              model=model, times_s=times, environment=healthy)
    np.testing.assert_array_equal(recovered.states[0], reference.states)
    assert threading.active_count() == baseline
    if native_threads is not None:
        assert {p.name for p in tasks.iterdir()} <= native_threads

