#pragma once
#include "leo/kernel/earth.hpp"
#include "leo/kernel/frames.hpp"
namespace leo {
InertialVector point_mass_acceleration(const Earth&,InertialVector r);
InertialVector j2_acceleration(const Earth&,InertialVector r);
struct GravityContributions {
    InertialVector point_mass_m_s2,j2_m_s2;
};
class EarthGravity {
    const Earth earth_;
    const bool enable_j2_;
public:
    EarthGravity(Earth,bool enable_j2);
    InertialVector operator()(InertialVector r) const;
    GravityContributions evaluate(InertialVector r) const;
    bool j2_enabled() const noexcept { return enable_j2_; }
    const Earth& earth() const noexcept { return earth_; }
};
}
