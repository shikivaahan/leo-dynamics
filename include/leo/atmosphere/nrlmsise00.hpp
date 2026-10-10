#pragma once
#include "leo/atmosphere/types.hpp"
#include <array>
namespace leo::atmosphere {
// Direct Brodowski C reference gtd7d. Process-wide serialization protects native state.
class Nrlmsise00 {
    std::array<int,24> switches_;
public:
    static constexpr std::array<int,24> standard_switches() {
        std::array<int,24> s{};
        for(unsigned i=1;i<s.size();++i) s[i]=1;
        return s;
    }
    explicit Nrlmsise00(std::array<int,24> switches=standard_switches());
    const std::array<int,24>& switches() const noexcept { return switches_; }
    Result evaluate(const NrlmsiseInput&) const;
};
}
