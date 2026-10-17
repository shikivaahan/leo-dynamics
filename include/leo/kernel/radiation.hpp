#pragma once
#include "leo/kernel/environment.hpp"
namespace leo {
inline constexpr double astronomical_unit_m = 149597870700.0;
inline constexpr double solar_pressure_1au_n_m2 = 1361.0/299792458.0;
// Umbra/penumbra treatment for the fractional direct-solar illumination factor.
enum class EclipseModel { none, cylindrical, conical };
// r and sun are Earth-centred J2000 positions in metres; both models require r
// outside the Earth and sun outside the photosphere, and throw std::invalid_argument
// otherwise. `cylindrical` is the hard-edged shadow cylinder. `conical` uses the
// uniform-brightness overlap of the apparent solar and Earth disks, giving a
// continuous penumbra that reaches exactly 1 outside and exactly 0 in total umbra,
// unlike the Julia tanh(6x) approximation with its jump at both cone boundaries.
// Limitation: small-solar-disk angular-plane overlap, uniform brightness, no
// limb darkening, atmospheric refraction or scattered light within umbra.
double illumination_factor(EclipseModel,InertialVector r,InertialVector sun,
                          double earth_radius_m,double sun_radius_m=696340000.0);
// Optical law per lit surface (c = n.s > 0, s the unit direction toward the source,
// absorption = 1-specular-diffuse, but diffuse reflection also transfers incident momentum):
//   f_body = -P A c [ (1-specular) s + (2 specular c + 2 diffuse/3) n ]
// The 2/3 term is Lambertian recoil. At face-on incidence, absorption gives PA,
// specular reflection 2PA, fully diffuse reflection (5/3)PA, all away from the source.
// Torques derive from the same force via surface_body_torque.
// A zero Earth-to-Sun position is missing data and is rejected, not interpreted
// as a radiation source at the Earth's centre.
Wrench solar_radiation_pressure(const Spacecraft&,const State&,const EnvironmentSnapshot&);
// Smallest defensible radial-beam albedo: Earth-reflected flux is treated as a
// point source at Earth's centre with radial falloff (R/|r|)^2, scaled by the
// local-day cosine max(0, rhat.s_hat) of the true solar direction and by the solar
// inverse-square factor at Earth (AU/|sun|)^2. Incidence uses the direction toward
// Earth (s = -rhat); no direct-Sun eclipse factor applies, since Earth's dayside is
// lit exactly where rhat.s_hat > 0. The optical panel law and the illumination-free
// source geometry match solar_radiation_pressure exactly.
// Julia's albedo torque instead multiplies solar FLUX by (AU/|r|)^2 without
// dividing by light speed, and is inconsistent with its panel force law.
// This radial-beam approximation deliberately omits resolved Earth radiance.
Wrench simplified_albedo(const Spacecraft&,const State&,const EnvironmentSnapshot&,
                         double earth_radius_m,double albedo=0.3);
}