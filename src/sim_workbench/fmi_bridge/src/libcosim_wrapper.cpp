// src/libcosim_wrapper.cpp
#include "fmi_bridge/libcosim_wrapper.hpp"
#include <fstream>
#include <stdexcept>

namespace fmi_bridge {

struct LibcosimWrapper::Impl {
    double step_size;
    double current_time = 0.0;
    std::string loaded_fmu_path;
};

LibcosimWrapper::LibcosimWrapper(double step_size)
    : impl_(std::make_unique<Impl>()) {
    impl_->step_size = step_size;
}

LibcosimWrapper::~LibcosimWrapper() = default;

void LibcosimWrapper::load_fmu(const std::string& fmu_path, const std::string& instance_name) {
    // Phase 1: verify .fmu file exists, validate modelDescription.xml inside
    (void)instance_name;
    std::ifstream fmu_file(fmu_path);
    if (!fmu_file.good()) {
        throw std::runtime_error("FMU file not found: " + fmu_path);
    }
    impl_->loaded_fmu_path = fmu_path;
}

void LibcosimWrapper::run_for(double duration_s) {
    int steps = static_cast<int>(duration_s / impl_->step_size);
    for (int i = 0; i < steps; ++i) {
        impl_->current_time += impl_->step_size;
    }
}

double LibcosimWrapper::get_real(const std::string& instance, const std::string& var_name) {
    (void)instance; (void)var_name;
    return 0.0;  // Phase 2: cosim::slave::get_real()
}

void LibcosimWrapper::set_real(const std::string& instance, const std::string& var_name, double value) {
    (void)instance; (void)var_name; (void)value;
    // Phase 2: cosim::slave::set_real()
}

}  // namespace fmi_bridge
