#include "leo/kernel/dynamics.hpp"
#include "leo/kernel/aerodynamics.hpp"
#include "leo/kernel/radiation.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>
namespace leo {
EarthDynamics::EarthDynamics(EarthGravity gravity,Spacecraft spacecraft,EnvironmentalModels models)
    :gravity_(std::move(gravity)),spacecraft_(std::move(spacecraft)),models_(models) {
    if (!std::isfinite(models.earth_albedo)||models.earth_albedo<0||models.earth_albedo>1)
        throw std::invalid_argument("Earth albedo must be in [0,1]");
    if (models.drag_model!=DragModel::none&&models.drag_model!=DragModel::flat_plate&&models.drag_model!=DragModel::sentman)
        throw std::invalid_argument("unsupported drag model");
    if (models.drag.enabled()&&models.drag_model==DragModel::none)
        throw std::invalid_argument("enabled drag needs an explicit drag model");
}
bool EarthDynamics::needs_environment() const noexcept {
    return models_.srp.enabled()||models_.drag.enabled()||models_.albedo.enabled();
}
BodyVector gravity_gradient_torque(const Earth& earth,const State& s,const Inertia& inertia) {
    if (!std::isfinite(earth.mu_m3_s2)||earth.mu_m3_s2<=0||!s.r.as_eigen().allFinite())
        throw std::invalid_argument("invalid gravity-gradient input");
    const double radius=norm(s.r);
    if (!std::isfinite(radius)||radius<=0) throw std::invalid_argument("gravity-gradient needs nonzero radius");
    const BodyVector radial=inertial_to_body(s.q,s.r/radius);
    const BodyVector torque=cross(radial,inertia.multiply(radial))*(3*(earth.mu_m3_s2/radius)/radius/radius);
    if (!torque.as_eigen().allFinite()) throw std::invalid_argument("nonfinite gravity-gradient torque");
    return torque;
}
StateDerivative EarthDynamics::operator()(ElapsedTime t,const State& s) const {
    if (needs_environment()) throw std::invalid_argument("enabled environmental models need an explicit snapshot");
    return (*this)(t,s,EnvironmentSnapshot{});
}
StateDerivative EarthDynamics::operator()(ElapsedTime t,const State& s,const EnvironmentSnapshot& env) const {
    if (!std::isfinite(t.seconds)||!s.r.as_eigen().allFinite()||!s.v.as_eigen().allFinite()||!s.omega.as_eigen().allFinite())
        throw std::invalid_argument("nonfinite dynamics input");
    (void)normalize_quaternion(s.q); // Validate only; evolving q must remain raw.
    Wrench total;
    const auto add=[&](const Wrench& contribution,SurfaceContribution selection) {
        if (selection.force) total.force_n=total.force_n+contribution.force_n;
        if (selection.torque) total.torque_nm=total.torque_nm+contribution.torque_nm;
    };
    if (models_.srp.enabled()) add(solar_radiation_pressure(spacecraft_,s,env),models_.srp);
    if (models_.drag.enabled()) {
        add(models_.drag_model==DragModel::flat_plate?flat_plate_drag(spacecraft_,s,env):
            sentman_drag(spacecraft_,s,env),models_.drag);
    }
    if (models_.albedo.enabled())
        add(simplified_albedo(spacecraft_,s,env,gravity_.earth().equatorial_radius_m,models_.earth_albedo),models_.albedo);
    if (models_.gravity_gradient)
        total.torque_nm=total.torque_nm+gravity_gradient_torque(gravity_.earth(),s,spacecraft_.inertia());
    const InertialVector acceleration=gravity_(s.r)+total.force_n/spacecraft_.mass_kg();
    if (!acceleration.as_eigen().allFinite()) throw std::invalid_argument("nonfinite total acceleration");
    return {s.v,acceleration,quaternion_derivative(s.q,s.omega),
            spacecraft_.inertia().solve(total.torque_nm-cross(s.omega,spacecraft_.inertia().multiply(s.omega)))};
}
}
