#pragma once
#include "leo/integration/detail/packed_state.hpp"
#include "leo/integration/options.hpp"
namespace leo::detail {
double scaled_error(const PackedState&,const PackedState&,const PackedState&,const IntegrationOptions&);
double next_step(double h,double error,bool accepted,double max_step_s);
}
