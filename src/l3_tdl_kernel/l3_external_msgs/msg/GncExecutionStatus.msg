# Bridge-translated GNC execution feedback (from ship_interfaces/RouteExecutionStatus).
# Consumed by M4/M5/M7 to react to GNC accept/reject/degrade. Spec D6.
builtin_interfaces/Time stamp
uint16 schema_version

string plan_id
string active_route_id
string command_source

bool accepted
bool executing
bool degraded
bool rejected

string execution_state         # ACCEPTED / EXECUTING / EXECUTING_WITH_LIMIT / REJECTED / ...
string reason                  # turn_radius_too_small / yaw_rate_too_high / ...
string suggested_action        # slow_down / send_points_earlier / ...

float64 requested_speed_mps
float64 applied_speed_mps
float64 suggested_max_speed_mps

float64 current_latitude
float64 current_longitude
float64 current_heading_deg
float64 current_speed_mps
float64 cross_track_error_m

float32 confidence
string rationale
