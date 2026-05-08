# Per v1.1.2 §15.1 ODD_StateMsg + §3.5 state machine + §3.6 Conformance_Score
# Frequency: 1 Hz + event-driven补发 (EDGE→OUT 不等周期)
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp

uint8 current_zone                       # ODD-A/B/C/D
uint8 ODD_ZONE_A = 0                     # 开阔水域
uint8 ODD_ZONE_B = 1                     # 狭水道/VTS
uint8 ODD_ZONE_C = 2                     # 港内/靠泊
uint8 ODD_ZONE_D = 3                     # 能见度不良

uint8 auto_level                         # D2/D3/D4
uint8 AUTO_LEVEL_D2 = 2
uint8 AUTO_LEVEL_D3 = 3
uint8 AUTO_LEVEL_D4 = 4

uint8 health
uint8 HEALTH_FULL = 0
uint8 HEALTH_DEGRADED = 1
uint8 HEALTH_CRITICAL = 2

uint8 envelope_state                     # IN | EDGE | OUT | MRC_PREP | MRC_ACTIVE
uint8 ENVELOPE_IN = 0
uint8 ENVELOPE_EDGE = 1
uint8 ENVELOPE_OUT = 2
uint8 ENVELOPE_MRC_PREP = 3
uint8 ENVELOPE_MRC_ACTIVE = 4

float32 conformance_score                # [0, 1]
float32 tmr_s                            # Maximum Operator Response Time
float32 tdl_s                            # Automation Response Deadline
string zone_reason                       # SAT-2: ODD 子域判断理由

uint8[] allowed_zones                    # 当前健康状态下可允许的 ODD 子域

float32 confidence
string rationale
