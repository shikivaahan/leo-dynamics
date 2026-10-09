#!/usr/bin/env python3
"""Measured spacecraft characteristics for the version-1 sensitivity cases.

Every number this script emits is either (a) copied from the metadata of a raw
audit result, (b) measured from the sampled load series of that result, or
(c) derived from (a)/(b) by a formula that is written out in the ``definitions``
block of the output. Nothing is fitted, extrapolated or taken from a generic
spacecraft prior: there is no "typical CubeSat" dimension, area-to-mass ratio or
ballistic coefficient anywhere in the pipeline.

The generator replaces an unpublished analytic draft that used exaggerated
dimension/torque scalings, vector-sum torque shares and invented CubeSat
priors. It measures the configuration that actually propagated.

Areas follow the harness sign convention (one-sided, ``n.d > 0``), which is the
convention of both the drag laws and the radiation laws; the two-sided
``silhouette`` diagnostics are reported separately and are never used as a force
area, because for these panel sets they count both sides of the same body.

Usage:

    python3 docs/numerical-audit/spacecraft_characteristics.py \
        --inputs <raw nominal result json> [<raw nominal result json> ...] \
        --output <fresh path to a new evidence json>
"""
import argparse
import hashlib
import itertools
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "experiments"))
from metrics import (  # noqa: E402
    MU_EARTH_M3_S2,
    MetricsError,
    load_scalar,
    load_vector,
    osculating_elements,
    time_mean,
    time_rms,
    unpack,
    vcross,
    vdot,
    vnorm,
    vunit,
)

#: SI constants of the audited kernel (include/leo/kernel/radiation.hpp). Recorded
#: here so the effective-coefficient measurements are reproducible from the file
#: itself instead of from the kernel's build directory.
ASTRONOMICAL_UNIT_M = 149597870700.0
SOLAR_PRESSURE_1AU_N_M2 = 1361.0 / 299792458.0
EARTH_EQUATORIAL_RADIUS_M = 6378136.0

#: Reference drag coefficient of the classic ballistic-coefficient definition
#: beta = m / (Cd * A). 2.2 is a *declared reference constant of the definition*,
#: not a measured property of any case; the measured coefficient is reported
#: separately in ``forces.effective_drag_coefficient``.
BALLASTIC_REFERENCE_CD = 2.2

#: Direction-grid cross-check of the exact projected-area bounds (deterministic
#: Fibonacci sphere; no random numbers are used anywhere in this script).
BOUND_CHECK_DIRECTIONS = 4096

#: Exponential subset enumeration is exact and cheap for panel sets; above this
#: many surfaces the generator falls back to the deterministic direction grid and
#: says so in the output instead of quietly losing the guarantee.
MAX_EXACT_SURFACES = 16

#: Documented per-case assumptions, transcribed from the versioned configuration
#: and provenance records. They carry the source that declares them; they are not
#: parameters invented here. An unrecognised case id is a hard error, so this
#: table can never silently omit a configuration that is being measured.
CASE_NOTES = {
    "CASE-A": {
        "configuration_source": "docs/numerical-audit/experiments/CASE-A-v1.cfg",
        "declared_assumptions": [
            {"assumption": "optical specular 0.3 / diffuse 0.2, drag coefficient 2.2, "
                           "Sentman accommodation 0.9",
             "status": "explicit exploratory assumption of this study",
             "source": "docs/numerical-audit/experiments/CASE-A-v1.cfg header"},
            {"assumption": "bus 0.1 x 0.2 x 0.3 m, six axis-aligned faces, centre of "
                           "pressure at each geometric face centre plus (0, 0.001, 0) m",
             "status": "analytic construction of the case",
             "source": "docs/numerical-audit/experiments/CASE-A-v1.cfg"},
            {"assumption": "principal moments = uniform-bus tensor minus m*d^2 on Ix and "
                           "Iz with d = 1e-3 m (centre-of-mass offset along body y)",
             "status": "analytic construction; every moment stays strictly positive and "
                       "below the uniform-bus value",
             "source": "docs/numerical-audit/experiments/provenance.json"},
        ],
    },
    "CASE-B": {
        "configuration_source": "docs/numerical-audit/experiments/CASE-B-v1.cfg",
        "declared_assumptions": [
            {"assumption": "9 m^2 two-sided membrane represented by one +z and one -z "
                           "panel sharing the centre point {0, 0, 0.05} m",
             "status": "idealisation; both panel records describe the same membrane, so "
                       "the physical membrane area is 9 m^2 and not 18 m^2",
             "source": "docs/numerical-audit/experiments/CASE-B-v1.cfg"},
            {"assumption": "specular 0.6 / diffuse 0.15 reproduces the example's scalar "
                           "Cr = 1.7 at normal incidence",
             "status": "non-unique provisional mapping, not measured sail optics",
             "source": "docs/numerical-audit/experiments/CASE-B-v1.cfg header"},
            {"assumption": "principal moments 3.75/3.75/7.5 kg m^2",
             "status": "the source example's own idealised cross-brace/sail mass lumping "
                       "(m L^2/24, m L^2/24, m L^2/12 with m = 10 kg, L = 3 m); no flight "
                       "mass budget exists",
             "source": "/home/user/Project-Svarog-Orbital/examples/earth_orbit_acc.jl "
                        "lines 68-71 via docs/numerical-audit/experiments/provenance.json"},
            {"assumption": "all torques enabled (SRP, gravity-gradient, drag, albedo)",
             "status": "declared departure: the source example sets MODEL_TORQUES=false, "
                       "so conclusions about the example's own behaviour do not transfer",
             "source": "docs/numerical-audit/experiments/provenance.json"},
        ],
    },
    "CASE-C": {
        "configuration_source": "benchmarks/cases/bench_support.hpp :: dynamics(true) / :: initial()",
        "declared_assumptions": [
            {"assumption": "six 2 m^2 panels with centre of pressure 0.5*n + (0, 0.1, 0) m, "
                           "mass 10 kg, principal moments (2, 3, 4) kg m^2",
             "status": "unmodified historical benchmark factory; preserved unchanged",
             "source": "benchmarks/cases/bench_support.hpp lines 104-109"},
            {"assumption": "initial state: r = (R_equatorial + 400 km, 0, 0), "
                           "|v| = sqrt(mu/r) in direction (0, 0.8, 0.6), "
                           "q = (cos45, sin45, 0, 0), omega = (0.002, 0.001, 0.0005) rad/s",
             "status": "unmodified historical benchmark initial state",
             "source": "benchmarks/cases/bench_support.hpp lines 111-116"},
        ],
    },
}

