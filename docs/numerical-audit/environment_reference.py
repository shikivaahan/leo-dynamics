#!/usr/bin/env python3
"""Test-only 60-digit cold-beam molecular momentum oracle.

This constructs incident/specular/diffuse velocities, sums outgoing-minus-incoming
momentum flux and projects onto the lift direction. It does not use the production
factored lift expression. Python floats are converted exactly to mpf, not through
shortest decimal repr strings. Run with mpmath installed to reproduce constants.
"""
import json
import math
import mpmath as mp
mp.mp.dps=60


def coefficients(angle, accommodation):
    a, sigma=mp.mpf(angle), mp.mpf(accommodation)
    incident=(mp.sin(a),mp.cos(a))
    specular=(-incident[0],incident[1])
    diffuse=(mp.mpf(0),mp.mpf(0))
    flux=incident[0]
    force=tuple(flux*((1-sigma)*(specular[i]-incident[i])+sigma*(diffuse[i]-incident[i])) for i in range(2))
    # q=rho*v²/2 with rho=v=A=1, so dimensionless coefficients are -2 F.
    drag=-2*sum(force[i]*incident[i] for i in range(2))
    lift=-2*(force[0]*incident[1]-force[1]*incident[0])
    return drag,lift


def fixtures():
    result=[]
    for angle in (math.radians(15),math.radians(45),math.radians(75)):
        for sigma in (1-1e-6,1-1e-9,1-1e-12,1-1e-15):
            drag,lift=coefficients(angle,sigma)
            result.append(dict(angle_rad=angle,accommodation=sigma,drag=float(drag),lift=float(lift)))
    return result


if __name__=='__main__':
    print(json.dumps(dict(oracle='60-digit incident/outgoing momentum-flux vectors',
                         mpmath_version=mp.__version__,fixtures=fixtures()),indent=2))
