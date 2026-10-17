#!/usr/bin/env python3
"""Consume frozen independent goldens; never import SciPy/Tudat or regenerate expectations."""
import argparse
import copy
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parent


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def so3_angles(a,b):
    a=a/np.linalg.norm(a,axis=1)[:,None]
    b=b/np.linalg.norm(b,axis=1)[:,None]
    signs=np.where(np.sum(a*b,axis=1)<0,-1.,1.)
    chord=np.linalg.norm(a-signs[:,None]*b,axis=1)
    return 4*np.arcsin(np.clip(chord/2,0.,np.sqrt(.5)))


def errors(a,b):
    return dict(position_m=float(np.linalg.norm(a[:,:3]-b[:,:3],axis=1).max()),
                velocity_m_s=float(np.linalg.norm(a[:,3:6]-b[:,3:6],axis=1).max()),
                attitude_rad=float(so3_angles(a[:,6:10],b[:,6:10]).max()),
                angular_rate_rad_s=float(np.linalg.norm(a[:,10:13]-b[:,10:13],axis=1).max()))


def states(samples):
    return np.array([row['r']+row['v']+row['q']+row['omega'] for row in samples],dtype=np.float64)


def execute(binary,document,directory,points=None,extra=()):
    directory.mkdir(parents=True,exist_ok=True)
    source=directory/'scenario.json'; target=directory/'result.json'
    source.write_text(json.dumps(document,allow_nan=False))
    command=[str(binary),str(source),str(target)]
    if points is not None: command+=['--points',str(points)]
    command+=list(extra)
    run=subprocess.run(command,capture_output=True,text=True)
    if run.returncode:
        raise RuntimeError(' '.join(command)+'\n'+run.stderr)
    return json.loads(target.read_text())


def pointwise_errors(actual,expected,limits):
    failures=[]; maxima={}
    if len(actual)!=len(expected): raise RuntimeError('pointwise count mismatch')
    def check(label,a,b,absolute):
        a,b=np.atleast_1d(a),np.atleast_1d(b)
        if not np.isfinite(a).all(): raise RuntimeError('nonfinite '+label)
        error=float(np.linalg.norm(a-b))
        bound=absolute+limits['relative']*float(np.linalg.norm(b))
        maxima[label]=max(maxima.get(label,0.),error)
        if error>bound: failures.append(dict(field=label,error=error,limit=bound))
    for got,ref in zip(actual,expected):
        if got['t']!=ref['t']: raise RuntimeError('pointwise time mismatch')
        for field in ('point_mass_m_s2','j2_m_s2','total_acceleration_m_s2'):
            check(field,got[field],ref[field],limits['acceleration_m_s2_absolute'])
        check('total_angular_acceleration_rad_s2',got['total_angular_acceleration_rad_s2'],
              ref['total_angular_acceleration_rad_s2'],limits['angular_acceleration_rad_s2_absolute'])
        for name in ('srp','drag','albedo'):
            check(name+'_force_n',got[name+'_force_n'],ref[name+'_force_n'],limits['force_n_absolute'])
            check(name+'_torque_nm',got[name+'_torque_nm'],ref[name+'_torque_nm'],limits['torque_nm_absolute'])
        check('gravity_gradient_nm',got['gravity_gradient_nm'],ref['gravity_gradient_nm'],limits['torque_nm_absolute'])
        for field in ('density_kg_m3','illumination'):
            key='density_kg_m3_absolute' if field.startswith('density') else 'illumination_absolute'
            check(field,got[field],ref[field],limits[key])
        for name,begin,end,absolute in [('rhs_velocity',0,3,limits['velocity_m_s_absolute']),
            ('rhs_acceleration',3,6,limits['acceleration_m_s2_absolute']),
            ('rhs_quaternion',6,10,limits['quaternion_derivative_absolute']),
            ('rhs_angular_acceleration',10,13,limits['angular_acceleration_rad_s2_absolute'])]:
            check(name,np.array(got['rhs'])[begin:end],np.array(ref['rhs'])[begin:end],absolute)
    return failures,maxima


