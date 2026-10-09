#!/usr/bin/env python3
"""Versioned physical sensitivity study using the existing numerical-audit binary.

Every RHS still queries direct CSPICE. Temporary run-result files are scientific
artifacts, not ephemeris caches. Conclusions use response magnitudes (not slopes)
against same-unit numerical envelopes and both method/step-size consistency.
"""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "experiments"))
from metrics import summarize, compare_observables, scalar_derivative, so3_relative_rotation

HORIZONS = (60, 600, 1800, 3600, 7200, 10800, 21600)
SETTINGS = {
    "default": {},
    "rtol-11": {"rtol": 1e-11}, "rtol-12": {"rtol": 1e-12}, "rtol-13": {"rtol": 1e-13},
    "atol-.1": {"atol_scale": .1}, "atol-.01": {"atol_scale": .01}, "atol-.001": {"atol_scale": .001},
    "step-5": {"max_step": 5}, "step-1": {"max_step": 1},
    "dopri-tight": {"rtol": 1e-13, "atol_scale": .001, "max_step": .5},
    "rkf78-2": {"method": "rkf78", "rtol": 1e-14, "atol_scale": .0001, "max_step": 2},
    "rkf78-1": {"method": "rkf78", "rtol": 1e-14, "atol_scale": .0001, "max_step": 1},
    "rkf78-half": {"method": "rkf78", "rtol": 1e-14, "atol_scale": .0001, "max_step": .5},
    "rk4-1": {"method": "rk4", "max_step": 1},
    "rk4-half": {"method": "rk4", "max_step": .5},
    "rk4-quarter": {"method": "rk4", "max_step": .25},
    "mixed-quarter": {"method": "mixed_rk4", "max_step": .25},
    "cadence-half": {"method": "rkf78", "rtol": 1e-14, "atol_scale": .0001, "max_step": .5, "cadence": 15},
}
FINE = ("dopri-tight", "rkf78-1", "rkf78-half", "rk4-half", "rk4-quarter", "mixed-quarter", "cadence-half")
SENSITIVITY_METHODS = {k: SETTINGS[k] for k in ("dopri-tight", "rkf78-half", "rk4-half", "rk4-quarter")}
PARAMETERS = (
    ("position_radial", "--position-radial", (.1, 1., 10.), False, "m", 0.),
    ("velocity_along", "--velocity-along", (1e-4, 1e-3, 1e-2), False, "m/s", 0.),
    ("attitude_orbit", "--attitude-orbit", (1e-6, 1e-5, 1e-4), False, "rad", 0.),
    ("body_rate", "--omega-x", (1e-6, 1e-5, 1e-4), False, "rad/s", 0.),
    ("density", "--density-factor", (.01, .05, .1), True, "fraction", 1.),
    ("area", "--area-factor", (.01, .05, .1), True, "fraction", 1.),
    ("mass", "--mass-factor", (.01, .05, .1), True, "fraction", 1.),
    ("inertia", "--inertia-factor", (.01, .05, .1), True, "fraction", 1.),
    ("cp_y", "--cp-y", (.0001, .001, .005), False, "m", 0.),
    ("accommodation", "--sigma-delta", (.01, .03, .05), False, "dimensionless", .9),
    ("drag_coefficient", "--cd-factor", (.01, .05, .1), True, "fraction", 1.),
    ("specular", "--specular-delta", (.01, .03, .05), False, "dimensionless", None),
    ("diffuse", "--diffuse-delta", (.01, .03, .05), False, "dimensionless", None),
    ("spin", "--spin-factor", (.01, .05, .1), True, "fraction", 1.),
)
SCALARS = {
    "sma_final_m": ("orbital", "semi_major_axis_m", "final"),
    "eccentricity": ("orbital", "eccentricity", "final"),
    "energy_change_j_kg": ("orbital", "specific_energy_change_j_kg"),
    "plane_rotation_rad": ("orbital", "angular_momentum_plane_rotation_rad"),
    "pointing_sun_rms_rad": ("pointing", "to_sun_rms_rad"),
    "mean_projected_area_m2": ("environment", "projected_area_time_average_m2"),
    "effective_projected_area_m2": ("environment", "effective_projected_area_time_average_m2"),
    "srp_work_j": ("non_gravitational", "srp_work_j"),
    "drag_work_j": ("non_gravitational", "drag_work_j"),
    "non_grav_work_j": ("non_gravitational", "total_non_grav_work_j"),
    "srp_impulse_n_s": ("non_gravitational", "srp_impulse_norm_n_s"),
    "drag_impulse_n_s": ("non_gravitational", "drag_impulse_norm_n_s"),
    "omega_final_rad_s": ("attitude", "angular_speed_rad_s", "final"),
    "momentum_final_kg_m2_s": ("attitude", "angular_momentum_kg_m2_s", "final"),
    "nutation_rad": ("attitude", "spin_axis_nutation_max_rad"),
    "precession_rad_s": ("attitude", "spin_axis_precession_rate_rad_s"),
}
STATE_KEYS = ("position_m", "velocity_m_s", "attitude_angle_rad", "omega_rad_s", "spin_axis_angle_rad")


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, allow_nan=False, separators=(",", ":")) + "\n")


