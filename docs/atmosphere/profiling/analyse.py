#!/usr/bin/env python3
"""Reanalyse retained raw evidence; optionally add isolated lock experiments."""
import argparse
import datetime
import gzip
import json
from pathlib import Path
import shutil
import subprocess
import sys
import run as runner

ROOT=runner.ROOT


def load(path):
    if not path.exists():
        path=Path(str(path)+'.gz')
    opener=gzip.open if path.suffix=='.gz' else open
    with opener(path,'rt') as stream:return json.load(stream)


def nav(value):
    if isinstance(value,dict):
        return {k:nav(v) for k,v in value.items() if k not in ('raw','calls_per_requested_epoch') and not k.endswith('_histogram')}
    if isinstance(value,list):return [nav(v) for v in value]
    return value


def analyse(out,build,run_isolated=False):
    manifest=load(out/'manifest.json');commands=load(out/'command-log.json')
    def command(cmd):
        cmd=list(map(str,cmd));print('RUN',' '.join(cmd),flush=True)
        result=subprocess.run(cmd,cwd=ROOT,text=True,capture_output=True)
        commands.append({'command':cmd,'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr,
                         'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
        runner.dump(out/'command-log.json',commands)
        runner.require(result.returncode==0,result.stdout+'\n'+result.stderr)
    if run_isolated:
        first=commands[0]['command'];base=first[4:first.index('--mode')]
        for variant in ('original','instrumented'):
            command(['taskset','-c',manifest['settings']['concurrency_cpu_mask'],build/f'profile_concurrency_{variant}',
                     *base,'--mode','timing','--out',out/f'unused-isolated-{variant}',
                     '--profile-out',out/f'isolated-concurrency-{variant}-raw.json'])
    data={v:load(out/f'stages-{v}-raw.json') for v in ('original','instrumented')}
    runner.dump(out/'stage-timings.json',{v:runner.strip_raw(d) for v,d in data.items()})
    isolated={v:load(out/f'isolated-concurrency-{v}-raw.json') for v in ('original','instrumented')}
    equality={k:r['output_hashes']==isolated['instrumented']['cases'][k]['output_hashes']
              for k,r in isolated['original']['cases'].items()}
    runner.require(all(equality.values()),'isolated concurrency scientific outputs changed')
    verification=load(out/'verification.json');verification['isolated_concurrency_output_equality']=equality
    runner.dump(out/'verification.json',verification)
    concurrency={v:runner.strip_raw(d['concurrency']) for v,d in data.items()}
    concurrency['isolated']={v:runner.strip_raw(d) for v,d in isolated.items()}
    runner.dump(out/'concurrency-results.json',concurrency)
    for v in ('original','instrumented'):
        command([sys.executable,ROOT/'docs/atmosphere/profiling/reduce_locality.py','--profile-out',out/v])
    runner.dump(out/'workload-timings.json',{v:runner.strip_raw(load(out/v/'workload-timings.json')) for v in data})
    locality={v:load(out/v/'locality-statistics.json') for v in data}
    runner.dump(out/'locality-statistics.json',{v:nav(d) for v,d in locality.items()})
    analysis={'units':'ns unless indicated','fixed':{},'propagation':{},'measured_speedups':False}
    segments=['validation','rotation','transform','geodesy','utc','weather','geometry','native','sun_total','sun_transform']
    for model in ('nrlmsise00','jb2008'):
        stages=data['instrumented']['models'][model]['active']['stages']
        means={k:stages[k]['ns']['mean'] for k in segments if k in stages}
        total=stages['provider']['ns']['mean'];summed=sum(means.values());midnight=sum(stages[k]['ns']['mean'] for k in ('midnight_first','midnight_second'))
        original=data['original']['models'][model]['disabled']['outer_ns'];active=data['instrumented']['models'][model]['active']['outer_ns'];disabled=data['instrumented']['models'][model]['disabled']['outer_ns']
        analysis['fixed'][model]={'instrumented_total_mean_ns':total,'segment_means_ns':means,
            'other_wrapper_and_instrumentation_residual_ns':total-summed,
            'residual_fraction':(total-summed)/total,
            'segment_fractions':{k:v/total for k,v in means.items()},
            'original_provider':original,'diagnostic_active_provider':active,'diagnostic_disabled_provider':disabled,
            'active_median_inflation_vs_original':active['median']/original['median']-1,
            'midnight_mean_ns':midnight,'midnight_fraction':midnight/total,
            'midnight_removal_provider_ceiling':1/(1-midnight/total),
            'utc_removal_provider_ceiling':1/(1-means['utc']/total)}
    for case in 'ABC':
        for model in ('nrlmsise00','jb2008'):
            tag=f'CASE-{case}_{model}';original=[load(out/f'original/propagation/{tag}_rep{i}.json') for i in range(3)]
            inst=[load(out/f'instrumented/propagation/{tag}_rep{i}.json') for i in range(3)]
            no_capture=load(out/f'original/propagation/{tag}_scientific.json')['no_capture_wall_seconds']
            wall=sum(r['wall_seconds'] for r in original)/3
            provider=sum(r['provider_call_nanoseconds']['mean'] for r in original)/3
            rhs=sum(r['rhs_nanoseconds']['mean'] for r in original)/3
            sun=sum(r['external_sun_call_nanoseconds']['mean'] for r in original)/3
            stage_means=[]
            for row in inst:
                stage=row['diagnostic_stages'];stage_means.append(dict(zip(stage['stage_names'],stage['stage_mean_nanoseconds'])))
            avg={k:sum(row[k] for row in stage_means)/3 for k in stage_means[0]}
            calls=original[0]['provider_calls'];midnight=avg['midnight_first']+avg['midnight_second']
            fraction=midnight/avg['provider'];atmosphere_fraction=provider*calls/1e9/wall
            estimated_saving=provider*calls/1e9*fraction
            loc=next(r for r in locality['original']['propagation_request_sequences'] if r['label']==tag)
            analysis['propagation'][tag]={'calls':calls,'wall_mean_s':wall,'no_capture_wall_s':no_capture,
                'original_provider_mean_ns':provider,'original_rhs_mean_ns':rhs,'external_sun_mean_ns':sun,
                'provider_share_rhs':provider/rhs,'provider_share_propagation':atmosphere_fraction,
                'instrumented_stage_means_ns':avg,'midnight_fraction_instrumented_provider':fraction,
                'estimated_midnight_removal_wall_s':estimated_saving,
                'estimated_midnight_removal_propagation_ceiling':1/(1-estimated_saving/wall),
                'bound_method':'estimated cross-context bound: instrumented midnight fraction x original provider sum; not an exact decomposition or a prediction',
                'repeated_epoch_fraction':loc['repeated_epoch_fraction'],'repeated_request_fraction':loc['repeated_request_fraction'],
                'day_keys':loc['weather_day_keys'],'three_hour_keys':loc['weather_three_hour_keys'],
                'hour_keys':loc['weather_hour_keys'],'dt_summary':nav(loc['consecutive_epoch_gap']),
                'position_delta_summary':nav(loc['consecutive_position_movement']),
                'sun_reacquisition':loc['sun_reacquisition']}
    runner.dump(out/'analysis.json',analysis)
    for path in list(out.rglob('*')):
        if path.is_file() and path.suffix in ('.json','.jsonl') and path.stat().st_size>2*1024*1024:
            with path.open('rb') as src,gzip.GzipFile(filename=str(path)+'.gz',mode='wb',mtime=0) as dst:shutil.copyfileobj(src,dst)
            path.unlink()
    manifest['commands']=commands
    manifest['diagnostic_tool_hashes']={str(q.relative_to(ROOT)):runner.sha(q) for q in Path(__file__).parent.iterdir()
        if q.is_file() and q.suffix in ('.cpp','.hpp','.py','.txt')}
    manifest['documentation_hashes']={str(q.relative_to(ROOT)):runner.sha(q) for q in Path(__file__).parent.glob('*.md')}
    manifest['build']['supplemental_compile_commands']=load(build/'compile_commands.json')
    manifest['build']['supplemental_binary_hashes']={str(q.relative_to(build)):runner.sha(q) for q in build.glob('profile_concurrency_*')}
    manifest['output_hashes']={str(q.relative_to(out)):runner.sha(q) for q in out.rglob('*') if q.is_file() and q.name!='manifest.json'}
    runner.dump(out/'manifest.json',manifest)
    print('ANALYSED',out,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path)
    p.add_argument('--build-dir',type=Path,default=ROOT/'build-atmosphere-profiling');p.add_argument('--run-isolated',action='store_true')
    a=p.parse_args();analyse(a.out.resolve(),a.build_dir.resolve(),a.run_isolated)
