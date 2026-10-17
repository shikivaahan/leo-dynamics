"""Independent SI equations for qualification, never linked to LEO Dynamics.

Model definitions (Earth constants, density knots, cold-beam and radial-albedo
approximations) are deliberately shared specifications, not independently
measured physical data. Algebra, integration and eclipse quadrature are separate
implementations. Quaternion convention: Hamilton wxyz, active body -> J2000.
"""
from dataclasses import dataclass
import math
import numpy as np

MU = 3.986004418e14
RE = 6378136.3
J2 = 1.08262668e-3
AU = 149597870700.0
SUN_RADIUS = 696340000.0
PRESSURE = 1361.0 / 299792458.0
ROTATION = np.array([0.0, 0.0, 7.292115e-5])
# These knots specify the existing exponential model, not an atmospheric oracle.
KNOTS = np.array([
    [0,1.225], [100,5.25e-7], [150,1.73e-9], [200,2.41e-10],
    [250,5.97e-11], [300,1.87e-11], [350,6.66e-12], [400,2.62e-12],
    [450,1.09e-12], [500,4.76e-13], [550,2.14e-13], [600,9.89e-14],
    [650,4.73e-14], [700,2.36e-14], [750,1.24e-14], [800,6.95e-15],
    [850,4.22e-15], [900,2.78e-15], [950,1.98e-15], [1000,1.49e-15],
    [1250,5.70e-16], [1500,2.79e-16], [2000,9.09e-17], [2500,4.23e-17],
    [3000,2.54e-17], [3500,1.77e-17], [4000,1.34e-17], [4500,1.06e-17],
    [5000,8.62e-18], [6000,6.09e-18], [7000,4.56e-18], [8000,3.56e-18],
    [9000,2.87e-18], [10000,2.37e-18], [15000,1.21e-18], [20000,7.92e-19],
    [25000,5.95e-19], [30000,4.83e-19], [35000,4.13e-19], [35786,4.04e-19]])
LOG_RHO = np.log(KNOTS[:, 1])


def unit(q):
    q = np.asarray(q, dtype=float)
    n = np.linalg.norm(q)
    if not np.isfinite(n) or n == 0:
        raise ValueError('zero/nonfinite quaternion')
    return q / n


def product(a, b):
    return np.r_[a[0]*b[0] - np.dot(a[1:], b[1:]),
                 a[0]*b[1:] + b[0]*a[1:] + np.cross(a[1:], b[1:])]


def matrix(q):
    """Active rotation via Rodrigues' quaternion formula."""
    w, x, y, z = unit(q)
    v = np.array([x, y, z])
    skew = np.array([[0, -z, y], [z, 0, -x], [-y, x, 0]])
    return (w*w - np.dot(v, v))*np.eye(3) + 2*np.outer(v, v) + 2*w*skew


def rotation_error(a, b):
    """Shortest SO(3) angle; stable at zero and invariant under q -> -q."""
    a, b = unit(a), unit(b)
    if np.dot(a, b) < 0:
        b = -b
    relative = product(np.r_[a[0], -a[1:]], b)
    return 2*math.atan2(float(np.linalg.norm(relative[1:])), abs(float(relative[0])))


def density(altitude_m, scale=1.0):
    if not np.isfinite(altitude_m) or not 0 <= altitude_m <= 35786000:
        raise ValueError('outside exponential atmosphere domain')
    if not np.isfinite(scale) or scale < 0:
        raise ValueError('invalid density scale')
    return float(scale*np.exp(np.interp(altitude_m/1000, KNOTS[:, 0], LOG_RHO)))


def gravity(r, j2=False):
    r = np.asarray(r, dtype=float)
    length = np.linalg.norm(r)
    if not np.isfinite(length) or length == 0:
        raise ValueError('invalid gravity radius')
    central = -MU*r/length**3
    additive = np.zeros(3)
    if j2:
        z2 = r[2]**2/length**2
        additive = (1.5*MU*J2*RE**2/length**5)*r*np.array([5*z2-1, 5*z2-1, 5*z2-3])
    return central, additive


