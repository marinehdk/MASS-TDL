# COLREGs 避碰链 — 修正架构设计（目标态）

| 项 | 值 |
|---|---|
| 文档日期 | 2026-06-17 |
| 文档定位 | 原架构报告《MASS_ADAS_L3_TDL_架构设计报告.md》COLREGs 避碰链相关章节的**修正 + 补充**，描述目标态。**不替代原报告**。 |
| 配套文档 | `COLREGs避碰链-整改路线图.md`（本文档定目标态，路线图定"怎么到"） |
| 覆盖范围 | 全避碰链 + 返航 + 安全监督：M1 ODD 参数 / M2 预分类 / M6 Rules 13-19 / M4 仲裁 / M5 规划 / M3 返航 / M7 doer-checker |
| 触发证据 | `docs/Design/Review/2026-06-16/` 多份 8-probe review + Sisyphus 独立评审 + 三篇桌面研究笔记（避碰说明 / 参数建议 / 系统执行流程） |
| 约束 | 不改原架构报告；本文档与原报告差异在 §5 显式声明。所有偏离项有代码 file:line 证据（见 §2）。 |

---

## 1. 文档定位

本文档的产出动因：用户报告 TDL 避碰行为与返航行为失败、不符合正常 COLREGs 执行流程。经三路并行代码探查（M6 / M4-M5 / 架构-配置对比）与三篇桌面研究交叉验证，确认当前实现存在 **D1-D14 共 14 项偏离**，其中 8 项为 🔴 高严重度，直接导致两个症状。

**本文档不是对原架构报告的重写**，而是针对 COLREGs 避碰链的**目标态修正设计**。它与原报告的关系：

| 本文档章节 | 对应原报告章节 | 关系 |
|---|---|---|
| §2 偏离基线 | — | 新增（原报告无偏离记录） |
| §3.1 ODD 参数体系 | §3.3 ODD 子域表（`:260`）、§9.3 关键参数（`:830-836`）、§10.4 | **修正**：单值阈值 → 4 层阈值 + 动态 CPA 函数 |
| §3.2 M6 Rule 14 状态机 | §9.2 规则推理架构（`:805-825`）、§9.1（`:799`） | **补充**：原报告 Rule 14 纯几何无 TCPA 门，本文档加入显式状态机 |
| §3.2 M6 Rule 19 ODD-D 切换 | §9.1（`:799`）、§9.2（`:813`） | **修正**：原报告称"Rule 19 覆盖 Rule 13-17"，但未定义硬切换机制；本文档明确 |
| §3.3 M4 硬约束 + RETURN_TO_ROUTE | §8.3 行为字典（`:754-766`）、§8.4（`:769`） | **修正 + 补充**：禁止 STARBOARD→REDUCE_SPEED；新增 RETURN_TO_ROUTE behavior |
| §3.4 M5 planner SLA | §4.5 算法选型（`:455`） | **补充**：原报告未规定 solver 收敛契约与 fallback SLA |
| §3.5 M3 返航一等行为 | §7.2（`:702`） | **补充**：原报告未把返航列为一等行为 |
| §3.6 M7 doer-checker | §11.6 MRM（`:1077-1082`）、P1-P5 优先级（`:1196-1239`） | **明确化**：启用条件 |
| §4 接口契约变更 | §15 接口契约总表 | **补充** |

**阅读顺序建议**：先读 §2（知道哪里偏了）→ §3（知道目标态）→ 配合路线图 §3（知道怎么改）。

---

## 2. 偏离基线（D1-D14）

下表是本修正设计的偏离起点，每项带设计来源、代码证据（file:line）、严重度。路线图文档 §2 给出每项对应的整改项（P0/P1/P2）。

> 注：本表严重度按"对用户两个症状（避碰失败 / 返航失败）的直接贡献"评级。