DEFINITIONS = {
    "one_sided_projected_area_m2":
        "A(u) = sum_i A_i * max(0, n_i . u) over the panel records, with u the unit "
        "vector pointing from the body towards the source/flow. This is the area the "
        "drag and radiation laws of the audited kernel actually use (they loop over "
        "panels with n.u > 0), so all force areas here are one-sided.",
    "projected_area_bounds":
        "Exact min/max of A(u) over the unit sphere, by enumeration of the cell interiors "
        "(u = +-w_S/|w_S| for every incidence subset S, w_S = sum_{i in S} A_i n_i), the "
        "cell-edge stationary points (orthogonal projection of w_S onto each panel normal "
        "and onto every pair plane), one interior point of each such plane and the cell "
        "vertices (+-unit(n_i x n_j)). f is piecewise linear in u, so these critical "
        "points contain its extrema; a deterministic direction grid is evaluated as an "
        "independent cross-check of every reported bound.",
    "silhouette_columns":
        "The harness logs projected_area_{wind,sun}_silhouette_m2 = sum_i A_i |n_i . u|. "
        "For a closed six-panel body and for a paired two-panel membrane this equals "
        "exactly twice the one-sided projected area, i.e. it counts both sides of the same "
        "body. It is a geometric diagnostic only and is never used as a force area here.",
    "ballistic_coefficient_reference_kg_m2":
        "m / (2.2 * A_wind) with the declared reference coefficient 2.2 of the classical "
        "definition; a convention, not a measured coefficient of the case.",
    "ballistic_coefficient_effective_kg_m2":
        "m / (mean_t Cd_eff * mean_t A_wind), and the peak variant m / (max_t Cd_eff * "
        "max_t A_wind). The coefficient is the one measured on the case's own samples; the "
        "product of the two separate time averages is used, not a sample-wise product.",
    "effective_drag_coefficient":
        "Cd_eff = -(F_drag . unit(relative_wind)) / (q * A_wind) with q the logged dynamic "
        "pressure 0.5 rho |v_rel|^2 and A_wind the logged one-sided wind-facing area. A "
        "two-sided or double-counted area would push this above the admissible bracket "
        "[2 sigma, 2 sigma + 4 (1 - sigma)] of the Sentman law "
        "F = -q A [4 (1 - sigma) sin^2(a) n + 2 sigma sin(a) v_hat].",
    "effective_srp_coefficient":
        "Cr_eff = -(F_srp . unit(to_sun)) / (P_sun * illumination * A_sun) with "
        "P_sun = (1361/299792458) * (AU/d)^2 Pa and d the spacecraft-to-Sun distance. The "
        "admissible bracket of the kernel's panel law is [1 - s, 1 + s + 2 d_opt/3].",
    "torque_share":
        "share_i = mean_t(|tau_i|) / sum_j mean_t(|tau_j|), i.e. a share of time-averaged "
        "torque magnitudes. It is NOT a share of the vector sum of the torques: the sources "
        "act at different instants and in different directions, so a vector-sum share "
        "would be smaller than 1 and is not reported.",
    "angular_acceleration_consistency":
        "alpha_body recomputed as I^-1 (tau_total - omega x (I omega)) from the logged "
        "total torque, body rate and principal moments, compared with the logged angular "
        "acceleration channel.",
    "characteristic_times":
        "Spin-rate and momentum-change times use the initial body rate and the largest "
        "sampled magnitude of the corresponding quantity; the orientation-change time is a "
        "constant-acceleration scaling sqrt(2 theta I_min / tau_max) with theta = 1 rad, "
        "valid as a kinematic scaling for zero-spin cases. No libration, resonance or "
        "period model is claimed.",
}


# --------------------------------------------------------------------------- #
# Small helpers
# --------------------------------------------------------------------------- #
def fibonacci_directions(count):
    """Deterministic near-uniform unit vectors (golden-angle spiral)."""
    golden = math.pi * (3.0 - math.sqrt(5.0))
    out = []
    for i in range(count):
        z = 1.0 - (2.0 * i + 1.0) / count
        r = math.sqrt(max(0.0, 1.0 - z * z))
        theta = golden * i
        out.append((r * math.cos(theta), r * math.sin(theta), z))
    return out


def stats(t, values):
    """min/max/final plus cadence-weighted mean and RMS of one sampled series."""
    if not values:
        return None
    lo, hi = min(values), max(values)
    out = {"min": lo, "max": hi, "final": values[-1],
           "mean_time_weighted": time_mean(t, values),
           "rms_time_weighted": time_rms(t, values)}
    return out


def undefined(reason):
    return {"value": None, "undefined_reason": reason}


def vector_stats(t, vectors):
    return stats(t, [vnorm(v) for v in vectors])


def time_fraction(t, indicator):
    """Fraction of the sampled span for which the indicator series is nonzero."""
    return time_mean(t, [1.0 if x else 0.0 for x in indicator])


# --------------------------------------------------------------------------- #
# Geometry derived from the recorded panel set
# --------------------------------------------------------------------------- #
def projected_area(u, panels):
    return sum(p["area_m2"] * max(0.0, vdot(p["normal_body"], u)) for p in panels)


