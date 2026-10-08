#pragma once
#include "leo/environment/ephemeris.hpp"
#include "leo/kernel/aerodynamics.hpp"
#include "leo/kernel/dynamics.hpp"
#include "leo/kernel/radiation.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>
namespace leo {
// The provider is borrowed and must outlive the source. A null provider means no solar acquisition.
// Epoch is explicit; elapsed solver seconds never masquerade as UTC or absolute ephemeris time.
// Does not validate compatibility with enabled dynamics models. SRP/albedo need
// a covering Sun provider here (or solar data from another snapshot source);
// null leaves a zero Sun position, which may cause evaluation failure or incorrect
// SRP geometry without an immediate error. Atmospheric acquisition must be enabled
// for model density; disabled leaves zero density and hence zero drag.
// Acquisition is independent of model switches. Geometry, atmosphere-domain and
// provider/coverage errors occur during evaluation and propagate to the caller.
class EarthEnvironment {
    Earth earth_;
    Epoch epoch_;
    const EphemerisProvider* ephemeris_;
    bool atmosphere_;
    EclipseModel eclipse_;
    ExponentialAtmosphere density_;
    InertialVector rotation_;
public:
    EarthEnvironment(Earth earth,Epoch epoch,const EphemerisProvider* ephemeris=nullptr,
                     bool atmosphere=false,EclipseModel eclipse=EclipseModel::conical,
                     ExponentialAtmosphere density={},
                     InertialVector rotation={0,0,7.292115e-5})
        :earth_(earth),epoch_(epoch),ephemeris_(ephemeris),atmosphere_(atmosphere),
         eclipse_(eclipse),density_(std::move(density)),rotation_(rotation) {
        (void)EarthGravity(earth,false);
        if (!std::isfinite(epoch.et_seconds)||!rotation.as_eigen().allFinite())
            throw std::invalid_argument("invalid environment epoch or rotation");
        if (eclipse!=EclipseModel::none&&eclipse!=EclipseModel::cylindrical&&eclipse!=EclipseModel::conical)
            throw std::invalid_argument("unsupported eclipse model");
    }
    EnvironmentSnapshot operator()(ElapsedTime t,const State& s) const {
        const double et=epoch_.et_seconds+t.seconds;
        if (!std::isfinite(t.seconds)||!std::isfinite(et)||!s.r.as_eigen().allFinite())
            throw std::invalid_argument("invalid environment time or position");
        EnvironmentSnapshot snapshot;
        snapshot.epoch={et};
        snapshot.atmosphere_angular_velocity_rad_s=rotation_;
        if (ephemeris_) {
            const auto sun=ephemeris_->geometric_state(10,snapshot.epoch);
            if (!sun.position_m.as_eigen().allFinite()||!sun.velocity_m_s.as_eigen().allFinite()||norm(sun.position_m)<=0)
                throw std::runtime_error("ephemeris returned invalid Sun state");
            snapshot.sun_position_m=sun.position_m;
            snapshot.illumination=illumination_factor(eclipse_,s.r,sun.position_m,earth_.equatorial_radius_m);
        }
        if (atmosphere_) snapshot.density_kg_m3=density_.density_kg_m3(norm(s.r)-earth_.equatorial_radius_m);
        return snapshot;
    }
};
// Acquisition is a wrapper, not a hidden dependency of EarthDynamics or its force laws.
// A source is any callable (ElapsedTime,const State&) -> EnvironmentSnapshot.
// Construction does not cross-check source data against enabled forces/torques.
// Source/RHS exceptions propagate unchanged; no missing-data fallback is provided.
template<class Source> class EnvironmentalDynamics {
    EarthDynamics dynamics_;
    Source source_;
public:
    EnvironmentalDynamics(EarthDynamics dynamics,Source source)
        :dynamics_(std::move(dynamics)),source_(std::move(source)) {}
    StateDerivative operator()(ElapsedTime t,const State& s) const {
        return dynamics_(t,s,source_(t,s));
    }
};
}
