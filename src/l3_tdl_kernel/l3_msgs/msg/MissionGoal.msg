# Per v1.1.2 §15.1 Mission_GoalMsg — D2.3 extended to schema_version 120 (v1.2.0)
uint16 schema_version        # 120 = v1.2.0  (was 112 = v1.1.2)
builtin_interfaces/Time stamp
geographic_msgs/GeoPoint current_target_wp
float32 eta_to_target_s
float32 speed_recommend_kn
float32 confidence
string rationale
uint8 current_error_severity  # 0=NORMAL 1=MEDIUM 2=HIGH  [D2.3 NEW]
float32 xte_nm               # L4 XTE (nm); -1 = L4 not available [D2.3 NEW]
float32 sea_current_kn       # M2 own_ship.current_speed_kn       [D2.3 NEW]
uint8 l1_watchdog_status     # 0=OK 1=WARNING 2=TIMEOUT           [D2.3 NEW]
