"""Explicit scenario JSON definitions; shared input data, independently derived states."""
import copy
import math
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
from physics import MU, RE, AU

ROOT = Path(__file__).resolve().parents[3]


def elements(a, e, inc, raan, argp, anomaly):
    p = a*(1-e*e)
    perifocal_r = p/(1+e*math.cos(anomaly))*np.array([math.cos(anomaly), math.sin(anomaly), 0.])
    perifocal_v = math.sqrt(MU/p)*np.array([-math.sin(anomaly), e+math.cos(anomaly), 0.])
    rotation = Rotation.from_euler('ZXZ', [raan, inc, argp]).as_matrix()
    return rotation@perifocal_r, rotation@perifocal_v


def case_data(name):
    """Read immutable CASE-A/B input data, not any engine force function."""
    values, surfaces = {}, []
    for line in (ROOT/'docs/numerical-audit/experiments'/f'{name}-v1.cfg').read_text().splitlines():
        fields = line.split('#',1)[0].split()
        if not fields:
            continue
        key, data = fields[0], fields[1:]
        if key == 'surface':
            row = list(map(float, data))
            surfaces.append(dict(area_m2=row[0], normal=row[1:4], centre=row[4:7],
                                 specular=row[7], diffuse=row[8], drag_coefficient=row[9], accommodation=row[10]))
        elif key not in ('id','version'):
            values[key] = list(map(float, data))
    r, v = elements(*values['orbit_m_rad'])
    radial = r/np.linalg.norm(r)
    normal = np.cross(r,v); normal /= np.linalg.norm(normal)
    transverse = np.cross(normal,radial)
    pitch = math.radians(values['attitude_in_plane_deg'][0])
    z = radial*math.cos(pitch)+transverse*math.sin(pitch)
    y = np.cross(z,normal); y /= np.linalg.norm(y)
    x = np.cross(y,z); x /= np.linalg.norm(x)
    xyzw = Rotation.from_matrix(np.column_stack([x,y,z])).as_quat()
    q = np.r_[xyzw[3],xyzw[:3]]
    state = np.r_[r,v,q,values['omega_body_rad_s']]
    craft = dict(mass_kg=values['mass_kg'][0], inertia_kg_m2=values['inertia_kg_m2'], surfaces=surfaces)
    return state, craft


def base(name, duration=3600., cadence=30.):
    state, craft = case_data('CASE-A')
    return dict(name=name, description=name.replace('_',' '),
                initial_state=dict(position_m=state[:3].tolist(), velocity_m_s=state[3:6].tolist(),
                                   quaternion=state[6:10].tolist(), omega_body_rad_s=state[10:].tolist()),
                spacecraft=craft,
                models=dict(j2=False, gravity_gradient=False, srp=False, drag=False, albedo=False,
                            drag_model='sentman', earth_albedo=0.3, density_scale=1.,
                            density_model='exponential', eclipse_model='none'),
                run=dict(duration_s=duration, cadence_s=cadence,
                         sample_times_s=np.r_[np.arange(0.,duration,cadence),duration].tolist(),
                         tolerances=dict(rtol=1e-11, position_atol_m=1e-4, velocity_atol_m_s=1e-7,
                                         quaternion_atol=1e-13, omega_atol_rad_s=1e-13,
                                         initial_step_s=1., max_step_s=5.)),
                checks=[], environment=dict(sun_position_m=[AU,0.,0.],
                                             atmosphere_angular_velocity_rad_s=[0.,0.,7.292115e-5]))


def set_state(document, state):
    document['initial_state'] = dict(position_m=state[:3].tolist(), velocity_m_s=state[3:6].tolist(),
                                     quaternion=state[6:10].tolist(), omega_body_rad_s=state[10:].tolist())


