#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "fcb_simulator/fcb_simulator_plugin.hpp"
#include "ship_sim_interfaces/ship_motion_simulator.hpp"
#include "ship_sim_interfaces/ship_state.hpp"

namespace py = pybind11;

PYBIND11_MODULE(fcb_mmg_py, m) {
    m.doc() = "FCB 45m MMG 4-DOF ship dynamics (Yasukawa 2015)";

    py::class_<ship_sim::ShipState>(m, "ShipState")
        .def(py::init<>())
        .def_readwrite("x", &ship_sim::ShipState::x)
        .def_readwrite("y", &ship_sim::ShipState::y)
        .def_readwrite("psi", &ship_sim::ShipState::psi)
        .def_readwrite("u", &ship_sim::ShipState::u)
        .def_readwrite("v", &ship_sim::ShipState::v)
        .def_readwrite("r", &ship_sim::ShipState::r)
        .def_readwrite("phi", &ship_sim::ShipState::phi)
        .def_readwrite("phi_dot", &ship_sim::ShipState::phi_dot);

    py::class_<ship_sim::FmuVariableSpec>(m, "FmuVariableSpec")
        .def(py::init<>())
        .def_readwrite("name", &ship_sim::FmuVariableSpec::name)
        .def_readwrite("causality", &ship_sim::FmuVariableSpec::causality)
        .def_readwrite("variability", &ship_sim::FmuVariableSpec::variability)
        .def_readwrite("type", &ship_sim::FmuVariableSpec::type)
        .def_readwrite("unit", &ship_sim::FmuVariableSpec::unit)
        .def_readwrite("start", &ship_sim::FmuVariableSpec::start)
        .def_readwrite("description", &ship_sim::FmuVariableSpec::description);

    py::class_<ship_sim::FmuInterfaceSpec>(m, "FmuInterfaceSpec")
        .def(py::init<>())
        .def_readwrite("fmi_version", &ship_sim::FmuInterfaceSpec::fmi_version)
        .def_readwrite("model_name", &ship_sim::FmuInterfaceSpec::model_name)
        .def_readwrite("model_identifier", &ship_sim::FmuInterfaceSpec::model_identifier)
        .def_readwrite("default_step_size", &ship_sim::FmuInterfaceSpec::default_step_size)
        .def_readwrite("variables", &ship_sim::FmuInterfaceSpec::variables);

    py::class_<fcb_sim::FCBSimulator>(m, "FCBSimulator")
        .def(py::init<>())
        .def("load_params", &fcb_sim::FCBSimulator::load_params)
        .def("step", &fcb_sim::FCBSimulator::step,
             py::arg("state"), py::arg("delta_rad"), py::arg("n_rps"), py::arg("dt_s"))
        .def("vessel_class", &fcb_sim::FCBSimulator::vessel_class)
        .def("hull_class", &fcb_sim::FCBSimulator::hull_class)
        .def("export_fmu_interface", &fcb_sim::FCBSimulator::export_fmu_interface);
}
