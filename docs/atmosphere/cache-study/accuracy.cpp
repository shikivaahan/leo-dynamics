// ATM-CACHE-001 Phase 5: density reconstruction accuracy.
//
// Every reconstructed value is compared against a fresh production
// DirectAtmosphere evaluation at the *same exact physical input*, so an error
// can never be mistaken for the difference between two spacecraft states.
// Construction and query streams use independent seeds. Native cache misses
// may populate the online cache, but no independently computed reference
// density is supplied to the cache being scored.
#include "support.hpp"

namespace cache_study {
namespace {

struct Holdout {
    std::string name, description;
    std::vector<QuerySample> samples;
    std::vector<std::string> physical_strata;
};

Holdout uniform_holdout(const StudyState& reference, std::size_t count, std::uint64_t seed,
                        double time_span_s, const std::string& frame) {
    Holdout holdout;
    holdout.name = "uniform-random";
    holdout.description =
        "independent uniform samples over the whole altitude band and the construction time window. "
        "Real trajectory queries are not uniform, so this holdout mostly measures the fallback path; "
        "it is the pessimistic bound, not the expected one.";
    holdout.samples = make_random_packets(reference.epoch, count, seed, domain_altitude_min_km,
                                         domain_altitude_max_km, time_span_s, frame);
    return holdout;
}

Holdout local_holdout(const StudyState& reference, std::size_t packets, std::size_t per_packet,
                      std::uint64_t seed, double altitude_spread_km, double time_spread_s) {
    Holdout holdout;
    holdout.name = "local-query-packet";
    holdout.description =
        "clustered samples around the reference state, with a seed and cluster centres that the "
        "construction set never used. This is the regime an adaptive integrator's stage sequence "
        "actually produces, and it is where reuse and interpolation error both become visible.";
    holdout.samples = make_local_packets(reference.epoch, reference.position, packets, per_packet, seed,
                                         altitude_spread_km, time_spread_s);
    return holdout;
}

Holdout stratified_holdout(const Config& c, const StudySettings& s, const StudyState& anchor) {
    Holdout result;
    result.name = "stratified-physical-boundaries";
    result.description =
        "independent off-node physical points spanning both hemispheres, dawn/dusk/day/night, "
        "200-1000 km, native altitude transitions, longitude wrap and near-polar fallback; "
        "fresh references are never supplied to candidate caches";
    const leo::Epoch at{anchor.epoch.et_seconds + 48637.13};
    const auto add = [&](double altitude, double latitude, double longitude, double lst,
                         leo::Epoch when, const char* role) {
        result.samples.push_back({when, inertial_from_geodetic(
            when, latitude * kRad, longitude * kRad, altitude * 1000.0, c.frame)});
        Json stratum;
        stratum.add("altitude_km", altitude);
        stratum.add("latitude_deg", latitude);
        stratum.add("longitude_deg", longitude);
        stratum.add("local_solar_time_hours", lst);
        stratum.add("epoch_et_seconds", when.et_seconds);
        stratum.add("role", role);
        result.physical_strata.push_back(stratum.str());
    };
    for (double altitude : {200.13, 249.73, 299.97, 300.03, 399.77, 600.37, 899.73, 999.87})
        for (double latitude : {-89.9999, -80.37, -45.13, 0.37, 45.13, 80.37, 89.9999})
            for (double lst : {0.13, 5.97, 12.13, 18.03}) {
                const double longitude = longitude_for_local_solar_time(at, lst);
                add(altitude, latitude, longitude, lst, at, "independent-physical-point");
                // Explicit interior-cell pairs exercise approximate reuse even
                // when a fine grid gives no hits on the broad random holdout.
                // Their native references remain scoring-only, never cache training.
                const double h0 = s.altitude_min_km +
                    std::floor((altitude - s.altitude_min_km) / s.base_cells.altitude_km) *
                        s.base_cells.altitude_km;
                const double p0 = -90.0 + cell_index_latitude(latitude, s.base_cells) *
                    s.base_cells.latitude_deg;
                const double l0 = -180.0 + cell_index_longitude(longitude, s.base_cells) *
                    s.base_cells.longitude_deg;
                const double t0 = anchor.epoch.et_seconds +
                    std::floor(48637.13 / s.base_cells.time_s) * s.base_cells.time_s;
                for (double fraction : {0.23, 0.76}) {
                    const double h = h0 + fraction * s.base_cells.altitude_km;
                    if (h < s.altitude_min_km || h > s.altitude_max_km) continue;
                    const double p = p0 + fraction * s.base_cells.latitude_deg;
                    const double l = l0 + fraction * s.base_cells.longitude_deg;
                    const leo::Epoch time{t0 + fraction * s.base_cells.time_s};
                    add(h, p, l, -1.0, time,
                        fraction < 0.5 ? "cell-interior-first" : "cell-interior-second");
                }
            }
    for (double longitude : {-180.00001, -179.99999, 179.99999, 180.00001})
        for (double latitude : {-80.1, 0.1, 80.1})
            add(600.37, latitude, longitude, -1.0, at, "wrap-boundary");
    return result;
}

struct HoldoutResult {
    ErrorMetrics metrics;
    std::size_t samples = 0;
    std::size_t nonpositive_or_nonfinite = 0;
    double minimum_density = 0, maximum_density = 0;
    std::vector<std::string> worst_samples;
    std::vector<std::string> strata;
    std::string json() const;
};

HoldoutResult score(const std::vector<QuerySample>& samples, DensityEvaluator& candidate,
                    const NativeProbe& probe, std::size_t worst_reported,
                    const std::vector<std::string>& physical_strata) {
    std::vector<double> signed_relative, log_error;
    std::vector<double> densities;
    HoldoutResult result;
    result.samples = samples.size();
    result.minimum_density = std::numeric_limits<double>::infinity();
    result.maximum_density = 0;
    for (const auto& sample : samples) {
        const double reference = probe.density(sample.position, sample.epoch);
        if (!std::isfinite(reference) || reference <= 0)
            throw std::runtime_error("holdout reference density is not finite and positive");
        const DensityStatistics before = candidate.counts();
        const double got = candidate.evaluate(sample.epoch, sample.position);
        densities.push_back(got);
        if (!std::isfinite(got) || got <= 0)
            throw std::runtime_error("invalid candidate reconstruction on an independent holdout");
        result.minimum_density = std::min(result.minimum_density, got);
        result.maximum_density = std::max(result.maximum_density, got);
        signed_relative.push_back((got - reference) / reference);
        log_error.push_back(std::log(got / reference));
        if (!physical_strata.empty()) {
            const DensityStatistics after = candidate.counts();
            Json row;
            row.raw("physical_point", physical_strata[result.strata.size()]);
            row.add("signed_relative_error", signed_relative.back());
            row.add("reference_density_kg_m3", reference);
            row.add("reconstructed_density_kg_m3", got);
            row.add("native_calls", after.native_calls - before.native_calls);
            row.add("guard_fallbacks", after.guard_fallbacks - before.guard_fallbacks);
            row.add("domain_fallbacks", after.domain_fallbacks - before.domain_fallbacks);
            row.add("out_of_contract_nodes",
                    after.out_of_contract_nodes - before.out_of_contract_nodes);
            result.strata.push_back(row.str());
        }
    }
    result.metrics = error_metrics(signed_relative, log_error);
    // The worst individual samples, so a large local error is always visible and
    // never only summarised by a percentile.
    std::vector<std::pair<double, std::size_t>> ranked;
    ranked.reserve(signed_relative.size());
    for (std::size_t index = 0; index < signed_relative.size(); ++index)
        ranked.emplace_back(std::abs(signed_relative[index]), index);
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t index = 0; index < std::min(worst_reported, ranked.size()); ++index) {
        const std::size_t slot = ranked[index].second;
        Json row;
        row.add("epoch_et_seconds", samples[slot].epoch.et_seconds);
        row.add("utc", utc_label(samples[slot].epoch));
        row.add("position_m", numbers({samples[slot].position.x(), samples[slot].position.y(),
                                       samples[slot].position.z()}));
        row.add("reference_density_kg_m3", probe.density(samples[slot].position, samples[slot].epoch));
        row.add("reconstructed_density_kg_m3", densities[slot]);
        row.add("signed_relative_error", signed_relative[slot]);
        result.worst_samples.push_back(row.str());
    }
    return result;
}

std::string HoldoutResult::json() const {
    Json j;
    j.add("samples", samples);
    j.add("nonpositive_or_nonfinite_reconstructions", nonpositive_or_nonfinite);
    j.add("minimum_reconstructed_density_kg_m3",
          std::isfinite(minimum_density) ? minimum_density : 0.0);
    j.add("maximum_reconstructed_density_kg_m3", maximum_density);
    j.raw("error_metrics", metrics.json());
    j.raw("threshold_pass_by_maximum", threshold_row(metrics, study_error_targets()));
    j.raw("worst_samples", raw_array(worst_samples));
    j.raw("physical_strata", raw_array(strata));
    return j.str();
}

} // namespace

