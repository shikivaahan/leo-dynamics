#include "configuration.hpp"
#include "leo/integration/batch.hpp"
#include "leo/integration/detail/packed_state.hpp"
#include <pybind11/eval.h>
#include <limits>
namespace leo::python {
namespace {
struct BatchResult {
    py::array states, times;
    std::vector<std::shared_ptr<const Context>> contexts;
    double epoch=0;
};
struct Evaluation {
    DynamicsContributions contributions;
    StateDerivative derivative;
};
py::array owned_array(std::vector<double>&& values, const std::vector<py::ssize_t>& shape) {
    auto owner=std::make_unique<std::vector<double>>(std::move(values));
    auto* pointer=owner.get();
    py::capsule base(pointer,[](void* p){delete static_cast<std::vector<double>*>(p);});
    owner.release();
    std::vector<py::ssize_t> strides(shape.size());
    py::ssize_t stride=sizeof(double);
    for(std::size_t i=shape.size();i-->0;) {strides[i]=stride;stride*=shape[i];}
    // NumPy accepts a non-null pointer even for empty arrays.
    static const double empty=0;
    py::array result(py::dtype::of<double>(),shape,strides,pointer->empty()?&empty:pointer->data(),base);
    readonly(result);
    return result;
}
py::array component(const py::array& source, py::ssize_t start, py::ssize_t count) {
    std::vector<py::ssize_t> shape(source.shape(),source.shape()+source.ndim());
    std::vector<py::ssize_t> strides(source.strides(),source.strides()+source.ndim());
    shape.back()=count;
    auto* data=static_cast<const double*>(source.data());
    if(source.size()) data+=start;
    py::array result(py::dtype::of<double>(),shape,strides,data,source);
    readonly(result);return result;
}
std::vector<double> schedule(py::handle value, double epoch, const IntegrationOptions& options) {
    auto array=numeric(value,"times_s");
    if(array.ndim()!=1) throw py::value_error("times_s must have shape (N,)");
    std::vector<double> times(array.data(),array.data()+array.size());
    validate_options(options);
    if(!std::isfinite(epoch)) throw py::value_error("epoch_et_s must be finite");
    if(times.empty()||times.front()!=0) throw py::value_error("times_s must start at exactly zero");
    for(std::size_t i=0;i<times.size();++i)
        if(!std::isfinite(times[i])||(i&&times[i]<=times[i-1]))
            throw py::value_error("times_s must be finite and strictly increasing");
    if(!std::isfinite(epoch+times.back())) throw py::value_error("absolute endpoint epoch must be finite");
    return times;
}
template<class T> T optional(py::handle value) {
    if(!value.is_none()) return py::cast<T>(value);
    if constexpr(std::is_default_constructible_v<T>) return T{};
    else throw py::value_error("configuration cannot be None");
}
template<class T> std::vector<T> broadcast(py::handle value,std::size_t count,const char* name,bool allow_none=false) {
    if(py::isinstance<T>(value)||(allow_none&&value.is_none())) return std::vector<T>(count,optional<T>(value));
    if(!py::isinstance<py::sequence>(value)) throw py::value_error(std::string(name)+" must be an object or sequence of batch length");
    auto seq=py::reinterpret_borrow<py::sequence>(value);
    if(static_cast<std::size_t>(seq.size())!=count) throw py::value_error(std::string(name)+" sequence length must equal batch size");
    std::vector<T> result;result.reserve(count);
    for(auto item:seq) {
        result.push_back(allow_none?optional<T>(item):py::cast<T>(item));
    }
    return result;
}
std::shared_ptr<const Context> context(Earth earth,Spacecraft spacecraft,Model model,Environment environment,double epoch) {
    auto result=std::make_shared<Context>(Context{earth,std::move(spacecraft),model,std::move(environment),{epoch}});
    result->source().validate_for(result->model.models);
    return result;
}
void coverage(const Context& c, const std::vector<double>& times) {
    if(c.environment.sun) {
        c.environment.sun->geometric_state(10,c.epoch);
        c.environment.sun->geometric_state(10,{c.epoch.et_seconds+times.back()});
    }
}
void pack_into(const Trajectory& trajectory, double* output) {
    for(const auto& sample:trajectory.samples) {
        const auto packed=detail::pack(sample.state);
        std::copy(packed.begin(),packed.end(),output);output+=13;
    }
}
py::object translated(std::exception_ptr cause) {
    try {std::rethrow_exception(cause);}
    catch(const std::out_of_range& e) {return py::module_::import("leo_dynamics._core").attr("DomainError")(e.what());}
    catch(const std::invalid_argument& e) {return py::reinterpret_borrow<py::object>(PyExc_ValueError)(e.what());}
    catch(const std::exception& e) {return py::reinterpret_borrow<py::object>(PyExc_RuntimeError)(e.what());}
    catch(...) {return py::reinterpret_borrow<py::object>(PyExc_RuntimeError)("unknown native error");}
}
}
void bind_integration(py::module_& m) {
    py::exec(R"(
class DomainError(ValueError):
    """Input outside the native supported domain or ephemeris coverage."""
class BatchPropagationError(RuntimeError):
    def __init__(self, trajectory_index, cause):
        super().__init__(f"batch trajectory {trajectory_index} failed: {cause}")
        self.trajectory_index = trajectory_index
        self.cause = cause
        self.__cause__ = cause
)",m.attr("__dict__"));
    py::register_exception_translator([](std::exception_ptr error) {
        try {if(error) std::rethrow_exception(error);}
        catch(const BatchPropagationError& e) {
            auto type=py::module_::import("leo_dynamics._core").attr("BatchPropagationError");
            auto instance=type(e.trajectory_index(),translated(e.cause()));
            PyErr_SetObject(type.ptr(),instance.ptr());
        }
        catch(const std::out_of_range& e) {
            auto type=py::module_::import("leo_dynamics._core").attr("DomainError");PyErr_SetString(type.ptr(),e.what());
        }
    });
    py::class_<Result>(m,"Trajectory")
        .def_property_readonly("states",[](const Result& r){return r.states;})
        .def_property_readonly("times_s",[](const Result& r){return r.times;})
        .def_property_readonly("epoch_et_s",[](const Result& r){return r.context->epoch.et_seconds;})
        .def_property_readonly("position_m",[](const Result& r){return component(r.states,0,3);})
        .def_property_readonly("velocity_m_s",[](const Result& r){return component(r.states,3,3);})
        .def_property_readonly("quaternion_wxyz",[](const Result& r){return component(r.states,6,4);})
        .def_property_readonly("omega_body_rad_s",[](const Result& r){return component(r.states,10,3);});
    py::class_<BatchResult>(m,"BatchTrajectory")
        .def("__len__",[](const BatchResult& r){return r.contexts.size();})
        .def("__getitem__",[](const BatchResult& r,py::ssize_t index) {
            auto count=static_cast<py::ssize_t>(r.contexts.size());if(index<0) index+=count;
            if(index<0||index>=count) throw py::index_error("batch trajectory index out of range");
            py::array view(py::dtype::of<double>(),{r.states.shape(1),py::ssize_t(13)},
                {r.states.strides(1),r.states.strides(2)},
                static_cast<const char*>(r.states.data())+index*r.states.strides(0),r.states);
            readonly(view);return Result{view,r.times,r.contexts[index]};
        })
        .def_property_readonly("states",[](const BatchResult& r){return r.states;})
        .def_property_readonly("times_s",[](const BatchResult& r){return r.times;})
        .def_property_readonly("epoch_et_s",[](const BatchResult& r){return r.epoch;})
        .def_property_readonly("position_m",[](const BatchResult& r){return component(r.states,0,3);})
        .def_property_readonly("velocity_m_s",[](const BatchResult& r){return component(r.states,3,3);})
        .def_property_readonly("quaternion_wxyz",[](const BatchResult& r){return component(r.states,6,4);})
        .def_property_readonly("omega_body_rad_s",[](const BatchResult& r){return component(r.states,10,3);});
    m.def("propagate",[](State initial,Spacecraft spacecraft,Model model,py::object times_s,py::object earth,py::object environment,double epoch,py::object options) {
        auto opts=optional<IntegrationOptions>(options);auto times=schedule(times_s,epoch,opts);
        auto c=context(optional<Earth>(earth),std::move(spacecraft),model,optional<Environment>(environment),epoch);
        Trajectory native;
        {py::gil_scoped_release release;coverage(*c,times);native=propagate(EnvironmentalDynamics(c->dynamics(),c->source()),initial,{epoch},times,opts);}
        std::vector<double> states(times.size()*13);pack_into(native,states.data());
        return Result{owned_array(std::move(states),{static_cast<py::ssize_t>(times.size()),13}),owned_array(std::move(times),{static_cast<py::ssize_t>(native.samples.size())}),std::move(c)};
    },py::kw_only(),py::arg("initial"),py::arg("spacecraft"),py::arg("model"),py::arg("times_s"),py::arg("earth")=py::none(),py::arg("environment")=py::none(),py::arg("epoch_et_s")=0.,py::arg("options")=py::none());
    m.def("propagate_batch",[](py::object initials,py::object spacecraft,py::object model,py::object times_s,py::object earth,py::object environment,double epoch,py::object options,py::object workers_object) {
        if(PyBool_Check(workers_object.ptr())||!PyIndex_Check(workers_object.ptr())) throw py::value_error("workers must be a positive integer");
        auto workers=PyLong_AsLongLong(workers_object.ptr());
        if(PyErr_Occurred()) {PyErr_Clear();throw py::value_error("workers is outside supported integer range");}
        if(workers<=0) throw py::value_error("workers must be a positive integer");
        auto opts=optional<IntegrationOptions>(options);auto times=schedule(times_s,epoch,opts);auto shared_earth=optional<Earth>(earth);
        std::vector<State> states;
        if(py::isinstance<py::array>(initials)) {
            auto a=numeric(initials,"initials");if(a.ndim()!=2||a.shape(1)!=13) throw py::value_error("initials must have shape (B, 13)");
            states.reserve(a.shape(0));
            for(py::ssize_t i=0;i<a.shape(0);++i) {
                detail::PackedState p;std::copy_n(a.data()+13*i,13,p.begin());states.push_back(validated_state(detail::unpack(p)));
            }
        } else {
            for(auto item:py::cast<py::sequence>(initials)) states.push_back(py::cast<State>(item));
        }
        auto crafts=broadcast<Spacecraft>(spacecraft,states.size(),"spacecraft");
        auto models=broadcast<Model>(model,states.size(),"model");
        auto environments=broadcast<Environment>(environment,states.size(),"environment",true);
        if(states.empty()&&(environment.is_none()||py::isinstance<Environment>(environment))) {
            // Broadcast configurations remain meaningful shared inputs at B=0.
            const auto shared_environment=optional<Environment>(environment);
            if(py::isinstance<Model>(model))
                shared_environment.source(shared_earth,{epoch}).validate_for(py::cast<Model>(model).models);
            if(shared_environment.sun) {
                shared_environment.sun->geometric_state(10,{epoch});
                shared_environment.sun->geometric_state(10,{epoch+times.back()});
            }
        }
        std::vector<std::shared_ptr<const Context>> contexts;contexts.reserve(states.size());
        for(std::size_t i=0;i<states.size();++i) contexts.push_back(context(shared_earth,std::move(crafts[i]),models[i],std::move(environments[i]),epoch));
        std::vector<Trajectory> native;
        {py::gil_scoped_release release;native=propagate_batch([&](std::size_t i,const State&) {
            coverage(*contexts[i],times);return EnvironmentalDynamics(contexts[i]->dynamics(),contexts[i]->source());
        },std::span<const State>(states),Epoch{epoch},std::span<const double>(times),opts,static_cast<std::size_t>(workers));}
        const auto b=static_cast<py::ssize_t>(states.size()),n=static_cast<py::ssize_t>(times.size());
        std::vector<double> packed(static_cast<std::size_t>(b)*n*13);
        for(std::size_t i=0;i<native.size();++i) pack_into(native[i],packed.data()+i*n*13);
        return BatchResult{owned_array(std::move(packed),{b,n,13}),owned_array(std::move(times),{n}),std::move(contexts),epoch};
    },py::kw_only(),py::arg("initials"),py::arg("spacecraft"),py::arg("model"),py::arg("times_s"),py::arg("earth")=py::none(),py::arg("environment")=py::none(),py::arg("epoch_et_s")=0.,py::arg("options")=py::none(),py::arg("workers")=1);
    auto vector_array=[](auto value) {
        std::vector<double> values{value.x(),value.y(),value.z()};
        return owned_array(std::move(values),{3});
    };
    py::class_<Wrench>(m,"Wrench")
        .def_property_readonly("force_n",[vector_array](const Wrench& w){return vector_array(w.force_n);})
        .def_property_readonly("torque_nm",[vector_array](const Wrench& w){return vector_array(w.torque_nm);});
    auto contributions=py::class_<DynamicsContributions>(m,"DynamicsContributions");
    contributions
        .def_property_readonly("point_mass_m_s2",[vector_array](const DynamicsContributions& c){return vector_array(c.point_mass_m_s2);})
        .def_property_readonly("j2_m_s2",[vector_array](const DynamicsContributions& c){return vector_array(c.j2_m_s2);})
        .def_property_readonly("gravity_gradient_nm",[vector_array](const DynamicsContributions& c){return vector_array(c.gravity_gradient_nm);})
        .def_readonly("srp",&DynamicsContributions::srp)
        .def_readonly("drag",&DynamicsContributions::drag)
        .def_readonly("albedo",&DynamicsContributions::albedo)
        .def_readonly("non_gravitational",&DynamicsContributions::non_gravitational)
        .def_property_readonly("total_acceleration_m_s2",[vector_array](const DynamicsContributions& c){return vector_array(c.total_acceleration_m_s2);})
        .def_property_readonly("total_angular_acceleration_rad_s2",[vector_array](const DynamicsContributions& c){return vector_array(c.total_angular_acceleration_rad_s2);});
    py::class_<Evaluation>(m,"DynamicsEvaluation")
        .def_readonly("contributions",&Evaluation::contributions)
        .def_property_readonly("state_derivative",[](const Evaluation& e){
            auto packed=detail::pack(e.derivative);
            return owned_array(std::vector<double>(packed.begin(),packed.end()),{13});
        });
    // Expose contribution attributes directly as well as through .contributions.
    for(const char* name:{"point_mass_m_s2","j2_m_s2","gravity_gradient_nm","srp","drag","albedo","non_gravitational","total_acceleration_m_s2","total_angular_acceleration_rad_s2"}) {
        py::object property=py::module_::import("builtins").attr("property");
        m.attr("DynamicsEvaluation").attr(name)=property(py::cpp_function([key=std::string(name)](const Evaluation& e) -> py::object {
            return py::cast(e.contributions).attr(key.c_str());
        }));
    }
    m.def("evaluate_dynamics",[](State state,double time,Spacecraft spacecraft,Model model,py::object earth,py::object environment,double epoch) {
        if(!std::isfinite(time)||!std::isfinite(epoch)||!std::isfinite(time+epoch))
            throw py::value_error("time_s and epoch_et_s must be finite");
        auto c=context(optional<Earth>(earth),std::move(spacecraft),model,optional<Environment>(environment),epoch);
        auto dynamics=c->dynamics();auto snapshot=c->source()({time},state);
        auto loads=dynamics.evaluate({time},state,snapshot);
        return Evaluation{loads,{state.v,loads.total_acceleration_m_s2,
            quaternion_derivative(state.q,state.omega),loads.total_angular_acceleration_rad_s2}};
    },py::kw_only(),py::arg("state"),py::arg("time_s"),py::arg("spacecraft"),py::arg("model"),
      py::arg("earth")=py::none(),py::arg("environment")=py::none(),py::arg("epoch_et_s")=0.);
}
}
