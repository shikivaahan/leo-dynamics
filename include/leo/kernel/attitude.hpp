#pragma once
#include "leo/kernel/frames.hpp"
namespace leo {
// Hamilton scalar-first, active body-to-J2000 rotation.
struct Quaternion { double w,x,y,z; };
Quaternion normalize_quaternion(Quaternion);
Quaternion quaternion_derivative(Quaternion,BodyVector omega);
InertialVector body_to_inertial(Quaternion,BodyVector);
BodyVector inertial_to_body(Quaternion,InertialVector);
class Inertia;
BodyVector torque_free_angular_acceleration(const Inertia&,BodyVector omega);
}
