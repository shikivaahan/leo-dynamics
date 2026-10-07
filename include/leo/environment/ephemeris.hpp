#pragma once
#include "leo/kernel/frames.hpp"
#include "leo/kernel/time.hpp"
namespace leo {
struct EphemerisState { InertialVector position_m,velocity_m_s; };
// Earth observer, J2000, geometric NONE correction, SI output.
// Epoch is TDB seconds past J2000; no UTC or leapsecond handling happens here.
// Implementations must explicitly reject invalid epochs, targets and coverage.
class EphemerisProvider {
public:
    virtual ~EphemerisProvider()=default;
    virtual EphemerisState geometric_state(int target_naif_id,Epoch) const=0;
};
}
