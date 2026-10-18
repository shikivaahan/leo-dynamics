"""Public API surface: constants, orbital utilities, state and configuration.

These tests pin the exact user-facing names, units, shapes, validation and
immutability rules of the v0.1 interface. Physics is only touched through
analytic two-body identities, never re-implemented.
"""

from __future__ import annotations

import math

import numpy as np
import pytest

import leo_dynamics as leo


#: Element grid: circular/equatorial singular gauges are included on purpose.
ELEMENT_GRID = (
    (0.0, 0.0, 0.0, 0.0, 0.0),
    (0.0, 0.9, 0.3, 0.4, 1.0),
    (0.3, 0.0, 0.0, 0.0, 0.0),
    (0.3, 0.0, 1.1, 0.0, 2.5),
    (0.3, math.pi, 0.7, 0.2, 0.4),
    (0.9, 1.2, 2.0, 3.0, 5.0),
    (0.01, 0.5, -0.3, 4.0, -1.0),   # wrapped angles
)




def test_error_taxonomy():
    assert issubclass(leo.DomainError, ValueError)
    assert issubclass(leo.BatchPropagationError, RuntimeError)


# --------------------------------------------------------------------------
# Earth
# --------------------------------------------------------------------------

def test_earth_defaults_match_the_documented_contract():
    earth = leo.Earth()
    assert earth.mu_m3_s2 == 3.986004418e14
    assert earth.equatorial_radius_m == 6378136.3
    assert earth.j2 == 1.08262668e-3


def test_earth_accepts_explicit_constants():
    earth = leo.Earth(mu_m3_s2=1.0, equatorial_radius_m=2.0, j2=0.0)
    assert (earth.mu_m3_s2, earth.equatorial_radius_m, earth.j2) == (1.0, 2.0, 0.0)


@pytest.mark.parametrize(
    "constants",
    [
        dict(mu_m3_s2=0.0),
        dict(mu_m3_s2=-1.0),
        dict(equatorial_radius_m=0.0),
        dict(equatorial_radius_m=-1.0),
        dict(j2=-1.0),
        dict(mu_m3_s2=float("nan")),
    ],
)
def test_invalid_earth_constants_are_rejected(constants):
    with pytest.raises(ValueError):
        leo.Earth(**constants)


def test_earth_is_immutable():
    earth = leo.Earth()
    with pytest.raises((AttributeError, TypeError)):
        earth.mu_m3_s2 = 1.0


# --------------------------------------------------------------------------
# Orbital utilities
# --------------------------------------------------------------------------

@pytest.mark.parametrize("elements", ELEMENT_GRID)
def test_keplerian_cartesian_round_trip(elements):
    eccentricity, inclination, raan, argp, anomaly = elements
    arguments = dict(
        a_m=7.178137e6, e=eccentricity, i_rad=inclination,
        raan_rad=raan, arg_periapsis_rad=argp, true_anomaly_rad=anomaly,
    )
    position, velocity = leo.keplerian_to_cartesian(**arguments)
    assert position.shape == (3,) and velocity.shape == (3,)
    recovered = leo.cartesian_to_keplerian(position, velocity)
    assert isinstance(recovered, leo.KeplerianElements)
    again = leo.keplerian_to_cartesian(
        a_m=recovered.a_m, e=recovered.e, i_rad=recovered.i_rad,
        raan_rad=recovered.raan_rad, arg_periapsis_rad=recovered.arg_periapsis_rad,
        true_anomaly_rad=recovered.true_anomaly_rad,
    )
    np.testing.assert_allclose(again[0], position, rtol=1e-9, atol=1e-6)
    np.testing.assert_allclose(again[1], velocity, rtol=1e-9, atol=1e-9)
    assert recovered.a_m == pytest.approx(arguments["a_m"], rel=1e-12)
    assert recovered.e == pytest.approx(eccentricity, abs=1e-12)
    assert recovered.i_rad == pytest.approx(inclination, abs=1e-12)
    # Vis-viva is an independent two-body identity, not a native conversion.
    radius = float(np.linalg.norm(position))
    speed = float(np.linalg.norm(velocity))
    expected = leo.Earth().mu_m3_s2 * (2.0 / radius - 1.0 / arguments["a_m"])
    assert speed * speed == pytest.approx(expected, rel=1e-12)


