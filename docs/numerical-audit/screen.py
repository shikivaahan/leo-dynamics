#!/usr/bin/env python3
"""Fixed-seed stratified screening, gated on resolved local physical responses.

Ranks parameters ONLY by response in one common observable at explicitly
exploratory tested spans. These spans are not hardware uncertainty priors.
This is not a variance decomposition, probability estimate or chaos test.
"""
import argparse
import hashlib
import gzip
import json
import random
from pathlib import Path
from experiment import PARAMETERS, SETTINGS, run, scalars, save

SEED=20261009
SAMPLES=12
METHODS=("dopri-tight","rkf78-half","rk4-half","rk4-quarter")


def candidates(case, horizon, observable):
    selected={}
    for row in case["sensitivity"]:
        if row["horizon_s"]!=horizon: continue
        cell=row["observables"].get(observable,{})
        if not cell.get("resolved"): continue
        parameter=row["parameter"]
        if parameter not in selected or row["delta"]>selected[parameter]["delta"]:
            selected[parameter]=dict(parameter=parameter,delta=row["delta"],**cell)
    return sorted(selected.values(),key=lambda cell:(-cell["response"],cell["parameter"]))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ("results","binary","spk","lsk","case","output"):
        p.add_argument("--"+key,type=Path,required=True)
    p.add_argument("--horizon",type=int,default=21600)
    p.add_argument("--observable",default="sma_final_m")
    a=p.parse_args()
    if a.output.exists(): p.error("fresh output required")
    source=json.loads(gzip.decompress(a.results.read_bytes()) if a.results.suffix==".gz" else a.results.read_text())
    identifier=next(line.split()[1] for line in a.case.read_text().splitlines() if line.startswith("id "))
    case=source["cases"][identifier]
    eligible=candidates(case,a.horizon,a.observable)
    chosen=eligible[:3]
    all_parameters=[row[0] for row in PARAMETERS]
    excluded=[name for name in all_parameters if name not in {r["parameter"] for r in eligible}]
    report=dict(protocol=__doc__,seed=SEED,case=identifier,horizon_s=a.horizon,
                observable=a.observable,screened_parameters=chosen,
                unresolved_parameters_excluded=excluded,rows=[],
                source_sha256=hashlib.sha256(a.results.read_bytes()).hexdigest(),
                binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),
                status="gated_off" if len(chosen)<2 else "measured")
    if len(chosen)<2:
        report["reason"]="Fewer than two parameters have trustworthy local responses for this common observable and horizon."
        save(a.output,report);print(identifier,report["reason"]);return
    definitions={r[0]:r for r in PARAMETERS}
    baselines={label:run(a,a.case,a.horizon,SETTINGS[label]) for label in METHODS}
    nominal={label:scalars(raw)[0][a.observable] for label,raw in baselines.items()}
    rng=random.Random(SEED)
    columns={}
    for cell in chosen:
        values=[-1+2*(i+rng.random())/SAMPLES for i in range(SAMPLES)]
        rng.shuffle(values);columns[cell["parameter"]]=values
    for trial in range(SAMPLES):
        offsets={cell["parameter"]:cell["delta"]*columns[cell["parameter"]][trial] for cell in chosen}
        options=[]
        for name,delta in offsets.items():
            _,flag,_,multiplicative,_,_=definitions[name]
            if name=="body_rate" and identifier=="CASE-B":flag="--omega-z"
            options.extend((flag,1+delta if multiplicative else delta))
        runs={label:run(a,a.case,a.horizon,SETTINGS[label],options) for label in METHODS}
        values={label:scalars(raw)[0][a.observable] for label,raw in runs.items()}
        finite=all(v is not None for v in (*values.values(),*nominal.values()))
        uncertainty=max(2*(max(values.values())-min(values.values())),2*(max(nominal.values())-min(nominal.values())),
                        *(cell["uncertainty"] for cell in chosen)) if finite else None
        response=values["rkf78-half"]-nominal["rkf78-half"] if finite else None
        linear=sum(cell["gradient"]*offsets[cell["parameter"]] for cell in chosen)
        row=dict(trial=trial,offsets=offsets,observable_by_method=values,response=response,
                 numerical_envelope=uncertainty,resolved=finite and abs(response)>=10*uncertainty and abs(response)>0,
                 linear_prediction=linear,nonlinear_residual=None if response is None else response-linear,
                 refinement_difference=None if not finite else abs(values["rk4-half"]-values["rk4-quarter"]))
        report["rows"].append(row)
        print(identifier,"screen",trial,"response",response,"resolved",row["resolved"],flush=True)
    report["nominal_by_method"]=nominal
    save(a.output,report)


if __name__=="__main__": main()
