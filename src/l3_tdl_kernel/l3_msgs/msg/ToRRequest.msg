# Per v1.1.2 §15.1 + §12.4 ToR 协议（C 交互验证：操作员主动点击）
uint16 schema_version  # 121 = v1.1 (D2.1)
builtin_interfaces/Time stamp

# Operator state enum (shared with OperatorState.msg)
uint8 OOS_BRIDGE_ON_DUTY = 0   # TMR=30s
uint8 OOS_BRIDGE_NEARBY  = 1   # TMR=60s
uint8 OOS_ROC_ATTENDED   = 2   # TMR=60s
uint8 OOS_MESS_REST      = 3   # TMR=90s
uint8 OOS_CABIN_SLEEP    = 4   # TMR=120s

uint8 reason
uint8 REASON_ODD_EXIT = 0
uint8 REASON_MANUAL_REQUEST = 1
uint8 REASON_SAFETY_ALERT = 2

float32 deadline_s                       # 接管时间窗口（典型 60 s）[R4]
float32 tdl_s                            # D2.1: TDL at time of tor_request — for SAT-3 transparency
uint8 assumed_operator_state OOS_BRIDGE_ON_DUTY  # D2.1: operator state for TMR lookup

uint8 target_level                       # AutoLevel D2/D3/D4
uint8 TARGET_LEVEL_D2 = 2
uint8 TARGET_LEVEL_D3 = 3
uint8 TARGET_LEVEL_D4 = 4

float32 confidence
string rationale    # SAT-2 摘要（接管请求理由）
string context_summary                   # SAT-1 摘要（操作员须阅读）
string recommended_action
