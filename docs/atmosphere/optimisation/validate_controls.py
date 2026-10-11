#!/usr/bin/env python3
"""Compare every non-All control against the captured original scientific streams."""
import argparse
import gzip
import json
import os
from pathlib import Path
import shutil
import subprocess
import datetime
from validate import ROOT, difference, dump, sha

CONTROLS = {'off_off_off':(False,False,False),'utc_only':(True,False,False),
    'sun_only':(False,True,False),'orientation_only':(False,False,True),
    'utc_sun':(True,True,False),'utc_orientation':(True,False,True),'sun_orientation':(False,True,True)}


def stream(path):
    if path.exists():
        return path.open('rb')
    return gzip.open(str(path)+'.gz','rb')


def compare_streams(a,b):
    offset=0
    with stream(a) as x, stream(b) as y:
        while True:
            p,q=x.read(1<<20),y.read(1<<20)
            if p!=q:
                return f'native stream differs in block starting at byte {offset}'
            if not p:
                return None
            offset+=len(p)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-evidence',type=Path,required=True)
    parser.add_argument('--candidate-build',type=Path,required=True)
    parser.add_argument('--deps',type=Path,default=Path('/tmp/leo-atmosphere-deps'))
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--cpu',type=int,default=0)
    args=parser.parse_args()
    out=args.out.resolve();baseline=args.baseline_evidence.resolve();build=args.candidate_build.resolve();deps=args.deps.resolve()
    if out.exists() and any(out.iterdir()):
        raise RuntimeError('refusing populated control evidence directory')
    out.mkdir(parents=True,exist_ok=True)
    verification=json.loads((baseline/'verification.json').read_text())
    if not verification['all_bitwise_equal']:
        raise RuntimeError('baseline validation is not successful')
    base=['--lsk',deps/'naif0012.tls','--spk',deps/'de432s.bsp','--pck',deps/'earth_000101_270105_261009.bpc',
        '--frame','ITRF93','--weather',deps/'SpaceWeather-All-v1.2_snapshot_20200224.txt','--solfsmy',deps/'SOLFSMY.TXT',
        '--dtcfile',deps/'DTCFILE.TXT','--epoch-utc','2003-10-30T00:00:00','--duration','21600','--cadence','30','--rtol','1e-10','--max-step','15']
    cases=[x for c in 'ABC' for x in ['--case',ROOT/f'docs/numerical-audit/experiments/CASE-{c}-v1.cfg']]
    fixtures=[x for name,utc in [('quiet_june','2008-07-01T00:00:00'),('quiet_december','2008-12-15T00:00:00'),
        ('active_march','2001-03-20T00:00:00'),('active_october','2003-10-28T00:00:00'),('storm_october','2003-10-30T00:00:00')]
        for x in ['--fixture',name+'='+utc]]
    commands=[];comparisons={}
    def run(command):
        command=list(map(str,command));print('RUN',' '.join(command),flush=True)
        started=datetime.datetime.now(datetime.timezone.utc).isoformat()
        result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
        commands.append(dict(command=command,started_utc=started,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr))
        dump(out/'commands.json',commands)
        if result.returncode:raise RuntimeError(result.stdout+'\n'+result.stderr)
    expected_fixtures=json.loads((baseline/'fixtures-original/fixtures.json').read_text())['samples']
    total_native=0
    for name,options in CONTROLS.items():
        switches=[x for flag,on in zip(['--utc-reuse','--sun-sharing','--orientation-reuse'],options) for x in [flag,'on' if on else 'off']]
        run(['taskset','-c',args.cpu,build/'atmosphere_experiment',*base,*switches,*fixtures,'--mode','fixtures','--out',out/f'fixtures-{name}'])
        run(['taskset','-c',args.cpu,build/'optimisation_validation',*base,*switches,*cases,'--mode','propagate','--out',out/name])
        actual=json.loads((out/f'fixtures-{name}/fixtures.json').read_text())['samples']
        error=difference(expected_fixtures,actual,name+'/fixtures')
        if error:raise RuntimeError(error)
        for path in sorted((baseline/'original').glob('*.json')):
            a=json.loads(path.read_text());b=json.loads((out/name/path.name).read_text())
            error=difference(a,b,name+'/'+path.stem)
            if error:raise RuntimeError(error)
            native_name=path.stem+'_native.bin'
            error=compare_streams(baseline/'original'/native_name,out/name/native_name)
            if error:raise RuntimeError(name+'/'+native_name+': '+error)
            comparisons[name+'/'+path.stem]=dict(full_series_bitwise_equal=True,native_stream_byte_equal=True,native_calls=a['native_calls'])
            total_native+=a['native_calls']
        dump(out/'comparisons.json',comparisons)
    dump(out/'verification.json',dict(configurations=list(CONTROLS),full_series=18*len(CONTROLS),fixture_outputs=10710*len(CONTROLS),
        native_calls=total_native,all_bitwise_equal=True,baseline_verification_sha256=sha(baseline/'verification.json')))
    for path in list(out.rglob('*')):
        if path.is_file() and (path.suffix=='.bin' or path.stat().st_size>2*1024*1024):
            with path.open('rb') as src,gzip.GzipFile(filename=str(path)+'.gz',mode='wb',mtime=0) as dst:
                shutil.copyfileobj(src,dst)
            path.unlink()
    dump(out/'output-hashes.json',{str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file()})
    print('ALL INDEPENDENT CONTROLS BITWISE VERIFIED',out,flush=True)


if __name__=='__main__':main()
