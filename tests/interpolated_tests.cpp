#include <catch2/catch_test_macros.hpp>
#include "leo/environment/interpolated.hpp"
#include "leo/environment/source.hpp"
#include <limits>
#include <numbers>
using namespace leo;
namespace {
constexpr double inf=std::numeric_limits<double>::infinity();
constexpr double nan_value=std::numeric_limits<double>::quiet_NaN();
struct CubicSun final:EphemerisProvider {
    EphemerisState geometric_state(int target,Epoch epoch) const override {
        if(target!=10||!std::isfinite(epoch.et_seconds)) throw std::invalid_argument("invalid fixture query");
        const double t=epoch.et_seconds-1e8;
        return {{1e11+10*t+2*t*t+.125*t*t*t,-2e10+3*t*t,-4e9+4*t},
                {10+4*t+.375*t*t,6*t,4}};
    }
};
struct OrbitSun final:EphemerisProvider {
    EphemerisState geometric_state(int,Epoch epoch) const override {
        const double w=2*std::numbers::pi/(365.25*86400),a=w*epoch.et_seconds;
        return {{astronomical_unit_m*std::cos(a),astronomical_unit_m*std::sin(a),1e9},
                {-astronomical_unit_m*w*std::sin(a),astronomical_unit_m*w*std::cos(a),0}};
    }
};
}
TEST_CASE("Hermite Sun cache reproduces cubic positions and derivative velocities", "[environment][cache]") {
    CubicSun direct;
    // Last segment is 2 s, not the nominal 3 s; all queries use absolute ET.
    InterpolatedEphemerisProvider cache(direct,{1e8},{1e8+11},3);
    for(double offset:{0.,1.25,3.,4.5,6.,8.125,9.,10.,11.}) {
        const auto expected=direct.geometric_state(10,{1e8+offset});
        const auto actual=cache.geometric_state(10,{1e8+offset});
        REQUIRE(norm(actual.position_m-expected.position_m)<1e-3);
        REQUIRE(norm(actual.velocity_m_s-expected.velocity_m_s)<1e-8);
    }
    for(double offset:{0.,3.,6.,9.,11.}) {
        const auto expected=direct.geometric_state(10,{1e8+offset});
        const auto actual=cache.geometric_state(10,{1e8+offset});
        REQUIRE(actual.position_m.as_eigen()==expected.position_m.as_eigen());
        REQUIRE(actual.velocity_m_s.as_eigen()==expected.velocity_m_s.as_eigen());
    }
}
TEST_CASE("Sun cache rejects invalid epochs targets and exact coverage neighbours", "[environment][cache]") {
    CubicSun source;
    InterpolatedEphemerisProvider cache(source,{1e8},{1e8+11},30);
    REQUIRE_THROWS_AS(cache.geometric_state(10,{std::nextafter(1e8,-inf)}),std::out_of_range);
    REQUIRE_THROWS_AS(cache.geometric_state(10,{std::nextafter(1e8+11,inf)}),std::out_of_range);
    for(double t:{nan_value,inf,-inf})
        REQUIRE_THROWS_AS(cache.geometric_state(10,{t}),std::invalid_argument);
    REQUIRE_THROWS_AS(cache.geometric_state(399,{1e8}),std::invalid_argument);
    for(double step:{0.,-1.,nan_value,inf})
        REQUIRE_THROWS_AS(InterpolatedEphemerisProvider(source,Epoch{0},Epoch{1},step),std::invalid_argument);
    REQUIRE_THROWS_AS(InterpolatedEphemerisProvider(source,Epoch{1},Epoch{1},1),std::invalid_argument);
    REQUIRE_THROWS_AS(InterpolatedEphemerisProvider(source,Epoch{2},Epoch{1},1),std::invalid_argument);
    REQUIRE_THROWS_AS(InterpolatedEphemerisProvider(source,Epoch{nan_value},Epoch{1},1),std::invalid_argument);
    REQUIRE_THROWS_AS(InterpolatedEphemerisProvider(source,Epoch{0},Epoch{inf},1),std::invalid_argument);
    REQUIRE_THROWS_AS(InterpolatedEphemerisProvider(source,Epoch{1e20},Epoch{1e20+1e6},1),std::invalid_argument);
    REQUIRE_THROWS_AS(InterpolatedEphemerisProvider(source,Epoch{0},Epoch{1},1e-300),std::length_error);
}
TEST_CASE("Sun cache construction propagates coverage failures and rejects invalid source samples", "[environment][cache]") {
    struct BadSun:EphemerisProvider {
        bool fail;
        explicit BadSun(bool f):fail(f) {}
        EphemerisState geometric_state(int,Epoch e) const override {
            if(fail&&e.et_seconds>0) throw std::out_of_range("source coverage");
            return {{1e11,0,0},{0,fail?0:nan_value,0}};
        }
    };
    REQUIRE_THROWS_AS(InterpolatedEphemerisProvider(BadSun{true},Epoch{0},Epoch{1},.5),std::out_of_range);
    REQUIRE_THROWS_AS(InterpolatedEphemerisProvider(BadSun{false},Epoch{0},Epoch{1},.5),std::runtime_error);
}
TEST_CASE("Sun cache owns samples and supports deterministic backward queries over a month", "[environment][cache]") {
    auto cache=[] {OrbitSun temporary;return InterpolatedEphemerisProvider(temporary,Epoch{0},Epoch{30*86400},900);}();
    OrbitSun direct;
    for(double t:{30.*86400,15.*86400+123,0.,86400.25,400.5,12345.75}) {
        const auto expected=direct.geometric_state(10,{t}),a=cache.geometric_state(10,{t}),b=cache.geometric_state(10,{t});
        REQUIRE(norm(a.position_m-expected.position_m)<1);
        REQUIRE(norm(a.velocity_m_s-expected.velocity_m_s)<1e-3);
        REQUIRE(a.position_m.as_eigen()==b.position_m.as_eigen());
        REQUIRE(a.velocity_m_s.as_eigen()==b.velocity_m_s.as_eigen());
    }
}
TEST_CASE("Cached Sun acquisition still computes eclipse from the current spacecraft", "[environment][cache]") {
    OrbitSun direct;
    InterpolatedEphemerisProvider cache(direct,{0},{3600},300);
    EarthEnvironment source(Earth{},Epoch{0},&cache);
    State day{{7e6,0,0},{},{1,0,0,0},{}},night=day;
    night.r=-day.r;
    REQUIRE(source({123},day).illumination==1);
    REQUIRE(source({123},night).illumination==0);
    REQUIRE(source({123},day).sun_position_m.as_eigen()==source({123},night).sun_position_m.as_eigen());
}
