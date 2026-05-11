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

/// Apply stale-input degradation factor to score.
/// CC = 2
[[nodiscard]] double apply_stale_degradation(double score,
                                             const EventFlags& events,
                                             double factor) noexcept {
  if (events.m2_input_stale || events.m7_input_stale) {
    return score * factor;
  }
  return score;
}

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
// compute_next: pure transition logic, dispatch to per-state helpers.
// CC = 2 (base 1 + switch 1)
// ---------------------------------------------------------------------------
EnvelopeState OddStateMachine::compute_next(
    double eff_score,
    double tdl_s,
    double tmr_s,
    const EventFlags& events) const noexcept {
  switch (current_) {
    case EnvelopeState::In:
      return handle_in_state(eff_score, thresholds_.in_to_edge,
                             thresholds_.edge_to_out);
    case EnvelopeState::Edge:
      return handle_edge_state(eff_score, thresholds_.in_to_edge,
                               thresholds_.edge_to_out, tdl_s, tmr_s);
    case EnvelopeState::MrCPrep:
      return handle_mrc_prep_state(events.m7_safety_mrc_required);
    case EnvelopeState::MrCActive:
      return handle_mrc_active_state(events.m7_safety_critical, eff_score,
                                     thresholds_.in_to_edge);
    case EnvelopeState::Out:
      [[fallthrough]];
    case EnvelopeState::Overridden:
      return handle_out_state(eff_score, thresholds_.in_to_edge,
                              thresholds_.edge_to_out);
  }
  // All enum values covered; unreachable.
  return current_;
}

// ---------------------------------------------------------------------------
// step: main entry point for state transitions.
// CC = 6 (base 1 + override-if 2 + stale-if 2 + transition-if 1)
// ---------------------------------------------------------------------------
EnvelopeState OddStateMachine::step(double score,  // NOLINT(bugprone-easily-swappable-parameters)
                                    double tdl_s,
                                    double tmr_s,
                                    EventFlags events,
                                    TimePoint now) noexcept {
  // --- Check override (highest priority) ---
  if (events.override_active || events.reflex_activation) {
    if (current_ != EnvelopeState::Overridden) {
      current_ = EnvelopeState::Overridden;
      last_transition_at_ = now;
      current_rationale_ = "Override or reflex arc activated (OVERRIDDEN)";
    }
    return current_;
  }

  // --- NaN score safety check (IEC 61508-3 Table C.1) ---
  // NaN causes all comparisons to return false, which silently stays In.
  // Guard: any NaN score forces immediate Out in the current cycle.
  if (std::isnan(score)) {
    if (current_ != EnvelopeState::Out) {
      current_ = EnvelopeState::Out;
      last_transition_at_ = now;
      current_rationale_ = "NaN score detected — immediate OUT (IEC 61508 guard)";
    }
    return current_;
  }

  // --- Apply stale input degradation ---
  const double kEffScore = apply_stale_degradation(score, events,
                                              thresholds_.stale_degradation_factor);

  // --- Compute next state ---
  const EnvelopeState kNext = compute_next(kEffScore, tdl_s, tmr_s, events);

  // --- Update state on transition ---
  if (kNext != current_) {
    current_ = kNext;
    last_transition_at_ = now;
    current_rationale_ = detail::rationale_for_state(kNext);
  }

  return current_;
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
