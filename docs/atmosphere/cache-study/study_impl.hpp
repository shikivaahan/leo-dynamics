// ATM-CACHE-001 candidate evaluator implementation. Included by support.hpp;
// header-only because the study is a handful of small translation units that
// must all see exactly the same code.
//
// Nothing in this file is production code. It links the production libraries,
// calls the production provider for every authoritative value, and never
// modifies, wraps or replaces the native models, the preprocessing, the
// integrator, the force laws or the public provider API.
#pragma once

namespace cache_study {

// ---------------------------------------------------------------------------
// Small JSON projections
// ---------------------------------------------------------------------------

inline std::string CellScheme::json() const {
    Json j;
    j.add("altitude_km", altitude_km);
    j.add("latitude_deg", latitude_deg);
    j.add("longitude_deg", longitude_deg);
    j.add("time_s", time_s);
    j.add("latitude_cells", static_cast<double>(latitude_cells()));
    j.add("longitude_cells_periodic", static_cast<double>(longitude_cells()));
    j.add("altitude_cells", static_cast<double>(altitude_cells()));
    // The widths the quantisation actually uses, stated as the geographic
    // degrees they really are. The cell counts are whole numbers of cells across
    // the sphere, so the realised width is the sphere extent divided by that
    // count; it coincides with the requested width only when the requested width
    // divides the extent evenly, and reporting the requested value alone would
    // misstate the real cell geometry by up to half a cell.
    j.add("realised_latitude_width_deg", 180.0 / double(latitude_cells()));
    j.add("realised_longitude_width_deg", 360.0 / double(longitude_cells()));
    j.add("realised_width_rule",
          "180/latitude_cells and 360/longitude_cells: the exact degree width of one cell, which "
          "is what cell_index_latitude and cell_index_longitude divide by. These are geographic "
          "degrees of latitude and of longitude on the WGS84 ellipsoid, not great-circle degrees");
    j.add("latitude_index_rule",
          "floor((latitude_deg + 90)/realised_latitude_width_deg), clamped into the grid");
    j.add("longitude_index_rule",
          "floor(mod(longitude_deg + 180, 360)/realised_longitude_width_deg), wrapped modulo the "
          "cell count, so the +180/-180 seam is one cell boundary and never a straddling cell");
    j.add("altitude_index_rule",
          "floor(max(altitude_km, band_min_km) - band_min_km)/altitude_km against the study's "
          "configured altitude band; the band, not a fixed constant, is the origin");
    j.add("time_index_rule", "floor((et_seconds - time_origin)/time_s)");
    return j.str();
}

inline std::string StudySettings::json() const {
    Json j;
    j.add("kind", kind);
    // The input set is an experimental parameter, not provenance only: it selects
    // which pinned bytes every reference value in this process was produced from,
    // so a result carries it beside every other knob that shaped it. Provenance
    // and hashes live in the shared study metadata; this is the knob.
    j.add("input_set", input_set);
    j.add("input_set_rule",
          "the id of the pinned input set this process verified and read; direct-v2 is the "
          "historical primary and every other id is a separately pinned scientific experiment "
          "whose evidence is labelled scientific-experimental-pinned, never as direct-v2");
    j.add("model", model);
    j.add("candidate", candidate);
    j.add("points", points);
    j.add("seed", seed);
    j.add("capacity", capacity);
    j.add("ensemble", ensemble);
    j.add("thread_workers", thread_workers);
    j.add("begin", begin);
    j.add("count", count);
    j.add("benchmark_route", benchmark_route);
    j.add("refine", refine);
    j.add("h_km", h_km);
    j.add("perturbation_scale", perturbation_scale);
    j.add("angle_deg", angle_deg);
    j.add("time_s", time_s);
    j.add("target", target);
    j.add("scale", scale);
    j.add("trace_path", trace_path);
    j.add("capacity_policy", replace_policy_lru ? "lru" : "fifo");
    j.add("time_reference_epoch_s", time_reference_epoch_s);
    j.raw("base_cells", base_cells.json());
    j.add("interpolation_mode", interpolation_mode_name(interpolation));
    j.add("interpolation_mode_rule",
          "non-interpolated axes retain the exact query coordinates; nodes are fresh native values, "
          "not values scattered or averaged from other spacecraft states");
    j.add("interpolation_axis", axis_mode_name(axis_mode));
    j.add("reconstructed_quantity", "ln(density)");
    j.add("log_altitude_width", log_altitude_width);
    j.add("log_time_width", log_time_width);
    j.add("log_altitude_width_rule",
          "uniform in ln(altitude_km); the width equals ln(1 + cell_altitude_km / altitude_reference_km), "
          "so a cell is cell_altitude_km wide at the reference altitude");
    j.add("altitude_reference_km", altitude_reference_km);
    j.add("time_reference_s", time_reference_s);
    j.add("log_time_width_rule",
          "uniform in ln(seconds since the recorded time origin); the width equals "
          "ln(1 + cell_time_s / time_reference_s), so a cell is cell_time_s wide at the reference offset");
    j.add("adaptive_max_depth", adaptive_max_depth);
    j.add("adaptive_probe_divisor", adaptive_probe_divisor);
    j.add("fixture_scan_days", fixture_scan_days);
    j.add("measurement", measurement);
    // The node geometry candidate C actually builds, stated as real widths. The
    // base_cells block above reports the requested widths; these report what the
    // interpolation divides by, which differs whenever the axis transform or
    // the whole-cell counts change it.
    {
        const bool logarithmic = axis_mode == AxisMode::logarithmic;
        const auto latitude_count = static_cast<double>(base_cells.latitude_cells());
        const auto longitude_count = static_cast<double>(base_cells.longitude_cells());
        Json geometry;
        geometry.add("altitude_axis_anchor", logarithmic ? "ln(1 km)" : "altitude_min_km");
        geometry.add("altitude_node_km",
                     logarithmic ? "exp(index * log_altitude_width)"
                                 : "altitude_min_km + index * base_cells.altitude_km");
        geometry.add("altitude_node_rule_note",
                     logarithmic
                         ? "a logarithmic altitude axis is anchored at 1 km, so its nodes near the "
                           "band are not integer multiples of base_cells.altitude_km and a node may "
                           "fall just below the band or just above its top. When that happens the "
                           "query is refused and answered natively, and out_of_contract_nodes counts "
                           "it; base_cells.altitude_km is not the altitude cell width in this mode"
                         : "in the linear mode the node altitude is the band origin plus a whole "
                           "number of base_cells.altitude_km cells");
        geometry.add("latitude_node_deg", "latitude_min - 90 + index * (180/latitude_cells)");
        geometry.add("longitude_node_deg", "-180 + index * (360/longitude_cells), wrapped");
        geometry.add("latitude_width_deg", 180.0 / latitude_count);
        geometry.add("longitude_width_deg", 360.0 / longitude_count);
        geometry.add("spatial_width_rule",
                     "the realised geographic degree widths are the sphere extent divided by the "
                     "whole-cell counts, which coincide with the requested widths only when those "
                     "divide the extent evenly. These are degrees of geodetic latitude and of "
                     "longitude on the WGS84 ellipsoid");
        geometry.add("time_origin_anchor", "time_origin_et_seconds");
        geometry.add("time_node_s",
                     logarithmic ? "ln(offset) = index * log_time_width"
                                 : "offset = index * base_cells.time_s");
        geometry.add("noninterpolated_axis_rule",
                     "an axis that is not interpolated is not divided by any width at all: its node "
                     "coordinate is the exact query coordinate, so it is carried bitwise into the "
                     "node identity and contributes a weight of one");
        geometry.add("log_time_note",
                     "a logarithmic time axis requires a positive offset from the time origin; a "
                     "backwards or out-of-order query on an active logarithmic time axis is refused "
                     "and answered natively, and negative_time_fallbacks counts it. A logarithmic "
                     "axis that is switched off is never evaluated, so it cannot refuse anything");
        j.raw("interpolation_node_geometry", geometry.str());
    }
    j.add("error_targets", numbers(study_error_targets()));
    return j.str();
}

inline std::string ErrorMetrics::json() const {
    Json j;
    j.add("count", count);
    j.add("mean_signed_relative", mean_signed_relative);
    j.add("mean_absolute_relative", mean_absolute_relative);
    j.add("rms_relative", rms_relative);
    j.add("median_absolute_relative", median_absolute_relative);
    j.add("p95_absolute_relative", p95_absolute_relative);
    j.add("p99_absolute_relative", p99_absolute_relative);
    j.add("p999_absolute_relative", p999_absolute_relative);
    j.add("maximum_absolute_relative", maximum_absolute_relative);
    j.add("rms_log_error", rms_log_error);
    j.add("rms_log_density_error", rms_log_density_error);
    j.add("largest_positive_bias", largest_positive_bias);
    j.add("largest_negative_bias", largest_negative_bias);
    j.add("units", "relative errors are dimensionless fractions; log errors are ln(reconstructed/reference)");
    return j.str();
}

inline std::string DensityStatistics::json() const {
    Json j;
    j.add("queries", queries);
    j.add("exact_hits", exact_hits);
    j.add("acquisition_skips", acquisition_skips);
    j.add("acquisition_skips_note",
          "candidate A queries answered from the memo without constructing the geometry or the "
          "native input and without calling the native model: no Earth-fixed rotation, no geodetic "
          "conversion, no local solar time, no Sun right ascension and declination. It is NOT a "
          "count of queries that skipped all preprocessing. The space-weather identity is part of "
          "the memo key, so those queries still perform the UTC calendar conversion and the "
          "space-weather record read that identity requires; that cost is inside lookup_seconds and "
          "is deliberately not hidden, because skipping it would mean not knowing whether the "
          "stored value belongs to this epoch's weather");
    j.add("cell_reuses", cell_reuses);
    j.add("interpolations", interpolations);
    j.add("adaptive_anchor_uses", adaptive_anchor_uses);
    j.add("adaptive_probes", adaptive_probes);
    j.add("adaptive_refinements", adaptive_refinements);
    j.add("adaptive_degradations", adaptive_degradations);
    j.add("native_calls", native_calls);
    j.add("native_calls_per_query", queries ? double(native_calls) / double(queries) : 0.0);
    // "Avoided" is the number of queries that did not cost a native evaluation,
    // not one minus a fallback rate. It is queries minus *every* native call the
    // run made, including the calls spent building the cache and the calls a
    // guard forced. It can legitimately be negative: a four-dimensional
    // interpolation spends up to 2^N fresh native evaluations per query, so on
    // a cold region it costs more than it saves. That number is the finding, and
    // hiding it behind a positive-looking fraction would be the bug.
    j.add("native_calls_avoided", double(queries) - double(native_calls));
    j.add("native_calls_avoided_fraction", avoided());
    j.add("native_calls_avoided_percent", avoided() * 100.0);
    j.add("native_calls_avoided_rule",
          "queries minus native_calls, where native_calls counts every native evaluation the run "
          "performed: cache construction, guard and domain fallbacks, adaptive probes and "
          "interpolation corner builds. The fraction is negative exactly when the run performed "
          "more native evaluations than it served queries, which is a real and reportable result");
    j.add("insertions", insertions);
    j.add("evictions", evictions);
    j.add("guard_fallbacks", guard_fallbacks);
    j.add("domain_fallbacks", domain_fallbacks);
    j.add("weather_guard_rejections", weather_guard_rejections);
    j.add("incomplete_corners", incomplete_corners);
    j.add("incomplete_corners_rule",
          "interpolation nodes this run had to evaluate natively because the store did not hold "
          "them: a cold cache, an unvisited cell, or a bounded store that evicted the node. It is "
          "a count of fresh native node evaluations, not a count of failed interpolations, and a "
          "four-dimensional interpolation needs up to 2^N of them per query");
    j.add("out_of_contract_nodes", out_of_contract_nodes);
    j.add("out_of_contract_nodes_rule",
          "a query abandoned and answered natively because a node it needed lay outside the "
          "model's documented native altitude range or inside the polar fallback band");
    j.add("negative_time_fallbacks", negative_time_fallbacks);
    j.add("invalid_values_refused", invalid_native_values);
    j.add("invalidations", invalidations);
    j.add("construction_queries", construction_queries);
    j.add("construction_queries_note",
          "queries issued while the evaluator was still in its population phase; this is a count of "
          "queries, not a separate timing region, and it only differs from queries once "
          "invalidate() has been called");
    j.add("entries", entries);
    j.add("peak_entries", peak_entries);
    j.add("capacity", capacity);
    j.add("payload_bytes", static_cast<double>(payload_bytes));
    j.add("payload_estimate_rule", bounded_store_payload_note());
    j.add("payload_bytes_per_query",
          queries ? static_cast<double>(payload_bytes) / double(queries) : 0.0);
    j.add("process_peak_rss_bytes", static_cast<double>(process_peak_rss_bytes()));
    j.add("payload_vs_rss_note",
          "payload_bytes is the cache's own storage, derived from real element sizes; "
          "process_peak_rss_bytes is what the whole process cost and is reported separately");
    j.add("construction_seconds", construction_seconds);
    j.add("lookup_seconds", lookup_seconds);
    j.add("native_seconds", native_seconds);
    j.add("section_timers_note",
          "these three are the in-evaluator section timers. They accumulate only when those timers "
          "were enabled for the run, and they add two clock reads per query when they are. A zero "
          "here means the section timers were switched off and nothing was measured; it does NOT "
          "mean the phase was free. The evaluator statistics object reports measurement_enabled, "
          "and the benchmark study measures these phases on an uninstrumented build instead");
    j.add("construction_note",
          "construction_seconds and construction_queries cover only the population phase; every "
          "native call spent building the cache is counted in native_calls and is never free");
    return j.str();
}

// ---------------------------------------------------------------------------
// DensityEvaluator
// ---------------------------------------------------------------------------

inline DensityEvaluator::DensityEvaluator(const Config& c, Environment& env, leo::atmosphere::Model model,
                                           const StudySettings& settings)
    : config_(c), environment_(env), settings_(settings), model_(model),
#ifdef LEO_HAS_NRLMSISE00
      switches_(c.ap_history ? ap_history_switches() : leo::atmosphere::Nrlmsise00::standard_switches()),
      authoritative_(model, env.ephemeris, env.weather, c.frame, c.ap_history, c.options),
      nrlmsise_(switches_)
#else
      authoritative_(model, env.ephemeris, env.weather, c.frame, c.ap_history, c.options)
#endif
      , measurement_(settings.measurement),
      exact_(settings.capacity, settings.replace_policy_lru),
      cells_(settings.capacity, settings.replace_policy_lru),
      corners_(settings.capacity, settings.replace_policy_lru),
      adaptive_(settings.capacity, settings.replace_policy_lru),
      trace_(settings.trace_path, static_cast<std::uint32_t>(model),
             static_cast<std::uint32_t>(settings.candidate_kind)) {
    if (model_ != leo::atmosphere::Model::exponential && !compiled_in(model_))
        throw std::invalid_argument(std::string("model ") + model_name(model_) +
                                    " is not compiled into this build");
    if (model_ != leo::atmosphere::Model::exponential &&
        settings_.candidate_kind == Candidate::control) {
        // Candidate E is meaningful for every model; nothing to reject.
    } else if (model_ == leo::atmosphere::Model::exponential &&
               settings_.candidate_kind != Candidate::control) {
        throw std::invalid_argument(
            "candidates A-D reconstruct geodetic density and require nrlmsise00 or jb2008; "
            "the exponential profile takes |r| - Re and has no density structure to cache");
    }
    if (settings_.time_reference_epoch_s != 0.0) {
        time_origin_ = settings_.time_reference_epoch_s;
        time_origin_fixed_ = true;
    }
    stats_.capacity = settings_.capacity;
}


// The raw bit pattern of a double, used for every key that must reproduce an
// input exactly rather than approximately.
inline std::uint64_t bits_of(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

} // namespace cache_study

// The shared environment a study process runs in. In the partial native-input
// mode only the pinned CSSI archive is loaded: the JB2008 solar and DSTDTC
// records are not read at all there, and an empty path is how the dataset
// declares itself intentionally absent. Nothing is ever substituted for a
// record the mode does not read.
inline std::unique_ptr<Environment> make_study_environment(const Config& c,
                                                                        bool native_geodetic_only) {
    if (!native_geodetic_only) return std::make_unique<Environment>(c);
    // The partial slice uses the leapsecond kernel and the pinned CSSI archive.
    // No Earth orientation kernel and no JB2008 record is furnished or read; the
    // provider's ephemeris slot carries the same leapsecond file, which the
    // provider ref-counts as a single load.
    Config reduced = c;
    reduced.pck.clear();
    reduced.solfsmy.clear();
    reduced.dtcfile.clear();
    return std::make_unique<Environment>(reduced);
}

namespace cache_study {

inline QueryInputs DensityEvaluator::build_inputs(leo::Epoch epoch, const leo::InertialVector& position,
                                                   const leo::SunObservation* sun) const {
    QueryInputs inputs;
    inputs.epoch = epoch;
    inputs.position = position;
    inputs.ap_history_mode = config_.ap_history;
    if (model_ == leo::atmosphere::Model::exponential) return inputs;

    constexpr double deg = 180.0 / std::numbers::pi;
    const Eigen::Matrix3d rotation =
        leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, config_.frame,
                                                          config_.options.orientation_reuse);
    const Eigen::Vector3d fixed = rotation * position.as_eigen();
    const auto geodetic = leo::atmosphere::geodetic(fixed);
    inputs.altitude_m = geodetic.altitude_m;
    inputs.latitude_rad = geodetic.latitude_rad;
    inputs.longitude_rad = geodetic.longitude_rad;
    inputs.latitude_deg = geodetic.latitude_rad * deg;
    inputs.longitude_deg = geodetic.longitude_rad * deg;
    inputs.latitude_geocentric_rad = std::atan2(fixed.z(), std::hypot(fixed.x(), fixed.y()));

