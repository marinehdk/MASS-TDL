# M2 World Model → TDL threat state
uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time stamp

string cpa_status              # "closing", "sustained", "cleared"
string target_relative_position # "ahead", "astern", "port", "starboard", etc.

float32 confidence
string rationale

# Backend-authoritative risk model output for M8 / Screen 03.
# Arrays share index order; primary_flags marks the selected primary threat.
string[] target_ids
string[] risk_phases            # Clear | Monitor | Warning | Danger | Critical
float32[] risk_scores
bool[] primary_flags
float64[] range_m
float64[] dcpa_m
float64[] tcpa_s
float64[] warning_margin_m
float64[] danger_margin_m
float64[] closing_speed_mps
float64[] relative_bearing_deg
string[] colregs_duties         # Free | StandOnHold | GiveWay | BothGiveWay | Rule17Action
float64[] tdv_warning_s
float64[] tdv_danger_s

# Backend safety-domain axes used by map/radar display.
float64 danger_forward_m
float64 danger_astern_m
float64 danger_starboard_m
float64 danger_port_m
float64 warning_forward_m
float64 warning_astern_m
float64 warning_starboard_m
float64 warning_port_m
float64 superellipse_power
float64 action_horizon_s
float64 critical_horizon_s
