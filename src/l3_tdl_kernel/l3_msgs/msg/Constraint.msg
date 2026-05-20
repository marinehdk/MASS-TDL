uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time stamp
float32 confidence  # [0, 1]
string rationale    # SAT-2 摘要

# Generic constraint slot used in COLREGsConstraint and BehaviorPlan
string constraint_type                   # "colregs" | "kinematic" | "envelope" | "tss"
string description                       # SAT-2 摘要（人读）
float64 numeric_value                    # 数值约束值
string unit                              # "deg" | "kn" | "m" | ...
