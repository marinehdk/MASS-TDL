# L1 Mission Layer → M3 Mission Manager (event-driven)
# Per architecture v1.1.2 §15.1 VoyageTask + ros2-idl-implementation-guide.md §3.3
# Wave 0 fix: F-CRIT-B-002 — fields rewritten to match v1.1.2 IDL exactly
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp

uint64 task_id

geographic_msgs/GeoPoint departure
geographic_msgs/GeoPoint destination

# ETA window (TimeWindow defined in this same package; F-CRIT-B-003 fix)
l3_external_msgs/TimeWindow eta_window

string optimization_priority              # "fuel_optimal" | "time_optimal" | "balanced"

# Mandatory waypoints (forced points the route MUST pass through)
geographic_msgs/GeoPoint[] mandatory_waypoints

# Forbidden zones — GeoJSON-style polygons (RFC-006 lock)
geographic_msgs/GeoPath[] exclusion_zones

string special_restrictions               # SAR-specific / weather constraints / etc.

float32 confidence
string rationale
