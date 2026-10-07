#pragma once
#include "leo/kernel/frames.hpp"
namespace leo {
// Bound elliptic elements; all angular fields are radians.
struct ClassicalElements { double a_m,e,i_rad,raan_rad,arg_periapsis_rad,true_anomaly_rad; };
struct CartesianOrbit { InertialVector r,v; };
CartesianOrbit coe_to_cartesian(const ClassicalElements&,double mu_m3_s2);
// Singular angular gauges: RAAN=0 if equatorial, argp=0 if circular.
// Circular nu is argument of latitude, or orbital-direction longitude.
ClassicalElements cartesian_to_coe(CartesianOrbit,double mu_m3_s2);
}
