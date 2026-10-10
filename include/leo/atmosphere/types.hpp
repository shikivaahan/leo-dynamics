#pragma once
#include <array>
namespace leo::atmosphere {
// SI outputs. Number density order: He, O, N2, O2, Ar, H, N, anomalous O.
struct Result {
    double density_kg_m3=0;
    double temperature_k=0;
    double exospheric_temperature_k=0;
    std::array<double,8> number_density_m3{};
    bool has_temperatures=false;
    bool has_constituents=false;
};
struct NrlmsiseInput {
    int year=2000, day_of_year=1;
    double utc_seconds=0;
    double altitude_km=400, latitude_deg=0, longitude_deg=0, local_solar_time_hours=0;
    double f107=150, f107_average=150, ap=4;
    std::array<double,7> ap_history{4,4,4,4,4,4,4};
};
struct Jb2008Input {
    double mjd_utc=51544.5;
    std::array<double,2> sun_ra_dec_rad{};
    std::array<double,3> position_ra_lat_alt{0,0,400}; // radians, radians, km
    double f10=150,f10_average=150,s10=150,s10_average=150;
    double m10=150,m10_average=150,y10=150,y10_average=150;
    double dstdtc_k=0;
};
} // namespace leo::atmosphere
