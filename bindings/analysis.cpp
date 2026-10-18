#include "common.hpp"
#include <pybind11/stl/filesystem.h>
#include "leo/analysis/export.hpp"
#include <array>
#include <chrono>
#include <ios>
#include <stdexcept>
#include <cstring>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
namespace leo::python {
namespace {
constexpr std::size_t state_width = 13;
py::array readonly_array(py::array array);  // defined below; marks results non-writeable
// Recarray column: name, subarray width, logical flag and byte offset.
struct Field { const char* name; std::size_t width; bool boolean; std::size_t offset; };
// Packs native samples into a byte-exact NumPy structured array. Fields are
// eight-byte float64 scalars/subarrays or one-byte bools, packed with the
// offsets declared here, so no interpreter-side alignment surprises exist.
struct Table {
    std::vector<Field> fields;
    std::size_t itemsize = 0;
    std::size_t rows = 0;
    std::vector<char> buffer;
    std::size_t add(const char* name, std::size_t width = 1, bool boolean = false) {
        fields.push_back({name, width, boolean, itemsize});
        itemsize += boolean ? 1 : sizeof(double) * width;
        return fields.size() - 1;
    }
    void allocate(std::size_t count) {
        rows = count;
        buffer.assign(count * itemsize, char{0});
    }
    void set(std::size_t row, std::size_t field, double value) {
        std::memcpy(buffer.data() + row * itemsize + fields[field].offset, &value, sizeof(double));
    }
    template<class V> void set3(std::size_t row, std::size_t field, const V& v) {
        const double values[3] = {v.x(), v.y(), v.z()};
        std::memcpy(buffer.data() + row * itemsize + fields[field].offset, values, sizeof(values));
    }
    void flag(std::size_t row, std::size_t field, bool value) {
        buffer[row * itemsize + fields[field].offset] = value ? char{1} : char{0};
    }
    py::array array() {
        std::vector<py::str> names;
        std::vector<py::dtype> formats;
        std::vector<py::ssize_t> offsets;
        for (const auto& field : fields) {
            names.emplace_back(field.name);
            // NumPy type strings: "d" is float64 and a leading count is a subarray.
            formats.push_back(field.boolean
                ? py::dtype::of<bool>()
                : py::dtype(field.width == 1
                    ? std::string("d")
                    : std::to_string(field.width) + "d"));
            offsets.push_back(static_cast<py::ssize_t>(field.offset));
        }
        py::dict spec;
        spec["names"] = names;
        spec["formats"] = formats;
        spec["offsets"] = offsets;
        spec["itemsize"] = static_cast<py::ssize_t>(itemsize);
        const py::dtype dtype = py::dtype::from_args(spec);
        const py::array::ShapeContainer shape{static_cast<py::ssize_t>(rows)};
        const py::array::StridesContainer strides{static_cast<py::ssize_t>(itemsize)};
        if (rows == 0) {
            static char empty = 0;  // No records: any non-null address satisfies NumPy.
            return readonly_array(py::array(dtype, shape, strides, &empty));
        }
        // The record buffer becomes the array's owner: no duplicate full copy.
        auto owner = std::unique_ptr<std::vector<char>>(new std::vector<char>(std::move(buffer)));
        char* const data = owner->data();
        const py::capsule capsule(owner.release(), "leo_analysis_records", [](void* pointer) {
            delete static_cast<std::vector<char>*>(pointer);
        });
        return readonly_array(py::array(dtype, shape, strides, data, capsule));
    }
};
py::array orbital_array(std::span<const analysis::OrbitalSample> samples) {
    Table t;
    const auto time = t.add("time_s"), radius = t.add("radius_m"), altitude = t.add("altitude_m");
    const auto sma = t.add("semi_major_axis_m"), ecc = t.add("eccentricity"), inc = t.add("inclination_rad");
    const auto raan = t.add("raan_rad"), argp = t.add("arg_periapsis_rad"), nu = t.add("true_anomaly_rad");
    const auto rp = t.add("periapsis_radius_m"), ra = t.add("apoapsis_radius_m");
    const auto hp = t.add("periapsis_altitude_m"), ha = t.add("apoapsis_altitude_m");
    const auto energy = t.add("specific_energy_j_kg"), momentum = t.add("specific_angular_momentum_m2_s", 3);
    const auto raan_u = t.add("raan_unwrapped_rad"), argp_u = t.add("arg_periapsis_unwrapped_rad");
    const auto nu_u = t.add("true_anomaly_unwrapped_rad");
    const auto kinetic = t.add("specific_kinetic_energy_j_kg"), potential = t.add("specific_potential_energy_j_kg");
    t.allocate(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        t.set(i, time, s.time_s); t.set(i, radius, s.radius_m); t.set(i, altitude, s.altitude_m);
        t.set(i, sma, s.elements.a_m); t.set(i, ecc, s.elements.e); t.set(i, inc, s.elements.i_rad);
        t.set(i, raan, s.elements.raan_rad); t.set(i, argp, s.elements.arg_periapsis_rad);
        t.set(i, nu, s.elements.true_anomaly_rad);
        t.set(i, rp, s.periapsis_radius_m); t.set(i, ra, s.apoapsis_radius_m);
        t.set(i, hp, s.periapsis_altitude_m); t.set(i, ha, s.apoapsis_altitude_m);
        t.set(i, energy, s.specific_energy_j_kg);
        t.set3(i, momentum, s.specific_angular_momentum_m2_s);
        t.set(i, raan_u, s.raan_unwrapped_rad); t.set(i, argp_u, s.arg_periapsis_unwrapped_rad);
        t.set(i, nu_u, s.true_anomaly_unwrapped_rad);
        t.set(i, kinetic, s.specific_kinetic_energy_j_kg); t.set(i, potential, s.specific_potential_energy_j_kg);
    }
    return t.array();
}
py::array apsides_array(std::span<const analysis::ApsisEvent> events) {
    Table t;
    const auto kind = t.add("kind"), time = t.add("time_s"), radius = t.add("radius_m");
    const auto left = t.add("left_index"), fraction = t.add("fraction");
    t.allocate(events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        t.set(i, kind, e.kind == analysis::ApsisKind::periapsis ? 0.0 : 1.0);
        t.set(i, time, e.time_s); t.set(i, radius, e.radius_m);
        t.set(i, left, static_cast<double>(e.left_index)); t.set(i, fraction, e.fraction);
    }
    return t.array();
}
py::array orbits_array(std::span<const analysis::OrbitSummary> orbits) {
    Table t;
    const auto index = t.add("index"), start = t.add("start_time_s"), end = t.add("end_time_s");
    const auto complete = t.add("complete", 1, true), period = t.add("period_s");
    const auto start_fields = std::array<std::size_t, 6>{
        t.add("start_semi_major_axis_m"), t.add("start_eccentricity"), t.add("start_inclination_rad"),
        t.add("start_raan_rad"), t.add("start_arg_periapsis_rad"), t.add("start_true_anomaly_rad")};
    const auto end_fields = std::array<std::size_t, 6>{
        t.add("end_semi_major_axis_m"), t.add("end_eccentricity"), t.add("end_inclination_rad"),
        t.add("end_raan_rad"), t.add("end_arg_periapsis_rad"), t.add("end_true_anomaly_rad")};
    const auto delta_fields = std::array<std::size_t, 6>{
        t.add("delta_semi_major_axis_m"), t.add("delta_eccentricity"), t.add("delta_inclination_rad"),
        t.add("delta_raan_rad"), t.add("delta_arg_periapsis_rad"), t.add("delta_true_anomaly_rad")};
    const auto raan_start = t.add("raan_start_unwrapped_rad"), raan_end = t.add("raan_end_unwrapped_rad");
    t.allocate(orbits.size());
    for (std::size_t i = 0; i < orbits.size(); ++i) {
        const auto& o = orbits[i];
        t.set(i, index, static_cast<double>(o.index));
        t.set(i, start, o.start_time_s); t.set(i, end, o.end_time_s);
        t.flag(i, complete, o.complete);
        t.set(i, period, o.period_s);
        const ClassicalElements sets[3] = {o.start_elements, o.end_elements, o.delta_elements};
        for (std::size_t k = 0; k < 3; ++k) {
            const auto& fields = k == 0 ? start_fields : (k == 1 ? end_fields : delta_fields);
            t.set(i, fields[0], sets[k].a_m); t.set(i, fields[1], sets[k].e); t.set(i, fields[2], sets[k].i_rad);
            t.set(i, fields[3], sets[k].raan_rad); t.set(i, fields[4], sets[k].arg_periapsis_rad);
            t.set(i, fields[5], sets[k].true_anomaly_rad);
        }
        t.set(i, raan_start, o.raan_start_unwrapped_rad); t.set(i, raan_end, o.raan_end_unwrapped_rad);
    }
    return t.array();
}
py::array environment_array(std::span<const analysis::EnvironmentalSample> samples) {
    Table t;
    const auto time = t.add("time_s"), epoch = t.add("epoch_et_s");
    const auto sun = t.add("sun_position_m", 3), illumination = t.add("illumination"), density = t.add("density_kg_m3");
    const auto rotation = t.add("atmosphere_angular_velocity_rad_s", 3);
    const auto point_mass = t.add("point_mass_m_s2", 3), j2 = t.add("j2_m_s2", 3);
    const auto gradient = t.add("gravity_gradient_nm", 3);
    const auto srp_force = t.add("srp_force_n", 3), srp_torque = t.add("srp_torque_nm", 3);
    const auto drag_force = t.add("drag_force_n", 3), drag_torque = t.add("drag_torque_nm", 3);
    const auto albedo_force = t.add("albedo_force_n", 3), albedo_torque = t.add("albedo_torque_nm", 3);
    const auto non_grav_force = t.add("non_gravitational_force_n", 3);
    const auto non_grav_torque = t.add("non_gravitational_torque_nm", 3);
    const auto total_a = t.add("total_acceleration_m_s2", 3), total_alpha = t.add("total_angular_acceleration_rad_s2", 3);
    t.allocate(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        const auto& c = s.contributions;
        t.set(i, time, s.time_s); t.set(i, epoch, s.snapshot.epoch.et_seconds);
        t.set3(i, sun, s.snapshot.sun_position_m);
        t.set(i, illumination, s.snapshot.illumination); t.set(i, density, s.snapshot.density_kg_m3);
        t.set3(i, rotation, s.snapshot.atmosphere_angular_velocity_rad_s);
        t.set3(i, point_mass, c.point_mass_m_s2); t.set3(i, j2, c.j2_m_s2);
        t.set3(i, gradient, c.gravity_gradient_nm);
        t.set3(i, srp_force, c.srp.force_n); t.set3(i, srp_torque, c.srp.torque_nm);
        t.set3(i, drag_force, c.drag.force_n); t.set3(i, drag_torque, c.drag.torque_nm);
        t.set3(i, albedo_force, c.albedo.force_n); t.set3(i, albedo_torque, c.albedo.torque_nm);
        t.set3(i, non_grav_force, c.non_gravitational.force_n);
        t.set3(i, non_grav_torque, c.non_gravitational.torque_nm);
        t.set3(i, total_a, c.total_acceleration_m_s2); t.set3(i, total_alpha, c.total_angular_acceleration_rad_s2);
    }
    return t.array();
}
py::array attitude_array(std::span<const analysis::AttitudeSample> samples) {
    Table t;
    const auto time = t.add("time_s"), quaternion = t.add("quaternion_wxyz", 4);
    const auto omega = t.add("omega_body_rad_s", 3), speed = t.add("angular_speed_rad_s");
    const auto rotational = t.add("rotational_energy_j"), momentum = t.add("inertial_momentum_kg_m2_s");
    const auto torque = t.add("torque_nm", 3), torque_magnitude = t.add("torque_magnitude_nm");
    const auto euler = t.add("relative_zyx_euler_rad", 3), singular = t.add("euler_singular", 1, true);
    t.allocate(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        t.set(i, time, s.time_s);
        const double q[4] = {s.quaternion.w, s.quaternion.x, s.quaternion.y, s.quaternion.z};
        std::memcpy(t.buffer.data() + i * t.itemsize + t.fields[quaternion].offset, q, sizeof(q));
        t.set3(i, omega, s.omega_rad_s);
        t.set(i, speed, s.angular_speed_rad_s); t.set(i, rotational, s.rotational_energy_j);
        // Native inertial momentum is a scalar magnitude in the inertial frame.
        t.set(i, momentum, s.inertial_momentum_kg_m2_s);
        t.set3(i, torque, s.torque_nm); t.set(i, torque_magnitude, s.torque_magnitude_nm);
        t.set3(i, euler, s.relative_zyx_euler_rad);
        t.flag(i, singular, s.euler_singular);
    }
    return t.array();
}
py::array surfaces_array(std::span<const analysis::AttitudeSample> samples) {
    std::size_t count = 0;
    for (const auto& s : samples) count += s.surfaces.size();
    Table t;
    const auto time = t.add("time_s"), surface = t.add("surface_index"), normal = t.add("normal_j2000", 3);
    const auto incidence = t.add("sun_incidence_rad"), illuminated = t.add("projected_illuminated_area_m2");
    const auto aerodynamic = t.add("projected_aerodynamic_area_m2"), orbit_normal = t.add("orbit_normal_angle_rad");
    t.allocate(count);
    std::size_t row = 0;
    for (const auto& s : samples)
        for (const auto& f : s.surfaces) {
            t.set(row, time, s.time_s);
            t.set(row, surface, static_cast<double>(f.surface_index));
            t.set3(row, normal, f.normal_j2000);
            t.set(row, incidence, f.sun_incidence_rad);
            t.set(row, illuminated, f.projected_illuminated_area_m2);
            t.set(row, aerodynamic, f.projected_aerodynamic_area_m2);
            t.set(row, orbit_normal, f.orbit_normal_angle_rad);
            ++row;
        }
    return t.array();
}
py::array work_array(std::span<const analysis::WorkSample> samples) {
    Table t;
    const auto time = t.add("time_s"), force = t.add("srp_force_n", 3);
    const auto acceleration = t.add("srp_acceleration_m_s2", 3), force_rtn = t.add("force_rtn_n", 3);
    const auto power_rtn = t.add("power_rtn_w", 3), work_rtn = t.add("cumulative_work_rtn_j", 3);
    const auto power = t.add("power_w"), cumulative = t.add("cumulative_work_j"), specific = t.add("specific_work_j_kg");
    const auto orbital = t.add("orbital_energy_change_j"), specific_change = t.add("specific_energy_change_j_kg");
    const auto residual = t.add("energy_minus_srp_work_j");
    const auto inbound = t.add("sun_inbound_transverse_work_j"), outbound = t.add("sun_outbound_transverse_work_j");
    const auto mass = t.add("mass_kg");
    t.allocate(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        t.set(i, time, s.time_s);
        t.set3(i, force, s.srp_force_n); t.set3(i, acceleration, s.srp_acceleration_m_s2);
        t.set3(i, force_rtn, s.force_rtn_n); t.set3(i, power_rtn, s.power_rtn_w);
        t.set3(i, work_rtn, s.cumulative_work_rtn_j);
        t.set(i, power, s.power_w); t.set(i, cumulative, s.cumulative_work_j);
        t.set(i, specific, s.specific_work_j_kg);
        t.set(i, orbital, s.orbital_energy_change_j); t.set(i, specific_change, s.specific_energy_change_j_kg);
        t.set(i, residual, s.energy_minus_srp_work_j);
        t.set(i, inbound, s.sun_inbound_transverse_work_j); t.set(i, outbound, s.sun_outbound_transverse_work_j);
        t.set(i, mass, s.mass_kg);
    }
    return t.array();
}
py::array orbit_work_array(std::span<const analysis::OrbitWork> works) {
    Table t;
    const auto index = t.add("index"), start = t.add("start_time_s"), end = t.add("end_time_s");
    const auto complete = t.add("complete", 1, true), work = t.add("work_j"), specific = t.add("specific_work_j_kg");
    const auto work_rtn = t.add("work_rtn_j", 3), orbital = t.add("orbital_energy_change_j");
    const auto residual = t.add("energy_minus_srp_work_j");
    t.allocate(works.size());
    for (std::size_t i = 0; i < works.size(); ++i) {
        const auto& w = works[i];
        t.set(i, index, static_cast<double>(w.index));
        t.set(i, start, w.start_time_s); t.set(i, end, w.end_time_s);
        t.flag(i, complete, w.complete);
        t.set(i, work, w.work_j); t.set(i, specific, w.specific_work_j_kg);
        t.set3(i, work_rtn, w.work_rtn_j);
        t.set(i, orbital, w.orbital_energy_change_j); t.set(i, residual, w.energy_minus_srp_work_j);
    }
    return t.array();
}
// Owns the native samples the reported arrays were derived from; never propagates again.
struct AnalysisBundle {
    Trajectory trajectory;
    analysis::AnalysisResult result;
};
struct Analysis {
    std::shared_ptr<const AnalysisBundle> bundle;
    py::array orbital, apsides, orbits, environment, attitude, surfaces, work, orbit_work;
};
Trajectory single_trajectory(const Result& result) {
    if (result.states.ndim() != 2 || result.times.ndim() != 1)
        throw std::invalid_argument("trajectory arrays must be a single (N, 13) history");
    if (result.states.dtype().is(py::dtype::of<double>()) == false || result.times.dtype().is(py::dtype::of<double>()) == false)
        throw std::invalid_argument("trajectory arrays must be float64");
    const auto states = result.states.unchecked<double, 2>();
    const auto times = result.times.unchecked<double, 1>();
    if (states.shape(1) != static_cast<py::ssize_t>(state_width))
        throw std::invalid_argument("trajectory states must have 13 columns");
    if (states.shape(0) != times.shape(0))
        throw std::invalid_argument("trajectory states and times disagree in length");
    if (!result.context)
        throw std::invalid_argument("trajectory has no dynamics context for analysis");
    Trajectory trajectory;
    trajectory.epoch = result.context->epoch;
    trajectory.samples.reserve(static_cast<std::size_t>(states.shape(0)));
    for (py::ssize_t i = 0; i < states.shape(0); ++i) {
        State state;
        state.r = InertialVector(states(i, 0), states(i, 1), states(i, 2));
        state.v = InertialVector(states(i, 3), states(i, 4), states(i, 5));
        state.q = Quaternion{states(i, 6), states(i, 7), states(i, 8), states(i, 9)};
        state.omega = BodyVector(states(i, 10), states(i, 11), states(i, 12));
        trajectory.samples.push_back({ElapsedTime{times(i)}, state});
    }
    return trajectory;
}
std::shared_ptr<const AnalysisBundle> bundle_of(const Result& result) {
    Trajectory trajectory = single_trajectory(result);
    const Context& context = *result.context;
    // Real acquisition history: re-evaluate the bound source at every sample.
    const EarthEnvironment source = context.source();
    std::vector<EnvironmentSnapshot> snapshots;
    snapshots.reserve(trajectory.samples.size());
    for (const auto& sample : trajectory.samples)
        snapshots.push_back(source(sample.t, sample.state));
    auto bundle = std::make_shared<AnalysisBundle>();
    bundle->trajectory = std::move(trajectory);
    bundle->result = analysis::analyze(bundle->trajectory, context.dynamics(), snapshots);
    return bundle;
}
Analysis make_analysis(const Result& result) {
    Analysis out;
    const std::shared_ptr<const AnalysisBundle> bundle = bundle_of(result);
    out.bundle = bundle;
    out.orbital = orbital_array(bundle->result.orbital);
    out.apsides = apsides_array(bundle->result.apsides);
    out.orbits = orbits_array(bundle->result.orbits);
    out.environment = environment_array(bundle->result.environment);
    out.attitude = attitude_array(bundle->result.attitude);
    out.surfaces = surfaces_array(bundle->result.attitude);
    out.work = work_array(bundle->result.work);
    out.orbit_work = orbit_work_array(bundle->result.orbit_work);
    return out;
}
std::string path_string(py::handle value, const char* name) {
    if (!py::isinstance<py::str>(value) && !py::hasattr(value, "__fspath__"))
        throw std::invalid_argument(std::string(name) + " must be a filesystem path string");
    return py::cast<std::filesystem::path>(value).string();
}
py::array readonly_array(py::array array) {
    readonly(array);
    return array;
}
// Single-diagnostic entry points compute only what they report.
py::array orbital_only(const Result& result) {
    const Trajectory trajectory = single_trajectory(result);
    const Earth earth = result.context->earth;
    return readonly_array(orbital_array(analysis::orbital_history(trajectory, earth)));
}
py::array apsides_only(const Result& result) {
    return readonly_array(apsides_array(analysis::detect_apsides(single_trajectory(result))));
}
py::array orbits_only(const Result& result) {
    const Trajectory trajectory = single_trajectory(result);
    const Earth earth = result.context->earth;
    return readonly_array(orbits_array(analysis::per_orbit(trajectory, earth)));
}
// Every failure leo::analysis::export_csv raises is a file system failure:
// validation problems are std::invalid_argument, I/O problems are runtime_error.
// This is the only place that classifies them, so no message text is matched.
void native_export(const std::filesystem::path& directory, const Trajectory& trajectory,
                   const analysis::AnalysisResult& result) {
    try {
        analysis::export_csv(directory, trajectory, result);
    } catch (const std::runtime_error& e) {
        PyErr_SetString(PyExc_OSError, e.what());
        throw py::error_already_set();
    }
}
void export_directory(const Analysis& analysis, const std::filesystem::path& directory) {
    native_export(directory, analysis.bundle->trajectory, analysis.bundle->result);
}
// Claims a staging directory this process owns; never adopts a pre-existing one,
// so cleanup can never remove somebody else's directory.
std::filesystem::path claim_staging_directory() {
    const std::filesystem::path root = std::filesystem::temp_directory_path();
    const long long stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        const std::filesystem::path candidate =
            root / ("leo-analysis-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(candidate)) return candidate;
        // Losing the race means the name was taken; retry with the next counter.
    }
    // Unambiguously a file system failure; report it as such rather than letting
    // it fall through as an uncategorised runtime error.
    PyErr_SetString(PyExc_OSError, "cannot create a unique analysis staging directory");
    throw py::error_already_set();
}
// The single trajectory.csv is produced by the native writer itself, so bytes,
// timestamp precision and schema cannot drift from the C++ path. The native
// exporter always writes all nine files, so staging costs more I/O than the
// single file it keeps; that buys exact native formatting.
void export_trajectory_file(const Analysis& analysis, const std::filesystem::path& path) {
    const std::filesystem::path staging = claim_staging_directory();
    std::error_code cleanup;
    try {
        native_export(staging, analysis.bundle->trajectory, analysis.bundle->result);
        std::filesystem::copy_file(staging / "trajectory.csv", path, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove_all(staging, cleanup);
    } catch (...) {
        std::filesystem::remove_all(staging, cleanup);
        throw;
    }
}
} // namespace
void bind_analysis(py::module_& m) {
    py::class_<Analysis>(m, "Analysis",
        "Complete native diagnostics for one trajectory. Every array owns its memory and is read-only.\n"
        "Times are elapsed seconds since the initial epoch; epochs are TDB seconds past J2000.\n"
        "Frames: J2000 for inertial vectors, body frame for torques and angular rates, R/T/N for work vectors.")
        .def_property_readonly("orbital", [](const Analysis& a) { return a.orbital; },
            "Osculating two-body element history; (N,) records, radians and metres.")
        .def_property_readonly("apsides", [](const Analysis& a) { return a.apsides; },
            "Radial-velocity zero crossings; (M,) records with kind 0=periapsis, 1=apoapsis.")
        .def_property_readonly("orbits", [](const Analysis& a) { return a.orbits; },
            "Periapsis-to-periapsis summaries; (K,) records, period_s is 0.0 when incomplete.")
        .def_property_readonly("environment", [](const Analysis& a) { return a.environment; },
            "Environment snapshots and selected force/torque contributions; (N,) records.")
        .def_property_readonly("attitude", [](const Analysis& a) { return a.attitude; },
            "Attitude diagnostics; (N,) records.")
        .def_property_readonly("surfaces", [](const Analysis& a) { return a.surfaces; },
            "Per-sample, per-surface incidence and projected areas; (N*S,) records, NaN when unavailable.")
        .def_property_readonly("work", [](const Analysis& a) { return a.work; },
            "SRP power/work history; (N,) records.")
        .def_property_readonly("orbit_work", [](const Analysis& a) { return a.orbit_work; },
            "SRP work integrated per orbit; (K,) records.")
        .def("export_csv", [](const Analysis& a, py::object directory) {
            export_directory(a, std::filesystem::path(path_string(directory, "directory")));
        }, py::arg("directory"),
            "Write the native CSV schema (metadata, trajectory, orbital_elements, attitude, surfaces,\n"
            "environment, work, per_orbit, apsides) into directory, creating it if needed.\n"
            "Raises OSError on any file system failure.");
    // Native CSV I/O is classified at its own boundary (see native_export); this
    // translator only covers standard library file system and stream failures.
    py::register_exception_translator([](std::exception_ptr p) {
        try {
            if (p) std::rethrow_exception(p);
        } catch (const std::filesystem::filesystem_error& e) {
            PyErr_SetString(PyExc_OSError, e.what());
        } catch (const std::ios_base::failure& e) {
            PyErr_SetString(PyExc_OSError, e.what());
        }
    });
    // Trajectory is registered by bind_integration; extend it in place.
    if (!py::hasattr(m, "Trajectory"))
        throw std::logic_error("bind_analysis requires bind_integration to register Trajectory first");
    py::class_<Result> trajectory = py::reinterpret_borrow<py::class_<Result>>(m.attr("Trajectory"));
    trajectory
        .def("orbital_elements", [](const Result& r) { return orbital_only(r); },
            "Osculating two-body element history; (N,) records.")
        .def("apsides", [](const Result& r) { return apsides_only(r); },
            "Apsis events; (M,) records.")
        .def("orbits", [](const Result& r) { return orbits_only(r); },
            "Per-orbit summaries; (K,) records.")
        .def("analyze", [](const Result& r) { return make_analysis(r); },
            "Full native analysis for this trajectory; an Analysis object.")
        .def("to_csv", [](const Result& r, py::object path) {
            export_trajectory_file(make_analysis(r), std::filesystem::path(path_string(path, "path")));
        }, py::arg("path"),
            "Write trajectory.csv, in the native schema, to this file path; raises OSError on file errors.\n"
            "Use analyze().export_csv(directory) for the full nine-file export.");
    py::module_ analysis = m.def_submodule("analysis", "Trajectory analysis helpers mirroring leo::analysis.");
    analysis.def("orbital", [](const Result& r) { return orbital_only(r); }, py::arg("trajectory"),
        "Osculating two-body element history for one trajectory.");
    analysis.def("apsides", [](const Result& r) { return apsides_only(r); }, py::arg("trajectory"),
        "Apsis events for one trajectory.");
    analysis.def("orbits", [](const Result& r) { return orbits_only(r); }, py::arg("trajectory"),
        "Per-orbit summaries for one trajectory.");
    analysis.def("environment", [](const Result& r) { return make_analysis(r).environment; }, py::arg("trajectory"),
        "Environment snapshots and selected contributions for one trajectory.");
    analysis.def("attitude", [](const Result& r) { return make_analysis(r).attitude; }, py::arg("trajectory"),
        "Attitude diagnostics for one trajectory.");
    analysis.def("work", [](const Result& r) { return make_analysis(r).work; }, py::arg("trajectory"),
        "SRP power/work history for one trajectory.");
    analysis.def("export_csv", [](const py::object& target, py::object directory) {
        const std::filesystem::path path{path_string(directory, "directory")};
        if (py::isinstance<Analysis>(target)) {
            export_directory(py::cast<const Analysis&>(target), path);
            return;
        }
        if (!py::isinstance<Result>(target))
            throw std::invalid_argument("export_csv expects an Analysis or Trajectory");
        export_directory(make_analysis(py::cast<const Result&>(target)), path);
    }, py::arg("analysis_or_trajectory"), py::arg("directory"),
        "Write the native CSV schema for an Analysis or Trajectory; raises OSError on file errors.");
}
}