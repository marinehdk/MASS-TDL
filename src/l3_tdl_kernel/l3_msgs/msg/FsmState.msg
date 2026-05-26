# FSM State aggregated from M1/M4/M5/M7
# Per v1.1.3-pre-stub §12.2 M8 transparency; per CLAUDE.md §3 v3.0 mandate
uint16 schema_version  # 1 (v1.0)
builtin_interfaces/Time stamp

# FSM state enumerator
uint8 current_state
uint8 STATE_TRANSIT = 0
uint8 STATE_COLREG_AVOIDANCE = 1
uint8 STATE_TOR = 2
uint8 STATE_OVERRIDE = 3
uint8 STATE_MRC = 4
uint8 STATE_HANDBACK = 5

# Active rule source (e.g., "Rule 14 head-on", "MRM-01", "SAT-1: Loss of Perception")
string active_rule

# Structured rationale for HMI display
# Format: state=TRANSIT rule='Nominal' odd_env=0 beh=0 safety_sev=0 avoid_wp=0
string rationale

# Confidence [0, 1] per v3.0 mandate (CLAUDE.md §3)
float32 confidence