def disk_visibility(alpha, beta, separation):
    """Independent adaptive integration of visible solar-disk vertical strips.

    Integrates the *visible* complement rather than copying the engine's circular
    segment series. Coordinates are scaled by solar angular radius; the Earth
    disk is centred at x=d. Split at contacts and the intersecting chord.
    """
    if separation >= alpha + beta:
        return 1.0
    if separation <= abs(beta-alpha):
        return max(0.0, 1 - min(alpha, beta)**2/alpha**2)
    from scipy.integrate import quad
    k, d = beta/alpha, separation/alpha
    left = (separation-beta)/alpha
    right = (separation+beta)/alpha
    chord = (1 + left*right)/(2*d)
    boundaries = sorted(set([-1.0, 1.0] + [x for x in (left, right, chord) if -1 < x < 1]))

    def visible(x):
        solar2 = max(0.0, 1-x*x)
        solar = math.sqrt(solar2)
        if x < left or x > right:
            return 2*solar/math.pi
        earth2 = max(0.0, (x-left)*(right-x))
        if earth2 >= solar2:
            return 0.0
        return 2*(solar2-earth2)/(solar+math.sqrt(earth2))/math.pi

    total = math.fsum(quad(visible, lo, hi, epsabs=2e-13, epsrel=2e-12)[0]
                      for lo, hi in zip(boundaries[:-1], boundaries[1:]))
    return min(1.0, max(0.0, total))


def illumination(r, sun, model):
    if model == 'none':
        return 1.0
    length, dsun = np.linalg.norm(r), np.linalg.norm(sun)
    if length <= RE or dsun <= SUN_RADIUS:
        raise ValueError('invalid eclipse geometry')
    if model == 'cylindrical':
        along = np.dot(r, sun/dsun)
        return 0.0 if along < 0 and np.linalg.norm(r-along*sun/dsun) < RE else 1.0
    if model != 'conical':
        raise ValueError('unknown eclipse model')
    to_sun = sun-r
    alpha = math.asin(SUN_RADIUS/np.linalg.norm(to_sun))
    beta = math.asin(RE/length)
    earth_direction, sun_direction = -r/length, to_sun/np.linalg.norm(to_sun)
    separation = math.atan2(np.linalg.norm(np.cross(earth_direction, sun_direction)),
                            np.dot(earth_direction, sun_direction))
    return disk_visibility(alpha, beta, separation)


