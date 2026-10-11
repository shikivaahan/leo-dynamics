// ATM-OPT-001 cleanup experiment: interleaved, order-balanced provider timing.
//
// Purpose: the previous run executed each configuration's batches as one
// contiguous block, so configuration differences could not be separated from
// execution order, thermal/scheduler drift or warm state left by the preceding
// configuration. This driver instead interleaves configurations round-robin
// inside one batch sequence, randomises the starting configuration per
// repetition with a recorded seed, and reports per-repetition distributions so
// a difference can be tested against within-configuration variability.
//
// It also records the actual operation counts for each configuration through
// the diagnostic counters, so "Sun sharing is irrelevant to NRLMSISE" is a
// measured claim (identical counts, identical results) rather than an
// assumption.
//
// Measurement only: no caching, approximation or scientific setting is
// introduced. Every density is a fresh unchanged native evaluation.
#define main atm_opt_interleave_experiment_main_renamed
#include ATM_OPT_EXPERIMENT_HEADER
#undef main

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <numbers>

namespace {

// Operation counters, read only in the candidate diagnostic build.
struct Counts {
    std::uint64_t et2utc = 0, str2et = 0, pxform = 0, spkez = 0;
    static Counts read() {
        Counts c;
#ifdef LEO_SPICE_REUSE_DIAGNOSTICS
        const auto s = leo::SpiceEphemerisProvider::reuse_statistics();
        c.et2utc = s.et2utc_calls; c.str2et = s.str2et_calls;
        c.pxform = s.pxform_calls; c.spkez = s.spkez_calls;
#endif
        return c;
    }
    Counts &operator+=(const Counts &other) {
        et2utc += other.et2utc; str2et += other.str2et;
        pxform += other.pxform; spkez += other.spkez;
        return *this;
    }
    Counts minus(const Counts &before) const {
        return {et2utc - before.et2utc, str2et - before.str2et,
                pxform - before.pxform, spkez - before.spkez};
    }
    std::string json() const {
        Json j;
        j.add("et2utc_calls", std::size_t(et2utc));
        j.add("str2et_calls", std::size_t(str2et));
        j.add("pxform_calls", std::size_t(pxform));
        j.add("spkez_calls", std::size_t(spkez));
        return j.str();
    }
};

struct InterleaveVariant {
    const char *name;
    bool utc, sun, orientation;
};

// Exactly the configurations whose comparison the anomaly concerns, plus the
// unchanged-original control that cannot be expressed through candidate flags.
const std::vector<InterleaveVariant> kVariants = {
    {"off_off_off", false, false, false}, {"utc_only", true, false, false},
    {"sun_only", false, true, false},      {"orientation_only", false, false, true},
    {"utc_orientation", true, false, true},{"utc_sun", true, true, false},
    {"sun_orientation", false, true, true},{"all", true, true, true}};

std::unique_ptr<leo::atmosphere::DirectAtmosphere> make(const leo::atmosphere::Model model,
                                                       const Environment &env, const Config &c,
                                                       const InterleaveVariant &variant) {
#ifdef ATM_OPT_CANDIDATE
    leo::atmosphere::EvaluationOptions options;
    options.utc_reuse = variant.utc;
    options.sun_sharing = variant.sun;
    options.orientation_reuse = variant.orientation;
    return std::make_unique<leo::atmosphere::DirectAtmosphere>(model, env.ephemeris, env.weather,
                                                                c.frame, c.ap_history, options);
#else
    (void)variant;
    return std::make_unique<leo::atmosphere::DirectAtmosphere>(model, env.ephemeris, env.weather,
                                                                c.frame, c.ap_history);
#endif
}

struct TimingStats {
    std::size_t count = 0;
    double mean = 0, median = 0, p95 = 0, p99 = 0, stdev = 0, minimum = 0, maximum = 0;
    static double quantile(const std::vector<double> &sorted, double q) {
        if (sorted.empty()) return 0;
        if (sorted.size() == 1) return sorted.front();
        const double position = q * double(sorted.size() - 1);
        const auto lower = std::size_t(std::floor(position));
        const auto upper = std::min(lower + 1, sorted.size() - 1);
        const double weight = position - double(lower);
        return sorted[lower] * (1 - weight) + sorted[upper] * weight;
    }
    static TimingStats of(std::vector<double> values) {
        TimingStats s;
        s.count = values.size();
        if (values.empty()) return s;
        std::sort(values.begin(), values.end());
        long double total = 0;
        for (double v : values) total += v;
        s.mean = double(total / static_cast<long double>(values.size()));
        s.median = quantile(values, 0.5);
        s.p95 = quantile(values, 0.95);
        s.p99 = quantile(values, 0.99);
        s.minimum = values.front();
        s.maximum = values.back();
        if (values.size() > 1) {
            long double variance = 0;
            for (double v : values) variance += (v - s.mean) * (v - s.mean);
            variance /= static_cast<long double>(values.size() - 1);
            s.stdev = std::sqrt(double(variance));
        }
        return s;
    }
    std::string json(const std::vector<double> *raw) const {
        Json j;
        j.add("count", count); j.add("mean", mean); j.add("median", median);
        j.add("p95", p95); j.add("p99", p99); j.add("minimum", minimum);
        j.add("maximum", maximum); j.add("stdev", stdev);
        j.add("estimator", "linear interpolation at q(n-1)");
        if (raw) {
            std::ostringstream values;
            values << std::setprecision(17) << '[';
            for (std::size_t i = 0; i < raw->size(); ++i) { if (i) values << ','; values << (*raw)[i]; }
            j.raw("raw_nanoseconds", values.str() + ']');
        }
        return j.str();
    }
};

std::string numbers(const std::vector<double> &values) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) out << ','; out << values[i]; }
    return out.str() + ']';
}

