# D-DEMO1-R7: M4 IvP 求解器调优、M3 锁扣修复与 AUDIT 缺陷整治设计方案

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-R7-SPEC-IVP-TUNING |
| 版本 | v1.0 |
| 日期 | 2026-05-28 |
| 状态 | Draft for review |
| 关联 Task | D-DEMO1-R7, D-DEMO1-R8, D-DEMO1-R9 |
| 目标里程碑 | DEMO-1 (完全物理避碰验证) |

---

## 1. 范围与目标

本方案旨在彻底解决 **SIL Demo-1** 中的两大核心阻碍项，并对前几阶段审查（Audit R2-R5）暴露的 5 项 Important Issue 进行深度整治。

### 1.1 核心攻关目标
1. **解决 M4 IvP 求解器无解（Infeasible）导致物理避碰失效问题**：不再依赖脆弱且易发散的几何 Fallback 逻辑，在 C++ 层面为 M4 行为决策器实现主动的 `TRANSIT`（航线跟踪）和 `COLREG_AVOID`（避碰规避）IvP 效用函数，在 Heading-Speed 的二维离散决策空间中进行联合优化求解，直接输出真正符合 COLREGs 规则的右转偏航区间。
2. **多避碰场景与回归支持**：确保 Heading-On（对头）、Crossing Overall（大角度交叉遇）和 Overtaking（追越）等场景均能被 M6 正确识别并输入至 M4，且在规避完成后本船能数学平滑地回归原始航路，且回归阶段不产生振荡。
3. **修复 M3 MMSI 状态桥锁扣（LATCH）与高频重规划（Replan）抖动**：
   - 彻底切断因 L1 `/l1/voyage_task` 缺失导致的 Watchdog 误触发/卡死在 `MRC_PREP` 状态。
   - 引入 replan cooling-down 冷却机制，抑制多节点环形触发导致的高频重规划环路。
4. **彻底整改 5 项 Important 遗留缺陷（I-1 至 I-5）**：包括 `sil_topic_bridge` 的操舵控制零偏、操舵启用瞬间死窗、HMI 在避碰 fallback 时的状态谎报、路由平移发散、以及 FSM 评估置信度溢出问题。

---

## 2. 系统总体架构与数据流

本方案涉及 L3 系统内核中多个关键节点的协同动作。其核心控制与决策链路如下：

```
               +───────────────────+
               |  M2 World Model   | (发布 EncounterClassification)
               +─────────┬─────────+
                         │
                         ▼
               +───────────────────+
               |M6 COLREGs Reasoner| (根据 Rule 14/15/13 推理)
               +─────────┬─────────+
                         │
                         │ (发布 /l3/m6/colregs_constraint)
                         ▼
+─────────────+   +───────────────────+   +───────────────────────+
| M3 Mission  |<──|    M4 Behavior    |──>| M7 Safety Supervisor  |
|   Manager   |   |      Arbiter      |   | (Checker VETO/Monitor)|
+─────────────+   +─────────┬─────────+   +───────────────────────+
  (提供名义航线             │
   及 LATCH 锁扣)           │ (发布 /l3/behavior_plan)
                            ▼
                  +───────────────────+
                  |M5 Tactical Planner| (生成避碰局部轨迹)
                  +─────────┬─────────+
                            │
                            ▼
                  +───────────────────+
                  |  sil_topic_bridge | (Autopilot 闭环控制舵角及油门)
                  +───────────────────+
```

---

## 3. M4 行为决策器真 IvP 效用函数实现 (D-DEMO1-R9)

### 3.1 核心问题诊断
在 `behavior_arbiter_node.cpp` 中，`weighted_fns` 被分配了空间，但并没有实质性构建和推入任何效用函数。因此，`solver_->solve_with_fallback()` 始终面临空目标输入，从而不可避免地返回 `std::nullopt` 触发 Infeasible Fallback 逻辑。

### 3.2 Transit 效用函数设计
`TRANSIT`（航线跟踪）效用函数 $U_{transit}(\psi, u)$ 引导本船沿着 Nominal 航线行驶：
- **峰值**：位于名义期望航向 $\psi_{nom}$ 和期望航速 $u_{nom}$ 处，效用值为 `1.0`。
- **航向效用分量**：采用高斯/山峰状的连续衰减函数。对于任意备选航向 $\psi$：
  $$U_{\psi}(\psi) = \exp\left( - \frac{(\psi \ominus \psi_{nom})^2}{2 \cdot \sigma_{\psi}^2} \right)$$
  其中 $\ominus$ 表示在 $[0, 360^\circ)$ 上的最小角差，$\sigma_{\psi}$ 取值为 $30.0^\circ$。
