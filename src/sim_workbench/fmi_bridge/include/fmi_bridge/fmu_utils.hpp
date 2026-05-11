#pragma once
#include <string>
namespace fmi_bridge {
bool validate_model_description(const std::string& xml_path);
std::string generate_model_description(const std::string& fmu_spec_json);
}
