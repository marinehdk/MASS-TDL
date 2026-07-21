# Per v1.1.2 §15.1 Behavior_PlanMsg
uint16 schema_version  # 113 = v2.2 (reachability contract); 112 = v1.1.2
builtin_interfaces/Time stamp

uint8 behavior
uint8 BEHAVIOR_TRANSIT = 0
uint8 BEHAVIOR_COLREG_AVOID = 1
uint8 BEHAVIOR_DP_HOLD = 2
uint8 BEHAVIOR_BERTH = 3
uint8 BEHAVIOR_MRC_DRIFT = 4
uint8 BEHAVIOR_MRC_ANCHOR = 5
uint8 BEHAVIOR_MRC_HEAVE_TO = 6
uint8 BEHAVIOR_RECOVERY = 7

float32 heading_min_deg
float32 heading_max_deg
float32 speed_min_kn
float32 speed_max_kn

float32 confidence
string rationale                         # IvP 求解摘要（SAT-2）

# v2.2 §4.6 reachability 合约（M4 publish, M5 consume）
# 0 = M4 未升级，M5 退化到 v2.1 ROT-only schedule
float32 heading_box_reachable_from_psi0_deg   # direction-aware: 从 own_psi 沿 preferred_direction 侧可达 box 边界的最大偏差幅值(deg,>=0)。
                                              # M4 compute_heading_box_reachability 按 pref_dir 计算(Starboard->d_max, Port->|d_min|, Hold/ReduceSpeed->max)。
                                              # 不携带方向符号;方向由 colregs_preferred_direction 提供。v2.2 Sec4.6。
float32 rot_step_deg                          # M4 假设的 ROT step（与 M5 GNC cruise 对齐校验）
float32 min_alt_required_rad                  # M4 期望本船达成的 min_alt（M6 packed）
float32 earliest_min_alt_k                    # M4 建议的 earliest k that min_alt can be reached
string   reachability_rationale               # 若 box narrower than required，说明 reason
