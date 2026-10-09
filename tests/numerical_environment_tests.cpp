#include <catch2/catch_test_macros.hpp>
#include "leo/kernel/aerodynamics.hpp"
#include <Eigen/Geometry>
#include <array>
#include <cmath>
#include <random>
using namespace leo;
namespace {
constexpr std::uint64_t seed=20251009;
Quaternion axis_quaternion(Eigen::Vector3d axis,double angle) {
    axis.normalize();const double s=std::sin(angle*.5);
    return {std::cos(angle*.5),axis.x()*s,axis.y()*s,axis.z()*s};
}
Quaternion multiply(Quaternion a,Quaternion b) {
    return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
            a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
Eigen::Matrix3d rodrigues(Eigen::Vector3d axis,double angle) {
    axis.normalize();Eigen::Matrix3d skew;
    skew<<0,-axis.z(),axis.y(),axis.z(),0,-axis.x(),-axis.y(),axis.x(),0;
    return std::cos(angle)*Eigen::Matrix3d::Identity()+(1-std::cos(angle))*axis*axis.transpose()+std::sin(angle)*skew;
}
Spacecraft panel(BodyVector normal,BodyVector centre,double sigma) {
    return Spacecraft(10,Inertia((Eigen::Vector3d{2,3,4}).asDiagonal()),
                      {Surface(2,normal,centre,{},AerodynamicProperties{2.2,sigma})});
}
}
TEST_CASE("Sentman small lift agrees with independent high-precision momentum flux", "[numerical][sentman]") {
    // 60-digit incident/outgoing molecular momentum vectors, projected onto
    // the direction perpendicular to the incoming beam. Exact binary64 inputs.
    // Generator: docs/numerical-audit/environment_reference.py.
    struct Fixture {double angle,sigma,lift;};
    const std::array fixtures{
        Fixture{.2617993877991494,.999999,2.5881904510996322e-7},
        Fixture{.2617993877991494,.999999999,2.5881903778261821e-10},
        Fixture{.2617993877991494,.999999999999,2.5881331958004284e-13},
        Fixture{.2617993877991494,.999999999999999,2.5861217677588382e-16},
        Fixture{.7853981633974483,.999999,1.4142135624137617e-6},
        Fixture{.7853981633974483,.999999999,1.414213522376404e-9},
        Fixture{.7853981633974483,.999999999999,1.4141822775364762e-12},
        Fixture{.7853981633974483,.999999999999999,1.4130832128153975e-15},
        Fixture{1.3089969389957472,.999999,9.6592582631684405e-7},
        Fixture{1.3089969389957472,.999999999,9.659257989708202e-10},
        Fixture{1.3089969389957472,.999999999999,9.6590445834828092e-13},
        Fixture{1.3089969389957472,.999999999999999,9.6515378318358257e-16}};
    for(const auto& f:fixtures) {
        CAPTURE(f.angle,f.sigma);
        REQUIRE(std::abs(sentman_coefficients(f.angle,f.sigma).lift-f.lift)/f.lift<1e-14);
    }
    // Cold fully accommodated molecules remove only incoming momentum: the
    // force is parallel to the flow, so lift must be exactly zero.
    for(double angle:{.1,.3,.7,1.,1.4}) REQUIRE(sentman_coefficients(angle,1).lift==0);
}
TEST_CASE("Atmosphere agrees with independent anchor interpolation away from knots", "[numerical][atmosphere]") {
    const std::array<std::array<double,2>,5> cases{{
        {1.,1.2248203937648561},{12345.,.20045152034782011},
        {321987.654,1.1876013192685037e-11},{400000.5,2.6199770227892707e-12},
        {35785999.,4.0400001132470354e-19}}};
    const ExponentialAtmosphere density;
    for(const auto& f:cases) REQUIRE(std::abs(density.density_kg_m3(f[0])-f[1])/f[1]<4e-15);
}
TEST_CASE("Atmospheric segment midpoints obey the geometric mean identity", "[numerical][atmosphere]") {
    const std::array<std::array<double,2>,6> cases{{
        {50000.,.000801950746617272},{225000.,1.1994873905131308e-10},
        {425000.,1.6899112402726955e-12},{1125000.,9.215747392371386e-16},
        {12500000.,1.6934284750174718e-18},{35393000.,4.0847521344629958e-19}}};
    const ExponentialAtmosphere density;
    for(const auto& f:cases) REQUIRE(std::abs(density.density_kg_m3(f[0])-f[1])/f[1]<4e-15);
}
TEST_CASE("Fixed-seed drag preserves dissipation lever-arm shifts and frame covariance", "[numerical][aerodynamics]") {
    std::mt19937_64 rng(seed);std::uniform_real_distribution<double> uniform(-1,1);
    for(int trial=0;trial<128;++trial) {
        const Eigen::Vector3d normal=Eigen::Vector3d{uniform(rng),uniform(rng),uniform(rng)}.normalized();
        const Eigen::Vector3d axis{uniform(rng),uniform(rng),uniform(rng)};
        const Quaternion q=axis_quaternion(axis,.02*trial);
        const BodyVector shift{.1,.2,-.3};const double sigma=.5+.5*uniform(rng);
        const auto a=panel(BodyVector{normal},BodyVector{},sigma),b=panel(BodyVector{normal},shift,sigma);
        const State state{{7e6,0,0},{6000*uniform(rng),6000*uniform(rng),6000*uniform(rng)},q,{}};
        EnvironmentSnapshot env;env.density_kg_m3=3.5e-12;env.atmosphere_angular_velocity_rad_s={0,0,7.292115e-5};
        const InertialVector wind=state.v-cross(env.atmosphere_angular_velocity_rad_s,state.r);
        const double rotation=.13+.01*trial;const Eigen::Matrix3d scene=rodrigues(axis,rotation);
        State turned=state;turned.r=InertialVector{scene*state.r.as_eigen()};turned.v=InertialVector{scene*state.v.as_eigen()};
        turned.q=multiply(axis_quaternion(axis,rotation),q);
        auto turned_env=env;turned_env.atmosphere_angular_velocity_rad_s=InertialVector{scene*env.atmosphere_angular_velocity_rad_s.as_eigen()};
        for(bool sentman:{false,true}) {
            const auto law=sentman?sentman_drag:flat_plate_drag;
            const auto centre=law(a,state,env),offset=law(b,state,env),rotated=law(b,turned,turned_env);
            CAPTURE(trial,sentman);
            REQUIRE(dot(offset.force_n,wind)<=0);
            const BodyVector body_force{rodrigues(axis,.02*trial).transpose()*centre.force_n.as_eigen()};
            const BodyVector expected=cross(shift,body_force);
            const double scale=std::max(norm(expected),1e-20);
            REQUIRE(norm(offset.torque_nm-centre.torque_nm-expected)<3e-11*scale);
            const double force_scale=std::max(norm(offset.force_n),1e-20);
            REQUIRE(norm(rotated.force_n-InertialVector{scene*offset.force_n.as_eigen()})<3e-11*force_scale);
            REQUIRE(norm(rotated.torque_nm-offset.torque_nm)<3e-11*scale);
        }
    }
}