def test_keplerian_elements_expose_their_values():
    elements = leo.KeplerianElements(
        a_m=7.0e6, e=0.01, i_rad=0.5, raan_rad=0.2,
        arg_periapsis_rad=0.3, true_anomaly_rad=0.4,
    )
    assert elements.a_m == 7.0e6
    assert elements.e == 0.01
    assert elements.i_rad == 0.5
    assert elements.raan_rad == 0.2
    assert elements.arg_periapsis_rad == 0.3
    assert elements.true_anomaly_rad == 0.4
    assert "KeplerianElements" in repr(elements)


@pytest.mark.parametrize(
    "arguments",
    [
        dict(a_m=0.0, e=0.0, i_rad=0.0, raan_rad=0.0, arg_periapsis_rad=0.0,
             true_anomaly_rad=0.0),
        dict(a_m=-1.0, e=0.0, i_rad=0.0, raan_rad=0.0, arg_periapsis_rad=0.0,
             true_anomaly_rad=0.0),
        dict(a_m=7.0e6, e=1.0, i_rad=0.0, raan_rad=0.0, arg_periapsis_rad=0.0,
             true_anomaly_rad=0.0),          # parabolic
        dict(a_m=7.0e6, e=1.2, i_rad=0.0, raan_rad=0.0, arg_periapsis_rad=0.0,
             true_anomaly_rad=0.0),          # hyperbolic
        dict(a_m=7.0e6, e=-0.1, i_rad=0.0, raan_rad=0.0, arg_periapsis_rad=0.0,
             true_anomaly_rad=0.0),
        dict(a_m=7.0e6, e=0.0, i_rad=3.2, raan_rad=0.0, arg_periapsis_rad=0.0,
             true_anomaly_rad=0.0),          # inclination out of [0, pi]
        dict(a_m=float("nan"), e=0.0, i_rad=0.0, raan_rad=0.0,
             arg_periapsis_rad=0.0, true_anomaly_rad=0.0),
    ],
)
def test_unsupported_orbit_domain_is_rejected(arguments):
    with pytest.raises(ValueError):
        leo.keplerian_to_cartesian(**arguments)
    with pytest.raises(ValueError):
        leo.KeplerianElements(**arguments)


def test_unbound_cartesian_state_is_rejected():
    escape = leo.keplerian_to_cartesian(
        a_m=7.0e6, e=0.9, i_rad=0.3, raan_rad=0.0, arg_periapsis_rad=0.0,
        true_anomaly_rad=0.0,
    )
    position = np.asarray(escape[0])
    velocity = np.asarray(escape[1])
    velocity = velocity * math.sqrt(2.0)  # energy becomes positive
    with pytest.raises(ValueError):
        leo.cartesian_to_keplerian(position, velocity)


# --------------------------------------------------------------------------
# State
# --------------------------------------------------------------------------

def test_state_packing_order_is_documented():
    state = leo.State(
        position_m=[1.0, 2.0, 3.0],
        velocity_m_s=[4.0, 5.0, 6.0],
        quaternion_wxyz=[7.0, 8.0, 9.0, 10.0],
        omega_body_rad_s=[11.0, 12.0, 13.0],
    )
    packed = state.to_array()
    assert packed.shape == (13,)
    # [rx, ry, rz, vx, vy, vz, qw, qx, qy, qz, wx, wy, wz], quaternion normalised.
    np.testing.assert_allclose(packed[:6], [1, 2, 3, 4, 5, 6])
    assert np.linalg.norm(packed[6:10]) == pytest.approx(1.0)
    np.testing.assert_allclose(packed[10:13], [11, 12, 13])
    np.testing.assert_array_equal(leo.State.from_array(packed).to_array(), packed)


def test_state_defaults():
    state = leo.State(position_m=[1.0, 0.0, 0.0], velocity_m_s=[0.0, 1.0, 0.0])
    np.testing.assert_array_equal(state.quaternion_wxyz, [1.0, 0.0, 0.0, 0.0])
    np.testing.assert_array_equal(state.omega_body_rad_s, [0.0, 0.0, 0.0])