// Welch's t statistic for two samples of batch means, plus a normal-approximation
// 95% interval on the difference of means. Reported as evidence of whether a
// difference is distinguishable from batch variability; never as a hypothesis
// test verdict on a physical model.
struct Welch {
    double t = 0, mean_difference = 0, ci_low = 0, ci_high = 0, standard_error = 0;
};
Welch welch(const std::vector<double> &a, const std::vector<double> &b) {
    Welch w;
    if (a.size() < 2 || b.size() < 2) return w;
    const auto stats_a = TimingStats::of(a), stats_b = TimingStats::of(b);
    w.mean_difference = stats_a.mean - stats_b.mean;
    const double variance_a = stats_a.stdev * stats_a.stdev / double(a.size());
    const double variance_b = stats_b.stdev * stats_b.stdev / double(b.size());
    const double se = std::sqrt(variance_a + variance_b);
    w.standard_error = se;
    if (se > 0) {
        w.t = w.mean_difference / se;
        w.ci_low = w.mean_difference - 1.96 * se;
        w.ci_high = w.mean_difference + 1.96 * se;
    }
    return w;
}
std::string welch_json(const Welch &w) {
    Json j;
    j.add("mean_difference_ns", w.mean_difference);
    j.add("standard_error_ns", w.standard_error);
    j.add("t_statistic", w.t);
    j.add("ci95_low_ns", w.ci_low);
    j.add("ci95_high_ns", w.ci_high);
    j.add("interpretation",
          "a 95% interval containing zero means the difference is not distinguishable from "
          "batch-level variability at this sample size");
    return j.str();
}

} // namespace

