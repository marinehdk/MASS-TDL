# M5 P6 — BC-MPC 激活 + 四状态交接机 + 回退链 设计

> **产出**: 2026-07-18,P6 brainstorming 落 spec。
> **工作树**: `.worktrees/m5-design-grounding`(分支 `codex/m5-design-grounding`)
> **前序**: P5(c9b7b558c)→ P5 完成(73e810cca,3 Critical 全修,0 Critical 通过)
> **来源裁决**: roadmap §P6 + colav-design-log VR-01b/VR-08 + 2026-07-18 brainstorming 9 个 Q&A
> **模式**: 全量实现,先 SIL baseline 后改 FSM

---

## 1. 权威性与文档分工

| 文档 | 管什么(权威) | 不管什么 |
|---|---|---|
| **本 spec(P6)** | P6 做什么 / 11 状态机定义 / 转换规则 / keep-last 废除 / FINAL_DEGRADE / L4 接线 / 验收门 | BC-MPC 三档分支几何(P2-B-01 已落地)、COLREGs 规则(M6 已落地) |
| `specs/2026-07-17-m5-mpc-p0-p7-roadmap.md` §P6 | phase scope / 依赖 / 风险 / 子项 P6-a/P6-b | 状态机细节(本 spec) |
| `specs/2026-07-16-m5-mpc-colav-solution-pack.md` VR-01b/VR-08 | 四状态机裁决依据 + keep-last 废除依据 + FINAL_DEGRADE 报 M7 | 工程实现细节(本 spec) |
| `design-logs/2026-07-16-m5-mpc-colav-design-log.md` | 决策树全流程(Step1–6) | phase 归属(roadmap) |

**冲突仲裁**: 本 spec 与 roadmap §P6 一致。roadmap §P6 原"GNC VR-08 ReactiveOverrideCmd →L4 接入路径归 GNC-P1/P2",**2026-07-18 brainstorming Q6 用户裁决扩展 P6 范围到 M5 + L4 双侧**,与本 spec §6 一致。

---

## 2. P6 起点证据(2026-07-18 codegraph + 代码探索)

### 2.1 已落地(P1b-1b / v2.2 Phase E2)

| 组件 | 现状 | 文件:行 |
|---|---|---|
| BC-MPC 节点骨架 | 202 行,订阅 `consecutive_failures`(Reliable QoS),发布 `ReactiveOverrideCmd` | `src/bc_mpc/bc_mpc_node.cpp` |
| BC-MPC 三档分支 | 5/7/13 candidates,恒向恒速 dead-reckoning,**无速度优化** | `src/bc_mpc/bc_mpc_branch_formulation.cpp:39-67` |
| BC-MPC override generator | 单位转换 + heading 归一化 | `src/bc_mpc/bc_mpc_override_generator.cpp` |
| `committed_route` 9 状态机 | Idle/CandidateEvaluating/Committed/HeartbeatOnly/KeepLast/Stale/DegradedHold/Released/BcMpcFollow | `include/.../committed_route.hpp:11-21` |
| take-over OR 条件 | `consecutive≥3 OR minalt_box_infeasible OR speed_gap_infeasible` | `src/mid_mpc/mid_mpc_node.cpp:896-908` |
| stale 45s/15°/20% 门控 | 已实装 | `src/committed_route/committed_route.cpp:252-289` |
| `publish_keep_last_` BcMpcFollow 特判 | suppress stale corridor + 空 plan heartbeat | `src/mid_mpc/mid_mpc_node.cpp:1420-1453` |
| **L4 override consumer(FCB Sim)** | **已订阅** `/l3/m5/reactive_override_cmd`,含 validity 过期回 trajectory 跟踪 | `src/sim_workbench/fcb_simulator/src/fcb_simulator_node.cpp:46-47, 136-159` |
| **L4 override consumer(GNC)** | GNC `ActiveRouteManagerNode` 含 `is_emergency_avoidance_mode` 逻辑 | `third_party/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp` |
| **M7 订阅 override + safety_concern** | **已订阅** `/l3/m5/reactive_override_cmd`(events QoS)+ `/l3/safety/concern`(events QoS) | `src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp:280-313` |

### 2.2 P6 真实债务

