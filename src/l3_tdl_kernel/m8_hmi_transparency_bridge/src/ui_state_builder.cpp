// src/ui_state_builder.cpp
#include "m8_hmi_transparency_bridge/ui_state_builder.hpp"

#include <cstdint>

namespace mass_l3::m8 {

// ---------------------------------------------------------------------------
// build — main entry
// ---------------------------------------------------------------------------

l3_msgs::msg::UIState UiStateBuilder::build(
    const BuildContext& ctx,
    const SatAggregator& sat_cache) const
{
  // TODO(Phase-E2): populate SAT display duration fields from sat_cache when UIState.msg is extended
  (void)sat_cache;

  l3_msgs::msg::UIState msg{};
  // stamp left at zero — node will stamp before publishing

  // auto_level_text: zone_reason + scenario
  if (ctx.odd.has_value()) {
    msg.auto_level_text = ctx.odd->zone_reason + " | " + scenario_to_string(ctx.scenario);
  } else {
    msg.auto_level_text = scenario_to_string(ctx.scenario);
  }

  // alert counts — populate from latest_alert
  msg.active_alert_count = 0U;
  msg.critical_alert_count = 0U;
  if (ctx.latest_alert.has_value()) {
    msg.active_alert_count = 1U;
    if (ctx.latest_alert->severity >= l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL) {
      msg.critical_alert_count = 1U;
    }
  }

  // confidence: from behavior plan if available, else 0
  msg.confidence = ctx.behavior.has_value() ? ctx.behavior->confidence : 0.0F;

  // rationale: build from scenario + tor state + m7 alert text
  msg.rationale = build_rationale(ctx);

  // view_mode: based on scenario
  msg.view_mode = scenario_to_view_mode(ctx.scenario);

  // ship position/heading/sog remain 0.0 — populated externally from nav filter

  apply_role_scenario_filter(ctx.role, ctx.scenario, msg);
  return msg;
}

// ---------------------------------------------------------------------------
// apply_role_scenario_filter — v1.1.2 §12.3 + detailed design Appendix A
// ---------------------------------------------------------------------------

void UiStateBuilder::apply_role_scenario_filter(
    Role role, Scenario scenario, l3_msgs::msg::UIState& msg) const
{
  const bool is_mrc = (scenario == Scenario::kMrcPreparation
                       || scenario == Scenario::kMrcActive);

  // MRC scenarios always show full detail regardless of role
  if (is_mrc) {
    return;
  }

  // Ship captain gets simplified view: SAT-2/3 detail hidden; alert count suppressed
  // (captain handles helm; ROC handles SAT monitoring)
  if (role == Role::kShipCaptain) {
    msg.active_alert_count = 0U;
    msg.critical_alert_count = 0U;
  }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::string UiStateBuilder::scenario_to_string(Scenario s)
{
  switch (s) {
    case Scenario::kTransit:            return "transit";
    case Scenario::kColregAvoidance:    return "colreg_avoidance";
    case Scenario::kMrcPreparation:     return "mrc_preparation";
    case Scenario::kMrcActive:          return "mrc_active";
    case Scenario::kOverrideActive:     return "override_active";
    case Scenario::kHandbackPreparation:return "handback_preparation";
    default:                            return "unknown";
  }
}

std::string UiStateBuilder::scenario_to_view_mode(Scenario s)
{
  switch (s) {
    case Scenario::kTransit:
    case Scenario::kHandbackPreparation:
      return "dashboard";
    case Scenario::kColregAvoidance:
    case Scenario::kMrcPreparation:
    case Scenario::kMrcActive:
      return "radar";
    case Scenario::kOverrideActive:
      return "chart";
    default:
      return "dashboard";
  }
}

std::string UiStateBuilder::build_rationale(const BuildContext& ctx)
{
  std::string r = "scenario=" + scenario_to_string(ctx.scenario);

  // ToR state annotation
  switch (ctx.tor_state) {
    case TorProtocol::State::kRequested:
      r += " tor=requested";
      break;
    case TorProtocol::State::kAcknowledged:
      r += " tor=acknowledged";
      break;
    case TorProtocol::State::kTimeoutMrc:
      r += " tor=timeout_mrc";
      break;
    default:
      break;
  }

  // Override annotation
  if (ctx.override_active) {
    r += " override=active";
  }

  // M7 degradation alert text
  if (ctx.m7_degradation_alert_active && !ctx.m7_degradation_alert_text.empty()) {
    r += " m7=" + ctx.m7_degradation_alert_text;
  }

  return r;
}

}  // namespace mass_l3::m8
