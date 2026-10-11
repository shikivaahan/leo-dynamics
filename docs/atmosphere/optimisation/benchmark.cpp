// ATM-OPT-001 independent and combined optimisation benchmark driver.
//
// Measurement only. The scientific configuration is the frozen one: the same
// included experiment translation unit that produced the ATM-PROF-001 evidence
// (its `main` renamed) supplies the case loader, `case_dynamics`, the project
// `tolerances()`, the output labels and the Environment, so the six-hour
// CASE-A/B/C runs are literally the same physics as the profiling evidence.
//
// The same source builds against two source trees:
//   * the candidate tree, with -DATM_OPT_CANDIDATE, which additionally
//     exercises the exact UTC / Sun-sharing / orientation reuse options;
//   * the unchanged original tree, where every reuse option is absent and the
//     candidate-only code below is compiled out entirely.
// Nothing inside the provider, the ephemeris or the models is added by this
// file, no allocation or logging happens inside a timed region, and no cache,
// interpolation, memoisation or configuration change is introduced here.
//
// Sections (selected with --modes):
//   provider      fixed-epoch full-provider batches: mean/median/p95/p99 of the
//                 per-call wall time plus the provider reuse counters
//   locality      the ten deterministic ATM-PROF-001 request schedules,
//                 including the epoch-major and trajectory-major ensembles
//   concurrency   1/2/4 threads, warm-up barrier, throughput and latency
//   propagate     six-hour CASE-A/B/C Dopri5 wall time and trajectory checksum
//   micro         isolated lookup cost: reuse hit path vs reuse-disabled path
//   replay        deterministic replay of the observed exact keys under the
//                 shared global policy and a thread-local prototype
//   invalidate    explicit invalidation cost, correctness across invalidation,
//                 cross-provider Sun compatibility (candidate builds only)
#define main atm_opt_experiment_main_renamed
#include ATM_OPT_EXPERIMENT_HEADER
#undef main

#include <barrier>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <thread>
#include <vector>
#ifdef LEO_SPICE_REUSE_DIAGNOSTICS
extern "C" void atm_opt_lookup_measurements(std::uint64_t*);
#endif
#ifdef ATM_OPT_ORIGINAL_DIAGNOSTICS
extern "C" void atm_opt_original_counts(std::uint64_t*);
extern "C" void atm_opt_original_reset();
#endif

namespace {

// ---------------------------------------------------------------------------
// Reuse statistics.
//
// The monotonic counters are differenced across a measured region; the
// occupancy fields (entries, capacities, cache bytes) describe the state after
// the region, exactly as the provider reports them.
// ---------------------------------------------------------------------------
struct opt_Reuse {
    std::uint64_t utc_hits = 0, utc_misses = 0, utc_evictions = 0;
    std::uint64_t orientation_hits = 0, orientation_misses = 0, orientation_evictions = 0;
    std::uint64_t invalidations = 0, et2utc_calls = 0, str2et_calls = 0, pxform_calls = 0;
    std::uint64_t spkez_calls = 0, mutex_acquisitions = 0, mutex_contended = 0, mutex_wait_ns = 0;
    std::uint64_t utc_entries = 0, orientation_entries = 0, utc_capacity = 0, orientation_capacity = 0;
    std::uint64_t cache_bytes = 0;
    std::array<std::uint64_t,4> lookup{};

    static bool available() {
#if defined(LEO_SPICE_REUSE_DIAGNOSTICS) || defined(ATM_OPT_ORIGINAL_DIAGNOSTICS)
        return true;
#else
        return false;
#endif
    }
    static opt_Reuse read() {
        opt_Reuse r;
#ifdef ATM_OPT_CANDIDATE
        const auto s = leo::SpiceEphemerisProvider::reuse_statistics();
        r.utc_hits = s.utc_hits;
        r.utc_misses = s.utc_misses;
        r.utc_evictions = s.utc_evictions;
        r.orientation_hits = s.orientation_hits;
        r.orientation_misses = s.orientation_misses;
        r.orientation_evictions = s.orientation_evictions;
        r.invalidations = s.invalidations;
        r.et2utc_calls = s.et2utc_calls;
        r.str2et_calls = s.str2et_calls;
        r.pxform_calls = s.pxform_calls;
        r.spkez_calls = s.spkez_calls;
        r.mutex_acquisitions = s.mutex_acquisitions;
        r.mutex_contended = s.mutex_contended;
        r.mutex_wait_ns = s.mutex_wait_ns;
        r.utc_entries = s.utc_entries;
        r.orientation_entries = s.orientation_entries;
        r.utc_capacity = s.utc_capacity;
        r.orientation_capacity = s.orientation_capacity;
        r.cache_bytes = s.cache_bytes;
#endif
#ifdef LEO_SPICE_REUSE_DIAGNOSTICS
        atm_opt_lookup_measurements(r.lookup.data());
#endif
#ifdef ATM_OPT_ORIGINAL_DIAGNOSTICS
        std::array<std::uint64_t,7> counts{};
        atm_opt_original_counts(counts.data());
        r.et2utc_calls=counts[0];r.str2et_calls=counts[1];r.pxform_calls=counts[2];r.spkez_calls=counts[3];
        r.mutex_acquisitions=counts[4];r.mutex_contended=counts[5];r.mutex_wait_ns=counts[6];
#endif
        return r;
    }
    static void reset() {
#ifdef ATM_OPT_CANDIDATE
        leo::SpiceEphemerisProvider::reset_reuse_statistics();
#endif
#ifdef ATM_OPT_ORIGINAL_DIAGNOSTICS
        atm_opt_original_reset();
#endif
    }
    opt_Reuse delta_from(const opt_Reuse &before) const {
        opt_Reuse d;
        d.utc_hits = utc_hits - before.utc_hits;
        d.utc_misses = utc_misses - before.utc_misses;
        d.utc_evictions = utc_evictions - before.utc_evictions;
        d.orientation_hits = orientation_hits - before.orientation_hits;
        d.orientation_misses = orientation_misses - before.orientation_misses;
        d.orientation_evictions = orientation_evictions - before.orientation_evictions;
        d.invalidations = invalidations - before.invalidations;
        d.et2utc_calls = et2utc_calls - before.et2utc_calls;
        d.str2et_calls = str2et_calls - before.str2et_calls;
        d.pxform_calls = pxform_calls - before.pxform_calls;
        d.spkez_calls = spkez_calls - before.spkez_calls;
        d.mutex_acquisitions = mutex_acquisitions - before.mutex_acquisitions;
        d.mutex_contended = mutex_contended - before.mutex_contended;
        d.mutex_wait_ns = mutex_wait_ns - before.mutex_wait_ns;
        for(std::size_t i=0;i<lookup.size();++i) d.lookup[i]=lookup[i]-before.lookup[i];
        d.utc_entries = utc_entries;
        d.orientation_entries = orientation_entries;
        d.utc_capacity = utc_capacity;
        d.orientation_capacity = orientation_capacity;
        d.cache_bytes = cache_bytes;
        return d;
    }
    opt_Reuse &operator+=(const opt_Reuse &other) {
        utc_hits += other.utc_hits;
        utc_misses += other.utc_misses;
        utc_evictions += other.utc_evictions;
        orientation_hits += other.orientation_hits;
        orientation_misses += other.orientation_misses;
        orientation_evictions += other.orientation_evictions;
        invalidations += other.invalidations;
        et2utc_calls += other.et2utc_calls;
        str2et_calls += other.str2et_calls;
        pxform_calls += other.pxform_calls;
        spkez_calls += other.spkez_calls;
        mutex_acquisitions += other.mutex_acquisitions;
        mutex_contended += other.mutex_contended;
        mutex_wait_ns += other.mutex_wait_ns;
        for(std::size_t i=0;i<lookup.size();++i) lookup[i]+=other.lookup[i];
        utc_entries = other.utc_entries;
        orientation_entries = other.orientation_entries;
        utc_capacity = other.utc_capacity;
        orientation_capacity = other.orientation_capacity;
        cache_bytes = other.cache_bytes;
        return *this;
    }
    std::string json() const {
        Json j;
        j.add("utc_hits", std::size_t(utc_hits));
        j.add("utc_misses", std::size_t(utc_misses));
        j.add("utc_evictions", std::size_t(utc_evictions));
        j.add("orientation_hits", std::size_t(orientation_hits));
        j.add("orientation_misses", std::size_t(orientation_misses));
        j.add("orientation_evictions", std::size_t(orientation_evictions));
        j.add("invalidations", std::size_t(invalidations));
        j.add("et2utc_calls", std::size_t(et2utc_calls));
        j.add("str2et_calls", std::size_t(str2et_calls));
        j.add("pxform_calls", std::size_t(pxform_calls));
        j.add("spkez_calls", std::size_t(spkez_calls));
        j.add("mutex_acquisitions", std::size_t(mutex_acquisitions));
        j.add("mutex_contended", std::size_t(mutex_contended));
        j.add("mutex_wait_ns", std::size_t(mutex_wait_ns));
        j.add("utc_lookup_ns",std::size_t(lookup[0]));
        j.add("utc_lookups",std::size_t(lookup[1]));
        j.add("orientation_lookup_ns",std::size_t(lookup[2]));
        j.add("orientation_lookups",std::size_t(lookup[3]));
        j.add("utc_entries_after", std::size_t(utc_entries));
        j.add("orientation_entries_after", std::size_t(orientation_entries));
        j.add("utc_capacity", std::size_t(utc_capacity));
        j.add("orientation_capacity", std::size_t(orientation_capacity));
        j.add("cache_bytes", std::size_t(cache_bytes));
        return j.str();
    }
};

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------

void opt_sink(double value) {
    asm volatile("" ::"g"(value) : "memory");
}

double opt_cpu_seconds() {
    timespec now{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now))
        throw std::runtime_error("CPU clock unavailable");
    return double(now.tv_sec) + 1e-9 * double(now.tv_nsec);
}