def build_scenarios():
    result = {}
    for name,e,inc in [('two_body_circular',0.,0.9),('two_body_eccentric',0.03,1.1),
                       ('j2_equatorial',0.,0.),('j2_inclined',0.03,1.2)]:
        d=base(name,21600.)
        r,v=elements(7178137.,e,inc,0.7,0.3,0.2)
        set_state(d,np.r_[r,v,[1.,0.,0.,0.],[0.,0.,0.]])
        d['models']['j2']=name.startswith('j2')
        d['checks']=['j2_invariants'] if d['models']['j2'] else ['kepler','two_body_invariants']
        result[name]=d
    for name,inertia,omega,check in [
        ('torque_free_spherical',[3.,3.,3.],[.01,.02,-.005],'principal_rotation'),
        ('torque_free_symmetric',[3.75,3.75,7.5],[.03,.005,-.002],'symmetric_top'),
        ('torque_free_elongated',[2.,3.,4.],[.002,.001,.0005],'torque_free_invariants')]:
        d=base(name,1800.,5.)
        d['spacecraft']['inertia_kg_m2']=inertia
        d['initial_state']['quaternion']=[1.,0.,0.,0.]
        d['initial_state']['omega_body_rad_s']=omega
        d['checks']=['torque_free_invariants',check]
        result[name]=d
    for name,model in [('gravity_gradient','gravity_gradient'),('srp','srp'),
                       ('albedo','albedo'),('drag_flat_plate','drag'),('drag_sentman','drag'),
                       ('eclipse_conical','srp'),('eclipse_cylindrical','srp')]:
        duration=7200. if name=='gravity_gradient' else 3600.
        d=base(name,duration,10.)
        d['models'][model]=True
        if model=='gravity_gradient':
            d['spacecraft']['inertia_tensor_kg_m2']=[[.10832333333333334,.0003,.0001],
                                                    [.0003,.08333333333333333,-.0002],
                                                    [.0001,-.0002,.041656666666666675]]
        if name=='drag_flat_plate': d['models']['drag_model']='flat_plate'
        if model=='drag':
            r,v=elements(RE+400e3,.001,.9,0.,0.,0.)
            d['initial_state']['position_m']=r.tolist(); d['initial_state']['velocity_m_s']=v.tolist()
        if name.startswith('eclipse'):
            d['models']['eclipse_model']=name.split('_')[1]
            d['environment']['sun_position_m']=[-AU,0.,0.]
        d['checks']=['pointwise_loads']
        result[name]=d
    for name,case in [('coupled_case_a','CASE-A'),('coupled_case_b','CASE-B')]:
        d=base(name,21600.)
        state,craft=case_data(case)
        set_state(d,state); d['spacecraft']=craft
        d['models'].update(j2=True,gravity_gradient=True,srp=True,drag=True,albedo=True,eclipse_model='conical')
        d['checks']=['pointwise_loads','quaternion_norm']
        d['environment']['description']='explicit constant J2000 Sun; not a DE432s ephemeris approximation'
        result[name]=d
    # The initial 1e-11/5 s CASE-B run exceeded the frozen attitude/rate
    # limits. Existing-solver refinement (not limit relaxation) establishes this
    # qualified configuration; see external case-b-convergence.json.
    qualified=result['coupled_case_b']['run']['tolerances']
    qualified.update(rtol=1e-12,max_step_s=2.)
    for field in ('position_atol_m','velocity_atol_m_s','quaternion_atol','omega_atol_rad_s'):
        qualified[field]*=.1
    d=base('case_c_short',3600.)
    radius=RE+400e3
    set_state(d,np.r_[[radius,0.,0.],[0.,.8*math.sqrt(MU/radius),.6*math.sqrt(MU/radius)],
                       [.9238795325112867,.3826834323650898,0.,0.],[.002,.001,.0005]])
    normals=np.r_[np.eye(3),-np.eye(3)]
    d['spacecraft']=dict(mass_kg=10.,inertia_kg_m2=[2.,3.,4.],surfaces=[
        dict(area_m2=2.,normal=n.tolist(),centre=(.5*n+[0.,.1,0.]).tolist(),
             specular=.3,diffuse=.2,drag_coefficient=2.2,accommodation=.9) for n in normals])
    d['models'].update(j2=True,gravity_gradient=True,srp=True,drag=True,albedo=True,eclipse_model='conical')
    d['checks']=['pointwise_loads','quaternion_norm']
    d['scientific_limit']='Six-hour fully coupled CASE-C remains unresolved; this is a shorter matched deterministic-environment qualification only.'
    result[d['name']]=d
    return result
