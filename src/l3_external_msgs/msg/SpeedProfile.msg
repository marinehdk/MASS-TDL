# L2 Voyage Planner → M3, M5 (1 Hz / event-driven)
# Per v1.1.2 §15.1 SpeedProfile
builtin_interfaces/Time stamp

uint64 profile_id
float64[] segment_start_distances_m
float64[] segment_end_distances_m
float64[] target_speeds_kn
float64[] max_speeds_kn
float64[] min_speeds_kn
string[] segment_types                     # "transit" | "approach" | "harbor" | "caution"

float32 confidence
string rationale
