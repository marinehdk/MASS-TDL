# MASS_ADAS COLREGs Engine 避碰规则引擎技术设计文档

**文档编号：** SANGO-ADAS-COL-001  
**版本：** 0.1 草案  
**日期：** 2026-04-25  
**编制：** 系统架构组  
**状态：** 开发参考

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的避碰决策思维模型
5. 计算流程总览
6. 步骤一：会遇态势分类
7. 步骤二：船舶类型优先序判定（Rule 18）
8. 步骤三：让路/直航责任判定
9. 步骤四：行动方式推荐
10. 步骤五：行动时机判定
11. 步骤六：行动幅度计算
12. 步骤七：多目标冲突消解
13. 狭水道规则实现（Rule 9）
14. 分道通航制规则实现（Rule 10）
15. 能见度不良规则实现（Rule 19）
16. 安全速度判定（Rule 6）
17. 直航船的逐级行动义务（Rule 17）
18. 责任条款与良好船艺（Rule 2）
19. 内部子模块分解
20. 数值算例
21. 参数总览表
22. 与其他模块的协作关系
23. 测试策略与验收标准
24. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

COLREGs Engine（避碰规则引擎）是 Layer 3（Tactical Layer）的决策核心。它接收 Target Tracker 输出的威胁列表，对每个 threat_level ≥ MEDIUM 的目标，依据《1972 年国际海上避碰规则》（COLREGs）及其修正案，判定会遇态势类型、本船责任（让路船/直航船）、推荐的行动方式（转向/减速/停船）、行动时机和行动幅度。

COLREGs Engine 是 MASS_ADAS 中最具挑战性的单一模块。它需要将一套以自然语言表述的、包含大量模糊判断的国际法律规则，转化为确定性的算法逻辑。真实海上的船长可能对同一态势做出不同但都"合理"的决策——COLREGs Engine 必须选择一套唯一的、可复现的、保守的量化标准。

### 1.2 核心职责

- **会遇分类：** 判定每对船舶之间的会遇态势类型（对遇/交叉/追越/安全通过）。
- **责任判定：** 确定本船在每个会遇中的角色（让路船/直航船/双方让路）。
- **行动推荐：** 输出推荐的避让方式（向右转/向左转/减速/停船/维持）。
- **时机判定：** 确定何时必须开始执行避让行动。
- **幅度计算：** 计算转向或减速的具体量值。
- **冲突消解：** 当多个目标同时需要避让时，找到一个同时满足所有目标的统一方案。
- **特殊规则适用：** 根据水域类型和能见度条件，应用狭水道规则、TSS 规则、能见度不良规则等特殊条款。

### 1.3 设计原则

- **规则忠实原则：** 算法逻辑必须忠实于 COLREGs 原文。每个判定逻辑都应在文档中标注对应的规则条款编号。
- **保守量化原则：** 对规则中的模糊用语（"及早"、"大幅"、"明显"），采用保守的量化标准。宁可过早行动、过大幅度，不可犹豫不决。
- **安全优先原则（Rule 2）：** 当严格遵守规则可能导致碰撞时（如对方不遵守规则），系统有权偏离规则，以避碰为最高优先级。
- **可审计原则：** 每个决策输出都附带完整的决策依据链——应用了哪些规则、基于什么数据、为何选择此行动。便于事后审查和船级社审计。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 说明 |
|-------|------|------|
| 威胁目标列表 | Target Tracker | ThreatList 消息，按 threat_score 降序排列 |
| 本船状态 | Nav Filter | 位置、航向、航速、转向角速度 |
| 水域类型 | ENC Reader | ZoneClassification（开阔/狭水道/TSS/港内等） |
| 能见度 | Perception | EnvironmentState.visibility_nm |
| 本船类型参数 | Parameter DB | 本船在 COLREGs 中的类别（机动船/限于吃水船等） |
| 本船操纵性参数 | Parameter DB | 旋回半径、停船距离（用于行动幅度计算） |

### 2.2 输出

**每个需要处理的目标输出一个 ColregsDecision：**

```
ColregsDecision:
    Header header
    
    # ---- 目标标识 ----
    uint32 target_track_id              # 对应 Target Tracker 的 track_id
    uint32 target_mmsi                  # AIS MMSI
    
    # ---- 会遇分类 ----
    string encounter_type               # 见第 6 节定义
    float64 encounter_confidence        # 分类置信度 [0, 1]（在模糊区降低）
    
    # ---- 责任判定 ----
    string my_role                      # "give_way" / "stand_on" / "both_give_way" / "overtaking_give_way"
    string[] applicable_rules           # 适用的规则条款列表，如 ["Rule 15", "Rule 16"]
    
    # ---- 行动推荐 ----
    string recommended_action           # "alter_course_starboard" / "alter_course_port" / "reduce_speed" / "stop" / "maintain" / "alter_course_and_reduce_speed"
    float64 recommended_course_change   # 度（正=右转，负=左转）
    float64 recommended_speed_change    # m/s（负=减速）
    float64 recommended_new_speed       # 推荐的新目标速度（m/s）
    
    # ---- 行动时机 ----
    bool action_required                # 是否需要立即行动
    float64 action_deadline_sec         # 必须在此时间内行动（距当前时间，秒）
    string urgency                      # "routine" / "urgent" / "emergency"
    
    # ---- 预期效果 ----
    float64 expected_cpa_after_action   # 执行推荐行动后的预期 CPA（米）
    float64 expected_tcpa_after_action  # 执行后的预期 TCPA（秒）
    
    # ---- 决策依据（审计追溯用）----
    string decision_rationale           # 人类可读的决策理由
    ColregsRuleApplication[] rules_applied  # 逐条应用的规则及判定过程

ColregsRuleApplication:
    string rule_number                  # "Rule 13" / "Rule 14" / "Rule 15" 等
    string rule_name                    # "Overtaking" / "Head-on" / "Crossing" 等
    string evaluation_result            # "APPLICABLE" / "NOT_APPLICABLE" / "INCONCLUSIVE"
    string details                      # 判定过程说明
```

**同时输出一个全局态势摘要：**

```
ColregsSituationSummary:
    Header header
    uint32 total_encounters             # 当前需要处理的会遇总数
    uint32 give_way_count               # 我船为让路船的会遇数
    uint32 stand_on_count               # 我船为直航船的会遇数
    bool multi_target_conflict          # 是否存在多目标冲突
    string overall_recommendation       # 全局推荐（当有多目标时为统一方案）
    float64 overall_course_change       # 统一航向变化量
    float64 overall_speed_change        # 统一速度变化量
```

---

## 3. 核心参数数据库

### 3.1 会遇分类角度阈值

以下角度阈值来自 COLREGs 规则原文和业界共识的量化标准：

| 参数 | 值 | 来源 |
|------|-----|------|
| 追越扇区半角 | 67.5°（从正后方起算） | Rule 13: "从正横后大于 22.5° 的方向赶上" |
| 追越扇区起始角 | 112.5°（相对方位） | 360° - 2 × 67.5° = 225°... 即 112.5° ~ 247.5° |
| 追越扇区结束角 | 247.5°（相对方位） | |
| 对遇判定角度阈值 | ±6°（相对方位偏离正前方） | Rule 14: "几乎相反的航向" |
| 对遇航向差范围 | 170° ~ 190° | Rule 14: 两船航向差接近 180° |
| 模糊区半宽 | 5° | 在分类边界 ± 5° 范围内按保守处理 |

**关键说明：** COLREGs Rule 13 定义追越是"从另一船的正横后大于 22.5° 的方向上赶上该船"。换算为相对方位：正横后 22.5° 对应相对方位 112.5°（右舷）和 247.5°（左舷）。目标的相对方位在 112.5° ~ 247.5° 范围内时，属于追越扇区。

### 3.2 行动时机参数

| 参数 | 开阔水域 | 沿岸 | 狭水道 | 港内 |
|------|---------|------|--------|------|
| 让路船行动 TCPA 阈值 T_give_way | 480s (8min) | 360s (6min) | 240s (4min) | 180s (3min) |
| 直航船预警 TCPA 阈值 T_stand_on_warn | 360s (6min) | 270s (4.5min) | 180s (3min) | 120s (2min) |
| 直航船行动 TCPA 阈值 T_stand_on_act | 240s (4min) | 180s (3min) | 120s (2min) | 90s (1.5min) |
| 紧急行动 TCPA 阈值 T_emergency | 60s (1min) | 45s | 30s | 20s |

### 3.3 行动幅度参数

| 参数 | 值 | 来源 |
|------|-----|------|
| 最小航向变化量（让路） | 30° | Rule 8: "大幅度" 的量化最低标准 |
| 推荐航向变化量 | 30° ~ 60° | 实践中 30° 已足够明显，60° 用于近距离或复杂态势 |
| 最大常规航向变化量 | 90° | 超过 90° 优先考虑减速替代 |
| 最大紧急航向变化量 | 180°（掉头） | 仅 EMERGENCY 态势 |
| 避让维持最短时间 | 120s (2min) | Rule 8: "明显" 的量化标准 |
| CPA 安全余量系数 | 1.5 | 避让后预期 CPA ≥ CPA_safe × 1.5 |
| BCR 安全系数 | 2.0 | 避免从对方船首近距离通过 |

### 3.4 本船类型参数

COLREGs Rule 18 定义了不同类型船舶之间的优先序。本船在 MASS_ADAS 中的类型需要在 Parameter DB 中配置：

```
own_ship_type:
    colregs_category: "power_driven"    # 机动船（最常见）
    # 其他可能值：
    # "not_under_command"     失去控制的船
    # "restricted_ability"   操纵能力受限的船
    # "constrained_by_draft" 限于吃水的船
    # "sailing"              帆船
    # "fishing"              从事捕鱼的船
    
    is_vessel_under_20m: false           # 是否小于 20 米
    is_air_cushion_vessel: false         # 是否气垫船
    is_wing_in_ground: false             # 是否地效翼船
```

