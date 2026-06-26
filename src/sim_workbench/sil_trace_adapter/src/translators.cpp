// sil_trace_adapter translators — implementation of the ASDRRecord -> ASDREvent
// field-map declared in translators.hpp. Mirrors the prior sil_topic_bridge.py
// _on_asdr_record callback (signature NOT carried; ASDREvent is the audit sink,
// not the tamper-proof channel).
#include "sil_trace_adapter/translators.hpp"

#include <algorithm>
#include <string>

namespace sil_trace_adapter {

namespace {
constexpr std::size_t kDecisionIdMax = 64;
}

sil_msgs::msg::ASDREvent asdr_record_to_event(
    const l3_msgs::msg::ASDRRecord& rec) {
  sil_msgs::msg::ASDREvent out;
  out.stamp = rec.stamp;
  out.event_type = rec.decision_type;
  out.rule_ref = rec.source_module;
  // decision_id = rationale truncated to 64 chars (ASDREvent field cap).
  std::string decision_id = rec.rationale;
  if (decision_id.size() > kDecisionIdMax) {
    decision_id.resize(kDecisionIdMax);
  }
  out.decision_id = std::move(decision_id);
  out.verdict = 0;  // PASS default (downstream audit; verdict is set by consumers)
  out.payload_json = rec.decision_json;
  return out;
}

}  // namespace sil_trace_adapter
