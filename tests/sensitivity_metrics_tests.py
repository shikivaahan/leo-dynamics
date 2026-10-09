#!/usr/bin/env python3
"""Independent geometric/physical identities for the sensitivity analysis tools."""
import importlib.util
import math
from pathlib import Path
import unittest

MODULE = Path(__file__).resolve().parents[1] / "docs/numerical-audit/experiments/metrics.py"
spec = importlib.util.spec_from_file_location("sensitivity_metrics", MODULE)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def state_result(inertia=(2, 3, 4), mass=10, times=(0, 5, 10), areas=(2, 4, 4), illumination=(1, 0, 0)):
    samples, loads = [], []
    for t, area, light in zip(times, areas, illumination):
        samples.append([t, 7e6, 0, 0, 0, 7500, 0, 1, 0, 0, 0, 0, 0, 1, light,
                        0, 0, 0, 0, 0, 0, 0])
        loads.append(dict(sun_position_m=[1.5e11, 0, 0], drag_force_n=[0, 0, 0],
                          albedo_force_n=[0, 0, 0], gravity_gradient_nm=[0, 0, 0],
                          drag_torque_nm=[0, 0, 0], albedo_torque_nm=[0, 0, 0],
                          projected_area_m2=area, density_kg_m3=0,
                          pointing_axis_inertial=[1, 0, 0]))
    return dict(samples=samples, loads=loads, physical=dict(mass_kg=mass, inertia_diagonal_kg_m2=list(inertia)))


class MetricIdentities(unittest.TestCase):
    def test_rotation_distance_is_shortest_arc(self):
        a = (math.cos(math.radians(80)), math.sin(math.radians(80)), 0, 0)
        b = (math.cos(math.radians(-80)), math.sin(math.radians(-80)), 0, 0)
        # Rotations +160 and -160 degrees are 40 degrees apart, not 320.
        self.assertAlmostEqual(m.so3_angle(a, b), math.radians(40), delta=2e-15)
        self.assertAlmostEqual(m.so3_angle(a, tuple(-x for x in b)), math.radians(40), delta=2e-15)
        self.assertLessEqual(m.so3_angle(a, b), math.pi)

    def test_small_direction_angle_is_not_lost(self):
        theta = 1e-10
        self.assertAlmostEqual(m.vangle((1, 0, 0), (math.cos(theta), math.sin(theta), 0)), theta, delta=1e-24)

    def test_each_trajectory_uses_its_own_inertia(self):
        base = state_result(inertia=(2, 3, 4))
        changed = state_result(inertia=(4, 6, 8))
        comparison = m.compare_observables(base, changed)
        self.assertAlmostEqual(comparison["final"]["angular_momentum_norm_kg_m2_s"], 4, delta=1e-14)

    def test_illumination_weighted_area_uses_light_not_only_valid_samples(self):
        summary = m.summarize(state_result())
        self.assertAlmostEqual(summary["environment"]["projected_area_illuminated_average_m2"], 2, delta=1e-14)
        self.assertAlmostEqual(summary["environment"]["effective_projected_area_time_average_m2"], .5, delta=1e-14)

    def test_resolution_compares_response_not_derivative_units(self):
        # A large slope does not resolve a sub-noise response: 100 rad/m over
        # one nanometre means only 1e-7 rad response against 1e-5 rad uncertainty.
        verdict = m.classify_resolution(1e-7, 1e-5, consistency=0)
        self.assertFalse(verdict["resolved"])
        self.assertFalse(m.classify_resolution(0, 0, consistency=0)["resolved"])

    def test_maximum_scalar_error_includes_negative_excursions(self):
        base=state_result(areas=(4,4,4))
        changed=state_result(areas=(3,1,2))
        comparison=m.compare_observables(base,changed)
        self.assertEqual(comparison["final"]["projected_area_m2"],-2)
        self.assertEqual(comparison["maximum"]["projected_area_m2"],3)


if __name__ == "__main__":
    unittest.main()
