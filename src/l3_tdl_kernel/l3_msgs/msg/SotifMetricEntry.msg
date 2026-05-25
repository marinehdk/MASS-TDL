# SOTIF metric entry — one per AssumptionId (6-element fixed array in SotifMetrics)
# Per v1.1.3 §11.3 + D3.3b-spec §4.1

uint8 assumption_id       # ASSUMPTION_* enum value (0–5)
float32 violation_score   # Normalized metric (0.0–1.0, higher = worse)
uint16 window_count       # Violation samples in current sliding window (0–100)
bool is_violated          # Threshold exceeded this cycle
float32 raw_value         # Raw sensor value for ASDR transparency (un-normalized)
