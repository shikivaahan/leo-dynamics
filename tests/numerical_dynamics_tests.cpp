#include <catch2/catch_test_macros.hpp>
#include "leo/kernel/dynamics.hpp"
#include "leo/kernel/orbital.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
using namespace leo;
namespace {
long double potential(const Earth& e,const std::array<long double,3>& p,bool j2) {
    const long double r=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
    if(!j2) return -static_cast<long double>(e.mu_m3_s2)/r;
    const long double z=p[2]/r,R=static_cast<long double>(e.equatorial_radius_m)/r;
    return static_cast<long double>(e.mu_m3_s2)*e.j2*R*R*(3*z*z-1)/(2*r);
}
Eigen::Matrix3d rodrigues(Eigen::Vector3d n,double angle) {
    n.normalize();Eigen::Matrix3d skew;
    skew<<0,-n.z(),n.y(),n.z(),0,-n.x(),-n.y(),n.x(),0;
    return std::cos(angle)*Eigen::Matrix3d::Identity()+(1-std::cos(angle))*n*n.transpose()+std::sin(angle)*skew;
}
}
TEST_CASE("Accepted roundoff asymmetry has one self-adjoint inertia operator", "[numerical][inertia]") {
    Eigen::Matrix3d m;
    m<<10,.25,.05,.25+9.9e-12,8,.1,.05,.1,6;
    const Inertia inertia(m);const BodyVector omega{.01,.02,.03};
    REQUIRE(norm(inertia.multiply(inertia.solve(omega))-omega)/norm(omega)<1e-15);
    const BodyVector a{1,2,3},b{-2,.5,1.5};
    REQUIRE(std::abs(dot(inertia.multiply(a),b)-dot(a,inertia.multiply(b)))<1e-13);
    const auto acceleration=torque_free_angular_acceleration(inertia,omega);
    REQUIRE(norm(inertia.multiply(acceleration)+cross(omega,inertia.multiply(omega)))<1e-16);
}
TEST_CASE("Inertia canonicalisation preserves equal large and subnormal finite entries", "[numerical][inertia]") {
    for(double scale:{std::numeric_limits<double>::denorm_min(),1e-250,1e250,1e308}) {
        const Inertia inertia(scale*Eigen::Matrix3d::Identity());
        const BodyVector moment=inertia.multiply({1,0,0});
        REQUIRE(moment.x()==scale);
        REQUIRE(norm(inertia.solve(moment)-BodyVector{1,0,0})<1e-14);
    }
}
TEST_CASE("Gravity and J2 agree with independent long-double potential gradients", "[numerical][gravity]") {
    std::mt19937_64 rng(20260909);std::uniform_real_distribution<double> unit(-1,1),radius(6.5e6,4.2e7);
    const Earth earth;
    for(int trial=0;trial<96;++trial) {
        const Eigen::Vector3d position=Eigen::Vector3d{unit(rng),unit(rng),unit(rng)}.normalized()*radius(rng);
        std::array<long double,3> p{position.x(),position.y(),position.z()};
        const long double h=1e-6L*static_cast<long double>(position.norm());
        for(bool j2:{false,true}) {
            Eigen::Vector3d reference;
            for(std::size_t axis=0;axis<3;++axis) {
                auto lo=p,hi=p;lo[axis]-=h;hi[axis]+=h;
                reference[static_cast<Eigen::Index>(axis)]=static_cast<double>(-(potential(earth,hi,j2)-potential(earth,lo,j2))/(2*h));
            }
            const auto actual=j2?j2_acceleration(earth,InertialVector{position}):point_mass_acceleration(earth,InertialVector{position});
            REQUIRE(norm(actual-InertialVector{reference})/reference.norm()<5e-10);
        }
    }
}
TEST_CASE("Quaternion rotation matches Rodrigues including tiny angles and large representatives", "[numerical][attitude]") {
    std::mt19937_64 rng(13572468);std::uniform_real_distribution<double> unit(-1,1);
    for(double angle:{0.,1e-12,1e-6,.4,1.5,std::numbers::pi}) {
        for(int trial=0;trial<32;++trial) {
            const Eigen::Vector3d n=Eigen::Vector3d{unit(rng),unit(rng),unit(rng)}.normalized();
            const Eigen::Vector3d v{1000*unit(rng),1000*unit(rng),1000*unit(rng)};
            const double s=std::sin(angle*.5);
            const Quaternion q{std::cos(angle*.5)*1e250,n.x()*s*1e250,n.y()*s*1e250,n.z()*s*1e250};
            const auto actual=body_to_inertial(q,BodyVector{v});
            REQUIRE(norm(actual-InertialVector{rodrigues(n,angle)*v})<1e-10);
            REQUIRE(norm(inertial_to_body(q,actual)-BodyVector{v})<1e-10);
        }
    }
}
TEST_CASE("Fixed-seed physical elliptic states satisfy Kepler energy and angular momentum", "[numerical][orbital]") {
    std::mt19937_64 rng(424242);
    std::uniform_real_distribution<double> a(1.8e7,4.2e7),ecc(0,.6),incl(0,std::numbers::pi),angle(0,2*std::numbers::pi);
    const double mu=Earth{}.mu_m3_s2;
    for(int trial=0;trial<128;++trial) {
        const ClassicalElements elements{a(rng),ecc(rng),incl(rng),angle(rng),angle(rng),angle(rng)};
        const auto orbit=coe_to_cartesian(elements,mu);
        const long double energy=.5L*static_cast<long double>(dot(orbit.v,orbit.v))-static_cast<long double>(mu)/norm(orbit.r);
        REQUIRE(std::abs(static_cast<double>(energy+mu/(2*elements.a_m)))/(mu/(2*elements.a_m))<1e-12);
        const double expected=std::sqrt(mu*elements.a_m*(1-elements.e)*(1+elements.e));
        REQUIRE(std::abs(norm(cross(orbit.r,orbit.v))-expected)/expected<1e-12);
    }
}
TEST_CASE("Torque-free Euler has independent instantaneous conservation identities", "[numerical][attitude]") {
    const Inertia inertia((Eigen::Vector3d{2,3,4}).asDiagonal());
    std::mt19937_64 rng(987654321);std::uniform_real_distribution<double> component(-.05,.05);
    for(int trial=0;trial<128;++trial) {
        const BodyVector omega{component(rng),component(rng),component(rng)};
        const auto angular_momentum=inertia.multiply(omega);
        const auto momentum_derivative=inertia.multiply(torque_free_angular_acceleration(inertia,omega));
        REQUIRE(std::abs(dot(omega,momentum_derivative))<1e-18);
        REQUIRE(std::abs(dot(angular_momentum,momentum_derivative))<1e-18);
    }
}
