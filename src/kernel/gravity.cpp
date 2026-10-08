#include "leo/kernel/gravity.hpp"
#include <cmath>
#include <stdexcept>
namespace leo {
namespace {
void validate(const Earth& e) {
    if (!std::isfinite(e.mu_m3_s2)||e.mu_m3_s2<=0 || !std::isfinite(e.equatorial_radius_m)||e.equatorial_radius_m<=0 || !std::isfinite(e.j2)||e.j2<0)
        throw std::invalid_argument("invalid Earth constants");
}
struct Radius { double inverse; InertialVector unit; };
Radius radius(InertialVector r) {
    const double length=norm(r);
    if (!r.as_eigen().allFinite()||!std::isfinite(length)||length<=0)
        throw std::invalid_argument("invalid gravity radius");
    return {1/length,r/length};
}
InertialVector checked(InertialVector result) {
    if (!result.as_eigen().allFinite()) throw std::invalid_argument("nonfinite gravity result");
    return result;
}
InertialVector central(const Earth& e,const Radius& r) {
    return r.unit*(-e.mu_m3_s2*r.inverse*r.inverse);
}
InertialVector additive(const Earth& e,const Radius& r) {
    if (e.j2==0) return {};
    const double scaled_radius=e.equatorial_radius_m*r.inverse;
    const double factor=1.5*e.j2*e.mu_m3_s2*r.inverse*r.inverse*scaled_radius*scaled_radius;
    const double zf=5*r.unit.z()*r.unit.z()-1;
    return {factor*r.unit.x()*zf,factor*r.unit.y()*zf,factor*r.unit.z()*(zf-2)};
}
}
InertialVector point_mass_acceleration(const Earth& e,InertialVector r) {
    validate(e); return checked(central(e,radius(r)));
}
InertialVector j2_acceleration(const Earth& e,InertialVector r) {
    validate(e); return checked(additive(e,radius(r)));
}
EarthGravity::EarthGravity(Earth e,bool j2):earth_(e),enable_j2_(j2) { validate(e); }
GravityContributions EarthGravity::evaluate(InertialVector position) const {
    const auto r=radius(position);
    return {checked(central(earth_,r)),enable_j2_?checked(additive(earth_,r)):InertialVector{}};
}
InertialVector EarthGravity::operator()(InertialVector position) const {
    const auto values=evaluate(position);
    return checked(values.point_mass_m_s2+values.j2_m_s2);
}
} // namespace leo
