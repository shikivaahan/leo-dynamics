#pragma once
#include "leo/kernel/gravity.hpp"
#include "leo/kernel/spacecraft.hpp"
#include "leo/kernel/state.hpp"
#include "leo/kernel/time.hpp"
#include "leo/kernel/environment.hpp"
namespace leo {
struct SurfaceContribution {
    bool force=false;
    bool torque=false;
    bool enabled() const noexcept { return force||torque; }
};
enum class DragModel { none,flat_plate,sentman };
struct EnvironmentalModels {
    SurfaceContribution srp{},drag{},albedo{};
    DragModel drag_model=DragModel::none;
    bool gravity_gradient=false;
    double earth_albedo=0.3;
};
// Selected contributions, not hypothetical loads from disabled models.
// Forces/accelerations are J2000; torques/angular acceleration are body frame.
struct DynamicsContributions {
    InertialVector point_mass_m_s2{},j2_m_s2{};
    Wrench srp{},drag{},albedo{};
    BodyVector gravity_gradient_nm{};
    Wrench non_gravitational{};
    InertialVector total_acceleration_m_s2{};
    BodyVector total_angular_acceleration_rad_s2{};
};
BodyVector gravity_gradient_torque(const Earth&,const State&,const Inertia&);
class EarthDynamics {
    EarthGravity gravity_;
    Spacecraft spacecraft_;
    EnvironmentalModels models_;
public:
    // Validates drag selection and albedo bounds, not environment/source compatibility.
    // SRP/albedo require solar data; either drag law requires density and rotation.
    EarthDynamics(EarthGravity,Spacecraft,EnvironmentalModels={});
    // Throws std::invalid_argument if SRP, drag or albedo force/torque is enabled
    // without using the explicit-snapshot overload below.
    StateDerivative operator()(ElapsedTime,const State&) const;
    // Environment/geometry validity is checked during model evaluation, not construction.
    StateDerivative operator()(ElapsedTime,const State&,const EnvironmentSnapshot&) const;
    // Reports model selection only; does not establish source compatibility.
    bool needs_environment() const noexcept;
    DynamicsContributions evaluate(ElapsedTime,const State&,const EnvironmentSnapshot&) const;
    const EarthGravity& gravity() const noexcept { return gravity_; }
    const Spacecraft& spacecraft() const noexcept { return spacecraft_; }
    const EnvironmentalModels& models() const noexcept { return models_; }
};
}
