# M3 Mission Manager → M1 ODD Manager mission context
# Provides mission-level context for MRC selection (water depth, anchorage, moored state)
uint16 schema_version  # 120 = v1.2.0
builtin_interfaces/Time stamp

float64 water_depth_m           # water depth at current position [m], -1 = unknown
bool in_anchorage_zone          # true if inside a designated anchorage area
bool is_moored                  # true if vessel is currently moored

float32 confidence
string rationale
