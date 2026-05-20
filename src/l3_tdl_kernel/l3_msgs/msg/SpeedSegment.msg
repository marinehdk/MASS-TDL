uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time stamp
float32 confidence  # [0, 1]
string rationale    # SAT-2 摘要

# Speed adjustment segment per v1.1.2 §10
float64 start_distance_m                 # 沿路径累计距离起点
float64 end_distance_m
float64 target_speed_kn
string phase                             # "accel" | "cruise" | "decel" | "hold"
