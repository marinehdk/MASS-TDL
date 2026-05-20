uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time stamp
float32 confidence  # [0, 1]
string rationale    # SAT-2 摘要

# SAT1Data — Status/State (Chen ARL-TR-7180 [R11])
string state_summary                     # "TRANSIT @ D3, ODD-A, 18 kn"
string[] active_alerts                   # ["DEGRADED: GNSS" ...]
