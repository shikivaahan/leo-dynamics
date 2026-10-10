// Reproducible boundary smoke: drives the built DirectAtmosphere provider at
// positions on the JB2008 120 km / 1500 km domain edge and records evaluations
// or rejections. Link against before/after libraries for the regression record.

#include "leo/atmosphere/provider.hpp"
#include "leo/atmosphere/coordinates.hpp"
#include "leo/atmosphere/weather.hpp"
#include "leo/environment/spice.hpp"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 8) {
        std::fprintf(stderr,
                     "usage: boundary_probe LSK SPK PCK WEATHER SOLFSMY DTCFILE FRAME\n");
        return 2;
    }
    const std::string lsk = argv[1], spk = argv[2], pck = argv[3];
    const std::string cssi = argv[4], solfsmy = argv[5], dtcfile = argv[6];
    const std::string frame = argv[7];

    const leo::SpiceEphemerisProvider spice{{lsk, spk, pck}};
    const auto weather = leo::atmosphere::WeatherDataset::load(cssi, solfsmy, dtcfile);
    const leo::atmosphere::DirectAtmosphere atmosphere(
        leo::atmosphere::Model::jb2008, spice, weather, frame, false);

    // 2003-10-30T00:00:00 UTC, the epoch the coordinate report used.
    const leo::Epoch epoch{120744064.18248665};

    struct Case { double lat_deg, lon_deg, h_km; };
    const std::vector<Case> cases = {
        {0.0, 0.0, 120.0}, {0.0, 123.456, 120.0}, {0.0, 359.999, 120.0},
        {45.0, 0.0, 120.0}, {45.0, 123.456, 120.0}, {45.0, -179.0, 120.0},
        {-45.0, 180.0, 120.0}, {-45.0, 270.0, 120.0},
        {89.9, 123.456, 120.0}, {-89.9, 0.0, 120.0}, {-89.9, 270.0, 120.0},
        {90.0, 0.0, 120.0}, {-90.0, 45.0, 120.0},
        {0.0, 0.0, 1500.0}, {0.0, 90.0, 1500.0}, {45.0, 180.0, 1500.0},
        {45.0, 270.0, 1500.0}, {-45.0, 0.0, 1500.0},
        {89.9, 0.0, 1500.0}, {89.9, 359.999, 1500.0},
        {90.0, 123.456, 1500.0}, {-90.0, 270.0, 1500.0},
        {0.0, 0.0, 400.0}, {45.0, 123.456, 400.0}, {-89.9, 0.0, 400.0},
    };

    const Eigen::Matrix3d to_fixed = leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, frame);
    std::printf("[\n");
    bool first = true;
    for (const Case& c : cases) {
        const leo::atmosphere::Geodetic point{c.lat_deg * std::numbers::pi / 180.0,
                                              c.lon_deg * std::numbers::pi / 180.0,
                                              c.h_km * 1e3};
        const Eigen::Vector3d fixed = leo::atmosphere::earth_fixed(point);
        const leo::InertialVector position{to_fixed.transpose() * fixed};
        const auto recovered = leo::atmosphere::geodetic(to_fixed * position.as_eigen());
        if (!first) std::printf(",\n");
        first = false;
        std::printf("  {\"lat_deg\": %.6f, \"lon_deg\": %.6f, \"requested_km\": %.6f, "
                    "\"pipeline_altitude_km\": %.15g",
                    c.lat_deg, c.lon_deg, c.h_km, recovered.altitude_m / 1000.0);
        try {
            const auto result = atmosphere.evaluate(epoch, position);
            std::printf(", \"status\": \"evaluated\", \"density_kg_m3\": %.9e}",
                        result.density_kg_m3);
        } catch (const std::exception& e) {
            std::printf(", \"status\": \"threw\", \"what\": \"%s\"}", e.what());
        }
    }
    std::printf("\n]\n");
    return 0;
}