#!/usr/bin/env python3
"""Matched-variant CASE-C continuation, not a certified chaos measurement.

Each seeded run retains ALL options of its own unperturbed physical variant.
Step refinements and seed growth use actual SO(3) distances; translational
forces remain on when individual attitude torques are disabled.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'experiments'))
from metrics import summarize, compare_observables

VARIANTS = {
    'baseline': ('full',()),
    'no_drag_torque': ('full',('--disable-drag-torque','1')),
    'no_radiation_torques': ('full',('--disable-srp-torque','1','--disable-albedo-torque','1')),
    'no_gg': ('full',('--disable-gg','1')),
    'no_environment_torques': ('full_no_torque',()),
    'no_eclipse': ('full_no_eclipse',()),
    'cp_zero': ('full',('--cp-y','-.1')),
    'cp_half': ('full',('--cp-y','-.05')),
    'cp_twice': ('full',('--cp-y','.1')),
    'cp_reversed': ('full',('--cp-y','-.2')),
    'inertia_x10': ('full',('--inertia-factor','10')),
    'spin_x10': ('full',('--spin-factor','10')),
    'inertia_ratio_z_x.9': ('full',('--inertia-z-factor','.9')),
    'inertia_ratio_z_x1.1': ('full',('--inertia-z-factor','1.1')),
    'density_x.1': ('full',('--density-factor','.1')),
    'all_area_x.1': ('full',('--area-factor','.1')),
}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('binary','spk','lsk','case','output'): p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--horizon',type=int,default=21600)
    a=p.parse_args()
    if a.output.exists():p.error('fresh output required')
    def run(scenario,options,step,seed=0):
        cmd=[str(a.binary),'--spk',str(a.spk),'--lsk',str(a.lsk),'--scenario',scenario,
             '--case-file',str(a.case),'--duration',str(a.horizon),'--method','rk4','--max-step',str(step)]
        cmd+=list(options)+['--attitude-orbit',str(seed)]
        return json.loads(subprocess.run(cmd,capture_output=True,text=True,check=True,timeout=3600).stdout)
    rows=[]
    for name,(scenario,options) in VARIANTS.items():
        baselines={step:run(scenario,options,step) for step in (1,.5,.25)}
        seed_rows=[]
        for seed in (1e-10,1e-8,1e-6):
            responses={}
            for step in (.5,.25):
                perturbed=run(scenario,options,step,seed)
                difference=compare_observables(baselines[step],perturbed)
                responses[str(step)]=dict(final=difference['final'],maximum=difference['maximum'],gain=difference['final']['attitude_angle_rad']/seed)
            seed_rows.append(dict(seed_rad=seed,grids=responses))
        row=dict(variant=name,scenario=scenario,options=list(options),horizon_s=a.horizon,seed_rows=seed_rows,
                 refinements=[dict(coarse=x,fine=y,differences=compare_observables(baselines[x],baselines[y])) for x,y in ((1,.5),(.5,.25))],
                 summary={str(step):summarize(raw) for step,raw in baselines.items()})
        rows.append(row)
        print(name,'quarter-refinement',row['refinements'][1]['differences']['final']['attitude_angle_rad'],flush=True)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(dict(protocol=__doc__,binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),variants=rows),allow_nan=False,separators=(',',':'))+'\n')


if __name__=='__main__':main()
