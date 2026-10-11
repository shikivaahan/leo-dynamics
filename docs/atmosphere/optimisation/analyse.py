#!/usr/bin/env python3
"""Reduce the ATM-OPT-001 benchmark evidence to a comparison summary.

Reads the raw benchmark JSON documents (gzipped or not), computes per-workload
speedups of every candidate configuration against the unchanged original,
extracts the reuse, memory and mutex counters, cross-checks the deterministic
cache replay against the measured hit rates and writes `summary.json` plus a
human-readable `summary.md`. Nothing here reruns a measurement.
"""
import gzip
import json
from pathlib import Path

VARIANT_ORDER = ['off_off_off', 'utc_only', 'sun_only', 'orientation_only', 'utc_sun',
                 'utc_orientation', 'sun_orientation', 'all']
SCHEDULE_ORDER = ['fixed', 'same_epoch_changing_positions', 'same_position_changing_epochs',
                  'nearby_positions_epochs', 'identical_daily_records_changing_geometry',
                  'weather_3h_boundary', 'weather_hour_boundary', 'utc_midnight',
                  'ensemble_trajectory_major', 'ensemble_epoch_major']
NATIVE_MODELS = ['nrlmsise00', 'jb2008']


def load(path):
    path = Path(path)
    if path.suffix == '.gz':
        with gzip.open(path, 'rt') as stream:
            return json.load(stream)
    return json.load(path.open())


def speedup(original, candidate):
    return (original / candidate) if candidate else None


def provider_rows(data):
    return {(row['model'], row['variant']): row for row in data['sections'].get('provider', [])}


def locality_rows(data):
    return {(row['schedule'], row['model'], row['variant']): row
            for row in data['sections'].get('locality', [])}


def concurrency_rows(data):
    return {(row['model'], row['variant'], row['threads']): row
            for row in data['sections'].get('concurrency', [])}


def propagate_rows(data):
    return {(row['case_id'], row['model'], row['variant']): row
            for row in data['sections'].get('propagate', [])}


def micro_rows(data):
    return {row['operation']: row for row in data['sections'].get('micro', [])}


def rate(hits, misses):
    total = hits + misses
    return (hits / total) if total else None


def provider_table(original,candidate,diagnostics,original_diagnostics=None):
    rows = []
    original_map = provider_rows(original)
    diagnostic_map = provider_rows(diagnostics) if diagnostics else {}
    original_counter_map=provider_rows(original_diagnostics) if original_diagnostics else {}
    for (model, variant), row in provider_rows(candidate).items():
        base = original_map.get((model, 'original'))
        if base is None:
            continue
        mean = row['aggregate']['per_call_ns']['mean']
        base_mean = base['aggregate']['per_call_ns']['mean']
        # The uninstrumented candidate build compiles the counters out, so the
        # measured counters are the diagnostic build's, for the same physics.
        reuse = diagnostic_map.get((model, variant), {}).get('aggregate', {}).get(
            'reuse_totals', row['aggregate']['reuse_totals'])
        base_reuse=original_counter_map.get((model,'original'),{}).get('aggregate',{}).get('reuse_totals')
        rows.append({
            'model': model, 'variant': variant,
            'original_mean_ns': base_mean,
            'candidate_mean_ns': mean,
            'speedup_mean': speedup(base_mean, mean),
            'original_median_ns': base['aggregate']['per_call_ns']['median'],
            'candidate_median_ns': row['aggregate']['per_call_ns']['median'],
            'speedup_median': speedup(base['aggregate']['per_call_ns']['median'],
                                      row['aggregate']['per_call_ns']['median']),
            'candidate_p95_ns': row['aggregate']['per_call_ns']['p95'],
            'candidate_p99_ns': row['aggregate']['per_call_ns']['p99'],
            'batch_mean_spread': row['aggregate']['batch_mean_ns']['relative_spread_of_mean'],
            'utc_hit_rate': rate(reuse['utc_hits'], reuse['utc_misses']),
            'orientation_hit_rate': rate(reuse['orientation_hits'], reuse['orientation_misses']),
            'str2et_calls': reuse['str2et_calls'], 'et2utc_calls': reuse['et2utc_calls'],
            'pxform_calls': reuse['pxform_calls'], 'spkez_calls': reuse['spkez_calls'],
            'utc_evictions': reuse['utc_evictions'],
            'orientation_evictions': reuse['orientation_evictions'],
            'cache_bytes': reuse['cache_bytes'],
            'utc_lookup_mean_ns': reuse['utc_lookup_ns']/reuse['utc_lookups'] if reuse['utc_lookups'] else None,
            'orientation_lookup_mean_ns': reuse['orientation_lookup_ns']/reuse['orientation_lookups'] if reuse['orientation_lookups'] else None,
            'baseline_str2et_calls': base_reuse['str2et_calls'] if base_reuse is not None else None,
        })
    rows.sort(key=lambda r: (r['model'], VARIANT_ORDER.index(r['variant'])
                             if r['variant'] in VARIANT_ORDER else len(VARIANT_ORDER)))
    return rows


