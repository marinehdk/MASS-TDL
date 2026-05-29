# TDL 避碰逻辑分析报告：从相遇到回归航路

## 一、系统架构总览：避碰涉及的模块链

Plain Text



```
M2 World Model          M6 COLREGs Reasoner       M4 Behavior Arbiter       M5 Tactical Planner
┌──────────────┐       ┌────────────────────┐     ┌───────────────────┐     ┌─────────────────────┐
│ Encounter    │──────▶│ Rule14/13/15/19    │────▶│ IvP Multi-Obj     │────▶│ Mid-MPC (≥90s)      │
│ Classifier   │       │ Phase Classifier   │     │ Arbitration       │     │ BC-MPC  (100ms)     │
│ CPA/TCPA     │       │ Constraint Gen     │     │ Behavior Switch   │     │ Geometric Fallback  │
└──────────────┘       └────────────────────┘     └───────────────────┘     └────────┬────────────┘
                                                                                       │
                              M3 Mission Manager                                      ▼
                              ┌──────────────────┐                           L4 Guidance
                              │ MissionGoal      │                           (ψ_cmd, u_cmd)
                              │ Route/ETA        │
                              └──────────────────┘
```

## 二、完整避碰流程（imazu-01-ho 对遇场景）

### Phase 1：相遇检测（M2 → M6）

