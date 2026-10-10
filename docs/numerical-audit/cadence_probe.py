#!/usr/bin/env python3
"""Actual cadence refinements on identical binary-exact RK4 RHS schedules.

State differences must be exactly zero at common times. Only sampled integral
quadrature and time averages change; no state/force interpolation is introduced.
"""
import argparse
import hashlib
import math
from pathlib import Path
from experiment import HORIZONS, run, prefix, save
from energy import account as matched_energy_account

CADENCES=(30,15,7.5,3.75)


def integrate(times,values):return math.fsum((times[i+1]-times[i])*(values[i]+values[i+1])*.5 for i in range(len(times)-1))


def measurements(raw):
    t=[r[0] for r in raw["samples"]];duration=t[-1]-t[0]
    areas=[r["projected_area_m2"] for r in raw["loads"]]
    result=dict(mean_projected_area_m2=integrate(t,areas)/duration)
    for source in ("srp","drag","albedo"):
        forces=[r[15:18] for r in raw["samples"]] if source=="srp" else [r[source+"_force_n"] for r in raw["loads"]]
        power=[sum(f*v for f,v in zip(force,row[4:7])) for force,row in zip(forces,raw["samples"])]
        impulse=[integrate(t,[f[k] for f in forces]) for k in range(3)]
        result[source+"_work_j"]=integrate(t,power)
        result[source+"_absolute_work_scale_j"]=integrate(t,[abs(v) for v in power])
        result[source+"_impulse_n_s"]=math.hypot(*impulse)
    result.update({key:val for key,val in matched_energy_account(raw).items() if key!="j2_enabled"})
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ("binary","spk","lsk","output"):p.add_argument("--"+key,type=Path,required=True)
    p.add_argument("--cases",type=Path,nargs="+",required=True)
    a=p.parse_args()
    if a.output.exists():p.error("fresh output required")
    report=dict(protocol=__doc__,binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),cases={})
    report["energy_convention"]="K+Ucentral+UJ2 for fixed J2000 Z; compare delta specific energy with integral Fnon-grav dot v / mass. Translational balance only."
    for case in a.cases:
        name=next(line.split()[1] for line in case.read_text().splitlines() if line.startswith("id "))
        runs={cadence:run(a,case,max(HORIZONS),dict(method="rk4",max_step=.25,cadence=cadence)) for cadence in CADENCES}
        # Raw SRP work is an output-cadence integral, not part of the state.
        common={r[0]:r[1:14] for r in runs[30]["samples"]}
        equality=all(r[1:14]==common[r[0]] for cadence,raw in runs.items() for r in raw["samples"] if r[0] in common)
        if not equality:raise ValueError("cadence changed an RK4 state at common times")
        rows=[]
        for horizon in HORIZONS:
            values={str(c):measurements(prefix(raw,horizon)) for c,raw in runs.items()}
            errors={key:[abs(values[str(a)][key]-values[str(b)][key]) for a,b in zip(CADENCES,CADENCES[1:])] for key in values["30"]}
            rows.append(dict(horizon_s=horizon,observables=values,successive_cadence_differences=errors))
        controls={step:run(a,case,max(HORIZONS),dict(method="rk4",max_step=step),("--scenario","j2"))
                  for step in (1,.5,.25)}
        report["cases"][name]=dict(common_states_bitwise_equal=equality,rhs_calls={str(c):raw["rhs_calls"] for c,raw in runs.items()},rows=rows,
            gravity_only_controls=[dict(horizon_s=h,by_step={str(step):matched_energy_account(prefix(raw,h)) for step,raw in controls.items()})
                                   for h in HORIZONS])
        save(a.output,report);print(name,"identical RK4 states",equality,"final integral differences",rows[-1]["successive_cadence_differences"],flush=True)


if __name__=="__main__":main()