def locality_table(original, candidate, diagnostics):
    rows = []
    original_map = locality_rows(original)
    diagnostic_map = locality_rows(diagnostics) if diagnostics else {}
    for (schedule, model, variant), row in locality_rows(candidate).items():
        base = original_map.get((schedule, model, 'original'))
        if base is None:
            continue
        mean = row['aggregate']['per_call_ns']['mean']
        base_mean = base['aggregate']['per_call_ns']['mean']
        measured = diagnostic_map.get((schedule, model, variant), {}).get('aggregate', {}).get(
            'reuse_totals', {})
        reuse = measured or row['aggregate']['reuse_totals']
        rows.append({
            'schedule': schedule, 'model': model, 'variant': variant,
            'original_mean_ns': base_mean, 'candidate_mean_ns': mean,
            'speedup_mean': speedup(base_mean, mean),
            'candidate_p99_ns': row['aggregate']['per_call_ns']['p99'],
            'batch_mean_spread': row['aggregate']['batch_mean_ns']['relative_spread_of_mean'],
            'utc_hits': reuse['utc_hits'], 'utc_misses': reuse['utc_misses'],
            'utc_hit_rate': rate(reuse['utc_hits'], reuse['utc_misses']),
            'utc_evictions': reuse['utc_evictions'],
            'orientation_hits': reuse['orientation_hits'],
            'orientation_misses': reuse['orientation_misses'],
            'orientation_hit_rate': rate(reuse['orientation_hits'], reuse['orientation_misses']),
            'orientation_evictions': reuse['orientation_evictions'],
            'mutex_acquisitions': reuse['mutex_acquisitions'],
            'mutex_contended': reuse['mutex_contended'],
            'mutex_wait_ns': reuse['mutex_wait_ns'],
            'cache_bytes': reuse['cache_bytes'],
            'utc_hit_rate_measured': rate(reuse['utc_hits'], reuse['utc_misses']),
            'measured_utc_hit_rate_diagnostic_build': rate(measured.get('utc_hits', 0),
                                                          measured.get('utc_misses', 0)),
        })
    rows.sort(key=lambda r: (r['schedule'], r['model'], VARIANT_ORDER.index(r['variant'])
                             if r['variant'] in VARIANT_ORDER else len(VARIANT_ORDER)))
    return rows


def concurrency_table(original,candidate,diagnostics,original_diagnostics=None):
    rows = []
    original_map = concurrency_rows(original)
    diagnostic_map = concurrency_rows(diagnostics) if diagnostics else {}
    original_counter_map=concurrency_rows(original_diagnostics) if original_diagnostics else {}
    for (model, variant, threads), row in concurrency_rows(candidate).items():
        base = original_map.get((model, 'original', threads))
        if base is None:
            continue
        throughput = row['throughput_calls_per_s']['mean']
        base_throughput = base['throughput_calls_per_s']['mean']
        measured = diagnostic_map.get((model, variant, threads), {}).get('reuse_totals', {})
        reuse = measured or row['reuse_totals']
        base_reuse=original_counter_map.get((model,'original',threads),{}).get('reuse_totals')
        rows.append({
            'model': model, 'variant': variant, 'threads': threads,
            'original_throughput_calls_per_s': base_throughput,
            'candidate_throughput_calls_per_s': throughput,
            'throughput_ratio': speedup(throughput, base_throughput),
            'original_latency_median_ns': base['latency_ns']['median'],
            'candidate_latency_median_ns': row['latency_ns']['median'],
            'candidate_latency_p99_ns': row['latency_ns']['p99'],
            'mutex_acquisitions': reuse['mutex_acquisitions'],
            'mutex_contended': reuse['mutex_contended'],
            'contended_fraction': reuse['mutex_contended']/reuse['mutex_acquisitions'] if reuse['mutex_acquisitions'] else None,
            'mutex_wait_ns_total': reuse['mutex_wait_ns'],
            'mutex_wait_ns_per_acquisition': (reuse['mutex_wait_ns'] / reuse['mutex_acquisitions']
                                              if reuse['mutex_acquisitions'] else None),
            'baseline_mutex_wait_ns_total': base_reuse['mutex_wait_ns'] if base_reuse is not None else None,
            'measured_contended_fraction_diagnostic_build': measured.get('mutex_contended',0)/measured['mutex_acquisitions'] if measured.get('mutex_acquisitions') else None,
        })
    rows.sort(key=lambda r: (r['model'], VARIANT_ORDER.index(r['variant'])
                             if r['variant'] in VARIANT_ORDER else len(VARIANT_ORDER), r['threads']))
    return rows


