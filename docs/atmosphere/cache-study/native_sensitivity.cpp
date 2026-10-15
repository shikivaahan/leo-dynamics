// ATM-CACHE-001 partial native-input sensitivity.
//
// Reachable with the pinned CSSI archive and the LSK alone: NRLMSISE-00 is
// queried through the production adapter at explicit Earth-fixed geodetic
// coordinates and absolute UTC instants. The input contract is the same one
// DirectAtmosphere builds, field for field; no Earth-orientation kernel is
// needed because no inertial state is ever formed.
//
// This is a *partial* slice. It is not a provider measurement, not a
// preprocessing measurement, not a trajectory result and not an end-to-end
// number, and it is never compared against a direct-v2 baseline. Its purpose is
// to keep the real primary scientific investigation moving for NRLMSISE-00
// while the orientation kernel is unavailable.
#include "support.hpp"

namespace cache_study {

#ifndef LEO_HAS_NRLMSISE00
std::string native_geodetic_sensitivity_study(const Config&, Environment&, const StudySettings&) {
    throw std::runtime_error(
        "--study-native-geodetic needs the NRLMSISE-00 backend, which is not compiled into this build");
}
#else

namespace {

struct GeodeticProbe {
    const NativeGeodeticProbe& probe;
    double density(leo::Epoch epoch, double latitude_deg, double longitude_deg, double altitude_km) const {
        const double value = probe.density(epoch, latitude_deg, longitude_deg, altitude_km);
        if (!std::isfinite(value) || value <= 0)
            throw std::runtime_error("reference density is not finite and positive at " + utc_label(epoch));
        return value;
    }
};

} // namespace

std::string native_geodetic_sensitivity_study(const Config& c, Environment& env, const StudySettings& s) {
    if (s.model_kind != leo::atmosphere::Model::nrlmsise00)
        throw std::invalid_argument(
            "--study-native-geodetic applies to NRLMSISE-00 only: its native input is a geodetic "
            "state plus a UTC calendar, while JB2008 additionally needs a Sun direction that can "
            "only be obtained from the orientation-capable ephemeris path");
    const NativeGeodeticProbe probe(env, c.ap_history);
    const GeodeticProbe reference{probe};

    // Only the CSSI record is needed here: JB2008's solar and DSTDTC records are not
    // loaded at all in this partial slice, and nothing is substituted for them.
    std::vector<WeatherEpoch> pool = build_epoch_pool(c, env, s, true, false);
    const std::vector<WeatherEpoch> selected = select_regimes(c, env, pool);

    Json result;
    result.add("section", "partial native-input density sensitivity, NRLMSISE-00");
    result.add("scope",
               "partial: the native model is exercised at its own input contract, but no provider, "
               "no Earth-orientation preprocessing, no trajectory and no end-to-end quantity is "
               "measured here. These numbers are not comparable with a direct-v2 baseline.");
    result.add("input_contract",
               "production leo::atmosphere::Nrlmsise00 adapter, production WeatherDataset record, "
               "production UTC calendar conversion from the LSK, production switch vector");
    result.add("required_inputs", strings({"leapseconds kernel (UTC calendar)", "pinned CSSI weather archive"}));
    result.add("not_required_inputs",
               strings({"planetary ephemeris", "Earth orientation kernel", "JB2008 solar and DSTDTC records"}));
    result.add("ap_history_mode", c.ap_history);
    result.raw("switch_vector", [&] {
        std::vector<double> values;
        for (int value : probe.switches()) values.push_back(double(value));
        return numbers(values);
    }());

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
        result.add("weather_policy",
                   "every index is read from the pinned CSSI record and published; nothing is "
                   "injected, scaled or hand-edited");
        result.add("weather_selection_rule",
                   "the caller-supplied pinned fixture epochs are the primary regimes and are never "
                   "replaced by a pick from the scan");
    }

