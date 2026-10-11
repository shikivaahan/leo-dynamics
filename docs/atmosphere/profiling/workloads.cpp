// Real-workload capture and locality-workload driver for ATM-PROF-001.
//
// Measurement only. Nothing here changes the scientific configuration: the
// propagation runs the production Dopri5 loop against the same case_dynamics,
// the same spec.start(), the same project tolerances() and the same output
// labels as docs/atmosphere/experiment.cpp `execute`, and every density comes
// from a fresh call into the selected reference model. The original experiment
// translation unit is included verbatim (its `main` renamed) so those helpers
// are literally the same code, not a re-derivation of them.
//
// Two workloads are produced:
//   * real propagation: CASE-A/B/C, both native models, --profile-batches
//     independent six-hour Dopri5 runs each. Per RHS call the external Sun
//     ephemeris call, the full provider call and the whole RHS are timed on
//     monotonic steady_clock. Preallocated capture is outside the provider/Sun
//     intervals but remains inside total RHS/propagation timing.
//     No JSON or I/O happens inside a timed region.
//   * controlled locality: deterministic request schedules (>= 1000 per mode)
//     that isolate epoch reuse, position movement, weather-record boundaries and
//     schedule ordering, timed the same way and both models.
//
// The same source builds twice: linked against the unmodified production
// libraries ("original") and against the generated instrumented copies
// ("instrumented"). Scientific results must be identical between the two;
// `result_hash` and the trajectory checksums are computed over results only,
// never over timings, so that comparison is meaningful.

#define main experiment_main_renamed_by_workloads
#include "../experiment.cpp"
#undef main

#include <ctime>

// Both builds use caller-owned stage storage. Unmodified production functions
// never write diagnostic records; generated diagnostic copies do so only while
// the caller's thread-local Record is bound.
#include "profile.hpp"
#include <string>
#include <vector>

namespace {

// Request-sequence capacity. A six-hour Dopri5 run at the frozen settings makes
// on the order of 10^4 provider requests; this is deliberately an order of
// magnitude larger so that no measured run can reallocate inside a timed region.
// Overflow is counted and reported, never silently absorbed.
constexpr std::size_t request_capacity = 1u << 21;

double cpu_seconds() {
    timespec now{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
    return double(now.tv_sec) + 1e-9 * double(now.tv_nsec);
}

double wall_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::nano>(b - a).count();
}

std::string quote_json(const std::string &text) {
    std::string out = "\"";
    for (char ch : text) {
        if (ch == '"' || ch == '\\')
            out += '\\';
        out += ch;
    }
    return out + '"';
}

// The included experiment translation unit already defines numbers_json, and
// both files share one anonymous namespace, so these carry distinct names.
std::string numbers_array_json(const std::vector<double> &values) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ',';
        out << values[i];
    }
    return out.str() + ']';
}

std::string string_array_json(const std::vector<std::string> &values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ',';
        out << quote_json(values[i]);
    }
    return out.str() + ']';
}

// ---------------------------------------------------------------------------
// Distribution with the documented estimator: exact order statistics, mean,
// p95/p99 by linear interpolation between closest ranks (the same estimator the
// Python reducer uses, so C++ and Python agree digit for digit on the same raw
// samples). Raw samples are always retained alongside the summary.
// ---------------------------------------------------------------------------
struct Summary {
    std::size_t count = 0;
    double minimum = 0, median = 0, mean = 0, p95 = 0, p99 = 0, maximum = 0;

    static double quantile_sorted(const std::vector<double> &sorted, double q) {
        if (sorted.empty())
            return 0;
        if (sorted.size() == 1)
            return sorted.front();
        const double position = q * double(sorted.size() - 1);
        const auto lower = std::size_t(std::floor(position));
        const auto upper = std::min(lower + 1, sorted.size() - 1);
        const double weight = position - double(lower);
        return sorted[lower] * (1 - weight) + sorted[upper] * weight;
    }
    static Summary of(std::vector<double> values) {
        Summary s;
        s.count = values.size();
        if (values.empty())
            return s;
        std::sort(values.begin(), values.end());
        long double total = 0;
        for (double v : values) {
            if (!std::isfinite(v))
                throw std::runtime_error("nonfinite timing sample");
            total += v;
        }
        s.mean = static_cast<double>(total / static_cast<long double>(values.size()));
        s.minimum = values.front();
        s.maximum = values.back();
        s.median = quantile_sorted(values, 0.5);
        s.p95 = quantile_sorted(values, 0.95);
        s.p99 = quantile_sorted(values, 0.99);
        return s;
    }
    // include_raw is used for the controlled locality schedules, whose request
    // count is bounded and small enough that every per-stage sample can be kept
    // in the file itself. The propagation workloads keep their raw per-request
    // timings in the request-sequence .jsonl instead.
    std::string json(bool include_raw = false, const std::vector<double> *raw = nullptr) const {
        Json j;
        j.add("count", count);
        j.add("minimum", minimum);
        j.add("median", median);
        j.add("mean", mean);
        j.add("p95", p95);
        j.add("p99", p99);
        j.add("maximum", maximum);
        j.add("units", std::string("nanoseconds"));
        j.add("estimator", std::string("exact order statistics; p95/p99 by linear interpolation "
                                       "between closest ranks at q*(n-1)"));
        if (include_raw && raw)
            j.raw("raw_nanoseconds", numbers_array_json(*raw));
        return j.str();
    }
};

