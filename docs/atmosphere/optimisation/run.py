#!/usr/bin/env python3
"""Run the ATM-OPT-001 optimisation benchmark serially and hash the evidence.

The runner never builds, never tests and never touches production sources or
frozen evidence. It executes the already-built benchmark binaries, verifies that
every configuration produces bitwise identical science, refuses to overwrite an
existing evidence directory, and records commands, environment, dependency and
kernel-data hashes, preserved-evidence hashes and raw results.
"""
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
ORIGINAL_VARIANT = 'original'
CANDIDATE_VARIANTS = 'off_off_off,utc_only,sun_only,orientation_only,utc_sun,utc_orientation,sun_orientation,all'
CONCURRENCY_VARIANTS = CANDIDATE_VARIANTS
PROPAGATE_VARIANTS = CANDIDATE_VARIANTS
DEPENDENCY_NAMES = ['naif0012.tls', 'de432s.bsp', 'earth_000101_270105_261009.bpc', 'pck00011.tpc',
                    'SpaceWeather-All-v1.2_snapshot_20200224.txt', 'SOLFSMY.TXT', 'DTCFILE.TXT',
                    'nrlmsise-00.c', 'nrlmsise-00_data.c', 'nrlmsise-00.h', 'JB2008.f']
PRESERVED_EVIDENCE = ['direct-v1', 'direct-v2', 'validation-amendment-v1']


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def load(path):
    return json.loads(Path(path).read_text())


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def fact(command):
    r = subprocess.run([str(part) for part in command], text=True, capture_output=True)
    return {'command': [str(part) for part in command], 'returncode': r.returncode,
            'stdout': r.stdout, 'stderr': r.stderr}


