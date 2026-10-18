#include "common.hpp"
PYBIND11_MODULE(_core,m) {
    m.doc()="Native LEO Dynamics v0.1 development interface";
    leo::python::bind_configuration(m);
    leo::python::bind_integration(m);
    leo::python::bind_analysis(m);
}
