#!/usr/bin/env python3
"""Run serial profiling processes, verify science, and hash immutable evidence."""
import argparse
import datetime
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
REFERENCE_CASES = [
    ('leo_nrlmsise00_tests', 'NRLMSISE00 reproduces the official seventeen published reference cases'),
    ('leo_jb2008_tests', 'JB2008 reproduces the official SET validation epochs'),
    ('leo_jb2008_tests', 'JB2008 reproduces twelve full-precision independent reference densities'),
]


def native_reference_checks(test_dir, environment, execute):
    checks=[]
    for binary, case in REFERENCE_CASES:
        command=[test_dir/binary,case,'--reporter','console']
        output=execute(command,env=environment)
        require('skipped' not in output.lower(),case+' was skipped instead of verified')
        checks.append({'command':list(map(str,command)),'stdout':output,'returncode':0})
    return checks


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def load(path):
    return json.loads(path.read_text())


def strip_raw(value):
    if isinstance(value, dict):
        return {k: strip_raw(v) for k, v in value.items()
                if k not in {'raw_ns','raw_outer_ns','raw_batch_ns_per_operation','raw_latency_ns','raw_nanoseconds','raw'}}
    if isinstance(value, list):
        return [strip_raw(v) for v in value]
    return value


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, default=ROOT/'build-atmosphere-profiling')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--deps', type=Path, default=Path('/tmp/leo-atmosphere-deps'))
    p.add_argument('--test-dir', type=Path, default=ROOT/'build-atmosphere-gcc')
    p.add_argument('--single-cpu', type=int, default=0)
    p.add_argument('--concurrency-cpus', default='0-3')
    args = p.parse_args()
    out=args.out.resolve(); build=args.build_dir.resolve(); deps=args.deps.resolve()
    require(not out.exists() or not any(out.iterdir()), 'refusing populated evidence directory')
    out.mkdir(parents=True, exist_ok=True)
    require(args.single_cpu in os.sched_getaffinity(0), 'single CPU not permitted')
    commands=[]
    def run(command, env=None):
        command=[str(s) for s in command]
        print('RUN', ' '.join(command), flush=True)
        start=datetime.datetime.now(datetime.timezone.utc).isoformat()
        result=subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True)
        entry={'command':command,'started_utc':start,'returncode':result.returncode,
               'stdout':result.stdout,'stderr':result.stderr}
        commands.append(entry);dump(out/'command-log.json',commands)
        if result.returncode:
            raise RuntimeError(f'command failed ({result.returncode}): {command}\n{result.stderr}\n{result.stdout}')
        return result.stdout
    def fact(command):
        r=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
        return {'command':command,'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr}
    initial={'git_sha':fact(['git','rev-parse','HEAD'])['stdout'].strip(),
             'dirty_status':fact(['git','status','--porcelain=v1'])['stdout'],
             'load_before':os.getloadavg(),'affinity_before':sorted(os.sched_getaffinity(0)),
             'cpu':fact(['lscpu']),'os':platform.platform(),
             'compiler':fact(['c++','--version']),
             'fortran':fact(['/tmp/leo-fortran/usr/bin/gfortran-13','--version']),
             'cmake':fact(['cmake','--version']),'python':sys.version,
             'perf':fact(['sh','-c','command -v perf']),
             'governors':{str(q):q.read_text().strip() for q in Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_governor')},
             'frequency':{str(q):q.read_text().strip() for q in Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_cur_freq')}}
    dump(out/'initial-environment.json',initial)
    preserved={str(q.relative_to(ROOT)):sha(q) for base in ('direct-v1','direct-v2','validation-amendment-v1')
               for q in (ROOT/'docs/atmosphere/evidence'/base).rglob('*') if q.is_file()}
    source_paths=list((ROOT/'src/atmosphere').glob('*'))+[ROOT/'src/environment/spice.cpp']+list((ROOT/'include/leo/atmosphere').glob('*.hpp'))+[ROOT/'include/leo/environment/spice.hpp']
    source_before={str(q.relative_to(ROOT)):sha(q) for q in source_paths if q.is_file()}
    base=['--lsk',deps/'naif0012.tls','--spk',deps/'de432s.bsp','--pck',deps/'earth_000101_270105_261009.bpc',
          '--frame','ITRF93','--weather',deps/'SpaceWeather-All-v1.2_snapshot_20200224.txt',
          '--solfsmy',deps/'SOLFSMY.TXT','--dtcfile',deps/'DTCFILE.TXT','--epoch-utc','2003-10-30T00:00:00',
          '--duration','21600','--cadence','30','--rtol','1e-10','--max-step','15']
    case_args=[]
    for case in 'ABC':case_args += ['--case',f'docs/numerical-audit/experiments/CASE-{case}-v1.cfg']
    stage_data={}
    for variant in ('original','instrumented'):
        path=out/f'stages-{variant}-raw.json'
        run(['taskset','-c',args.concurrency_cpus,build/f'profile_stages_{variant}',*base,
             '--mode','timing','--out',out/f'unused-stage-{variant}', '--profile-out',path,
             '--profile-samples','2000','--profile-batches','5'])
        stage_data[variant]=load(path)
    stage_summary={variant:strip_raw(data) for variant,data in stage_data.items()}
    dump(out/'stage-timings.json',stage_summary)
    isolated={}
    for variant in ('original','instrumented'):
        path=out/f'isolated-concurrency-{variant}-raw.json'
        run(['taskset','-c',args.concurrency_cpus,build/f'profile_concurrency_{variant}',*base,
             '--mode','timing','--out',out/f'unused-isolated-{variant}','--profile-out',path])
        isolated[variant]=load(path)
    for key, row in isolated['original']['cases'].items():
        require(row['output_hashes']==isolated['instrumented']['cases'][key]['output_hashes'],
                key+' isolated concurrency outputs changed')
    concurrency={v:strip_raw(stage_data[v]['concurrency']) for v in ('original','instrumented')}
    concurrency['isolated']={v:strip_raw(isolated[v]) for v in ('original','instrumented')}
    dump(out/'concurrency-results.json',concurrency)
    for variant in ('original','instrumented'):
        directory=out/variant
        run(['taskset','-c',str(args.single_cpu),build/f'profile_workloads_{variant}',*base,*case_args,
             '--mode','propagate','--out',out/f'unused-workload-{variant}',
             '--profile-out',directory,'--profile-batches','3'])
        run([sys.executable,ROOT/'docs/atmosphere/profiling/reduce_locality.py','--profile-out',directory])
    dump(out/'workload-timings.json',{v:strip_raw(load(out/v/'workload-timings.json')) for v in ('original','instrumented')})
    # Preserve exact locality distributions in the per-variant files; top-level copy is navigational.
    dump(out/'locality-statistics.json',{v:strip_raw(load(out/v/'locality-statistics.json')) for v in ('original','instrumented')})
    fixture_args=[]
    for name,utc in [('quiet_june','2008-07-01T00:00:00'),('quiet_december','2008-12-15T00:00:00'),
                     ('active_march','2001-03-20T00:00:00'),('active_october','2003-10-28T00:00:00'),
                     ('storm_october','2003-10-30T00:00:00')]:
        fixture_args+=['--fixture',name+'='+utc]
    for variant,binary in [('original',build/'baseline/atmosphere_experiment'),('instrumented',build/'profile_fixtures')]:
        run(['taskset','-c',str(args.single_cpu),binary,*base,*fixture_args,'--mode','fixtures','--out',out/f'fixtures-{variant}'])
    coordinates=run(['taskset','-c',str(args.single_cpu),build/'baseline/atmosphere_experiment',
                     *base,'--mode','coordinates','--out',out/'coordinates'])
    toolkit_version=json.loads(coordinates)['spice_toolkit_version']
    verification={'native_provider':{},'trajectories':{},'locality_result_hashes':{},'fixtures':{}}
    for model in ('nrlmsise00','nrlmsise00_history','jb2008'):
        a,b=(stage_data[v]['models'][model] for v in ('original','instrumented'))
        equal=a['provider_output']==b['provider_output'] and a['native_output']==b['native_output'] and a['provider_output']==a['native_output']
        verification['native_provider'][model]={'bitwise_json_equal':equal,'output':a['provider_output']}
        require(equal,model+' native/provider output changed')
    for case in 'ABC':
        for model in ('nrlmsise00','jb2008'):
            tag=f'CASE-{case}_{model}'
            historical=load(ROOT/f'docs/atmosphere/evidence/direct-v2/propagate/propagation/{tag}.json')
            original=load(out/f'original/propagation/{tag}_trajectory.json')
            instrumented=load(out/f'instrumented/propagation/{tag}_trajectory.json')
            equality=original['trajectory_checksum']==instrumented['trajectory_checksum']==historical['trajectory_checksum']
            series_equal=original['samples']==instrumented['samples']==historical['samples']
            repetitions=[]
            for variant in ('original','instrumented'):
                rep=load(out/f'{variant}/propagation/{tag}_repetitions.json')
                require(rep['repetitions_bitwise_identical'],tag+' repetitions differ')
                for i in range(3):
                    row=load(out/f'{variant}/propagation/{tag}_rep{i}.json')
                    require(row['trajectory_checksum']==historical['trajectory_checksum'],tag+' recorded trajectory differs')
                    require(row['requests_dropped_over_capacity']==0,tag+' dropped requests')
                    repetitions.append(row['trajectory_checksum'])
            verification['trajectories'][tag]={'checksum':original['trajectory_checksum'],'baseline_equal':equality,
                'all_scientific_samples_equal':series_equal,'six_profiled_checksums':repetitions}
            require(equality and series_equal,tag+' scientific baseline differs')
    for path in (out/'original/locality').glob('*_rep[0-9]*.json'):
        a=load(path);b=load(out/'instrumented/locality'/path.name)
        equal=a['result_hash']==b['result_hash']
        verification['locality_result_hashes'][path.name]=equal
        require(equal,path.name+' locality outputs changed')
    historical=load(ROOT/'docs/atmosphere/evidence/direct-v2/fixtures/fixtures.json')
    a=load(out/'fixtures-original/fixtures.json');b=load(out/'fixtures-instrumented/fixtures.json')
    equal=a['samples']==b['samples']==historical['samples']
    verification['fixtures']={'all_samples_equal':equal,'count':len(a['samples'])}
    require(equal,'fixture scientific samples changed')
    test_env=os.environ.copy();test_env.update({
        'LEO_SPICE_TEST_SPK':str(deps/'de432s.bsp'),'LEO_SPICE_TEST_LSK':str(deps/'naif0012.tls'),
        'LEO_ATMOSPHERE_TEST_PCK':str(deps/'earth_000101_270105_261009.bpc'),
        'LEO_ATMOSPHERE_TEST_LOW_PRECISION_PCK':str(deps/'pck00011.tpc'),
        'LEO_NRLMSISE00_TEST_CSSI':str(deps/'SpaceWeather-All-v1.2_snapshot_20200224.txt'),
        'LEO_NRLMSISE00_REFERENCE_DOCUMENTATION':str(deps/'NRLMSISE-DOCUMENTATION'),
        'LEO_JB2008_VALIDATION_DIR':str(deps/'jb-validation')})
    test_output=run(['ctest','--test-dir',args.test_dir,'--output-on-failure','--parallel','4'],env=test_env)
    verification['regression_tests']={'returncode':0,'output':test_output,'environment':{k:v for k,v in test_env.items() if k.startswith('LEO_')}}
    verification['native_reference_checks']=native_reference_checks(args.test_dir,test_env,run)
    require(all(sha(ROOT/k)==v for k,v in source_before.items()),'production source changed')
    require(all(sha(ROOT/k)==v for k,v in preserved.items()),'historical evidence changed')
    verification['production_sources_unchanged']=True;verification['historical_evidence_unchanged']=True
    dump(out/'verification.json',verification)
    # Keep raw distributions compressed, not discarded. All JSON remains readable through gzip.
    for path in list(out.rglob('*')):
        if path.is_file() and path.suffix in ('.json','.jsonl') and path.stat().st_size>2*1024*1024 and path.name not in ('stage-timings.json','workload-timings.json','locality-statistics.json'):
            with path.open('rb') as src, gzip.GzipFile(filename=str(path)+'.gz',mode='wb',mtime=0) as dst:
                shutil.copyfileobj(src,dst)
            path.unlink()
    dep_names=['naif0012.tls','de432s.bsp','earth_000101_270105_261009.bpc','pck00011.tpc',
               'SpaceWeather-All-v1.2_snapshot_20200224.txt','SOLFSMY.TXT','DTCFILE.TXT',
               'nrlmsise-00.c','nrlmsise-00_data.c','nrlmsise-00.h','JB2008.f','NRLMSISE-DOCUMENTATION']
    manifest={'kind':'ATM-PROF-001','generated_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'source':initial,'initial_session_dirty_status':'?? outputs/','scientific_baseline':'direct-v2',
        'historical_timing_baseline':'direct-v1','source_hashes':source_before,'preserved_evidence_hashes':preserved,
        'diagnostic_tool_hashes':{str(q.relative_to(ROOT)):sha(q) for q in (ROOT/'docs/atmosphere/profiling').iterdir()
                                 if q.is_file() and q.suffix in ('.cpp','.hpp','.py','.txt')},
        'dependency_hashes':{str(deps/name):sha(deps/name) for name in dep_names},
        'cspice_library':{'path':'/tmp/leo_cspice/cspice/lib/cspice.a','sha256':sha(Path('/tmp/leo_cspice/cspice/lib/cspice.a'))},
        'spice_toolkit_version':toolkit_version,
        'build':{'directory':str(build),'cache_sha256':sha(build/'CMakeCache.txt'),
                 'compile_commands':load(build/'compile_commands.json'),
                 'binaries':{str(q.relative_to(build)):sha(q) for q in build.glob('profile_*') if q.is_file()},
                 'configure_command':'cmake -S docs/atmosphere/profiling -B build-atmosphere-profiling -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG" -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG" -DCMAKE_Fortran_FLAGS_RELEASE="-O3 -DNDEBUG" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DLEO_NRLMSISE00_ROOT=/tmp/leo-atmosphere-deps -DLEO_JB2008_SOURCE=/tmp/leo-atmosphere-deps/JB2008.f -DCMAKE_Fortran_COMPILER=/tmp/leo-fortran/usr/bin/gfortran-13 -DCMAKE_PREFIX_PATH="/tmp/leo-deps/usr;/tmp/leo-deps/catch-install" -DCSPICE_ROOT=/tmp/leo_cspice/cspice',
                 'build_command':'cmake --build build-atmosphere-profiling --parallel 4'},
        'commands':commands,'settings':{'frame':'ITRF93','epoch_utc':'2003-10-30T00:00:00','duration_s':21600,
            'cadence_s':30,'rtol':1e-10,'max_step_s':15,'initial_step_s':1,'max_attempts':1000000,
            'absolute_tolerances':{'position_m':1e-3,'velocity_m_s':1e-6,'quaternion':1e-12,'body_rate_rad_s':1e-12},
            'nrl_switches':[0]+[1]*23,'baseline_ap_history':False,'separate_history_diagnostic':True,
            'samples':2000,'stage_batches':5,'propagation_batches':3,'warmup':256,
            'single_cpu':args.single_cpu,'concurrency_cpu_mask':args.concurrency_cpus},
        'methodology':'EXPERIMENT_DESIGN.md and DECISIONS.md; stage nearest-rank tails; workload linear-interpolated tails',
        'verification_file':'verification.json','load_after':os.getloadavg(),
        'output_hashes':{str(q.relative_to(out)):sha(q) for q in out.rglob('*') if q.is_file() and q.name!='manifest.json'}}
    dump(out/'manifest.json',manifest)
    import analyse
    analyse.analyse(out,build)
    print('VERIFIED AND HASHED',out,flush=True)


if __name__=='__main__':
    main()
