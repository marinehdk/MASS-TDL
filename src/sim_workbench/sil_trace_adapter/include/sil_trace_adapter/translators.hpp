#ifndef SIL_TRACE_ADAPTER_TRANSLATORS_HPP_
#define SIL_TRACE_ADAPTER_TRANSLATORS_HPP_
// sil_trace_adapter translators — pure field-maps for the SIL-facing decision
// trace. No ROS node state, so the maps are unit-testable without an executor.
//
// Mapping (mirrors the prior sil_topic_bridge.py trace callbacks):
//   l3_msgs/ASDRRecord -> sil_msgs/ASDREvent
//     (decision_type -> event_type, source_module -> rule_ref,
//      rationale[:64] -> decision_id, decision_json -> payload_json,
//      verdict = 0 PASS default; signature NOT carried — ASDREvent is the
//      downstream audit sink, not the tamper-proof channel)
//
// UIState is a pure passthrough (same type in/out), so no translator function
// is needed — the node republishes the message unchanged.
#include "l3_msgs/msg/asdr_record.hpp"
#include "sil_msgs/msg/asdr_event.hpp"

namespace sil_trace_adapter {

// ASDRRecord (L3 internal decision audit) -> ASDREvent (SIL-facing sink).
// Matches the bridge's _on_asdr_record: decision_id is rationale truncated to
// 64 chars (the ASDREvent field cap), verdict defaults to PASS (0).
sil_msgs::msg::ASDREvent asdr_record_to_event(
    const l3_msgs::msg::ASDRRecord& rec);

}  // namespace sil_trace_adapter

#endif  // SIL_TRACE_ADAPTER_TRANSLATORS_HPP_