std::string opt_numbers(const std::vector<double> &values) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ',';
        out << values[i];
    }
    return out.str() + ']';
}

std::string opt_strings(const std::vector<std::string> &values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ',';
        out << '"' << values[i] << '"';
    }
    return out.str() + ']';
}

std::vector<std::string> opt_list(const std::string &list);

std::string opt_split(const std::string &list) {
    std::ostringstream out;
    const auto items = opt_list(list);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i)
            out << ',';
        out << items[i];
    }
    return out.str();
}

std::vector<std::string> opt_list(const std::string &list) {
    std::vector<std::string> items;
    std::istringstream rows(list);
    std::string item;
    while (std::getline(rows, item, ','))
        if (!item.empty())
            items.push_back(item);
    return items;
}

// Quantiles use linear interpolation at q(n-1), the estimator the profiling
// evidence documents for workload distributions, so the two datasets use the
// same estimator. Raw samples are always retained alongside the summary.
struct opt_Summary {
    std::size_t count = 0;
    double minimum = 0, median = 0, mean = 0, p95 = 0, p99 = 0, maximum = 0, stdev = 0;

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
    static opt_Summary of(std::vector<double> values) {
        opt_Summary s;
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
        s.minimum = values.front();
        s.maximum = values.back();
        s.mean = double(total) / double(values.size());
        s.median = quantile_sorted(values, 0.5);
        s.p95 = quantile_sorted(values, 0.95);
        s.p99 = quantile_sorted(values, 0.99);
        if (values.size() > 1) {
            long double variance = 0;
            for (double v : values)
                variance += (v - s.mean) * (v - s.mean);
            variance /= static_cast<long double>(values.size() - 1);
            s.stdev = std::sqrt(double(variance));
        }
        return s;
    }
    std::string json() const {
        Json j;
        j.add("count", count);
        j.add("minimum", minimum);
        j.add("median", median);
        j.add("mean", mean);
        j.add("p95", p95);
        j.add("p99", p99);
        j.add("maximum", maximum);
        j.add("stdev", stdev);
        j.add("relative_spread_of_mean", mean != 0 ? stdev / std::abs(mean) : 0.0);
        return j.str();
    }
};

struct opt_Batch {
    std::vector<double> per_call_ns;
    double wall_seconds = 0, cpu_seconds = 0;
    std::size_t calls = 0;
    std::string checksum;
    opt_Reuse reuse;
};

// ---------------------------------------------------------------------------
// Variants: the exact reuse options under test, independently and combined.
// ---------------------------------------------------------------------------
struct opt_Variant {
    std::string name;
    bool utc = false, sun = false, orientation = false;
};

// The full set is only offered where the options exist. An original build has
// exactly one configuration and is named accordingly, so the comparison is
// explicit rather than silent.
std::vector<opt_Variant> opt_variant_table(bool candidate) {
    if (!candidate)
        return {opt_Variant{"original", false, false, false}};
    return {opt_Variant{"off_off_off", false, false, false},
            opt_Variant{"utc_only", true, false, false},
            opt_Variant{"sun_only", false, true, false},
            opt_Variant{"orientation_only", false, false, true},
            opt_Variant{"utc_sun", true, true, false},
            opt_Variant{"utc_orientation", true, false, true},
            opt_Variant{"sun_orientation", false, true, true},
            opt_Variant{"all", true, true, true}};
}

std::vector<opt_Variant> opt_select_variants(const std::string &wanted, bool candidate) {
    const auto table = opt_variant_table(candidate);
    std::vector<opt_Variant> selected;
    for (const auto &name : opt_list(wanted)) {
        const auto found = std::find_if(table.begin(), table.end(),
                                        [&](const opt_Variant &v) { return v.name == name; });
        if (found == table.end())
            throw std::invalid_argument("unknown or unavailable variant: " + name);
        selected.push_back(*found);
    }
    if (selected.empty())
        throw std::invalid_argument("empty variant selection");
    return selected;
}

std::unique_ptr<leo::atmosphere::DirectAtmosphere>
opt_make_atmosphere(leo::atmosphere::Model model, const Environment &env, const Config &c,
                    const opt_Variant &variant) {
#ifdef ATM_OPT_CANDIDATE
    leo::atmosphere::EvaluationOptions options;
    options.utc_reuse = variant.utc;
    options.sun_sharing = variant.sun;
    options.orientation_reuse = variant.orientation;
    return std::make_unique<leo::atmosphere::DirectAtmosphere>(
        model, env.ephemeris, env.weather, c.frame, c.ap_history, options);
#else
    (void)variant;
    return std::make_unique<leo::atmosphere::DirectAtmosphere>(model, env.ephemeris, env.weather,
                                                                c.frame, c.ap_history);
#endif
}

// ---------------------------------------------------------------------------
// Serial batches shared by every section.
//
// Warm-up runs first and is never recorded; the reuse counters are reset after
// the warm-up so they describe the measured region only. No allocation,
// formatting or I/O happens inside the timed call itself.
// ---------------------------------------------------------------------------
opt_Batch opt_serial_batch_indexed(const std::function<double(std::size_t)> &operation,
                                   std::size_t calls, std::size_t warmup) {
    for (std::size_t i = 0; i < warmup; ++i)
        opt_sink(operation(0));
    opt_Reuse::reset();
    const auto reuse_before = opt_Reuse::read();
    opt_Batch batch;
    batch.per_call_ns.reserve(calls);
    batch.calls = calls;
    Checksum checksum;
    const auto cpu_start = opt_cpu_seconds();
    const auto wall_start = Clock::now();
    for (std::size_t i = 0; i < calls; ++i) {
        const auto start = Clock::now();
        const double value = operation(i);
        batch.per_call_ns.push_back(
            std::chrono::duration<double, std::nano>(Clock::now() - start).count());
        checksum.add(value);
        opt_sink(value);
    }
    batch.wall_seconds = elapsed(wall_start);
    batch.cpu_seconds = opt_cpu_seconds() - cpu_start;
    batch.checksum = checksum.str();
    batch.reuse = opt_Reuse::read().delta_from(reuse_before);
    return batch;
}

