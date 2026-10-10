#pragma once
#include "leo/atmosphere/types.hpp"
namespace leo::atmosphere {
// Official external Fortran model; native output kg/m3, K. No composition output.
class Jb2008 {
public:
    Result evaluate(const Jb2008Input&) const;
};
}
