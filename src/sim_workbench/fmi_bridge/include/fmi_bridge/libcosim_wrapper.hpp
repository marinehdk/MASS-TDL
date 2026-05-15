#pragma once
#include <memory>
#include <string>
#include <vector>

namespace fmi_bridge {

struct FmuVariableInfo {
    std::string name;
    int value_reference;
    std::string causality;  // "input" | "output" | "parameter" | "local"
};

class LibcosimWrapper {
public:
    explicit LibcosimWrapper(double step_size = 0.02);
    ~LibcosimWrapper();

    void load_fmu(const std::string& fmu_path, const std::string& instance_name);
    bool is_loaded() const;

    void run_for(double duration_s);

    double get_real(const std::string& instance, const std::string& var_name);
    void set_real(const std::string& instance, const std::string& var_name, double value);

    std::vector<std::string> get_variable_names() const;
    const std::string& loaded_fmu_path() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
