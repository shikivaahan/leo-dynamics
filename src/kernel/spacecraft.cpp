#include "leo/kernel/spacecraft.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>
namespace leo {
Inertia::Inertia(const Eigen::Matrix3d& m):matrix_(m) {
    if (!m.allFinite() || (m-m.transpose()).cwiseAbs().maxCoeff()>1e-12*m.cwiseAbs().maxCoeff())
        throw std::invalid_argument("inertia must be finite and symmetric");
    factor_.compute(m);
    if (factor_.info()!=Eigen::Success || !(factor_.matrixL().toDenseMatrix().diagonal().array()>0).all())
        throw std::invalid_argument("inertia must be positive definite");
}
BodyVector Inertia::multiply(BodyVector v) const {
    if (!v.as_eigen().allFinite()) throw std::invalid_argument("nonfinite body vector");
    BodyVector result(matrix_*v.as_eigen());
    if (!result.as_eigen().allFinite()) throw std::invalid_argument("nonfinite inertia product");
    return result;
}
BodyVector Inertia::solve(BodyVector v) const {
    if (!v.as_eigen().allFinite()) throw std::invalid_argument("nonfinite body vector");
    BodyVector result(factor_.solve(v.as_eigen()));
    if (!result.as_eigen().allFinite()) throw std::invalid_argument("nonfinite inertia solution");
    return result;
}
Spacecraft::Spacecraft(double mass,Inertia tensor):mass_kg(mass),inertia(std::move(tensor)) {
    if (!std::isfinite(mass)||mass<=0) throw std::invalid_argument("mass must be finite and positive");
}
}
