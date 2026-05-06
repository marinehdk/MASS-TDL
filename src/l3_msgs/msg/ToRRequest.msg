# Per v1.1.2 §15.1 + §12.4 ToR 协议（C 交互验证：操作员主动点击）
builtin_interfaces/Time stamp

uint8 reason
uint8 REASON_ODD_EXIT = 0
uint8 REASON_MANUAL_REQUEST = 1
uint8 REASON_SAFETY_ALERT = 2

float32 deadline_s                       # 接管时间窗口（典型 60 s）[R4]

uint8 target_level                       # AutoLevel D2/D3/D4
string context_summary                   # SAT-1 摘要（操作员须阅读）
string recommended_action
