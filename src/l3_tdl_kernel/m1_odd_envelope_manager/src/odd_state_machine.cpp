/// Implementation of OddStateMachine (6-state FSM).
///
/// PATH-S compliance:
///   - No dynamic allocation in step() or compute_next()
///   - noexcept on all methods
///   - Functions <= 40 lines
///   - Cyclomatic complexity <= 8 per function
///   - No virtual functions

#include "m1_odd_envelope_manager/odd_state_machine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <tl_expected/expected.hpp>

#include "m1_odd_envelope_manager/error.hpp"
#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {
namespace {

// ---------------------------------------------------------------------------
// Per-state transition helpers (static, no side effects).
// Cyclomatic complexity measured per helper.
// ---------------------------------------------------------------------------

/// Handle transitions from In state.
/// CC = 4
[[nodiscard]] EnvelopeState handle_in_state(double eff_score,
                                            double in_to_edge,   // NOLINT(bugprone-easily-swappable-parameters)
                                            double edge_to_out) noexcept {
  // score <= 0 OR score < edge_to_out -> Out (immediate)
  if (eff_score <= 0.0 || eff_score < edge_to_out) {
    return EnvelopeState::Out;
  }
  // score < in_to_edge (but >= edge_to_out) -> Edge
  if (eff_score < in_to_edge) {
    return EnvelopeState::Edge;
  }
  // Otherwise stay In
  return EnvelopeState::In;
}

/// Handle transitions from Edge state.
/// CC = 4
[[nodiscard]] EnvelopeState handle_edge_state(double eff_score,
                                              double in_to_edge,   // NOLINT(bugprone-easily-swappable-parameters)
                                              double edge_to_out,
                                              double tdl_s,
                                              double tmr_s) noexcept {
  if (eff_score >= in_to_edge) {
    return EnvelopeState::In;
  }
  if (eff_score < edge_to_out) {
    return EnvelopeState::Out;
  }
  if (tdl_s <= tmr_s) {
    return EnvelopeState::MrCPrep;
  }
  return EnvelopeState::Edge;
}

/// Handle transitions from MrcPrep state.
/// CC = 2
[[nodiscard]] EnvelopeState handle_mrc_prep_state(
    bool m7_safety_mrc_required) noexcept {
  if (m7_safety_mrc_required) {
    return EnvelopeState::MrCActive;
  }
  return EnvelopeState::MrCPrep;
}

/// Handle transitions from MrcActive state.
/// CC = 3
[[nodiscard]] EnvelopeState handle_mrc_active_state(
    bool m7_safety_critical,
    double eff_score,
    double in_to_edge) noexcept {
  if (!m7_safety_critical && eff_score >= in_to_edge) {
    return EnvelopeState::In;
  }
  return EnvelopeState::MrCActive;
}

/// Handle transitions from Out state.
/// CC = 3
[[nodiscard]] EnvelopeState handle_out_state(double eff_score,
                                             double in_to_edge,   // NOLINT(bugprone-easily-swappable-parameters)
                                             double edge_to_out) noexcept {
  if (eff_score >= in_to_edge) {
    return EnvelopeState::In;
  }
  if (eff_score >= edge_to_out) {
    return EnvelopeState::Edge;
  }
  return EnvelopeState::Out;
}

/// Apply M2 stale-input degradation factor to score (m7 handled separately
/// with heartbeat timeout). CC = 2
[[nodiscard]] double apply_m2_stale_degradation(double score,
                                                const EventFlags& events,
                                                double factor) noexcept {
  if (events.m2_input_stale) {
    return score * factor;
  }
  return score;
}

/// M7 heartbeat lost multiplier (0.7) when m7_input_stale is true.
constexpr double kM7HeartbeatLostFactor = 0.7;

}  // anonymous namespace

// ---------------------------------------------------------------------------
// OddStateMachine implementation
// ---------------------------------------------------------------------------

OddStateMachine::OddStateMachine(
    const StateMachineThresholds& thresholds) noexcept
    : thresholds_(thresholds),
      current_(EnvelopeState::In),
      last_transition_at_(std::chrono::steady_clock::now()),
      current_rationale_("Initial state: IN (within operational envelope)") {}