def validate(binary,suite,output):
    acceptance=json.loads((ROOT/'acceptance.json').read_text())
    manifest=json.loads((ROOT/'golden/manifest.json').read_text())
    if set(f['name'] for f in manifest['fixtures'])!=set(acceptance['scenarios']):
        raise RuntimeError('golden matrix differs from frozen acceptance scenarios')
    native_manifest=json.loads((ROOT/'golden/tudat/manifest.json').read_text())
    native={row['name']:row for row in native_manifest['fixtures']}
    rows=[]; failures=[]
    with tempfile.TemporaryDirectory(prefix='leo-qualification-') as scratch:
        scratch=Path(scratch)
        for fixture in manifest['fixtures']:
            name=fixture['name']
            golden_path=ROOT/'golden'/fixture['file']
            points_path=ROOT/'golden'/fixture['pointwise']
            scenario_path=ROOT/'scenarios'/(name+'.json')
            if sha(golden_path)!=fixture['sha256'] or sha(points_path)!=fixture['pointwise_sha256'] or sha(scenario_path)!=fixture['scenario_sha256']:
                raise RuntimeError('fixture integrity failure: '+name)
            document=json.loads(scenario_path.read_text())
            golden=json.loads(golden_path.read_text())
            times=np.array(golden['time_s'])
            expected=np.array(golden['states'])
            thresholds=acceptance['classes'][acceptance['scenarios'][name]]
            for metric,change in golden['reference_convergence'].items():
                if change>acceptance['reference_margin_fraction']*thresholds[metric]:
                    raise RuntimeError(f'{name}: unqualified reference convergence for {metric}')
            if suite=='light':
                keep=times<=600.
                times,expected=times[keep],expected[keep]
                document['run']['sample_times_s']=times.tolist()
                document['run']['duration_s']=float(times[-1])
            fixed=execute(binary,document,scratch/(name+'-pointwise'),points_path,extra=['--points-only'])
            point_failures,point_maxima=pointwise_errors(
                fixed['pointwise'],json.loads(points_path.read_text())['rows'],acceptance['pointwise'])
            got=execute(binary,document,scratch/name)
            if not np.array_equal(times,np.array([s['t'] for s in got['samples']])):
                raise RuntimeError(name+': exact requested output times not preserved')
            actual=states(got['samples'])
            if not np.isfinite(actual).all(): raise RuntimeError(name+': nonfinite trajectory')
            error=errors(actual,expected)
            end=errors(actual[-1:],expected[-1:])
            trajectory_failures=[dict(field=k,error=v,limit=thresholds[k]) for k,v in error.items() if v>thresholds[k]]
            other_references={}
            if 'analytical_kepler' in golden:
                oracle=golden['analytical_kepler']
                count=len(times)
                analytic=dict(position_m=float(np.linalg.norm(actual[:,:3]-np.array(oracle['position_m'])[:count],axis=1).max()),
                              velocity_m_s=float(np.linalg.norm(actual[:,3:6]-np.array(oracle['velocity_m_s'])[:count],axis=1).max()))
                other_references['analytical_kepler']=analytic
            if name in native:
                entry=native[name]
                native_path=ROOT/'golden/tudat'/entry['file']
                if sha(native_path)!=entry['sha256'] or sha(scenario_path)!=entry['scenario_sha256']:
                    raise RuntimeError('native Tudat fixture identity mismatch: '+name)
                oracle=json.loads(native_path.read_text())
                if not np.array_equal(np.array(oracle['time_s'])[:len(times)],times):
                    raise RuntimeError('native Tudat output times differ')
                other_references['native_tudat']=errors(actual,np.array(oracle['states'])[:len(times)])
            for source,values in other_references.items():
                trajectory_failures.extend(dict(field=source+'/'+key,error=value,limit=thresholds[key])
                    for key,value in values.items() if value>thresholds[key])
            row=dict(name=name,duration_s=float(times[-1]),samples=len(times),max_errors=error,
                     end_errors=end,pointwise_max_absolute=point_maxima,thresholds=thresholds,
                     reference_convergence=golden['reference_convergence'],
                     rhs_evaluations=got['rhs_evaluations'],failures=trajectory_failures+point_failures)
            row['other_references']=other_references
            rows.append(row)
            if row['failures']: failures.append(name)
            print(f'{name:25} '+('FAIL' if row['failures'] else 'PASS')+
                  f"  {error['position_m']:.3g} m  {error['velocity_m_s']:.3g} m/s  {error['attitude_rad']:.3g} rad  {error['angular_rate_rad_s']:.3g} rad/s",flush=True)
        # This is a determinism contract, not an independent physics oracle:
        # compare every scalar of an explicit sequential loop and batch workers.
        document=json.loads((ROOT/'scenarios/coupled_case_a.json').read_text())
        document['run']['duration_s']=600.
        document['run']['sample_times_s']=np.arange(0.,601.,30.).tolist()
        seq=execute(binary,document,scratch/'sequential',extra=['--route','sequential','--members','8','--dump-batch'])
        baseline=np.array([states(t) for t in seq['trajectories']])
        batch=[]
        for workers in (1,2,4,8):
            value=execute(binary,document,scratch/f'batch-{workers}',extra=['--route','batch','--members','8','--workers',str(workers),'--dump-batch'])
            observed=np.array([states(t) for t in value['trajectories']])
            same=np.array_equal(baseline.view(np.uint64),observed.view(np.uint64))
            batch.append(dict(workers=workers,members=8,bitwise_identical=same))
            if not same: failures.append(f'batch-{workers}')
        print('batch complete-state identity:',batch,flush=True)
    result=dict(kind='v0.1-reference-acceptance',suite=suite,status='PASS' if not failures else 'FAIL',
                binary_sha256=sha(binary),acceptance_sha256=sha(ROOT/'acceptance.json'),
                golden_manifest_sha256=sha(ROOT/'golden/manifest.json'),scenarios=rows,batch=batch,failures=failures)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    return 0 if not failures else 1


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--suite',choices=['light','full'],default='light')
    p.add_argument('--out',type=Path,default=Path('reference-acceptance.json'))
    args=p.parse_args()
    raise SystemExit(validate(args.binary.resolve(),args.suite,args.out))


if __name__=='__main__': main()
