"""Shared fixtures for the Python API suite.

The frozen-fixture translation, the direct C++ harness helpers and the numerical
comparison helpers live in :mod:`scenario_fixtures` so the benchmark runner can
reuse them without pytest; this module adds the pytest fixtures and re-exports
the shared names used by the suite.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from scenario_fixtures import (  # noqa: E402  (path guard must run first)
    ACCEPTANCE,
    GOLDEN,
    GOLDEN_MANIFEST,
    POINTWISE_LIMITS,
    REFERENCE,
    ROOT,
    SCENARIOS,
    SCENARIO_NAMES,
    _order_key,
    assert_identical,
    diagnostics_arguments,
    fixture_entry,
    golden_states,
    harness_candidates,
    harness_samples,
    harness_trajectories,
    install_package_path,
    leo,
    load_scenario,
    member_initials,
    pointwise_failures,
    pointwise_rows,
    propagate_scenario,
    reference_helpers,
    resolve_harness,
    run_harness,
    sample_times,
    scenario_arguments,
    scenario_craft,
    scenario_contributions,
    scenario_environment,
    scenario_model,
    scenario_options,
    scenario_spacecraft,
    scenario_state,
    states_checksum,
    thread_count,
    thresholds,
    truncate,
    ulp_distance,
)

__all__ = [
    "ACCEPTANCE", "GOLDEN", "GOLDEN_MANIFEST", "POINTWISE_LIMITS", "REFERENCE",
    "ROOT", "SCENARIOS", "SCENARIO_NAMES", "assert_identical", "leo",
    "diagnostics_arguments", "fixture_entry", "golden_states",
    "harness_candidates", "harness_samples", "harness_trajectories",
    "load_scenario", "member_initials", "pointwise_failures", "pointwise_rows",
    "propagate_scenario", "reference_helpers", "resolve_harness", "run_harness",
    "sample_times", "scenario_arguments", "scenario_craft",
    "scenario_contributions", "scenario_environment", "scenario_model",
    "scenario_options", "scenario_spacecraft", "scenario_state",
    "states_checksum", "thread_count", "thresholds", "truncate", "ulp_distance",
]


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--harness",
        action="store",
        default=os.environ.get("LEO_REFERENCE_DUMP", ""),
        help="path to the direct C++ leo_reference_dump binary",
    )


@pytest.fixture(scope="session")
def harness_binary(pytestconfig: pytest.Config) -> Path:
    """Path to the direct C++ harness; skips parity tests when not built."""
    try:
        return resolve_harness(pytestconfig.getoption("--harness"))
    except FileNotFoundError as error:
        pytest.skip(str(error))


def require_workers(count: int) -> None:
    """Skip a test when the host cannot really run ``count`` workers at once."""
    if thread_count() < count:
        pytest.skip(f"needs at least {count} usable cores, saw {thread_count()}")