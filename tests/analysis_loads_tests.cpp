#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "leo/analysis/loads.hpp"
#include "leo/environment/source.hpp"
#include "leo/integration/propagate.hpp"
#include <array>
#include <numbers>
using namespace leo;
using namespace leo::analysis;
using Catch::Approx;
namespace {
State orbit_state() {return {{7e6,0,0},{0,7500,0},{1,0,0,0},{.01,.02,.03}};}
Spacecraft box() {
    return {10,Inertia((Eigen::Vector3d{2,3,4}).asDiagonal()),
        {Surface(2,{1,0,0},{0,1,0}),Surface(3,{-1,0,0},{0,1,0}),Surface(4,{0,1,0},{1,0,0})}};
}
std::vector<EnvironmentalSample> force_data(const Trajectory& t,InertialVector force) {
    std::vector<EnvironmentalSample> result;
    for(const auto& s:t.samples) {
        EnvironmentSnapshot snapshot;snapshot.epoch={t.epoch.et_seconds+s.t.seconds};snapshot.sun_position_m={astronomical_unit_m,0,0};
        DynamicsContributions loads;loads.srp.force_n=force;
        result.push_back({s.t.seconds,snapshot,loads});
    }
    return result;
}
}
TEST_CASE("Selected load diagnostics reconstruct RHS for both drag laws", "[analysis][diagnostics]") {
    const Earth earth;const auto craft=box();State state=orbit_state();
    state.q={std::cos(.3),0,0,std::sin(.3)};
    EnvironmentSnapshot env;env.sun_position_m={astronomical_unit_m,0,0};env.density_kg_m3=1e-12;
    for(const auto model:{DragModel::flat_plate,DragModel::sentman}) for(int switches=0;switches<8;++switches) {
        EnvironmentalModels models;models.drag_model=model;
        models.srp={bool(switches&1),bool(switches&2)};models.drag=models.srp;models.albedo=models.srp;
        models.gravity_gradient=bool(switches&4);
        const EarthDynamics dynamics(EarthGravity(earth,true),craft,models);
        const auto d=dynamics.evaluate({0},state,env);const auto rhs=dynamics({0},state,env);
        const auto force=d.srp.force_n+d.drag.force_n+d.albedo.force_n;
        const auto torque=d.srp.torque_nm+d.drag.torque_nm+d.albedo.torque_nm+d.gravity_gradient_nm;
        REQUIRE(norm(force-d.non_gravitational.force_n)<1e-18);
        REQUIRE(norm(torque-d.non_gravitational.torque_nm)<1e-18);
        REQUIRE(norm(d.point_mass_m_s2+d.j2_m_s2+force/craft.mass_kg()-rhs.v_dot)<1e-14);
        REQUIRE(norm(craft.inertia().multiply(rhs.omega_dot)+cross(state.omega,craft.inertia().multiply(state.omega))-torque)<1e-17);
        if(!(switches&1)) {REQUIRE(norm(force)==0);REQUIRE(norm(d.srp.force_n)==0);REQUIRE(norm(d.drag.force_n)==0);REQUIRE(norm(d.albedo.force_n)==0);}
        if(!(switches&2)) {REQUIRE(norm(d.srp.torque_nm)==0);REQUIRE(norm(d.drag.torque_nm)==0);REQUIRE(norm(d.albedo.torque_nm)==0);}
        if(!(switches&4)) REQUIRE(norm(d.gravity_gradient_nm)==0);
    }
    const EarthDynamics disabled(EarthGravity(earth,false),craft);
    const auto d=disabled.evaluate({0},state,{});
    REQUIRE(norm(d.j2_m_s2)==0);REQUIRE(norm(d.non_gravitational.force_n)==0);REQUIRE(norm(d.non_gravitational.torque_nm)==0);
    REQUIRE(d.point_mass_m_s2.x()==Approx(-earth.mu_m3_s2/49e12).epsilon(1e-14));
}
TEST_CASE("Surface illumination area and off-centre torque share body to inertial convention", "[analysis][attitude]") {
    const Spacecraft craft(10,Inertia(2*Eigen::Matrix3d::Identity()),{Surface(2,{1,0,0},{0,1,0})});
    State state=orbit_state();state.omega={0,0,.1};
    state.q={std::sqrt(.5),0,0,std::sqrt(.5)};
    Trajectory trajectory{{50},{{{0},state}}};
    EnvironmentSnapshot env;env.epoch={50};env.sun_position_m=state.r+InertialVector{0,astronomical_unit_m,0};env.illumination=.25;env.atmosphere_angular_velocity_rad_s={};
    EnvironmentalModels models;models.srp={true,true};
    const auto history=environmental_history(trajectory,EarthDynamics(EarthGravity(Earth{},false),craft,models),std::array{env});
    const auto attitude=attitude_history(trajectory,craft,history);
    const auto& surface=attitude[0].surfaces[0];
    REQUIRE(norm(surface.normal_j2000-InertialVector{0,1,0})<1e-15);
    REQUIRE(surface.sun_incidence_rad==Approx(0).margin(3e-8));
    REQUIRE(surface.projected_illuminated_area_m2==Approx(.5).margin(1e-15));
    REQUIRE(surface.projected_aerodynamic_area_m2==Approx(2).margin(1e-15));
    REQUIRE(attitude[0].rotational_energy_j==Approx(.01));REQUIRE(attitude[0].inertial_momentum_kg_m2_s==Approx(.2));
    const double magnitude=solar_pressure_1au_n_m2*2*.25;
    REQUIRE(history[0].contributions.srp.force_n.y()==Approx(-magnitude).epsilon(1e-14));
    REQUIRE(history[0].contributions.srp.torque_nm.z()==Approx(magnitude).epsilon(1e-14));
    REQUIRE(norm(body_to_inertial(state.q,inertial_to_body(state.q,history[0].contributions.srp.force_n))-history[0].contributions.srp.force_n)<1e-18);
    const auto missing=attitude_history(trajectory,craft);
    REQUIRE(std::isnan(missing[0].surfaces[0].sun_incidence_rad));
    REQUIRE(std::isnan(missing[0].surfaces[0].projected_aerodynamic_area_m2));
    env.epoch={0};REQUIRE_THROWS_AS(environmental_history(trajectory,EarthDynamics(EarthGravity(Earth{},false),craft),std::array{env}),std::invalid_argument);
}
TEST_CASE("Attitude energy momentum and relative Euler lock follow physical rotations", "[analysis][attitude]") {
    const auto craft=box();const auto state=orbit_state();
    const std::array times{0.,20.,50.,100.,200.};
    IntegrationOptions options;options.initial_step_s=.1;options.max_step_s=.5;options.rtol=1e-11;
    const auto trajectory=propagate(EarthDynamics(EarthGravity(Earth{},false),craft),state,{0},times,options);
    const auto history=attitude_history(trajectory,craft);
    for(const auto& row:history) {
        REQUIRE(row.rotational_energy_j==Approx(history[0].rotational_energy_j).epsilon(2e-9));
        REQUIRE(row.inertial_momentum_kg_m2_s==Approx(history[0].inertial_momentum_kg_m2_s).epsilon(2e-9));
    }
    Trajectory rotations{{0},{{{0},state},{{1},state}}};
    rotations.samples[1].state.q={std::sqrt(.5),0,std::sqrt(.5),0};
    const auto lock=attitude_history(rotations,craft);
    REQUIRE(lock.back().euler_singular);REQUIRE(lock.back().relative_zyx_euler_rad.y()==Approx(std::numbers::pi/2));
    REQUIRE(lock.back().relative_zyx_euler_rad.z()==0);
    const double w=std::cos(.3)/std::sqrt(2.),z=std::sin(.3)/std::sqrt(2.);
    rotations.samples[1].state.q={w,-z,w,z};
    const auto positive=attitude_history(rotations,craft).back();
    rotations.samples[1].state.q={-w,z,-w,-z};
    const auto negative=attitude_history(rotations,craft).back();
    REQUIRE(positive.relative_zyx_euler_rad.x()==Approx(.6));
    REQUIRE(negative.relative_zyx_euler_rad.x()==Approx(.6));
}
TEST_CASE("SRP work integrates constant force using actual irregular timestamps", "[analysis][work]") {
    Trajectory trajectory{{0},{}};
    for(double t:{0.,1.,3.,4.}) trajectory.samples.push_back({{t},{{7e6,10*t+.5*t*t,0},{0,10+t,0},{1,0,0,0},{}}});
    const auto data=force_data(trajectory,{0,3,0});const auto work=srp_work(trajectory,Earth{},2,data);
    for(const auto& row:work) {
        REQUIRE(row.power_w==Approx(30+3*row.time_s));
        REQUIRE(row.cumulative_work_j==Approx(30*row.time_s+1.5*row.time_s*row.time_s));
        REQUIRE(row.specific_work_j_kg==Approx(row.cumulative_work_j/2));
        REQUIRE(row.srp_acceleration_m_s2.y()==Approx(1.5));
        REQUIRE(row.power_rtn_w.x()+row.power_rtn_w.y()+row.power_rtn_w.z()==Approx(row.power_w).epsilon(1e-14));
        REQUIRE(row.cumulative_work_rtn_j.x()+row.cumulative_work_rtn_j.y()+row.cumulative_work_rtn_j.z()==Approx(row.cumulative_work_j).margin(1e-12));
    }
    const std::array<OrbitSummary,2> orbits{{{0,.5,2,true,1.5,{},{},{},0,0},{1,2,3.5,false,0,{},{},{},0,0}}};
    const auto intervals=work_per_orbit(orbits,work);
    const auto analytic=[](double t){return 30*t+1.5*t*t;};
    REQUIRE(intervals[0].work_j==Approx(analytic(2)-analytic(.5)));
    REQUIRE(intervals[1].specific_work_j_kg==Approx((analytic(3.5)-analytic(2))/2));
    REQUIRE(intervals[0].work_j+intervals[1].work_j==Approx(analytic(3.5)-analytic(.5)));
    const auto zero=srp_work(trajectory,Earth{},2,force_data(trajectory,{}));
    for(const auto& row:zero) {REQUIRE(row.power_w==0);REQUIRE(row.cumulative_work_j==0);}
}
TEST_CASE("Fractional work remains correct when endpoint work cancels", "[analysis][work]") {
    Trajectory t{{0},{{{0},orbit_state()},{{2},orbit_state()}}};
    auto data=force_data(t,{0,1,0});data[1].contributions.srp.force_n={0,-1,0};
    const auto work=srp_work(t,Earth{},10,data);
    REQUIRE(work.back().cumulative_work_j==0);
    const std::array<OrbitSummary,1> orbits{{{0,0,1,false,0,{},{},{},0,0}}};
    const auto result=work_per_orbit(orbits,work);
    REQUIRE(result[0].work_j==Approx(3750));REQUIRE(result[0].specific_work_j_kg==Approx(375));
}
TEST_CASE("Umbra gives zero solar power and RTN rejects degenerate orbits", "[analysis][work]") {
    Trajectory trajectory{{0},{{{0},orbit_state()},{{1},orbit_state()}}};
    EnvironmentalModels models;models.srp={true,true};
    const EarthDynamics dynamics(EarthGravity(Earth{},false),box(),models);
    std::array<EnvironmentSnapshot,2> snapshots;
    for(std::size_t i=0;i<2;++i) {snapshots[i].epoch={double(i)};snapshots[i].sun_position_m={astronomical_unit_m,0,0};snapshots[i].illumination=0;}
    const auto result=srp_work(trajectory,Earth{},10,environmental_history(trajectory,dynamics,snapshots));
    REQUIRE(result.back().power_w==0);REQUIRE(result.back().cumulative_work_j==0);
    const auto basis=rtn_basis(orbit_state());
    REQUIRE(norm(cross(basis.radial,basis.transverse)-basis.normal)<1e-15);
    REQUIRE(norm(rtn_components(basis,{1,2,3})-RtnVector{1,2,3})<8e-15);
    auto bad=orbit_state();bad.v=bad.r;REQUIRE_THROWS_AS(rtn_basis(bad),std::invalid_argument);
    bad.r={};REQUIRE_THROWS_AS(rtn_basis(bad),std::invalid_argument);
}
TEST_CASE("Integrated constant applied force work accounts for two body energy change", "[analysis][work]") {
    const Earth earth;const double mass=10;const InertialVector force{0,.01,0};
    const auto rhs=[&](ElapsedTime,const State& s) {
        return StateDerivative{s.v,point_mass_acceleration(earth,s.r)+force/mass,quaternion_derivative(s.q,s.omega),{}};
    };
    std::vector<double> times;for(int i=0;i<=1000;++i) times.push_back(i*.1);
    const auto trajectory=propagate(rhs,orbit_state(),{0},times);
    const auto result=srp_work(trajectory,earth,mass,force_data(trajectory,force));
    const double displacement_work=dot(force,trajectory.samples.back().state.r-trajectory.samples.front().state.r);
    REQUIRE(result.back().cumulative_work_j==Approx(displacement_work).margin(.001));
    REQUIRE(result.back().orbital_energy_change_j==Approx(displacement_work).margin(.01));
    REQUIRE(std::abs(result.back().energy_minus_srp_work_j)<.01);
}
