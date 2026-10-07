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
OpticalProperties::OpticalProperties(double specular,double diffuse):specular_(specular),diffuse_(diffuse) {
    if (!std::isfinite(specular)||!std::isfinite(diffuse)||specular<0||diffuse<0||specular+diffuse>1)
        throw std::invalid_argument("optical fractions must be finite, nonnegative and sum to at most one");
}
AerodynamicProperties::AerodynamicProperties(double cd,double accommodation):drag_coefficient_(cd),accommodation_(accommodation) {
    if (!std::isfinite(cd)||cd<0||!std::isfinite(accommodation)||accommodation<0||accommodation>1)
        throw std::invalid_argument("invalid aerodynamic coefficient or accommodation");
}
Surface::Surface(double area,BodyVector normal,BodyVector center,OpticalProperties optical,AerodynamicProperties aerodynamic)
    :area_m2_(area),normal_body_(normal),center_of_pressure_body_(center),optical_(optical),aerodynamic_(aerodynamic) {
    if (!std::isfinite(area)||area<=0||!normal.as_eigen().allFinite()||!center.as_eigen().allFinite()||
        std::abs(norm(normal)-1)>1e-12)
        throw std::invalid_argument("surface requires positive finite area, unit normal and finite application point");
}
Spacecraft::Spacecraft(double mass,Inertia tensor,std::vector<Surface> surfaces)
    :mass_kg_(mass),inertia_(std::move(tensor)),surfaces_(std::move(surfaces)) {
    if (!std::isfinite(mass)||mass<=0) throw std::invalid_argument("mass must be finite and positive");
}
}