def test_state_accepts_arrays_lists_and_tuples():
    array = np.array([1.0, 0.0, 0.0])
    from_array = leo.State(position_m=array, velocity_m_s=(0.0, 1.0, 0.0))
    from_list = leo.State(position_m=[1.0, 0.0, 0.0], velocity_m_s=[0.0, 1.0, 0.0])
    np.testing.assert_array_equal(from_array.to_array(), from_list.to_array())


def test_state_properties_are_independent_copies():
    state = leo.State(position_m=[1.0, 2.0, 3.0], velocity_m_s=[4.0, 5.0, 6.0])
    before = state.to_array().copy()
    for values in (state.position_m, state.velocity_m_s,
                   state.quaternion_wxyz, state.omega_body_rad_s):
        try:  # a read-only view is an equally safe contract
            values[...] = 99.0
        except ValueError:
            pass
    np.testing.assert_array_equal(state.to_array(), before)


@pytest.mark.parametrize(
    "arguments",
    [
        dict(position_m=[1.0, 2.0], velocity_m_s=[0.0, 1.0, 0.0]),
        dict(position_m=[1.0, 2.0, 3.0, 4.0], velocity_m_s=[0.0, 1.0, 0.0]),
        dict(position_m=[1.0, float("nan"), 3.0], velocity_m_s=[0.0, 1.0, 0.0]),
        dict(position_m=[1.0, 2.0, 3.0], velocity_m_s=[0.0, float("inf"), 0.0]),
        dict(position_m=[1.0, 2.0, 3.0], velocity_m_s=[0.0, 1.0, 0.0],
             quaternion_wxyz=[1.0, 0.0, 0.0]),
        dict(position_m=[1.0, 2.0, 3.0], velocity_m_s=[0.0, 1.0, 0.0],
             quaternion_wxyz=[0.0, 0.0, 0.0, 0.0]),
        dict(position_m=[1.0, 2.0, 3.0], velocity_m_s=[0.0, 1.0, 0.0],
             omega_body_rad_s=[float("nan"), 0.0, 0.0]),
    ],
)
def test_invalid_states_are_rejected(arguments):
    with pytest.raises(ValueError):
        leo.State(**arguments)
    with pytest.raises(ValueError):
        leo.State.from_array(np.asarray(arguments["position_m"], dtype=float))


# --------------------------------------------------------------------------
# Spacecraft configuration
# --------------------------------------------------------------------------

def _inertia(scale: float = 1.0) -> np.ndarray:
    return scale * np.diag([4.0, 6.0, 9.0])


def test_spacecraft_round_trip():
    surfaces = (
        leo.Surface(
            area_m2=0.5,
            normal_body=[1.0, 0.0, 0.0],
            center_of_pressure_body_m=[0.1, 0.0, 0.0],
            optical=leo.OpticalProperties(specular=0.3, diffuse=0.2),
            aerodynamic=leo.AerodynamicProperties(
                drag_coefficient=2.2, accommodation=0.9
            ),
        ),
    )
    craft = leo.Spacecraft(mass_kg=12.5, inertia_kg_m2=_inertia(), surfaces=surfaces)
    assert craft.mass_kg == 12.5
    np.testing.assert_allclose(craft.inertia_kg_m2, _inertia())
    assert len(craft.surfaces) == 1
    surface = craft.surfaces[0]
    assert surface.area_m2 == 0.5
    np.testing.assert_array_equal(surface.normal_body, [1.0, 0.0, 0.0])
    np.testing.assert_array_equal(surface.center_of_pressure_body_m, [0.1, 0.0, 0.0])
    assert surface.optical.specular == 0.3
    assert surface.optical.diffuse == 0.2
    assert surface.aerodynamic.drag_coefficient == 2.2
    assert surface.aerodynamic.accommodation == 0.9


def test_spacecraft_defaults_to_no_surfaces():
    craft = leo.Spacecraft(mass_kg=1.0, inertia_kg_m2=_inertia())
    assert len(craft.surfaces) == 0


