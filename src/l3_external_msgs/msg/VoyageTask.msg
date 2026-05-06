# L1 Mission Layer → M3 Mission Manager (event-driven)
# Per v1.1.2 §15.1 VoyageTask
builtin_interfaces/Time stamp

uint64 task_id
string task_type                          # "transit" | "station_keep" | "berth" | "anchor"
geographic_msgs/GeoPoint origin
geographic_msgs/GeoPoint destination
float64[] waypoint_latitudes              # intermediate WP latitudes
float64[] waypoint_longitudes             # intermediate WP longitudes
float64 speed_cmd_kn
float32 eta_requirement_s

float32 confidence
string rationale