1. **`bc_mpc_node` 不在 launch**:`m5_mid_mpc.launch.py` 只启 mid_mpc,**BC-MPC 从未在 SIL 实际运行过**
2. **take-over 信号闭环未验证**:mid→BC `consecutive_failures` 主题已连,但 BC 实际发 `ReactiveOverrideCmd` 到 L4 的端到端链路没在 SIL 跑通
3. **当前 FSM 不是四状态机**:实际是 `consecutive≥3` 单 boolean 阈值 → `BcMpcFollow`/`DegradedHold` 二选一,**无 hysteresis**(连续 2 周期交还)、**无 M7 态**(FINAL_DEGRADE)、**无 neutral safe state**
4. **`keep-last` 空 plan 路径仍存在**:`publish_keep_last_` 对非 BcMpcFollow 状态仍 republish stale active_geometry(L1455+)
5. **BC validity 过期 silent deactivate**:`is_bc_active_=false` 仅 log warn,**不 escalate**
6. **`SafetyConcernEvent.concern_type` 缺 FINAL_DEGRADE 枚举**:现有 3 值(IvpInfeasible/OddDegraded/EtaInfeasible),无 BC-MPC 终态
7. **无 BC health metrics 上报通道**:committed_route 无法基于 BC 失效判定 FINAL_DEGRADE

---

## 3. 范围与决策汇总(2026-07-18 brainstorming 9 个 Q&A)

| # | 决策 | 选项 | 影响 |
|---|---|---|---|
| Q1 | 范围 | **全量一次性**(5 项一起) | 不拆 P6-a/P6-b,单 spec/plan 覆盖 |
| Q1.5 | 实施顺序 | **先 SIL baseline 后改 FSM** | T1 先跑当前行为,T2-T7 全量改 |
| Q2 | keep-last 策略 | **彻底废除**,所有非 Committed 状态发空 plan heartbeat + ASDR audit | `publish_keep_last_` 重写 |
| Q3 | FINAL_DEGRADE 触发 | **BC 失效 + Mid 未恢复 双条件** | 新增 BC health metrics |
| Q4 | Handover hysteresis | **2 周期 × 60s + 双条件**(Mid Converged + BC predicted_cpa ≥ cpa_safe) | committed_route 加 hysteresis 计数器 |
| Q5 | launch 方式 | **同 launch 同 yaml** | `m5_mid_mpc.launch.py` + `m5_params.yaml` |
| Q6 | L4 接线 | **M5 + L4 双侧**(L4 consumer 已存在,P6 验证 + 补 safety_concern) | L4 工作量低于预估 |
| Q7 | geo fallback | **转发 M7 MRM 指令**,M5 不自生成 | FINAL_DEGRADE 后 M5 静默等 M7 |
| Q8 | BC 激活门控 | **Mid 失败触发 + BC inner gate**(urgency/CPA) | 维持现状 OR 条件 |
| Q9 | 验收 | **SIL 端到端验证** | 近距场景触发完整链 |

**架构方案(2026-07-18 方案 A)**:分层组件 + committed_route 升级为权威 FSM。`committed_route` 从 9 状态扩展到 11 状态,BC-MPC 不自管 FSM,职责单一(detector + override publish + health 上报)。

---

## 4. 11 状态机权威定义

### 4.1 状态枚举扩展

```cpp
// include/m5_tactical_planner/committed_route/committed_route.hpp
enum class LifecycleState : std::uint8_t {
  Idle = 0U,
  CandidateEvaluating = 1U,
  Committed = 2U,
  HeartbeatOnly = 3U,
  KeepLast = 4U,           // publish 路径改为空 plan heartbeat(P6 废除 keep-last republish)
  Stale = 5U,              // publish 路径改为空 plan heartbeat
  DegradedHold = 6U,       // publish 路径改为空 plan heartbeat + ASDR audit
  Released = 7U,
  BcMpcFollow = 8U,        // 维持语义,转换规则改(P6)
  HandoverNeutral = 9U,    // 新(P6):BC 交还 Mid 的过渡态,连续 2 周期双条件才 Committed
  FinalDegrade = 10U,      // 新(P6):BC 失效双条件触发,报 M7,M5 静默
};
```

### 4.2 状态语义

