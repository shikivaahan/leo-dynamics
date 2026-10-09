#pragma once
#include "bench_support.hpp"
#include "leo/kernel/orbital.hpp"
#include <cmath>
#include <fstream>
namespace experiment {
using namespace leo;
struct Case {
    std::string id="CASE-C",file;
    unsigned version=1;
    bool legacy=true;
    double mass=10,density_scale=1;
    Eigen::Vector3d diagonal{2,3,4};
    ClassicalElements orbit{};
    double pitch_deg=45;
    BodyVector omega{},pointing_axis{0,0,1};
    std::vector<Surface> surfaces;
    State start()const {
        if(legacy) return bench::initial();
        const auto cart=coe_to_cartesian(orbit,Earth{}.mu_m3_s2);
        const auto h=cross(cart.r,cart.v)/norm(cross(cart.r,cart.v)),r=cart.r/norm(cart.r);
        const auto t=cross(h,r)/norm(cross(h,r));const double p=pitch_deg*std::numbers::pi/180;
        const auto z=r*std::cos(p)+t*std::sin(p),y=cross(z,h)/norm(cross(z,h)),x=cross(y,z)/norm(cross(y,z));
        Eigen::Matrix3d matrix;matrix.col(0)=x.as_eigen();matrix.col(1)=y.as_eigen();matrix.col(2)=z.as_eigen();
        const Eigen::Quaterniond q(matrix);
        return {cart.r,cart.v,normalize_quaternion({q.w(),q.x(),q.y(),q.z()}),omega};
    }
    Spacecraft craft()const {
        if(legacy) return bench::dynamics(true).spacecraft();
        return Spacecraft(mass,Inertia(diagonal.asDiagonal()),surfaces);
    }
};
inline Case load(const std::string& path) {
    Case c;if(path.empty()) return c;
    c.file=path;c.legacy=false;std::ifstream in(path);if(!in) throw std::invalid_argument("case file cannot be read");
    std::string line;bool has_mass=false,has_inertia=false,has_orbit=false,has_surfaces=false;
    while(std::getline(in,line)) {
        line=line.substr(0,line.find('#'));std::istringstream row(line);std::string key;if(!(row>>key)) continue;
        if(key=="id") row>>c.id;
        else if(key=="version") row>>c.version;
        else if(key=="legacy") row>>c.legacy;
        else if(key=="mass_kg") {row>>c.mass;has_mass=true;}
        else if(key=="inertia_kg_m2") {row>>c.diagonal.x()>>c.diagonal.y()>>c.diagonal.z();has_inertia=true;}
        else if(key=="orbit_m_rad") {row>>c.orbit.a_m>>c.orbit.e>>c.orbit.i_rad>>c.orbit.raan_rad>>c.orbit.arg_periapsis_rad>>c.orbit.true_anomaly_rad;has_orbit=true;}
        else if(key=="attitude_in_plane_deg") row>>c.pitch_deg;
        else if(key=="omega_body_rad_s"||key=="pointing_axis_body") {
            double x,y,z;row>>x>>y>>z;if(key=="omega_body_rad_s") c.omega={x,y,z};else c.pointing_axis={x,y,z};
        }
        else if(key=="density_scale") row>>c.density_scale;
        else if(key=="surface") {
            double a,nx,ny,nz,x,y,z,s,d,cd,sigma;row>>a>>nx>>ny>>nz>>x>>y>>z>>s>>d>>cd>>sigma;
            if(!row) throw std::invalid_argument("invalid surface row");
            c.surfaces.emplace_back(a,BodyVector{nx,ny,nz},BodyVector{x,y,z},OpticalProperties{s,d},AerodynamicProperties{cd,sigma});has_surfaces=true;
        } else throw std::invalid_argument("unknown case field: "+key);
        if(!row) throw std::invalid_argument("incomplete case field: "+key);
    }
    if(c.version!=1||norm(c.pointing_axis)==0||!std::isfinite(c.density_scale)||c.density_scale<0) throw std::invalid_argument("invalid case metadata");
    if(!std::isfinite(c.mass)||c.mass<=0||!c.diagonal.allFinite()||c.diagonal.minCoeff()<=0)
        throw std::invalid_argument("case mass and principal moments must be finite and positive");
    // The audit reports per-surface incidence masks and projected areas, both of
    // which assume a unit normal and a positive area; a malformed row would
    // silently corrupt every one of those diagnostics instead of failing here.
    for(const auto& s:c.surfaces) {
        if(!std::isfinite(s.area_m2())||s.area_m2()<=0) throw std::invalid_argument("surface area must be finite and positive");
        if(std::abs(norm(s.normal_body())-1)>1e-9) throw std::invalid_argument("surface normal must be a unit vector");
        if(!s.center_of_pressure_body().as_eigen().allFinite()) throw std::invalid_argument("surface centre of pressure must be finite");
        const double specular=s.optical().specular(),diffuse=s.optical().diffuse();
        if(!std::isfinite(specular)||!std::isfinite(diffuse)||specular<0||diffuse<0||specular+diffuse>1)
            throw std::invalid_argument("surface optical fractions must lie in [0,1] and sum to at most 1");
        if(!std::isfinite(s.aerodynamic().drag_coefficient())||s.aerodynamic().drag_coefficient()<0)
            throw std::invalid_argument("surface drag coefficient must be finite and nonnegative");
        if(!std::isfinite(s.aerodynamic().accommodation())||s.aerodynamic().accommodation()<0||s.aerodynamic().accommodation()>1)
            throw std::invalid_argument("surface accommodation must lie in [0,1]");
    }
    if(!c.legacy&&!(has_mass&&has_inertia&&has_orbit&&has_surfaces)) throw std::invalid_argument("incomplete physical case");
    return c;
}
inline Eigen::Matrix3d tensor(const Spacecraft& craft) {
    Eigen::Matrix3d m;
    m.col(0)=craft.inertia().multiply({1,0,0}).as_eigen();m.col(1)=craft.inertia().multiply({0,1,0}).as_eigen();m.col(2)=craft.inertia().multiply({0,0,1}).as_eigen();
    return m;
}
}
