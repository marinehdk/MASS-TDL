# M1 ODD/Envelope Manager → M4 Behavior Arbiter (event-driven)
# Communicates the commanded operational mode based on ODD state
uint16 schema_version  # 121 = v1.1 (D2.1)
builtin_interfaces/Time stamp

uint8 mode
uint8 MODE_NORMAL = 0
uint8 MODE_DEGRADED = 1
uint8 MODE_LIMITED = 2
uint8 MODE_EMERGENCY = 3

uint8 behavior_constraint
uint8 CONSTRAINT_NONE = 0
uint8 CONSTRAINT_SPEED = 1
uint8 CONSTRAINT_HEADING = 2
uint8 CONSTRAINT_BOTH = 3

# Operator state (re-exported from OperatorState.msg for downstream consumers)
uint8 assumed_operator_state
uint8 OOS_BRIDGE_ON_DUTY = 0   # TMR=30s
uint8 OOS_BRIDGE_NEARBY  = 1   # TMR=60s
uint8 OOS_ROC_ATTENDED   = 2   # TMR=60s
uint8 OOS_MESS_REST      = 3   # TMR=90s
uint8 OOS_CABIN_SLEEP    = 4   # TMR=120s

float32 confidence
string rationale