---

## 4. 船长的避碰决策思维模型

### 4.1 船长如何做避碰决策

一个经验丰富的船长面对一个正在接近的目标时，他的决策过程非常系统化，尽管看起来像是"凭直觉"。他的思维链是这样的：

**第一步：这是什么类型的会遇？** 他看雷达上目标的相对方位和航向——"这家伙是对着我来的（对遇）、从我右边斜插过来的（交叉）、还是我在追他（追越）？" 这个判断几乎是瞬间完成的，因为他已经做了几十年。

**第二步：我该谁让谁？** 会遇类型决定了责任分配。对遇时双方都该让（都向右转）。交叉时看对方在哪一侧——右舷来的我让，左舷来的他让（或者至少他应该让）。追越时追的一方让。

**第三步：但是，有没有特殊情况？** 对方是帆船？渔船？操纵能力受限的船？如果是，即使按交叉规则我是直航船，我也可能需要让路，因为 Rule 18 规定了类型优先序。

**第四步：我什么时候必须动？** 这取决于距离、速度和 CPA。如果 TCPA 还有 15 分钟，我可以再观察一会儿。如果只有 5 分钟了，我必须现在就动。如果只有 2 分钟，我已经有点晚了。

**第五步：我怎么动？** 通常向右转。转多少度取决于需要让出多大的 CPA。一般先试 30°——如果不够就加到 45° 或 60°。如果转向空间不够（比如右边有浅水），那就减速或者甚至停船。

**第六步：动了之后效果怎么样？** 动完了不是就万事大吉——要持续盯着看 CPA 是不是真的在增大。如果对方也在动（比如他也在让路，或者他在做奇怪的动作），情况可能变化。

### 4.2 船长绝对不做的事

- **不会犹豫不决。** 做了决定就要执行得干脆——犹犹豫豫的小幅转向比不转还危险，因为对方无法判断你在干什么。
- **不会向左转来避让右舷来船。** 这违反 COLREGs 且可能导致两船同时向对方方向转。
- **不会在正横前方有目标时向该目标方向转。** Rule 19(d) 明确禁止。
- **不会做了一个大动作后又立刻恢复原航向。** 避让动作必须维持足够长的时间让对方看到。

### 4.3 COLREGs 规则结构概览

COLREGs 共 41 条规则和 4 个附件，其中与避碰决策直接相关的核心规则是：

| 规则 | 名称 | 核心内容 | 算法化要点 |
|------|------|---------|----------|
| Rule 2 | 责任 | 遵守规则不免除因特殊情况需偏离规则的责任 | 安全优先覆盖 |
| Rule 5 | 了望 | 必须保持正规了望 | Perception 子系统的职责 |
| Rule 6 | 安全速度 | 随时保持安全速度 | 速度上限约束 |
| Rule 7 | 碰撞危险 | 判断是否存在碰撞危险 | CPA/TCPA 判定 |
| Rule 8 | 避碰行动 | 行动应及早、大幅、明显 | 行动幅度和时机 |
| Rule 9 | 狭水道 | 靠右行驶、不妨碍大船 | 特殊规则 |
| Rule 10 | 分道通航制 | 沿分道方向、横穿规则 | 特殊规则 |
| Rule 13 | 追越 | 追越船让路 | 会遇分类 |
| Rule 14 | 对遇 | 双方右转 | 会遇分类 |
| Rule 15 | 交叉相遇 | 右舷来船我让 | 会遇分类 |
| Rule 16 | 让路船行动 | 及早大幅行动 | 行动规范 |
| Rule 17 | 直航船行动 | 保持→可行动→必须行动 | 逐级义务 |
| Rule 18 | 船舶类型优先序 | 失控>受限>限吃水>帆船>渔船>机动船 | 类型判定 |
| Rule 19 | 能见度不良 | 不适用 13/14/15，禁止左转 | 特殊模式 |

---

## 5. 计算流程总览

```
输入：ThreatList（威胁目标列表）+ 本船状态 + 水域/能见度
          │
          ▼
    ┌──────────────────────────────────┐
    │ 步骤一：会遇态势分类              │ ← Rule 13/14/15
    │ 对每个 threat_level ≥ MEDIUM 的目标│
    │ 判定：对遇/交叉/追越/安全通过      │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤二：船舶类型优先序判定         │ ← Rule 18
    │ 检查目标是否有优先权               │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤三：让路/直航责任判定          │ ← Rule 13~18
    │ 综合会遇类型和类型优先序           │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤四：行动方式推荐              │ ← Rule 8/16
    │ 向右转？减速？停船？组合？         │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤五：行动时机判定              │ ← Rule 8/16/17
    │ 现在行动？等待？紧急？             │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤六：行动幅度计算              │ ← Rule 8
    │ 转多少度？减速多少？               │
    │ 验证预期 CPA ≥ CPA_safe × 1.5    │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤七：多目标冲突消解（如需要）   │ ← Rule 2
    │ 找到同时满足所有目标的统一方案      │
    └────────────┬─────────────────────┘
                 │
                 ▼
输出：ColregsDecision[]（每个目标一个）+ ColregsSituationSummary
```

**特殊规则覆盖：** 在步骤一之前，先检查水域类型和能见度，决定是否需要应用特殊规则模式：

```
IF visibility < VISIBILITY_THRESHOLD:
    → 切换到 Rule 19 模式（能见度不良，见第 15 节）
    → 跳过步骤一的常规分类，直接使用 Rule 19 逻辑

IF zone_type == "narrow_channel":
    → 在步骤四中额外应用 Rule 9 约束（见第 13 节）

IF zone_type == "tss_lane":
    → 在步骤四中额外应用 Rule 10 约束（见第 14 节）

VISIBILITY_THRESHOLD = 2.0 nm（经验值，可配置）
```

---

## 6. 步骤一：会遇态势分类

### 6.1 分类判定的输入数据

对于每个目标，需要以下来自 Target Tracker 的数据：

```
target.bearing_relative    # 目标相对于本船船首的方位（0°~360°，顺时针）
target.cog                 # 目标对地航向
own_ship.heading           # 本船航向
own_ship.cog               # 本船对地航向
target.sog                 # 目标速度
own_ship.sog               # 本船速度
target.range_rate          # 距离变化率（负=接近）
target.cpa                 # 最近会遇距离
```

### 6.2 分类算法

```
FUNCTION classify_encounter(own_ship, target):
    
    # ---- 前置检查：是否存在碰撞危险？（Rule 7）----
    IF target.cpa > CPA_safe × 3.0 AND target.range_rate > 0:
        # CPA 远大于安全距离且目标正在远离
        RETURN {encounter_type: "safe_passing", my_role: "none", applicable_rules: ["Rule 7"]}
    
    IF target.tcpa < 0 AND target.range > CPA_safe × 2.0:
        # CPA 已过且目标已在安全距离外
        RETURN {encounter_type: "safe_passing", my_role: "none", applicable_rules: ["Rule 7"]}
    
    # ---- 计算关键角度 ----
    
    # 目标相对方位（从本船船首顺时针度量）
    rel_bearing = target.bearing_relative
    
    # 航向差：目标航向相对于本船航向的差值
    heading_diff = normalize_pm180(target.cog - own_ship.cog)
    
    # ---- Rule 13 检验：追越？ ----
    # 追越的定义：从另一船的正横后大于 22.5° 的方向赶上该船
    # 即：目标看我方的相对方位在追越扇区内
    # 反过来说：我看目标的相对方位在 112.5° ~ 247.5° 意味着目标在我的后方扇区
    # 但这里要判定的是"谁追谁"
    
    # 计算目标看本船的相对方位（reciprocal bearing）
    target_sees_us_bearing = normalize_0_360(
        compute_bearing(target.position, own_ship.position) - target.cog
    )
    
    own_sees_target_bearing = rel_bearing
    
    # 判定追越关系
    overtaking_result = check_overtaking(own_ship, target, own_sees_target_bearing, target_sees_us_bearing)
    IF overtaking_result.is_overtaking:
        RETURN overtaking_result
    
    # ---- Rule 14 检验：对遇？ ----
    head_on_result = check_head_on(own_ship, target, rel_bearing, heading_diff)
    IF head_on_result.is_head_on:
        RETURN head_on_result
    
    # ---- Rule 15 检验：交叉相遇 ----
    crossing_result = check_crossing(own_ship, target, rel_bearing, heading_diff)
    RETURN crossing_result
```

### 6.3 追越判定详解（Rule 13）

```
FUNCTION check_overtaking(own_ship, target, own_sees_target, target_sees_us):
    
    # COLREGs Rule 13 原文要点：
    # (a) "从另一船的正横后大于 22.5° 的方向上赶上该船" 即为追越
    # (b) 追越船应给被追越船让路
    # (c) 一旦判定为追越，直到最终驶过让清为止，不因方位变化而改变
    # (d) 有疑问时，应认为是追越
    
    OVERTAKE_SECTOR_START = 112.5    # 正横后 22.5° 对应的相对方位
    OVERTAKE_SECTOR_END = 247.5
    
    # 情况 A：我船追越目标（目标在我前方，但我速度更快）
    # 判定条件：目标看我方的相对方位在追越扇区内（即我在目标的后方扇区接近）
    IF OVERTAKE_SECTOR_START ≤ target_sees_us ≤ OVERTAKE_SECTOR_END:
        IF own_ship.sog > target.sog AND target.range_rate < 0:
            # 我在从目标的后方接近，且我更快
            RETURN {
                is_overtaking: true,
                encounter_type: "overtaking",
                my_role: "overtaking_give_way",    # 我是追越船，我让路
                applicable_rules: ["Rule 13(a)", "Rule 13(b)"],
                confidence: compute_overtake_confidence(target_sees_us)
            }
    
    # 情况 B：目标追越我船
    IF OVERTAKE_SECTOR_START ≤ own_sees_target ≤ OVERTAKE_SECTOR_END:
        IF target.sog > own_ship.sog AND target.range_rate < 0:
            # 目标在我的后方接近，且它更快
            RETURN {
                is_overtaking: true,
                encounter_type: "being_overtaken",
                my_role: "stand_on",    # 我是被追越船，保持航向航速
                applicable_rules: ["Rule 13(a)", "Rule 13(b)"],
                confidence: compute_overtake_confidence(own_sees_target)
            }
    
    RETURN {is_overtaking: false}

FUNCTION compute_overtake_confidence(bearing_in_sector):
    # 在扇区边界附近，置信度降低
    dist_from_center = abs(bearing_in_sector - 180)   # 距正后方的偏离
    IF dist_from_center < 60:    # 距正后方 < 60°，深在扇区内
        RETURN 1.0
    ELSE:    # 接近扇区边界
        RETURN 0.6 + 0.4 × (67.5 - dist_from_center) / 67.5
```

