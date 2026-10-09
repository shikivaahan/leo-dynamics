#!/usr/bin/env python3
"""Common-time RTN errors and physical state-gradient projections; no interpolation."""
import argparse
import gzip
import json
from pathlib import Path
from experiment import HORIZONS, prefix, save
from metrics import summarize, compare_observables, rtn_basis, rtn_components

LABELS=("default","dopri-tight","rkf78-2","rkf78-1","rk4-1","rk4-half","rk4-quarter","mixed-quarter")


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
            base=prefix(nominal,horizon);summary=summarize(base);orbital=summary["orbital"]
            errors={label:compare_observables(base,prefix(raw,horizon))["orbital_rtn"] for label,raw in runs.items()}
            energy=orbital["specific_energy_j_kg"];initial=energy["final"]-orbital["specific_energy_change_j_kg"]
            rows.append(dict(horizon_s=horizon,orbital=orbital,mean_specific_energy_change_j_kg=energy["mean"]-initial,
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
