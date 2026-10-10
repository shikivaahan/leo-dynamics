#include "leo/atmosphere/jb2008.hpp"
#include <cmath>
#include <mutex>
#include <numbers>
#include <stdexcept>
extern "C" void leo_jb2008(const double*,const double*,const double*,
    const double*,const double*,const double*,const double*,const double*,const double*,
    const double*,const double*,const double*,double*,double*);
namespace leo::atmosphere {
namespace { std::mutex native_mutex; }
Result Jb2008::evaluate(const Jb2008Input& in) const {
    for(double v:{in.mjd_utc,in.sun_ra_dec_rad[0],in.sun_ra_dec_rad[1],
        in.position_ra_lat_alt[0],in.position_ra_lat_alt[1],in.position_ra_lat_alt[2],
        in.f10,in.f10_average,in.s10,in.s10_average,in.m10,in.m10_average,
        in.y10,in.y10_average,in.dstdtc_k})
        if(!std::isfinite(v)) throw std::invalid_argument("nonfinite JB2008 input");
    if(in.position_ra_lat_alt[2]<120 || in.position_ra_lat_alt[2]>1500 ||
       std::abs(in.position_ra_lat_alt[1])>std::numbers::pi/2 ||
       std::abs(in.sun_ra_dec_rad[1])>std::numbers::pi/2 ||
       in.mjd_utc<33282 || in.mjd_utc>=88069 ||
       in.f10<40 || in.f10_average<40 || in.s10<40 || in.s10_average<40 ||
       in.m10<40 || in.m10_average<40 || in.y10<40 || in.y10_average<40 ||
       in.dstdtc_k<0 || in.dstdtc_k>2000)
        throw std::invalid_argument("JB2008 input outside supported domain");
    const auto wrap=[](double a) {const double p=2*std::numbers::pi;
        const double b=std::fmod(a,p);return b<0?b+p:b;};
    const double sun[]{wrap(in.sun_ra_dec_rad[0]),in.sun_ra_dec_rad[1]};
    const double sat[]{wrap(in.position_ra_lat_alt[0]),in.position_ra_lat_alt[1],in.position_ra_lat_alt[2]};
    double temp[2]{},rho=0;
    {
        std::lock_guard lock{native_mutex};
        leo_jb2008(&in.mjd_utc,sun,sat,&in.f10,&in.f10_average,&in.s10,&in.s10_average,
            &in.m10,&in.m10_average,&in.y10,&in.y10_average,&in.dstdtc_k,temp,&rho);
    }
    if(!std::isfinite(rho) || rho<0 || !std::isfinite(temp[0]) || temp[0]<=0 ||
       !std::isfinite(temp[1]) || temp[1]<=0)
        throw std::runtime_error("JB2008 returned invalid density or temperature");
    return {rho,temp[1],temp[0],{},true,false};
}
}
