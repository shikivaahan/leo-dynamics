#pragma once
#include "leo/kernel/frames.hpp"
#include <Eigen/Cholesky>
#include <span>
#include <vector>
namespace leo {
class Inertia {
    Eigen::Matrix3d matrix_;
    Eigen::LLT<Eigen::Matrix3d> factor_;
public:
    explicit Inertia(const Eigen::Matrix3d&);
    BodyVector multiply(BodyVector) const;
    BodyVector solve(BodyVector) const;
};
// Fractions of incident optical power; absorption is 1-specular-diffuse.
class OpticalProperties {
    double specular_,diffuse_;
public:
    OpticalProperties(double specular=0,double diffuse=0);
    double specular() const noexcept { return specular_; }
    double diffuse() const noexcept { return diffuse_; }
};
// The Julia Sentman approximation uses only momentum accommodation, not temperature.
class AerodynamicProperties {
    double drag_coefficient_,accommodation_;
public:
    AerodynamicProperties(double drag_coefficient=2.2,double accommodation=0.9);
    double drag_coefficient() const noexcept { return drag_coefficient_; }
    double accommodation() const noexcept { return accommodation_; }
};
// One-sided plate; represent a two-sided panel with two opposing surfaces.
class Surface {
    double area_m2_;
    BodyVector normal_body_,center_of_pressure_body_;
    OpticalProperties optical_;
    AerodynamicProperties aerodynamic_;
public:
    Surface(double area_m2,BodyVector unit_normal_body,BodyVector center_of_pressure_body,
            OpticalProperties optical={},AerodynamicProperties aerodynamic={});
    double area_m2() const noexcept { return area_m2_; }
    BodyVector normal_body() const noexcept { return normal_body_; }
    BodyVector center_of_pressure_body() const noexcept { return center_of_pressure_body_; }
    const OpticalProperties& optical() const noexcept { return optical_; }
    const AerodynamicProperties& aerodynamic() const noexcept { return aerodynamic_; }
};
// Construction establishes mass/inertia invariants; access is read-only.
class Spacecraft {
public:
    Spacecraft(double mass_kg,Inertia inertia,std::vector<Surface> surfaces={});
    double mass_kg() const noexcept { return mass_kg_; }
    const Inertia& inertia() const noexcept { return inertia_; }
    std::span<const Surface> surfaces() const noexcept { return surfaces_; }
private:
    double mass_kg_;
    Inertia inertia_;
    std::vector<Surface> surfaces_;
};
}
