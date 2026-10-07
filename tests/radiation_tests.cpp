#include <catch2/catch_test_macros.hpp>
#include "leo/kernel/radiation.hpp"
#include <cmath>
using namespace leo;
namespace {
constexpr double kRe = 6378136.3;
constexpr double kTolerance = 1e-12;
// Independent physical fixture: irradiance divided by light speed, not a production constant.
constexpr double kAuPressure = 1361.0/299792458.0;
Inertia diag(double a,double b,double c) { return Inertia(Eigen::Vector3d(a,b,c).asDiagonal()); }
Spacecraft panel(double area,BodyVector normal,BodyVector cop,OpticalProperties optical=OpticalProperties()) {
    return Spacecraft(1000,diag(20,20,20),{Surface(area,normal,cop,optical)});
}
EnvironmentSnapshot sunlit(InertialVector sun,double illumination=1) {
    EnvironmentSnapshot env;
    env.sun_position_m=sun; env.illumination=illumination;
    return env;
}
State at(InertialVector r,Quaternion q={1,0,0,0}) { return {r,{0,0,0},q,{0,0,0}}; }
double ratio(InertialVector a,InertialVector b) {
    const double scale=std::max(norm(a),norm(b));
    return norm(a-b)/(scale>0?scale:1);
}
double ratio(BodyVector a,BodyVector b) {
    const double scale=std::max(norm(a),norm(b));
    return norm(a-b)/(scale>0?scale:1);
}
}
TEST_CASE("Diffuse reflection transfers incident momentum as well as recoil", "[radiation]") {
    const auto diffuse=solar_radiation_pressure(panel(2,{0,0,1},{1,0,0},{0,1}),
        at({}),sunlit({0,0,astronomical_unit_m}));
    // Incident photon momentum PA, plus outward Lambert recoil (2/3)PA.
    REQUIRE(ratio(diffuse.force_n,InertialVector{0,0,-(5./3)*2*kAuPressure})<1e-12);
    REQUIRE(ratio(diffuse.torque_nm,BodyVector{0,(5./3)*2*kAuPressure,0})<1e-12);
}
TEST_CASE("Solar pressure incidence face, edge and back face", "[radiation]") {
    const InertialVector sun{0,0,astronomical_unit_m};
    const InertialVector source{0,0,1};
    const double area=2.0;
    const auto face=solar_radiation_pressure(panel(area,{0,0,1},{1,0,0},OpticalProperties(1,0)),
        at({0,0,0}),sunlit(sun));
    REQUIRE(ratio(face.force_n,source*(-2*area*kAuPressure))<=kTolerance);
    REQUIRE(ratio(face.force_n,InertialVector{0,0,-4*kAuPressure})<=kTolerance);
    REQUIRE(norm(solar_radiation_pressure(panel(area,{1,0,0},{0,0,0},OpticalProperties(1,0)),
        at({0,0,0}),sunlit(sun)).force_n)==0);
    REQUIRE(norm(solar_radiation_pressure(panel(area,{0,0,-1},{0,0,0},OpticalProperties(1,0)),
        at({0,0,0}),sunlit(sun)).force_n)==0);
}
TEST_CASE("Oblique absorption specular and diffuse momentum fixtures", "[radiation]") {
    const double a=std::sqrt(.5),pa=2*kAuPressure;
    const BodyVector normal{a,0,a},cop{0,1,0};
    const auto state=at({});
    const auto env=sunlit({0,0,astronomical_unit_m});
    const auto absorbed=solar_radiation_pressure(panel(2,normal,cop),state,env);
    REQUIRE(ratio(absorbed.force_n,InertialVector{0,0,-pa*a})<1e-12);
    const auto mirror=solar_radiation_pressure(panel(2,normal,cop,{1,0}),state,env);
    REQUIRE(ratio(mirror.force_n,InertialVector{-pa*a,0,-pa*a})<1e-12);
    const auto diffuse=solar_radiation_pressure(panel(2,normal,cop,{0,1}),state,env);
    REQUIRE(ratio(diffuse.force_n,InertialVector{-pa/3,0,-pa*(a+1./3)})<1e-12);
    REQUIRE(ratio(diffuse.torque_nm,BodyVector{-pa*(a+1./3),0,pa/3})<1e-12);
}
TEST_CASE("Inverse square pressure and illumination multiplier", "[radiation]") {
    const auto craft=panel(1.0,{0,0,1},{0,0,0},OpticalProperties(1,0));
    const auto near=solar_radiation_pressure(craft,at({0,0,0}),sunlit(InertialVector{0,0,astronomical_unit_m}));
    const auto far=solar_radiation_pressure(craft,at({0,0,0}),sunlit(InertialVector{0,0,2*astronomical_unit_m}));
    REQUIRE(ratio(far.force_n,near.force_n/4)<=kTolerance);
    const auto half=solar_radiation_pressure(craft,at({0,0,0}),
        sunlit(InertialVector{0,0,astronomical_unit_m},0.25));
    REQUIRE(ratio(half.force_n,near.force_n*0.25)<=kTolerance);
    REQUIRE(norm(solar_radiation_pressure(craft,at({0,0,0}),
        sunlit(InertialVector{0,0,astronomical_unit_m},0)).force_n)==0);
    const auto moved=solar_radiation_pressure(craft,at({0,0,1000}),
        sunlit(InertialVector{0,0,astronomical_unit_m}));
    REQUIRE(std::abs(norm(moved.force_n)/norm(near.force_n)
        -std::pow(astronomical_unit_m/(astronomical_unit_m-1000),2))<=1e-15);
}
TEST_CASE("Attitude rotates optical force while torque remains body framed", "[radiation]") {
    const double theta=.8,c=std::cos(theta),s=std::sin(theta),k=kAuPressure*c;
    const Quaternion q{std::cos(theta/2),std::sin(theta/2),0,0};
    const auto craft=panel(1,{0,0,1},{1,0,0},{.3,.2});
    const auto tilted=solar_radiation_pressure(craft,at({},q),sunlit({0,0,astronomical_unit_m}));
    // Source in body is (0,s,c), incident coefficient .7 and recoil (.6c+.4/3).
    const double fy=-k*.7*s,fz=-k*(1.3*c+.4/3);
    REQUIRE(ratio(tilted.force_n,InertialVector{0,c*fy-s*fz,s*fy+c*fz})<1e-12);
    REQUIRE(ratio(tilted.torque_nm,BodyVector{0,-fz,fy})<1e-12);
    const auto upright=solar_radiation_pressure(craft,at({}),sunlit({0,0,astronomical_unit_m}));
    REQUIRE(norm(upright.force_n)>norm(tilted.force_n));
    const auto flipped=solar_radiation_pressure(craft,at({},{0,1,0,0}),sunlit({0,0,astronomical_unit_m}));
    REQUIRE(norm(flipped.force_n)==0);
    REQUIRE(norm(flipped.torque_nm)==0);
}
TEST_CASE("Centre of pressure, lever arm and multiple surfaces", "[radiation]") {
    const InertialVector sun{0,0,astronomical_unit_m};
    const BodyVector normal{0,0,1};
    const double area=2.0, force=2*area*kAuPressure;
    const auto single=panel(area,normal,{0.3,-0.4,0},OpticalProperties(1,0));
    const double a=std::sqrt(.5);
    const auto oblique=panel(1,{a,0,a},{0,1,0},{1,0});
    const Spacecraft pair(1000,diag(20,20,20),
        {Surface(area,normal,{.3,-.4,0},{1,0}),Surface(1,{a,0,a},{0,1,0},{1,0}),
         Surface(100,{0,0,-1},{3,4,5},{1,0})});
    const auto state=at({0,0,0});
    const auto one=solar_radiation_pressure(single,state,sunlit(sun));
    const auto two=solar_radiation_pressure(pair,state,sunlit(sun));
    const auto other=solar_radiation_pressure(oblique,state,sunlit(sun));
    REQUIRE(ratio(two.force_n,one.force_n+other.force_n)<=kTolerance);
    REQUIRE(ratio(two.force_n,InertialVector{-a*kAuPressure,0,-(4+a)*kAuPressure})<=kTolerance);
    REQUIRE(ratio(one.torque_nm,cross(BodyVector{0.3,-0.4,0},BodyVector{0,0,-force}))<=kTolerance);
    REQUIRE(ratio(two.torque_nm,BodyVector{(1.6-a)*kAuPressure,1.2*kAuPressure,a*kAuPressure})<=kTolerance);
    REQUIRE(ratio(one.force_n,InertialVector{0,0,-force})<=kTolerance);
    const auto offset=Spacecraft(1000,diag(20,20,20),
        {Surface(area,normal,{0,0,1},OpticalProperties(1,0)),
         Surface(area,normal,{0,0,-1},OpticalProperties(1,0))});
    const auto w=solar_radiation_pressure(offset,state,sunlit(sun));
    REQUIRE(ratio(w.force_n,one.force_n*2)<=kTolerance);
    REQUIRE(norm(w.torque_nm)==0);
    REQUIRE(norm(solar_radiation_pressure(panel(area,normal,{}, {1,0}),state,sunlit(sun)).torque_nm)==0);
}
TEST_CASE("Eclipse models day side, umbra and outside geometry", "[radiation]") {
    const InertialVector sun{astronomical_unit_m,0,0};
    REQUIRE(illumination_factor(EclipseModel::none,{0,0,0},sun,kRe)==1.0);
    REQUIRE(illumination_factor(EclipseModel::none,{-kRe/2,0,0},sun,kRe)==1.0);
    REQUIRE(illumination_factor(EclipseModel::cylindrical,{7e6,0,0},sun,kRe)==1.0);
    REQUIRE(illumination_factor(EclipseModel::cylindrical,{-7e6,0,0},sun,kRe)==0.0);
    REQUIRE(illumination_factor(EclipseModel::cylindrical,{-7e6,7.5e6,0},sun,kRe)==1.0);
    REQUIRE(illumination_factor(EclipseModel::conical,{7e6,0,0},sun,kRe)==1.0);
    REQUIRE(illumination_factor(EclipseModel::conical,{-7e6,0,0},sun,kRe)==0.0);
    REQUIRE(illumination_factor(EclipseModel::conical,{-7e6,7.5e6,0},sun,kRe)==1.0);
    REQUIRE(illumination_factor(EclipseModel::cylindrical,{-7e6,3e6,-2e6},sun,kRe)==0.0);
    REQUIRE(illumination_factor(EclipseModel::conical,{-7e6,3e6,-2e6},sun,kRe)==0.0);
    REQUIRE(illumination_factor(EclipseModel::cylindrical,{7e6,3e6,-2e6},sun,kRe)==1.0);
    REQUIRE(illumination_factor(EclipseModel::conical,{7e6,3e6,-2e6},sun,kRe)==1.0);
}
TEST_CASE("Conical penumbra is continuous, symmetric and monotone", "[radiation]") {
    const InertialVector sun{astronomical_unit_m,0,0};
    const double radius=7e6;
    REQUIRE(illumination_factor(EclipseModel::conical,{-radius,0,0},sun,kRe)==0.0);
    REQUIRE(illumination_factor(EclipseModel::conical,{-radius,4e6,0},sun,kRe)==0.0);
    REQUIRE(illumination_factor(EclipseModel::conical,{-radius,9e6,0},sun,kRe)==1.0);
    REQUIRE(illumination_factor(EclipseModel::cylindrical,{-radius,6.35e6,0},sun,kRe)==0.0);
    REQUIRE(illumination_factor(EclipseModel::conical,{-radius,6.35e6,0},sun,kRe)>0.0);
    REQUIRE(illumination_factor(EclipseModel::cylindrical,{-radius,6.5e6,0},sun,kRe)==1.0);
    double coarse_step=0.0, previous=0.0;
    for (int i=0;i<=2000;++i) {
        const double f=illumination_factor(EclipseModel::conical,{-radius,6.0e6+1.2e6*i/2000,0},sun,kRe);
        REQUIRE((f>=0&&f<=1));
        if (i>0) {
            REQUIRE(f>=previous-1e-15);
            coarse_step=std::max(coarse_step,f-previous);
        }
        previous=f;
    }
    REQUIRE(previous==1.0);
    REQUIRE(coarse_step<0.02);
    double fine_step=0.0;
    previous=0.0;
    for (int i=0;i<=8000;++i) {
        const double f=illumination_factor(EclipseModel::conical,{-radius,6.0e6+1.2e6*i/8000,0},sun,kRe);
        if (i>0) fine_step=std::max(fine_step,f-previous);
        previous=f;
    }
    REQUIRE(fine_step<coarse_step);
    double half_offset=0;
    for (int i=0;i<=2000;++i) {
        const double y=6.0e6+1.2e6*i/2000;
        if (illumination_factor(EclipseModel::conical,{-radius,y,0},sun,kRe)<0.5) half_offset=y;
    }
    REQUIRE((half_offset>6.3e6&&half_offset<6.5e6));
    const InertialVector probe{-radius,3e6,-2e6};
    const double angle=0.7, c=std::cos(angle), s=std::sin(angle);
    const InertialVector spun{probe.x(),c*probe.y()-s*probe.z(),s*probe.y()+c*probe.z()};
    REQUIRE(std::abs(illumination_factor(EclipseModel::conical,spun,sun,kRe)
        -illumination_factor(EclipseModel::conical,probe,sun,kRe))<=1e-12);
    REQUIRE(std::abs(illumination_factor(EclipseModel::cylindrical,spun,sun,kRe)
        -illumination_factor(EclipseModel::cylindrical,probe,sun,kRe))<=1e-12);
}
TEST_CASE("Eclipse geometry validation", "[radiation]") {
    const InertialVector sun{astronomical_unit_m,0,0};
    for (auto model:{EclipseModel::cylindrical,EclipseModel::conical}) {
        REQUIRE_THROWS_AS(illumination_factor(model,{0,0,0},sun,kRe),std::invalid_argument);
        REQUIRE_THROWS_AS(illumination_factor(model,{kRe,0,0},sun,kRe),std::invalid_argument);
        REQUIRE_THROWS_AS(illumination_factor(model,{7e6,0,0},InertialVector{},kRe),std::invalid_argument);
        REQUIRE_THROWS_AS(illumination_factor(model,{7e6,0,0},InertialVector{NAN,0,0},kRe),std::invalid_argument);
        REQUIRE_THROWS_AS(illumination_factor(model,{7e6,0,0},sun,0),std::invalid_argument);
        REQUIRE_THROWS_AS(illumination_factor(model,{7e6,0,0},sun,-kRe),std::invalid_argument);
        REQUIRE_THROWS_AS(illumination_factor(model,{7e6,0,0},sun,kRe,0),std::invalid_argument);
        REQUIRE_THROWS_AS(illumination_factor(model,{7e6,0,0},InertialVector{1,0,0},kRe),std::invalid_argument);
        REQUIRE_THROWS_AS(illumination_factor(model,{7e6,0,0},sun,NAN),std::invalid_argument);
    }
    REQUIRE(illumination_factor(EclipseModel::none,{0,0,0},InertialVector{},0)==1.0);
    const auto craft=panel(1.0,{0,0,1},{0,0,0},OpticalProperties(1,0));
    const auto state=at({7e6,0,0});
    REQUIRE_THROWS_AS(solar_radiation_pressure(craft,state,sunlit(sun,NAN)),std::invalid_argument);
    REQUIRE_THROWS_AS(solar_radiation_pressure(craft,state,sunlit(sun,-0.1)),std::invalid_argument);
    REQUIRE_THROWS_AS(solar_radiation_pressure(craft,state,sunlit(sun,1.5)),std::invalid_argument);
    REQUIRE_THROWS_AS(solar_radiation_pressure(craft,state,sunlit(state.r)),std::invalid_argument);
    REQUIRE_THROWS_AS(solar_radiation_pressure(craft,at({NAN,0,0}),sunlit(sun)),std::invalid_argument);
    REQUIRE_THROWS_AS(solar_radiation_pressure(craft,at({7e6,0,0},{0,0,0,0}),sunlit(sun)),std::invalid_argument);
    REQUIRE_THROWS_AS(simplified_albedo(craft,state,sunlit(sun),kRe,-0.2),std::invalid_argument);
    REQUIRE_THROWS_AS(simplified_albedo(craft,state,sunlit(sun),kRe,1.5),std::invalid_argument);
    REQUIRE_THROWS_AS(simplified_albedo(craft,state,sunlit(sun),0),std::invalid_argument);
    REQUIRE_THROWS_AS(simplified_albedo(craft,state,sunlit(sun),-kRe),std::invalid_argument);
    REQUIRE_THROWS_AS(simplified_albedo(craft,at({0,0,0}),sunlit(sun),kRe),std::invalid_argument);
    REQUIRE_THROWS_AS(simplified_albedo(craft,at({kRe,0,0}),sunlit(sun),kRe),std::invalid_argument);
    REQUIRE_THROWS_AS(simplified_albedo(craft,state,sunlit(InertialVector{}),kRe),std::invalid_argument);
}
TEST_CASE("Albedo pressure magnitude scaling and sign", "[radiation]") {
    const InertialVector sun{astronomical_unit_m,0,0};
    const double radius=8e6, albedo=0.3, area=2.0;
    const double pressure=kAuPressure*albedo*std::pow(kRe/radius,2);
    const auto craft=panel(area,{-1,0,0},{0,0,0},OpticalProperties(1,0));
    const auto day=simplified_albedo(craft,at({radius,0,0}),sunlit(sun),kRe,albedo);
    REQUIRE(ratio(day.force_n,InertialVector{2*area*pressure,0,0})<=kTolerance);
    REQUIRE(norm(simplified_albedo(craft,at({-radius,0,0}),sunlit(sun),kRe,albedo).force_n)==0);
    const auto terminator=simplified_albedo(craft,at({radius,0,0}),
        sunlit(InertialVector{0,astronomical_unit_m,0}),kRe,albedo);
    REQUIRE(norm(terminator.force_n)==0);
    const InertialVector tilted{astronomical_unit_m/2,astronomical_unit_m/2,0};
    const auto oblique_day=simplified_albedo(craft,at({radius,0,0}),sunlit(tilted),kRe,albedo);
    const double distance=norm(tilted), cosine=InertialVector{1,0,0}.as_eigen().dot(tilted.as_eigen())/distance;
    REQUIRE(ratio(oblique_day.force_n,
        day.force_n*(cosine*std::pow(astronomical_unit_m/distance,2)))<=kTolerance);
    const auto doubled=simplified_albedo(craft,at({2*radius,0,0}),sunlit(sun),kRe,albedo);
    REQUIRE(ratio(doubled.force_n,day.force_n/4)<=kTolerance);
    const auto brighter=simplified_albedo(craft,at({radius,0,0}),
        sunlit(InertialVector{2*astronomical_unit_m,0,0}),kRe,albedo);
    REQUIRE(ratio(brighter.force_n,day.force_n/4)<=kTolerance);
    const auto darker=simplified_albedo(craft,at({radius,0,0}),sunlit(sun),kRe,0.6);
    REQUIRE(ratio(darker.force_n,day.force_n*2)<=kTolerance);
    REQUIRE(norm(simplified_albedo(craft,at({radius,0,0}),sunlit(sun),kRe,0).force_n)==0);
}
TEST_CASE("Albedo optical momentum and torque use the same panel interaction", "[radiation]") {
    const double radius=8e6,area=4,pressure=kAuPressure*.3*std::pow(kRe/radius,2);
    const auto env=sunlit({astronomical_unit_m,0,0});
    const auto state=at({radius,0,0});
    const BodyVector cop{0,1,0};
    const auto mirror=simplified_albedo(panel(area,{-1,0,0},cop,{1,0}),state,env,kRe);
    REQUIRE(ratio(mirror.force_n,InertialVector{2*area*pressure,0,0})<1e-12);
    REQUIRE(ratio(mirror.torque_nm,BodyVector{0,0,-2*area*pressure})<1e-12);
    const auto absorbing=simplified_albedo(panel(area,{-1,0,0},{}),state,env,kRe);
    REQUIRE(ratio(absorbing.force_n,InertialVector{area*pressure,0,0})<1e-12);
    REQUIRE(norm(absorbing.torque_nm)==0);
    const auto diffuse=simplified_albedo(panel(area,{-1,0,0},cop,{0,1}),state,env,kRe);
    REQUIRE(ratio(diffuse.force_n,InertialVector{(5./3)*area*pressure,0,0})<1e-12);
    REQUIRE(ratio(diffuse.torque_nm,BodyVector{0,0,-(5./3)*area*pressure})<1e-12);
    const Spacecraft pair(1000,diag(20,20,20),{Surface(area,{-1,0,0},cop,{1,0}),
                                              Surface(area,{-1,0,0},-cop,{1,0})});
    const auto sum=simplified_albedo(pair,state,env,kRe);
    REQUIRE(ratio(sum.force_n,mirror.force_n*2)<1e-12);
    REQUIRE(norm(sum.torque_nm)<1e-18);
}