def projected_area_bounds(panels):
    """Exact bounds of the one-sided projected area over all unit directions."""
    normals = [p["normal_body"] for p in panels]
    planes = list(normals)
    for i, j in itertools.combinations(range(len(normals)), 2):
        axis = vcross(normals[i], normals[j])
        if vnorm(axis) > 1e-12:
            planes.append(vunit(axis))
    candidates = []
    if len(panels) <= MAX_EXACT_SURFACES:
        count = 1 << len(panels)
        for mask in range(count):
            w = (0.0, 0.0, 0.0)
            for i in range(len(panels)):
                if mask >> i & 1:
                    a = panels[i]["area_m2"]
                    n = panels[i]["normal_body"]
                    w = (w[0] + a * n[0], w[1] + a * n[1], w[2] + a * n[2])
            wn = vnorm(w)
            if wn > 1e-300:
                u = (w[0] / wn, w[1] / wn, w[2] / wn)
                candidates.append(u)
            for k in planes:
                along = vdot(w, k)
                p = (w[0] - along * k[0], w[1] - along * k[1], w[2] - along * k[2])
                pn = vnorm(p)
                if pn > 1e-300:
                    u = (p[0] / pn, p[1] / pn, p[2] / pn)
                    candidates.append(u)
        for k in planes:
            candidates.append(k)
            candidates.append((-k[0], -k[1], -k[2]))
            # A cell edge whose objective is constant along it (antiparallel panel
            # pairs make the edge a whole plane) has no stationary projection, so
            # sample one interior point of it from the coordinate axes instead.
            for axis in ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)):
                along = vdot(axis, k)
                p = (axis[0] - along * k[0], axis[1] - along * k[1], axis[2] - along * k[2])
                pn = vnorm(p)
                if pn > 1e-9:
                    u = (p[0] / pn, p[1] / pn, p[2] / pn)
                    candidates.append(u)
                    candidates.append((-u[0], -u[1], -u[2]))
        values = [projected_area(u, panels) for u in candidates]
        method = "exact_piecewise_linear_enumeration"
    else:
        values = [projected_area(u, panels) for u in fibonacci_directions(BOUND_CHECK_DIRECTIONS)]
        method = f"direction_grid_{BOUND_CHECK_DIRECTIONS}_points"

    grid = [projected_area(u, panels) for u in fibonacci_directions(BOUND_CHECK_DIRECTIONS)]
    best_index = max(range(len(values)), key=lambda i: values[i])
    worst_index = min(range(len(values)), key=lambda i: values[i])
    return {
        "min_m2": values[worst_index],
        "max_m2": values[best_index],
        "direction_of_max_body": list(candidates[best_index]) if len(panels) <= MAX_EXACT_SURFACES else None,
        "direction_of_min_body": list(candidates[worst_index]) if len(panels) <= MAX_EXACT_SURFACES else None,
        "method": method,
        "direction_grid_cross_check": {
            "points": BOUND_CHECK_DIRECTIONS,
            "grid_max_m2": max(grid),
            "grid_min_m2": min(grid),
            "grid_inside_bounds": max(grid) <= values[best_index] + 1e-12
                                   and min(grid) >= values[worst_index] - 1e-12,
        },
    }


def panel_closure(panels):
    """Residual of sum_i A_i n_i; zero for a closed panel set, 2*A for a paired membrane."""
    acc = [0.0, 0.0, 0.0]
    for p in panels:
        a = p["area_m2"]
        n = p["normal_body"]
        acc = [acc[0] + a * n[0], acc[1] + a * n[1], acc[2] + a * n[2]]
    return acc, vnorm(acc)


def cuboid_inference(panels):
    """Edges implied by axis-aligned face areas, and the CP-plane edge they imply."""
    axes = {}
    for p in panels:
        n = p["normal_body"]
        dominant = max(range(3), key=lambda k: abs(n[k]))
        if abs(abs(n[dominant]) - 1.0) > 1e-9 or any(abs(n[k]) > 1e-12 for k in range(3) if k != dominant):
            return {"applies": False,
                    "reason": "panel normals are not axis-aligned unit axes; no cuboid can "
                              "be inferred from the recorded face areas"}
        key = (dominant, 1 if n[dominant] > 0 else -1)
        axes[key] = axes.get(key, 0.0) + p["area_m2"]
    if len(axes) != 6 or len({k[0] for k in axes}) != 3:
        return {"applies": False,
                "reason": f"{len(axes)} axis-aligned panel record(s); a closed six-panel "
                          "cuboid is not determined by the recorded areas"}
    for k in (0, 1, 2):
        if abs(axes[(k, 1)] - axes[(k, -1)]) > 1e-12 * max(1.0, axes[(k, 1)]):
            return {"applies": False,
                    "reason": f"opposite faces normal to axis {k} have unequal areas; the "
                              "panel set is not a closed cuboid"}
    ax, ay, az = axes[(0, 1)], axes[(1, 1)], axes[(2, 1)]
    edges = {"x": math.sqrt(ay * az / ax), "y": math.sqrt(ax * az / ay), "z": math.sqrt(ax * ay / az)}
    return {"applies": True, "method": "edges from axis-aligned face areas: "
                                       "Lx = sqrt(Ay Az / Ax) and cyclic permutations",
            "face_area_m2": {"x": ax, "y": ay, "z": az},
            "edges_m": edges, "surface_area_sum_m2": 2.0 * (ax + ay + az)}