def propagate_table(original, candidate, diagnostics):
    rows = []
    original_map = propagate_rows(original)
    diagnostic_map = propagate_rows(diagnostics) if diagnostics else {}
    for (case, model, variant), row in propagate_rows(candidate).items():
        base = original_map.get((case, model, 'original'))
        if base is None:
            continue
        median = row['wall_s']['median']
        base_median = base['wall_s']['median']
        counters = diagnostic_map.get((case, model, variant), {}).get('reuse_totals') \
            or row['reuse_totals']
        rows.append({
            'case_id': case, 'model': model, 'variant': variant,
            'original_median_wall_s': base_median, 'candidate_median_wall_s': median,
            'wall_time_speedup': speedup(base_median, median),
            'original_wall_spread': base['wall_s']['relative_spread_of_mean'],
            'candidate_wall_spread': row['wall_s']['relative_spread_of_mean'],
            'rhs_calls': row['repetitions'][0]['rhs_calls'],
            'atmosphere_calls': row['repetitions'][0]['atmosphere_calls'],
            'original_atmosphere_calls': base['repetitions'][0]['atmosphere_calls'],
            'str2et_calls': counters['str2et_calls'],
            'et2utc_calls': counters['et2utc_calls'],
            'spkez_calls': counters['spkez_calls'],
            'utc_hits': counters['utc_hits'],
            'utc_hit_rate': rate(counters['utc_hits'], counters['utc_misses']),
            'orientation_hits': counters['orientation_hits'],
            'orientation_misses': counters['orientation_misses'],
            'orientation_hit_rate': rate(counters['orientation_hits'],counters['orientation_misses']),
            'orientation_evictions': counters['orientation_evictions'],
            'utc_evictions': counters['utc_evictions'],
            'pxform_calls': counters['pxform_calls'],
            'utc_lookup_mean_ns': counters['utc_lookup_ns']/counters['utc_lookups'] if counters['utc_lookups'] else None,
            'orientation_lookup_mean_ns': counters['orientation_lookup_ns']/counters['orientation_lookups'] if counters['orientation_lookups'] else None,
            'original_rhs_median_ns': base['repetitions'][0]['rhs_ns']['median'],
            'candidate_rhs_median_ns': row['repetitions'][0]['rhs_ns']['median'],
            'rhs_speedup_median': speedup(base['repetitions'][0]['rhs_ns']['median'],row['repetitions'][0]['rhs_ns']['median']),
            'original_rhs_mean_ns': base['repetitions'][0]['rhs_ns']['mean'],
            'candidate_rhs_mean_ns': row['repetitions'][0]['rhs_ns']['mean'],
            'candidate_rhs_p95_ns': row['repetitions'][0]['rhs_ns']['p95'],
            'candidate_rhs_p99_ns': row['repetitions'][0]['rhs_ns']['p99'],
            'trajectory_checksum': row['repetitions'][0]['trajectory_checksum'],
        })
    rows.sort(key=lambda r: (r['case_id'], r['model'], VARIANT_ORDER.index(r['variant'])
                             if r['variant'] in VARIANT_ORDER else len(VARIANT_ORDER)))
    return rows