### 6.4 对遇判定详解（Rule 14）

```
FUNCTION check_head_on(own_ship, target, rel_bearing, heading_diff):
    
    # COLREGs Rule 14 原文要点：
    # (a) 两机动船在相反或接近相反的航向上相遇，存在碰撞危险时，
    #     各应向右转向，从对方左舷通过
    # (b) 当一船看到他船在正前方或接近正前方...航向相反或接近相反
    # (c) 有疑问时，应认为是对遇
    
    HEAD_ON_BEARING_TOLERANCE = 6.0     # 目标在正前方 ±6° 内
    HEAD_ON_HEADING_MIN = 170.0          # 航向差最小值
    HEAD_ON_HEADING_MAX = 190.0          # 航向差最大值
    
    # 条件 1：目标在正前方附近
    bearing_from_bow = min(rel_bearing, 360 - rel_bearing)   # 距船首的最小角度
    is_ahead = (bearing_from_bow ≤ HEAD_ON_BEARING_TOLERANCE)
    
    # 条件 2：航向接近相反
    abs_heading_diff = abs(heading_diff)
    is_reciprocal = (abs_heading_diff ≥ HEAD_ON_HEADING_MIN AND abs_heading_diff ≤ HEAD_ON_HEADING_MAX)
    
    # 严格对遇
    IF is_ahead AND is_reciprocal:
        RETURN {
            is_head_on: true,
            encounter_type: "head_on",
            my_role: "both_give_way",
            applicable_rules: ["Rule 14(a)"],
            confidence: 1.0
        }
    
    # Rule 14(c)：有疑问时应认为是对遇
    # 扩大判定范围：方位 ±15°，航向差 160°~200°
    is_ahead_fuzzy = (bearing_from_bow ≤ 15.0)
    is_reciprocal_fuzzy = (abs_heading_diff ≥ 160.0 AND abs_heading_diff ≤ 200.0)
    
    IF is_ahead_fuzzy AND is_reciprocal_fuzzy:
        # 在模糊区——按保守原则判定为对遇
        RETURN {
            is_head_on: true,
            encounter_type: "head_on",
            my_role: "both_give_way",
            applicable_rules: ["Rule 14(a)", "Rule 14(c) - doubt resolved as head-on"],
            confidence: 0.7
        }
    
    RETURN {is_head_on: false}
```

### 6.5 交叉相遇判定详解（Rule 15）

```
FUNCTION check_crossing(own_ship, target, rel_bearing, heading_diff):
    
    # COLREGs Rule 15 原文要点：
    # 两机动船交叉相遇，存在碰撞危险时，
    # 有他船在本船右舷的船舶应给他船让路，
    # 如当时环境许可，还应避免横越他船的前方
    
    # 如果走到这里，说明不是追越也不是对遇，那就是交叉
    
    # 判定目标在右舷还是左舷
    IF 0 < rel_bearing < 112.5:
        # 目标在右舷前方
        RETURN {
            encounter_type: "crossing",
            my_role: "give_way",    # 目标在我右舷，我让路
            applicable_rules: ["Rule 15"],
            confidence: compute_crossing_confidence(rel_bearing),
            decision_rationale: f"目标在本船右舷方位 {rel_bearing:.0f}°，本船为让路船"
        }
    
    ELIF 247.5 < rel_bearing < 360:
        # 目标在左舷前方
        RETURN {
            encounter_type: "crossing",
            my_role: "stand_on",    # 目标在我左舷，我是直航船
            applicable_rules: ["Rule 15", "Rule 17"],
            confidence: compute_crossing_confidence(360 - rel_bearing),
            decision_rationale: f"目标在本船左舷方位 {rel_bearing:.0f}°，本船为直航船"
        }
    
    ELSE:
        # 目标在正横或正横后附近——不应到达这里（应已被追越规则捕获）
        # 保守处理：视为我需要让路
        RETURN {
            encounter_type: "crossing",
            my_role: "give_way",
            applicable_rules: ["Rule 15", "Rule 2 - precaution"],
            confidence: 0.5,
            decision_rationale: "方位处于模糊区域，按保守原则判定为让路船"
        }

FUNCTION compute_crossing_confidence(angle_from_boundary):
    # 距分类边界越远，置信度越高
    IF angle_from_boundary > 30:
        RETURN 1.0
    ELIF angle_from_boundary > 10:
        RETURN 0.8
    ELSE:
        RETURN 0.6    # 接近边界——模糊区
```

### 6.6 分类结果的锁定机制

COLREGs Rule 13(c) 规定："追越船在最终驶过让清被追越船之前，即使相互间的方位有所改变，仍应继续让路。" 这意味着一旦判定为追越，不应因后续方位变化而重新分类。

```
FUNCTION apply_classification_lock(track, new_classification):
    
    IF track.locked_encounter_type IS NOT NULL:
        # 已锁定的分类
        IF track.locked_encounter_type == "overtaking" AND new_classification != "safe_passing":
            # Rule 13(c)：追越关系锁定，直到安全通过
            RETURN track.locked_encounter_type
        
        IF track.lock_duration < CLASSIFICATION_LOCK_DURATION:
            # 锁定期内不变更分类（防止因数据波动反复切换）
            RETURN track.locked_encounter_type
    
    # 更新分类
    track.locked_encounter_type = new_classification
    track.lock_duration = 0
    RETURN new_classification

CLASSIFICATION_LOCK_DURATION = 60 秒    # 分类变更至少间隔 60 秒
```

---

## 7. 步骤二：船舶类型优先序判定（Rule 18）

### 7.1 Rule 18 优先序

COLREGs Rule 18 定义了不同类型船舶之间的让路优先序（从高到低）：

```
优先级 1（最高）：失去控制的船舶（Not Under Command, NUC）
优先级 2：操纵能力受限的船舶（Restricted in Ability to Manoeuvre, RAM）
优先级 3：限于吃水的船舶（Constrained by Draft, CBD）
优先级 4：从事捕鱼的船舶（Fishing, FV）
优先级 5：帆船（Sailing Vessel, SV）
优先级 6：机动船（Power-driven Vessel, PDV）
```

低优先级的船舶应给高优先级的船舶让路，无论其他规则如何规定。

### 7.2 目标类型识别

目标的类型可从以下来源获取：

```
FUNCTION determine_target_type(target):
    
    # 来源 1：AIS 导航状态（最可靠）
    IF target.mmsi != 0:
        nav_status = target.nav_status
        
        SWITCH nav_status:
            CASE 0:  RETURN "power_driven"         # 机动航行中
            CASE 1:  RETURN "at_anchor"             # 锚泊（不参与避碰决策）
            CASE 2:  RETURN "not_under_command"     # 失控
            CASE 3:  RETURN "restricted_ability"    # 操纵受限
            CASE 4:  RETURN "constrained_by_draft"  # 限于吃水
            CASE 5:  RETURN "moored"                # 系泊（不参与避碰决策）
            CASE 7:  RETURN "fishing"               # 从事捕鱼
            CASE 8:  RETURN "sailing"               # 帆船航行
            DEFAULT: RETURN "power_driven"          # 默认为机动船
    
    # 来源 2：AIS 船舶类型
    IF target.ship_type IS NOT NULL:
        IF target.ship_type contains "fishing":
            RETURN "fishing"
        IF target.ship_type contains "sailing":
            RETURN "sailing"
        IF target.ship_type contains "tug":
            RETURN "power_driven"   # 拖轮虽然特殊但仍是机动船
    
    # 来源 3：行为推断（无 AIS 数据时）
    IF target.sog < 3.0 AND target.cog_variance > 30°:
        RETURN "possible_fishing"   # 低速且航向不定，可能是渔船
    
    # 默认：机动船
    RETURN "power_driven"
```

### 7.3 优先序覆盖判定

```
FUNCTION check_type_priority_override(own_type, target_type):
    
    priority = {
        "not_under_command": 1,
        "restricted_ability": 2,
        "constrained_by_draft": 3,
        "fishing": 4,
        "possible_fishing": 4,    # 保守：视同渔船
        "sailing": 5,
        "power_driven": 6
    }
    
    own_priority = priority.get(own_type, 6)
    target_priority = priority.get(target_type, 6)
    
    IF target_priority < own_priority:
        # 目标优先级更高，我应该让路（无论 Rule 13/14/15 如何规定）
        RETURN {
            override: true,
            my_role: "give_way",
            applicable_rule: "Rule 18",
            rationale: f"目标类型 '{target_type}'（优先级 {target_priority}）高于本船 '{own_type}'（优先级 {own_priority}）"
        }
    
    IF own_priority < target_priority:
        # 本船优先级更高，对方应该让路
        # 但本船仍需保持谨慎——对方可能不会让路（特别是 MASS_ADAS 不能依赖他船行为）
        RETURN {
            override: false,    # 不覆盖原有分类
            note: "本船优先级较高，但仍需监控目标行为"
        }
    
    # 同类型——按 Rule 13/14/15 常规规则处理
    RETURN {override: false}
```

