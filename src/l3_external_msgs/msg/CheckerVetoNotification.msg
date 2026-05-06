# X-axis Deterministic Checker → M7 Safety Supervisor (event-driven)
# Per ros2-idl-implementation-guide.md §3.2 enum pattern
builtin_interfaces/Time stamp

string checker_layer
string vetoed_module
uint8 veto_reason_class
uint8 VETO_REASON_COLREGS_VIOLATION = 0
uint8 VETO_REASON_CPA_BELOW_THRESHOLD = 1
uint8 VETO_REASON_ENC_CONSTRAINT = 2
uint8 VETO_REASON_ACTUATOR_LIMIT = 3
uint8 VETO_REASON_TIMEOUT = 4
uint8 VETO_REASON_OTHER = 5

string veto_reason_detail
bool fallback_provided
float32 confidence
