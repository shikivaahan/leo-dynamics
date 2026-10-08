#pragma once
#include "leo/environment/ephemeris.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace leo {
// Owned immutable Sun states: Earth observer, J2000, geometric NONE, SI.
// The source is used only during construction and need not outlive this cache.
// No CSPICE dependency, global state, allocation or mutable cursor on lookup.
class InterpolatedEphemerisProvider final:public EphemerisProvider {
    struct Sample { double et; EphemerisState state; };
    double start_,end_,interval_;
    std::vector<Sample> samples_;
public:
    // Both endpoints included. A nonmultiple span ends with a shorter segment.
    // Zero/reversed ranges and unrepresentable sampling grids are rejected.
    InterpolatedEphemerisProvider(const EphemerisProvider& source,Epoch start,Epoch end,double interval_s)
        :start_(start.et_seconds),end_(end.et_seconds),interval_(interval_s) {
        const double span=end_-start_;
        if(!std::isfinite(start_)||!std::isfinite(end_)||!std::isfinite(span)||span<=0||
           !std::isfinite(interval_)||interval_<=0)
            throw std::invalid_argument("finite increasing cache range and positive interval required");
        const double segments=std::max(1.,std::ceil(span/interval_));
        if(!std::isfinite(segments)||segments>=static_cast<double>(samples_.max_size()-1))
            throw std::length_error("ephemeris cache sampling grid too large");
        if(start_+std::min(interval_,span)<=start_)
            throw std::invalid_argument("cache interval is not representable at start epoch");
        const auto count=static_cast<std::size_t>(segments)+1;
        samples_.reserve(count);
        for(std::size_t i=0;i<count;++i) {
            const double et=i==count-1?end_:start_+static_cast<double>(i)*interval_;
            if(!std::isfinite(et)||et>end_||(i&&et<=samples_.back().et))
                throw std::invalid_argument("cache sampling grid is not strictly increasing");
            const auto state=source.geometric_state(10,{et});
            if(!state.position_m.as_eigen().allFinite()||!state.velocity_m_s.as_eigen().allFinite())
                throw std::runtime_error("nonfinite source ephemeris sample");
            samples_.push_back({et,state});
        }
    }
    EphemerisState geometric_state(int target_naif_id,Epoch epoch) const override {
        if(target_naif_id!=10||!std::isfinite(epoch.et_seconds))
            throw std::invalid_argument("Sun target and finite TDB epoch required");
        const double et=epoch.et_seconds;
        if(et<start_||et>end_) throw std::out_of_range("epoch outside ephemeris cache coverage");
        if(et==end_) return samples_.back().state;
        const double offset=(et-start_)/interval_;
        auto i=static_cast<std::size_t>(std::min(offset,static_cast<double>(samples_.size()-2)));
        // Absolute-epoch rounding can place a query on the neighbouring segment.
        while(i&&et<samples_[i].et) --i;
        while(i+1<samples_.size()-1&&et>=samples_[i+1].et) ++i;
        const auto& a=samples_[i];const auto& b=samples_[i+1];
        if(et==a.et) return a.state;
        const double h=b.et-a.et,u=(et-a.et)/h;
        const InertialVector delta=b.state.position_m-a.state.position_m;
        const InertialVector c2=delta*3-(a.state.velocity_m_s*2+b.state.velocity_m_s)*h;
        const InertialVector c3=delta*(-2)+(a.state.velocity_m_s+b.state.velocity_m_s)*h;
        // Difference-based Hermite form avoids cancellation of absolute Sun
        // positions (~1e11 m), especially in the differentiated polynomial.
        return {a.state.position_m+(a.state.velocity_m_s*h+(c2+c3*u)*u)*u,
                a.state.velocity_m_s+(c2*2+c3*(3*u))*(u/h)};
    }
    std::size_t sample_count() const noexcept { return samples_.size(); }
    // Owned capacity plus object size; excludes allocator bookkeeping.
    std::size_t memory_bytes() const noexcept { return sizeof(*this)+samples_.capacity()*sizeof(Sample); }
};
}
