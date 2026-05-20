uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time stamp
float32 confidence  # [0, 1] — 聚合置信度
string rationale    # SAT-2 摘要

# SAT2Data — Reasoning (按需触发)
string trigger_reason                    # "colregs_conflict" | "system_low_confidence" | "operator_request"
string reasoning_chain                   # 推理摘要
float32 system_confidence                # 系统对当前决策的置信度
