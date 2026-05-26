# M7Observability — read-only observability channel for M7 SafetySupervisor
# Topic: /m7/sil_observability
# Publisher: M7 (10 Hz, independent timer callback — NOT triggered by verdict events)
# Subscriber: M8 HMI Bridge (SAT-1 transparency display only, no arbiter influence)
# Closes G P1-G-1: M7 verdict reaches HMI without passing through Doer decision bus
# Architecture §8.2 / schema_version = 1

builtin_interfaces/Time stamp
uint8 schema_version
uint8 verdict_code
string rationale
float32[6] hc_scores
bool path_s_clean
