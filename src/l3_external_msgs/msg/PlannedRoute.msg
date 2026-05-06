# L2 Voyage Planner → M3, M5 (1 Hz / event-driven)
# Per v1.1.2 §15.1 PlannedRoute
builtin_interfaces/Time stamp

uint64 route_id
geographic_msgs/GeoPath route             # full planned route path
float64 total_distance_nm
float64 estimated_duration_s
float64[] speed_profile_kn                # speed per segment
string safety_zone                        # L2-defined safety corridor

float32 confidence
string rationale
