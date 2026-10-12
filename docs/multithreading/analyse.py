#!/usr/bin/env python3
"""Reduce raw repetitions, keeping production and diagnostic results separate."""
import argparse
import collections
import json
from pathlib import Path
import statistics


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return {'n': len(values), 'min': values[0], 'median': statistics.median(values),
            'max': values[-1], 'mean': statistics.mean(values),
            'cv': statistics.pstdev(values) / statistics.mean(values) if statistics.mean(values) else 0}


def summarize(path):
    raw = json.loads(path.read_text())
    if raw['matrix']['configurations_failed'] or raw['matrix']['configurations_skipped']:
        raise RuntimeError(f'{path}: incomplete benchmark matrix')
    grouped = collections.defaultdict(list)
    for run in raw['runs']:
        grouped[run['configuration_id']].append(run)
    rows = []
    for key, runs in grouped.items():
        first = runs[0]
        row = {field: first[field] for field in ('executor', 'case_id', 'workload', 'variant', 'batch_size', 'requested_workers')}
        row['scheduling'] = first.get('scheduling', key.rsplit('|', 1)[-1] if first['executor'] == 'control_no_rhs' else 'sequential')
        row['configuration_id'] = key
        row['duration_s'] = raw['duration_s']
        row['source_file'] = str(path)
        row['repetitions'] = len(runs)
        row['wall_s'] = distribution([r['wall_seconds'] for r in runs])
        row['throughput_trajectories_s'] = distribution([r['trajectories_per_second'] for r in runs])
        for field in ('cpu_utilization', 'dispatch_seconds', 'active_envelope_seconds', 'join_return_seconds',
                      'rhs_construction_seconds_total', 'rhs_destruction_seconds_total',
                      'worker_busy_cv', 'worker_busy_max_over_span'):
            row[field] = distribution([r[field] for r in runs])
        row['peak_rss_bytes'] = max(r['peak_rss_bytes'] for r in runs)
        row['rhs_calls'] = sorted(set(r['rhs_calls'] for r in runs))
        row['atmosphere_calls'] = sorted(set(r['atmosphere_calls'] for r in runs))
        row['trajectory_wall_s'] = distribution([v for r in runs for v in r['trajectory_seconds_series']])
        row['adaptive_rhs_calls_per_trajectory'] = distribution([v for r in runs for v in r['rhs_calls_per_trajectory']])
        row['checksum_repeatable'] = len({r['final_state_checksum'] for r in runs}) == 1
        if not row['checksum_repeatable']:
            raise RuntimeError(f'{path}: non-repeatable final states: {key}')
        for block in ('spice_reuse_delta', 'native_mutex_delta'):
            samples = [r[block] for r in runs if r[block] is not None]
            row[block] = samples
        rows.append(row)
    baselines = {(r['case_id'], r['workload'], r['variant'], r['batch_size']): r
                 for r in rows if r['executor'] == 'sequential'}
    for row in rows:
        if row['executor'] != 'batch':
            continue
        baseline = baselines[(row['case_id'], row['workload'], row['variant'], row['batch_size'])]
        row['sequential_wall_s'] = baseline['wall_s']
        row['speedup_ratio_of_medians'] = baseline['wall_s']['median'] / row['wall_s']['median']
        row['efficiency_requested_workers'] = row['speedup_ratio_of_medians'] / row['requested_workers']
        row['efficiency_active_bound'] = row['speedup_ratio_of_medians'] / min(row['requested_workers'], row['batch_size'])
        row['checksum_matches_sequential'] = grouped[row['configuration_id']][0]['final_state_checksum'] == grouped[baseline['configuration_id']][0]['final_state_checksum']
        if (not row['checksum_matches_sequential'] or row['rhs_calls'] != baseline['rhs_calls']
                or row['atmosphere_calls'] != baseline['atmosphere_calls']):
            raise RuntimeError(f'{path}: non-repeatable or mismatched trajectory/control count: {row["configuration_id"]}')
    return raw, rows


