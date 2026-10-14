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
    return j.str();
}

inline std::string StudySettings::json() const {
    Json j;
    j.add("kind", kind);
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
    j.add("cell_reuses", cell_reuses);
    j.add("interpolations", interpolations);
    j.add("adaptive_anchor_uses", adaptive_anchor_uses);
    j.add("adaptive_probes", adaptive_probes);
    j.add("adaptive_refinements", adaptive_refinements);
    j.add("adaptive_degradations", adaptive_degradations);
    j.add("native_calls", native_calls);
    j.add("native_call_fraction", measured());
    j.add("native_calls_avoided_fraction", avoided());
    j.add("native_calls_avoided_percent", avoided() * 100.0);
    j.add("insertions", insertions);
    j.add("evictions", evictions);
    j.add("guard_fallbacks", guard_fallbacks);
    j.add("domain_fallbacks", domain_fallbacks);
    j.add("weather_guard_rejections", weather_guard_rejections);
    j.add("incomplete_corners", incomplete_corners);
    j.add("out_of_contract_nodes", out_of_contract_nodes);
    j.add("corner_note",
          "incomplete_corners includes corners refused because their space-weather identity differs "
          "bitwise from the query's, which is the weather guard doing its work");
    j.add("negative_time_fallbacks", negative_time_fallbacks);
    j.add("invalid_values_refused", invalid_native_values);
    j.add("invalidations", invalidations);
    j.add("construction_queries", construction_queries);
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

inline double DensityEvaluator::evaluate(leo::Epoch epoch, leo::InertialVector position,
                                          const leo::SunObservation* sun) {
    const auto start = measurement_ ? Clock::now() : Clock::time_point{};
    if (generation_observation_ &&
        !environment_.ephemeris.compatible_sun(*generation_observation_, generation_observation_->epoch())) {
        refresh_kernel_generation();
    }
    if (!generation_observation_) {
        if (sun != nullptr && environment_.ephemeris.compatible_sun(*sun, epoch))
            generation_observation_.emplace(*sun);
        else
            generation_observation_.emplace(environment_.ephemeris.observe_sun(epoch));
    }
    const QueryInputs inputs = build_inputs(epoch, position, sun);
    if (!time_origin_fixed_) {
        // The log-time origin is fixed at the first epoch the evaluator ever
        // sees and then held, so a later backwards or out-of-order query cannot
        // silently rescale the time axis.
        time_origin_ = epoch.et_seconds;
        time_origin_fixed_ = true;
    }
    double density = 0;
    switch (settings_.candidate_kind) {
    case Candidate::exact: density = serve_exact(inputs); break;
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

inline double DensityEvaluator::serve_exact(const QueryInputs& inputs) {
    const ExactKey key{bits_of(inputs.epoch.et_seconds), bits_of(inputs.position.x()),
                       bits_of(inputs.position.y()), bits_of(inputs.position.z()), inputs.weather};
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
    const CellKey key{cell_index_altitude(altitude_km, scheme),
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
    const std::array<double, 4> raw{
        (logarithmic ? std::log(altitude_km) : altitude_km - settings_.altitude_min_km) /
            (logarithmic ? settings_.log_altitude_width : settings_.base_cells.altitude_km),
        (inputs.latitude_deg + 90.0) / latitude_width, longitude / longitude_width,
        (active[3] && logarithmic ? std::log(offset) : offset) /
            (logarithmic ? settings_.log_time_width : settings_.base_cells.time_s)};
    std::array<std::int64_t, 4> index{};
    std::array<double, 4> fraction{};
    const std::array<double, 4> exact{altitude_km, inputs.latitude_deg, inputs.longitude_deg, inputs.epoch.et_seconds};
    std::size_t corner_count = 1;
    for (std::size_t axis = 0; axis < 4; ++axis) {
        if (!std::isfinite(raw[axis]))
            throw std::runtime_error("nonfinite interpolation coordinate");
        index[axis] = active[axis] ? static_cast<std::int64_t>(std::floor(raw[axis]))
                                  : std::bit_cast<std::int64_t>(exact[axis]);
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
        if (active[2]) node_index[2] = (node_index[2] % longitude_count + longitude_count) % longitude_count;
        if (active[1]) node_index[1] = std::clamp<std::int64_t>(node_index[1], 0, latitude_count);
        const CornerKey key{node_index[0], node_index[1], node_index[2], node_index[3], inputs.weather};
        double node_log;
        if (const auto* stored = corners_.find(key)) {
            node_log = stored->log_density;
        } else {
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
    // informative as the coarse one it replaced.
    auto locate = [&](std::int64_t depth, AdaptiveKey& out) -> const AdaptiveValue* {
        const CellScheme scheme = depth_scheme(depth);
        out = AdaptiveKey{cell_index_altitude(altitude_km, scheme),
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
        if (anchor != nullptr && usable(density) && density > 0.0) {
            // Midpoint-error measurement: the stored anchor value is compared
            // with the native value at this exact physical input. That measured
            // local relative error, not a model, drives the resolution.
            AdaptiveValue updated = *anchor;
            updated.measured_relative_error = std::abs(updated.density - density) / density;
            updated.probes += 1;
            if (updated.measured_relative_error > settings_.target &&
                depth < static_cast<std::int64_t>(settings_.adaptive_max_depth)) {
                // Refinement: the anchor moves into the finer sub-cell that
                // contains this query, at one level deeper. Storage stays
                // bounded and the policy stays a plain rule.
                adaptive_.erase(anchor_key);
                AdaptiveKey fine{};
                locate(depth + 1, fine);
                AdaptiveValue created{density, updated.measured_relative_error, 1, 0};
                adaptive_.insert(fine, created, stats_.evictions);
                ++stats_.adaptive_refinements;
            } else if (updated.measured_relative_error > settings_.target) {
                ++stats_.adaptive_degradations;
                adaptive_.erase(anchor_key);
            } else {
                updated.density = density;
                adaptive_.insert(anchor_key, updated, stats_.evictions);
            }
        } else if (usable(density)) {
            AdaptiveKey fresh{};
            locate(0, fresh);
            adaptive_.insert(fresh, AdaptiveValue{density, 0.0, 1, 0}, stats_.evictions);
            ++stats_.insertions;
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