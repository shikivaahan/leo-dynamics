#!/usr/bin/env python3
"""Run one explicit command and retain its exact provenance and output."""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--test-environment', action='store_true')
    parser.add_argument('--deps', type=Path, default=Path('/tmp/leo-atmosphere-deps'))
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('an explicit command is required after --')
    args.evidence.mkdir(parents=True, exist_ok=True)
    log = args.evidence / (args.label + '-command.json')
    if log.exists():
        parser.error(f'refusing to overwrite {log}')
    environment = os.environ.copy()
    overrides = {}
    if args.test_environment:
        overrides = {key: str(args.deps / file) for key, file in {
            'LEO_SPICE_TEST_SPK': 'de432s.bsp',
            'LEO_SPICE_TEST_LSK': 'naif0012.tls',
            'LEO_SPICE_TEST_TEXT_PCK': 'pck00011.tpc',
            'LEO_ATMOSPHERE_TEST_PCK': 'earth_000101_270105_261009.bpc',
            'LEO_ATMOSPHERE_TEST_LOW_PRECISION_PCK': 'pck00011.tpc',
            'LEO_NRLMSISE00_TEST_CSSI': 'SpaceWeather-All-v1.2_snapshot_20200224.txt',
            'LEO_NRLMSISE00_REFERENCE_DOCUMENTATION': 'NRLMSISE-DOCUMENTATION',
            'LEO_JB2008_VALIDATION_DIR': 'jb-validation',
        }.items()}
        environment.update(overrides)
    source_sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    record = {'command': command, 'cwd': os.getcwd(), 'environment_overrides': overrides,
              'started_utc': started, 'source_sha': source_sha,
              'affinity': sorted(os.sched_getaffinity(0)), 'status': 'running'}
    log.write_text(json.dumps(record, indent=2) + '\n')
    stdout_path = args.evidence / (args.label + '-stdout.txt')
    stderr_path = args.evidence / (args.label + '-stderr.txt')
    before = time.monotonic()
    try:
        with stdout_path.open('w') as out, stderr_path.open('w') as err:
            result = subprocess.run(command, env=environment, stdout=out, stderr=err, text=True)
        stdout, stderr = stdout_path.read_text(), stderr_path.read_text()
    except OSError as error:
        result = subprocess.CompletedProcess(command, 127)
        stdout, stderr = '', str(error) + '\n'
    record.update({'wall_s': time.monotonic() - before, 'returncode': result.returncode,
                   'status': 'completed', 'stdout': stdout, 'stderr': stderr})
    log.write_text(json.dumps(record, indent=2) + '\n')
    sys.stdout.write(stdout)
    sys.stderr.write(stderr)
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
