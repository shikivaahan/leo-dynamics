#!/usr/bin/env python3
"""Independent-method refinement versus horizon for the fully coupled workload.

No acceptance thresholds/defaults are changed. A reference that fails its own
refinement is explicitly unsuitable as a correctness oracle at that horizon.
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
    def execute(duration,method,step):
        command=[str(a.binary.resolve()),'--spk',str(a.spk.resolve()),'--lsk',str(a.lsk.resolve()),
                 '--scenario','full','--method',method,'--duration',str(duration),
                 '--rtol','1e-14','--atol-scale','0.0001','--max-step',str(step)]
        return json.loads(subprocess.run(command,capture_output=True,text=True,check=True,timeout=900).stdout)
    rows=[]
    for duration in (1800,3600,7200,10800,21600):
        first=execute(duration,'rkf78',.5)
        refined=execute(duration,'rkf78',.25)
        production=execute(duration,'dopri',.25)
        row=dict(duration_s=duration,reference_refinement=compare(refined,first),
                 independent_method_difference=compare(refined,production),
                 rkf78_half=compact(first),rkf78_quarter=compact(refined),dopri_tight_quarter=compact(production))
        if duration==21600:
            eighth=execute(duration,'rkf78',.125)
            row['further_reference_refinement']=compare(eighth,refined)
            row['rkf78_eighth']=compact(eighth)
        rows.append(row)
        print(duration,row['reference_refinement']['final'],flush=True)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(dict(configuration='full original spacecraft/physics; rtol1e-14, original field atols x1e-4; exact30s output timestamps',
                                      note='diagnostic tolerances only, production defaults and original OPT001 limits untouched',rows=rows),indent=2)+'\n')


if __name__=='__main__':
    main()
