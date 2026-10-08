#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "leo/analysis/orbital.hpp"
#include "leo/integration/propagate.hpp"
#include "leo/kernel/dynamics.hpp"
#include "support/analytical_orbits.hpp"
#include <array>
#include <limits>
using namespace leo;
using namespace leo::analysis;
using Catch::Approx;
namespace {
Trajectory exact(const leo_test::EllipticOrbit& orbit,double start,double end,int count) {
    Trajectory result{{123.456},{}};
    const double period=leo_test::period_s(orbit);
    for(int i=0;i<=count;++i) {
        const double time=(start+(end-start)*i/count)*period;
        result.samples.push_back({{time},leo_test::identity_attitude_state(leo_test::kepler_state_at(orbit,time))});
    }
    return result;
}
}
TEST_CASE("Analysis preserves analytic circular and elliptic invariants", "[analysis][orbital]") {
    const Earth earth;
    for(double e:{0.,.15}) {
        const leo_test::EllipticOrbit orbit{8e6,e,.7,.4,.3};
        const auto trajectory=exact(orbit,0,3,1200);
        const auto history=orbital_history(trajectory,earth);
        const double angular_momentum=std::sqrt(earth.mu_m3_s2*orbit.a_m*(1-e*e));
        for(const auto& row:history) {
            REQUIRE(row.elements.a_m==Approx(orbit.a_m).epsilon(2e-14));
            REQUIRE(row.elements.e==Approx(e).margin(2e-15));
            REQUIRE(row.elements.i_rad==Approx(.7).margin(2e-15));
            REQUIRE(row.elements.raan_rad==Approx(.4).margin(2e-15));
            REQUIRE(row.specific_energy_j_kg==Approx(-earth.mu_m3_s2/(2*orbit.a_m)).epsilon(2e-14));
            REQUIRE(norm(row.specific_angular_momentum_m2_s)==Approx(angular_momentum).epsilon(2e-14));
            REQUIRE(row.periapsis_radius_m==Approx(orbit.a_m*(1-e)).epsilon(2e-14));
            REQUIRE(row.apoapsis_altitude_m==Approx(orbit.a_m*(1+e)-earth.equatorial_radius_m).epsilon(2e-14));
            REQUIRE(row.altitude_m==Approx(row.radius_m-earth.equatorial_radius_m));
            if(e==0) REQUIRE(row.elements.arg_periapsis_rad==0);
            else REQUIRE(row.elements.arg_periapsis_rad==Approx(.3).margin(2e-14));
        }
        REQUIRE(history.front().specific_kinetic_energy_j_kg==Approx(earth.mu_m3_s2/(2*orbit.a_m)*(1+e)/(1-e)).epsilon(2e-14));
        REQUIRE(history.front().specific_potential_energy_j_kg==Approx(-earth.mu_m3_s2/(orbit.a_m*(1-e))).epsilon(2e-14));
        REQUIRE(history.back().true_anomaly_unwrapped_rad-history.front().true_anomaly_unwrapped_rad==Approx(6*std::numbers::pi).margin(1e-11));
    }
    const auto equatorial=orbital_history(exact({8e6,0,0,0,0},0,1,200),earth);
    for(const auto& row:equatorial) {REQUIRE(row.elements.raan_rad==0);REQUIRE(row.elements.arg_periapsis_rad==0);}
}
TEST_CASE("Analysis unwraps both angular directions and rejects malformed trajectories", "[analysis][orbital]") {
    const std::array angles{6.2,.1,.2,6.1};
    const auto values=unwrap_angles(angles);
    REQUIRE(values[1]==Approx(.1+2*std::numbers::pi));
    REQUIRE(values[3]==Approx(6.1));
    const std::array invalid{std::numeric_limits<double>::infinity()};
    REQUIRE_THROWS_AS(unwrap_angles(invalid),std::invalid_argument);
    auto trajectory=exact({8e6,.1,.7,.4,.3},0,1,100);
    trajectory.samples[1].t=trajectory.samples[0].t;
    REQUIRE_THROWS_AS(orbital_history(trajectory,Earth{}),std::invalid_argument);
    REQUIRE_THROWS_AS(validate_trajectory(Trajectory{{0},{}}),std::invalid_argument);
    trajectory.samples[1].t={1};trajectory.samples[0].state.q={0,0,0,0};
    REQUIRE_THROWS_AS(validate_trajectory(trajectory),std::invalid_argument);
}
TEST_CASE("Apsis events and segments recover periods and partial boundary orbits", "[analysis][events]") {
    const leo_test::EllipticOrbit orbit{8e6,.15,.7,.4,.3};
    const double period=leo_test::period_s(orbit);
    const auto trajectory=exact(orbit,.12,2.35,800);
    const auto events=detect_apsides(trajectory);
    REQUIRE(events.size()==4);
    for(std::size_t i=0;i<events.size();++i) {
        REQUIRE(events[i].time_s==Approx((i+1)*period/2).margin(.03));
        REQUIRE(events[i].kind==(i%2==0?ApsisKind::apoapsis:ApsisKind::periapsis));
        REQUIRE(events[i].fraction>=0);REQUIRE(events[i].fraction<=1);
    }
    const auto orbits=per_orbit(trajectory,Earth{});
    REQUIRE(orbits.size()==3);REQUIRE_FALSE(orbits.front().complete);REQUIRE(orbits[1].complete);REQUIRE_FALSE(orbits.back().complete);
    REQUIRE(orbits[1].period_s==Approx(period).margin(.06));
    REQUIRE(orbits.front().period_s==0);REQUIRE(orbits.back().end_time_s==trajectory.samples.back().t.seconds);
    for(const auto& row:orbits) {REQUIRE(std::abs(row.delta_elements.a_m)<1e-7);REQUIRE(std::abs(row.delta_elements.raan_rad)<1e-13);}
    const auto full=per_orbit(exact(orbit,0,2,1000),Earth{});
    REQUIRE(full.size()==2);REQUIRE(full[0].complete);REQUIRE(full[1].complete);
}
TEST_CASE("Linear radial velocity crossing has an explicit fractional event", "[analysis][events]") {
    Trajectory t{{0},{{{1},{{8e6,0,0},{-2,1000,0},{1,0,0,0},{}}},{{4},{{7e6,0,0},{4,1000,0},{1,0,0,0},{}}}}};
    const auto events=detect_apsides(t);
    REQUIRE(events.size()==1);REQUIRE(events[0].kind==ApsisKind::periapsis);
    REQUIRE(events[0].fraction==Approx(1./3));REQUIRE(events[0].time_s==Approx(2));
    REQUIRE(events[0].radius_m==Approx(8e6-1e6/3));
}
TEST_CASE("Circular orbits count phase turns without fictional apsides", "[analysis][events]") {
    const leo_test::EllipticOrbit orbit{8e6,0,.7,.4,0};
    const auto trajectory=exact(orbit,.2,2.2,800);
    REQUIRE(detect_apsides(trajectory).empty());
    const auto orbits=per_orbit(trajectory,Earth{});
    REQUIRE(orbits.size()==3);REQUIRE_FALSE(orbits.front().complete);REQUIRE(orbits[1].complete);REQUIRE_FALSE(orbits.back().complete);
    REQUIRE(orbits[1].period_s==Approx(leo_test::period_s(orbit)).margin(1e-9));
    REQUIRE_THROWS_AS(per_orbit(exact(orbit,0,2,2),Earth{}),std::invalid_argument);
}
TEST_CASE("Analysis on propagation preserves two body elements and J2 nodal trends", "[analysis][orbital]") {
    const Earth earth;const leo_test::EllipticOrbit orbit{8e6,.03,.8,.4,.3};
    const auto initial=leo_test::identity_attitude_state(leo_test::kepler_state_at(orbit,0));
    const double period=leo_test::period_s(orbit);
    std::vector<double> times;for(int i=0;i<=1600;++i) times.push_back(i*20*period/1600);
    const Spacecraft craft(10,Inertia(Eigen::Matrix3d::Identity()));
    IntegrationOptions options;options.rtol=1e-11;options.max_step_s=20;
    const auto two_body=orbital_history(propagate(EarthDynamics(EarthGravity(earth,false),craft),initial,{0},times,options),earth);
    for(const auto& row:two_body) {REQUIRE(row.elements.a_m==Approx(8e6).epsilon(2e-9));REQUIRE(row.elements.e==Approx(.03).margin(2e-9));}
    const auto j2=orbital_history(propagate(EarthDynamics(EarthGravity(earth,true),craft),initial,{0},times,options),earth);
    const double expected=-1.5*earth.j2*std::sqrt(earth.mu_m3_s2/std::pow(orbit.a_m,3))*std::pow(earth.equatorial_radius_m/(orbit.a_m*(1-orbit.e*orbit.e)),2)*std::cos(orbit.i_rad);
    const double observed=(j2.back().raan_unwrapped_rad-j2.front().raan_unwrapped_rad)/times.back();
    REQUIRE(observed==Approx(expected).epsilon(.02));
}
