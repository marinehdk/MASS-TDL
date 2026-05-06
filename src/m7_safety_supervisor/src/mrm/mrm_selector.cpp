#include "m7_safety_supervisor/mrm/mrm_selector.hpp"

#include <string>

namespace mass_l3::m7::mrm {

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
                               l3_msgs::msg::ODDState const& odd) const noexcept
{
  // 1. Harbor + >= 2 violations -> MRM-04 (highest priority)
  if (odd.current_zone == l3_msgs::msg::ODDState::ODD_ZONE_C &&
      ctx.assumption.total_violation_count >= 2u) {
    return MrmId::kMrm04_Mooring;
  }

  // 2. Multiple targets close -> MRM-03
  if (ctx.performance.multiple_targets_nearby) {
    return MrmId::kMrm03_HeaveTo;
  }

  // 3. Extreme: >= 3 violations -> MRM-02
  if (ctx.assumption.total_violation_count >= 3u) {
    return MrmId::kMrm02_Anchor;
  }

  // 4. CPA trend degrading -> MRM-02
  if (ctx.performance.cpa_trend_degrading) {
    return MrmId::kMrm02_Anchor;
  }

  // 5. >= 3 modules critical -> MRM-02
  if (ctx.watchdog.critical_count >= 3u) {
    return MrmId::kMrm02_Anchor;
  }

  // 6. >= 2 SOTIF violations -> MRM-02
  if (ctx.assumption.total_violation_count >= 2u) {
    return MrmId::kMrm02_Anchor;
  }

  // 7. 1-2 modules critical -> MRM-01
  if (ctx.watchdog.critical_count >= 1u && ctx.watchdog.critical_count <= 2u) {
    return MrmId::kMrm01_Drift;
  }

  // 8. 1 SOTIF violation -> MRM-01
  if (ctx.assumption.total_violation_count >= 1u) {
    return MrmId::kMrm01_Drift;
  }

  // 9. Default: no MRM
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
  MrmId const candidate = raw_select(ctx, odd);

  // Apply 30s lockout: if candidate differs from last and insufficient time
  // has passed, keep last. Skip lockout when last_id_ is kNone (first call).
  MrmId chosen = candidate;
  if (candidate != last_id_) {
    auto const elapsed = now - last_change_;
    if (elapsed < cfg_.change_lockout && last_id_ != MrmId::kNone) {
      chosen = last_id_;  // lockout: keep current
    } else {
      last_id_ = candidate;
      last_change_ = now;
    }
  }

  // Evaluate applicability of chosen MRM (for confidence)
  bool applicable = false;
  switch (chosen) {
    case MrmId::kMrm01_Drift:   applicable = mrm01_.is_applicable(odd, world); break;
    case MrmId::kMrm02_Anchor:  applicable = mrm02_.is_applicable(odd, world); break;
    case MrmId::kMrm03_HeaveTo: applicable = mrm03_.is_applicable(world); break;
    case MrmId::kMrm04_Mooring: applicable = mrm04_.is_applicable(odd, world); break;
    default: applicable = true; break;
  }

  MrmDecision dec;
  dec.mrm_id = chosen;
  dec.confidence = applicable ? cfg_.upgrade_confidence_threshold : 0.5F;
  dec.rationale = std::string{to_string(chosen)};
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