opt_Batch opt_serial_batch(const std::function<double()> &operation, std::size_t calls,
                           std::size_t warmup) {
    return opt_serial_batch_indexed([&](std::size_t) { return operation(); }, calls, warmup);
}

std::string opt_batches_json(const std::vector<opt_Batch> &batches) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < batches.size(); ++i) {
        if (i)
            out << ',';
        Json j;
        j.add("index", i);
        j.add("calls", batches[i].calls);
        j.add("wall_s", batches[i].wall_seconds);
        j.add("cpu_s", batches[i].cpu_seconds);
        j.raw("per_call_ns", opt_Summary::of(batches[i].per_call_ns).json());
        j.add("result_checksum", batches[i].checksum);
        j.raw("reuse", batches[i].reuse.json());
        j.raw("raw_per_call_ns", opt_numbers(batches[i].per_call_ns));
        out << j.str();
    }
    return out.str() + ']';
}

// Aggregate over batches. Every batch is retained, and the spread between
// batch means is what makes a single reported mean interpretable.
std::string opt_aggregate_json(const std::vector<opt_Batch> &batches) {
    std::vector<double> all, walls, cpus, means;
    opt_Reuse total;
    bool identical = true;
    for (const auto &batch : batches) {
        all.insert(all.end(), batch.per_call_ns.begin(), batch.per_call_ns.end());
        walls.push_back(batch.wall_seconds);
        cpus.push_back(batch.cpu_seconds);
        means.push_back(opt_Summary::of(batch.per_call_ns).mean);
        total += batch.reuse;
        identical = identical && batch.checksum == batches.front().checksum;
    }
    Json j;
    j.raw("per_call_ns", opt_Summary::of(all).json());
    j.raw("batch_mean_ns", opt_Summary::of(means).json());
    j.raw("batch_wall_s", opt_Summary::of(walls).json());
    j.raw("batch_cpu_s", opt_Summary::of(cpus).json());
    j.raw("reuse_totals", total.json());
    j.add("batches_bitwise_identical", identical);
    return j.str();
}

// ---------------------------------------------------------------------------
// Deterministic locality schedules, adapted from the frozen ATM-PROF-001
// workloads with identical inputs, ordering and semantics.
// ---------------------------------------------------------------------------
struct opt_Request {
    double epoch_et = 0;
    Eigen::Vector3d position{Eigen::Vector3d::Zero()};
};

Eigen::Vector3d opt_scheduled_position(leo::Epoch epoch, double latitude_deg, double longitude_deg,
                                       double altitude_km, const std::string &frame) {
    const Eigen::Vector3d fixed = leo::atmosphere::earth_fixed(
        leo::atmosphere::Geodetic{latitude_deg * std::numbers::pi / 180.0,
                                  longitude_deg * std::numbers::pi / 180.0, altitude_km * 1e3});
    const Eigen::Matrix3d to_fixed = leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, frame);
    return to_fixed.transpose() * fixed;
}

std::vector<opt_Request> opt_make_schedule(const std::string &name, leo::Epoch base,
                                           const std::string &frame) {
    std::vector<opt_Request> requests;
    constexpr std::size_t total = 1024;
    const double day = 86400.0;
    if (name == "fixed") {
        const Eigen::Vector3d position = opt_scheduled_position(base, 45.0, 10.0, 400.0, frame);
        for (std::size_t i = 0; i < total; ++i)
            requests.push_back({base.et_seconds, position});
    } else if (name == "same_epoch_changing_positions") {
        for (std::size_t i = 0; i < total; ++i) {
            const double latitude = 44.0 + 0.5 * double(i % 32) / 31.0;
            const double longitude = 9.0 + 0.5 * double(i / 32) / 31.0;
            requests.push_back(
                {base.et_seconds, opt_scheduled_position(base, latitude, longitude, 400.0, frame)});
        }
    } else if (name == "same_position_changing_epochs") {
        const Eigen::Vector3d position = opt_scheduled_position(base, 45.0, 10.0, 400.0, frame);
        for (std::size_t i = 0; i < total; ++i)
            requests.push_back({base.et_seconds + 10.0 * double(i), position});
    } else if (name == "nearby_positions_epochs") {
        for (std::size_t i = 0; i < total; ++i) {
            const double t = base.et_seconds + double(i);
            requests.push_back({t, opt_scheduled_position(leo::Epoch{t}, 45.0 + 1e-4 * double(i % 17),
                                                          10.0 + 1e-4 * double(i % 11), 400.0,
                                                          frame)});
        }
    } else if (name == "identical_daily_records_changing_geometry") {
        for (std::size_t i = 0; i < total; ++i) {
            const double t = base.et_seconds + double(i);
            requests.push_back({t, opt_scheduled_position(leo::Epoch{t}, 44.0 + double(i % 32) / 64,
                                                          9.0 + double(i / 32) / 64, 400.0, frame)});
        }
    } else if (name == "weather_3h_boundary") {
        const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(base);
        const double seconds_into_day = calendar.seconds_of_day;
        const double boundary = 10800.0 * (std::floor(seconds_into_day / 10800.0) + 1);
        for (std::size_t i = 0; i < total; ++i) {
            const double t = base.et_seconds + (boundary - seconds_into_day) +
                             day * double(i / 128) + 30.0 * double(i % 128) - 1920.0;
            requests.push_back({t, opt_scheduled_position(leo::Epoch{t}, 45.0, 10.0, 400.0, frame)});
        }
    } else if (name == "weather_hour_boundary") {
        const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(base);
        const double seconds_into_day = calendar.seconds_of_day;
        const double boundary = 3600.0 * (std::floor(seconds_into_day / 3600.0) + 1);
        for (std::size_t i = 0; i < total; ++i) {
            const double t = base.et_seconds + (boundary - seconds_into_day) +
                             day * double(i / 128) + 15.0 * double(i % 128) - 960.0;
            requests.push_back({t, opt_scheduled_position(leo::Epoch{t}, 45.0, 10.0, 400.0, frame)});
        }
    } else if (name == "utc_midnight") {
        const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(base);
        const double seconds_of_day = calendar.seconds_of_day;
        for (std::size_t i = 0; i < total; ++i) {
            const double t = base.et_seconds + (86400.0 - seconds_of_day) + day * double(i / 128) +
                             60.0 * double(i % 128) - 3840.0;
            requests.push_back({t, opt_scheduled_position(leo::Epoch{t}, 45.0, 10.0, 400.0, frame)});
        }
    } else if (name == "ensemble_trajectory_major") {
        for (std::size_t trajectory = 0; trajectory < 8; ++trajectory)
            for (std::size_t step = 0; step < 128; ++step) {
                const double t = base.et_seconds + 15.0 * double(step);
                requests.push_back({t, opt_scheduled_position(leo::Epoch{t}, 45.0 + 0.2 * double(trajectory),
                                                             10.0 + 0.2 * double(trajectory), 400.0,
                                                             frame)});
            }
    } else if (name == "ensemble_epoch_major") {
        for (std::size_t step = 0; step < 128; ++step)
            for (std::size_t trajectory = 0; trajectory < 8; ++trajectory) {
                const double t = base.et_seconds + 15.0 * double(step);
                requests.push_back({t, opt_scheduled_position(leo::Epoch{t}, 45.0 + 0.2 * double(trajectory),
                                                             10.0 + 0.2 * double(trajectory), 400.0,
                                                             frame)});
            }
    } else {
        throw std::invalid_argument("unknown locality schedule: " + name);
    }
    if (requests.size() < 1000)
        throw std::runtime_error("schedule " + name + " has fewer than 1000 requests");
    for (const auto &request : requests)
        if (!std::isfinite(request.epoch_et) || !request.position.allFinite())
            throw std::runtime_error("schedule " + name + " produced a non-finite request");
    return requests;
}

