#pragma once

#include <chrono>
#include <cstdint>

#include <tl/expected.hpp>

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
