# Per v1.1.2 §15.1 + ADR-001 (M7 仅触发预定义 MRM 命令集，不注入轨迹)
builtin_interfaces/Time stamp

uint8 alert_type
uint8 ALERT_IEC61508_FAULT = 0
uint8 ALERT_SOTIF_ASSUMPTION = 1
uint8 ALERT_PERFORMANCE_DEGRADED = 2

uint8 severity
uint8 SEVERITY_INFO = 0
uint8 SEVERITY_WARNING = 1
uint8 SEVERITY_CRITICAL = 2
uint8 SEVERITY_MRC_REQUIRED = 3

string description

string recommended_mrm                   # "MRM-01" | "MRM-02" | "MRM-03" | "MRM-04"

float32 confidence
string rationale
