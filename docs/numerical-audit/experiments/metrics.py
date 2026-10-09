#!/usr/bin/env python3
"""Physical observables for the coupled leo-dynamics experiment matrices.

Consumes the machine-readable JSON emitted by the audit harness. It performs no
force or torque physics of its own: every environmental quantity it reports is
either read from the ``loads`` series the harness already evaluates, or derived
from the reported state by an explicitly documented geometric construction.

Two entry points are used by the orchestration layer:

``summarize(result, ...)``
    Flat scalar observables of a single propagation, optionally truncated to an
    analysis horizon, optionally with a compact time series.

``compare_observables(base, result, ...)``
    Terminal, maximum, RMS and (optionally) time-resolved differences between two
    propagations, including orbital RTN decomposition and SO(3) relative rotation
    vectors.

Design rules enforced here:

* Rotations are compared with the SO(3) relative rotation vector, not by
  subtracting quaternion components. Quaternion sign is canonicalised first, so
  ``q`` and ``-q`` give an identically zero difference.
* Unit axes that are undefined (zero momentum, zero spin cone, absent Sun) and
  normalisations that are ill-conditioned (orbit energy at or above zero) are
  reported as JSON ``null`` together with an explicit ``*_reason`` string. They
  are never silently replaced by zero.
* Cumulative quantities integrated over the sampled data are reported on the
  native output cadence and again after local cubic quadrature refinement, so a
  cadence-limited integral can be distinguished from a converged one.
* Orbital elements are point-mass osculating diagnostics. Under a full J2 field
  they are not conserved and are labelled as diagnostics wherever they appear.

Standard library only; no NumPy, no SciPy, no dependency on the propagator.
"""

from __future__ import annotations

import math

MU_EARTH_M3_S2 = 3.986004418e14

#: Fixed positional layout of one row of the harness ``samples`` array.
SAMPLE_FIELDS = (
    "time_s",
    "r_x_m", "r_y_m", "r_z_m",
    "v_x_m_s", "v_y_m_s", "v_z_m_s",
    "q_w", "q_x", "q_y", "q_z",
    "omega_x_rad_s", "omega_y_rad_s", "omega_z_rad_s",
    "illumination",
    "srp_force_x_n", "srp_force_y_n", "srp_force_z_n",
    "srp_torque_x_nm", "srp_torque_y_nm", "srp_torque_z_nm",
    "srp_work_j",
)
SAMPLE_WIDTH = len(SAMPLE_FIELDS)

#: Names in the harness ``loads`` dictionaries required for the load-derived block.
LOAD_VECTOR_KEYS = (
    "sun_position_m",
    "drag_force_n",
    "albedo_force_n",
    "gravity_gradient_nm",
    "drag_torque_nm",
    "albedo_torque_nm",
    "angular_momentum_inertial",
    "pointing_axis_inertial",
)
LOAD_SCALAR_KEYS = ("projected_area_m2", "density_kg_m3")

#: Numerical guards. Chosen far above double-precision round-off and far below any
#: physically meaningful quantity in these scenarios; they exist to turn
#: "undefined" into an explicit ``null`` rather than to filter real signal.
ZERO_NORM = 1e-300          # below this a vector carries no direction at all
DEGENERATE_CONE_RAD = 32 * math.ulp(1.0)  # only a machine-unresolvable cone is geometrically undefined
NORMALISATION_REL = 1e-9    # |J| below this fraction of its own scale is not normalised


class MetricsError(ValueError):
    """Raised when an input result does not satisfy the documented contract."""


# --------------------------------------------------------------------------- #
# Small vector helpers (3-tuples of floats)
# --------------------------------------------------------------------------- #

def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vscale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def vnorm(a):
    return math.hypot(*a)


def vunit(a):
    """Unit vector, or ``None`` when the input carries no direction."""
    n = vnorm(a)
    if not math.isfinite(n) or n <= ZERO_NORM:
        return None
    return (a[0] / n, a[1] / n, a[2] / n)


def vangle(a, b):
    """Geodesic angle between two vectors, or ``None`` if either has no direction."""
    ua, ub = vunit(a), vunit(b)
    if ua is None or ub is None:
        return None
    return math.atan2(vnorm(vcross(ua, ub)), vdot(ua, ub))


# --------------------------------------------------------------------------- #
# Quaternions and SO(3)
# --------------------------------------------------------------------------- #

def quaternion_normalize(q):
    scale = max(abs(x) for x in q)
    if not math.isfinite(scale) or scale == 0:
        raise MetricsError("quaternion has no valid direction")
    scaled = tuple(x / scale for x in q)
    n = math.hypot(*scaled)
    return tuple(x / n for x in scaled)


def quaternion_conjugate(q):
    return (q[0], -q[1], -q[2], -q[3])


