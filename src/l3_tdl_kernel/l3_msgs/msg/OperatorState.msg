# OperatorState.msg — M8 → M1 operator availability
# schema_version 121 = v1.1 (D2.1)

uint16 schema_version 121

# Operator state enum
uint8 OOS_BRIDGE_ON_DUTY = 0   # TMR=30s — bridge crew actively supervising
uint8 OOS_BRIDGE_NEARBY  = 1   # TMR=60s — bridge crew nearby (Veitch 2024 baseline)
uint8 OOS_ROC_ATTENDED   = 2   # TMR=60s — ROC operator present
uint8 OOS_MESS_REST      = 3   # TMR=90s — operator in mess/rest area
uint8 OOS_CABIN_SLEEP    = 4   # TMR=120s — operator in cabin

uint8 assumed_operator_state OOS_BRIDGE_ON_DUTY

builtin_interfaces/Time stamp
float32 confidence 1.0
string rationale
