#pragma once
#include "leo/kernel/state.hpp"
#include <array>
namespace leo::detail {
using PackedState=std::array<double,13>;
inline PackedState pack(const State& s) {
    return {s.r.x(),s.r.y(),s.r.z(),s.v.x(),s.v.y(),s.v.z(),s.q.w,s.q.x,s.q.y,s.q.z,s.omega.x(),s.omega.y(),s.omega.z()};
}
inline PackedState pack(const StateDerivative& d) {
    return {d.r_dot.x(),d.r_dot.y(),d.r_dot.z(),d.v_dot.x(),d.v_dot.y(),d.v_dot.z(),d.q_dot.w,d.q_dot.x,d.q_dot.y,d.q_dot.z,d.omega_dot.x(),d.omega_dot.y(),d.omega_dot.z()};
}
inline State unpack(const PackedState& p) {
    return {{p[0],p[1],p[2]},{p[3],p[4],p[5]},{p[6],p[7],p[8],p[9]},{p[10],p[11],p[12]}};
}
}
