"""Frozen-fixture translation shared by the Python test suite and benchmarks.

This module has no pytest dependency on purpose: ``tests/python/conftest.py``
re-exports it for the suite and ``benchmarks/python/binding_overhead.py`` imports
it directly. It never re-implements physics - it reads the qualified scenario
documents and goldens in ``tests/reference`` and translates them into public
``leo_dynamics`` configuration objects.
"""

from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / "tests" / "reference"
SCENARIOS = REFERENCE / "scenarios"
GOLDEN = REFERENCE / "golden"


def package_roots() -> list[Path]:
    """Directories that may contain the locally built ``leo_dynamics`` package."""
    roots: list[Path] = []
    override = os.environ.get("LEO_PYTHON_PATH", "")
    roots.extend(Path(part) for part in override.split(os.pathsep) if part)
    roots.extend([
        ROOT / "build-python" / "python",
        ROOT / "build-python",
        ROOT / "python",
        ROOT / "build" / "python",
    ])
    return roots


def install_package_path() -> None:
    """Make the locally built extension importable without an installed wheel."""
    try:  # an already importable installation wins
        import leo_dynamics  # noqa: F401

        return
    except ImportError:
        pass
    for root in package_roots():
        if (root / "leo_dynamics" / "__init__.py").is_file():
            sys.path.insert(0, str(root))
            return
    raise ImportError(
        "leo_dynamics is not importable. Build the extension with\n"
        "  cmake -S . -B build-python -DCMAKE_BUILD_TYPE=Release "
        "-DLEO_BUILD_PYTHON_BINDINGS=ON\n"
        "  cmake --build build-python -j\n"
        "and run with LEO_PYTHON_PATH=$PWD/build-python/python"
    )


install_package_path()

import leo_dynamics as leo  # noqa: E402  (import after the path guard above)


