#include <catch2/catch_test_macros.hpp>
#include "leo/kernel/radiation.hpp"
#include <array>
#include <cmath>
using namespace leo;
TEST_CASE("Conical disk overlap agrees with independent Cartesian area quadrature at contacts", "[radiation][numerical]") {
    // 80-digit integration of vertical disk-intersection widths, not the
    // production acos/lens or segment-area formula. Binary64 Cartesian inputs.
    // Generator: docs/numerical-audit/eclipse_reference.py.
    struct Fixture { double sun_x,sun_y,illumination; };
    const std::array fixtures{
        Fixture{62268219084.476128, 136019557439.19482, 1.7016745128472572e-15},
        Fixture{62268219071.939987, 136019557444.93437, 1.7011150870232408e-12},
        Fixture{62268217818.325363, 136019558018.88951, 1.7011102281850352e-09},
        Fixture{62266952804.513763, 136020137185.9149, 5.3777570480789964e-05},
        Fixture{62255556040.855957, 136025354376.42937, 0.0016959566549171598},
        Fixture{62141564377.747086, 136077473755.79453, 0.052137327338716496},
        Fixture{61634408024.260223, 136307959176.12656, 0.50043092867431971},
        Fixture{61126396905.201584, 136536554449.00098, 0.94804883502777837},
        Fixture{61011977325.820381, 136587727574.93602, 0.99831083510454321},
        Fixture{61000533010.598381, 136592839614.43465, 0.99994644013927636},
        Fixture{60999262665.199577, 136593406991.69414, 0.99999999830578479},
        Fixture{60999261406.29612, 136593407553.95401, 0.9999999999983058},
        Fixture{60999261393.707077, 136593407559.57663, 0.99999999999999833},
    };
    double previous=-1;
    for (const auto& f:fixtures) {
        const double actual=illumination_factor(EclipseModel::conical,{-7e6,0,0},
            {f.sun_x,f.sun_y,0},6378137.,696340000.);
        REQUIRE(std::abs(actual-f.illumination)<1e-12);
        REQUIRE(actual>=previous-1e-13);
        previous=actual;
    }
}

TEST_CASE("Conical illumination retains small centre separations near the physical shadow apex", "[radiation][numerical]") {
    // Nominal Earth/Sun radii; nearly equal apparent disks. A 1e-8 rad centre
    // separation rounds the unit-vector dot product to one, but the uncovered
    // area is not zero. Independent 80-digit Cartesian quadrature reference.
    const InertialVector r{-1370244010.4444466,0,0};
    const InertialVector sun{148227626689.55554,1495.978707,0};
    const double actual=illumination_factor(EclipseModel::conical,r,sun,6378137.,696340000.);
    REQUIRE(std::abs(actual-1.3676741029071159e-6)<1e-12);
}
