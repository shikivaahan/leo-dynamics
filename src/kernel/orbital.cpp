#include "leo/kernel/orbital.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace leo {
namespace {
constexpr double pi=std::numbers::pi;
void validate_mu(double mu) {
    if (!std::isfinite(mu)||mu<=0) throw std::invalid_argument("invalid gravitational parameter");
}
double wrap(double x) {
    x=std::fmod(x,2*pi);
    if (x<0) x+=2*pi;
    return x==0||x>=2*pi?0:x;
}
}
CartesianOrbit coe_to_cartesian(const ClassicalElements& c,double mu) {
    validate_mu(mu);
    if (!std::isfinite(c.a_m)||!std::isfinite(c.e)||!std::isfinite(c.i_rad)||!std::isfinite(c.raan_rad)||!std::isfinite(c.arg_periapsis_rad)||!std::isfinite(c.true_anomaly_rad)
        ||c.a_m<=0||c.e<0||c.e>=1||c.i_rad<0||c.i_rad>pi)
        throw std::invalid_argument("invalid bound elliptic elements");
    const double p=c.a_m*(1-c.e)*(1+c.e);
    const double cs=std::cos(c.true_anomaly_rad),sn=std::sin(c.true_anomaly_rad);
    const double radius=p/(1+c.e*cs), speed=std::sqrt(mu/p);
    const Eigen::Matrix3d rotation=(Eigen::AngleAxisd(c.raan_rad,Eigen::Vector3d::UnitZ())
        *Eigen::AngleAxisd(c.i_rad,Eigen::Vector3d::UnitX())
        *Eigen::AngleAxisd(c.arg_periapsis_rad,Eigen::Vector3d::UnitZ())).toRotationMatrix();
    CartesianOrbit result{InertialVector(rotation*Eigen::Vector3d(radius*cs,radius*sn,0)),
        InertialVector(rotation*Eigen::Vector3d(-speed*sn,speed*(c.e+cs),0))};
    if (!result.r.as_eigen().allFinite()||!result.v.as_eigen().allFinite()||norm(result.r)==0)
        throw std::invalid_argument("unrepresentable elliptic orbit");
    return result;
}
ClassicalElements cartesian_to_coe(CartesianOrbit orbit,double mu) {
    validate_mu(mu);
    const auto r=orbit.r,v=orbit.v;
    if (!r.as_eigen().allFinite()||!v.as_eigen().allFinite()) throw std::invalid_argument("nonfinite Cartesian orbit");
    const double radius=norm(r);
    const auto h=cross(r,v);
    const double hn=norm(h);
    if (!std::isfinite(radius)||radius<=0||!std::isfinite(hn)||hn<=0)
        throw std::invalid_argument("zero or invalid radius/angular momentum");
    const double energy=.5*dot(v,v)-mu/radius;
    if (!std::isfinite(energy)||energy>=0) throw std::invalid_argument("orbit must be bound");
    const auto ev=cross(v,h)/mu-r/radius;
    const double ecc=norm(ev),a=-mu/(2*energy);
    if (!std::isfinite(ecc)||ecc>=1||!std::isfinite(a)||a<=0) throw std::invalid_argument("unrepresentable bound orbit");
    const InertialVector node{-h.y(),h.x(),0};
    const double nn=norm(node);
    const auto axis=h/hn;
    auto angle=[&](InertialVector u,InertialVector w) {
        // Unit arguments avoid dimensional products and cancellation at small e.
        u=u/norm(u); w=w/norm(w);
        return wrap(std::atan2(dot(axis,cross(u,w)),dot(u,w)));
    };
    const bool circular=ecc<=1e-10,equatorial=nn/hn<=1e-10;
    ClassicalElements c{a,ecc,std::atan2(std::hypot(h.x(),h.y()),h.z()),0,0,0};
    if (!equatorial) c.raan_rad=wrap(std::atan2(node.y(),node.x()));
    if (!circular) {
        c.arg_periapsis_rad=equatorial?wrap(std::atan2((h.z()<0?-1:1)*ev.y(),ev.x())):angle(node,ev);
        c.true_anomaly_rad=angle(ev,r);
    } else {
        c.true_anomaly_rad=equatorial?wrap(std::atan2((h.z()<0?-1:1)*r.y(),r.x())):angle(node,r);
    }
    return c;
}
}
