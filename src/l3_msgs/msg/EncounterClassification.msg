# COLREG geometric pre-classification per v1.1.2 §6.2
# 由 M2 World Model 在 TrackedTarget 内置；M6 在此基础上做规则推理
uint8 encounter_type
uint8 ENCOUNTER_TYPE_NONE = 0
uint8 ENCOUNTER_TYPE_HEAD_ON = 1         # Rule 14
uint8 ENCOUNTER_TYPE_OVERTAKING = 2      # Rule 13
uint8 ENCOUNTER_TYPE_CROSSING = 3        # Rule 15
uint8 ENCOUNTER_TYPE_OVERTAKEN = 4       # 被超越（直航船）
uint8 ENCOUNTER_TYPE_CROSSED_BY = 5      # 被横越（直航船）
uint8 ENCOUNTER_TYPE_AMBIGUOUS = 6       # 边界情况

float64 relative_bearing_deg             # 相对方位 (-180–180)
float64 aspect_angle_deg                 # 目标对本船的舷角
bool is_giveway                          # 本船是否为让路船
