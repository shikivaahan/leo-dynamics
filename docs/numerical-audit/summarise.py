#!/usr/bin/env python3
"""Compact completed measurements and render qualified same-observable results."""
import argparse
import copy
import gzip
import hashlib
import json
import math
from pathlib import Path
from experiment import PARAMETERS, SCALARS, STATE_KEYS, value

J_UNITS={"sma_final_m":"m","eccentricity":"1","energy_change_j_kg":"J/kg","plane_rotation_rad":"rad",
         "pointing_sun_rms_rad":"rad","mean_projected_area_m2":"m²","effective_projected_area_m2":"m²",
         "srp_work_j":"J","drag_work_j":"J","non_grav_work_j":"J","srp_impulse_n_s":"N·s","drag_impulse_n_s":"N·s",
         "omega_final_rad_s":"rad/s","momentum_final_kg_m2_s":"kg·m²/s","nutation_rad":"rad","precession_rad_s":"rad/s",
         "position_m":"m","velocity_m_s":"m/s","attitude_angle_rad":"rad","omega_rad_s":"rad/s","spin_axis_angle_rad":"rad"}


def load(path):
    return json.loads(gzip.decompress(path.read_bytes()) if path.suffix==".gz" else path.read_text())


def physical_origin(parameter,physical,details):
    if parameter=="mass":return physical["mass_kg"],"kg"
    if parameter=="area":return (9. if physical["case_id"]=="CASE-B" else physical["total_area_m2"]),"m²"
    if parameter=="inertia":return sum(physical["inertia_diagonal_kg_m2"]),"kg·m² (trace, fixed ratios)"
    if parameter=="spin":return physical["initial_state"]["omega_norm_rad_s"],"rad/s (fixed direction)"
    if parameter=="drag_coefficient":return physical["surfaces"][0]["cd"],"1"
    initial=physical["initial_state"]
    if parameter=="position_radial":return initial["initial_radius_m"],"m"
    if parameter=="velocity_along":return initial["initial_speed_m_s"],"m/s"
    if parameter=="body_rate":return initial["omega_body_rad_s"][2 if physical["case_id"]=="CASE-B" else 0],"rad/s"
    if parameter=="cp_y":return sum(s["cp_body"][1] for s in physical["surfaces"])/len(physical["surfaces"]),"m"
    if parameter in ("specular","diffuse","accommodation"):return physical["surfaces"][0][parameter],"1"
    if parameter=="density":return physical["density_scale_applied"],"1"
    return details.get("parameter_value"),next(r[4] for r in PARAMETERS if r[0]==parameter)


def compact(doc):
    report=copy.deepcopy(doc)
    for case in report["cases"].values():
        physical=case["convergence"][0]["physical"]
        for row in case["sensitivity"]:
            detail=row.pop("method_details")
            row["gradient_vectors"]={label:{key:d["gradient_vector"] for key,d in cells.items() if "gradient_vector" in d} for label,cells in detail.items()}
            row["one_sided_gradients"]={key:{side:detail[cell.get("reference_method","rkf78-half")].get(key,{}).get(side) for side in ("forward_gradient","backward_gradient")} for key,cell in row["observables"].items() if key in SCALARS}
            for key,cell in row["observables"].items():
                d=detail[cell.get("reference_method","rkf78-half")].get(key,{})
                origin,unit=physical_origin(row["parameter"],physical,d)
                multiplicative=next(p[3] for p in PARAMETERS if p[0]==row["parameter"])
                denominator=origin if multiplicative else 1
                cell["physical_parameter_origin"]=origin;cell["physical_parameter_unit"]=unit
                cell["gradient_physical"]=cell["gradient"]/denominator if cell["resolved"] and denominator and cell["gradient"] is not None else None
                cell["gradient_unit"]=J_UNITS[key]+" / "+cell["parameter_unit"]
                cell["gradient_physical_unit"]=J_UNITS[key]+" / "+unit
                baseline=d.get("base");u=cell["uncertainty"]
                good=cell["resolved"] and origin and baseline is not None and u is not None and abs(baseline)>=10*u and baseline!=0
                cell["normalized_gradient"]=origin/baseline*cell["gradient_physical"] if good and cell["gradient_physical"] is not None else None
                cell["normalized_reason"]=None if cell["normalized_gradient"] is not None else "unresolved_or_zero_origin_or_baseline_not_separated_from_zero_or_vector_observable"
        for row in case["convergence"]:
            summaries=row["observables"]
            row["nominal_scalars"]={label:{key:value(s,path) for key,path in SCALARS.items()} for label,s in summaries.items()}
            row["observables"]={"rkf78-half":summaries["rkf78-half"]}
    report["compact_policy"]="Full state trajectories remain temporary. All methods' scalar values/slopes, local state-gradient vectors, signed one-sided scalar gradients, response histories and same-unit uncertainty are retained. Physical gradients are published only for resolved cells; elasticities require a nonzero physical origin and baseline >=10U away from zero."
    return report


