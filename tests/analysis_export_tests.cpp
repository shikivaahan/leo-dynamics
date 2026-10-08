#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "leo/analysis/export.hpp"
#include "leo/kernel/radiation.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <limits>
using namespace leo;
using namespace leo::analysis;
namespace {
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("leo-analysis-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() {std::filesystem::create_directories(path);}
    ~Temp() {std::error_code error;std::filesystem::remove_all(path,error);}
};
std::string contents(const std::filesystem::path& path) {
    std::ifstream in(path);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
Trajectory trajectory() {
    const State state{{7000000.123456789,0,0},{0,7500.987654321,0},{1,0,0,0},{.01,.02,.03}};
    return {{123456.78901234567},{{{0},state},{{.12345678901234567},state},{{1.9876543210987654},state}}};
}
AnalysisResult result(const Trajectory& t) {
    const Spacecraft craft(10,Inertia(Eigen::Matrix3d::Identity()),{Surface(2,{1,0,0},{})});
    const EarthDynamics dynamics(EarthGravity(Earth{},false),craft);
    std::vector<EnvironmentSnapshot> snapshots;
    for(const auto& s:t.samples) {EnvironmentSnapshot env;env.epoch={t.epoch.et_seconds+s.t.seconds};snapshots.push_back(env);}
    return analyze(t,dynamics,snapshots);
}
std::vector<std::string> fields(const std::string& row) {
    std::stringstream stream(row);std::vector<std::string> values;std::string value;
    while(std::getline(stream,value,',')) values.push_back(value);
    return values;
}
}
TEST_CASE("CSV preserves every timestamp full precision schema and deterministic values", "[analysis][export]") {
    Temp tmp;const auto t=trajectory();const auto a=result(t);
    export_csv(tmp.path/"first",t,a);export_csv(tmp.path/"second",t,a);
    std::ifstream in(tmp.path/"first/trajectory.csv");std::string row;std::getline(in,row);
    REQUIRE(row=="time_s,x_m,y_m,z_m,vx_m_s,vy_m_s,vz_m_s,qw,qx,qy,qz,omega_x_rad_s,omega_y_rad_s,omega_z_rad_s");
    for(const auto& sample:t.samples) {
        REQUIRE(bool(std::getline(in,row)));const auto values=fields(row);
        REQUIRE(values.size()==14);REQUIRE(std::stod(values[0])==sample.t.seconds);
        REQUIRE(std::stod(values[1])==sample.state.r.x());REQUIRE(std::stod(values[5])==sample.state.v.y());
        REQUIRE(std::stod(values[7])==sample.state.q.w);REQUIRE(std::stod(values[13])==sample.state.omega.z());
    }
    REQUIRE_FALSE(bool(std::getline(in,row)));
    for(const auto name:{"metadata","trajectory","orbital_elements","attitude","surfaces","environment","work","per_orbit","apsides"})
        REQUIRE(contents(tmp.path/"first"/(std::string(name)+".csv"))==contents(tmp.path/"second"/(std::string(name)+".csv")));
    const auto metadata=contents(tmp.path/"first/metadata.csv");
    REQUIRE(metadata.find("inertial_frame,J2000\n")!=std::string::npos);
    REQUIRE(metadata.find("epoch_scale,TDB_seconds_past_J2000\n")!=std::string::npos);
    REQUIRE(metadata.find("orbital_elements,osculating_two_body_not_mean\n")!=std::string::npos);
    std::istringstream surface_rows(contents(tmp.path/"first/surfaces.csv"));std::getline(surface_rows,row);
    std::size_t count=0;while(std::getline(surface_rows,row)) {
        const auto values=fields(row);REQUIRE(std::isnan(std::stod(values[5])));REQUIRE(std::isnan(std::stod(values[6])));++count;
    }
    REQUIRE(count==t.samples.size());
    std::istringstream orbital_rows(contents(tmp.path/"first/orbital_elements.csv"));std::getline(orbital_rows,row);
    std::getline(orbital_rows,row);const auto values=fields(row);
    REQUIRE(std::stod(values[3])==a.orbital[0].elements.a_m);
    REQUIRE(std::stod(values[13])==a.orbital[0].specific_energy_j_kg);
}
TEST_CASE("CSV rejects malformed analyses before creating outputs and reports invalid paths", "[analysis][export]") {
    Temp tmp;const auto t=trajectory();const auto a=result(t);
    SECTION("row mismatch") {auto broken=a;broken.work.pop_back();REQUIRE_THROWS_AS(export_csv(tmp.path/"bad",t,broken),std::invalid_argument);REQUIRE_FALSE(std::filesystem::exists(tmp.path/"bad"));}
    SECTION("timestamp mismatch") {auto broken=a;broken.orbital[1].time_s+=1;REQUIRE_THROWS_AS(export_csv(tmp.path/"bad",t,broken),std::invalid_argument);}
    SECTION("nonfinite diagnostic") {auto broken=a;broken.work[0].power_w=std::numeric_limits<double>::infinity();REQUIRE_THROWS_AS(export_csv(tmp.path/"bad",t,broken),std::invalid_argument);}
    SECTION("bad surface index") {auto broken=a;broken.attitude[0].surfaces[0].surface_index=5;REQUIRE_THROWS_AS(export_csv(tmp.path/"bad",t,broken),std::invalid_argument);}
    SECTION("invalid apsis bracket") {auto broken=a;broken.apsides.push_back({ApsisKind::periapsis,0,7e6,std::numeric_limits<std::size_t>::max(),0});REQUIRE_THROWS_AS(export_csv(tmp.path/"bad",t,broken),std::invalid_argument);}
    SECTION("truncated orbit coverage") {auto broken=a;broken.orbits.clear();broken.orbit_work.clear();REQUIRE_THROWS_AS(export_csv(tmp.path/"bad",t,broken),std::invalid_argument);}
    SECTION("invalid Earth metadata") {auto broken=a;broken.earth.mu_m3_s2=-1;REQUIRE_THROWS_AS(export_csv(tmp.path/"bad",t,broken),std::invalid_argument);}
    SECTION("empty path") {REQUIRE_THROWS_AS(export_csv({},t,a),std::invalid_argument);}
    SECTION("regular file directory") {std::ofstream(tmp.path/"file")<<"occupied";REQUIRE_THROWS_AS(export_csv(tmp.path/"file",t,a),std::runtime_error);}
    SECTION("directory in place of CSV") {std::filesystem::create_directory(tmp.path/"trajectory.csv");REQUIRE_THROWS_AS(export_csv(tmp.path,t,a),std::runtime_error);}
    SECTION("snapshot count mismatch") {const EarthDynamics dynamics(EarthGravity(Earth{},false),Spacecraft(10,Inertia(Eigen::Matrix3d::Identity())));REQUIRE_THROWS_AS(analyze(t,dynamics,{}),std::invalid_argument);}
}
TEST_CASE("CSV specific SRP work has explicit J per kg columns and mass scaling", "[analysis][export]") {
    Temp tmp;const auto t=trajectory();
    const Spacecraft craft(10,Inertia(Eigen::Matrix3d::Identity()),{Surface(2,{0,1,0},{})});
    EnvironmentalModels models;models.srp={true,false};
    const EarthDynamics dynamics(EarthGravity(Earth{},false),craft,models);
    std::vector<EnvironmentSnapshot> snapshots;
    for(const auto& sample:t.samples) {
        EnvironmentSnapshot env;env.epoch={t.epoch.et_seconds+sample.t.seconds};
        env.sun_position_m=sample.state.r+InertialVector{0,astronomical_unit_m,0};
        snapshots.push_back(env);
    }
    const auto a=analyze(t,dynamics,snapshots);
    export_csv(tmp.path,t,a);
    const double expected=-2*solar_pressure_1au_n_m2*t.samples.back().state.v.y()*t.samples.back().t.seconds;
    for(const auto name:{"work.csv","per_orbit.csv"}) {
        std::istringstream rows(contents(tmp.path/name));std::string row;std::getline(rows,row);
        const auto header=fields(row);
        const auto specific=std::find(header.begin(),header.end(),"specific_srp_work_j_kg");
        REQUIRE(specific!=header.end());
        const auto total=std::find(header.begin(),header.end(),std::string(name)=="work.csv"?"cumulative_srp_work_j":"srp_work_j");
        REQUIRE(total!=header.end());
        std::vector<std::string> last;
        while(std::getline(rows,row)) last=fields(row);
        REQUIRE(last.size()==header.size());
        REQUIRE(std::stod(last[static_cast<std::size_t>(total-header.begin())])==Catch::Approx(expected).epsilon(1e-14));
        REQUIRE(std::stod(last[static_cast<std::size_t>(specific-header.begin())])==Catch::Approx(expected/10).epsilon(1e-14));
    }
}