@pytest.mark.parametrize(
    "kwargs",
    [
        dict(mass_kg=0.0, inertia_kg_m2=_inertia()),
        dict(mass_kg=-1.0, inertia_kg_m2=_inertia()),
        dict(mass_kg=float("nan"), inertia_kg_m2=_inertia()),
        dict(mass_kg=1.0, inertia_kg_m2=np.diag([1.0, 2.0, 0.0])),      # singular
        dict(mass_kg=1.0, inertia_kg_m2=np.diag([1.0, -2.0, 3.0])),    # indefinite
        dict(mass_kg=1.0, inertia_kg_m2=np.zeros((3, 3))),
        dict(mass_kg=1.0, inertia_kg_m2=np.array([[1.0, 2.0, 0.0],
                                                   [0.0, 2.0, 0.0],
                                                   [0.0, 0.0, 3.0]])),  # asymmetric
        dict(mass_kg=1.0, inertia_kg_m2=np.array([4.0, 6.0, 9.0])),    # not a matrix
        dict(mass_kg=1.0, inertia_kg_m2=np.full((3, 3), np.nan)),
    ],
)
def test_invalid_spacecraft_is_rejected(kwargs):
    with pytest.raises(ValueError):
        leo.Spacecraft(**kwargs)


@pytest.mark.parametrize(
    "factory",
    [
        lambda: leo.Surface(area_m2=0.0, normal_body=[1.0, 0.0, 0.0],
                            center_of_pressure_body_m=[0.0, 0.0, 0.0]),
        lambda: leo.Surface(area_m2=-1.0, normal_body=[1.0, 0.0, 0.0],
                            center_of_pressure_body_m=[0.0, 0.0, 0.0]),
        lambda: leo.Surface(area_m2=1.0, normal_body=[0.5, 0.0, 0.0],
                            center_of_pressure_body_m=[0.0, 0.0, 0.0]),
        lambda: leo.Surface(area_m2=1.0, normal_body=[0.0, 0.0, 0.0],
                            center_of_pressure_body_m=[0.0, 0.0, 0.0]),
        lambda: leo.Surface(area_m2=1.0, normal_body=[1.0, 0.0, 0.0],
                            center_of_pressure_body_m=[float("inf"), 0.0, 0.0]),
        lambda: leo.Surface(area_m2=1.0, normal_body=[1.0, 0.0, 0.0],
                            center_of_pressure_body_m=[0.0, 0.0, 0.0],
                            optical=leo.OpticalProperties(specular=0.8,
                                                          diffuse=0.5)),
        lambda: leo.Surface(area_m2=1.0, normal_body=[1.0, 0.0, 0.0],
                            center_of_pressure_body_m=[0.0, 0.0, 0.0],
                            optical=leo.OpticalProperties(specular=-0.1)),
        lambda: leo.Surface(area_m2=1.0, normal_body=[1.0, 0.0, 0.0],
                            center_of_pressure_body_m=[0.0, 0.0, 0.0],
                            aerodynamic=leo.AerodynamicProperties(
                                drag_coefficient=-1.0)),
        lambda: leo.Surface(area_m2=1.0, normal_body=[1.0, 0.0, 0.0],
                            center_of_pressure_body_m=[0.0, 0.0, 0.0],
                            aerodynamic=leo.AerodynamicProperties(
                                accommodation=1.5)),
    ],
)
def test_invalid_surface_is_rejected(factory):
    """The invalid configuration is built inside the guard, never at import."""
    with pytest.raises(ValueError):
        factory()


def test_configuration_objects_are_immutable():
    craft = leo.Spacecraft(mass_kg=1.0, inertia_kg_m2=_inertia())
    surface = leo.Surface(area_m2=1.0, normal_body=[0.0, 0.0, 1.0],
                          center_of_pressure_body_m=[0.0, 0.0, 0.0])
    for target, attribute in ((craft, "mass_kg"), (craft, "surfaces"),
                              (surface, "area_m2")):
        with pytest.raises((AttributeError, TypeError)):
            setattr(target, attribute, 1.0)


# --------------------------------------------------------------------------
# Dynamics selection
# --------------------------------------------------------------------------