def cp_summary(panels):
    """Common centre-of-pressure offset and the residual per-panel lever arms."""
    n = len(panels)
    mean = [sum(p["cp_body"][k] for p in panels) / n for k in range(3)]
    residual = [tuple(p["cp_body"][k] - mean[k] for k in range(3)) for p in panels]
    residual_lever = [abs(vdot(residual[i], panels[i]["normal_body"])) for i in range(n)]
    per_axis = {}
    for i, p in enumerate(panels):
        k = max(range(3), key=lambda j: abs(p["normal_body"][j]))
        axis = "xyz"[k]
        per_axis[axis] = max(per_axis.get(axis, 0.0), residual_lever[i])
    return {
        "mean_cp_body_m": mean,
        "common_offset_norm_m": vnorm(mean),
        "max_cp_offset_norm_m": max(vnorm(p["cp_body"]) for p in panels),
        "residual_lever_max_m": max(vnorm(x) for x in residual),
        "residual_normal_lever_max_by_axis_m": per_axis,
        "per_surface_cp_body_m": [p["cp_body"] for p in panels],
        "per_surface_cp_offset_norm_m": [vnorm(p["cp_body"]) for p in panels],
        "per_surface_normal_lever_m": [abs(vdot(p["cp_body"], p["normal_body"])) for p in panels],
        "interpretation": "the mean centre of pressure is a common offset of every panel; the "
                          "residual lever of each panel is measured from that common point, so "
                          "the two do not combine into a single lever arm acting on a common "
                          "centre of pressure",
    }


def uniform_box_moments(mass, edges):
    return (mass * (edges["y"] ** 2 + edges["z"] ** 2) / 12.0,
            mass * (edges["x"] ** 2 + edges["z"] ** 2) / 12.0,
            mass * (edges["x"] ** 2 + edges["y"] ** 2) / 12.0)


def brace_lumping_check(mass, moments, panels):
    """Cross-check an idealised m L^2/24, m L^2/12 lumping against a single panel area."""
    by_normal = {}
    for p in panels:
        n = p["normal_body"]
        dominant = max(range(3), key=lambda k: abs(n[k]))
        key = (dominant, 1 if n[dominant] > 0 else -1)
        by_normal[key] = by_normal.get(key, 0.0) + p["area_m2"]
    if len(by_normal) != 2:
        return {"applies": False, "reason": "panel set is not an antiparallel pair"}
    areas = sorted(by_normal.values())
    if abs(areas[0] - areas[1]) > 1e-12 * max(1.0, areas[0]):
        return {"applies": False, "reason": "antiparallel panels have unequal areas"}
    length = math.sqrt(areas[0])
    thin, thick = min(moments), max(moments)
    return {
        "applies": True,
        "source_length_from_single_panel_area_m": length,
        "thin_moment_m_L2_over_24_kg_m2": mass * length * length / 24.0,
        "thick_moment_m_L2_over_12_kg_m2": mass * length * length / 12.0,
        "measured_thin_kg_m2": thin,
        "measured_thick_kg_m2": thick,
        "thin_relative_residual": abs(thin - mass * length * length / 24.0) / thin,
        "thick_relative_residual": abs(thick - mass * length * length / 12.0) / thick,
        "note": "the two panel records are the two sides of one membrane, so the physical "
                "membrane area is one panel area, not their sum",
    }