| 状态 | 含义 | M5 输出 | L4 行为 |
|---|---|---|---|
| Idle / CandidateEvaluating | 现状不变 | 无 avoidance plan | 跟踪 nominal route |
| **Committed** | NLP 收敛 + corridor 有效 | avoidance_plan(waypoints) | 跟踪 avoidance route |
| HeartbeatOnly | 无新 commit,heartbeat refresh | avoidance_plan(heartbeat) | 跟踪上一 corridor |
| **KeepLast / Stale / DegradedHold**(改) | **非 Committed** | **空 plan heartbeat + ASDR audit "keep_last_abolished"** | **释放 avoidance,回 nominal 或 hold last trajectory** |
| Released | 现状不变 | NORMAL 空 plan | 释放 avoidance |
| **BcMpcFollow**(改) | BC 接管 | 空 plan heartbeat(BC override 是主路径) | 跟踪 `ReactiveOverrideCmd` |
| **HandoverNeutral**(新) | BC 交还 Mid 过渡,等 2 周期双条件 | 空 plan heartbeat + BC override 继续(防真空) | BC override 仍有效,Mid 恢复中 |
| **FinalDegrade**(新) | BC 失效双条件触发 | 空 plan heartbeat + `SafetyConcernEvent` 给 M7 | M7 接管 MRM,M5 静默 |

### 4.3 状态转换规则(权威)

```
                     ┌─────────────────────────────────────────────┐
                     │                                             ▼
  Idle ─► CandidateEvaluating ─► Committed ◄───────────────── HandoverNeutral
                                │ ▲ │                             ▲ │
                  stale>45s/    │ │ │ nlp_ok+commit               │ │ 2 周期双条件
                  hdg>15°/      │ │ └► DegradedHold ◄─────────────┘ │ 满足
                  cpa>20%       │ │            ▲                    │
                               │ │            │ stale>45s/          │
                               │ │            │ hdg>15°/cpa>20%     │
                               │ │            │ OR nlp_failed       │
                               ▼ │            │                     │
                            BcMpcFollow ──────┼─────────────────────┘
                                ▲             │ consecutive≥3 OR
                                │             │ minalt_box_infeasible
                                │             │ OR speed_gap_infeasible
                                │             │
                                │             ▼
                                └──── BC 失效双条件 ──► FinalDegrade ──► M7 MRM
                                       (见 §4.4)
```

### 4.4 BcMpcFollow 进入/退出条件(权威)

**进入条件**(任一满足,Mid 侧 mid_mpc_node.cpp 计算):
- `consecutive_failures ≥ 3`(P3 已实装)
- `minalt_box_infeasible`(P5 已实装)
- `speed_gap_infeasible`(P5 已实装)
- **AND `bc_mpc_takeover_requested_` = true**(由 mid_mpc_node 在 OR 条件满足时 mark)

**退出条件(进入 HandoverNeutral)**:连续 **2 个 Mid replan 周期(60s × 2 = 120s)**同时满足:
- Mid NLP `status == Converged`(status=0)
- BC `predicted_short_horizon_cpa_m ≥ cpa_safe_m`(BC inner gate 无近距告警)

任一周期不满足,重置 hysteresis 计数器为 0,回 BcMpcFollow。

**HandoverNeutral → Committed**:HandoverNeutral 持续 1 个 Mid replan 周期(60s),期间 **BC override 仍生效(防真空),Mid 已恢复 Converged**;下一周期(即第 3 个连续双条件满足周期)双条件仍满足则转 `Committed` 并清除 BC takeover(`bc_mpc_takeover_requested_ = false`)。若 HandoverNeutral 期间任一双条件破坏,回 `BcMpcFollow` 并重置 hysteresis 计数器。

**实现**:hysteresis 计数器 `handover_hysteresis_count_` 在 BcMpcFollow 状态内累计(每周期双条件满足 ++,不满足重置 0);达到阈值 2 → 转 HandoverNeutral;HandoverNeutral 期间双条件满足 → 转 Committed,破坏 → 回 BcMpcFollow 重置计数器。

> **Q4 裁决边界澄清**:Q4 裁决"2 周期 × 60s + 双条件"指的是 **BcMpcFollow 内累计 2 个连续周期双条件**才能进入交还流程。HandoverNeutral 是这 2 周期累计达成后的过渡审计态(1 周期),所以完整交还时序是:2 周期(BcMpcFollow 内 hysteresis)+ 1 周期(HandoverNeutral)= 3 个 Mid replan 周期(180s)完成 BcMpcFollow → Committed。

### 4.5 FinalDegrade 进入条件(权威,BC 失效双条件)

