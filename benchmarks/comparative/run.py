#!/usr/bin/env python3
"""Serial, checkpointed comparative qualification; no public process/batch API."""
import argparse
import copy
import hashlib
import json
import os
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
REF=ROOT/'tests/reference'
sys.path.insert(0,str(REF))
from test_reference_regression import errors,states
WORKER=Path(__file__).with_name('worker.py')
NATIVE_TUDAT={'two_body_circular','j2_inclined','torque_free_elongated'}
SCENARIOS=['two_body_circular','j2_inclined','torque_free_elongated','drag_flat_plate',
           'coupled_case_a','coupled_case_b']


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path,document):
    temporary=path.with_suffix(path.suffix+'.tmp')
    temporary.write_text(json.dumps(document,indent=2,allow_nan=False)+'\n')
    temporary.replace(path)


def execute(command):
    start=time.perf_counter()
    result=subprocess.run([str(x) for x in command],cwd=ROOT,capture_output=True,text=True)
    duration=time.perf_counter()-start
    if result.returncode:
        raise RuntimeError(' '.join(map(str,command))+'\n'+result.stderr)
    return duration


def statistics_row(samples,members):
    median=statistics.median(samples)
    return dict(median_seconds=median,min_seconds=min(samples),max_seconds=max(samples),
                trajectories_per_second=members/median,repetitions_seconds=samples)


