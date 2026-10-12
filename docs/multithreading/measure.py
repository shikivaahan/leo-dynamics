#!/usr/bin/env python3
"""Execute the fixed MT-001 measurement protocol sequentially, without interference."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, default=Path('build-multithreading'))
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--deps', type=Path, default=Path('/tmp/leo-atmosphere-deps'))
    parser.add_argument('--phase', choices=('all', 'matrix', 'confirmation', 'diagnostics', 'startup'), default='all')
    args = parser.parse_args()
    deps = args.deps
    common = ['--lsk', str(deps / 'naif0012.tls'), '--spk', str(deps / 'de432s.bsp'),
              '--pck', str(deps / 'earth_000101_270105_261009.bpc'), '--frame', 'ITRF93',
              '--weather', str(deps / 'SpaceWeather-All-v1.2_snapshot_20200224.txt'),
              '--solfsmy', str(deps / 'SOLFSMY.TXT'), '--dtcfile', str(deps / 'DTCFILE.TXT'),
              '--epoch-utc', '2003-10-30T00:00:00']
    scheduling = ['--benchmark-workers', '1,2,4,8', '--benchmark-policies', 'static,dynamic',
                  '--benchmark-variants', 'identical,perturbed', '--seed', '20261012']
    affinity = ['taskset', '-c', '0,2,4,6,8,10,12,14']
    def execute(label, command):
        recorder = [sys.executable, str(ROOT / 'docs/multithreading/run.py'),
                    '--evidence', str(args.evidence), '--label', label, '--']
        subprocess.run(recorder + command, cwd=ROOT, check=True)
    for name, duration, sizes, repetitions, cases, binary, output in (
        ('matrix', '300', '1,2,4,8,16,32,64', '5', 'A', 'mt_benchmark', 'benchmarks.json'),
        ('confirmation', '21600', '8', '5', 'AB', 'mt_benchmark', 'confirmation.json'),
        ('diagnostics', '300', '8,32', '3', 'A', 'mt_benchmark_diagnostics', 'diagnostics.json'),
    ):
        if args.phase not in ('all', name):
            continue
        case_args = [item for case in cases for item in (
            '--case', str(ROOT / f'docs/numerical-audit/experiments/CASE-{case}-v1.cfg'))]
        execute(name, affinity + [str(args.build / binary), '--benchmark-out', str(args.evidence / output)]
                + common + case_args + scheduling
                + ['--benchmark-duration', duration, '--benchmark-sizes', sizes, '--repetitions', repetitions])
    if args.phase in ('all', 'startup'):
        execute('startup', affinity + [str(args.build / 'mt_startup'), str(args.evidence / 'startup.json')])


if __name__ == '__main__':
    main()