def value(summary, path):
    node = summary
    for key in path:
        if not isinstance(node, dict): return None
        node = node.get(key)
    return node if isinstance(node, (int, float)) and not isinstance(node, bool) and math.isfinite(node) else None


def scalars(result):
    summary = summarize(result)
    return {key: value(summary, path) for key, path in SCALARS.items()}, summary


def prefix(result, horizon):
    keep = sum(row[0] <= horizon for row in result["samples"])
    return dict(result, samples=result["samples"][:keep], loads=result["loads"][:keep])


def run(args, case, horizon, settings=None, option=()):
    cmd = [str(args.binary.resolve()), "--spk", str(args.spk.resolve()), "--lsk", str(args.lsk.resolve()),
           "--scenario", "full", "--case-file", str(case.resolve()), "--duration", str(horizon)]
    for key, val in sorted((settings or {}).items()): cmd += ["--" + key.replace("_", "-"), str(val)]
    cmd += list(map(str, option))
    done = subprocess.run(cmd, check=True, text=True, capture_output=True, timeout=3600)
    result = json.loads(done.stdout)
    result["command"] = cmd
    return result


def spread(results):
    scalar_results = {k: scalars(r)[0] for k, r in results.items()}
    bounds = {key: None for key in (*STATE_KEYS, *SCALARS)}
    for key in SCALARS:
        vals = [s[key] for s in scalar_results.values()]
        if all(v is not None for v in vals): bounds[key] = max(vals) - min(vals)
    for a, b in itertools.combinations(results.values(), 2):
        comparison = compare_observables(a, b)
        for key in STATE_KEYS:
            number = comparison["maximum"].get(key)
            if number is not None: bounds[key] = max(bounds[key] or 0, number)
    return bounds


def relative_difference(a, b):
    if a is None or b is None: return None
    scale = max(abs(a), abs(b))
    return abs(a-b)/scale if scale else 0.

def parameter_origin(raw, parameter, case_id, fallback):
    phys=raw["physical"];surfaces=phys["surfaces"];s=raw["samples"][0]
    if parameter=="position_radial": return math.hypot(*s[1:4])
    if parameter=="velocity_along": return math.hypot(*s[4:7])
    if parameter=="body_rate": return s[13] if case_id=="CASE-B" else s[11]
    if parameter=="cp_y": return sum(row["cp_body"][1] for row in surfaces)/len(surfaces)
    if parameter=="specular": return surfaces[0]["specular"]
    if parameter=="diffuse": return surfaces[0]["diffuse"]
    return fallback


def realised_parameters(base, plus, minus, parameter, multiplicative, case_id):
    if parameter=="attitude_orbit":
        return [so3_relative_rotation(base["samples"][0][7:11],r["samples"][0][7:11])[1] for r in (plus,minus)]
    if parameter=="density":
        return [r["physical"]["density_scale_applied"]/base["physical"]["density_scale_applied"] for r in (plus,minus)]
    if parameter in ("mass","area","inertia","drag_coefficient","spin"):
        def field(r):
            phys=r["physical"]
            if parameter=="mass": return phys["mass_kg"]
            if parameter=="area": return sum(x["area_m2"] for x in phys["surfaces"])
            if parameter=="inertia": return sum(phys["inertia_diagonal_kg_m2"])
            if parameter=="drag_coefficient": return phys["surfaces"][0]["cd"]
            return math.hypot(*r["samples"][0][11:14])
        b=field(base)
        return [field(r)/b if b else None for r in (plus,minus)]
    return [parameter_origin(r,parameter,case_id,0) for r in (plus,minus)]



