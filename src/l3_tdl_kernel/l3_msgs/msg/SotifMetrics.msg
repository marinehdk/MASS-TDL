# SotifMetrics — M7 SOTIF metrics and violation aggregation
# D3.4 M8 HMI bridge awareness message

uint16 schema_version 114
builtin_interfaces/Time stamp
SotifViolation[] violations
uint8 active_violation_count
bool degradation_alert
float32 degradation_display_latency_ms
uint32 sequence_number
