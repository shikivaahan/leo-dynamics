#pragma once
#include "leo/integration/detail/error_control.hpp"
#include "leo/integration/trajectory.hpp"
#include <boost/numeric/odeint/stepper/runge_kutta_dopri5.hpp>
#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
namespace leo {
// Forward adaptive Dopri5 with exact requested sample labels. Epoch is metadata.
// User RHS exceptions propagate; detected invalid numerical data is runtime_error.
template<class Rhs>
Trajectory propagate(const Rhs& rhs,const State& initial,Epoch epoch,
                     std::span<const double> sample_times_s,
                     const IntegrationOptions& options={}) {
    const auto& o=options;
    for (double value:{o.rtol,o.position_atol_m,o.velocity_atol_m_s,o.quaternion_atol,o.omega_atol_rad_s,o.initial_step_s,o.max_step_s})
        if (!std::isfinite(value)||value<=0) throw std::invalid_argument("tolerances and steps must be finite and positive");
    if (o.max_step_s<o.initial_step_s||o.max_attempts==0) throw std::invalid_argument("invalid step limits");
    if (!std::isfinite(epoch.et_seconds)||sample_times_s.empty()||sample_times_s.front()!=0)
        throw std::invalid_argument("finite epoch and sample times starting at zero required");
    for (std::size_t i=0;i<sample_times_s.size();++i)
        if (!std::isfinite(sample_times_s[i])||(i>0&&sample_times_s[i]<=sample_times_s[i-1]))
            throw std::invalid_argument("sample times must be finite and strictly increasing");
    const State start=validated_state(initial);
    Trajectory result{epoch,{}};
    result.samples.reserve(sample_times_s.size());
    result.samples.push_back({{0},start});
    if (sample_times_s.size()==1) return result;
    using detail::PackedState;
    auto finite=[](const PackedState& p) {
        for (double value:p) if (!std::isfinite(value)) throw std::runtime_error("nonfinite integration state/derivative/error");
    };
    auto system=[&](const PackedState& p,PackedState& derivative,double time) {
        finite(p);
        if (!std::isfinite(time)) throw std::runtime_error("nonfinite stage time");
        derivative=detail::pack(rhs(ElapsedTime{time},detail::unpack(p)));
        finite(derivative);
    };
    boost::numeric::odeint::runge_kutta_dopri5<PackedState> stepper;
    PackedState y=detail::pack(start),dy{},trial{},trial_derivative{},error{};
    double t=0,proposed_h=o.initial_step_s;
    std::size_t attempts=0;
    system(y,dy,t);
    for (std::size_t sample=1;sample<sample_times_s.size();++sample) {
        const double target=sample_times_s[sample];
        while (t<target) {
            if (attempts==o.max_attempts) throw std::runtime_error("integration attempt limit exhausted");
            const double remaining=target-t;
            const double h=std::min({proposed_h,o.max_step_s,remaining});
            const double endpoint=t+h;
            if (h<=0||endpoint==t||!std::isfinite(endpoint)) throw std::runtime_error("integration step makes no progress");
            ++attempts;
            // Explicit derivative overload: no hidden FSAL cache is trusted.
            stepper.do_step(system,y,dy,t,trial,trial_derivative,h,error);
            finite(trial); finite(trial_derivative); finite(error);
            const double e=detail::scaled_error(y,trial,error,o);
            if (!std::isfinite(e)) throw std::runtime_error("nonfinite scaled integration error");
            // Nonzero q norm is a torque-free kinematic invariant. An invalid
            // trial is a hard failure, even when its error would reject the step.
            Quaternion projected;
            try { projected=normalize_quaternion({trial[6],trial[7],trial[8],trial[9]}); }
            catch (const std::invalid_argument&) { throw std::runtime_error("invalid trial quaternion"); }
            const bool accepted=e<=1;
            proposed_h=detail::next_step(h,e,accepted,o.max_step_s);
            if (!accepted) continue; // Keep y, dy and t unchanged; do not project trial.
            const bool changed=projected.w!=trial[6]||projected.x!=trial[7]||projected.y!=trial[8]||projected.z!=trial[9];
            trial[6]=projected.w; trial[7]=projected.x; trial[8]=projected.y; trial[9]=projected.z;
            const double accepted_time=h==remaining?target:endpoint;
            if (changed||accepted_time!=endpoint) system(trial,trial_derivative,accepted_time);
            y=trial; dy=trial_derivative; t=accepted_time;
        }
        result.samples.push_back({{target},detail::unpack(y)});
    }
    return result;
}
}
