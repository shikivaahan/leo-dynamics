#pragma once
#include "leo/atmosphere/weather.hpp"
#include "leo/environment/spice.hpp"
#include "leo/kernel/aerodynamics.hpp"
#include <string>
#ifdef LEO_HAS_NRLMSISE00
#include "leo/atmosphere/nrlmsise00.hpp"
#endif
namespace leo::atmosphere {
enum class Model { exponential,nrlmsise00,jb2008 };
// Per-object switches for the exact SPICE preprocessing reused across calls.
// They are independent: turning one off never changes the numerical result, it
// only makes that particular conversion be recomputed on every call. All three
// default to on, which is the intended production configuration.
struct EvaluationOptions {
    bool utc_reuse = true;
    bool sun_sharing = true;
    bool orientation_reuse = true;
};
// Borrowed ephemeris and immutable weather must outlive this provider.
class DirectAtmosphere {
    Model model_;
    const SpiceEphemerisProvider& spice_;
    const WeatherDataset& weather_;
    std::string frame_;
    bool ap_history_;
    EvaluationOptions options_;
#ifdef LEO_HAS_NRLMSISE00
    Nrlmsise00 nrlmsise_;
#endif
    ExponentialAtmosphere exponential_;
public:
    DirectAtmosphere(Model,const SpiceEphemerisProvider&,const WeatherDataset&,
                     std::string frame="ITRF93",bool ap_history=false,
                     EvaluationOptions options={});
    // supplied_sun is optional and only consulted by the JB2008 backend, which
    // is the only one that needs a Sun direction. Exact epoch bits and the
    // process-wide managed kernel generation must match; incompatible input
    // falls back to an independent lookup. Opaque construction fixes geometry.
    // NRLMSISE00 and the exponential model never read a Sun, so
    // they ignore the argument entirely. Passing nothing is always correct and
    // always gives the identical result.
    Result evaluate(Epoch,InertialVector position_m,const SunObservation* supplied_sun=nullptr) const;
};
}