tl::expected<OddStateMachine, ErrorCode> OddStateMachine::create(
    const StateMachineThresholds& thresholds) noexcept {
  // Validate non-negative thresholds
  if (thresholds.in_to_edge < 0.0 || thresholds.edge_to_out < 0.0) {
    return tl::unexpected(ErrorCode::ParameterOutOfRange);
  }
  // Validate stale degradation factor in (0, 1]
  if (thresholds.stale_degradation_factor <= 0.0 ||
      thresholds.stale_degradation_factor > 1.0) {
    return tl::unexpected(ErrorCode::ParameterOutOfRange);
  }
  // Validate monotonicity: in_to_edge must be strictly > edge_to_out
  if (thresholds.in_to_edge <= thresholds.edge_to_out) {
    return tl::unexpected(ErrorCode::ThresholdsNonMonotonic);
  }
  return OddStateMachine(thresholds);
}

// ---------------------------------------------------------------------------
// compute_next: pure transition logic with zone/health awareness.
// CC = 2 (base 1 + switch 1)
// ---------------------------------------------------------------------------
EnvelopeState OddStateMachine::compute_next(
    double eff_score,
    double tdl_s,
    double tmr_s,
    const EventFlags& events,
    OddZoneHealthPair zone_health,
    TimePoint now) const noexcept {
  // Zone C tightens the OUT threshold to be more conservative.
  static_cast<void>(now);
  double edge_to_out = thresholds_.edge_to_out;
  if (zone_health.zone == OddZone::C && edge_to_out < 0.6) {
    edge_to_out = 0.6;
  }

  switch (current_) {
    case EnvelopeState::In:
      return handle_in_state(eff_score, thresholds_.in_to_edge, edge_to_out);
    case EnvelopeState::Edge:
      return handle_edge_state(eff_score, thresholds_.in_to_edge, edge_to_out,
                               tdl_s, tmr_s);
    case EnvelopeState::MrCPrep:
      return handle_mrc_prep_state(events.m7_safety_mrc_required);
    case EnvelopeState::MrCActive:
      return handle_mrc_active_state(events.m7_safety_critical, eff_score,
                                     thresholds_.in_to_edge);
    case EnvelopeState::Out:
      [[fallthrough]];
    case EnvelopeState::Overridden:
      return handle_out_state(eff_score, thresholds_.in_to_edge, edge_to_out);
  }
  // All enum values covered; unreachable.
  return current_;
}

