"""Orbital energy for the existing fixed-axis Earth gravity law, SI throughout.

Constants match include/leo/kernel/earth.hpp; the potential matches the
independently differentiated fixture in tests/numerical_dynamics_tests.cpp.
This is translational energy, not a closed orbit/attitude Hamiltonian.
"""
import math

MU=3.986004418e14
EARTH_RADIUS=6378136.3
J2=1.08262668e-3


def potential_components(position, j2_enabled):
    radius=math.hypot(*position)
    central=-MU/radius
    correction=MU/radius*J2*(EARTH_RADIUS/radius)**2*(3*(position[2]/radius)**2-1)/2 if j2_enabled else 0.
    return central,correction


def specific_energy_change(initial, sample, j2_enabled):
    """Difference of K+U without subtracting two ~3e7 J/kg energies."""
    r0,r=math.hypot(*initial[1:4]),math.hypot(*sample[1:4])
    kinetic=math.fsum((v-v0)*(v+v0)/2 for v0,v in zip(initial[4:7],sample[4:7]))
    central=(MU/r0)*((r-r0)/r)
    j0=potential_components(initial[1:4],j2_enabled)[1]
    j=potential_components(sample[1:4],j2_enabled)[1]
    return math.fsum((kinetic,central,j-j0))


def account(raw):
    """Compare actual endpoint energy with actual output-sampled force work."""
    samples,loads=raw['samples'],raw['loads']
    physical=raw['physical'];mass=physical['mass_kg'];enabled=physical['j2_enabled']
    changes=[specific_energy_change(samples[0],s,enabled) for s in samples]
    point_mass=[specific_energy_change(samples[0],s,False) for s in samples]
    powers=[]
    for sample,load in zip(samples,loads,strict=True):
        force=[math.fsum((sample[15+k],load['drag_force_n'][k],load['albedo_force_n'][k])) for k in range(3)]
        powers.append(math.fsum(f*v for f,v in zip(force,sample[4:7]))/mass)
    work=[0.]
    for i in range(1,len(samples)):
        work.append(work[-1]+(samples[i][0]-samples[i-1][0])*(powers[i]+powers[i-1])/2)
    residuals=[e-w for e,w in zip(changes,work)]
    span=samples[-1][0]-samples[0][0]
    return dict(j2_enabled=enabled,matched_energy_change_j_kg=changes[-1],
                point_mass_change_j_kg=point_mass[-1],
                j2_potential_change_j_kg=changes[-1]-point_mass[-1],
                non_grav_specific_work_j_kg=work[-1],
                mean_non_grav_specific_power_w_kg=work[-1]/span,
                energy_work_residual_j_kg=residuals[-1],
                maximum_absolute_energy_work_residual_j_kg=max(map(abs,residuals)),
                matched_energy_change_range_j_kg=max(changes)-min(changes))
