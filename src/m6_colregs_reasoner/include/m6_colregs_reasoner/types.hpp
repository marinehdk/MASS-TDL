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
  double bearing_deg;           // [0, 360)
  double aspect_deg;            // [0, 360)
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
  double max_turn_rate_deg_s;
  double rule_9_weight;
};

struct RuleEvaluation {
  bool is_active;
  int32_t rule_id;
  EncounterType encounter_type;
  Role role;
  TimingPhase phase;
  double min_alteration_deg;
  std::string preferred_direction;  // STARBOARD | PORT | REDUCE_SPEED | HOLD
  float confidence;
  std::string rationale;
};

}  // namespace mass_l3::m6_colregs
