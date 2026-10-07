#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "leo/environment/source.hpp"
#include <limits>
#include <numbers>
#include <type_traits>
using namespace leo;
using Catch::Approx;
namespace {
constexpr double nan_value=std::numeric_limits<double>::quiet_NaN();
Quaternion z_rotation(double angle) { return {std::cos(angle/2),0,0,std::sin(angle/2)}; }
void near(BodyVector value,BodyVector expected,double tolerance=1e-14) {
    REQUIRE(norm(value-expected)<=tolerance);
}
// A physical deterministic moving-Sun fixture with narrow ephemeris coverage.
class MovingSun final:public EphemerisProvider {
public:
    EphemerisState geometric_state(int target,Epoch epoch) const override {
        if (target!=10||!std::isfinite(epoch.et_seconds)||epoch.et_seconds<100||epoch.et_seconds>110)
            throw std::out_of_range("fixture coverage");
        return {{astronomical_unit_m,1e8*(epoch.et_seconds-100),0},{0,1e8,0}};
    }
};
}
TEST_CASE("Surface and materials preserve geometry invariants", "[geometry]") {
    for (double area:{0.,-1.,nan_value,std::numeric_limits<double>::infinity()})
        REQUIRE_THROWS_AS(Surface(area,{1,0,0},{}),std::invalid_argument);
    for (BodyVector n: {BodyVector{},BodyVector{2,0,0},BodyVector{nan_value,0,0}})
        REQUIRE_THROWS_AS(Surface(1,n,{}),std::invalid_argument);
    REQUIRE_THROWS_AS(Surface(1,{1,0,0},{0,nan_value,0}),std::invalid_argument);
    REQUIRE_THROWS_AS(OpticalProperties(.6,.5),std::invalid_argument);
    REQUIRE_THROWS_AS(OpticalProperties(-.1,0),std::invalid_argument);
    REQUIRE_THROWS_AS(OpticalProperties(0,nan_value),std::invalid_argument);
    REQUIRE_THROWS_AS(AerodynamicProperties(-1,.9),std::invalid_argument);
    REQUIRE_THROWS_AS(AerodynamicProperties(2.2,1.1),std::invalid_argument);
    REQUIRE_THROWS_AS(AerodynamicProperties(nan_value,.9),std::invalid_argument);
    const Spacecraft craft(10,Inertia(Eigen::Matrix3d::Identity()),{Surface(2,{1,0,0},{0,2,0})});
    static_assert(std::is_same_v<decltype(craft.surfaces()),std::span<const Surface>>);
    const auto wrench=surface_wrench(craft.surfaces()[0],z_rotation(std::numbers::pi/2),{3,0,0});
    REQUIRE(norm(wrench.force_n-InertialVector{0,3,0})<1e-14);
    near(wrench.torque_nm,{0,0,-6});
    near(inertial_to_body(z_rotation(std::numbers::pi/2),wrench.force_n),{3,0,0});
}
TEST_CASE("Gravity gradient analytical body torque and inverse cube law", "[gravity_gradient]") {
    const Earth earth;
    const double r=7e6;
    State s{{r/std::sqrt(2.),r/std::sqrt(2.),0},{},{1,0,0,0},{}};
    const Inertia inertia((Eigen::Vector3d{2,3,4}).asDiagonal());
    near(gravity_gradient_torque(earth,s,Inertia(2*Eigen::Matrix3d::Identity())),{});
    const double expected=1.5*earth.mu_m3_s2/(r*r*r);
    near(gravity_gradient_torque(earth,s,inertia),{0,0,expected});
    s.r=s.r*2;
    near(gravity_gradient_torque(earth,s,inertia),{0,0,expected/8});
    s.r={r,0,0};
    near(gravity_gradient_torque(earth,s,inertia),{});
    s.q=z_rotation(std::numbers::pi/4);
    near(gravity_gradient_torque(earth,s,inertia),{0,0,-expected});
    s.r={};
    REQUIRE_THROWS_AS(gravity_gradient_torque(earth,s,inertia),std::invalid_argument);
}
TEST_CASE("Environment uses epoch plus fractional elapsed time and explicit coverage", "[environment]") {
    MovingSun sun;
    const Earth earth;
    EarthEnvironment source(earth,{100},&sun,true,EclipseModel::conical);
    const State day{{earth.equatorial_radius_m+400e3,0,0},{},{1,0,0,0},{}};
    const auto a=source({0},day),b=source({.25},day);
    REQUIRE(a.epoch.et_seconds==100);
    REQUIRE(b.epoch.et_seconds==100.25);
    REQUIRE(b.sun_position_m.y()==25e6);
    REQUIRE(b.illumination==1);
    REQUIRE(b.density_kg_m3==Approx(2.62e-12));
    REQUIRE_THROWS_AS(source({11},day),std::out_of_range);
    REQUIRE_THROWS_AS(source({nan_value},day),std::invalid_argument);
    REQUIRE_THROWS_AS(EarthEnvironment(earth,{nan_value}),std::invalid_argument);
    REQUIRE_THROWS_AS(EarthEnvironment(earth,{0},nullptr,false,EclipseModel::none,ExponentialAtmosphere{}, {nan_value,0,0}),std::invalid_argument);
    State night=day; night.r=-day.r;
    REQUIRE(source({0},night).illumination==0);
    // Disabling atmospheric acquisition allows otherwise unsupported altitudes.
    EarthEnvironment empty(earth,{0});
    State far=day; far.r={1e9,0,0};
    REQUIRE_THROWS_AS(source({0},far),std::out_of_range);
    REQUIRE(empty({0},far).density_kg_m3==0);
}