const std::vector<std::string> &opt_schedule_names() {
    static const std::vector<std::string> names{
        "fixed",
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

const std::string &opt_schedule_description(const std::string &name) {
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
        throw std::invalid_argument("unknown locality schedule: " + name);
    return found->second;
}

// ---------------------------------------------------------------------------
// Section: fixed full provider
// ---------------------------------------------------------------------------
std::string opt_provider_section(const Config &c, Environment &env, leo::Epoch epoch,
                                 const std::vector<opt_Variant> &variants, std::size_t batches,
                                 std::size_t calls, std::size_t warmup) {
    const InertialVector position{opt_scheduled_position(epoch, 45.0, 30.0, 400.0, c.frame)};
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto model : requested_models())
        for (const auto &variant : variants) {
            const auto atmosphere = opt_make_atmosphere(model, env, c, variant);
#ifdef ATM_OPT_CANDIDATE
            const auto supplied=env.ephemeris.observe_sun(epoch);
#endif
            std::vector<opt_Batch> rows;
            for (std::size_t batch = 0; batch < batches; ++batch)
                rows.push_back(opt_serial_batch(
                    [&] {
#ifdef ATM_OPT_CANDIDATE
                        return atmosphere->evaluate(epoch,position,&supplied).density_kg_m3;
#else
                        return atmosphere->evaluate(epoch,position).density_kg_m3;
#endif
                    },calls,warmup));
            if (!first)
                out << ',';
            first = false;
            Json row;
            row.add("model", model_name(model));
            row.add("variant", variant.name);
            row.raw("batches", opt_batches_json(rows));
            row.raw("aggregate", opt_aggregate_json(rows));
            out << row.str();
        }
    return out.str() + ']';
}

// ---------------------------------------------------------------------------
// Section: deterministic locality schedules
// ---------------------------------------------------------------------------
struct opt_Keys {
    std::vector<std::uint64_t> et_bits;
    std::vector<long long> civil_day;
};

// Exact keys for the replay and for independent reanalysis: the ET bit pattern
// and the real civil day, acquired outside every timed region through the real
// wrapper. No rounded ET and no ET/86400 arithmetic is used anywhere.
opt_Keys opt_record_keys(const std::vector<opt_Request> &requests) {
    opt_Keys keys;
    keys.et_bits.reserve(requests.size());
    keys.civil_day.reserve(requests.size());
    for (const auto &request : requests) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &request.epoch_et, sizeof bits);
        keys.et_bits.push_back(bits);
        keys.civil_day.push_back(
            leo::SpiceEphemerisProvider::utc_calendar(leo::Epoch{request.epoch_et}).days_since_1970);
    }
    return keys;
}

std::string opt_keys_json(const opt_Keys &keys) {
    std::ostringstream et, civil;
    et << std::setprecision(17) << '[';
    civil << '[';
    for (std::size_t i = 0; i < keys.et_bits.size(); ++i) {
        if (i) {
            et << ',';
            civil << ',';
        }
        std::ostringstream hex;
        hex << std::hex << keys.et_bits[i];
        et << '"' << hex.str() << '"';
        civil << keys.civil_day[i];
    }
    return "{\"et_bits_hex\":" + et.str() + "],\"civil_day_ordinal\":" + civil.str() + "]}";
}

std::string opt_locality_section(const Config &c, Environment &env, leo::Epoch epoch,
                                 const std::vector<opt_Variant> &variants, std::size_t batches,
                                 std::size_t warmup, std::string *keys_json) {
    std::ostringstream out;
    out << '[';
    std::ostringstream keys;
    keys << '{';
    bool first_schedule = true, first_row = true;
    for (const auto &name : opt_schedule_names()) {
        const auto requests = opt_make_schedule(name, epoch, c.frame);
        if (!first_schedule)
            keys << ',';
        first_schedule = false;
        Json key_row;
        key_row.add("schedule", name);
        key_row.add("description", opt_schedule_description(name));
        key_row.add("requests", requests.size());
        key_row.raw("keys", opt_keys_json(opt_record_keys(requests)));
        keys << '"' << name << "\":" << key_row.str();

        for (const auto model : requested_models())
            for (const auto &variant : variants) {
                const auto atmosphere = opt_make_atmosphere(model, env, c, variant);
                std::vector<opt_Batch> rows;
                for (std::size_t batch = 0; batch < batches; ++batch) {
#ifdef ATM_OPT_CANDIDATE
                    leo::SpiceEphemerisProvider::invalidate_reuse();
#endif
                    rows.push_back(opt_serial_batch_indexed(
                        [&](std::size_t i) {
                            const leo::Epoch at{requests[i].epoch_et};
                            const InertialVector position{requests[i].position};
#ifdef ATM_OPT_CANDIDATE
                            const auto sun=env.ephemeris.observe_sun(at);
                            return atmosphere->evaluate(at,position,&sun).density_kg_m3;
#else
                            const auto sun=env.ephemeris.geometric_state(leo::sun_naif_id,at);
                            opt_sink(sun.position_m.x());
                            return atmosphere->evaluate(at,position).density_kg_m3;
#endif
                        },
                        requests.size(), warmup));
                }
                if (!first_row)
                    out << ',';
                first_row = false;
                Json row;
                row.add("schedule", name);
                row.add("model", model_name(model));
                row.add("variant", variant.name);
                row.add("requests_per_batch", requests.size());
                row.raw("batches", opt_batches_json(rows));
                row.raw("aggregate", opt_aggregate_json(rows));
                out << row.str();
            }
    }
    *keys_json = keys.str() + '}';
    return out.str() + ']';
}

// ---------------------------------------------------------------------------
// Section: 1/2/4 thread throughput with the production locks retained
// ---------------------------------------------------------------------------
std::string opt_concurrency_section(const Config &c, Environment &env, leo::Epoch epoch,
                                    const std::vector<opt_Variant> &variants,
                                    const std::vector<unsigned> &threads, std::size_t batches,
                                    std::size_t calls, std::size_t warmup) {
    const InertialVector position{opt_scheduled_position(epoch, 45.0, 30.0, 400.0, c.frame)};
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto model : requested_models())
        for (const auto &variant : variants)
            for (const auto count : threads) {
                const auto atmosphere = opt_make_atmosphere(model, env, c, variant);
#ifdef ATM_OPT_CANDIDATE
                const auto supplied=env.ephemeris.observe_sun(epoch);
#endif
                std::vector<double> walls, cpus, throughput;
                std::vector<double> latencies;
                std::vector<std::string> checksums;
                opt_Reuse total;
                for (std::size_t batch = 0; batch < batches; ++batch) {
                    std::barrier ready(int(count) + 1), start(int(count) + 1), finish(int(count) + 1);
                    std::vector<std::vector<double>> times(count, std::vector<double>(calls));
                    std::vector<Checksum> checks(count);
                    std::vector<std::thread> workers;
                    for (unsigned tid = 0; tid < count; ++tid)
                        workers.emplace_back([&, tid] {
                            auto operation = [&] {
#ifdef ATM_OPT_CANDIDATE
                                return atmosphere->evaluate(epoch,position,&supplied).density_kg_m3;
#else
                                return atmosphere->evaluate(epoch,position).density_kg_m3;
#endif
                            };
                            for (std::size_t i = 0; i < warmup; ++i)
                                opt_sink(operation());
                            ready.arrive_and_wait();
                            start.arrive_and_wait();
                            for (std::size_t k = 0; k < calls; ++k) {
                                const auto begin = Clock::now();
                                const double value = operation();
                                times[tid][k] = std::chrono::duration<double, std::nano>(
                                                     Clock::now() - begin)
                                                     .count();
                                checks[tid].add(value);
                                opt_sink(value);
                            }
                            finish.arrive_and_wait();
                        });
                    ready.arrive_and_wait();
                    // Counters are reset once every worker has left the warm-up,
                    // so the reported reuse and mutex counts cover the measured
                    // region only.
                    opt_Reuse::reset();
                    const auto reuse_before = opt_Reuse::read();
                    const auto cpu_start = opt_cpu_seconds();
                    const auto wall_start = Clock::now();
                    start.arrive_and_wait();
                    finish.arrive_and_wait();
                    const double wall = elapsed(wall_start);
                    const double cpu = opt_cpu_seconds() - cpu_start;
                    for (auto &worker : workers)
                        worker.join();
                    walls.push_back(wall);
                    cpus.push_back(cpu);
                    throughput.push_back(double(count) * double(calls) / wall);
                    for (unsigned tid = 0; tid < count; ++tid) {
                        latencies.insert(latencies.end(), times[tid].begin(), times[tid].end());
                        checksums.push_back(checks[tid].str());
                    }
                    total += opt_Reuse::read().delta_from(reuse_before);
                }
                if (!first)
                    out << ',';
                first = false;
                Json row;
                row.add("model", model_name(model));
                row.add("variant", variant.name);
                row.add("threads", std::size_t(count));
                row.add("calls_per_thread_per_batch", calls);
                row.raw("batch_wall_s", opt_numbers(walls));
                row.raw("batch_cpu_s", opt_numbers(cpus));
                row.raw("batch_throughput_calls_per_s", opt_numbers(throughput));
                row.raw("throughput_calls_per_s", opt_Summary::of(throughput).json());
                row.raw("latency_ns", opt_Summary::of(latencies).json());
                row.raw("reuse_totals", total.json());
                row.raw("raw_latency_ns", opt_numbers(latencies));
                std::ostringstream hashes;
                hashes << '[';
                for (std::size_t i = 0; i < checksums.size(); ++i) {
                    if (i)
                        hashes << ',';
                    hashes << '"' << checksums[i] << '"';
                }
                row.raw("per_worker_checksums", hashes.str() + ']');
                out << row.str();
            }
    return out.str() + ']';
}

