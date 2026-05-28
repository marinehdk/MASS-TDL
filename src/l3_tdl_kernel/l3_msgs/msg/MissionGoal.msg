# Per v1.1.2 §15.1 Mission_GoalMsg — D2.3 extended to schema_version 121 (v1.2.1)
uint16 schema_version        # 121 = v1.2.1  (was 120 = v1.2.0)
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

# W3: M3 FSM state + task validity sub-state (schema_version 121)
uint8 FSM_INIT=0
uint8 FSM_IDLE=1
uint8 FSM_TASK_VALIDATION=2
uint8 FSM_AWAITING_ROUTE=3
uint8 FSM_ACTIVE=4
uint8 FSM_REPLAN_WAIT=5

uint8 TASK_VALIDITY_PENDING=0
uint8 TASK_VALIDITY_VALID=1
uint8 TASK_VALIDITY_INVALID=2
uint8 TASK_VALIDITY_REPLANNING=3

uint8 fsm_state        # current M3 FSM state (FSM_* constants above)  [W3 NEW]
uint8 task_validity    # 4-condition gate sub-state (TASK_VALIDITY_*) [W3 NEW]
