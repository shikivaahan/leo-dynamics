#!/usr/bin/env python3
"""Index completed ATM-OPT-001 evidence, source/build provenance and dependencies."""
import argparse
import datetime
import hashlib
import json
import platform
import subprocess
from pathlib import Path
from validate import ROOT, sha, dump


def tree_digest(path):
    entries={str(p.relative_to(path)):sha(p) for p in sorted(path.rglob('*')) if p.is_file()}
    digest=hashlib.sha256(json.dumps(entries,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    return dict(path=str(path),files=len(entries),canonical_file_hashes_sha256=digest,file_hashes=entries)


def fact(command):
    r=subprocess.run(list(map(str,command)),cwd=ROOT,text=True,capture_output=True)
    return dict(command=list(map(str,command)),returncode=r.returncode,stdout=r.stdout,stderr=r.stderr)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence',type=Path,default=ROOT/'docs/atmosphere/optimisation/evidence')
    parser.add_argument('--measurements',default='measurements')
    parser.add_argument('--candidate-build',type=Path,default=ROOT/'build-atmosphere-optimisation')
    parser.add_argument('--original-build',type=Path,default=ROOT/'build-atmosphere-optimisation-original')
    parser.add_argument('--test-build',type=Path,default=ROOT/'build-atmosphere-gcc')
    parser.add_argument('--original-source',type=Path,default=Path('/tmp/atm-opt-original'))
    parser.add_argument('--deps',type=Path,default=Path('/tmp/leo-atmosphere-deps'))
    args=parser.parse_args();out=args.evidence.resolve()
    load=lambda path:json.loads(path.read_text())
    validation=load(out/'validation/verification.json')
    controls=load(out/'controls/verification.json')
    benchmark=load(out/args.measurements/'manifest.json')
    if not validation['all_bitwise_equal'] or not controls['all_bitwise_equal']:
        raise RuntimeError('scientific evidence is not complete')
    # Closure-pass evidence (2026-10-11) is additional, versioned and never
    # replaces the original evidence above.
    cleanup={}
    for name in ('cleanup-interleave','cleanup-validation','cleanup-controls'):
        directory=out/name
        if directory.is_dir():
            verification=load(directory/'verification.json')
            cleanup[name]=dict(verification=verification,
                manifest=sha(directory/'manifest.json') if (directory/'manifest.json').is_file() else None,
                files=sorted(q.name for q in directory.iterdir() if q.is_file()))
    if not (out/args.measurements/'summary.json').is_file():
        raise RuntimeError('benchmark reduction is absent')
    builds={}
    for name,path in [('candidate',args.candidate_build),('original',args.original_build),('tests',args.test_build)]:
        path=path.resolve()
        binaries=[p for p in path.iterdir() if p.is_file() and (p.name.startswith(('atm_opt_','optimisation_validation','leo_')) or p.name=='atmosphere_experiment') and p.suffix=='']
        builds[name]=dict(directory=str(path),cmake_cache_sha256=sha(path/'CMakeCache.txt'),
            binaries={p.name:sha(p) for p in binaries})
        compile_commands=path/'compile_commands.json'
        if compile_commands.exists():
            builds[name]['compile_commands']=load(compile_commands)
            builds[name]['compile_commands_sha256']=sha(compile_commands)
    dependency_names=['naif0012.tls','de432s.bsp','earth_000101_270105_261009.bpc','pck00011.tpc',
        'SpaceWeather-All-v1.2_snapshot_20200224.txt','SOLFSMY.TXT','DTCFILE.TXT',
        'nrlmsise-00.c','nrlmsise-00_data.c','nrlmsise-00.h','JB2008.f','NRLMSISE-DOCUMENTATION']
    dependencies={str(args.deps/name):sha(args.deps/name) for name in dependency_names}
    dependencies.update({str(p):sha(p) for p in (args.deps/'jb-validation').rglob('*.DAT')})
    header_roots=[Path('/tmp/leo-deps/usr/include/eigen3'),Path('/tmp/leo-deps/usr/include/boost')]
    header_hashes=[tree_digest(p) for p in header_roots if p.is_dir()]
    caches={}
    for name in ['candidate','original','tests']:
        cache_path=Path(builds[name]['directory'])/'CMakeCache.txt'
        caches[name]={}
        for line in cache_path.read_text().splitlines():
            if line and not line.startswith(('#','//')) and '=' in line and ':' in line.split('=',1)[0]:
                key,value=line.split('=',1);caches[name][key]=value
    compilers={name:fact(command) for name,command in {'cxx':['c++','--version'],'c':['cc','--version'],
        'fortran':['/tmp/leo-fortran/usr/bin/gfortran-13','--version'],'cmake':['cmake','--version'],'python':['python3','--version']}.items()}
    source_hashes={str(p.relative_to(ROOT)):sha(p) for part in ('src','include','tests') for p in (ROOT/part).rglob('*') if p.is_file()}
    source_hashes.update(benchmark['original_source_hashes'] and {
        relative:sha(ROOT/relative) for relative in benchmark['original_source_hashes']})
    optimisation=ROOT/'docs/atmosphere/optimisation'
    tool_and_doc_hashes={p.name:sha(p) for p in optimisation.iterdir() if p.is_file()}
    command_files=[p for p in out.rglob('*') if p.is_file() and p.name in {'commands.json','command-log.json','build-commands.json','regression-final.json'}]
    output_hashes={str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file() and p!=out/'manifest.json'}
    manifest=dict(kind='ATM-OPT-001',generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        source_revision=fact(['git','rev-parse','HEAD']),original_revision=fact(['git','-C',args.original_source,'rev-parse','HEAD']),
        implementation_commits={'utc':'eca483f','sun':'856dacf','orientation':'a03b98f'},
        manifest_lifecycle='Generated after the source/tooling/evidence commit and before the documentation commit; final documentation commit is reported in delivery, avoiding a self-referential hash.',
        science=validation,independent_controls=controls,cleanup_pass=cleanup,
        benchmark_manifest=str((out/args.measurements/'manifest.json').relative_to(ROOT)),
        benchmark_environment=benchmark['source'],platform=platform.platform(),compilers=compilers,builds=builds,
        build_cache_variables=caches,source_hashes=source_hashes,tool_and_doc_hashes=tool_and_doc_hashes,
        dependency_hashes=dependencies,installed_header_dependencies=header_hashes,cspice_library=benchmark['cspice_library'],
        commands={str(p.relative_to(out)):load(p) for p in command_files},output_hashes=output_hashes,
        reproduction_commands='docs/atmosphere/optimisation/README.md',
        retained_baseline_build='/tmp/atm-opt-original/build-opt-original/atmosphere_experiment',
        historical_preservation='Validation compared all atmospheric/profiling/numerical-audit evidence hashes before/after; benchmark checked atmospheric evidence, frozen profiling tools/evidence and scientific driver/case inputs.')
    dump(out/'manifest.json',manifest)
    print('MANIFEST',out/'manifest.json','outputs',len(output_hashes),flush=True)


if __name__=='__main__':main()