// ---------------------------------------------------------------------------
// Section: six-hour CASE-A/B/C propagation
// ---------------------------------------------------------------------------
struct opt_PropagationResult {
    double wall_seconds = 0;
    std::size_t rhs_calls = 0, atmosphere_calls = 0;
    std::string trajectory_checksum, density_checksum;
    opt_Reuse reuse;
    std::vector<double> rhs_ns,provider_ns;
};

opt_PropagationResult opt_run_propagation(const Config &c, Environment &env,
                                          const experiment::Case &spec,
                                          leo::atmosphere::Model model, const opt_Variant &variant,
                                          leo::Epoch epoch) {
    const auto dynamics = case_dynamics(spec);
    const State start = spec.start();
    const Earth earth;
    const auto atmosphere = opt_make_atmosphere(model, env, c, variant);
    const auto labels = sample_labels(c);
    constexpr std::size_t timing_capacity=1u<<21;
    opt_PropagationResult result;
    result.rhs_ns.resize(timing_capacity);
    result.provider_ns.resize(timing_capacity);

    std::size_t atmosphere_calls = 0;
    Checksum densities;
    auto source = [&](ElapsedTime t, const State &s) {
        const leo::Epoch at{epoch.et_seconds + t.seconds};
        leo::EnvironmentSnapshot snapshot;
        snapshot.epoch = at;
        // Existing atmospheric corotation assumption, unchanged.
        snapshot.atmosphere_angular_velocity_rad_s = InertialVector{0, 0, 7.292115e-5};
#ifdef ATM_OPT_CANDIDATE
        // Candidate build: the right-hand side needs the Sun for illumination
        // anyway, so the provider is offered that very observation. It may use
        // it only when its own sun_sharing option is on and the identity check
        // passes; otherwise it performs its own lookup exactly as before.
        const auto observation = env.ephemeris.observe_sun(at);
        const auto &sun = observation.state();
        snapshot.sun_position_m = sun.position_m;
        snapshot.illumination = illumination_factor(EclipseModel::conical, s.r, sun.position_m,
                                                     earth.equatorial_radius_m);
        const auto provider_start=Clock::now();
        const auto atmospheric = atmosphere->evaluate(at, s.r, &observation);
#else
        const auto sun = env.ephemeris.geometric_state(leo::sun_naif_id, at);
        snapshot.sun_position_m = sun.position_m;
        snapshot.illumination = illumination_factor(EclipseModel::conical, s.r, sun.position_m,
                                                     earth.equatorial_radius_m);
        const auto provider_start=Clock::now();
        const auto atmospheric = atmosphere->evaluate(at, s.r);
#endif
        if(atmosphere_calls==timing_capacity) throw std::runtime_error("provider timing capacity exceeded");
        result.provider_ns[atmosphere_calls]=std::chrono::duration<double,std::nano>(Clock::now()-provider_start).count();
        if (!std::isfinite(atmospheric.density_kg_m3) || atmospheric.density_kg_m3 < 0)
            throw std::runtime_error("nonfinite or negative atmosphere density");
        snapshot.density_kg_m3 = atmospheric.density_kg_m3;
        densities.add(atmospheric.density_kg_m3);
        ++atmosphere_calls;
        return snapshot;
    };

    IntegrationOptions options = tolerances();
    options.rtol = c.rtol;
    options.max_step_s = c.max_step;
    options.initial_step_s = std::min(1.0, c.max_step);
    options.max_attempts = IntegrationOptions{}.max_attempts;

    std::size_t rhs_calls = 0;
    auto system = [&](ElapsedTime t, const State &s) {
        const auto rhs_start=Clock::now();
        const auto derivative=dynamics(t,s,source(t,s));
        if(rhs_calls==timing_capacity) throw std::runtime_error("RHS timing capacity exceeded");
        result.rhs_ns[rhs_calls++]=std::chrono::duration<double,std::nano>(Clock::now()-rhs_start).count();
        return derivative;
    };
    opt_Reuse::reset();
    const auto reuse_before = opt_Reuse::read();
    const auto start_timer = Clock::now();
    const Trajectory trajectory = propagate(system, start, epoch, labels, options);
    result.wall_seconds = elapsed(start_timer);
    result.reuse = opt_Reuse::read().delta_from(reuse_before);
    result.rhs_ns.resize(rhs_calls);
    result.provider_ns.resize(atmosphere_calls);
    Checksum checksum;
    for (const auto &sample : trajectory.samples)
        checksum.add(sample.state);
    result.trajectory_checksum = checksum.str();
    result.density_checksum = densities.str();
    result.rhs_calls = rhs_calls;
    result.atmosphere_calls = atmosphere_calls;
    return result;
}

