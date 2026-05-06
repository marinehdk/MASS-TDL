# MASS_ADAS Deterministic Checker 确定性校验器技术设计文档

**文档编号：** SANGO-ADAS-CHK-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  
**状态：** v2.0 防御性架构升级新增文档

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. Doer-Checker 双轨哲学
5. 计算流程总览
6. L2 Checker：Route Safety Gate
7. L3 Checker：COLREGs Compliance
8. L4 Checker：Corridor Guard
9. L5 Checker：Actuator Envelope
10. 否决后的回退策略
11. Checker 与 Doer 的接口协议
12. Checker 规则的形式化验证
13. Checker 在降级模式下的行为
14. 内部子模块分解
15. 数值算例
16. 参数总览表
17. 与其他模块的协作关系
18. 测试策略与验收标准
19. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Deterministic Checker（确定性校验器）是 v2.0 防御性架构升级中新增的 X 轴制衡机制。它作为独立的"右轨"与 AI 决策管线（"左轨 Doer"）并行运行。所有由 Doer（L2~L5 的 AI/算法模块）生成的指令，在下发给下一层或执行机构之前，必须经过对应层级的 Checker 校验。

Checker 的核心特征是**确定性**——它只使用数学公式、物理约束和规则查表来做判断，不使用任何机器学习模型或启发式算法。它的判定结果对于同样的输入永远一致，可以被数学证明是正确的。

### 1.2 核心职责

- **安全边界守护：** 确保 Doer 的所有输出都在物理和法规的安全边界之内。
- **一票否决：** 当 Doer 的输出违反安全约束时，Checker 有权否决。
- **回退方案输出：** 否决时不仅说"不行"，还输出"最近的合规方案"——告诉 Doer "你最多只能转到这个角度"。
- **形式化可证明：** Checker 的规则集可以用形式化方法证明其正确性——这是 AI 黑盒无法提供的信任基础。

### 1.3 与现有模块的关系

当前架构中已有 Checker 的雏形：L2 Safety Checker 和 L3 Risk Monitor。v2.0 的升级是将这些嵌入式的校验逻辑**提升为独立的右轨**，并扩展到 L4 和 L5 层。

| 现有模块 | v2.0 中的角色 |
|---------|-------------|
| L2 Safety Checker | 提升为 L2 Checker (Route Safety Gate) |
| L3 Risk Monitor 的安全检查部分 | 提升为 L3 Checker (COLREGs Compliance) |
| L4 CTE Calculator 的走廊评估 | 提升为 L4 Checker (Corridor Guard) |
| L5 Actuator Limiter | 提升为 L5 Checker (Actuator Envelope) |

---

## 2. 输入与输出接口

### 2.1 通用 Checker 接口

每层 Checker 的接口遵循统一模式：

```
CheckerInput:
    Header header
    string layer                    # "L2"/"L3"/"L4"/"L5"
    Any doer_output                 # Doer 层的原始输出（待校验）
    OwnShipState own_ship           # 当前本船状态
    EnvironmentState environment    # 当前环境态势

CheckerOutput:
    Header header
    string layer
    bool approved                   # true = 通过, false = 否决
    string veto_reason              # 否决原因（approved=false 时）
    string[] violated_rules         # 违反的规则列表
    Any nearest_compliant           # 最近的合规方案（否决时提供）
    float64 compliance_margin       # 合规余量（距安全边界的距离，归一化 0~1）
    float64 check_duration_ms       # 校验耗时
```

### 2.2 各层 Checker 的具体输入输出

| Checker | 校验对象 | 主要输入 | 否决时的回退输出 |
|---------|---------|---------|---------------|
| L2 Route Gate | PlannedRoute（规划航线） | 航线航点 + ENC 数据 | 去除违规航段后的安全航线 |
| L3 COLREGs | ColregsDecision + AvoidanceCommand | 避碰决策 + 目标态势 | 最大合规转向角 + 安全速度 |
| L4 Corridor | ψ_cmd + u_cmd（航向/速度指令） | 指令 + CTE + 走廊 | 限幅后的指令 |
| L5 Envelope | δ_cmd + RPM（舵角/转速指令） | 指令 + 物理限位 | 限幅后的执行指令 |

---

## 3. 核心参数数据库

