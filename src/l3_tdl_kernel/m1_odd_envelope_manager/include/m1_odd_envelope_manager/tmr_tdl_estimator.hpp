#pragma once

#include <chrono>
#include <cstdint>

#include <tl_expected/expected.hpp>

#include "m1_odd_envelope_manager/error.hpp"
#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

/// TMR (Maximum Operator Response Time) and TDL (Tactical Decision Latency)
/// estimator.
///
/// PATH-S: noexcept, no dynamic allocation, no exceptions.
class TmrTdlEstimator final {
 public:
  /// Factory method. Returns ParameterOutOfRange if any parameter is invalid.
  [[nodiscard]] static tl::expected<TmrTdlEstimator, ErrorCode> create(
      const TmrTdlParams& params) noexcept;

  /// Compute TMR/TDL from current system inputs.
  [[nodiscard]] TmrTdlPair compute(const TmrTdlInputs& inputs) const noexcept;

  /// Look up the ToR TMR for a given operator state from the parameter set.
  /// Returns params.tmr_baseline_s if the matrix entry's tmr_s is 0.0
  /// (fallback for unconfigured states).
  [[nodiscard]] static double lookup_tor_tmr(
      OperatorState op_state,
      const ParameterSet& params) noexcept;

  /// Compute TMR/TDL with operator-state-aware ToR matrix lookup (D2.1).
  /// Calls the existing compute(), then overrides TMR with the tor_matrix
  /// value for the given operator state when it is non-zero.
  [[nodiscard]] TmrTdlPair compute(const TmrTdlInputs& inputs,
                                   const ParameterSet& params,
                                   OperatorState op_state) const noexcept;

  /// Forecast TMR/TDL at a future horizon from current inputs.
  [[nodiscard]] TmrTdlPair forecast(const TmrTdlInputs& current,
                                    std::chrono::seconds horizon) const noexcept;

 private:
  explicit TmrTdlEstimator(const TmrTdlParams& params) noexcept;

  /// Estimate available communication time window from RTT.
  [[nodiscard]] double estimate_t_comm_ok(double current_rtt_s) const noexcept;

  /// Estimate available system health time window.
  [[nodiscard]] double estimate_t_sys_health(
      const SystemHealthSnapshot& health) const noexcept;

  TmrTdlParams params_;
};

}  // namespace mass_l3::m1
