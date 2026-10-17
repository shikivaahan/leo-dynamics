#!/usr/bin/env python3
"""One isolated Python measurement process; not a propagation/batch public API."""
import time
IMPORT_START=time.perf_counter()
import argparse
import json
import platform
import resource
import sys
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests/reference/generators'))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('scenario',type=Path); p.add_argument('output',type=Path)
    p.add_argument('--tool',choices=['scipy','tudat'],required=True)
    p.add_argument('--members',type=int,default=1)
    p.add_argument('--repeats',type=int,default=3)
    p.add_argument('--warmups',type=int,default=1)
    p.add_argument('--reference',action='store_true',help='explicit untimed fixture/convergence operation')
    args=p.parse_args()
    if args.tool=='scipy':
        import scipy
        from dop853_reference import propagate_dop853,perturbed,initial_state
        from physics import Model
        version=scipy.__version__
    else:
        import copy
        from tudat_reference import propagate,runtime_version
        version=runtime_version
        def perturbed(document,member):
            d=copy.deepcopy(document)
            d['initial_state']['position_m'][0]+=.25*member
            d['initial_state']['velocity_m_s'][1]+=1e-4*member
            return d
    import_seconds=time.perf_counter()-IMPORT_START
    setup_start=time.perf_counter()
    document=json.loads(args.scenario.read_text())
    inputs=[perturbed(document,i) for i in range(args.members)]
    prepared=Model(document) if args.tool=='scipy' else None
    setup_seconds=time.perf_counter()-setup_start
    times=[];solvers=[];samplings=[];setups=[];calls=[]
    retained=[]
    for iteration in range(args.repeats+args.warmups):
        retained.clear()
        solver=sampling=setup=0.;evaluations=0
        begin=time.perf_counter()
        for d in inputs:
            if args.tool=='scipy':
                value=propagate_dop853(d,rtol=1e-13 if args.reference else 1e-12,
                                      max_step=2.5 if args.reference else 10.,
                                      atol_scale=.01 if args.reference else .1,model=prepared,
                                      internal_history=args.reference)
            else:
                value=propagate(d,step=2.5 if args.reference else 5.)
            retained.append(value)
            solver+=value['solver_seconds'];sampling+=value['sampling_seconds']
            setup+=value['setup_seconds'];evaluations+=value['rhs_evaluations']
        loop_seconds=time.perf_counter()-begin
        if iteration>=args.warmups:
            times.append(loop_seconds);solvers.append(solver);samplings.append(sampling)
            setups.append(setup);calls.append(evaluations)
    checksum=float(sum(np.asarray(value['states']).sum() for value in retained))
    diagnostics={}
    if args.tool=='scipy' and not args.reference:
        # Separate RHS measurement, not subtracted from independently measured
        # solver medians to invent a speedup. DOP853's controller/stages are
        # Python; its NumPy vector operations are compiled, not an isolated
        # Fortran/C++ DOP853 solver whose overhead can be measured separately.
        y=initial_state(document)
        latency=[];sink=0.
        for _ in range(3):
            begin=time.perf_counter()
            for i in range(200): sink+=prepared.rhs(float(i),y)[3]
            latency.append((time.perf_counter()-begin)/200)
        diagnostics=dict(rhs_seconds_per_call=latency,rhs_sink=float(sink),
                         implementation='Python DOP853 stages/controller and Python RHS, compiled NumPy algebra; no separately isolated compiled solver')
    output=dict(tool=args.tool,version=version,python=platform.python_version(),numpy=np.__version__,
                import_seconds=import_seconds,setup_seconds=setup_seconds,
                repetitions_seconds=times,solver_seconds=solvers,sampling_seconds=samplings,
                repeated_setup_seconds=setups,rhs_evaluations_per_repeat=calls,
                members=args.members,workers_actual=1,checksum=checksum,
                time_s=np.asarray(retained[0]['time_s']).tolist(),
                states=np.asarray(retained[0]['states']).tolist(),
                logical_output_bytes=sum(np.asarray(value['states']).nbytes+np.asarray(value['time_s']).nbytes for value in retained),
                process_peak_rss_bytes=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss*1024,
                memory_scope='fresh isolated process peak including interpreter/import/setup, not incremental trajectory memory',
                method=retained[0]['method'],diagnostics=diagnostics)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(output,allow_nan=False)+'\n')


if __name__=='__main__': main()