def test_dynamics_model_defaults_and_switches():
    # Independent force/torque switches are accepted in every combination.
    for force in (False, True):
        for torque in (False, True):
            model = leo.DynamicsModel(srp=leo.Contribution(force=force, torque=torque))
            assert model is not None
    leo.DynamicsModel()                      # defaults: point mass, no models
    leo.DynamicsModel(gravity="j2", gravity_gradient=True)
    leo.DynamicsModel(drag=leo.Contribution(force=True, torque=True),
                      drag_law="sentman")
    leo.DynamicsModel(albedo=leo.Contribution(force=True, torque=True),
                      earth_albedo=0.0)


@pytest.mark.parametrize(
    "kwargs",
    [
        dict(gravity="central"),
        dict(gravity="J2"),
        dict(drag_law="newton"),
        dict(drag_law="drag"),
        dict(earth_albedo=1.5),
        dict(earth_albedo=-0.1),
    ],
)
def test_invalid_dynamics_selection_is_rejected(kwargs):
    with pytest.raises(ValueError):
        leo.DynamicsModel(**kwargs)


def test_enabled_drag_needs_an_explicit_law():
    """The native rule is enforced when the model is built, not at run time."""
    with pytest.raises(ValueError):
        leo.DynamicsModel(drag=leo.Contribution(force=True, torque=True),
                          drag_law="none")
    # Selecting no drag at all keeps drag_law="none" valid.
    leo.DynamicsModel(drag_law="none")


def test_contribution_switches_are_independent():
    assert leo.Contribution(force=True, torque=False) is not None
    assert leo.Contribution(force=False, torque=True) is not None
    assert leo.Contribution() is not None       # both switches default off


# --------------------------------------------------------------------------
# Environment selection
# --------------------------------------------------------------------------

def test_environment_construction_accepts_every_documented_option():
    # Defaults and every documented keyword must construct; the selected values
    # are exercised behaviourally in test_environment.py.
    leo.Environment()
    leo.Environment(sun=leo.SunEphemeris.constant([1.5e11, 0.0, 0.0]))
    leo.Environment(atmosphere=leo.ExponentialAtmosphere())
    leo.Environment(
        sun=leo.SunEphemeris.constant([1.5e11, 0.0, 0.0]),
        atmosphere=leo.ExponentialAtmosphere(scale=1.0),
        eclipse="conical",
        atmosphere_rotation_rad_s=[0.0, 0.0, 7.292115e-5],
    )




def test_unsupported_eclipse_model_is_rejected():
    with pytest.raises(ValueError):
        leo.Environment(eclipse="umbra_penumbra")


def test_exponential_atmosphere_scale_validation():
    with pytest.raises(ValueError):
        leo.ExponentialAtmosphere(scale=-1.0)
    with pytest.raises(ValueError):
        leo.ExponentialAtmosphere(scale=float("inf"))


def test_constant_sun_requires_a_finite_nonzero_position():
    with pytest.raises(ValueError):
        leo.SunEphemeris.constant([0.0, 0.0, 0.0])
    with pytest.raises(ValueError):
        leo.SunEphemeris.constant([float("nan"), 0.0, 0.0])
    with pytest.raises(ValueError):
        leo.SunEphemeris.constant([1.0, 2.0])


@pytest.mark.parametrize("values", [["1"] * 13, [1 + 1j] * 13, [object()] * 13])
def test_non_numeric_sequences_are_not_coerced_into_states(values):
    with pytest.raises(ValueError):
        leo.State.from_array(values)
    with pytest.raises(ValueError):
        leo.propagate(initial=values,
                      spacecraft=leo.Spacecraft(mass_kg=1, inertia_kg_m2=np.eye(3)),
                      model=leo.DynamicsModel(), times_s=[0., 1.])


@pytest.mark.parametrize("times", [["0", "1"], [0j, 1j]])
def test_non_numeric_schedule_sequences_are_rejected(times):
    with pytest.raises(ValueError):
        leo.propagate(initial=leo.State(position_m=[7e6, 0, 0], velocity_m_s=[0, 7500, 0]),
                      spacecraft=leo.Spacecraft(mass_kg=1, inertia_kg_m2=np.eye(3)),
                      model=leo.DynamicsModel(), times_s=times)