---

## 8. 步骤三：让路/直航责任判定

### 8.1 综合判定算法

```
FUNCTION determine_responsibility(encounter_classification, type_priority_result):
    
    # Rule 18 覆盖
    IF type_priority_result.override:
        RETURN {
            my_role: "give_way",
            reason: type_priority_result.rationale,
            applicable_rules: [encounter_classification.applicable_rules, "Rule 18"]
        }
    
    # 按会遇类型判定
    SWITCH encounter_classification.encounter_type:
        
        CASE "head_on":
            RETURN {my_role: "both_give_way", applicable_rules: ["Rule 14"]}
        
        CASE "crossing" WHERE encounter_classification.my_role == "give_way":
            RETURN {my_role: "give_way", applicable_rules: ["Rule 15", "Rule 16"]}
        
        CASE "crossing" WHERE encounter_classification.my_role == "stand_on":
            RETURN {my_role: "stand_on", applicable_rules: ["Rule 15", "Rule 17"]}
        
        CASE "overtaking":
            RETURN {my_role: "overtaking_give_way", applicable_rules: ["Rule 13"]}
        
        CASE "being_overtaken":
            RETURN {my_role: "stand_on", applicable_rules: ["Rule 13", "Rule 17"]}
        
        CASE "safe_passing":
            RETURN {my_role: "none", applicable_rules: ["Rule 7 - no risk"]}
```

---

## 9. 步骤四：行动方式推荐

### 9.1 让路船的行动方式（Rule 8, Rule 16）

```
FUNCTION recommend_give_way_action(encounter, target, own_ship, zone_type):
    
    # Rule 8 核心要求：
    # (a) 应及早、大幅地采取行动
    # (b) 航向和/或航速的改变应足够大，使他船通过目视或雷达观测容易察觉
    # (c) 如仅用航向改变即可导致在适当距离驶过，且不致造成另一紧迫局面，
    #     则应避免对航速进行改变
    # (d) 为避碰而采取的行动，应使与他船在安全距离驶过
    # (e) 如需要，应减速或停船
    
    # ---- 基本行动方式选择 ----
    
    IF encounter.encounter_type == "head_on":
        # Rule 14：对遇时双方向右转
        action = "alter_course_starboard"
        preferred_direction = "starboard"
    
    ELIF encounter.encounter_type == "crossing" AND encounter.my_role == "give_way":
        # Rule 15：让路船通常向右转，从目标船尾后方通过
        # Rule 15 还规定：避免横越他船前方
        action = "alter_course_starboard"
        preferred_direction = "starboard"
        
        # 特殊情况：如果目标在本船正前方偏右（< 30°），
        # 向右转可能导致本船切入目标航路，此时考虑减速或停船
        IF target.bearing_relative < 30:
            action = "alter_course_starboard"    # 仍然向右，但幅度要足够大
    
    ELIF encounter.encounter_type == "overtaking":
        # Rule 13：追越船从任一侧通过均可
        # 选择更安全的一侧
        IF target.bearing_relative < 180:
            # 目标在右侧前方，从左侧超越
            preferred_direction = "port"
            action = "alter_course_port"
        ELSE:
            # 目标在左侧前方，从右侧超越
            preferred_direction = "starboard"
            action = "alter_course_starboard"
    
    # ---- 空间约束检查 ----
    
    # 检查推荐转向方向是否有空间
    IF preferred_direction == "starboard":
        corridor = enc_reader.get_corridor_width(own_ship.position)
        IF corridor.width_starboard < MIN_AVOIDANCE_CORRIDOR:
            # 右侧空间不足——考虑减速替代转向
            action = "reduce_speed"
            preferred_direction = "none"
    
    ELIF preferred_direction == "port":
        IF corridor.width_port < MIN_AVOIDANCE_CORRIDOR:
            action = "reduce_speed"
            preferred_direction = "none"
    
    # ---- Rule 8(c)：如果仅转向就够，优先转向；否则组合减速 ----
    
    # 此时具体幅度在步骤六中计算
    
    RETURN {
        recommended_action: action,
        preferred_direction: preferred_direction,
        applicable_rules: ["Rule 8", "Rule 16"]
    }

MIN_AVOIDANCE_CORRIDOR = 500m    # 避让方向至少需要 500m 空间
```

### 9.2 直航船的行动方式（Rule 17）

直航船的行动在步骤五（时机判定）中根据 Rule 17 的逐级义务确定。见第 17 节。

### 9.3 行动方式禁忌

```
FUNCTION check_action_prohibitions(action, encounter, target, zone_type, visibility):
    
    prohibitions = []
    
    # Rule 8(f)(i)：不应朝位于正横前方的船舶转向
    IF action contains "alter_course" AND target.bearing_relative > 350 OR target.bearing_relative < 10:
        IF action == "alter_course_starboard" AND target.bearing_relative < 10:
            prohibitions.append("Rule 8(f)(i): 不应朝正横前方目标转向")
    
    # Rule 19(d)(i)：能见度不良时，不应向左转向避让正横前方的目标
    IF visibility < VISIBILITY_THRESHOLD:
        IF action == "alter_course_port" AND target.bearing_relative < 90:
            prohibitions.append("Rule 19(d)(i): 能见度不良时禁止向左转避让正横前方目标")
    
    # Rule 19(d)(ii)：能见度不良时，不应向左转向避让正横或正横后方的目标
    IF visibility < VISIBILITY_THRESHOLD:
        IF action == "alter_course_port" AND target.bearing_relative >= 90 AND target.bearing_relative <= 270:
            prohibitions.append("Rule 19(d)(ii): 能见度不良时禁止向左转避让正横/后方目标")
    
    RETURN prohibitions
```

---

## 10. 步骤五：行动时机判定

### 10.1 时机判定算法

```
FUNCTION determine_action_timing(encounter, target, own_ship, zone_type):
    
    tcpa = target.tcpa
    cpa = target.cpa
    threat_level = target.threat_level
    
    # 获取当前水域的时机阈值
    thresholds = get_timing_thresholds(zone_type)
    
    # ---- 让路船的时机判定 ----
    IF encounter.my_role IN ["give_way", "both_give_way", "overtaking_give_way"]:
        
        IF tcpa ≤ thresholds.T_emergency:
            RETURN {
                action_required: true,
                urgency: "emergency",
                action_deadline_sec: 0,    # 立即行动
                rationale: f"TCPA {tcpa:.0f}s 已达紧急阈值"
            }
        
        IF tcpa ≤ thresholds.T_give_way:
            RETURN {
                action_required: true,
                urgency: "urgent",
                action_deadline_sec: max(0, tcpa - thresholds.T_emergency),
                rationale: f"TCPA {tcpa:.0f}s，应立即执行让路行动（Rule 16: 及早行动）"
            }
        
        IF tcpa ≤ thresholds.T_give_way × 1.5:
            RETURN {
                action_required: false,
                urgency: "routine",
                action_deadline_sec: tcpa - thresholds.T_give_way,
                rationale: f"TCPA {tcpa:.0f}s，建议开始准备让路行动"
            }
        
        RETURN {action_required: false, urgency: "none"}
    
    # ---- 直航船的时机判定（见第 17 节 Rule 17 逐级义务）----
    IF encounter.my_role == "stand_on":
        RETURN determine_stand_on_timing(encounter, target, thresholds)
```

---

## 11. 步骤六：行动幅度计算

### 11.1 转向幅度计算

```
FUNCTION compute_course_alteration(own_ship, target, preferred_direction, zone_type):
    
    CPA_target = CPA_safe × CPA_SAFETY_FACTOR    # 目标 CPA（安全距离 × 1.5）
    
    direction_sign = 1 IF preferred_direction == "starboard" ELSE -1
    
    # 二分搜索：找到使预期 CPA ≥ CPA_target 的最小转向角度
    Δψ_low = MIN_COURSE_CHANGE    # 30°（Rule 8 要求的最低"大幅"标准）
    Δψ_high = MAX_COURSE_CHANGE   # 90°
    
    best_Δψ = NaN
    
    FOR Δψ = Δψ_low TO Δψ_high STEP 1°:
        new_heading = own_ship.heading + Δψ × direction_sign
        new_cpa = predict_cpa_after_course_change(own_ship, target, new_heading)
        
        IF new_cpa ≥ CPA_target:
            best_Δψ = Δψ
            BREAK
    
    IF isnan(best_Δψ):
        # 转 90° 仍不够——需要减速辅助
        RETURN {
            course_change: 60 × direction_sign,
            speed_change_needed: true,
            rationale: "仅转向不足以达到安全 CPA，需要减速辅助"
        }
    
    # 确保至少 30°（Rule 8 "大幅" 要求）
    best_Δψ = max(best_Δψ, MIN_COURSE_CHANGE)
    
    # 验证不穿越目标船首（Rule 15 约束）
    IF encounter.encounter_type == "crossing" AND encounter.my_role == "give_way":
        new_bcr = predict_bcr_after_course_change(own_ship, target, own_ship.heading + best_Δψ × direction_sign)
        IF new_bcr < BCR_SAFE_FACTOR × CPA_safe:
            # 避让后仍会从目标船首前方通过——增大转向角度或减速
            best_Δψ = min(best_Δψ + 15, MAX_COURSE_CHANGE)
    
    RETURN {
        course_change: best_Δψ × direction_sign,
        expected_cpa: new_cpa,
        rationale: f"向{'右' if direction_sign>0 else '左'}转 {best_Δψ:.0f}°，预期 CPA {new_cpa:.0f}m"
    }

MIN_COURSE_CHANGE = 30    # 度
MAX_COURSE_CHANGE = 90    # 度
CPA_SAFETY_FACTOR = 1.5
BCR_SAFE_FACTOR = 2.0
```

### 11.2 预测变更航向后的 CPA

