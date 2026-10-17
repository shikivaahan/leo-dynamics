#!/usr/bin/env python3
"""Native Tudat gravity/rigid-body reference; explicit constants and J2000 frames.

No LEO force function or Python custom-RHS callback is used. Environmental panel
laws are physically unmatched and rejected, not silently replaced by Tudat defaults.
Run using the separately installed Tudat conda interpreter.
"""
import argparse
import json
import math
import time
from pathlib import Path
import numpy as np
from tudatpy.numerical_simulation import environment_setup as es, propagation_setup as ps
from tudatpy.numerical_simulation import create_dynamics_simulator
from tudatpy._version import __version__ as runtime_version

MU, RE, J2 = 3.986004418e14, 6378136.3, 1.08262668e-3


def propagate(document, step=None):
    start=time.perf_counter()
    mode=document['models']
    if any(mode[name] for name in ('srp','drag','albedo')):
        raise ValueError('unmatched: native Tudat panel SRP/drag/albedo equations are not the LEO cold-beam/radial-beam definitions')
    run=document['run']
    times=np.array(run['sample_times_s'],dtype=float)
    cadence=float(run['cadence_s'])
    step=min(5.,cadence) if step is None else float(step)
    if step<=0 or np.max(np.abs(times/step-np.rint(times/step)))>1e-8:
        raise ValueError('native fixed-step Tudat reference requires requested outputs on its time grid')
    settings=es.BodyListSettings('Earth','J2000')
    settings.add_empty_settings('Earth'); settings.add_empty_settings('Vehicle')
    earth=settings.get('Earth')
    earth.ephemeris_settings=es.ephemeris.constant(np.zeros(6),'SSB','J2000')
    earth.rotation_model_settings=es.rotation_model.simple('J2000','EarthFixed',np.eye(3),0.,0.)
    earth.shape_settings=es.shape.spherical(RE)
    if mode['j2']:
        cosine=np.zeros((3,3)); sine=np.zeros((3,3))
        cosine[0,0]=1.; cosine[2,0]=-J2/math.sqrt(5.)
        earth.gravity_field_settings=es.gravity_field.spherical_harmonic(MU,RE,cosine,sine,'EarthFixed')
    else:
        earth.gravity_field_settings=es.gravity_field.central(MU)
    c=document['spacecraft']
    inertia=np.array(c.get('inertia_tensor_kg_m2',np.diag(c['inertia_kg_m2'])))
    vehicle=settings.get('Vehicle')
    vehicle.rigid_body_settings=es.rigid_body.constant_rigid_body_properties(c['mass_kg'],np.zeros(3),inertia)
    vehicle.rotation_model_settings=es.rotation_model.simple('J2000','VehicleFixed',np.eye(3),0.,0.)
    bodies=es.create_system_of_bodies(settings)
    acceleration=ps.acceleration.spherical_harmonic_gravity(2,0) if mode['j2'] else ps.acceleration.point_mass_gravity()
    accelerations=ps.create_acceleration_models(bodies,{'Vehicle':{'Earth':[acceleration]}},['Vehicle'],['Earth'])
    torques=ps.create_torque_models(bodies,{'Vehicle':{'Earth':[ps.torque.second_degree_gravitational()]}
                                    if mode['gravity_gradient'] else {}},['Vehicle'])
    initial=document['initial_state']
    translation=np.array(initial['position_m']+initial['velocity_m_s'])
    rotation=np.array(initial['quaternion']+initial['omega_body_rad_s'])
    # Tudat's integrated scalar-first orientation follows body->inertial and
    # body rates; this was checked with a principal-axis analytical rotation.
    integration=ps.integrator.runge_kutta_fixed_step_size(step,ps.integrator.CoefficientSets.rkdp_87,
                                                         ps.integrator.OrderToIntegrate.higher)
    termination=ps.propagator.time_termination(float(times[-1]),True)
    trans=ps.propagator.translational(['Earth'],accelerations,['Vehicle'],translation,0.,integration,termination)
    rot=ps.propagator.rotational(torques,['Vehicle'],rotation,0.,integration,termination)
    combined=ps.propagator.multitype([trans,rot],integration,0.,termination)
    combined.processing_settings.results_save_frequency_in_steps=0
    combined.processing_settings.results_save_frequency_in_seconds=cadence
    setup_seconds=time.perf_counter()-start
    start=time.perf_counter()
    simulator=create_dynamics_simulator(bodies,combined)
    results=simulator.propagation_results
    solver_seconds=time.perf_counter()-start
    start=time.perf_counter()
    history=results.state_history
    # No velocity gradients or extrapolation: all requested times must be actual
    # saved native states on the explicit grid, including the terminal point.
    states=np.array([history[float(t)] for t in times])
    if states.shape!=(len(times),13) or not np.isfinite(states).all():
        raise RuntimeError('invalid native Tudat trajectory')
    sampling_seconds=time.perf_counter()-start
    return dict(name=document['name'],time_s=times,states=states,
                method='native Tudat RKDP 8(7), higher-order fixed step; no output interpolation',
                step_s=step,constants=dict(mu_m3_s2=MU,equatorial_radius_m=RE,j2=J2,
                                          normalized_C20=-J2/math.sqrt(5.),J2_axis='fixed J2000 +Z'),
                runtime_version=runtime_version,setup_seconds=setup_seconds,
                solver_seconds=solver_seconds,sampling_seconds=sampling_seconds,
                wall_seconds=solver_seconds+sampling_seconds,
                rhs_evaluations=results.total_number_of_function_evaluations)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('scenario',type=Path); p.add_argument('output',type=Path)
    p.add_argument('--step',type=float)
    args=p.parse_args()
    document=json.loads(args.scenario.read_text())
    output=propagate(document,args.step)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(output,default=lambda value:value.tolist(),
                                     separators=(',',':'),allow_nan=False)+'\n')
    print(document['name'],len(output['states']),'states',output['rhs_evaluations'],'RHS')


if __name__=='__main__': main()
