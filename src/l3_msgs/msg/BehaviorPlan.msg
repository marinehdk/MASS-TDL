# Per v1.1.2 §15.1 Behavior_PlanMsg
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp

uint8 behavior
uint8 BEHAVIOR_TRANSIT = 0
uint8 BEHAVIOR_COLREG_AVOID = 1
uint8 BEHAVIOR_DP_HOLD = 2
uint8 BEHAVIOR_BERTH = 3
uint8 BEHAVIOR_MRC_DRIFT = 4
uint8 BEHAVIOR_MRC_ANCHOR = 5
uint8 BEHAVIOR_MRC_HEAVE_TO = 6

float32 heading_min_deg
float32 heading_max_deg
float32 speed_min_kn
float32 speed_max_kn

float32 confidence
string rationale                         # IvP 求解摘要（SAT-2）