def micro_table(candidate):
    rows = micro_rows(candidate)
    out = {}
    for name, row in rows.items():
        reuse = row['aggregate']['reuse_totals']
        out[name] = {
            'mean_ns': row['aggregate']['per_call_ns']['mean'],
            'median_ns': row['aggregate']['per_call_ns']['median'],
            'p99_ns': row['aggregate']['per_call_ns']['p99'],
            'batch_mean_spread': row['aggregate']['batch_mean_ns']['relative_spread_of_mean'],
            'utc_hits': reuse['utc_hits'], 'utc_misses': reuse['utc_misses'],
            'orientation_hits': reuse['orientation_hits'],
            'orientation_misses': reuse['orientation_misses'],
        }
    lookups = {}
    if 'utc_fixed_reuse' in out and 'utc_fixed_noreuse' in out:
        lookups['utc'] = {
            'reuse_mean_ns': out['utc_fixed_reuse']['mean_ns'],
            'no_reuse_mean_ns': out['utc_fixed_noreuse']['mean_ns'],
            'saved_work_ns': out['utc_fixed_noreuse']['mean_ns']-out['utc_fixed_reuse']['mean_ns'],
            'hit_rate': rate(out['utc_fixed_reuse']['utc_hits'], out['utc_fixed_reuse']['utc_misses']),
            'miss_path_mean_ns': out.get('utc_changing', {}).get('mean_ns'),
        }
    if 'orientation_fixed_reuse' in out and 'orientation_fixed_noreuse' in out:
        lookups['orientation'] = {
            'reuse_mean_ns': out['orientation_fixed_reuse']['mean_ns'],
            'no_reuse_mean_ns': out['orientation_fixed_noreuse']['mean_ns'],
            'saved_work_ns': out['orientation_fixed_noreuse']['mean_ns']-out['orientation_fixed_reuse']['mean_ns'],
            'hit_rate': rate(out['orientation_fixed_reuse']['orientation_hits'],
                             out['orientation_fixed_reuse']['orientation_misses']),
            'miss_path_mean_ns': out.get('orientation_changing', {}).get('mean_ns'),
        }
    if 'sun_observation' in out and 'sun_geometric' in out:
        lookups['sun'] = {
            'observation_mean_ns': out['sun_observation']['mean_ns'],
            'geometric_state_mean_ns': out['sun_geometric']['mean_ns'],
            'shared_difference_ns': out['sun_observation']['mean_ns'] - out['sun_geometric']['mean_ns'],
        }
    return out, lookups


def replay_table(candidate):
    replay = candidate['sections'].get('replay', {})
    rows = []
    for schedule, entry in replay.items():
        for thread_text, row in entry['by_thread_count'].items():
            shared_utc = row['shared_utc_fifo32']
            local_utc = row['thread_local_utc_fifo32']
            shared_orientation = row['shared_orientation_4way256']
            local_orientation = row['thread_local_orientation_4way256']
            rows.append({
                'schedule': schedule, 'threads': int(thread_text),
                'shared_utc_hits': shared_utc['hits'], 'shared_utc_misses': shared_utc['misses'],
                'thread_local_utc_hits': local_utc['hits'], 'thread_local_utc_misses': local_utc['misses'],
                'utc_miss_delta_thread_local_minus_shared': local_utc['misses'] - shared_utc['misses'],
                'shared_orientation_hits': shared_orientation['hits'],
                'shared_orientation_misses': shared_orientation['misses'],
                'thread_local_orientation_hits': local_orientation['hits'],
                'thread_local_orientation_misses': local_orientation['misses'],
                'orientation_miss_delta_thread_local_minus_shared':
                    local_orientation['misses'] - shared_orientation['misses'],
            })
    rows.sort(key=lambda r: (SCHEDULE_ORDER.index(r['schedule']) if r['schedule'] in SCHEDULE_ORDER
                             else len(SCHEDULE_ORDER), r['threads']))
    return rows


def replay_crosscheck(locality, replay_rows):
    """The simulation is only meaningful if it reproduces the measured hit rate."""
    measured = {(row['schedule'], row['model'], row['variant']): row for row in locality}
    checks = []
    for entry in replay_rows:
        if entry['threads']!=1:
            continue
        shared_utc_hits, shared_utc_misses = entry['shared_utc_hits'], entry['shared_utc_misses']
        observed = [row for (schedule, model, variant), row in measured.items()
                    if schedule == entry['schedule'] and variant == 'all'
                    and model in NATIVE_MODELS]
        if not observed or (shared_utc_hits + shared_utc_misses) == 0:
            continue
        for row in observed:
            measured_rate = row.get('utc_hit_rate_measured')
            simulated_rate = shared_utc_hits / (shared_utc_hits + shared_utc_misses)
            simulated_orientation=rate(entry['shared_orientation_hits'],entry['shared_orientation_misses'])
            measured_orientation=row['orientation_hit_rate']
            if measured_rate!=simulated_rate or measured_orientation!=simulated_orientation:
                raise RuntimeError('replay does not reproduce measured locality: '+entry['schedule']+'/'+row['model'])
            checks.append({
                'schedule': entry['schedule'], 'model': row['model'],
                'simulated_utc_hit_rate': simulated_rate, 'measured_utc_hit_rate': measured_rate,
                'absolute_difference': abs(simulated_rate - measured_rate) if measured_rate is not None else None,
                'simulated_orientation_hit_rate':simulated_orientation,
                'measured_orientation_hit_rate':measured_orientation,
            })
    return checks


