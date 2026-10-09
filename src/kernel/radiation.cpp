#include "leo/kernel/radiation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace leo {
namespace {
constexpr double kPi = 3.14159265358979323846;
struct Geometry { InertialVector sun_axis; double sun_distance_m; double orbital_radius_m; };
Geometry external_geometry(InertialVector r, InertialVector sun, double earth_radius_m,
                          double sun_radius_m) {
    if (!std::isfinite(earth_radius_m)||earth_radius_m<=0)
        throw std::invalid_argument("eclipse geometry requires a positive Earth radius");
    if (!std::isfinite(sun_radius_m)||sun_radius_m<=0)
        throw std::invalid_argument("eclipse geometry requires a positive Sun radius");
    if (!r.as_eigen().allFinite()||!sun.as_eigen().allFinite())
        throw std::invalid_argument("eclipse geometry requires finite positions");
    const double radius = norm(r);
    if (!std::isfinite(radius)||!(radius>earth_radius_m))
        throw std::invalid_argument("eclipse geometry requires a position outside the Earth");
    const double sun_distance = norm(sun);
    if (!std::isfinite(sun_distance)||!(sun_distance>sun_radius_m))
        throw std::invalid_argument("eclipse geometry requires the Sun outside its photosphere");
    return {sun/sun_distance, sun_distance, radius};
}
double apparent_radius(double body_radius_m, double observer_distance_m) {
    return std::asin(std::min(1.0, body_radius_m/observer_distance_m));
}
// Circular-segment area divided by radius squared. The direct expression
// theta - sin(theta)*cos(theta) cancels for the Earth's small segment angle.
double segment_area(double theta) {
    if (theta<0.1) {
        const double z=theta*theta;
        return theta*z*(2.0/3+z*(-2.0/15+z*(4.0/315+z*(-2.0/2835+z*(4.0/155925)))));
    }
    return theta-std::sin(theta)*std::cos(theta);
}
// Illuminated fraction of the uniform-brightness solar disk occluded by the Earth disk.
// Both disks are reduced to the unit sphere and overlapped as circles of angular radii
// alpha (Sun) and beta (Earth) separated by the observed centre separation `distance`,
// so the area fraction is the exact apparent-disk overlap for small angular sizes.
double disk_overlap_illumination(double alpha, double beta, double distance) {
    if (distance>=alpha+beta) return 1.0;
    // One disk fully inside the other: the occluded area is the whole smaller disk,
    // which is the whole solar disk (illumination exactly 0) in total umbra.
    if (distance<=std::abs(alpha-beta)) {
        const double smaller = std::min(alpha,beta);
        return std::clamp(1.0 - (smaller/alpha)*(smaller/alpha), 0.0, 1.0);
    }
    // Scale by the solar radius. Difference-before-division retains contact
    // offsets; factoring Heron's product avoids the subtraction of squared
    // Earth-sized terms in a small solar chord.
    const double k=beta/alpha, d=distance/alpha;
    const double delta=(distance-beta)/alpha, sum=(distance+beta)/alpha;
    const double height=std::sqrt(std::max(0.0,(1-delta)*(1+delta)))*
        std::sqrt((sum-1)/(2*d))*std::sqrt((sum+1)/(2*d));
    const double solar_chord=(1+delta*sum)/(2*d);
    const double earth_chord=d-solar_chord;
    const double earth_segment=k*k*segment_area(std::atan2(height,earth_chord));
    // Near internal contact, compute the small illuminated complement directly
    // rather than subtracting nearly the full solar disk area from pi.
    const double illumination=solar_chord<0 ?
        (segment_area(std::atan2(height,-solar_chord))-earth_segment)/kPi :
        1-(segment_area(std::atan2(height,solar_chord))+earth_segment)/kPi;
    // Only roundoff may leave the mathematically bounded area fraction.
    return std::clamp(illumination,0.0,1.0);
}
double axis_distance(InertialVector r, InertialVector sun_axis) {
    return norm(r - sun_axis*dot(r,sun_axis));
}
void require_unit_range(double value, const char* what) {
    if (!std::isfinite(value)||value<0||value>1) throw std::invalid_argument(what);
}
double inverse_square_pressure(double source_distance_m) {
    const double ratio = astronomical_unit_m/source_distance_m;
    return solar_pressure_1au_n_m2*ratio*ratio;
}
// Shared collimated optical panel law; the source direction is unit length in J2000.
Wrench collimated_wrench(const Spacecraft& craft, Quaternion q, InertialVector source,
                         double pressure_n_m2) {
    if (!std::isfinite(pressure_n_m2)||pressure_n_m2<0)
        throw std::invalid_argument("radiation pressure must be finite and nonnegative");
    const BodyVector source_body = inertial_to_body(q, source);
    BodyVector force_body;
    BodyVector torque_body;
    for (const Surface& surface : craft.surfaces()) {
        const double cosine = dot(surface.normal_body(), source_body);
        if (!(cosine>0)) continue;
        const double specular = surface.optical().specular();
        const double diffuse = surface.optical().diffuse();
        const BodyVector force = source_body*(-pressure_n_m2*surface.area_m2()*cosine
                                             *(1-specular))
            + surface.normal_body()*(-pressure_n_m2*surface.area_m2()*cosine
                                    *(2*specular*cosine + 2*diffuse/3));
        force_body = force_body + force;
        torque_body = torque_body + surface_body_torque(surface, force);
    }
    if (!torque_body.as_eigen().allFinite()) throw std::invalid_argument("nonfinite radiation torque");
    return {body_to_inertial(q, force_body), torque_body};
}
}
double illumination_factor(EclipseModel model, InertialVector r, InertialVector sun,
                          double earth_radius_m, double sun_radius_m) {
    if (model==EclipseModel::none) return 1.0;
    if (model!=EclipseModel::cylindrical&&model!=EclipseModel::conical)
        throw std::invalid_argument("unsupported eclipse model");
    const Geometry geometry = external_geometry(r,sun,earth_radius_m,sun_radius_m);
    if (model==EclipseModel::cylindrical) {
        if (dot(r,geometry.sun_axis)>=0) return 1.0;
        return axis_distance(r,geometry.sun_axis)<earth_radius_m ? 0.0 : 1.0;
    }
    const InertialVector to_sun = sun - r;
    const double sun_separation = norm(to_sun);
    if (!std::isfinite(sun_separation)||!(sun_separation>sun_radius_m))
        throw std::invalid_argument("eclipse observer must be outside the solar photosphere");
    const double solar_alpha = apparent_radius(sun_radius_m,sun_separation);
    const double earth_beta = apparent_radius(earth_radius_m,geometry.orbital_radius_m);
    const double cosine = ((-r)/geometry.orbital_radius_m).as_eigen()
        .dot((to_sun/sun_separation).as_eigen());
    return disk_overlap_illumination(solar_alpha, earth_beta,
                                     std::acos(std::clamp(cosine, -1.0, 1.0)));
}
Wrench solar_radiation_pressure(const Spacecraft& craft, const State& state,
                                const EnvironmentSnapshot& env) {
    require_unit_range(env.illumination,"illumination must lie in [0,1]");
    if (!state.r.as_eigen().allFinite()||!env.sun_position_m.as_eigen().allFinite())
        throw std::invalid_argument("solar radiation pressure requires a finite state and environment");
    const InertialVector to_sun = env.sun_position_m - state.r;
    const double distance = norm(to_sun);
    if (!std::isfinite(distance)||!(distance>0)) throw std::invalid_argument("solar radiation pressure requires a distinct finite source");
    return collimated_wrench(craft, state.q, to_sun/distance,
                             inverse_square_pressure(distance)*env.illumination);
}
Wrench simplified_albedo(const Spacecraft& craft, const State& state,
                         const EnvironmentSnapshot& env, double earth_radius_m, double albedo) {
    require_unit_range(albedo,"albedo must lie in [0,1]");
    if (!std::isfinite(earth_radius_m)||earth_radius_m<=0)
        throw std::invalid_argument("albedo requires a positive Earth radius");
    if (!state.r.as_eigen().allFinite()||!env.sun_position_m.as_eigen().allFinite())
        throw std::invalid_argument("albedo requires a finite state and environment");
    const double radius = norm(state.r);
    if (!std::isfinite(radius)||!(radius>earth_radius_m))
        throw std::invalid_argument("albedo requires a position outside the Earth");
    const double sun_distance = norm(env.sun_position_m);
    if (!std::isfinite(sun_distance)||!(sun_distance>0)) throw std::invalid_argument("albedo requires a distinct finite solar position");
    const InertialVector radial = state.r/radius;
    const double local_day = std::max(0.0, dot(radial, env.sun_position_m/sun_distance));
    const double ratio = earth_radius_m/radius;
    return collimated_wrench(craft, state.q, radial*(-1),
        inverse_square_pressure(sun_distance)*albedo*ratio*ratio*local_day);
}
}