# --------------------------------------------------------------------------- #
# Per-case measurement
# --------------------------------------------------------------------------- #
def measure(result, path, root):
    data = unpack(result)
    physical = data["physical"]
    if not physical:
        raise MetricsError(f"{path}: result has no physical metadata; refusing to guess")
    case_id = physical["case_id"]
    notes = CASE_NOTES.get(case_id)
    if notes is None:
        raise MetricsError(f"{path}: case {case_id} has no declared assumption record; add "
                           "one to CASE_NOTES before publishing measurements for it")
    t = data["time_s"]
    mass = float(physical["mass_kg"])
    moments = tuple(float(x) for x in physical["inertia_diagonal_kg_m2"])
    panels = [{"area_m2": float(p["area_m2"]),
               "normal_body": tuple(float(x) for x in p["normal_body"]),
               "cp_body": tuple(float(x) for x in p["cp_body"]),
               "specular": float(p["specular"]), "diffuse": float(p["diffuse"]),
               "cd": float(p["cd"]), "accommodation": float(p["accommodation"])}
              for p in physical["surfaces"]]

    r = data["r"]
    v = data["v"]
    omega = data["omega_body_rad_s"]
    r0, v0, w0 = r[0], v[0], omega[0]
    elements = osculating_elements(r0, v0)

    wind = load_vector(data, "relative_wind_m_s")
    drag_force = load_vector(data, "drag_force_n")
    albedo_force = load_vector(data, "albedo_force_n")
    srp_force = data["srp_force_n"]
    non_grav = load_vector(data, "non_gravitational_force_n")
    sun_position = load_vector(data, "sun_position_m")
    total_torque = load_vector(data, "total_torque_nm")
    alpha = load_vector(data, "angular_acceleration_rad_s2")
    torque_sources = {
        "drag": load_vector(data, "drag_torque_nm"),
        "srp": load_vector(data, "srp_torque_nm"),
        "albedo": load_vector(data, "albedo_torque_nm"),
        "gravity_gradient": load_vector(data, "gravity_gradient_nm"),
    }
    q = load_scalar(data, "dynamic_pressure_pa")
    density = load_scalar(data, "density_kg_m3")
    altitude = load_scalar(data, "altitude_m")
    a_wind = load_scalar(data, "projected_area_wind_m2")
    a_sun = load_scalar(data, "projected_area_m2")
    silhouette_wind = load_scalar(data, "projected_area_wind_silhouette_m2")
    silhouette_sun = load_scalar(data, "projected_area_sun_silhouette_m2")
    illumination = data["illumination"]

    # Non-gravitational force closure: the harness reports the sum, so the
    # reconstruction residual is a measurement of internal consistency.
    residual = [max(abs(srp_force[k][i] - (non_grav[k][i] - drag_force[k][i] - albedo_force[k][i]))
                     for i in range(3)) for k in range(len(t))]
    # Angular-acceleration consistency against I^-1 (tau - omega x I omega).
    alpha_residual = []
    for k in range(len(t)):
        w = omega[k]
        angular_momentum = (moments[0] * w[0], moments[1] * w[1], moments[2] * w[2])
        gyroscopic = vcross(w, angular_momentum)
        body = tuple(total_torque[k][i] - gyroscopic[i] for i in range(3))
        predicted = (body[0] / moments[0], body[1] / moments[1], body[2] / moments[2])
        alpha_residual.append(math.sqrt(sum((predicted[i] - alpha[k][i]) ** 2 for i in range(3))))

    cd_eff, cd_t, cr_eff, cr_t = [], [], [], []
    for k in range(len(t)):
        direction = vunit(wind[k])
        if direction is not None and q[k] > 0.0 and a_wind[k] > 0.0:
            cd_eff.append(-vdot(drag_force[k], direction) / (q[k] * a_wind[k]))
            cd_t.append(t[k])
        if illumination[k] >= 0.5 and a_sun[k] > 0.0:
            offset = tuple(sun_position[k][i] - r[k][i] for i in range(3))
            distance = vnorm(offset)
            to_sun = vunit(offset)
            if to_sun is not None and distance > 0.0:
                pressure = SOLAR_PRESSURE_1AU_N_M2 * (ASTRONOMICAL_UNIT_M / distance) ** 2
                cr_eff.append(-vdot(srp_force[k], to_sun) /
                              (pressure * illumination[k] * a_sun[k]))
                cr_t.append(t[k])

    sigma = sorted({p["accommodation"] for p in panels})
    optical = sorted({(p["specular"], p["diffuse"]) for p in panels})
    torque_norms = {name: [vnorm(x) for x in vec] for name, vec in torque_sources.items()}
    total_norm = [vnorm(x) for x in total_torque]
    mean_norms = {name: time_mean(t, series) for name, series in torque_norms.items()}
    mean_total = sum(mean_norms.values())
    alpha_norm = [vnorm(x) for x in alpha]
    alpha_max = max(alpha_norm)
    i_min = min(moments)
    omega_norm = vnorm(w0)
    momentum_norm = vnorm((moments[0] * w0[0], moments[1] * w0[1], moments[2] * w0[2]))
    tau_max = max(total_norm)
    bounds = projected_area_bounds(panels)
    closure, closure_norm = panel_closure(panels)
    cuboid = cuboid_inference(panels)
    cp = cp_summary(panels)
    cp_half_edges = dict(cp["residual_normal_lever_max_by_axis_m"])
    if cuboid.get("applies"):
        area_half = {axis: 0.5 * cuboid["edges_m"][axis] for axis in cuboid["edges_m"]}
        cuboid["area_implied_half_edges_m"] = area_half
        cuboid["cp_residual_lever_half_edges_m"] = cp_half_edges
        cuboid["cp_plane_over_area_implied_edge_ratio"] = {
            axis: (cp_half_edges[axis] / area_half[axis]) for axis in area_half}
        cuboid["cp_plane_note"] = (
            "the comparison uses the residual lever measured from the common centre-of-pressure "
            "offset, so a common offset is not mistaken for a geometric edge")

    wind_stats = stats(t, a_wind)
    sun_stats = stats(t, a_sun)
    mean_a_wind = wind_stats["mean_time_weighted"]
    max_a_wind = wind_stats["max"]
    beta_ref = {
        "reference_cd": BALLASTIC_REFERENCE_CD,
        "at_mean_wind_facing_area": mass / (BALLASTIC_REFERENCE_CD * mean_a_wind) if mean_a_wind else None,
        "at_max_wind_facing_area": mass / (BALLASTIC_REFERENCE_CD * max_a_wind) if max_a_wind else None,
        "at_projected_area_upper_bound": (mass / (BALLASTIC_REFERENCE_CD * bounds["max_m2"])
                                          if bounds["max_m2"] > 0 else None),
        "definition": "m / (2.2 * A_wind); 2.2 is a declared reference constant of the "
                      "classical definition, not a measured property of the case",
    }
    cd_stats = stats(cd_t, cd_eff) if cd_eff else None
    cr_stats = stats(cr_t, cr_eff) if cr_eff else None
    if cd_stats is None or cr_stats is None:
        raise MetricsError(f"{path}: no sample qualified for the effective-coefficient "
                           "measurement; the channels are required to publish this case")

    times = {
        "spin_period_s": (2.0 * math.pi / omega_norm) if omega_norm > 0 else
        undefined("initial body rate is zero, so no spin period exists"),
        "spin_rate_change_time_s": (omega_norm / alpha_max) if (omega_norm > 0 and alpha_max > 0) else
        undefined("initial body rate is zero, so a spin-rate change time is not defined; "
                  "use the orientation-change scaling"),
        "momentum_change_time_s": (momentum_norm / tau_max) if (momentum_norm > 0 and tau_max > 0) else
        undefined("initial angular momentum is zero (zero spin), so a momentum-change time "
                  "is not defined; use the orientation-change scaling"),
        "orientation_change_1rad_time_s": (math.sqrt(2.0 * i_min / tau_max) if tau_max > 0 else None),
        "orientation_change_1rad_definition":
            "sqrt(2 * 1 rad * I_min / max_t |tau_total|): constant-acceleration kinematic "
            "scaling from the largest sampled torque. Not a libration or resonance period.",
        "per_source_momentum_change_time_s": {
            name: ((momentum_norm / max(series)) if (momentum_norm > 0 and max(series) > 0)
                   else undefined("zero initial angular momentum" if momentum_norm <= 0
                                  else "this source produced no sampled torque"))
            for name, series in torque_norms.items()},
    }

    inertia = {
        "mass_kg": mass,
        "principal_moments_kg_m2": list(moments),
        "harness_asymmetry_ratio": physical.get("inertia_asymmetry_ratio"),
        "measured_asymmetry_ratio_max_over_min": max(moments) / min(moments),
        "triangle_inequalities_satisfied": all(
            moments[i] + moments[j] >= moments[k]
            for i, j, k in ((0, 1, 2), (0, 2, 1), (1, 2, 0))),
        "all_moments_positive": all(x > 0 for x in moments),
    }
    if cuboid.get("applies"):
        reference = uniform_box_moments(mass, cuboid["edges_m"])
        ratios = [moments[i] / reference[i] for i in range(3)]
        deviation = max(abs(x - 1.0) for x in ratios)
        inertia["uniform_cuboid_moments_kg_m2"] = list(reference)
        inertia["measured_over_uniform_cuboid"] = ratios
        inertia["uniform_cuboid_max_relative_deviation"] = deviation
        inertia["uniform_cuboid_note"] = (
            "the recorded moments reproduce the uniform-bus tensor of the inferred cuboid to "
            f"{deviation:.2e} in relative terms; the residual deficit is the centre-of-mass "
            "offset declared in the case configuration, not an independently measured mass "
            "distribution" if deviation <= 1e-3 else
            "the recorded moments are not the uniform-bus tensor of the inferred cuboid "
            f"(ratios {[round(x, 4) for x in ratios]}); they are declared case inputs and no "
            "mass distribution in the source records explains the difference")

    block = {
        "input": {
            "path": os_relative(path, root),
            "setting_name": Path(path).stem,
            "sha256": sha256_file(path),
            "command": result.get("command"),
            "scenario": result.get("scenario"),
            "method": result.get("method"),
            "duration_s": result.get("duration_s"),
            "output_cadence_s": result.get("cadence_s"),
            "rtol": result.get("rtol"),
            "atol_scale": result.get("atol_scale"),
            "max_step_s": result.get("max_step_s"),
            "state_precision": result.get("state_precision"),
            "double_mantissa_bits": result.get("double_mantissa_bits"),
            "rhs_calls": result.get("rhs_calls"),
            "rejected_steps": result.get("rejected"),
            "samples": len(t),
        },
        "configuration": {
            "case_id": case_id,
            "version": physical.get("version"),
            "configuration_source": physical.get("source_file"),
            "legacy_benchmark_factory": physical.get("legacy_benchmark_factory"),
            "density_scale_case": physical.get("density_scale_case"),
            "density_scale_applied": physical.get("density_scale_applied"),
            "earth_albedo": physical.get("earth_albedo"),
            "j2_enabled": physical.get("j2_enabled"),
            "models": physical.get("models"),
            "declared_assumptions": notes["declared_assumptions"],
        },
        "initial_conditions": {
            "orbit_radius_m": vnorm(r0),
            "orbit_altitude_above_equator_m": vnorm(r0) - EARTH_EQUATORIAL_RADIUS_M,
            "orbit_speed_m_s": vnorm(v0),
            "point_mass_sma_m": elements["sma_m"],
            "point_mass_eccentricity": elements["eccentricity"],
            "point_mass_inclination_deg": (math.degrees(elements["inclination_rad"])
                                           if elements["inclination_rad"] is not None else None),
            "body_rate_rad_s": list(w0),
            "body_rate_norm_rad_s": omega_norm,
            "note": "these are the t = 0 source values; they are exact for the versioned "
                    "configuration and carry no integration-setting dependence",
        },
        "mass_and_inertia": inertia,
        "statistics_scope": {
            "authoritative_at_t0": "the configuration block, the panel records, the principal "
                                    "moments, the declared optical/aerodynamic coefficients, "
                                    "the initial body rate and the initial orbital elements are "
                                    "exact consequences of the versioned source and hold "
                                    "independently of any integration setting",
            "long_horizon_statistics": "every mean, RMS, maximum and share below is a "
                                        "statistic of the single integration recorded in "
                                        "input.path, not a converged physical prediction; the "
                                        "convergence status of a case's final state is assessed "
                                        "in docs/numerical-audit/CONVERGENCE.md and not here",
        },
        "surfaces": {
            "panel_records": len(panels),
            "sum_of_panel_record_areas_m2": physical.get("total_area_m2"),
            "panels": panels,
            "closure_residual_area_vector_m2": closure,
            "closure_residual_norm_m2": closure_norm,
            "presents_area_in_every_direction": bounds["min_m2"] > 0.0,
            "physical_area_m2": (sum(p["area_m2"] for p in panels) if bounds["min_m2"] > 0.0
                                 else 0.5 * sum(p["area_m2"] for p in panels)),
            "panel_set_interpretation": (
                "every direction meets at least one panel, so the record sum is the physical "
                "surface area of a closed shell"
                if bounds["min_m2"] > 0.0 else
                "some direction meets no panel at all, so the records are an open/two-sided "
                "representation of one physical surface and its record sum is twice that "
                "surface; every area-to-mass and ballistic-coefficient number here uses the "
                "one-sided projected area, never the record sum"),
            "one_sided_projected_area_bounds_m2": bounds,
            "centres_of_pressure": cp,
            "cuboid_inference": cuboid,
            "idealised_lumping_check": brace_lumping_check(mass, moments, panels),
        },
        "orbit": {
            "initial_radius_m": vnorm(r0),
            "initial_altitude_above_equator_m": vnorm(r0) - EARTH_EQUATORIAL_RADIUS_M,
            "initial_speed_m_s": vnorm(v0),
            "point_mass_elements_at_t0": {
                "sma_m": elements["sma_m"],
                "eccentricity": elements["eccentricity"],
                "inclination_rad": elements["inclination_rad"],
                "inclination_deg": (math.degrees(elements["inclination_rad"])
                                    if elements["inclination_rad"] is not None else None),
                "specific_energy_j_kg": elements["specific_energy_j_kg"],
                "angular_momentum_kg_m2_s": elements["angular_momentum_kg_m2_s"],
                "note": "point-mass diagnostic of the initial state; with J2 enabled these "
                        "elements are not conserved along the propagation",
            },
            "initial_altitude_stats_m": stats(t, altitude),
            "radius_stats_m": stats(t, [vnorm(x) for x in r]),
            "speed_stats_m_s": stats(t, [vnorm(x) for x in v]),
            "relative_wind_speed_stats_m_s": stats(t, [vnorm(x) for x in wind]),
        },
        "environment": {
            "density_stats_kg_m3": stats(t, density),
            "dynamic_pressure_stats_pa": stats(t, q),
            "solar_illumination_fraction_full_sun": time_fraction(t, [x >= 0.999 for x in illumination]),
            "solar_illumination_fraction_umbra": time_fraction(t, [x <= 0.0 for x in illumination]),
            "solar_illumination_fraction_penumbra": time_fraction(t, [0.0 < x < 0.999 for x in illumination]),
            "solar_illumination_mean_time_weighted": time_mean(t, illumination),
        },
        "areas_and_area_to_mass": {
            "wind_facing_one_sided_area_stats_m2": wind_stats,
            "sun_facing_one_sided_area_stats_m2": sun_stats,
            "area_to_mass_at_mean_wind_facing_m2_per_kg": mean_a_wind / mass,
            "area_to_mass_at_projected_area_upper_bound_m2_per_kg": bounds["max_m2"] / mass,
            "area_to_mass_at_projected_area_lower_bound_m2_per_kg": bounds["min_m2"] / mass,
            "area_to_mass_total_panel_record_area_m2_per_kg": physical["total_area_m2"] / mass,
            "silhouette_diagnostic": {
                "wind_silhouette_mean_m2": time_mean(t, silhouette_wind),
                "sun_silhouette_mean_m2": time_mean(t, silhouette_sun),
                "wind_silhouette_over_one_sided_mean_ratio": (time_mean(t, silhouette_wind) / mean_a_wind
                                                             if mean_a_wind else None),
                "sun_silhouette_over_one_sided_mean_ratio": (time_mean(t, silhouette_sun) /
                                                             (sun_stats["mean_time_weighted"] or 1.0)
                                                             if sun_stats["mean_time_weighted"] else None),
                "note": DEFINITIONS["silhouette_columns"],
            },
        },
        "forces": {
            "drag_force_norm_n": vector_stats(t, drag_force),
            "srp_force_norm_n": vector_stats(t, srp_force),
            "albedo_force_norm_n": vector_stats(t, albedo_force),
            "non_gravitational_force_norm_n": vector_stats(t, non_grav),
            "accelerations_m_s2": {
                "drag": vector_stats(t, [tuple(x / mass for x in f) for f in drag_force]),
                "srp": vector_stats(t, [tuple(x / mass for x in f) for f in srp_force]),
                "albedo": vector_stats(t, [tuple(x / mass for x in f) for f in albedo_force]),
                "non_gravitational": vector_stats(t, [tuple(x / mass for x in f) for f in non_grav]),
            },
            "non_gravitational_force_reconstruction_residual_n": {
                "max": max(residual),
                "note": "max over samples and axes of |F_non_grav - (F_srp + F_drag + F_albedo)|",
            },
            "effective_drag_coefficient": {
                "measured": cd_stats,
                "samples_used": len(cd_eff),
                "admissible_bracket": {
                    "min": 2.0 * min(sigma),
                    "max": 2.0 * max(sigma) + 4.0 * (1.0 - max(sigma)),
                    "definition": DEFINITIONS["effective_drag_coefficient"],
                },
                "measured_within_bracket": (cd_stats["min"] >= 2.0 * min(sigma) - 1e-9 and
                                            cd_stats["max"] <= 2.0 * max(sigma) +
                                            4.0 * (1.0 - max(sigma)) + 1e-9),
            },
            "effective_srp_coefficient": {
                "measured": cr_stats,
                "samples_used": len(cr_eff),
                "selection": "samples with solar illumination factor >= 0.5 and nonzero "
                             "sun-facing projected area",
                "normal_incidence_value": [[s, d, 1.0 + s + 2.0 * d / 3.0] for s, d in optical],
                "admissible_bracket": {
                    "min": min(1.0 - s for s, _ in optical),
                    "max": max(1.0 + s + 2.0 * d / 3.0 for s, d in optical),
                    "definition": DEFINITIONS["effective_srp_coefficient"],
                },
                "measured_within_bracket": (cr_stats["min"] >= min(1.0 - s for s, _ in optical) - 1e-9 and
                                            cr_stats["max"] <= max(1.0 + s + 2.0 * d / 3.0
                                                                  for s, d in optical) + 1e-9),
            },
            "albedo_effective_pressure": {
                "value": None,
                "undefined_reason": "the harness logs the solar and wind incidence areas but "
                                    "not the anti-solar incidence area of the simplified "
                                    "albedo law, so no effective albedo pressure coefficient "
                                    "can be measured from these records",
            },
            "ballistic_coefficient_reference_kg_m2": beta_ref,
            "ballistic_coefficient_effective_kg_m2": {
                "at_mean_wind_facing_area": (mass / (cd_stats["mean_time_weighted"] * mean_a_wind)
                                             if cd_stats["mean_time_weighted"] and mean_a_wind else None),
                "at_max_wind_facing_area": (mass / (cd_stats["max"] * max_a_wind)
                                            if cd_stats["max"] and max_a_wind else None),
                "definition": DEFINITIONS["ballistic_coefficient_effective_kg_m2"],
            },
        },
        "torques_and_attitude": {
            "source_norm_stats_nm": {name: stats(t, series) for name, series in torque_norms.items()},
            "total_norm_stats_nm": stats(t, total_norm),
            "relative_shares_of_mean_norms": {
                name: (mean_norms[name] / mean_total if mean_total > 0 else None)
                for name in torque_norms},
            "share_definition": DEFINITIONS["torque_share"],
            "angular_acceleration_norm_stats_rad_s2": stats(t, alpha_norm),
            "angular_acceleration_recomputation_residual_rad_s2": {
                "max": max(alpha_residual),
                "definition": DEFINITIONS["angular_acceleration_consistency"],
            },
            "initial_body_rate_rad_s": list(w0),
            "initial_body_rate_norm_rad_s": omega_norm,
            "initial_angular_momentum_norm_kg_m2_s": momentum_norm,
            "characteristic_times": times,
        },
    }
    return case_id, block


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def os_relative(path, root):
    path = Path(path).resolve()
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


