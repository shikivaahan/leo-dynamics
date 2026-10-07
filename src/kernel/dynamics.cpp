#include "leo/kernel/dynamics.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>
namespace leo {
EarthDynamics::EarthDynamics(EarthGravity gravity,Spacecraft spacecraft):gravity_(std::move(gravity)),spacecraft_(std::move(spacecraft)) {}
StateDerivative EarthDynamics::operator()(ElapsedTime t,const State& s) const {
    if (!std::isfinite(t.seconds)||!s.r.as_eigen().allFinite()||!s.v.as_eigen().allFinite()||!s.omega.as_eigen().allFinite())
        throw std::invalid_argument("nonfinite dynamics input");
    (void)normalize_quaternion(s.q); // Validate only; evolving q must remain raw.
    return {s.v,gravity_(s.r),quaternion_derivative(s.q,s.omega),torque_free_angular_acceleration(spacecraft_.inertia(),s.omega)};
}
}
