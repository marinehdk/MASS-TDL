#pragma once
#include <memory>
#include <string>
namespace fmi_bridge {
class LibcosimWrapper {
public:
    explicit LibcosimWrapper(double step_size = 0.02);
    ~LibcosimWrapper();
    void load_fmu(const std::string& fmu_path, const std::string& instance_name);
    void run_for(double duration_s);
    double get_real(const std::string& instance, const std::string& var_name);
    void set_real(const std::string& instance, const std::string& var_name, double value);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}