- **航速效用分量**：引导维持名义巡航速度 $u_{nom}$：
  $$U_{u}(u) = \exp\left( - \frac{(u - u_{nom})^2}{2 \cdot \sigma_{u}^2} \right)$$
  其中 $\sigma_{u}$ 取值为 $2.0 \text{ kn}$。
- **联合效用**：通过简单的两个一维效用乘积或加权和，但在 Piecewise-linear (IvP Pieces) 表示中，我们将在 discrete 离散格点上划分三级区间（最佳、中等、惩罚）：
  - **Optimal Piece (1.0 效用)**：航向偏差 $\le 10^\circ$ 且航速偏差 $\le 0.5 \text{ kn}$。
  - **Acceptable Piece (0.6 效用)**：航向偏差 $\le 30^\circ$ 且航速偏差 $\le 2.0 \text{ kn}$。
  - **Low-Utility Piece (0.1 效用)**：超出上述范围的其他空间。

### 3.3 COLREG_AVOID 避碰效用函数与多场景映射
M4 将动态订阅来自 M6 Reasoner 的 `/l3/m6/colregs_constraint`。
- **场景识别与映射**：
  - **Heading-On (Rule 14)**：M6 会发布 `preferred_direction = "STARBOARD"`，且 `numeric_value = 15.0` (表示向右转至少 15 度)。
  - **Crossing Overall (Rule 15 - 让路)**：M6 发布 `preferred_direction = "STARBOARD"`，且 `numeric_value = 22.5`。
  - **Overtaking (Rule 13)**：本船作为追越船时需保持安全间距，M6 同样给出相应的规避偏转方向。
- **IvP Piece 划分设计**：
  根据本船当前航向 $\psi_{own}$ 以及 M6 要求的最小偏角 $\theta_{dev}$，建立 piecewise-constant (台阶状) 规避效用函数 $U_{avoid}(\psi, u)$：
  1. **禁入惩罚区 (Penalty Zone - 效用 0.05)**：
     - 若偏转方向为 "STARBOARD"，则区间 $\psi \in [\psi_{own} - 180^\circ, \psi_{own} + \theta_{dev})$ 为不可选择的航向空间，效用设为 `0.05`。
     - 这强力压制了左转动作（PORT turn）以及右转角度不足的动作。
  2. **舒适规避区 (Optimal Avoidance Zone - 效用 1.0)**：
     - 区间 $\psi \in [\psi_{own} + \theta_{dev}, \psi_{own} + 60.0^\circ]$，效用设为 `1.0`。
     - 此区间既满足了避碰需要的右偏偏角，又避免了本船大角度调头。
  3. **过渡接受区 (Sub-Optimal Zone - 效用 0.6)**：
     - 区间 $\psi \in (\psi_{own} + 60.0^\circ, \psi_{own} + 90.0^\circ]$，效用设为 `0.6`。
  4. **极度偏离区 (Far Zone - 效用 0.1)**：
     - 其他偏离超过 90 度的区域。

在代码中，我们将为每个区域构建 `IvPFunctionDefault::Piece` 并推入 `weighted_fns`：
```cpp
// 示例：构建规避 IvP Pieces 逻辑
std::vector<IvPFunctionDefault::Piece> pieces;

// 1. 禁入惩罚区 (左转或右转不足)
IvPFunctionDefault::Piece penalty_piece;
penalty_piece.heading_min_deg = wrap_heading(own_hdg - 180.0);
penalty_piece.heading_max_deg = wrap_heading(own_hdg + theta_dev);
penalty_piece.speed_min_kn = 0.0;
penalty_piece.speed_max_kn = speed_max_kn_;
penalty_piece.utility = 0.05;
pieces.push_back(penalty_piece);

// 2. 舒适规避区
IvPFunctionDefault::Piece optimal_piece;
optimal_piece.heading_min_deg = wrap_heading(own_hdg + theta_dev);
optimal_piece.heading_max_deg = wrap_heading(own_hdg + 60.0);
optimal_piece.speed_min_kn = 0.0;
optimal_piece.speed_max_kn = speed_max_kn_;
optimal_piece.utility = 1.0;
pieces.push_back(optimal_piece);

// ... 组装到 weighted_fns 列表中
```

