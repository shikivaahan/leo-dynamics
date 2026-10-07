#include <catch2/catch_test_macros.hpp>
#include "leo/kernel/state.hpp"
#include "leo/kernel/spacecraft.hpp"
#include <cmath>
#include <limits>
using namespace leo;
TEST_CASE("Hamilton body-to-inertial rotation", "[attitude]") {
    const double h=std::sqrt(.5);
    for (double sign:{1.,-1.}) {
        Quaternion q{sign*h,0,0,sign*h};
        auto v=body_to_inertial(q,BodyVector{1,0,0});
        REQUIRE(norm(v-InertialVector{0,1,0})<=1e-14);
        REQUIRE(norm(inertial_to_body(q,v)-BodyVector{1,0,0})<=1e-14);
    }
    REQUIRE(norm(body_to_inertial({2,0,0,0},{1,2,3})-InertialVector{1,2,3})==0);
}
TEST_CASE("Quaternion validation and raw derivative", "[attitude]") {
    for (double w:{0.,1e-13,1e-12,std::numeric_limits<double>::infinity()})
        REQUIRE_THROWS_AS(normalize_quaternion({w,0,0,0}),std::invalid_argument);
    REQUIRE(std::abs(normalize_quaternion({1e308,1e308,0,0}).w-std::sqrt(.5))<=1e-14);
    auto d=quaternion_derivative({2,0,0,0},{0,0,3});
    REQUIRE(d.w==0); REQUIRE(d.z==3);
    auto s=validated_state({{1,0,0},{0,1,0},{2,0,0,0},{0,0,0}});
    REQUIRE(s.q.w==1);
    s.v=InertialVector{NAN,0,0};
    REQUIRE_THROWS_AS(validated_state(s),std::invalid_argument);
}
TEST_CASE("Full inertia and torque-free Euler equation", "[attitude]") {
    Eigen::Matrix3d m;
    m<<2,.2,.1,.2,3,.3,.1,.3,4;
    const Inertia inertia(m);
    const BodyVector omega{.01,.02,.03};
    REQUIRE(norm(inertia.multiply(omega)-BodyVector{.027,.071,.127})<=1e-15);
    REQUIRE_THROWS_AS(inertia.multiply({NAN,0,0}),std::invalid_argument);
    REQUIRE_THROWS_AS(inertia.solve({0,INFINITY,0}),std::invalid_argument);
    REQUIRE(norm(inertia.multiply(inertia.solve(omega))-omega)/norm(omega)<=1e-13);
    REQUIRE(norm(inertia.multiply(torque_free_angular_acceleration(inertia,omega))
                 +cross(omega,inertia.multiply(omega)))<=1e-15);
    const Inertia diagonal(Eigen::Vector3d(1,2,3).asDiagonal());
    REQUIRE(norm(torque_free_angular_acceleration(diagonal,{0,0,1}))==0);
    for (double mass:{0.,-1.,std::numeric_limits<double>::infinity()})
        REQUIRE_THROWS_AS(Spacecraft(mass,inertia),std::invalid_argument);
    for (int invalid=0;invalid<4;++invalid) {
        Eigen::Matrix3d bad=m;
        if (invalid==0) bad.setZero();
        if (invalid==1) bad(0,0)=-1;
        if (invalid==2) bad(0,1)=1;
        if (invalid==3) bad(0,0)=NAN;
        REQUIRE_THROWS_AS(Inertia{bad},std::invalid_argument);
    }
}