### 3.1 L2 Checker 参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 最小 UKC（开阔） | 2.0 m | 龙骨下最小间隙 |
| 最小 UKC（港内） | 0.5 m | |
| 最小岸距 | 200 m | |
| 禁区缓冲距离 | 100 m | |
| 最大单航段长度 | 50 nm | 超过需分段 |

### 3.2 L3 Checker 参数

| 参数 | 值 | 说明 |
|------|-----|------|
| CPA 最小安全圆 | CPA_safe（分水域） | 不可侵入 |
| 右转避让强制（交叉） | Rule 15 | 交叉让路必须右转 |
| 能见度不良禁左转 | Rule 19(d) | |
| 最小避让幅度 | 30° | Rule 8 "大幅" |

### 3.3 L4 Checker 参数

| 参数 | 值 | 说明 |
|------|-----|------|
| CTE 安全走廊 | 按水域类型（见 CTE Calculator 文档） | |
| 航向变化率上限 | 10°/s | 防止过快转向 |
| 速度上限 | 水域限速 | |
| 速度下限 | V_min_maneuver | |

### 3.4 L5 Checker 参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 舵角限位 | ±35° | 机械极限 |
| 舵角速率限位 | 5°/s | 舵机速率 |
| 主机转速上限 | RPM_max | 额定转速 |
| 主机转速变化率 | 50 RPM/s | 防止热冲击 |
| 侧推力限位 | F_bow_max | 额定推力 |

---

## 4. Doer-Checker 双轨哲学

### 4.1 为什么需要独立的 Checker

AI（深度学习、强化学习、VLM）的决策过程是不可解释的黑盒。它可能在 99.99% 的情况下做出正确决策，但在 0.01% 的极端情况下产生"AI 幻觉"——输出一个物理上不可能或法规上违规的指令。

Checker 的存在不是为了替代 AI——而是为 AI 套上物理级缰绳。AI 可以自由探索最优解，但探索的边界被 Checker 严格限定。

### 4.2 Checker 不做的事

- **Checker 不做优化。** 它不寻找最优解——那是 Doer 的事。Checker 只判断"这个解是否在安全边界内"。
- **Checker 不做预测。** 它不预测目标会怎么动——那是 Target Tracker 的事。Checker 只检查当前时刻的约束是否被满足。
- **Checker 不使用 AI。** 它的全部逻辑都是确定性的数学和规则。

### 4.3 Checker 的信任链

```
Checker 的正确性信任链：

数学/物理定律（不可违反）
    ↓
Checker 规则集（由数学/物理推导，可形式化验证）
    ↓
Checker 实现代码（经过独立 V&V 验证）
    ↓
Checker 运行时输出（确定性——同输入同输出）

对比 AI Doer 的信任链：

训练数据（可能有偏差/遗漏）
    ↓
训练过程（不完全可控）
    ↓
模型权重（黑盒，不可解释）
    ↓
运行时输出（概率性——可能出现幻觉）
```

---

## 5. 计算流程总览

```
Doer 输出（待校验指令）
          │
          ▼
    ┌──────────────────────────────────┐
    │ 对应层级的 Checker                │
    │ 逐条检查安全规则                  │
    └────────────┬─────────────────────┘
                 │
           ┌─────┴─────┐
           │            │
        通过 ✓      否决 ✗
           │            │
           ▼            ▼
      原样下发    ┌──────────────────┐
      给下一层    │ 输出回退方案       │
                  │ "最近的合规方案"   │
                  │ 替代 Doer 输出    │
                  └──────────────────┘
                         │
                         ▼
                    记录否决事件
                    通知 Doer 和 ASDR
```

---

## 6. L2 Checker：Route Safety Gate