std::string accuracy_study(const Config& c, Environment& env, const StudySettings& s) {
    if (s.native_geodetic)
        throw std::invalid_argument(
            "--study-native-geodetic cannot drive the accuracy study: the candidates consume inertial "
            "states and their own preprocessing, which need the Earth-orientation kernel. Run the "
            "partial native-input sensitivity instead");
    if (s.model_kind == leo::atmosphere::Model::exponential)
        throw std::invalid_argument(
            "candidates A-D reconstruct geodetic density and require nrlmsise00 or jb2008");

    NativeProbe probe(c, env, s.model_kind, c.ap_history);
    const StudyState reference = study_state(c, s);
    std::vector<WeatherEpoch> pool = build_epoch_pool(c, env, s);
    const std::vector<WeatherEpoch> selected = select_regimes(c, env, pool);

    // The construction set lives inside the first primary regime; the holdouts
    // are generated from independent seeds and disjoint neighbourhoods.
    StudyState anchor = reference;
    anchor.epoch = selected.front().epoch;

    const std::size_t construction_packets = std::max<std::size_t>(1, s.points / 20);
    const std::size_t per_packet = std::max<std::size_t>(1, s.points / construction_packets);
    const std::vector<QuerySample> construction =
        make_local_packets(anchor.epoch, anchor.position, construction_packets, per_packet,
                           Rng::derive(s.seed, 0x01), 40.0, 300.0);
    Holdout broad = local_holdout(anchor, construction_packets, per_packet, Rng::derive(s.seed, 0x04),
                                  200.0, 1800.0);
    broad.name = "broad-query-packet";
    const double window_s = 3600.0;
    const std::vector<Holdout> holdouts{
        uniform_holdout(anchor, s.points, Rng::derive(s.seed, 0x02), window_s, c.frame),
        local_holdout(anchor, construction_packets, per_packet, Rng::derive(s.seed, 0x03), 40.0, 300.0),
        std::move(broad),
        stratified_holdout(c, s, anchor),
    };

    Json result;
    result.add("section", "density reconstruction accuracy");
    result.add("comparison_rule",
               "each reconstructed value is compared with a fresh production DirectAtmosphere "
               "evaluation at the same exact epoch and the same exact position, so no error can come "
               "from a different spacecraft state");
    result.add("holdout_rule",
               "independent construction and holdout seeds; native online misses are charged and "
               "scored, but fresh holdout references are never injected into the cache");
    result.add("threshold_rule",
               "a threshold is satisfied only when the maximum absolute relative error over every "
               "tested sample is within it; a mean or percentile never satisfies a threshold");
    result.add("reference_state", anchor.origin);
    result.add("reference_epoch_utc", utc_label(anchor.epoch));
    result.add("construction_samples", construction.size());
    result.add("construction_distribution",
               "clustered packets with 40 km and 300 s spreads around the reference state, which is "
               "the regime an adaptive integrator's stage sequence produces");
    result.raw("holdouts", raw_array([&] {
        std::vector<std::string> rows;
        for (const auto& holdout : holdouts) {
            Json row;
            row.add("name", holdout.name);
            row.add("description", holdout.description);
            row.add("samples", holdout.samples.size());
            rows.push_back(row.str());
        }
        return rows;
    }()));
    result.add("requested_resolution_scale", s.scale);
    result.add("coarse_to_fine_rule",
               "each rung changes exactly one documented knob, the resolution scale, and rebuilds the "
               "evaluator from scratch; the requested rung is the one whose scale equals the requested "
               "scale");

    // ---- coarse-to-fine rungs ----
    std::vector<std::string> rungs;
    const std::vector<double> scales = s.cell_altitude_km > 0 || s.cell_degrees > 0 || s.cell_time_s > 0
        ? std::vector<double>{s.scale} : std::vector<double>{s.scale * 0.5, s.scale, s.scale * 2.0};
    for (double rung_scale : scales) {
        StudySettings rung = settings_at_scale(s, rung_scale);
        rung.measurement = true;   // screening diagnostics only, labelled as such below
        DensityEvaluator candidate(c, env, s.model_kind, rung);
        candidate.set_time_origin(anchor.epoch.et_seconds);

        // ---- construction: its cost is part of the result ----
        const auto construction_timer = Clock::now();
        const DensityStatistics before_construction = candidate.counts();
        for (const auto& sample : construction) candidate.evaluate(sample.epoch, sample.position);
        const double construction_seconds = elapsed(construction_timer);
        const DensityStatistics after_construction = candidate.counts();
        const std::uint64_t construction_native_calls =
            after_construction.native_calls - before_construction.native_calls;

        Json row;
        row.add("scale", rung_scale);
        row.add("requested_rung", std::abs(rung_scale - s.scale) < 1e-12);
        row.raw("settings", rung.json());
        row.add("construction_samples", construction.size());
        row.add("construction_seconds", construction_seconds);
        row.add("construction_native_calls", construction_native_calls);
        row.add("construction_seconds_note",
                "every native call spent building the cache is counted and charged; a construction "
                "that never pays for itself is visible here rather than hidden in the lookup time");
        row.add("construction_native_calls_avoided_during_build",
                construction.size() ? 1.0 - double(construction_native_calls) / double(construction.size())
                                    : 0.0);
        row.add("construction_native_calls_avoided_during_build_rule",
                "construction_samples minus construction_native_calls, over construction_samples. "
                "It is negative when the population phase performed more native evaluations than it "
                "served queries, which is exactly what a four-dimensional interpolation does on a "
                "cold region: it spends up to 2^N fresh node evaluations per query. It is reported "
                "as a signed number rather than clamped, because clamping it would hide the one "
                "result a reader most needs to see");
        row.add("construction_native_calls_per_sample",
                construction.size() ? double(construction_native_calls) / double(construction.size())
                                    : 0.0);
        row.add("construction_timer_note",
                "the external timer here covers the whole population phase including the in-"
                "evaluator section timers, which add two clock reads per query because this rung "
                "runs instrumented for its screening diagnostics. It is an upper bound on the "
                "uninstrumented cost; the benchmark study measures that separately on an "
                "uninstrumented build");

        DensityStatistics before_holdouts = candidate.counts();
        for (const auto& holdout : holdouts) {
            const HoldoutResult scored = score(
                holdout.samples, candidate, probe, 20, holdout.physical_strata);
            const DensityStatistics after = candidate.counts();
            Json holdout_row;
            holdout_row.add("name", holdout.name);
            holdout_row.add("samples", scored.samples);
            holdout_row.raw("results", scored.json());
            const std::uint64_t holdout_queries = after.queries - before_holdouts.queries;
            const std::uint64_t holdout_native = after.native_calls - before_holdouts.native_calls;
            holdout_row.add("queries_on_this_holdout", holdout_queries);
            holdout_row.add("native_calls_on_this_holdout", holdout_native);
            holdout_row.add("native_calls_per_query_on_this_holdout",
                            holdout_queries ? double(holdout_native) / double(holdout_queries) : 0.0);
            holdout_row.add("native_calls_avoided_on_this_holdout",
                            static_cast<double>(holdout_queries) - double(holdout_native));
            holdout_row.add("native_calls_avoided_fraction",
                            holdout_queries ? 1.0 - double(holdout_native) / double(holdout_queries)
                                            : 0.0);
            holdout_row.add("native_calls_avoided_rule",
                            "queries minus native_calls for this holdout alone. It is signed and is "
                            "negative when the candidate performed more native evaluations than it "
                            "served queries on this holdout, which is the honest outcome for an "
                            "interpolation whose nodes are not yet stored. Every native call the "
                            "holdout caused is counted, including the guard and domain fallbacks and "
                            "the interpolation node builds: it is not one minus a fallback rate");
            holdout_row.add("guard_fallbacks_on_this_holdout",
                            after.guard_fallbacks + after.domain_fallbacks +
                                after.out_of_contract_nodes + after.negative_time_fallbacks -
                                (before_holdouts.guard_fallbacks + before_holdouts.domain_fallbacks +
                                 before_holdouts.out_of_contract_nodes +
                                 before_holdouts.negative_time_fallbacks));
            holdout_row.add("fallback_fraction",
                            after.queries > before_holdouts.queries
                                ? double((after.guard_fallbacks + after.domain_fallbacks +
                                          after.out_of_contract_nodes + after.negative_time_fallbacks) -
                                         (before_holdouts.guard_fallbacks + before_holdouts.domain_fallbacks +
                                          before_holdouts.out_of_contract_nodes +
                                          before_holdouts.negative_time_fallbacks)) /
                                      double(after.queries - before_holdouts.queries)
                                : 0.0);
            row.raw(("holdout_" + holdout.name).c_str(), holdout_row.str());
            before_holdouts = after;
        }
        const DensityStatistics after_holdouts = candidate.counts();

        // ---- screening latency diagnostic: serial interleaved batches ----
        // Measured on a second, uninstrumented evaluator. The scored evaluator
        // runs with the in-evaluator section timers enabled, which add two clock
        // reads to every query on the candidate path only, so timing it would
        // charge the candidate for instrumentation the direct path does not pay.
        row.raw("screening_latency_diagnostic", [&] {
            const std::size_t batch = std::max<std::size_t>(1, std::min<std::size_t>(200, s.points));
            if (holdouts[1].samples.empty()) {
                Json timing;
                timing.add("status", "diagnostic, not a headline performance number");
                timing.add("skipped", true);
                timing.add("reason", "the local-query-packet holdout is empty, so there is no "
                                     "interleaved sequence to time");
                return timing.str();
            }
            StudySettings uninstrumented = rung;
            uninstrumented.measurement = false;
            DensityEvaluator latency(c, env, s.model_kind, uninstrumented);
            // One discarded execution of each path so first-touch effects are
            // outside both measured batches.
            {
                const auto& sample = holdouts[1].samples.front();
                latency.evaluate(sample.epoch, sample.position);
                latency.direct(sample.epoch, sample.position);
            }
            std::vector<double> direct_nanoseconds, candidate_nanoseconds;
            std::size_t cursor = 0;
            for (int repeat = 0; repeat < 9; ++repeat) {
                const bool measure_candidate = (repeat % 2) == 1;
                const auto timer = Clock::now();
                for (std::size_t index = 0; index < batch; ++index) {
                    const auto& sample = holdouts[1].samples[(cursor + index) % holdouts[1].samples.size()];
                    if (measure_candidate) latency.evaluate(sample.epoch, sample.position);
                    else latency.direct(sample.epoch, sample.position);
                }
                const double per_call = elapsed(timer) / double(batch) * 1e9;
                (measure_candidate ? candidate_nanoseconds : direct_nanoseconds).push_back(per_call);
                cursor = (cursor + batch) % holdouts[1].samples.size();
            }
            std::sort(direct_nanoseconds.begin(), direct_nanoseconds.end());
            std::sort(candidate_nanoseconds.begin(), candidate_nanoseconds.end());
            Json timing;
            timing.add("status", "diagnostic, not a headline performance number");
            timing.add("method",
                       "serial batches interleaved in time, so the two paths see the same machine "
                       "state. Both run on one uninstrumented evaluator, so neither pays for the "
                       "in-evaluator section timers. This is a screening measurement; the reported "
                       "headline performance comes from the benchmark study on an uninstrumented "
                       "build");
            timing.add("timed_unit",
                       "nanoseconds per evaluate/direct call over a batch of interleaved "
                       "holdout points at one epoch neighbourhood");
            timing.add("caller", std::string("sun supplied: null, so the provider performs its "
                                             "own Sun lookup on every call"));
            timing.add("batches_per_path", static_cast<double>(direct_nanoseconds.size()));
            timing.add("median_direct_ns_per_call",
                       direct_nanoseconds.empty() ? 0.0 : direct_nanoseconds[direct_nanoseconds.size() / 2]);
            timing.add("median_candidate_ns_per_call",
                       candidate_nanoseconds.empty() ? 0.0
                                                      : candidate_nanoseconds[candidate_nanoseconds.size() / 2]);
            timing.add("median_ratio_candidate_over_direct",
                       direct_nanoseconds.empty() || direct_nanoseconds[direct_nanoseconds.size() / 2] == 0
                           ? 0.0
                           : candidate_nanoseconds[candidate_nanoseconds.size() / 2] /
                                 direct_nanoseconds[direct_nanoseconds.size() / 2]);
            return timing.str();
        }());

        row.raw("evaluator_statistics", candidate.statistics());
        candidate.finish_trace();
        row.raw("payload", [&] {
            const DensityStatistics counts = candidate.counts();
            Json payload;
            payload.add("payload_bytes", static_cast<double>(counts.payload_bytes));
            payload.add("entries", counts.entries);
            payload.add("capacity", counts.capacity);
            payload.add("bytes_per_query", counts.queries
                                               ? static_cast<double>(counts.payload_bytes) / double(counts.queries)
                                               : 0.0);
            payload.add("process_peak_rss_bytes", static_cast<double>(process_peak_rss_bytes()));
            payload.add("note",
                        "payload_bytes is the cache's own storage from real element sizes; "
                        "process_peak_rss_bytes is the whole process and is reported separately");
            return payload.str();
        }());
        rungs.push_back(row.str());
    }
    result.raw("resolution_ladder", raw_array(rungs));

    result.add("selection_note",
               "a candidate is worth propagating or benchmarking only if it passes a threshold by "
               "maximum error on the local-query-packet holdout; this study decides nothing about "
               "production on its own");
    result.add("invalid_reconstruction_policy",
               "a reconstruction that is not finite and positive is counted, reported and compared "
               "against its reference rather than being dropped from the distribution");
    return result.str();
}

} // namespace cache_study