def quaternion_multiply(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (aw * bw - ax * bx - ay * by - az * bz,
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw)


def body_to_inertial(q, v):
    """Rotate a body vector to the inertial frame (scalar-first Hamilton convention)."""
    q = quaternion_normalize(q)
    u = (q[1], q[2], q[3])
    t = vscale(vcross(u, v), 2.0)
    return vadd(vadd(v, vscale(t, q[0])), vcross(u, t))


def canonical_quaternion(q):
    """Sign-fixed unit quaternion so that ``q`` and ``-q`` compare as identical."""
    q = quaternion_normalize(q)
    for c in q:
        if c > 0.0:
            return q
        if c < 0.0:
            return (-q[0], -q[1], -q[2], -q[3])
    return q


def so3_relative_rotation(q_base, q_result):
    """Signed SO(3) relative rotation vector taking the base attitude to the result.

    Returns ``(rotation_vector, angle_rad, near_pi)``. The construction is the
    exponential (log) map of the relative quaternion. It is evaluated as
    ``axis * 2*atan2(|v|, w)``, which stays accurate for arbitrarily small angles
    (no ``asin``/``acos`` cancellation) and remains finite at exactly zero
    difference, where it returns a zero vector. ``near_pi`` flags the branch
    ambiguity of the rotation axis above pi - 1e-7 rad; a rotation vector is
    still returned there but its axis sign must not be interpreted physically.
    """
    a = canonical_quaternion(q_base)
    b = canonical_quaternion(q_result)
    d = canonical_quaternion(quaternion_multiply(quaternion_conjugate(a), b))
    v = (d[1], d[2], d[3])
    s = vnorm(v)
    if s <= ZERO_NORM:
        return (0.0, 0.0, 0.0), 0.0, False
    w = max(-1.0, min(1.0, d[0]))
    angle = 2.0 * math.atan2(s, w)  # in [0, pi] after sign canonicalisation
    scale = angle / s
    return (v[0] * scale, v[1] * scale, v[2] * scale), angle, angle > math.pi - 1e-7


def so3_angle(q_base, q_result):
    return so3_relative_rotation(q_base, q_result)[1]


# --------------------------------------------------------------------------- #
# Point-mass osculating orbital diagnostics
# --------------------------------------------------------------------------- #

def rtn_basis(r, v):
    """Radial / transverse (along-track) / orbit-normal unit basis.

    Matches the harness convention: ``radial = r/|r|``, ``normal = h/|h|`` with
    ``h = r x v``, ``transverse = normal x radial`` (the along-track direction).
    Returns ``None`` when radius or angular momentum vanish.
    """
    radius = vnorm(r)
    h = vcross(r, v)
    hn = vnorm(h)
    if not (math.isfinite(radius) and math.isfinite(hn)):
        return None
    if radius <= ZERO_NORM or hn <= ZERO_NORM:
        return None
    radial = vscale(r, 1.0 / radius)
    normal = vscale(h, 1.0 / hn)
    return radial, vcross(normal, radial), normal


def rtn_components(basis, vector):
    return tuple(vdot(vector, basis[i]) for i in range(3))


def specific_energy(v, r, mu=MU_EARTH_M3_S2):
    """Specific two-body orbital energy in J/kg (negative for bound orbits)."""
    return 0.5 * vdot(v, v) - mu / vnorm(r)


def osculating_elements(r, v, mu=MU_EARTH_M3_S2):
    """Point-mass osculating elements of a state vector.

    Diagnostic only: with a full J2 field these are not conserved. ``sma_m`` is
    ``None`` for orbits with non-negative specific energy rather than a fabricated
    infinite value.
    """
    out = {}
    radius = vnorm(r)
    speed2 = vdot(v, v)
    h = vcross(r, v)
    hn = vnorm(h)
    energy = 0.5 * speed2 - mu / radius if radius > ZERO_NORM else None
    out["specific_energy_j_kg"] = energy
    out["speed_m_s"] = math.sqrt(speed2)
    out["radius_m"] = radius
    out["angular_momentum_kg_m2_s"] = hn if hn > ZERO_NORM else None
    out["orbit_normal"] = vunit(h)
    if energy is None or hn <= ZERO_NORM:
        out.update(sma_m=None, eccentricity=None, inclination_rad=None,
                   raan_rad=None, arg_periapsis_rad=None, true_anomaly_rad=None,
                   periapsis_radius_m=None, apoapsis_radius_m=None,
                   unbounded_orbit_reason=None if energy is None else "nonnegative_specific_energy")
        return out
    if energy >= 0.0:
        out.update(sma_m=None, eccentricity=None, inclination_rad=None, raan_rad=None,
                   arg_periapsis_rad=None, true_anomaly_rad=None,
                   periapsis_radius_m=None, apoapsis_radius_m=None,
                   unbounded_orbit_reason="nonnegative_specific_energy")
        return out
    sma = -mu / (2.0 * energy)
    eccentricity_vector = vsub(vscale(vcross(v, h), 1.0 / mu), vscale(r, 1.0 / radius))
    ecc = vnorm(eccentricity_vector)
    inclination = math.acos(max(-1.0, min(1.0, h[2] / hn)))
    raan = math.atan2(h[1], h[0]) % (2.0 * math.pi)
    node = vcross((0.0, 0.0, 1.0), h)
    nn = vnorm(node)
    if nn <= ZERO_NORM or ecc <= ZERO_NORM:
        argp = None
        true_anomaly = None
    else:
        argp = math.acos(max(-1.0, min(1.0, vdot(node, eccentricity_vector) / (nn * ecc))))
        if eccentricity_vector[2] < 0.0:
            argp = 2.0 * math.pi - argp
        true_anomaly = math.acos(max(-1.0, min(1.0, vdot(eccentricity_vector, r) / (ecc * radius))))
        if vdot(r, v) < 0.0:
            true_anomaly = 2.0 * math.pi - true_anomaly
    out.update(sma_m=sma, eccentricity=ecc, inclination_rad=inclination,
               raan_rad=raan, arg_periapsis_rad=argp, true_anomaly_rad=true_anomaly,
               periapsis_radius_m=sma * (1.0 - ecc), apoapsis_radius_m=sma * (1.0 + ecc),
               unbounded_orbit_reason=None)
    return out


# --------------------------------------------------------------------------- #
# Sampled quadrature
# --------------------------------------------------------------------------- #

def cumulative_trapezoid(t, y):
    """Trapezoidal cumulative integral of ``y`` sampled at ``t`` (starts at zero)."""
    n = len(t)
    if n != len(y):
        raise MetricsError("time and value series length mismatch")
    out = [0.0] * n
    for i in range(1, n):
        out[i] = out[i - 1] + 0.5 * (y[i] + y[i - 1]) * (t[i] - t[i - 1])
    return out


def total_trapezoid(t, y):
    c = cumulative_trapezoid(t, y)
    return c[-1] if c else 0.0


def time_mean(t, y):
    """Cadence-weighted time average; ``None`` for a degenerate time span."""
    if len(t) < 2:
        return None
    span = t[-1] - t[0]
    if span <= 0.0:
        return None
    return total_trapezoid(t, y) / span


def time_rms(t, y):
    """Cadence-weighted RMS; ``None`` for a degenerate time span."""
    if len(t) < 2:
        return None
    span = t[-1] - t[0]
    if span <= 0.0:
        return None
    return math.sqrt(max(0.0, total_trapezoid(t, [v * v for v in y])) / span)


def _lagrange_basis(nodes, x):
    n = len(nodes)
    basis = [1.0] * n
    for i in range(n):
        for j in range(n):
            if i != j:
                basis[i] *= (x - nodes[j]) / (nodes[i] - nodes[j])
    return basis


def integral_cadence_spread(t, components, strides=(2, 4)):
    """Empirical cadence limit of a sampled integral, by decimation.

    The integral is recomputed with the trapezoidal rule on progressively
    decimated sample sets. A spread that stays large as the stride grows means the
    reported integral is limited by output cadence, not by the quadrature order.

    This is preferred over interpolant-based refinement as the cadence evidence
    because it uses only reported samples: any difference is genuinely in the
    data, never in an interpolant's overshoot. Decimation under-samples peaks, so
    the spread is an indicator of cadence sensitivity rather than a bound on the
    integral's error.
    """
    n = len(t)
    if n < 3 or not components:
        return {"reference": None, "by_stride": {}, "spread": None,
                "reason": "insufficient_samples"}
    reference = [total_trapezoid(t, [c[i] for i in range(n)]) for c in components]
    by_stride = {}
    values = []
    for stride in strides:
        if n // stride < 3:
            continue
        index = list(range(0, n, stride))
        if index[-1] != n - 1:
            index.append(n - 1)
        td = [t[i] for i in index]
        estimate = [total_trapezoid(td, [c[i] for i in index]) for c in components]
        by_stride[str(stride)] = {
            "integrals": estimate,
            "absolute_difference": [estimate[k] - reference[k] for k in range(len(reference))],
        }
        values.append(estimate)
    if not values:
        return {"reference": reference, "by_stride": {}, "spread": None,
                "reason": "insufficient_samples_for_decimation"}
    spread = [max(abs(v[k] - reference[k]) for v in values) for k in range(len(reference))]
    return {"reference": reference, "by_stride": by_stride, "spread": spread, "reason": None}


def _stencil(t, i, degree):
    """Index window of ``degree + 1`` nodes bracketing interval ``i``.

    The window is chosen to contain both interval endpoints and to sit as close as
    possible to the interval midpoint, which keeps the local polynomial
    well-conditioned. Centring on the interval index instead lets the window drift
    onto nodes far from the interval being integrated and makes the refined
    quadrature worse than the plain trapezoidal rule on the same samples.
    """
    n = len(t)
    lowest = max(0, i + 1 - degree)
    highest = min(i, n - degree - 1)
    if highest < lowest:
        lowest = max(0, min(i, n - degree - 1))
        highest = lowest
    mid = 0.5 * (t[i] + t[i + 1])
    best = min(range(lowest, highest + 1),
               key=lambda lo: (sum(abs(t[k] - mid) for k in range(lo, lo + degree + 1)), lo))
    return best, best + degree + 1


def refined_integrals(t, components, substeps=8, degree=3):
    """Total integral of each component series using local Lagrange quadrature.

    This is a *cadence* check, not a force-model refinement: it integrates the
    very same sampled values on a finer quadrature grid using local polynomial
    interpolation. A large difference from the plain trapezoidal result means the
    reported integral is cadence limited. It is not an accuracy oracle: on a very
    coarse grid a high-degree interpolant can itself overshoot, so the difference
    is reported alongside the value rather than being used to correct it.
    """
    n = len(t)
    m = len(components)
    if n < 2 or m == 0:
        return [0.0] * m
    degree = max(1, min(int(degree), n - 1))
    substeps = max(1, int(substeps))
    totals = [0.0] * m
    for i in range(n - 1):
        lo, hi = _stencil(t, i, degree)
        nodes = t[lo:hi]
        width = t[i + 1] - t[i]
        previous = None
        for k in range(substeps + 1):
            x = t[i] + width * k / substeps
            basis = _lagrange_basis(nodes, x)
            value = [0.0] * m
            for j, node_index in enumerate(range(lo, hi)):
                b = basis[j]
                if b == 0.0:
                    continue
                for c in range(m):
                    value[c] += b * components[c][node_index]
            if previous is not None:
                dx = x - previous[0]
                for c in range(m):
                    totals[c] += 0.5 * dx * (value[c] + previous[1][c])
            previous = (x, value)
    return totals


# --------------------------------------------------------------------------- #
# Unpacking
# --------------------------------------------------------------------------- #

def _require(condition, message):
    if not condition:
        raise MetricsError(message)


def unpack(result):
    """Validate and split a harness result into plain Python series."""
    _require(isinstance(result, dict), "result must be a JSON object")
    samples = result.get("samples")
    _require(isinstance(samples, list) and len(samples) >= 1,
             "result must contain a nonempty samples array")
    columns = [[] for _ in range(SAMPLE_WIDTH)]
    for row in samples:
        _require(isinstance(row, list) and len(row) == SAMPLE_WIDTH,
                 f"sample row must have {SAMPLE_WIDTH} values")
        for i, value in enumerate(row):
            _require(isinstance(value, (int, float)) and not isinstance(value, bool),
                     f"sample column {SAMPLE_FIELDS[i]} must be numeric")
            columns[i].append(float(value))
    data = {name: columns[i] for i, name in enumerate(SAMPLE_FIELDS)}
    data["r"] = list(zip(data["r_x_m"], data["r_y_m"], data["r_z_m"]))
    data["v"] = list(zip(data["v_x_m_s"], data["v_y_m_s"], data["v_z_m_s"]))
    data["q"] = list(zip(data["q_w"], data["q_x"], data["q_y"], data["q_z"]))
    data["omega_body_rad_s"] = list(zip(data["omega_x_rad_s"], data["omega_y_rad_s"], data["omega_z_rad_s"]))
    data["srp_force_n"] = list(zip(data["srp_force_x_n"], data["srp_force_y_n"], data["srp_force_z_n"]))
    data["srp_torque_nm"] = list(zip(data["srp_torque_x_nm"], data["srp_torque_y_nm"], data["srp_torque_z_nm"]))
    times = data["time_s"]
    _require(all(times[i] < times[i + 1] for i in range(len(times) - 1)),
             "sample times must be strictly increasing")

    loads = result.get("loads")
    if loads is not None:
        _require(isinstance(loads, list) and len(loads) == len(times),
                 "loads must align one-to-one with samples when present")
        for entry in loads:
            _require(isinstance(entry, dict), "each loads entry must be an object")
        data["loads"] = loads
    else:
        data["loads"] = None
    physical = result.get("physical")
    data["physical"] = physical if isinstance(physical, dict) else None
    return data


def load_vector(data, key, required=True):
    """Column ``key`` of the loads series as a list of 3-tuples, or ``None``."""
    if data["loads"] is None:
        _require(not required, "loads series absent; cannot report load-derived observables")
        return None
    column = []
    for entry in data["loads"]:
        value = entry.get(key)
        if value is None:
            column.append(None)
            continue
        _require(isinstance(value, list) and len(value) == 3,
                 f"loads.{key} must be a 3-element vector when present")
        column.append(tuple(float(c) for c in value))
    return column


def load_scalar(data, key, required=True):
    """Column ``key`` of the loads series as a list of floats (or ``None`` entries)."""
    if data["loads"] is None:
        _require(not required, "loads series absent; cannot report load-derived observables")
        return None
    column = []
    for entry in data["loads"]:
        value = entry.get(key)
        column.append(None if value is None else float(value))
    return column


def inertia_diagonal(data):
    """Body-frame principal moments, or ``None`` when the metadata omits them."""
    physical = data["physical"]
    if not physical:
        return None
    diagonal = physical.get("inertia_diagonal_kg_m2")
    if diagonal is None:
        return None
    _require(isinstance(diagonal, list) and len(diagonal) == 3,
             "physical.inertia_diagonal_kg_m2 must have three entries")
    return tuple(float(x) for x in diagonal)


def body_momentum(inertia, omega):
    return (inertia[0] * omega[0], inertia[1] * omega[1], inertia[2] * omega[2])


def default_spin_axis_body(data):
    """Body axis used as the spin reference.

    Chosen as the axis carrying the largest initial angular-momentum component,
    falling back to the largest principal moment when the inertia is unavailable.
    """
    inertia = inertia_diagonal(data)
    if inertia is None:
        return 0
    momentum = body_momentum(inertia, data["omega_body_rad_s"][0])
    return max(range(3), key=lambda i: abs(momentum[i]))


# --------------------------------------------------------------------------- #
# Time-series reductions
# --------------------------------------------------------------------------- #

def _finite(values):
    return [v for v in values if v is not None and math.isfinite(v)]





def _stats(t, values):
    """min/max/mean/rms/final with ``None`` where any sample is undefined."""
    if values is None:
        return None
    finite = _finite(values)
    if not finite:
        return {"final": None, "mean": None, "rms": None, "min": None, "max": None,
                "undefined_reason": "no_defined_samples"}
    clean = all(v is not None and math.isfinite(v) for v in values)
    return {"final": values[-1], "mean": time_mean(t, values) if clean else None,
            "rms": time_rms(t, values) if clean else None,
            "min": min(finite), "max": max(finite), "undefined_reason": None}


def _vector_stats(t, vectors):
    """min/max/mean/rms/final magnitudes of a vector series (directions handled elsewhere)."""
    if vectors is None:
        return None
    return _stats(t, [None if v is None else vnorm(v) for v in vectors])


def _unwrapped(t, angles):
    """Unwrap an angle series (radians) to make differences meaningful."""
    if not angles:
        return []
    out = [angles[0]]
    for a in angles[1:]:
        if a is None or out[-1] is None:
            out.append(a)
            continue
        step = a - out[-1]
        step = (step + math.pi) % (2.0 * math.pi) - math.pi
        out.append(out[-1] + step)
    return out


def _interval_stats(values):
    """Absolute max/RMS errors with signed final/mean; plain common-time samples."""
    finite = _finite(values)
    if not finite:
        return {"final": None, "max": None, "rms": None, "mean": None,
                "undefined_reason": "no_defined_samples"}
    n = len(finite)
    rms = math.sqrt(sum(v * v for v in finite) / n)
    return {"final": values[-1], "max": max(abs(v) for v in finite), "rms": rms, "mean": sum(finite) / n,
            "undefined_reason": None if values[-1] is not None else "undefined_final_sample"}


def _unresolved(reason):
    return {"final": None, "max": None, "rms": None, "mean": None, "undefined_reason": reason}


# --------------------------------------------------------------------------- #
# summarize
# --------------------------------------------------------------------------- #

def summarize(result, horizon_s=None, spin_axis_body=None, series=False,
              cadence_substeps=8, mu=MU_EARTH_M3_S2):
    """Flat scalar observables of one propagation.

    Parameters
    ----------
    result
        Harness JSON object for a single run.
    horizon_s
        Optional analysis horizon; only samples with ``t <= horizon_s`` are used.
    spin_axis_body
        Body axis (0/1/2) used as the spin reference; defaults to the axis with the
        largest initial angular-momentum component.
    series
        Include a compact time series under ``"series"`` for plotting.
    cadence_substeps
        Sub-intervals per output interval for the refined quadrature check.

    Undefined quantities appear as JSON ``null`` with a companion ``*_reason``
    string; they are never reported as zero.
    """
    data = unpack(result)
    t_all = data["time_s"]
    keep = [i for i, t in enumerate(t_all) if horizon_s is None or t <= horizon_s + 1e-12]
    _require(len(keep) >= 1, "analysis horizon contains no samples")
    if len(keep) < len(t_all):
        data = {k: ([v[i] for i in keep] if isinstance(v, list) and v and isinstance(v[0], float) else v)
                for k, v in data.items()}
        for key in ("r", "v", "q", "omega_body_rad_s", "srp_force_n", "srp_torque_nm"):
            data[key] = [data[key][i] for i in keep]
        if data["loads"] is not None:
            data["loads"] = [data["loads"][i] for i in keep]
    t = data["time_s"]
    n = len(t)
    inertia = inertia_diagonal(data)
    mass = None
    if data["physical"]:
        mass = data["physical"].get("mass_kg")
        mass = None if mass is None else float(mass)
    spin_axis = default_spin_axis_body(data) if spin_axis_body is None else int(spin_axis_body)
    _require(spin_axis in (0, 1, 2), "spin_axis_body must be 0, 1 or 2")

    out = {
        "case_id": (data["physical"] or {}).get("case_id"),
        "version": (data["physical"] or {}).get("version"),
        "source_file": (data["physical"] or {}).get("source_file"),
        "method": result.get("method"),
        "n_samples": n,
        "t0_s": t[0],
        "tf_s": t[-1],
        "mass_kg": mass,
        "inertia_diagonal_kg_m2": list(inertia) if inertia else None,
        "spin_axis_body_index": spin_axis,
    }

    gaps = [t[i + 1] - t[i] for i in range(n - 1)]
    out["cadence_s"] = {"min": min(gaps) if gaps else None, "max": max(gaps) if gaps else None,
                        "mean": (sum(gaps) / len(gaps)) if gaps else None}

    # ---- orbital diagnostics ------------------------------------------------
    elements = [osculating_elements(data["r"][i], data["v"][i], mu) for i in range(n)]
    sma = [e["sma_m"] for e in elements]
    ecc = [e["eccentricity"] for e in elements]
    energy = [e["specific_energy_j_kg"] for e in elements]
    inclination = [e["inclination_rad"] for e in elements]
    raan = [e["raan_rad"] for e in elements]
    angular_momentum = [vcross(data["r"][i], data["v"][i]) for i in range(n)]
    orbital = {
        "note": "point-mass osculating diagnostics; not conserved under the J2 field",
        "semi_major_axis_m": _stats(t, sma),
        "semi_major_axis_range_m": (max(_finite(sma)) - min(_finite(sma))) if _finite(sma) else None,
        "eccentricity": _stats(t, ecc),
        "eccentricity_range": (max(_finite(ecc)) - min(_finite(ecc))) if _finite(ecc) else None,
        "specific_energy_j_kg": _stats(t, energy),
        "specific_energy_change_j_kg": (energy[-1] - energy[0]) if (energy[-1] is not None and energy[0] is not None) else None,
        "specific_energy_rate_w_kg": ((energy[-1] - energy[0]) / (t[-1] - t[0])) if (n > 1 and energy[-1] is not None and energy[0] is not None) else None,
        "total_orbital_energy_j": None if (mass is None or energy[-1] is None) else mass * energy[-1],
        "total_orbital_energy_change_j": None if (mass is None or energy[-1] is None or energy[0] is None) else mass * (energy[-1] - energy[0]),
        "inclination_rad": _stats(t, inclination),
        "inclination_change_rad": (inclination[-1] - inclination[0]) if (inclination[-1] is not None and inclination[0] is not None) else None,
        "raan_change_rad": (_unwrapped(t, raan)[-1] - _unwrapped(t, raan)[0]) if raan[0] is not None else None,
        "angular_momentum_plane_rotation_rad": vangle(angular_momentum[0], angular_momentum[-1]),
        "angular_momentum_plane_rotation_max_rad": max(
            (vangle(angular_momentum[0], h) for h in angular_momentum), default=None),
        "specific_angular_momentum_m2_s": _stats(t, [vnorm(h) for h in angular_momentum]),
        "angular_momentum_magnitude_kg_m2_s": None if mass is None else _stats(t, [mass*vnorm(h) for h in angular_momentum]),
        "unbounded_orbit_reason": elements[-1]["unbounded_orbit_reason"],
    }
    out["orbital"] = orbital

    # ---- attitude -----------------------------------------------------------
    omega = data["omega_body_rad_s"]
    attitude = {}
    if inertia is None:
        momentum_inertial = None
        attitude["angular_momentum_kg_m2_s"] = None
        attitude["angular_momentum_reason"] = "physical.inertia_diagonal_kg_m2 absent"
    else:
        momentum_inertial = [body_to_inertial(data["q"][i], body_momentum(inertia, omega[i])) for i in range(n)]
        attitude["angular_momentum_kg_m2_s"] = _vector_stats(t, momentum_inertial)
    declared = load_vector(data, "angular_momentum_inertial", required=False)
    if declared is not None:
        usable = [d for d in declared if d is not None]
        attitude["declared_momentum_present"] = bool(usable)
        attitude["declared_momentum_note"] = ("inertial momentum also supplied by the harness loads"
                                              if usable else "loads.angular_momentum_inertial empty")
    if inertia is not None:
        rotation_energy = [0.5 * vdot(omega[i], body_momentum(inertia, omega[i])) for i in range(n)]
        attitude["rotational_energy_j"] = _stats(t, rotation_energy)
        attitude["rotational_energy_drift_j"] = rotation_energy[-1] - rotation_energy[0]
        magnitudes = [vnorm(momentum_inertial[i]) for i in range(n)]
        reference = magnitudes[0]
        attitude["angular_momentum_drift_relative"] = (
            (max(magnitudes) - min(magnitudes)) / reference) if reference and reference > ZERO_NORM else None
    attitude["angular_speed_rad_s"] = _stats(t, [vnorm(w) for w in omega])
    attitude["omega_body_norm_drift_rad_s"] = vnorm(omega[-1]) - vnorm(omega[0])

    # Physical instantaneous angular-velocity direction, not a chosen body axis.
    # Zero-spin samples have no spin direction; a sail normal is a different observable.
    spin_inertial = [body_to_inertial(data["q"][i], omega[i]) for i in range(n)]
    spin_unit = [vunit(s) if vnorm(omega[i]) > ZERO_NORM else None for i, s in enumerate(spin_inertial)]
    defined_spin = [s for s in spin_unit if s is not None]
    mean_spin = vunit(tuple(sum(s[k] for s in defined_spin) for k in range(3))) if defined_spin else None
    if mean_spin is None:
        attitude["spin_axis_nutation_half_angle_rad"] = None
        attitude["spin_axis_nutation_mean_rad"] = None
        attitude["spin_axis_precession_rate_rad_s"] = None
        attitude["spin_axis_reason"] = "spin_axis_direction_undefined"
    else:
        cone = [vangle(s, mean_spin) if s is not None else None for s in spin_unit]
        attitude["spin_axis_nutation_half_angle_rad"] = cone[-1]
        attitude["spin_axis_nutation_max_rad"] = max(_finite(cone)) if _finite(cone) else None
        attitude["spin_axis_nutation_mean_rad"] = time_mean(t, cone) if all(c is not None for c in cone) else None
        half_angle = attitude["spin_axis_nutation_max_rad"] or 0.0
        if half_angle < DEGENERATE_CONE_RAD:
            attitude["spin_axis_precession_rate_rad_s"] = None
            attitude["spin_axis_precession_turns"] = None
            attitude["spin_axis_reason"] = "precession_undefined_degenerate_cone"
        else:
            seed = spin_unit[0] if spin_unit[0] is not None else mean_spin
            u1 = vsub(seed, vscale(mean_spin, vdot(seed, mean_spin)))
            u1 = vunit(u1)
            if u1 is None:
                u1 = (1.0, 0.0, 0.0) if abs(mean_spin[0]) < 0.9 else (0.0, 1.0, 0.0)
                u1 = vsub(u1, vscale(mean_spin, vdot(u1, mean_spin)))
                u1 = vunit(u1)
            u2 = vcross(mean_spin, u1)
            azimuth = []
            for s in spin_unit:
                azimuth.append(None if s is None else math.atan2(vdot(s, u2), vdot(s, u1)))
            unwrapped = _unwrapped(t, azimuth)
            if unwrapped[0] is None or unwrapped[-1] is None or n < 2 or t[-1] <= t[0]:
                attitude["spin_axis_precession_rate_rad_s"] = None
                attitude["spin_axis_precession_turns"] = None
                attitude["spin_axis_reason"] = "precession_undefined_undefined_azimuth"
            else:
                turns = (unwrapped[-1] - unwrapped[0]) / (2.0 * math.pi)
                attitude["spin_axis_precession_rate_rad_s"] = turns * 2.0 * math.pi / (t[-1] - t[0])
                attitude["spin_axis_precession_turns"] = turns
                attitude["spin_axis_reason"] = None
    out["attitude"] = attitude

    # ---- pointing -----------------------------------------------------------
    pointing = load_vector(data, "pointing_axis_inertial", required=False)
    if pointing is None:
        out["pointing"] = None
        out["pointing_reason"] = "loads.pointer absent"
    else:
        sun = load_vector(data, "sun_position_m", required=False)
        block = {}
        if sun is not None:
            to_sun = []
            for i in range(n):
                if pointing[i] is None or sun[i] is None:
                    to_sun.append(None)
                    continue
                direction = vsub(sun[i], data["r"][i])
                to_sun.append(vangle(pointing[i], direction))
            block["to_sun_rad"] = _stats(t, to_sun)
            block["to_sun_max_rad"] = max(_finite(to_sun)) if _finite(to_sun) else None
            block["to_sun_rms_rad"] = time_rms(t, to_sun) if all(a is not None for a in to_sun) else None
        block["to_orbit_normal_rad"] = _stats(t, [vangle(pointing[i], vcross(data["r"][i], data["v"][i]))
                                                  if pointing[i] is not None else None for i in range(n)])
        block["to_velocity_rad"] = _stats(t, [vangle(pointing[i], data["v"][i])
                                              if pointing[i] is not None else None for i in range(n)])
        block["to_radial_rad"] = _stats(t, [vangle(pointing[i], data["r"][i])
                                            if pointing[i] is not None else None for i in range(n)])
        block["undefined_reason"] = None if any(p is not None for p in pointing) else "pointing_axis_undefined"
        out["pointing"] = block

    # ---- environment and non-gravitational loads ---------------------------
    area = load_scalar(data, "projected_area_m2", required=False)
    density = load_scalar(data, "density_kg_m3", required=False)
    environment = {
        "illumination": _stats(t, data["illumination"]),
        "illuminated_time_fraction": time_mean(t, data["illumination"]),
    }
    if area is None:
        environment["projected_area_m2"] = None
        environment["projected_area_reason"] = "loads.projected_area_m2 absent"
    else:
        defined = [i for i, a in enumerate(area) if a is not None]
        environment["projected_area_m2"] = _stats(t, area)
        environment["projected_area_time_average_m2"] = time_mean(t, area) if len(defined) == n else None
        effective = [area[i] * data["illumination"][i] for i in range(n)] if len(defined) == n else None
        duration = t[-1] - t[0]
        light_time = total_trapezoid(t, data["illumination"]) if n > 1 else data["illumination"][0]
        lit_integral = total_trapezoid(t, effective) if effective is not None and n > 1 else (effective[0] if effective else None)
        environment["projected_area_illuminated_average_m2"] = (
            lit_integral / light_time if lit_integral is not None and light_time > 0 else None)
        environment["effective_projected_area_time_average_m2"] = (
            lit_integral / duration if lit_integral is not None and duration > 0 else None)
        if light_time <= 0:
            environment["projected_area_illuminated_reason"] = "no_illumination"
    if density is None:
        environment["density_kg_m3"] = None
        environment["density_reason"] = "loads.density_kg_m3 absent"
    else:
        environment["density_kg_m3"] = _stats(t, density)
    out["environment"] = environment

    out["non_gravitational"] = _non_gravitational_block(data, t, mass, cadence_substeps)

    if series:
        out["series"] = {
            "time_s": list(t),
            "specific_energy_j_kg": energy,
            "semi_major_axis_m": sma,
            "eccentricity": ecc,
            "angular_speed_rad_s": [vnorm(w) for w in omega],
            "spin_axis_inertial": [list(s) if s is not None else None for s in spin_unit],
            "angular_momentum_inertial": [list(m) if m is not None else None for m in (momentum_inertial or [])],
            "srp_work_j": list(data["srp_work_j"]),
            "projected_area_m2": list(area) if area is not None else None,
            "illumination": list(data["illumination"]),
        }
    return out




def _non_gravitational_block(data, t, mass, substeps):
    """Sampled impulses, works and cadence-refinement differences."""
    block = {"integration": "trapezoid on the reported output cadence"}
    velocity = data["v"]
    forces = {"srp": data["srp_force_n"]}
    declared = {
        "drag": load_vector(data, "drag_force_n", required=False),
        "albedo": load_vector(data, "albedo_force_n", required=False),
    }
    for key, series in declared.items():
        forces[key] = series
    missing = sorted(k for k, s in declared.items() if s is None)
    if missing:
        block["note"] = "loads series absent for: " + ", ".join(missing)
    for key in ("srp", "drag", "albedo"):
        force = forces[key]
        if force is None:
            block[f"{key}_work_j"] = None
            block[f"{key}_impulse_n_s"] = None
            block[f"{key}_reason"] = "loads series absent"
            continue
        usable = all(f is not None for f in force)
        if not usable:
            block[f"{key}_work_j"] = None
            block[f"{key}_impulse_n_s"] = None
            block[f"{key}_reason"] = "force undefined at one or more samples"
            continue
        power = [vdot(force[i], velocity[i]) for i in range(len(t))]
        work = total_trapezoid(t, power)
        impulse_components = list(zip(*force))
        impulse = [total_trapezoid(t, list(c)) for c in impulse_components]
        refined_work = refined_integrals(t, [power], substeps=substeps)[0]
        refined_impulse = refined_integrals(t, [list(c) for c in impulse_components], substeps=substeps)
        cadence = integral_cadence_spread(t, [power] + [list(c) for c in impulse_components])
        block[f"{key}_work_j"] = work
        block[f"{key}_work_refined_j"] = refined_work
        block[f"{key}_work_cadence_difference_j"] = refined_work - work
        block[f"{key}_impulse_n_s"] = impulse
        block[f"{key}_impulse_refined_n_s"] = refined_impulse
        block[f"{key}_impulse_cadence_difference_n_s"] = [refined_impulse[k] - impulse[k] for k in range(3)]
        block[f"{key}_impulse_norm_n_s"] = vnorm(tuple(impulse))
        # Decimation is the primary cadence evidence; the interpolant-based
        # refined value is secondary and can overshoot on a coarse grid.
        if cadence["spread"] is not None:
            block[f"{key}_work_cadence_spread_j"] = cadence["spread"][0]
            block[f"{key}_impulse_cadence_spread_n_s"] = [cadence["spread"][k] for k in range(1, 4)]
            block[f"{key}_work_cadence_limited"] = cadence["spread"][0] > 0.05 * abs(work) if work else None
        else:
            block[f"{key}_work_cadence_spread_j"] = None
            block[f"{key}_work_cadence_limited"] = None
            block[f"{key}_cadence_reason"] = cadence["reason"]
        block[f"{key}_mean_power_w"] = (work / (t[-1] - t[0])) if len(t) > 1 and t[-1] > t[0] else None
        block[f"{key}_specific_impulse_m_s"] = (
            None if mass in (None, 0) else vnorm(tuple(impulse)) / mass)
    srp_reported = data["srp_work_j"][-1]
    srp_here = block.get("srp_work_j")
    block["srp_work_reported_j"] = srp_reported
    block["srp_work_reported_difference_j"] = (
        None if srp_here is None else srp_reported - srp_here)
    if all(block.get(f"{k}_work_j") is not None for k in ("srp", "drag", "albedo")):
        block["total_non_grav_work_j"] = (block["srp_work_j"] + block["drag_work_j"] + block["albedo_work_j"])
        block["total_non_grav_work_refined_j"] = (block["srp_work_refined_j"] + block["drag_work_refined_j"]
                                                  + block["albedo_work_refined_j"])
        block["total_non_grav_work_cadence_difference_j"] = (block["total_non_grav_work_refined_j"]
                                                             - block["total_non_grav_work_j"])
        block["mean_non_grav_power_w"] = (block["total_non_grav_work_j"] / (t[-1] - t[0])) if len(t) > 1 and t[-1] > t[0] else None
    else:
        block["total_non_grav_work_j"] = None
    for key in ("gravity_gradient_nm", "drag_torque_nm", "albedo_torque_nm", "srp_torque_nm"):
        source = {"gravity_gradient_nm": load_vector(data, "gravity_gradient_nm", required=False),
                  "drag_torque_nm": load_vector(data, "drag_torque_nm", required=False),
                  "albedo_torque_nm": load_vector(data, "albedo_torque_nm", required=False),
                  "srp_torque_nm": data["srp_torque_nm"]}[key]
        short = key[:-3] if key.endswith("_nm") else key
        if source is None:
            block[f"{short}_torque_norm_nm"] = None
            block[f"{short}_torque_reason"] = "loads series absent"
            continue
        block[f"{short}_torque_norm_nm"] = _stats(t, [None if s is None else vnorm(s) for s in source])
        block[f"{short}_torque_angular_impulse_nm_s"] = [
            total_trapezoid(t, [s[k] for s in source]) for k in range(3)]
    return block


# --------------------------------------------------------------------------- #
# compare_observables
# --------------------------------------------------------------------------- #

def compare_observables(base, result, series=False, spin_axis_body=None,
                        mu=MU_EARTH_M3_S2):
    """Difference two propagations on identical physical output times.

    Returns ``time_alignment``, ``n_common``/``n_dropped_*`` counts, ``final``,
    ``maximum`` and ``rms`` scalar difference blocks, an ``orbital_rtn`` block and,
    when the grids match exactly, an optional per-sample ``series``.

    Any quantity that is undefined on either side is reported as ``null`` with a
    reason; a difference is never manufactured for an undefined operand.
    """
    a = unpack(base)
    b = unpack(result)
    ta, tb = a["time_s"], b["time_s"]
    exact = len(ta) == len(tb) and all(ta[i] == tb[i] for i in range(len(ta)))
    index = {t: i for i, t in enumerate(tb)}
    if exact:
        common = list(range(len(ta)))
        alignment = "identical"
    else:
        common = [i for i, t in enumerate(ta) if t in index]
        alignment = "intersected" if common else "disjoint"
    out = {
        "time_alignment": alignment,
        "n_base_samples": len(ta),
        "n_result_samples": len(tb),
        "n_common": len(common),
        "n_dropped_base": len(ta) - len(common),
        "n_dropped_result": len(tb) - len(common),
    }
    if not common:
        reason = ("output time grids do not overlap; observable differences require "
                  "identical physical output times")
        out.update(final=_unresolved(reason), maximum=_unresolved(reason),
                   rms=_unresolved(reason), orbital_rtn=None, series=None)
        return out
    ib = [index[t] for t in (ta[i] for i in common)]

    inertia_a, inertia_b = inertia_diagonal(a), inertia_diagonal(b)

    position = [vsub(b["r"][j], a["r"][i]) for i, j in zip(common, ib)]
    velocity = [vsub(b["v"][j], a["v"][i]) for i, j in zip(common, ib)]
    attitude_angle = [so3_angle(a["q"][i], b["q"][j]) for i, j in zip(common, ib)]
    rotation_vectors = [so3_relative_rotation(a["q"][i], b["q"][j])[0] for i, j in zip(common, ib)]
    near_pi = any(so3_relative_rotation(a["q"][i], b["q"][j])[2] for i, j in zip(common, ib))
    omega_difference = [vsub(b["omega_body_rad_s"][j], a["omega_body_rad_s"][i]) for i, j in zip(common, ib)]
    time_common = [ta[i] for i in common]

    elements_a = [osculating_elements(a["r"][i], a["v"][i], mu) for i in common]
    elements_b = [osculating_elements(b["r"][j], b["v"][j], mu) for j in ib]
    sma_difference = [_sub_or_none(eb["sma_m"], ea["sma_m"]) for ea, eb in zip(elements_a, elements_b)]
    eccentricity_difference = [_sub_or_none(eb["eccentricity"], ea["eccentricity"]) for ea, eb in zip(elements_a, elements_b)]
    energy_difference = [_sub_or_none(eb["specific_energy_j_kg"], ea["specific_energy_j_kg"])
                         for ea, eb in zip(elements_a, elements_b)]
    inclination_difference = [_sub_or_none(eb["inclination_rad"], ea["inclination_rad"])
                              for ea, eb in zip(elements_a, elements_b)]
    raan_difference = [None if ea["raan_rad"] is None or eb["raan_rad"] is None else
                       (eb["raan_rad"]-ea["raan_rad"]+math.pi) % (2*math.pi)-math.pi
                       for ea, eb in zip(elements_a, elements_b)]

    momentum_norm_difference = None
    momentum_angle_difference = None
    spin_axis_difference = None
    if inertia_a is not None and inertia_b is not None:
        la = [body_to_inertial(a["q"][i], body_momentum(inertia_a, a["omega_body_rad_s"][i])) for i in common]
        lb = [body_to_inertial(b["q"][j], body_momentum(inertia_b, b["omega_body_rad_s"][j])) for j in ib]
        momentum_norm_difference = [vnorm(x) - vnorm(y) for x, y in zip(lb, la)]
        momentum_angle_difference = [vangle(x, y) for x, y in zip(la, lb)]
    spin_a = [body_to_inertial(a["q"][i], a["omega_body_rad_s"][i]) for i in common]
    spin_b = [body_to_inertial(b["q"][j], b["omega_body_rad_s"][j]) for j in ib]
    spin_axis_difference = [vangle(x, y) if vnorm(a["omega_body_rad_s"][i]) > ZERO_NORM and vnorm(b["omega_body_rad_s"][j]) > ZERO_NORM else None
                            for i, j, x, y in zip(common, ib, spin_a, spin_b)]

    srp_work_difference = [b["srp_work_j"][j] - a["srp_work_j"][i] for i, j in zip(common, ib)]
    illumination_difference = [b["illumination"][j] - a["illumination"][i] for i, j in zip(common, ib)]

    area_difference = None
    area_a = load_scalar(a, "projected_area_m2", required=False)
    area_b = load_scalar(b, "projected_area_m2", required=False)
    if area_a is not None and area_b is not None:
        area_difference = [_sub_or_none(area_b[j], area_a[i]) for i, j in zip(common, ib)]

    pointing_difference = None
    point_a = load_vector(a, "pointing_axis_inertial", required=False)
    point_b = load_vector(b, "pointing_axis_inertial", required=False)
    if point_a is not None and point_b is not None:
        pointing_difference = [vangle(point_a[i], point_b[j]) for i, j in zip(common, ib)]

    metrics = {
        "position_m": _interval_stats([vnorm(d) for d in position]),
        "velocity_m_s": _interval_stats([vnorm(d) for d in velocity]),
        "attitude_angle_rad": _interval_stats(attitude_angle),
        "omega_rad_s": _interval_stats([vnorm(d) for d in omega_difference]),
        "spin_axis_angle_rad": _interval_stats(spin_axis_difference),
        "semi_major_axis_m": _interval_stats(sma_difference),
        "eccentricity": _interval_stats(eccentricity_difference),
        "specific_energy_j_kg": _interval_stats(energy_difference),
        "inclination_rad": _interval_stats(inclination_difference),
        "raan_rad": _interval_stats(raan_difference),
        "srp_work_j": _interval_stats(srp_work_difference),
        "illumination": _interval_stats(illumination_difference),
    }
    if momentum_norm_difference is not None:
        metrics["angular_momentum_norm_kg_m2_s"] = _interval_stats(momentum_norm_difference)
        metrics["angular_momentum_direction_rad"] = _interval_stats(momentum_angle_difference)
    else:
        metrics["angular_momentum_norm_kg_m2_s"] = _unresolved("inertia_diagonal_kg_m2 absent")
        metrics["angular_momentum_direction_rad"] = _unresolved("inertia_diagonal_kg_m2 absent")
    if area_difference is not None:
        metrics["projected_area_m2"] = _interval_stats(area_difference)
    else:
        metrics["projected_area_m2"] = _unresolved("loads.projected_area_m2 absent")
    if pointing_difference is not None:
        metrics["pointing_axis_angle_rad"] = _interval_stats(pointing_difference)
    else:
        metrics["pointing_axis_angle_rad"] = _unresolved("loads.pointing_axis_inertial absent")

    out["final"] = {k: v["final"] for k, v in metrics.items()}
    out["maximum"] = {k: v["max"] for k, v in metrics.items()}
    out["rms"] = {k: v["rms"] for k, v in metrics.items()}
    out["mean"] = {k: v["mean"] for k, v in metrics.items()}
    out["undefined_reason"] = {k: v["undefined_reason"] for k, v in metrics.items()
                               if v["undefined_reason"]}
    out["attitude_rotation_vector_rad"] = {
        "final": list(rotation_vectors[-1]),
        "note": "signed SO(3) relative rotation vector, body/inertial consistent with q ordering",
    }
    out["attitude_rotation_vector_near_pi"] = near_pi
    if near_pi:
        out["attitude_rotation_vector_note"] = "a sample lies within 1e-7 rad of pi; axis sign is ambiguous there"

    # Orbital RTN decomposition in the base frame. Differences are projected on
    # the base trajectory basis, so a discrepancy that is purely along-track is
    # not confused with a radial or plane change.
    rtn = {"basis": "base trajectory RTN (radial, transverse/along-track, orbit normal)"}
    if alignment == "identical":
        radial_series, along_series, normal_series = [], [], []
        velocity_series = {"radial": [], "transverse": [], "normal": []}
        for k, (i, _j) in enumerate(zip(common, ib)):
            basis = rtn_basis(a["r"][i], a["v"][i])
            if basis is None:
                continue
            dp = rtn_components(basis, position[k])
            dv = rtn_components(basis, velocity[k])
            radial_series.append(dp[0])
            along_series.append(dp[1])
            normal_series.append(dp[2])
            velocity_series["radial"].append(dv[0])
            velocity_series["transverse"].append(dv[1])
            velocity_series["normal"].append(dv[2])
        if radial_series:
            rtn["radial_m"] = _interval_stats(radial_series)
            rtn["transverse_along_track_m"] = _interval_stats(along_series)
            rtn["normal_cross_track_m"] = _interval_stats(normal_series)
            rtn["velocity_rtn_m_s"] = {k: _interval_stats(v) for k, v in velocity_series.items()}
            rtn["_series"] = {"radial_m": radial_series,
                              "transverse_along_track_m": along_series,
                              "normal_cross_track_m": normal_series}
        else:
            reason = "RTN basis undefined (zero radius or angular momentum)"
            rtn["radial_m"] = _unresolved(reason)
            rtn["transverse_along_track_m"] = _unresolved(reason)
            rtn["normal_cross_track_m"] = _unresolved(reason)
            rtn["velocity_rtn_m_s"] = None
    else:
        reason = "requires identical output time grids"
        rtn["radial_m"] = _unresolved(reason)
        rtn["transverse_along_track_m"] = _unresolved(reason)
        rtn["normal_cross_track_m"] = _unresolved(reason)
        rtn["velocity_rtn_m_s"] = None
    rtn_series = rtn.pop("_series", None)
    out["orbital_rtn"] = rtn

    if series and alignment == "identical":
        out["series"] = {
            "time_s": list(time_common),
            "position_m": [vnorm(d) for d in position],
            "velocity_m_s": [vnorm(d) for d in velocity],
            "attitude_angle_rad": attitude_angle,
            "attitude_rotation_vector_rad": [list(rv) for rv in rotation_vectors],
            "omega_rad_s": [vnorm(d) for d in omega_difference],
            "spin_axis_angle_rad": spin_axis_difference,
            "specific_energy_j_kg": energy_difference,
            "semi_major_axis_m": sma_difference,
            "srp_work_j": srp_work_difference,
            "angular_momentum_norm_kg_m2_s": momentum_norm_difference,
            "angular_momentum_direction_rad": momentum_angle_difference,
            "projected_area_m2": area_difference,
            "pointing_axis_angle_rad": pointing_difference,
            "radial_m": rtn_series["radial_m"] if rtn_series else None,
            "transverse_along_track_m": rtn_series["transverse_along_track_m"] if rtn_series else None,
            "normal_cross_track_m": rtn_series["normal_cross_track_m"] if rtn_series else None,
        }
    elif series:
        out["series"] = None
        out["series_reason"] = "identical output time grids required for a time-resolved difference"
    else:
        out["series"] = None
    return out


def _sub_or_none(x, y):
    if x is None or y is None:
        return None
    return x - y


# --------------------------------------------------------------------------- #
# Finite differences and resolution classification
# --------------------------------------------------------------------------- #
def scalar_derivative(plus, minus, delta, base=None, parameter_value=None):
    """Central finite difference of one observable over a symmetric +/- delta pair.

    ``delta`` is the signed step applied to the physical parameter. Returns the
    gradient ``dJ/dp``, the one-sided estimates, their relative asymmetry, and the
    dimensionless elasticity ``S* = (p/J)(dJ/dp)`` when both the parameter value
    and the unperturbed ``J`` are supplied and well conditioned.

    The elasticity stays ``None`` near ``J = 0``: normalising by a near-zero
    observable is not mathematically defined and no substitute value is invented.
    """
    out = {
        "plus": plus, "minus": minus, "base": base, "parameter_value": parameter_value,
        "gradient": None, "forward_gradient": None, "backward_gradient": None,
        "asymmetry_relative": None, "normalized_gradient": None,
        "normalized_reason": None, "unresolved_reason": None,
    }
    if delta == 0.0:
        out["unresolved_reason"] = "zero_perturbation"
        out["normalized_reason"] = "zero_perturbation"
        return out
    if plus is None or minus is None:
        out["unresolved_reason"] = "undefined_operand"
        out["normalized_reason"] = out["unresolved_reason"]
        return out
    gradient = (plus - minus) / (2.0 * delta)
    out["gradient"] = gradient
    if base is not None:
        forward = (plus - base) / delta
        backward = (base - minus) / delta
        out["forward_gradient"] = forward
        out["backward_gradient"] = backward
        mean = 0.5 * (forward + backward)
        if mean != 0.0:
            out["asymmetry_relative"] = abs(forward - backward) / abs(mean)
    if parameter_value is None or base is None:
        out["normalized_reason"] = "parameter_value_or_reference_observable_absent"
        return out
    scale = max(abs(plus), abs(minus), abs(base))
    if abs(base) <= max(NORMALISATION_REL * scale, ZERO_NORM):
        out["normalized_reason"] = "near_zero_observable"
        return out
    if parameter_value == 0.0:
        out["normalized_reason"] = "zero_parameter_value"
        return out
    out["normalized_gradient"] = (parameter_value / base) * gradient
    return out


def central_difference(base_summary, plus_summary, minus_summary, delta, keys=None,
                       parameters=None):
    """Per-observable central differences between two flat observable mappings.

    The three summaries are ``{name: value}`` mappings, for example the ``final``,
    ``maximum`` or ``rms`` block of a comparison. ``parameters`` optionally maps
    the same names to the unperturbed physical parameter value, which enables the
    dimensionless elasticity.
    """
    parameters = parameters or {}
    out = {}
    for key in (keys if keys is not None else sorted(base_summary)):
        if key not in base_summary or key not in plus_summary or key not in minus_summary:
            continue
        out[key] = scalar_derivative(plus_summary[key], minus_summary[key], delta,
                                     base_summary[key], parameters.get(key))
    return out


def envelope(*mappings):
    """Largest absolute value per key across difference blocks (a numerical envelope).

    Any block containing an unresolved (``None``) operand marks that key
    unresolved; a partially undefined set is never collapsed to a zero difference.
    """
    if not mappings:
        return {}
    keys = set()
    for m in mappings:
        keys.update(m)
    out = {}
    for key in sorted(keys):
        values = [m[key] for m in mappings if key in m]
        if any(v is None for v in values):
            out[key] = None
            continue
        out[key] = max(abs(v) for v in values)
    return out


def classify_resolution(response, numerical_envelope, ratio=10.0, consistency=None,
                         consistency_tolerance=0.20):
    """Decide whether a physical response is distinguishable from numerical noise.

    A response counts as resolved only when it exceeds the numerical envelope by
    at least ``ratio`` and, when several refined estimates are supplied, their
    relative spread stays within ``consistency_tolerance``. Otherwise the result
    is reported as unresolved with the reason; it is never promoted by rank order.
    """
    if response is None or numerical_envelope is None:
        return {"resolved": False, "reason": "undefined_response_or_envelope",
                "ratio": None, "consistency": consistency}
    achieved = abs(response) / numerical_envelope if numerical_envelope > 0.0 else None
    if achieved is not None and achieved == math.inf:
        # A bit-identical fine envelope is a measurement artefact, not accuracy.
        return {"resolved": False, "reason": "zero_numerical_envelope_flux", "ratio": None,
                "achieved_ratio_infinite": True, "consistency": consistency}
    if achieved is None or achieved < ratio:
        return {"resolved": False, "reason": "response_below_numerical_envelope",
                "ratio": achieved, "consistency": consistency}
    if consistency is not None and consistency > consistency_tolerance:
        return {"resolved": False, "reason": "refined_slopes_inconsistent",
                "ratio": achieved, "consistency": consistency}
    return {"resolved": True, "reason": None, "ratio": achieved, "consistency": consistency}


def rank_sensitivity(entries, key="gradient"):
    """Rank entries by |key|; unresolved entries are labelled, never ranked.

    Returns ``(ranked, unresolved)`` where ``ranked`` contains only entries with a
    finite, resolved value and ``unresolved`` retains the rest with their reason.
    """
    ranked, unresolved = [], []
    for entry in entries:
        value = entry.get(key)
        if value is None or not math.isfinite(value) or not entry.get("resolved", False):
            unresolved.append(dict(entry, ranking_reason=entry.get("resolution_reason")
                                   or "unresolved_or_nonfinite_gradient"))
        else:
            ranked.append(entry)
    ranked.sort(key=lambda e: -abs(e[key]))
    return ranked, unresolved


__all__ = [
    "MU_EARTH_M3_S2", "SAMPLE_FIELDS", "SAMPLE_WIDTH", "LOAD_VECTOR_KEYS", "LOAD_SCALAR_KEYS",
    "MetricsError", "unpack", "summarize", "compare_observables", "so3_relative_rotation",
    "so3_angle", "osculating_elements", "rtn_basis", "specific_energy", "cumulative_trapezoid",
    "refined_integrals", "scalar_derivative", "central_difference", "envelope",
    "classify_resolution", "rank_sensitivity",
]