```
FUNCTION predict_cpa_after_course_change(own_ship, target, new_heading):
    
    # 假设本船立即改变航向到 new_heading，速度不变
    new_vx = own_ship.sog × sin(new_heading × π/180)
    new_vy = own_ship.sog × cos(new_heading × π/180)
    
    # 新的相对速度
    Δvx_new = target.vx - new_vx
    Δvy_new = target.vy - new_vy
    
    # 新的 CPA
    Δx = target.x - own_ship.x
    Δy = target.y - own_ship.y
    
    new_tcpa = -(Δx × Δvx_new + Δy × Δvy_new) / (Δvx_new² + Δvy_new²)
    
    IF new_tcpa < 0:
        RETURN current_range    # CPA 已过
    
    cpa_x = Δx + Δvx_new × new_tcpa
    cpa_y = Δy + Δvy_new × new_tcpa
    new_cpa = sqrt(cpa_x² + cpa_y²)
    
    RETURN new_cpa
```

### 11.3 减速幅度计算

当仅靠转向不够时，或空间不允许转向时：

```
FUNCTION compute_speed_reduction(own_ship, target, course_change):
    
    CPA_target = CPA_safe × CPA_SAFETY_FACTOR
    
    # 在已经转向 course_change 度的基础上，搜索需要减速多少
    FOR V_new = own_ship.sog - 0.5 DOWNTO V_MIN_MANEUVER STEP 0.5:
        
        new_heading = own_ship.heading + course_change
        new_vx = V_new × sin(new_heading × π/180)
        new_vy = V_new × cos(new_heading × π/180)
        
        # 重新计算 CPA
        Δvx = target.vx - new_vx
        Δvy = target.vy - new_vy
        new_cpa = compute_cpa_with_new_rel_velocity(own_ship, target, Δvx, Δvy)
        
        IF new_cpa ≥ CPA_target:
            RETURN {
                new_speed: V_new,
                speed_change: V_new - own_ship.sog,    # 负值
                expected_cpa: new_cpa,
                rationale: f"减速至 {V_new:.1f} m/s，预期 CPA {new_cpa:.0f}m"
            }
    
    # 即使减到最低可操纵速度仍不够——紧急停船
    RETURN {
        new_speed: 0,
        speed_change: -own_ship.sog,
        action: "stop",
        rationale: "需要紧急停船"
    }
```

---

## 12. 步骤七：多目标冲突消解

### 12.1 问题描述

当同时面对 2 个以上需要避让的目标时，不同目标可能要求相反方向的避让——一个要求右转，另一个要求左转。COLREGs Engine 必须找到一个"统一方案"同时满足所有目标。

### 12.2 统一方案搜索算法

```
FUNCTION resolve_multi_target(threats_requiring_action, own_ship, zone_type):
    
    # 生成候选方案空间
    candidates = []
    
    # 方案类型 1：纯转向
    FOR Δψ = -90 TO +90 STEP 5:    # 正=右转，负=左转
        IF Δψ == 0: CONTINUE
        
        new_heading = own_ship.heading + Δψ
        
        # 检查行动禁忌
        IF has_prohibition(Δψ, threats_requiring_action, visibility):
            CONTINUE
        
        # 计算对每个目标的预期 CPA
        all_safe = true
        min_cpa = +∞
        
        FOR EACH target IN threats_requiring_action:
            expected_cpa = predict_cpa_after_course_change(own_ship, target, new_heading)
            min_cpa = min(min_cpa, expected_cpa)
            IF expected_cpa < CPA_safe:
                all_safe = false
                BREAK
        
        IF all_safe:
            candidates.append({
                type: "course_only",
                course_change: Δψ,
                speed_change: 0,
                min_cpa: min_cpa,
                score: evaluate_candidate(Δψ, 0, min_cpa)
            })
    
    # 方案类型 2：转向 + 减速
    FOR Δψ = -60 TO +60 STEP 10:
        FOR ΔV = -1.0 TO -(own_ship.sog - V_MIN) STEP -1.0:
            # ... 与上面类似的检查逻辑 ...
            # 计算所有目标的预期 CPA
            # 如果全部安全，加入候选列表
    
    # 方案类型 3：仅减速或停船
    FOR V_new = own_ship.sog - 1.0 DOWNTO 0 STEP 1.0:
        # ... 检查所有目标 ...
    
    IF len(candidates) == 0:
        # 没有任何方案能同时满足所有目标——紧急模式
        RETURN {
            action: "emergency",
            rationale: "无法找到同时满足所有目标的避让方案，触发紧急停船",
            applicable_rules: ["Rule 2 - responsibility"]
        }
    
    # 从候选方案中选择最优
    best = select_best_candidate(candidates)
    
    RETURN {
        overall_course_change: best.course_change,
        overall_speed_change: best.speed_change,
        overall_min_cpa: best.min_cpa,
        applicable_rules: ["Rule 2", "Rule 8"]
    }
```

### 12.3 候选方案评分函数

```
FUNCTION evaluate_candidate(course_change, speed_change, min_cpa):
    
    score = 0
    
    # 因子 1：CPA 越大越好
    score += 30 × min(min_cpa / (CPA_safe × 2), 1.0)
    
    # 因子 2：转向幅度越小越好（在满足 ≥ 30° 的前提下）
    IF abs(course_change) >= 30:
        score += 20 × (1 - abs(course_change) / 90)
    ELSE:
        score -= 10    # 惩罚小于 30° 的转向（不够"大幅"）
    
    # 因子 3：右转优于左转（Rule 8 偏好）
    IF course_change > 0:
        score += 10    # 右转奖励
    
    # 因子 4：减速越少越好（Rule 8(c)：优先用转向解决）
    score += 15 × (1 - abs(speed_change) / own_ship.sog)
    
    # 因子 5：不穿越任何目标的船首
    IF no_bow_crossing:
        score += 15
    
    RETURN score
```

### 12.4 冲突消解优先级

当无法同时完美满足所有目标时，按以下优先级做取舍：

```
1. 首先保证不碰撞（所有目标 CPA > 0）
2. 优先满足 CRITICAL 威胁等级的目标
3. 优先满足 TCPA 最短的目标
4. 尽可能满足所有 Rule 15 让路义务（右转偏好）
5. 最后考虑效率（最小化航向和速度变化）
```

---

## 13. 狭水道规则实现（Rule 9）

### 13.1 Rule 9 核心条款与量化实现

```
FUNCTION apply_narrow_channel_rules(own_ship, target, encounter, channel_info):
    
    constraints = []
    
    # Rule 9(a): 靠近航道右侧行驶
    own_position_in_channel = compute_lateral_position(own_ship, channel_info)
    IF own_position_in_channel > 0.5:    # 偏左（0=右边界，1=左边界）
        constraints.append({
            rule: "Rule 9(a)",
            action: "move_to_starboard_side",
            rationale: "本船偏离航道右侧，应靠右行驶"
        })
    
    # Rule 9(b): 长度小于 20m 的船舶不得妨碍大船
    IF own_ship.length < 20 AND target.length > 20:
        constraints.append({
            rule: "Rule 9(b)",
            override_role: "give_way",
            rationale: "本船 < 20m，不得妨碍大型船舶在狭水道内航行"
        })
    
    # Rule 9(c): 从事捕鱼的船舶不得妨碍其他船舶
    IF own_ship.type == "fishing":
        constraints.append({
            rule: "Rule 9(c)",
            override_role: "give_way",
            rationale: "渔船不得在狭水道内妨碍其他船舶"
        })
    
    # Rule 9(d): 不得横穿狭水道
    IF is_crossing_channel(own_ship.heading, channel_info.direction):
        constraints.append({
            rule: "Rule 9(d)",
            action: "avoid_crossing",
            rationale: "不得横穿狭水道（除非不会妨碍大船）"
        })
    
    # 狭水道内的避让限制：转向空间有限
    # 通常只能减速或紧贴右侧
    IF encounter.recommended_action == "alter_course_starboard":
        max_turn = compute_max_safe_turn_in_channel(own_ship, channel_info, "starboard")
        IF max_turn < encounter.recommended_course_change:
            encounter.recommended_course_change = max_turn
            IF max_turn < 15:
                # 转向空间极小——改为减速
                encounter.recommended_action = "reduce_speed"
    
    RETURN constraints
```

---

## 14. 分道通航制规则实现（Rule 10）

### 14.1 Rule 10 核心条款与量化实现

```
FUNCTION apply_tss_rules(own_ship, target, encounter, tss_info):
    
    constraints = []
    
    # Rule 10(a): 沿通航分道方向行驶
    own_alignment = abs(normalize_pm180(own_ship.heading - tss_info.direction))
    IF own_alignment > 15:
        constraints.append({
            rule: "Rule 10(a)",
            action: "align_with_tss_direction",
            rationale: f"本船航向偏离 TSS 方向 {own_alignment:.0f}°，应调整至 ≤ 15°"
        })
    
    # Rule 10(b)(i): 进出分道时尽可能小角度
    IF own_ship is entering or exiting TSS:
        entry_angle = compute_tss_entry_angle(own_ship, tss_info)
        IF entry_angle > 15:
            constraints.append({
                rule: "Rule 10(b)(i)",
                action: "reduce_entry_angle",
                rationale: f"进出 TSS 角度 {entry_angle:.0f}° > 15°"
            })
    
    # Rule 10(b)(iii): 横穿时应以接近直角横穿
    IF own_ship is crossing TSS:
        crossing_angle = compute_tss_crossing_angle(own_ship, tss_info)
        IF crossing_angle < 70:
            constraints.append({
                rule: "Rule 10(b)(iii)",
                action: "increase_crossing_angle",
                rationale: f"横穿 TSS 角度 {crossing_angle:.0f}° < 70°，应接近 90°"
            })
    
    # Rule 10(j): 长度 < 20m 的船舶或帆船不得妨碍沿分道行驶的机动船
    IF (own_ship.length < 20 OR own_ship.type == "sailing"):
        IF target_in_tss_lane AND target.type == "power_driven":
            constraints.append({
                rule: "Rule 10(j)",
                override_role: "give_way",
                rationale: "小型船舶/帆船不得在 TSS 内妨碍大型机动船"
            })
    
    RETURN constraints
```

