// SPDX-License-Identifier: Proprietary
// D1.3b pybind11 bindings — exposes FcbState, MmgParams, rk4_step to Python.
// Batch runner uses this to run MMG simulations without ROS2 overhead.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "fcb_simulator/rk4_integrator.hpp"
#include "fcb_simulator/types.hpp"

namespace py = pybind11;
using namespace fcb_sim;

PYBIND11_MODULE(fcb_sim_py, m) {
    m.doc() = "FCB MMG simulator Python binding (D1.3b). "
              "psi uses math convention (CCW from East, rad). "
              "Batch runner converts from nautical heading before calling rk4_step.";

    py::class_<FcbState>(m, "FcbState")
        .def(py::init<>())
        .def_readwrite("x",       &FcbState::x)
        .def_readwrite("y",       &FcbState::y)
        .def_readwrite("psi",     &FcbState::psi)
        .def_readwrite("u",       &FcbState::u)
        .def_readwrite("v",       &FcbState::v)
        .def_readwrite("r",       &FcbState::r)
        .def_readwrite("phi",     &FcbState::phi)
        .def_readwrite("phi_dot", &FcbState::phi_dot)
        .def("__repr__", [](const FcbState& s) {
            return "<FcbState x=" + std::to_string(s.x) +
                   " y=" + std::to_string(s.y) +
                   " psi=" + std::to_string(s.psi) +
                   " u=" + std::to_string(s.u) + ">";
        });

    py::class_<MmgParams>(m, "MmgParams")
        .def(py::init<>(),
             "Default FCB parameters (L=46m, Yasukawa & Yoshimura 2015 [R7]).");

    m.def("rk4_step",
          &fcb_sim::rk4_step,
          py::arg("state"),
          py::arg("delta_rad"),
          py::arg("n_rps"),
          py::arg("params"),
          py::arg("dt"),
          "Single RK4 step. Returns new FcbState. "
          "delta_rad: rudder angle (+ = stbd CW). "
          "n_rps: propeller rev/s (0 = stopped). "
          "dt: time step in seconds.");
}
