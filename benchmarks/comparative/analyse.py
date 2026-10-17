#!/usr/bin/env python3
"""Mechanical comparative reporting; no timing estimates or performance pass gate."""
import argparse
import json
from pathlib import Path
from xml.sax.saxutils import escape


def scaling_svg(rows):
    curves=[]
    for name in ('j2_inclined','coupled_case_a'):
        candidates=[r for r in rows if r['status']=='MEASURED' and r['tool']=='cpp' and
                    r['name']==name and r['members']==64 and r['route']=='batch']
        if not candidates: continue
        baseline=next(r['median_seconds'] for r in candidates if r['workers']==1)
        curves.append((name,[(r['workers'],baseline/r['median_seconds']) for r in candidates]))
    width,height=780,380; left,top,right,bottom=65,45,750,320
    maximum=max([1.]+[v for _,points in curves for _,v in points])*1.15
    x=lambda workers:left+(workers-1)/7*(right-left)
    y=lambda speed:bottom-speed/maximum*(bottom-top)
    svg=[f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" font-family="system-ui" font-size="14" fill="currentColor">',
         '<text x="65" y="24">Actual 64-trajectory strong scaling (same inline batch baseline)</text>',
         f'<path d="M {left} {top} V {bottom} H {right}" fill="none" stroke="currentColor"/>']
    for workers in (1,2,4,8):
        svg.append(f'<text x="{x(workers):.1f}" y="345" text-anchor="middle">{workers}</text>')
    for speed in (0.,1.,2.,4.,8.):
        if speed<=maximum:
            svg.append(f'<text x="55" y="{y(speed)+5:.1f}" text-anchor="end">{speed:g}×</text>')
            svg.append(f'<path d="M {left} {y(speed):.1f} H {right}" stroke="var(--border)" fill="none"/>')
    for index,(name,points) in enumerate(curves):
        color=f'var(--c{index+1})'
        svg.append('<polyline points="'+' '.join(f'{x(w):.2f},{y(s):.2f}' for w,s in sorted(points))+
                   f'" fill="none" stroke="{color}" stroke-width="2"/>')
        for workers,speed in points:
            svg.append(f'<circle cx="{x(workers):.2f}" cy="{y(speed):.2f}" r="4" fill="{color}"/>')
        svg.append(f'<text x="{80+index*300}" y="375" fill="{color}">{escape(name)}</text>')
    svg.append('<text x="410" y="345" text-anchor="middle">native worker count</text></svg>')
    return '\n'.join(svg)


def render(document):
    rows=document['rows']
    measured=[r for r in rows if r['status']=='MEASURED']
    lines=['# Comparative performance (measured)', '',
           'Boundary: propagation plus generation/retention of requested output. All rows were accuracy-checked at their actual duration/cadence before timing. Setup, imports and whole-process costs are separate; no reference generation is inside headline clocks.', '',
           '| Scenario / simulated seconds | LEO C++ ms | SciPy DOP853 ms | Native Tudat ms | SciPy / LEO | Tudat / LEO |',
           '|---|---:|---:|---:|---:|---:|']
    keys=sorted(set((r['name'],r['duration_s'],r['cadence_s']) for r in rows if r['members']==1))
    for name,duration,cadence in keys:
        subset={r['tool']:r for r in rows if r['name']==name and r['members']==1 and r['duration_s']==duration and r['cadence_s']==cadence}
        cpp=subset['cpp']['median_seconds']
        def value(tool):
            r=subset.get(tool)
            return f"{r['median_seconds']*1000:.3f}" if r and r['status']=='MEASURED' else 'Unmatched'
        scipy=subset['scipy']['median_seconds']/cpp
        tudat=subset.get('tudat')
        ratio=f"{tudat['median_seconds']/cpp:.2f}×" if tudat and tudat['status']=='MEASURED' else '—'
        lines.append(f'| {name} / {duration:g} (cadence {cadence:g} s) | {cpp*1000:.3f} | {value("scipy")} | {value("tudat")} | {scipy:.2f}× | {ratio} |')
    lines+=['', 'Ratios compare equivalent equations, not identical nominal tolerances. Values below 1× mean the named Python tool is faster than LEO; there is no arbitrary performance acceptance gate.', '',
            '## Sequential ensembles', '', '| Scenario | Tool | Trajectories | Median s | Min–max s | Trajectories/s |', '|---|---|---:|---:|---|---:|']
    for r in measured:
        if r['members']>1 and r['route']=='sequential':
            lines.append(f"| {r['name']} | {r['tool']} | {r['members']} | {r['median_seconds']:.4f} | {r['min_seconds']:.4f}–{r['max_seconds']:.4f} | {r['trajectories_per_second']:.1f} |")
    lines+=['', '## Native strong scaling', '',
            'See `strong-scaling.svg`: actual 1/2/4/8 native worker counts, normalised to the same 64-member inline batch. This matched constant-Sun environment does not acquire SPICE and is not a replacement for MT-002’s real-environment contention measurements.', '',
            '## Setup, imports and output', '', '| Scenario / tool (single, longest window) | Setup ms | Import ms | Whole fresh process s (warmup + all repeats + publication) |', '|---|---:|---:|---:|']
    for name in sorted(set(r['name'] for r in measured)):
        for tool in ('cpp','scipy','tudat'):
            candidates=[r for r in measured if r['name']==name and r['tool']==tool and r['members']==1]
            if not candidates: continue
            r=max(candidates,key=lambda r:r['duration_s'])
            imported='—' if r['import_seconds'] is None else f"{r['import_seconds']*1000:.1f}"
            lines.append(f"| {name} / {tool} | {r['setup_seconds']*1000:.3f} | {imported} | {r['whole_process_wall_seconds_including_startup_warmups_and_publication']:.3f} |")
    lines+=['', 'The whole-process value is independently measured around a fresh child, not a per-trajectory solver median and not an inferred interpreter-startup estimate. Python workers retain only requested output in ordinary timing. Native Tudat body construction is outside its solver clock and explicitly included in repeated-setup/whole-process data.', '',
            '## Counters and memory', '',
            'Raw rows retain RHS evaluation counts, all repetitions, achieved position/velocity/SO(3)/rate errors, setup/solver/sampling times and child command lines. Python-level RHS latencies are separate diagnostics; they are not subtracted from independently measured medians. SciPy DOP853 uses Python stage/control logic with compiled NumPy algebra, not an independently isolatable compiled DOP853 engine.', '',
            'Python memory values are reliable fresh-process peak RSS including interpreter, imports and setup, not incremental trajectory memory. Logical retained float64 output bytes are separate. No comparable C++ RSS or total-memory speedup is inferred.', '',
            '## Unmatched models', '',
            'Native Tudat environmental panel/thermal/atmosphere/albedo equations differ from the project’s flat-plate/cold-beam/radial-beam definitions. Such rows are marked `UNMATCHED`; a default Tudat atmosphere is never presented as equivalent. SciPy covers those matched equations.', '']
    return '\n'.join(lines)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',type=Path,required=True); p.add_argument('--out',type=Path,required=True)
    args=p.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    document=json.loads(args.input.read_text())
    if document['status']!='complete': raise RuntimeError('cannot report unfinished measurements as complete')
    (args.out/'README.md').write_text(render(document))
    (args.out/'strong-scaling.svg').write_text(scaling_svg(document['rows']))
    print(args.out/'README.md')


if __name__=='__main__': main()
