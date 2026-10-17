"""Analytical checks independent of all production and numerical integrators."""
import math
import numpy as np
from physics import MU, RE, J2, product, unit, rotation_error, matrix


def kepler_states(initial, times):
    """Closed elliptic Kepler solution derived from the initial Cartesian orbit."""
    r,v=initial[:3],initial[3:6]
    radius=np.linalg.norm(r)
    h=np.cross(r,v); normal=h/np.linalg.norm(h)
    energy=np.dot(v,v)/2-MU/radius
    a=-MU/(2*energy)
    evec=np.cross(v,h)/MU-r/radius
    eccentricity=np.linalg.norm(evec)
    if eccentricity < 1e-12:
        eccentricity=0.; p=r/radius
    else:
        p=evec/eccentricity
    q=np.cross(normal,p)
    nu0=math.atan2(np.dot(r,q),np.dot(r,p))
    e0=math.atan2(math.sqrt(1-eccentricity**2)*math.sin(nu0),eccentricity+math.cos(nu0))
    m0=e0-eccentricity*math.sin(e0)
    n=math.sqrt(MU/a**3)
    mean=m0+n*np.asarray(times)
    eccentric=mean.copy()
    for _ in range(15):
        eccentric-=(eccentric-eccentricity*np.sin(eccentric)-mean)/(1-eccentricity*np.cos(eccentric))
    x=a*(np.cos(eccentric)-eccentricity)
    y=a*math.sqrt(1-eccentricity**2)*np.sin(eccentric)
    factor=n/(1-eccentricity*np.cos(eccentric))
    velocity=(-a*np.sin(eccentric)*factor)[:,None]*p + (
        a*math.sqrt(1-eccentricity**2)*np.cos(eccentric)*factor)[:,None]*q
    return x[:,None]*p+y[:,None]*q,velocity


def invariants(document, times, states):
    r,v=states[:,:3],states[:,3:6]
    radius=np.linalg.norm(r,axis=1)
    h=np.cross(r,v)
    specific_energy=np.sum(v*v,axis=1)/2-MU/radius
    checks={}
    labels=document['checks']
    if 'kepler' in labels:
        analytic_r,analytic_v=kepler_states(states[0],times)
        checks['kepler_position_max_m']=float(np.linalg.norm(r-analytic_r,axis=1).max())
        checks['kepler_velocity_max_m_s']=float(np.linalg.norm(v-analytic_v,axis=1).max())
    if 'two_body_invariants' in labels:
        checks['specific_energy_relative_drift']=float(np.max(np.abs(specific_energy-specific_energy[0]))/abs(specific_energy[0]))
        checks['specific_momentum_relative_drift']=float(np.linalg.norm(h-h[0],axis=1).max()/np.linalg.norm(h[0]))
    if 'j2_invariants' in labels:
        potential_correction=MU*J2*RE**2/radius**3*(3*(r[:,2]/radius)**2-1)/2
        energy=specific_energy+potential_correction
        checks['j2_energy_relative_drift']=float(np.max(np.abs(energy-energy[0]))/abs(energy[0]))
        checks['axial_momentum_relative_drift']=float(np.max(np.abs(h[:,2]-h[0,2]))/np.linalg.norm(h[0]))
    if 'torque_free_invariants' in labels:
        c=document['spacecraft']
        inertia=np.array(c.get('inertia_tensor_kg_m2',np.diag(c['inertia_kg_m2'])))
        omega=states[:,10:13]
        body_momentum=omega@inertia.T
        inertial=np.array([matrix(q)@l for q,l in zip(states[:,6:10],body_momentum)])
        energy=np.sum(omega*body_momentum,axis=1)/2
        checks['inertial_momentum_relative_drift']=float(np.linalg.norm(inertial-inertial[0],axis=1).max()/np.linalg.norm(inertial[0]))
        checks['kinetic_energy_relative_drift']=float(np.max(np.abs(energy-energy[0]))/energy[0])
    if 'principal_rotation' in labels:
        omega=states[0,10:13]; speed=np.linalg.norm(omega)
        exact=np.array([product(states[0,6:10],np.r_[math.cos(speed*t/2),omega/speed*math.sin(speed*t/2)]) for t in times])
        checks['analytic_attitude_max_rad']=float(max(rotation_error(a,b) for a,b in zip(states[:,6:10],exact)))
        checks['analytic_rate_max_rad_s']=float(np.linalg.norm(states[:,10:13]-omega,axis=1).max())
    if 'symmetric_top' in labels:
        i=document['spacecraft']['inertia_kg_m2']
        w=states[0,10:13]
        freq=(i[2]-i[0])/i[0]*w[2]
        exact=np.column_stack([w[0]*np.cos(freq*times)-w[1]*np.sin(freq*times),
                               w[0]*np.sin(freq*times)+w[1]*np.cos(freq*times),np.full(len(times),w[2])])
        checks['analytic_rate_max_rad_s']=float(np.linalg.norm(states[:,10:13]-exact,axis=1).max())
    checks['quaternion_norm_max_error']=float(np.max(np.abs(np.linalg.norm(states[:,6:10],axis=1)-1)))
    return checks
