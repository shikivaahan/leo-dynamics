#pragma once
#include "common.hpp"
#include "leo/environment/ephemeris.hpp"
#include "leo/integration/options.hpp"
#include "leo/kernel/aerodynamics.hpp"
#include "leo/kernel/dynamics.hpp"
#include "leo/kernel/spacecraft.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace leo::python {
// Owns immutable, strictly increasing Sun samples; interpolation is the same
// difference-based cubic Hermite used by InterpolatedEphemerisProvider, generalised
// to an irregular epoch grid. No allocation or mutable state on lookup.
class TabulatedSunProvider final : public EphemerisProvider {
    struct Sample { double et; EphemerisState state; };
    std::vector<Sample> samples_;
public:
    TabulatedSunProvider(std::vector<double> et_seconds,std::vector<EphemerisState> states);
    EphemerisState geometric_state(int target_naif_id,Epoch epoch) const override;
    std::size_t sample_count() const noexcept { return samples_.size(); }
    double start_et_s() const noexcept { return samples_.front().et; }
    double end_et_s() const noexcept { return samples_.back().et; }
};
// Constant Sun position, zero relative velocity; for controlled experiments only.
class ConstantSunProvider final : public EphemerisProvider {
    InertialVector position_m_;
public:
    explicit ConstantSunProvider(InertialVector position_m);
    EphemerisState geometric_state(int target_naif_id,Epoch epoch) const override;
    const InertialVector& position_m() const noexcept { return position_m_; }
};
// Owning Python-facing Sun handle (exposed as `SunEphemeris`). Copying shares the
// immutable provider, so one construction can feed every batch worker safely.
struct SunSource {
    std::shared_ptr<const EphemerisProvider> provider;
    bool constant=false;
    EphemerisState state_at(double et_seconds) const {
        if(!provider) throw std::invalid_argument("Sun ephemeris is not configured");
        return provider->geometric_state(10,{et_seconds});
    }
    double start_et_s() const {
        const auto* table=dynamic_cast<const TabulatedSunProvider*>(provider.get());
        return table?table->start_et_s():0.;
    }
    double end_et_s() const {
        const auto* table=dynamic_cast<const TabulatedSunProvider*>(provider.get());
        return table?table->end_et_s():0.;
    }
    std::size_t sample_count() const {
        const auto* table=dynamic_cast<const TabulatedSunProvider*>(provider.get());
        return table?table->sample_count():0u;
    }
};
// Owning Python-facing atmosphere handle (exposed as `ExponentialAtmosphere`). The
// scale is retained because the native model exposes only density evaluation.
struct Atmosphere {
    double scale=1.;
    ExponentialAtmosphere model{1.};
    double density_kg_m3(double altitude_m) const { return model.density_kg_m3(altitude_m); }
};
// Shared conversion helpers for the other binding translation units.
Earth resolved_earth(const py::handle& earth);
InertialVector as_inertial_vector(const py::handle& value, const char* name);
BodyVector as_body_vector(const py::handle& value, const char* name);
EclipseModel as_eclipse_model(const std::string& name);
std::string eclipse_model_name(EclipseModel model);
// Native-only solver option validation: the unchanged propagate() precondition,
// reusable where no propagation runs (construction, empty batch validation).
void validate_options(const IntegrationOptions& options);
}