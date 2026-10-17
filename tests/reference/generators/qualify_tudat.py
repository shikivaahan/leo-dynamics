#!/usr/bin/env python3
"""Explicit native-Tudat validation against frozen independently converged data."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
from tudat_reference import propagate, runtime_version

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from test_reference_regression import errors

MATCHED=['two_body_circular','two_body_eccentric','j2_equatorial','j2_inclined',
         'torque_free_spherical','torque_free_symmetric','torque_free_elongated','gravity_gradient']


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args(); args.out.mkdir(parents=True,exist_ok=True)
    limits=json.loads((ROOT/'acceptance.json').read_text())
    rows=[]
    for name in MATCHED:
        source=ROOT/'scenarios'/(name+'.json')
        scenario=json.loads(source.read_text())
        expected=json.loads((ROOT/'golden'/(name+'.json')).read_text())
        coarse=propagate(scenario,5.)
        fine=propagate(scenario,2.5)
        reference=np.array(expected['states'])
        delta=errors(coarse['states'],fine['states'])
        disagreement=errors(fine['states'],reference)
        thresholds=limits['classes'][limits['scenarios'][name]]
        passed=all(delta[k]<=.05*thresholds[k] and disagreement[k]<=thresholds[k] for k in disagreement)
        output=args.out/(name+'.json')
        output.write_text(json.dumps(fine,default=lambda v:v.tolist(),separators=(',',':'),allow_nan=False)+'\n')
        rows.append(dict(name=name,passed=passed,reference_refinement=delta,vs_dop853=disagreement,
                         file=output.name,sha256=sha(output),scenario_sha256=sha(source)))
        print(name,'PASS' if passed else 'FAIL',disagreement,flush=True)
    manifest=dict(kind='v0.1-native-tudat-qualification',runtime_module_version=runtime_version,
                  integrator='native RKDP 8(7) higher order; 5/2.5 s fixed-step convergence; exact requested samples',
                  frame='Earth-centred J2000; inertially fixed Earth pole, explicit normalized C20',
                  fixtures=rows,unmatched=['panel optical SRP','cold-beam Sentman/flat-plate panel drag',
                                         'radial-beam albedo','full matched environmental coupling'])
    (args.out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    raise SystemExit(0 if all(r['passed'] for r in rows) else 1)


if __name__=='__main__': main()
