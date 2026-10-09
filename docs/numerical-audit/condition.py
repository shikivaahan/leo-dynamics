#!/usr/bin/env python3
"""Measure initial-attitude conditioning on identical fixed-time grids.

This is distinct from a Sun perturbation or two independently adaptive runs.
It is not a proof of chaos or an absolute trajectory accuracy certificate.
"""
import argparse
import json
from pathlib import Path
import subprocess
from run import compare, compact


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--spk',type=Path,required=True)
    p.add_argument('--lsk',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():
        p.error('output exists; preserve earlier evidence')
    rows=[]
    for step in (.5,.25):
        def run(seed):
            command=[str(a.binary.resolve()),'--spk',str(a.spk.resolve()),'--lsk',str(a.lsk.resolve()),
                     '--scenario','full','--method','rk4','--max-step',str(step),'--attitude-seed',str(seed)]
            return json.loads(subprocess.run(command,capture_output=True,text=True,check=True,timeout=900).stdout)
        zero=run(0)
        for seed in (1e-10,1e-8,1e-6):
            perturbed=run(seed);difference=compare(zero,perturbed,history=True)
            rows.append(dict(step_s=step,initial_rotation_rad=seed,comparison=difference,
                             final_gain=difference['final']['attitude_rad']/seed,
                             baseline=compact(zero),perturbed=compact(perturbed)))
            print(step,seed,difference['final']['attitude_rad'],flush=True)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(dict(note=__doc__,rows=rows),indent=2)+'\n')


if __name__=='__main__':
    main()
