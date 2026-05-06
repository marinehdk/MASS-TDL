#pragma once
#include <chrono>
#include <cstdint>
#include <string>

namespace mass_l3::m6_colregs {

enum class OddDomain : uint8_t {
  ODD_A = 0, ODD_B = 1, ODD_C = 2, ODD_D = 3, ODD_UNKNOWN = 255
};

enum class Role : uint8_t {
  STAND_ON = 0, GIVE_WAY = 1, BOTH_GIVE_WAY = 2, FREE = 3
};

enum class EncounterType : uint8_t {
  NONE = 0, HEAD_ON = 1, OVERTAKING = 2, CROSSING = 3, RESTRICTED_VIS = 4, AMBIGUOUS = 5
};

enum class TimingPhase : uint8_t {
  PRESERVE_COURSE = 0, SOUND_WARNING = 1, INDEPENDENT_ACTION = 2, CRITICAL_ACTION = 3
};

struct TargetGeometricState {
  uint64_t target_id;
  double bearing_deg;           // absolute bearing from ownship [0, 360)
  double target_heading_deg;    // target's true heading [0, 360)
  double aspect_deg;            // aspect angle from target's bow [0, 360)
  double relative_speed_kn;
  double cpa_m;                 // >= 0
  double tcpa_s;                // >= 0
  double ownship_heading_deg;
  double ownship_speed_kn;
  int32_t target_ship_type_priority;
  std::chrono::system_clock::time_point stamp;
};

struct RuleParameters {
  double t_standOn_s;            // [TBD-HAZID]
  double t_act_s;                // [TBD-HAZID]
  double t_emergency_s;          // [TBD-HAZID]
  double min_alteration_deg;     // [TBD-HAZID]
  double cpa_safe_m;             // [TBD-HAZID]
  double max_speed_kn;           // [TBD-HAZID] Rule 6 safe speed ceiling
  double max_turn_rate_deg_s;    // [TBD-HAZID]
  double rule_9_weight;
};

struct RuleEvaluation {
  bool is_active{false};
  int32_t rule_id{0};
  uint64_t target_id{0};           // populated by run_reasoning after evaluate()
  EncounterType encounter_type{EncounterType::NONE};
  Role role{Role::FREE};
  TimingPhase phase{TimingPhase::PRESERVE_COURSE};
  double min_alteration_deg{0.0};
  std::string preferred_direction{"HOLD"};  // STARBOARD | PORT | REDUCE_SPEED | HOLD
  float confidence{0.0f};
  std::string rationale;
};

}  // namespace mass_l3::m6_colregs
