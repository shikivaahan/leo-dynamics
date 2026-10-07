#pragma once
#include <Eigen/Core>
#include <Eigen/Geometry>
namespace leo {
struct J2000 {};
struct Body {};
template<class Frame> class Vector3 {
    Eigen::Vector3d value_ = Eigen::Vector3d::Zero();
public:
    Vector3() = default;
    Vector3(double x,double y,double z): value_(x,y,z) {}
    explicit Vector3(const Eigen::Vector3d& value): value_(value) {}
    double x() const noexcept { return value_.x(); }
    double y() const noexcept { return value_.y(); }
    double z() const noexcept { return value_.z(); }
    const Eigen::Vector3d& as_eigen() const noexcept { return value_; }
    friend Vector3 operator+(Vector3 a,Vector3 b) { return Vector3(a.value_+b.value_); }
    friend Vector3 operator-(Vector3 a,Vector3 b) { return Vector3(a.value_-b.value_); }
    friend Vector3 operator-(Vector3 a) { return Vector3(-a.value_); }
    friend Vector3 operator*(Vector3 a,double s) { return Vector3(a.value_*s); }
    friend Vector3 operator*(double s,Vector3 a) { return a*s; }
    friend Vector3 operator/(Vector3 a,double s) { return Vector3(a.value_/s); }
};
using InertialVector = Vector3<J2000>;
using BodyVector = Vector3<Body>;
template<class F> double dot(Vector3<F> a,Vector3<F> b) { return a.as_eigen().dot(b.as_eigen()); }
template<class F> Vector3<F> cross(Vector3<F> a,Vector3<F> b) { return Vector3<F>(a.as_eigen().cross(b.as_eigen())); }
template<class F> double norm(Vector3<F> a) { return a.as_eigen().stableNorm(); }
}
