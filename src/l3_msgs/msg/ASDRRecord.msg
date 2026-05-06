# Per v1.1.2 §15.1 + RFC-004 决议
# Frequency: 事件 + 2 Hz
builtin_interfaces/Time stamp
string source_module                     # "M1_ODD_Manager" | "M2_World_Model" | ... | "M7_Safety_Supervisor"

string decision_type                     # "encounter_classification" | "avoid_wp" |
                                          # "cpa_alert" | "checker_veto_received" |
                                          # "sotif_alert" | "mrm_triggered" | ...

string decision_json                     # JSON 序列化（兼容 ASDR §5.2 AiDecisionRecord schema）
uint8[] signature                        # SHA-256 签名（防篡改）
