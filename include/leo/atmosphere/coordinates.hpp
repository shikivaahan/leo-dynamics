#pragma once
#include <Eigen/Core>
namespace leo::atmosphere {
inline constexpr double wgs84_a_m=6378137.0;
inline constexpr double wgs84_f=1.0/298.257223563;
struct Geodetic { double latitude_rad,longitude_rad,altitude_m; };
Geodetic geodetic(const Eigen::Vector3d& earth_fixed_m);
Eigen::Vector3d earth_fixed(Geodetic);
}