struct RequestRecord {
    double epoch_et = 0, x = 0, y = 0, z = 0, rhs_ns = 0, provider_ns = 0, sun_ns = 0;
};
struct RequestLog {
    std::vector<RequestRecord> rows;
    std::size_t dropped = 0;
    void reset() {
        rows.clear();
        dropped = 0;
    }
    std::size_t push(const RequestRecord &row) {
        if (rows.size() == rows.capacity()) {
            ++dropped;
            throw std::runtime_error("request capacity exceeded");
        }
        rows.push_back(row);
        return rows.size() - 1;
    }
};
void reserve_request_log(RequestLog &log) { log.rows.reserve(request_capacity); }
std::string requests_jsonl(const RequestLog &log) {
    std::ostringstream out;
    out << std::setprecision(17);
    for (const auto &r : log.rows) {
        const auto u = leo::SpiceEphemerisProvider::utc_calendar({r.epoch_et});
        out << '[' << r.epoch_et << ',' << r.x << ',' << r.y << ',' << r.z << ',' << r.rhs_ns << ','
            << r.provider_ns << ',' << r.sun_ns << ',' << u.year << ',' << u.month << ',' << u.day
            << ',' << u.day_of_year << ',' << u.seconds_of_day << ',' << u.day_length_seconds << ','
            << u.days_since_1970 << ',' << u.mjd << "]\n";
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// Diagnostic stage capture.
//
// The recorder owns one preallocated atm_profile::Record and points
// atm_profile::active at it for exactly the duration of one provider call,
// then clears it again immediately. Clearing it matters: a record left active
// would silently absorb unrelated SPICE or native work from anywhere else in
// the process and attribute it to the provider.
//
// Per-request stage nanoseconds are kept in preallocated rows so the summary
// can report a real per-call distribution per stage, not just a total. No I/O
// and no JSON happens inside a recorded region.
// ---------------------------------------------------------------------------
class StageRecorder {
    // Live per-request record. atm_profile::active points here for exactly one
    // provider call; its contents are snapshotted and cleared after each one.
    atm_profile::Record live_{};
    // Batch aggregates, never cleared by commit().
    std::array<double, atm_profile::count> total_ns_{};
    std::array<unsigned, atm_profile::count> total_calls_{};
    std::vector<std::array<double, atm_profile::count>> rows_;
    std::size_t recorded_ = 0, dropped_ = 0;
    unsigned iterations_ = 0;

  public:
    explicit StageRecorder(std::size_t capacity) { rows_.reserve(capacity); }

    // Scope guard: active during construction, cleared on destruction even if
    // the provider call throws.
    struct Scope {
        StageRecorder &owner;
        explicit Scope(StageRecorder &value) : owner(value) { atm_profile::active = &owner.live_; }
        ~Scope() { atm_profile::active = nullptr; }
    };

    // Called after the scope closes: fold this request's stage totals into the
    // batch aggregates and keep the per-request row for the distributions.
    // Outside the provider interval; counted in total RHS/propagation timing.
    void commit() {
        for (std::size_t id = 0; id < atm_profile::count; ++id) {
            total_ns_[id] += live_.ns[id];
            total_calls_[id] += live_.calls[id];
        }
        iterations_ += live_.iterations;
        if (rows_.size() == rows_.capacity()) {
            ++dropped_;
        } else {
            rows_.push_back(live_.ns);
        }
        live_ = atm_profile::Record{};
        ++recorded_;
    }

    void reset() {
        live_ = atm_profile::Record{};
        total_ns_.fill(0);
        total_calls_.fill(0);
        rows_.clear();
        recorded_ = 0;
        dropped_ = 0;
        iterations_ = 0;
        atm_profile::active = nullptr;
    }

    // Batch stage means over the recorded requests plus per-stage per-call
    // distributions. Nothing is rounded away and nothing is dropped.
    // include_raw controls whether every per-call stage sample is written into
    // this file. The locality schedules enable it: their request count is
    // bounded and the samples are the evidence. The propagation workloads leave
    // it off: propagation retains per-stage distribution summaries here and
    // per-request outer provider/RHS/Sun samples in the request-sequence JSONL.
    std::string json(bool include_raw = false) const {
        Json j;
        j.add("recorded_requests", recorded_);
        j.add("dropped_over_capacity", dropped_);
        j.add("total_iterations", static_cast<std::size_t>(iterations_));
        j.add(
            "note",
            std::string(
                "atm_profile::active is bound to this batch's preallocated Record for exactly one "
                "provider call and cleared immediately afterwards; stages are inclusive and "
                "nested, "
                "so they are not additive"));
        j.raw("stage_names", [&] {
            std::ostringstream list;
            list << '[';
            for (std::size_t id = 0; id < atm_profile::count; ++id) {
                if (id)
                    list << ',';
                list << quote_json(std::string(atm_profile::names[id]));
            }
            return list.str() + ']';
        }());
        j.raw("stage_calls", [&] {
            std::ostringstream list;
            list << '[';
            for (std::size_t id = 0; id < atm_profile::count; ++id) {
                if (id)
                    list << ',';
                list << total_calls_[id];
            }
            return list.str() + ']';
        }());
        j.raw("stage_mean_nanoseconds", [&] {
            std::ostringstream list;
            list << '[';
            for (std::size_t id = 0; id < atm_profile::count; ++id) {
                if (id)
                    list << ',';
                const double mean = recorded_ ? double(total_ns_[id]) / double(recorded_) : 0.0;
                list << mean;
            }
            return list.str() + ']';
        }());
        j.raw("stage_per_call_nanoseconds", [&] {
            std::ostringstream list;
            list << '[';
            bool first_stage = true;
            for (std::size_t id = 0; id < atm_profile::count; ++id) {
                if (total_calls_[id] == 0)
                    continue;
                std::vector<double> samples;
                samples.reserve(rows_.size());
                for (const auto &row : rows_)
                    if (row[id] > 0)
                        samples.push_back(row[id]);
                if (samples.empty())
                    continue;
                if (!first_stage)
                    list << ',';
                first_stage = false;
                Json stage;
                stage.add("stage", std::string(atm_profile::names[id]));
                // Every per-call sample is retained: the request count per
                // batch is bounded and the samples are the evidence.
                stage.raw("summary", Summary::of(samples).json(include_raw, &samples));
                list << stage.str();
            }
            return list.str() + ']';
        }());
        return j.str();
    }
};

// ---------------------------------------------------------------------------
// Real propagation, instrumented.
//
// The source snapshot below is the experiment's `execute` source lambda,
// statement for statement. The only additions are the two clock pairs around
// the Sun and provider calls and the request append, all outside the returned
// value and outside any other timed region.
// ---------------------------------------------------------------------------
struct ProfiledRun {
    Trajectory trajectory;
    std::size_t rhs_calls = 0, atmosphere_calls = 0;
    double wall_seconds = 0, cpu_seconds_total = 0;
    std::string checksum;
    Checksum hash;
};

ProfiledRun profiled_execute(const Config &c, const Environment &env, const experiment::Case &spec,
                             leo::atmosphere::Model model, leo::Epoch epoch, RequestLog &log,
                             StageRecorder &stages) {
    const auto dynamics = case_dynamics(spec);
    const State start = spec.start();
    const Earth earth;
    const auto atmosphere =
        leo::atmosphere::DirectAtmosphere(model, env.ephemeris, env.weather, c.frame, c.ap_history);
    const auto labels = sample_labels(c);

    ProfiledRun run;
    std::size_t atmosphere_calls = 0;
    // Index of the request row the current RHS call recorded, so the outer RHS
    // timing is stamped on exactly that row instead of on whatever is last.
    std::size_t current_row = std::size_t(-1);
    auto source = [&](ElapsedTime t, const State &s) {
        const leo::Epoch at{epoch.et_seconds + t.seconds};
        leo::EnvironmentSnapshot snapshot;
        snapshot.epoch = at;
        // Existing atmospheric corotation assumption, unchanged.
        snapshot.atmosphere_angular_velocity_rad_s = InertialVector{0, 0, 7.292115e-5};

        RequestRecord row;
        row.epoch_et = at.et_seconds;
        row.x = s.r.x();
        row.y = s.r.y();
        row.z = s.r.z();

        const auto sun_start = Clock::now();
        const auto sun = env.ephemeris.geometric_state(leo::sun_naif_id, at);
        const auto sun_end = Clock::now();
        snapshot.sun_position_m = sun.position_m;
        snapshot.illumination = illumination_factor(EclipseModel::conical, s.r, sun.position_m,
                                                    earth.equatorial_radius_m);

        const auto provider_start = Clock::now();
        leo::atmosphere::Result result;
        {
            // The diagnostic stages cover exactly the provider call and
            // nothing else; the guard clears the binding again immediately.
            StageRecorder::Scope scope(stages);
            result = atmosphere.evaluate(at, s.r);
        }
        const auto provider_end = Clock::now();
        if (!std::isfinite(result.density_kg_m3) || result.density_kg_m3 < 0)
            throw std::runtime_error("nonfinite or negative atmosphere density");
        snapshot.density_kg_m3 = result.density_kg_m3;
        ++atmosphere_calls;

        // Outside every timed region above: bookkeeping only.
        row.sun_ns = wall_between(sun_start, sun_end);
        row.provider_ns = wall_between(provider_start, provider_end);
        stages.commit();
        current_row = log.push(row);
        run.hash.add(result.density_kg_m3);
        return snapshot;
    };

    IntegrationOptions options = tolerances();
    options.rtol = c.rtol;
    options.max_step_s = c.max_step;
    options.initial_step_s = std::min(1.0, c.max_step);
    options.max_attempts = IntegrationOptions{}.max_attempts;

    std::size_t rhs_calls = 0;
    auto system = [&](ElapsedTime t, const State &s) {
        ++rhs_calls;
        const auto start = Clock::now();
        auto derivative = dynamics(t, s, source(t, s));
        const auto end = Clock::now();
        if (current_row != std::size_t(-1))
            log.rows[current_row].rhs_ns = wall_between(start, end);
        return derivative;
    };

    const auto timer = Clock::now();
    const double cpu_start = cpu_seconds();
    run.trajectory = propagate(system, start, epoch, labels, options);
    run.cpu_seconds_total = cpu_seconds() - cpu_start;
    run.wall_seconds = elapsed(timer);
    run.rhs_calls = rhs_calls;
    run.atmosphere_calls = atmosphere_calls;

    Checksum checksum;
    for (const auto &sample : run.trajectory.samples) {
        checksum.add(sample.state);
        run.hash.add(sample.state);
    }
    run.checksum = checksum.str();
    return run;
}

std::string trajectory_json(const RunRecord &record) {
    Checksum checksum;
    std::ostringstream series;
    series << std::setprecision(17) << '[';
    for (std::size_t index = 0; index < record.trajectory.samples.size(); ++index) {
        const auto &sample = record.trajectory.samples[index];
        checksum.add(sample.state);
        if (index)
            series << ',';
        series << '[' << sample.t.seconds;
        for (double value : leo::detail::pack(sample.state))
            series << ',' << value;
        series << ',' << record.altitude[index] << ',' << record.density[index] << ','
               << record.drag_acceleration[index] << ',' << record.drag_torque[index] << ','
               << record.cumulative_drag_work[index] << ','
               << record.cumulative_non_gravitational_work[index] << ','
               << record.matched_energy_change[index] << ',' << record.attitude_angle[index] << ','
               << record.angular_momentum_norm[index] << ',' << record.angular_speed[index] << ','
               << record.total_torque_norm[index] << ']';
    }
    series << ']';
    Json j;
    j.add("kind", "profiling-trajectory");
    j.add("case_id", record.case_id);
    j.add("atmosphere_model", record.model);
    j.add("integrator", std::string("dopri5 (production leo::propagate)"));
    j.add("trajectory_checksum", checksum.str());
    j.add("purpose", std::string("packed state samples for direct-v2 comparison; produced by "
                                 "the unmodified experiment execute()"));
    j.raw("samples", series.str());
    j.raw("samples_fields",
          R"(["time_s","x_m","y_m","z_m","vx_m_s","vy_m_s","vz_m_s","qw","qx","qy","qz",)"
          R"("omega_x_rad_s","omega_y_rad_s","omega_z_rad_s","altitude_m","density_kg_m3",)"
          R"("drag_acceleration_m_s2","drag_torque_nm","cumulative_drag_work_j",)"
          R"("cumulative_non_gravitational_work_j","matched_specific_energy_change_j_kg",)"
          R"("attitude_angle_rad","angular_momentum_norm_error_kg_m2_s","angular_speed_rad_s",)"
          R"("total_torque_norm_nm"])");
    j.add("sample_count", static_cast<double>(record.trajectory.samples.size()));
    return j.str();
}

// The unmodified experiment run, reported as-is: every scientific verification
// number in this file comes from that function, not from a reimplementation.
std::string production_checksum(const RunRecord &record) {
    Checksum checksum;
    for (const auto &sample : record.trajectory.samples)
        checksum.add(sample.state);
    return checksum.str();
}

// Scientific verification outputs for one case/model, taken from the
// unmodified experiment `execute` run and compared against the instrumented
// profiled run of the same configuration. A difference here would mean the
// capture changed the science, which is exactly what must never happen.
std::string scientific_json(const Config &c, const RunRecord &record, const ProfiledRun &profiled) {
    Json j;
    j.add("kind", "profiling-scientific-verification");
    j.add("case_id", record.case_id);
    j.add("case_file", record.case_file);
    j.add("atmosphere_model", record.model);
    j.add("integrator", std::string("dopri5 (production leo::propagate)"));
    j.add("epoch_et_seconds", record.epoch.et_seconds);
    j.add("epoch_utc", utc_label(record.epoch));
    j.add("duration_s", c.duration);
    j.add("output_cadence_s", c.cadence);
    j.add("rtol", c.rtol);
    j.add("max_step_s", c.max_step);
    j.add("initial_step_s", std::min(1.0, c.max_step));
    j.add("position_atol_m", 1e-3);
    j.add("velocity_atol_m_s", 1e-6);
    j.add("quaternion_atol", 1e-12);
    j.add("omega_atol_rad_s", 1e-12);
    j.add("max_attempts", static_cast<double>(IntegrationOptions{}.max_attempts));
    j.add("gravity", std::string("J2 enabled; symmetry axis fixed J2000 +Z"));
    j.add("drag_model", std::string("sentman"));
    j.add("solar_illumination_model", std::string("conical"));
    j.add("atmosphere_rotation_rad_s",
          std::string("0,0,7.292115e-5 (existing corotation assumption)"));
    j.add("output_sample_count", static_cast<double>(record.trajectory.samples.size()));
    j.add("rhs_calls", record.rhs_calls);
    j.add("provider_calls", record.atmosphere_calls);
    j.add("wall_seconds", record.wall_seconds);
    j.add("density_min_kg_m3", *std::min_element(record.density.begin(), record.density.end()));
    j.add("density_max_kg_m3", *std::max_element(record.density.begin(), record.density.end()));
    j.add("density_mean_kg_m3", std::accumulate(record.density.begin(), record.density.end(), 0.0) /
                                    double(record.density.size()));
    j.add("drag_acceleration_max_m_s2",
          *std::max_element(record.drag_acceleration.begin(), record.drag_acceleration.end()));
    j.add("drag_torque_max_nm",
          *std::max_element(record.drag_torque.begin(), record.drag_torque.end()));
    j.add("altitude_min_m", *std::min_element(record.altitude.begin(), record.altitude.end()));
    j.add("altitude_max_m", *std::max_element(record.altitude.begin(), record.altitude.end()));
    j.add("cumulative_drag_work_j", record.cumulative_drag_work_final);
    j.add("cumulative_non_gravitational_work_j", record.cumulative_non_gravitational_work_final);
    j.add("matched_specific_energy_change_j_kg", record.matched_energy_change_final);
    j.add("energy_work_residual_j_kg", record.energy_work_residual);
    j.add("max_quaternion_norm_error", record.max_quaternion_norm_error);
    j.add("attitude_angle_from_initial_final_rad", record.attitude_angle.back());
    j.add("final_angular_momentum_norm_error_kg_m2_s", record.angular_momentum_norm.back());
    const auto checksum = production_checksum(record);
    j.add("trajectory_checksum", checksum);
    j.add("profiled_trajectory_checksum", profiled.checksum);
    j.add("profiled_trajectory_checksum_matches_production", checksum == profiled.checksum);
    j.add("profiled_rhs_calls", profiled.rhs_calls);
    j.add("profiled_provider_calls", profiled.atmosphere_calls);
    // Paired capture / no-capture wall comparison. The no-capture run is the
    // unmodified execute() above; the difference is the cost the capture itself
    // adds, and it is reported rather than subtracted away.
    j.add("no_capture_wall_seconds", record.wall_seconds);
    j.add("capture_wall_seconds", profiled.wall_seconds);
    j.add("capture_wall_overhead_seconds", profiled.wall_seconds - record.wall_seconds);
    j.add("capture_wall_overhead_fraction",
          record.wall_seconds ? (profiled.wall_seconds - record.wall_seconds) / record.wall_seconds
                              : 0.0);
    j.add("capture_overhead_note",
          std::string(
              "request capture adds one clock pair per Sun, provider and RHS boundary plus a "
              "preallocated store per request; this overhead is measured, not corrected for"));
    if (checksum != profiled.checksum)
        throw std::runtime_error("request capture changed the propagated trajectory for " +
                                 record.case_id + "/" + record.model);
    const auto &final = record.trajectory.samples.back().state;
    j.vector("final_position_m", final.r);
    j.vector("final_velocity_m_s", final.v);
    j.vector("final_omega_body_rad_s", final.omega);
    std::ostringstream quaternion;
    quaternion << std::setprecision(17) << '[' << final.q.w << ',' << final.q.x << ',' << final.q.y
               << ',' << final.q.z << ']';
    j.raw("final_quaternion", quaternion.str());
    return j.str();
}

// ---------------------------------------------------------------------------
// Controlled locality schedules
// ---------------------------------------------------------------------------
struct ScheduledRequest {
    double epoch_et = 0;
    Eigen::Vector3d position{Eigen::Vector3d::Zero()};
};

// Real geodetic Earth-fixed location -> J2000, using the real WGS84 ellipsoid
// and the real Earth-fixed rotation. Every position here is prepared outside
// all timed regions; only the provider call itself is timed.
Eigen::Vector3d scheduled_position(leo::Epoch epoch, double latitude_deg, double longitude_deg,
                                   double altitude_km, const std::string &frame) {
    const Eigen::Vector3d fixed = leo::atmosphere::earth_fixed(
        leo::atmosphere::Geodetic{latitude_deg * std::numbers::pi / 180.0,
                                  longitude_deg * std::numbers::pi / 180.0, altitude_km * 1e3});
    const Eigen::Matrix3d to_fixed =
        leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, frame);
    return to_fixed.transpose() * fixed;
}

struct LocalityMode {
    std::string name, description;
    std::vector<ScheduledRequest> requests;
};

std::vector<ScheduledRequest> make_mode(const std::string &name, leo::Epoch base,
                                        const std::string &frame) {
    std::vector<ScheduledRequest> requests;
    constexpr std::size_t total = 1024;
    const double day = 86400.0;

    if (name == "fixed") {
        // One epoch, one position, repeated: the pure reuse limit. The
        // position is prepared once, outside every timed region.
        const Eigen::Vector3d position = scheduled_position(base, 45.0, 10.0, 400.0, frame);
        for (std::size_t i = 0; i < total; ++i)
            requests.push_back({base.et_seconds, position});
    } else if (name == "same_epoch_changing_positions") {
        // Epoch constant, position walks a nearby real grid: isolates the cost
        // of a fresh position with identical time-dependent inputs.
        for (std::size_t i = 0; i < total; ++i) {
            const double latitude = 44.0 + 0.5 * double(i % 32) / 31.0;
            const double longitude = 9.0 + 0.5 * double(i / 32) / 31.0;
            requests.push_back(
                {base.et_seconds, scheduled_position(base, latitude, longitude, 400.0, frame)});
        }
    } else if (name == "same_position_changing_epochs") {
        // Same J2000 position, changing ET. Earth-fixed geometry still rotates;
        // daily weather record identities may remain unchanged.
        const Eigen::Vector3d position = scheduled_position(base, 45.0, 10.0, 400.0, frame);
        for (std::size_t i = 0; i < total; ++i)
            requests.push_back({base.et_seconds + 10.0 * double(i), position});
    } else if (name == "nearby_positions_epochs") {
        // Small step in both epoch and position: the regime a propagator
        // actually produces between consecutive RHS calls.
        for (std::size_t i = 0; i < total; ++i) {
            const double t = base.et_seconds + double(i);
            requests.push_back({t, scheduled_position(leo::Epoch{t}, 45.0 + 1e-4 * double(i % 17),
                                                      10.0 + 1e-4 * double(i % 11), 400.0, frame)});
        }
    } else if (name == "identical_daily_records_changing_geometry") {
        // One unchanged UTC day and daily driver record set; both time and geometry vary.
        for (std::size_t i = 0; i < total; ++i) {
            const double t = base.et_seconds + double(i);
            requests.push_back({t, scheduled_position(leo::Epoch{t}, 44.0 + double(i % 32) / 64,
                                                      9.0 + double(i / 32) / 64, 400.0, frame)});
        }
    } else if (name == "weather_3h_boundary") {
        // Minute steps straight through a three-hour weather-record boundary,
        // repeated on many days.
        const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(base);
        const double seconds_into_day = calendar.seconds_of_day;
        const double boundary = 10800.0 * (std::floor(seconds_into_day / 10800.0) + 1);
        for (std::size_t i = 0; i < total; ++i) {
            const double day_offset = day * double(i / 128);
            const double t = base.et_seconds + (boundary - seconds_into_day) + day_offset +
                             30.0 * double(i % 128) - 1920.0;
            requests.push_back({t, scheduled_position(leo::Epoch{t}, 45.0, 10.0, 400.0, frame)});
        }
    } else if (name == "weather_hour_boundary") {
        // The same sweep at hourly DTCFILE resolution.
        const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(base);
        const double seconds_into_day = calendar.seconds_of_day;
        const double boundary = 3600.0 * (std::floor(seconds_into_day / 3600.0) + 1);
        for (std::size_t i = 0; i < total; ++i) {
            const double day_offset = day * double(i / 128);
            const double t = base.et_seconds + (boundary - seconds_into_day) + day_offset +
                             15.0 * double(i % 128) - 960.0;
            requests.push_back({t, scheduled_position(leo::Epoch{t}, 45.0, 10.0, 400.0, frame)});
        }
    } else if (name == "utc_midnight") {
        // Minute steps through UTC midnight: the calendar conversion path with
        // a real date change underneath it.
        const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(base);
        const double seconds_into_day = calendar.seconds_of_day;
        for (std::size_t i = 0; i < total; ++i) {
            const double day_offset = day * double(i / 128);
            const double t = base.et_seconds + (86400.0 - seconds_into_day) + day_offset +
                             60.0 * double(i % 128) - 3840.0;
            requests.push_back({t, scheduled_position(leo::Epoch{t}, 45.0, 10.0, 400.0, frame)});
        }
    } else if (name == "ensemble_trajectory_major") {
        // Eight independent nearby trajectories, traversed one after another.
        for (std::size_t trajectory = 0; trajectory < 8; ++trajectory) {
            for (std::size_t step = 0; step < 128; ++step) {
                const double t = base.et_seconds + 15.0 * double(step);
                requests.push_back(
                    {t, scheduled_position(leo::Epoch{t}, 45.0 + 0.2 * double(trajectory),
                                           10.0 + 0.2 * double(trajectory), 400.0, frame)});
            }
        }
    } else if (name == "ensemble_epoch_major") {
        // The identical request set, ordered epoch-major: same inputs, same
        // count, different interleaving.
        for (std::size_t step = 0; step < 128; ++step) {
            for (std::size_t trajectory = 0; trajectory < 8; ++trajectory) {
                const double t = base.et_seconds + 15.0 * double(step);
                requests.push_back(
                    {t, scheduled_position(leo::Epoch{t}, 45.0 + 0.2 * double(trajectory),
                                           10.0 + 0.2 * double(trajectory), 400.0, frame)});
            }
        }
    } else {
        throw std::invalid_argument("unknown locality mode: " + name);
    }
    if (requests.size() < 1000)
        throw std::runtime_error("locality mode " + name + " has fewer than 1000 requests");
    for (const auto &request : requests)
        if (!std::isfinite(request.epoch_et) || !request.position.allFinite())
            throw std::runtime_error("locality mode " + name + " produced a non-finite request");
    return requests;
}

const std::vector<std::string> &locality_mode_names() {
    static const std::vector<std::string> names{"fixed",
                                                "same_epoch_changing_positions",
                                                "same_position_changing_epochs",
                                                "nearby_positions_epochs",
                                                "identical_daily_records_changing_geometry",
                                                "weather_3h_boundary",
                                                "weather_hour_boundary",
                                                "utc_midnight",
                                                "ensemble_trajectory_major",
                                                "ensemble_epoch_major"};
    return names;
}

const std::string &locality_description(const std::string &name) {
    static const std::map<std::string, std::string> descriptions{
        {"fixed", "one epoch and one position, repeated: the maximum-reuse limit"},
        {"same_epoch_changing_positions",
         "epoch constant, position walks a nearby real geodetic grid"},
        {"same_position_changing_epochs", "position constant, epoch advances 10 s per request"},
        {"nearby_positions_epochs",
         "epoch advances 1 s and position shifts by ~10 m, as consecutive RHS calls do"},
        {"identical_daily_records_changing_geometry",
         "one unchanged UTC day and daily record set, changing epoch and geodetic grid"},
        {"weather_3h_boundary", "30 s steps across three-hour weather-record boundaries"},
        {"weather_hour_boundary", "15 s steps across hourly DTCFILE boundaries"},
        {"utc_midnight", "60 s steps across UTC midnight, so the UTC date changes mid-run"},
        {"ensemble_trajectory_major",
         "eight independent nearby trajectories, each traversed completely before the next"},
        {"ensemble_epoch_major", "the same eight trajectories interleaved epoch-major"}};
    const auto found = descriptions.find(name);
    if (found == descriptions.end())
        throw std::invalid_argument("unknown locality mode: " + name);
    return found->second;
}

struct LocalityResult {
    std::vector<double> nanoseconds;
    Checksum hash;
    double wall_seconds = 0, cpu_seconds_total = 0;
    std::size_t calls = 0;
    double first_density = 0, last_density = 0;
    std::string utc_rows;
};

LocalityResult run_locality(const Config &c, const Environment &env, leo::atmosphere::Model model,
                            const LocalityMode &mode, bool retain_raw, StageRecorder &stages) {
    const auto atmosphere =
        leo::atmosphere::DirectAtmosphere(model, env.ephemeris, env.weather, c.frame, c.ap_history);

    // Warm-up outside every measured region: 256 fresh provider calls, exactly
    // as the propagation workload does, never recorded.
    for (std::size_t warm = 0; warm < 256; ++warm)
        (void)atmosphere.evaluate(leo::Epoch{mode.requests.front().epoch_et},
                                  InertialVector{mode.requests.front().position});

    LocalityResult result;
    result.nanoseconds.reserve(mode.requests.size());
    std::vector<leo::atmosphere::Result> retained(mode.requests.size());
    std::ostringstream utc;
    utc << std::setprecision(17);

    const auto timer = Clock::now();
    const double cpu_start = cpu_seconds();
    for (std::size_t index = 0; index < mode.requests.size(); ++index) {
        const auto &request = mode.requests[index];
        const leo::Epoch epoch{request.epoch_et};
        const InertialVector position{request.position};

        const auto start = Clock::now();
        leo::atmosphere::Result evaluated;
        {
            StageRecorder::Scope scope(stages);
            evaluated = atmosphere.evaluate(epoch, position);
        }
        const auto end = Clock::now();

        const double ns = wall_between(start, end);
        if (!std::isfinite(ns))
            throw std::runtime_error("nonfinite locality timing");
        result.nanoseconds.push_back(ns);
        result.hash.add(evaluated.density_kg_m3);
        result.hash.add(evaluated.temperature_k);
        result.hash.add(evaluated.exospheric_temperature_k);
        if (!std::isfinite(evaluated.density_kg_m3) || evaluated.density_kg_m3 < 0)
            throw std::runtime_error("locality workload produced an invalid density");
        if (index == 0)
            result.first_density = evaluated.density_kg_m3;
        result.last_density = evaluated.density_kg_m3;

        stages.commit();
        if (retain_raw)
            retained[index] = evaluated;
    }
    result.cpu_seconds_total = cpu_seconds() - cpu_start;
    result.wall_seconds = elapsed(timer);
    result.calls = mode.requests.size();
    if (retain_raw)
        for (std::size_t index = 0; index < mode.requests.size(); ++index) {
            const auto &request = mode.requests[index];
            const auto &evaluated = retained[index];
            const auto calendar = leo::SpiceEphemerisProvider::utc_calendar({request.epoch_et});
            const int trajectory = mode.name == "ensemble_trajectory_major" ? int(index / 128)
                                   : mode.name == "ensemble_epoch_major"    ? int(index % 8)
                                                                            : -1;
            utc << '[' << request.epoch_et << ',' << request.position.x() << ','
                << request.position.y() << ',' << request.position.z() << ',' << calendar.year
                << ',' << calendar.month << ',' << calendar.day << ',' << calendar.day_of_year
                << ',' << calendar.seconds_of_day << ',' << calendar.day_length_seconds << ','
                << calendar.days_since_1970 << ',' << calendar.mjd << ',' << evaluated.density_kg_m3
                << ',' << evaluated.temperature_k << ',' << evaluated.exospheric_temperature_k
                << ',' << (evaluated.has_temperatures ? 1 : 0) << ','
                << (evaluated.has_constituents ? 1 : 0) << ',' << trajectory << "]\n";
        }
    result.utc_rows = utc.str();
    return result;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

struct WorkloadOptions {
    std::filesystem::path out;
    std::size_t batches = 3;
};

// Own CLI filter: --profile-out/--profile-batches are consumed here and every
// other argument is handed to the experiment's own parse_arguments unchanged,
// so the scientific configuration cannot drift between the two executables.
WorkloadOptions split_arguments(int argc, char **argv, std::vector<char *> &forwarded) {
    WorkloadOptions options;
    bool have_out = false;
    forwarded.emplace_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 == argc)
                throw std::invalid_argument("missing value for " + key);
            return argv[++i];
        };
        if (key == "--profile-out") {
            options.out = value();
            have_out = true;
        } else if (key == "--profile-batches") {
            const std::string raw = value();
            std::size_t used = 0;
            const long long parsed = std::stoll(raw, &used);
            if (used != raw.size() || parsed <= 0)
                throw std::invalid_argument("invalid --profile-batches: " + raw);
            options.batches = static_cast<std::size_t>(parsed);
        } else {
            forwarded.emplace_back(argv[i]);
        }
    }
    if (!have_out)
        throw std::invalid_argument("--profile-out is required");
    return options;
}

} // namespace