int main(int argc, char **argv) {
    try {
        std::string profile_out;
        std::uint64_t seed = 20251011;
        std::size_t repetitions = 12, calls = 2000, warmup = 256;
        std::vector<std::string> model_names;
        std::vector<char *> forwarded{argv[0]};
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            const auto value = [&]() -> std::string {
                if (i + 1 == argc) throw std::invalid_argument("missing value for " + key);
                return argv[++i];
            };
            if (key == "--profile-out") profile_out = value();
            else if (key == "--seed") seed = std::stoull(value());
            else if (key == "--repetitions") repetitions = std::size_t(std::stoul(value()));
            else if (key == "--calls") calls = std::size_t(std::stoul(value()));
            else if (key == "--warmup") warmup = std::size_t(std::stoul(value()));
            else if (key == "--models") {
                std::istringstream items(value());
                    std::string item;
                    while (std::getline(items, item, ',')) if (!item.empty()) model_names.push_back(item);
            }
            else forwarded.push_back(argv[i]);
        }
        if (profile_out.empty()) throw std::invalid_argument("--profile-out is required");
        if (std::filesystem::exists(profile_out))
            throw std::invalid_argument("refusing to overwrite existing --profile-out: " + profile_out);
        forwarded.push_back(const_cast<char *>("--mode"));
        forwarded.push_back(const_cast<char *>("timing"));
        forwarded.push_back(const_cast<char *>("--out"));
        forwarded.push_back(const_cast<char *>(profile_out.c_str()));
        const Config c = parse_arguments(int(forwarded.size()), forwarded.data());
        if (c.weather.empty() || c.solfsmy.empty() || c.dtcfile.empty())
            throw std::invalid_argument("--weather, --solfsmy and --dtcfile are required");

        const Environment env(c);
        const leo::Epoch epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
        const InertialVector position = inertial_from_geodetic(epoch, 45.0 * std::numbers::pi / 180.0,
                                                                 30.0 * std::numbers::pi / 180.0,
                                                                 400e3, c.frame);

        const auto requested = requested_models();
        std::vector<leo::atmosphere::Model> models;
        for (const auto model : requested) {
            const std::string name = model_name(model);
            if (model_names.empty() ||
                std::find(model_names.begin(), model_names.end(), name) != model_names.end())
                models.push_back(model);
        }
        if (models.empty()) throw std::invalid_argument("no requested model selected");

        // Warm every configuration once before any measurement so no
        // configuration is penalised for first-touch cold state.
        for (const auto model : models)
            for (const auto &variant : kVariants) {
                const auto atmosphere = make(model, env, c, variant);
                volatile double warm_sink = 0;
                for (std::size_t i = 0; i < warmup; ++i)
                    warm_sink = atmosphere->evaluate(epoch, position).density_kg_m3;
                (void)warm_sink;
            }

        std::mt19937_64 generator(seed);
        std::ostringstream order_log;
        order_log << '[';
        std::ostringstream body;
        body << '{';
        bool first_model = true;
        for (const auto model : models) {
            // Per configuration: per-repetition batch means, all per-call samples,
            // result checksum and diagnostic operation counts.
            std::vector<std::vector<double>> batch_means(kVariants.size());
            std::vector<std::vector<double>> all_samples(kVariants.size());
            std::vector<std::string> checksums(kVariants.size());
            std::vector<Counts> operation_counts(kVariants.size());
            for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
                std::vector<std::size_t> order(kVariants.size());
                std::iota(order.begin(), order.end(), std::size_t{0});
                std::shuffle(order.begin(), order.end(), generator);
                if (!order_log.str().empty() && order_log.str().back() != '[') order_log << ',';
                order_log << '[';
                for (std::size_t i = 0; i < order.size(); ++i) { if (i) order_log << ','; order_log << order[i]; }
                order_log << ']';
                for (const std::size_t index : order) {
                    const auto atmosphere = make(model, env, c, kVariants[index]);
#ifdef LEO_SPICE_REUSE_DIAGNOSTICS
                    leo::SpiceEphemerisProvider::reset_reuse_statistics();
#endif
                    const Counts before = Counts::read();
                    std::vector<double> samples;
                    samples.reserve(calls);
                    Checksum checksum;
                    const auto start = Clock::now();
                    for (std::size_t i = 0; i < calls; ++i) {
                        const auto call_start = Clock::now();
                        checksum.add(atmosphere->evaluate(epoch, position).density_kg_m3);
                        samples.push_back(std::chrono::duration<double, std::nano>(
                            Clock::now() - call_start).count());
                    }
                    const double wall = elapsed(start);
                    // Record only this repetition's batch mean; raw samples are
                    // accumulated separately so memory stays bounded.
                    operation_counts[index] += Counts::read().minus(before);
                    batch_means[index].push_back(samples.empty() ? 0 : wall * 1e9 / double(samples.size()));
                    all_samples[index].insert(all_samples[index].end(), samples.begin(), samples.end());
                    checksums[index] = checksum.str();
                }
            }
            if (!first_model) body << ',';
            first_model = false;
            Json entry;
            entry.add("workload", "fixed epoch and position; identical inputs for every configuration");
            std::ostringstream rows;
            rows << '[';
            for (std::size_t i = 0; i < kVariants.size(); ++i) {
                if (i) rows << ',';
                Json row;
                row.add("variant", kVariants[i].name);
                row.add("utc_reuse", kVariants[i].utc);
                row.add("sun_sharing", kVariants[i].sun);
                row.add("orientation_reuse", kVariants[i].orientation);
                row.add("result_checksum", checksums[i]);
                row.raw("per_call_ns", TimingStats::of(all_samples[i]).json(&all_samples[i]));
                row.raw("batch_mean_ns", TimingStats::of(batch_means[i]).json(&batch_means[i]));
                row.raw("operation_counts", operation_counts[i].json());
                rows << row.str();
            }
            entry.raw("configurations", rows.str() + ']');
            // Pairwise batch-mean comparisons against the all-off control, which
            // is what makes any Sun-toggle difference assessable.
            std::ostringstream comparisons;
            comparisons << '[';
            for (std::size_t i = 1; i < kVariants.size(); ++i) {
                if (i > 1) comparisons << ',';
                Json pair;
                pair.add("baseline", kVariants[0].name);
                pair.add("candidate", kVariants[i].name);
                pair.raw("batch_means", welch_json(welch(batch_means[i], batch_means[0])));
                comparisons << pair.str();
            }
            entry.raw("batch_mean_comparisons_vs_all_off", comparisons.str() + ']');
            // entry.str() is already a complete object; only the enclosing
            // sections object still needs its closing brace.
            body << '"' << model_name(model) << "\":" << entry.str();
        }
        body << '}';
        order_log << ']';

        Json root;
        root.add("kind", "atm-opt-001-interleaved-provider");
