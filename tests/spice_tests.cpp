#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
#include "SpiceUsr.h"
#include "leo/environment/spice.hpp"
using namespace leo;
namespace {
constexpr double j2000_obliquity_rad=0.40909280422232904;
constexpr double fixture_states_km[3][6]={
    {1.0e8,0.0,0.5e8,0.0,20.0,0.0},
    {0.9e8,0.2e8,0.5e8,-1.0,20.0,0.0},
    {0.8e8,0.4e8,0.5e8,-2.0,20.0,0.0}};
constexpr double fixture_epochs_s[3]={0.0,86400.0,172800.0};
// Fixture SPK removed with the file; never leaves state behind for other tests.
struct TempKernel {
    std::filesystem::path path;
    TempKernel():path(std::filesystem::temp_directory_path()/("leo_sun_fixture_"
        +std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bsp")) {}
    ~TempKernel() { std::error_code ec; std::filesystem::remove(path,ec); }
};
void spice_return_mode() {
    SpiceChar action[]="RETURN";
    erract_c("SET",0,action);
    reset_c();
}
// Sun (10) relative to the Earth (399) in J2000, quadratic through three nodes
// spanning two days; exact states and exact mid-interval averages.
void write_sun_fixture(const std::filesystem::path& path) {
    spice_return_mode();
    std::error_code ec;
    std::filesystem::remove(path,ec);
    SpiceInt handle=0;
    spkopn_c(path.string().c_str(),"SPK",0,&handle);
    REQUIRE(!failed_c());
    spkw09_c(handle,sun_naif_id,earth_naif_id,"J2000",fixture_epochs_s[0],fixture_epochs_s[2],
        "SUN REL EARTH",2,3,fixture_states_km,fixture_epochs_s);
    REQUIRE(!failed_c());
    spkcls_c(handle);
    REQUIRE(!failed_c());
}
std::vector<std::filesystem::path> only(const std::filesystem::path& file) { return {file}; }
double position_error_m(EphemerisState state,const double expected_km[3]) {
    return norm(state.position_m-InertialVector(expected_km[0]*1e3,expected_km[1]*1e3,expected_km[2]*1e3));
}
const char* env_path(const char* name) {
    const char* value=std::getenv(name);
    return value!=nullptr&&*value!='\0'?value:nullptr;
}
}
TEST_CASE("Sun geometry reproduces the loaded ephemeris kernel","[spice]") {
    TempKernel fixture;
    write_sun_fixture(fixture.path);
    SpiceEphemerisProvider provider{only(fixture.path)};
    for(int node=0;node<3;++node) {
        const auto state=provider.geometric_state(sun_naif_id,Epoch{fixture_epochs_s[node]});
        REQUIRE(position_error_m(state,fixture_states_km[node])<1e-3);
        REQUIRE(norm(state.velocity_m_s-InertialVector(fixture_states_km[node][3]*1e3,
            fixture_states_km[node][4]*1e3,fixture_states_km[node][5]*1e3))<1e-6);
    }
}
TEST_CASE("Sun geometry is continuous and reported in J2000","[spice]") {
    TempKernel fixture;
    write_sun_fixture(fixture.path);
    SpiceEphemerisProvider provider{only(fixture.path)};
    const auto midpoint=provider.geometric_state(sun_naif_id,Epoch{43200.0});
    const double expected_midpoint_km[3]={9.5e7,1.0e7,5.0e7};
    REQUIRE(position_error_m(midpoint,expected_midpoint_km)<1.0);
    REQUIRE(norm(midpoint.velocity_m_s-InertialVector(-5.0e2,2.0e4,0.0))<1e-3);
    // ECLIPJ2000 would tilt the out-of-plane components by the obliquity.
    const InertialVector ecliptic{9.5e10,1.0e10*std::cos(j2000_obliquity_rad)+5.0e10*std::sin(j2000_obliquity_rad),
        -1.0e10*std::sin(j2000_obliquity_rad)+5.0e10*std::cos(j2000_obliquity_rad)};
    REQUIRE(norm(midpoint.position_m-ecliptic)>1e9);
}
TEST_CASE("Only the Sun is a supported ephemeris target","[spice]") {
    TempKernel fixture;
    write_sun_fixture(fixture.path);
    SpiceEphemerisProvider provider{only(fixture.path)};
    REQUIRE_THROWS_AS(provider.geometric_state(earth_naif_id,Epoch{0.0}),std::invalid_argument);
    REQUIRE_THROWS_AS(provider.geometric_state(0,Epoch{0.0}),std::invalid_argument);
    REQUIRE_THROWS_AS(provider.geometric_state(-3,Epoch{0.0}),std::invalid_argument);
}
TEST_CASE("Non-finite ephemeris epochs are rejected","[spice]") {
    TempKernel fixture;
    write_sun_fixture(fixture.path);
    SpiceEphemerisProvider provider{only(fixture.path)};
    REQUIRE_THROWS_AS(provider.geometric_state(sun_naif_id,Epoch{std::nan("")}),std::invalid_argument);
    REQUIRE_THROWS_AS(provider.geometric_state(sun_naif_id,Epoch{std::numeric_limits<double>::infinity()}),
        std::invalid_argument);
}
TEST_CASE("Epochs outside kernel coverage are reported as failures","[spice]") {
    TempKernel fixture;
    write_sun_fixture(fixture.path);
    SpiceEphemerisProvider provider{only(fixture.path)};
    for(const double et:{-1.0e6,1.0e8,1.0e12}) {
        try {
            provider.geometric_state(sun_naif_id,Epoch{et});
            FAIL("expected a coverage failure at ET="<<et);
        } catch(const std::runtime_error& error) {
            REQUIRE(std::string(error.what()).find("CSPICE")!=std::string::npos);
        }
    }
}
TEST_CASE("Missing, empty and unloadable kernel lists are rejected","[spice]") {
    REQUIRE_THROWS_AS(SpiceEphemerisProvider({}),std::invalid_argument);
    REQUIRE_THROWS_AS(SpiceEphemerisProvider({std::filesystem::temp_directory_path()/"leo_absent_kernel.bsp"}),
        std::invalid_argument);
    // An existing path SPICE cannot read is a load failure, not a missing file.
    REQUIRE_THROWS_AS(SpiceEphemerisProvider({std::filesystem::temp_directory_path()}),std::runtime_error);
}
TEST_CASE("Kernels stay loaded while another provider holds them","[spice]") {
    TempKernel fixture;
    write_sun_fixture(fixture.path);
    SpiceEphemerisProvider outer{only(fixture.path)};
    {
        SpiceEphemerisProvider inner{only(fixture.path)};
        REQUIRE(position_error_m(inner.geometric_state(sun_naif_id,Epoch{0.0}),fixture_states_km[0])<1e-3);
    }
    REQUIRE(position_error_m(outer.geometric_state(sun_naif_id,Epoch{0.0}),fixture_states_km[0])<1e-3);
    SpiceEphemerisProvider reloaded{only(fixture.path)};
    REQUIRE(position_error_m(reloaded.geometric_state(sun_naif_id,Epoch{0.0}),fixture_states_km[0])<1e-3);
}
TEST_CASE("UTC conversion without a leapseconds kernel is reported","[spice]") {
    TempKernel fixture;
    write_sun_fixture(fixture.path);
    SpiceEphemerisProvider provider{only(fixture.path)};
    REQUIRE_THROWS_AS(SpiceEphemerisProvider::epoch_from_utc("2000-01-01T12:00:00"),std::runtime_error);
}
// Real kernel checks use caller-supplied paths; the tests never download files.
TEST_CASE("Sun geometry against a DE planetary kernel","[spice][external]") {
    const char* spk=env_path("LEO_SPICE_TEST_SPK");
    if(spk==nullptr) SKIP("Set LEO_SPICE_TEST_SPK to exercise a real planetary kernel");
    SpiceEphemerisProvider provider{{spk}};
    const auto at_j2000=provider.geometric_state(sun_naif_id,Epoch{0.0});
    REQUIRE(norm(at_j2000.position_m)>1.46e11);
    REQUIRE(norm(at_j2000.position_m)<1.54e11);
    REQUIRE(norm(at_j2000.velocity_m_s)>25e3);
    REQUIRE(norm(at_j2000.velocity_m_s)<35e3);
    // The Earth-Sun direction turns with the orbit: one sidereal year repeats
    // it, thirty days move it by about thirty degrees.
    const auto thirty_days_later=provider.geometric_state(sun_naif_id,Epoch{30.0*86400.0});
    REQUIRE(dot(at_j2000.position_m,thirty_days_later.position_m)
        <0.95*norm(at_j2000.position_m)*norm(thirty_days_later.position_m));
}
TEST_CASE("UTC strings convert through a leapseconds kernel","[spice][external]") {
    const char* lsk=env_path("LEO_SPICE_TEST_LSK");
    if(lsk==nullptr) SKIP("Set LEO_SPICE_TEST_LSK to exercise UTC conversion");
    SpiceEphemerisProvider provider{{lsk}};
    // ET counts TDB seconds past J2000, and TT-UTC is 64.184 s in 2000, so UTC
    // noon sits at ET 64.184 while TDB noon is UTC 11:58:55.816.
    REQUIRE(SpiceEphemerisProvider::epoch_from_utc("2000-01-01T12:00:00").et_seconds==Catch::Approx(64.184).margin(0.05));
    REQUIRE(SpiceEphemerisProvider::epoch_from_utc("2000-01-01T11:58:55.816").et_seconds==Catch::Approx(0.0).margin(0.05));
}