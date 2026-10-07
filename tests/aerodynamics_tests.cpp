#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "leo/kernel/aerodynamics.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>
using namespace leo;
namespace {
constexpr double kRotationRate=7.292115e-5;
constexpr double kSqrtHalf=std::numbers::sqrt2/2;
// Relative comparison only: these quantities span 1e-19 .. 1e3, so an absolute tolerance
// would either pass meaningless differences or reject exact ones.
bool close(double a,double b,double rel=1e-12) {
    if (!std::isfinite(a)||!std::isfinite(b)) return false;
    if (a==b) return true;
    return std::abs(a-b)<=rel*std::max(std::abs(a),std::abs(b));
}
Spacecraft panel_pair(double area,BodyVector normal,double cd,double sigma,std::vector<BodyVector> cops) {
    std::vector<Surface> surfaces;
    for (const BodyVector& cop:cops)
        surfaces.emplace_back(area,normal,cop,OpticalProperties(),AerodynamicProperties(cd,sigma));
    return Spacecraft(1000,Inertia(Eigen::Vector3d(400,500,600).asDiagonal()),std::move(surfaces));
}
Spacecraft single_plate(double area,BodyVector normal,BodyVector cop,double cd,double sigma,
                        double mass=1000) {
    return Spacecraft(mass,Inertia(Eigen::Vector3d(400,500,600).asDiagonal()),
                      {Surface(area,normal,cop,OpticalProperties(),AerodynamicProperties(cd,sigma))});
}
State body_aligned_state(InertialVector r,InertialVector v) { return State{r,v,{1,0,0,0},{0,0,0}}; }
EnvironmentSnapshot snapshot_at(double density,InertialVector rotation={0,0,0}) {
    EnvironmentSnapshot env;
    env.density_kg_m3=density;
    env.atmosphere_angular_velocity_rad_s=rotation;
    return env;
}
// +90 degrees about +X maps the body +Y axis onto inertial +Z.
constexpr Quaternion kPitchUp{kSqrtHalf,kSqrtHalf,0,0};
}
TEST_CASE("Exponential atmosphere reproduces the tabulated reference","[aerodynamics][atmosphere]") {
    ExponentialAtmosphere atmosphere;
    REQUIRE(atmosphere.density_kg_m3(0.0)==1.225);
    REQUIRE(atmosphere.density_kg_m3(4e5)==2.62e-12);       // tabulated 400 km row
    REQUIRE(atmosphere.density_kg_m3(1e5)==5.25e-07);       // exact knot
    REQUIRE(atmosphere.density_kg_m3(3.5786e7)==4.04e-19);  // top anchor
    REQUIRE(atmosphere.density_kg_m3(1.25e6)==5.70e-16);    // 1250 km knot
    REQUIRE(atmosphere.density_kg_m3(1e6)==1.49e-15);        // 1000 km knot
    // A midpoint in altitude is the geometric mean of adjacent density anchors.
    REQUIRE(close(atmosphere.density_kg_m3(225e3),std::sqrt(2.41e-10*5.97e-11)));
    REQUIRE(close(atmosphere.density_kg_m3(425e3),std::sqrt(2.62e-12*1.09e-12)));
    REQUIRE(close(atmosphere.density_kg_m3(12500e3),std::sqrt(2.37e-18*1.21e-18)));
}
TEST_CASE("Exponential atmosphere is globally monotonic and continuous", "[aerodynamics][atmosphere]") {
    ExponentialAtmosphere atmosphere;
    constexpr double knots_m[]={0,1e5,1.5e5,2e5,2.5e5,3e5,3.5e5,4e5,4.5e5,5e5,5.5e5,6e5,6.5e5,7e5,
        7.5e5,8e5,8.5e5,9e5,9.5e5,1e6,1.25e6,1.5e6,2e6,2.5e6,3e6,3.5e6,4e6,4.5e6,5e6,6e6,7e6,8e6,
        9e6,1e7,1.5e7,2e7,2.5e7,3e7,3.5e7,3.5786e7};
    constexpr std::size_t count=sizeof(knots_m)/sizeof(knots_m[0]);
    for (std::size_t i=0;i+1<count;++i) {
        const double lo=knots_m[i],hi=knots_m[i+1],step=(hi-lo)/9;
        for (int k=1;k<9;++k)
            REQUIRE(atmosphere.density_kg_m3(lo+step*k)<atmosphere.density_kg_m3(lo+step*(k-1)));
        REQUIRE(atmosphere.density_kg_m3(lo)>atmosphere.density_kg_m3(hi-1e-3));
        REQUIRE(atmosphere.density_kg_m3(hi)<atmosphere.density_kg_m3(hi-1e-3));
        REQUIRE(close(atmosphere.density_kg_m3(hi-1e-3),atmosphere.density_kg_m3(hi),1e-6));
    }
    REQUIRE(atmosphere.density_kg_m3(1.5e7)<atmosphere.density_kg_m3(1.5e7-1e3));
    REQUIRE(atmosphere.density_kg_m3(1.25e6)<atmosphere.density_kg_m3(1.25e6-1e3));
}
TEST_CASE("Exponential atmosphere scale, domain and validation","[aerodynamics][atmosphere]") {
    ExponentialAtmosphere plain,doubled(2.0),zero(0.0),scaled(1e-3);
    REQUIRE(close(doubled.density_kg_m3(4e5),2*plain.density_kg_m3(4e5)));
    REQUIRE(close(scaled.density_kg_m3(4e5),1e-3*plain.density_kg_m3(4e5)));
    REQUIRE(zero.density_kg_m3(4e5)==0.0);
    REQUIRE(plain.density_kg_m3(0.0)>0.0);
    REQUIRE(plain.density_kg_m3(3.5786e7)>0.0);
    // unsupported altitudes are rejected, never silently reported as vacuum
    REQUIRE_THROWS_AS(plain.density_kg_m3(-1e-9),std::out_of_range);
    REQUIRE_THROWS_AS(plain.density_kg_m3(3.5786e7+1e-3),std::out_of_range);
    REQUIRE_THROWS_AS(plain.density_kg_m3(-INFINITY),std::invalid_argument);
    REQUIRE_THROWS_AS(plain.density_kg_m3(NAN),std::invalid_argument);
    REQUIRE_THROWS_AS(plain.density_kg_m3(INFINITY),std::invalid_argument);
    REQUIRE_THROWS_AS(ExponentialAtmosphere(-1e-12),std::invalid_argument);
    REQUIRE_THROWS_AS(ExponentialAtmosphere(NAN),std::invalid_argument);
    REQUIRE_THROWS_AS(ExponentialAtmosphere(INFINITY),std::invalid_argument);
}
TEST_CASE("Atmospheric relative velocity from co-rotation","[aerodynamics][atmosphere]") {
    const InertialVector omega{0,0,kRotationRate};
    const InertialVector equatorial{7e6,0,0};
    auto wind=atmospheric_relative_velocity(equatorial,{0,0,0},omega);
    REQUIRE(close(wind.x(),0));
    REQUIRE(close(wind.z(),0));
    REQUIRE(close(wind.y(),-kRotationRate*7e6));
    // the default rotation is the sidereal Earth rate about +Z
    REQUIRE(norm(atmospheric_relative_velocity(equatorial,{0,0,0})-wind)==0);
    REQUIRE(norm(atmospheric_relative_velocity(equatorial,{0,0,0},{0,0,kRotationRate})-wind)==0);
    REQUIRE(norm(atmospheric_relative_velocity(equatorial,{0,0,0},{0,0,-kRotationRate})+wind)==0);
    // on the polar axis the rotation axis is parallel to r, so the wind is exactly v
    REQUIRE(norm(atmospheric_relative_velocity({0,0,7e6},{300,400,500},omega)-InertialVector{300,400,500})==0);
    // off the polar axis the co-rotation contributes cross(omega, r), of magnitude
    // omega r sin(latitude): maximal on the equator and vanishing at the pole
    REQUIRE(close(norm(cross(omega,{0,7e6,0})),kRotationRate*7e6));
    REQUIRE(close(norm(cross(omega,equatorial)),kRotationRate*7e6));
    const InertialVector midlatitude{0,7e6*kSqrtHalf,7e6*kSqrtHalf};
    REQUIRE(close(norm(cross(omega,midlatitude)),kRotationRate*7e6*kSqrtHalf));
    REQUIRE(norm(cross(omega,midlatitude))<norm(cross(omega,equatorial)));
    // so the same stationary craft feels less wind away from the equator
    REQUIRE(norm(atmospheric_relative_velocity(midlatitude,{0,0,0},omega))<
            norm(atmospheric_relative_velocity(equatorial,{0,0,0},omega)));
    // an equatorial craft stationary in the rotating frame sees no wind
    REQUIRE(norm(atmospheric_relative_velocity(equatorial,cross(omega,equatorial),omega))<=1e-9);
    REQUIRE(norm(atmospheric_relative_velocity(equatorial,cross(omega,equatorial)))<=1e-9);
    // an equatorial craft moving with the rotation still sees no wind
    REQUIRE(norm(atmospheric_relative_velocity(equatorial,{0,kRotationRate*7e6,0},omega))<=1e-9);
    REQUIRE(norm(cross(omega,equatorial)-InertialVector{0,kRotationRate*7e6,0})<=1e-9);
    REQUIRE(norm(atmospheric_relative_velocity(equatorial,{1,2,3},{0,0,0})-InertialVector{1,2,3})==0);
    REQUIRE(norm(atmospheric_relative_velocity(equatorial,{-1,-2,-3},{0,0,0})+InertialVector{1,2,3})==0);
    REQUIRE_THROWS_AS(atmospheric_relative_velocity({NAN,0,0},{0,0,0},{0,0,0}),std::invalid_argument);
    REQUIRE_THROWS_AS(atmospheric_relative_velocity(equatorial,{0,0,NAN},{0,0,0}),std::invalid_argument);
    REQUIRE_THROWS_AS(atmospheric_relative_velocity(equatorial,{0,0,0},{0,0,NAN}),std::invalid_argument);
}
TEST_CASE("Flat plate drag projection, sign and scaling","[aerodynamics][flatplate]") {
    const double rho=3.5e-12,A=2.5,cd=2.2,speed=7600,q_dyn=0.5*rho*speed*speed;
    const InertialVector r{7e6,0,0};
    auto craft=single_plate(A,{0,0,1},{1,0,0},cd,0.9);
    auto head_on=flat_plate_drag(craft,body_aligned_state(r,{0,0,speed}),snapshot_at(rho));
    REQUIRE(close(head_on.force_n.z(),-q_dyn*cd*A));
    REQUIRE(close(head_on.force_n.x(),0));
    REQUIRE(close(head_on.force_n.y(),0));
    REQUIRE(norm(cross(InertialVector{0,0,speed},head_on.force_n))<=1e-9);
    // flow parallel to the plate loads nothing; flow at 45 degrees loads cos(45)
    REQUIRE(norm(flat_plate_drag(craft,body_aligned_state(r,{speed,0,0}),snapshot_at(rho)).force_n)==0);
    auto oblique=flat_plate_drag(craft,body_aligned_state(r,{speed*kSqrtHalf,0,speed*kSqrtHalf}),snapshot_at(rho));
    REQUIRE(close(norm(oblique.force_n),q_dyn*cd*A*kSqrtHalf));
    REQUIRE(close(oblique.force_n.z(),-0.5*q_dyn*cd*A));
    REQUIRE(close(oblique.force_n.x(),-0.5*q_dyn*cd*A));
    // a rear-facing panel is shielded
    REQUIRE(norm(flat_plate_drag(single_plate(A,{0,0,-1},{1,0,0},cd,0.9),
        body_aligned_state(r,{0,0,speed}),snapshot_at(rho)).force_n)==0);
    REQUIRE(norm(flat_plate_drag(craft,body_aligned_state(r,{0,0,-speed}),snapshot_at(rho)).force_n)==0);
    // linear in density and Cd, quadratic in speed, linear in area
    REQUIRE(close(norm(flat_plate_drag(craft,body_aligned_state(r,{0,0,speed}),snapshot_at(2*rho)).force_n),
                 2*norm(head_on.force_n)));
    REQUIRE(close(norm(flat_plate_drag(craft,body_aligned_state(r,{0,0,2*speed}),snapshot_at(rho)).force_n),
                 4*norm(head_on.force_n)));
    REQUIRE(close(norm(flat_plate_drag(single_plate(A,{0,0,1},{1,0,0},2*cd,0.9),
                 body_aligned_state(r,{0,0,speed}),snapshot_at(rho)).force_n),2*norm(head_on.force_n)));
    REQUIRE(close(norm(flat_plate_drag(single_plate(2*A,{0,0,1},{1,0,0},cd,0.9),
                 body_aligned_state(r,{0,0,speed}),snapshot_at(rho)).force_n),2*norm(head_on.force_n)));
    // mass is not an input to the force law
    REQUIRE(norm(flat_plate_drag(single_plate(A,{0,0,1},{1,0,0},cd,0.9,1e6),
                 body_aligned_state(r,{0,0,speed}),snapshot_at(rho)).force_n-head_on.force_n)<=1e-18);
    // exact zeros
    REQUIRE(norm(flat_plate_drag(single_plate(A,{0,0,1},{1,0,0},0,0.9),
                 body_aligned_state(r,{0,0,speed}),snapshot_at(rho)).force_n)==0);
    REQUIRE(norm(flat_plate_drag(craft,body_aligned_state(r,{0,0,speed}),snapshot_at(0)).force_n)==0);
    REQUIRE(norm(flat_plate_drag(craft,body_aligned_state(r,{0,0,0}),snapshot_at(rho)).force_n)==0);
    // a surface-less craft produces nothing
    Spacecraft bare(1000,Inertia(Eigen::Vector3d(400,500,600).asDiagonal()));
    REQUIRE(norm(flat_plate_drag(bare,body_aligned_state(r,{0,0,speed}),snapshot_at(rho)).force_n)==0);
    REQUIRE(norm(flat_plate_drag(bare,body_aligned_state(r,{0,0,speed}),snapshot_at(rho)).torque_nm)==0);
}
TEST_CASE("Flat plate drag lever arm, centre of mass and composition","[aerodynamics][flatplate]") {
    const double rho=3.5e-12,A=2.5,cd=2.2,speed=7600,q_dyn=0.5*rho*speed*speed;
    const InertialVector r{7e6,0,0};
    auto state=body_aligned_state(r,{0,0,speed});
    auto centre=flat_plate_drag(single_plate(A,{0,0,1},{0,0,0},cd,0.9),state,snapshot_at(rho));
    REQUIRE(close(centre.force_n.z(),-q_dyn*cd*A));
    REQUIRE(norm(centre.torque_nm)==0);                     // force through the centre of mass
    auto lever=flat_plate_drag(single_plate(A,{0,0,1},{1,0,0},cd,0.9),state,snapshot_at(rho));
    REQUIRE(norm(lever.force_n-centre.force_n)<=1e-18);      // the lever arm does not change the force
    // torque = r_cp x F_body, so an X offset against a -Z force produces a +Y body torque
    REQUIRE(close(lever.torque_nm.y(),q_dyn*cd*A));
    REQUIRE(close(lever.torque_nm.x(),0));
    REQUIRE(close(lever.torque_nm.z(),0));
    auto moved=flat_plate_drag(single_plate(A,{0,0,1},{1,2,3},cd,0.9),state,snapshot_at(rho));
    REQUIRE(norm(moved.force_n-lever.force_n)<=1e-18);
    // the torque increment is delta_r x F evaluated entirely in the body frame
    const BodyVector force_body{0,0,-q_dyn*cd*A};
    REQUIRE(norm(moved.torque_nm-lever.torque_nm-cross(BodyVector{0,2,3},force_body))<=1e-18);
    REQUIRE(close(moved.torque_nm.x(),-2*q_dyn*cd*A));
    // two identical loaded panels symmetric about the centre of mass cancel their torque
    auto pair=panel_pair(A,{0,0,1},cd,0.9,{{1,0,0},{-1,0,0}});
    auto symmetric=flat_plate_drag(pair,state,snapshot_at(rho));
    REQUIRE(norm(symmetric.torque_nm)<=1e-18);
    REQUIRE(close(norm(symmetric.force_n),2*norm(centre.force_n)));
    // forces and torques add linearly over independent spacecraft
    auto left=flat_plate_drag(panel_pair(A,{0,0,1},cd,0.9,{{1,0,0}}),state,snapshot_at(rho));
    auto right=flat_plate_drag(panel_pair(A,{0,0,1},cd,0.9,{{-1,0,0}}),state,snapshot_at(rho));
    REQUIRE(close(norm(left.force_n),norm(right.force_n)));
    REQUIRE(close(norm(left.torque_nm),norm(right.torque_nm)));
    REQUIRE(norm(symmetric.force_n-(left.force_n+right.force_n))<=1e-18);
    REQUIRE(norm(symmetric.torque_nm-(left.torque_nm+right.torque_nm))<=1e-18);
    REQUIRE(norm(left.torque_nm+right.torque_nm)==0);
    // three panels with unequal arms sum to the same wrench as the explicit sum
    auto trio=panel_pair(A,{0,0,1},cd,0.9,{{1,0,0},{-1,0,0},{2,1,0}});
    auto split=flat_plate_drag(trio,state,snapshot_at(rho));
    auto extra=flat_plate_drag(panel_pair(A,{0,0,1},cd,0.9,{{2,1,0}}),state,snapshot_at(rho));
    REQUIRE(norm(split.force_n-(symmetric.force_n+extra.force_n))<=1e-18);
    REQUIRE(norm(split.torque_nm-(symmetric.torque_nm+extra.torque_nm))<=1e-18);
    // rear-facing panels see nothing
    REQUIRE(norm(flat_plate_drag(panel_pair(A,{0,0,-1},cd,0.9,{{1,0,0},{-1,0,0}}),
                 state,snapshot_at(rho)).force_n)==0);
}
TEST_CASE("Flat plate drag attitude and co-rotation coupling","[aerodynamics][flatplate]") {
    const double rho=3.5e-12,A=2.5,cd=2.2,speed=300.0,q_dyn=0.5*rho*speed*speed;
    const InertialVector r{7e6,0,0};
    // attitude changes the incidence, so the same plate normal gives a different load
    auto plate=[&](Quaternion q,BodyVector cop) {
        return flat_plate_drag(single_plate(A,{0,1,0},cop,cd,0.9),
                               State{r,{0,0,speed},q,{0,0,0}},snapshot_at(rho));
    };
    auto unrotated=plate({1,0,0,0},{0,0,0});
    REQUIRE(norm(unrotated.force_n)==0);                    // +Y normal, +Z flow: no incidence
    auto pitched=plate(kPitchUp,{0,0,0});
    REQUIRE(close(pitched.force_n.z(),-q_dyn*cd*A));         // now the normal faces the flow
    REQUIRE(close(pitched.force_n.x(),0));
    REQUIRE(close(pitched.force_n.y(),0));
    REQUIRE(norm(pitched.torque_nm)==0);
    // the body torque stays in the body frame regardless of attitude
    auto offset=plate(kPitchUp,{0,0,1});
    REQUIRE(close(offset.torque_nm.x(),q_dyn*cd*A));
    REQUIRE(close(offset.torque_nm.y(),0));
    REQUIRE(close(offset.torque_nm.z(),0));
    REQUIRE(close(norm(offset.force_n),norm(pitched.force_n)));
    // an unnormalised quaternion describing the same attitude gives the same wrench
    State scaled{r,{0,0,speed},{2*kPitchUp.w,2*kPitchUp.x,2*kPitchUp.y,2*kPitchUp.z},{0,0,0}};
    REQUIRE(norm(flat_plate_drag(single_plate(A,{0,1,0},{0,0,0},cd,0.9),scaled,snapshot_at(rho)).force_n
                 -pitched.force_n)<=1e-18);
    // a craft parked over the equator is dragged along by the co-rotating atmosphere
    const double wind=kRotationRate*r.x();
    auto parked=flat_plate_drag(single_plate(A,{0,-1,0},{0,0,0},cd,0.9),
                               body_aligned_state(r,{0,0,0}),snapshot_at(rho,{0,0,kRotationRate}));
    REQUIRE(close(parked.force_n.y(),0.5*rho*wind*wind*cd*A));
    REQUIRE(close(parked.force_n.x(),0));
    // under reversed co-rotation the same panel is shielded instead of loaded
    auto reversed=flat_plate_drag(single_plate(A,{0,-1,0},{0,0,0},cd,0.9),
                                  body_aligned_state(r,{0,0,0}),snapshot_at(rho,{0,0,-kRotationRate}));
    REQUIRE(norm(reversed.force_n)==0);
}
TEST_CASE("Flat plate drag input validation","[aerodynamics][flatplate]") {
    auto craft=single_plate(1,{0,0,1},{1,0,0},2.2,0.9);
    auto state=body_aligned_state({7e6,0,0},{0,0,7600});
    REQUIRE_THROWS_AS(flat_plate_drag(craft,state,snapshot_at(NAN)),std::invalid_argument);
    REQUIRE_THROWS_AS(flat_plate_drag(craft,state,snapshot_at(-1e-9)),std::invalid_argument);
    REQUIRE_THROWS_AS(flat_plate_drag(craft,state,snapshot_at(INFINITY)),std::invalid_argument);
    REQUIRE_THROWS_AS(flat_plate_drag(craft,state,snapshot_at(1,{NAN,0,0})),std::invalid_argument);
    State bad=state; bad.r={NAN,0,0};
    REQUIRE_THROWS_AS(flat_plate_drag(craft,bad,snapshot_at(1)),std::invalid_argument);
    bad=state; bad.v={0,INFINITY,0};
    REQUIRE_THROWS_AS(flat_plate_drag(craft,bad,snapshot_at(1)),std::invalid_argument);
    bad=state; bad.q={0,0,0,0};
    REQUIRE_THROWS_AS(flat_plate_drag(craft,bad,snapshot_at(1)),std::invalid_argument);
}
TEST_CASE("Sentman coefficient hand cases","[aerodynamics][sentman]") {
    auto grazing=sentman_coefficients(0,0.9);
    REQUIRE(grazing.drag==0.0);
    REQUIRE(grazing.lift==0.0);
    auto face_on=sentman_coefficients(std::numbers::pi/2,0.9);
    REQUIRE(close(face_on.drag,4-2*0.9));
    REQUIRE(std::abs(face_on.lift)<1e-15);
    REQUIRE(close(sentman_coefficients(std::numbers::pi/4,0.9).drag,std::sqrt(2.0)));
    REQUIRE(close(sentman_coefficients(std::numbers::pi/4,0.9).lift,2*(1-0.9)*kSqrtHalf));
    // sigma = 1 is fully diffuse with cold re-emission: Cd = 2 sin(alpha), Cl = 0
    for (double deg:{5.0,30.0,45.0,80.0}) {
        auto c=sentman_coefficients(deg*std::numbers::pi/180,1.0);
        REQUIRE(close(c.drag,2*std::sin(deg*std::numbers::pi/180)));
        REQUIRE(std::abs(c.lift)<1e-15);
    }
    // sigma = 0 is a purely normal specular impulse: Cd = 4 sin^3, Cl = 4 sin^2 cos
    for (double deg:{10.0,30.0,60.0}) {
        const double a=deg*std::numbers::pi/180,s=std::sin(a);
        auto c=sentman_coefficients(a,0.0);
        REQUIRE(close(c.drag,4*s*s*s));
        REQUIRE(close(c.lift,4*s*s*std::cos(a)));
    }
    REQUIRE(close(sentman_coefficients(std::numbers::pi/2,0.0).drag,4.0));
    REQUIRE(std::abs(sentman_coefficients(std::numbers::pi/2,0.0).lift)<1e-15);
    // the drag coefficient at 45 degrees does not depend on accommodation
    for (double sigma:{0.0,0.5,0.9,1.0})
        REQUIRE(close(sentman_coefficients(std::numbers::pi/4,sigma).drag,std::sqrt(2.0)));
    // Lift vanishes for fully diffuse cold re-emission and grows as accommodation falls.
    REQUIRE(std::abs(sentman_coefficients(std::numbers::pi/4,1.0).lift)<1e-15);
    REQUIRE(sentman_coefficients(std::numbers::pi/4,0.0).lift>sentman_coefficients(std::numbers::pi/4,0.5).lift);
    REQUIRE(sentman_coefficients(std::numbers::pi/4,0.5).lift>sentman_coefficients(std::numbers::pi/4,0.9).lift);
}
TEST_CASE("Sentman coefficients stay finite and physical over the full range","[aerodynamics][sentman]") {
    for (int i=0;i<=180;++i) {
        const double a=i*std::numbers::pi/360;
        for (double sigma:{0.0,0.25,0.5,0.75,1.0}) {
            auto coefficients=sentman_coefficients(a,sigma);
            REQUIRE(std::isfinite(coefficients.drag));
            REQUIRE(std::isfinite(coefficients.lift));
            // Fully diffuse cold re-emission has exactly zero lift; only rounding
            // can make it a few ulps negative
            REQUIRE(coefficients.drag>=0.0);
            REQUIRE(coefficients.lift>=-1e-15);
            REQUIRE(coefficients.drag<=4.0);
            REQUIRE(coefficients.lift<=1.54);
        }
    }
    // Fully diffuse cold re-emission carries no lift and matches 2 sin(alpha).
    REQUIRE(std::abs(sentman_coefficients(0,1.0).lift)<1e-15);
    REQUIRE(std::abs(sentman_coefficients(std::numbers::pi/2,1.0).lift)<1e-15);
    REQUIRE(sentman_coefficients(0,0.5).lift==0.0);
    REQUIRE(std::abs(sentman_coefficients(std::numbers::pi/2,0.5).lift)<1e-15);
    // drag increases monotonically with incidence for every accommodation
    for (double sigma:{0.0,0.5,0.9,1.0}) {
        double previous=-1.0;
        for (int i=0;i<=90;++i) {
            const double drag=sentman_coefficients(i*std::numbers::pi/180,sigma).drag;
            REQUIRE(drag>=previous);
            previous=drag;
        }
    }
}
TEST_CASE("Sentman coefficient input validation","[aerodynamics][sentman]") {
    REQUIRE_THROWS_AS(sentman_coefficients(-1e-9,0.9),std::invalid_argument);
    REQUIRE_THROWS_AS(sentman_coefficients(std::numbers::pi/2+1e-9,0.9),std::invalid_argument);
    REQUIRE_THROWS_AS(sentman_coefficients(-std::numbers::pi,0.9),std::invalid_argument);
    REQUIRE_THROWS_AS(sentman_coefficients(NAN,0.9),std::invalid_argument);
    REQUIRE_THROWS_AS(sentman_coefficients(INFINITY,0.9),std::invalid_argument);
    REQUIRE_THROWS_AS(sentman_coefficients(1.0,-1e-9),std::invalid_argument);
    REQUIRE_THROWS_AS(sentman_coefficients(1.0,1+1e-9),std::invalid_argument);
    REQUIRE_THROWS_AS(sentman_coefficients(1.0,NAN),std::invalid_argument);
    REQUIRE_THROWS_AS(sentman_coefficients(1.0,INFINITY),std::invalid_argument);
}
TEST_CASE("Sentman force orientation follows the plate-frame definition","[aerodynamics][sentman]") {
    const double rho=3.5e-12,A=2.5,speed=7600,q_dyn=0.5*rho*speed*speed;
    const InertialVector r{7e6,0,0};
    // Flow along +Z with the plate normal set by its grazing angle.
    auto plate=[&](double deg,double sigma) {
        const double a=deg*std::numbers::pi/180;
        return sentman_drag(single_plate(A,{std::cos(a),0,std::sin(a)},{0,0,0},2.2,sigma),
                            body_aligned_state(r,{0,0,speed}),snapshot_at(rho));
    };
    // Face-on diffuse interaction with cold re-emission: impulse magnitude 2 q A.
    auto face_on=plate(90,1.0);
    REQUIRE(close(face_on.force_n.z(),-2*q_dyn*A));
    REQUIRE(close(face_on.force_n.x(),0));
    // Fully diffuse cold re-emission pushes opposite the flow at every incidence.
    for (double deg:{15.0,40.0,65.0,89.0}) {
        auto f=plate(deg,1.0);
        REQUIRE(close(norm(f.force_n),2*q_dyn*A*std::sin(deg*std::numbers::pi/180)));
        REQUIRE(norm(cross(InertialVector{0,0,1},f.force_n))<=1e-9*norm(f.force_n));
    }
    // sigma = 0 is a pure normal impulse along -n, never along the flow
    for (double deg:{20.0,45.0,70.0}) {
        const double a=deg*std::numbers::pi/180;
        auto f=plate(deg,0.0);
        const double magnitude=4*q_dyn*A*std::sin(a)*std::sin(a);
        REQUIRE(close(norm(f.force_n),magnitude));
        REQUIRE(close(f.force_n.x(),-magnitude*std::cos(a)));
        REQUIRE(close(f.force_n.z(),-magnitude*std::sin(a)));
    }
    // Mixed specular/diffuse interaction at 45 degrees has drag and lift components.
    auto coefficients=sentman_coefficients(std::numbers::pi/4,0.5);
    auto oblique=plate(45,0.5);
    REQUIRE(close(norm(oblique.force_n),q_dyn*A*std::hypot(coefficients.drag,coefficients.lift)));
    REQUIRE(close(oblique.force_n.x(),-4*q_dyn*A*0.5*0.5*kSqrtHalf));
    REQUIRE(close(oblique.force_n.z(),-2*q_dyn*A*kSqrtHalf));
    // the flow-parallel component of the force matches the reported drag coefficient
    REQUIRE(close(-dot(oblique.force_n,InertialVector{0,0,1}),q_dyn*A*coefficients.drag));
    // the perpendicular component matches the reported lift coefficient
    REQUIRE(close(-dot(oblique.force_n,InertialVector{1,0,0}),q_dyn*A*coefficients.lift));
    // at 45 degrees the flow-parallel drag component is accommodation independent,
    // but the lift component (and so the total magnitude) is not
    REQUIRE(close(-dot(plate(45,0.5).force_n,InertialVector{0,0,1}),
                  -dot(plate(45,0.9).force_n,InertialVector{0,0,1})));
    REQUIRE(close(-dot(plate(45,0.0).force_n,InertialVector{0,0,1}),
                  -dot(plate(45,0.5).force_n,InertialVector{0,0,1})));
    REQUIRE(norm(plate(45,0.9).force_n)<norm(plate(45,0.5).force_n));
    REQUIRE(close(plate(45,0.0).force_n.x(),2*plate(45,0.5).force_n.x()));
    REQUIRE(norm(plate(45,0.9).force_n)<norm(plate(45,0.0).force_n));
    // shielded rear panels, grazing flow and vanishing density or wind all give nothing
    REQUIRE(norm(sentman_drag(single_plate(A,{0,0,-1},{0,0,0},2.2,0.9),
        body_aligned_state(r,{0,0,speed}),snapshot_at(rho)).force_n)==0);
    REQUIRE(norm(plate(0.0,0.9).force_n)==0);
    REQUIRE(norm(sentman_drag(single_plate(A,{0,0,1},{0,0,0},2.2,0.9),
        body_aligned_state(r,{0,0,speed}),snapshot_at(0)).force_n)==0);
    REQUIRE(norm(sentman_drag(single_plate(A,{0,0,1},{0,0,0},2.2,0.9),
        body_aligned_state(r,{0,0,0}),snapshot_at(rho)).force_n)==0);
    Spacecraft bare(1000,Inertia(Eigen::Vector3d(400,500,600).asDiagonal()));
    REQUIRE(norm(sentman_drag(bare,body_aligned_state(r,{0,0,speed}),snapshot_at(rho)).force_n)==0);
}
TEST_CASE("Sentman torque, attitude and composition","[aerodynamics][sentman]") {
    const double rho=3.5e-12,A=2.5,speed=7600,q_dyn=0.5*rho*speed*speed;
    const InertialVector r{7e6,0,0};
    auto state=body_aligned_state(r,{0,0,speed});
    auto centre=sentman_drag(single_plate(A,{0,0,1},{0,0,0},2.2,1.0),state,snapshot_at(rho));
    REQUIRE(close(centre.force_n.z(),-2*q_dyn*A));
    REQUIRE(norm(centre.torque_nm)==0);
    auto lever=sentman_drag(single_plate(A,{0,0,1},{1,0,0},2.2,1.0),state,snapshot_at(rho));
    REQUIRE(norm(lever.force_n-centre.force_n)<=1e-18);
    REQUIRE(close(lever.torque_nm.y(),2*q_dyn*A));           // r_cp x F_body
    REQUIRE(close(lever.torque_nm.x(),0));
    REQUIRE(close(lever.torque_nm.z(),0));
    auto moved=sentman_drag(single_plate(A,{0,0,1},{1,3,0},2.2,1.0),state,snapshot_at(rho));
    REQUIRE(norm(moved.force_n-lever.force_n)<=1e-18);
    REQUIRE(close(moved.torque_nm.x(),-6*q_dyn*A));
    REQUIRE(close(moved.torque_nm.y(),lever.torque_nm.y()));
    // a symmetric pair cancels its torque about the centre of mass and doubles the force
    auto pair=panel_pair(A,{0,0,1},2.2,1.0,{{1,0,0},{-1,0,0}});
    auto symmetric=sentman_drag(pair,state,snapshot_at(rho));
    REQUIRE(norm(symmetric.torque_nm)<=1e-18);
    REQUIRE(close(norm(symmetric.force_n),2*norm(centre.force_n)));
    auto left=sentman_drag(panel_pair(A,{0,0,1},2.2,1.0,{{1,0,0}}),state,snapshot_at(rho));
    auto right=sentman_drag(panel_pair(A,{0,0,1},2.2,1.0,{{-1,0,0}}),state,snapshot_at(rho));
    REQUIRE(norm(symmetric.force_n-(left.force_n+right.force_n))<=1e-18);
    REQUIRE(norm(symmetric.torque_nm-(left.torque_nm+right.torque_nm))<=1e-18);
    REQUIRE(norm(left.torque_nm+right.torque_nm)==0);
    // attitude changes the incidence and carries the body torque into inertial axes
    auto plate=[&](Quaternion q,BodyVector normal,BodyVector cop) {
        return sentman_drag(single_plate(A,normal,cop,2.2,1.0),State{r,{0,0,speed},q,{0,0,0}},snapshot_at(rho));
    };
    REQUIRE(norm(plate({1,0,0,0},{0,1,0},{0,0,0}).force_n)==0);
    auto turned=plate(kPitchUp,{0,1,0},{0,0,0});
    REQUIRE(close(turned.force_n.z(),-2*q_dyn*A));
    REQUIRE(close(turned.force_n.x(),0));
    auto turned_offset=plate(kPitchUp,{0,1,0},{0,0,1});
    REQUIRE(close(turned_offset.torque_nm.x(),2*q_dyn*A));
    REQUIRE(close(turned_offset.torque_nm.y(),0));
    REQUIRE(close(turned_offset.torque_nm.z(),0));
    REQUIRE(close(norm(turned_offset.force_n),norm(turned.force_n)));
    // A parked equatorial craft feels co-rotating drag on a fully diffuse cold plate.
    const double wind=kRotationRate*r.x();
    auto parked=sentman_drag(single_plate(A,{0,-1,0},{0,0,0},2.2,1.0),
                             body_aligned_state(r,{0,0,0}),snapshot_at(rho,{0,0,kRotationRate}));
    REQUIRE(close(parked.force_n.y(),2*0.5*rho*wind*wind*A));
    REQUIRE(close(parked.force_n.x(),0));
}
TEST_CASE("Sentman drag input validation","[aerodynamics][sentman]") {
    auto craft=single_plate(1,{0,0,1},{1,0,0},2.2,0.9);
    auto state=body_aligned_state({7e6,0,0},{0,0,7600});
    REQUIRE_THROWS_AS(sentman_drag(craft,state,snapshot_at(NAN)),std::invalid_argument);
    REQUIRE_THROWS_AS(sentman_drag(craft,state,snapshot_at(-1e-9)),std::invalid_argument);
    REQUIRE_THROWS_AS(sentman_drag(craft,state,snapshot_at(INFINITY)),std::invalid_argument);
    REQUIRE_THROWS_AS(sentman_drag(craft,state,snapshot_at(1,{NAN,0,0})),std::invalid_argument);
    State bad=state; bad.r={0,0,NAN};
    REQUIRE_THROWS_AS(sentman_drag(craft,bad,snapshot_at(1)),std::invalid_argument);
    bad=state; bad.v={0,INFINITY,0};
    REQUIRE_THROWS_AS(sentman_drag(craft,bad,snapshot_at(1)),std::invalid_argument);
    bad=state; bad.q={0,0,0,0};
    REQUIRE_THROWS_AS(sentman_drag(craft,bad,snapshot_at(1)),std::invalid_argument);
}