    // ---- altitude profiles and altitude finite differences ----
    {
        std::vector<std::string> profiles, differences;
        ErrorHistogram histogram;
        double worst = 0, worst_altitude = 0, worst_step = 0, worst_lst = 0;
        std::vector<double> gradient(altitude_grid_km().size(), 0.0);
        std::vector<double> curvature(altitude_grid_km().size(), 0.0);
        std::vector<double> heat(altitude_grid_km().size() * local_solar_time_grid_hours().size(), 0.0);

        for (const auto& weather : selected) {
            for (double latitude : {-80.0, -45.0, 0.0, 45.0, 80.0})
            for (double local_solar : local_solar_time_grid_hours()) {
                const double longitude = longitude_for_local_solar_time(weather.epoch, local_solar);
                const std::size_t column = static_cast<std::size_t>(
                    std::find(local_solar_time_grid_hours().begin(), local_solar_time_grid_hours().end(),
                              local_solar) - local_solar_time_grid_hours().begin());
                std::vector<double> log_density;
                for (double altitude : altitude_grid_km()) {
                    if (altitude < s.altitude_min_km || altitude > s.altitude_max_km) continue;
                    log_density.push_back(std::log(
                        reference.density(weather.epoch, latitude, longitude, altitude)));
                }
                Json profile;
                profile.add("weather", weather.name);
                profile.add("utc", weather.utc);
                profile.add("latitude_deg", latitude);
                profile.add("longitude_deg", longitude);
                profile.add("local_solar_time_hours", local_solar);
                profile.raw("altitude_km", numbers(altitude_grid_km()));
                profile.raw("log_density", numbers(log_density));
                profiles.push_back(profile.str());

                for (double step : altitude_perturbations_km())
                    for (std::size_t index = 0; index < altitude_grid_km().size(); ++index) {
                        const double altitude = altitude_grid_km()[index];
                        if (altitude - step < s.altitude_min_km || altitude + step > s.altitude_max_km)
                            continue;
                        const double up = std::log(
                            reference.density(weather.epoch, latitude, longitude, altitude + step));
                        const double centre = std::log(
                            reference.density(weather.epoch, latitude, longitude, altitude));
                        const double down = std::log(
                            reference.density(weather.epoch, latitude, longitude, altitude - step));
                        const double first = (up - down) / (2.0 * step);
                        const double second = (up - 2.0 * centre + down) / (step * step);
                        const double span = std::abs(up - down);
                        gradient[index] = std::max(gradient[index], std::abs(first));
                        curvature[index] = std::max(curvature[index], std::abs(second));
                        heat[index * local_solar_time_grid_hours().size() + column] =
                            std::max(heat[index * local_solar_time_grid_hours().size() + column],
                                     std::abs(first));
                        histogram.add(span);
                        if (span > worst) {
                            worst = span;
                            worst_altitude = altitude;
                            worst_step = step;
                            worst_lst = local_solar;
                        }
                        Json row;
                        row.add("weather", weather.name);
                        row.add("latitude_deg", latitude);
                        row.add("reference_density_kg_m3", std::exp(centre));
                        row.add("upper_signed_absolute_change_kg_m3", std::exp(up) - std::exp(centre));
                        row.add("upper_signed_relative_change", std::expm1(up - centre));
                        row.add("upper_log_density_change", up - centre);
                        row.add("midpoint_interpolation_log_residual", 0.5 * (up + down) - centre);
                        row.add("local_solar_time_hours", local_solar);
                        row.add("altitude_km", altitude);
                        row.add("step_km", step);
                        row.add("dlog_density_daltitude_per_km", first);
                        row.add("second_derivative_per_km2", second);
                        row.add("span_log_density", span);
                        row.add("max_over_min_relative_span", std::expm1(span));
                        differences.push_back(row.str());
                    }
            }
        }
        result.raw("altitude_profile", raw_array(profiles));
        result.add("altitude_perturbations_km", numbers(altitude_perturbations_km()));
        result.raw("altitude_difference", raw_array(differences));
        result.raw("max_abs_dlog_rho_daltitude_by_altitude", numbers(gradient));
        result.raw("max_abs_d2log_rho_daltitude2_by_altitude", numbers(curvature));
        result.raw("altitude_difference_histogram", histogram.json());
        Json worst_row;
        worst_row.add("span_log_density", worst);
        worst_row.add("altitude_km", worst_altitude);
        worst_row.add("step_km", worst_step);
        worst_row.add("local_solar_time_hours", worst_lst);
        result.add("largest_observed_log_density_span_in_altitude", worst_row.str());
        result.raw("altitude_by_local_solar_time_heatmap", [&] {
            Json heatmap;
            heatmap.add("quantity", "max |d ln rho / dh| per km over tested weather, latitudes and perturbation scales");
            heatmap.raw("rows_altitude_km", numbers(altitude_grid_km()));
            heatmap.raw("columns_local_solar_time_hours", numbers(local_solar_time_grid_hours()));
            std::vector<std::string> matrix;
            for (std::size_t index = 0; index < altitude_grid_km().size(); ++index)
                matrix.push_back(numbers(std::vector<double>(
                    heat.begin() + long(index * local_solar_time_grid_hours().size()),
                    heat.begin() + long((index + 1) * local_solar_time_grid_hours().size()))));
            heatmap.raw("values", raw_array(matrix));
            return heatmap.str();
        }());
    }

