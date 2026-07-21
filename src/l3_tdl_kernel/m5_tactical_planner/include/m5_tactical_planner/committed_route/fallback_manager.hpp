#ifndef MASS_L3_M5_COMMITTED_ROUTE_FALLBACK_MANAGER_HPP_
#define MASS_L3_M5_COMMITTED_ROUTE_FALLBACK_MANAGER_HPP_

#include <cstdint>
#include <string>

namespace mass_l3::m5::committed_route {

// P6: FinalDegrade evidence captured at the moment the dual-condition
// (Condition A: BC override_no_improve >= 5 && Condition B: Mid unrecovered >= 3)
// is met and FinalDegrade is entered. This struct is the payload for the
// ASDR audit record so the trigger conditions are recoverable from
// /l3/asdr/record alone.
struct FinalDegradeEvidence {
  bool triggered{false};
  double timestamp_s{0.0};
  std::uint32_t bc_override_no_improve_count{0U};
  std::uint32_t mid_unrecovered_count{0U};
  double bc_worst_case_cpa_m{1.0e9};
  bool mid_last_converged{true};
  std::string safety_concern_event;
  std::string lifecycle_state;
  std::string suggested_action;
};

// Minimal FallbackManager (spec SS5.4).
//
// Responsibilities:
//   - Track FinalDegrade -> MRM state transitions.
//   - Hold the evidence needed to emit an ASDR audit record when M5 enters
//     FinalDegrade (BC failed + Mid unrecovered).
//   - Provide query interfaces so callers (mid_mpc_node, tests) can assert
//     whether an MRM has been suggested.
//
// This class does NOT emit MRM commands; M7 is the sole MRM authority (MUST-9).
// It does NOT hold the last-committed route or compute conservative corridors
// -- those responsibilities are distributed across CommittedAvoidanceRoute
// (keep-last) and the geometric-fallback path.
class FallbackManager {
 public:
  FallbackManager() = default;

  // Record a FinalDegrade transition with the dual-condition trigger data.
  // Called from mid_mpc_node when should_enter_final_degrade() returns true
  // and enter_final_degrade() has been invoked.
  void record_final_degrade(
      std::uint32_t bc_override_no_improve_count,
      std::uint32_t mid_unrecovered_count,
      double bc_worst_case_cpa_m,
      bool mid_last_converged,
      std::string safety_concern_event,
      std::string lifecycle_state,
      std::string suggested_action,
      double timestamp_s);

  // True if this manager has recorded a FinalDegrade -> MRM suggestion.
  [[nodiscard]] bool mrm_suggested() const noexcept {
    return evidence_.triggered;
  }

  // Immutable reference to the last recorded evidence for ASDR emission.
  [[nodiscard]] const FinalDegradeEvidence& evidence() const noexcept {
    return evidence_;
  }

  // Reset evidence (used when scenario changes).
  void reset() noexcept { evidence_ = FinalDegradeEvidence{}; }

 private:
  FinalDegradeEvidence evidence_;
};

}  // namespace mass_l3::m5::committed_route

#endif  // MASS_L3_M5_COMMITTED_ROUTE_FALLBACK_MANAGER_HPP_
