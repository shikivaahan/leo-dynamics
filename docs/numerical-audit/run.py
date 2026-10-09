#!/usr/bin/env python3
"""Execute numerical audit studies; preserve NEW compact evidence, never history.

The supplied executable must be built against the desired original/remediated
source. Standard-library orchestration; no performance claims. Full runs include
independent RKF78, prescribed binary-exact RK4 grids, subsystem switches, separate
rtol/atol/max-step sweeps and perturbation/step traces. Raw selected differences
are retained compactly; full in-memory trajectories are not dumped for every run.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys


def qangle(a, b):
    if sum(x*y for x,y in zip(a,b)) < 0:
        b = [-v for v in b]
    return 4*math.asin(min(1., math.sqrt(sum((x-y)**2 for x,y in zip(a,b)))*.5))


def error_row(a, b):
    def difference(start, end):
        return math.sqrt(sum((x-y)**2 for x,y in zip(a[start:end], b[start:end])))
    return dict(time_s=a[0],position_m=difference(1,4),velocity_m_s=difference(4,7),
                attitude_rad=qangle(a[7:11],b[7:11]),omega_rad_s=difference(11,14),
                illumination=abs(a[14]-b[14]),srp_force_n=difference(15,18),
                srp_torque_nm=difference(18,21),work_j=abs(a[21]-b[21]))


def compare(a,b,history=False):
    if [s[0] for s in a['samples']] != [s[0] for s in b['samples']]:
        raise ValueError('comparison sample timestamps differ')
    errors=[error_row(x,y) for x,y in zip(a['samples'],b['samples'])]
    keys=list(errors[0])[1:]
    out=dict(final=errors[-1],maximum={k:max(e[k] for e in errors) for k in keys},
             rms={k:math.sqrt(sum(e[k]**2 for e in errors)/len(errors)) for k in keys},
             first_state_difference_s=next((e['time_s'] for e in errors if any(e[k]>0 for k in keys[:4])),None),
             first_attitude_1urad_s=next((e['time_s'] for e in errors if e['attitude_rad']>1e-6),None),
             first_position_1mm_s=next((e['time_s'] for e in errors if e['position_m']>1e-3),None))
    # A compact growth history: samples around first significant growth plus
    # every ten minutes, not huge committed per-stage state datasets.
    significant=out['first_attitude_1urad_s']
    if history:
        out['growth_history']=[e for e in errors if e['time_s']%600==0 or (significant is not None and abs(e['time_s']-significant)<=90)]
    return out


def compact(r):
    out={k:v for k,v in r.items() if k not in ('samples','steps')}
    if 'samples' in r:
        out['final_sample']=r['samples'][-1]
    return out


def trace_comparison(a,b):
    differences=[]
    for x,y in zip(a['steps'],b['steps']):
        if x[:4]!=y[:4]:
            differences.append(dict(baseline=x,perturbed=y))
            if len(differences)==12:
                break
    return dict(baseline_attempts=len(a['steps']),perturbed_attempts=len(b['steps']),
                first_step_sequence_differences=differences,
                first_penumbra_steps=[s for s in a['steps'] if 0<s[4]<1][:12])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--spk',type=Path,required=True)
    p.add_argument('--lsk',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--source-commit',required=True)
    p.add_argument('--full',action='store_true')
    a=p.parse_args()
    if a.output.exists():
        p.error('output exists; preserve earlier evidence')
    results={}
    def execute(name,scenario='full',method='dopri',stdin='',**options):
        command=[str(a.binary.resolve()),'--spk',str(a.spk.resolve()),'--lsk',str(a.lsk.resolve()),'--scenario',scenario,'--method',method]
        for k,v in options.items():
            command += ['--'+k.replace('_','-'),str(v)]
        result=subprocess.run(command,input=stdin,text=True,capture_output=True,timeout=900)
        if result.returncode:
            raise RuntimeError(f'{name}: {result.stderr}')
        data=json.loads(result.stdout)
        results[name]=compact(data)
        print(name, data.get('rhs_calls'), data.get('checksum'), file=sys.stderr,flush=True)
        return data
    baseline=execute('full-default')
    comparisons={}
    repeat=execute('full-zero-repeat')
    assert repeat['checksum']==baseline['checksum']
    for name,opts in [('ulp-plus',{'ulp_x':1}),('ulp-minus',{'ulp_x':-1}),('1um',{'perturb_x':1e-6}),
                      ('30um',{'perturb_x':3e-5}),('1mm',{'perturb_x':1e-3}),('1m',{'perturb_x':1})]:
        candidate=execute('full-'+name,**opts)
        comparisons[name]=compare(baseline,candidate,history=True)
    trace=execute('trace-zero',method='trace')
    assert trace['checksum']==baseline['checksum'],'diagnostic mirror differs from production'
    perturbed_trace=execute('trace-30um',method='trace',perturb_x=3e-5)
    replay_input=''.join(' '.join(format(v,'.17g') for v in row[:14])+'\n' for row in baseline['samples'])
    replay=execute('common-state-replay-zero',method='replay',stdin=replay_input)
    replay_perturbed=execute('common-state-replay-30um',method='replay',stdin=replay_input,perturb_x=3e-5)
    comparisons['common-state-model-differences']=compare(replay,replay_perturbed)
    spice=execute('spice-units-time',method='spice')
    convergence={}
    if a.full:
        for scenario in ('two_body','j2','attitude','principal','srp','drag','albedo','gg','full'):
            normal=baseline if scenario=='full' else execute(scenario+'-default',scenario=scenario)
            ref=execute(scenario+'-rkf78-reference',scenario=scenario,method='rkf78',rtol=1e-13,atol_scale=.001,max_step=1)
            refined=execute(scenario+'-rkf78-refinement',scenario=scenario,method='rkf78',rtol=1e-14,atol_scale=.0001,max_step=.5)
            if scenario=='full':
                full_refined=refined
            convergence[scenario]=dict(default_vs_reference=compare(ref,normal),reference_refinement=compare(refined,ref),sweeps={})
            for field,values in [('rtol',(1e-11,1e-12,1e-13)),('atol_scale',(.1,.01,.001)),('max_step',(5,1,.25))]:
                for value in values:
                    row=execute(f'{scenario}-{field}-{value}',scenario=scenario,**{field:value})
                    convergence[scenario]['sweeps'][f'{field}={value}']=compare(refined,row)
        for scenario in ('full_no_eclipse','full_no_torque','srp','drag','albedo','gg'):
            zero=execute(scenario+'-switch-zero',scenario=scenario)
            perturbed=execute(scenario+'-switch-30um',scenario=scenario,perturb_x=3e-5)
            comparisons[scenario]=compare(zero,perturbed)
        for step in (1,.5,.25):
            zero=execute(f'fixed-full-{step}-zero',method='rk4',max_step=step)
            perturbed=execute(f'fixed-full-{step}-30um',method='rk4',max_step=step,perturb_x=3e-5)
            comparisons[f'fixed-grid-step={step}']=compare(zero,perturbed)
            comparisons[f'fixed-grid-vs-rkf78-step={step}']=compare(full_refined,zero)
        for scenario in ('two_body','principal'):
            for step in (60,30,15,7.5):
                execute(f'{scenario}-rk4-order-{step}',scenario=scenario,method='rk4',max_step=step,cadence=21600)
        long_normal=execute('j2-long-default',scenario='j2',duration=172800)
        long_reference=execute('j2-long-rkf78',scenario='j2',method='rkf78',duration=172800,rtol=1e-13,atol_scale=.001,max_step=5)
        comparisons['j2-two-day-reference']=compare(long_reference,long_normal)
    evidence=dict(source_commit=a.source_commit,inputs='benchmarks/cases/bench_support.hpp unchanged original initial state and six-hour workload',
                  independent_reference='Boost controlled RKF7(8) with field-scaled variables; independent fixed RK4 prescribed binary-exact grids; Kepler and principal-axis analytic references',
                  results=results,perturbation_comparisons=comparisons,step_localisation=trace_comparison(trace,perturbed_trace),convergence=convergence,spice=spice)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(evidence,indent=2,allow_nan=False)+'\n')
    print('Saved',a.output)


if __name__=='__main__':
    main()
