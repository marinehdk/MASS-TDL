#include "m8_hmi_transparency_bridge/asdr_logger.hpp"

#include <string>
#include <nlohmann/json.hpp>

namespace mass_l3::m8 {

l3_msgs::msg::ASDRRecord AsdrLogger::build_record(
    const builtin_interfaces::msg::Time& stamp,
    std::string_view decision_type,
    std::string_view decision_json) const
{
    l3_msgs::msg::ASDRRecord msg{};
    msg.stamp = stamp;
    msg.source_module = "M8_HMI_Transparency_Bridge";
    msg.decision_type = std::string{decision_type};
    msg.decision_json = std::string{decision_json};
    auto digest = detail::sha256(msg.decision_json);
    msg.signature.assign(digest.begin(), digest.end());
    return msg;
}

l3_msgs::msg::ASDRRecord AsdrLogger::build_tor_acknowledgment_record(
    const builtin_interfaces::msg::Time& stamp,
    std::string_view operator_id,
    double sat1_display_duration_s,
    const std::string& odd_zone,
    float conformance_score) const
{
    nlohmann::json j;
    j["operator_id"]      = std::string{operator_id};
    j["sat1_duration_s"]  = sat1_display_duration_s;
    j["odd_zone"]         = odd_zone;
    j["conformance_score"] = static_cast<double>(conformance_score);
    return build_record(stamp, "tor_acknowledged", j.dump());
}

l3_msgs::msg::ASDRRecord AsdrLogger::build_ui_snapshot_record(
    const builtin_interfaces::msg::Time& stamp,
    std::string_view ui_state_summary) const
{
    nlohmann::json j;
    j["ui_state"] = std::string{ui_state_summary};
    return build_record(stamp, "ui_state_snapshot", j.dump());
}

}  // namespace mass_l3::m8
