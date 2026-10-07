#pragma once
#include <cstddef>
namespace leo {
struct IntegrationOptions {
    double rtol=1e-10;
    double position_atol_m=1e-3,velocity_atol_m_s=1e-6;
    double quaternion_atol=1e-12,omega_atol_rad_s=1e-12;
    double initial_step_s=1,max_step_s=15;
    std::size_t max_attempts=1000000;
};
}
