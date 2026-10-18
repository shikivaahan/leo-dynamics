"""Frozen v0.1 regression: fixture integrity and Python/native parity.

The qualified evidence in ``tests/reference`` is the authority. This file first
proves the fixtures are unmodified, then shows that the Python path reproduces
the direct C++ execution of the same configuration exactly - including the
native batch determinism contract - and finally that the result stays inside
the frozen independent references.
"""

from __future__ import annotations

import json

import numpy as np
import pytest

import leo_dynamics as leo
from conftest import (
    ACCEPTANCE,
    GOLDEN,
    GOLDEN_MANIFEST,
    ROOT,
    SCENARIOS,
    SCENARIO_NAMES,
    assert_identical,
    fixture_entry,
    golden_states,
    harness_samples,
    harness_trajectories,
    load_scenario,
    member_initials,
    reference_helpers,
    run_harness,
    scenario_arguments,
    scenario_environment,
    scenario_model,
    scenario_options,
    scenario_spacecraft,
    thresholds,
)

#: The qualified batch-determinism contract, frozen at commit 0110469.
BATCH_MEMBERS = 8
BATCH_WORKERS = (1, 2, 4, 8)


@pytest.fixture(scope="module")
def python_trajectories() -> dict:
    """Every qualified scenario propagated once through the public API."""
    return {name: leo.propagate(**scenario_arguments(load_scenario(name)))
            for name in SCENARIO_NAMES}


# --------------------------------------------------------------------------
# Fixture integrity
# --------------------------------------------------------------------------

@pytest.mark.parametrize("name", SCENARIO_NAMES)
def test_frozen_fixture_identities(name):
    entry = fixture_entry(name)
    assert reference_helpers.sha(GOLDEN / entry["file"]) == entry["sha256"]
    assert reference_helpers.sha(GOLDEN / entry["pointwise"]) == \
        entry["pointwise_sha256"]
    assert reference_helpers.sha(SCENARIOS / f"{name}.json") == \
        entry["scenario_sha256"]


def test_golden_matrix_matches_the_acceptance_scenarios():
    assert {entry["name"] for entry in GOLDEN_MANIFEST["fixtures"]} == \
        set(ACCEPTANCE["scenarios"])
    assert set(SCENARIO_NAMES) == set(ACCEPTANCE["scenarios"])


@pytest.mark.parametrize("name", SCENARIO_NAMES)
def test_reference_convergence_stays_inside_the_qualified_margin(name):
    entry = fixture_entry(name)
    golden = json.loads((GOLDEN / entry["file"]).read_text())
    limits = thresholds(name)
    for metric, change in golden["reference_convergence"].items():
        bound = ACCEPTANCE["reference_margin_fraction"] * limits[metric]
        assert change <= bound, f"{name}: {metric} changed by {change} > {bound}"




# --------------------------------------------------------------------------
# Python <-> direct C++ parity
# --------------------------------------------------------------------------

@pytest.mark.parametrize("name", SCENARIO_NAMES)
def test_python_reproduces_the_direct_cpp_path(name, harness_binary,
                                               python_trajectories):
    document = load_scenario(name)
    native = run_harness(harness_binary, document)
    trajectory = python_trajectories[name]
    assert_identical(trajectory.states, harness_samples(native), name)
    np.testing.assert_array_equal(
        trajectory.times_s,
        np.asarray([row["t"] for row in native["samples"]], dtype=np.float64))


@pytest.mark.parametrize("name", ("two_body_circular", "two_body_eccentric"))
def test_python_matches_the_analytical_kepler_solution(name, python_trajectories):
    entry = fixture_entry(name)
    golden = json.loads((GOLDEN / entry["file"]).read_text())
    oracle = golden["analytical_kepler"]
    count = len(python_trajectories[name].times_s)
    states = python_trajectories[name].states
    limits = thresholds(name)
    position_error = float(np.linalg.norm(
        states[:, :3] - np.asarray(oracle["position_m"], dtype=np.float64)[:count],
        axis=1).max())
    velocity_error = float(np.linalg.norm(
        states[:, 3:6] - np.asarray(oracle["velocity_m_s"], dtype=np.float64)[:count],
        axis=1).max())
    assert position_error <= limits["position_m"]
    assert velocity_error <= limits["velocity_m_s"]


# --------------------------------------------------------------------------
# Native batch determinism through Python
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def batch_document() -> dict:
    """The frozen determinism case: eight members over a 600 s window."""
    document = json.loads((SCENARIOS / "coupled_case_a.json").read_text())
    document["run"]["duration_s"] = 600.0
    document["run"]["sample_times_s"] = np.arange(0.0, 601.0, 30.0).tolist()
    return document


def _batch_arguments(document: dict) -> dict:
    arguments = scenario_arguments(document)
    return dict(
        spacecraft=arguments["spacecraft"], model=arguments["model"],
        times_s=arguments["times_s"], environment=arguments["environment"],
        options=arguments["options"],
    )


def test_native_batch_determinism_through_python(batch_document, harness_binary):
    arguments = _batch_arguments(batch_document)
    initials = member_initials(batch_document, BATCH_MEMBERS)
    sequential = run_harness(harness_binary, batch_document,
                             extra=["--route", "sequential", "--members",
                                    str(BATCH_MEMBERS), "--dump-batch"])
    reference = harness_trajectories(sequential)
    assert reference.shape == (BATCH_MEMBERS, len(arguments["times_s"]), 13)
    for workers in BATCH_WORKERS:
        native = run_harness(harness_binary, batch_document,
                             extra=["--route", "batch", "--members",
                                    str(BATCH_MEMBERS), "--workers", str(workers),
                                    "--dump-batch"])
        assert_identical(harness_trajectories(native), reference,
                         f"native batch workers={workers}")
        python_batch = leo.propagate_batch(initials=initials, workers=workers,
                                           **arguments)
        assert_identical(python_batch.states, reference,
                         f"python batch workers={workers}")


def test_python_batch_equals_the_python_single_path(batch_document):
    arguments = _batch_arguments(batch_document)
    initials = member_initials(batch_document, BATCH_MEMBERS)
    batch = leo.propagate_batch(initials=initials, workers=1, **arguments)
    for index, row in enumerate(initials):
        single = leo.propagate(
            initial=leo.State.from_array(row),
            spacecraft=scenario_spacecraft(batch_document),
            model=scenario_model(batch_document),
            times_s=arguments["times_s"],
            environment=scenario_environment(batch_document),
            options=scenario_options(batch_document),
        )
        assert_identical(batch.states[index], single.states,
                         f"member {index} sequential identity")


def test_reference_regression_helpers_are_reused(python_trajectories):
    """The frozen error metric is the one used for acceptance."""
    for name in SCENARIO_NAMES:
        _, expected = golden_states(name)
        error = reference_helpers.errors(python_trajectories[name].states, expected)
        limits = thresholds(name)
        for field, value in error.items():
            assert value <= limits[field], f"{name}: {field} {value} > {limits[field]}"


def test_qualification_report_is_present():
    """The numerical qualification evidence stays in the repository."""
    for name in ("v0.1-qualification.md", "v0.1-release-readiness.md",
                 "python-api-design.md"):
        assert (ROOT / "docs" / name).is_file()