    const auto utc = leo::SpiceEphemerisProvider::utc_calendar(epoch, config_.options.utc_reuse);
    inputs.utc_seconds_of_day = utc.seconds_of_day;
    inputs.leap_second_day = utc.day_length_seconds != 86400.0;

    if (model_ == leo::atmosphere::Model::nrlmsise00) {
        auto in = environment_.weather.nrlmsise(utc, config_.ap_history);
        in.latitude_deg = inputs.latitude_deg;
        in.longitude_deg = inputs.longitude_deg;
        in.altitude_km = inputs.altitude_m / 1000.0;
        in.local_solar_time_hours = std::fmod(utc.seconds_of_day / 3600 + in.longitude_deg / 15 + 24, 24);
        inputs.local_solar_time_hours = in.local_solar_time_hours;
        inputs.nrlmsise = in;
    } else {
        auto in = environment_.weather.jb2008(utc);
        // Exactly the production sharing rule: a supplied Sun is used only when
        // the provider itself considers it compatible, which is exact in the
        // epoch bits and in the current kernel generation. Anything else falls
        // back to a full observation, exactly as DirectAtmosphere does.
        const leo::SunObservation* observation = nullptr;
        if (sun != nullptr && config_.options.sun_sharing &&
            environment_.ephemeris.compatible_sun(*sun, epoch))
            observation = sun;
        const InertialVector sun_position = observation != nullptr
                                                ? observation->state().position_m
                                                : environment_.ephemeris.observe_sun(epoch).state().position_m;
        const Eigen::Vector3d fixed_sun = rotation * sun_position.as_eigen();
        in.sun_ra_dec_rad = {std::atan2(fixed_sun.y(), fixed_sun.x()),
                             std::atan2(fixed_sun.z(), std::hypot(fixed_sun.x(), fixed_sun.y()))};
        in.position_ra_lat_alt = {geodetic.longitude_rad, inputs.latitude_geocentric_rad,
                                  inputs.altitude_m / 1000.0};
        inputs.jb2008 = in;
        inputs.sun_ra_rad = in.sun_ra_dec_rad[0];
        inputs.sun_dec_rad = in.sun_ra_dec_rad[1];
        inputs.sun_valid = true;
    }
    // One definition of the space-weather identity, shared with every study
    // analysis that has to decide whether two epochs may share a value.
    const WeatherIdentity weather = weather_identity(config_, environment_, model_, epoch);
    inputs.weather = weather;
    // A convenience summary for the report only. It never decides anything.
    inputs.exact_identity = weather.summary();
    return inputs;
}

inline double DensityEvaluator::native_density(const QueryInputs& inputs, const leo::SunObservation* sun) const {
    if (model_ == leo::atmosphere::Model::nrlmsise00) {
#ifdef LEO_HAS_NRLMSISE00
        return nrlmsise_.evaluate(inputs.nrlmsise).density_kg_m3;
#else
        throw std::runtime_error("NRLMSISE backend not enabled at build time");
#endif
    }
    if (model_ == leo::atmosphere::Model::jb2008) {
#ifdef LEO_HAS_JB2008
        return leo::atmosphere::Jb2008{}.evaluate(inputs.jb2008).density_kg_m3;
#else
        throw std::runtime_error("JB2008 backend not enabled at build time");
#endif
    }
    return authoritative_.evaluate(inputs.epoch, inputs.position, sun).density_kg_m3;
}

inline double DensityEvaluator::direct(leo::Epoch epoch, leo::InertialVector position,
                                       const leo::SunObservation* sun) const {
    ++authoritative_calls_;
    return authoritative_.evaluate(epoch, position, sun).density_kg_m3;
}

inline leo::atmosphere::Result DensityEvaluator::authoritative(leo::Epoch epoch, leo::InertialVector position,
                                                                const leo::SunObservation* sun) const {
    ++authoritative_calls_;
    return authoritative_.evaluate(epoch, position, sun);
}

inline double DensityEvaluator::native_call(const QueryInputs& inputs, const leo::SunObservation* sun) {
    const auto start = measurement_ ? Clock::now() : Clock::time_point{};
    const double density = native_density(inputs, sun);
    if (measurement_) stats_.native_seconds += elapsed(start);
    ++stats_.native_calls;
    if (!usable(density)) ++stats_.invalid_native_values;
    return density;
}

inline double DensityEvaluator::emit(const QueryInputs& inputs, double density, std::uint32_t flags) {
    if (trace_.active()) {
        const std::uint32_t recorded = usable(density) ? flags : flags | trace_invalid_value;
        trace_.record(inputs.epoch.et_seconds, inputs.position.as_eigen(), inputs.altitude_m,
                      inputs.latitude_deg, inputs.longitude_deg, density, recorded);
    }
    if (!usable(density)) ++stats_.invalid_native_values;
    return density;
}

// The complete identity of a native evaluation, assembled without performing
// one. The space-weather vector is the shared weather_identity() definition, so
// it is the same vector build_inputs() would carry and not a second rule; the
// epoch and the three position components are the original bits, never a
// re-derived geodetic form; the managed kernel token is part of the key, so a
// generation change is a miss even if it somehow reached a lookup.
//
// The supplied Sun's position bits are carried too, under exactly the rule the
// provider itself applies: they count only when the provider would actually use
// the supplied observation. This makes the key complete by construction rather
// than complete by assumption. It does not imply that several different Sun
// geometries are available at one epoch: under the held configuration the
// observation is fixed by the epoch and the managed kernel generation, and
// compatible_sun() is exact in both, so a second, different same-epoch Sun
// position is not representable through the public API. The bits are here so
// that if that ever stopped being true the memo would miss rather than serve a
// value built from a different Sun.
inline DensityEvaluator::ExactKey DensityEvaluator::exact_key(
    leo::Epoch epoch, const leo::InertialVector& position, const leo::SunObservation* sun) const {
    ExactKey key;
    key.epoch_bits = bits_of(epoch.et_seconds);
    key.x_bits = bits_of(position.x());
    key.y_bits = bits_of(position.y());
    key.z_bits = bits_of(position.z());
    key.weather = weather_identity(config_, environment_, model_, epoch);
    // Exactly the production sharing rule: a supplied Sun counts only when the
    // provider itself would use it.
    const bool shared = sun != nullptr && config_.options.sun_sharing &&
                        environment_.ephemeris.compatible_sun(*sun, epoch);
    key.sun_x_bits = key.sun_y_bits = key.sun_z_bits = 0;
    if (shared) {
        const Eigen::Vector3d value = sun->state().position_m.as_eigen();
        key.sun_x_bits = bits_of(value.x());
        key.sun_y_bits = bits_of(value.y());
        key.sun_z_bits = bits_of(value.z());
    }
    key.kernel_token = kernel_token_;
    return key;
}

// A trace record for a value served without building the QueryInputs. The
// geodetic coordinates are still derived, but only when a trace is actually
// being written: the Earth-fixed rotation is the production cached one, so this
// adds no native call and no weather read, and it keeps the trace format and
// the locality analysis's altitude/latitude/longitude columns complete. The
// coordinates are always the real ones, including for a refused value, so a
// trace never carries a fabricated altitude.
inline void DensityEvaluator::emit_prepared(leo::Epoch epoch, const leo::InertialVector& position,
                                            double density, std::uint32_t flags) {
    const Eigen::Matrix3d rotation =
        leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, config_.frame,
                                                          config_.options.orientation_reuse);
    const auto geodetic = leo::atmosphere::geodetic(rotation * position.as_eigen());
    trace_.record(epoch.et_seconds, position.as_eigen(), geodetic.altitude_m,
                  geodetic.latitude_rad * kDeg, geodetic.longitude_rad * kDeg, density, flags);
}