def source_tree_commit(root):
    head = root / ".git" / "HEAD"
    if not head.exists():
        return None
    text = head.read_text().strip()
    if text.startswith("ref: "):
        ref = text[5:]
        loose = root / ".git" / ref
        if loose.exists():
            return loose.read_text().strip()
        packed = root / ".git" / "packed-refs"
        if packed.exists():
            for line in packed.read_text().splitlines():
                if line.endswith(" " + ref) and not line.startswith("#"):
                    return line.split(" ", 1)[0]
        return None
    return text or None


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--inputs", nargs="+", required=True, metavar="RAW_JSON",
                        help="one raw nominal audit result per case")
    parser.add_argument("--output", required=True, metavar="PATH",
                        help="fresh path for the generated evidence file")
    parser.add_argument("--overwrite", action="store_true",
                        help="replace an existing output file (default: refuse)")
    args = parser.parse_args()

    output = Path(args.output)
    if output.exists() and not args.overwrite:
        raise SystemExit(f"{output} already exists; pass --overwrite or choose a fresh path")
    root = HERE.parent.parent
    if not (root / ".git").exists():
        root = Path.cwd()

    cases, seen = {}, set()
    for path in args.inputs:
        path = Path(path)
        if not path.is_file():
            raise SystemExit(f"input not found: {path}")
        with open(path) as handle:
            result = json.load(handle)
        case_id, block = measure(result, path, root)
        if case_id in seen:
            raise SystemExit(f"case {case_id} supplied twice; pass exactly one nominal file per case")
        seen.add(case_id)
        cases[case_id] = block

    document = {
        "schema": "leo-dynamics/spacecraft-characteristics/2",
        "generator": "docs/numerical-audit/spacecraft_characteristics.py",
        "content": "measured spacecraft characteristics of the version-1 sensitivity cases; "
                   "every value is metadata, a measurement of the sampled load series, or a "
                   "documented function of those. No generic spacecraft prior, typical "
                   "dimension or typical coefficient is used anywhere.",
        "supersedes": "the unpublished analytic draft of this file, which used exaggerated "
                      "dimension and torque scalings, vector-sum torque shares and invented "
                      "CubeSat area-to-mass and ballistic-coefficient ranges",
        "leo_dynamics_source_commit": source_tree_commit(root),
        "kernel_constants_used": {
            "mu_earth_m3_s2": MU_EARTH_M3_S2,
            "earth_equatorial_radius_m": EARTH_EQUATORIAL_RADIUS_M,
            "astronomical_unit_m": ASTRONOMICAL_UNIT_M,
            "solar_pressure_1au_n_m2": SOLAR_PRESSURE_1AU_N_M2,
            "ballistic_reference_cd": BALLASTIC_REFERENCE_CD,
            "source": "include/leo/kernel/radiation.hpp, include/leo/environment",
        },
        "definitions": DEFINITIONS,
        "cases": dict(sorted(cases.items())),
        "cross_case": cross_case(cases),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2, allow_nan=False, sort_keys=False) + "\n")
    print(f"wrote {output}")