def n(x):return "undefined" if x is None else f"{x:.5g}"


def render(report):
    out=["# Physical sensitivity results — v1.1","",
         "Measured after [EXPERIMENT_DESIGN.md](EXPERIMENT_DESIGN.md) and the documented",
         "[analysis correction amendment](experiments/ANALYSIS_AMENDMENT-v1.1.md).",
         "Evidence: `evidence/sensitivity-results.json.gz`; regenerate with",
         "`run_experiments.sh` (a fresh output directory is required).","",
         "## Interpretation contract","",
         "A resolved response is at least 10 times its same-unit empirical numerical",
         "envelope and has at most 20% cross-method and neighboring-size derivative",
         "disagreement. The envelope includes nominal AND both perturbed trajectories,",
         "independent adaptive DP5/RKF78, identical-schedule RK4 refinement, and nominal",
         "mixed-state exposure. Sampled scalar derivatives additionally pass actual",
         "15 s cadence checks of BOTH perturbed trajectories on the identical RK4 grid.",
         "This is an empirical floor, not a certified bound.",
         "No finite difference from an unresolved cell is interpreted as a physical derivative.","",
         "Dimensional derivatives for uniform mass, area, inertia and spin scaling are",
         "converted to kg, physical surface area (CASE-B membrane 9 m²), inertia trace",
         "at fixed ratios, and initial rate magnitude at fixed direction. They are not",
         "independent derivatives of individual tensor entries or individual panels.",
         "Elasticities are undefined for a zero/arbitrary parameter origin, vector-valued",
         "state observable, or a baseline not separated from zero by 10U. Exploratory",
         "spans are not mission uncertainty distributions; OFAT inputs are independent",
         "model changes, not complete redesigns obeying hardware parameter correlations.","",
         "## Numerical envelopes and prediction horizons","",
         "State envelopes are maximum common-time discrepancies over the horizon; scalar",
         "envelopes are spreads of the specified terminal or integrated observable.",
         "Cadence is included for sampled averages/work, not inferred from an interpolant.","",
         "| Case | Horizon (s) | Position U (m) | SO(3) U (rad) | Rate U (rad/s) | SMA U (m) | Mean-area U (m²) | SRP-work U (J) |",
         "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for name,case in report["cases"].items():
        for row in case["convergence"]:
            out.append("| "+" | ".join([name,str(row["horizon_s"]),*[n(row["uncertainty"][key]) for key in ("position_m","attitude_angle_rad","omega_rad_s","sma_final_m","mean_projected_area_m2","srp_work_j")]])+" |")
    out += ["","For the predeclared **conditional** 0.1 m orbit and 0.01° orientation",
            "integration allowances, CASE-A and CASE-B support six hours; CASE-C supports",
            "three hours for orbit and one hour for instantaneous orientation among the",
            "tested horizons. These are not flight requirements or certified global bounds.",
            "CASE-C's formal two-times SO(3) spread may exceed π; it is an uncertainty",
            "margin, not an orientation distance (all actual distances are ≤π).",
            "CASE-B's nominal six-hour orientation is resolved at coarse planning accuracy",
            "but several perturbed six-hour trajectories/derivatives are not.","",
            "## Resolved finite differences","",
            "Each table separates observables and horizons. The largest resolved tested",
            "span is shown for each parameter, with all resolved sizes listed. Response R",
            "and envelope U share the stated observable units. Signed scalar derivatives",
            "and local relative-rotation-vector norms are distinguished; vector state",
            "derivatives are not derivatives of a wrapped angle component."]
    for name,case in report["cases"].items():
        for horizon in (3600,21600):
            rows=[r for r in case["sensitivity"] if r["horizon_s"]==horizon]
            count=sum(c["resolved"] for r in rows for c in r["observables"].values())
            out += ["",f"### {name}, {horizon} s — {count}/{len(rows)*len(rows[0]['observables'])} resolved cells"]
            for key in ("sma_final_m","mean_projected_area_m2","attitude_angle_rad"):
                out += ["",f"**{key} [{J_UNITS[key]}]**","",
                        "| Parameter | Resolved tested magnitudes | R | U | Dimensional derivative | S* | Method / size disagreement | Asymmetry |",
                        "|---|---|---:|---:|---|---:|---|---:|"]
                for parameter,*_ in PARAMETERS:
                    group=[r for r in rows if r["parameter"]==parameter and r["observables"][key]["resolved"]]
                    if not group:continue
                    selected=max(group,key=lambda r:r["delta"]);c=selected["observables"][key]
                    out.append("| "+" | ".join([parameter,", ".join(n(r["delta"]) for r in group),n(c["response"]),n(c["uncertainty"]),n(c["gradient_physical"])+" "+c["gradient_physical_unit"],n(c["normalized_gradient"]),n(c["method_consistency"])+" / "+n(c["size_consistency"]),n(c["asymmetry_relative"])])+" |")
                if not any(r["observables"][key]["resolved"] for r in rows):out += ["","No resolved derivative for this observable and horizon."]
    out += ["","## Linearity, asymmetry and time dependence","",
            "Machine-readable evidence retains all three signed magnitudes, every scheme's",
            "slope, neighboring-size disagreement, both one-sided scalar slopes, asymmetry",
            "and time-resolved physical responses. `response_series / delta` gives a",
            "one-sided physical response-norm estimate as a function of time, not a signed",
            "tangent vector or a certified exponential growth rate. Maximum/RMS error",
            "statistics are sampled at common physical times; unsampled extrema are not bounded.",
            "For state vectors, method and size consistency compare vector directions as",
            "well as norms. Scalar one-sided asymmetry is reported; state-only asymmetry",
            "is not independently available from the retained terminal central vectors.",
            "The distinction matters: numerical resolution does not imply linearity over",
            "an exploratory span. A large asymmetric response remains a finite-pair result.","",
            "`Cd` scaling is structurally inactive in the selected Sentman force model.",
            "CASE-B starts with zero spin: multiplying that spin creates no perturbation",
            "and no spin-rate-direction derivative; additive body-z rate perturbations are",
            "measured separately. Undefined initial spin direction is never assigned a",
            "manufactured body principal axis.","",
            "## Global screening and model contributions","",
            "`screen-case-{a,b,c}-v11.json` records seed 20261009, 12 stratified controlled",
            "samples and independent-method/refined-grid checks. Rankings use one common",
            "observable (terminal point-mass SMA), the largest locally resolved tested",
            "span, and three parameters only. A/B use six hours; CASE-C uses its converged",
            "one-hour regime rather than its unresolved six-hour instantaneous dynamics.",
            "This is not a Sobol decomposition or a probability-based importance ranking.",
            "`force-contributions-v11.json` separates complete SRP removal, SRP torque-only",
            "removal (force retained), and gravity-gradient removal in all cases. These",
            "finite model-isolation changes are not infinitesimal parameter derivatives.","",
            "## CASE-C mechanism and unresolved six-hour state","",
            "Matched continuations show six-hour RK4 0.5→0.25 s final SO(3) disagreement",
            "1.4903 rad for the original case, 2.6054 rad when the common CP offset is",
            "doubled, 0.000290 rad with drag torque alone removed, and 5.90e−11 rad when",
            "the common CP offset is zero. Inertia-ratio continuation also changes the",
            "disagreement. These are controlled evidence of drag/geometry/conditioning",
            "involvement, not proof that a particular six-hour attitude trajectory is physical.",
            "The original baseline's seed gains depend strongly on grid; they are not",
            "certified Lyapunov exponents. Coarse six-hour SMA responses to ±10% density,",
            "area and mass pass the response/derivative gate even though instantaneous",
            "attitude and small sensitivities do not. Do not turn that coarse finite-span",
            "orbital result into centimetre ephemerides or a long-term decay prediction.","",
            "## Figures","",
            "Six measured figures and their source digest are in `evidence/figures/`: ",
            "timestep_vs_error, convergence_vs_horizon, attitude_sensitivity_vs_time,",
            "perturbation_vs_response, parameter_ranking, uncertainty_vs_response.",
            "Hollow/dashed/unresolved data are diagnostic differences, never resolved",
            "physical sensitivities; zero/undefined channels are explicitly omitted.",""]
    return "\n".join(out)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--results",type=Path,nargs="+",required=True)
    p.add_argument("--evidence",type=Path,required=True);p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    if a.evidence.exists():p.error("fresh compact evidence required")
    merged=dict(schema="leo-dynamics/sensitivity-v1.1",cases={},sources={},config_sha256={})
    for path in a.results:
        data=load(path)
        if merged["cases"] and merged["binary_sha256"]!=data["binary_sha256"]:raise ValueError("different measured binaries")
        merged["binary_sha256"]=data["binary_sha256"]
        merged["sources"][str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
        merged["config_sha256"].update(data["config_sha256"])
        for name,case in data["cases"].items():
            if name in merged["cases"]:raise ValueError("duplicate case")
            if len(case["sensitivity"])!=84 or any(c["size_consistency"] is None and c["resolved"] for r in case["sensitivity"] for c in r["observables"].values()):raise ValueError("incomplete sensitivity study")
            merged["cases"][name]=case
    merged["policy"]=data["policy"];merged["source_parent"]=data["source_parent"]
    report=compact(merged)
    text=json.dumps(report,allow_nan=False,separators=(",",":"))+"\n"
    a.evidence.parent.mkdir(parents=True,exist_ok=True)
    a.evidence.write_bytes(gzip.compress(text.encode(),mtime=0) if a.evidence.suffix==".gz" else text.encode())
    a.output.write_text(render(report));print("Wrote",a.evidence,a.output)


if __name__=="__main__":main()