**双条件同时满足**:
- **条件 A(BC 失效)**:BC-MPC 连续 ≥ N_override(N=5,**可调,见 §7.1**)个 validity tick(`kTickInterval_s=0.1s`,即 ≥0.5s)处于 `Status::Override` **但** `worst_case_cpa_m` 未改善(连续 N 次 `worst_case_cpa_m ≤ cpa_safe_m * override_cpa_multiplier`,即 BC override 未能把 CPA 推到安全域)
- **条件 B(Mid 未恢复)**:Mid NLP 连续 ≥ K_unrecovered(K=3,与 BC 触发阈值对齐)周期 `status != Converged`

任一不满足,不进 FinalDegrade(继续 BcMpcFollow + 重试)。

**FinalDegrade 后**:
- M5 `committed_route.state = FinalDegrade`
- 发 `SafetyConcernEvent`(`concern_type=CONCERN_BC_FINAL_DEGRADE=4`,severity=1.0,suggested_action="MRM")到 `/l3/safety/concern`(M7 已订阅)
- M5 不再发 `ReactiveOverrideCmd`(BC 停止 override)
- M5 不再发有效 avoidance_plan(空 plan heartbeat)
- **M7 接管 MRM,M5 静默等 M7 指令**(Q7 裁决)

**不可逆**:FinalDegrade 一旦进入,只能通过 M7 MRM 指令或场景重置退出(不自动恢复)。

---

## 5. 组件改动详图

### 5.1 committed_route(权威 FSM 升级)

**改动文件**:
- `include/m5_tactical_planner/committed_route/committed_route.hpp`
- `src/committed_route/committed_route.cpp`

**改动内容**:

1. **枚举加 2 状态**(§4.1):`HandoverNeutral = 9U, FinalDegrade = 10U`,`lifecycle_state_name()` 同步加 case。

2. **新增成员变量**:
```cpp
// P6 hysteresis:连续 N 周期双条件满足才交还
std::uint32_t handover_hysteresis_count_{0U};
constexpr std::uint32_t kHandoverHysteresisThreshold = 2U;

// P6 BC health 缓存(从 bc_mpc_node 订阅的 /l3/m5/bc_mpc/health)
std::uint32_t bc_override_no_improve_count_{0U};  // 条件 A 计数器
double bc_last_worst_case_cpa_m_{1.0e9};          // 上次 worst_case_cpa_m
bool bc_health_received_{false};                  // 是否收过 BC health
```

3. **新增方法**:
```cpp
// P6:接收 BC health metrics(从 mid_mpc_node 转发,bc_mpc_node 不直接访问 committed_route)
void notify_bc_mpc_health(std::uint32_t override_no_improve_count,
                          double worst_case_cpa_m,
                          bool override_active);

// P6:接收 Mid NLP Converged + BC predicted_cpa(用于 hysteresis 判定)
// 由 mid_mpc_node 每周期调用,传入 solver.status + bc inner gate 结果
void notify_handover_inputs(bool mid_converged, double bc_predicted_cpa_m);

// P6:评估是否进入 FinalDegrade(由 mid_mpc_node 每周期调用)
[[nodiscard]] bool should_enter_final_degrade() const;

// P6:进入 FinalDegrade 并生成 safety_concern_event
void enter_final_degrade();
```

4. **try_revise / should_enter_degraded_hold 改动**:
   - `BcMpcFollow` 状态下,每周期检查 hysteresis 计数器:双条件满足 → `++handover_hysteresis_count_`,达到 2 → 转 `Committed`(经 HandoverNeutral 1 周期审计);不满足 → 重置 0。
   - `BcMpcFollow` 状态下,每周期检查 `should_enter_final_degrade()`:双条件满足 → 转 `FinalDegrade` + 生成 `safety_concern_event = "bc_final_degrade"`。

5. **`publish_keep_last_` 重写**(Q2 彻底废除,改动在 `src/mid_mpc/mid_mpc_node.cpp`):
   - **所有非 Committed 状态**(KeepLast/Stale/DegradedHold/BcMpcFollow/HandoverNeutral/FinalDegrade)都发**空 plan heartbeat + ASDR audit**。
   - **不再 republish stale active_geometry**(彻底废除 keep-last republish)。
   - 删除 `src/mid_mpc/mid_mpc_node.cpp` 中 `publish_keep_last_` 的 active_geometry republish 代码块(现 L1467-1493 附近,读 `committed_route_manager_.current().active_geometry`),改为统一空 plan heartbeat 路径,仅 `status` 字段区分具体状态("DEGRADED"/"KEEP_LAST"/"STALE"/"BCMPC_FOLLOW"/"HANDOVER_NEUTRAL"/"FINAL_DEGRADE")。
   - `committed_route.cpp` 不需改 publish 逻辑(它只管状态机,publish 在 mid_mpc_node)。

