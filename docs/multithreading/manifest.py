#!/usr/bin/env python3
"""Fingerprint actual MT-001 inputs, tools, outputs and immutable prior evidence."""
import argparse
import datetime
import hashlib
import json
import os
import re
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tree(path):
    files = {str(p.relative_to(path)): sha(p) for p in sorted(path.rglob('*'))
             if p.is_file() and '__pycache__' not in str(p)}
    return {'path': str(path), 'files': len(files),
            'canonical_file_hashes_sha256': hashlib.sha256(
                json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest()}


def command(args):
    result = subprocess.run(args, capture_output=True, text=True)
    return {'command': args, 'returncode': result.returncode,
            'stdout': result.stdout, 'stderr': result.stderr}


def ensure_indexes(evidence):
    """Build fresh-run indexes from actual accepted runs, never a prior success."""
    documents, counts = [], {}
    numeric = ('rtol', 'position_atol_m', 'velocity_atol_m_s', 'quaternion_atol',
               'omega_atol_rad_s', 'initial_step_s', 'max_step_s', 'max_attempts')
    for name in ('validation-long/correctness.json', 'validation-wide/correctness.json'):
        data = json.loads((evidence / name).read_text())
        if data['summary']['failed'] or not data['summary']['bitwise_equal_everywhere']:
            raise RuntimeError(f'failed scientific acceptance: {name}')
        for comparison in data['comparisons']:
            workload = comparison['workload']
            counts[workload] = counts.get(workload, 0) + 1
        documents.append({'file': name, 'sha256': sha(evidence / name), 'summary': data['summary'],
                          'duration_s': data['duration_s'],
                          'integration_options': {key: data[key] for key in numeric},
                          'validation_settings': data['validation_settings']})
    regressions = json.loads((evidence / 'regressions-command.json').read_text())
    match = re.search(r'100% tests passed.*out of (\d+)', regressions['stdout'])
    if regressions['returncode'] or not match:
        raise RuntimeError('a successful complete regression command record is required')
    total = sum(item['summary']['comparisons'] for item in documents)
    index = {'kind': 'MT-001 correctness index',
             'summary': {'comparisons': total, 'passed': total, 'failed': 0,
                         'mismatching_values': 0, 'bitwise_equal_everywhere': True},
             'by_workload': counts, 'scientific_documents': documents,
             'regressions': {'test_count': int(match[1]), 'passed': int(match[1]),
                             'command_file': 'regressions-command.json'}}
    target = evidence / 'correctness.json'
    if not target.exists():
        target.write_text(json.dumps(index, indent=2) + '\n')
    else:
        existing = json.loads(target.read_text())
        if existing['summary'] != index['summary'] or any(
                item['sha256'] != sha(evidence / item['file']) for item in existing['scientific_documents']):
            raise RuntimeError('existing correctness index does not match current accepted runs')
    baseline = json.loads((ROOT / 'docs/multithreading/evidence/constraints.json').read_text())
    for name, hashes in baseline['core_source_hashes'].items():
        actual = sha(ROOT / name)
        if actual != hashes['starting_sha256']:
            raise RuntimeError(f'existing scientific source changed: {name}')
        hashes['current_sha256'] = actual
    baseline['existing_core_source_files_modified'] = []
    target = evidence / 'constraints.json'
    if not target.exists():
        baseline['numeric_acceptance'] = f'{total} scientific comparisons passed with binary64 equality.'
        target.write_text(json.dumps(baseline, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--deps', type=Path, default=Path('/tmp/leo-atmosphere-deps'))
    parser.add_argument('--prefix', type=Path, default=Path('/tmp/leo-deps/usr'))
    parser.add_argument('--fortran', default='/tmp/leo-fortran/usr/bin/gfortran-13')
    parser.add_argument('--regression-build', type=Path, default=ROOT / 'build-atmosphere-gcc')
    parser.add_argument('--cspice', type=Path, default=Path('/tmp/leo_cspice/cspice'))
    args = parser.parse_args()
    historical = json.loads(args.baseline.read_text())
    changes = [name for name, expected in historical.items()
               if not (ROOT / name).is_file() or sha(ROOT / name) != expected]
    if changes:
        raise RuntimeError(f'frozen historical files changed: {changes}')
    ensure_indexes(args.evidence)
    source_paths = []
    for folder in ('include', 'src', 'cmake', 'tests', 'docs/multithreading'):
        source_paths += [p for p in (ROOT / folder).rglob('*') if p.is_file()
                         and 'evidence' not in p.relative_to(ROOT).parts
                         and '__pycache__' not in p.parts]
    source_paths += [ROOT / 'CMakeLists.txt', ROOT / 'README.md', ROOT / 'docs/atmosphere/experiment.cpp',
                     ROOT / 'benchmarks/cases/bench_support.hpp',
                     ROOT / 'docs/numerical-audit/experiments/scenario.hpp']
    source_paths += list((ROOT / 'docs/numerical-audit/experiments').glob('CASE-*-v1.cfg'))
    dependencies = [args.deps / name for name in (
        'naif0012.tls', 'de432s.bsp', 'earth_000101_270105_261009.bpc', 'pck00011.tpc',
        'SpaceWeather-All-v1.2_snapshot_20200224.txt', 'SOLFSMY.TXT', 'DTCFILE.TXT',
        'nrlmsise-00.c', 'nrlmsise-00_data.c', 'nrlmsise-00.h', 'JB2008.f')]
    dependencies += [args.cspice / 'lib/cspice.a']
    dependencies += [args.deps / 'NRLMSISE-DOCUMENTATION']
    dependencies += [p for p in sorted((args.deps / 'jb-validation').rglob('*')) if p.is_file()]
    binaries = [p for p in args.build.glob('mt_*') if p.is_file()]
    binaries += [p for p in args.regression_build.glob('leo_*tests') if p.is_file()]
    linked_libraries = [command(['ldd', str(p)]) for p in binaries]
    for linked in linked_libraries:
        for line in linked['stdout'].splitlines():
            for word in line.split():
                if word.startswith('/') and Path(word).is_file():
                    dependencies.append(Path(word).resolve())
    commands = [json.loads(p.read_text()) for p in sorted(args.evidence.glob('*-command.json'))]
    cache = args.build / 'CMakeCache.txt'
    scientific_metadata = []
    for name in ('validation-long/correctness.json', 'validation-wide/correctness.json'):
        data = json.loads((args.evidence / name).read_text())
        scientific_metadata.append({'file': name, **{key: value for key, value in data.items() if key != 'comparisons'}})
    benchmark_metadata = []
    for name in ('benchmarks.json', 'confirmation.json', 'diagnostics.json', 'startup.json'):
        data = json.loads((args.evidence / name).read_text())
        benchmark_metadata.append({'file': name, **{key: value for key, value in data.items() if key != 'runs'}})
    manifest = {
        'kind': 'MT-001', 'generated_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'starting_git_sha': 'cfcf105da91d385e9748e87b69c1820c73f111f5',
        'source_revision': command(['git', 'rev-parse', 'HEAD']),
        'commit_history': command(['git', 'log', '-4', '--format=%H %s']),
        'manifest_lifecycle': 'Generated before documentation commit; source hashes identify the exact working sources. Manifest omits its own hash to avoid self-reference.',
        'manifest_generation_command': [sys.executable, *sys.argv],
        'source_hashes': {str(p.relative_to(ROOT)): sha(p) for p in sorted(set(source_paths))},
        'dependency_hashes': {str(p): sha(p) for p in dependencies},
        'header_trees': [tree(args.prefix / 'include/eigen3'), tree(args.prefix / 'include/boost'), tree(args.cspice / 'include')],
        'toolchain': [command([tool, '--version']) for tool in ('c++', 'cc', args.fortran, 'cmake', 'ninja', 'python3')],
        'compiler_binary_hashes': {tool: sha(Path(subprocess.check_output(['which', tool], text=True).strip()).resolve())
                                   for tool in ('c++', 'cc', 'cmake', 'ninja', 'python3')},
        'fortran_compiler_sha256': sha(Path(args.fortran).resolve()),
        'os': platform.platform(), 'uname': command(['uname', '-a']),
        'os_release': Path('/etc/os-release').read_text(),
        'wsl_environment': {key: os.environ.get(key) for key in ('WSL_DISTRO_NAME', 'WSL_INTEROP', 'WSLENV')},
        'cpu': command(['lscpu']), 'topology': command(['lscpu', '-e=CPU,CORE,SOCKET,ONLINE']),
        'affinity_at_manifest': sorted(os.sched_getaffinity(0)),
        'cpu_quota': Path('/sys/fs/cgroup/cpu.max').read_text().strip(),
        'build_cache': cache.read_text(), 'build_cache_sha256': sha(cache),
        'compile_commands_sha256': sha(args.build / 'compile_commands.json'),
        'regression_build_cache': (args.regression_build / 'CMakeCache.txt').read_text(),
        'regression_build_cache_sha256': sha(args.regression_build / 'CMakeCache.txt'),
        'diagnostic_source_hashes': {str(p): sha(p) for p in sorted((args.build / 'native-diagnostics').glob('*')) if p.is_file()},
        'binary_hashes': {str(p): sha(p) for p in binaries},
        'linked_libraries': linked_libraries,
        'commands': commands,
        'test_results': json.loads((args.evidence / 'correctness.json').read_text()),
        'scientific_configuration': scientific_metadata,
        'benchmark_configuration_and_execution_order': benchmark_metadata,
        'constraints': json.loads((args.evidence / 'constraints.json').read_text()),
        'historical_preservation': {'checked_files': len(historical), 'changed_files': changes,
                                    'baseline_hash_file': str(args.baseline), 'sha256': sha(args.baseline)},
        'output_hashes': {str(p.relative_to(args.evidence)): sha(p) for p in sorted(args.evidence.rglob('*'))
                          if p.is_file() and p.name != 'manifest.json'},
    }
    (args.evidence / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'MT-001 manifest: {len(source_paths)} source paths, {len(commands)} commands, {len(historical)} unchanged historical files')


if __name__ == '__main__':
    main()
