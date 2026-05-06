#pragma once

#include <cstdint>

#include <tl/expected.hpp>

#include "m1_odd_envelope_manager/error.hpp"
#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

/// E/T/H three-axis weighted conformance score calculator.
/// PATH-S: noexcept, no dynamic allocation, no exceptions.
class ConformanceScoreCalculator final {
 public:
  /// Factory method. Returns WeightsNotNormalized if weights do not sum to 1.0.
  [[nodiscard]] static tl::expected<ConformanceScoreCalculator, ErrorCode> create(
      const ScoreWeights& weights,
      const EScoreThresholds& e_thresholds,
      const TScoreThresholds& t_thresholds,
      const HScoreThresholds& h_thresholds) noexcept;

  /// Evaluate all three axes and produce the combined conformance score.
  [[nodiscard]] ScoreTriple compute(const ScoringInputs& inputs) const noexcept;

  /// Environment score: visibility_nm (nm) and sea_state_hs (Hs, metres).
  [[nodiscard]] double evaluate_e_score(double visibility_nm,
                                        double sea_state_hs) const noexcept;

  /// Task score: telemetry / health of GNSS, radar, comm, sensor.
  [[nodiscard]] double evaluate_t_score(const TScoreInputs& t_inputs) const noexcept;

  /// Human score: TMR availability and communication status.
  [[nodiscard]] double evaluate_h_score(const HScoreInputs& h_inputs) const noexcept;

 private:
  ConformanceScoreCalculator(const ScoreWeights& weights,
                             const EScoreThresholds& e_thresholds,
                             const TScoreThresholds& t_thresholds,
                             const HScoreThresholds& h_thresholds) noexcept;

  ScoreWeights weights_;
  EScoreThresholds e_thresholds_;
  TScoreThresholds t_thresholds_;
  HScoreThresholds h_thresholds_;
};

}  // namespace mass_l3::m1