std::string opt_propagate_section(const Config &c, Environment &env, leo::Epoch epoch,
                                  const std::vector<opt_Variant> &variants, std::size_t repeats) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto &file : c.case_files) {
        const auto spec = experiment::load(file);
        for (const auto &variant : variants)
            for (const auto model : requested_models()) {
                std::vector<opt_PropagationResult> rows;
                std::vector<double> walls;
                for (std::size_t rep = 0; rep < repeats; ++rep) {
                    rows.push_back(opt_run_propagation(c, env, spec, model, variant, epoch));
                    walls.push_back(rows.back().wall_seconds);
                }
                const bool identical = std::all_of(rows.begin(), rows.end(),
                                                   [&](const opt_PropagationResult &r) {
                                                       return r.trajectory_checksum ==
                                                                  rows.front().trajectory_checksum &&
                                                              r.rhs_calls == rows.front().rhs_calls;
                                                   });
                if (!first)
                    out << ',';
                first = false;
                std::ostringstream repetitions;
                repetitions << '[';
                opt_Reuse total;
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    if (i)
                        repetitions << ',';
                    Json row;
                    row.add("index", i);
                    row.add("wall_s", rows[i].wall_seconds);
                    row.add("rhs_calls", rows[i].rhs_calls);
                    row.add("atmosphere_calls", rows[i].atmosphere_calls);
                    row.add("trajectory_checksum", rows[i].trajectory_checksum);
                    row.add("density_checksum", rows[i].density_checksum);
                    row.raw("reuse", rows[i].reuse.json());
                    row.raw("rhs_ns",opt_Summary::of(rows[i].rhs_ns).json());
                    row.raw("provider_ns",opt_Summary::of(rows[i].provider_ns).json());
                    row.raw("raw_rhs_ns",opt_numbers(rows[i].rhs_ns));
                    row.raw("raw_provider_ns",opt_numbers(rows[i].provider_ns));
                    repetitions << row.str();
                    total += rows[i].reuse;
                }
                Json entry;
                entry.add("case_id", spec.id);
                entry.add("case_file", spec.file);
                entry.add("model", model_name(model));
                entry.add("variant", variant.name);
                entry.add("repetitions_bitwise_identical", identical);
                entry.raw("wall_s", opt_Summary::of(walls).json());
                entry.raw("raw_wall_s", opt_numbers(walls));
                entry.raw("reuse_totals", total.json());
                entry.raw("repetitions", repetitions.str() + ']');
                out << entry.str();
            }
    }
    return out.str() + ']';
}

// ---------------------------------------------------------------------------
// Section: isolated lookup cost
// ---------------------------------------------------------------------------
std::vector<std::string> opt_micro_cases(bool candidate) {
    if (!candidate)
        return {"utc_fixed_default", "utc_changing", "orientation_fixed_default",
                "orientation_changing", "sun_geometric"};
    return {"utc_fixed_reuse", "utc_fixed_noreuse", "utc_changing", "orientation_fixed_reuse",
            "orientation_fixed_noreuse", "orientation_changing", "sun_observation",
            "sun_geometric", "provider_control", "provider_full"};
}

std::string opt_micro_section(const Config &c, Environment &env, leo::Epoch epoch,
                              std::size_t batches, std::size_t calls, std::size_t warmup) {
    const InertialVector position{opt_scheduled_position(epoch, 45.0, 30.0, 400.0, c.frame)};
    std::ostringstream out;
    out << '[';
    bool first = true;
#ifdef ATM_OPT_CANDIDATE
    constexpr bool candidate = true;
#else
    constexpr bool candidate = false;
#endif
    for (const auto &name : opt_micro_cases(candidate)) {
        std::size_t index = 0;
        std::function<double()> operation;
        std::unique_ptr<leo::atmosphere::DirectAtmosphere> atmosphere;
        std::vector<double> daily_epochs;
        if (name == "utc_fixed_default" || name == "utc_fixed_reuse" || name == "utc_fixed_noreuse") {
#ifdef ATM_OPT_CANDIDATE
            const bool reuse = name != "utc_fixed_noreuse";
            operation = [epoch, reuse] {
                return leo::SpiceEphemerisProvider::utc_calendar(epoch, reuse).mjd;
            };
#else
            operation = [epoch] { return leo::SpiceEphemerisProvider::utc_calendar(epoch).mjd; };
#endif
        } else if (name == "orientation_fixed_default" || name == "orientation_fixed_reuse" ||
                   name == "orientation_fixed_noreuse") {
#ifdef ATM_OPT_CANDIDATE
            const bool reuse = name != "orientation_fixed_noreuse";
            operation = [epoch, reuse, frame = c.frame] {
                return leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, frame, reuse)(0, 0);
            };
#else
            operation = [epoch, frame = c.frame] {
                return leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, frame)(0, 0);
            };
#endif
        } else if (name == "utc_changing" || name == "orientation_changing") {
            const bool orientation = name == "orientation_changing";
            if(!orientation) {
                using namespace std::chrono;
                const auto serial=leo::SpiceEphemerisProvider::utc_calendar(epoch).days_since_1970;
                daily_epochs.reserve(batches*calls+2*warmup+1);
                for(std::size_t i=0;i<batches*calls+2*warmup+1;++i) {
                    const year_month_day date{sys_days{days{serial+365+static_cast<long long>(i)}}};
                    char text[40]{};
                    std::snprintf(text,sizeof(text),"%04d-%02u-%02uT12:00:00",
                        static_cast<int>(date.year()),static_cast<unsigned>(date.month()),static_cast<unsigned>(date.day()));
                    daily_epochs.push_back(leo::SpiceEphemerisProvider::epoch_from_utc(text).et_seconds);
                }
            }
            operation = [&, orientation] {
                const double t=orientation ? epoch.et_seconds+0.5*double(index++):daily_epochs.at(index++);
                if(orientation)
                    return leo::SpiceEphemerisProvider::earth_fixed_rotation(leo::Epoch{t},c.frame)(0,0);
                return leo::SpiceEphemerisProvider::utc_calendar(leo::Epoch{t}).mjd;
            };
        } else if (name == "sun_observation" || name == "sun_geometric") {
#ifdef ATM_OPT_CANDIDATE
            if (name == "sun_observation")
                operation = [&env, epoch] {
                    return env.ephemeris.observe_sun(epoch).state().position_m.x();
                };
            else
#endif
                operation = [&env, epoch] {
                    return env.ephemeris.geometric_state(leo::sun_naif_id, epoch).position_m.x();
                };
        } else if (name == "provider_full" || name == "provider_control") {
            const opt_Variant variant = name == "provider_full"
                                            ? opt_Variant{"all", true, true, true}
                                            : opt_Variant{"off_off_off", false, false, false};
            atmosphere=opt_make_atmosphere(leo::atmosphere::Model::jb2008,env,c,variant);
            operation = [&] { return atmosphere->evaluate(epoch, position).density_kg_m3; };
        }
        std::vector<opt_Batch> rows;
        for (std::size_t batch = 0; batch < batches; ++batch) {
            index = batch * calls + warmup;
            rows.push_back(opt_serial_batch(operation, calls, warmup));
        }
        if (!first)
            out << ',';
        first = false;
        Json row;
        row.add("operation", name);
        row.raw("batches", opt_batches_json(rows));
        row.raw("aggregate", opt_aggregate_json(rows));
        out << row.str();
    }
    return out.str() + ']';
}

// ---------------------------------------------------------------------------
// Section: deterministic replay of the observed exact keys.
//
// This is a policy simulation over recorded key sequences, not a measurement
// and not a production cache: nothing here is used by the provider, and the
// thread-local rows model a design study that is deliberately not shipped.
// ---------------------------------------------------------------------------
struct opt_ReplayCounters {
    std::uint64_t hits = 0, misses = 0, evictions = 0;
    void add(const opt_ReplayCounters &other) {
        hits += other.hits;
        misses += other.misses;
        evictions += other.evictions;
    }
    std::string json() const {
        Json j;
        j.add("hits", std::size_t(hits));
        j.add("misses", std::size_t(misses));
        j.add("evictions", std::size_t(evictions));
        const double total = double(hits + misses);
        j.add("hit_rate", total > 0 ? double(hits) / total : 0.0);
        return j.str();
    }
};

// FIFO cache of civil days: the UTC cache policy of the candidate.
opt_ReplayCounters opt_replay_fifo(const std::vector<long long> &keys,std::size_t capacity,bool warm) {
    opt_ReplayCounters counters;
    capacity = std::max<std::size_t>(1, capacity);
    std::deque<long long> entries;
    if(warm && !keys.empty()) entries.push_back(keys.front());
    for (const auto key : keys) {
        if (std::find(entries.begin(), entries.end(), key) != entries.end()) {
            ++counters.hits;
            continue;
        }
        ++counters.misses;
        if (entries.size() == capacity) {
            entries.pop_front();
            ++counters.evictions;
        }
        entries.push_back(key);
    }
    return counters;
}

