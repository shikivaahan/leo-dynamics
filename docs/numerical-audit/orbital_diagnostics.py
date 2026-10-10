#!/usr/bin/env python3
"""Common-time RTN errors and physical state-gradient projections; no interpolation."""
import argparse
import gzip
import json
from pathlib import Path
from experiment import HORIZONS, prefix, save
from metrics import summarize, compare_observables, rtn_basis, rtn_components
from energy import account as matched_energy_account

LABELS=("default","dopri-tight","rkf78-2","rkf78-1","rk4-1","rk4-half","rk4-quarter","mixed-quarter")


def energy_account(raw, summary):
    """Point-mass diagnostics alongside the force-matched translational balance."""
    mass_kg=raw["physical"]["mass_kg"]
    non_grav=summary["non_gravitational"]
    specific={key+"_specific_work_j_kg":non_grav[key+"_work_j"]/mass_kg
              for key in ("srp","drag","albedo")}
    matched=matched_energy_account(raw)
    return dict(point_mass_terminal_change_j_kg=summary["orbital"]["specific_energy_change_j_kg"],
                point_mass_range_j_kg=summary["orbital"]["specific_energy_j_kg"]["max"]-summary["orbital"]["specific_energy_j_kg"]["min"],
                accumulated_specific_non_grav_work_j_kg=matched["non_grav_specific_work_j_kg"],
                accumulated_specific_non_grav_work_rate_w_kg=matched["mean_non_grav_specific_power_w_kg"],
                matched_gravity=matched,
                interpretation="Kepler energy omits the J2 potential. Matched K+U can be compared with source work without transforming orbital elements. Residual includes integration, output quadrature and arithmetic exposure, not a certified physical error.",
                **specific)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--workdirs",type=Path,nargs="+",required=True)
    p.add_argument("--evidence",type=Path,required=True);p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error("fresh output required")
    evidence=json.loads(gzip.decompress(a.evidence.read_bytes()) if a.evidence.suffix==".gz" else a.evidence.read_text())
    report=dict(protocol=__doc__,basis="nominal RKF78 0.5 s trajectory RTN; Kepler point-mass elements evaluated from J2-propagated states",cases={})
    for directory in a.workdirs:
        nominal=json.loads((directory/"rkf78-half.json").read_text());name=nominal["physical"]["case_id"]
        runs={label:json.loads((directory/(label+".json")).read_text()) for label in LABELS}
        rows=[]
        for horizon in HORIZONS:
            base=prefix(nominal,horizon);summary=summarize(base)
            errors={label:compare_observables(base,prefix(raw,horizon))["orbital_rtn"] for label,raw in runs.items()}
            energy_methods={label:matched_energy_account(prefix(raw,horizon)) for label,raw in runs.items()}
            energy_methods["rkf78-half"]=matched_energy_account(base)
            selected=[energy_methods[label]["matched_energy_change_j_kg"]
                      for label in ("dopri-tight","rkf78-1","rkf78-half","rk4-half","rk4-quarter","mixed-quarter")]
            rows.append(dict(horizon_s=horizon,orbital=summary["orbital"],
                             energy_account=energy_account(base,summary),
                             matched_energy_by_method=energy_methods,
                             matched_energy_fine_spread_j_kg=max(selected)-min(selected),
                             rtn_errors=errors))
        gradients=[]
        for row in evidence["cases"][name]["sensitivity"]:
            state=prefix(nominal,row["horizon_s"])["samples"][-1];basis=rtn_basis(state[1:4],state[4:7])
            projections={label:{key:list(rtn_components(basis,v)) for key,v in vectors.items() if key in ("position_m","velocity_m_s")} for label,vectors in row["gradient_vectors"].items()}
            gradients.append(dict(parameter=row["parameter"],delta=row["delta"],horizon_s=row["horizon_s"],
                                  raw_parameter_unit=row["observables"]["position_m"]["parameter_unit"],
                                  resolved_position=row["observables"]["position_m"]["resolved"],
                                  resolved_velocity=row["observables"]["velocity_m_s"]["resolved"],rtn_gradient_vectors=projections))
        report["cases"][name]=dict(convergence=rows,sensitivity=gradients)
        print(name,"terminal and maximum/RMS RTN errors and gradient projections recorded",flush=True)
    save(a.output,report)


if __name__=="__main__":main()
