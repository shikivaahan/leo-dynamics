#pragma once
#include "leo/kernel/earth.hpp"
#include "leo/kernel/frames.hpp"
namespace leo {
InertialVector point_mass_acceleration(const Earth&,InertialVector r);
InertialVector j2_acceleration(const Earth&,InertialVector r);
class EarthGravity {
    const Earth earth_;
    const bool enable_j2_;
public:
    EarthGravity(Earth,bool enable_j2);
    InertialVector operator()(InertialVector r) const;
};
}