```
FUNCTION check_l2_route(planned_route, enc_data, ship_params):
    
    violations = []
    
    FOR EACH leg IN planned_route.legs:
        
        # Rule 1：水深检查
        min_depth = enc_data.get_min_depth_along(leg.start, leg.end, buffer=200m)
        required_depth = ship_params.max_draft + UKC_MIN
        IF min_depth < required_depth:
            violations.append({
                rule: "MIN_DEPTH",
                leg: leg.index,
                detail: f"最小水深 {min_depth:.1f}m < 要求 {required_depth:.1f}m",
                nearest_compliant: suggest_deeper_route(leg, enc_data, required_depth)
            })
        
        # Rule 2：禁区检查
        IF enc_data.intersects_prohibited(leg.start, leg.end, buffer=PROHIBITED_BUFFER):
            violations.append({
                rule: "PROHIBITED_AREA",
                leg: leg.index,
                detail: "航线穿越禁航区",
                nearest_compliant: suggest_route_avoiding(leg, enc_data)
            })
        
        # Rule 3：岸距检查
        min_shore_dist = enc_data.get_min_shore_distance(leg.start, leg.end)
        IF min_shore_dist < MIN_SHORE_DISTANCE:
            violations.append({
                rule: "MIN_SHORE_DISTANCE",
                leg: leg.index,
                detail: f"岸距 {min_shore_dist:.0f}m < 要求 {MIN_SHORE_DISTANCE}m"
            })
        
        # Rule 4：航段长度检查
        IF leg.length > MAX_LEG_LENGTH:
            violations.append({
                rule: "MAX_LEG_LENGTH",
                leg: leg.index,
                detail: f"航段长度 {leg.length:.0f}nm > 上限 {MAX_LEG_LENGTH}nm"
            })
    
    IF len(violations) > 0:
        RETURN {approved: false, violations, veto_reason: format_violations(violations)}
    
    RETURN {approved: true, compliance_margin: compute_min_margin(planned_route, enc_data)}
```

---

## 7. L3 Checker：COLREGs Compliance

```
FUNCTION check_l3_colregs(colregs_decision, avoidance_cmd, targets, environment):
    
    violations = []
    
    FOR EACH decision IN colregs_decision:
        target = find_target(decision.target_track_id)
        
        # Rule 1：CPA 安全圆不可侵入
        IF decision.expected_cpa_after_action < CPA_safe:
            violations.append({
                rule: "CPA_MINIMUM",
                target: decision.target_mmsi,
                detail: f"预期 CPA {decision.expected_cpa_after_action:.0f}m < 安全距离 {CPA_safe:.0f}m",
                nearest_compliant: {max_course_change: compute_min_safe_course_change(target)}
            })
        
        # Rule 2：交叉让路必须右转（Rule 15）
        IF decision.encounter_type == "crossing" AND decision.my_role == "give_way":
            IF decision.recommended_course_change < 0:    # 左转
                violations.append({
                    rule: "RULE_15_STARBOARD",
                    detail: "交叉让路船应向右转",
                    nearest_compliant: {course_change: abs(decision.recommended_course_change)}
                })
        
        # Rule 3：能见度不良时禁止左转（Rule 19(d)）
        IF environment.visibility_nm < 2.0:
            IF decision.recommended_course_change < 0 AND target.bearing_relative < 90:
                violations.append({
                    rule: "RULE_19D_NO_PORT",
                    detail: "能见度不良时禁止向左转避让正横前方目标",
                    nearest_compliant: {course_change: abs(decision.recommended_course_change)}
                })
        
        # Rule 4：避让幅度至少 30°（Rule 8 "大幅"）
        IF decision.action_required AND abs(decision.recommended_course_change) < 30 AND abs(decision.recommended_course_change) > 0:
            violations.append({
                rule: "RULE_8_SUBSTANTIAL",
                detail: f"避让幅度 {abs(decision.recommended_course_change):.0f}° < 最小要求 30°",
                nearest_compliant: {course_change: 30 × sign(decision.recommended_course_change)}
            })
        
        # Rule 5：避让不得穿越浅水
        IF avoidance_cmd.active:
            FOR EACH wp IN avoidance_cmd.waypoints:
                depth = enc_reader.get_depth_at(wp.latitude, wp.longitude)
                IF depth < ship_params.max_draft + UKC_MIN:
                    violations.append({
                        rule: "AVOIDANCE_INTO_SHALLOW",
                        detail: f"避让航点 ({wp.latitude:.4f}, {wp.longitude:.4f}) 水深 {depth:.1f}m 不足",
                        nearest_compliant: "减小转向角度或改用减速避让"
                    })
    
    IF len(violations) > 0:
        RETURN {approved: false, violations, veto_reason: format_violations(violations)}
    
    RETURN {approved: true}
```

---

## 8. L4 Checker：Corridor Guard