| # | 偏离描述 | 设计来源 | 代码/配置证据 | 严重度 |
|---|---|---|---|---|
| **D1** | ODD-A `cpa_safe_m` 配置 1000m（0.54nm），设计要求 1852m（1.0nm） | 原报告 §3.3:260、§10.4:925 | `m6_colregs_reasoner/config/odd_aware_thresholds.yaml:8` = `1000.0` | 🔴 高 |
| **D2** | ODD-A `min_alteration_deg` 配置 15°（设计 30°）；ODD-D 配置 10°（设计 30°） | 原报告 §9.3:834 | `odd_aware_thresholds.yaml:7` = `15.0`；`:31` = `10.0` | 🔴 高 |
| **D3** | M4 把 Rule 14 BOTH_GIVE_WAY 的 STARBOARD 改写为 REDUCE_SPEED，无 Rule 14 守卫 | 原报告 §8.4:769-783（M4 应作为硬约束） | `m4_behavior_arbiter/src/colregs_directive.cpp:151-160`：`can_reduce_speed` 显式接纳 `BothGiveWay`（`:153`），`ample_tcpa>180s`（`:154`）即改写 direction | 🔴 高 |
| **D4** | Rule 17 三阶段时序代码实现正确，但 launch `rule_set` 不含 Rule 17 → 运行时无效 | 原报告 §9.2:821、§9.3:835-836 | `launch/l3_params.yaml:33` = `["Rule13","Rule14","Rule15","Rule16"]`；实现见 `m6_colregs_reasoner/src/rules/colregs/rule17_stand_on.cpp:40-75` | 🔴 高 |
| **D5** | ODD-D 下 Rule 19 应覆盖 Rule 13-17，但代码不切换规则集，且 Rule 19 未在 launch 加载 | 原报告 §9.1:799、§9.2:813 | `l3_params.yaml:33` 无 Rule19；`colregs_reasoner_node.cpp:548,688` 对每条规则评估无 ODD 过滤；`rule19_restricted_visibility.cpp:21` 仅自门控 | 🔴 高 |
| **D6** | M7 doer-checker 一票否决（P2）被关闭，M7 进程未运行 | 原报告 §11.6、P2 优先级 `:1196-1239` | `l3_params.yaml:39` `veto_enabled: false`；preflight gate_2 显示 M7 未运行 | 🔴 高 |
| **D7** | M6 RuleAssessment.trigger_conditions 硬编码且错误：发布 "heading_diff<22.5°" + "bearing_rate<0.5°/min"，实际 Rule 14 用 6°/6°/10° 且从不计算 bearing_rate | 原报告 §9.2:815（Rule 14 几何） | `colregs_reasoner_node.cpp:928-931` 硬编码；实际阈值在 `rule14_head_on.cpp:30,34,39` | 🟡 中 |
| **D8** | M6 残留幽灵冲突：线性衰减定时器 `rule14_state_=30`，0.5/s = 60s | 原报告无（设计缺陷） | 见 Sisyphus review line 281；`rule_latch.hpp` 释放依赖投影而非 range opening | 🟡 中 |
| **D9** | M5 Mid-MPC solver 是 D3.1 stub，永不收敛 → 几何 fallback 几乎总是触发 | 原报告 §4.5:455（MPC 选型） | `m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:363-368` 源码注释自承"never converges"；`l3_params.yaml:29` `use_ipopt: false` | 🔴 高 |
| **D10** | M5 BC-MPC 订阅 `/m2/world_state`（缺 `/l3/` 前缀）→ 永远收不到 world state → 永不激活 | 原报告 §4.5（BC-MPC 10Hz 兜底） | `m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp:35` = `/m2/world_state`；其他模块统一 `/l3/m2/world_state` | 🔴 高 |
| **D11** | M4 COLREG_AVOIDANCE 权重提升是 no-op：设到 0.85，但实际 IvP 用硬编码 10.0 | 原报告 §8.3:761（权重 0.7） | `behavior_arbiter_node.cpp:224-230` 设权重；`:620` `weighted_fns.push_back({10.0, avoid_fn})` 硬编码 | 🟡 中 |
| **D12** | M4 无 RETURN_TO_ROUTE behavior，返航/航迹恢复非一等行为 | 原报告 §8.3:754-766（5 个 behavior 无 return） | `m4_behavior_arbiter/config/behavior_definitions.yaml` 仅 TRANSIT/COLREG_AVOID/RESTRICTED_VIS/CHANNEL_FOLLOW/MRC_DRIFT | 🟡 中 |
| **D13** | M5 几何 fallback 首航点在 t=60s → 末段游荡；CPA/TCPA ×0.2 突变污染原始 M2 数据并泄露到 SAT-3 | 原报告无 | `m5_tactical_planner/include/.../types.hpp:385` `geometric_fallback_waypoint_time_s`；`mid_mpc_node.cpp:267-268` 原地突变 `tgt.cpa_m *= 0.2` | 🟡 中 |
| **D14** | 所有 ODD 阈值仍标 `[TBD-HAZID]` 未标定；YAML ODD-A=1000m 与代码 fallback 默认 1852m 不一致 | 原报告 §3.3:265、§9.3（HAZID 标定） | `odd_aware_thresholds.yaml:1-2`；`colregs_reasoner_node.cpp:1153-1157` fallback `cpa_safe_m=1852.0` | 🟡 中 |