### 5.2 bc_mpc_node(职责单一化 + health 上报)

**改动文件**:
- `include/m5_tactical_planner/bc_mpc/bc_mpc_node.hpp`
- `src/bc_mpc/bc_mpc_node.cpp`

**改动内容**:

1. **新增 publisher** `/l3/m5/bc_mpc/health`(新内部主题,QoS Reliable 10):
```cpp
rclcpp::Publisher<l3_msgs::msg::BcMpcHealth>::SharedPtr pub_health_;
```

2. **新增内部状态(BC 失效检测)**:
```cpp
std::uint32_t consecutive_override_no_improve_{0U};  // 条件 A 计数器
double last_worst_case_cpa_m_{1.0e9};
```

3. **`on_world_state_` 改动**:
   - 每次 Override 后,比较 `sol.worst_case_cpa_m` 与上次:
     - `worst_case_cpa_m ≤ cpa_safe * override_cpa_multiplier` 且未改善(差值 < ε)→ `++consecutive_override_no_improve_`
     - 改善 → 重置 0
   - Resolved 状态 → 重置 0
   - 每 tick publish `BcMpcHealth` 到 `/l3/m5/bc_mpc/health`

4. **validity 过期 escalate**(Q8 BC inner gate):
   - `on_validity_tick_` 中 validity 过期时,**不再 silent deactivate**。
   - 改为:保持 `is_bc_active_=true`,标记 `validity_expired_=true`,在下次 `on_world_state_` 重新评估;若仍 Override 则续发,若 Resolved 则正常退出。
   - ASDR 记录 `validity_expired_re_resolve` 事件。

### 5.3 mid_mpc_node(FSM 编排 + safety_concern publish)

**改动文件**:
- `include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp`
- `src/mid_mpc/mid_mpc_node.cpp`

**改动内容**:

1. **新增订阅** `/l3/m5/bc_mpc/health`(Reliable QoS 10,与 BC-MPC 同 callback group):
```cpp
rclcpp::Subscription<l3_msgs::msg::BcMpcHealth>::SharedPtr sub_bc_health_;
BcMpcHealth last_bc_health_;  // atomic cache 或 mutex 保护
```

2. **新增 publisher** `/l3/safety/concern`(M7 已订阅,QoS events Reliable 10):
```cpp
rclcpp::Publisher<l3_msgs::msg::SafetyConcernEvent>::SharedPtr pub_safety_concern_;
```

3. **每周期编排**(在 `on_timer_` 主循环中,现有 consecutive_failures publish 之后):
```cpp
// P6:转发 BC health 到 committed_route
committed_route_manager_.notify_bc_mpc_health(
    last_bc_health_.override_no_improve_count,
    last_bc_health_.worst_case_cpa_m,
    last_bc_health_.override_active);

// P6:转发 handover 双条件到 committed_route
const bool mid_converged = (sol.status == MidMpcSolution::Status::Converged);
const double bc_pred_cpa = last_bc_health_.predicted_short_horizon_cpa_m;
committed_route_manager_.notify_handover_inputs(mid_converged, bc_pred_cpa);

// P6:检查 FinalDegrade
if (committed_route_manager_.current().state == LifecycleState::BcMpcFollow
    && committed_route_manager_.should_enter_final_degrade()) {
  committed_route_manager_.enter_final_degrade();
  publish_safety_concern_final_degrade_();  // 发 SafetyConcernEvent 给 M7
}
```

4. **`publish_safety_concern_final_degrade_` 新方法**:
```cpp
void MidMpcNode::publish_safety_concern_final_degrade_() {
  l3_msgs::msg::SafetyConcernEvent evt;
  evt.stamp = this->get_clock()->now();
  evt.concern_type = l3_msgs::msg::SafetyConcernEvent::CONCERN_BC_FINAL_DEGRADE;
  evt.severity = 1.0F;
  evt.suggested_action = "MRM";
  evt.anchor_hdg = 0.0F;  // 不适用
  pub_safety_concern_->publish(evt);
}
```

### 5.4 launch 激活(Q5)

**改动文件**:`launch/m5_mid_mpc.launch.py`

