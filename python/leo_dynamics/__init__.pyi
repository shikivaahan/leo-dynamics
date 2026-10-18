"""Type stubs for the public leo_dynamics interface.

Units are SI: metres, seconds, radians, kilograms, newtons. Positions, velocities
and Sun geometry are Earth-centred J2000; torques, angular rates and surface
geometry are body frame; attitude is a Hamilton scalar-first active
body-to-J2000 quaternion ``(w, x, y, z)``. ``epoch_et_s`` is TDB ephemeris
seconds past J2000.
"""

from __future__ import annotations

from os import PathLike
from typing import Any, Iterator, Literal, Sequence

import numpy as np
from numpy.typing import ArrayLike, NDArray

FloatArray = NDArray[np.float64]
Record = np.void

__version__: str

class DomainError(ValueError): ...

class BatchPropagationError(RuntimeError):
    trajectory_index: int
    cause: BaseException | None

class KeplerianElements:
    def __init__(self, *, a_m: float, e: float, i_rad: float, raan_rad: float,
                 arg_periapsis_rad: float, true_anomaly_rad: float) -> None: ...
    a_m: float
    e: float
    i_rad: float
    raan_rad: float
    arg_periapsis_rad: float
    true_anomaly_rad: float

class Earth:
    def __init__(self, *, mu_m3_s2: float = 3.986004418e14,
                 equatorial_radius_m: float = 6378136.3, j2: float = 1.08262668e-3) -> None: ...
    mu_m3_s2: float
    equatorial_radius_m: float
    j2: float

class State:
    def __init__(self, *, position_m: ArrayLike, velocity_m_s: ArrayLike,
                 quaternion_wxyz: ArrayLike = (1, 0, 0, 0),
                 omega_body_rad_s: ArrayLike = (0, 0, 0)) -> None: ...
    position_m: FloatArray
    velocity_m_s: FloatArray
    quaternion_wxyz: FloatArray
    omega_body_rad_s: FloatArray
    def to_array(self) -> FloatArray: ...
    @staticmethod
    def from_array(x: FloatArray | Sequence[float]) -> State: ...

class OpticalProperties:
    def __init__(self, specular: float = 0.0, diffuse: float = 0.0) -> None: ...
    specular: float
    diffuse: float

class AerodynamicProperties:
    def __init__(self, drag_coefficient: float = 2.2, accommodation: float = 0.9) -> None: ...
    drag_coefficient: float
    accommodation: float

class Surface:
    def __init__(self, *, area_m2: float, normal_body: ArrayLike,
                 center_of_pressure_body_m: ArrayLike,
                 optical: OpticalProperties | None = None,
                 aerodynamic: AerodynamicProperties | None = None) -> None: ...
    area_m2: float
    normal_body: FloatArray
    center_of_pressure_body_m: FloatArray
    optical: OpticalProperties
    aerodynamic: AerodynamicProperties

class Spacecraft:
    def __init__(self, *, mass_kg: float, inertia_kg_m2: ArrayLike,
                 surfaces: Sequence[Surface] = ()) -> None: ...
    mass_kg: float
    inertia_kg_m2: FloatArray
    surfaces: list[Surface]

class Contribution:
    def __init__(self, force: bool = False, torque: bool = False) -> None: ...
    force: bool
    torque: bool
    def enabled(self) -> bool: ...

class DynamicsModel:
    def __init__(self, *, gravity: Literal["point_mass", "j2"] = "point_mass",
                 srp: Contribution | None = None, drag: Contribution | None = None,
                 drag_law: Literal["none", "flat_plate", "sentman"] = "none",
                 albedo: Contribution | None = None, earth_albedo: float = 0.3,
                 gravity_gradient: bool = False) -> None: ...
    gravity: Literal["point_mass", "j2"]
    srp: Contribution | None
    drag: Contribution | None
    drag_law: Literal["none", "flat_plate", "sentman"]
    albedo: Contribution | None
    earth_albedo: float
    gravity_gradient: bool
    def needs_environment(self) -> bool: ...

class SunEphemeris:
    @staticmethod
    def constant(position_m: FloatArray | Sequence[float]) -> SunEphemeris: ...
    @staticmethod
    def tabulated(
        *,
        et_seconds: FloatArray | Sequence[float],
        position_m: FloatArray,
        velocity_m_s: FloatArray,
    ) -> SunEphemeris: ...
    kind: str
    sample_count: int
    start_et_s: float
    end_et_s: float
    def sample(self, et_seconds: float) -> tuple[FloatArray, FloatArray]: ...

class ExponentialAtmosphere:
    def __init__(self, scale: float = 1.0) -> None: ...
    min_altitude_m: float
    max_altitude_m: float
    scale: float
    def density_kg_m3(self, altitude_m: float) -> float: ...

class Environment:
    def __init__(self, *, sun: SunEphemeris | None = None,
                 atmosphere: ExponentialAtmosphere | None = None,
                 eclipse: Literal["none", "cylindrical", "conical"] = "conical",
                 atmosphere_rotation_rad_s: ArrayLike = (0, 0, 7.292115e-5)) -> None: ...
    sun: SunEphemeris | None
    eclipse: Literal["none", "cylindrical", "conical"]
    atmosphere_rotation_rad_s: FloatArray
    has_atmosphere: bool
    def density_kg_m3(self, altitude_m: float) -> float: ...

class SolverOptions:
    def __init__(self, *, rtol: float = 1e-10, position_atol_m: float = 1e-3,
                 velocity_atol_m_s: float = 1e-6, quaternion_atol: float = 1e-12,
                 omega_atol_rad_s: float = 1e-12, initial_step_s: float = 1.0,
                 max_step_s: float = 15.0, max_attempts: int = 1_000_000) -> None: ...
    rtol: float
    position_atol_m: float
    velocity_atol_m_s: float
    quaternion_atol: float
    omega_atol_rad_s: float
    initial_step_s: float
    max_step_s: float
    max_attempts: int

