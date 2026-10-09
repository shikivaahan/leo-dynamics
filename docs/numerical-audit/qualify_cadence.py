#!/usr/bin/env python3
"""Check actual perturbed 15 s cadence against stored 30 s measurements.

Same RK4 0.25 s RHS schedule; no state/force interpolation. Sampled scalar
responses require their OWN cadence envelope, not only the nominal envelope.
"""
import argparse
import copy
import json
from pathlib import Path
from experiment import PARAMETERS, SCALARS, run, prefix, scalars, save, relative_difference
from metrics import scalar_derivative

SAMPLED=("pointing_sun_rms_rad","mean_projected_area_m2","effective_projected_area_m2",
         "srp_work_j","drag_work_j","non_grav_work_j","srp_impulse_n_s","drag_impulse_n_s","nutation_rad","precession_rad_s")
LABEL="rk4-quarter-cadence15"


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ("binary","spk","lsk","input","case","output"):p.add_argument("--"+key,type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error("fresh output required")
    report=json.loads(a.input.read_text());identifier=next(line.split()[1] for line in a.case.read_text().splitlines() if line.startswith("id "))
    case=report["cases"][identifier];horizons=sorted({r["horizon_s"] for r in case["sensitivity"]})
    report["cases"]={identifier:case}
    settings=dict(method="rk4",max_step=.25,cadence=15)
    nominal=run(a,a.case,max(horizons),settings)
    bases={h:scalars(prefix(nominal,h))[0] for h in horizons}
    for parameter,flag,deltas,multiplicative,unit,p0 in PARAMETERS:
        if parameter=="body_rate" and identifier=="CASE-B":flag="--omega-z"
        for delta in deltas:
            plus=run(a,a.case,max(horizons),settings,(flag,1+delta if multiplicative else delta))
            minus=run(a,a.case,max(horizons),settings,(flag,1-delta if multiplicative else -delta))
            for horizon in horizons:
                row=next(r for r in case["sensitivity"] if r["parameter"]==parameter and r["delta"]==delta and r["horizon_s"]==horizon)
                ps,ms=scalars(prefix(plus,horizon))[0],scalars(prefix(minus,horizon))[0]
                details={};slopes={}
                for key in SAMPLED:
                    old=row["method_details"]["rk4-quarter"][key];cell=row["observables"][key];b=bases[horizon][key]
                    d=scalar_derivative(ps[key],ms[key],delta,b,old.get("parameter_value"));details[key]=d;slopes[key]=d["gradient"]
                    paired=((old["base"],b),(old["plus"],ps[key]),(old["minus"],ms[key]))
                    defined=all(x is not None and y is not None for x,y in paired)
                    cadence_u=2*max(abs(x-y) for x,y in paired) if defined else None
                    u=max(cell["uncertainty"],cadence_u) if cell["uncertainty"] is not None and cadence_u is not None else None
                    response=max(abs(ps[key]-b),abs(ms[key]-b)) if defined else None
                    consistency=relative_difference(cell["gradient"],d["gradient"])
                    if cell["method_consistency"] is not None and consistency is not None:consistency=max(consistency,cell["method_consistency"])
                    cell.update(gradient=d["gradient"],normalized_gradient=d["normalized_gradient"],response=response,uncertainty=u,
                                asymmetry_relative=d["asymmetry_relative"],reference_method=LABEL,
                                perturbed_cadence_envelope=cadence_u,method_consistency=consistency)
                    cell["resolved"]=response is not None and u is not None and u>0 and response>0 and response>=10*u and consistency is not None and consistency<=.2
                    cell["reason"]=None if cell["resolved"] else "perturbed_cadence_or_method_not_resolved"
                row["method_details"][LABEL]=details;row["method_slopes"][LABEL]=slopes
            print(identifier,parameter,delta,"cadence qualified",flush=True)
        save(a.output,report)
    for parameter,*_ in PARAMETERS:
        for horizon in horizons:
            group=sorted((r for r in case["sensitivity"] if r["parameter"]==parameter and r["horizon_s"]==horizon),key=lambda r:r["delta"])
            for i,row in enumerate(group):
                neighbor=group[1 if i==0 else i-1]
                for key in SAMPLED:
                    cell=row["observables"][key];size=relative_difference(cell["gradient"],neighbor["observables"][key]["gradient"])
                    cell["size_consistency"]=size
                    if size is None or size>.2:cell["resolved"]=False;cell["reason"]="perturbed_cadence_neighbor_sizes_inconsistent"
    report["perturbed_cadence_protocol"]=__doc__
    save(a.output,report)


if __name__=="__main__":main()