def thread_local_decision(replay_rows, concurrency):
    """Shared cache versus thread-local prototype, on measured and replayed data."""
    decision = {'replay': replay_rows, 'threads': []}
    for threads in sorted({row['threads'] for row in concurrency}):
        rows = [row for row in concurrency if row['threads'] == threads and row['variant'] == 'all']
        base = [row for row in concurrency if row['threads'] == threads and row['variant'] == 'off_off_off']
        replay_rows_for_threads = [row for row in replay_rows if row['threads'] == threads]
        utc_delta = sum(row['utc_miss_delta_thread_local_minus_shared'] for row in replay_rows_for_threads)
        orientation_delta = sum(row['orientation_miss_delta_thread_local_minus_shared']
                                for row in replay_rows_for_threads)
        rows_out = []
        for row in rows:
            control = [b for b in base if b['model'] == row['model']]
            rows_out.append({
                'model': row['model'],
                'contended_fraction': row['contended_fraction'],
                'mutex_wait_ns_per_acquisition': row['mutex_wait_ns_per_acquisition'],
                'throughput_ratio_all_over_control':
                    speedup(row['candidate_throughput_calls_per_s'],
                            control[0]['candidate_throughput_calls_per_s']) if control else None,
            })
        entry = {
            'threads': threads,
            'replayed_extra_utc_misses_thread_local': utc_delta,
            'replayed_extra_orientation_misses_thread_local': orientation_delta,
            'rows': rows_out,
        }
        decision['threads'].append(entry)
    return decision


def markdown(summary):
    lines = ['# ATM-OPT-001 measured optimisation summary', '',
             'Every number below comes from `summary.json`; the raw per-call samples remain in',
             'the benchmark documents and the manifest records the exact commands, environment,',
             'dependency hashes and preserved evidence hashes.', '',
             '## Fixed full provider (per call, mean of all samples)', '',
             '| model | variant | original ns | candidate ns | speedup | p95 ns | p99 ns | batch spread |',
             '|---|---|---:|---:|---:|---:|---:|---:|']
    for row in summary['provider']:
        lines.append(f"| {row['model']} | {row['variant']} | {row['original_mean_ns']:.1f} | "
                     f"{row['candidate_mean_ns']:.1f} | {row['speedup_mean']:.3f}x | "
                     f"{row['candidate_p95_ns']:.1f} | {row['candidate_p99_ns']:.1f} | "
                     f"{row['batch_mean_spread']:.3f} |")
    lines += ['', '## Isolated lookup cost (mean ns per call)', '',
              '| operation | mean ns | median ns | p99 ns |', '|---|---:|---:|---:|']
    for name, row in summary['micro'].items():
        lines.append(f"| {name} | {row['mean_ns']:.1f} | {row['median_ns']:.1f} | {row['p99_ns']:.1f} |")
    for key, value in summary['lookup_cost'].items():
        lines += ['', f"### {key} lookup", '', '```json', json.dumps(value, indent=2), '```']
    lines += ['', '## Six-hour CASE propagation (median of the repeats)', '',
              '| case | model | variant | original s | candidate s | speedup | spread | '
              'UTC hits | str2et | spkez |', '|---|---|---|---:|---:|---:|---:|---:|---:|---:|']
    for row in summary['propagate']:
        lines.append(f"| {row['case_id']} | {row['model']} | {row['variant']} | "
                     f"{row['original_median_wall_s']:.3f} | {row['candidate_median_wall_s']:.3f} | "
                     f"{row['wall_time_speedup']:.3f}x | {row['candidate_wall_spread']:.3f} | "
                     f"{row['utc_hits']} | {row['str2et_calls']} | {row['spkez_calls']} |")
    lines += ['', '## Concurrency (throughput calls/s, mean over batches)', '',
              '| model | variant | threads | original | candidate | ratio | median latency ns | '
              'contended | wait ns/acquisition |', '|---|---|---:|---:|---:|---:|---:|---:|---:|']
    for row in summary['concurrency']:
        contended = 'n/a' if row['contended_fraction'] is None else f"{row['contended_fraction']:.3f}"
        wait = 'n/a' if row['mutex_wait_ns_per_acquisition'] is None \
            else f"{row['mutex_wait_ns_per_acquisition']:.0f}"
        lines.append(f"| {row['model']} | {row['variant']} | {row['threads']} | "
                     f"{row['original_throughput_calls_per_s']:.0f} | "
                     f"{row['candidate_throughput_calls_per_s']:.0f} | {row['throughput_ratio']:.3f}x | "
                     f"{row['candidate_latency_median_ns']:.1f} | {contended} | {wait} |")
    lines += ['', '## Deterministic locality schedules (mean ns per call, speedup vs original)', '',
              '| schedule | model | variant | original ns | candidate ns | speedup | UTC hit rate | '
              'orientation hit rate |', '|---|---|---|---:|---:|---:|---:|---:|']
    for row in summary['locality']:
        utc_rate = 'n/a' if row['utc_hit_rate'] is None else f"{row['utc_hit_rate']:.4f}"
        orientation_rate = 'n/a' if row['orientation_hit_rate'] is None \
            else f"{row['orientation_hit_rate']:.4f}"
        lines.append(f"| {row['schedule']} | {row['model']} | {row['variant']} | "
                     f"{row['original_mean_ns']:.1f} | {row['candidate_mean_ns']:.1f} | "
                     f"{row['speedup_mean']:.3f}x | {utc_rate} | {orientation_rate} |")
    lines += ['', '## Replay versus measured hit rates', '',
              '| schedule | model | simulated UTC hit rate | measured UTC hit rate | difference |',
              '|---|---|---:|---:|---:|']
    for row in summary['replay_crosscheck']:
        measured = 'n/a' if row['measured_utc_hit_rate'] is None else f"{row['measured_utc_hit_rate']:.4f}"
        difference = 'n/a' if row['absolute_difference'] is None else f"{row['absolute_difference']:.4f}"
        lines.append(f"| {row['schedule']} | {row['model']} | {row['simulated_utc_hit_rate']:.4f} | "
                     f"{measured} | {difference} |")
    lines += ['', '## Shared cache versus thread-local prototype', '', '```json',
              json.dumps(summary['thread_local_decision'], indent=2), '```', '',
              '## Invalidation', '', '```json', json.dumps(summary['invalidation'], indent=2), '```', '',
              '## Scientific verification', '', '```json',
              json.dumps(summary['verification'], indent=2), '```', '']
    return '\n'.join(lines)