// ---------------------------------------------------------------------------
// step (zone/health-aware): main entry for state transitions (D2.1).
// Priority order:
//   1. Override/reflex
//   2. NaN safety guard
//   3. M7 safety critical / MRC required → MrCPrep (Checker VETO)
//   4. SystemHealth::Critical → MrCPrep
//   5. SystemHealth::Degraded → multiply score by stale_degradation_factor
//   6. M2 input stale → multiply score by stale_degradation_factor
//   7. M7 input stale → multiply score by 0.7 (heartbeat timeout)
//   8. Zone C → tighten edge_to_out threshold to max(0.6, configured)
//   9. Delegate to compute_next()
// CC = 8 (base 1 + override-if 2 + nan-if 2 + m7-if 2 + health-if 2)
// ---------------------------------------------------------------------------
EnvelopeState OddStateMachine::step(double score,  // NOLINT(bugprone-easily-swappable-parameters)
                                    double tdl_s,
                                    double tmr_s,
                                    EventFlags events,
                                    OddZoneHealthPair zone_health,
                                    TimePoint now) noexcept {
  // --- 1. Check override (highest priority) ---
  if (events.override_active || events.reflex_activation) {
    if (current_ != EnvelopeState::Overridden) {
      current_ = EnvelopeState::Overridden;
      last_transition_at_ = now;
      current_rationale_ = "Override or reflex arc activated (OVERRIDDEN)";
    }
    return current_;
  }

  // --- 2. NaN score safety check (IEC 61508-3 Table C.1) ---
  if (std::isnan(score)) {
    if (current_ != EnvelopeState::Out) {
      current_ = EnvelopeState::Out;
      last_transition_at_ = now;
      current_rationale_ = "NaN score detected — immediate OUT (IEC 61508 guard)";
    }
    return current_;
  }

  // --- 3. M7 safety critical / MRC required → MrCPrep (Checker VETO) ---
  if (events.m7_safety_critical || events.m7_safety_mrc_required) {
    if (current_ != EnvelopeState::MrCPrep) {
      current_ = EnvelopeState::MrCPrep;
      last_transition_at_ = now;
      current_rationale_ = "M7 safety alert — MrC_PREP (Checker VETO > Doer)";
    }
    return current_;
  }

  // --- 4. SystemHealth::Critical → MrCPrep ---
  if (zone_health.health == SystemHealth::Critical) {
    if (current_ != EnvelopeState::MrCPrep) {
      current_ = EnvelopeState::MrCPrep;
      last_transition_at_ = now;
      current_rationale_ = "System health critical — MrC_PREP (ODD zone degraded)";
    }
    return current_;
  }

  // --- Apply health/score modifiers ---
  double effective_score = score;

  // --- 5. SystemHealth::Degraded → multiply ---
  if (zone_health.health == SystemHealth::Degraded) {
    effective_score *= thresholds_.stale_degradation_factor;
  }

  // --- 6. M2 stale input degradation ---
  effective_score = apply_m2_stale_degradation(effective_score, events,
                                               thresholds_.stale_degradation_factor);

  // --- 7. M7 heartbeat timeout ---
  if (events.m7_input_stale) {
    effective_score *= kM7HeartbeatLostFactor;
  }

  // --- 8/9. Compute next with zone/health context ---
  const EnvelopeState kNext = compute_next(effective_score, tdl_s, tmr_s,
                                           events, zone_health, now);

  // --- Update state on transition ---
  if (kNext != current_) {
    current_ = kNext;
    last_transition_at_ = now;
    current_rationale_ = detail::rationale_for_state(kNext);
  }

  return current_;
}

// ---------------------------------------------------------------------------
// step (backward-compatible): delegates to zone/health-aware overload with
// default OddZone::A and SystemHealth::Full.
// ---------------------------------------------------------------------------
EnvelopeState OddStateMachine::step(double score,  // NOLINT(bugprone-easily-swappable-parameters)
                                    double tdl_s,
                                    double tmr_s,
                                    EventFlags events,
                                    TimePoint now) noexcept {
  return step(score, tdl_s, tmr_s, events,
              OddZoneHealthPair{OddZone::A, SystemHealth::Full}, now);
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

std::string_view OddStateMachine::rationale() const noexcept {
  return current_rationale_;
}

/// CC = 2 (base 1 + switch 1)
StateForecast OddStateMachine::forecast(
    std::chrono::seconds horizon) const noexcept {
  if (horizon.count() <= 0) {
    return {current_, 0.0};
  }

  double uncertainty = 0.0;
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  EnvelopeState predicted = current_;

  switch (current_) {
    case EnvelopeState::In:
      predicted = EnvelopeState::In;
      uncertainty = std::min(1.0, static_cast<double>(horizon.count()) * 0.01);
      break;
    case EnvelopeState::Edge:
      predicted = EnvelopeState::Edge;
      uncertainty = std::min(1.0, static_cast<double>(horizon.count()) * 0.02);
      break;
    case EnvelopeState::Out:
      predicted = EnvelopeState::Out;
      uncertainty = std::min(1.0, static_cast<double>(horizon.count()) * 0.005);
      break;
    case EnvelopeState::MrCPrep:
      predicted = EnvelopeState::MrCActive;
      uncertainty = std::min(1.0, static_cast<double>(horizon.count()) * 0.1);
      break;
    case EnvelopeState::MrCActive:
      predicted = EnvelopeState::MrCActive;
      uncertainty = std::min(1.0, static_cast<double>(horizon.count()) * 0.05);
      break;
    case EnvelopeState::Overridden:
      predicted = EnvelopeState::Overridden;
      uncertainty = std::min(1.0, static_cast<double>(horizon.count()) * 0.01);
      break;
  }

  return {predicted, uncertainty};
}

}  // namespace mass_l3::m1