```
FUNCTION check_l4_guidance(psi_cmd, u_cmd, cte, corridor, zone_type):
    
    violations = []
    
    # Rule 1：CTE 安全走廊
    IF abs(cte) > corridor.half_width:
        violations.append({
            rule: "CTE_CORRIDOR",
            detail: f"CTE {abs(cte):.0f}m 超出走廊 {corridor.half_width:.0f}m"
        })
    
    # Rule 2：航向变化率
    psi_rate = abs(psi_cmd - psi_cmd_prev) / dt
    IF psi_rate > MAX_HEADING_RATE:
        violations.append({
            rule: "HEADING_RATE",
            detail: f"航向变化率 {psi_rate*180/pi:.1f}°/s > 上限 {MAX_HEADING_RATE*180/pi:.1f}°/s",
            nearest_compliant: {psi_cmd: psi_cmd_prev + sign(psi_cmd - psi_cmd_prev) × MAX_HEADING_RATE × dt}
        })
    
    # Rule 3：速度限制
    zone_speed_limit = get_zone_speed_limit(zone_type)
    IF u_cmd > zone_speed_limit:
        violations.append({
            rule: "SPEED_LIMIT",
            detail: f"速度指令 {u_cmd:.1f}m/s > 水域限速 {zone_speed_limit:.1f}m/s",
            nearest_compliant: {u_cmd: zone_speed_limit}
        })
    
    # Rule 4：速度不低于最低可操纵
    IF u_cmd > 0 AND u_cmd < V_MIN_MANEUVER:
        violations.append({
            rule: "MIN_SPEED",
            detail: f"速度指令 {u_cmd:.1f}m/s 低于最低可操纵速度"
        })
    
    IF len(violations) > 0:
        # L4 Checker 的否决更多是"限幅"而非完全拒绝
        psi_limited = clamp_heading_rate(psi_cmd, psi_cmd_prev, MAX_HEADING_RATE, dt)
        u_limited = clamp(u_cmd, 0, zone_speed_limit)
        RETURN {approved: false, violations, nearest_compliant: {psi_cmd: psi_limited, u_cmd: u_limited}}
    
    RETURN {approved: true}
```

---

## 9. L5 Checker：Actuator Envelope

```
FUNCTION check_l5_actuator(delta_cmd, rpm_cmd, bow_thrust_cmd, actuator_params):
    
    violations = []
    
    # Rule 1：舵角限位
    IF abs(delta_cmd) > actuator_params.delta_max:
        violations.append({
            rule: "RUDDER_LIMIT",
            detail: f"舵角 {delta_cmd*180/pi:.1f}° 超限",
            nearest_compliant: {delta: clamp(delta_cmd, -actuator_params.delta_max, actuator_params.delta_max)}
        })
    
    # Rule 2：舵角速率限位
    delta_rate = abs(delta_cmd - delta_prev) / dt
    IF delta_rate > actuator_params.delta_rate_max:
        violations.append({
            rule: "RUDDER_RATE",
            nearest_compliant: {delta: delta_prev + sign(delta_cmd - delta_prev) × actuator_params.delta_rate_max × dt}
        })
    
    # Rule 3：转速限位
    IF rpm_cmd > actuator_params.rpm_max:
        violations.append({rule: "RPM_LIMIT", nearest_compliant: {rpm: actuator_params.rpm_max}})
    
    # Rule 4：转速变化率
    rpm_rate = abs(rpm_cmd - rpm_prev) / dt
    IF rpm_rate > actuator_params.rpm_rate_max:
        violations.append({
            rule: "RPM_RATE",
            nearest_compliant: {rpm: rpm_prev + sign(rpm_cmd - rpm_prev) × actuator_params.rpm_rate_max × dt}
        })
    
    # Rule 5：侧推力限位
    IF abs(bow_thrust_cmd) > actuator_params.bow_thrust_max:
        violations.append({rule: "BOW_THRUST_LIMIT"})
    
    # L5 Checker 总是输出限幅后的值（不完全否决，而是钳位）
    output = {
        delta: clamp(delta_cmd, -actuator_params.delta_max, actuator_params.delta_max),
        rpm: clamp(rpm_cmd, actuator_params.rpm_min, actuator_params.rpm_max),
        bow_thrust: clamp(bow_thrust_cmd, -actuator_params.bow_thrust_max, actuator_params.bow_thrust_max)
    }
    output = apply_rate_limits(output, prev_output, dt, actuator_params)
    
    RETURN {approved: len(violations) == 0, violations, nearest_compliant: output}
```

---

## 10. 否决后的回退策略

### 10.1 各层的回退行为

