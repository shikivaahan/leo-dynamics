#pragma once
#include "leo/kernel/attitude.hpp"
namespace leo {
struct State { InertialVector r,v; Quaternion q; BodyVector omega; };
struct StateDerivative { InertialVector r_dot,v_dot; Quaternion q_dot; BodyVector omega_dot; };
State validated_state(State);
}
