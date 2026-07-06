std_msgs/Header header

uint16 schema_version
builtin_interfaces/Time source_stamp

# Collision-avoidance command plan from the COLAV/planning side to GNC.
# GNC treats this as an intended command, then accepts, degrades, or rejects it
# according to vessel physical limits and current navigation state.

string plan_id
string parent_route_id
string behavior_mode          # avoidance / emergency_avoidance / return_to_route / dp_hold
string command_source         # route_planner / collision_avoidance / test_tool

# WGS84 route points. Arrays must be non-empty and latitude/longitude lengths must match.
float64[] latitude
float64[] longitude

# Commanded per-waypoint targets. Empty array means "derive internally".
# If present, array length must match latitude/longitude length.
float64[] command_speed_mps
float64[] command_heading_deg
string[] navigation_mode

# Optional validity and strictness.
builtin_interfaces/Time valid_until
bool require_exact_heading
bool require_exact_speed
bool allow_degraded_execution

# Optional return-to-route hint after avoidance is complete.
bool has_return_to_route_point
float64 return_latitude
float64 return_longitude

# Preserved M5 audit metadata for GNC handoff/M8 traceability.
uint8 MID_MPC_OPTIMIZED=0
uint8 MID_MPC_TERMINAL_HOLD=1
uint8 REJOIN_TO_L2=2
uint8 L2_NOMINAL_SUFFIX=3
uint8 DEGRADED_CORRIDOR=4
uint8 L2_HISTORICAL_PREFIX=5
uint8[] segment_source
uint32 route_hash
builtin_interfaces/Time stale_committed_at

uint8 NLP_CONVERGED=0
uint8 NLP_NONCONVERGED=1
uint8 NLP_RESTOREMENT_FAILED=2
uint8 NLP_MAX_ITER=3
uint8 nlp_solver_status
float32 nlp_kkt_residual
bool nlp_tail_gate_failed

float32 confidence
string rationale