inline double DensityEvaluator::evaluate(leo::Epoch epoch, InertialVector position,
                                          const leo::SunObservation* sun) {
    const auto start = measurement_ ? Clock::now() : Clock::time_point{};
    if (generation_observation_ &&
        !environment_.ephemeris.compatible_sun(*generation_observation_, generation_observation_->epoch())) {
        // The managed generation stamp is checked *before* anything is served
        // from the store, including the pre-acquisition exact path below, so a
        // load/unload cycle or an explicit invalidation can never let a stale
        // value out. refresh_kernel_generation() clears every store.
        refresh_kernel_generation();
    }
    if (!generation_observation_) {
        if (sun != nullptr && environment_.ephemeris.compatible_sun(*sun, epoch))
            generation_observation_.emplace(*sun);
        else
            generation_observation_.emplace(environment_.ephemeris.observe_sun(epoch));
    }
    if (!time_origin_fixed_) {
        // The log-time origin is fixed at the first epoch the evaluator ever
        // sees and then held, so a later backwards or out-of-order query cannot
        // silently rescale the time axis.
        time_origin_ = epoch.et_seconds;
        time_origin_fixed_ = true;
    }
    ExactKey prebuilt{};
    if (settings_.candidate_kind == Candidate::exact) {
        // Candidate A is a memo of the *complete* native evaluation, so the
        // lookup happens before the geometry and the native input are built,
        // not after them.
        //
        // The owner is immutable for the lifetime of the evaluator: the Config
        // (frame, model selection, preprocessing switches, Ap-history switch),
        // the Environment (pinned weather archive, managed kernels) and the model
        // are all fixed and shared under the existing held-state contract. Inside
        // that owner the discrete space-weather vector is a pure function of the
        // epoch, and the managed generation is checked above and clears every
        // store when it changes. So (epoch bits, position bits, full weather
        // identity, supplied-Sun bits, kernel token) identifies the whole
        // evaluation, and a hit is a hit on the whole thing rather than on a
        // cheaper proxy for it.
        //
        // What this does not skip: the weather identity is part of the key, so
        // the UTC calendar conversion and the space-weather record read still
        // happen on every query, hit or miss. Skipping them would mean serving a
        // stored value without knowing whether it belongs to this epoch's
        // weather, which is exactly the error the weather guard exists to stop.
        // acquisition_skips counts what is genuinely skipped and its JSON note
        // states the rest.
        prebuilt = exact_key(epoch, position, sun);
        if (const double* stored = exact_.find(prebuilt)) {
            ++stats_.exact_hits;
            ++stats_.acquisition_skips;
            if (trace_.active()) emit_prepared(epoch, position, *stored, trace_exact_hit);
            if (measurement_) stats_.lookup_seconds += elapsed(start);
            previous_weather_ = prebuilt.weather;
            has_previous_weather_ = true;
            ++stats_.queries;
            if (building_) ++stats_.construction_queries;
            const std::size_t live = entries();
            if (live > stats_.peak_entries) stats_.peak_entries = live;
            return *stored;
        }
    }
    const QueryInputs inputs = build_inputs(epoch, position, sun);
    double density = 0;
    switch (settings_.candidate_kind) {
    case Candidate::exact: density = serve_exact(inputs, prebuilt); break;
    case Candidate::cell: density = serve_cell(inputs); break;
    case Candidate::interp: density = serve_interpolation(inputs); break;
    case Candidate::adaptive: density = serve_adaptive(inputs); break;
    case Candidate::control: density = serve_control(inputs); break;
    }
    if (measurement_) stats_.lookup_seconds += elapsed(start);
    previous_weather_ = inputs.weather;
    has_previous_weather_ = true;
    ++stats_.queries;
    if (building_) ++stats_.construction_queries;
    const std::size_t live = entries();
    if (live > stats_.peak_entries) stats_.peak_entries = live;
    return density;
}