#ifdef ATM_OPT_CANDIDATE
        root.add("candidate_build", true);
#else
        root.add("candidate_build", false);
#endif
        root.add("seed", seed);
        root.add("repetitions", repetitions);
        root.add("calls_per_batch", calls);
        root.add("warmup_calls_per_configuration", warmup);
        root.add("configurations_under_test", kVariants.size());
        root.add("protocol",
                 "every configuration is warmed once, then configurations are executed round-robin "
                 "inside each repetition with a recorded seed; per-repetition batch means and all "
                 "per-call samples are retained; Welch 95% intervals compare each configuration "
                 "against the all-off control; process pinned by the caller");
        root.add("spice_toolkit_version", leo::SpiceEphemerisProvider::toolkit_version());
#ifdef LEO_SPICE_REUSE_DIAGNOSTICS
        root.add("diagnostics_build", true);
#else
        root.add("diagnostics_build", false);
#endif
        root.add("operation_counts_note",
                 "summed over all repetitions and divided by (repetitions*calls) these are the "
                 "actual per-call SPICE operation counts per configuration");
        root.add("epoch_utc", c.epoch_utc);
        root.add("frame", c.frame);
        root.raw("execution_order_per_repetition", order_log.str());
        root.raw("sections", body.str());
        save(profile_out, root.str());
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}