```python
return LaunchDescription([
    Node(
        package="m5_tactical_planner",
        executable="m5_mid_mpc_node",
        name="mid_mpc",
        ...
    ),
    Node(  # P6 新增:BC-MPC 激活
        package="m5_tactical_planner",
        executable="m5_bc_mpc_node",
        name="bc_mpc",
        output="screen",
        parameters=[os.path.join(config_dir, "m5_params.yaml")],
    ),
])
```

### 5.5 配置参数(Q5 同 yaml)

**改动文件**:`config/m5_params.yaml`

```yaml
bc_mpc:
  ros__parameters:
    cpa_safe_m: 1852.0
    horizon_s: 60.0        # BC 短 horizon(与 Mid 1200s 区分)
    dt_s: 0.5              # BC 细粒度 dt
    override_cpa_multiplier: 1.0
    urgency_threshold: 0.80
    # P6 FINAL_DEGRADE 触发参数
    override_no_improve_threshold: 5   # 条件 A:连续 Override 但 CPA 不改善次数
    mid_unrecovered_threshold: 3       # 条件 B:Mid NLP 未恢复周期
    handover_hysteresis_cycles: 2      # Q4:连续 2 周期 × 60s
```

---

## 6. ROS2 主题与消息改动

### 6.1 新增消息 `l3_msgs/msg/BcMpcHealth.msg`

```
# P6 新增:BC-MPC health metrics,供 committed_route 判定 FINAL_DEGRADE
# Frequency: 与 WorldState 同频(BC-MPC 事件驱动)
builtin_interfaces/Time stamp

bool override_active                      # BC 是否处于 Override 状态
float32 worst_case_cpa_m                  # 本次 solve 的 worst-case CPA
float32 predicted_short_horizon_cpa_m     # BC inner gate 输入(短 horizon 预测 CPA)
uint32 override_no_improve_count          # 条件 A 计数器(连续 Override 但 CPA 不改善)
uint32 consecutive_failures               # BC-MPC solver 内部失败计数器
float32 confidence                        # BC-MPC confidence
string rationale                          # SAT-2 摘要
```

### 6.2 扩展 `l3_msgs/msg/SafetyConcernEvent.msg`

```diff
 uint8 CONCERN_IVP_INFEASIBLE=1
 uint8 CONCERN_ODD_DEGRADED=2
 uint8 CONCERN_ETA_INFEASIBLE=3
+uint8 CONCERN_BC_FINAL_DEGRADE=4   # P6:BC-MPC FinalDegrade 报 M7

 uint8 concern_type
 float32 anchor_hdg
 string suggested_action
 float32 severity
 builtin_interfaces/Time stamp
```

### 6.3 主题清单(P6 后)

| 主题 | 方向 | 频率 | QoS | 说明 |
|---|---|---|---|---|
| `/l3/m5/mid_mpc/consecutive_failures` | Mid→BC | 1Hz(Mid replan) | Reliable 10 | 现状不变 |
| `/l3/m5/avoidance_plan` | Mid→L4/M7 | 事件+heartbeat | Reliable 10 | P6:非 Committed 状态发空 plan |
| `/l3/m5/reactive_override_cmd` | BC→L4/M7 | 事件 | Reliable 10 | 现状不变,L4 已订阅 |
| **`/l3/m5/bc_mpc/health`**(新) | BC→Mid | 与 WorldState 同频 | Reliable 10 | P6 新增 |
| **`/l3/safety/concern`**(新发) | Mid→M7 | 事件(FinalDegrade 时) | Reliable 10 | M7 已订阅,M5 侧新发 |

---

## 7. 关键参数与阈值

### 7.1 FINAL_DEGRADE 双条件阈值(可调)

| 参数 | 默认值 | 含义 | 来源 |
|---|---|---|---|
| `override_no_improve_threshold` (条件 A) | **5** | BC 连续 Override 但 CPA 不改善次数 | brainstorming 推荐值(可 SIL 调) |
| `mid_unrecovered_threshold` (条件 B) | **3** | Mid NLP 未恢复周期 | 与 BC 触发阈值(consecutive≥3)对齐 |
| "CPA 不改善" 判定 ε | **1.0 m** | `last_worst_case_cpa_m - current ≤ ε` 视为不改善 | 工程经验值(可调) |

**SIL 校准**:T6 SIL 验证中实测调整,默认值用于首次集成。

### 7.2 Handover hysteresis

| 参数 | 默认值 | 含义 |
|---|---|---|
| `handover_hysteresis_cycles` | **2** | Q4 裁决,连续 2 个 Mid replan 周期(60s × 2) |
| Mid replan 周期 | **60s** | P4 已落地(solve_timer) |