def cross_case(cases):
    """Comparisons between the measured cases only; no external reference ranges."""
    rows = {}
    for case_id, block in cases.items():
        areas = block["areas_and_area_to_mass"]
        torques = block["torques_and_attitude"]
        forces = block["forces"]
        rows[case_id] = {
            "area_to_mass_at_mean_wind_facing_m2_per_kg": areas["area_to_mass_at_mean_wind_facing_m2_per_kg"],
            "projected_area_bounds_m2": [block["surfaces"]["one_sided_projected_area_bounds_m2"]["min_m2"],
                                         block["surfaces"]["one_sided_projected_area_bounds_m2"]["max_m2"]],
            "ballistic_coefficient_reference_kg_m2":
                forces["ballistic_coefficient_reference_kg_m2"]["at_mean_wind_facing_area"],
            "ballistic_coefficient_effective_kg_m2":
                forces["ballistic_coefficient_effective_kg_m2"]["at_mean_wind_facing_area"],
            "dynamic_pressure_mean_pa": block["environment"]["dynamic_pressure_stats_pa"]["mean_time_weighted"],
            "dominant_torque_source": max(
                torques["relative_shares_of_mean_norms"].items(),
                key=lambda kv: kv[1] if kv[1] is not None else -1.0)[0],
            "max_angular_acceleration_rad_s2":
                torques["angular_acceleration_norm_stats_rad_s2"]["max"],
        }
    out = {"measured_rows": dict(sorted(rows.items())),
           "note": "rows are comparable because they are measured with the same definitions on "
                   "each case's own nominal propagation; they are not comparisons against any "
                   "external or generic spacecraft class"}
    ratios = {}
    ids = sorted(rows)
    for i, a in enumerate(ids):
        for b in ids[i + 1:]:
            key = f"{a}_over_{b}"
            entry = {}
            for field, va in rows[a].items():
                vb = rows[b][field]
                if isinstance(va, (int, float)) and isinstance(vb, (int, float)) and vb not in (0, None):
                    entry[field] = va / vb
            ratios[key] = entry
    out["ratios"] = ratios
    return out


if __name__ == "__main__":
    main()