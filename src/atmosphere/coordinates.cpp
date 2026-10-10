#include "leo/atmosphere/coordinates.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace leo::atmosphere {
namespace { constexpr double e2=wgs84_f*(2-wgs84_f), b=wgs84_a_m*(1-wgs84_f); }
Eigen::Vector3d earth_fixed(Geodetic g) {
    if(!std::isfinite(g.latitude_rad)||!std::isfinite(g.longitude_rad)||!std::isfinite(g.altitude_m)||
       std::abs(g.latitude_rad)>std::numbers::pi/2 || g.altitude_m<=-b)
        throw std::invalid_argument("invalid WGS84 geodetic coordinate");
    const double s=std::sin(g.latitude_rad),c=std::cos(g.latitude_rad);
    const double n=wgs84_a_m/std::sqrt(1-e2*s*s);
    return {(n+g.altitude_m)*c*std::cos(g.longitude_rad),
            (n+g.altitude_m)*c*std::sin(g.longitude_rad),(n*(1-e2)+g.altitude_m)*s};
}
Geodetic geodetic(const Eigen::Vector3d& r) {
    if(!r.allFinite() || r.squaredNorm()==0)
        throw std::invalid_argument("WGS84 conversion requires finite nonzero position");
    const double p=std::hypot(r.x(),r.y());
    if(p<1e-9) return {std::copysign(std::numbers::pi/2,r.z()),0,std::abs(r.z())-b};
    double lat=std::atan2(r.z(),p*(1-e2));
    bool converged=false;
    for(unsigned i=0;i<16;++i) {
        const double s=std::sin(lat),n=wgs84_a_m/std::sqrt(1-e2*s*s);
        const double next=std::atan2(r.z()+e2*n*s,p);
        if(std::abs(next-lat)<2e-15) {lat=next;converged=true;break;}
        lat=next;
    }
    if(!converged) throw std::runtime_error("WGS84 latitude iteration did not converge");
    const double s=std::sin(lat),c=std::cos(lat),n=wgs84_a_m/std::sqrt(1-e2*s*s);
    // Project onto the ellipsoid normal, avoiding divisions by sin/cos at poles/equator.
    const double height=p*c+r.z()*s-n*(1-e2*s*s);
    return {lat,std::atan2(r.y(),r.x()),height};
}
}
