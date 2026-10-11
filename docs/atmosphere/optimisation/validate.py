#!/usr/bin/env python3
"""Run real native regressions and exact original/candidate/frozen comparisons."""
import argparse
import datetime
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def dump(path, data):
    path.write_text(json.dumps(data, indent=2, allow_nan=False) + '\n')


def difference(a, b, path=''):
    if isinstance(a, dict) and isinstance(b, dict):
        if a.keys() != b.keys():
            return path + ': keys differ'
        for key in a:
            error = difference(a[key], b[key], path + '/' + key)
            if error:
                return error
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            return path + ': lengths differ'
        for i, (x, y) in enumerate(zip(a, b)):
            error = difference(x, y, path + '/' + str(i))
            if error:
                return error
    elif isinstance(a, (float, int)) and not isinstance(a, bool) and isinstance(b, (float, int)):
        if struct.pack('d', a) != struct.pack('d', b):
            return f'{path}: IEEE-754 differs: {a!r} != {b!r}'
    elif a != b:
        return f'{path}: {a!r} != {b!r}'
    return None


def compare_binary(a, b):
    offset = 0
    with a.open('rb') as x, b.open('rb') as y:
        while True:
            first, second = x.read(1 << 20), y.read(1 << 20)
            if first != second:
                for i, (p, q) in enumerate(zip(first, second)):
                    if p != q:
                        return f'first byte {offset+i}, native row {(offset+i)//144}, field {((offset+i)%144)//8}'
                return f'length differs at {offset+min(len(first),len(second))}'
            if not first:
                return None
            offset += len(first)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original-build', type=Path, required=True)
    parser.add_argument('--candidate-build', type=Path, required=True)
    parser.add_argument('--test-dir', type=Path, default=ROOT/'build-atmosphere-gcc')
    parser.add_argument('--deps', type=Path, default=Path('/tmp/leo-atmosphere-deps'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=0)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists() and any(out.iterdir()):
        raise RuntimeError('refusing populated verification directory')
    out.mkdir(parents=True, exist_ok=True)
    deps = args.deps.resolve()
    commands, comparisons = [], {}
    historical_dirs = [ROOT/'docs/atmosphere/evidence', ROOT/'docs/atmosphere/profiling/evidence', ROOT/'docs/numerical-audit/evidence']
    preserved = {str(p.relative_to(ROOT)): sha(p) for directory in historical_dirs for p in directory.rglob('*') if p.is_file()}
    def run(command, env=None):
        command = list(map(str, command))
        print('RUN', ' '.join(command), flush=True)
        started = datetime.datetime.now(datetime.timezone.utc).isoformat()
        result = subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True)
        commands.append(dict(command=command, started_utc=started, returncode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        dump(out/'commands.json', commands)
        if result.returncode:
            raise RuntimeError(result.stdout+'\n'+result.stderr)
        return result.stdout
    def equal(label, a, b):
        error = difference(a, b, label)
        comparisons[label] = dict(bitwise_equal=error is None, first_difference=error)
        dump(out/'comparisons.json', comparisons)
        if error:
            raise RuntimeError(error)
    env = os.environ.copy()
    env.update({
        'LEO_SPICE_TEST_SPK': str(deps/'de432s.bsp'),
        'LEO_SPICE_TEST_LSK': str(deps/'naif0012.tls'),
        'LEO_SPICE_TEST_TEXT_PCK': str(deps/'pck00011.tpc'),
        'LEO_ATMOSPHERE_TEST_PCK': str(deps/'earth_000101_270105_261009.bpc'),
        'LEO_ATMOSPHERE_TEST_LOW_PRECISION_PCK': str(deps/'pck00011.tpc'),
        'LEO_NRLMSISE00_TEST_CSSI': str(deps/'SpaceWeather-All-v1.2_snapshot_20200224.txt'),
        'LEO_NRLMSISE00_REFERENCE_DOCUMENTATION': str(deps/'NRLMSISE-DOCUMENTATION'),
        'LEO_JB2008_VALIDATION_DIR': str(deps/'jb-validation')})
    run(['ctest','--test-dir',args.test_dir,'--output-on-failure','--parallel','4'], env)
    reference_cases = [
        ('leo_nrlmsise00_tests','NRLMSISE00 reproduces the official seventeen published reference cases'),
        ('leo_jb2008_tests','JB2008 reproduces the official SET validation epochs'),
        ('leo_jb2008_tests','JB2008 reproduces twelve full-precision independent reference densities')]
    for binary, case in reference_cases:
        result = run([args.test_dir/binary,case,'--reporter','console'],env)
        if 'skipped' in result.lower():
            raise RuntimeError('required native reference case skipped: '+case)
    base = ['--lsk',deps/'naif0012.tls','--spk',deps/'de432s.bsp','--pck',deps/'earth_000101_270105_261009.bpc',
            '--frame','ITRF93','--weather',deps/'SpaceWeather-All-v1.2_snapshot_20200224.txt',
            '--solfsmy',deps/'SOLFSMY.TXT','--dtcfile',deps/'DTCFILE.TXT','--epoch-utc','2003-10-30T00:00:00',
            '--duration','21600','--cadence','30','--rtol','1e-10','--max-step','15']
    cases = [value for case in 'ABC' for value in ['--case',ROOT/f'docs/numerical-audit/experiments/CASE-{case}-v1.cfg']]
    fixtures = [value for name, utc in [('quiet_june','2008-07-01T00:00:00'),('quiet_december','2008-12-15T00:00:00'),
        ('active_march','2001-03-20T00:00:00'),('active_october','2003-10-28T00:00:00'),('storm_october','2003-10-30T00:00:00')]
        for value in ['--fixture',name+'='+utc]]
    for variant, build in [('original',args.original_build.resolve()),('candidate',args.candidate_build.resolve())]:
        run(['taskset','-c',args.cpu,build/'atmosphere_experiment',*base,*fixtures,'--mode','fixtures','--out',out/f'fixtures-{variant}'])
        binary = 'optimisation_validation_original' if variant == 'original' else 'optimisation_validation'
        run(['taskset','-c',args.cpu,build/binary,*base,*cases,'--mode','propagate','--out',out/variant])
    load = lambda p: json.loads(p.read_text())
    historical = load(ROOT/'docs/atmosphere/evidence/direct-v2/fixtures/fixtures.json')['samples']
    if len(historical) != 10710:
        raise RuntimeError('frozen fixture count is not 10710')
    for variant in ['original','candidate']:
        equal('fixtures/'+variant, historical, load(out/f'fixtures-{variant}/fixtures.json')['samples'])
    for path in sorted((out/'original').glob('*.json')):
        original, candidate = load(path), load(out/'candidate'/path.name)
        equal('extended/'+path.stem, original, candidate)
        tag, method = path.stem.rsplit('_',1)
        frozen = load(ROOT/'docs/atmosphere/evidence/direct-v2/propagate/propagation'/f'{tag}.json')
        equal('frozen-checksum/'+path.stem, original['trajectory_checksum'], frozen['trajectory_checksum' if method == 'dopri' else 'refinement_trajectory_checksum'])
        if method == 'dopri':
            equal('frozen-series/'+tag, original['samples'], frozen['samples'])
        a, b = path.with_name(path.stem+'_native.bin'), out/'candidate'/f'{path.stem}_native.bin'
        error = compare_binary(a,b)
        comparisons['native/'+path.stem] = dict(bitwise_equal=error is None, first_difference=error,
            calls=original['native_calls'], fields_per_call=18, original_sha256=sha(a), candidate_sha256=sha(b))
        dump(out/'comparisons.json', comparisons)
        if error:
            raise RuntimeError('native/'+path.stem+': '+error)
    if any(sha(ROOT/p) != h for p,h in preserved.items()):
        raise RuntimeError('historical evidence changed')
    dump(out/'verification.json',dict(fixture_outputs=10710, full_series=18, frozen_nominal_series=9,
        frozen_refined_checksums=9, all_bitwise_equal=True, native_binary_format='18 native-endian binary64 values: MJD,SunRA,SunDec,SatRA,SatLat,SatAlt,F10,F10B,S10,S10B,M10,M10B,Y10,Y10B,DST,T_exo,T_local,rho',
        frozen_limit='Frozen refined evidence records checksums and numerical diagnostics, not complete refined series or native input streams. Complete original-source and candidate refined series and actual native streams are compared byte-for-byte.',
        test_environment={k:v for k,v in env.items() if k.startswith('LEO_')}, historical_hashes=preserved))
    for path in list(out.rglob('*')):
        if path.is_file() and (path.suffix == '.bin' or path.stat().st_size > 2*1024*1024):
            with path.open('rb') as src, gzip.GzipFile(filename=str(path)+'.gz',mode='wb',mtime=0) as dst:
                shutil.copyfileobj(src,dst)
            path.unlink()
    dump(out/'output-hashes.json',{str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file()})
    print('BITWISE VERIFIED',out,flush=True)


if __name__ == '__main__':
    main()
