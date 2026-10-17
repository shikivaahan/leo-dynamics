#!/usr/bin/env python3
"""Explicit golden generation. Normal tests only consume, never regenerate, data."""
import argparse
import hashlib
import json
import platform
import subprocess
from pathlib import Path
import numpy as np
import scipy
from physics import Model, unit, rotation_error, RE, AU
from dop853_reference import initial_state, propagate_dop853
from conservation import invariants, kepler_states

LADDER = [dict(rtol=1e-10,max_step=30.,atol_scale=10.),
          dict(rtol=1e-12,max_step=10.,atol_scale=.1),
          dict(rtol=1e-13,max_step=2.5,atol_scale=.01)]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def difference(a,b):
    return dict(position_m=float(np.linalg.norm(a[:,:3]-b[:,:3],axis=1).max()),
                velocity_m_s=float(np.linalg.norm(a[:,3:6]-b[:,3:6],axis=1).max()),
                attitude_rad=float(max(rotation_error(x,y) for x,y in zip(a[:,6:10],b[:,6:10]))),
                angular_rate_rad_s=float(np.linalg.norm(a[:,10:13]-b[:,10:13],axis=1).max()))


def plain(value):
    if isinstance(value,np.ndarray): return value.tolist()
    if isinstance(value,np.generic): return value.item()
    raise TypeError(type(value).__name__)


def pointwise(document, result):
    states=result['states']
    times=result['time_s']
    points=[(float(times[i]),states[i].copy()) for i in sorted(set([0,len(times)//4,len(times)//2,3*len(times)//4,len(times)-1]))]
    off_axis=states[0].copy()
    off_axis[:3]=[RE+400e3,1.2e6,2.1e6]
    off_axis[3:6]=[-1200.,6800.,2200.]
    off_axis[6:10]=unit([.83,.2,-.1,.4])
    points.append((0.,off_axis))
    eclipse=document['models'].get('eclipse_model','none')
    if eclipse!='none':
        sun=np.array(document['environment']['sun_position_m'])
        direction=sun/np.linalg.norm(sun)
        perpendicular=np.cross(direction,[0.,0.,1.]); perpendicular/=np.linalg.norm(perpendicular)
        radius=RE+400e3
        beta=np.arcsin(RE/radius)
        alpha=np.arcsin(696340000./AU)
        offsets=[-2*alpha,-alpha/2,0.,alpha/2,2*alpha] if eclipse=='conical' else [-1e-7,1e-7]
        for offset in offsets:
            y=states[0].copy()
            y[:3]=radius*(-direction*np.cos(beta+offset)+perpendicular*np.sin(beta+offset))
            points.append((0.,y))
    model=Model(document)
    rows=[]
    for t,y in points:
        rows.append(dict(t=t,state=y.tolist(),rhs=model.rhs(t,y).tolist(),
                         **model.contributions(t,y)))
    return dict(name=document['name'],rows=rows)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    root=Path(__file__).resolve().parents[1]
    parser.add_argument('--scenarios',type=Path,default=root/'scenarios')
    parser.add_argument('--out',type=Path,default=root/'golden')
    parser.add_argument('--only',help='comma-separated explicit subset')
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    names=args.only.split(',') if args.only else sorted(p.stem for p in args.scenarios.glob('*.json'))
    previous=json.loads((args.out/'manifest.json').read_text()) if args.only and (args.out/'manifest.json').exists() else None
    records=[]
    for name in names:
        path=args.scenarios/(name+'.json')
        document=json.loads(path.read_text())
        print('generating',name,flush=True)
        runs=[propagate_dop853(document,**rung) for rung in LADDER]
        changes=[difference(a['states'],b['states']) for a,b in zip(runs[:-1],runs[1:])]
        result=runs[-1]
        checks=invariants(document,result['time_s'],result['states'])
        # Only analytically justified invariants are asserted. GG is not
        # dissipative, body-frame angular momentum components need not be constant,
        # and J2 conserves axial momentum, not the full momentum vector.
        for key in ('specific_energy_relative_drift','specific_momentum_relative_drift',
                    'inertial_momentum_relative_drift','kinetic_energy_relative_drift',
                    'j2_energy_relative_drift','axial_momentum_relative_drift'):
            if key in checks and checks[key]>1e-8:
                raise RuntimeError(f'{name}: independent invariant failed: {key}={checks[key]}')
        if checks.get('kepler_position_max_m',0)>1e-3:
            raise RuntimeError(f'{name}: Kepler reference disagreement')
        for key,limit in [('analytic_attitude_max_rad',1e-8),('analytic_rate_max_rad_s',1e-10)]:
            if checks.get(key,0)>limit:
                raise RuntimeError(f'{name}: analytical rigid-body check failed: {key}')
        golden=dict(name=name,time_s=result['time_s'],states=result['states'],
                    units=dict(time_s='s',states=['m']*3+['m/s']*3+['dimensionless']*4+['rad/s']*3),
                    quaternion_convention='Hamilton wxyz active body-to-J2000; q and -q equivalent',
                    scenario_sha256=sha(path),physics_checks=checks,
                    reference_convergence=changes[-1],reference_step_changes=changes,
                    reference_ladder=[{k:v for k,v in run.items() if k not in ('states','time_s')} for run in runs])
        if 'kepler' in document['checks']:
            r,v=kepler_states(result['states'][0],result['time_s'])
            golden['analytical_kepler']=dict(position_m=r,velocity_m_s=v)
        output=args.out/(name+'.json')
        output.write_text(json.dumps(golden,default=plain,separators=(',',':'),allow_nan=False)+'\n')
        points=args.out/(name+'.pointwise.json')
        points.write_text(json.dumps(pointwise(document,result),default=plain,separators=(',',':'),allow_nan=False)+'\n')
        records.append(dict(name=name,file=output.name,sha256=sha(output),pointwise=points.name,
                            pointwise_sha256=sha(points),scenario_sha256=sha(path),
                            reference_convergence=changes[-1]))
        print('  convergence',changes[-1],flush=True)
    source=Path(__file__).resolve().parent
    provenance=dict(python=platform.python_version(),numpy=np.__version__,scipy=scipy.__version__,
                    method='independent Python equations, SciPy DOP853; analytical Kepler/top/invariant checks',
                    sources={p.name:sha(p) for p in sorted(source.glob('*.py'))})
    for record in records:
        record['provenance']=provenance
    if previous is not None:
        replaced=set(names)
        retained=[]
        for record in previous['fixtures']:
            if record['name'] not in replaced:
                record.setdefault('provenance',previous['provenance'])
                retained.append(record)
        records=retained+records
    records.sort(key=lambda record:record['name'])
    (args.out/'manifest.json').write_text(json.dumps(dict(kind='v0.1-independent-reference',
        provenance=provenance,fixtures=records),indent=2,allow_nan=False)+'\n')


if __name__=='__main__':
    main()
