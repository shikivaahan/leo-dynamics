#!/usr/bin/env python3
"""Independent 80-digit Cartesian disk-intersection quadrature.

Requires test-only mpmath; production and CI regressions use generated constants.
Usage: python eclipse_reference.py --probe /path/to/eclipse_probe --output NEW.json
The probe prints actual binary64 Cartesian geometry plus production illumination.
Output paths must not exist, to preserve previous evidence.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import mpmath as mp
mp.mp.dps = 80


def reference(re, rs, sun_x, sun_y, observer_radius=7e6):
    r = mp.mpf(observer_radius)
    sx, sy = mp.mpf(sun_x) + r, mp.mpf(sun_y)
    alpha = mp.asin(mp.mpf(rs) / mp.sqrt(sx*sx + sy*sy))
    beta = mp.asin(mp.mpf(re) / r)
    separation = mp.atan2(abs(sy), sx)
    if separation >= alpha + beta:
        return mp.mpf(1)
    if separation <= abs(alpha-beta):
        return 1 - (min(alpha,beta)/alpha)**2
    # Integrate the width of the intersection in Cartesian coordinates, not
    # circular-sector angles, acos/lens terms or the production segment series.
    earth_radius, distance = beta/alpha, separation/alpha
    left = max(-mp.mpf(1), distance-earth_radius)
    right = min(mp.mpf(1), distance+earth_radius)
    cross = (distance*distance + 1-earth_radius*earth_radius)/(2*distance)
    boundaries = sorted(set((left, right, max(left, min(right, cross)))))
    def height(x):
        return min(mp.sqrt(max(mp.mpf(0), 1-x*x)),
                   mp.sqrt(max(mp.mpf(0), earth_radius*earth_radius-(x-distance)**2)))
    area = mp.mpf(0)
    for low, high in zip(boundaries, boundaries[1:]):
        width = high-low
        # sin² substitution removes square-root endpoint singularities.
        area += mp.quad(lambda t: 4*height(low+width*mp.sin(t)**2)*width*mp.sin(t)*mp.cos(t),
                        [0, mp.pi/2])
    return 1-area/mp.pi


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--probe', required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.output.exists():
        p.error('output already exists; preserve earlier evidence')
    re, rs, sun_distance = 6378137., 696340000., 149597870700.
    alpha, beta = math.asin(rs/sun_distance), math.asin(re/7e6)
    fractions = [1e-10,1e-8,1e-6,.001,.01,.1,.5,.9,.99,.999,1-1e-6,1-1e-8,1-1e-10]
    angles = [beta-alpha+f*2*alpha for f in fractions]
    text = ''.join(f'{re:.17g} {rs:.17g} {d:.17g}\n' for d in angles)
    result = subprocess.run([args.probe], input=text, text=True, capture_output=True, check=True)
    fixtures = []
    for line, requested in zip(result.stdout.splitlines(), angles):
        er,sr,x,y,production = map(float, line.split())
        expected = float(reference(er,sr,x,y))
        fixtures.append(dict(earth_radius_m=er,sun_radius_m=sr,sun_position_m=[x,y,0],
                             spacecraft_position_m=[-7e6,0,0],separation_requested_rad=requested,
                             reference=expected,production=production,absolute_error=abs(production-expected)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(oracle='80-digit Cartesian vertical-width quadrature',
                                         mpmath_version=mp.__version__,fixtures=fixtures), indent=2)+'\n')
    print('Maximum absolute illumination error:', max(f['absolute_error'] for f in fixtures))


if __name__ == '__main__':
    main()