// Four-way FIFO set-associative cache of exact ET bit patterns: the
// orientation cache policy of the candidate.
opt_ReplayCounters opt_replay_4way(const std::vector<std::uint64_t> &keys,
                                   std::size_t capacity,bool itrf,bool warm) {
    opt_ReplayCounters counters;
    constexpr std::size_t ways = 4;
    const std::size_t sets = std::max<std::size_t>(1, capacity / ways);
    std::vector<std::vector<std::uint64_t>> table(sets);
    const auto index=[&](std::uint64_t key) {
        auto mixed=key^static_cast<std::uint64_t>(itrf);
        mixed^=mixed>>30;mixed*=0xbf58476d1ce4e5b9ULL;
        mixed^=mixed>>27;mixed*=0x94d049bb133111ebULL;
        mixed^=mixed>>31;
        return mixed%sets;
    };
    if(warm && !keys.empty()) table[index(keys.front())].push_back(keys.front());
    for (const auto key : keys) {
        auto &line = table[index(key)];
        if (std::find(line.begin(), line.end(), key) != line.end()) {
            ++counters.hits;
            continue;
        }
        ++counters.misses;
        if (line.size() == ways) {
            line.erase(line.begin());
            ++counters.evictions;
        }
        line.push_back(key);
    }
    return counters;
}

std::string opt_replay_section(const Config &c, leo::Epoch epoch,
                               const std::vector<unsigned> &threads,bool warm) {
    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const auto &name : opt_schedule_names()) {
        const auto requests = opt_make_schedule(name, epoch, c.frame);
        const auto keys = opt_record_keys(requests);
        std::ostringstream by_threads;
        by_threads << '{';
        for (std::size_t ti = 0; ti < threads.size(); ++ti) {
            const std::size_t count = threads[ti];
            if (ti)
                by_threads << ',';
            // Shared global policy: one cache per key space with exactly the
            // capacity and replacement rule of the candidate.
            const auto shared_utc = opt_replay_fifo(keys.civil_day,32,warm);
            const auto shared_orientation = opt_replay_4way(keys.et_bits,256,c.frame=="ITRF93",warm);
            // Thread-local prototype: the same total capacity split across
            // threads with no shared synchronisation. Requests are handed to
            // threads round-robin, the deterministic analogue of concurrent
            // arrival, so the interleaving is the schedule's own ordering, and
            // each thread keeps its own cache for the whole sequence.
            const std::size_t utc_capacity = std::max<std::size_t>(1, 32 / count);
            const std::size_t orientation_capacity = std::max<std::size_t>(4, 256 / count);
            std::vector<std::vector<long long>> utc_streams(count);
            std::vector<std::vector<std::uint64_t>> et_streams(count);
            for (std::size_t i = 0; i < keys.et_bits.size(); ++i) {
                const auto thread = i % count;
                utc_streams[thread].push_back(keys.civil_day[i]);
                et_streams[thread].push_back(keys.et_bits[i]);
            }
            opt_ReplayCounters local_utc, local_orientation;
            for (std::size_t thread = 0; thread < count; ++thread) {
                local_utc.add(opt_replay_fifo(utc_streams[thread],utc_capacity,warm));
                local_orientation.add(opt_replay_4way(et_streams[thread],orientation_capacity,c.frame=="ITRF93",warm));
            }
            Json row;
            row.add("threads", count);
            row.add("thread_local_utc_capacity_per_thread", std::size_t(utc_capacity));
            row.add("thread_local_orientation_capacity_per_thread",
                    std::size_t(orientation_capacity));
            row.raw("shared_utc_fifo32", shared_utc.json());
            row.raw("shared_orientation_4way256", shared_orientation.json());
            row.raw("thread_local_utc_fifo32", local_utc.json());
            row.raw("thread_local_orientation_4way256", local_orientation.json());
            by_threads << '"' << count << "\":" << row.str();
        }
        if (!first)
            out << ',';
        first = false;
        Json row;
        row.add("schedule", name);
        row.add("description", opt_schedule_description(name));
        row.add("requests", requests.size());
        row.add("method",
                "deterministic policy replay over the exact recorded key sequences: shared "
                "FIFO-32 civil-day cache and shared four-way 256-entry ET cache versus per-thread "
                "caches of the same total capacity with no synchronisation; a design study only, "
                "absent from production code");
        row.raw("by_thread_count", by_threads.str() + '}');
        out << '"' << name << "\":" << row.str();
    }
    return out.str() + '}';
}

// ---------------------------------------------------------------------------
// Section: explicit invalidation
// ---------------------------------------------------------------------------
std::string opt_invalidate_section(const Config &c, Environment &env, leo::Epoch epoch,
                                   std::size_t calls, std::size_t warmup) {
    Json row;
#ifdef ATM_OPT_CANDIDATE
    row.add("available", true);
    const auto atmosphere = opt_make_atmosphere(leo::atmosphere::Model::jb2008, env, c,
                                                opt_Variant{"all", true, true, true});
    const InertialVector position{opt_scheduled_position(epoch, 45.0, 30.0, 400.0, c.frame)};
    const auto evaluate = [&] { return atmosphere->evaluate(epoch, position).density_kg_m3; };
    // Warm the caches, fingerprint the results, then measure the explicit
    // invalidation the provider offers for a quiescent external mutation of
    // the managed configuration.
    for (std::size_t i = 0; i < warmup; ++i)
        opt_sink(evaluate());
    const auto before = opt_serial_batch(evaluate, calls, 0);
    std::vector<double> samples;
    samples.reserve(calls);
    opt_Reuse::reset();
    const auto reuse_before = opt_Reuse::read();
    for (std::size_t i = 0; i < calls; ++i) {
        const auto start = Clock::now();
        leo::SpiceEphemerisProvider::invalidate_reuse();
        samples.push_back(std::chrono::duration<double, std::nano>(Clock::now() - start).count());
    }
    const auto invalidation = opt_Reuse::read().delta_from(reuse_before);
    const auto after = opt_serial_batch(evaluate, calls, 0);
    row.add("result_checksum_before", before.checksum);
    row.add("result_checksum_after", after.checksum);
    row.add("results_identical_across_invalidation", before.checksum == after.checksum);
    row.raw("invalidate_reuse_ns", opt_Summary::of(samples).json());
    row.raw("raw_invalidate_reuse_ns", opt_numbers(samples));
    row.raw("invalidate_reuse_counters", invalidation.json());
    row.raw("reuse_before_invalidation", before.reuse.json());
    row.raw("reuse_after_invalidation", after.reuse.json());
    // Sun identity across providers of the same managed configuration, and the
    // rejection of a mismatched epoch. This runs after every timed region:
    // The same paths add refcount owners only; configuration does not change.
    {
        const auto other = leo::SpiceEphemerisProvider(Environment::make_kernels(c));
        const auto observation = env.ephemeris.observe_sun(epoch);
        row.add("cross_provider_compatible_sun", env.ephemeris.compatible_sun(observation, epoch));
        row.add("second_provider_compatible_sun", other.compatible_sun(observation, epoch));
        row.add("wrong_epoch_compatible_sun",
                env.ephemeris.compatible_sun(observation, leo::Epoch{epoch.et_seconds + 1.0}));
    }
    row.add("note",
            "cross-provider checks add/remove shared owners after timing; the configuration "
            "generation remains unchanged because these exact kernel paths are already loaded");
    return row.str();
#else
    // The unchanged original provider has no reuse cache and therefore no
    // explicit invalidation surface; nothing is emulated for it.
    row.add("available", false);
    row.add("reason",
            "the unchanged original provider has no reuse cache and therefore no explicit "
            "invalidation surface; nothing was emulated");
    return row.str();
#endif
}

} // namespace

