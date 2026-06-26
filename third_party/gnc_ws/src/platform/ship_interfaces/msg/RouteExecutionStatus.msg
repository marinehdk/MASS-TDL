std_msgs/Header header

# GNC execution feedback for a RoutePlan or AvoidancePlan.
# This is the contract back to route planning / collision avoidance:
# accepted commands are executed, infeasible commands are rejected or degraded
# with an explicit reason and suggested correction.

string plan_id
string active_route_id
string command_source

bool accepted
bool executing
bool degraded
bool rejected

string execution_state         # ACCEPTED / EXECUTING / EXECUTING_WITH_LIMIT / REJECTED / FAIL_SAFE_STOP / COMPLETED
string reason                  # machine-readable reason, e.g. turn_radius_too_small
string suggested_action        # slow_down / send_points_earlier / enlarge_turn_radius / hold_position

float64 requested_speed_mps
float64 applied_speed_mps
float64 requested_heading_deg
float64 applied_heading_deg

float64 current_latitude
float64 current_longitude
float64 current_heading_deg
float64 current_course_deg
float64 current_speed_mps
float64 cross_track_error_m

float64 required_turn_radius_m
float64 estimated_available_turn_radius_m
float64 required_decel_distance_m
float64 available_decel_distance_m

float64 suggested_max_speed_mps
float64 suggested_min_distance_m