def load_benchmark(out, stem):
    for name in (stem + '.json', stem + '.json.gz'):
        if (out / name).exists():
            return load(out / name)
    return None


def analyse(out):
    out = Path(out)
    original = load_benchmark(out, 'benchmark-original')
    candidate = load_benchmark(out, 'benchmark-candidate')
    diagnostics = load_benchmark(out, 'benchmark-diagnostics')
    original_diagnostics=load_benchmark(out,'benchmark-original_diagnostics')
    if original is None or candidate is None:
        raise RuntimeError('both the original and candidate benchmark documents are required')
    verification_path = out / 'verification.json'
    verification = load(verification_path) if verification_path.exists() else {}

    micro, lookups = micro_table(candidate)
    locality = locality_table(original, candidate, diagnostics)
    replay_rows = replay_table(candidate)
    concurrency = concurrency_table(original,candidate,diagnostics,original_diagnostics)
    summary = {
        'kind': 'atm-opt-001-summary',
        'candidate_build': candidate.get('candidate_build'),
        'diagnostics_build': candidate.get('diagnostics_build'),
        'diagnostics_run_present': diagnostics is not None,
        'quantile_estimator': 'linear interpolation at q(n-1), identical to the profiling evidence',
        'provider': provider_table(original,candidate,diagnostics,original_diagnostics),
        'locality': locality,
        'concurrency': concurrency,
        'propagate': propagate_table(original, candidate, diagnostics),
        'micro': micro,
        'lookup_cost': lookups,
        'replay': replay_rows,
        'replay_crosscheck': replay_crosscheck(locality, replay_rows),
        'thread_local_decision': thread_local_decision(replay_rows, concurrency),
        'invalidation': verification.get('invalidation', {}),
        'verification': {key: value for key, value in verification.items()
                         if key not in {'propagate_checksums'}},
        'propagate_checksums': verification.get('propagate_checksums', {}),
    }
    (out / 'summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False) + '\n')
    (out / 'summary.md').write_text(markdown(summary))
    return summary