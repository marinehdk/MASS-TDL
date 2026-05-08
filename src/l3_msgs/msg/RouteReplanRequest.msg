# Per v1.1.2 §15.1 + RFC-006 决议
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp

uint8 reason                             # 见下 enum
uint8 REASON_ODD_EXIT = 0
uint8 REASON_MISSION_INFEASIBLE = 1
uint8 REASON_MRC_REQUIRED = 2
uint8 REASON_CONGESTION = 3

float32 deadline_s
string context_summary                   # SAT-2 摘要供 L2 / ROC 阅
geographic_msgs/GeoPoint current_position
geographic_msgs/GeoPath[] exclusion_zones  # GeoJSON Polygon 格式（RFC-006 锁定）

float32 confidence
string rationale
