#include "m7_safety_supervisor/mrm/mrm_selector.hpp"

#include <chrono>

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"

namespace mass_l3::m7::mrm {

namespace {

// ---------------------------------------------------------------------------
// raw_select_watchdog — watchdog sub-selection (split from raw_select for complexity)
// ---------------------------------------------------------------------------

MrmId raw_select_watchdog(ScenarioContext const& ctx) noexcept {
  if (ctx.watchdog.critical_count >= 3U) {
    return MrmId::kMrm02_Anchor;
  }
  if (ctx.watchdog.critical_count >= 1U && ctx.watchdog.critical_count <= 2U) {
    return MrmId::kMrm01_Drift;
  }
  return MrmId::kNone;
}

// ---------------------------------------------------------------------------
// compute_confidence — extracted from select() for complexity
// ---------------------------------------------------------------------------

float compute_confidence(bool lockout_active,
                         bool applicable,
                         float upgrade_threshold) noexcept {
  if (lockout_active) {
    return 0.5F;
  }
  return applicable ? upgrade_threshold : 0.5F;
}

}  // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MrmSelector::MrmSelector(Config const& cfg,
                          MrmCommandSet const& cmd_set,
                          l3_msgs::msg::ODDState const& odd_snapshot) noexcept
  : cfg_{cfg}
  , mrm01_{cmd_set.mrm01}
  , mrm02_{cmd_set.mrm02}
  , mrm03_{cmd_set.mrm03}
  , mrm04_{cmd_set.mrm04}
{
  // odd_snapshot accepted for API compatibility; not used beyond construction.
  (void)odd_snapshot;
}

// ---------------------------------------------------------------------------
// raw_select() — 8-entry priority decision table
// Priority (highest first): MRM-04, MRM-03, MRM-02 (3 conditions), MRM-01 (2 conditions)
// ---------------------------------------------------------------------------

MrmId MrmSelector::raw_select(ScenarioContext const& ctx,
                               l3_msgs::msg::ODDState const& odd) noexcept
{
  // 1. Harbor + >= 2 violations -> MRM-04 (highest priority)
  if (odd.current_zone == l3_msgs::msg::ODDState::ODD_ZONE_C &&
      ctx.assumption.total_violation_count >= 2U) {
    return MrmId::kMrm04_Mooring;
  }

  // 2. Multiple targets close -> MRM-03
  if (ctx.performance.multiple_targets_nearby) {
    return MrmId::kMrm03_HeaveTo;
  }

  // 3. Extreme: >= 3 violations -> MRM-02
  if (ctx.assumption.total_violation_count >= 3U) {
    return MrmId::kMrm02_Anchor;
  }

  // 4. CPA trend degrading -> MRM-02
  if (ctx.performance.cpa_trend_degrading) {
    return MrmId::kMrm02_Anchor;
  }

  // 5/6: Watchdog-based selection (extracted for complexity)
  MrmId const kWatchdogChoice = raw_select_watchdog(ctx);
  if (kWatchdogChoice != MrmId::kNone) {
    return kWatchdogChoice;
  }

  // 6. >= 2 SOTIF violations -> MRM-02
  if (ctx.assumption.total_violation_count >= 2U) {
    return MrmId::kMrm02_Anchor;
  }

  // 7. 1 SOTIF violation -> MRM-01
  if (ctx.assumption.total_violation_count >= 1U) {
    return MrmId::kMrm01_Drift;
  }

  // 8. Default: no MRM
  return MrmId::kNone;
}

// ---------------------------------------------------------------------------
// select() — apply 30s anti-oscillation lockout
// ---------------------------------------------------------------------------

MrmDecision MrmSelector::select(ScenarioContext const& ctx,
                                 l3_msgs::msg::ODDState const& odd,
                                 l3_msgs::msg::WorldState const& world,
                                 std::chrono::steady_clock::time_point now) noexcept
{
  MrmId const kCandidate = raw_select(ctx, odd);

  // Apply 30s lockout: if candidate differs from last and insufficient time
  // has passed, keep last. Skip lockout when last_id_ is kNone (first call).
  MrmId chosen = kCandidate;
  bool lockout_active = false;
  if (kCandidate != last_id_) {
    auto const kElapsed = now - last_change_;
    if (kElapsed < cfg_.change_lockout && last_id_ != MrmId::kNone) {
      chosen = last_id_;  // lockout: keep current
      lockout_active = true;
    } else {
      last_id_ = kCandidate;
      last_change_ = now;
    }
  }

  // Evaluate applicability of chosen MRM (for confidence)
  bool applicable = false;
  switch (chosen) {
    case MrmId::kMrm01_Drift:   applicable = mrm01_.is_applicable(odd, world); break;
    case MrmId::kMrm02_Anchor:  applicable = mrm02_.is_applicable(odd, world); break;
    case MrmId::kMrm03_HeaveTo: applicable = Mrm03HeaveTo::is_applicable(world); break;
    case MrmId::kMrm04_Mooring: applicable = mrm04_.is_applicable(odd, world); break;
    default: applicable = true; break;
  }

  MrmDecision dec;
  dec.mrm_id = chosen;
  dec.confidence = compute_confidence(lockout_active, applicable, cfg_.upgrade_confidence_threshold);
  dec.rationale = to_string(chosen);
  return dec;
}

// ---------------------------------------------------------------------------
// reset()
// ---------------------------------------------------------------------------

void MrmSelector::reset() noexcept {
  last_id_ = MrmId::kNone;
  last_change_ = {};
}

}  // namespace mass_l3::m7::mrm
