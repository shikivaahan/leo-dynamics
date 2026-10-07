#pragma once
#include "leo/kernel/state.hpp"
#include "leo/kernel/spacecraft.hpp"
#include "leo/kernel/time.hpp"
namespace leo {
// Explicit SI data, acquired outside the force laws.
struct EnvironmentSnapshot {
    Epoch epoch{0};
    InertialVector sun_position_m{};
    double illumination=1;
    double density_kg_m3=0;
    InertialVector atmosphere_angular_velocity_rad_s{0,0,7.292115e-5};
};
struct Wrench {
    InertialVector force_n{};
    BodyVector torque_nm{};
    Wrench& operator+=(const Wrench& other) {
        force_n=force_n+other.force_n;
        torque_nm=torque_nm+other.torque_nm;
        return *this;
    }
};
// All surface models use this single force -> body torque boundary.
inline BodyVector surface_body_torque(const Surface& surface,BodyVector force_body_n) {
    return cross(surface.center_of_pressure_body(),force_body_n);
}
inline Wrench surface_wrench(const Surface& surface,Quaternion q,BodyVector force_body_n) {
    return {body_to_inertial(q,force_body_n),surface_body_torque(surface,force_body_n)};
}
} 