def configurations(quick):
    configs=[]
    for name in SCENARIOS:
        source=json.loads((REF/'scenarios'/(name+'.json')).read_text())
        horizons=[(60.,10.)] if quick else [(600.,10.),(source['run']['duration_s'],source['run']['cadence_s'])]
        for duration,cadence in horizons:
            for tool in ('cpp','scipy','tudat'):
                configs.append(dict(name=name,tool=tool,duration_s=duration,cadence_s=cadence,members=1,workers=1,route='sequential'))
    # A small matrix asks two distinct questions: size1/8/64, and actual strong
    # scaling at64. No redundant Cartesian sweep of every force/parameter.
    for name in ['j2_inclined','coupled_case_a']:
        duration=60. if quick else 600.
        for members in (8,64):
            for tool in ('cpp','scipy','tudat'):
                configs.append(dict(name=name,tool=tool,duration_s=duration,cadence_s=30.,members=members,workers=1,route='sequential'))
        for workers in (1,2,4,8):
            configs.append(dict(name=name,tool='cpp',duration_s=duration,cadence_s=30.,members=64,workers=workers,route='batch'))
    return configs


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--tudat-python',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--repeats',type=int,default=3)
    parser.add_argument('--warmups',type=int,default=1)
    parser.add_argument('--quick',action='store_true',help='short tooling smoke, not six-hour qualification')
    parser.add_argument('--resume',action='store_true')
    args=parser.parse_args()
    args.binary=args.binary.resolve();args.tudat_python=args.tudat_python.resolve()
    args.out=args.out.resolve();args.out.mkdir(parents=True,exist_ok=True)
    configs=configurations(args.quick)
    sources=[Path(__file__),WORKER,REF/'acceptance.json']+sorted((REF/'generators').glob('*.py'))+sorted((REF/'scenarios').glob('*.json'))
    identity=dict(binary_sha256=sha(args.binary),sources={str(p.relative_to(ROOT)):sha(p) for p in sources},
                  configs=configs,repeats=args.repeats,warmups=args.warmups,
                  tudat_python=str(args.tudat_python),python=sys.executable)
    ledger_path=args.out/'ledger.json'
    if ledger_path.exists():
        ledger=json.loads(ledger_path.read_text())
        if not args.resume or ledger['identity']!=identity:
            raise RuntimeError('nonempty run or changed experiment identity: select a new --out directory')
        for entry in ledger['completed'].values():
            if sha(args.out/entry['file'])!=entry['sha256']:
                raise RuntimeError('published measurement hash mismatch')
    else:
        ledger=dict(identity=identity,completed={})
        save(ledger_path,ledger)
    limits=json.loads((REF/'acceptance.json').read_text())
    checked={}
    rows=[]
    for index,config in enumerate(configs):
        key=f'{index:03d}-{config["name"]}-{config["tool"]}-{config["members"]}-{config["workers"]}-{config["route"]}'
        if key in ledger['completed']:
            rows.append(json.loads((args.out/ledger['completed'][key]['file']).read_text()));continue
        target=args.out/(key+'.json')
        if config['tool']=='tudat' and config['name'] not in NATIVE_TUDAT:
            row=dict(**config,status='UNMATCHED',reason='Native Tudat environmental panel equations differ; not an equivalent propagation timing')
        else:
            scenario=copy.deepcopy(json.loads((REF/'scenarios'/(config['name']+'.json')).read_text()))
            duration,cadence=config['duration_s'],config['cadence_s']
            scenario['run'].update(duration_s=duration,cadence_s=cadence,
                                   sample_times_s=np.r_[np.arange(0.,duration,cadence),duration].tolist())
            source=args.out/(key+'-scenario.json');source.write_text(json.dumps(scenario))
            # Verify accuracy at this actual duration/cadence before timing. The
            # explicit reference generation is a different untimed operation.
            check_key=(config['name'],duration,cadence)
            if check_key not in checked:
                truth=args.out/(key+'-independent-reference.json')
                execute([sys.executable,WORKER,source,truth,'--tool','scipy','--reference','--members','1','--repeats','1','--warmups','0'])
                checked[check_key]=json.loads(truth.read_text())
            truth=checked[check_key]
            validation=args.out/(key+'-validation.json')
            if config['tool']=='cpp':
                execute([args.binary,source,validation])
                candidate=states(json.loads(validation.read_text())['samples'])
            else:
                python=sys.executable if config['tool']=='scipy' else str(args.tudat_python)
                execute([python,WORKER,source,validation,'--tool',config['tool'],'--members','1','--repeats','1','--warmups','0'])
                candidate=np.array(json.loads(validation.read_text())['states'])
            achieved=errors(candidate,np.array(truth['states']))
            thresholds=limits['classes'][limits['scenarios'][config['name']]]
            if any(value>thresholds[metric] for metric,value in achieved.items()):
                raise RuntimeError(f'benchmark configuration is not accuracy-qualified: {key}: {achieved}')
            raw_path=args.out/(key+'-raw.json')
            if config['tool']=='cpp':
                command=[args.binary,source,raw_path,'--route',config['route'],'--members',str(config['members']),
                         '--workers',str(config['workers']),'--repeats',str(args.repeats),'--warmups',str(args.warmups)]
            else:
                python=sys.executable if config['tool']=='scipy' else str(args.tudat_python)
                command=[python,WORKER,source,raw_path,'--tool',config['tool'],'--members',str(config['members']),
                         '--repeats',str(args.repeats),'--warmups',str(args.warmups)]
            outer_wall=execute(command)
            raw=json.loads(raw_path.read_text())
            core=raw['repetitions_seconds'] if config['tool']=='cpp' else [a+b for a,b in zip(raw['solver_seconds'],raw['sampling_seconds'])]
            row=dict(**config,status='MEASURED',**statistics_row(core,config['members']),achieved_accuracy=achieved,
                     scenario_sha256=sha(source),raw_file=raw_path.name,raw_sha256=sha(raw_path),
                     setup_seconds=raw['setup_seconds'],import_seconds=raw.get('import_seconds'),
                     python_solver_seconds=raw.get('solver_seconds'),sampling_seconds=raw.get('sampling_seconds'),
                     repeated_setup_seconds=raw.get('repeated_setup_seconds'),
                     whole_process_wall_seconds_including_startup_warmups_and_publication=outer_wall,
                     rhs_evaluations_per_repeat=raw['rhs_evaluations_per_repeat'],
                     process_peak_rss_bytes=raw.get('process_peak_rss_bytes'),
                     memory_scope=raw.get('memory_scope'),logical_output_bytes=raw.get('logical_output_bytes'),
                     diagnostics=raw.get('diagnostics'),command=list(map(str,command)))
        save(target,row)
        ledger['completed'][key]=dict(file=target.name,sha256=sha(target));save(ledger_path,ledger)
        rows.append(row)
        print(key,row['status'],row.get('median_seconds'),flush=True)
    output=dict(kind='v0.1-comparative-benchmarks',status='complete',quick=args.quick,
                host=dict(cpu=platform.processor(),platform=platform.platform(),logical_cpus=os.cpu_count()),
                identity=identity,rows=rows,
                timing_boundary='propagation plus requested-output generation and retention; setup, import, publication and outer process times separately reported',
                fairness=['same explicit SI inputs and physical equations','each actual duration/cadence accuracy-checked before timing',
                          'one benchmark child at a time, one discarded warmup and measured repetitions',
                          'no automatic process strategy, new integrator, hidden atmosphere model or reference generation in headline clocks',
                          'matched constant-Sun environment has no SPICE contention; not a replacement for MT-002 environmental scaling evidence'])
    save(args.out/'summary.json',output)


if __name__=='__main__': main()