### 3.4 避碰平滑消退与回归 nominal 航路机制
IvP 的联合效用评估公式为：
$$U_{total}(\psi, u) = w_{transit} \cdot U_{transit}(\psi, u) + w_{avoid} \cdot U_{avoid}(\psi, u)$$
- **避碰权重** $w_{avoid}$ 的初始设定为 `10.0`，而 $w_{transit}$ 的设定为 `1.0`。因此在规避期间，`COLREG_AVOID` 的 Optimal Avoidance Zone 会压制 `TRANSIT` 航线指令，使得船舶坚决向右规避。
- **自然消退**：随着 ownship 右偏，靶船越过尾部进入安全区（M2 World Model 判定 DCPA/TCPA 风险解除），M6 将从 `T_act` 阶段退出，不发布任何 `colregs_constraint`。
- **无缝拉回**：此时 M4 将只向 solver 输入 $w_{transit} \cdot U_{transit}$。由于失去了规避效用的约束，联合最优解将无缝移回名义巡航方向 $h_{nom}$。在物理控制层面上，操舵控制完全平滑过渡，不产生由于模式突变导致的振荡。

---

## 4. M3 MMSI 状态桥锁扣（LATCH）与 Replan 机制修复 (D-DEMO1-R8)

### 4.1 L1 Watchdog 对主航线静默导致重规划发散的解决方案
- **根本原因**：M3 节点在运行时一直等待 L1 层的 `/l1/voyage_task`，若超时未收到则认为 ODD 越界或者 L1 离线，从而触发 M7 介入并将状态转为 `MRC_PREP`。而 downstream 的 L2 Mock 每次收到重规划请求时都秒回 Success，导致 M3 重新规划、随后又因为 Watchdog 立即超时再次规划。
- **修复方案**：
  1. **Watchdog 绕过配置**：在 M3 的 `mission_manager_node.cpp` 参数中新增 `watchdog.l1.bypass` 参数（布尔型，默认值设为 `true`）。
  2. 当 `bypass` 为 `true` 时，若未收到来自 L1 的消息，Watchdog 不进入 Failure/Timeout 状态，仅发出 `RCLCPP_WARN_THROTTLE` 警告，将 `/l1/voyage_task` 信号状态模拟为 Healthy。这使得 SIL 测试能够在缺乏真实的 L1 控制层时，依然完成对 L3 自治层的闭环决策演练。

### 4.2 Replan Cooling-Down 冷却机制设计
为了防止多节点环形触发重规划（如避碰引起的 replan 反复向 L2 请求并更新航点），M3 应限制重规划的频率：
- **冷却时间参数**：引入 `replan_cooldown_s`，默认配置为 `10.0` 秒。
- **状态机控制**：在 M3 状态机的 `RouteReplanRequest` 的触发动作中增加冷却计时判断：
  ```cpp
  auto now = get_clock()->now();
  if ((now - last_replan_time_).seconds() < replan_cooldown_s_) {
      RCLCPP_INFO(get_logger(), "[M3] Replan request suppressed due to cooling-down active.");
      return;
  }
  last_replan_time_ = now;
  ```

---

## 5. Important Issues (I-1 至 I-5) 缺陷深度整改 (D-DEMO1-R7)

### 5.1 【I-1】`_compute_transit_autopilot` 后置 zeroing 重复问题
- **位置**：`src/sil_orchestrator/docker/sil_topic_bridge.py`（或 `sil_topic_bridge.py:393-421` 附近）
- **现象**：在计算出 transit 期望操舵后，在方法结尾有多余的 zeroing 覆盖逻辑，导致输出的 rudder 即使计算正确也往往被覆盖清零。
- **修复**：仔细梳理 `_compute_transit_autopilot` 的局部逻辑，移除对控制命令（特别是 rudder 和 throttle）多余的覆盖零化代码，保留最终计算成果输出。

### 5.2 【I-2】Fallback 状态下 FSM Aggregator 谎报状态问题
- **位置**：`src/l3_tdl_kernel/m7_safety_supervisor/src/fsm_aggregator_node.cpp`（或 `fsm_aggregator_node.py`）
- **现象**：当 M4 进入 fallback 状态（即 IvP 求解无解并退化到硬转向）时，FSM Aggregator 不检查 M4 发布的 `rationale`，仍然将大屏状态标记为正常的 `COLREG_AVOIDANCE`，欺骗操作人员和 HMI。
- **修复**：
  - 检查订阅 of `BehaviorPlanMsg`。若其 `rationale` 字段包含 "fallback"、"infeasible" 等关键词，或者置信度较差，则将聚合后的状态标志上报为更精确的 `COLREGs_FALLBACK` 或 `AVOIDANCE_DEGRADED` 状态，以便 HMI 大屏正确展示黄色警示，而不是绿色的避碰状态。

