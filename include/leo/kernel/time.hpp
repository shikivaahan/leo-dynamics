#pragma once
namespace leo {
struct ElapsedTime { double seconds; };
// TDB ephemeris seconds past J2000; metadata, not kernel time.
struct Epoch { double et_seconds; };
}
