# M1 ODD/Envelope Manager → M4 Behavior Arbiter (event-driven)
# Communicates the commanded operational mode based on ODD state
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

float32 confidence
string rationale
