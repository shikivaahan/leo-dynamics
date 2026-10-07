#include <catch2/catch_test_macros.hpp>
#include "leo/kernel/orbital.hpp"
#include "leo/kernel/earth.hpp"
#include <cmath>
#include <numbers>
using namespace leo;
namespace {
constexpr double pi=std::numbers::pi;
double delta(double a,double b){return std::remainder(a-b,2*pi);}
}
TEST_CASE("Radians elements use active prograde orientation", "[orbital]") {
    const double mu=Earth{}.mu_m3_s2;
    auto signed_orbit=coe_to_cartesian({7e6,0,pi/4,0,0,0},mu);
    REQUIRE(signed_orbit.v.z()>0);
    REQUIRE(std::abs(cartesian_to_coe(signed_orbit,mu).raan_rad)<=1e-12);
    ClassicalElements c{7e6,.05,pi/4,pi/6,pi/3,pi/2};
    auto back=cartesian_to_coe(coe_to_cartesian(c,mu),mu);
    REQUIRE(std::abs(back.a_m/c.a_m-1)<=1e-12);
    REQUIRE(std::abs(back.e-c.e)<=1e-12);
    REQUIRE(std::abs(back.i_rad-c.i_rad)<=1e-12);
    REQUIRE(std::abs(delta(back.raan_rad,c.raan_rad))<=1e-12);
    REQUIRE(std::abs(delta(back.arg_periapsis_rad,c.arg_periapsis_rad))<=1e-12);
    REQUIRE(std::abs(delta(back.true_anomaly_rad,c.true_anomaly_rad))<=1e-12);
}
TEST_CASE("Singular elements reconstruct with deterministic gauges", "[orbital]") {
    const double mu=Earth{}.mu_m3_s2;
    for(double e:{0.,1e-12,.05}) for(double i:{0.,1e-12,.7,pi}) {
        ClassicalElements c{7e6,e,i,.4,.8,1.2};
        auto orbit=coe_to_cartesian(c,mu);
        auto back=cartesian_to_coe(orbit,mu);
        auto rebuilt=coe_to_cartesian(back,mu);
        CAPTURE(e,i,back.raan_rad,back.arg_periapsis_rad,back.true_anomaly_rad);
        REQUIRE(norm(rebuilt.r-orbit.r)<=1e-2);
        REQUIRE(norm(rebuilt.v-orbit.v)<=1e-5);
        if(e<=1e-10) REQUIRE(back.arg_periapsis_rad==0);
        if(i<=1e-10||i==pi) REQUIRE(back.raan_rad==0);
        REQUIRE(back.true_anomaly_rad>=0);
        REQUIRE(back.true_anomaly_rad<2*pi);
    }
}
TEST_CASE("Invalid elliptic and Cartesian domains fail explicitly", "[orbital]") {
    double mu=Earth{}.mu_m3_s2;
    ClassicalElements good{7e6,.01,.5,.4,.3,.2};
    for(int n=0;n<8;++n){ auto c=good;
        if(n==0)c.e=1;
        if(n==1)c.e=-.1;
        if(n==2)c.a_m=0;
        if(n==3)c.i_rad=-1;
        if(n==4)c.i_rad=4;
        if(n==5)c.raan_rad=NAN;
        if(n==6)c.arg_periapsis_rad=INFINITY;
        if(n==7)c.true_anomaly_rad=NAN;
        REQUIRE_THROWS_AS(coe_to_cartesian(c,mu),std::invalid_argument);
    }
    REQUIRE_THROWS_AS(coe_to_cartesian(good,0),std::invalid_argument);
    REQUIRE_THROWS_AS(cartesian_to_coe({{0,0,0},{0,1,0}},mu),std::invalid_argument);
    REQUIRE_THROWS_AS(cartesian_to_coe({{7e6,0,0},{1,0,0}},mu),std::invalid_argument);
    REQUIRE_THROWS_AS(cartesian_to_coe({{7e6,0,0},{0,2e4,0}},mu),std::invalid_argument);
    REQUIRE_THROWS_AS(cartesian_to_coe({{NAN,0,0},{0,1,0}},mu),std::invalid_argument);
}
TEST_CASE("Angles just below zero wrap into a half-open interval", "[orbital]") {
    const double mu=Earth{}.mu_m3_s2;
    for (auto c:{ClassicalElements{7e6,.05,.7,-1e-20,0,0},
                 ClassicalElements{7e6,.05,.7,0,-1e-20,0},
                 ClassicalElements{7e6,.05,.7,0,0,-1e-20}}) {
        const auto orbit=coe_to_cartesian(c,mu);
        const auto back=cartesian_to_coe(orbit,mu);
        for(double angle:{back.raan_rad,back.arg_periapsis_rad,back.true_anomaly_rad}) {
            REQUIRE(angle>=0);
            REQUIRE(angle<2*pi);
        }
        const auto rebuilt=coe_to_cartesian(back,mu);
        REQUIRE(norm(rebuilt.r-orbit.r)<=1e-7);
        REQUIRE(norm(rebuilt.v-orbit.v)<=1e-10);
    }
}