inline double DensityEvaluator::serve_control(const QueryInputs& inputs) {
    // The control: exact preprocessing, no density caching at all. Every query
    // is one fresh native evaluation through the same acquisition path the other
    // candidates pay before they look anything up.
    return emit(inputs, native_call(inputs, nullptr), trace_native_call);
}

inline double DensityEvaluator::serve_exact(const QueryInputs& inputs, const ExactKey& key) {
    // The key was already assembled and probed by evaluate() before the
    // acquisition path ran; it is the same key, so a hit here can only happen
    // when the store gained the entry between the probe and here, which is
    // impossible on this single-threaded path but is handled rather than assumed.
    if (const double* stored = exact_.find(key)) {
        ++stats_.exact_hits;
        return emit(inputs, *stored, trace_exact_hit);
    }
    const double density = native_call(inputs, nullptr);
    if (usable(density)) {
        exact_.insert(key, density, stats_.evictions);
        ++stats_.insertions;
    }
    return emit(inputs, density, trace_native_call);
}

inline double DensityEvaluator::serve_cell(const QueryInputs& inputs) {
    const double altitude_km = inputs.altitude_m / 1000.0;
    if (!in_study_domain(altitude_km, inputs.latitude_deg, settings_.altitude_min_km,
                            settings_.altitude_max_km)) {
        ++stats_.domain_fallbacks;
        return emit(inputs, native_call(inputs, nullptr), trace_native_call | trace_guard_fallback);
    }
    const CellScheme& scheme = settings_.base_cells;
    const CellKey key{cell_index_altitude(altitude_km, scheme, settings_.altitude_min_km),
                      cell_index_latitude(inputs.latitude_deg, scheme),
                      cell_index_longitude(inputs.longitude_deg, scheme),
                      cell_index_time(inputs.epoch.et_seconds, time_origin_, scheme),
                      inputs.weather};
    if (const double* stored = cells_.find(key)) {
        // First-hit reuse is order dependent by construction: the value returned
        // is the one the first query into this cell produced, not an average or
        // a recomputation. The study measures that dependence rather than
        // hiding it.
        if (usable(*stored)) {
            ++stats_.cell_reuses;
            return emit(inputs, *stored, trace_cell_reuse);
        }
        ++stats_.guard_fallbacks;
        cells_.erase(key);
    }
    if (has_previous_weather_ && previous_weather_ != inputs.weather) ++stats_.weather_guard_rejections;
    const double density = native_call(inputs, nullptr);
    if (usable(density)) {
        cells_.insert(key, density, stats_.evictions);
        ++stats_.insertions;
    }
    return emit(inputs, density, trace_native_call);
}

