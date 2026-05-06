# L2 Voyage Planner → M3 (event-driven) per RFC-006
# Per v1.1.2 §15.1 + ros2-idl-implementation-guide.md §3.3
builtin_interfaces/Time stamp

uint8 status
uint8 STATUS_SUCCESS = 0
uint8 STATUS_FAILED_TIMEOUT = 1
uint8 STATUS_FAILED_INFEASIBLE = 2
uint8 STATUS_FAILED_NO_RESOURCES = 3

string failure_reason
bool retry_recommended

float32 confidence
string rationale