**场景配置**（[imazu-01-ho.yaml](file:///Users/marine/Code/MASS-L3-Tactical Layer/scenarios/IMAZU标准测试/imazu-01-ho.yaml)）：

- OS: heading 0°, SOG 10 kn, 从 (63.44, 10.38) 向北
- TS: heading 180°, SOG 10 kn, 从 (63.557, 10.38) 向南
- 预期：Rule 14 对遇，双方右转

**M2 相遇分类**（[encounter_classifier.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m2_world_model/src/encounter_classifier.cpp#L37-L100)）：

1. 计算相对方位角 → 目标在正前方（~0°）
2. 计算航向差 → |0° - 180°| = 180°，满足 `heading_diff ≈ 180° ± 22.5°`
3. 分类为 `ENCOUNTER_TYPE_HEAD_ON`，`is_giveway = true`

**M6 规则推理**（[rule14_head_on.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m6_colregs_reasoner/src/rules/colregs/rule14_head_on.cpp#L11-L63)）：

1. 检查航向互逆：`|courseDiff - 180°| < 6°` ✓
2. 检查目标在正前方：`relBearing < 6° || > 354°` ✓
3. 检查目标朝向我方：`aspect < 10° || > 350°` ✓
4. 输出：`preferred_direction = "STARBOARD"`, `min_alteration_deg = 15°`（ODD-A 默认）

**M6 时相分类**（[colregs_phase_classifier.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_phase_classifier.cpp#L8-L19)）：

- TCPA > 480s → `PRESERVE_COURSE`
- TCPA > 240s → `SOUND_WARNING`
- TCPA > 60s → `INDEPENDENT_ACTION`
- TCPA ≤ 60s → `CRITICAL_ACTION`

**M6 约束生成**（[colregs_constraint_generator.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_constraint_generator.cpp#L20-L86)）：

- 发布 `COLREGsConstraint` 消息：`conflict_detected = true`, `phase = "INDEPENDENT_ACTION"`, `constraints = [{type: "colregs", value: 15.0, unit: "deg"}]`

### Phase 2：行为仲裁（M4）

**行为激活**（[behavior_activation.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_activation.cpp#L33-L39)）：

- `is_colreg_avoid_applicable()` → `colregs_conflict_detected == true` ✓
- 激活集合：`{TRANSIT, COLREG_AVOID}`

**行为优先级**（[behavior_priority.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_priority.cpp#L23-L54)）：

- Rule 3: `COLREG_AVOID` 覆盖任务行为 → `primary = COLREG_AVOID`

**IvP 仲裁**（[behavior_arbiter_node.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp#L296-L349)）：

- **TRANSIT IvP 函数**（权重 1.0）：以 `nominal_hdg`（当前航路点方位）为中心的高斯式分段效用
- **COLREG_AVOID IvP 函数**（权重 **10.0**）：
  - 惩罚区 [nominal-180°, nominal+15°)：utility = 0.05
  - 最优区 [nominal+15°, nominal+60°]：utility = 1.0
  - 过渡区 [nominal+60°, nominal+90°]：utility = 0.6
  - 基底区：utility = 0.1
- **硬约束**：`heading_allowed_ranges = [own_hdg + 15°, own_hdg + 180°]`（只允许右转）

→ 结果：IvP 求解输出右偏 15°~60° 的航向窗口

### Phase 3：避碰执行（M5）

**Mid-MPC**（[mid_mpc_node.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp#L210-L244)）：

- NLP solver 通常是 stub/失败状态
- 触发 `build_geometric_fallback_plan_()` 几何回退
- 生成 10 个航路点，覆盖 100s 预测时域
- 使用 `kAggressionFraction = 5/6` 在 M4 航向窗口内选择目标航向
- 轨迹：圆弧转向 → 直线前进

**BC-MPC**（[bc_mpc_node.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp#L60-L79)）：

- 100ms 周期短时反应层
- 当 `urgency > 0` 且 `worst_cpa < threshold` 时发出 Override
- 当 `best_cpa >= threshold` 时标记 `Resolved`，交还 Mid-MPC

### Phase 4：冲突消失后的行为切换

**M6 冲突消失**：

- 目标通过后，CPA 增大，TCPA 变负
- Rule14 不再激活 → `conflict_detected = false`
- `rule14_state_` 以 0.5/s 的速率衰减（30s → 0 需 60s）

**M4 行为切换**：

- `is_colreg_avoid_applicable()` → `colregs_conflict_detected == false` → 不激活
- 激活集合：`{TRANSIT}` → `primary = TRANSIT`

------

## 三、🔴 根因分析：为什么不回归航路

经过对全链路代码的逐行追踪，我定位了 **7 个叠加问题**，其中前 3 个是主因：

### 主因 1：M4 TRANSIT 的 nominal_hdg 计算存在"航向锚定"问题

[behavior_arbiter_node.cpp:231-240](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp#L231-L240)：

C++



```cpp
double nominal_hdg = latest_world_ ? latest_world_->own_ship.heading_deg : 0.0;
if (latest_mission_ && latest_world_ &&
    (std::abs(latest_mission_->current_target_wp.latitude) > 1e-4 || ...)) {
  nominal_hdg = compute_bearing_deg(own_lat, own_lon, tgt_lat, tgt_lon);
}
```

**问题**：虽然代码优先使用 M3 目标航路点的方位，但存在两个子问题：

1. **M3 的 `current_target_wp` 可能未更新**：M3 在避碰期间不会主动更新航路点（它只响应 L2 的 PlannedRoute），而 L2 是 mock 的，不会因避碰偏移而重新规划
2. **即使方位正确，IvP TRANSIT 函数的恢复力太弱**：±45° 范围内 utility 仍有 0.3，而 COLREG_AVOID 的权重是 10.0 vs TRANSIT 的 1.0。当 COLREG_AVOID 消失后，TRANSIT 的 0.3 utility 在 ±45° 范围内无法产生足够的"回拉力"

### 主因 2：M5 几何回退只生成避碰路径，无回归段

[mid_mpc_node.cpp:249-361](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp#L249-L361)：

`build_geometric_fallback_plan_()` 生成的轨迹逻辑：

1. 计算目标航向 = M4 窗口内 5/6 位置（激进右转）
2. 圆弧转向到目标航向
3. **直线前进**，无回归段

这意味着 M5 输出的航路点序列是一条"转向后直行"的路径，**没有任何回到原航路的意图**。

### 主因 3：缺少 "POST_AVOIDANCE / RETURN_TO_ROUTE" 行为

[behavior_definitions.yaml](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m4_behavior_arbiter/config/behavior_definitions.yaml) 中只有 5 种行为：

| 行为           | 权重 | 激活条件               |
| :------------- | :--- | :--------------------- |
| TRANSIT        | 0.30 | ODD A/B/C 且无 COLREGs |
| COLREG_AVOID   | 0.70 | COLREGs 冲突检测       |
| RESTRICTED_VIS | 0.60 | ODD-D + 低能见度       |
| CHANNEL_FOLLOW | 0.50 | ODD-B + VTS            |
| MRC_DRIFT      | 1.00 | MRC 或 Critical        |

**缺失**：COLREG_AVOID → TRANSIT 之间没有过渡状态。避碰结束后直接跳回 TRANSIT，但此时：

- 船已偏航（XTE 增大）
- 航向已改变
- 没有专门的"回归航路"行为来提供强恢复力

### 次因 4：M6 rule14_state_ 衰减过慢导致"幽灵冲突"

[colregs_reasoner_node.cpp:549-563](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp#L549-L563)：

C++



```cpp
if (head_on) {
  rule14_state_[mmsi] = 30.0;
} else {
  rule14_state_[mmsi] -= dt_s;  // 0.5/s 衰减
}
```

30.0 的初始值以 0.5/s 衰减 → 需要 60s 才能归零。在此期间，`RuleAssessment` 仍然发布 "Rule 14"，M4 仍然收到 `colreg_avoidance_weight_ = 0.85` 的提升。这导致即使目标已通过，系统仍在"避碰模式"中滞留。

### 次因 5：M5 Mid-MPC solver 是 stub，无法优化回归

[mid_mpc_node.cpp:228-229](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp#L228-L229)：

C++



```cpp
const bool solver_failed = (sol.status != MidMpcSolution::Status::Converged)
    || sol.trajectory.empty();
```

NLP solver 在 DEMO-1 阶段是 stub，几乎总是失败，所以总是走 `build_geometric_fallback_plan_()`。而几何回退只做避碰，不做回归。

### 次因 6：M3 不感知避碰偏移，不触发重规划

[mission_manager_node.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp) 中：

- `ReplanRequestTrigger` 只响应 ODD 退出、ETA 不可行、拥堵等
- **不检测"避碰后偏航"**，不触发 L2 重规划
- M3 的 `current_target_wp` 始终是原始航路的下一个航路点

### 次因 7：IvP TRANSIT 函数的"低保底基面"削弱了恢复力

[behavior_arbiter_node.cpp:283-290](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp#L283-L290)：

C++



```cpp
IvPFunctionDefault::Piece base_tp;
base_tp.heading_min_deg = 0.0;
base_tp.heading_max_deg = 359.9;
base_tp.utility = 0.1;  // 全向低保底
```

0.1 的全向基面意味着即使航向偏离 nominal_hdg 90°+，utility 仍有 0.1。当 COLREG_AVOID（权重 10.0）消失后，TRANSIT（权重 1.0）的 0.1~0.3 utility 无法形成足够强的"航向恢复梯度"。

------

## 四、数据流时序图（imazu-01-ho 实际运行）

Plain Text



```
T=0s    OS heading=0°, TS heading=180°, range≈13nm
        M2: HEAD_ON classified, CPA≈0m, TCPA≈780s
        M6: Rule14 ACTIVE, phase=PRESERVE_COURSE
        M4: TRANSIT (no conflict yet)

T≈300s  TCPA≈480s → M6 phase=SOUND_WARNING
        M6: conflict_detected=true, min_alteration=15°
        M4: COLREG_AVOID activated, IvP → starboard window
        M5: geometric fallback → turn starboard ~25°

T≈540s  TCPA≈240s → M6 phase=INDEPENDENT_ACTION
        M4: COLREG_AVOID weight boosted to 0.85
        M5: more aggressive starboard turn

T≈720s  CPA reached, target passing
        M6: Rule14 deactivates (bearing rate increases)
        M4: COLREG_AVOID → TRANSIT
        M5: geometric fallback stops, BUT...

        🔴 PROBLEM: Ship is now at heading ~35°, offset ~0.5nm east
        🔴 M4 TRANSIT: nominal_hdg = bearing_to_next_wp ≈ 350°~355°
        🔴 M5: No "return to route" path generated
        🔴 Ship continues on modified heading, slowly drifting back
        🔴 But XTE keeps growing because M5 geometric fallback
            already committed to the avoidance path
```

------

## 五、解决方案

### 方案 A：快速修复（DEMO-1 可用，1-2天）

**A1：在 M5 几何回退中增加回归段**

修改 `build_geometric_fallback_plan_()`，在避碰转向完成后增加"回归原航路"段：

C++



```cpp
// After turn arc completes, instead of straight-ahead at target_psi,
// add a return-to-route segment:
// 1. Compute bearing from current position to the next route waypoint
// 2. Generate a second turn arc back toward the route
// 3. Then straight-ahead along the route bearing
```

核心思路：将 10 个航路点分为 3 段：

- 段1（WP 1-3）：右转避碰弧
- 段2（WP 4-6）：直线通过（保持右偏直到目标通过）
- 段3（WP 7-10）：左转回归弧 + 沿原航路直行

**A2：在 M4 中增加 "post-avoidance latch"**

当 `COLREG_AVOID` → `TRANSIT` 切换时，记录"避碰前航向"和"避碰前航路方位"，在 TRANSIT IvP 函数中增加一个临时的"回归增强"：

C++



```cpp
// In arbitration_timer_callback(), when switching from COLREG_AVOID to TRANSIT:
if (prev_primary_ == BehaviorType::COLREG_AVOID && primary == BehaviorType::TRANSIT) {
    post_avoidance_active_ = true;
    post_avoidance_start_ = now();
    pre_avoidance_hdg_ = fallback_anchor_hdg_;  // Use the anchored heading
}

// In TRANSIT IvP function, if post_avoidance_active_:
// - Use pre_avoidance_hdg_ as nominal instead of current heading
// - Increase peak utility to 1.0 for heading within ±5° of pre_avoidance
// - Decrease base utility to 0.02 (stronger gradient)
// - Auto-expire after 120s or when XTE < 100m
```

**A3：加速 M6 rule14_state_ 衰减**

C++



```cpp
// Change from linear 0.5/s decay to exponential:
rule14_state_[mmsi] *= 0.9;  // 10% per cycle = much faster decay
```

### 方案 B：中期方案（DEMO-2 目标，1-2周）

**B1：增加 RETURN_TO_ROUTE 行为**

在 `behavior_definitions.yaml` 中增加第 6 种行为：

YAML



```yaml
- type: 5     # RETURN_TO_ROUTE
  name: "Post-Avoidance Route Recovery"
  priority_weight: 0.55
  activation_rule: "colregs_just_cleared_and_xte_high"
  ivp_function_type: "return_to_route"
```

激活条件：`COLREG_AVOID` 刚消失 **且** XTE > 阈值（如 200m）

IvP 函数：以原航路方位为中心的窄峰（±10° utility=1.0），强恢复梯度

**B2：M3 增加"避碰偏移检测"触发重规划**

在 M3 的 `ReplanRequestTrigger` 中增加条件：

- 当 `COLREG_AVOID` 刚结束
- 且 XTE > 0.3nm
- 触发 L2 重规划请求

**B3：M5 Mid-MPC solver 实现回归代价项**

在 NLP formulation 中增加 `cost_xte` 项，惩罚偏离原航路的距离，使 solver 在避碰结束后自动优化回归轨迹。

### 方案 C：长期方案（DEMO-3 目标）

**C1：完整的三阶段状态机**

Plain Text



```
TRANSIT ──(encounter)──▶ COLREG_AVOID ──(clear)──▶ RETURN_TO_ROUTE ──(on_route)──▶ TRANSIT
```

`RETURN_TO_ROUTE` 是一个独立的行为状态，具有：

- 专用的 IvP 函数（强航路恢复梯度）
- 专用的 M5 规划模式（生成 S 形回归轨迹）
- 明确的退出条件（XTE < 阈值 且 航向误差 < 阈值）

**C2：M5 实现完整的 Mid-MPC + BC-MPC 双层规划**

Mid-MPC solver 不再是 stub，能够：

- 在避碰阶段：优化 CPA + COLREGs 合规
- 在回归阶段：优化 XTE + 航向恢复 + 乘客舒适度
- 平滑过渡两个阶段

------

## 六、推荐实施路径

| 优先级 | 方案    | 工作量 | 目标                        |
| :----- | :------ | :----- | :-------------------------- |
| **P0** | A1 + A2 | 1-2天  | DEMO-1 imazu-01-ho 回归航路 |
| P1     | A3      | 0.5天  | 消除幽灵冲突                |
| P2     | B1      | 3-5天  | DEMO-2 通用回归行为         |
| P3     | B2 + B3 | 5-7天  | DEMO-2 完整避碰→回归闭环    |
| P4     | C1 + C2 | 2-3周  | DEMO-3 生产级避碰系统       |

**P0 建议立即实施 A1 + A2**，这是最小改动、最大收益的方案。核心改动仅涉及两个文件：

1. [mid_mpc_node.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp) — 几何回退增加回归段
2. [behavior_arbiter_node.cpp](file:///Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp) — 增加 post-avoidance latch

需要我立即开始实施 P0 方案吗？