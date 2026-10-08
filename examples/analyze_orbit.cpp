#include "leo/analysis/export.hpp"
#include "leo/environment/source.hpp"
#include "leo/integration/propagate.hpp"
#ifdef LEO_EXAMPLE_CSPICE
#include "leo/environment/spice.hpp"
#endif
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
namespace {
// Constructed stationary geometric Sun; deterministic demonstration, not an
// ephemeris approximation for mission results. No kernel downloads required.
class FixedSun final:public leo::EphemerisProvider {
public:
    leo::EphemerisState geometric_state(int target,leo::Epoch epoch) const override {
        if(target!=10||!std::isfinite(epoch.et_seconds)) throw std::invalid_argument("FixedSun requires Sun target and finite ET");
        return {{leo::astronomical_unit_m,0,0},{}};
    }
};
}
int main(int argc,char** argv) {
    try {
        if(argc!=2&&argc!=6) throw std::invalid_argument("usage: analyze_orbit OUTPUT_DIR [--spice leap-seconds.tls planets.bsp epoch_et_seconds]");
        leo::Epoch epoch{0};
        std::unique_ptr<leo::EphemerisProvider> sun=std::make_unique<FixedSun>();
        if(argc==6) {
            if(std::string(argv[2])!="--spice") throw std::invalid_argument("expected --spice");
#ifdef LEO_EXAMPLE_CSPICE
            std::size_t consumed=0;const std::string text=argv[5];
            epoch.et_seconds=std::stod(text,&consumed);
            if(consumed!=text.size()||!std::isfinite(epoch.et_seconds)) throw std::invalid_argument("invalid TDB ET seconds");
            sun=std::make_unique<leo::SpiceEphemerisProvider>(std::vector<std::filesystem::path>{argv[3],argv[4]});
#else
            throw std::invalid_argument("--spice requires LEO_ENABLE_CSPICE=ON");
#endif
        }
        const leo::Earth earth;
        std::vector<leo::Surface> surfaces;
        for(const leo::BodyVector& normal:{leo::BodyVector{1,0,0},leo::BodyVector{-1,0,0},leo::BodyVector{0,1,0},
            leo::BodyVector{0,-1,0},leo::BodyVector{0,0,1},leo::BodyVector{0,0,-1}})
            surfaces.emplace_back(2,normal,normal*.5+leo::BodyVector{0,.1,0},leo::OpticalProperties{.3,.2});
        const leo::Spacecraft craft(10,leo::Inertia((Eigen::Vector3d{2,3,4}).asDiagonal()),std::move(surfaces));
        leo::EnvironmentalModels models;
        models.srp={true,true};models.drag={true,true};models.albedo={true,true};
        models.drag_model=leo::DragModel::sentman;models.gravity_gradient=true;
        const leo::EarthDynamics dynamics(leo::EarthGravity(earth,true),craft,models);
        const leo::EarthEnvironment source(earth,epoch,sun.get(),true);
        const leo::EnvironmentalDynamics coupled(dynamics,source);
        const leo::ClassicalElements elements{earth.equatorial_radius_m+700e3,.02,.8,.4,.3,.5};
        const auto orbit=leo::coe_to_cartesian(elements,earth.mu_m3_s2);
        const leo::State initial{orbit.r,orbit.v,{1,0,0,0},{.001,.002,.003}};
        const double period=2*std::numbers::pi*std::sqrt(elements.a_m*elements.a_m*elements.a_m/earth.mu_m3_s2);
        std::vector<double> times;times.reserve(751);
        for(int i=0;i<=750;++i) times.push_back(2.5*period*i/750);
        leo::IntegrationOptions options;options.max_step_s=10;
        const auto trajectory=leo::propagate(coupled,initial,epoch,times,options);
        std::vector<leo::EnvironmentSnapshot> snapshots;snapshots.reserve(times.size());
        for(const auto& sample:trajectory.samples) snapshots.push_back(source(sample.t,sample.state));
        const auto analysis=leo::analysis::analyze(trajectory,dynamics,snapshots);
        leo::analysis::export_csv(argv[1],trajectory,analysis);
        std::size_t complete=0;for(const auto& orbit_summary:analysis.orbits) complete+=orbit_summary.complete;
        std::cout<<std::setprecision(17)<<"Exported "<<trajectory.samples.size()<<" samples to "<<argv[1]<<'\n'
                 <<"Complete orbits: "<<complete<<"; partial intervals: "<<analysis.orbits.size()-complete<<'\n'
                 <<"Cumulative SRP work [J]: "<<analysis.work.back().cumulative_work_j<<'\n'
                 <<"Two-body orbital energy change [J]: "<<analysis.work.back().orbital_energy_change_j<<'\n'
                 <<"Plot: python3 plotting/plot_analysis.py "<<argv[1]<<" --output "<<argv[1]<<"/plots --format both\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
