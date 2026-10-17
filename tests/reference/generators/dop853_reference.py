"""Independent SciPy DOP853; no LEO library or production force calls."""
import copy
import time
import numpy as np
from scipy.integrate import solve_ivp
from physics import Model


def initial_state(document):
    s = document['initial_state']
    return np.array(s['position_m']+s['velocity_m_s']+s['quaternion']+s['omega_body_rad_s'], dtype=float)


def sample_times(document):
    run = document['run']
    if 'sample_times_s' in run:
        result = np.array(run['sample_times_s'], dtype=float)
    else:
        end, step = run['duration_s'], run['cadence_s']
        result = np.r_[np.arange(0.0, end, step), end]
    if len(result) == 0 or result[0] != 0 or not np.isfinite(result).all() or np.any(np.diff(result) <= 0):
        raise ValueError('finite increasing output times starting at zero required')
    return result


def propagate_dop853(document, rtol=1e-12, max_step=10.0, atol_scale=1.0, model=None,
                     internal_history=True):
    """Retain the entire requested trajectory, with separate setup/solver/output costs.

    Absolute tolerances are field-specific SI tolerances, not one arbitrary
    decimal applied equally to metres, quaternions and radians/second. Golden
    generation uses an explicit refinement ladder; ordinary tests never run it.
    """
    setup_start = time.perf_counter()
    model = Model(document) if model is None else model
    y0 = initial_state(document)
    times = sample_times(document)
    atol = np.array([1e-6]*3+[1e-9]*3+[1e-14]*4+[1e-14]*3)*atol_scale
    setup_seconds = time.perf_counter()-setup_start
    begin = time.perf_counter()
    output_options={'dense_output':True} if internal_history else {'t_eval':times,'dense_output':False}
    solution = solve_ivp(model.rhs, (0.0, float(times[-1])), y0, method='DOP853',
                         rtol=rtol, atol=atol, max_step=max_step, **output_options)
    solver_seconds = time.perf_counter()-begin
    if not solution.success:
        raise RuntimeError('DOP853 failed: '+solution.message)
    begin = time.perf_counter()
    states = (solution.sol(times).T if internal_history else solution.y.T).copy()
    sampling_seconds = time.perf_counter()-begin
    if not np.isfinite(states).all():
        raise RuntimeError('nonfinite reference trajectory')
    return {'time_s': times, 'states': states, 'rhs_evaluations': solution.nfev,
            'accepted_steps': len(solution.t)-1 if internal_history else None, 'setup_seconds': setup_seconds,
            'solver_seconds': solver_seconds, 'sampling_seconds': sampling_seconds,
            'wall_seconds': solver_seconds+sampling_seconds,
            'method': 'scipy.integrate.solve_ivp DOP853 (8th order; requested-output retention)',
            'rtol': rtol, 'atol': atol.tolist(), 'max_step_s': max_step}


def perturbed(document, member):
    """Simple deterministic nonidentical batch inputs, mirrored in the C++ harness."""
    result = copy.deepcopy(document)
    s = result['initial_state']
    s['position_m'][0] += 0.25*member
    s['velocity_m_s'][1] += 1e-4*member
    return result
