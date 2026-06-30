# M5 committed avoidance route contract (Slice A, spec v2 §5/§6).
# /l3/m5/avoidance_plan is the execution truth for GNC;
# /l3/m5/avoidance_waypoints is a compatibility shadow only.
uint16 schema_version 114
builtin_interfaces/Time stamp

# Exactly five per-waypoint segment-source labels (spec v2 §0).
uint8 MID_MPC_OPTIMIZED=0
uint8 MID_MPC_TERMINAL_HOLD=1
uint8 REJOIN_TO_L2=2
uint8 L2_NOMINAL_SUFFIX=3
uint8 DEGRADED_CORRIDOR=4

# NLP solver status audit (Slice A field plumbing; Slice B/K consume semantics).
uint8 NLP_CONVERGED=0
uint8 NLP_NONCONVERGED=1
uint8 NLP_RESTOREMENT_FAILED=2
uint8 NLP_MAX_ITER=3

# Rich M5 audit waypoints retained for M7/M8/ASDR compatibility.
l3_msgs/AvoidanceWaypoint[] waypoints
l3_msgs/SpeedSegment[] speed_adjustments
float32 horizon_s

string status
string[] active_constraints

# Complete route snapshot consumed by gnc_bridge -> ship_interfaces/AvoidancePlan.
string plan_id
string parent_route_id
string behavior_mode
string command_source
float64[] latitude
float64[] longitude
float64[] command_speed_mps
string[] navigation_mode
builtin_interfaces/Time valid_until
bool allow_degraded_execution
bool has_return_to_route_point
float64 return_latitude
float64 return_longitude
uint8[] segment_source
uint32 route_hash
builtin_interfaces/Time stale_committed_at

uint8 nlp_solver_status
float32 nlp_kkt_residual
bool nlp_tail_gate_failed

float32 confidence
string rationale
