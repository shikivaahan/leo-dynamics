// Project-owned adapter probe: the same stdin/stdout contract as
// jb2008_native_kernel.f90, but routed through the project's own
// leo::atmosphere::Jb2008 adapter (C++ wrapper + Fortran ABI bridge + domain
// checks) instead of calling the official Fortran model directly.
//
// Running both probes on byte-identical inputs isolates whatever the bridge and
// the adapter add on top of the official model itself.
//
// Input : one whitespace separated record per case in native JB2008 order
//         AMJD SUN(1) SUN(2) SAT(1) SAT(2) SAT(3) F10 F10B S10 S10B XM10 XM10B
//         Y10 Y10B DSTDTC, terminated by EOF.
// Output: one record per case: RHO TEMP(1) TEMP(2), 17 significant digits,
//         with TEMP(1) the exospheric temperature and TEMP(2) the local one.
//
// Only full precision is offered: the printed-width comparison is a property of
// the fixture format, not of the adapter, and the native kernel covers it.
//
// No JB2008 equation is reproduced here; the adapter is exercised as shipped.

#include "leo/atmosphere/jb2008.hpp"

#include <cstdio>
#include <iostream>

int main() {
    const leo::atmosphere::Jb2008 model;
    for (double mjd, s1, s2, p1, p2, p3, f10, f10b, s10, s10b, m10, m10b, y10,
                y10b, dtc;
         std::cin >> mjd >> s1 >> s2 >> p1 >> p2 >> p3 >> f10 >> f10b >> s10 >>
             s10b >> m10 >> m10b >> y10 >> y10b >> dtc;) {
        leo::atmosphere::Jb2008Input in;
        in.mjd_utc = mjd;
        in.sun_ra_dec_rad = {s1, s2};
        in.position_ra_lat_alt = {p1, p2, p3};
        in.f10 = f10; in.f10_average = f10b;
        in.s10 = s10; in.s10_average = s10b;
        in.m10 = m10; in.m10_average = m10b;
        in.y10 = y10; in.y10_average = y10b;
        in.dstdtc_k = dtc;
        const auto r = model.evaluate(in);
        std::printf("%.17g %.17g %.17g\n", r.density_kg_m3,
                    r.exospheric_temperature_k, r.temperature_k);
    }
    return 0;
}