inline double DensityEvaluator::serve_interpolation(const QueryInputs& inputs) {
    const double altitude_km = inputs.altitude_m / 1000.0;
    if (!in_study_domain(altitude_km, inputs.latitude_deg, settings_.altitude_min_km, settings_.altitude_max_km)) {
        ++stats_.domain_fallbacks;
        return emit(inputs, native_call(inputs, nullptr), trace_native_call | trace_guard_fallback);
    }
    const auto mode = settings_.interpolation;
    const std::array<bool, 4> active{
        mode == InterpolationMode::altitude || mode == InterpolationMode::four_dimensional,
        mode == InterpolationMode::spatial || mode == InterpolationMode::four_dimensional,
        mode == InterpolationMode::spatial || mode == InterpolationMode::four_dimensional,
        mode == InterpolationMode::time || mode == InterpolationMode::four_dimensional};
    const bool logarithmic = settings_.axis_mode == AxisMode::logarithmic;
    const double offset = inputs.epoch.et_seconds - time_origin_;
    if (active[3] && logarithmic && offset <= 0) {
        ++stats_.negative_time_fallbacks;
        return emit(inputs, native_call(inputs, nullptr), trace_native_call | trace_guard_fallback);
    }
    const auto longitude_count = static_cast<std::int64_t>(settings_.base_cells.longitude_cells());
    const auto latitude_count = static_cast<std::int64_t>(settings_.base_cells.latitude_cells());
    const double longitude_width = 360.0 / longitude_count;
    const double latitude_width = 180.0 / latitude_count;
    double longitude = std::fmod(inputs.longitude_deg + 180.0, 360.0);
    if (longitude < 0) longitude += 360.0;
    // Only an *active* axis is quantised. An inactive axis keeps the exact query
    // coordinate as its node coordinate and contributes a weight of one, so it
    // is never divided by a width and never becomes a source of a nonfinite
    // coordinate: a logarithmic axis that is switched off must not evaluate
    // log() of a nonpositive time offset on a backwards or out-of-order query.
    std::array<double, 4> raw{0, 0, 0, 0};
    if (active[0])
        raw[0] = (logarithmic ? std::log(altitude_km) : altitude_km - settings_.altitude_min_km) /
                 (logarithmic ? settings_.log_altitude_width : settings_.base_cells.altitude_km);
    if (active[1]) raw[1] = (inputs.latitude_deg + 90.0) / latitude_width;
    if (active[2]) raw[2] = longitude / longitude_width;
    if (active[3])
        raw[3] = (logarithmic ? std::log(offset) : offset) /
                 (logarithmic ? settings_.log_time_width : settings_.base_cells.time_s);
    std::array<std::int64_t, 4> index{};
    std::array<double, 4> fraction{};
    const std::array<double, 4> exact{altitude_km, inputs.latitude_deg, inputs.longitude_deg, inputs.epoch.et_seconds};
    std::size_t corner_count = 1;
    for (std::size_t axis = 0; axis < 4; ++axis) {
        if (!active[axis]) {
            index[axis] = std::bit_cast<std::int64_t>(exact[axis]);
            fraction[axis] = 0;
            continue;
        }
        if (!std::isfinite(raw[axis]))
            throw std::runtime_error("nonfinite interpolation coordinate on an active axis");
        index[axis] = static_cast<std::int64_t>(std::floor(raw[axis]));
        fraction[axis] = raw[axis] - std::floor(raw[axis]);
        if (active[axis]) corner_count *= 2;
    }
    double reconstructed_log = 0;
    for (std::size_t slot = 0; slot < corner_count; ++slot) {
        std::size_t remaining = slot;
        auto node_index = index;
        double weight = 1;
        for (std::size_t axis = 0; axis < 4; ++axis) {
            if (!active[axis]) continue;
            const auto digit = remaining & 1;
            remaining >>= 1;
            node_index[axis] += static_cast<std::int64_t>(digit);
            weight *= digit ? fraction[axis] : 1 - fraction[axis];
        }
        // A corner whose weight is exactly zero contributes exactly zero to the
        // weighted sum, because every node value here is finite. Building it
        // anyway would spend a real native evaluation on nothing, which is an
        // experimentally avoidable cost and not part of the candidate's
        // definition, so it is skipped. The reconstructed sum is unchanged
        // bit for bit.
        if (weight == 0.0) continue;
        if (active[2]) node_index[2] = (node_index[2] % longitude_count + longitude_count) % longitude_count;
        if (active[1]) node_index[1] = std::clamp<std::int64_t>(node_index[1], 0, latitude_count);
        const CornerKey key{node_index[0], node_index[1], node_index[2], node_index[3], inputs.weather};
        double node_log;
        if (const auto* stored = corners_.find(key)) {
            node_log = stored->log_density;
        } else {
            // Every corner this query needs that the store does not already
            // hold, whatever the reason: a cold cache, a cell never visited, or
            // a bounded store that evicted it. The figure is therefore the
            // number of fresh native node evaluations, and it can exceed the
            // number of queries because a four-dimensional interpolation needs up
            // to 2^N of them.
            ++stats_.incomplete_corners;
            const double node_altitude = active[0]
                ? (logarithmic ? std::exp(node_index[0] * settings_.log_altitude_width)
                               : settings_.altitude_min_km + node_index[0] * settings_.base_cells.altitude_km)
                : altitude_km;
            const double node_latitude = active[1] ? -90.0 + node_index[1] * latitude_width : inputs.latitude_deg;
            const double node_longitude = active[2] ? -180.0 + node_index[2] * longitude_width : inputs.longitude_deg;
            const leo::Epoch node_epoch{active[3]
                ? time_origin_ + (logarithmic ? std::exp(node_index[3] * settings_.log_time_width)
                                             : node_index[3] * settings_.base_cells.time_s)
                : inputs.epoch.et_seconds};
            if (node_altitude < model_altitude_min_km(model_) ||
                node_altitude > model_altitude_max_km(model_) + 1e-9 ||
                (active[1] && std::abs(node_latitude) >= domain_polar_fallback_deg)) {
                ++stats_.out_of_contract_nodes;
                return emit(inputs, native_call(inputs, nullptr), trace_native_call | trace_guard_fallback);
            }
            const auto node_position = inertial_from_geodetic(node_epoch, node_latitude / kDeg,
                node_longitude / kDeg, node_altitude * 1000.0, config_.frame);
            const auto node_inputs = build_inputs(node_epoch, node_position, nullptr);
            if (node_inputs.weather != inputs.weather) {
                ++stats_.weather_guard_rejections;
                ++stats_.guard_fallbacks;
                return emit(inputs, native_call(inputs, nullptr), trace_native_call | trace_guard_fallback);
            }
            const double density = native_call(node_inputs, nullptr);
            if (!usable(density)) throw std::runtime_error("invalid native interpolation node");
            node_log = std::log(density);
            corners_.insert(key, CornerValue{node_log}, stats_.evictions);
            ++stats_.insertions;
        }
        reconstructed_log += weight * node_log;
    }
    const double density = std::exp(reconstructed_log);
    if (!usable(density)) {
        ++stats_.guard_fallbacks;
        return emit(inputs, native_call(inputs, nullptr), trace_native_call | trace_guard_fallback);
    }
    ++stats_.interpolations;
    return emit(inputs, density, trace_interpolation);
}