**重要认知校正**（针对桌面研究笔记「COLREGs 避碰说明.md」）：
> 笔记暗示设计要的是"TCPA ≤ 12min → 开始规避"，代码没实现。但逐条核对原报告后发现：**§3.3:260 的 12min 是 ODD-A 的 CPA/TCPA 安全包络，不是 Rule 14 动作触发门**；§9.2:811-825 的 Rule 14 流程 Layer 5 三阶段时序**仅作用于 stand-on (Rule 17)**，不作用于 Rule 14。所以"代码 Rule 14 纯几何无 TCPA 门"与设计一致，不是偏离。**真正的设计缺陷是原报告本身没给 Rule 14 加 TCPA/分层时序门** —— 本文档 §3.2 将其作为设计升级补上。

---

## 3. 目标态架构

### 3.1 ODD 参数体系（M1 → 全链）

#### 3.1.1 设计偏离与目标

原报告 §3.3 / §9.3 给每个 ODD 一个单值 CPA/TCPA 阈值。桌面研究「参数建议.md」论证：单值阈值会同时导致"目标很远但 CPA≈0 → 过早触发"和"目标晚进入 WorldState → 过晚触发"。目标态改为 **4 层阈值 + 动态 CPA 函数**。

#### 3.1.2 4 层阈值结构

每个 ODD 的 CPA/TCPA 拆成 4 层（取代单值）：

| 层级 | 用途 | ODD-A 开阔水域 | ODD-B 狭水道/VTS | ODD-C 港内（注 1） | ODD-D 能见度不良 |
|---|---|---|---|---|---|
| **detect / observe** | 提前识别目标，不决策 | long-range 扫描 + AIS/雷达融合 + 方位稳定性 | 同 A，叠加航道边界 | ship-domain 监视 | 同 A，叠加雷达系统观测 |
| **monitor / caution** | 纳入威胁队列，HMI 显示 | soft CPA 1.2-1.5nm；TCPA 24-30min | soft CPA 0.5-0.7nm；TCPA 8-12min | 4-6 LOA | 对应 ODD ×1.5 |
| **plan / action** | M6/M4/M5 生成并执行可见避让 | hard CPA ≥ 1.0nm；TCPA 12-15min | hard CPA ≥ 0.3nm；TCPA 4-6min | 2-3 LOA | 对应 ODD ×1.5 + 强制降速 |
| **emergency / MRM** | M7 或 Reflex Arc 接管 | TCPA 4-5min；CPA 快速恶化 | TCPA 2-3min | ≤1 LOA 或停船距离 | MRM-01/02/03 |

> 注 1：ODD-C 的主指标改为 ship-domain / 停船距离 / LOA 倍数 / 泊位边界，**不再用海里级 CPA/TCPA 作为主触发**（见 §3.1.5）。

#### 3.1.3 动态 CPA_safe 函数

取代静态常数 `CPA_safe = fixed_value_by_ODD`：

```
CPA_safe = max(
    CPA_min_by_ODD,                 # §3.1.2 的 hard CPA
    k_LOA × LOA,                    # 船长相关（45m FCB 初值 k_LOA ≈ 3-5）
    k_stop × stopping_distance(speed, sea_state),   # Rule 6 停船距离
    lane_margin_or_channel_constraint,               # ODD-B 航道横向裕度
    sensor_uncertainty_margin                        # 感知不确定度
)
```

依据：COLREG Rule 6 要求安全航速考虑能见度、交通密度、操纵性、停船距离、风浪流等。对 45m 高速 FCB，CPA 不应是静态常数。

#### 3.1.4 ODD-D：Rule 19 规则集硬切换（取代数值放大）

原报告 §9.1:799 称"Rule 19 完全取代 Rule 13-17"，但代码无硬切换机制（D5）。目标态明确：**ODD-D 下 M6 `run_reasoning()` 显式 skip Rules 11-18**，仅运行 Rule 19 + Rule 5/6/7/8/9。

ODD-D 的 M6 输出语义改为：
```
rule_set = RULE_19_RESTRICTED_VISIBILITY
role = NO_STAND_ON_GIVE_WAY_SEMANTICS    # Rule 19 无 stand-on/give-way 责任分配
preferred_action = SAFE_SPEED + RADAR_BASED_AVOIDANCE
avoid_action = avoid_port_alteration_forward_of_beam unless overtaking   # Rule 19(d)
```

