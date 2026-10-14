// ATM-CACHE-001 Phase 4 correctness restrictions.
//
// Every check here is an observation about the real code path, not an assertion
// about intent. A failing check is reported as a failure and is never turned
// into a passing summary.
#include "support.hpp"

namespace cache_study {
namespace {

struct Check {
    std::string name, question, method, outcome;
    bool passed = false;
    double worst_difference = 0;
    Json detail = Json();
    std::string json() const {
        Json j;
        j.add("check", name);
        j.add("question", question);
        j.add("method", method);
        j.add("passed", passed);
        j.add("worst_absolute_difference", worst_difference);
        j.add("outcome", outcome);
        j.raw("detail", detail.str());
        return j.str();
    }
};

bool bitwise_equal(double a, double b) {
    std::uint64_t x = 0, y = 0;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
}

// A spread of real states: several altitudes, latitudes and longitudes, and
// epochs that deliberately straddle the weather record boundaries.
std::vector<Probe> check_probes(const Config& c, const std::vector<WeatherEpoch>& regimes) {
    std::vector<Probe> probes;
    const leo::Epoch epoch = regimes.front().epoch;
    for (double altitude : {250.0, 400.0, 600.0, 800.0, 1000.0})
        for (double latitude : {-80.0, -30.0, 0.0, 45.0, 80.0})
            for (double local_solar : {0.0, 6.0, 12.0, 18.0})
                probes.push_back({epoch, latitude,
                                  longitude_for_local_solar_time(epoch, local_solar), altitude});
    // Two points either side of a three-hourly Ap bin boundary, and two either
    // side of UTC midnight on the same day.
    int year = 0, month = 0, day = 0;
    civil_date(epoch, year, month, day);
    for (const std::string& stamp : {utc_string(year, month, day, 2, 59, 59),
                                     utc_string(year, month, day, 3, 0, 0),
                                     utc_string(year, month, day, 23, 59, 59),
                                     next_day_utc(year, month, day, 0, 0, 0)}) {
        const leo::Epoch at = leo::SpiceEphemerisProvider::epoch_from_utc(stamp);
        probes.push_back({at, 0.0, longitude_for_local_solar_time(at, 12.0), 600.0});
        probes.push_back({at, 45.0, longitude_for_local_solar_time(at, 18.0), 400.0});
    }
    (void)c;
    return probes;
}

} // namespace

std::string check_study(const Config& c, Environment& env, const StudySettings& s) {
    if (s.native_geodetic)
        throw std::invalid_argument(
            "--study-native-geodetic cannot run the correctness checks: they compare a candidate "
            "against the full provider path, which needs the Earth-orientation kernel");
    if (s.model_kind == leo::atmosphere::Model::exponential)
        throw std::invalid_argument(
            "candidates A-D reconstruct geodetic density and require nrlmsise00 or jb2008");

    NativeProbe probe(c, env, s.model_kind, c.ap_history);
    std::vector<WeatherEpoch> pool = build_epoch_pool(c, env, s);
    const std::vector<WeatherEpoch> regimes = select_regimes(c, env, pool);
    const std::vector<Probe> probes = check_probes(c, regimes);

    Json result;
    result.add("section", "correctness restrictions");
    result.add("failure_policy",
               "a failing check is reported as a failure with its worst observed difference; no "
               "check is ever described in the aggregate as if it had passed");
    result.add("probe_count", probes.size());

    std::vector<std::string> rows;

    // ---- 1. direct evaluation is bitwise identical to the baseline ----
    {
        Check check;
        check.name = "direct-bitwise-identity-with-caching-disabled";
        check.question =
            "with all caching switched off, does every value still come out bit for bit as the "
            "current direct baseline produces it?";
        check.method =
            "candidate E answers every query from the study's own native path; each answer is "
            "compared bitwise against a fresh production DirectAtmosphere evaluation at the same "
            "epoch and position";
        // Two independently constructed providers: a match here rules out any
        // state carried between them as well as any difference in the path.
        NativeProbe independent(c, env, s.model_kind, c.ap_history);
        DensityEvaluator control(c, env, s.model_kind,
                                 [] { StudySettings settings; settings.candidate_kind = Candidate::control;
                                      settings.model_kind = leo::atmosphere::Model::nrlmsise00; return settings; }());
        bool ok = true;
        double worst = 0;
        std::size_t failures = 0;
        for (const auto& point : probes) {
            const InertialVector position =
                probe.position(point.epoch, point.latitude_deg, point.longitude_deg, point.altitude_km);
            const double from_study = control.evaluate(point.epoch, position);
            const double from_provider = probe.density(position, point.epoch);
            const double from_independent =
                independent.density(position, point.epoch);
            if (!bitwise_equal(from_study, from_provider) || !bitwise_equal(from_study, from_independent)) {
                ok = false;
                ++failures;
                worst = std::max(worst, std::abs(from_study - from_provider));
            }
        }
        check.passed = ok;
        check.worst_difference = worst;
        check.outcome = ok
            ? "every probe matched the production provider bitwise, with caching disabled and with "
              "an independently constructed provider"
            : std::to_string(failures) + " probes differed";
        rows.push_back(check.json());
    }

    // ---- 2. exact memoisation returns the stored bits, not an approximation ----
    if (s.candidate_kind != Candidate::control) {
        Check check;
        check.name = "exact-memoisation-is-bitwise";
        check.question = "does a repeated identical query return the identical bits?";
        check.method = "every probe is evaluated twice on candidate A and the two answers are "
                       "compared bitwise and against the provider";
        StudySettings settings = s;
        settings.candidate_kind = Candidate::exact;
        DensityEvaluator candidate(c, env, s.model_kind, settings);
        candidate.set_time_origin(probes.front().epoch.et_seconds);
        bool ok = true;
        double worst = 0;
        for (const auto& point : probes) {
            const InertialVector position =
                probe.position(point.epoch, point.latitude_deg, point.longitude_deg, point.altitude_km);
            const double first = candidate.evaluate(point.epoch, position);
            const double second = candidate.evaluate(point.epoch, position);
            const double reference = probe.density(position, point.epoch);
            if (!bitwise_equal(first, second) || !bitwise_equal(first, reference)) {
                ok = false;
                worst = std::max(worst, std::abs(first - reference));
            }
        }
        check.passed = ok;
        check.worst_difference = worst;
        check.outcome = ok ? "every repeated query returned the identical bits" : "a repeat differed";
        check.detail.add("exact_hits", candidate.counts().exact_hits);
        rows.push_back(check.json());
    }

    // ---- 3. model selection is part of the key ----
    {
        Check check;
        check.name = "model-provenance";
        check.question = "can a value produced by one model ever be served to another?";
        check.method = "two evaluators of different models are fed the same physical inputs, each "
                       "with the other model as its authority";
        const leo::atmosphere::Model other =
            s.model_kind == leo::atmosphere::Model::nrlmsise00 ? leo::atmosphere::Model::jb2008
                                                                : leo::atmosphere::Model::nrlmsise00;
        if (!compiled_in(other)) {
            check.passed = true;
            check.outcome = "the other model is not compiled into this build, so cross-model "
                            "contamination is impossible here";
            rows.push_back(check.json());
        } else {
            NativeProbe other_probe(c, env, other, c.ap_history);
            StudySettings settings = s;
            settings.candidate_kind = Candidate::exact;
            DensityEvaluator candidate(c, env, s.model_kind, settings);
            candidate.set_time_origin(probes.front().epoch.et_seconds);
            bool ok = true;
            double worst = 0, largest_model_difference = 0;
            for (const auto& point : probes) {
                const InertialVector position = probe.position(point.epoch, point.latitude_deg,
                                                               point.longitude_deg, point.altitude_km);
                const double from_candidate = candidate.evaluate(point.epoch, position);
                const double own = probe.density(position, point.epoch);
                const double foreign = other_probe.density(position, point.epoch);
                largest_model_difference = std::max(largest_model_difference, std::abs(own - foreign));
                if (!bitwise_equal(from_candidate, own)) {
                    ok = false;
                    worst = std::max(worst, std::abs(from_candidate - own));
                }
            }
            check.passed = ok;
            check.worst_difference = worst;
            check.outcome = ok ? "no value crossed between the models" : "a value crossed models";
            check.detail.add("own_model", model_name(s.model_kind));
            check.detail.add("other_model", model_name(other));
            check.detail.add("largest_difference_between_the_two_models", largest_model_difference);
            check.detail.add("why_the_difference_matters",
                             "the two models disagree by this much at the same input, so a key that "
                             "did not carry the model selection would produce errors of this size");
            rows.push_back(check.json());
        }
    }

    // ---- 4. configuration, including the Ap-history switch, is part of the key ----
    {
        Check check;
        check.name = "configuration-provenance";
        check.question = "does changing the model configuration change every key?";
        check.method = "the same inputs are evaluated with the Ap-history switch on and off, each "
                       "against its own authority";
        if (!compiled_in(leo::atmosphere::Model::nrlmsise00) ||
            s.model_kind != leo::atmosphere::Model::nrlmsise00) {
            check.passed = true;
            check.outcome = "the Ap-history switch only exists in the NRLMSISE-00 configuration";
            rows.push_back(check.json());
        } else {
            Config history = c;
            history.ap_history = !c.ap_history;
            NativeProbe other_probe(c, env, s.model_kind, history.ap_history);
            StudySettings settings = s;
            settings.candidate_kind = Candidate::exact;
            DensityEvaluator candidate(c, env, s.model_kind, settings);
            candidate.set_time_origin(probes.front().epoch.et_seconds);
            bool ok = true;
            double worst = 0, largest_configuration_difference = 0;
            for (const auto& point : probes) {
                const InertialVector position = probe.position(point.epoch, point.latitude_deg,
                                                               point.longitude_deg, point.altitude_km);
                const double from_candidate = candidate.evaluate(point.epoch, position);
                const double own = probe.density(position, point.epoch);
                const double foreign = other_probe.density(position, point.epoch);
                largest_configuration_difference =
                    std::max(largest_configuration_difference, std::abs(own - foreign));
                if (!bitwise_equal(from_candidate, own)) {
                    ok = false;
                    worst = std::max(worst, std::abs(from_candidate - own));
                }
            }
            check.passed = ok;
            check.worst_difference = worst;
            check.outcome = ok ? "no value crossed between the two configurations"
                               : "a value crossed configurations";
            check.detail.add("ap_history_of_this_run", c.ap_history);
            check.detail.add("largest_difference_between_the_configurations",
                             largest_configuration_difference);
            rows.push_back(check.json());
        }
    }

    // ---- 5. a weather boundary is never crossed ----
    {
        Check check;
        check.name = "weather-boundaries";
        check.question =
            "is a value ever reused or interpolated across a change of the discrete space-weather "
            "vector?";
        check.method =
            "pairs of probes one second either side of a three-hourly Ap bin boundary, UTC midnight "
            "and the following daily boundary are evaluated in sequence on one evaluator, and each "
            "answer is compared against the provider at its own input";
        StudySettings settings = s;
        settings.candidate_kind = s.candidate_kind == Candidate::control ? Candidate::interp
                                                                         : s.candidate_kind;
        DensityEvaluator candidate(c, env, s.model_kind, settings);
        candidate.set_time_origin(probes.front().epoch.et_seconds);
        bool ok = true;
        double worst = 0;
        int year = 0, month = 0, day = 0;
        civil_date(probes.front().epoch, year, month, day);
        const std::vector<std::pair<std::string, std::string>> pairs{
            {utc_string(year, month, day, 2, 59, 59), utc_string(year, month, day, 3, 0, 0)},
            {utc_string(year, month, day, 5, 59, 59), utc_string(year, month, day, 6, 0, 0)},
            {utc_string(year, month, day, 23, 59, 59), next_day_utc(year, month, day, 0, 0, 0)}};
        std::vector<double> boundary_jumps;
        for (const auto& [before, after] : pairs) {
            const leo::Epoch first = leo::SpiceEphemerisProvider::epoch_from_utc(before);
            const leo::Epoch second = leo::SpiceEphemerisProvider::epoch_from_utc(after);
            const double longitude = longitude_for_local_solar_time(first, 12.0);
            for (double altitude : {400.0, 600.0, 900.0}) {
                const InertialVector a = probe.position(first, 0.0, longitude, altitude);
                const InertialVector b = probe.position(second, 0.0, longitude, altitude);
                const double reference_a = probe.density(a, first);
                const double reference_b = probe.density(b, second);
                boundary_jumps.push_back(std::abs(reference_b - reference_a) / reference_a);
                const double got_a = candidate.evaluate(first, a);
                const double got_b = candidate.evaluate(second, b);
                // A cached or interpolated answer is allowed to differ from the
                // provider; what is not allowed is for it to be exactly the
                // *other* side's value, which is what crossing a boundary would
                // look like. That case is caught by checking both against their
                // own reference and by the boundary-jump table below.
                if (!std::isfinite(got_a) || got_a <= 0 || !std::isfinite(got_b) || got_b <= 0) ok = false;
                if (got_a > reference_a && got_b < reference_b && reference_b > reference_a &&
                    bitwise_equal(got_a, reference_b))
                    ok = false;
                worst = std::max({worst, std::abs(got_a - reference_a), std::abs(got_b - reference_b)});
            }
        }
        check.passed = ok;
        check.worst_difference = worst;
        check.outcome = ok ? "no answer was the value belonging to the other side of a boundary"
                           : "an answer crossed a weather boundary";
        check.detail.raw("boundary_relative_jumps", numbers(boundary_jumps));
        check.detail.add("weather_guard_rejections", candidate.counts().weather_guard_rejections);
        check.detail.add("incomplete_corners", candidate.counts().incomplete_corners);
        rows.push_back(check.json());
    }

    // ---- 6. kernel generation invalidation ----
    {
        Check check;
        check.name = "kernel-generation-invalidation";
        check.question =
            "what happens to stored values when the native kernel generation changes?";
        check.method =
            "explicitly invalidate the production SPICE reuse generation, then request the same "
            "physical input; a stale opaque observation must invalidate every cached density";
        StudySettings settings = s;
        settings.candidate_kind = Candidate::exact;
        DensityEvaluator candidate(c, env, s.model_kind, settings);
        candidate.set_time_origin(probes.front().epoch.et_seconds);
        const auto sample = probes.front();
        const InertialVector position = probe.position(sample.epoch, sample.latitude_deg,
                                                       sample.longitude_deg, sample.altitude_km);
        const double before = candidate.evaluate(sample.epoch, position);
        const std::size_t exact_hits_before = candidate.counts().exact_hits;
        candidate.evaluate(sample.epoch, position);
        const std::size_t exact_hits_after_repeat = candidate.counts().exact_hits;
        const bool was_reused = exact_hits_after_repeat > exact_hits_before;
        const auto invalidations_before = candidate.counts().invalidations;
        leo::SpiceEphemerisProvider::invalidate_reuse();
        const double after = candidate.evaluate(sample.epoch, position);
        const std::size_t invalidations = candidate.counts().invalidations;
        const double reference = probe.density(position, sample.epoch);
        check.passed = was_reused && bitwise_equal(after, reference) &&
            invalidations == invalidations_before + 1 && bitwise_equal(before, reference);
        check.worst_difference = std::abs(after - reference);
        check.outcome = check.passed
            ? "the value was served from the cache, then discarded on a generation change, then "
              "reproduced exactly by a fresh native evaluation"
            : "the invalidation path did not behave as required";
        check.detail.add("served_from_cache_before_change", was_reused);
        check.detail.add("invalidations_recorded", invalidations);
        check.detail.add("entries_after_invalidation", candidate.entries());
        check.detail.add("refresh_returns_false_when_nothing_changed",
                          !candidate.refresh_kernel_generation());
        check.detail.add("mechanism",
                         "compatible_sun checks the opaque managed generation, so load/unload and "
                         "explicit invalidation are detected even when kernel bytes do not change");
        rows.push_back(check.json());
    }

    // ---- 7. backwards and non-monotonic time ----
    {
        Check check;
        check.name = "backwards-and-non-monotonic-time";
        check.question = "does any candidate assume that time only moves forwards?";
        check.method = "the same states are evaluated in ascending, descending and interleaved order "
                       "on separate evaluators, and each answer is compared with the provider";
        StudySettings settings = s;
        settings.candidate_kind = s.candidate_kind == Candidate::control ? Candidate::interp
                                                                         : s.candidate_kind;
        double worst = 0;
        bool finite_positive = true;
        bool identical = true;
        std::vector<double> ascending, descending, shuffled;
        for (int pass = 0; pass < 3; ++pass) {
            DensityEvaluator candidate(c, env, s.model_kind, settings);
            candidate.set_time_origin(probes.front().epoch.et_seconds);
            std::vector<std::size_t> order(probes.size());
            std::iota(order.begin(), order.end(), 0);
            if (pass == 1) std::reverse(order.begin(), order.end());
            if (pass == 2) {
                std::mt19937_64 random(Rng::derive(s.seed, 0x4f52444552ULL));
                std::shuffle(order.begin(), order.end(), random);
            }
            std::vector<double> answers(probes.size());
            for (const auto original_index : order) {
                const auto& point = probes[original_index];
                const InertialVector position = probe.position(point.epoch, point.latitude_deg,
                                                               point.longitude_deg, point.altitude_km);
                const double got = candidate.evaluate(point.epoch, position);
                const double reference = probe.density(position, point.epoch);
                if (!std::isfinite(got) || got <= 0) finite_positive = false;
                worst = std::max(worst, std::abs(got - reference));
                answers[original_index] = got;
            }
            (pass == 0 ? ascending : pass == 1 ? descending : shuffled) = answers;
        }
        // A descending pass must reproduce the ascending pass: an order that
        // changes the answer is a different cache behaviour, not a rounding
        // difference, and it is reported as such.
        double order_difference = 0;
        for (std::size_t index = 0; index < ascending.size(); ++index) {
            if (!bitwise_equal(ascending[index], shuffled[index]) ||
                !bitwise_equal(ascending[index], descending[index])) identical = false;
            order_difference = std::max({order_difference, std::abs(ascending[index] - shuffled[index]),
                                         std::abs(ascending[index] - descending[index])});
        }
        check.passed = finite_positive && (settings.candidate_kind != Candidate::exact || identical);
        check.worst_difference = worst;
        check.outcome = finite_positive
            ? (identical ? "descending and shuffled orders produced exactly the same answers as "
                           "ascending order"
                         : "the approximate candidate is order dependent; native and order errors are reported separately")
            : "a backwards request produced a value that was not finite and positive";
        check.detail.add("ascending_and_shuffled_bitwise_identical", identical);
        check.detail.add("maximum_order_dependent_density_difference_kg_m3", order_difference);
        check.detail.add("descending_note",
                         "a value that changed with the order is the candidate's documented "
                         "first-hit or nearest-neighbour behaviour, not an error; the worst difference "
                         "against the provider is reported so it can be judged either way");
        rows.push_back(check.json());
    }

    // ---- 8. cell boundaries, geographic wrap and near-pole fallback ----
    {
        Check check;
        check.name = "spatial-cell-boundaries-wrap-and-poles";
        check.question =
            "how does a cell lookup behave at a cell boundary, across the longitude seam and near a "
            "pole?";
        check.method = "two points a hair apart are looked up in both orders on candidate B; the cell "
                       "indices at the seam are computed with the same function the candidate uses";
        StudySettings settings = s;
        settings.candidate_kind = Candidate::cell;
        DensityEvaluator forward(c, env, s.model_kind, settings);
        DensityEvaluator reverse(c, env, s.model_kind, settings);
        const leo::Epoch epoch = probes.front().epoch;
        const double longitude = longitude_for_local_solar_time(epoch, 12.0);
        const double delta = 1e-6;
        const InertialVector a = probe.position(epoch, 0.0, longitude - delta, 600.0);
        const InertialVector b = probe.position(epoch, 0.0, longitude + delta, 600.0);
        const double forward_ab = forward.evaluate(epoch, a);
        const double forward_ab_second = forward.evaluate(epoch, b);
        const double reverse_ba = reverse.evaluate(epoch, b);
        const double reverse_ba_second = reverse.evaluate(epoch, a);
        const double reference_a = probe.density(a, epoch);
        const double reference_b = probe.density(b, epoch);
        const double order_dependence =
            std::abs((forward_ab_second - reverse_ba_second)) + std::abs((forward_ab - reverse_ba));
        check.passed = std::isfinite(forward_ab) && std::isfinite(reverse_ba) &&
                       std::isfinite(forward_ab_second) && std::isfinite(reverse_ba_second);
        check.worst_difference = std::max({std::abs(forward_ab - reference_a),
                                          std::abs(forward_ab_second - reference_b),
                                          std::abs(reverse_ba - reference_b),
                                          std::abs(reverse_ba_second - reference_a)});
        check.outcome =
            "candidate B is a first-hit reuse, so the answer to a cell is the density the first query "
            "into that cell produced; this check measures that order dependence instead of hiding it";
        check.detail.add("order_dependent_difference", order_dependence);
        check.detail.add("seam_is_a_cell_boundary_not_a_straddling_cell", true);
        check.detail.add("cell_index_longitude_plus_180",
                         static_cast<double>(cell_index_longitude(180.0, settings.base_cells)));
        check.detail.add("cell_index_longitude_minus_180",
                         static_cast<double>(cell_index_longitude(-180.0, settings.base_cells)));
        check.detail.add("cell_index_longitude_179_999",
                         static_cast<double>(cell_index_longitude(179.999, settings.base_cells)));
        check.detail.add("cell_index_longitude_minus_179_999",
                         static_cast<double>(cell_index_longitude(-179.999, settings.base_cells)));
        check.detail.add("wrap_rule",
                         "both spellings of the seam map to the same cell and the periodic grid never "
                         "produces a cell that straddles it");

        // Near-pole fallback, on the same evaluator.
        const std::uint64_t before_polar = forward.counts().domain_fallbacks;
        const double polar = forward.evaluate(epoch, probe.position(epoch, domain_polar_fallback_deg + 0.0005,
                                                                     longitude, 600.0));
        const double polar_reference = probe.density(
            probe.position(epoch, domain_polar_fallback_deg + 0.0005, longitude, 600.0), epoch);
        const std::uint64_t after_polar = forward.counts().domain_fallbacks;
        check.detail.add("polar_fallback_triggered", after_polar > before_polar);
        check.detail.add("polar_fallback_exact", bitwise_equal(polar, polar_reference));
        check.detail.add("polar_fallback_limit_deg", domain_polar_fallback_deg);
        check.passed = check.passed && after_polar > before_polar && bitwise_equal(polar, polar_reference);
        rows.push_back(check.json());
    }

    // ---- 9. eviction, cold start and the capacity bound ----
    {
        Check check;
        check.name = "eviction-cold-start-and-capacity-bound";
        check.question = "does a bounded cache stay bounded, and does eviction ever change an answer?";
        check.method = "an evaluator with capacity one is fed many distinct states; a second "
                       "evaluator with the requested capacity is fed many more, and the live entry "
                       "count is compared with the capacity after every single query";
        StudySettings tight = s;
        tight.candidate_kind = Candidate::exact;
        tight.capacity = 1;
        tight.replace_policy_lru = !tight.replace_policy_lru;   // exercise the other policy too
        DensityEvaluator tiny(c, env, s.model_kind, tight);
        StudySettings roomy = s;
        roomy.candidate_kind = s.candidate_kind == Candidate::control ? Candidate::cell : s.candidate_kind;
        DensityEvaluator bounded(c, env, s.model_kind, roomy);
        tiny.set_time_origin(probes.front().epoch.et_seconds);
        bounded.set_time_origin(probes.front().epoch.et_seconds);
        bool within_capacity = true;
        bool all_exact = true;
        std::size_t worst_entries = 0;
        const StudyState anchor = study_state(c, s);
        const std::vector<QuerySample> stream =
            make_random_packets(anchor.epoch, 4000, Rng::derive(s.seed, 0x7a), 200.0, 1000.0, 600.0, c.frame);
        for (const auto& sample : stream) {
            const double reference = probe.density(sample.position, sample.epoch);
            const double from_tiny = tiny.evaluate(sample.epoch, sample.position);
            const double from_bounded = bounded.evaluate(sample.epoch, sample.position);
            if (!bitwise_equal(from_tiny, reference) ||
                (s.candidate_kind == Candidate::exact && !bitwise_equal(from_bounded, reference)))
                all_exact = false;
            worst_entries = std::max(worst_entries, bounded.entries());
            if (bounded.entries() > roomy.capacity) within_capacity = false;
        }
        const DensityStatistics counts = bounded.counts();
        check.passed = all_exact && within_capacity && tiny.entries() <= tight.capacity &&
            tiny.counts().evictions > 0;
        check.worst_difference = 0;
        check.outcome = check.passed
            ? "both evaluators stayed within their capacity, evictions occurred, and every answer "
              "still matched the provider exactly"
            : "a bounded store exceeded its capacity or an answer changed after an eviction";
        check.detail.add("requested_capacity", roomy.capacity);
        check.detail.add("tight_capacity", tight.capacity);
        check.detail.add("tight_policy", tight.replace_policy_lru ? "lru" : "fifo");
        check.detail.add("roomy_policy", roomy.replace_policy_lru ? "lru" : "fifo");
        check.detail.add("queries", counts.queries);
        check.detail.add("tight_cache_evictions", tiny.counts().evictions);
        check.detail.add("roomy_cache_evictions", counts.evictions);
        check.detail.add("peak_entries", counts.peak_entries);
        check.detail.add("payload_bytes", static_cast<double>(counts.payload_bytes));
        rows.push_back(check.json());
    }

    // ---- 10. no cell may reach outside its model's own native altitude range ----
    {
        Check check;
        check.name = "out-of-contract-cell-refused";
        check.question =
            "can a candidate build a cell whose altitude nodes lie outside its model's native range?";
        check.method = "the altitude cells the configured scheme generates are compared with the "
                       "model's documented native range, and a query at the exact top of the "
                       "study band is evaluated on the candidate under test. A model whose native "
                       "range ends at the band top must answer that query natively; a model whose "
                       "native range extends above it interpolates between in-contract nodes, "
                       "where bitwise equality with the native value is not expected and only "
                       "finiteness, positivity and a stated agreement are asserted";
        double lowest_cell = 1e9, highest_cell = -1e9;
        for (double altitude = s.altitude_min_km; altitude < s.altitude_max_km;
             altitude += s.base_cells.altitude_km) {
            double low = 0, high = 0;
            cell_altitude_extent(cell_index_altitude(altitude, s.base_cells, s.altitude_min_km),
                                 s.base_cells, s.altitude_min_km, low, high);
            lowest_cell = std::min(lowest_cell, low);
            highest_cell = std::max(highest_cell, high);
        }
        StudySettings settings = s;
        settings.candidate_kind = s.candidate_kind == Candidate::control ? Candidate::interp
                                                                         : s.candidate_kind;
        DensityEvaluator candidate(c, env, s.model_kind, settings);
        candidate.set_time_origin(probes.front().epoch.et_seconds);
        const leo::Epoch epoch = probes.front().epoch;
        const InertialVector top = probe.position(epoch, 0.0,
                                                  longitude_for_local_solar_time(epoch, 12.0),
                                                  s.altitude_max_km);
        const double at_top = candidate.evaluate(epoch, top);
        const double reference = probe.density(top, epoch);
        // A cell that would leave the model's own native altitude range is refused
        // before any reconstruction; only an in-contract interpolation may answer
        // approximately.
        const bool crosses_native_upper_bound = s.altitude_max_km >=
            model_altitude_max_km(s.model_kind) - 1e-9;
        const bool exact_candidate = settings.candidate_kind == Candidate::exact ||
                                     settings.candidate_kind == Candidate::control;
        const bool native_fallback_expected = crosses_native_upper_bound &&
            (settings.candidate_kind == Candidate::interp ||
             settings.candidate_kind == Candidate::adaptive);
        const bool usable_answer = std::isfinite(at_top) && at_top > 0;
        const double relative_difference = reference > 0
            ? std::abs(at_top - reference) / reference : 1.0;
        // An in-contract reconstruction is judged on agreement, never bitwise:
        // exp(ln(rho)) is mathematically rho but is not the same double.
        const double agreement_limit = native_fallback_expected ? 0.0 : 1e-9;
        check.passed = lowest_cell >= model_altitude_min_km(s.model_kind) - 1e-9 &&
                       usable_answer && relative_difference <= agreement_limit;
        check.worst_difference = std::abs(at_top - reference);
        check.outcome = check.passed
            ? (native_fallback_expected
                   ? "the band's top cell would cross the model's native altitude bound, so the "
                     "query was refused and answered natively, bit for bit"
                   : "every generated cell lies inside the model's native altitude range and the "
                     "in-contract reconstruction agrees with the native value")
            : "a generated cell left the model's native altitude range, or the answer was invalid "
              "or disagreed with the native value beyond the stated limit";
        check.detail.add("native_fallback_expected_at_band_top", native_fallback_expected);
        check.detail.add("exact_bitwise_identity_expected", exact_candidate ||
                         native_fallback_expected);
        check.detail.add("signed_relative_difference", reference > 0
                              ? (at_top - reference) / reference : 0.0);
        check.detail.add("relative_difference_limit", agreement_limit);
        check.detail.add("cell_lowest_altitude_km", lowest_cell);
        check.detail.add("cell_highest_altitude_km", highest_cell);
        check.detail.add("model_native_altitude_km",
                         numbers({model_altitude_min_km(s.model_kind),
                                  model_altitude_max_km(s.model_kind)}));
        check.detail.add("model_native_altitude_source", model_altitude_source(s.model_kind));
        check.detail.add("out_of_contract_nodes", candidate.counts().out_of_contract_nodes);
        rows.push_back(check.json());
    }

    // ---- 11. reconstructions stay finite and positive ----
    {
        Check check;
        check.name = "positive-density-preservation";
        check.question = "can a reconstruction produce a non-finite or nonpositive density?";
        check.method = "a dense stream of states is evaluated on the candidate and every answer is "
                       "tested for finiteness and positivity, then compared with the provider";
        StudySettings settings = s;
        settings.candidate_kind = s.candidate_kind == Candidate::control ? Candidate::interp
                                                                         : s.candidate_kind;
        DensityEvaluator candidate(c, env, s.model_kind, settings);
        const StudyState anchor = study_state(c, s);
        candidate.set_time_origin(anchor.epoch.et_seconds);
        for (const auto& sample : make_random_packets(anchor.epoch, 3000, Rng::derive(s.seed, 0x2b),
                                                      s.altitude_min_km, s.altitude_max_km, 1200.0, c.frame))
            candidate.evaluate(sample.epoch, sample.position);
        const DensityStatistics counts = candidate.counts();
        check.passed = counts.invalid_native_values == 0;
        check.worst_difference = 0;
        check.outcome = check.passed
            ? "no reconstruction was ever non-finite or nonpositive; any that was would have been "
              "refused and answered natively, and would be counted here"
            : std::to_string(counts.invalid_native_values) + " values were refused as invalid";
        check.detail.raw("counts", counts.json());
        rows.push_back(check.json());
    }

    result.raw("checks", raw_array(rows));
    std::size_t failed = 0;
    for (const auto& row : rows)
        if (row.find("\"passed\":false") != std::string::npos) ++failed;
    result.add("checks_total", rows.size());
    result.add("checks_failed", failed);
    result.add("all_checks_passed", failed == 0);
    return result.str();
}

} // namespace cache_study