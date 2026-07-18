uint8 CONCERN_IVP_INFEASIBLE=1
uint8 CONCERN_ODD_DEGRADED=2
uint8 CONCERN_ETA_INFEASIBLE=3
uint8 CONCERN_BC_FINAL_DEGRADE=4   # P6: BC-MPC FinalDegrade — both BC failed (Condition A) and Mid unrecovered (Condition B)

uint8 concern_type
float32 anchor_hdg
string suggested_action
float32 severity
builtin_interfaces/Time stamp
