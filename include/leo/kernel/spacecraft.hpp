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
struct Spacecraft {
    double mass_kg;
    Inertia inertia;
    Spacecraft(double mass_kg,Inertia inertia);
};
}
