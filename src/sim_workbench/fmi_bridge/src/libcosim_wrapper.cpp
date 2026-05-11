#include "fmi_bridge/libcosim_wrapper.hpp"

namespace fmi_bridge {

struct LibcosimWrapper::Impl {
    double step_size;
};

LibcosimWrapper::LibcosimWrapper(double step_size)
    : impl_(std::make_unique<Impl>()) {
    impl_->step_size = step_size;
}

LibcosimWrapper::~LibcosimWrapper() = default;

void LibcosimWrapper::load_fmu(const std::string& fmu_path, const std::string& instance_name) {
    (void)fmu_path;
    (void)instance_name;
    // TODO(D1.3c): delegate to libcosim::CoSimulation::load
}

void LibcosimWrapper::run_for(double duration_s) {
    (void)duration_s;
    // TODO(D1.3c): step libcosim slave for duration_s at step_size
}

double LibcosimWrapper::get_real(const std::string& instance, const std::string& var_name) {
    (void)instance;
    (void)var_name;
    // TODO(D1.3c): libcosim::VariableValue::as_real
    return 0.0;
}

void LibcosimWrapper::set_real(const std::string& instance, const std::string& var_name, double value) {
    (void)instance;
    (void)var_name;
    (void)value;
    // TODO(D1.3c): libcosim::Slave::set_real
}

} // namespace fmi_bridge