### 7.3 BC-MPC inner gate(现状不变)

| 参数 | 默认值 | 含义 |
|---|---|---|
| `override_cpa_multiplier` | **1.0** | CPA 阈值 = `cpa_safe_m * multiplier` |
| `urgency_threshold` | **0.80** | 三档分支切换 |

---

## 8. 验收门(8 条,P6 自闭环)

| # | 门 | 验证方式 |
|---|---|---|
| **G1** | bc_mpc_node 在 SIL 启动 | `m5_mid_mpc.launch.py` 加载后 `ros2 node list` 含 `/bc_mpc` |
| **G2** | mid→BC consecutive_failures 主题连通 | SIL 注入近距场景,`ros2 topic echo /l3/m5/mid_mpc/consecutive_failures` 有非零值 |
| **G3** | BC 发 ReactiveOverrideCmd 到 L4 | SIL 注入近距场景,`ros2 topic echo /l3/m5/reactive_override_cmd` 有消息,FCB Sim 收到(ASDR 记录) |
| **G4** | 11 状态机单测覆盖 | `test_committed_route.cpp` 加 HandoverNeutral/FinalDegrade 转换测试,所有状态转换边覆盖 |
| **G5** | keep-last 彻底废除 | `test_mid_mpc_node.cpp` 验证非 Committed 状态发空 plan + ASDR audit "keep_last_abolished" |
| **G6** | FINAL_DEGRADE 触发 + 报 M7 | unit test:mock BC health(条件 A+B 满足)→ committed_route 进 FinalDegrade + 发 SafetyConcernEvent |
| **G7** | Handover hysteresis 2 周期 | unit test:mock 连续 2 周期双条件 → BcMpcFollow → Committed;中间断 → 重置 |
| **G8** | SIL 端到端 | 近距场景(target_y<2000m,P5 已确认 Mid 不收敛)→ Mid 失败 → BC 接管 → ReactiveOverrideCmd → FCB Sim 收到 → 交还/MRM |

**额外自闭环要求**(与 P0-P5 一致):
- 所有 touched 代码 unit test 绿
- `test_bc_mpc_node_handover.cpp` 现有测试不回归(2 ASDR signature 测试)
- Mandatory codex adversarial review(Task 7)

---

## 9. 测试矩阵

### 9.1 Unit tests

| 测试文件 | 新增测试 | 覆盖门 |
|---|---|---|
| `test_committed_route.cpp` | `HandoverHysteresis_TwoCycleThenCommit` | G7 |
| `test_committed_route.cpp` | `HandoverHysteresis_ResetOnBreak` | G7 |
| `test_committed_route.cpp` | `FinalDegrade_BCFailedAndMidUnrecovered` | G6 |
| `test_committed_route.cpp` | `FinalDegrade_NotTriggeredWhenOnlyBCFailed` | G6 |
| `test_committed_route.cpp` | `KeepLastAbolished_AllNonCommittedEmitEmptyPlan` | G5 |
| `test_committed_route.cpp` | `StateTransitions_All11StatesCovered` | G4 |
| `test_mid_mpc_solver.cpp` | `SafetyConcernEvent_PublishedOnFinalDegrade` | G6 |
| `test_bc_mpc_node_handover.cpp` | `HealthMetrics_PublishedOnOverride` | G3(部分) |
| `test_bc_mpc_node_handover.cpp` | `OverrideNoImprove_CounterIncrements` | G6(部分) |

### 9.2 SIL 端到端测试(Q9)

**场景**:近距 head-on,target_y=1500m(P5 已确认 Mid-MPC ample-time 边界 ~2000m,1500m 不收敛)

**验证序列**:
1. SIL 启动(P6 G1),`ros2 node list` 含 `/bc_mpc`
2. 注入 target at 1500m head-on
3. Mid-MPC consecutive_failures 累积到 3(G2)
4. `committed_route.state → BcMpcFollow`
5. BC-MPC 发 ReactiveOverrideCmd(G3),FCB Sim ASDR 记录
6. 场景 A(BC 成功推开 CPA):CPA 改善 → 条件 A 不满足 → 持续 BcMpcFollow → Mid 恢复 Converged → hysteresis 2 周期 → Committed(G7)
7. 场景 B(BC 失效):mock BC CPA 不改善连续 5 次 + Mid 持续不收敛 → FinalDegrade → SafetyConcernEvent → M7 收到(G6, G8)

