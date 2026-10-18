#include "configuration.hpp"
#include "leo/integration/detail/packed_state.hpp"
#include "leo/kernel/aerodynamics.hpp"
#include "leo/kernel/orbital.hpp"
#include "leo/kernel/radiation.hpp"
#include <algorithm>
#include <array>
#include <span>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
namespace leo::python {
namespace {
using Array = py::array_t<double,py::array::c_style|py::array::forcecast>;
using Shape = std::vector<py::ssize_t>;

// Strict (count,) vector: one dimension, exactly count values, contiguous float64.
Array vector_checked(const py::handle& input,const char* name,py::ssize_t count) {
    Array array=numeric(input,name);
    if(array.ndim()!=1||array.size()!=count)
        throw py::value_error(std::string(name)+": expected shape ("+std::to_string(count)+",)");
    return array;
}
Array vector3(const py::handle& input,const char* name) { return vector_checked(input,name,3); }
Array quaternion4(const py::handle& input) { return vector_checked(input,"quaternion_wxyz",4); }

// Strict (rows, 3) sample grid; rows are copied into owned native storage.
Array samples_checked(const py::handle& input,const char* name,std::size_t rows) {
    Array array=numeric(input,name);
    if(array.ndim()!=2||array.shape(0)!=static_cast<py::ssize_t>(rows)||array.shape(1)!=3)
        throw py::value_error(std::string(name)+": expected shape ("+std::to_string(rows)+", 3)");
    return array;
}
Array matrix3(const py::handle& input,const char* name) {
    Array array=numeric(input,name);
    if(array.ndim()!=2||array.shape(0)!=3||array.shape(1)!=3)
        throw py::value_error(std::string(name)+": expected shape (3, 3)");
    return array;
}
Eigen::Matrix3d inertia_from(const py::handle& input) {
    const Array array=matrix3(input,"inertia_kg_m2");
    Eigen::Matrix3d matrix;
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) matrix(i,j)=array.at(i,j);
    if (!matrix.allFinite()) throw py::value_error("inertia_kg_m2: entries must be finite");
    return matrix;
}
// Inertia keeps its canonicalised tensor private; multiplying the unit basis recovers
// exactly the operator multiply()/solve() apply, so no native accessor is required.
Eigen::Matrix3d inertia_matrix(const Inertia& inertia) {
    Eigen::Matrix3d matrix;
    for (int j=0;j<3;++j) {
        const BodyVector unit{j==0?1.:0.,j==1?1.:0.,j==2?1.:0.};
        matrix.col(j)=inertia.multiply(unit).as_eigen();
    }
    return matrix;
}
std::vector<InertialVector> samples_from(const py::handle& input,const char* name,std::size_t rows) {
    const Array array=samples_checked(input,name,rows);
    std::vector<InertialVector> samples;
    samples.reserve(rows);
    for (std::size_t i=0;i<rows;++i) {
        const auto row=static_cast<py::ssize_t>(i);
        const InertialVector sample{array.at(row,0),array.at(row,1),array.at(row,2)};
        if (!sample.as_eigen().allFinite()) throw py::value_error(std::string(name)+": entries must be finite");
        samples.push_back(sample);
    }
    return samples;
}
std::vector<double> epochs_from(const py::handle& input) {
    const Array array=numeric(input,"et_seconds");
    if(array.ndim()!=1||array.size()<2) throw py::value_error("et_seconds: expected a 1-D array of at least two epochs");
    std::vector<double> epochs(static_cast<std::size_t>(array.size()));
    for (std::size_t i=0;i<epochs.size();++i) {
        epochs[i]=array.at(static_cast<py::ssize_t>(i));
        if (!std::isfinite(epochs[i])) throw py::value_error("et_seconds: epochs must be finite");
        if (i&&epochs[i]<=epochs[i-1]) throw py::value_error("et_seconds: epochs must be strictly increasing");
    }
    return epochs;
}
py::array owning(const Shape& shape,std::span<const double> values) {
    py::array_t<double> result(shape);
    std::copy(values.begin(),values.end(),result.mutable_data());
    readonly(result);
    return result;
}
// Read-only owning (3,) array for either frame's vector type.
template<class V> py::array make_array(const V& v) {
    py::array_t<double> result(3);
    auto m=result.mutable_unchecked<1>();
    m(0)=v.x(); m(1)=v.y(); m(2)=v.z();
    readonly(result);
    return result;
}
py::array make_array(const Quaternion& q) {
    const std::array values{q.w,q.x,q.y,q.z};
    return owning({4},values);
}
DragModel drag_model_from(const std::string& name) {
    if (name=="none") return DragModel::none;
    if (name=="flat_plate") return DragModel::flat_plate;
    if (name=="sentman") return DragModel::sentman;
    throw py::value_error("drag_law must be 'none', 'flat_plate' or 'sentman'");
}
std::string drag_model_name(DragModel model) {
    switch (model) {
        case DragModel::none: return "none";
        case DragModel::flat_plate: return "flat_plate";
        case DragModel::sentman: return "sentman";
    }
    throw py::value_error("unsupported drag model");
}
// None means "not selected"; any other value must be a Contribution.
SurfaceContribution optional_contribution(const py::handle& value,const char* name) {
    if (value.is_none()) return {};
    if (!py::isinstance<SurfaceContribution>(value))
        throw py::value_error(std::string(name)+" must be a Contribution or None");
    return py::cast<SurfaceContribution>(value);
}
// A disabled selection is reported as None: an absent and a fully disabled contribution
// are the same native configuration.
py::object reported(const SurfaceContribution& selection) {
    if (selection.enabled()) return py::cast(selection);
    return py::object(py::none());
}
// Positive integer conversion for the attempt limit: booleans, floats and out-of-range
// values are rejected instead of being truncated into an unsigned count.
std::size_t attempt_limit(const py::handle& value) {
    if (PyBool_Check(value.ptr())||!PyIndex_Check(value.ptr()))
        throw py::value_error("max_attempts must be a positive integer");
    const long long count=PyLong_AsLongLong(value.ptr());
    if (PyErr_Occurred()) {PyErr_Clear();throw py::value_error("max_attempts is outside supported integer range");}
    if (count<=0) throw py::value_error("max_attempts must be a positive integer");
    return static_cast<std::size_t>(count);
}
std::string format_number(double value) { return std::to_string(value); }
} // namespace

