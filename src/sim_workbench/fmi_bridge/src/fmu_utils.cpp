// src/fmu_utils.cpp
#include "fmi_bridge/fmu_utils.hpp"
#include <fstream>
#include <string>

namespace fmi_bridge {

bool validate_model_description(const std::string& xml_path) {
    std::ifstream file(xml_path);
    if (!file.good()) return false;
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return content.find("<fmiModelDescription") != std::string::npos &&
           content.find("fmiVersion=\"2.0\"") != std::string::npos;
}

std::string generate_model_description(const std::string& fmu_spec_json) {
    (void)fmu_spec_json;
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<fmiModelDescription fmiVersion=\"2.0\" modelName=\"FCBShipDynamics\" "
           "guid=\"{phase1-stub}\" generationTool=\"fmi_bridge v0.1.0\"/>";
}

}  // namespace fmi_bridge