inline double DensityEvaluator::serve_adaptive(const QueryInputs& inputs) {
    const double altitude_km = inputs.altitude_m / 1000.0;
    if (!in_study_domain(altitude_km, inputs.latitude_deg, settings_.altitude_min_km,
                            settings_.altitude_max_km)) {
        ++stats_.domain_fallbacks;
        return emit(inputs, native_call(inputs, nullptr), trace_native_call | trace_guard_fallback);
    }
    const double offset = inputs.epoch.et_seconds - time_origin_;
    if (!(offset > 0.0) || !std::isfinite(offset)) {
        ++stats_.negative_time_fallbacks;
        return emit(inputs, native_call(inputs, nullptr), trace_native_call | trace_guard_fallback);
    }
    // Refine the local spatial-temporal cell after deterministic periodic native
    // validation; no sampled local target is claimed as a worst-case guarantee.
    const auto depth_scheme = [&](std::int64_t depth) {
        const double factor = std::ldexp(1.0, -static_cast<double>(depth));
        CellScheme scheme = settings_.base_cells;
        scheme.altitude_km = std::max(1e-6, scheme.altitude_km * factor);
        scheme.latitude_deg = std::max(1e-6, scheme.latitude_deg * factor);
        scheme.longitude_deg = std::max(1e-6, scheme.longitude_deg * factor);
        scheme.time_s = std::max(1e-6, scheme.time_s * factor);
        return scheme;
    };
    const std::uint64_t divisor = std::max<std::uint64_t>(1, settings_.adaptive_probe_divisor);
    const bool probe = (++probe_counter_ % divisor) == 0;

    // Look for the finest anchor first: a refined anchor is always at least as
    // informative as the coarse one it replaced. The altitude index uses the
    // configured band, exactly as every other candidate and the locality
    // analysis do, so a refined cell is always a genuine sub-cell of the coarse
    // one it replaced and a moved band moves all of them together.
    auto locate = [&](std::int64_t depth, AdaptiveKey& out) -> const AdaptiveValue* {
        const CellScheme scheme = depth_scheme(depth);
        out = AdaptiveKey{cell_index_altitude(altitude_km, scheme, settings_.altitude_min_km),
                          cell_index_latitude(inputs.latitude_deg, scheme),
                          cell_index_longitude(inputs.longitude_deg, scheme),
                          cell_index_time(inputs.epoch.et_seconds, time_origin_, scheme),
                          depth, inputs.weather};
        return adaptive_.find(out);
    };

    std::int64_t depth = 0;
    AdaptiveKey anchor_key{};
    const AdaptiveValue* anchor = nullptr;
    for (depth = static_cast<std::int64_t>(settings_.adaptive_max_depth); depth >= 0; --depth) {
        anchor = locate(depth, anchor_key);
        if (anchor != nullptr) break;
    }

    if (probe || anchor == nullptr) {
        if (anchor == nullptr && has_previous_weather_ && previous_weather_ != inputs.weather)
            ++stats_.weather_guard_rejections;
        const double density = native_call(inputs, nullptr);
        ++stats_.adaptive_probes;
        if (usable(density)) {
            if (anchor != nullptr) {
                // The measured local relative error is the difference between
                // the value this cell currently serves and the native value at
                // this exact physical input. That measurement, not a model of
                // the density field, is what drives the resolution.
                AdaptiveValue updated = *anchor;
                updated.measured_relative_error = std::abs(updated.density - density) / density;
                updated.probes += 1;
                if (updated.measured_relative_error > settings_.target &&
                    depth < static_cast<std::int64_t>(settings_.adaptive_max_depth)) {
                    // Refinement: the anchor moves into the finer sub-cell that
                    // contains this query, one level deeper. Storage stays
                    // bounded and the policy stays a plain rule.
                    adaptive_.erase(anchor_key);
                    AdaptiveKey fine{};
                    locate(depth + 1, fine);
                    adaptive_.insert(fine, AdaptiveValue{density, updated.measured_relative_error, 1, 0},
                                     stats_.evictions);
                    ++stats_.adaptive_refinements;
                } else if (updated.measured_relative_error > settings_.target) {
                    // At the deepest permitted level the cell cannot be made
                    // finer. It is kept, and re-seeded with the value just
                    // measured at this exact input: discarding it here would
                    // leave the next query in this cell with no anchor at all and
                    // would silently drop the whole region back to the coarsest
                    // cell, which is a worse answer than the one already there.
                    ++stats_.adaptive_degradations;
                    AdaptiveValue degraded = *anchor;
                    degraded.density = density;
                    degraded.measured_relative_error = 0.0;
                    degraded.probes = 1;
                    adaptive_.insert(anchor_key, degraded, stats_.evictions);
                } else {
                    updated.density = density;
                    adaptive_.insert(anchor_key, updated, stats_.evictions);
                }
            } else {
                AdaptiveKey fresh{};
                locate(0, fresh);
                adaptive_.insert(fresh, AdaptiveValue{density, 0.0, 1, 0}, stats_.evictions);
                ++stats_.insertions;
            }
        } else if (anchor != nullptr) {
            // A native value that is not usable at an anchor's own cell can
            // never be served again from it.
            adaptive_.erase(anchor_key);
        }
        return emit(inputs, density, trace_native_call | trace_adaptive_probe);
    }

    if (usable(anchor->density)) {
        AdaptiveValue updated = *anchor;
        updated.serves += 1;
        adaptive_.insert(anchor_key, updated, stats_.evictions);
        ++stats_.adaptive_anchor_uses;
        return emit(inputs, updated.density, trace_adaptive_anchor);
    }
    ++stats_.guard_fallbacks;
    adaptive_.erase(anchor_key);
    return emit(inputs, native_call(inputs, nullptr), trace_native_call | trace_guard_fallback);
}