int main(int argc, char **argv) {
    try {
        std::vector<char *> forwarded;
        const WorkloadOptions options = split_arguments(argc, argv, forwarded);
        const Config c = parse_arguments(static_cast<int>(forwarded.size()), forwarded.data());
        if (c.mode != "propagate")
            throw std::invalid_argument("profile workloads require --mode propagate");
        if (options.batches < 1)
            throw std::invalid_argument("--profile-batches must be positive");

        const std::filesystem::path out(options.out);
        prepare_output_directory(out);

        const auto load_timer = Clock::now();
        Environment env(c);
        const double environment_seconds = elapsed(load_timer);

        const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
        std::vector<leo::atmosphere::Model> models;
        if (compiled_in(leo::atmosphere::Model::nrlmsise00))
            models.push_back(leo::atmosphere::Model::nrlmsise00);
        if (compiled_in(leo::atmosphere::Model::jb2008))
            models.push_back(leo::atmosphere::Model::jb2008);
        if (models.size() != 2)
            throw std::runtime_error(
                "both frozen native backends (nrlmsise00, jb2008) must be compiled in; "
                "a reduced model set would be a narrower benchmark than the design requires");

        RequestLog log;
        reserve_request_log(log);
        // One recorder per workload, sized for the same capacity as the request
        // log so no recorded run reallocates inside a timed region.
        StageRecorder propagation_stages(request_capacity);
        StageRecorder locality_stages(request_capacity);

        std::vector<std::filesystem::path> written;

        // ------------------------------------------------------------------
        // Real propagation workload
        // ------------------------------------------------------------------
        std::ostringstream propagation_rows;
        propagation_rows << std::setprecision(17) << '[';
        bool first_row = true;
        const auto propagation_start = Clock::now();

        for (const auto &case_file : c.case_files) {
            const auto spec = experiment::load(case_file);
            for (const auto model : models) {
                const std::string tag = spec.id + "_" + model_name(model);

                // The unmodified experiment run: scientific verification
                // outputs and the production trajectory checksum.
                const RunRecord production = execute(c, env, spec, model, "dopri", epoch);

                std::vector<std::string> batch_checksums;
                for (std::size_t batch = 0; batch < options.batches; ++batch) {
                    // Provider warm-up of 256 fresh calls, outside every timed region.
                    {
                        const auto warm_atmosphere = leo::atmosphere::DirectAtmosphere(
                            model, env.ephemeris, env.weather, c.frame, c.ap_history);
                        const State warm_state = spec.start();
                        for (std::size_t warm = 0; warm < 256; ++warm)
                            (void)warm_atmosphere.evaluate(epoch, warm_state.r);
                    }

                    log.reset();
                    propagation_stages.reset();
                    const ProfiledRun run =
                        profiled_execute(c, env, spec, model, epoch, log, propagation_stages);
                    batch_checksums.push_back(run.checksum);

                    std::vector<double> rhs_ns, provider_ns, sun_ns;
                    rhs_ns.reserve(log.rows.size());
                    provider_ns.reserve(log.rows.size());
                    sun_ns.reserve(log.rows.size());
                    for (const auto &row : log.rows) {
                        rhs_ns.push_back(row.rhs_ns);
                        provider_ns.push_back(row.provider_ns);
                        sun_ns.push_back(row.sun_ns);
                    }

                    Json j;
                    j.add("kind", "propagation-workload-batch");
                    j.add("case_id", spec.id);
                    j.add("case_file", spec.file);
                    j.add("atmosphere_model", model_name(model));
                    j.add("batch", batch);
                    j.add("batches", options.batches);
                    j.add("warm_up_calls", static_cast<double>(256));
                    j.add("integrator", std::string("dopri5 (production leo::propagate)"));
                    j.add("duration_s", c.duration);
                    j.add("output_cadence_s", c.cadence);
                    j.add("rtol", c.rtol);
                    j.add("max_step_s", c.max_step);
                    j.add("initial_step_s", std::min(1.0, c.max_step));
                    j.add("max_attempts", static_cast<double>(IntegrationOptions{}.max_attempts));
                    j.add("rhs_calls", run.rhs_calls);
                    j.add("provider_calls", run.atmosphere_calls);
                    j.add("requests_recorded", log.rows.size());
                    j.add("requests_dropped_over_capacity", log.dropped);
                    j.add("request_capacity", static_cast<double>(request_capacity));
                    j.add("wall_seconds", run.wall_seconds);
                    j.add("cpu_seconds", run.cpu_seconds_total);
                    j.add("trajectory_checksum", run.checksum);
                    j.add("production_trajectory_checksum", production_checksum(production));
                    j.add("result_hash", run.hash.str());
                    j.raw("rhs_nanoseconds", Summary::of(rhs_ns).json());
                    j.raw("provider_call_nanoseconds", Summary::of(provider_ns).json());
                    j.raw("external_sun_call_nanoseconds", Summary::of(sun_ns).json());
                    j.raw("rhs_nanoseconds_raw", numbers_array_json(rhs_ns));
                    j.raw("provider_call_nanoseconds_raw", numbers_array_json(provider_ns));
                    j.raw("external_sun_call_nanoseconds_raw", numbers_array_json(sun_ns));
                    j.raw("diagnostic_stages", propagation_stages.json(false));
                    const std::filesystem::path path =
                        out / "propagation" / (tag + "_rep" + std::to_string(batch) + ".json");
                    save(path, j.str());
                    written.push_back(path);

                    if (batch == 0) {
                        const auto raw_path =
                            out / "propagation" / "raw" / (tag + "_requests.jsonl");
                        save(raw_path, requests_jsonl(log));
                        written.push_back(raw_path);
                        const auto science_path = out / "propagation" / (tag + "_scientific.json");
                        save(science_path, scientific_json(c, production, run));
                        written.push_back(science_path);
                        const auto trajectory_path =
                            out / "propagation" / (tag + "_trajectory.json");
                        save(trajectory_path, trajectory_json(production));
                        written.push_back(trajectory_path);
                    }

                    if (!first_row)
                        propagation_rows << ',';
                    first_row = false;
                    Json row;
                    row.add("case_id", spec.id);
                    row.add("atmosphere_model", model_name(model));
                    row.add("batch", batch);
                    row.add("rhs_calls", run.rhs_calls);
                    row.add("wall_seconds", run.wall_seconds);
                    row.add("cpu_seconds", run.cpu_seconds_total);
                    row.add("trajectory_checksum", run.checksum);
                    propagation_rows << row.str();
                }

                // Identical work repeated: the checksums must agree exactly.
                bool identical = true;
                for (const auto &checksum : batch_checksums)
                    if (checksum != batch_checksums.front())
                        identical = false;
                Json repeat;
                repeat.add("case_id", spec.id);
                repeat.add("atmosphere_model", model_name(model));
                repeat.add("repetitions", options.batches);
                repeat.raw("trajectory_checksums", string_array_json(batch_checksums));
                repeat.add("repetitions_bitwise_identical", identical);
                const std::filesystem::path repeat_path =
                    out / "propagation" / (tag + "_repetitions.json");
                save(repeat_path, repeat.str());
                written.push_back(repeat_path);
            }
        }
        const double propagation_wall = elapsed(propagation_start);
        propagation_rows << ']';

        // ------------------------------------------------------------------
        // Controlled locality workload
        // ------------------------------------------------------------------
        std::vector<LocalityMode> modes;
        for (const auto &name : locality_mode_names())
            modes.push_back({name, locality_description(name), make_mode(name, epoch, c.frame)});

        std::ostringstream locality_rows;
        locality_rows << std::setprecision(17) << '[';
        bool first_locality = true;
        const auto locality_start = Clock::now();

        for (const auto &mode : modes) {
            for (const auto model : models) {
                std::vector<std::string> batch_hashes;
                for (std::size_t batch = 0; batch < options.batches; ++batch) {
                    locality_stages.reset();
                    const LocalityResult result =
                        run_locality(c, env, model, mode, batch == 0, locality_stages);
                    batch_hashes.push_back(result.hash.str());

                    Json j;
                    j.add("kind", "locality-workload-batch");
                    j.add("mode", mode.name);
                    j.add("description", mode.description);
                    j.add("atmosphere_model", model_name(model));
                    j.add("batch", batch);
                    j.add("batches", options.batches);
                    j.add("warm_up_calls", static_cast<double>(256));
                    j.add("requests", result.calls);
                    j.add("wall_seconds", result.wall_seconds);
                    j.add("cpu_seconds", result.cpu_seconds_total);
                    j.add("first_density_kg_m3", result.first_density);
                    j.add("last_density_kg_m3", result.last_density);
                    j.add("result_hash", result.hash.str());
                    j.raw("call_nanoseconds", Summary::of(result.nanoseconds).json());
                    j.raw("call_nanoseconds_raw", numbers_array_json(result.nanoseconds));
                    j.raw("diagnostic_stages", locality_stages.json(true));
                    const std::filesystem::path path = out / "locality" /
                                                       (mode.name + "_" + model_name(model) +
                                                        "_rep" + std::to_string(batch) + ".json");
                    save(path, j.str());
                    written.push_back(path);

                    if (batch == 0) {
                        const auto raw_path = out / "locality" / "raw" /
                                              (mode.name + "_" + model_name(model) + ".jsonl");
                        save(raw_path, result.utc_rows);
                        written.push_back(raw_path);
                    }

                    if (!first_locality)
                        locality_rows << ',';
                    first_locality = false;
                    Json row;
                    row.add("mode", mode.name);
                    row.add("atmosphere_model", model_name(model));
                    row.add("batch", batch);
                    row.add("requests", result.calls);
                    row.add("wall_seconds", result.wall_seconds);
                    locality_rows << row.str();
                }
                Json repeat;
                repeat.add("mode", mode.name);
                repeat.add("atmosphere_model", model_name(model));
                repeat.add("repetitions", options.batches);
                repeat.raw("result_hashes", string_array_json(batch_hashes));
                bool identical = true;
                for (const auto &hash : batch_hashes)
                    if (hash != batch_hashes.front())
                        identical = false;
                repeat.add("repetitions_bitwise_identical", identical);
                const std::filesystem::path repeat_path =
                    out / "locality" / (mode.name + "_" + model_name(model) + "_repetitions.json");
                save(repeat_path, repeat.str());
                written.push_back(repeat_path);
            }
        }
        const double locality_wall = elapsed(locality_start);
        locality_rows << ']';

        // ------------------------------------------------------------------
        // Manifest
        // ------------------------------------------------------------------
        std::ostringstream cases;
        cases << '[';
        for (std::size_t i = 0; i < c.case_files.size(); ++i) {
            if (i)
                cases << ',';
            cases << quote_json(c.case_files[i]);
        }
        cases << ']';
        std::ostringstream model_list;
        model_list << '[';
        for (std::size_t i = 0; i < models.size(); ++i) {
            if (i)
                model_list << ',';
            model_list << quote_json(model_name(models[i]));
        }
        model_list << ']';

        Json manifest_json;
        manifest_json.add("kind", "profiling-workloads-manifest");
        manifest_json.add("instrumented_build",
#ifdef ATM_INSTRUMENTED
                          true
#else
                          false
#endif
        );
        manifest_json.add(
            "workload",
            std::string(
                "real six-hour production Dopri5 propagation plus controlled locality schedules; "
                "measurement only, no caching, interpolation, memoisation or configuration "
                "change"));
        manifest_json.add("kernel_setup_and_weather_load_seconds", environment_seconds);
        manifest_json.add("propagation_phase_wall_seconds", propagation_wall);
        manifest_json.add("locality_phase_wall_seconds", locality_wall);
        manifest_json.add("profile_batches", options.batches);
        manifest_json.add("provider_warm_up_calls", static_cast<double>(256));
        manifest_json.add("frame", c.frame);
        manifest_json.add("epoch_utc", c.epoch_utc);
        manifest_json.add("epoch_et_seconds", epoch.et_seconds);
        manifest_json.add("duration_s", c.duration);
        manifest_json.add("output_cadence_s", c.cadence);
        manifest_json.add("rtol", c.rtol);
        manifest_json.add("max_step_s", c.max_step);
        manifest_json.add("initial_step_s", std::min(1.0, c.max_step));
        manifest_json.add("position_atol_m", 1e-3);
        manifest_json.add("velocity_atol_m_s", 1e-6);
        manifest_json.add("quaternion_atol", 1e-12);
        manifest_json.add("omega_atol_rad_s", 1e-12);
        manifest_json.add("max_attempts", static_cast<double>(IntegrationOptions{}.max_attempts));
        manifest_json.add("request_sequence_capacity", static_cast<double>(request_capacity));
        manifest_json.raw("case_files", cases.str());
        manifest_json.raw("models", model_list.str());
        manifest_json.raw("locality_modes", [&] {
            std::ostringstream list;
            list << '[';
            for (std::size_t i = 0; i < modes.size(); ++i) {
                if (i)
                    list << ',';
                Json mode;
                mode.add("name", modes[i].name);
                mode.add("description", modes[i].description);
                mode.add("requests", modes[i].requests.size());
                list << mode.str();
            }
            return list.str() + ']';
        }());
        manifest_json.add(
            "raw_jsonl_fields",
            std::string(
                "propagation/raw: epoch_et,x_m,y_m,z_m,rhs_ns,provider_ns,external_sun_ns. "
                "locality/raw: epoch_et,x_m,y_m,z_m,utc_year,utc_month,utc_day,day_of_year,"
                "seconds_of_day,day_length_seconds,days_since_1970,mjd_utc,density_kg_m3,"
                "temperature_k,exospheric_temperature_k,has_temperatures,has_number_density"));
        manifest_json.add("utc_metadata_source",
                          std::string("real SpiceEphemerisProvider::utc_calendar, captured after "
                                      "the timed region on every "
                                      "request; never approximated from ephemeris seconds"));
        manifest_json.add("external_sun_note",
                          std::string("the propagation RHS performs its own Sun geometric_state "
                                      "call in addition to any Sun "
                                      "acquisition inside the provider; it is timed separately so "
                                      "its contribution can be "
                                      "distinguished from provider-internal Sun work"));
        manifest_json.add("diagnostic_stage_note",
                          std::string("the instrumented build binds atm_profile::active to a "
                                      "preallocated Record for exactly "
                                      "one provider call per request and clears it immediately; "
                                      "the original build leaves it "
                                      "null and records no stage at all"));
        manifest_json.add("capture_overhead_note",
                          std::string("request capture adds a clock pair per boundary and a "
                                      "preallocated store per request; "
                                      "it inflates propagation wall time and is why the unmodified "
                                      "execute() run is reported "
                                      "alongside every instrumented batch"));
        manifest_json.raw("propagation_batches", propagation_rows.str());
        manifest_json.raw("locality_batches", locality_rows.str());
        manifest_json.raw("files", [&] {
            std::ostringstream list;
            list << '[';
            for (std::size_t i = 0; i < written.size(); ++i) {
                if (i)
                    list << ',';
                Json entry;
                entry.add("path", written[i].string());
                entry.add("sha256", sha256_file(written[i]));
                entry.add("bytes", static_cast<double>(std::filesystem::file_size(written[i])));
                list << entry.str();
            }
            return list.str() + ']';
        }());
        const std::filesystem::path manifest_path = out / "workloads-manifest.json";
        save(manifest_path, manifest_json.str());
        std::cout << "workloads written to " << out.string() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}