class Wrench:
    force_n: FloatArray
    torque_nm: FloatArray

class DynamicsContributions:
    point_mass_m_s2: FloatArray
    j2_m_s2: FloatArray
    gravity_gradient_nm: FloatArray
    srp: Wrench
    drag: Wrench
    albedo: Wrench
    non_gravitational: Wrench
    total_acceleration_m_s2: FloatArray
    total_angular_acceleration_rad_s2: FloatArray

class DynamicsEvaluation:
    state_derivative: FloatArray
    contributions: DynamicsContributions
    point_mass_m_s2: FloatArray
    j2_m_s2: FloatArray
    gravity_gradient_nm: FloatArray
    srp: Wrench
    drag: Wrench
    albedo: Wrench
    non_gravitational: Wrench
    total_acceleration_m_s2: FloatArray
    total_angular_acceleration_rad_s2: FloatArray

class Analysis:
    """Native diagnostics for one trajectory.

    Every array is a read-only, owning, one-dimensional NumPy structured array.
    ``orbital``/``apsides``/``orbits``/``environment``/``attitude``/``work``/
    ``orbit_work`` hold the documented record fields; ``surfaces`` holds one row
    per sample and surface. NaN marks diagnostics the native model cannot supply.
    """

    orbital: np.ndarray[Any, np.dtype[Record]]
    apsides: np.ndarray[Any, np.dtype[Record]]
    orbits: np.ndarray[Any, np.dtype[Record]]
    environment: np.ndarray[Any, np.dtype[Record]]
    attitude: np.ndarray[Any, np.dtype[Record]]
    surfaces: np.ndarray[Any, np.dtype[Record]]
    work: np.ndarray[Any, np.dtype[Record]]
    orbit_work: np.ndarray[Any, np.dtype[Record]]
    def export_csv(self, directory: str | PathLike[str]) -> None: ...

class Trajectory:
    epoch_et_s: float
    times_s: FloatArray
    states: FloatArray
    position_m: FloatArray
    velocity_m_s: FloatArray
    quaternion_wxyz: FloatArray
    omega_body_rad_s: FloatArray
    def orbital_elements(self) -> np.ndarray[Any, np.dtype[Record]]: ...
    def apsides(self) -> np.ndarray[Any, np.dtype[Record]]: ...
    def orbits(self) -> np.ndarray[Any, np.dtype[Record]]: ...
    def analyze(self) -> Analysis: ...
    def to_csv(self, path: str | PathLike[str]) -> None: ...

class BatchTrajectory:
    epoch_et_s: float
    times_s: FloatArray
    states: FloatArray
    position_m: FloatArray
    velocity_m_s: FloatArray
    quaternion_wxyz: FloatArray
    omega_body_rad_s: FloatArray
    def __len__(self) -> int: ...
    def __getitem__(self, index: int) -> Trajectory: ...
    def __iter__(self) -> Iterator[Trajectory]: ...

class _AnalysisModule:
    """``leo.analysis``: trajectory analysis helpers mirroring ``leo::analysis``."""

    def orbital(self, trajectory: Trajectory) -> np.ndarray[Any, np.dtype[Record]]: ...
    def apsides(self, trajectory: Trajectory) -> np.ndarray[Any, np.dtype[Record]]: ...
    def orbits(self, trajectory: Trajectory) -> np.ndarray[Any, np.dtype[Record]]: ...
    def environment(self, trajectory: Trajectory) -> np.ndarray[Any, np.dtype[Record]]: ...
    def attitude(self, trajectory: Trajectory) -> np.ndarray[Any, np.dtype[Record]]: ...
    def work(self, trajectory: Trajectory) -> np.ndarray[Any, np.dtype[Record]]: ...
    def export_csv(self, analysis_or_trajectory: Analysis | Trajectory,
                   directory: str | PathLike[str]) -> None: ...

analysis: _AnalysisModule

def keplerian_to_cartesian(
    *,
    a_m: float,
    e: float,
    i_rad: float,
    raan_rad: float,
    arg_periapsis_rad: float,
    true_anomaly_rad: float,
    earth: Earth | None = None,
) -> tuple[FloatArray, FloatArray]: ...
def cartesian_to_keplerian(
    position_m: FloatArray | Sequence[float],
    velocity_m_s: FloatArray | Sequence[float],
    *,
    earth: Earth | None = None,
) -> KeplerianElements: ...
def propagate(
    *,
    initial: State | FloatArray | Sequence[float],
    spacecraft: Spacecraft,
    model: DynamicsModel,
    times_s: FloatArray | Sequence[float],
    earth: Earth | None = None,
    environment: Environment | None = None,
    epoch_et_s: float = 0.0,
    options: SolverOptions | None = None,
) -> Trajectory: ...
def propagate_batch(
    *,
    initials: FloatArray | Sequence[State | FloatArray],
    spacecraft: Spacecraft | Sequence[Spacecraft],
    model: DynamicsModel | Sequence[DynamicsModel],
    times_s: FloatArray | Sequence[float],
    earth: Earth | None = None,
    environment: Environment | Sequence[Environment | None] | None = None,
    epoch_et_s: float = 0.0,
    options: SolverOptions | None = None,
    workers: int = 1,
) -> BatchTrajectory: ...
def evaluate_dynamics(
    *,
    state: State | FloatArray | Sequence[float],
    time_s: float,
    spacecraft: Spacecraft,
    model: DynamicsModel,
    earth: Earth | None = None,
    environment: Environment | None = None,
    epoch_et_s: float = 0.0,
) -> DynamicsEvaluation: ...