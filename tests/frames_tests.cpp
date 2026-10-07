#include <catch2/catch_test_macros.hpp>
#include <Eigen/Geometry>
#include "leo/kernel/frames.hpp"
#include <type_traits>
using namespace leo;
static_assert(!std::is_convertible_v<BodyVector,InertialVector>);
static_assert(!std::is_convertible_v<Eigen::Vector3d,InertialVector>);
template<class A,class B> concept Addable=requires(A a,B b){ a+b; };
static_assert(!Addable<BodyVector,InertialVector>);
TEST_CASE("Frame vector algebra", "[frames]") {
    REQUIRE(norm(InertialVector{})==0);
    const InertialVector a{1,0,0},b{0,1,0};
    REQUIRE(norm(cross(a,b)-InertialVector{0,0,1})==0);
    REQUIRE(dot(a,b)==0);
    REQUIRE(norm((a+b)*2/2-a-b)==0);
}
