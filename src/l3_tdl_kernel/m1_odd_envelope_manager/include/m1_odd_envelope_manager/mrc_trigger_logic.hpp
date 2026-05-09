#pragma once

#include <cstdint>
#include <optional>

#include <tl_expected/expected.hpp>

#include "m1_odd_envelope_manager/error.hpp"
#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

/// MRC (Minimum Risk Condition) selection engine.
///
/// PATH-S: noexcept, tl::expected, no exceptions, functions <=40 lines,
///         cyclomatic <=8 per function.
class MrcTriggerLogic final {
 public:
  /// Factory: validates params, returns logic instance or error.
  static tl::expected<MrcTriggerLogic, ErrorCode> create(
      const MrcParams& params) noexcept;

  /// Select the best MRC for the given inputs.
  /// Returns std::nullopt when no MRC is required.
  [[nodiscard]] std::optional<MrcSelection> select(
      const MrcSelectionInputs& inputs) const noexcept;

  /// True if anchoring is feasible (depth <= max AND in anchorage zone).
  [[nodiscard]] bool can_anchor(double water_depth_m,
                                bool in_anchorage_zone) const noexcept;

  /// True if heave-to is feasible (Hs <= max AND wind <= max).
  [[nodiscard]] bool can_heave_to(double sea_state_hs,
                                  double wind_speed_kn) const noexcept;

 private:
  explicit MrcTriggerLogic(const MrcParams& params) noexcept;

  MrcParams params_;
};

}  // namespace mass_l3::m1
