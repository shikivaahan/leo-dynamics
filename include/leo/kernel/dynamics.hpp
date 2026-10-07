#pragma once
#include "leo/kernel/gravity.hpp"
#include "leo/kernel/spacecraft.hpp"
#include "leo/kernel/state.hpp"
#include "leo/kernel/time.hpp"
namespace leo {
class EarthDynamics {
    const EarthGravity gravity_;
    const Spacecraft spacecraft_;
public:
    EarthDynamics(EarthGravity,Spacecraft);
    StateDerivative operator()(ElapsedTime,const State&) const;
};
}
