#pragma once
#include "leo/kernel/state.hpp"
#include "leo/kernel/time.hpp"
#include <vector>
namespace leo {
struct Sample { ElapsedTime t; State state; };
struct Trajectory { Epoch epoch; std::vector<Sample> samples; };
}
