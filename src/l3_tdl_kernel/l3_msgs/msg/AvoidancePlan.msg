# M5 committed avoidance route contract (Slice A, spec v2 §5/§6).
# /l3/m5/avoidance_plan is the execution truth for GNC.
# (Phase 2.6 R9: legacy /l3/m5/avoidance_waypoints shadow removed; do not
#  reintroduce. Phase 2.4 G-M5-1: commit_branch enum makes the
#  publish_committed_route_ 4-branch identity observable from /l3/asdr/record
#  + trace without container log scraping.)
uint16 schema_version 116

# Phase 2.4 (G-M5-1, spec v2.3 §15): which publish_committed_route_ branch
# produced this plan. Stable across schema revisions; consumers group/audit
# by this enum instead of string-matching command_source/rationale.
uint8 COMMIT_BRANCH_UNSPECIFIED=0
uint8 COMMIT_BRANCH_OPTIMIZED=1     # NLP converged + tail ok + try_revise ok
uint8 COMMIT_BRANCH_CORRIDOR=2      # encounter-anchored fallback (NLP/optimized failed)
uint8 COMMIT_BRANCH_RETURN=3        # M6 conflict-clear return-to-route
uint8 COMMIT_BRANCH_KEEP_LAST=4     # preflight/commit reject → stale heartbeat
uint8 COMMIT_BRANCH_BCMPC_FOLLOW=5  # BC-MPC takeover, NLP corridor suppressed
uint8 COMMIT_BRANCH_EMPTY=6         # TRANSIT release / no conflict

builtin_interfaces/Time stamp
uint8 commit_branch

# Per-waypoint segment-source labels (spec v2 §0; Phase 3.10 §5.2 adds the
# L2 historical prefix used to align coordinate_transform_node's
# first_geometry_change_index pairing against an L2-anchored feedback path).
uint8 MID_MPC_OPTIMIZED=0
uint8 MID_MPC_TERMINAL_HOLD=1
uint8 REJOIN_TO_L2=2
uint8 L2_NOMINAL_SUFFIX=3
uint8 DEGRADED_CORRIDOR=4
uint8 L2_HISTORICAL_PREFIX=5

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
