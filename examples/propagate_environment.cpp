#include "leo/environment/source.hpp"
#include "leo/environment/spice.hpp"
#include "leo/integration/propagate.hpp"
#include <array>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc,char** argv) {
    try {
        if (argc!=4) throw std::invalid_argument("usage: propagate_environment leap-seconds.tls planets.bsp epoch_et_seconds");
        std::size_t consumed=0;
        const std::string input=argv[3];
        const double et=std::stod(input,&consumed);
        if (consumed!=input.size()||!std::isfinite(et)) throw std::invalid_argument("invalid TDB ET seconds");
        leo::SpiceEphemerisProvider sun({argv[1],argv[2]});
        const leo::Earth earth;
        const double r=earth.equatorial_radius_m+400e3;
        const auto sun_state=sun.geometric_state(10,{et});
        const leo::InertialVector radial=sun_state.position_m/leo::norm(sun_state.position_m);
        const leo::InertialVector axis=std::abs(radial.z())<.9?leo::InertialVector{0,0,1}:leo::InertialVector{0,1,0};
        const leo::InertialVector tangent=leo::cross(axis,radial)/leo::norm(leo::cross(axis,radial));
        const leo::State initial{radial*r,tangent*std::sqrt(earth.mu_m3_s2/r),{1,0,0,0},{}};
        // A six-face box makes illumination and drag observable at any Sun direction.
        std::vector<leo::Surface> surfaces;
        for (const leo::BodyVector& n:{leo::BodyVector{1,0,0},leo::BodyVector{-1,0,0},
             leo::BodyVector{0,1,0},leo::BodyVector{0,-1,0},leo::BodyVector{0,0,1},leo::BodyVector{0,0,-1}})
            surfaces.emplace_back(2,n,n*.5+leo::BodyVector{0,.1,0},leo::OpticalProperties{.3,.2});
        const leo::Spacecraft craft(10,leo::Inertia((Eigen::Vector3d{2,3,4}).asDiagonal()),std::move(surfaces));
        leo::EnvironmentalModels models;
        models.srp={true,true}; models.drag={true,true}; models.albedo={true,true};
        models.drag_model=leo::DragModel::sentman; models.gravity_gradient=true;
        const leo::EarthDynamics dynamics(leo::EarthGravity(earth,true),craft,models);
        const leo::EarthEnvironment source(earth,{et},&sun,true);
        const leo::EnvironmentalDynamics coupled(dynamics,source);
        const auto snapshot=source({0},initial);
        const auto derivative=coupled({0},initial);
        const std::array times{0.,30.,60.};
        const auto result=leo::propagate(coupled,initial,{et},times);
        const auto& final=result.samples.back().state;
        std::cout<<std::setprecision(12)
                 <<"Sun distance [m]: "<<leo::norm(snapshot.sun_position_m)<<'\n'
                 <<"Initial illumination: "<<snapshot.illumination<<'\n'
                 <<"Initial density [kg/m^3]: "<<snapshot.density_kg_m3<<'\n'
                 <<"Initial angular acceleration [rad/s^2]: "<<derivative.omega_dot.as_eigen().transpose()<<'\n'
                 <<"Samples: "<<result.samples.size()<<'\n'
                 <<"Final radius [m]: "<<leo::norm(final.r)<<'\n'
                 <<"Final body angular velocity [rad/s]: "<<final.omega.as_eigen().transpose()<<'\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
