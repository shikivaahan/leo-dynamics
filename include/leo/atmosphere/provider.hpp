#pragma once
#include "leo/atmosphere/weather.hpp"
#include "leo/kernel/aerodynamics.hpp"
#include <string>
#ifdef LEO_HAS_NRLMSISE00
#include "leo/atmosphere/nrlmsise00.hpp"
#endif
namespace leo::atmosphere {
enum class Model { exponential,nrlmsise00,jb2008 };
// Borrowed ephemeris and immutable weather must outlive this provider.
class DirectAtmosphere {
    Model model_;
    const SpiceEphemerisProvider& spice_;
    const WeatherDataset& weather_;
    std::string frame_;
    bool ap_history_;
#ifdef LEO_HAS_NRLMSISE00
    Nrlmsise00 nrlmsise_;
#endif
    ExponentialAtmosphere exponential_;
public:
    DirectAtmosphere(Model,const SpiceEphemerisProvider&,const WeatherDataset&,
                     std::string frame="ITRF93",bool ap_history=false);
    Result evaluate(Epoch,InertialVector position_m) const;
};
}