而非 `CPA_safe *= 1.5; TCPA_safe *= 1.5` 后继续让 M6 输出 give-way/stand-on。

#### 3.1.5 ODD-C：ship-domain 驱动（取代 CPA/TCPA）

ODD-C 主指标改为近距控制域，CPA/TCPA 仅作辅助显示：
```
range_to_target_m, range_rate_mps, bearing_rate,
stopping_distance_m, lateral_clearance_m,
berth_boundary_margin_m, thruster_authority_margin
```

理由：港内低速相遇 TCPA 会因相对速度很低而被拉长，易误判为"不急"，但船可能已进入泊位/岸壁/缆桩高风险区。

#### 3.1.6 ODD 参数单一真相源

当前 YAML（`odd_aware_thresholds.yaml`）ODD-A=1000m 与代码 fallback 默认 1852m（`colregs_reasoner_node.cpp:1153-1157`）不一致（D14）。目标态：**YAML 为唯一真相源**，代码 fallback 仅在 YAML 缺失时用作安全保守默认（取较大值 1852m），且加载时校验 YAML 完整性，缺失键即 fail-fast 报错而非静默回退。

---

### 3.2 M6 COLREGs Reasoner（Rules 13-19）

#### 3.2.1 Rule 14 显式状态机（设计升级，补原报告缺陷）

原报告 §9.2 Rule 14 纯几何（对遇 ±6° + 航向差 170-190°）无 TCPA 门，Layer 5 时序仅 stand-on。当前代码 `rule14_head_on.cpp:30,34,39` 实现 6°/6°/10° 阈值，与设计一致，但**过早/过晚触发问题**无法靠几何解决。目标态引入显式状态机：

```
Candidate:
    Rule 14 geometry true（course_diff |180|<6° ∧ rel_bearing ±6° ∧ aspect ±10°）

PrePlan（监视）:
    Candidate ∧ CPA < CPA_safe ∧ TCPA ≤ T_preplan    # ODD-A T_preplan ≈ 18-20min
    → SAT-2 展示，M4 不动作

Active（规划）:
    Candidate 稳定 N 周期 ∧ CPA < CPA_safe ∧ TCPA ≤ T_action    # ODD-A T_action ≈ 12-15min
    → M6 发布 BOTH_GIVE_WAY + STARBOARD locked
    → M4/M5 生成右转避让

Commit（执行锁定）:
    TCPA ≤ T_commit      # ODD-A T_commit ≈ 8min
    OR CPA not improving
    → 禁止 M4 把 STARBOARD 改成 pure REDUCE_SPEED（见 §3.3.2）

Release:
    target past-and-clear（|rel_bearing on ref_heading| > 112.5°）
    ∧ range opening
    ∧ CPA ≥ CPA_safe for dwell_time    # ODD-A dwell ≈ 60s
    → 状态机复位
```

关键：**Rule 14 的 onset 仍以几何为主，但动作门加 TCPA**，避免"目标 8nm 进入 WorldState 且 CPA≈0 就立即 latch"。

#### 3.2.2 Rule 17 三阶段时序（仅 stand-on，与原设计一致）

原报告 §9.2:821 三阶段时序设计正确，代码 `rule17_stand_on.cpp:40-75` + `colregs_phase_classifier.cpp:8-19` 实现正确。目标态：

- **明确声明**：三阶段时序仅作用于 Rule 17 stand-on，**不作用于 Rule 14 both-give-way**（Rule 14 用 §3.2.1 状态机）。
- **修复 D4**：launch `rule_set` 必须包含 `Rule17`，否则 stand-on 船无保向/警告/独立避让三阶段。

| 阶段 | 条件 | TDL 行为 |
|---|---|---|
| 保向 | TCPA > T_standOn | 保持航向航速，持续监控 |
| 警告 | T_standOn ≥ TCPA > T_act | SAT-2/M8 展示规则依据，准备升级 |
| 独立避让 | TCPA ≤ T_act | 直航船可独立避让，M5/M7 触发保守动作 |

#### 3.2.3 Rule 19 ODD-D 覆盖（硬切换）

见 §3.1.4。Rule 19 `rule19_restricted_visibility.cpp:21` 当前仅自门控（`if odd != ODD_D return inactive`），但 Rules 13-18 在 ODD-D 仍评估（`colregs_reasoner_node.cpp:548,688` 无 ODD 过滤）。目标态在 `run_reasoning()` 加框架级规则集切换。

#### 3.2.4 RuleLatch 释放机制（消除幽灵冲突，D8）