---

## 15. 能见度不良规则实现（Rule 19）

### 15.1 能见度不良模式

当能见度低于阈值（默认 2 nm）时，COLREGs Engine 进入 Rule 19 特殊模式。此模式下：

- **Rule 13/14/15 不适用。** 不再区分对遇、交叉、追越。所有目标统一按 Rule 19 处理。
- **所有目标都被视为需要避让的潜在威胁。**
- **严格限制转向方向。**

```
FUNCTION apply_rule_19(own_ship, target, current_cpa, current_tcpa):
    
    # Rule 19(d): 仅凭雷达探测到他船时
    
    decision = ColregsDecision()
    decision.applicable_rules = ["Rule 19"]
    
    # Rule 19(d)(i): 避免向左转向避让位于正横前方的船舶
    IF target.bearing_relative < 90 OR target.bearing_relative > 270:
        # 目标在正横前方
        decision.action_prohibitions.append("no_port_turn")
        decision.recommended_action = "alter_course_starboard"
    
    # Rule 19(d)(ii): 避免向左转向避让位于正横或正横后方的船舶
    IF 90 ≤ target.bearing_relative ≤ 270:
        # 目标在正横或正横后方
        # 可以向右转或减速，但不可向左转
        decision.action_prohibitions.append("no_port_turn")
        
        IF target.bearing_relative < 180:
            # 目标在右舷正横后方——向右转离开
            decision.recommended_action = "alter_course_starboard"
        ELSE:
            # 目标在左舷正横后方——可向右转或减速
            decision.recommended_action = "alter_course_starboard"
    
    # Rule 19(e): 除非已断定不存在碰撞危险，
    #            每一船当听到他船的雾号显似在本船正横前方时，
    #            应将航速减至能维持操纵的最低速度
    IF fog_signal_detected AND signal_from_ahead:
        decision.recommended_action = "reduce_speed"
        decision.recommended_new_speed = V_MIN_MANEUVER
    
    # Rule 6: 能见度不良时的安全速度
    safe_speed = compute_safe_speed_for_visibility(visibility, own_ship, zone_type)
    IF own_ship.sog > safe_speed:
        decision.speed_limit = safe_speed
        decision.rationale += f"; Rule 6: 能见度 {visibility:.1f}nm 下安全速度 ≤ {safe_speed:.1f} m/s"
    
    RETURN decision
```

### 15.2 能见度不良下的安全速度（Rule 6）

```
FUNCTION compute_safe_speed_for_visibility(visibility_nm, own_ship, zone_type):
    
    # Rule 6 列出了确定安全速度时应考虑的因素：
    # (a) 能见度状况
    # (b) 交通密度
    # (c) 船舶的操纵性能（停船距离）
    # (d) 夜间背景灯光
    # (e) 风、海浪状况及潮流
    # (f) 吃水与可用水深的关系
    
    # 量化模型：安全速度应使停船距离 ≤ 能见度距离的某个比例
    
    visibility_m = visibility_nm × 1852
    
    # 停船距离公式（从 Parameter DB 查表或用经验公式）
    # 安全条件：停船距离 ≤ visibility_m × k_vis
    # k_vis 取值：0.3~0.5（保守原则）
    
    k_vis = 0.4
    target_stop_distance = visibility_m × k_vis
    
    # 反推安全速度（从停船距离查表反算）
    V_safe = lookup_speed_for_stop_distance(target_stop_distance, own_ship.decel_params)
    
    # 不低于最低可操纵速度
    V_safe = max(V_safe, V_MIN_MANEUVER)
    
    # 不高于当前限速
    V_safe = min(V_safe, get_zone_speed_limit(zone_type))
    
    RETURN V_safe
```

---

## 16. 安全速度判定（Rule 6）

### 16.1 在所有能见度条件下

Rule 6 不仅适用于能见度不良——它在任何时候都要求船舶保持安全速度。

```
FUNCTION compute_safe_speed(own_ship, environment, traffic_density, zone_type):
    
    V_safe = V_MAX_SHIP    # 从最大航速开始向下收紧
    
    # 因素 1：能见度
    IF environment.visibility_nm < 5:
        V_vis = compute_safe_speed_for_visibility(environment.visibility_nm, own_ship, zone_type)
        V_safe = min(V_safe, V_vis)
    
    # 因素 2：交通密度
    IF traffic_density > DENSITY_THRESHOLD_HIGH:
        V_safe = min(V_safe, V_MAX_SHIP × 0.7)    # 密集交通区降速 30%
    ELIF traffic_density > DENSITY_THRESHOLD_MEDIUM:
        V_safe = min(V_safe, V_MAX_SHIP × 0.85)   # 中等交通密度降速 15%
    
    # 因素 3：水域限速
    V_zone = get_zone_speed_limit(zone_type)
    V_safe = min(V_safe, V_zone)
    
    # 因素 4：浅水 Squat 限速
    IF environment.h_over_T < 3.0:
        V_squat = compute_squat_speed_limit(environment.min_depth, own_ship)
        V_safe = min(V_safe, V_squat)
    
    RETURN V_safe

DENSITY_THRESHOLD_HIGH = 10     # 半径 6nm 内目标数 > 10
DENSITY_THRESHOLD_MEDIUM = 5    # 半径 6nm 内目标数 > 5
```

---

## 17. 直航船的逐级行动义务（Rule 17）

### 17.1 Rule 17 的三个阶段

COLREGs Rule 17 规定了直航船的行动义务是渐进的：

**阶段 1 — 维持（Rule 17(a)(i)）：** 当一船被要求让路时，另一船（直航船）应保持航向和航速。

**阶段 2 — 可以行动（Rule 17(a)(ii)）：** 当直航船发现让路船显然没有按规则采取适当行动时，直航船可以独自采取操纵行动以避免碰撞。

**阶段 3 — 必须行动（Rule 17(b)）：** 当直航船发现不论由于何种原因，碰撞已不能单凭让路船的行动来避免时，直航船也应采取最有助于避碰的行动。

### 17.2 三阶段判定逻辑

```
FUNCTION determine_stand_on_behavior(target, own_ship, thresholds):
    
    tcpa = target.tcpa
    cpa = target.cpa
    target_action_detected = detect_target_action(target)
    
    # ---- 阶段 1：维持 ----
    IF tcpa > thresholds.T_stand_on_warn:
        RETURN {
            action: "maintain",
            phase: "phase_1_maintain",
            applicable_rules: ["Rule 17(a)(i)"],
            rationale: "让路船应有足够时间采取行动，本船保持航向航速"
        }
    
    # ---- 过渡：让路船是否在行动？ ----
    IF NOT target_action_detected AND tcpa ≤ thresholds.T_stand_on_warn AND tcpa > thresholds.T_stand_on_act:
        RETURN {
            action: "maintain_with_warning",
            phase: "phase_1_to_2_transition",
            applicable_rules: ["Rule 17(a)(i)", "Rule 17(a)(ii)"],
            rationale: f"TCPA {tcpa:.0f}s，让路船未检测到行动。本船继续保持但提高警戒。",
            alert_shore: true    # 通知岸基
        }
    
    # ---- 阶段 2：可以行动 ----
    IF NOT target_action_detected AND tcpa ≤ thresholds.T_stand_on_act AND tcpa > thresholds.T_emergency:
        RETURN {
            action: "take_independent_action",
            phase: "phase_2_may_act",
            applicable_rules: ["Rule 17(a)(ii)"],
            recommended_action: compute_stand_on_avoidance(own_ship, target),
            rationale: f"TCPA {tcpa:.0f}s，让路船未行动。本船独自采取避碰行动。"
        }
    
    # ---- 阶段 3：必须行动 ----
    IF tcpa ≤ thresholds.T_emergency OR (cpa < CPA_safe × 0.5 AND tcpa < thresholds.T_stand_on_act):
        RETURN {
            action: "emergency_action",
            phase: "phase_3_must_act",
            applicable_rules: ["Rule 17(b)"],
            recommended_action: compute_emergency_avoidance(own_ship, target),
            urgency: "emergency",
            rationale: f"TCPA {tcpa:.0f}s / CPA {cpa:.0f}m，碰撞不可单凭让路船避免。本船必须行动。"
        }
    
    # 让路船已采取行动——继续保持
    RETURN {action: "maintain", phase: "phase_1_maintain", rationale: "让路船正在行动，继续监控"}
```

### 17.3 检测目标是否在采取让路行动

```
FUNCTION detect_target_action(target):
    
    # 检查目标是否有明显的航向或航速变化
    
    # 航向变化 > 10° 在最近 60 秒内
    heading_change = abs(target.cog - target.cog_60s_ago)
    IF heading_change > 10:
        RETURN true
    
    # 速度降低 > 20% 在最近 60 秒内
    speed_change = (target.sog - target.sog_60s_ago) / max(target.sog_60s_ago, 0.1)
    IF speed_change < -0.20:
        RETURN true
    
    # CPA 持续改善（连续 30 秒 CPA 在增大）
    IF target.cpa_trend == "INCREASING" FOR > 30s:
        RETURN true
    
    RETURN false
```

### 17.4 直航船独自行动的约束

