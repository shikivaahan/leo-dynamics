#include "leo/integration/propagate.hpp"
#include "leo/kernel/dynamics.hpp"
#include "leo/kernel/orbital.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <string_view>
#include <vector>
int main(int argc,char** argv) {
    std::string_view model="point-mass";
    if (argc==3&&std::string_view(argv[1])=="--model") model=argv[2];
    else if (argc!=1) {
        std::cerr<<"Usage: propagate_leo [--model point-mass|j2]\n";
        return 2;
    }
    if (model!="point-mass"&&model!="j2") {
        std::cerr<<"Unknown model: "<<model<<"\n";
        return 2;
    }
    try {
        constexpr double radians=std::numbers::pi/180;
        const leo::Earth earth;
        const leo::EarthDynamics rhs(leo::EarthGravity(earth,model=="j2"),
            leo::Spacecraft(10,leo::Inertia(Eigen::Vector3d(1,2,3).asDiagonal())));
        const auto initial=leo::coe_to_cartesian({7000000,.01,60*radians,30*radians,40*radians,0},earth.mu_m3_s2);
        std::vector<double> times;
        times.reserve(1441);
        for (int i=0;i<=1440;++i) times.push_back(60.*i);
        const auto trajectory=leo::propagate(rhs,{initial.r,initial.v,{1,0,0,0},{0,0,0}},leo::Epoch{0},times);
        const auto& final=trajectory.samples.back().state;
        const auto first_elements=leo::cartesian_to_coe(initial,earth.mu_m3_s2);
        const auto last_elements=leo::cartesian_to_coe({final.r,final.v},earth.mu_m3_s2);
        const double raan_change=std::remainder(last_elements.raan_rad-first_elements.raan_rad,2*std::numbers::pi);
        std::cout<<std::setprecision(15)<<"model="<<model<<"\n"
                 <<"sample_count="<<trajectory.samples.size()<<"\n"
                 <<"final_radius_m="<<leo::norm(final.r)<<"\n"
                 <<"raan_change_rad="<<raan_change<<"\n";
    } catch (const std::exception& error) {
        std::cerr<<"Propagation failed: "<<error.what()<<"\n";
        return 1;
    }
}
