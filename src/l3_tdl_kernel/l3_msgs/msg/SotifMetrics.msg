# M7 SOTIF assumption violation metrics (topic: /sil/sotif_metrics @10Hz)
# Per v1.1.3 §11.3 + RFC-003 + D3.3b-spec §4.1

uint16 schema_version 113  # v1.1.3
builtin_interfaces/Time stamp

# Enumerated assumption IDs — MUST match AssumptionId enum order in assumption_monitor.hpp
uint8 ASSUMPTION_AIS_RADAR_CONSISTENCY    = 0  # ① AIS/Radar fusion confidence
uint8 ASSUMPTION_MOTION_PREDICTABILITY    = 1  # ② Target motion prediction RMSE
uint8 ASSUMPTION_PERCEPTION_COVERAGE      = 2  # ③ Perception blind zone fraction
uint8 ASSUMPTION_COLREGS_SOLVABILITY      = 3  # ④ COLREGs consecutive failures
uint8 ASSUMPTION_COMM_LINK                = 4  # ⑤ Communication RTT / packet loss
uint8 ASSUMPTION_CHECKER_VETO_RATE        = 5  # ⑥ X-axis Checker veto rate (RFC-003)

# Metric entry — one per assumption, 6-element fixed array
SotifMetricEntry[6] metrics

# D3.4 M8 HMI bridge awareness fields
SotifViolation[] violations
uint8 active_violation_count
bool degradation_alert
float32 degradation_display_latency_ms
uint32 sequence_number
