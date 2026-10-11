#!/usr/bin/env python3
"""Run the ATM-OPT-001 cleanup interleaved provider experiment.

Serial, CPU-pinned, no compilation during measurement. Emits machine-readable
raw evidence plus a reduction that (a) checks every configuration produced the
same scientific result and the same SPICE operation counts where the model
cannot consume the Sun, and (b) tests each timing difference against
batch-level variability with Welch intervals.
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
ORDER = ['off_off_off', 'utc_only', 'sun_only', 'orientation_only',
         'utc_orientation', 'utc_sun', 'sun_orientation', 'all']


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def load(path):
    return json.loads(Path(path).read_text())


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def fact(command):
    result = subprocess.run([str(part) for part in command], text=True, capture_output=True)
    return {'command': [str(part) for part in command], 'returncode': result.returncode,
            'stdout': result.stdout, 'stderr': result.stderr}


def welch_test(a, b):
    """Welch difference of means on per-repetition batch means, 95% normal interval."""
    if len(a) < 2 or len(b) < 2:
        return {'mean_difference_ns': 0.0, 't_statistic': 0.0, 'ci95_low_ns': 0.0, 'ci95_high_ns': 0.0}
    mean_a = sum(a) / len(a)
    mean_b = sum(b) / len(b)
    var_a = sum((v - mean_a) ** 2 for v in a) / (len(a) - 1)
    var_b = sum((v - mean_b) ** 2 for v in b) / (len(b) - 1)
    se = (var_a / len(a) + var_b / len(b)) ** 0.5
    difference = mean_a - mean_b
    if se == 0:
        return {'mean_difference_ns': difference, 't_statistic': 0.0,
                'ci95_low_ns': difference, 'ci95_high_ns': difference}
    return {'mean_difference_ns': difference, 't_statistic': difference / se,
            'ci95_low_ns': difference - 1.96 * se, 'ci95_high_ns': difference + 1.96 * se}


def summarise(data):
    rows = []
    for model, section in data['sections'].items():
        calls = data['repetitions'] * data['calls_per_batch']
        configurations = {row['variant']: row for row in section['configurations']}
        require(set(configurations) == set(ORDER),
                f'{model}: configuration set differs: {sorted(configurations)}')
        # Scientific results must be identical across every configuration.
        checksums = {row['result_checksum'] for row in section['configurations']}
        require(len(checksums) == 1, f'{model}: result checksums differ across configurations: {checksums}')
        per_call = {}
        for row in section['configurations']:
            counts = row['operation_counts']
            per_call[row['variant']] = {
                'mean_ns': row['per_call_ns']['mean'],
                'median_ns': row['per_call_ns']['median'],
                'p95_ns': row['per_call_ns']['p95'],
                'p99_ns': row['per_call_ns']['p99'],
                'batch_mean_sd_ns': row['batch_mean_ns']['stdev'],
                'calls': row['per_call_ns']['count'],
                'per_call_operations': {
                    'et2utc': counts['et2utc_calls'] / calls,
                    'str2et': counts['str2et_calls'] / calls,
                    'pxform': counts['pxform_calls'] / calls,
                    'spkez': counts['spkez_calls'] / calls},
            }
        # Batch means are retained per configuration so any pair can be compared.
        batch_means = {row['variant']: row['batch_mean_ns'].get('raw_nanoseconds', [])
                       for row in section['configurations']}
        # The anomaly test: for a model that cannot consume a Sun observation,
        # toggling sun_sharing must change neither operation counts nor results.
        sun_pairs = [('utc_only', 'utc_sun'), ('orientation_only', 'sun_orientation')]
        anomaly = {}
        for name, off, on in [(model, ) + pair for pair in sun_pairs for name in (model, )]:
            base, other = per_call[off], per_call[on]
            same_counts = all(abs(base['per_call_operations'][key] - other['per_call_operations'][key]) < 1e-9
                              for key in base['per_call_operations'])
            welch = welch_test(batch_means.get(off, []), batch_means.get(on, []))
            ci = (welch['ci95_low_ns'], welch['ci95_high_ns'])
            distinguishable = not (ci[0] <= 0 <= ci[1])
            anomaly[f'{off}_vs_{on}'] = {
                'identical_operation_counts': same_counts,
                'spkez_calls_per_call': [base['per_call_operations']['spkez'], other['per_call_operations']['spkez']],
                'mean_difference_ns': welch['mean_difference_ns'],
                't_statistic': welch['t_statistic'],
                'ci95_ns': list(ci),
                'difference_distinguishable_from_variability': distinguishable,
            }
        rows.append({'model': model, 'result_checksum': next(iter(checksums)),
                     'all_configurations_bitwise_identical': True,
                     'per_configuration': per_call, 'sun_toggle_test': anomaly})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-candidate', type=Path, default=ROOT/'build-atmosphere-optimisation')
    parser.add_argument('--build-original', type=Path, default=ROOT/'build-atmosphere-optimisation-original')
    parser.add_argument('--deps', type=Path, default=Path('/tmp/leo-atmosphere-deps'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=20251011)
    parser.add_argument('--repetitions', type=int, default=12)
    parser.add_argument('--calls', type=int, default=2000)
    parser.add_argument('--warmup', type=int, default=256)
    parser.add_argument('--models', default='nrlmsise00,jb2008')
    parser.add_argument('--cpu', type=int, default=0)
    args = parser.parse_args()
    out = args.out.resolve()
    require(not out.exists() or not any(out.iterdir()), 'refusing populated evidence directory')
    out.mkdir(parents=True, exist_ok=True)
    require(args.cpu in os.sched_getaffinity(0), 'single CPU not permitted')
    deps = args.deps.resolve()
    commands = []

    def run(command):
        command = [str(part) for part in command]
        print('RUN', ' '.join(command), flush=True)
        started = datetime.datetime.now(datetime.timezone.utc).isoformat()
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        commands.append({'command': command, 'started_utc': started, 'returncode': result.returncode,
                         'stdout': result.stdout, 'stderr': result.stderr})
        dump(out/'command-log.json', commands)
        if result.returncode:
            raise RuntimeError(result.stdout + '\n' + result.stderr)
        return result.stdout

    base = ['--lsk', deps/'naif0012.tls', '--spk', deps/'de432s.bsp',
            '--pck', deps/'earth_000101_270105_261009.bpc', '--frame', 'ITRF93',
            '--weather', deps/'SpaceWeather-All-v1.2_snapshot_20200224.txt',
            '--solfsmy', deps/'SOLFSMY.TXT', '--dtcfile', deps/'DTCFILE.TXT',
            '--epoch-utc', '2003-10-30T00:00:00', '--seed', args.seed,
            '--repetitions', args.repetitions, '--calls', args.calls,
            '--warmup', args.warmup, '--models', args.models]
    binaries = {'original': args.build_original.resolve()/'atm_opt_interleave_original',
                'candidate': args.build_candidate.resolve()/'atm_opt_interleave',
                'diagnostics': args.build_candidate.resolve()/'atm_opt_interleave_diagnostics'}
    for label, binary in binaries.items():
        require(Path(binary).is_file(), f'{label} binary missing: {binary}; this runner never builds')
    raw = {}
    for label, binary in binaries.items():
        path = out/f'interleaved-{label}.json'
        run(['taskset', '-c', args.cpu, binary, *base, '--profile-out', path])
        raw[label] = load(path)
    summary = {}
    for label in ('candidate', 'diagnostics'):
        summary[label] = summarise(raw[label])
    original_summary = summarise(raw['original'])
    # Scientific equality against the unchanged original.
    verification = {'all_configurations_bitwise_identical': True}
    for label in ('candidate', 'diagnostics'):
        for candidate_row, original_row in zip(summary[label], original_summary):
            require(candidate_row['result_checksum'] == original_row['result_checksum'],
                    f'{label} {candidate_row["model"]}: result checksum differs from the original')
    verification['all_equal_to_unchanged_original'] = True
    verification['models'] = [row['model'] for row in summary['diagnostics']]
    nrl = next((row for row in summary['diagnostics'] if row['model'] == 'nrlmsise00'), None)
    if nrl:
        identical = all(entry['identical_operation_counts']
                        for entry in nrl['sun_toggle_test'].values())
        spkez = max(entry['spkez_calls_per_call'][0] for entry in nrl['sun_toggle_test'].values())
        verification['nrlmsise00_sun_toggle_identical_operation_counts'] = identical
        verification['nrlmsise00_spkez_calls_per_call'] = spkez
        verification['nrlmsise00_sun_toggle_verdict'] = (
            'NRLMSISE-00 performs no Sun lookup in any configuration, so sun_sharing changes no '
            'operation and no result; every timing difference is scheduling/variability only')
    dump(out/'verification.json', verification)
    dump(out/'summary.json', {'kind': 'atm-opt-001-interleaved-summary',
                              'protocol': raw['candidate']['protocol'],
                              'seed': args.seed, 'repetitions': args.repetitions,
                              'calls_per_batch': args.calls, 'warmup': args.warmup,
                              'models': summary})
    lines = ['# ATM-OPT-001 cleanup interleaved provider timing', '',
             'Configurations are executed round-robin inside each repetition after a full warm-up,',
             'with the per-repetition order recorded. Welch 95% intervals compare each configuration',
             'against the all-off control on per-repetition batch means.', '',
             '| model | variant | mean ns | median ns | p95 ns | p99 ns | batch SD ns | '
             'et2utc/call | str2et/call | pxform/call | spkez/call |', '|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    for row in summary['diagnostics']:
        for variant in ORDER:
            entry = row['per_configuration'][variant]
            ops = entry['per_call_operations']
            lines.append(f"| {row['model']} | {variant} | {entry['mean_ns']:.1f} | {entry['median_ns']:.1f} | "
                         f"{entry['p95_ns']:.1f} | {entry['p99_ns']:.1f} | {entry['batch_mean_sd_ns']:.1f} | "
                         f"{ops['et2utc']:.2f} | {ops['str2et']:.2f} | {ops['pxform']:.2f} | {ops['spkez']:.2f} |")
    lines += ['', '## Sun-toggle test', '', '```json', json.dumps(nrl['sun_toggle_test'] if nrl else {}, indent=2), '```', '',
              '## Verification', '', '```json', json.dumps(verification, indent=2), '```', '']
    (out/'summary.md').write_text('\n'.join(lines))
    manifest = {
        'kind': 'ATM-OPT-001 cleanup interleaved experiment',
        'generated_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'git_sha': fact(['git', 'rev-parse', 'HEAD'])['stdout'].strip(),
        'dirty_status': fact(['git', 'status', '--porcelain=v1'])['stdout'],
        'seed': args.seed, 'repetitions': args.repetitions, 'calls_per_batch': args.calls,
        'warmup': args.warmup, 'models': args.models, 'single_cpu': args.cpu,
        'affinity_mask': sorted(os.sched_getaffinity(0)),
        'host': {'cpu': fact(['lscpu']), 'os': platform.platform(),
                 'compiler': fact(['c++', '--version']), 'cmake': fact(['cmake', '--version']),
                 'python': sys.version, 'load_average': os.getloadavg(),
                 'governors': {str(q): q.read_text().strip() for q in
                               Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_governor')}},
        'dependency_hashes': {str(deps/name): sha(deps/name) for name in
                              ['naif0012.tls', 'de432s.bsp', 'earth_000101_270105_261009.bpc',
                               'SpaceWeather-All-v1.2_snapshot_20200224.txt', 'SOLFSMY.TXT', 'DTCFILE.TXT']},
        'tool_hashes': {q.name: sha(q) for q in (ROOT/'docs/atmosphere/optimisation').iterdir()
                        if q.is_file() and q.suffix in {'.cpp', '.py', '.txt'}},
        'binaries': {label: sha(binary) for label, binary in binaries.items()},
        'commands': commands, 'verification_file': 'verification.json',
        'limitations': 'WSL2 scheduling, physical-core placement and frequency are uncontrolled; '
                       'no perf counters are available; 12 repetitions support descriptive '
                       'distributions, not a precise population p99.',
        'previous_evidence_preserved': 'docs/atmosphere/optimisation/evidence/measurements, '
                                       'evidence/validation and evidence/controls are unmodified',
    }
    dump(out/'manifest.json', manifest)
    for path in list(out.rglob('*')):
        if path.is_file() and path.suffix == '.json' and path.stat().st_size > 2 * 1024 * 1024:
            with path.open('rb') as src, gzip.GzipFile(filename=str(path)+'.gz', mode='wb', mtime=0) as dst:
                shutil.copyfileobj(src, dst)
            path.unlink()
    manifest['output_hashes'] = {str(q.relative_to(out)): sha(q) for q in sorted(out.rglob('*'))
                                 if q.is_file() and q.name != 'manifest.json'}
    dump(out/'manifest.json', manifest)
    print('INTERLEAVED EXPERIMENT VERIFIED', out, flush=True)


if __name__ == '__main__':
    main()