def observations(documents, startup_path):
    diagnostic = documents['diagnostics']['rows']
    contention = []
    for row in diagnostic:
        if (row['executor'] != 'batch' or row['workload'] == 'two_body_j2'
                or row['batch_size'] != 32 or row['requested_workers'] != 8
                or row['scheduling'] != 'static_partition'):
            continue
        spice = row['spice_reuse_delta']
        native = row['native_mutex_delta']
        model = row['workload'].removeprefix('full_')
        entry = {key: row[key] for key in ('case_id', 'workload', 'variant')}
        entry.update(
            wall_s=row['wall_s']['median'],
            spice_contended_pct=100 * statistics.median(s['spice_mutex_contended'] / s['spice_mutex_acquisitions'] for s in spice),
            spice_acquisition_aggregate_s=statistics.median(s['spice_mutex_wait_ns'] / 1e9 for s in spice),
            sun_calls_per_rhs=statistics.median(s['spkez_calls'] / row['rhs_calls'][0] for s in spice),
            utc_hit_pct=100 * statistics.median(s['utc_hits'] / max(1, s['utc_hits'] + s['utc_misses']) for s in spice),
            orientation_hit_pct=100 * statistics.median(s['orientation_hits'] / max(1, s['orientation_hits'] + s['orientation_misses']) for s in spice),
        )
        if model in ('nrlmsise00', 'jb2008'):
            entry.update(
                native_contended_pct=100 * statistics.median(n[model]['contended_fraction'] for n in native),
                native_acquisition_aggregate_s=statistics.median(n[model]['acquisition_ns'] / 1e9 for n in native),
                native_held_aggregate_s=statistics.median(n[model]['held_ns'] / 1e9 for n in native),
            )
        else:
            entry.update(native_contended_pct=0, native_acquisition_aggregate_s=0, native_held_aggregate_s=0)
        contention.append(entry)
    startup = json.loads(startup_path.read_text())
    startup_rows = []
    for count in sorted({run['threads_created'] for run in startup['runs']}):
        selected = [run for run in startup['runs'] if run['threads_created'] == count]
        startup_rows.append({'threads_created': count, **{
            key: distribution([run[key] for run in selected])
            for key in ('creation_ready_s', 'release_join_s', 'total_s')}})
    policy_ratios = {}
    for model in sorted({row['workload'] for row in documents['confirmation']['rows']}):
        grouped = collections.defaultdict(dict)
        for row in documents['confirmation']['rows']:
            if row['executor'] == 'batch' and row['workload'] == model and row['requested_workers'] > 1:
                grouped[(row['case_id'], row['variant'], row['requested_workers'])][row['scheduling']] = row['wall_s']['median']
        policy_ratios[model] = distribution([pair['static_partition'] / pair['dynamic'] for pair in grouped.values()])
    return {'diagnostic_selection': 'CASE-A, batch 32, workers 8, static; medians over 3 repetitions; acquisition time includes instrumentation/lock overhead and aggregate waits may exceed wall time',
            'contention': contention, 'startup_source': str(startup_path), 'startup': startup_rows,
            'confirmation_static_over_dynamic_wall_ratios': policy_ratios,
            'cpu_accounting_limit': 'Short guest-thread runs sometimes report process CPU/wall below 1 despite observed wall-speedup; cause not isolated. Retain raw CPU clocks but use six-hour runs rather than short-run CPU utilization as concurrency evidence.'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--matrix', type=Path, required=True)
    parser.add_argument('--confirmation', type=Path, required=True)
    parser.add_argument('--diagnostics', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--startup', type=Path)
    args = parser.parse_args()
    documents = {}
    for name, path in (('matrix', args.matrix), ('confirmation', args.confirmation), ('diagnostics', args.diagnostics)):
        raw, rows = summarize(path)
        documents[name] = {'source_file': str(path), 'matrix': raw['matrix'], 'duration_s': raw['duration_s'],
                           'rows': rows, 'diagnostic_build': raw['build'], 'seed': raw['seed']}
    startup_path = args.startup or args.matrix.parent / 'startup.json'
    result = {'kind': 'MT-001 scaling', 'method': 'ratio of matched sequential median batch wall time to parallel median; efficiency divided by configured workers; min/max and CV retain repetition spread, not confidence intervals',
              'measurements': documents,
              'bottleneck_observations': observations(documents, startup_path),
              'hardware_counters': 'Unavailable: perf executable not installed; false sharing and cache locality are hypotheses, not measured bottlenecks.'}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print('Reduced', sum(len(d['rows']) for d in documents.values()), 'configurations to', args.out)


if __name__ == '__main__':
    main()
