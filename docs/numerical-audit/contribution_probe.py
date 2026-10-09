#!/usr/bin/env python3
"""Finite source-isolation changes, not local physical parameter derivatives."""
import argparse
import hashlib
from pathlib import Path
from experiment import HORIZONS, SETTINGS, STATE_KEYS, SCALARS, run, prefix, spread, scalars, save
from metrics import compare_observables

METHODS=("dopri-tight","rkf78-half","rk4-half","rk4-quarter")
VARIANTS={"no_srp_force_or_torque":("--disable-srp",1),
          "no_srp_torque":("--disable-srp-torque",1),
          "no_gravity_gradient":("--disable-gg",1)}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ("binary","spk","lsk","output"):p.add_argument("--"+key,type=Path,required=True)
    p.add_argument("--cases",type=Path,nargs="+",required=True)
    a=p.parse_args()
    if a.output.exists():p.error("fresh output required")
    report=dict(protocol=__doc__,binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),cases={})
    for case in a.cases:
        identifier=next(line.split()[1] for line in case.read_text().splitlines() if line.startswith("id "))
        baselines={k:run(a,case,max(HORIZONS),SETTINGS[k]) for k in METHODS}
        records=[]
        for variant,options in VARIANTS.items():
            changed={k:run(a,case,max(HORIZONS),SETTINGS[k],options) for k in METHODS}
            for horizon in HORIZONS:
                base={k:prefix(v,horizon) for k,v in baselines.items()}
                altered={k:prefix(v,horizon) for k,v in changed.items()}
                envelopes=[spread(x) for x in (base,altered)]
                b,c=base["rkf78-half"],altered["rkf78-half"]
                difference=compare_observables(b,c)
                bs,cs=scalars(b)[0],scalars(c)[0]
                cells={}
                for key in (*STATE_KEYS,*SCALARS):
                    response=difference["maximum"].get(key) if key in STATE_KEYS else None if bs[key] is None or cs[key] is None else abs(cs[key]-bs[key])
                    estimates=[e.get(key) for e in envelopes]
                    uncertainty=2*max(estimates) if all(v is not None for v in estimates) else None
                    cells[key]=dict(response=response,uncertainty=uncertainty,
                                    resolved=response is not None and uncertainty is not None and response>0 and response>=10*uncertainty)
                records.append(dict(variant=variant,horizon_s=horizon,observables=cells,
                                    final=difference["final"],models=altered["rkf78-half"]["physical"]["models"]))
            print(identifier,variant,flush=True)
        report["cases"][identifier]=records;save(a.output,report)


if __name__=="__main__":main()
