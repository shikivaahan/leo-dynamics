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
        // The second evaluation of each probe is served before the geometry and
        // the native input are rebuilt. The UTC calendar and the space-weather
        // record read are not skipped, because the weather identity is part of
        // the key; this counter names exactly what was avoided.
        check.detail.add("acquisition_skips", candidate.counts().acquisition_skips);
        check.detail.add("acquisition_skip_rule",
                         "one per repeated probe, counted when the memo answered before the "
                         "Earth-fixed rotation, the geodetic conversion, the local solar time and "
                         "the native model call; the weather identity read still happened, because "
                         "serving a stored value without checking its weather identity is exactly "
                         "the error this study's weather guard exists to prevent");
        check.detail.add("repeats_served_before_acquisition",
                         candidate.counts().acquisition_skips == probes.size());
        if (candidate.counts().acquisition_skips != probes.size())
            check.passed = false;
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

    // ---- 5. isolated weather contexts, including actual interior DTC transitions ----
    {
        Check check;
        check.name = "weather-boundaries";
        check.question = "can an evaluator carry a value across a changed discrete input vector?";
        check.method =
            "warm the pre-transition context, then compare the post-transition result bitwise with "
            "an isolated evaluator at the same grid origin; bracket actual rising and falling "
            "integer-DTC transitions within archived hours, plus daily, Ap-bin and leap boundaries";
        StudySettings settings = s;
        if (settings.candidate_kind == Candidate::control) settings.candidate_kind = Candidate::interp;
        bool ok = true;
        double worst = 0;
        std::size_t changed_contexts = 0;
        std::vector<std::string> evidence;
        const auto exercise = [&](leo::Epoch first, leo::Epoch second, const std::string& kind) {
            const auto before_weather = weather_identity(c, env, s.model_kind, first);
            const auto after_weather = weather_identity(c, env, s.model_kind, second);
            const bool changed = before_weather != after_weather;
            changed_contexts += changed;
            const double origin = first.et_seconds - settings.base_cells.time_s * 0.5;
            DensityEvaluator warmed(c, env, s.model_kind, settings);
            DensityEvaluator isolated(c, env, s.model_kind, settings);
            warmed.set_time_origin(origin);
            isolated.set_time_origin(origin);
            const double longitude = longitude_for_local_solar_time(first, 12.0);
            const auto a = probe.position(first, 0.0, longitude, 600.37);
            const auto b = probe.position(second, 0.0, longitude, 600.37);
            const double before = warmed.evaluate(first, a);
            const auto counts_before = warmed.counts();
            const double after = warmed.evaluate(second, b);
            const double clean_after = isolated.evaluate(second, b);
            const auto counts_after = warmed.counts();
            const bool isolated_match = bitwise_equal(after, clean_after);
            const bool no_pretransition_hit = !changed ||
                (counts_after.cell_reuses == counts_before.cell_reuses &&
                 counts_after.exact_hits == counts_before.exact_hits);
            ok = ok && std::isfinite(before) && before > 0 && std::isfinite(after) && after > 0 &&
                (!changed || (isolated_match && no_pretransition_hit));
            if (changed) worst = std::max(worst, std::abs(after - clean_after));
            Json row;
            row.add("kind", kind);
            row.add("before_utc", utc_label(first));
            row.add("after_utc", utc_label(second));
            row.add("before_epoch_et_seconds", first.et_seconds);
            row.add("after_epoch_et_seconds", second.et_seconds);
            row.add("discrete_inputs_changed", changed);
            row.raw("before_inputs", read_weather(c, env, first, c.ap_history).json());
            row.raw("after_inputs", read_weather(c, env, second, c.ap_history).json());
            row.add("posttransition_matches_isolated_evaluator_bitwise", isolated_match);
            row.add("pretransition_hit_refused", no_pretransition_hit);
            row.add("native_calls_after_transition",
                    counts_after.native_calls - counts_before.native_calls);
            row.add("weather_guard_rejections_after_transition",
                    counts_after.weather_guard_rejections - counts_before.weather_guard_rejections);
            row.add("guard_fallbacks_after_transition",
                    counts_after.guard_fallbacks - counts_before.guard_fallbacks);
            row.add("before_density_kg_m3", before);
            row.add("after_density_kg_m3", after);
            row.add("isolated_after_density_kg_m3", clean_after);
            row.add("fresh_native_after_density_kg_m3", probe.density(b, second));
            evidence.push_back(row.str());
        };
        for (const auto& regime : regimes) {
            int year = 0, month = 0, day = 0;
            civil_date(regime.epoch, year, month, day);
            for (int hour : {3, 6, 12})
                exercise(leo::SpiceEphemerisProvider::epoch_from_utc(
                             utc_string(year, month, day, hour - 1, 59, 59)),
                         leo::SpiceEphemerisProvider::epoch_from_utc(
                             utc_string(year, month, day, hour, 0, 0)), "three-hourly-Ap");
            exercise(leo::SpiceEphemerisProvider::epoch_from_utc(
                         utc_string(year, month, day, 23, 59, 59)),
                     leo::SpiceEphemerisProvider::epoch_from_utc(
                         next_day_utc(year, month, day, 0, 0, 0)), "daily");
        }
        exercise(leo::SpiceEphemerisProvider::epoch_from_utc("2016-12-31T23:59:60"),
                 leo::SpiceEphemerisProvider::epoch_from_utc("2017-01-01T00:00:00"), "leap-second");
        bool rising = false, falling = false;
        if (s.model_kind == leo::atmosphere::Model::jb2008) {
            for (const auto& regime : regimes) {
                for (int hour = 0; hour < 24 && !(rising && falling); ++hour) {
                    double lo = regime.epoch.et_seconds + hour * 3600.0 + 1.0;
                    double hi = regime.epoch.et_seconds + hour * 3600.0 + 3599.0;
                    const auto input = [&](double et) {
                        return env.weather.jb2008(leo::SpiceEphemerisProvider::utc_calendar(leo::Epoch{et}));
                    };
                    const double initial = input(lo).dstdtc_k, final = input(hi).dstdtc_k;
                    if (initial == final || (final > initial ? rising : falling)) continue;
                    while (hi - lo > 1e-5) {
                        const double mid = lo + (hi - lo) * 0.5;
                        if (input(mid).dstdtc_k == initial) lo = mid; else hi = mid;
                    }
                    exercise(leo::Epoch{lo}, leo::Epoch{hi},
                             final > initial ? "interior-DTC-rising" : "interior-DTC-falling");
                    if (final > initial) rising = true; else falling = true;
                }
                if (rising && falling) break;
            }
            ok = ok && rising && falling;
        }
        check.passed = ok && changed_contexts > 0;
        check.worst_difference = worst;
        check.outcome = check.passed
            ? "changed weather contexts match isolated evaluators and never hit a pretransition value"
            : "a changed context failed isolation, or a required archived DTC direction was not found";
        check.detail.add("changed_contexts_tested", changed_contexts);
        check.detail.add("interior_DTC_rising_found", rising);
        check.detail.add("interior_DTC_falling_found", falling);
        check.detail.raw("boundary_evidence", raw_array(evidence));
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
        // Both exact spellings map to cell zero. Points just west and east are
        // in the last and first cells: that is a boundary, not one straddling cell.
        const auto seam_index = [&](double degrees) {
            return cell_index_longitude(degrees, settings.base_cells);
        };
        const std::int64_t seam_cell = seam_index(180.0);
        const std::int64_t last_cell =
            static_cast<std::int64_t>(settings.base_cells.longitude_cells()) - 1;
        const bool seam_is_one_cell = seam_index(-180.0) == seam_cell && seam_cell == 0;
        const bool seam_is_a_boundary_not_a_straddling_cell = seam_is_one_cell &&
            seam_index(180.0 - 1e-9) == last_cell && seam_index(-180.0 + 1e-9) == 0;
        check.detail.add("seam_cell_index", static_cast<double>(seam_cell));
        check.detail.add("periodic_grid_last_cell_index", static_cast<double>(last_cell));
        check.detail.add("seam_is_one_cell_for_both_spellings", seam_is_one_cell);
        check.detail.add("seam_is_a_cell_boundary_not_a_straddling_cell",
                         seam_is_a_boundary_not_a_straddling_cell);
        check.detail.add("cell_index_longitude_plus_180",
                         static_cast<double>(cell_index_longitude(180.0, settings.base_cells)));
        check.detail.add("cell_index_longitude_minus_180",
                         static_cast<double>(cell_index_longitude(-180.0, settings.base_cells)));
        check.detail.add("cell_index_longitude_179_999",
                         static_cast<double>(cell_index_longitude(179.999, settings.base_cells)));
        check.detail.add("cell_index_longitude_minus_179_999",
                         static_cast<double>(cell_index_longitude(-179.999, settings.base_cells)));
        check.detail.add("wrap_rule",
                         seam_is_a_boundary_not_a_straddling_cell
                             ? "both exact seam spellings map to cell zero; the immediate west "
                               "and east neighbourhoods map to the last and first periodic cells"
                             : "seam spellings or the west/east periodic cell boundary disagreed");
        DensityEvaluator selected_candidate(c, env, s.model_kind, s);
        selected_candidate.set_time_origin(epoch.et_seconds);
        std::vector<std::string> selected_boundaries;
        for (double boundary_longitude : {-180.00001, -180.0, -179.99999, 179.99999, 180.0, 180.00001}) {
            const auto position = probe.position(epoch, 0.37, boundary_longitude, 600.37);
            const auto before = selected_candidate.counts();
            const double reconstructed = selected_candidate.evaluate(epoch, position);
            const double native = probe.density(position, epoch);
            const auto after = selected_candidate.counts();
            check.passed = check.passed && std::isfinite(reconstructed) && reconstructed > 0;
            Json sample;
            sample.add("longitude_deg", boundary_longitude);
            sample.add("fresh_native_density_kg_m3", native);
            sample.add("reconstructed_density_kg_m3", reconstructed);
            sample.add("signed_relative_error", (reconstructed - native) / native);
            sample.add("native_calls", after.native_calls - before.native_calls);
            sample.add("interpolations", after.interpolations - before.interpolations);
            sample.add("guard_fallbacks", after.guard_fallbacks - before.guard_fallbacks);
            selected_boundaries.push_back(sample.str());
        }
        check.detail.raw("selected_candidate_seam_evidence", raw_array(selected_boundaries));

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
        check.passed = check.passed && after_polar > before_polar && bitwise_equal(polar, polar_reference) &&
                       seam_is_a_boundary_not_a_straddling_cell;
        check.outcome = seam_is_a_boundary_not_a_straddling_cell
            ? "periodic indexing is consistent; candidate B first-hit order dependence is measured"
            : "the seam spellings or neighbouring periodic indices are inconsistent";
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
        check.method = "inspect configured altitude cell bounds, then evaluate the band top. "
                       "Zero-weight outer nodes are not required; only an actually refused node "
                       "requires native fallback. In-contract log-density reconstruction need not "
                       "be bitwise identical to exp(log(native density)).";
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
        const bool out_of_contract_refused = candidate.counts().out_of_contract_nodes > 0;
        const bool exact_candidate = settings.candidate_kind == Candidate::exact ||
                                     settings.candidate_kind == Candidate::control;
        const bool native_fallback_expected = out_of_contract_refused;
        const bool usable_answer = std::isfinite(at_top) && at_top > 0;
        const double relative_difference = reference > 0
            ? std::abs(at_top - reference) / reference : 1.0;
        check.detail.add("absolute_relative_difference", relative_difference);
        check.detail.add("relative_difference_note",
                         "reported, never thresholded: only a path that must reproduce the native "
                         "value exactly is judged on it");
        // Bitwise identity is required only where the answer is required to *be*
        // the native value: the exact and control candidates, which cache
        // nothing and reconstruct nothing, and an interpolation or adaptive
        // candidate at a band top whose cell would cross the model's own native
        // bound and is therefore refused. Everywhere else the answer is a
        // reconstruction and is judged on being finite, positive and reported.
        // A first-hit cell candidate inside JB2008's range returns the density
        // some earlier query in that cell produced; demanding bitwise identity
        // of it would fail a correct implementation for being what it is.
        const bool bitwise_required = exact_candidate || native_fallback_expected;
        const bool answered_as_required = bitwise_required ? bitwise_equal(at_top, reference)
                                                           : usable_answer;
        check.passed = lowest_cell >= model_altitude_min_km(s.model_kind) - 1e-9 &&
                       usable_answer && answered_as_required;
        check.worst_difference = std::abs(at_top - reference);
        check.outcome = check.passed
            ? (native_fallback_expected
                   ? "the band's top cell would cross the model's native altitude bound, so the "
                     "query was refused and answered natively, bit for bit"
                   : bitwise_required
                         ? "every generated cell lies inside the model's native altitude range and "
                           "this candidate is required to reproduce the native value exactly, which "
                           "it did"
                         : "every generated cell lies inside the model's native altitude range and "
                           "the in-contract reconstruction is finite and positive; its signed "
                           "difference from the native value is reported and is not required to be "
                           "zero, because this candidate reconstructs by design")
            : bitwise_required
                  ? "a generated cell left the model's native altitude range, or the answer was "
                    "invalid, or a path required to reproduce the native value bit for bit did not"
                  : "a generated cell left the model's native altitude range, or the answer was not "
                    "finite and positive";
        check.detail.add("native_fallback_expected_at_band_top", native_fallback_expected);
        check.detail.add("bitwise_identity_required", bitwise_required);
        check.detail.add("bitwise_identity_rule",
                         "required only for the exact and control candidates and for a refused "
                         "out-of-contract interpolation or adaptive cell; an in-contract "
                         "reconstruction is never judged bitwise, because exp(ln(rho)) is "
                         "mathematically rho but is not the same double");
        check.detail.add("signed_relative_difference", reference > 0
                              ? (at_top - reference) / reference : 0.0);
        check.detail.add("band_top_against_model_native_upper_bound",
                         s.altitude_max_km - model_altitude_max_km(s.model_kind));
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