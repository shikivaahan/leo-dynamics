#pragma once
#include "leo/kernel/frames.hpp"
#include <Eigen/Cholesky>
namespace leo {
class Inertia {
    Eigen::Matrix3d matrix_;
    Eigen::LLT<Eigen::Matrix3d> factor_;
public:
    explicit Inertia(const Eigen::Matrix3d&);
    BodyVector multiply(BodyVector) const;
    BodyVector solve(BodyVector) const;
};
// Construction establishes mass/inertia invariants; access is read-only.
class Spacecraft {
public:
    Spacecraft(double mass_kg,Inertia inertia);
    double mass_kg() const noexcept { return mass_kg_; }
    const Inertia& inertia() const noexcept { return inertia_; }
private:
    double mass_kg_;
    Inertia inertia_;
};
}