| 层级 | 否决后的行为 |
|------|-----------|
| L2 | 返回去除违规航段的安全航线；如无可行航线，拒绝整个航次并通知岸基 |
| L3 | 使用 Checker 给出的最大合规转向角度；如仍不安全，减速至安全速度或停船 |
| L4 | 限幅——将指令钳位到安全范围内（航向变化率、速度限制）|
| L5 | 限幅——将执行指令钳位到物理限位内（始终执行，永不完全否决）|

### 10.2 回退优先级

```
当 Checker 否决时的回退搜索顺序：
1. 使用 Checker 输出的 nearest_compliant（最优回退）
2. 维持上一周期的指令（保守回退）
3. 减速至安全速度（安全回退）
4. 停船（最终回退）
```

---

## 11. Checker 与 Doer 的接口协议

```
# 时序协议：
# 1. Doer 计算完成 → 发布到 checker_input 话题
# 2. Checker 在 T_check_max 内完成校验 → 发布到 checker_output 话题
# 3. 如果 approved=true → 原始指令下发
# 4. 如果 approved=false → nearest_compliant 下发 + 通知 Doer 重新规划
# 5. 如果 Checker 超时（> T_check_max）→ 按"维持上一指令"处理 + 告警

T_check_max = {
    "L2": 5000,     # ms — L2 不是实时关键
    "L3": 100,      # ms — L3 每 500ms 一个决策周期
    "L4": 20,       # ms — L4 每 100ms 一个周期
    "L5": 5         # ms — L5 每 20ms 一个周期
}
```

---

## 12. Checker 规则的形式化验证

### 12.1 可证明的安全性质

```
# L2 Route Gate 的安全性质（可用定理证明器验证）：
Property_1: ∀ leg ∈ approved_route : min_depth(leg) ≥ ship_draft + UKC_min
Property_2: ∀ leg ∈ approved_route : ¬intersects(leg, prohibited_areas)
Property_3: ∀ leg ∈ approved_route : min_shore_dist(leg) ≥ MIN_SHORE_DISTANCE

# L3 COLREGs Checker 的安全性质：
Property_4: ∀ decision ∈ approved_decisions : expected_CPA ≥ CPA_safe
Property_5: ∀ crossing_give_way ∈ approved : course_change > 0  (右转)
Property_6: ∀ poor_vis ∈ approved : ¬(port_turn ∧ target_ahead)

# L5 Actuator Checker 的安全性质：
Property_7: ∀ output : |δ| ≤ δ_max
Property_8: ∀ output : |dδ/dt| ≤ δ_rate_max
Property_9: ∀ output : RPM_min ≤ RPM ≤ RPM_max
```

### 12.2 验证方法建议

- **L5 Checker：** 逻辑简单（纯限幅），可用模型检查器（如 UPPAAL 或 NuSMV）完全验证。
- **L4 Checker：** 逻辑较简单（限幅+走廊），同上。
- **L3 Checker：** 涉及 COLREGs 规则映射，需要领域专家审查规则集的完整性和正确性。可用结构化测试覆盖每条规则的所有分支。
- **L2 Checker：** 涉及 ENC 查询，需要用大量航线数据做回归测试。

---

## 13. Checker 在降级模式下的行为

```
# 传感器降级时，Checker 收紧安全边界

FUNCTION adjust_checker_for_degradation(sensor_status):
    
    IF sensor_count < 4:
        # 传感器不足——增大安全余量
        CPA_safe_effective = CPA_safe × 1.5
        UKC_min_effective = UKC_min × 1.3
        speed_limit_reduction = 0.7    # 降速 30%
    
    IF gps_degraded:
        # GPS 降级——CTE 走廊收窄（位置不确定性增大）
        corridor_effective = corridor × 0.6
    
    IF radar_failed:
        # 雷达故障——最关键传感器丢失
        CPA_safe_effective = CPA_safe × 2.0    # 大幅增大安全距离
        # 建议停船等待修复
```

---

## 14. 内部子模块分解

| 子模块 | 职责 | 层级 | 频率 | 最大延迟 |
|-------|------|------|------|---------|
| Route Safety Gate | 航线安全校验 | L2 | 事件 | 5s |
| COLREGs Compliance | 避碰合规校验 | L3 | 2 Hz | 100ms |
| Corridor Guard | 航向/速度/走廊校验 | L4 | 10 Hz | 20ms |
| Actuator Envelope | 执行机构限幅 | L5 | 50 Hz | 5ms |
| Veto Logger | 否决事件记录 | 全层 | 事件 | N/A |
| Fallback Manager | 回退方案管理 | 全层 | 事件 | N/A |