def load_reference_helpers():
    """Reuse the frozen reference-regression helpers instead of duplicating them."""
    path = REFERENCE / "test_reference_regression.py"
    spec = importlib.util.spec_from_file_location("leo_reference_regression", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


reference_helpers = load_reference_helpers()

#: Frozen acceptance limits and golden manifest (never regenerated).
ACCEPTANCE = json.loads((REFERENCE / "acceptance.json").read_text())
POINTWISE_LIMITS = ACCEPTANCE["pointwise"]
GOLDEN_MANIFEST = json.loads((GOLDEN / "manifest.json").read_text())
SCENARIO_NAMES = tuple(fixture["name"] for fixture in GOLDEN_MANIFEST["fixtures"])


# --------------------------------------------------------------------------
# Frozen fixtures
# --------------------------------------------------------------------------

def load_scenario(name: str) -> dict:
    """Read a frozen qualified scenario document without modifying it."""
    return json.loads((SCENARIOS / f"{name}.json").read_text())


def golden_states(name: str) -> tuple[np.ndarray, np.ndarray]:
    """(times_s, states) from a frozen independent golden."""
    document = json.loads((GOLDEN / f"{name}.json").read_text())
    return np.asarray(document["time_s"], dtype=np.float64), np.asarray(
        document["states"], dtype=np.float64)


def pointwise_rows(name: str) -> list[dict]:
    """Frozen independent pointwise loads and derivatives at fixed instants."""
    return json.loads((GOLDEN / f"{name}.pointwise.json").read_text())["rows"]


def thresholds(name: str) -> dict:
    return ACCEPTANCE["classes"][ACCEPTANCE["scenarios"][name]]


def fixture_entry(name: str) -> dict:
    for fixture in GOLDEN_MANIFEST["fixtures"]:
        if fixture["name"] == name:
            return fixture
    raise KeyError(name)


def truncate(document: dict, limit_s: float = 600.0) -> dict:
    """The frozen 'light' window used by the qualified reference regression."""
    times = np.asarray(document["run"]["sample_times_s"], dtype=np.float64)
    keep = times <= limit_s
    document["run"]["sample_times_s"] = times[keep].tolist()
    document["run"]["duration_s"] = float(times[keep][-1])
    return document


def sample_times(document: dict) -> np.ndarray:
    return np.asarray(document["run"]["sample_times_s"], dtype=np.float64)


# --------------------------------------------------------------------------
# Scenario -> public API configuration
# --------------------------------------------------------------------------

def scenario_state(document: dict) -> leo.State:
    initial = document["initial_state"]
    return leo.State(
        position_m=initial["position_m"],
        velocity_m_s=initial["velocity_m_s"],
        quaternion_wxyz=initial["quaternion"],
        omega_body_rad_s=initial["omega_body_rad_s"],
    )


def scenario_spacecraft(document: dict) -> leo.Spacecraft:
    craft = document["spacecraft"]
    inertia = craft.get("inertia_tensor_kg_m2")
    if inertia is None:
        inertia = np.diag(np.asarray(craft["inertia_kg_m2"], dtype=np.float64))
    else:
        inertia = np.asarray(inertia, dtype=np.float64)
    surfaces = [
        leo.Surface(
            area_m2=surface["area_m2"],
            normal_body=surface["normal"],
            center_of_pressure_body_m=surface["centre"],
            optical=leo.OpticalProperties(
                specular=surface["specular"], diffuse=surface["diffuse"]
            ),
            aerodynamic=leo.AerodynamicProperties(
                drag_coefficient=surface["drag_coefficient"],
                accommodation=surface["accommodation"],
            ),
        )
        for surface in craft.get("surfaces", ())
    ]
    return leo.Spacecraft(
        mass_kg=craft["mass_kg"], inertia_kg_m2=inertia, surfaces=surfaces
    )


def scenario_contributions(document: dict) -> dict:
    """Per-model (force, torque) switches; scenarios enable both or neither."""
    models = document["models"]
    selected = {}
    for name in ("srp", "drag", "albedo"):
        force = bool(models.get(f"{name}_force", models[name]))
        torque = bool(models.get(f"{name}_torque", models[name]))
        selected[name] = (force, torque)
    return selected


def _enabled(selected: dict, name: str) -> bool:
    force, torque = selected[name]
    return force or torque


def scenario_model(document: dict) -> leo.DynamicsModel:
    models = document["models"]
    selected = scenario_contributions(document)
    srp = drag = albedo = None
    drag_law = "none"
    if _enabled(selected, "srp"):
        srp = leo.Contribution(force=selected["srp"][0], torque=selected["srp"][1])
    if _enabled(selected, "drag"):
        drag = leo.Contribution(force=selected["drag"][0], torque=selected["drag"][1])
        drag_law = models["drag_model"]
    if _enabled(selected, "albedo"):
        albedo = leo.Contribution(
            force=selected["albedo"][0], torque=selected["albedo"][1]
        )
    return leo.DynamicsModel(
        gravity="j2" if models["j2"] else "point_mass",
        srp=srp,
        drag=drag,
        drag_law=drag_law,
        albedo=albedo,
        earth_albedo=models["earth_albedo"],
        gravity_gradient=bool(models["gravity_gradient"]),
    )


def scenario_environment(document: dict) -> leo.Environment | None:
    """Build the environment a scenario needs, or None when it needs none."""
    models = document["models"]
    selected = scenario_contributions(document)
    radiation = any(_enabled(selected, name) for name in ("srp", "albedo"))
    drag = _enabled(selected, "drag")
    if not radiation and not drag:
        return None
    source = document["environment"]
    return leo.Environment(
        sun=leo.SunEphemeris.constant(source["sun_position_m"]),
        atmosphere=leo.ExponentialAtmosphere(scale=models["density_scale"]),
        eclipse=models["eclipse_model"],
        atmosphere_rotation_rad_s=source.get(
            "atmosphere_angular_velocity_rad_s", (0.0, 0.0, 7.292115e-5)
        ),
    )


def scenario_options(document: dict) -> leo.SolverOptions:
    tolerances = document["run"]["tolerances"]
    return leo.SolverOptions(
        rtol=tolerances["rtol"],
        position_atol_m=tolerances["position_atol_m"],
        velocity_atol_m_s=tolerances["velocity_atol_m_s"],
        quaternion_atol=tolerances["quaternion_atol"],
        omega_atol_rad_s=tolerances["omega_atol_rad_s"],
        initial_step_s=tolerances["initial_step_s"],
        max_step_s=tolerances["max_step_s"],
    )


def scenario_arguments(document: dict) -> dict:
    """Every propagate() keyword argument for a qualified scenario."""
    return dict(
        initial=scenario_state(document),
        spacecraft=scenario_spacecraft(document),
        model=scenario_model(document),
        times_s=sample_times(document),
        environment=scenario_environment(document),
        epoch_et_s=0.0,
        options=scenario_options(document),
    )


def scenario_craft(document: dict, *, mass_kg: float | None = None,
                   inertia_scale: float = 1.0, surfaces: bool = True) -> leo.Spacecraft:
    """Scenario spacecraft with optional mass/inertia/surface changes."""
    craft = scenario_spacecraft(document)
    payload = document["spacecraft"]
    return leo.Spacecraft(
        mass_kg=payload["mass_kg"] if mass_kg is None else mass_kg,
        inertia_kg_m2=inertia_scale * craft.inertia_kg_m2,
        surfaces=craft.surfaces if surfaces else (),
    )


def heterogeneous_spacecraft(document: dict, count: int) -> list[leo.Spacecraft]:
    """``count`` spacecraft that differ in mass, inertia and surface area."""
    craft = []
    for index in range(count):
        surfaces = [
            leo.Surface(
                area_m2=(1.0 + 0.25 * index) * surface.area_m2,
                normal_body=surface.normal_body,
                center_of_pressure_body_m=surface.center_of_pressure_body_m,
                optical=surface.optical,
                aerodynamic=surface.aerodynamic,
            )
            for surface in scenario_spacecraft(document).surfaces
        ]
        craft.append(leo.Spacecraft(
            mass_kg=document["spacecraft"]["mass_kg"] * (1.0 + index),
            inertia_kg_m2=(1.0 + 0.5 * index)
            * scenario_spacecraft(document).inertia_kg_m2,
            surfaces=surfaces,
        ))
    return craft


def heterogeneous_models(count: int) -> list[leo.DynamicsModel]:
    """``count`` models that differ in gravity, radiation and attitude coupling."""
    models = []
    for index in range(count):
        models.append(leo.DynamicsModel(
            gravity="j2" if index % 2 else "point_mass",
            srp=leo.Contribution(force=True, torque=bool(index % 2)),
            drag=leo.Contribution(force=True, torque=True) if index % 3 == 0 else None,
            drag_law="flat_plate",
            gravity_gradient=bool(index % 2),
        ))
    return models


def heterogeneous_environments(document: dict, count: int) -> list:
    """``count`` environments that differ in Sun distance, scale and eclipse."""
    sun = document["environment"]["sun_position_m"]
    environments: list = []
    for index in range(count):
        environments.append(leo.Environment(
            sun=leo.SunEphemeris.constant([
                sun[0] * (1.0 + 0.01 * index), sun[1], sun[2]]),
            atmosphere=leo.ExponentialAtmosphere(scale=1.0 + 0.1 * index),
            eclipse="conical" if index % 2 else "cylindrical",
            atmosphere_rotation_rad_s=(0.0, 0.0, 7.292115e-5 * (1.0 + index)),
        ))
    return environments


def propagate_scenario(name: str, **overrides) -> leo.Trajectory:
    arguments = scenario_arguments(load_scenario(name))
    arguments.update(overrides)
    return leo.propagate(**arguments)


def diagnostics_arguments(document: dict, time_s: float) -> dict:
    """Every evaluate_dynamics() keyword argument for a scenario at time_s."""
    return dict(
        state=scenario_state(document),
        time_s=float(time_s),
        spacecraft=scenario_spacecraft(document),
        model=scenario_model(document),
        environment=scenario_environment(document),
        epoch_et_s=0.0,
    )


def member_initials(document: dict, members: int) -> np.ndarray:
    """(B, 13) initials that match the harness' own batch member offsets."""
    base = scenario_state(document).to_array()
    initials = np.repeat(base[None, :], members, axis=0)
    for index in range(members):
        initials[index, 0] += 0.25 * index
        initials[index, 4] += 1e-4 * index
    return initials


# --------------------------------------------------------------------------
# Comparison helpers
# --------------------------------------------------------------------------

def _order_key(values: np.ndarray) -> np.ndarray:
    """Monotone unsigned key for float64, so ULP distance is well defined."""
    bits = np.ascontiguousarray(values, dtype=np.float64).view(np.uint64)
    negative = (bits >> np.uint64(63)).astype(bool)
    return np.where(negative, ~bits, bits | np.uint64(1 << 63))


def ulp_distance(actual: np.ndarray, expected: np.ndarray) -> int:
    """Largest representable-number distance between two float64 arrays."""
    a = np.broadcast_to(np.asarray(actual, dtype=np.float64), np.shape(expected))
    keys_a, keys_b = _order_key(a), _order_key(expected)
    return int((np.maximum(keys_a, keys_b) - np.minimum(keys_a, keys_b)).max()) if keys_a.size else 0


def assert_identical(actual: np.ndarray, expected: np.ndarray, label: str) -> bool:
    """Require exact float64 values; harness JSON does not preserve signed zero."""
    actual = np.asarray(actual, dtype=np.float64)
    expected = np.asarray(expected, dtype=np.float64)
    assert actual.shape == expected.shape, f"{label}: {actual.shape} != {expected.shape}"
    assert np.array_equal(actual, expected), (
        f"{label}: native paths differ by up to {ulp_distance(actual, expected)} ULP"
    )
    return True


def pointwise_failures(actual: dict, expected: dict) -> list[dict]:
    """Compare one evaluated sample against a frozen pointwise golden row."""
    failures: list[dict] = []
    limits = POINTWISE_LIMITS

    def check(field: str, got, ref, absolute: float) -> None:
        got = np.atleast_1d(np.asarray(got, dtype=np.float64))
        ref = np.atleast_1d(np.asarray(ref, dtype=np.float64))
        assert got.shape == ref.shape, f"{field}: {got.shape} != {ref.shape}"
        assert np.isfinite(got).all(), f"{field}: nonfinite Python value"
        error = float(np.linalg.norm(got - ref))
        bound = absolute + limits["relative"] * float(np.linalg.norm(ref))
        if error > bound:
            failures.append(dict(field=field, error=error, limit=bound))

    assert actual["t"] == expected["t"], "pointwise time mismatch"
    for field in ("point_mass_m_s2", "j2_m_s2"):
        check(field, actual[field], expected[field], limits["acceleration_m_s2_absolute"])
    check("total_acceleration_m_s2", actual["total_acceleration_m_s2"],
          expected["total_acceleration_m_s2"], limits["acceleration_m_s2_absolute"])
    check("total_angular_acceleration_rad_s2",
          actual["total_angular_acceleration_rad_s2"],
          expected["total_angular_acceleration_rad_s2"],
          limits["angular_acceleration_rad_s2_absolute"])
    for name in ("srp", "drag", "albedo"):
        check(f"{name}_force_n", actual[f"{name}_force_n"],
              expected[f"{name}_force_n"], limits["force_n_absolute"])
        check(f"{name}_torque_nm", actual[f"{name}_torque_nm"],
              expected[f"{name}_torque_nm"], limits["torque_nm_absolute"])
    check("gravity_gradient_nm", actual["gravity_gradient_nm"],
          expected["gravity_gradient_nm"], limits["torque_nm_absolute"])
    for name, begin, end, absolute in (
        ("rhs_velocity", 0, 3, limits["velocity_m_s_absolute"]),
        ("rhs_acceleration", 3, 6, limits["acceleration_m_s2_absolute"]),
        ("rhs_quaternion", 6, 10, limits["quaternion_derivative_absolute"]),
        ("rhs_angular_acceleration", 10, 13,
         limits["angular_acceleration_rad_s2_absolute"]),
    ):
        check(name, actual["rhs"][begin:end], expected["rhs"][begin:end], absolute)
    for field, absolute in (("illumination", limits["illumination_absolute"]),
                            ("density_kg_m3", limits["density_kg_m3_absolute"])):
        if field in actual:
            check(field, actual[field], expected[field], absolute)
    return failures


def states_checksum(states: np.ndarray) -> float:
    """Reproduce the harness checksum so both paths can be shown to agree."""
    values = np.asarray(states, dtype=np.float64)
    weights = np.arange(1, values.shape[-1] + 1, dtype=np.float64)
    checksum = 0.0
    # Match the C++ scalar accumulation order, outside every measured region.
    for value in (values * weights).ravel():
        checksum += float(value)
    return checksum


# --------------------------------------------------------------------------
# Direct C++ reference harness
# --------------------------------------------------------------------------

def harness_candidates(option: str | None = None) -> list[Path]:
    """Where a configured ``leo_reference_dump`` binary may live."""
    candidates: list[Path] = []
    if option:
        candidates.append(Path(option))
    for name in ("LEO_REFERENCE_DUMP", "LEO_HARNESS"):
        value = os.environ.get(name)
        if value:
            candidates.append(Path(value))
    candidates.extend([
        ROOT / "build-reference" / "leo_reference_dump",
        ROOT / "build-reference" / "leo" / "leo_reference_dump",
        ROOT / "build-reference" / "tests" / "reference" / "leo_reference_dump",
        ROOT / "build" / "reference" / "leo_reference_dump",
    ])
    unique: list[Path] = []
    for candidate in candidates:
        resolved = candidate.resolve() if candidate.is_absolute() else candidate
        if resolved.is_file() and os.access(resolved, os.X_OK) and resolved not in unique:
            unique.append(resolved)
    return unique


def resolve_harness(option: str | None = None) -> Path:
    candidates = harness_candidates(option)
    if not candidates:
        raise FileNotFoundError(
            "leo_reference_dump not found; configure tests/reference "
            "(cmake -S tests/reference -B build-reference -DCMAKE_BUILD_TYPE=Release) "
            "and pass --harness or set LEO_REFERENCE_DUMP"
        )
    return candidates[0]


def run_harness(binary: Path, document: dict, extra: list[str] | None = None,
                points: Path | None = None, timeout: float = 3600.0) -> dict:
    """Execute the direct C++ path on a scenario document."""
    with tempfile.TemporaryDirectory(prefix="leo-python-benchmark-") as scratch:
        directory = Path(scratch)
        source = directory / "scenario.json"
        target = directory / "result.json"
        source.write_text(json.dumps(document, allow_nan=False))
        command = [str(binary), str(source), str(target)]
        if points is not None:
            command += ["--points", str(points)]
        command += list(extra or ())
        completed = subprocess.run(command, capture_output=True, text=True,
                                   timeout=timeout)
        if completed.returncode:
            raise RuntimeError(" ".join(command) + "\n" + completed.stderr)
        return json.loads(target.read_text())


def harness_samples(result: dict) -> np.ndarray:
    """(N, 13) float64 states from a harness result document."""
    return np.asarray([row["r"] + row["v"] + row["q"] + row["omega"]
                       for row in result["samples"]], dtype=np.float64)


def harness_trajectories(result: dict) -> np.ndarray:
    """(B, N, 13) float64 states from a harness ``--dump-batch`` result."""
    return np.asarray([harness_samples({"samples": trajectory})
                       for trajectory in result["trajectories"]], dtype=np.float64)


# --------------------------------------------------------------------------
# Concurrency helpers
# --------------------------------------------------------------------------

def thread_count() -> int:
    try:
        return len(os.sched_getaffinity(0))
    except AttributeError:  # pragma: no cover - non-Linux
        return os.cpu_count() or 1