inline std::size_t DensityEvaluator::entries() const {
    switch (settings_.candidate_kind) {
    case Candidate::exact: return exact_.size();
    case Candidate::cell: return cells_.size();
    case Candidate::interp: return corners_.size();
    case Candidate::adaptive: return adaptive_.size();
    case Candidate::control: return 0;
    }
    return 0;
}

inline std::size_t DensityEvaluator::payload_bytes() const {
    switch (settings_.candidate_kind) {
    case Candidate::exact: return exact_.payload_bytes();
    case Candidate::cell: return cells_.payload_bytes();
    case Candidate::interp: return corners_.payload_bytes();
    case Candidate::adaptive: return adaptive_.payload_bytes();
    case Candidate::control: return 0;
    }
    return 0;
}

inline DensityStatistics DensityEvaluator::counts() const {
    DensityStatistics snapshot = stats_;
    snapshot.entries = entries();
    snapshot.capacity = settings_.capacity;
    snapshot.payload_bytes = payload_bytes();
    return snapshot;
}

inline std::string DensityEvaluator::statistics() const {
    const DensityStatistics snapshot = counts();
    Json j;
    j.add("kind", "density-evaluator-statistics");
    j.add("model", model_name(model_));
    j.add("candidate", candidate_name(settings_.candidate_kind));
    j.add("capacity_policy", settings_.replace_policy_lru ? "lru" : "fifo");
    j.add("time_origin_et_seconds", time_origin_);
    j.add("time_origin_rule", "fixed at the first epoch seen, then held; a backwards or out-of-order "
                              "query cannot rescale the log-time axis");
    j.add("kernel_generation_token", static_cast<double>(kernel_token_));
    j.add("authoritative_direct_calls", authoritative_calls_);
    j.add("last_invalidation_reason", last_invalidation_reason_);
    j.raw("counts", snapshot.json());
    j.raw("trace", trace_summary_);
    j.add("measurement_enabled", measurement_);
    j.add("measurement_note",
          "construction_seconds, lookup_seconds and native_seconds are in-evaluator diagnostics and "
          "add two clock reads per query; headline performance is measured with them disabled");
    return j.str();
}

