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
};
}
