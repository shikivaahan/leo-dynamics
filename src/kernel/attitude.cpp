#include "leo/kernel/attitude.hpp"
#include "leo/kernel/spacecraft.hpp"
#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace leo {
Quaternion normalize_quaternion(Quaternion q) {
    if (!std::isfinite(q.w)||!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z))
        throw std::invalid_argument("nonfinite quaternion");
    const double scale=std::max({std::abs(q.w),std::abs(q.x),std::abs(q.y),std::abs(q.z)});
    if (scale==0) throw std::invalid_argument("near-zero quaternion");
    Quaternion s{q.w/scale,q.x/scale,q.y/scale,q.z/scale};
    const double n=std::sqrt(s.w*s.w+s.x*s.x+s.y*s.y+s.z*s.z);
    if (scale<=1e-12/n) throw std::invalid_argument("near-zero quaternion");
    return {s.w/n,s.x/n,s.y/n,s.z/n};
}
Quaternion quaternion_derivative(Quaternion q,BodyVector o) {
    if (!std::isfinite(q.w)||!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z)||!o.as_eigen().allFinite())
        throw std::invalid_argument("nonfinite quaternion kinematics");
    return {-.5*(q.x*o.x()+q.y*o.y()+q.z*o.z()),
        .5*(q.w*o.x()+q.y*o.z()-q.z*o.y()),
        .5*(q.w*o.y()+q.z*o.x()-q.x*o.z()),
        .5*(q.w*o.z()+q.x*o.y()-q.y*o.x())};
}
InertialVector body_to_inertial(Quaternion q,BodyVector v) {
    q=normalize_quaternion(q);
    if (!v.as_eigen().allFinite()) throw std::invalid_argument("nonfinite vector");
    return InertialVector(Eigen::Quaterniond(q.w,q.x,q.y,q.z)*v.as_eigen());
}
BodyVector inertial_to_body(Quaternion q,InertialVector v) {
    q=normalize_quaternion(q);
    if (!v.as_eigen().allFinite()) throw std::invalid_argument("nonfinite vector");
    return BodyVector(Eigen::Quaterniond(q.w,q.x,q.y,q.z).conjugate()*v.as_eigen());
}
BodyVector torque_free_angular_acceleration(const Inertia& inertia,BodyVector omega) {
    return inertia.solve(-cross(omega,inertia.multiply(omega)));
}
}