// --------------------------------------------------------------------------
// Shared numeric conversion
// --------------------------------------------------------------------------
py::array_t<double,py::array::c_style|py::array::forcecast> numeric(py::handle input,const char* name) {
    if (!input||input.is_none()) throw py::value_error(std::string(name)+" is required");
    py::array raw=py::array::ensure(input);
    if(!raw) throw py::value_error(std::string(name)+": expected a real numeric array");
    // Inspect the original dtype before float64 conversion for arrays AND sequences;
    // strings, objects and complex numbers must not be silently coerced or truncated.
    const char kind=raw.dtype().kind();
    if(kind!='f'&&kind!='i'&&kind!='u'&&kind!='b')
        throw py::value_error(std::string(name)+": expected a real numeric array");
    // Converts to contiguous native-endian float64, copying unless the input already
    // matches. Callers copy values out before the temporary is released.
    Array out=Array::ensure(raw);
    if (!out) throw py::value_error(std::string(name)+": expected a real numeric array");
    return out;
}

State state_from_array(py::handle input) {
    const Array array=vector_checked(input,"x",13);
    return validated_state(State{{array.at(0),array.at(1),array.at(2)},
                                 {array.at(3),array.at(4),array.at(5)},
                                 {array.at(6),array.at(7),array.at(8),array.at(9)},
                                 {array.at(10),array.at(11),array.at(12)}});
}

py::array state_array(const State& state) {
    const detail::PackedState packed=detail::pack(state);
    return owning({13},packed);
}

void readonly(py::array& array) { array.attr("flags").attr("writeable")=false; }

void validate_options(const IntegrationOptions& options) {
    // The unchanged propagate() precondition, reused where no integration runs.
    for (double value:{options.rtol,options.position_atol_m,options.velocity_atol_m_s,
                       options.quaternion_atol,options.omega_atol_rad_s,
                       options.initial_step_s,options.max_step_s})
        if (!std::isfinite(value)||value<=0)
            throw std::invalid_argument("tolerances and steps must be finite and positive");
    if (options.max_step_s<options.initial_step_s||options.max_attempts==0)
        throw std::invalid_argument("invalid step limits");
}

// --------------------------------------------------------------------------
// Sun ephemeris providers
// --------------------------------------------------------------------------
TabulatedSunProvider::TabulatedSunProvider(std::vector<double> epochs,std::vector<EphemerisState> states) {
    if (epochs.size()<2||epochs.size()!=states.size())
        throw std::invalid_argument("tabulated ephemeris needs matching epoch and state samples, at least two");
    samples_.reserve(epochs.size());
    for (std::size_t i=0;i<epochs.size();++i) {
        if (!std::isfinite(epochs[i])||!states[i].position_m.as_eigen().allFinite()||!states[i].velocity_m_s.as_eigen().allFinite())
            throw std::invalid_argument("tabulated ephemeris samples must be finite");
        if (i&&(epochs[i]<=epochs[i-1]||!std::isfinite(epochs[i]-epochs[i-1])))
            throw std::invalid_argument("tabulated ephemeris epochs must be strictly increasing with a finite span");
        if (norm(states[i].position_m)<=0)
            throw std::invalid_argument("tabulated ephemeris Sun position must be nonzero");
        samples_.push_back({epochs[i],states[i]});
    }
}

