// ATM-CACHE-001 Phase 2: atmospheric density sensitivity.
//
// Every reference density is a fresh production DirectAtmosphere evaluation
// with all ATM-OPT-001 preprocessing reuse enabled. Every space-weather value
// is read from the pinned records and published; nothing is injected.
#include "support.hpp"

namespace cache_study {
std::string sensitivity_study(const Config& c, Environment& env, const StudySettings& s) {
    NativeProbe probe(c, env, s.model_kind, c.ap_history);
    std::vector<WeatherEpoch> pool = build_epoch_pool(c, env, s);
    const std::vector<WeatherEpoch> selected = select_regimes(c, env, pool);

    Json result;
    result.add("section", "atmospheric density sensitivity");
    result.add("quantity", "log density ln(rho(t,h,phi,lambda)) under the models' own input contract");
    result.add("method",
               "finite differences at several perturbation scales plus controlled parameter sweeps; "
               "local linearity is measured, never assumed");
    result.add("weather_policy",
               "every weather value is read from the pinned records and published; no index is "
               "injected, scaled, smoothed or hand-edited anywhere in this study");

    {
        std::vector<std::string> rows;
        for (const auto& entry : pool) {
            Json row;
            row.add("name", entry.name);
            row.add("utc", entry.utc);
            row.raw("inputs", entry.record.json());
            row.add("role", [&] {
                const bool anchor = std::any_of(selected.begin(), selected.end(),
                                                [&](const WeatherEpoch& chosen) {
                                                    return chosen.name == entry.name &&
                                                           chosen.epoch.et_seconds == entry.epoch.et_seconds;
                                                });
                return anchor ? "primary weather regime" : "additional epoch inside the pinned records";
            }());
            rows.push_back(row.str());
        }
        result.raw("weather_pool", raw_array(rows));
        result.raw("primary_regimes", raw_array([&] {
            std::vector<std::string> names;
            for (const auto& entry : selected) names.push_back("\"" + entry.name + "\"");
            return names;
        }()));
        result.add("weather_selection_rule",
                   "the caller-supplied pinned fixture epochs are the primary regimes and are never "
                   "replaced by a pick from the scan; the scan only adds coverage around them");
        result.add("weather_index_proof",
                   "the actual ap, F10.7 and its averages, the Ap vector and DSTDTC of every primary "
                   "regime are printed above, so a quiet/moderate/storm claim is checkable rather than asserted");
    }

    // ---- 1. altitude profiles and altitude finite differences ----
    // Altitude sensitivity is reported at the equator, at a mid-latitude and at
    // a high-latitude point, because the gradient itself depends on latitude and
    // reporting only the equator would understate it.
    const std::vector<double> section_latitudes{0.0, 45.0, 80.0};
    const std::vector<double> finite_difference_lst{0.0, 6.0, 12.0, 18.0};
    {
        std::vector<std::string> profiles, differences;
        ErrorHistogram histogram;
        double worst = 0, worst_altitude = 0, worst_step = 0, worst_lst = 0, worst_latitude = 0;
        std::vector<double> gradient(altitude_grid_km().size(), 0.0);
        std::vector<double> curvature(altitude_grid_km().size(), 0.0);
        std::vector<double> heat(altitude_grid_km().size() * local_solar_time_grid_hours().size(), 0.0);

        for (const auto& weather : selected) {
            for (double latitude : section_latitudes) {
                for (double local_solar : local_solar_time_grid_hours()) {
                    const double longitude = longitude_for_local_solar_time(weather.epoch, local_solar);
                    std::vector<double> log_density, density;
                    for (double altitude : altitude_grid_km()) {
                        if (altitude < s.altitude_min_km || altitude > s.altitude_max_km) continue;
                        const double value = probe_density(
                            probe, Probe{weather.epoch, latitude, longitude, altitude});
                        density.push_back(value);
                        log_density.push_back(std::log(value));
                    }
                    Json profile;
                    profile.add("weather", weather.name);
                    profile.add("utc", weather.utc);
                    profile.add("latitude_deg", latitude);
                    profile.add("longitude_deg", longitude);
                    profile.add("local_solar_time_hours", local_solar);
                    profile.raw("altitude_km", numbers(altitude_grid_km()));
                    profile.raw("density_kg_m3", numbers(density));
                    profile.raw("log_density", numbers(log_density));
                    profiles.push_back(profile.str());
                }
            }

            for (double latitude : section_latitudes)
                for (double local_solar : finite_difference_lst) {
                    const double longitude = longitude_for_local_solar_time(weather.epoch, local_solar);
                    const std::size_t column = static_cast<std::size_t>(
                        std::find(local_solar_time_grid_hours().begin(),
                                  local_solar_time_grid_hours().end(),
                                  local_solar) - local_solar_time_grid_hours().begin());
                    for (double step : altitude_perturbations_km())
                        for (std::size_t index = 0; index < altitude_grid_km().size(); ++index) {
                            const double altitude = altitude_grid_km()[index];
                            if (altitude - step < s.altitude_min_km ||
                                altitude + step > s.altitude_max_km)
                                continue;
                            const double up_density =
                                probe_density(probe, Probe{weather.epoch, latitude, longitude, altitude + step});
                            const double centre_density =
                                probe_density(probe, Probe{weather.epoch, latitude, longitude, altitude});
                            const double down_density =
                                probe_density(probe, Probe{weather.epoch, latitude, longitude, altitude - step});
                            const double up = std::log(up_density);
                            const double centre = std::log(centre_density);
                            const double down = std::log(down_density);
                            const double first = (up - down) / (2.0 * step);
                            const double second = (up - 2.0 * centre + down) / (step * step);
                            const double span = std::abs(up - down);
                            const std::size_t slot = index * local_solar_time_grid_hours().size() + column;
                            gradient[index] = std::max(gradient[index], std::abs(first));
                            curvature[index] = std::max(curvature[index], std::abs(second));
                            // The heat map is defined at a 1 km central difference
                            // only; the wider steps below it are recorded in the
                            // difference table and never folded into the map.
                            if (step == 1.0) heat[slot] = std::max(heat[slot], std::abs(first));
                            histogram.add(span);
                            if (span > worst) {
                                worst = span;
                                worst_altitude = altitude;
                                worst_step = step;
                                worst_lst = local_solar;
                                worst_latitude = latitude;
                            }
                            Json row;
                            row.add("weather", weather.name);
                            row.add("latitude_deg", latitude);
                            row.add("local_solar_time_hours", local_solar);
                            row.add("altitude_km", altitude);
                            row.add("step_km", step);
                            row.add("reference_density_kg_m3", centre_density);
                            row.add("reference_log_density", centre);
                            row.add("density_above_kg_m3", up_density);
                            row.add("density_below_kg_m3", down_density);
                            row.add("signed_absolute_difference_kg_m3", up_density - down_density);
                            row.add("signed_relative_difference", (up_density - down_density) / centre_density);
                            row.add("signed_log_difference", up - down);
                            row.add("ratio_span_upper_to_lower", up_density / down_density);
                            row.add("log_ratio_span", span);
                            row.add("dlog_density_daltitude_per_km", first);
                            row.add("second_derivative_per_km2", second);
                            differences.push_back(row.str());
                        }
                }
        }
        result.raw("altitude_profile", raw_array(profiles));
        result.add("altitude_profile_latitudes_deg", numbers(section_latitudes));
        result.add("altitude_profile_note",
                   "altitude sensitivity is reported at the equator, a mid-latitude and a "
                   "high-latitude point, because the gradient itself depends on latitude");
        result.add("altitude_perturbations_km", numbers(altitude_perturbations_km()));
        result.add("altitude_finite_difference_local_solar_hours", numbers(finite_difference_lst));
        result.raw("altitude_difference", raw_array(differences));
        result.raw("max_abs_dlog_rho_daltitude_by_altitude", numbers(gradient));
        result.raw("max_abs_d2log_rho_daltitude2_by_altitude", numbers(curvature));
        result.raw("altitude_difference_histogram", histogram.json());
        Json worst_row;
        worst_row.add("log_ratio_span", worst);
        worst_row.add("ratio_span", std::expm1(worst));
        worst_row.add("altitude_km", worst_altitude);
        worst_row.add("step_km", worst_step);
        worst_row.add("latitude_deg", worst_latitude);
        worst_row.add("local_solar_time_hours", worst_lst);
        worst_row.add("reading",
                      "a pair of states this far apart in log density defines the cell width this "
                      "location can afford; the larger steps in the table show how much of that "
                      "change is curvature rather than gradient");
        result.add("largest_observed_log_density_span_in_altitude", worst_row.str());
        result.raw("altitude_by_local_solar_time_heatmap", [&] {
            Json heatmap;
            heatmap.add("quantity",
                        "max |d ln rho / dh| per km over the tested latitudes, from a 1 km central "
                        "difference only");
            heatmap.raw("rows_altitude_km", numbers(altitude_grid_km()));
            heatmap.raw("columns_local_solar_time_hours", numbers(local_solar_time_grid_hours()));
            heatmap.raw("rows_latitudes_deg", numbers(section_latitudes));
            std::vector<std::string> matrix;
            for (std::size_t index = 0; index < altitude_grid_km().size(); ++index)
                matrix.push_back(numbers(std::vector<double>(
                    heat.begin() + long(index * local_solar_time_grid_hours().size()),
                    heat.begin() + long((index + 1) * local_solar_time_grid_hours().size()))));
            heatmap.raw("values", raw_array(matrix));
            heatmap.add("reading",
                        "rows are altitudes, columns are local solar time; the steepest cells are "
                        "where a uniform altitude cell width costs the most accuracy");
            return heatmap.str();
        }());
    }

    // ---- 2. latitude, longitude, wrap and near-pole behaviour ----
    {
        std::vector<std::string> latitude_rows, longitude_rows, differences;
        ErrorHistogram histogram;
        double worst = 0, worst_step = 0;
        for (const auto& weather : selected) {
            const double longitude = longitude_for_local_solar_time(weather.epoch, 12.0);
            std::vector<double> latitude_log, longitude_log;
            for (double latitude : latitude_grid_deg())
                latitude_log.push_back(std::log(
                    probe_density(probe, Probe{weather.epoch, latitude, longitude, s.h_km})));
            for (double sample : longitude_grid_deg())
                longitude_log.push_back(std::log(
                    probe_density(probe, Probe{weather.epoch, 0.0, sample, s.h_km})));
            Json latitude_row;
            latitude_row.add("weather", weather.name);
            latitude_row.add("utc", weather.utc);
            latitude_row.add("altitude_km", s.h_km);
            latitude_row.add("longitude_deg", longitude);
            latitude_row.raw("latitude_deg", numbers(latitude_grid_deg()));
            latitude_row.raw("log_density", numbers(latitude_log));
            latitude_row.add("note",
                             "the first and last latitudes sit exactly on the study's polar fallback "
                             "limit, so the reader can see where a uniform longitude cell stops "
                             "meaning anything");
            latitude_rows.push_back(latitude_row.str());

            Json longitude_row;
            longitude_row.add("weather", weather.name);
            longitude_row.add("utc", weather.utc);
            longitude_row.add("altitude_km", s.h_km);
            longitude_row.add("latitude_deg", 0.0);
            longitude_row.raw("longitude_deg", numbers(longitude_grid_deg()));
            longitude_row.raw("log_density", numbers(longitude_log));
            longitude_rows.push_back(longitude_row.str());

            for (std::size_t index = 1; index + 1 < latitude_grid_deg().size(); ++index)
                for (double step : {0.1, 1.0, 5.0, 10.0}) {
                    const double latitude = latitude_grid_deg()[index];
                    if (std::abs(latitude + step) > domain_polar_fallback_deg ||
                        std::abs(latitude - step) > domain_polar_fallback_deg) continue;
                    const double up = std::log(probe_density(
                        probe, Probe{weather.epoch, latitude + step, longitude, s.h_km}));
                    const double down = std::log(probe_density(
                        probe, Probe{weather.epoch, latitude - step, longitude, s.h_km}));
                    const double span = std::abs(up - down);
                    histogram.add(span);
                    Json row;
                    row.add("latitude_deg", latitude);
                    row.add("step_deg", step);
                    row.add("dlog_density_dlatitude_per_deg", (up - down) / (2.0 * step));
                    row.add("log_ratio_span", span);
                    row.add("ratio_span", std::expm1(span));
                    row.add("signed_log_difference", up - down);
                    row.add("signed_relative_difference", std::expm1(up - down));
                    differences.push_back(row.str());
                    if (span > worst) { worst = span; worst_step = step; }
                }
        }
        result.raw("latitude_profile", raw_array(latitude_rows));
        result.raw("longitude_profile", raw_array(longitude_rows));
        result.raw("latitude_grid_deg", numbers(latitude_grid_deg()));
        result.raw("longitude_grid_deg", numbers(longitude_grid_deg()));
        result.raw("latitude_difference", raw_array(differences));
        result.raw("latitude_difference_histogram", histogram.json());
        result.add("largest_observed_log_density_span_in_latitude", worst);
        result.add("largest_latitude_span_step_deg", worst_step);

        Json wrap;
        wrap.add("question",
                 "does a geographic coordinate change across the +180/-180 seam behave like the same "
                 "change away from the seam, or does it introduce an artificial discontinuity?");
        std::vector<double> seam, interior;
        for (const auto& weather : selected)
            for (double altitude : {400.0, 600.0, 900.0}) {
                seam.push_back(std::log(probe_density(probe, Probe{weather.epoch, 0.0, 179.9999, altitude})) -
                               std::log(probe_density(probe, Probe{weather.epoch, 0.0, -179.9999, altitude})));
                interior.push_back(std::log(probe_density(probe, Probe{weather.epoch, 0.0, 0.0001, altitude})) -
                                   std::log(probe_density(probe, Probe{weather.epoch, 0.0, -0.0001, altitude})));
            }
        wrap.add("span_log_across_seam", numbers(seam));
        wrap.add("span_log_away_from_seam", numbers(interior));
        wrap.add("max_span_log_across_seam", seam.empty() ? 0.0 : *std::max_element(seam.begin(), seam.end()));
        wrap.add("max_span_log_away_from_seam",
                 interior.empty() ? 0.0 : *std::max_element(interior.begin(), interior.end()));
        wrap.add("conclusion",
                 "the models are evaluated in their own Earth-fixed frame, so the seam is a coordinate "
                 "artefact of any geographic quantisation rather than of the model; this study wraps its "
                 "cell indices so no cache cell ever straddles it, and this table shows what a "
                 "non-wrapping implementation would have to lose");
        result.raw("longitude_wrap_check", wrap.str());

        const leo::Epoch epoch = selected.front().epoch;
        std::vector<double> polar_log;
        for (double latitude : latitude_grid_deg())
            polar_log.push_back(std::log(probe_density(probe, Probe{epoch, latitude, 0.0, s.h_km})));
        Json polar;
        polar.raw("latitude_deg", numbers(latitude_grid_deg()));
        polar.raw("log_density_at_longitude_zero", numbers(polar_log));
        polar.add("fallback_limit_deg", domain_polar_fallback_deg);
        polar.add("fallback_rule",
                  "above this limit a geographic cell has no meaning, so every candidate falls back "
                  "to the native model instead of inventing a cell boundary");
        polar.add("longitude_spread_across_the_grid_log", [&] {
            double worst_spread = 0;
            for (double latitude : {80.0, 89.0, 89.9, domain_polar_fallback_deg}) {
                std::vector<double> values;
                for (double sample : {-180.0, -90.0, 0.0, 90.0, 180.0})
                    values.push_back(std::log(probe_density(probe, Probe{epoch, latitude, sample, s.h_km})));
                worst_spread = std::max(
                    worst_spread, *std::max_element(values.begin(), values.end()) -
                                       *std::min_element(values.begin(), values.end()));
            }
            return worst_spread;
        }());
        result.raw("near_polar_behaviour", polar.str());
    }

    // ---- 3. local solar time, with day/night/dawn/dusk named ----
    {
        std::vector<std::string> rows, differences;
        ErrorHistogram histogram;
        for (const auto& weather : selected)
            for (double altitude : {400.0, 600.0, 900.0}) {
                std::vector<double> log_density;
                for (double local_solar : local_solar_time_grid_hours())
                    log_density.push_back(std::log(probe_density(
                        probe, Probe{weather.epoch, 0.0,
                                     longitude_for_local_solar_time(weather.epoch, local_solar), altitude})));
                Json row;
                row.add("weather", weather.name);
                row.add("utc", weather.utc);
                row.add("altitude_km", altitude);
                row.add("local_solar_time_grid_hours", numbers(local_solar_time_grid_hours()));
                row.add("labels", strings({"midnight", "dawn", "morning", "noon", "afternoon", "dusk", "evening"}));
                row.raw("log_density", numbers(log_density));
                row.add("day_night_ratio_noon_over_midnight",
                        log_density[3] - log_density[0]);
                rows.push_back(row.str());
                for (double local_solar : local_solar_time_grid_hours())
                    for (double step : {0.25, 1.0, 3.0}) {
                        const double east = longitude_for_local_solar_time(weather.epoch, local_solar + step);
                        const double west = longitude_for_local_solar_time(weather.epoch, local_solar - step);
                        const double east_log =
                            std::log(probe_density(probe, Probe{weather.epoch, 0.0, east, altitude}));
                        const double west_log =
                            std::log(probe_density(probe, Probe{weather.epoch, 0.0, west, altitude}));
                        const double span = std::abs(east_log - west_log);
                        histogram.add(span);
                        Json difference;
                        difference.add("altitude_km", altitude);
                        difference.add("local_solar_time_hours", local_solar);
                        difference.add("step_hours", step);
                        difference.add("log_ratio_span", span);
                        difference.add("ratio_span", std::expm1(span));
                        difference.add("signed_log_difference", east_log - west_log);
                        difference.add("signed_relative_difference",
                                        std::expm1(east_log - west_log));
                        differences.push_back(difference.str());
                    }
            }
        result.raw("local_solar_time_profile", raw_array(rows));
        result.raw("local_solar_time_difference", raw_array(differences));
        result.raw("local_solar_time_difference_histogram", histogram.json());
        result.add("local_solar_time_note",
                   "the local solar time table is where a dawn or dusk gradient shows up; the "
                   "interpolation candidate's error distribution has to be read against these values "
                   "rather than against a global average");
    }

    // ---- 4. time separations from milliseconds to hours ----
    {
        std::vector<std::string> rows;
        ErrorHistogram histogram;
        double worst_span = 0, worst_step = 0, worst_lst = 0;
        std::string worst_utc, worst_weather;
        double worst_asymmetry = 0;
        for (const auto& weather : selected)
          for (double anchor_offset : {0.0, 48637.13}) {
            const leo::Epoch centre_epoch{weather.epoch.et_seconds + anchor_offset};
            const auto centre_identity = weather_identity(c, env, s.model_kind, centre_epoch);
            for (double local_solar : {0.0, 6.0, 12.0, 18.0}) {
                const double longitude = longitude_for_local_solar_time(centre_epoch, local_solar);
                const auto centre_day =
                    leo::SpiceEphemerisProvider::utc_calendar(centre_epoch).days_since_1970;
                for (double separation : time_separations_s()) {
                    const double centre_density =
                        probe_density(probe, Probe{centre_epoch, 0.0, longitude, s.h_km});
                    const double centre = std::log(centre_density);
                    // One symmetric pair per separation. The forward and backward
                    // one-sided deltas are reported separately, because they are
                    // different quantities and a symmetric pair alone would hide
                    // the asymmetry that matters near a weather boundary.
                    const double forward_density =
                        probe_density(probe, Probe{leo::Epoch{centre_epoch.et_seconds + separation},
                                                  0.0, longitude, s.h_km});
                    const double backward_density =
                        probe_density(probe, Probe{leo::Epoch{centre_epoch.et_seconds - separation},
                                                  0.0, longitude, s.h_km});
                    const double forward = std::log(forward_density);
                    const double backward = std::log(backward_density);
                    const double span = std::abs(forward - backward);
                    const double asymmetry = std::abs(forward - centre) - std::abs(backward - centre);
                    const auto forward_day =
                        leo::SpiceEphemerisProvider::utc_calendar(leo::Epoch{centre_epoch.et_seconds + separation})
                            .days_since_1970;
                    const auto backward_day =
                        leo::SpiceEphemerisProvider::utc_calendar(leo::Epoch{centre_epoch.et_seconds - separation})
                            .days_since_1970;
                    histogram.add(span);
                    worst_asymmetry = std::max(worst_asymmetry, std::abs(asymmetry));
                    if (span > worst_span) {
                        worst_span = span;
                        worst_step = separation;
                        worst_lst = local_solar;
                        worst_utc = utc_label(centre_epoch);
                        worst_weather = weather.name;
                    }
                    Json row;
                    row.add("weather", weather.name);
                    row.add("utc", utc_label(centre_epoch));
                    row.add("anchor_scope", anchor_offset == 0.0 ? "midnight-boundary" : "civil-day-interior");
                    const bool same_forward_inputs = centre_identity == weather_identity(
                        c, env, s.model_kind, leo::Epoch{centre_epoch.et_seconds + separation});
                    const bool same_backward_inputs = centre_identity == weather_identity(
                        c, env, s.model_kind, leo::Epoch{centre_epoch.et_seconds - separation});
                    row.add("forward_matches_discrete_inputs", same_forward_inputs);
                    row.add("backward_matches_discrete_inputs", same_backward_inputs);
                    row.add("finite_difference_scope",
                            same_forward_inputs && same_backward_inputs
                                ? "identical-discrete-inputs; continuous-provider-response"
                                : "discrete-input-transition; not a smooth derivative");
                    row.add("separation_s", separation);
                    row.add("local_solar_time_hours", local_solar);
                    row.add("reference_density_kg_m3", centre_density);
                    row.add("reference_log_density", centre);
                    row.add("forward_density_kg_m3", forward_density);
                    row.add("backward_density_kg_m3", backward_density);
                    row.add("signed_forward_log_difference", forward - centre);
                    row.add("signed_backward_log_difference", backward - centre);
                    row.add("signed_forward_relative", (forward_density - centre_density) / centre_density);
                    row.add("signed_backward_relative", (backward_density - centre_density) / centre_density);
                    row.add("signed_absolute_forward_kg_m3", forward_density - centre_density);
                    row.add("signed_absolute_backward_kg_m3", backward_density - centre_density);
                    row.add("log_ratio_span", span);
                    row.add("ratio_span", std::expm1(span));
                    row.add("one_sided_asymmetry_log", asymmetry);
                    row.add("first_derivative_log_per_s", (forward - backward) / (2.0 * separation));
                    row.add("second_order_residual_log_density",
                            std::abs(forward - 2.0 * centre + backward));
                    row.add("forward_crosses_civil_day", forward_day != centre_day);
                    row.add("backward_crosses_civil_day", backward_day != centre_day);
                    rows.push_back(row.str());
                }
            }
          }
        result.raw("time_difference", raw_array(rows));
        result.add("time_separations_s", numbers(time_separations_s()));
        result.raw("time_difference_histogram", histogram.json());
        Json worst_row;
        worst_row.add("log_ratio_span", worst_span);
        worst_row.add("ratio_span", std::expm1(worst_span));
        worst_row.add("separation_s", worst_step);
        worst_row.add("local_solar_time_hours", worst_lst);
        worst_row.add("weather", worst_weather);
        worst_row.add("utc", worst_utc);
        result.add("largest_observed_log_density_span_over_time", worst_row.str());
        result.add("largest_one_sided_asymmetry_log", worst_asymmetry);
        result.add("backward_note",
                   "every separation is measured as a symmetric pair and its forward and backward "
                   "one-sided deltas are reported separately, so an asymmetric response near a "
                   "weather boundary cannot hide behind the symmetric span");
        result.add("civil_day_check",
                   "each row records whether the forward and backward samples leave the civil day of "
                   "the anchor, compared as integer day indices rather than as formatted stamps");
        result.add("integrator_relevance",
                   "separations of 1 ms to 1 s are the adaptive integrator's own stage spacing, so a "
                   "time axis that is already non-linear at that scale cannot be interpolated with a "
                   "coarse uniform cell");
    }

    // ---- 5. discrete-input boundaries ----
    {
        Json boundaries;
        boundaries.add("question",
                       "which changes in density are genuine physical sensitivity and which are jumps "
                       "caused by discrete space-weather inputs?");
        std::vector<std::string> rows;
        std::size_t covered = 0, uncovered = 0;
        for (const auto& weather : selected) {
            int year = 0, month = 0, day = 0;
            civil_date(weather.epoch, year, month, day);
            struct Pair { std::string transition, before, after; };
            const std::vector<Pair> pairs{
                {"three_hourly_ap_bin", utc_string(year, month, day, 2, 59, 59),
                 utc_string(year, month, day, 3, 0, 0)},
                {"three_hourly_ap_bin", utc_string(year, month, day, 5, 59, 59),
                 utc_string(year, month, day, 6, 0, 0)},
                {"three_hourly_ap_bin", utc_string(year, month, day, 11, 59, 59),
                 utc_string(year, month, day, 12, 0, 0)},
                {"hourly_dtc_record", utc_string(year, month, day, 10, 59, 59),
                 utc_string(year, month, day, 11, 0, 0)},
                {"utc_midnight_and_daily_record", utc_string(year, month, day, 23, 59, 59),
                 next_day_utc(year, month, day, 0, 0, 0)},
                {"utc_midnight_and_daily_record", utc_string(year, month, day, 2, 59, 59),
                 next_day_utc(year, month, day, 2, 59, 59)},
            };
            for (const auto& pair : pairs) {
                Json row;
                row.add("transition", pair.transition);
                row.add("before_utc", pair.before);
                row.add("after_utc", pair.after);
                WeatherRecord before_record, after_record;
                try {
                    const leo::Epoch before = leo::SpiceEphemerisProvider::epoch_from_utc(pair.before);
                    const leo::Epoch after = leo::SpiceEphemerisProvider::epoch_from_utc(pair.after);
                    before_record = read_weather(c, env, before, c.ap_history);
                    after_record = read_weather(c, env, after, c.ap_history);
                    row.add("separation_s", after.et_seconds - before.et_seconds);
                    row.raw("before_inputs", before_record.json());
                    row.raw("after_inputs", after_record.json());
                    if (!before_record.available || !after_record.available) {
                        row.add("covered_by_pinned_records", false);
                        ++uncovered;
                        rows.push_back(row.str());
                        continue;
                    }
                    row.add("covered_by_pinned_records", true);
                    row.add("space_weather_vector_changed",
                            before_record.ap != after_record.ap ||
                                before_record.f107 != after_record.f107 ||
                                before_record.f107_average != after_record.f107_average ||
                                before_record.dstdtc_k != after_record.dstdtc_k ||
                                before_record.ap_history != after_record.ap_history ||
                                // JB2008's own drivers are the eight SOLFSMY lag
                                // proxies. Comparing only the NRLMSISE indices
                                // and DSTDTC would report "unchanged" across an
                                // hourly solar transition that does change the
                                // density.
                                before_record.f10 != after_record.f10 ||
                                before_record.f10_average != after_record.f10_average ||
                                before_record.s10 != after_record.s10 ||
                                before_record.s10_average != after_record.s10_average ||
                                before_record.m10 != after_record.m10 ||
                                before_record.m10_average != after_record.m10_average ||
                                before_record.y10 != after_record.y10 ||
                                before_record.y10_average != after_record.y10_average);
                    for (double altitude : {400.0, 600.0, 900.0}) {
                        const double a = probe_density(probe, Probe{before, 0.0,
                                                                     longitude_for_local_solar_time(before, 12.0),
                                                                     altitude});
                        const double b = probe_density(probe, Probe{after, 0.0,
                                                                     longitude_for_local_solar_time(after, 12.0),
                                                                     altitude});
                        row.add("jump_log_density_at_" + std::to_string(static_cast<int>(altitude)) + "km",
                                std::log(b) - std::log(a));
                    }
                    ++covered;
                    rows.push_back(row.str());
                } catch (const std::exception& error) {
                    row.add("covered_by_pinned_records", false);
                    row.add("unavailable_reason", std::string(error.what()));
                    ++uncovered;
                    rows.push_back(row.str());
                }
            }
        }
        // The integer DSTDTC transitions the dataset actually contains, found
        // rather than assumed: walk one day of the pinned record hour by hour
        // and report where the truncated interpolation steps.
        {
            int year = 0, month = 0, day = 0;
            civil_date(selected.front().epoch, year, month, day);
            std::vector<std::string> dtc_rows;
            double previous = 0;
            bool first = true;
            for (int hour = 0; hour < 24; ++hour) {
                const WeatherRecord record = read_weather(
                    c, env, leo::SpiceEphemerisProvider::epoch_from_utc(utc_string(year, month, day, hour)),
                    c.ap_history);
                if (!record.available) continue;
                if (!first && record.dstdtc_k != previous) {
                    Json row;
                    row.add("transition", "integer_dstdtc_within_day");
                    row.add("utc", record.utc);
                    row.add("dstdtc_k_from", previous);
                    row.add("dstdtc_k_to", record.dstdtc_k);
                    row.add("inside_an_hour", true);
                    dtc_rows.push_back(row.str());
                }
                previous = record.dstdtc_k;
                first = false;
            }
            boundaries.raw("observed_dstdtc_transitions", raw_array(dtc_rows));
            boundaries.add("dstdtc_note",
                            "DSTDTC is an integer after truncating an hourly interpolation, so it "
                            "changes inside an hour and not only on the hour. Its bit pattern is "
                            "carried in every cache key, so no cell or interpolation can span one of "
                            "these transitions");
        }
        // The leap-second transition the pinned kernels cover.
        {
            Json row;
            row.add("transition", "leap_second_boundary");
            row.add("before_utc", leap_boundary_before_utc());
            row.add("after_utc", leap_boundary_after_utc());
            try {
                const leo::Epoch before =
                    leo::SpiceEphemerisProvider::epoch_from_utc(leap_boundary_before_utc());
                const leo::Epoch after =
                    leo::SpiceEphemerisProvider::epoch_from_utc(leap_boundary_after_utc());
                row.add("separation_s", after.et_seconds - before.et_seconds);
                row.raw("before_inputs", read_weather(c, env, before, c.ap_history).json());
                row.raw("after_inputs", read_weather(c, env, after, c.ap_history).json());
                const double longitude = longitude_for_local_solar_time(before, 12.0);
                row.add("jump_log_density_at_600km",
                        std::log(probe_density(probe, Probe{after, 0.0, longitude, 600.0})) -
                            std::log(probe_density(probe, Probe{before, 0.0, longitude, 600.0})));
                row.add("covered", true);
                ++covered;
            } catch (const std::exception& error) {
                row.add("covered", false);
                row.add("unavailable_reason", std::string(error.what()));
                ++uncovered;
            }
            boundaries.raw("leap_second_boundary", row.str());
        }
        boundaries.raw("transitions", raw_array(rows));
        boundaries.add("transitions_covered_by_pinned_records", covered);
        boundaries.add("transitions_not_covered", uncovered);
        boundaries.add("guard_consequence",
                       "every one of these transitions changes the discrete space-weather vector or the "
                       "civil day, both of which are carried bitwise in every key. A candidate therefore "
                       "cannot reuse or interpolate across any of them: a mismatch is a miss, never a "
                       "value");
        boundaries.add("uncovered_policy",
                       "a transition the pinned records do not cover is reported as uncovered and left "
                       "unmeasured; it is never bridged with an assumed or extrapolated value");
        result.raw("discrete_boundary_structure", boundaries.str());
    }

    // ---- 6. Ap-history mode as a separate experiment ----
    if (s.model_kind == leo::atmosphere::Model::nrlmsise00 && compiled_in(leo::atmosphere::Model::nrlmsise00) &&
        !c.ap_history) {
        NativeProbe history_probe(c, env, s.model_kind, true);
        std::vector<std::string> rows;
        for (const auto& weather : selected)
            for (double altitude : {400.0, 600.0, 900.0}) {
                const Probe point{weather.epoch, 0.0,
                                  longitude_for_local_solar_time(weather.epoch, 12.0), altitude};
                const double daily = probe_density(probe, point);
                const double history_value = history_probe.density(point.epoch, point.latitude_deg,
                                                                    point.longitude_deg, point.altitude_km);
                Json row;
                row.add("weather", weather.name);
                row.add("utc", weather.utc);
                row.add("altitude_km", altitude);
                row.add("density_daily_ap_kg_m3", daily);
                row.add("density_ap_history_kg_m3", history_value);
                row.add("relative_difference", (history_value - daily) / daily);
                row.add("log_density_difference", std::log(history_value) - std::log(daily));
                rows.push_back(row.str());
            }
        Json ap_history;
        ap_history.add("separateness",
                       "Ap-history mode is a different model configuration and is reported as its own "
                       "experiment; it is never blended with the daily-Ap reference");
        ap_history.add("switch_difference", "switch 9 is -1 (seven 3-hourly values) instead of 1");
        ap_history.add("record_status",
                       "both configurations read the same pinned records; no index is synthesised");
        ap_history.raw("comparison", raw_array(rows));
        result.raw("ap_history_experiment", ap_history.str());
    }

    result.add("invalid_density_policy",
               "every reference value must be finite and positive; a violation aborts the sweep rather "
               "than being skipped silently");
    result.add("interpretation_limit",
               "these are single-point local sensitivities of the model's own output. They bound what an "
               "interpolant must reproduce; they are not themselves an interpolation error estimate, and "
               "the accuracy study measures that separately");
    result.add("reported_not_averaged",
               "the largest observed variation is reported alongside the histograms so a large local "
               "error can never be averaged away");
    return result.str();
}

} // namespace cache_study