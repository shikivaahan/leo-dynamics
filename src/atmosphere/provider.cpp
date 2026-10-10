#include "leo/atmosphere/provider.hpp"
#include "leo/atmosphere/coordinates.hpp"
#ifdef LEO_HAS_NRLMSISE00
#include "leo/atmosphere/nrlmsise00.hpp"
#endif
#ifdef LEO_HAS_JB2008
#include "leo/atmosphere/jb2008.hpp"
#endif
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>
namespace leo::atmosphere {
namespace {
#ifdef LEO_HAS_NRLMSISE00
std::array<int,24> selected_switches(bool history) {
    auto s=Nrlmsise00::standard_switches();
    if(history) s[9]=-1;
    return s;
}
#endif
}
DirectAtmosphere::DirectAtmosphere(Model model,const SpiceEphemerisProvider& spice,
    const WeatherDataset& weather,std::string frame,bool ap_history)
    :model_(model),spice_(spice),weather_(weather),frame_(std::move(frame)),ap_history_(ap_history)
#ifdef LEO_HAS_NRLMSISE00
    ,nrlmsise_(selected_switches(ap_history))
#endif
    {
    switch(model_) {
    case Model::exponential:break;
    case Model::nrlmsise00:
#ifndef LEO_HAS_NRLMSISE00
        throw std::invalid_argument("NRLMSISE backend not enabled at build time");
#else
        break;
#endif
    case Model::jb2008:
#ifndef LEO_HAS_JB2008
        throw std::invalid_argument("JB2008 backend not enabled at build time");
#else
        break;
#endif
    default:throw std::invalid_argument("invalid atmosphere selection");
    }
}
Result DirectAtmosphere::evaluate(Epoch epoch,InertialVector position) const {
    if(!std::isfinite(epoch.et_seconds) || !position.as_eigen().allFinite())
        throw std::invalid_argument("nonfinite atmosphere epoch or position");
    if(model_==Model::exponential)
        return {exponential_.density_kg_m3(norm(position)-wgs84_a_m),0,0,{}};
    const auto rotation=SpiceEphemerisProvider::earth_fixed_rotation(epoch,frame_);
    const Eigen::Vector3d fixed=rotation*position.as_eigen();
    const auto g=geodetic(fixed);
    const auto utc=SpiceEphemerisProvider::utc_calendar(epoch);
#ifdef LEO_HAS_NRLMSISE00
    constexpr double deg=180/std::numbers::pi;
    if(model_==Model::nrlmsise00) {
        auto in=weather_.nrlmsise(utc,ap_history_);
        in.latitude_deg=g.latitude_rad*deg;in.longitude_deg=g.longitude_rad*deg;in.altitude_km=g.altitude_m/1000;
        // Reference header recommends UT hours + longitude/15; no equation-of-time correction.
        in.local_solar_time_hours=std::fmod(utc.seconds_of_day/3600+in.longitude_deg/15+24,24);
        return nrlmsise_.evaluate(in);
    }
#endif
#ifdef LEO_HAS_JB2008
    if(model_==Model::jb2008) {
        auto in=weather_.jb2008(utc);
        const auto sun=spice_.geometric_state(sun_naif_id,epoch).position_m;
        const Eigen::Vector3d fixed_sun=rotation*sun.as_eigen();
        // JB2008 uses RA only through H=SAT(1)-SUN(1). A common z-axis rotation
        // cancels exactly; use longitudes in the same terrestrial equator basis.
        in.sun_ra_dec_rad={std::atan2(fixed_sun.y(),fixed_sun.x()),
            std::atan2(fixed_sun.z(),std::hypot(fixed_sun.x(),fixed_sun.y()))};
        in.position_ra_lat_alt={g.longitude_rad,std::atan2(fixed.z(),std::hypot(fixed.x(),fixed.y())),g.altitude_m/1000};
        return Jb2008{}.evaluate(in);
    }
#endif
    throw std::runtime_error("atmosphere backend unavailable");
}
}