EphemerisState TabulatedSunProvider::geometric_state(int target_naif_id,Epoch epoch) const {
    if (target_naif_id!=10||!std::isfinite(epoch.et_seconds))
        throw std::invalid_argument("Sun target and finite TDB epoch required");
    const double et=epoch.et_seconds;
    if (et<samples_.front().et||et>samples_.back().et)
        throw std::out_of_range("epoch outside tabulated Sun coverage");
    if (et==samples_.back().et) return samples_.back().state;
    // First sample at or after et; sample_count>=2 and coverage checked above.
    const auto upper=std::lower_bound(samples_.begin(),samples_.end(),et,
        [](const Sample& sample,double value) { return sample.et<value; });
    std::size_t i=static_cast<std::size_t>(upper-samples_.begin());
    if (i==0) return samples_.front().state;
    const Sample& a=samples_[i-1];
    const Sample& b=samples_[i];
    if (et==a.et) return a.state;
    // An interior knot returns the supplied sample; a Hermite evaluation at u=1 would
    // differ from it by the cancellation of absolute Sun positions.
    if (et==b.et) return b.state;
    const double h=b.et-a.et,u=(et-a.et)/h;
    // Difference-based Hermite form, identical to InterpolatedEphemerisProvider.
    const InertialVector delta=b.state.position_m-a.state.position_m;
    const InertialVector c2=delta*3-(a.state.velocity_m_s*2+b.state.velocity_m_s)*h;
    const InertialVector c3=delta*(-2)+(a.state.velocity_m_s+b.state.velocity_m_s)*h;
    return {a.state.position_m+(a.state.velocity_m_s*h+(c2+c3*u)*u)*u,
            a.state.velocity_m_s+(c2*2+c3*(3*u))*(u/h)};
}

ConstantSunProvider::ConstantSunProvider(InertialVector position_m):position_m_(position_m) {
    if (!position_m.as_eigen().allFinite()||norm(position_m)<=0)
        throw std::invalid_argument("constant Sun position must be finite and nonzero");
}

EphemerisState ConstantSunProvider::geometric_state(int target_naif_id,Epoch epoch) const {
    if (target_naif_id!=10||!std::isfinite(epoch.et_seconds))
        throw std::invalid_argument("Sun target and finite TDB epoch required");
    return {position_m_,{}};
}

// --------------------------------------------------------------------------
// Shared conversion helpers
// --------------------------------------------------------------------------
Earth resolved_earth(const py::handle& earth) {
    if (earth.is_none()) return Earth{};
    if (!py::isinstance<Earth>(earth)) throw py::value_error("earth must be an Earth or None");
    return earth.cast<Earth>();
}

InertialVector as_inertial_vector(const py::handle& value,const char* name) {
    const Array array=vector3(value,name);
    return InertialVector(array.at(0),array.at(1),array.at(2));
}

BodyVector as_body_vector(const py::handle& value,const char* name) {
    const Array array=vector3(value,name);
    return BodyVector(array.at(0),array.at(1),array.at(2));
}

EclipseModel as_eclipse_model(const std::string& name) {
    if (name=="none") return EclipseModel::none;
    if (name=="cylindrical") return EclipseModel::cylindrical;
    if (name=="conical") return EclipseModel::conical;
    throw py::value_error("eclipse must be 'none', 'cylindrical' or 'conical'");
}

std::string eclipse_model_name(EclipseModel model) {
    switch (model) {
        case EclipseModel::none: return "none";
        case EclipseModel::cylindrical: return "cylindrical";
        case EclipseModel::conical: return "conical";
    }
    throw py::value_error("unsupported eclipse model");
}