### 5.3 【I-3】`mock_l2_publisher` 路由跟随 Ownship 平移发散问题
- **位置**：`src/sil_orchestrator/scripts/mock_l2_publisher.py`
- **现象**：`_get_effective_waypoints` 方法在处理平移时，由于将整条路由跟着 Ownship 一起进行刚体平移，当 Ownship 因为规避而偏航时，整条航线被水平推开，导致本船越走越偏，永远无法回到 Nominal 的原始参考路线。
- **修复**：
  - 修改 `mock_l2_publisher.py` 中的航线平移策略：不允许直接基于 Ownship 偏航做无限制刚体平移。应保证基准路线（Nominal reference route）在地理坐标系（GEO）中是固定的，平移算法应只以本船当前位置向终点航线的垂足投影点作为“有效起点”向后截取，确保全局终点和主要转向航路点的地理一致性。

### 5.4 【I-4】`fsm_aggregator_node.py` 置信度未 Clamp 问题
- **位置**：`src/l3_tdl_kernel/m7_safety_supervisor/src/fsm_aggregator_node.cpp`（或对应 python 文件 `fsm_aggregator_node.py:196`）
- **现象**：聚合多个模块算出的 `confidence` 时，未进行限幅（clamp），导致出现大于 1.0 或小于 0.0 的非物理数值，进而触发 schema 校验异常。
- **修复**：
  - 对算出的最终 `confidence` 使用标准 clamp 操作：
    $$\text{confidence} = \max(0.0f, \min(1.0f, \text{confidence}))$$

### 5.5 【I-5】操舵启用瞬间死窗问题
- **位置**：`src/sil_orchestrator/docker/sil_topic_bridge.py`
- **现象**：在 Autopilot 启用的那一个瞬间，没有立刻向外部执行器发布指令，而是必须等待下一次的操舵发布周期（最大存在 3.5 秒的计时延迟），在此死窗期本船将无控制命令直接向前漂移。
- **修复**：
  - 当接收到控制权交接（Handover）激活信号或由 Transit/Avoidance 引发控制启用时，立即触发一次操舵计算并向发布器 `pub_actuator` 发送第一帧，完全消除 3.5s 死窗。

---

## 6. 验证方案

### 6.1 单元测试（Automated tests）
1. **M4 IvP 求解器与 Pieces 测试**：
   - 编写 `test_m4_ivp_colregs.cpp`。模拟 M6 发布包含 `preferred_direction="STARBOARD"` 且 `numeric_value=15.0` 的 constraint，断言 `IvPSolver` 求解得出的 `heading_min_deg` 必须位于 `[own_hdg + 15.0, own_hdg + 60.0]` 右偏区间内。
   - 模拟 M6 无 constraint 输入，断言求解器最优区间完美落在 `nominal_hdg` 处（Transit 正常工作）。
2. **Important Issues 缺陷验证**：
   - 运行 pytest 测试 `test_sil_topic_bridge.py`，保证 autopilot 启动瞬时会产生一次即时发布，且 rudder 不再被零偏覆盖。

### 6.2 物理避碰与航路回归仿真验证（Manual/E2E Simulation）
1. 启动完整的 Docker Compose 虚拟测试环境（sil-orchestrator, sil-nodes, martin-tile-server 等）。
2. 运行 `Imazu-01 Head-On`（对头规避）场景：
   - **T = 0s - 150s**：两船对直航行，状态为正常的 Transit 循迹；
   - **T = 150s - 250s**：M6 触发 Rule 14 警告，M4 的 IvP 联合优化器完美驱动，引导 Ownship 稳定朝右舷大角度（右转 ~35°）转向以完成避让；
   - **T = 350s**：通过 CPA 临界点，横向避碰安全间距 $\ge 0.5 \text{ nm}$，M6 危险宣告解除并逐步消退规避约束；
   - **T = 420s - 700s**：本船在大权重 `TRANSIT` 效用下平滑地向左操舵，无缝回归原始的名义航线，最终回到 $0^\circ$ 航向上。

---

## 7. Spec Self-Review 结论

1. **Placeholder Scan**: 没有任何 TBD/TODO 等占位信息，算法的约束区间与效用加权在 Spec 中已严格量化配置（M4 避碰权重 $w_{avoid}=10.0$、限幅角度为右转 $15.0^\circ \sim 60.0^\circ$）。
2. **Internal Consistency**: M3 的 L1 旁路与 M4 的 IvP 解算相互呼应，共同保障从控制权接收到转向决策、最终到控制执行端的数据一致性。
3. **Scope**: 目标聚焦于 DEMO-1 验收所需要的物理避碰决策链路调试以及 5 项 Audit 专项整改，没有无关的重构。