def initial_environment():
    return {
        'git_sha': fact(['git', 'rev-parse', 'HEAD'])['stdout'].strip(),
        'dirty_status': fact(['git', 'status', '--porcelain=v1'])['stdout'],
        'load_before': os.getloadavg(),
        'affinity_before': sorted(os.sched_getaffinity(0)),
        'cpu': fact(['lscpu']), 'os': platform.platform(), 'compiler': fact(['c++', '--version']),
        'cmake': fact(['cmake', '--version']), 'python': sys.version,
        'perf': fact(['sh', '-c', 'command -v perf']),
        'governors': {str(q): q.read_text().strip()
                      for q in Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_governor')},
        'frequency': {str(q): q.read_text().strip()
                      for q in Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_cur_freq')},
    }


def tree_hashes(base, patterns=('*',)):
    files = []
    for pattern in patterns:
        files.extend(q for q in Path(base).rglob(pattern) if q.is_file())
    return {str(q.relative_to(base)): sha(q) for q in sorted(set(files))}
def scientific_source_hashes(root):
    result={}
    for part in ('src','include'):
        result.update({part+'/'+p:h for p,h in tree_hashes(root/part).items()})
    for relative in ['docs/atmosphere/experiment.cpp','benchmarks/cases/bench_support.hpp',
                     'docs/numerical-audit/experiments/scenario.hpp',
                     *[f'docs/numerical-audit/experiments/CASE-{c}-v1.cfg' for c in 'ABC']]:
        result[relative]=sha(root/relative)
    return result



def scientific_arguments(args, case_files):
    command = ['--lsk', args.deps / 'naif0012.tls', '--spk', args.deps / 'de432s.bsp',
               '--pck', args.deps / 'earth_000101_270105_261009.bpc', '--frame', 'ITRF93',
               '--weather', args.deps / 'SpaceWeather-All-v1.2_snapshot_20200224.txt',
               '--solfsmy', args.deps / 'SOLFSMY.TXT', '--dtcfile', args.deps / 'DTCFILE.TXT',
               '--epoch-utc', args.epoch_utc, '--duration', str(args.duration),
               '--cadence', str(args.cadence), '--rtol', str(args.rtol),
               '--max-step', str(args.max_step)]
    for case in case_files:
        command += ['--case', case]
    return command


def concurrency_key(row):
    return (row['model'], row['threads'])


def batch_checksums(section):
    """First-batch result checksum per case and variant."""
    result = {}
    for row in section:
        key = (row['model'],) if 'schedule' not in row else (row['schedule'], row['model'])
        checksums = {batch['result_checksum'] for batch in row['batches']}
        require(len(checksums) == 1, 'inconsistent scientific batches: '+str(key)+'/'+row['variant'])
        result.setdefault(key, {})[row['variant']] = next(iter(checksums))
    return result

def compare_science(original, candidate, diagnostics, verification):
    """Every configuration must reproduce the unchanged original bit for bit."""
    for name in ('provider', 'locality'):
        tables = {}
        for label, data in (('original', original), ('candidate', candidate),
                            ('diagnostics', diagnostics)):
            if name in data['sections']:
                tables[label] = batch_checksums(data['sections'][name])
        require('original' in tables, f'no original {name} results to compare against')
        for label, table in tables.items():
            require(set(table) == set(tables['original']),
                    f'{label} {name} covers different cases than the original')
            for key, variants in table.items():
                distinct = set(variants.values())
                require(len(distinct) == 1, f'{label} {name} variants disagree at {key}')
                reference = next(iter(set(tables['original'][key].values())))
                for variant, checksum in variants.items():
                    require(checksum == reference,
                            f'{label} {name} result differs from the original at {key} '
                            f'(variant {variant})')
        verification[f'{name}_bitwise_equal_to_original'] = True

    if original['sections'].get('concurrency') and candidate['sections'].get('concurrency'):
        original_rows = {concurrency_key(row): row for row in original['sections']['concurrency']}
        for label, data in (('candidate', candidate), ('diagnostics', diagnostics)):
            for row in data['sections'].get('concurrency', []):
                reference = original_rows.get(concurrency_key(row))
                require(reference is not None, f'{label} concurrency row has no original counterpart')
                require(sorted(row['per_worker_checksums']) == sorted(reference['per_worker_checksums']),
                        f'{label} concurrency results differ from the original')
        verification['concurrency_bitwise_equal_to_original'] = True

    if original['sections'].get('propagate'):
        original_rows = {(row['case_id'], row['model']): row
                         for row in original['sections']['propagate']}
        for label, data in (('candidate', candidate), ('diagnostics', diagnostics)):
            for row in data['sections'].get('propagate', []):
                reference = original_rows[(row['case_id'], row['model'])]
                for rep, base in zip(row['repetitions'], reference['repetitions']):
                    require(rep['trajectory_checksum'] == base['trajectory_checksum'],
                            f'{label} trajectory differs from the original')
                    require(rep['rhs_calls'] == base['rhs_calls'],
                            f'{label} RHS call count differs')
                    require(rep['density_checksum'] == base['density_checksum'],
                            f'{label} density series differs from the original')
                require(row['repetitions_bitwise_identical'],
                        f'{label} repeated propagation runs are not bitwise identical')
        verification['propagate_bitwise_equal_to_original'] = True
        verification['propagate_checksums'] = {
            f'{case}_{model}': row['repetitions'][0]['trajectory_checksum']
            for (case, model), row in sorted(original_rows.items())}


def cspice_library(build_dir):
    """The exact CSPICE archive the benchmark binaries linked, resolved from the build cache."""
    cache = Path(build_dir) / 'CMakeCache.txt'
    resolved = None
    if cache.is_file():
        for line in cache.read_text(errors='replace').splitlines():
            if line.startswith('ATM_OPT_CSPICE_LIBRARY:'):
                resolved = line.split('=', 1)[-1]
                break
    if not resolved or not Path(resolved).is_file():
        resolved = '/tmp/leo_cspice/cspice/lib/cspice.a'
    return {'path': resolved, 'sha256': sha(Path(resolved))}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-candidate', type=Path, default=ROOT / 'build-atmosphere-optimisation')
    p.add_argument('--build-original', type=Path,
                   default=ROOT / 'build-atmosphere-optimisation-original')
    p.add_argument('--original-source', type=Path, default=Path('/tmp/atm-opt-original'))
    p.add_argument('--deps', type=Path, default=Path('/tmp/leo-atmosphere-deps'))
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--epoch-utc', default='2003-10-30T00:00:00')
    p.add_argument('--duration', type=float, default=21600)
    p.add_argument('--cadence', type=float, default=30)
    p.add_argument('--rtol', default='1e-10')
    p.add_argument('--max-step', type=float, default=15)
    p.add_argument('--batches', type=int, default=5)
    p.add_argument('--locality-batches', type=int, default=3)
    p.add_argument('--calls', type=int, default=2000)
    p.add_argument('--warmup', type=int, default=256)
    p.add_argument('--propagate-repeats', type=int, default=3)
    p.add_argument('--thread-counts', default='1,2,4')
    p.add_argument('--single-cpu', type=int, default=0)
    p.add_argument('--concurrency-cpus', default='0-3')
    args = p.parse_args()

    out = args.out.resolve()
    build_candidate = args.build_candidate.resolve()
    build_original = args.build_original.resolve()
    deps = args.deps.resolve()
    require(not out.exists() or not any(out.iterdir()), 'refusing populated evidence directory')
    out.mkdir(parents=True, exist_ok=True)
    require(args.single_cpu in os.sched_getaffinity(0), 'single CPU not permitted')

    # Hashed before anything runs and compared afterwards: production sources,
    # the frozen profiling tooling and the historical evidence must not move.
    preserved_before = {str(q.relative_to(ROOT)): sha(q) for name in PRESERVED_EVIDENCE
                        for q in (ROOT / 'docs/atmosphere/evidence' / name).rglob('*') if q.is_file()}
    profiling_before = tree_hashes(ROOT / 'docs/atmosphere/profiling')
    sources_before = scientific_source_hashes(ROOT)
    environment = initial_environment()
    dump(out / 'initial-environment.json', environment)

    commands = []

    def run(command):
        command = [str(part) for part in command]
        print('RUN', ' '.join(command), flush=True)
        started = datetime.datetime.now(datetime.timezone.utc).isoformat()
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        commands.append({'command': command, 'started_utc': started, 'returncode': result.returncode,
                         'stdout': result.stdout, 'stderr': result.stderr})
        dump(out / 'command-log.json', commands)
        if result.returncode:
            raise RuntimeError(f'command failed ({result.returncode}): {command}\n'
                               f'{result.stderr}\n{result.stdout}')
        return result.stdout

    case_files = [str(ROOT / 'docs/numerical-audit/experiments' / f'CASE-{case}-v1.cfg') for case in 'ABC']
    base = scientific_arguments(args, case_files)
    schedule = ['--batches', str(args.batches), '--locality-batches', str(args.locality_batches),
                '--calls', str(args.calls), '--warmup', str(args.warmup),
                '--propagate-repeats', str(args.propagate_repeats),
                '--thread-counts', args.thread_counts]

    binaries = {
        'original': (build_original / 'atm_opt_benchmark_original',
                     ['--variants', ORIGINAL_VARIANT, '--concurrency-variants', ORIGINAL_VARIANT,
                      '--propagate-variants', ORIGINAL_VARIANT]),
        'original_diagnostics': (build_original/'atm_opt_benchmark_original_diagnostics',
                     ['--variants', ORIGINAL_VARIANT,'--concurrency-variants',ORIGINAL_VARIANT,
                      '--propagate-variants',ORIGINAL_VARIANT]),
        'candidate': (build_candidate / 'atm_opt_benchmark',
                      ['--variants', CANDIDATE_VARIANTS, '--concurrency-variants', CONCURRENCY_VARIANTS,
                       '--propagate-variants', PROPAGATE_VARIANTS]),
        # Separate instrumented processes attribute real propagation call counts
        # for every independently measurable configuration, never timings used
        # for the production speedup.
        'diagnostics': (build_candidate / 'atm_opt_benchmark_diagnostics',
                        ['--variants', CANDIDATE_VARIANTS, '--concurrency-variants', CONCURRENCY_VARIANTS,
                         '--propagate-variants', PROPAGATE_VARIANTS]),
    }
    for label, (binary, _) in binaries.items():
        require(Path(binary).is_file(), f'{label} benchmark binary is missing: {binary}\n'
                                        'build it first; this runner never builds')

    # Sections are split by affinity: everything except the concurrency sweep
    # runs pinned to one CPU, the thread sweep runs on the shared mask.
    core_modes = {'original': 'provider,locality,propagate,micro',
                  'candidate': 'provider,locality,propagate,micro,replay,invalidate',
                  'diagnostics': 'provider,locality,propagate',
                  'original_diagnostics': 'provider,locality,propagate'}
    threads_modes = 'concurrency'
    results = {}
    for label, (binary, variant_args) in binaries.items():
        core = out / f'benchmark-{label}.json'
        threads = out / f'benchmark-{label}-concurrency.json'
        run(['taskset', '-c', str(args.single_cpu), binary, *base, *schedule, *variant_args,
             '--modes', core_modes[label], '--label', label, '--profile-out', core])
        run(['taskset', '-c', args.concurrency_cpus, binary, *base, *schedule, *variant_args,
             '--modes', threads_modes, '--label', label + '-concurrency', '--profile-out', threads])
        data = load(core)
        data['sections'].update(load(threads)['sections'])
        data['settings']['concurrency_file'] = threads.name
        dump(core, data)
        results[label] = data

    verification = {}
    compare_science(results['original'], results['candidate'], results['diagnostics'], verification)
    original_diagnostic_verification={}
    compare_science(results['original'],results['original_diagnostics'],results['original_diagnostics'],
                    original_diagnostic_verification)
    verification['original_diagnostics']=original_diagnostic_verification
    invalidate = results['candidate']['sections'].get('invalidate', {})
    if invalidate.get('available'):
        require(invalidate['results_identical_across_invalidation'],
                'results changed across an explicit invalidation')
        require(invalidate['cross_provider_compatible_sun'] is True,
                'a same-configuration observation was rejected by its own provider')
        require(invalidate['second_provider_compatible_sun'] is True,
                'a same-configuration observation was rejected across providers')
        require(invalidate['wrong_epoch_compatible_sun'] is False,
                'an observation was accepted for a mismatched epoch')
        verification['invalidation'] = {
            'results_identical_across_invalidation': True,
            'cross_provider_compatible_sun': True,
            'wrong_epoch_compatible_sun': False,
            'invalidate_reuse_ns_mean': invalidate['invalidate_reuse_ns']['mean'],
            'invalidate_reuse_ns_p99': invalidate['invalidate_reuse_ns']['p99'],
        }
    else:
        verification['invalidation'] = {'available': False,
                                        'reason': invalidate.get('reason', 'not exercised')}
    # Nothing may have moved since the hashes were taken before the first run.
    preserved_after = {str(q.relative_to(ROOT)): sha(q) for name in PRESERVED_EVIDENCE
                       for q in (ROOT / 'docs/atmosphere/evidence' / name).rglob('*') if q.is_file()}
    profiling_after = tree_hashes(ROOT / 'docs/atmosphere/profiling')
    sources_after = scientific_source_hashes(ROOT)
    require(preserved_after == preserved_before, 'historical evidence changed during the run')
    require(profiling_after == profiling_before, 'frozen profiling tooling changed during the run')
    require(sources_after == sources_before, 'production source changed during the run')
    verification['preserved_evidence_unchanged'] = True
    verification['profiling_unchanged'] = True
    verification['production_sources_unchanged'] = True
    dump(out / 'verification.json', verification)
    dump(out / 'source-hashes.json', {'preserved_evidence': preserved_before,
                                      'profiling': profiling_before, 'production': sources_before})

    # Raw distributions stay in the evidence, gzipped rather than discarded.
    for path in sorted(out.glob('*.json')):
        if path.stat().st_size > 2 * 1024 * 1024:
            with path.open('rb') as src, gzip.GzipFile(filename=str(path) + '.gz', mode='wb', mtime=0) as dst:
                shutil.copyfileobj(src, dst)
            path.unlink()

    dependencies = {str(deps / name): sha(deps / name) for name in DEPENDENCY_NAMES
                    if (deps / name).exists()}
    tools = {q.name:sha(q) for q in (ROOT/'docs/atmosphere/optimisation').iterdir()
             if q.is_file() and q.suffix in {'.cpp','.hpp','.py','.txt'}}
    original_source = scientific_source_hashes(args.original_source)
    manifest = {
        'kind': 'ATM-OPT-001', 'generated_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'scientific_baseline': 'direct-v2 frozen evidence; not rewritten by this task',
        'historical_profiling': 'docs/atmosphere/profiling, read-only and unchanged',
        'source': environment, 'original_source_tree': str(args.original_source),
        'original_source_sha': fact(['git', '-C', str(args.original_source), 'rev-parse', 'HEAD']),
        'original_source_hashes': original_source,
        'tool_hashes': tools, 'dependency_hashes': dependencies,
        'cspice_library': cspice_library(build_candidate),
        'spice_toolkit_version': results['candidate']['spice_toolkit_version'],
        'builds': {
            'candidate': {'directory': str(build_candidate),
                          'cache_sha256': sha(build_candidate / 'CMakeCache.txt'),
                          'binaries': {name: sha(binary) for name, binary in
                                       (('atm_opt_benchmark', binaries['candidate'][0]),
                                        ('atm_opt_benchmark_diagnostics', binaries['diagnostics'][0]))}},
            'original': {'directory': str(build_original),
                         'cache_sha256': sha(build_original / 'CMakeCache.txt'),
                         'binaries': {'atm_opt_benchmark_original': sha(binaries['original'][0]),
                                      'atm_opt_benchmark_original_diagnostics':sha(binaries['original_diagnostics'][0])}},
        },
        'settings': {'frame': 'ITRF93', 'epoch_utc': args.epoch_utc, 'duration_s': args.duration,
                     'cadence_s': args.cadence, 'rtol': args.rtol, 'max_step_s': args.max_step,
                     'batches': args.batches, 'locality_batches': args.locality_batches,
                     'calls_per_batch': args.calls, 'warmup': args.warmup,
                     'propagate_repeats': args.propagate_repeats,
                     'thread_counts': [int(v) for v in args.thread_counts.split(',') if v],
                     'single_cpu': args.single_cpu, 'concurrency_cpu_mask': args.concurrency_cpus},
        'commands': commands, 'verification_file': 'verification.json',
        'load_after': os.getloadavg(),
        'output_hashes': {str(q.relative_to(out)): sha(q) for q in sorted(out.rglob('*'))
                          if q.is_file() and q.name != 'manifest.json'},
    }
    # Reduction first, so a failed analysis can never be published as verified,
    # and so the summary files are covered by the manifest hashes.
    import analyse
    analyse.analyse(out)
    manifest['output_hashes'] = {str(q.relative_to(out)): sha(q) for q in sorted(out.rglob('*'))
                                 if q.is_file() and q.name != 'manifest.json'}
    dump(out / 'manifest.json', manifest)
    print('VERIFIED AND HASHED', out, flush=True)


if __name__ == '__main__':
    main()