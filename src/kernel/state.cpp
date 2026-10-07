#include "leo/kernel/state.hpp"
#include <stdexcept>
namespace leo {
State validated_state(State s) {
    if (!s.r.as_eigen().allFinite()||!s.v.as_eigen().allFinite()||!s.omega.as_eigen().allFinite())
        throw std::invalid_argument("nonfinite state");
    s.q=normalize_quaternion(s.q);
    return s;
}
}
