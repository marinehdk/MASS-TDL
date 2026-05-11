#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

#include <tl_expected/expected.hpp>

#include "m1_odd_envelope_manager/error.hpp"
#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

/// 6-state FSM governing the operational envelope of the vessel.
///
/// States: In, Edge, Out, MRC_Prep, MRC_Active, Overridden.
///
/// PATH-S safety-critical constraints:
///   - No dynamic allocation in step() or compute_next()
///   - noexcept on all methods
///   - No virtual functions
///   - Functions <= 40 lines, cyclomatic complexity <= 8
///
/// Design authority: M1 ODD/Envelope Manager.
class OddStateMachine final {
 public:
  using TimePoint = std::chrono::steady_clock::time_point;

  /// Factory: creates an OddStateMachine in the In state.
  /// Returns an error if thresholds are invalid.
  static tl::expected<OddStateMachine, ErrorCode> create(
      const StateMachineThresholds& thresholds) noexcept;

  /// Step the state machine with current score and events.
  /// Returns the new EnvelopeState after the transition.
  EnvelopeState step(double score,
                     double tdl_s,
                     double tmr_s,
                     EventFlags events,
                     TimePoint now) noexcept;

  /// Current envelope state.
  [[nodiscard]] EnvelopeState current() const noexcept { return current_; }

  /// Human-readable rationale for the current state.
  [[nodiscard]] std::string_view rationale() const noexcept;

  /// Time point of the last state transition.
  [[nodiscard]] TimePoint last_transition_at() const noexcept {
    return last_transition_at_;
  }

  /// Project the state forward by a given horizon.
  [[nodiscard]] StateForecast forecast(std::chrono::seconds horizon) const noexcept;

 private:
  explicit OddStateMachine(const StateMachineThresholds& thresholds) noexcept;

  /// Pure transition logic (no side effects).
  EnvelopeState compute_next(double eff_score,
                             double tdl_s,
                             double tmr_s,
                             const EventFlags& events) const noexcept;

  StateMachineThresholds thresholds_;
  EnvelopeState current_;
  TimePoint last_transition_at_;
  std::string_view current_rationale_;
};

// ---------------------------------------------------------------------------
// Inline helper: set rationale on transition (PATH-S: no exceptions).
// ---------------------------------------------------------------------------
namespace detail {

[[nodiscard]] inline std::string_view rationale_for_state(EnvelopeState s) noexcept {
  using namespace std::string_view_literals;
  switch (s) {
    case EnvelopeState::In:
      return "Within operational envelope (IN)"sv;
    case EnvelopeState::Edge:
      return "Approaching envelope boundary (EDGE)"sv;
    case EnvelopeState::Out:
      return "Outside operational envelope (OUT)"sv;
    case EnvelopeState::MrCPrep:
      return "Preparing minimal risk condition (MRC_PREP)"sv;
    case EnvelopeState::MrCActive:
      return "Executing minimal risk condition (MRC_ACTIVE)"sv;
    case EnvelopeState::Overridden:
      return "Override or reflex arc active (OVERRIDDEN)"sv;
  }
  return "Unknown state"sv;
}

}  // namespace detail

}  // namespace mass_l3::m1
