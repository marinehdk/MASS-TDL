# Per v1.1.2 §15.1 World_StateMsg + §6
# Frequency: 4 Hz (M2 内部 2 Hz 输入聚合 + 1 次插值/外推)
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp

l3_msgs/TrackedTarget[] targets         # 含 cpa_m / tcpa_s（M2 计算）
l3_msgs/OwnShipState own_ship           # 取自最近 FilteredOwnShipState 快照
l3_msgs/ZoneConstraint zone             # ENC 约束

float32 confidence                       # 整体世界视图置信度
string rationale                         # SAT-2: 聚合质量摘要
