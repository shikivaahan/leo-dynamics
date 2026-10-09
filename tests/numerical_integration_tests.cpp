#include <catch2/catch_test_macros.hpp>
#include "leo/integration/propagate.hpp"
#include <array>
#include <cmath>
using namespace leo;
namespace {
Quaternion exact_spin(Quaternion q,BodyVector omega,double t) {
    const double w=norm(omega),a=.5*w*t,s=std::sin(a)/w;
    const Quaternion d{std::cos(a),s*omega.x(),s*omega.y(),s*omega.z()};
    return {q.w*d.w-q.x*d.x-q.y*d.y-q.z*d.z,
            q.w*d.x+q.x*d.w+q.y*d.z-q.z*d.y,
            q.w*d.y-q.x*d.z+q.y*d.w+q.z*d.x,
            q.w*d.z+q.x*d.y-q.y*d.x+q.z*d.w};
}
double spin_error(Quaternion a,Quaternion b) {
    if(a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z<0) b={-b.w,-b.x,-b.y,-b.z};
    return std::sqrt(std::pow(a.w-b.w,2)+std::pow(a.x-b.x,2)+std::pow(a.y-b.y,2)+std::pow(a.z-b.z,2));
}
}
TEST_CASE("Rejected large adaptive trials preserve the exponential IVP", "[integration][numerical]") {
    State start{{1e6,0,0},{1,0,0},{1,0,0,0},{}};
    double previous_time=0;bool saw_backtrack=false;
    const auto rhs=[&](ElapsedTime t,const State& s) {
        saw_backtrack=saw_backtrack||t.seconds<previous_time;previous_time=t.seconds;
        return StateDerivative{s.v,s.v*(-.2),{0,0,0,0},{}};
    };
    IntegrationOptions o;o.rtol=1e-12;o.position_atol_m=1e-12;o.velocity_atol_m_s=1e-12;
    o.initial_step_s=20;o.max_step_s=20;
    const std::array times{0.,40.};
    const auto result=propagate(rhs,start,Epoch{0},times,o);
    REQUIRE(saw_backtrack);
    const auto& state=result.samples.back().state;
    const double decay=std::exp(-.2*40);
    REQUIRE(std::abs(state.v.x()-decay)<2e-11);
    REQUIRE(std::abs(state.r.x()-(1e6+(1-decay)/.2))<1e-7);
}
TEST_CASE("Projected adaptive attitude follows an independent arbitrary-axis spin solution", "[integration][numerical]") {
    const BodyVector omega{.0015,.0025,.0005};
    const Quaternion q=normalize_quaternion({.9238795325112867,.3826834323650898,0,0});
    State start{{},{},q,omega};
    const auto rhs=[&](ElapsedTime,const State& s) {
        return StateDerivative{{},{},quaternion_derivative(s.q,omega),{}};
    };
    IntegrationOptions o;o.rtol=1e-13;o.quaternion_atol=1e-15;o.omega_atol_rad_s=1e-15;o.max_step_s=60;
    const std::array times{0.,3600.,10800.,21600.};
    const auto result=propagate(rhs,start,Epoch{0},times,o);
    for(const auto& sample:result.samples) {
        REQUIRE(spin_error(sample.state.q,exact_spin(q,omega,sample.t.seconds))<2e-11);
        const auto& p=sample.state.q;
        REQUIRE(std::abs(std::sqrt(p.w*p.w+p.x*p.x+p.y*p.y+p.z*p.z)-1)<1e-14);
    }
}
