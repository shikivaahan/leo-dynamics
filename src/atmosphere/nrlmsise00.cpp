#include "leo/atmosphere/nrlmsise00.hpp"
extern "C" {
#include "nrlmsise-00.h"
}
#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
namespace leo::atmosphere {
namespace { std::mutex native_mutex; }
Nrlmsise00::Nrlmsise00(std::array<int,24> switches):switches_(switches) {
    for(unsigned i=0;i<switches_.size();++i) {
        const int s=switches_[i];
        if((i==0 && s!=0 && s!=1) || (i!=0 && s!=0 && s!=1 && s!=2 && !(i==9 && s==-1)))
            throw std::invalid_argument("invalid NRLMSISE switch");
    }
}
Result Nrlmsise00::evaluate(const NrlmsiseInput& in) const {
    for(double v:{in.utc_seconds,in.altitude_km,in.latitude_deg,in.longitude_deg,
                  in.local_solar_time_hours,in.f107,in.f107_average,in.ap})
        if(!std::isfinite(v)) throw std::invalid_argument("nonfinite NRLMSISE input");
    // Explicit supported project domain, not a claimed published sharp validity cutoff.
    // Position transformed J2000 -> Earth-fixed -> geodetic recovers a requested
    // altitude to about 2e-12 km, so a point requested at exactly the boundary
    // arrives marginally above it. The boundary therefore accepts that documented
    // round-off margin and still evaluates the exact value received; nothing is
    // clamped, rounded or approximated.
    constexpr double altitude_roundoff_km = 1e-9;
    if(in.year<1 || in.year>9999 || in.day_of_year<1 ||
       in.day_of_year>((in.year%4==0 && (in.year%100!=0 || in.year%400==0))?366:365) ||
       in.utc_seconds<0 || in.utc_seconds>=86401 || in.altitude_km<0 ||
       in.altitude_km>1000+altitude_roundoff_km ||
       std::abs(in.latitude_deg)>90 || in.f107<=0 || in.f107_average<=0 || in.ap<0 || in.ap>400)
        throw std::invalid_argument("NRLMSISE input outside supported domain");
    ap_array history{};
    if(switches_[9]==-1) for(unsigned i=0;i<7;++i) {
        if(!std::isfinite(in.ap_history[i]) || in.ap_history[i]<0 || in.ap_history[i]>400)
            throw std::invalid_argument("invalid NRLMSISE Ap history");
        history.a[i]=in.ap_history[i];
    }
    const auto wrap=[](double x,double period) { double v=std::fmod(x,period);return v<0?v+period:v; };
    nrlmsise_input input{in.year,in.day_of_year,in.utc_seconds,in.altitude_km,in.latitude_deg,
        wrap(in.longitude_deg+180,360)-180,wrap(in.local_solar_time_hours,24),
        in.f107_average,in.f107,in.ap,&history};
    nrlmsise_flags flags{};
    std::copy(switches_.begin(),switches_.end(),flags.switches);
    nrlmsise_output output{};
    {
        std::lock_guard lock{native_mutex};
        gtd7d(&input,&flags,&output);
    }
    const double mass_scale=switches_[0]==0?1e3:1;
    const double number_scale=switches_[0]==0?1e6:1;
    Result result{output.d[5]*mass_scale,output.t[1],output.t[0],{},true,true};
    constexpr std::array<unsigned,8> indices{0,1,2,3,4,6,7,8};
    for(unsigned i=0;i<indices.size();++i) {
        result.number_density_m3[i]=output.d[indices[i]]*number_scale;
        if(!std::isfinite(result.number_density_m3[i]) || result.number_density_m3[i]<0)
            throw std::runtime_error("NRLMSISE returned invalid constituent density");
    }
    if(!std::isfinite(result.density_kg_m3) || result.density_kg_m3<0 ||
       !std::isfinite(result.temperature_k) || result.temperature_k<=0 ||
       !std::isfinite(result.exospheric_temperature_k) || result.exospheric_temperature_k<=0)
        throw std::runtime_error("NRLMSISE returned invalid density or temperature");
    return result;
}
}
