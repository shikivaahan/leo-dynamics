#include "leo/integration/detail/error_control.hpp"
#include <algorithm>
#include <cmath>
namespace leo::detail {
double scaled_error(const PackedState& y,const PackedState& trial,const PackedState& error,const IntegrationOptions& o) {
    double maximum=0;
    for (std::size_t i=0;i<y.size();++i) {
        const double atol=i<3?o.position_atol_m:i<6?o.velocity_atol_m_s:i<10?o.quaternion_atol:o.omega_atol_rad_s;
        const double ratio=std::abs(error[i])/(atol+o.rtol*std::max(std::abs(y[i]),std::abs(trial[i])));
        if (!std::isfinite(ratio)) return ratio;
        maximum=std::max(maximum,ratio);
    }
    return maximum;
}
double next_step(double h,double error,bool accepted,double max_step_s) {
    const double factor=error==0?5:.9*std::pow(error,-.2);
    return std::min(max_step_s,h*std::clamp(factor,accepted?.2:.1,accepted?5.:.9));
}
}