---

## 15. 数值算例

### 算例：L3 Checker 否决左转避让

```
态势：
  交叉相遇，目标在右舷 45°，CPA 800m < CPA_safe 1852m
  COLREGs Engine (Doer) 建议：向左转 40°

L3 Checker 校验：
  Rule 检查：encounter_type="crossing", my_role="give_way"
  Rule 15 要求：让路船应向右转
  Doer 建议左转 → 违反 Rule 15！

  → VETO: "交叉让路船应向右转（Rule 15）"
  → nearest_compliant: {course_change: +40°}（改为右转 40°）

  Doer 收到否决后：
  → 使用 nearest_compliant 的右转 40° 作为新方案
  → 重新验证 CPA → 2500m > CPA_safe ✓
  → L3 Checker 再次校验 → approved ✓
```

---

## 16. 参数总览表

| Checker | 参数 | 值 |
|---------|------|-----|
| L2 | 最小 UKC（开阔） | 2.0 m |
| L2 | 最小岸距 | 200 m |
| L2 | 禁区缓冲 | 100 m |
| L3 | CPA 安全距离 | CPA_safe（分水域） |
| L3 | 最小避让幅度 | 30° |
| L3 | 能见度不良阈值 | 2.0 nm |
| L4 | 航向变化率上限 | 10°/s |
| L4 | CTE 走廊 | 按水域类型 |
| L5 | 舵角限位 | ±35° |
| L5 | 舵角速率 | 5°/s |
| L5 | 转速限位 | RPM_max |
| L5 | 转速变化率 | 50 RPM/s |
| 全局 | Checker 超时回退 | 维持上一指令 |

---

## 17. 与其他模块的协作关系

| 交互方 | 方向 | 数据 |
|-------|------|------|
| L2 Doer → L2 Checker | 输入 | 规划航线（待校验） |
| L3 Doer → L3 Checker | 输入 | COLREGs 决策 + 避让指令 |
| L4 Doer → L4 Checker | 输入 | ψ_cmd + u_cmd |
| L5 Doer → L5 Checker | 输入 | δ_cmd + RPM_cmd |
| Checker → Doer | 反馈 | approved/veto + nearest_compliant |
| Checker → ASDR | 日志 | 所有否决事件 |
| Checker → Shore Link | 报告 | 高频否决时通知岸基 |
| ENC Reader → L2/L3 Checker | 查询 | 水深、禁区等安全数据 |

---

## 18. 测试策略与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| CHK-001 | 合规航线通过 L2 Checker | approved = true |
| CHK-002 | 穿越浅水的航线 | 否决 + 输出安全替代 |
| CHK-003 | 穿越禁区的航线 | 否决 + 输出绕行方案 |
| CHK-004 | 合规的右转避让 | L3 Checker 通过 |
| CHK-005 | 交叉时左转避让 | L3 Checker 否决 + 改为右转 |
| CHK-006 | 能见度不良时左转 | L3 Checker 否决 |
| CHK-007 | 避让幅度 20°（< 30°） | L3 Checker 否决 + 最小 30° |
| CHK-008 | 航向变化率超限 | L4 Checker 限幅 |
| CHK-009 | 速度超水域限制 | L4 Checker 限幅 |
| CHK-010 | 舵角超限 | L5 Checker 限幅（始终） |
| CHK-011 | Checker 超时 | 维持上一指令 + 告警 |
| CHK-012 | 传感器降级 | 安全参数收紧 |
| CHK-013 | 100 次决策的延迟统计 | L5 < 5ms，L4 < 20ms，L3 < 100ms |

---

## 19. 参考文献

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Shalev-Shwartz, S. et al. "On a Formal Model of Safe and Scalable Self-driving Cars" 2018 | RSS 模型（自动驾驶安全边界） |
| [2] | IMO COLREGs 1972 (amended) | 避碰规则——Checker 规则的法律来源 |
| [3] | IEC 61508 "Functional Safety" | 安全完整性等级（SIL） |
| [4] | DO-178C "Software for Airborne Systems" | 航空软件安全认证标准（参考方法论） |

---

**文档结束**
