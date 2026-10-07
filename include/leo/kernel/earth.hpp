#pragma once
namespace leo {
// Stage 1 approximation: J2 symmetry axis is fixed J2000 +Z.
struct Earth {
    double mu_m3_s2=3.986004418e14;
    double equatorial_radius_m=6378136.3;
    double j2=1.08262668e-3;
};
}