    // ---- latitude, longitude, wrap and near-pole behaviour ----
    {
        std::vector<std::string> latitude_rows, longitude_rows, differences;
        ErrorHistogram histogram;
        double worst = 0, worst_step = 0;
        for (const auto& weather : selected) {
            const double longitude = longitude_for_local_solar_time(weather.epoch, 12.0);
            std::vector<double> latitude_log, longitude_log;
            for (double latitude : latitude_grid_deg())
                latitude_log.push_back(
                    std::log(reference.density(weather.epoch, latitude, longitude, s.h_km)));
            for (double sample : longitude_grid_deg())
                longitude_log.push_back(
                    std::log(reference.density(weather.epoch, 0.0, sample, s.h_km)));
            Json latitude_row;
            latitude_row.add("weather", weather.name);
            latitude_row.add("utc", weather.utc);
            latitude_row.add("altitude_km", s.h_km);
            latitude_row.add("longitude_deg", longitude);
            latitude_row.raw("latitude_deg", numbers(latitude_grid_deg()));
            latitude_row.raw("log_density", numbers(latitude_log));
            latitude_row.add("note", "the extreme latitudes sit on the study's polar fallback limit");
            latitude_rows.push_back(latitude_row.str());
            Json longitude_row;
            longitude_row.add("weather", weather.name);
            longitude_row.add("utc", weather.utc);
            longitude_row.add("latitude_deg", 0.0);
            longitude_row.raw("longitude_deg", numbers(longitude_grid_deg()));
            longitude_row.raw("log_density", numbers(longitude_log));
            longitude_rows.push_back(longitude_row.str());

            for (std::size_t index = 1; index + 1 < latitude_grid_deg().size(); ++index)
                for (double step : {0.1, 1.0, 5.0, 10.0}) {
                    const double latitude = latitude_grid_deg()[index];
                    if (std::abs(latitude + step) > domain_polar_fallback_deg ||
                        std::abs(latitude - step) > domain_polar_fallback_deg) continue;
                    const double up = std::log(
                        reference.density(weather.epoch, latitude + step, longitude, s.h_km));
                    const double down = std::log(
                        reference.density(weather.epoch, latitude - step, longitude, s.h_km));
                    const double span = std::abs(up - down);
                    histogram.add(span);
                    Json row;
                    row.add("latitude_deg", latitude);
                    row.add("step_deg", step);
                    row.add("dlog_density_dlatitude_per_deg", (up - down) / (2.0 * step));
                    row.add("span_log_density", span);
                    row.add("relative_difference", std::expm1(span));
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

        std::vector<double> seam, interior;
        for (const auto& weather : selected)
            for (double altitude : {400.0, 600.0, 900.0}) {
                seam.push_back(std::log(reference.density(weather.epoch, 0.0, 179.9999, altitude)) -
                               std::log(reference.density(weather.epoch, 0.0, -179.9999, altitude)));
                interior.push_back(std::log(reference.density(weather.epoch, 0.0, 0.0001, altitude)) -
                                   std::log(reference.density(weather.epoch, 0.0, -0.0001, altitude)));
            }
        Json wrap;
        wrap.add("span_log_across_seam", numbers(seam));
        wrap.add("span_log_away_from_seam", numbers(interior));
        wrap.add("max_span_log_across_seam", seam.empty() ? 0.0 : *std::max_element(seam.begin(), seam.end()));
        wrap.add("max_span_log_away_from_seam",
                 interior.empty() ? 0.0 : *std::max_element(interior.begin(), interior.end()));
        wrap.add("reading",
                 "the seam behaves like any other longitude step, so the discontinuity a geographic "
                 "quantisation would introduce there is a property of the cache cells, not of the model");
        result.raw("longitude_wrap_check", wrap.str());
    }

    // ---- local solar time ----
    {
        std::vector<std::string> rows;
        for (const auto& weather : selected)
            for (double altitude : {400.0, 600.0, 900.0}) {
                std::vector<double> log_density;
                for (double local_solar : local_solar_time_grid_hours())
                    log_density.push_back(std::log(reference.density(
                        weather.epoch, 0.0, longitude_for_local_solar_time(weather.epoch, local_solar),
                        altitude)));
                Json row;
                row.add("weather", weather.name);
                row.add("utc", weather.utc);
                row.add("altitude_km", altitude);
                row.add("local_solar_time_grid_hours", numbers(local_solar_time_grid_hours()));
                row.add("labels", strings({"midnight", "dawn", "morning", "noon", "afternoon", "dusk", "evening"}));
                row.raw("log_density", numbers(log_density));
                row.add("log_day_night_ratio_noon_over_midnight", log_density[3] - log_density[0]);
                rows.push_back(row.str());
            }
        result.raw("local_solar_time_profile", raw_array(rows));
    }

    // ---- time separations, forwards and backwards ----
    {
        std::vector<std::string> rows;
        ErrorHistogram histogram;
        double worst = 0, worst_step = 0;
        for (const auto& weather : selected) {
            const leo::Epoch centre_epoch{weather.epoch.et_seconds + 48600.0};
            for (double local_solar : {0.0, 6.0, 12.0, 18.0}) {
                const double longitude = longitude_for_local_solar_time(centre_epoch, local_solar);
                for (double separation : time_separations_s())
                    for (int direction : {1, -1}) {
                        if (direction < 0 && separation > 600.0) continue;
                        const double centre =
                            std::log(reference.density(centre_epoch, 0.0, longitude, s.h_km));
                        const double ahead = std::log(reference.density(
                            leo::Epoch{centre_epoch.et_seconds + direction * separation}, 0.0, longitude,
                            s.h_km));
                        const double behind = std::log(reference.density(
                            leo::Epoch{centre_epoch.et_seconds - direction * separation}, 0.0, longitude,
                            s.h_km));
                        const double span = std::abs(ahead - behind);
                        histogram.add(span);
                        Json row;
                        row.add("weather", weather.name);
                        row.add("reference_utc", utc_label(centre_epoch));
                        row.add("separation_s", separation);
                        row.add("direction", double(direction));
                        row.add("local_solar_time_hours", local_solar);
                        row.add("span_log_density", span);
                        row.add("reference_density_kg_m3", std::exp(centre));
                        row.add("signed_relative_change", std::expm1(ahead - centre));
                        row.add("signed_absolute_change_kg_m3", std::exp(ahead) - std::exp(centre));
                        row.add("signed_log_density_change", ahead - centre);
                        row.add("dlog_density_dt_per_s", (ahead - behind) / (2.0 * direction * separation));
                        row.add("weather_vector_changed", weather_identity(c, env, s.model_kind,
                            leo::Epoch{centre_epoch.et_seconds + direction * separation}) !=
                            weather_identity(c, env, s.model_kind,
                            leo::Epoch{centre_epoch.et_seconds - direction * separation}));
                        row.add("second_order_residual_log_density", std::abs(ahead - 2.0 * centre + behind));
                        row.add("max_over_min_relative_span", std::expm1(span));
                        row.add("crosses_civil_day",
                                leo::SpiceEphemerisProvider::utc_calendar(
                                    leo::Epoch{centre_epoch.et_seconds + direction * separation}).days_since_1970 !=
                                    leo::SpiceEphemerisProvider::utc_calendar(centre_epoch).days_since_1970 ||
                                leo::SpiceEphemerisProvider::utc_calendar(
                                    leo::Epoch{centre_epoch.et_seconds - direction * separation}).days_since_1970 !=
                                    leo::SpiceEphemerisProvider::utc_calendar(centre_epoch).days_since_1970);
                        rows.push_back(row.str());
                        if (span > worst) { worst = span; worst_step = separation; }
                    }
            }
        }
        result.raw("time_difference", raw_array(rows));
        result.add("time_separations_s", numbers(time_separations_s()));
        result.raw("time_difference_histogram", histogram.json());
        result.add("largest_observed_log_density_span_over_time", worst);
        result.add("largest_time_span_separation_s", worst_step);
        result.add("backward_note",
                   "every separation up to 600 s is applied backwards as well as forwards; no "
                   "candidate or interpolant may assume monotone time");
    }

    // ---- discrete space-weather boundaries ----
    {
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
                try {
                    const leo::Epoch before = leo::SpiceEphemerisProvider::epoch_from_utc(pair.before);
                    const leo::Epoch after = leo::SpiceEphemerisProvider::epoch_from_utc(pair.after);
                    const WeatherRecord before_record = read_weather(c, env, before, c.ap_history, true, false);
                    const WeatherRecord after_record = read_weather(c, env, after, c.ap_history, true, false);
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
                            weather_identity(c, env, s.model_kind, before) !=
                                weather_identity(c, env, s.model_kind, after));
                    for (double altitude : {400.0, 600.0, 900.0})
                        row.add("jump_log_density_at_" + std::to_string(static_cast<int>(altitude)) + "km",
                                std::log(reference.density(after, 0.0, 0.0, altitude)) -
                                    std::log(reference.density(before, 0.0, 0.0, altitude)));
                    ++covered;
                } catch (const std::exception& error) {
                    row.add("covered_by_pinned_records", false);
                    row.add("unavailable_reason", std::string(error.what()));
                    ++uncovered;
                }
                rows.push_back(row.str());
            }
        }
        Json boundaries;
        boundaries.raw("transitions", raw_array(rows));
        boundaries.add("transitions_covered_by_pinned_records", covered);
        boundaries.add("transitions_not_covered", uncovered);
        boundaries.add("guard_consequence",
                       "reuse is refused when the actual weather vector or civil day changes; "
                       "a three-hour boundary need not change daily-Ap inputs");
        boundaries.add("not_measured_here",
                       "the integer DSTDTC transitions belong to the JB2008 record, which this partial "
                       "slice does not load; they are measured by the full study when its inputs are "
                       "available");
        result.raw("discrete_boundary_structure", boundaries.str());
    }
    {
        std::vector<std::string> rows;
        double previous_density = 0;
        for (const char* utc : {"2016-12-31T23:59:59", "2016-12-31T23:59:60", "2017-01-01T00:00:00"}) {
            const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(utc);
            const auto calendar = leo::SpiceEphemerisProvider::utc_calendar(epoch);
            const double density = reference.density(epoch, 60.0, -70.0, 600.0);
            Json row;
            row.add("utc", utc);
            row.add("seconds_of_day", calendar.seconds_of_day);
            row.add("day_length_seconds", calendar.day_length_seconds);
            row.add("density_kg_m3", density);
            row.raw("weather_inputs", read_weather(c, env, epoch, c.ap_history, true, false).json());
            if (previous_density > 0) row.add("signed_relative_change_from_previous", density / previous_density - 1);
            previous_density = density;
            rows.push_back(row.str());
        }
        result.raw("leap_second_probe", raw_array(rows));
    }
    // Independent one-dimensional altitude holdouts: endpoint values construct
    // the interpolant; none of the queried fractions is a construction point.
    {
        std::vector<std::string> rows;
        Rng random(Rng::derive(s.seed, 0x414c54484f4c44ULL));
        for (double width : {1.0, 5.0, 10.0, 25.0, 50.0, 100.0}) {
            std::vector<double> relative, logs;
            std::vector<std::string> strata;
            double worst = 0;
            Json worst_point;
            for (const auto& weather : selected) {
                std::vector<double> weather_relative, weather_logs;
                for (double latitude : {-80.0, -45.0, 0.0, 45.0, 80.0})
                    for (double local_solar : {0.0, 6.0, 12.0, 18.0}) {
                        const double longitude = longitude_for_local_solar_time(weather.epoch, local_solar);
                        // The cell boundaries are integer multiples of the width
                        // from a fixed origin. Repeatedly adding a floating-point
                        // width to a running lower bound drifts, and a drifted
                        // boundary would quietly compare two different cells
                        // against each other.
                        const std::size_t cells =
                            static_cast<std::size_t>(std::floor((1000.0 - 200.0) / width));
                        for (std::size_t cell = 0; cell < cells; ++cell) {
                            const double lower = 200.0 + width * double(cell);
                            const double upper = lower + width;
                            const double low = reference.density(weather.epoch, latitude, longitude, lower);
                            const double high = reference.density(weather.epoch, latitude, longitude, upper);
                            for (int holdout = 0; holdout < 3; ++holdout) {
                                const double fraction = random.uniform(0.1, 0.9);
                                const double altitude = lower + fraction * width;
                                const double fresh = reference.density(weather.epoch, latitude, longitude, altitude);
                                const double reconstructed = std::exp(
                                    (1.0 - fraction) * std::log(low) + fraction * std::log(high));
                                const double error = (reconstructed - fresh) / fresh;
                                const double log_error = std::log(reconstructed / fresh);
                                relative.push_back(error);
                                logs.push_back(log_error);
                                weather_relative.push_back(error);
                                weather_logs.push_back(log_error);
                                if (std::abs(error) > worst) {
                                    worst = std::abs(error);
                                    Json point;
                                    point.add("weather", weather.name);
                                    point.add("latitude_deg", latitude);
                                    point.add("local_solar_time_hours", local_solar);
                                    point.add("altitude_km", altitude);
                                    point.add("reference_density_kg_m3", fresh);
                                    point.add("reconstructed_density_kg_m3", reconstructed);
                                    point.add("signed_absolute_error_kg_m3", reconstructed - fresh);
                                    point.add("signed_relative_error", error);
                                    point.add("log_density_error", log_error);
                                    worst_point = std::move(point);
                                }
                            }
                        }
                    }
                Json stratum;
                stratum.add("weather", weather.name);
                stratum.raw("error_metrics", error_metrics(weather_relative, weather_logs).json());
                strata.push_back(stratum.str());
            }
            const auto metrics = error_metrics(relative, logs);
            Json row;
            row.add("cell_width_km", width);
            row.raw("error_metrics", metrics.json());
            row.raw("threshold_pass_by_maximum", threshold_row(metrics, study_error_targets()));
            row.raw("weather_strata", raw_array(strata));
            row.raw("worst_point", [&] {
                if (worst != 0.0) return worst_point.str();
                Json empty;
                empty.add("none", true);
                empty.add("reason", "no sampled holdout point produced a nonzero reconstruction "
                                    "error at this cell width");
                return empty.str();
            }());
            rows.push_back(row.str());
        }
        result.raw("altitude_log_density_interpolation_holdouts", raw_array(rows));
        result.add("altitude_holdout_scope",
            "one-dimensional altitude only, identical UTC/latitude/longitude/weather at nodes and "
            "holdout; no nearest-neighbour error on other axes, no cache reuse or speedup claim");
    }

    result.add("invalid_density_policy",
               "every reference value must be finite and positive; a violation aborts the sweep");
    result.add("interpretation_limit",
               "these are local sensitivities of the native model's own output. They bound what an "
               "interpolant must reproduce and are not themselves an interpolation error estimate");
    return result.str();
}
#endif

} // namespace cache_study