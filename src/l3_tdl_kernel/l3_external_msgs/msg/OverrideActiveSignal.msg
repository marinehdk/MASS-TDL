# Hardware Override Arbiter → M1 ODD/Envelope Manager (event-driven)
# Per architecture v1.1.2 §15.1 OverrideActiveSignal
# Wave 0 fix: F-CRIT-B-006 — field renamed override_source → activation_source per spec
uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time stamp

bool override_active                       # true = hardware override engaged; false = released
string activation_source                   # "manual_button" | "automatic_trigger" | "joystick" | "emergency_stop"
float32 confidence  # [0, 1] — 超控信号置信度
string rationale    # SAT-2 摘要