当前 RuleLatch 释放依赖投影（`tcpa_s ≤ 0.5s ∧ cpa_m ≥ cpa_safe_m`）+ reference-relative-bearing ≥ 40°，但 Sisyphus review 指出存在线性衰减定时器残留 60s 幽灵冲突。目标态：**释放条件改为 `range_opening ∧ CPA 回升 ∧ dwell_time`**，删除线性衰减定时器。

```
Release:
    range_closing == false                    # 距离正在拉开
    ∧ cpa_m ≥ cpa_safe_m                      # CPA 回到安全
    ∧ past_and_clear                          # 目标已让清（ref-heading 相对方位 > 112.5°）
    ∧ 持续 dwell_time（ODD-A 60s / ODD-B 45s / ODD-D 90s）
    → latched_ = false, released_past_clear_ = true
```

#### 3.2.5 RuleAssessment 真实化（D7）

当前 `colregs_reasoner_node.cpp:928-931` 硬编码 trigger_conditions 为 `"heading_diff < 22.5°"` + `"bearing_rate < 0.5°/min"`，但实际 Rule 14 用 6°/6°/10°，且 bearing_rate 从不计算。目标态：trigger_conditions 从 Rule 14 evaluate() 的实际命中条件动态生成，删除虚假字段。

#### 3.2.6 Rule 13 / Rule 15 几何（保持，明确边界）

| 规则 | 几何条件 | 责任 | 最小转向 |
|---|---|---|---|
| Rule 13 追越 | 艉扇区 [112.5°,247.5°] ∧ 同航向(≤45°) ∧ 速度差≥2kn | 追越方 GIVE_WAY / 被追越方 STAND_ON | max(min_alt, 65°) |
| Rule 14 对遇 | course_diff\|180\|<6° ∧ rel_bearing ±6° ∧ aspect ±10° | BOTH_GIVE_WAY | §3.2.1 状态机 |
| Rule 15 交叉 | starboard [22.5°,112.5°) / port [247.5°,337.5°) | starboard GIVE_WAY / port STAND_ON | max(min_alt, 50°) |

Rule 13/15 onset 仍纯几何（与原报告一致），RuleLatch + CPA/range 门控决定是否进入稳定避让。

---

### 3.3 M4 Behavior Arbiter

#### 3.3.1 COLREGs 约束作为硬约束（修正 D3）

原报告 §8.4:769-783 明确 M4 把 M6 的 `{role, direction, min_action}` 作为**硬约束**嵌入 IvP，使合规"不被其他行为目标压倒"。当前代码 `colregs_directive.cpp:151-160` 违反此设计：`can_reduce_speed` 显式接纳 `BothGiveWay`（`:153`），在 `ample_tcpa>180s ∧ outside_danger ∧ speed_reduction_improves/arrests` 时把 STARBOARD 改写为 REDUCE_SPEED。

目标态：**Rule 14 BOTH_GIVE_WAY 的 STARBOARD 不得改写为 pure REDUCE_SPEED**。具体：
- `can_reduce_speed` 排除 `BothGiveWay`（仅 `GiveWay` 可考虑减速辅助）。
- 即使 `GiveWay`，减速仅作 **speed constraint 附加**（降低速度上限），**不替代转向方向**。
- Rule 14 Commit 阶段（§3.2.1）强制锁定 STARBOARD，M4 不得改写。

```
原（错误）:
    can_reduce_speed = (duty == GiveWay || duty == BothGiveWay)
    if (can_reduce_speed && ample_tcpa && ...) directive.direction = ReduceSpeed

目标态:
    can_reduce_speed = (duty == GiveWay)   # 排除 BothGiveWay
    if (can_reduce_speed && ample_tcpa && ...) {
        directive.speed_reduction_preferred = true   # 附加 speed constraint
        // 不改写 directive.direction，STARBOARD 保留
    }
```

#### 3.3.2 heading window（保持）

STARBOARD allowed range = `[base_heading + required_deviation, base_heading + 180°]`（`colregs_directive.cpp:248-263`），与原设计一致，保留。

#### 3.3.3 required_deviation 计算（保持 + 明确）

`colregs_directive.cpp:174-198`：base floor = M6 `min_alteration_deg`；target 外 CPA_safe 时 `asin(cpa_safe/range) × boldness(2.5)`；target 内时 snap 到 `max_deviation_deg`。目标态保留，但 `min_alteration_deg` 必须用 §3.1 修正后的值（ODD-A 30° 而非 15°）。

#### 3.3.4 权重机制修复（修正 D11）

