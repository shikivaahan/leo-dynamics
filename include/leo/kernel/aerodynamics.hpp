#pragma once
#include "leo/kernel/environment.hpp"
#include "leo/kernel/frames.hpp"
#include "leo/kernel/spacecraft.hpp"
#include "leo/kernel/state.hpp"
namespace leo {
// Velocity of the craft relative to the co-rotating atmosphere, v - omega x r.
// All inputs are inertial (J2000); the rotation vector is in rad/s about J2000 +Z by default.
InertialVector atmospheric_relative_velocity(InertialVector r,InertialVector v,
                                             InertialVector rotation={0,0,7.292115e-5});

// Spherical exponential intervals through the altitude/density knots of Julia
// Constants.jl (0 .. 35786 km). Log-linear interpolation re-derives scale heights
// from adjacent densities, avoiding the reference's nonphysical upward jumps.
// The model is spherically symmetric and carries no latitude/solar/local-time
// dependence; density_scale lets an environment source scale or mute the profile.
class ExponentialAtmosphere {
    double density_scale_;
public:
    static constexpr double min_altitude_m=0.0;
    static constexpr double max_altitude_m=3.5786e7;
    ExponentialAtmosphere(double density_scale=1);
    // Throws std::invalid_argument on nonfinite input or a nonfinite/negative scale,
    // std::out_of_range outside [min_altitude_m, max_altitude_m] (never a silent zero).
    double density_kg_m3(double altitude_m) const;
};

// Newtonian flat-plate model per surface: F = -0.5 rho |v_rel|^2 Cd A max(0, n.v_hat) v_hat.
// A surface sees the flow when its normal has n.v_hat > 0 (normal facing into the wind).
// Uses EnvironmentSnapshot::density_kg_m3 and atmosphere_angular_velocity_rad_s only;
// no SPICE or ephemeris access happens inside the force law.
Wrench flat_plate_drag(const Spacecraft&,const State&,const EnvironmentSnapshot&);

// Free-molecular (Sentman) limit for a flat plate, specular fraction 1-sigma and
// diffuse fraction sigma with negligible re-emission thermal speed; the missing
// thermodynamic parameters are deliberately not invented:
//   Cn = (4 - 2 sigma) sin(alpha)^2            normal momentum-exchange coefficient
//   Ct = 2 sigma sin(alpha) cos(alpha)          tangential momentum-exchange coefficient
//   Cd = Cn sin(alpha) + Ct cos(alpha)          along the incoming flow
//   Cl = Cn cos(alpha) - Ct sin(alpha)          perpendicular to it, in the flow/normal plane
// alpha is the grazing angle: 0 = flow parallel to the plate, pi/2 = face-on incidence.
// sigma = 1 is fully diffuse with cold re-emission (Cd = 2 sin(alpha), Cl = 0),
// sigma = 0 to a purely normal specular impulse (Cd = 4 sin(alpha)^3, Cl = 4 sin(alpha)^2 cos(alpha)).
// Unlike the reference Julia coefficients this face-on limit is nonzero and physical.
struct SentmanCoefficients { double drag,lift; };
// Throws std::invalid_argument unless the angle lies in [0, pi/2] and accommodation in [0, 1].
SentmanCoefficients sentman_coefficients(double grazing_angle_rad,double accommodation=0.9);

// Per-surface Sentman wrench F = -0.5 rho |v_rel|^2 A (Cn n + Ct t_hat) with t_hat the
// incoming-flow unit direction tangential to the plate. Front surfaces only (n.v_hat > 0).
// No lift direction is reconstructed, so the force stays exact when the flow is axis aligned.
Wrench sentman_drag(const Spacecraft&,const State&,const EnvironmentSnapshot&);

}