@dataclass
class Model:
    """Prepared independent reference; inputs are authoritative scenario JSON."""
    document: dict

    def __post_init__(self):
        d, c = self.document, self.document['spacecraft']
        self.mass = float(c['mass_kg'])
        self.inertia = np.array(c.get('inertia_tensor_kg_m2', np.diag(c['inertia_kg_m2'])), dtype=float)
        self.inverse = np.linalg.inv(self.inertia)
        self.modes = d['models']
        self.environment = d.get('environment', {})
        self.surfaces = c['surfaces']
        self.normals = np.array([s['normal'] for s in self.surfaces], dtype=float).reshape(-1, 3)
        self.centres = np.array([s['centre'] for s in self.surfaces], dtype=float).reshape(-1, 3)
        self.areas = np.array([s['area_m2'] for s in self.surfaces])
        self.specular = np.array([s['specular'] for s in self.surfaces])
        self.diffuse = np.array([s['diffuse'] for s in self.surfaces])
        self.cd = np.array([s['drag_coefficient'] for s in self.surfaces])
        self.sigma = np.array([s['accommodation'] for s in self.surfaces])
        if (self.modes['srp'] or self.modes['albedo']) and 'sun_position_m' not in self.environment:
            raise ValueError('selected radiation requires explicit Sun data')

    def optical(self, qmatrix, direction, pressure):
        source = qmatrix.T @ direction
        cosine = np.maximum(0.0, self.normals @ source)
        absorbed_plus_diffuse = 1-self.specular
        normal_recoil = 2*self.specular*cosine+2*self.diffuse/3
        panel = -(pressure*self.areas*cosine)[:, None]*(
            absorbed_plus_diffuse[:, None]*source + normal_recoil[:, None]*self.normals)
        return qmatrix@panel.sum(axis=0), np.cross(self.centres, panel).sum(axis=0)

    def contributions(self, t, y):
        r, v, q, omega = y[:3], y[3:6], y[6:10], y[10:13]
        rot = matrix(q)
        central, j2term = gravity(r, self.modes['j2'])
        result = {'point_mass_m_s2': central, 'j2_m_s2': j2term}
        for key in ('srp_force_n','srp_torque_nm','drag_force_n','drag_torque_nm',
                    'albedo_force_n','albedo_torque_nm','gravity_gradient_nm'):
            result[key] = np.zeros(3)
        env = self.environment
        if self.modes['gravity_gradient']:
            radial = rot.T@(r/np.linalg.norm(r))
            result['gravity_gradient_nm'] = (3*MU/np.linalg.norm(r)**3)*np.cross(radial, self.inertia@radial)
        light, rho = 1.0, 0.0
        if self.modes['srp'] or self.modes['albedo']:
            sun = np.array(env['sun_position_m'], dtype=float)
            if np.linalg.norm(sun) == 0:
                raise ValueError('zero Sun position')
            light = (float(env.get('illumination',1.0)) if self.modes.get('eclipse_model','none')=='none'
                     else illumination(r,sun,self.modes['eclipse_model']))
        if self.modes['srp']:
            towards = sun-r
            dist = np.linalg.norm(towards)
            f, tau = self.optical(rot, towards/dist, PRESSURE*(AU/dist)**2*light)
            result['srp_force_n'], result['srp_torque_nm'] = f, tau
        if self.modes['albedo']:
            radius = np.linalg.norm(r)
            daylight = max(0.0, np.dot(r/radius, sun/np.linalg.norm(sun)))
            pressure = PRESSURE*(AU/np.linalg.norm(sun))**2*self.modes['earth_albedo']*(RE/radius)**2*daylight
            result['albedo_force_n'], result['albedo_torque_nm'] = self.optical(rot, -r/radius, pressure)
        if self.modes['drag']:
            if self.modes.get('density_model','exponential') == 'constant':
                rho = float(env['density_kg_m3'])
            else:
                rho = density(np.linalg.norm(r)-RE, self.modes['density_scale'])
            relative = v-np.cross(env.get('atmosphere_angular_velocity_rad_s',ROTATION), r)
            speed = np.linalg.norm(relative)
            if speed > 0:
                incoming = rot.T@(relative/speed)
                cosine = np.maximum(0.0, self.normals@incoming)
                if self.modes['drag_model'] == 'flat_plate':
                    exchange = self.cd[:, None]*cosine[:, None]*incoming
                elif self.modes['drag_model'] == 'sentman':
                    exchange = (4*(1-self.sigma)*cosine**2)[:, None]*self.normals + (2*self.sigma*cosine)[:, None]*incoming
                else:
                    raise ValueError('unknown drag law')
                panels = -(0.5*rho*speed**2*self.areas)[:, None]*exchange
                result['drag_force_n'] = rot@panels.sum(axis=0)
                result['drag_torque_nm'] = np.cross(self.centres, panels).sum(axis=0)
        for model in ('srp','drag','albedo'):
            for part in ('force','torque'):
                if not self.modes.get(model+'_'+part, self.modes[model]):
                    result[model+'_'+part+('_n' if part == 'force' else '_nm')] = np.zeros(3)
        result['total_acceleration_m_s2'] = central+j2term+sum(result[k+'_force_n'] for k in ('srp','drag','albedo'))/self.mass
        torque = result['gravity_gradient_nm']+sum(result[k+'_torque_nm'] for k in ('srp','drag','albedo'))
        result['total_angular_acceleration_rad_s2'] = self.inverse@(torque-np.cross(omega, self.inertia@omega))
        result['illumination'], result['density_kg_m3'] = light, rho
        return result

    def rhs(self, t, y):
        loads = self.contributions(t, y)
        # Raw evolving quaternion, not projection of the DOP853 stage state.
        w, x, yy, z = y[6:10]
        a, b, c = y[10:13]
        qdot = 0.5*np.array([-x*a-yy*b-z*c, w*a+yy*c-z*b,
                             w*b+z*a-x*c, w*c+x*b-yy*a])
        return np.r_[y[3:6], loads['total_acceleration_m_s2'], qdot,
                     loads['total_angular_acceleration_rad_s2']]
