#include <catch2/catch_test_macros.hpp>
#include "leo/kernel/dynamics.hpp"
#include <cmath>
using namespace leo;
TEST_CASE("Central gravity covariance and inverse square", "[gravity]") {
    Earth e;
    double r=7e6, a=e.mu_m3_s2/(r*r);
    REQUIRE(norm(point_mass_acceleration(e,{r,0,0})-InertialVector{-a,0,0})/a<=1e-13);
    auto direction=InertialVector{1,2,3}/std::sqrt(14.);
    REQUIRE(norm(point_mass_acceleration(e,direction*r)+direction*a)/a<=1e-13);
    REQUIRE(std::abs(norm(point_mass_acceleration(e,{2*r,0,0}))/a-.25)<=1e-13);
}
TEST_CASE("J2 signs symmetry and composition", "[gravity]") {
    Earth e; double r=7e6;
    double a=1.5*e.j2*e.mu_m3_s2*std::pow(e.equatorial_radius_m/r,2)/(r*r);
    for (double s:{-1.,1.}) {
        REQUIRE(norm(j2_acceleration(e,{s*r,0,0})-InertialVector{-s*a,0,0})/a<=1e-13);
        REQUIRE(norm(j2_acceleration(e,{0,s*r,0})-InertialVector{0,-s*a,0})/a<=1e-13);
        REQUIRE(norm(j2_acceleration(e,{0,0,s*r})-InertialVector{0,0,2*s*a})/a<=1e-13);
    }
    const InertialVector p{4e6,3e6,5e6};
    auto correction=j2_acceleration(e,p);
    REQUIRE(norm(j2_acceleration(e,p*2)-correction/16)/norm(correction)<=1e-13);
    REQUIRE(norm(j2_acceleration(e,{-p.x(),p.y(),-p.z()})-InertialVector{-correction.x(),correction.y(),-correction.z()})<=1e-15);
    REQUIRE(norm(j2_acceleration(e,{-p.y(),p.x(),p.z()})-InertialVector{-correction.y(),correction.x(),correction.z()})<=1e-15);
    REQUIRE(norm(EarthGravity(e,true)(p)-point_mass_acceleration(e,p)-correction)<=1e-14);
    e.j2=0; REQUIRE(norm(j2_acceleration(e,p))==0);
}
TEST_CASE("Gravity and named dynamics validation", "[gravity]") {
    Earth e;
    for (auto r:{InertialVector{},InertialVector{NAN,0,0}})
        REQUIRE_THROWS_AS(point_mass_acceleration(e,r),std::invalid_argument);
    for (int field=0;field<3;++field) {
        Earth bad=e; if(field==0) bad.mu_m3_s2=0; if(field==1) bad.equatorial_radius_m=-1; if(field==2) bad.j2=NAN;
        REQUIRE_THROWS_AS(EarthGravity(bad,true),std::invalid_argument);
    }
    Spacecraft craft(10,Inertia(Eigen::Vector3d(1,2,3).asDiagonal()));
    EarthDynamics rhs(EarthGravity(e,false),craft);
    State s{{7e6,0,0},{0,7500,0},{2,0,0,0},{0,0,.01}};
    auto d=rhs({0},s);
    REQUIRE(norm(d.r_dot-s.v)==0); REQUIRE(d.v_dot.x()<0);
    REQUIRE(d.q_dot.z==.01); REQUIRE(norm(d.omega_dot)==0);
    REQUIRE_THROWS_AS(rhs({NAN},s),std::invalid_argument);
    s.q={0,0,0,0}; REQUIRE_THROWS_AS(rhs({0},s),std::invalid_argument);
}
