# Per v1.1.2 §15.1 AvoidancePlanMsg + RFC-001 方案 B
# Frequency: 1–2 Hz; L4 在避让模式下用此覆盖 L2 PlannedRoute
builtin_interfaces/Time stamp

l3_msgs/AvoidanceWaypoint[] waypoints   # 避让路径航点序列
l3_msgs/SpeedSegment[] speed_adjustments # 速度调整曲线
float32 horizon_s                        # 有效时域（典型 60–120 s）

string status                            # "NORMAL" | "OVERRIDDEN"
string[] active_constraints              # 激活的约束列表（SAT-2）

float32 confidence
string rationale                         # IvP / MPC 求解摘要
