// src/libcosim_wrapper.cpp
#include "fmi_bridge/libcosim_wrapper.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>

namespace fmi_bridge {

struct LibcosimWrapper::Impl {
    double step_size;
    double current_time = 0.0;
    std::string loaded_fmu_path;
    bool loaded = false;
    std::vector<FmuVariableInfo> variables;
    std::unordered_map<std::string, int> name_to_vr;
    std::unordered_map<int, double> vr_to_value;
};

LibcosimWrapper::LibcosimWrapper(double step_size)
    : impl_(std::make_unique<Impl>()) {
    impl_->step_size = step_size;
}

LibcosimWrapper::~LibcosimWrapper() = default;

void LibcosimWrapper::load_fmu(const std::string& fmu_path,
                                const std::string& instance_name) {
    (void)instance_name;
    std::ifstream fmu_file(fmu_path);
    if (!fmu_file.good()) {
        throw std::runtime_error("FMU file not found: " + fmu_path);
    }
    fmu_file.close();

    char tmpdir[] = "/tmp/fmu_load_XXXXXX";
    char* created = mkdtemp(tmpdir);
    if (!created) {
        throw std::runtime_error("Failed to create temp directory for FMU extraction");
    }
    std::string tmpdir_str(created);

    std::string cmd = "unzip -o -q \"" + fmu_path
                      + "\" modelDescription.xml -d " + tmpdir_str;
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::system(("rm -rf " + tmpdir_str).c_str());
        throw std::runtime_error("Failed to unzip modelDescription.xml from " + fmu_path);
    }

    std::string xml_path = tmpdir_str + "/modelDescription.xml";
    std::ifstream xml_file(xml_path);
    if (!xml_file.good()) {
        std::system(("rm -rf " + tmpdir_str).c_str());
        throw std::runtime_error("modelDescription.xml not found in " + fmu_path);
    }

    std::stringstream buf;
    buf << xml_file.rdbuf();
    std::string xml_content = buf.str();
    xml_file.close();

    if (xml_content.find("fmiVersion=\"2.0\"") == std::string::npos) {
        std::system(("rm -rf " + tmpdir_str).c_str());
        throw std::runtime_error("Unsupported FMI version in " + fmu_path);
    }

    std::string model_name = "unknown";
    auto name_start = xml_content.find("modelName=\"");
    if (name_start != std::string::npos) {
        name_start += 11;
        auto name_end = xml_content.find("\"", name_start);
        model_name = xml_content.substr(name_start, name_end - name_start);
    }

    impl_->variables.clear();
    impl_->name_to_vr.clear();
    size_t pos = 0;
    while ((pos = xml_content.find("<ScalarVariable", pos)) != std::string::npos) {
        size_t end = xml_content.find("/>", pos);
        if (end == std::string::npos) end = xml_content.find("</ScalarVariable>", pos);
        if (end == std::string::npos) break;
        std::string var_xml = xml_content.substr(pos, end - pos);

        FmuVariableInfo var;
        auto n = var_xml.find("name=\"");
        if (n != std::string::npos) {
            n += 6;
            auto ne = var_xml.find("\"", n);
            var.name = var_xml.substr(n, ne - n);
        }
        auto vr = var_xml.find("valueReference=\"");
        if (vr != std::string::npos) {
            vr += 16;
            auto vre = var_xml.find("\"", vr);
            var.value_reference = std::stoi(var_xml.substr(vr, vre - vr));
        }
        var.causality = "local";
        if (var_xml.find("causality=\"input\"") != std::string::npos) var.causality = "input";
        if (var_xml.find("causality=\"output\"") != std::string::npos) var.causality = "output";
        if (var_xml.find("causality=\"parameter\"") != std::string::npos) var.causality = "parameter";

        impl_->variables.push_back(var);
        impl_->name_to_vr[var.name] = var.value_reference;
        impl_->vr_to_value[var.value_reference] = 0.0;
        pos = end + 1;
    }

    std::system(("rm -rf " + tmpdir_str).c_str());

    impl_->loaded_fmu_path = fmu_path;
    impl_->loaded = true;
}

bool LibcosimWrapper::is_loaded() const {
    return impl_->loaded;
}

void LibcosimWrapper::run_for(double duration_s) {
    int steps = static_cast<int>(duration_s / impl_->step_size);
    for (int i = 0; i < steps; ++i) {
        impl_->current_time += impl_->step_size;
    }
}

double LibcosimWrapper::get_real(const std::string& instance,
                                  const std::string& var_name) {
    (void)instance;
    auto it = impl_->name_to_vr.find(var_name);
    if (it == impl_->name_to_vr.end()) {
        throw std::runtime_error("Variable not found: " + var_name);
    }
    auto vit = impl_->vr_to_value.find(it->second);
    return vit != impl_->vr_to_value.end() ? vit->second : 0.0;
}

void LibcosimWrapper::set_real(const std::string& instance,
                                const std::string& var_name, double value) {
    (void)instance;
    auto it = impl_->name_to_vr.find(var_name);
    if (it == impl_->name_to_vr.end()) {
        throw std::runtime_error("Variable not found: " + var_name);
    }
    impl_->vr_to_value[it->second] = value;
}

std::vector<std::string> LibcosimWrapper::get_variable_names() const {
    std::vector<std::string> names;
    for (const auto& v : impl_->variables) {
        names.push_back(v.name);
    }
    return names;
}

const std::string& LibcosimWrapper::loaded_fmu_path() const {
    return impl_->loaded_fmu_path;
}

}
