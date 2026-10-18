#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include "leo/environment/source.hpp"
#include "leo/integration/options.hpp"
#include "leo/integration/trajectory.hpp"
#include <memory>
namespace py = pybind11;
namespace leo::python {
// Native-only immutable owners; no Python references cross a GIL-release boundary.
struct Model { bool j2=false; EnvironmentalModels models; };
struct Environment {
    std::shared_ptr<const EphemerisProvider> sun;
    bool atmosphere=false;
    ExponentialAtmosphere density;
    EclipseModel eclipse=EclipseModel::conical;
    InertialVector rotation{0,0,7.292115e-5};
    EarthEnvironment source(Earth earth, Epoch epoch) const {
        return EarthEnvironment(earth,epoch,sun.get(),atmosphere,eclipse,density,rotation);
    }
};
struct Context {
    Earth earth;
    Spacecraft spacecraft;
    Model model;
    Environment environment;
    Epoch epoch;
    EarthDynamics dynamics() const { return EarthDynamics(EarthGravity(earth,model.j2),spacecraft,model.models); }
    EarthEnvironment source() const { return environment.source(earth,epoch); }
};
struct Result {
    py::array states;
    py::array times;
    std::shared_ptr<const Context> context;
};
// Validates numerical dtypes and converts to contiguous float64; callers copy
// values into native storage under the GIL before any native execution.
py::array_t<double,py::array::c_style|py::array::forcecast> numeric(py::handle input, const char* name);
State state_from_array(py::handle input);
py::array state_array(const State& state);
void readonly(py::array& array);
void bind_configuration(py::module_& m);
void bind_integration(py::module_& m);
void bind_analysis(py::module_& m);
}
