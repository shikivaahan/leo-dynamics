"""LEO orbital dynamics: the public Python interface.

Everything numerical runs in the compiled private extension ``leo_dynamics._core``;
this module only re-exports it and accepts NumPy arrays where that is more natural
than a configuration object.

Units and frames
----------------
Distances are metres, velocities metres per second, times seconds, angles radians
and masses kilograms. Positions, velocities and Sun geometry are Earth-centred J2000.
Torques, angular rates, body angular rates and surface geometry are body frame.
Attitude is a Hamilton scalar-first active body-to-J2000 quaternion ordered
``(w, x, y, z)``. ``epoch_et_s`` is TDB ephemeris seconds past J2000, never a UTC or
Unix timestamp.

Errors
------
``ValueError`` for invalid inputs or configuration, ``DomainError`` (a ``ValueError``
subclass) for inputs outside the supported domain or ephemeris coverage,
``RuntimeError`` for integration failure, ``BatchPropagationError`` for a failed
batch trajectory, and ``OSError`` for file system failures.
"""

from __future__ import annotations

import numpy as np

try:
    from . import _core
except ImportError as exc:  # pragma: no cover - development build guidance
    raise ImportError(
        "leo_dynamics._core has not been built. Configure the project with "
        "-DLEO_BUILD_PYTHON_BINDINGS=ON, build it, and put <build>/python on PYTHONPATH."
    ) from exc

from ._core import (
    AerodynamicProperties,
    Analysis,
    BatchPropagationError,
    BatchTrajectory,
    Contribution,
    DomainError,
    DynamicsContributions,
    DynamicsEvaluation,
    DynamicsModel,
    Earth,
    Environment,
    ExponentialAtmosphere,
    KeplerianElements,
    OpticalProperties,
    SolverOptions,
    Spacecraft,
    State,
    SunEphemeris,
    Surface,
    Trajectory,
    Wrench,
    analysis,
    cartesian_to_keplerian,
    keplerian_to_cartesian,
)

__version__ = "0.1.0"

__all__ = [
    "AerodynamicProperties",
    "Analysis",
    "BatchPropagationError",
    "BatchTrajectory",
    "Contribution",
    "DomainError",
    "DynamicsContributions",
    "DynamicsEvaluation",
    "DynamicsModel",
    "Earth",
    "Environment",
    "ExponentialAtmosphere",
    "KeplerianElements",
    "OpticalProperties",
    "SolverOptions",
    "Spacecraft",
    "State",
    "SunEphemeris",
    "Surface",
    "Trajectory",
    "Wrench",
    "analysis",
    "cartesian_to_keplerian",
    "evaluate_dynamics",
    "keplerian_to_cartesian",
    "propagate",
    "propagate_batch",
]


def _state(value) -> State:
    """Accept a State, or any (13,) array or sequence packed the same way."""
    if isinstance(value, State):
        return value
    return State.from_array(value)


def _initials(value):
    """Accept a (B, 13) array, or a sequence of States and (13,) arrays."""
    if isinstance(value, np.ndarray):
        return value
    return [_state(item) for item in value]


def propagate(
    *,
    initial,
    spacecraft,
    model,
    times_s,
    earth=None,
    environment=None,
    epoch_et_s=0.0,
    options=None,
) -> Trajectory:
    """Propagate one trajectory through the native C++ engine.

    ``initial`` is a :class:`State` or a ``(13,)`` array packed
    ``[r, v, q(w, x, y, z), omega_body]``. ``times_s`` is a finite, strictly
    increasing ``(N,)`` schedule starting at exactly zero, as the native solver
    requires. ``earth``, ``environment``, ``epoch_et_s`` and ``options`` default to
    the standard Earth constants, no acquisition, a J2000 epoch and the qualified
    default solver settings.
    """
    return _core.propagate(
        initial=_state(initial),
        spacecraft=spacecraft,
        model=model,
        times_s=times_s,
        earth=earth,
        environment=environment,
        epoch_et_s=epoch_et_s,
        options=options,
    )


def propagate_batch(
    *,
    initials,
    spacecraft,
    model,
    times_s,
    earth=None,
    environment=None,
    epoch_et_s=0.0,
    options=None,
    workers=1,
) -> BatchTrajectory:
    """Propagate B independent trajectories with the native bounded scheduler.

    ``initials`` is a ``(B, 13)`` array or a sequence of States and ``(13,)``
    arrays. ``spacecraft``, ``model`` and ``environment`` accept either one object,
    broadcast to every trajectory, or a sequence of exactly B objects; sequences are
    never cycled, truncated or padded. One schedule, epoch and solver configuration
    is shared by the batch. ``workers`` must be a positive integer and is capped
    internally by the trajectory count; ``workers=1`` runs inline without creating a
    thread. Trajectories do not interact.
    """
    return _core.propagate_batch(
        initials=_initials(initials),
        spacecraft=spacecraft,
        model=model,
        times_s=times_s,
        earth=earth,
        environment=environment,
        epoch_et_s=epoch_et_s,
        options=options,
        workers=workers,
    )


def evaluate_dynamics(
    *,
    state,
    time_s,
    spacecraft,
    model,
    earth=None,
    environment=None,
    epoch_et_s=0.0,
) -> DynamicsEvaluation:
    """Evaluate the selected models at one point, without integrating.

    ``time_s`` is elapsed seconds since ``epoch_et_s``. The result carries the
    selected force and torque contributions, the total translational acceleration
    and the total angular acceleration, exactly as the native model computes them.
    """
    return _core.evaluate_dynamics(
        state=_state(state),
        time_s=time_s,
        spacecraft=spacecraft,
        model=model,
        earth=earth,
        environment=environment,
        epoch_et_s=epoch_et_s,
    )