// --------------------------------------------------------------------------
// Python classes
// --------------------------------------------------------------------------
void bind_configuration(py::module_& m) {
    py::class_<Earth>(m,"Earth","Central Earth constants used by gravity and geopotential models.")
        .def(py::init([](double mu,double radius,double j2) {
                 const Earth earth{mu,radius,j2};
                 (void)EarthGravity(earth,false); // Rejects invalid constants.
                 return earth;
             }),py::kw_only(),py::arg("mu_m3_s2")=3.986004418e14,
             py::arg("equatorial_radius_m")=6378136.3,py::arg("j2")=1.08262668e-3)
        .def_property_readonly("mu_m3_s2",[](const Earth& e){return e.mu_m3_s2;})
        .def_property_readonly("equatorial_radius_m",[](const Earth& e){return e.equatorial_radius_m;})
        .def_property_readonly("j2",[](const Earth& e){return e.j2;})
        .def("__repr__",[](const Earth& e){return "leo_dynamics.Earth(mu_m3_s2="+format_number(e.mu_m3_s2)+
             ", equatorial_radius_m="+format_number(e.equatorial_radius_m)+", j2="+format_number(e.j2)+")";});

    py::class_<ClassicalElements>(m,"KeplerianElements","Bound elliptic elements; all angles in radians.")
        .def(py::init([](double a,double e,double i,double raan,double argp,double nu) {
                 const ClassicalElements elements{a,e,i,raan,argp,nu};
                 (void)coe_to_cartesian(elements,Earth{}.mu_m3_s2); // Domain validation.
                 return elements;
             }),py::kw_only(),py::arg("a_m"),py::arg("e"),py::arg("i_rad"),
             py::arg("raan_rad"),py::arg("arg_periapsis_rad"),py::arg("true_anomaly_rad"))
        .def_property_readonly("a_m",[](const ClassicalElements& c){return c.a_m;})
        .def_property_readonly("e",[](const ClassicalElements& c){return c.e;})
        .def_property_readonly("i_rad",[](const ClassicalElements& c){return c.i_rad;})
        .def_property_readonly("raan_rad",[](const ClassicalElements& c){return c.raan_rad;})
        .def_property_readonly("arg_periapsis_rad",[](const ClassicalElements& c){return c.arg_periapsis_rad;})
        .def_property_readonly("true_anomaly_rad",[](const ClassicalElements& c){return c.true_anomaly_rad;})
        .def("__repr__",[](const ClassicalElements& c){return "leo_dynamics.KeplerianElements(a_m="+format_number(c.a_m)+
             ", e="+format_number(c.e)+", i_rad="+format_number(c.i_rad)+", raan_rad="+format_number(c.raan_rad)+
             ", arg_periapsis_rad="+format_number(c.arg_periapsis_rad)+
             ", true_anomaly_rad="+format_number(c.true_anomaly_rad)+")";});

    m.def("keplerian_to_cartesian",[](double a,double e,double i,double raan,double argp,double nu,const py::handle& earth) {
             const ClassicalElements elements{a,e,i,raan,argp,nu};
             const CartesianOrbit orbit=coe_to_cartesian(elements,resolved_earth(earth).mu_m3_s2);
             return py::make_tuple(make_array(orbit.r),make_array(orbit.v));
         },py::kw_only(),py::arg("a_m"),py::arg("e"),py::arg("i_rad"),py::arg("raan_rad"),
         py::arg("arg_periapsis_rad"),py::arg("true_anomaly_rad"),py::arg("earth")=py::none(),
         "Convert bound elliptic elements to a (position_m, velocity_m_s) pair of (3,) arrays.");

    m.def("cartesian_to_keplerian",[](const py::handle& position,const py::handle& velocity,const py::handle& earth) {
             return cartesian_to_coe({as_inertial_vector(position,"position_m"),as_inertial_vector(velocity,"velocity_m_s")},
                                    resolved_earth(earth).mu_m3_s2);
         },py::arg("position_m"),py::arg("velocity_m_s"),py::kw_only(),py::arg("earth")=py::none(),
         "Convert an inertial state to bound elliptic elements using native singular-gauge conventions.");

    py::class_<State>(m,"State","13-state inertial/attitude vector [r, v, q(wxyz), omega_body].")
        .def(py::init([](const py::object& r,const py::object& v,const py::object& q,const py::object& omega) {
                 const Array position=vector3(r,"position_m");
                 const Array velocity=vector3(v,"velocity_m_s");
                 const Array quaternion=quaternion4(q);
                 const Array rate=vector3(omega,"omega_body_rad_s");
                 return validated_state(State{{position.at(0),position.at(1),position.at(2)},
                                              {velocity.at(0),velocity.at(1),velocity.at(2)},
                                              {quaternion.at(0),quaternion.at(1),quaternion.at(2),quaternion.at(3)},
                                              {rate.at(0),rate.at(1),rate.at(2)}});
             }),py::kw_only(),py::arg("position_m"),py::arg("velocity_m_s"),
             py::arg("quaternion_wxyz")=py::make_tuple(1.0,0.0,0.0,0.0),
             py::arg("omega_body_rad_s")=py::make_tuple(0.0,0.0,0.0))
        .def_property_readonly("position_m",[](const State& s){return make_array(s.r);})
        .def_property_readonly("velocity_m_s",[](const State& s){return make_array(s.v);})
        .def_property_readonly("quaternion_wxyz",[](const State& s){return make_array(s.q);})
        .def_property_readonly("omega_body_rad_s",[](const State& s){return make_array(s.omega);})
        .def("to_array",[](const State& s){return state_array(s);},
             "Pack as [rx, ry, rz, vx, vy, vz, qw, qx, qy, qz, wx, wy, wz].")
        .def_static("from_array",[](const py::handle& x){return state_from_array(x);},py::arg("x"),
                    "Unpack a (13,) [r, v, q(wxyz), omega_body] array.")
        .def("__repr__",[](const State& s){return "leo_dynamics.State(position_m="+format_number(s.r.x())+
             ", velocity_m_s="+format_number(s.v.x())+", ...)";});

    py::class_<OpticalProperties>(m,"OpticalProperties","Fractions of incident optical power.")
        .def(py::init([](double specular,double diffuse){return OpticalProperties(specular,diffuse);}),
             py::arg("specular")=0.0,py::arg("diffuse")=0.0)
        .def_property_readonly("specular",[](const OpticalProperties& o){return o.specular();})
        .def_property_readonly("diffuse",[](const OpticalProperties& o){return o.diffuse();})
        .def("__repr__",[](const OpticalProperties& o){return "leo_dynamics.OpticalProperties(specular="+
             format_number(o.specular())+", diffuse="+format_number(o.diffuse())+")";});

    py::class_<AerodynamicProperties>(m,"AerodynamicProperties","Drag coefficient and momentum accommodation.")
        .def(py::init([](double cd,double accommodation){return AerodynamicProperties(cd,accommodation);}),
             py::arg("drag_coefficient")=2.2,py::arg("accommodation")=0.9)
        .def_property_readonly("drag_coefficient",[](const AerodynamicProperties& a){return a.drag_coefficient();})
        .def_property_readonly("accommodation",[](const AerodynamicProperties& a){return a.accommodation();})
        .def("__repr__",[](const AerodynamicProperties& a){return "leo_dynamics.AerodynamicProperties(drag_coefficient="+
             format_number(a.drag_coefficient())+", accommodation="+format_number(a.accommodation())+")";});

    py::class_<Surface>(m,"Surface","One-sided plate with body-frame geometry and optical/aerodynamic properties.")
        .def(py::init([](double area,const py::object& normal,const py::object& center,
                         const py::object& optical,const py::object& aerodynamic) {
                 return Surface(area,as_body_vector(normal,"normal_body"),
                                as_body_vector(center,"center_of_pressure_body_m"),
                                optical.is_none()?OpticalProperties{}:py::cast<OpticalProperties>(optical),
                                aerodynamic.is_none()?AerodynamicProperties{}:py::cast<AerodynamicProperties>(aerodynamic));
             }),py::kw_only(),py::arg("area_m2"),py::arg("normal_body"),
             py::arg("center_of_pressure_body_m"),py::arg("optical")=py::none(),
             py::arg("aerodynamic")=py::none())
        .def_property_readonly("area_m2",[](const Surface& s){return s.area_m2();})
        .def_property_readonly("normal_body",[](const Surface& s){return make_array(s.normal_body());})
        .def_property_readonly("center_of_pressure_body_m",[](const Surface& s){return make_array(s.center_of_pressure_body());})
        .def_property_readonly("optical",[](const Surface& s){return s.optical();},py::return_value_policy::copy)
        .def_property_readonly("aerodynamic",[](const Surface& s){return s.aerodynamic();},py::return_value_policy::copy)
        .def("__repr__",[](const Surface& s){return "leo_dynamics.Surface(area_m2="+format_number(s.area_m2())+", ...)";});

    py::class_<Spacecraft>(m,"Spacecraft","Mass, symmetric positive-definite body inertia and one-sided surfaces.")
        .def(py::init([](double mass,const py::object& inertia,std::vector<Surface> surfaces) {
                 return Spacecraft(mass,Inertia(inertia_from(inertia)),std::move(surfaces));
             }),py::kw_only(),py::arg("mass_kg"),py::arg("inertia_kg_m2"),
             py::arg("surfaces")=std::vector<Surface>{})
        .def_property_readonly("mass_kg",[](const Spacecraft& s){return s.mass_kg();})
        .def_property_readonly("inertia_kg_m2",[](const Spacecraft& s) {
                 const Eigen::Matrix3d matrix=inertia_matrix(s.inertia());
                 std::array<double,9> values;
                 for (int i=0;i<3;++i) for (int j=0;j<3;++j) values[3*i+j]=matrix(i,j);
                 return owning({3,3},values);
             })
        .def_property_readonly("surfaces",[](const Spacecraft& s) {
                 return std::vector<Surface>(s.surfaces().begin(),s.surfaces().end());
             },py::return_value_policy::copy)
        .def("__repr__",[](const Spacecraft& s){return "leo_dynamics.Spacecraft(mass_kg="+format_number(s.mass_kg())+
             ", surfaces="+std::to_string(s.surfaces().size())+")";});

    py::class_<SurfaceContribution>(m,"Contribution","Independent force and torque switches for one model.")
        .def(py::init([](bool force,bool torque){return SurfaceContribution{force,torque};}),
             py::arg("force")=false,py::arg("torque")=false)
        .def_property_readonly("force",[](const SurfaceContribution& c){return c.force;})
        .def_property_readonly("torque",[](const SurfaceContribution& c){return c.torque;})
        .def("enabled",[](const SurfaceContribution& c){return c.enabled();},
             "True when either the force or the torque contribution is selected.")
        .def("__repr__",[](const SurfaceContribution& c){return std::string("leo_dynamics.Contribution(force=")+
             (c.force?"True":"False")+", torque="+(c.torque?"True":"False")+")";});

    py::class_<Model>(m,"DynamicsModel",
            "Immutable force/torque model selection. J2 is fixed to the J2000 +Z axis;\n"
            "drag laws are the native flat-plate and cold-beam Sentman approximations and the\n"
            "albedo model is the native simplified radial-beam Earth reflection.")
        .def(py::init([](const std::string& gravity,const py::object& srp,const py::object& drag,
                         const std::string& drag_law,const py::object& albedo,double earth_albedo,bool gravity_gradient) {
                 if (gravity!="point_mass"&&gravity!="j2") throw py::value_error("gravity must be 'point_mass' or 'j2'");
                 Model model;
                 model.j2=gravity=="j2";
                 model.models.srp=optional_contribution(srp,"srp");
                 model.models.drag=optional_contribution(drag,"drag");
                 model.models.albedo=optional_contribution(albedo,"albedo");
                 model.models.drag_model=drag_model_from(drag_law);
                 model.models.earth_albedo=earth_albedo;
                 model.models.gravity_gradient=gravity_gradient;
                 if (!std::isfinite(earth_albedo)||earth_albedo<0||earth_albedo>1)
                     throw py::value_error("Earth albedo must be in [0,1]");
                 if (model.models.drag.enabled()&&model.models.drag_model==DragModel::none)
                     throw py::value_error("enabled drag needs an explicit drag model");
                 return model;
             }),py::kw_only(),py::arg("gravity")="point_mass",py::arg("srp")=py::none(),
             py::arg("drag")=py::none(),py::arg("drag_law")="none",py::arg("albedo")=py::none(),
             py::arg("earth_albedo")=0.3,py::arg("gravity_gradient")=false)
        .def_property_readonly("gravity",[](const Model& m){return std::string(m.j2?"j2":"point_mass");})
        .def_property_readonly("drag_law",[](const Model& m){return drag_model_name(m.models.drag_model);})
        .def_property_readonly("srp",[](const Model& m){return reported(m.models.srp);})
        .def_property_readonly("drag",[](const Model& m){return reported(m.models.drag);})
        .def_property_readonly("albedo",[](const Model& m){return reported(m.models.albedo);})
        .def_property_readonly("earth_albedo",[](const Model& m){return m.models.earth_albedo;})
        .def_property_readonly("gravity_gradient",[](const Model& m){return m.models.gravity_gradient;})
        .def("needs_environment",[](const Model& m){
                 return m.models.srp.enabled()||m.models.drag.enabled()||m.models.albedo.enabled();},
             "True when a selected model needs a Sun or an atmospheric acquisition.")
        .def("__repr__",[](const Model& m){
             return std::string("leo_dynamics.DynamicsModel(gravity=\"")+(m.j2?"j2":"point_mass")+
                    "\", drag_law=\""+drag_model_name(m.models.drag_model)+
                    "\", earth_albedo="+format_number(m.models.earth_albedo)+
                    ", gravity_gradient="+(m.models.gravity_gradient?"True":"False")+")";});

    py::class_<SunSource>(m,"SunEphemeris",
            "Sun acquisition for an Environment. Requires no CSPICE installation.\n"
            "Epochs are TDB ephemeris seconds past J2000, never UTC or Unix timestamps.")
        .def_static("constant",[](const py::handle& position) {
                 SunSource source;
                 source.constant=true;
                 source.provider=std::make_shared<ConstantSunProvider>(as_inertial_vector(position,"position_m"));
                 return source;
             },py::arg("position_m"),
             "Constant Earth-centred J2000 Sun position in metres with zero relative\n"
             "velocity, for controlled experiments only.")
        .def_static("tabulated",[](const py::handle& et_seconds,const py::handle& position,const py::handle& velocity) {
                 std::vector<double> epochs=epochs_from(et_seconds);
                 const std::vector<InertialVector> positions=samples_from(position,"position_m",epochs.size());
                 const std::vector<InertialVector> velocities=samples_from(velocity,"velocity_m_s",epochs.size());
                 std::vector<EphemerisState> states;
                 states.reserve(epochs.size());
                 for (std::size_t i=0;i<epochs.size();++i) states.push_back({positions[i],velocities[i]});
                 SunSource source;
                 source.provider=std::make_shared<TabulatedSunProvider>(std::move(epochs),std::move(states));
                 return source;
             },py::kw_only(),py::arg("et_seconds"),py::arg("position_m"),py::arg("velocity_m_s"),
             "Independently owned Sun samples interpolated with the native cubic Hermite\n"
             "scheme. et_seconds is a strictly increasing 1-D array of at least two epochs;\n"
             "position_m and velocity_m_s are matching (N, 3) arrays. Queries outside the\n"
             "covered range raise DomainError instead of extrapolating.")
        .def_property_readonly("kind",[](const SunSource& s){return std::string(s.constant?"constant":"tabulated");})
        .def_property_readonly("sample_count",[](const SunSource& s){return s.sample_count();},
             "Owned samples; 0 for the constant provider, which stores none.")
        .def_property_readonly("start_et_s",[](const SunSource& s){return s.start_et_s();},
             "First covered TDB epoch in seconds past J2000; 0.0 when the provider is constant.")
        .def_property_readonly("end_et_s",[](const SunSource& s){return s.end_et_s();},
             "Last covered TDB epoch in seconds past J2000; 0.0 when the provider is constant.")
        .def("sample",[](const SunSource& s,double et_seconds) {
                 const EphemerisState state=s.state_at(et_seconds);
                 return py::make_tuple(make_array(state.position_m),make_array(state.velocity_m_s));
             },py::arg("et_seconds"),
             "Geometric Earth-observed Sun state at one TDB epoch as (position_m, velocity_m_s).")
        .def("__repr__",[](const SunSource& s){
             return std::string("leo_dynamics.SunEphemeris.")+(s.constant?"constant()":"tabulated()");});

    py::class_<Atmosphere>(m,"ExponentialAtmosphere",
            "Spherically symmetric exponential density profile scaled by `scale`.\n"
            "It carries no latitude, solar or local-time dependence and needs no external data.")
        .def(py::init([](double scale){return Atmosphere{scale,ExponentialAtmosphere(scale)};}),
             py::arg("scale")=1.0)
        .def_property_readonly("scale",[](const Atmosphere& a){return a.scale;})
        .def_property_readonly_static("min_altitude_m",
             [](const py::object&){return ExponentialAtmosphere::min_altitude_m;},
             "Lowest supported altitude above the equatorial radius, in metres.")
        .def_property_readonly_static("max_altitude_m",
             [](const py::object&){return ExponentialAtmosphere::max_altitude_m;},
             "Highest supported altitude above the equatorial radius, in metres.")
        .def("density_kg_m3",[](const Atmosphere& a,double altitude_m){return a.density_kg_m3(altitude_m);},
             py::arg("altitude_m"),
             "Density in kg/m3 at an altitude above the equatorial radius.\n"
             "Raises DomainError outside the supported altitude range.")
        .def("__repr__",[](const Atmosphere& a){return "leo_dynamics.ExponentialAtmosphere(scale="+format_number(a.scale)+")";});

    py::class_<Environment>(m,"Environment",
            "Solar, atmospheric and eclipse acquisition for one trajectory.\n"
            "A selected radiation model requires `sun`; selected drag requires `atmosphere`.\n"
            "An environment is immutable and owns its Sun provider for the whole simulation.")
        .def(py::init([](const py::object& sun,const py::object& atmosphere,const std::string& eclipse,
                         const py::handle& rotation) {
                 Environment environment;
                 if (!sun.is_none()) {
                     if (!py::isinstance<SunSource>(sun)) throw py::value_error("sun must be a SunEphemeris or None");
                     environment.sun=py::cast<SunSource>(sun).provider;
                 }
                 if (!atmosphere.is_none()) {
                     if (!py::isinstance<Atmosphere>(atmosphere))
                         throw py::value_error("atmosphere must be an ExponentialAtmosphere or None");
                     environment.density=py::cast<Atmosphere>(atmosphere).model;
                     environment.atmosphere=true;
                 }
                 environment.eclipse=as_eclipse_model(eclipse);
                 environment.rotation=as_inertial_vector(rotation,"atmosphere_rotation_rad_s");
                 // Native construction rejects unsupported eclipse models and nonfinite
                 // configuration before any propagation runs.
                 (void)EarthEnvironment(Earth{},Epoch{0},environment.sun.get(),environment.atmosphere,
                                       environment.eclipse,environment.density,environment.rotation);
                 return environment;
             }),py::kw_only(),py::arg("sun")=py::none(),py::arg("atmosphere")=py::none(),
             py::arg("eclipse")="conical",
             py::arg("atmosphere_rotation_rad_s")=py::make_tuple(0.0,0.0,7.292115e-5))
        .def_property_readonly("sun",[](const Environment& e)->py::object {
                 if (!e.sun) return py::object(py::none());
                 return py::cast(SunSource{e.sun,dynamic_cast<const ConstantSunProvider*>(e.sun.get())!=nullptr});
             })
        .def_property_readonly("has_atmosphere",[](const Environment& e){return e.atmosphere;},
             "True when atmospheric density is acquired; False means no drag is available.")
        .def_property_readonly("eclipse",[](const Environment& e){return eclipse_model_name(e.eclipse);})
        .def_property_readonly("atmosphere_rotation_rad_s",[](const Environment& e){return make_array(e.rotation);})
        .def("density_kg_m3",[](const Environment& e,double altitude_m){
                 if (!e.atmosphere) throw py::value_error("environment has no atmospheric acquisition");
                 return e.density.density_kg_m3(altitude_m);
             },py::arg("altitude_m"),
             "Density in kg/m3 at an altitude above the equatorial radius.")
        .def("__repr__",[](const Environment& e){
             return std::string("leo_dynamics.Environment(sun=")+(e.sun?"SunEphemeris":"None")+
                    ", atmosphere="+(e.atmosphere?"ExponentialAtmosphere":"None")+
                    ", eclipse=\""+eclipse_model_name(e.eclipse)+"\")";});

    py::class_<IntegrationOptions>(m,"SolverOptions",
            "Adaptive Dopri5 solver settings; the defaults are the qualified v0.1 settings.")
        .def(py::init([](double rtol,double position_atol_m,double velocity_atol_m_s,double quaternion_atol,
                         double omega_atol_rad_s,double initial_step_s,double max_step_s,const py::object& attempts) {
                 const IntegrationOptions options{rtol,position_atol_m,velocity_atol_m_s,quaternion_atol,
                                                 omega_atol_rad_s,initial_step_s,max_step_s,attempt_limit(attempts)};
                 validate_options(options);
                 return options;
             }),py::kw_only(),py::arg("rtol")=1e-10,py::arg("position_atol_m")=1e-3,
             py::arg("velocity_atol_m_s")=1e-6,py::arg("quaternion_atol")=1e-12,
             py::arg("omega_atol_rad_s")=1e-12,py::arg("initial_step_s")=1.0,py::arg("max_step_s")=15.0,
             py::arg("max_attempts")=1000000)
        .def_property_readonly("rtol",[](const IntegrationOptions& o){return o.rtol;})
        .def_property_readonly("position_atol_m",[](const IntegrationOptions& o){return o.position_atol_m;})
        .def_property_readonly("velocity_atol_m_s",[](const IntegrationOptions& o){return o.velocity_atol_m_s;})
        .def_property_readonly("quaternion_atol",[](const IntegrationOptions& o){return o.quaternion_atol;})
        .def_property_readonly("omega_atol_rad_s",[](const IntegrationOptions& o){return o.omega_atol_rad_s;})
        .def_property_readonly("initial_step_s",[](const IntegrationOptions& o){return o.initial_step_s;})
        .def_property_readonly("max_step_s",[](const IntegrationOptions& o){return o.max_step_s;})
        .def_property_readonly("max_attempts",[](const IntegrationOptions& o){return o.max_attempts;})
        .def("__repr__",[](const IntegrationOptions& o){return "leo_dynamics.SolverOptions(rtol="+format_number(o.rtol)+
             ", position_atol_m="+format_number(o.position_atol_m)+", velocity_atol_m_s="+format_number(o.velocity_atol_m_s)+
             ", quaternion_atol="+format_number(o.quaternion_atol)+", omega_atol_rad_s="+format_number(o.omega_atol_rad_s)+
             ", initial_step_s="+format_number(o.initial_step_s)+", max_step_s="+format_number(o.max_step_s)+
             ", max_attempts="+std::to_string(o.max_attempts)+")";});
}
} // namespace leo::python