inline void DensityEvaluator::invalidate(const std::string& reason) {
    exact_.clear();
    cells_.clear();
    corners_.clear();
    adaptive_.clear();
    ++stats_.invalidations;
    last_invalidation_reason_ = reason;
}

inline bool DensityEvaluator::refresh_kernel_generation() {
    if (!generation_observation_ ||
        environment_.ephemeris.compatible_sun(*generation_observation_, generation_observation_->epoch()))
        return false;
    generation_observation_.reset();
    ++kernel_token_;
    invalidate("managed SPICE generation changed");
    return true;
}


inline void DensityEvaluator::set_capacity(std::size_t capacity) {
    settings_.capacity = std::max<std::size_t>(1, capacity);
    exact_ = BoundedStore<ExactKey, double>(settings_.capacity, settings_.replace_policy_lru);
    cells_ = BoundedStore<CellKey, double>(settings_.capacity, settings_.replace_policy_lru);
    corners_ = BoundedStore<CornerKey, CornerValue>(settings_.capacity, settings_.replace_policy_lru);
    adaptive_ = BoundedStore<AdaptiveKey, AdaptiveValue>(settings_.capacity, settings_.replace_policy_lru);
    stats_.capacity = settings_.capacity;
}

inline void DensityEvaluator::set_time_origin(double et_seconds) {
    time_origin_ = et_seconds;
    time_origin_fixed_ = true;
    invalidate("time origin changed: every log-time cell index is invalidated with it");
}

inline void DensityEvaluator::finish_trace() {
    if (trace_finished_) return;
    trace_finished_ = true;
    trace_summary_ = trace_.finish();
}

} // namespace cache_study