# Active COLREGs rule per v1.1.2 §9
uint8 rule_id                            # 8, 13, 14, 15, 16, 17 ...
string rule_phase                        # "T_standOn" | "T_act" | "T_postAvoid"
uint64 target_id                         # 关联目标
float32 rule_confidence
string rationale