当前 `behavior_arbiter_node.cpp:224-230` 在 Rule 14 时把权重设到 0.85，但 `:620` `weighted_fns.push_back({10.0, avoid_fn})` 用硬编码 10.0，导致权重设置是 no-op。目标态：**让 `dictionary_.set_priority_weight` 真正生效**，IvP push 使用 dictionary 权重而非硬编码。或明确：COLREG_AVOID 在冲突期作为硬约束（权重 ∞ 或独立 hard constraint 通道），不走 IvP 软仲裁。

#### 3.3.5 新增 RETURN_TO_ROUTE behavior（补充 D12）

原报告 §8.3:754-766 行为字典仅 5 个 behavior，无 RETURN_TO_ROUTE。返航在代码里仅是 L2 `PlannedRoute` 末航点，走与避碰相同的 TRANSIT 链，冲突释放后无显式航迹恢复。目标态新增：

```
BehaviorType::RETURN_TO_ROUTE:
    trigger: COLREG conflict release（RuleLatch released_past_clear）
           ∧ 存在 L2 PlannedRoute 偏差
    action: 计算从当前位置到原航迹最近点的恢复路径
          ∧ 平滑切回 TRANSIT（避免急转）
    priority_weight: 0.6（介于 TRANSIT 0.3 与 COLREG_AVOID 0.7 之间）
    ODD 适用: A, B, D
```

这同时是返航失败（症状 B）的根因修复之一。

---

### 3.4 M5 Tactical Planner

#### 3.4.1 Mid-MPC solver 收敛契约（修正 D9）

当前 `mid_mpc_node.cpp:363-368` 源码自承"NLP solver is a Phase-3 stub that never converges"，`l3_params.yaml:29` `use_ipopt: false`。目标态二选一：

- **方案 A（推荐）**：接入真 MPC solver（CasADi/IPOPT 或等价），定义收敛契约：成功率 ≥ 95%，失败时进入 fallback。这是 D9.2（M5 NLP convergence，`fix/m5-nlp-convergence` 分支已开始）的延续。
- **方案 B（过渡）**：明确几何 fallback 为常态路径，定义 SLA：fallback 首航点时间 ≤ 15s（当前 60s，见 D13），corridor 合理，confidence 标注准确。

两个方案都必须让"避碰轨迹生成"有可预测的行为，而非依赖一个永不收敛的 stub。

#### 3.4.2 几何 fallback 首航点时间（修正 D13）

当前 `types.hpp:385` `geometric_fallback_waypoint_time_s` 使首航点在 t=60s，低速末段（返航）看起来"游荡"。目标态：首航点 ≤ 15s，后续航点按合理间距（10s）排布，覆盖 Mid-MPC 时域。

#### 3.4.3 CPA/TCPA scaling 隔离（修正 D13）

当前 `mid_mpc_node.cpp:267-268` 原地突变 `tgt.cpa_m *= 0.2` / `tgt.tcpa_s *= 0.2` 来 boost MPC cost weight，但同一突变后的数据泄露到 SAT-3 trajectory candidates 和几何 fallback 方向判断。目标态：**scaling 仅作用于 MPC cost weight 计算，不污染原始 M2 输入**。引入独立的 `cost_cpa_m` / `cost_tcpa_s` 字段，原始 `tgt.cpa_m` / `tgt.tcpa_s` 保持不变。

#### 3.4.4 BC-MPC topic 前缀修复（修正 D10）

当前 `bc_mpc_node.cpp:35` 订阅 `/m2/world_state`（缺 `/l3/` 前缀），其他模块统一 `/l3/m2/world_state`，导致 BC-MPC 永远收不到 world state → 永不激活 → 无 10Hz 短程紧急避碰兜底。目标态：统一为 `/l3/m2/world_state`。

#### 3.4.5 AvoidancePlan vs ReactiveOverrideCmd 选择语义

- **Mid-MPC**（1Hz）发布 `AvoidancePlan`（航点序列 + 速度），覆盖 L2 PlannedRoute，L4 继续 LOS+WOP。
- **BC-MPC**（10Hz）发布 `ReactiveOverrideCmd`（绝对 heading/speed/ROT + validity），仅在 `sol.status == Override` 时发布，CPA 急剧恶化时触发。
- 桥接层：Override 有效时用 Override，否则跟 AvoidancePlan。

目标态明确两者非互斥仲裁，而是短程 Override 覆盖长程 Plan。

---

### 3.5 M3 Mission Manager 返航一等行为（补充）

