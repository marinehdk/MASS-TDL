std_msgs/Header header

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