int main(int argc, char **argv) {
    try {
        std::string profile_out, label = "benchmark";
        std::string modes = "provider,locality,concurrency,propagate,micro,replay,invalidate";
        std::string variants_wanted, concurrency_variants = "off_off_off,utc_only,sun_only,all";
        std::string propagate_variants = "off_off_off,all";
        std::size_t batches = 5, locality_batches = 3, calls = 2000, warmup = 256, repeats = 3;
        std::vector<unsigned> threads{1, 2, 4};
        std::vector<char *> forwarded{argv[0]};
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            const auto value = [&]() -> std::string {
                if (i + 1 == argc)
                    throw std::invalid_argument("missing value for " + key);
                return argv[++i];
            };
            if (key == "--profile-out")
                profile_out = value();
            else if (key == "--label")
                label = value();
            else if (key == "--modes")
                modes = value();
            else if (key == "--variants")
                variants_wanted = value();
            else if (key == "--concurrency-variants")
                concurrency_variants = value();
            else if (key == "--propagate-variants")
                propagate_variants = value();
            else if (key == "--batches")
                batches = std::size_t(positive(value(), "--batches"));
            else if (key == "--locality-batches")
                locality_batches = std::size_t(positive(value(), "--locality-batches"));
            else if (key == "--calls")
                calls = std::size_t(positive(value(), "--calls"));
            else if (key == "--warmup")
                warmup = std::size_t(positive(value(), "--warmup"));
            else if (key == "--propagate-repeats")
                repeats = std::size_t(positive(value(), "--propagate-repeats"));
            else if (key == "--thread-counts") {
                threads.clear();
                for (const auto &item : opt_list(value()))
                    threads.push_back(unsigned(positive(item, "--thread-counts")));
                if (threads.empty())
                    throw std::invalid_argument("--thread-counts needs at least one count");
            } else {
                forwarded.push_back(argv[i]);
            }
        }
        for(const auto count:threads) if(count!=1 && count!=2 && count!=4)
            throw std::invalid_argument("supported --thread-counts are 1,2,4 (equal-budget replay)");
        if (profile_out.empty())
            throw std::invalid_argument("--profile-out is required");
        if (std::filesystem::exists(profile_out))
            throw std::invalid_argument("refusing to overwrite existing --profile-out: " +
                                        profile_out);
        // parse_arguments validates the frozen scientific configuration; the
        // renamed experiment main never runs, so --mode and --out are inert
        // placeholders that keep that validation available.
        forwarded.push_back(const_cast<char *>("--mode"));
        forwarded.push_back(const_cast<char *>("timing"));
        forwarded.push_back(const_cast<char *>("--out"));
        forwarded.push_back(const_cast<char *>(profile_out.c_str()));

        const Config c = parse_arguments(int(forwarded.size()), forwarded.data());
        if (c.weather.empty() || c.solfsmy.empty() || c.dtcfile.empty())
            throw std::invalid_argument("--weather, --solfsmy and --dtcfile are required");
#ifdef ATM_OPT_CANDIDATE
        constexpr bool candidate = true;
#else
        constexpr bool candidate = false;
#endif
        const auto table = opt_variant_table(candidate);
        const auto variants =
            variants_wanted.empty() ? table : opt_select_variants(variants_wanted, candidate);
        const auto concurrency_set = opt_select_variants(concurrency_variants, candidate);
        const auto propagate_set = opt_select_variants(propagate_variants, candidate);
        const auto sections = opt_list(modes);
        const auto wants = [&](const std::string &name) {
            return std::find(sections.begin(), sections.end(), name) != sections.end();
        };
        if (wants("propagate") && c.case_files.empty())
            throw std::invalid_argument("the propagate section needs at least one --case");

        const auto environment_start = Clock::now();
        Environment env(c);
        const double environment_seconds = elapsed(environment_start);
        const leo::Epoch epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);

        std::ostringstream body;
        body << '{';
        bool first_section = true;
        const auto section = [&](const std::string &name, const std::string &json) {
            if (!first_section)
                body << ',';
            first_section = false;
            body << '"' << name << "\":" << json;
        };
        std::string keys_json;
        if (wants("provider"))
            section("provider",
                    opt_provider_section(c, env, epoch, variants, batches, calls, warmup));
        if (wants("locality"))
            section("locality", opt_locality_section(c, env, epoch, variants, locality_batches,
                                                     warmup, &keys_json));
        if (wants("concurrency"))
            section("concurrency", opt_concurrency_section(c, env, epoch, concurrency_set, threads,
                                                           batches, calls, warmup));
        if (wants("propagate"))
            section("propagate",
                    opt_propagate_section(c, env, epoch, propagate_set, repeats));
        if (wants("micro"))
            section("micro",
                    opt_micro_section(c, env, epoch, batches, calls, warmup));
        if (wants("replay"))
            section("replay", opt_replay_section(c, epoch, threads,warmup!=0));
        if (wants("invalidate"))
            section("invalidate", opt_invalidate_section(c, env, epoch, calls, warmup));
        if (!keys_json.empty())
            section("locality_keys", keys_json);

        Json root;
        root.add("kind", "atm-opt-benchmark");
        root.add("label", label);
        root.add("candidate_build", candidate);
#if defined(LEO_SPICE_REUSE_DIAGNOSTICS) || defined(ATM_OPT_ORIGINAL_DIAGNOSTICS)
        root.add("diagnostics_build", true);
#else
        root.add("diagnostics_build", false);
#endif
        root.add("reuse_statistics_available", opt_Reuse::available());
        root.add("kernel_setup_and_weather_load_s", environment_seconds);
        root.add("spice_toolkit_version", leo::SpiceEphemerisProvider::toolkit_version());
        root.add("epoch_utc", c.epoch_utc);
        root.add("epoch_et_seconds", epoch.et_seconds);
        root.add("frame", c.frame);
        root.add("protocol",
                 "exact UTC calendar, Sun sharing and Earth orientation reuse are measured "
                 "independently and combined against the unchanged original provider; every "
                 "density is a fresh model evaluation, there is no interpolation, approximation, "
                 "surrogate or density caching, and no allocation, logging or I/O occurs inside a "
                 "timed region");
        Json settings;
        settings.add("duration_s", c.duration);
        settings.add("cadence_s", c.cadence);
        settings.add("rtol", c.rtol);
        settings.add("max_step_s", c.max_step);
        settings.add("initial_step_s", std::min(1.0, c.max_step));
        settings.add("batches", batches);
        settings.add("locality_batches", locality_batches);
        settings.add("calls_per_batch", calls);
        settings.add("warmup", warmup);
        settings.add("propagate_repeats", repeats);
        settings.add("quantile_estimator", "linear interpolation at q(n-1)");
        settings.add("timer", "std::chrono::steady_clock wall; CLOCK_PROCESS_CPUTIME_ID per batch");
        settings.add("sections", opt_split(modes));
        std::ostringstream thread_list;
        thread_list << '[';
        for (std::size_t i = 0; i < threads.size(); ++i) {
            if (i)
                thread_list << ',';
            thread_list << threads[i];
        }
        settings.raw("thread_counts", thread_list.str() + ']');
        settings.raw("models", opt_strings([&] {
            std::vector<std::string> names;
            for (const auto model : requested_models())
                names.emplace_back(model_name(model));
            return names;
        }()));
        settings.raw("models_not_compiled_in", opt_strings([&] {
            std::vector<std::string> names;
            for (const auto model : absent_models())
                names.emplace_back(model_name(model));
            return names;
        }()));
        root.raw("settings", settings.str());
        std::ostringstream variant_table;
        variant_table << '[';
        for (std::size_t i = 0; i < table.size(); ++i) {
            if (i)
                variant_table << ',';
            Json row;
            row.add("name", table[i].name);
            row.add("utc_reuse", table[i].utc);
            row.add("sun_sharing", table[i].sun);
            row.add("orientation_reuse", table[i].orientation);
            variant_table << row.str();
        }
        root.raw("variants_available", variant_table.str() + ']');
        root.raw("sections", body.str() + '}');
        save(profile_out, root.str());
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}