def selected_series(comparison):
    src = comparison["series"]
    idx = sorted(set(range(0, len(src["time_s"]), max(1, len(src["time_s"])//80))) | {len(src["time_s"])-1})
    return {key: [src[key][i] for i in idx] for key in ("time_s", "position_m", "attitude_angle_rad", "omega_rad_s")}


def state_slope(base, plus, minus, step):
    b, p, n = (x["samples"][-1] for x in (base, plus, minus))
    rotation_p = so3_relative_rotation(b[7:11], p[7:11])[0]
    rotation_n = so3_relative_rotation(b[7:11], n[7:11])[0]
    vectors = {
        "position_m": [(p[i]-n[i])/(2*step) for i in range(1, 4)],
        "velocity_m_s": [(p[i]-n[i])/(2*step) for i in range(4, 7)],
        "omega_rad_s": [(p[i]-n[i])/(2*step) for i in range(11, 14)],
        "attitude_angle_rad": [(x-y)/(2*step) for x, y in zip(rotation_p, rotation_n)],
    }
    return {key: math.hypot(*v) for key, v in vectors.items()}, vectors


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, required=True); p.add_argument("--spk", type=Path, required=True)
    p.add_argument("--lsk", type=Path, required=True); p.add_argument("--cases", type=Path, nargs="+", required=True)
    p.add_argument("--output", type=Path, required=True); p.add_argument("--workdir", type=Path, required=True)
    p.add_argument("--horizons", type=int, nargs="+", default=list(HORIZONS))
    p.add_argument("--sensitivity-horizons", type=int, nargs="+", default=[3600, 21600])
    p.add_argument("--phase", choices=("convergence", "all"), default="all")
    a = p.parse_args()
    if a.output.exists(): p.error("fresh output required")
    if not set(a.sensitivity_horizons) <= set(a.horizons) and a.phase=="all": p.error("sensitivity horizons must be measured")
    report = dict(schema="leo-dynamics/sensitivity-v1.1", cases={}, horizons_s=a.horizons,
                  source_parent="0c6a9c365a5d09c2049ea5591e76bf7d99b0761e", seed=20261009,
                  policy="Responses require >=10x same-unit numerical envelope, <=20% method and neighbor-size slope disagreement; undefined/zero channels unresolved.",
                  binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),
                  config_sha256={str(c):hashlib.sha256(c.read_bytes()).hexdigest() for c in a.cases})
    maximum_horizon = max(a.horizons)
    for case in a.cases:
        case_id = next(line.split()[1] for line in case.read_text().splitlines() if line.startswith("id "))
        entry = report["cases"][case_id] = dict(convergence=[], sensitivity=[], config=str(case), contributions={})
        base_runs = {}
        for label, settings in SETTINGS.items():
            raw = run(a, case, maximum_horizon, settings)
            save(a.workdir/case_id/(label+".json"), raw); base_runs[label] = raw
            print(case_id,label,raw["rhs_calls"],flush=True)
        for horizon in a.horizons:
            runs = {k:prefix(v,horizon) for k,v in base_runs.items()}
            fine = {k:runs[k] for k in FINE}; fine_u = spread(fine)
            u = {k:None if v is None else 2*v for k,v in fine_u.items()}
            ref = runs["rkf78-half"]
            errors = {k:compare_observables(ref,v)["maximum"] for k,v in runs.items()}
            trends = dict(rkf78=[errors[k] for k in ("rkf78-2","rkf78-1")],
                          rk4=[compare_observables(runs[x],runs[y])["maximum"] for x,y in (("rk4-1","rk4-half"),("rk4-half","rk4-quarter"))])
            entry["convergence"].append(dict(horizon_s=horizon,uncertainty=u,fine_uncertainty=u,coarse_spread=spread(runs),
                observables={k:summarize(v) for k,v in runs.items()},
                sweeps=[dict(label=k,settings=SETTINGS[k],observables={q:errors[k][q] for q in STATE_KEYS if errors[k].get(q) is not None}) for k in runs],
                refinement=trends,precision={k:{q:v[q] for q in ("double_mantissa_bits","long_double_mantissa_bits","state_precision","force_ephemeris_precision","rhs_calls")} for k,v in runs.items()},
                physical=ref["physical"]))
        save(a.output,report)
        if a.phase!="all": continue
        sensitivity_names = ("dopri-tight","rkf78-half","rk4-half","rk4-quarter")
        records = []
        for parameter, flag, deltas, multiplicative, unit, p0 in PARAMETERS:
            if parameter=="body_rate" and case_id=="CASE-B": flag="--omega-z"
            for delta in deltas:
                plus_value = 1+delta if multiplicative else delta
                minus_value = 1-delta if multiplicative else -delta
                pairs = {}
                for label in sensitivity_names:
                    pairs[label] = (run(a,case,maximum_horizon,SETTINGS[label],(flag,plus_value)),
                                    run(a,case,maximum_horizon,SETTINGS[label],(flag,minus_value)))
                for horizon in a.sensitivity_horizons:
                    nominal = {k:prefix(base_runs[k],horizon) for k in sensitivity_names}
                    envelope = next(r["uncertainty"] for r in entry["convergence"] if r["horizon_s"]==horizon)
                    method_data = {}; responses = {}
                    perturbed_envelopes=[spread({k:prefix(pair[side],horizon) for k,pair in pairs.items()}) for side in (0,1)]
                    realised = []
                    for label,(plus,minus) in pairs.items():
                        base = nominal[label]; plus=prefix(plus,horizon);minus=prefix(minus,horizon)
                        bs,ps,ns = (scalars(x)[0] for x in (base,plus,minus))
                        origin=parameter_origin(base,parameter,case_id,p0)
                        method_data[label] = {key:scalar_derivative(ps[key],ns[key],delta,bs[key],origin) for key in SCALARS}
                        gradients, vectors = state_slope(base,plus,minus,delta)
                        responses[label] = (compare_observables(base,plus,series=True),compare_observables(base,minus))
                        for key,val in gradients.items(): method_data[label][key] = dict(gradient=val,gradient_vector=vectors[key],normalized_gradient=None,asymmetry_relative=None)
                        if label=="rkf78-half":
                            realised=realised_parameters(base,plus,minus,parameter,multiplicative,case_id)
                    cells = {}
                    for key in (*SCALARS,*STATE_KEYS):
                        gradients = [method_data[k].get(key,{}).get("gradient") for k in sensitivity_names]
                        consistency = max((relative_difference(x,y) for x,y in itertools.combinations(gradients,2) if x is not None and y is not None),default=None)
                        if key in STATE_KEYS and key!="spin_axis_angle_rad":
                            vectors=[method_data[k][key]["gradient_vector"] for k in sensitivity_names]
                            consistency=max(math.hypot(*(x-y for x,y in zip(v,w)))/max(math.hypot(*v),math.hypot(*w),1e-300) for v,w in itertools.combinations(vectors,2))
                        if key in STATE_KEYS:
                            known=[r[side]["maximum"].get(key) for r in responses.values() for side in (0,1)]
                            response=max(known) if all(v is not None for v in known) else None
                        else:
                            selected = method_data["rkf78-half"][key]
                            response = max(abs(selected["plus"]-selected["base"]),abs(selected["minus"]-selected["base"])) if all(selected[x] is not None for x in ("plus","minus","base")) else None
                        estimates=[envelope.get(key),*(None if e.get(key) is None else 2*e[key] for e in perturbed_envelopes)]
                        uncertainty=max(estimates) if all(v is not None for v in estimates) else None
                        asymmetry = method_data["rkf78-half"].get(key,{}).get("asymmetry_relative")
                        resolved = response is not None and uncertainty is not None and response>0 and uncertainty>0 and response>=10*uncertainty and consistency is not None and consistency<=.2
                        reason = None if resolved else ("undefined" if response is None or uncertainty is None else "method_inconsistent" if consistency is not None and consistency>.2 else "below_numerical_envelope_or_zero")
                        cells[key] = dict(gradient=method_data["rkf78-half"].get(key,{}).get("gradient"),
                            normalized_gradient=method_data["rkf78-half"].get(key,{}).get("normalized_gradient"),response=response,
                            uncertainty=uncertainty,resolved=resolved,reason=reason,method_consistency=consistency,size_consistency=None,
                            asymmetry_relative=asymmetry,parameter_unit=unit,gradient_unit=f"{key}/{unit}")
                    records.append(dict(parameter=parameter,delta=delta,horizon_s=horizon,method_slopes={k:{q:v.get("gradient") for q,v in x.items()} for k,x in method_data.items()},
                        observables=cells,response_series=selected_series(responses["rkf78-half"][0]),realised_input=realised,
                        method_details=method_data,axis=("body-z" if parameter=="body_rate" and case_id=="CASE-B" else "body-x" if parameter=="body_rate" else "orbital-normal" if parameter=="attitude_orbit" else None)))
                print(case_id,parameter,delta,flush=True)
                entry["sensitivity"]=records;save(a.output,report)
        # Neighbor-size check is mandatory; no promotion solely on method agreement.
        for parameter,_,_,_,_,_ in PARAMETERS:
            for horizon in a.sensitivity_horizons:
                group=sorted((r for r in records if r["parameter"]==parameter and r["horizon_s"]==horizon),key=lambda r:r["delta"])
                for i,row in enumerate(group):
                    neighbor=group[1 if i==0 else i-1]
                    for key,cell in row["observables"].items():
                        sizes=relative_difference(cell["gradient"],neighbor["observables"][key]["gradient"])
                        if key in STATE_KEYS and key!="spin_axis_angle_rad":
                            v=row["method_details"]["rkf78-half"][key]["gradient_vector"];w=neighbor["method_details"]["rkf78-half"][key]["gradient_vector"]
                            sizes=math.hypot(*(x-y for x,y in zip(v,w)))/max(math.hypot(*v),math.hypot(*w),1e-300)
                        cell["size_consistency"]=sizes
                        if sizes is None or sizes>.2:
                            cell["resolved"]=False;cell["reason"]="perturbation_sizes_inconsistent_or_undefined"
        entry["sensitivity"]=records;save(a.output,report)
    print("Saved",a.output)


if __name__=="__main__": main()
