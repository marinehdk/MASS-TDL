#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(fcb_mmg_py, m) {
    m.doc() = "FCBSimulator MMG pybind11 bindings (D1.3c)";
    // TODO(D1.3c): expose fcb_simulator C++ API to Python
}