```
FUNCTION compute_stand_on_avoidance(own_ship, target):
    
    # Rule 17(c)：在交叉相遇态势下，直航船如果条件许可，
    # 不应朝左舷方向转向避让位于本船左舷的船舶。
    
    IF encounter.type == "crossing" AND target.bearing_relative > 270:
        # 目标在左舷——不得向左转
        prohibited_directions = ["port"]
    ELSE:
        prohibited_directions = []
    
    # 优先右转，次选减速
    IF "starboard" NOT IN prohibited_directions:
        course_change = compute_course_alteration(own_ship, target, "starboard", zone_type)
        IF course_change.expected_cpa ≥ CPA_safe:
            RETURN {action: "alter_course_starboard", ...}
    
    # 右转不够或不可用——减速
    speed_reduction = compute_speed_reduction(own_ship, target, 0)
    RETURN {action: "reduce_speed", ...}
```

---

## 18. 责任条款与良好船艺（Rule 2）

### 18.1 Rule 2 的算法化实现

Rule 2 是 COLREGs 的"总则"——它规定遵守规则并不免除船长因特殊情况需要偏离规则的责任。在算法中，Rule 2 体现为一个最终的安全检查层：

```
FUNCTION apply_rule_2_override(decision, own_ship, all_targets, environment):
    
    # 检查 1：执行推荐行动后是否会制造新的危险？
    IF decision.recommended_action involves course change:
        new_course = own_ship.heading + decision.recommended_course_change
        
        FOR EACH other_target IN all_targets:
            IF other_target.track_id == decision.target_track_id:
                CONTINUE    # 跳过当前正在避让的目标
            
            new_cpa_other = predict_cpa_after_course_change(own_ship, other_target, new_course)
            
            IF new_cpa_other < CPA_safe:
                # 避让一个目标的动作导致与另一个目标产生碰撞风险！
                decision.override = true
                decision.override_reason = f"Rule 2: 执行推荐行动会导致与目标 {other_target.track_id} 的 CPA 降至 {new_cpa_other:.0f}m"
                decision.fallback_action = "reduce_speed"    # 回退到减速方案
    
    # 检查 2：在极近距离时（< 0.5 × CPA_safe），不受常规规则约束
    # 按最有利于避碰的方式行动
    IF decision.target.range < CPA_safe × 0.5 AND decision.target.tcpa < 60:
        decision.applicable_rules.append("Rule 2(b) - imminent danger override")
        decision.urgency = "emergency"
        decision.rationale += "; Rule 2(b): 近距离紧急情况，按最有利方式避碰"
    
    RETURN decision
```

---

## 19. 内部子模块分解

| 子模块 | 职责 | 建议语言 | 计算复杂度 |
|-------|------|---------|----------|
| Encounter Classifier | 会遇分类（Rule 13/14/15） | C++ | O(n)，n = 目标数 |
| Type Priority Checker | Rule 18 类型优先序判定 | C++ | O(n) |
| Responsibility Determiner | 综合让路/直航判定 | C++ | O(n) |
| Action Recommender | 行动方式/方向推荐 | C++ | O(n) |
| Timing Judge | 行动时机判定 | C++ | O(n) |
| Amplitude Calculator | 转向/减速幅度计算（含 CPA 预测） | C++ | O(n × m)，m = 搜索步数 |
| Multi-Target Resolver | 多目标冲突消解 | C++ | O(n² × m) |
| Special Rules Handler | 狭水道/TSS/能见度不良规则 | C++ | O(n) |
| Classification Lock Manager | 分类锁定机制（Rule 13(c)） | C++ | O(n) |
| Rule 2 Safety Checker | 最终安全检查与覆盖 | C++ | O(n²) |
| Decision Logger | 决策依据记录（审计） | Python | O(n) |

---

## 20. 数值算例

### 20.1 算例一：标准交叉相遇（右舷来船）

**初始态势：**

```
本船：(0, 0), 航向 000°, 速度 8 m/s, 类型: 机动船
目标：(3000, 2000), 航向 270°, 速度 6 m/s, 类型: 机动船, MMSI: 123456789
```

**步骤一（会遇分类）：**

```
rel_bearing = 56.3°（右舷前方）
heading_diff = 270° - 0° = 270°（归一化后 = -90°）

追越检验：目标看本船方位 ≈ 213°（不在追越扇区 112.5°~247.5°...实际计算需要精确）
  → 不是追越

对遇检验：|heading_diff| = 90°，不在 170°~190° 范围
  → 不是对遇

交叉检验：rel_bearing = 56.3°，在 0°~112.5°（右舷）
  → 交叉相遇，目标在右舷，本船为让路船

分类结果：encounter_type = "crossing", my_role = "give_way"
适用规则：Rule 15, Rule 16
```

**步骤二（类型优先序）：** 双方都是机动船，优先级相同，不覆盖。

**步骤三（责任判定）：** my_role = "give_way"，确认。

**步骤四（行动推荐）：** 让路船向右转，从目标船尾后方通过。recommended_action = "alter_course_starboard"。

**步骤五（时机判定）：**

```
TCPA = 340s = 5.67 分钟
CPA = 1200m（< CPA_safe = 1852m）
开阔水域 T_give_way = 480s

TCPA(340s) < T_give_way(480s) → action_required = true, urgency = "urgent"
```

**步骤六（幅度计算）：**

```
搜索转向角度：
  Δψ = 30°（右转）: predict_cpa = 1650m < 1852×1.5 = 2778m → 不够
  Δψ = 40°: predict_cpa = 2100m < 2778m → 不够
  Δψ = 50°: predict_cpa = 2600m < 2778m → 不够
  Δψ = 55°: predict_cpa = 2850m ≥ 2778m → 足够

结果：向右转 55°，预期 CPA 2850m
```

**输出决策：**

```
ColregsDecision:
    encounter_type: "crossing"
    my_role: "give_way"
    recommended_action: "alter_course_starboard"
    recommended_course_change: +55°
    action_required: true
    urgency: "urgent"
    expected_cpa_after_action: 2850m
    applicable_rules: ["Rule 15", "Rule 16", "Rule 8"]
    decision_rationale: "目标在右舷 56° 方位，交叉相遇，本船为让路船。向右转 55°，预期 CPA 2850m（> 安全距离 1852m × 1.5）"
```

### 20.2 算例二：能见度不良

```
能见度 = 1.0 nm
目标在左舷正横（rel_bearing = 270°），接近中

Rule 19 模式激活：
  → 不适用 Rule 13/14/15
  → 目标在正横 → Rule 19(d)(ii): 禁止向左转
  → 推荐：向右转或减速
  → 同时应用 Rule 6 安全速度限制

安全速度 = 反推停船距离 ≤ 1852m × 0.4 = 741m 对应的速度
  → 查表得 V_safe ≈ 5 m/s
```

### 20.3 算例三：多目标冲突

```
本船：航向 000°, 速度 8 m/s

目标 A：右舷 45°, CPA 800m, TCPA 300s → 交叉, 我让路, 需右转
目标 B：左舷 320°, CPA 600m, TCPA 250s → 交叉, 我直航, 但 CPA 偏小

如果右转 50° 避让 A：
  → 对 A 的新 CPA = 2500m（安全）
  → 但对 B 的新 CPA = 300m（变得更危险！）

冲突消解搜索：
  右转 30° + 减速 3 m/s:
    → 对 A 新 CPA = 2200m ≥ 2778m? → 不够
  右转 40° + 减速 2 m/s:
    → 对 A 新 CPA = 2400m → 不够
  右转 35° + 减速 4 m/s:
    → 对 A 新 CPA = 2900m → OK
    → 对 B 新 CPA = 1900m → OK

最终方案：右转 35° + 减速至 4 m/s
```

---

## 21. 参数总览表

| 类别 | 参数 | 默认值 | 来源 |
|------|------|-------|------|
| **分类角度** | 追越扇区起始角 | 112.5° | Rule 13 |
| | 追越扇区结束角 | 247.5° | Rule 13 |
| | 对遇方位容差 | ±6° | Rule 14 |
| | 对遇航向差范围 | 170°~190° | Rule 14 |
| | 对遇模糊区方位扩展 | ±15° | Rule 14(c) 保守原则 |
| | 对遇模糊区航向差扩展 | 160°~200° | Rule 14(c) |
| | 分类边界模糊区半宽 | 5° | 工程经验 |
| | 分类锁定持续时间 | 60 秒 | 防止频繁切换 |
| **行动时机** | 让路船行动阈值（开阔） | 480s (8min) | Rule 16 量化 |
| | 直航船预警阈值（开阔） | 360s (6min) | Rule 17(a)(ii) |
| | 直航船行动阈值（开阔） | 240s (4min) | Rule 17(a)(ii) |
| | 紧急阈值（开阔） | 60s (1min) | Rule 17(b) |
| **行动幅度** | 最小航向变化 | 30° | Rule 8 "大幅" |
| | 最大常规航向变化 | 90° | 工程经验 |
| | 避让维持最短时间 | 120s (2min) | Rule 8 "明显" |
| | CPA 安全余量系数 | 1.5 | 保守原则 |
| | BCR 安全系数 | 2.0 | Rule 15 "避免横越船首" |
| **能见度** | 能见度不良阈值 | 2.0 nm | 工程经验 |
| | 安全速度折减因子 k_vis | 0.4 | Rule 6 量化 |
| **多目标** | 搜索步长（航向） | 5° | 精度 vs 速度权衡 |
| | 搜索步长（速度） | 0.5 m/s | |
| | 最大搜索范围（航向） | ±90° | |
| | 右转偏好奖励 | +10 分 | Rule 8 偏好 |

---

## 22. 与其他模块的协作关系

| 交互方 | 方向 | 数据内容 | 频率 |
|-------|------|---------|------|
| Target Tracker → COLREGs Engine | 输入 | ThreatList（按评分排序的威胁目标） | 2 Hz |
| ENC Reader → COLREGs Engine | 查询 | ZoneClassification（水域类型）、CorridorWidth（避让空间） | 按需 |
| Perception → COLREGs Engine | 输入 | EnvironmentState（能见度） | 0.2 Hz |
| COLREGs Engine → Avoidance Planner | 输出 | ColregsDecision[]（每个目标的决策） | 2 Hz |
| COLREGs Engine → Risk Monitor | 输出 | 决策状态变化通知 | 事件驱动 |
| COLREGs Engine → Shore Link | 输出 | ColregsSituationSummary（态势报告） | 1 Hz |
| Parameter DB → COLREGs Engine | 配置 | 分类阈值、时机参数、幅度参数 | 启动时 |

