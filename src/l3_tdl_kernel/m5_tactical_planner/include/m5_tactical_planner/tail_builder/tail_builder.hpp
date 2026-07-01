#ifndef MASS_L3_M5_TAIL_BUILDER_TAIL_BUILDER_HPP_
#define MASS_L3_M5_TAIL_BUILDER_TAIL_BUILDER_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "m5_tactical_planner/tail_builder/route_frame.hpp"

namespace mass_l3::m5::tail_builder {

enum class ColregSide : std::int8_t { PORT = -1, NONE = 0, STBD = 1 };
enum class ColregRole : std::uint8_t { StandOn = 0U, GiveWay = 1U, BothGiveWay = 2U, Free = 3U };
enum class EncounterState : std::uint8_t { Active = 0U, Release = 1U, Clear = 2U };

struct TargetSnapshot {
  std::int32_t id{0};
  double cpa_m{0.0};
  double tcpa_s{0.0};
  double cpa_sigma_m{0.0};
  double relative_bearing_deg{0.0};
};

struct GncExecutionOdd {
  double ship_length_m{50.0};
  double max_lateral_offset_m{400.0};
  double min_segment_length_m{50.0};
  double min_turn_radius_m{120.0};
  double max_yaw_rate_rad_s{0.04};
  double max_lateral_accel_mps2{0.25};
  double max_decel_mps2{0.20};
  double min_first_changed_distance_m{100.0};
  double min_update_interval_s{1.0};
};

struct TailSegment {
  std::vector<GeoWP> waypoints;
  std::vector<std::uint8_t> source_labels;
};

struct TailInputs {
  ColregRole role{ColregRole::Free};
  GeoWP pN;
  double psiN_rad{0.0};
  double uN_mps{0.0};
  ColregSide protected_side{ColregSide::NONE};
  bool m6_past_clear{false};
  std::uint8_t m6_encounter_state{static_cast<std::uint8_t>(EncounterState::Active)};
  bool m6_release_predicted{false};
  RouteFrame route_frame;
  std::vector<TargetSnapshot> targets;
  double cpa_release_m{1852.0};
  double cpa_safe_m{1852.0};
  GncExecutionOdd gnc_odd;
};

struct TailResult {
  std::optional<TailSegment> hold_then_rejoin;
  std::string reject_reason;
};

class TailBuilder {
 public:
  [[nodiscard]] static TailResult build(const TailInputs& inputs);
};

}  // namespace mass_l3::m5::tail_builder

#endif  // MASS_L3_M5_TAIL_BUILDER_TAIL_BUILDER_HPP_