原报告 §7.2:702 M3 是"任务令有效性看门人 + ETA 投影器 + 重规划请求触发器"，未把返航列为一等行为。当前代码 `mission_state_machine.cpp:12-20` 仅 7 状态（Init/Idle/TaskValidation/AwaitingRoute/Active/ReplanWait/MrcTransit），返航仅靠 L2 末航点 + M3 末段 1000m lookahead（`mission_manager_node.cpp:29,788-823`）。

目标态：返航作为一等行为，与 §3.3.5 RETURN_TO_ROUTE 配合：
- M3 增加 `ReturningHome` 状态（或显式 mission phase 标记）。
- 返航段遇 COLREG 冲突 → 走正常避碰链 → 冲突释放后 M4 切 RETURN_TO_ROUTE → M3 维持返航目标，M4/M5 生成恢复路径回原返航航迹。
- 末段 lookahead 保留，但与 RETURN_TO_ROUTE 衔接，避免"避碰后丢失返航目标"。

---

### 3.6 M7 Safety Supervisor（明确化启用）

原报告 §11.6:1077-1082 MRM 命令集 + P1-P5 优先级（`:1196-1239`）设计完整。当前 `l3_params.yaml:39` `veto_enabled: false`，M7 进程未运行（D6）。目标态明确启用条件：

- `veto_enabled: true`，M7 进程随 compose 启动。
- M7 监控两类：IEC 61508 组件失效（超时/心跳/输出矛盾）+ SOTIF 功能不足（AIS 关闭/预测残差恶化/感知盲区/COLREGs 不可解析/CPA 趋势恶化）。
- M7 触发 `Safety_AlertMsg` 携带 `recommended_mrm = MRM-01/02/03/04`，由 M1 仲裁下发。
- M7 **不重新规划**，只触发预定义 MRM（ADR-001 独立性）。
- P1 Reflex（CPA<50m ∧ TCPA<30s）绕过 L3/L4；P2 M7 VETO 10ms 一票否决。

| MRM | 动作 | 触发 |
|---|---|---|
| MRM-01 | 减速至安全速度并维持航向 | 一般性能退化 / SOTIF 告警 |
| MRM-02 | 停车 / 漂航 | Checker 多次否决 / 假设违反 |
| MRM-03 | 紧急转向 | CPA 急剧恶化 |
| MRM-04 | 抛锚序列 | 港内 / 近岸 + 系统失能 |

---

## 4. 接口契约变更表

所有变更保留原报告 §15 接口契约的 `stamp` / `schema_version` / `confidence` / `rationale` 强制字段。

| 消息 | 字段 | 变更类型 | 说明 |
|---|---|---|---|
| `COLREGsConstraint` | `phase` | 语义扩展 | 现值 "T_standOn\|T_warn\|T_act\|T_emergency"；Rule 14 状态机增加 "PrePlan" 阶段标识（可复用 phase 字符串或新增） |
| `COLREGsConstraint` | `primary_preferred_direction` | 语义强化 | Rule 14 Commit 阶段锁定 STARBOARD，M4 不得改写 |
| `RuleActive` | `min_alteration_deg` | 值修正 | ODD-A 从 15° → 30°（恢复设计值） |
| `RuleAssessment` | `trigger_conditions[]` | 真实化 | 从 Rule 14 evaluate() 动态生成，删除虚假 "bearing_rate<0.5°/min" |
| `RuleAssessment` | `applicable_rule` | 扩展 | Rule 14 状态机阶段信息（Candidate/PrePlan/Active/Commit/Release） |
| `BehaviorPlan` | `behavior` 枚举 | 新增 RETURN_TO_ROUTE | 见 §3.3.5 |
| `WorldState` → M5 | `targets[].cpa_m` | 不再原地突变 | M5 scaling 移到独立 cost 字段（§3.4.3） |
| ODD 参数 YAML | 4 层阈值 | 结构变更 | 单值 → detect/monitor/plan/action/emergency（§3.1.2） |
| launch `rule_set` | 加 Rule17/Rule19 | 配置变更 | 激活三阶段时序 + ODD-D 切换（D4/D5） |

---

## 5. 与原报告差异声明

逐条列出本文档对原报告的修正/补充/明确化，便于后续合并评审。