---

## 23. 测试策略与验收标准

### 23.1 测试场景

| 场景编号 | 描述 | 验证目标 |
|---------|------|---------|
| COL-001 | 对遇——双方正前方 | Rule 14 正确分类 + 双方右转 |
| COL-002 | 对遇——模糊区（方位偏 12°） | Rule 14(c) 保守判定 |
| COL-003 | 交叉——目标右舷 45° | Rule 15 正确分类 + 我让路 |
| COL-004 | 交叉——目标左舷 315° | Rule 15 我直航 |
| COL-005 | 追越——我追目标 | Rule 13 我让路 |
| COL-006 | 被追越——目标追我 | Rule 13 我直航 |
| COL-007 | 分类边界（方位 110°） | 模糊区保守处理 |
| COL-008 | Rule 18——渔船 vs 机动船 | 类型优先序覆盖 |
| COL-009 | Rule 17 阶段 1→2 | 让路船未行动，直航船预警 |
| COL-010 | Rule 17 阶段 2→3 | 直航船主动采取行动 |
| COL-011 | Rule 19——能见度不良 | 禁止左转 + 安全速度 |
| COL-012 | Rule 9——狭水道 | 靠右行驶 + 空间受限避让 |
| COL-013 | Rule 10——TSS 内 | 沿分道行驶 + 横穿角度 |
| COL-014 | 多目标——2 艘无冲突 | 分别处理 |
| COL-015 | 多目标——2 艘有冲突 | 统一方案消解 |
| COL-016 | 多目标——3 艘极端 | 搜索算法正确性 |
| COL-017 | Rule 2——避让导致新危险 | 安全覆盖生效 |
| COL-018 | 分类锁定——追越中方位变化 | Rule 13(c) 锁定不变 |
| COL-019 | 行动幅度——30° 不够需要 60° | 二分搜索正确 |
| COL-020 | 转向空间不足——改为减速 | 空间约束正确应用 |

### 23.2 验收标准

| 指标 | 标准 |
|------|------|
| 会遇分类准确率 | 与人工判定一致 ≥ 95%（非模糊区 100%） |
| 责任判定准确率 | 与 COLREGs 规则一致 100% |
| 推荐行动方向 | 符合 COLREGs 要求 100%（无禁忌违反） |
| 推荐行动幅度 | 执行后预期 CPA ≥ CPA_safe × 1.5 |
| 行动时机 | 让路船在 T_give_way 之前行动 |
| 多目标消解 | 统一方案满足所有目标安全 |
| 能见度不良模式 | 正确切换 + 无左转违规 |
| Rule 2 覆盖 | 检测到避让导致新危险时正确回退 |
| 单次决策延迟 | < 50ms（10 个目标） |

---

## 24. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | IMO COLREGs (COLREG 1972, as amended through 2007) | 国际海上避碰规则全文 |
| [2] | Cockcroft, A.N. & Lameijer, J.N.F. "A Guide to the Collision Avoidance Rules" 7th Ed., 2012 | COLREGs 权威解读与案例分析 |
| [3] | Statheros, T. et al. "Autonomous Ship Collision Avoidance Navigation" JSR, 2008 | 自主避碰算法综述 |
| [4] | Szlapczynski, R. & Szlapczynska, J. "Review of Ship Safety Domains" Ocean Engineering, 2017 | 船舶安全域模型 |
| [5] | IMO MSC.1/Circ.1638 "Outcome of the Regulatory Scoping Exercise for MASS" 2021 | MASS 避碰要求 |
| [6] | Woerner, K. et al. "Quantifying Protocol Evaluation for Autonomous COLREGS-Compliant Vessels" ATR, 2019 | COLREGs 合规性量化评估 |
| [7] | Huang, Y. et al. "Ship Collision Avoidance Methods: State-of-the-Art" Safety Science, 2020 | 避碰方法综述 |
| [8] | IMO Resolution A.1106(29) "Guidelines for Voyage Planning" 2015 | 航次计划指南 |

---

## v2.0 架构升级：COLREGs Compliance Checker 接口

### A. Doer-Checker 双轨定位

在 v2.0 防御性架构中，COLREGs Engine 是 L3 层 Doer（决策者）——它基于 Target Tracker 的威胁列表和可能的 AI/VLM 辅助，生成避碰决策。但在决策发布之前，必须经过独立的 **COLREGs Compliance Checker**（确定性校验器的 L3 层实例）校验。

```
决策流程（v2.0）：

COLREGs Engine (Doer)                    COLREGs Compliance Checker
    │                                           │
    │ 1. 生成 ColregsDecision                   │
    │    (会遇分类+责任判定+行动推荐)             │
    │                                           │
    ├──── ColregsDecision ────────────────────→  │
    │                                           │ 2. 独立校验：
    │                                           │    - 避让方向是否符合 Rule 8/14/15/19？
    │                                           │    - CPA_expected ≥ CPA_safe？
    │                                           │    - 是否从目标船首前方穿越？（Rule 15 禁止）
    │                                           │    - 能见度不良时是否违反 Rule 19(d)？
    │                                           │
    │  ←──── CheckResult ────────────────────── │
    │    {approved: true/false,                 │
    │     violations: [...],                    │
    │     nearest_compliant: ColregsDecision}   │
    │                                           │
    │ 3. 如果 approved=true → 发布决策           │
    │    如果 approved=false → 采用替代方案       │
    │    或重新计算                               │
    ▼                                           │
  Avoidance Planner                             │
```

### B. Checker 校验规则集

COLREGs Compliance Checker 执行的校验是纯确定性的——不涉及 AI 或概率判断：

```
FUNCTION check_colregs_compliance(decision, own_ship, target):
    
    violations = []
    
    # Rule 8: 避让行动应"大幅度"
    IF decision.action_required AND abs(decision.recommended_course_change) < 15:
        violations.append({
            rule: "Rule 8",
            detail: f"航向变化 {abs(decision.recommended_course_change):.0f}° < 最低要求 30°",
            severity: "WARNING"
        })
    
    # Rule 14: 对遇时必须向右转
    IF decision.encounter_type == "head_on" AND decision.recommended_course_change < 0:
        violations.append({
            rule: "Rule 14",
            detail: "对遇时不应向左转",
            severity: "CRITICAL"
        })
    
    # Rule 15: 让路船不应从目标船首前方穿越
    IF decision.my_role == "give_way" AND decision.encounter_type == "crossing":
        bcr = predict_bcr(own_ship, target, decision.recommended_course_change)
        IF bcr < CPA_safe × 0.5:
            violations.append({
                rule: "Rule 15",
                detail: f"避让后 BCR={bcr:.0f}m，将从目标船首前方近距离通过",
                severity: "CRITICAL"
            })
    
    # Rule 19(d): 能见度不良时的转向禁忌
    IF visibility < VISIBILITY_THRESHOLD:
        IF decision.recommended_action == "alter_course_port" AND target.bearing_relative < 90:
            violations.append({
                rule: "Rule 19(d)(i)",
                detail: "能见度不良时禁止向左转避让正横前方目标",
                severity: "CRITICAL"
            })
    
    # CPA 安全检查
    IF decision.expected_cpa_after_action < CPA_safe:
        violations.append({
            rule: "CPA_SAFETY",
            detail: f"预期 CPA {decision.expected_cpa_after_action:.0f}m < CPA_safe {CPA_safe:.0f}m",
            severity: "CRITICAL"
        })
    
    # 生成最近合规替代方案
    IF len(violations) > 0:
        nearest_compliant = compute_nearest_compliant_decision(decision, violations, own_ship, target)
    ELSE:
        nearest_compliant = NULL
    
    RETURN {
        approved: len([v for v in violations if v.severity == "CRITICAL"]) == 0,
        violations: violations,
        nearest_compliant: nearest_compliant
    }
```

### C. 否决后的最近合规替代方案

```
FUNCTION compute_nearest_compliant_decision(original, violations, own_ship, target):
    
    alternative = copy(original)
    
    FOR EACH violation IN violations:
        IF violation.rule == "Rule 14":
            # 对遇向左转被否决 → 改为向右转相同角度
            alternative.recommended_course_change = abs(original.recommended_course_change)
            alternative.recommended_action = "alter_course_starboard"
        
        IF violation.rule == "Rule 15":
            # 穿越船首被否决 → 增大转向角度使 BCR 增大
            alternative.recommended_course_change += 15    # 额外右转 15°
        
        IF violation.rule == "Rule 19(d)(i)":
            # 能见度不良左转被否决 → 改为右转或减速
            alternative.recommended_action = "alter_course_starboard"
            alternative.recommended_course_change = abs(original.recommended_course_change)
        
        IF violation.rule == "CPA_SAFETY":
            # CPA 不足 → 增大转向角度或补充减速
            alternative.recommended_course_change += 10
    
    # 重新计算替代方案的预期效果
    alternative.expected_cpa_after_action = predict_cpa(own_ship, target, alternative)
    
    RETURN alternative
```

### D. Checker 与 ASDR 的集成

所有 Checker 的否决事件必须记录到 ASDR：

```
IF NOT check_result.approved:
    asdr_publish("checker_veto", {
        layer: "L3",
        checker: "COLREGs_Compliance",
        original_decision: decision,
        violations: check_result.violations,
        alternative: check_result.nearest_compliant,
        target: {mmsi, cpa, tcpa, bearing}
    })
```

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-25 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v2.0 升级：增加 COLREGs Compliance Checker 接口（Doer-Checker 双轨）；增加否决后最近合规替代方案生成逻辑；增加 ASDR 否决事件记录 |

---

**文档结束**
