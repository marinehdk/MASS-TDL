# L3-owned waypoint avoidance plan. The GNC bridge (Track A) translates this to
# ship_interfaces/AvoidancePlan. L3 internals use this L3-owned type only;
# ship_interfaces types live solely in gnc_bridge. Spec D3/D6.
builtin_interfaces/Time stamp
uint16 schema_version

# L3-owned waypoint avoidance plan. Bridge translates to ship_interfaces/AvoidancePlan.
string plan_id
string parent_route_id
string behavior_mode          # avoidance / emergency_avoidance / return_to_route
string command_source

# WGS84 route points
float64[] latitude
float64[] longitude

# Per-waypoint targets (empty = derive internally)
float64[] command_speed_mps
string[] navigation_mode

builtin_interfaces/Time valid_until
bool allow_degraded_execution

# Return-to-route hint (reserved semantics, mirrored from GNC contract)
bool has_return_to_route_point
float64 return_latitude
float64 return_longitude

float32 confidence
string rationale
