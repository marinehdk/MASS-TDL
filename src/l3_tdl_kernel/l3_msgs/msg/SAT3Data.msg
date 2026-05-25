uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time stamp
float32 confidence  # [0, 1] — 预测置信度
string rationale    # SAT-2 摘要

# SAT3Data — Forecast (TDL 压缩时优先推送)
float64 forecast_horizon_s
string predicted_state
float32 prediction_uncertainty           # [0, 1]
float32 tdl_s
float32 tmr_s

# D3.4 M5 TacticalPlanner: trajectory candidates (fixed-size 13)
TrajectoryCandidate[13] trajectory_candidates
uint8 primary_trajectory_idx
float64[] cpa_forecast_300s
float32 takeover_window_s