**证据输出**:
- `runs/p6_sil_e2e_<scenario>_<timestamp>.json`(主题捕获 + 状态转换序列)
- ASDR 记录全链路

---

## 10. 排除项(P6 不做)

| 项 | 理由 |
|---|---|
| BC-MPC 三档分支几何改动 | P2-B-01 已落地,P6 不碰 detector 几何 |
| COLREGs 规则集成 | M6 已落地(constraint_compiler),P6 不碰 |
| BC-MPC 速度优化 | bc_mpc_solver.cpp rot_cmd=0/optimal_speed=输入 是 Phase E1 设计,P6 不改(留 P7) |
| GNC `ActiveRouteManagerNode` 接线 | L4 侧 override consumer 已存在(fcb_simulator + active_route_manager),P6 验证可用,不改 GNC 代码 |
| TimedTrajectory 输出 | 契约兑现项(roadmap §5.5),归 GNC-P1,不属 P6 |
| preflight 删硬编码 | 契约兑现项(roadmap §5.5),归 GNC-P1,不属 P6 |
| 四状态机扩展为更多状态 | VR-01b 裁决 4 状态,P6 严格按 4 状态 + 现有 Idle/Candidate/Heartbeat/Released 扩展到 11 |
| A+ 不确定性 / 意图建模 | P7 范围 |

---

## 11. 风险与缓解

| 风险 | 等级 | 缓解 |
|---|---|---|
| BC-MPC 从未在 SIL 运行,激活后可能暴露未发现 bug | 高 | T1 先 SIL baseline 跑当前行为,记录 evidence 再改 FSM |
| 11 状态机转换边覆盖不全,引入死锁 | 中 | G4 单测强制覆盖所有转换边;T7 codex review |
| FINAL_DEGRADE 不可逆,误触发导致 MRM 误启动 | 高 | 双条件设计(BC 失效 AND Mid 未恢复);G6 单测验证"仅 BC 失效不触发";阈值可 SIL 调 |
| Handover hysteresis 2 周期太短,BC-MPC 恒向恒速风险 | 中 | BC inner gate(urgency/CPA)作为二次保护;SIL 实测调整 |
| keep-last 废除后 L4 失去 corridor 跟踪,行为退化 | 中 | L4 fcb_simulator 已有 validity 过期回 trajectory 逻辑;空 plan heartbeat 仍发,GNC active_route_manager 见空 plan 回 nominal |
| BC health 主题新增增加 M5 内部耦合 | 低 | 单向数据流(BC→Mid→committed_route),无反向依赖;BC 不依赖 committed_route 状态 |
| SafetyConcernEvent 枚举扩展破坏现有消费者 | 低 | 现有 3 值不变,只加 CONCERN_BC_FINAL_DEGRADE=4;M7 on_safety_concern switch 加 default 分支 |

---

## 12. 实施顺序(P6 plan 概要)

> 详细 plan 见 `plans/2026-07-18-m5-p6-bcmpc-four-state-handover.md`

1. **T1**: SIL baseline(先跑当前行为,建立 evidence)
2. **T2**: launch 激活 + BcMpcHealth msg + bc_mpc_node health 上报
3. **T3**: SafetyConcernEvent 枚举扩展 + committed_route 11 状态机 + hysteresis + FinalDegrade
4. **T4**: keep-last 彻底废除(publish_keep_last_ 重写)
5. **T5**: mid_mpc_node 编排(BC health 转发 + handover inputs + FinalDegrade publish)
6. **T6**: SIL 端到端验证(近距场景完整链)
7. **T7**: codex adversarial review + regression + 验收门 8 条

---

## 13. 文档交叉引用

- **roadmap §P6**: `specs/2026-07-17-m5-mpc-p0-p7-roadmap.md` §4 P6 + §3.2 状态表
- **VR-01b/VR-08 裁决依据**: `specs/2026-07-16-m5-mpc-colav-solution-pack.md` + `design-logs/2026-07-16-m5-mpc-colav-design-log.md`
- **L3→L4 契约**: `specs/2026-07-17-l3-l4-gnc-contract-solution-pack.md`(ReactiveOverrideCmd 契约)
- **P5 ample-time 边界**: `specs/2026-07-18-m5-p5-anti-chatter-ample-time-design.md`(收敛边界 ~2000m,BC-MPC 负责 <2000m)
- **P5 收敛边界 evidence**: `specs/2026-07-18-m5-p5-acados-convergence-design.md`