| 原报告章节 | 原报告内容 | 本文档 | 关系 | 依据 |
|---|---|---|---|---|
| §3.3:260 | ODD-A CPA ≥ 1.0nm / TCPA ≥ 12min（单值） | §3.1.2 4 层阈值 | 修正 | D1 + 参数建议.md |
| §9.3:834 | ODD-A min_alteration 30° | §3.1 + §3.3.3 保持 30° | 明确化（代码应恢复） | D2 |
| §9.2:811-825 | Rule 14 纯几何，Layer 5 仅 stand-on | §3.2.1 Rule 14 显式状态机 + TCPA 门 | 补充（设计升级） | 避碰说明.md + 8-probe review |
| §9.1:799 | "Rule 19 完全取代 Rule 13-17" | §3.1.4 + §3.2.3 硬切换机制 | 修正（定义实现机制） | D5 |
| §9.2:821 | Rule 17 三阶段时序 | §3.2.2 仅 stand-on（明确声明） | 明确化 | D4 |
| §8.3:754-766 | 5 个 behavior，无 RETURN_TO_ROUTE | §3.3.5 新增 RETURN_TO_ROUTE | 补充 | D12 |
| §8.4:769-783 | M4 COLREGs 作为硬约束 | §3.3.1 禁止 STARBOARD→REDUCE_SPEED | 修正（代码违反设计） | D3 |
| §4.5:455 | M5 MPC 选型 | §3.4.1 solver 收敛契约 / fallback SLA | 补充 | D9 |
| §7.2:702 | M3 三角色 | §3.5 返航一等行为 | 补充 | D12 + 症状 B |
| §11.6:1077 | MRM 命令集 | §3.6 启用条件 | 明确化 | D6 |
| §15 | 接口契约总表 | §4 字段变更 | 补充 | D7/D13 |

---

## 6. 待 HAZID 标定项清单

以下参数仍为工程初值，须在 HAZID RUN-001（蒙特卡洛 + Rule 8/13-17 合规验证）标定后冻结。本文档不展开标定策略。

| 参数 | 当前值（待标定） | 标定方法 |
|---|---|---|
| ODD-A 4 层 CPA/TCPA | detect/monitor/plan/action/emergency 初值见 §3.1.2 | 蒙特卡洛 + Rule 8 合规 |
| ODD-A `min_alteration_deg` | 30°（恢复设计值，HAZID 复核） | Rule 8 "明显大幅"工程判据 |
| `k_LOA` / `k_stop` in 动态 CPA | 初值 3-5 / TBD | 45m FCB 实船操纵试验 |
| Rule 14 状态机 `T_preplan`/`T_action`/`T_commit` | §3.1.2 ODD-A 初值 | Wang 2021 + Imazu 1987 + 蒙特卡洛 |
| RuleLatch `dwell_time` | ODD-A 60s / B 45s / D 90s | CPA 回升稳定性观测 |
| ODD-C ship-domain 阈值 | 4-6 LOA 监视 / 2-3 LOA 动作 | 港规 + 实船 |
| Rule 19 visibility trigger | 1.0nm（原报告锁定，不可 HAZID 改） | Rule 19(b) 字面 |

---

## 附录 A：症状根因链（便于评审）

### 症状 A：避碰行为失败 / 不像正常 COLREGs 流程
```
M6: Rule14 几何命中 → BOTH_GIVE_WAY + STARBOARD + min_alt=15°(D2 偏小)
   ↓
M4: TCPA>180s ∧ 非 Danger → STARBOARD 被改写为 REDUCE_SPEED (D3)
     + 权重提升 0.85 是 no-op (D11)
   ↓
M5: colregs_preferred_direction=ReduceSpeed → 不再横向避让
     + Mid-MPC solver 永不收敛 (D9) → 只走几何 fallback
     + fallback 首航点 t=60s → 游荡 (D13)
     + BC-MPC topic bug 永不激活 → 无短程兜底 (D10)
   ↓
M7: veto_enabled=false → 无 doer-checker 否决兜底 (D6)
```

### 症状 B：返航行为失败
```
返航 = L2 末航点走 TRANSIT 链（非一等行为，D12）
   + 末段遇 COLREG 冲突 → 同症状 A 链 (D3/D9/D10)
   + 几何 fallback 首航点 t=60s → 末段游荡 (D13)
   + 无 RETURN_TO_ROUTE → 冲突释放后无航迹恢复 (D12)
   + COLREG release dwell 4 周期延迟恢复
```

**根因优先级**：D3 + D9 + D10 + D6 是两个症状的共同核心；D12 是返航特有根因。修完 P0（路线图 §3.1）后两个症状应有肉眼可见改善。

---

*本文档为 COLREGs 避碰链目标态设计。具体整改步骤、WBS、验证闭环见 `COLREGs避碰链-整改路线图.md`。*
