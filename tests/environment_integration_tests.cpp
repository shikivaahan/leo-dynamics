#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "leo/environment/source.hpp"
#include "leo/integration/propagate.hpp"
#include <array>
#include <numbers>
using namespace leo;
using Catch::Approx;
namespace {
const Earth earth;
State orbit() { const double r=earth.equatorial_radius_m+400e3; return {{r,0,0},{0,std::sqrt(earth.mu_m3_s2/r),0},{1,0,0,0},{}}; }
State advance(const EarthDynamics& rhs,const State& state,const EnvironmentSnapshot& env,double duration=10) {
    const auto source=[env](ElapsedTime,const State&){return env;};
    const EnvironmentalDynamics coupled(rhs,source);
    const std::array times{0.,duration};
    IntegrationOptions options; options.initial_step_s=.1; options.max_step_s=.5;
    return propagate(coupled,state,env.epoch,times,options).samples.back().state;
}
double energy(const State& s) { return .5*dot(s.v,s.v)-earth.mu_m3_s2/norm(s.r); }
Spacecraft panel(BodyVector normal,BodyVector center={}) {
    return Spacecraft(10,Inertia(10*Eigen::Matrix3d::Identity()),{Surface(1000,normal,center)});
}
}
TEST_CASE("Coupled SRP changes orbit only for illuminated front-facing panels", "[environment_integration]") {
    const State start=orbit();
    EnvironmentSnapshot env; env.sun_position_m=start.r+InertialVector{astronomical_unit_m,0,0};
    EnvironmentalModels models; models.srp={true,false};
    const EarthDynamics srp(EarthGravity(earth,false),panel({1,0,0}),models);
    const EarthDynamics gravity(EarthGravity(earth,false),panel({1,0,0}));
    const State base=advance(gravity,start,env);
    const State light=advance(srp,start,env);
    REQUIRE(light.r.x()<base.r.x()-.01);
    env.illumination=0;
    const State dark=advance(srp,start,env);
    REQUIRE(norm(dark.r-base.r)<1e-9);
    env.illumination=1;
    State back=start; back.q={0,0,0,1};
    const State back_srp=advance(srp,back,env),back_gravity=advance(gravity,back,env);
    REQUIRE(norm(back_srp.r-back_gravity.r)<1e-9);
    REQUIRE_THROWS_AS(srp({0},start),std::invalid_argument);
}
TEST_CASE("Atmospheric co-rotation and attitude affect orbital energy loss", "[environment_integration]") {
    const State start=orbit();
    EnvironmentSnapshot env; env.density_kg_m3=2.62e-12;
    EnvironmentalModels models; models.drag={true,false}; models.drag_model=DragModel::flat_plate;
    const EarthDynamics drag(EarthGravity(earth,false),panel({0,1,0}),models);
    const EarthDynamics gravity(EarthGravity(earth,false),panel({0,1,0}));
    const State base=advance(gravity,start,env,30),rotating=advance(drag,start,env,30);
    env.atmosphere_angular_velocity_rad_s={};
    const State still=advance(drag,start,env,30);
    const double loss=energy(base)-energy(rotating),still_loss=energy(base)-energy(still);
    REQUIRE(loss>100);
    REQUIRE(still_loss>loss*1.1);
    State back=start; back.q={0,0,0,1};
    const State hidden=advance(drag,back,env,30);
    REQUIRE(std::abs(energy(hidden)-energy(base))<1e-5);
    models.drag_model=DragModel::sentman;
    const EarthDynamics sentman(EarthGravity(earth,false),panel({0,1,0}),models);
    REQUIRE(energy(advance(sentman,start,env,30))<energy(base)-100);
}
TEST_CASE("Off-centre environmental force couples angular rate and attitude", "[environment_integration]") {
    State start=orbit();
    EnvironmentSnapshot env; env.sun_position_m=start.r+InertialVector{astronomical_unit_m,0,0};
    EnvironmentalModels models; models.srp={true,true};
    const EarthDynamics offset(EarthGravity(earth,false),panel({1,0,0},{0,1,0}),models);
    const EarthDynamics center(EarthGravity(earth,false),panel({1,0,0}),models);
    const State moved=advance(offset,start,env),balanced=advance(center,start,env);
    const double initial_acceleration=solar_pressure_1au_n_m2*1000/10;
    REQUIRE(offset({0},start,env).omega_dot.z()==Approx(initial_acceleration).epsilon(1e-12));
    // Attitude feedback reduces the absorbed force and torque: theta <= a0*T^2/2,
    // and a0*cos(theta_max)^2 <= theta_ddot <= a0 (Sun displacement is negligible here).
    const double upper_rate=initial_acceleration*10,max_angle=initial_acceleration*100/2;
    REQUIRE(moved.omega.z()<upper_rate);
    REQUIRE(moved.omega.z()>upper_rate*std::pow(std::cos(max_angle),2));
    REQUIRE(moved.q.z>1e-3);
    REQUIRE(norm(balanced.omega)==0);
    REQUIRE(balanced.q.z==0);
    REQUIRE(moved.r.x()>balanced.r.x()+1e-7);
}
TEST_CASE("Gravity-gradient propagation uses body torque and spherical-inertia equilibrium", "[environment_integration]") {
    State initial=orbit();
    const Spacecraft craft(10,Inertia((Eigen::Vector3d{2,3,4}).asDiagonal()));
    EnvironmentalModels models; models.gravity_gradient=true;
    const EarthDynamics gradient(EarthGravity(earth,false),craft,models);
    const EarthDynamics free(EarthGravity(earth,false),craft);
    initial.q={std::cos(std::numbers::pi/8),0,0,std::sin(std::numbers::pi/8)};
    const auto forced=advance(gradient,initial,{},10),unforced=advance(free,initial,{},10);
    REQUIRE(forced.omega.z()<-4e-6);
    REQUIRE(forced.q.z<unforced.q.z-1e-6);
    const Spacecraft sphere(10,Inertia(3*Eigen::Matrix3d::Identity()));
    const EarthDynamics spherical(EarthGravity(earth,false),sphere,models);
    const State equilibrium=advance(spherical,initial,{},10);
    REQUIRE(norm(equilibrium.omega)<1e-20);
    REQUIRE(std::abs(equilibrium.q.z-initial.q.z)<1e-14);
}
TEST_CASE("Force and body torque switches compose against independent analytical loads", "[environment_integration]") {
    const double r=7e6,speed=1000,rho=1e-12;
    const State s{{r,0,0},{speed,0,0},{1,0,0,0},{}};
    const Spacecraft craft(10,Inertia((Eigen::Vector3d{2,3,4}).asDiagonal()),
        {Surface(2,{1,0,0},{0,1,0}, {}, {2,.9}),Surface(3,{-1,0,0},{0,1,0})});
    EnvironmentSnapshot env; env.sun_position_m={r+astronomical_unit_m,0,0}; env.density_kg_m3=rho;
    env.atmosphere_angular_velocity_rad_s={};
    EnvironmentalModels all; all.srp={true,true}; all.drag={true,true}; all.drag_model=DragModel::flat_plate;
    all.albedo={true,true}; all.gravity_gradient=true;
    const double ratio=astronomical_unit_m/(astronomical_unit_m+r);
    const double albedo_pressure=solar_pressure_1au_n_m2*ratio*ratio*.3*std::pow(earth.equatorial_radius_m/r,2);
    const double fx=-2*solar_pressure_1au_n_m2-2*rho*speed*speed+3*albedo_pressure;
    const auto derivative=EarthDynamics(EarthGravity(earth,false),craft,all)({0},s,env);
    REQUIRE(derivative.v_dot.x()==Approx(-earth.mu_m3_s2/(r*r)+fx/10).margin(1e-14));
    REQUIRE(derivative.omega_dot.z()==Approx(-fx/4).margin(1e-18));
    all.srp.torque=false; all.drag.torque=false; all.albedo.torque=false;
    const auto force_only=EarthDynamics(EarthGravity(earth,false),craft,all)({0},s,env);
    REQUIRE(force_only.omega_dot.z()==0);
    all.srp={false,true}; all.drag={false,true}; all.albedo={false,true};
    const auto torque_only=EarthDynamics(EarthGravity(earth,false),craft,all)({0},s,env);
    REQUIRE(torque_only.v_dot.x()==Approx(-earth.mu_m3_s2/(r*r)).margin(1e-14));
    REQUIRE(torque_only.omega_dot.z()==Approx(-fx/4).margin(1e-18));
    EnvironmentalModels invalid; invalid.drag={true,false};
    REQUIRE_THROWS_AS(EarthDynamics(EarthGravity(earth,false),craft,invalid),std::invalid_argument);
}
