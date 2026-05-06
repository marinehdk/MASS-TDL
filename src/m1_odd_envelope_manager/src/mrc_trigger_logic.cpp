/// Implementation of MrcTriggerLogic (MRC selection).
///
/// PATH-S compliance:
///   - noexcept on all methods
///   - Functions <= 40 lines
///   - Cyclomatic complexity <= 8 per function
///   - No dynamic allocation

#include "m1_odd_envelope_manager/mrc_trigger_logic.hpp"

namespace mass_l3::m1 {
namespace {

/// Returns true if any MRC must be selected (M7 request or special state).
/// CC = 3
[[nodiscard]] bool mrc_needed(const MrcSelectionInputs& inputs) noexcept {
  if (inputs.m7_safety_mrc_required) {
    return true;
  }
  if (inputs.current_state == EnvelopeState::Out) {
    return true;
  }
  if (inputs.current_state == EnvelopeState::MrCActive) {
    return true;
  }
  return inputs.current_state == EnvelopeState::MrCPrep;
}

/// Try to honour M7's recommended MRM; returns nullopt if infeasible.
/// CC = 7
[[nodiscard]] std::optional<MrcSelection> try_m7_recommendation(
    const MrcSelectionInputs& inputs,
    const MrcTriggerLogic& logic) noexcept {
  switch (inputs.m7_recommended_mrm) {
    case MrcType::Moored:
      if (inputs.is_moored) {
        return MrcSelection{MrcType::Moored, 0.0, "MOORED (M7)"};
      }
      break;
    case MrcType::Anchor:
      if (logic.can_anchor(inputs.water_depth_m, inputs.in_anchorage_zone)) {
        return MrcSelection{MrcType::Anchor, 0.0, "ANCHOR (M7)"};
      }
      break;
    case MrcType::HeaveTo:
      if (logic.can_heave_to(inputs.sea_state_hs, inputs.wind_speed_kn)) {
        return MrcSelection{MrcType::HeaveTo, 2.0, "HEAVE_TO (M7)"};
      }
      break;
    case MrcType::Drift:
      return MrcSelection{MrcType::Drift, 4.0, "DRIFT (M7)"};
  }
  return std::nullopt;
}

/// Fallback priority: MOORED > ANCHOR > HEAVE_TO > DRIFT.
/// CC = 4
[[nodiscard]] MrcSelection select_by_priority(
    const MrcSelectionInputs& inputs,
    const MrcTriggerLogic& logic) noexcept {
  if (inputs.is_moored) {
    return MrcSelection{MrcType::Moored, 0.0, "MOORED"};
  }
  if (logic.can_anchor(inputs.water_depth_m, inputs.in_anchorage_zone)) {
    return MrcSelection{MrcType::Anchor, 0.0, "ANCHOR"};
  }
  if (logic.can_heave_to(inputs.sea_state_hs, inputs.wind_speed_kn)) {
    return MrcSelection{MrcType::HeaveTo, 2.0, "HEAVE_TO"};
  }
  return MrcSelection{MrcType::Drift, 4.0, "DRIFT"};
}

}  // anonymous namespace

// =============================================================================
// Public interface
// =============================================================================

tl::expected<MrcTriggerLogic, ErrorCode> MrcTriggerLogic::create(
    const MrcParams& params) noexcept {
  if (params.max_anchor_depth_m < 0.0 ||
      params.max_heave_to_sea_state_hs < 0.0 ||
      params.max_heave_to_wind_kn < 0.0) {
    return tl::unexpected(ErrorCode::ParameterOutOfRange);
  }
  return MrcTriggerLogic(params);
}

MrcTriggerLogic::MrcTriggerLogic(const MrcParams& params) noexcept
    : params_(params) {}

std::optional<MrcSelection> MrcTriggerLogic::select(
    const MrcSelectionInputs& inputs) const noexcept {
  if (!mrc_needed(inputs)) {
    return std::nullopt;
  }

  if (inputs.m7_safety_mrc_required) {
    auto m7_result = try_m7_recommendation(inputs, *this);
    if (m7_result.has_value()) {
      return m7_result;
    }
  }

  return select_by_priority(inputs, *this);
}

bool MrcTriggerLogic::can_anchor(double water_depth_m,
                                  bool in_anchorage_zone) const noexcept {
  return water_depth_m <= params_.max_anchor_depth_m && in_anchorage_zone;
}

bool MrcTriggerLogic::can_heave_to(double sea_state_hs,
                                    double wind_speed_kn) const noexcept {
  return sea_state_hs <= params_.max_heave_to_sea_state_hs &&
         wind_speed_kn <= params_.max_heave_to_wind_kn;
}

}  // namespace mass_l3::m1
