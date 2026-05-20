# Per v1.1.2 §15.1 + RFC-001 紧急避让接口
# Frequency: 事件 / 上限 10 Hz
uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time trigger_time

string trigger_reason                    # "CPA_EMERGENCY" | "COLLISION_IMMINENT" | ...
float32 heading_cmd_deg
float32 speed_cmd_kn
float32 rot_cmd_deg_s
float32 validity_s                       # 命令有效期（典型 1–3 s）

float32 confidence
string rationale    # SAT-2 摘要（BC-MPC 紧急决策依据）
