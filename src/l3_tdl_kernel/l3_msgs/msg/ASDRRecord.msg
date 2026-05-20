# Per v1.1.2 §15.1 + RFC-004 决议
# Frequency: 事件 + 2 Hz
uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time stamp
float32 confidence  # [0, 1] — 审计记录置信度（SHA-256 signature 可不填 1.0）
string rationale    # SAT-2 决策摘要（ASDR 审计用）
string source_module                     # "M1_ODD_Manager" | "M2_World_Model" | ... | "M7_Safety_Supervisor"

string decision_type                     # "encounter_classification" | "avoid_wp" |
                                          # "cpa_alert" | "checker_veto_received" |
                                          # "sotif_alert" | "mrm_triggered" | ...

string decision_json                     # JSON 序列化（兼容 ASDR §5.2 AiDecisionRecord schema）
uint8[] signature                        # SHA-256 签名（防篡改）
