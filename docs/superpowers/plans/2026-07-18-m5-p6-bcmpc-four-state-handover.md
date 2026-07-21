# M5 P6 实施计划 — BC-MPC 激活 + 四状态交接机 + 回退链

> **Spec**: `docs/superpowers/specs/2026-07-18-m5-p6-bcmpc-four-state-handover-design.md`
> **基线 HEAD**: 待 P5 完成提交后取(执行时为当前 worktree HEAD)
> **工作树**: `.worktrees/m5-design-grounding`(分支 `codex/m5-design-grounding`)
> **范围**: 全量(P6-a + P6-b 合并),先 SIL baseline 后改 FSM
> **验收门**: spec §8 共 8 条
> **强制**: 每个子任务 TDD(RED→GREEN→REFACTOR),Task 7 强制 codex 对抗评审

---

## Task 依赖图

```
T1 (SIL baseline) ──► T2 (launch + BcMpcHealth msg + bc_mpc_node health)
                   │
                   ├─► T3 (SafetyConcernEvent 枚举 + committed_route 11 状态机)
                   │      │
                   │      └─► T4 (keep-last 废除, publish_keep_last_ 重写)
                   │             │
                   │             └─► T5 (mid_mpc_node 编排: BC health 转发 + handover + FinalDegrade publish)
                   │                    │
                   │                    └─► T6 (SIL 端到端)
                   │                           │
                   │                           └─► T7 (codex review + regression + 8 验收门)
                   │
                   └─► (T1 仅 evidence,不改代码)
```

**顺序**: T1 → T2 → T3 → T4 → T5 → T6 → T7(T1 是 evidence-only baseline,可与 T2 并行准备但必须先完成)。

---

## Task 1: SIL Baseline Evidence(不改代码)

**目标**: BC-MPC 从未在 SIL 运行过。T1 先激活 BC-MPC(临时加 launch,本 task 内验证后**可能撤回**或留作 T2 基础),跑当前行为的 evidence baseline,记录:
- 当前 `consecutive≥3` 阈值在近距场景何时触发
- BC 是否真的发 `ReactiveOverrideCmd` 到 `/l3/m5/reactive_override_cmd`
- FCB Simulator 是否收到 override(`fcb_simulator_node.cpp:46-47` 已订阅)
- 当前 `publish_keep_last_` 在 BcMpcFollow 状态的实际行为
- BC validity 过期如何 escalate(预期:silent deactivate)

### Step 1.1 — 临时激活 BC-MPC(本 task 范围内)

**RED**: 无 launch 激活,BC-MPC 在 SIL 不运行。
**GREEN**: 编辑 `launch/m5_mid_mpc.launch.py` 加 `m5_bc_mpc_node`(本 task 临时改动,T2 时正式落地)。

```python
# launch/m5_mid_mpc.launch.py(临时,与 T2 正式版一致)
Node(
    package="m5_tactical_planner",
    executable="m5_bc_mpc_node",
    name="bc_mpc",
    output="screen",
    parameters=[os.path.join(config_dir, "m5_params.yaml")],
),
```

### Step 1.2 — 配置 BC-MPC 参数

**RED**: `m5_params.yaml` 无 `bc_mpc` 节。
**GREEN**: 加 `bc_mpc.ros__parameters` 块(spec §5.5),先填默认值(`cpa_safe_m: 1852.0` 等)。

### Step 1.3 — 跑 SIL 近距场景,记录 evidence

**场景**: head-on,target_y=1500m(P5 已确认 Mid ample-time 边界 ~2000m,1500m 不收敛)。

**证据采集**:
- `ros2 node list` 含 `/bc_mpc`
- `ros2 topic echo /l3/m5/mid_mpc/consecutive_failures` → 累积到 3
- `ros2 topic echo /l3/m5/reactive_override_cmd` → 有消息
- FCB Sim ASDR 记录 override 接收
- `committed_route.state` 转换序列(通过 ASDR `decision_json` 的 `lifecycle_state` 字段)
- BC validity 过期行为(观察 `is_bc_active_` 翻转,spdlog warn)

**输出**:`runs/p6_t1_sil_baseline_<timestamp>.json`(主题捕获 + 状态转换序列)。

### Step 1.4 — T1 完成判据(DONE 条件)

- [ ] BC-MPC 在 SIL 启动(`ros2 node list` 含 `/bc_mpc`)
- [ ] 近距场景下 mid→BC consecutive_failures 主题有非零值
- [ ] BC 发 ReactiveOverrideCmd,FCB Sim ASDR 记录
- [ ] evidence JSON 写入 `runs/`
- [ ] 记录 3 个关键观察:① 当前阈值触发时机;② BC override 是否到 L4;③ validity 过期行为
- [ ] **不改任何生产代码逻辑**(launch + yaml 改动留作 T2 基础,本 task 不撤回)

**Status**: T1 是 evidence-only,但 launch + yaml 改动直接作为 T2 输入(不撤回)。

---

## Task 2: launch 正式激活 + BcMpcHealth msg + bc_mpc_node health 上报

**目标**: 把 T1 的临时 launch 改为正式版;新建 `BcMpcHealth.msg`;bc_mpc_node 加 health metrics 上报(条件 A 计数器 + worst_case_cpa_m);validity 过期 escalate。

### Step 2.1 — 新建 `l3_msgs/msg/BcMpcHealth.msg`

**RED**: 无此消息。
**GREEN**: 创建(spec §6.1):
```
builtin_interfaces/Time stamp
bool override_active
float32 worst_case_cpa_m
float32 predicted_short_horizon_cpa_m
uint32 override_no_improve_count
uint32 consecutive_failures
float32 confidence
string rationale
```

更新 `l3_msgs/CMakeLists.txt` 加 `BcMpcHealth.msg` 到 `rosidl_generate_interfaces`。

### Step 2.2 — bc_mpc_node 加 health publisher + 条件 A 计数器

**RED**: `test_bc_mpc_node_handover.cpp` 加测试 `HealthMetrics_PublishedOnOverride`(mock Override → 期望 `/l3/m5/bc_mpc/health` 有消息,`override_no_improve_count` 字段正确)。
**GREEN**:

`include/.../bc_mpc_node.hpp` 加:
```cpp
rclcpp::Publisher<l3_msgs::msg::BcMpcHealth>::SharedPtr pub_health_;
std::uint32_t consecutive_override_no_improve_{0U};
double last_worst_case_cpa_m_{1.0e9};
constexpr static double kCpaImproveEpsilon_m = 1.0;  // spec §7.1 ε
```

`src/bc_mpc/bc_mpc_node.cpp` `on_world_state_` 改:
```cpp
if (sol.status == BcMpcSolution::Status::Override) {
  is_bc_active_ = true;
  remaining_validity_s_ = sol.validity_s;
  // P6 条件 A 计数器:CPA 不改善(差值 < ε)
  const double improve = sol.worst_case_cpa_m - last_worst_case_cpa_m_;
  const double cpa_threshold = inp.cpa_safe_m * formulation_.config().override_cpa_multiplier;
  if (sol.worst_case_cpa_m <= cpa_threshold && improve < kCpaImproveEpsilon_m) {
    ++consecutive_override_no_improve_;
  } else {
    consecutive_override_no_improve_ = 0U;
  }
  last_worst_case_cpa_m_ = sol.worst_case_cpa_m;
  publish_override_(sol);
} else if (is_bc_active_ && sol.status == BcMpcSolution::Status::Resolved) {
  is_bc_active_ = false;
  remaining_validity_s_ = 0.0;
  consecutive_override_no_improve_ = 0U;  // 重置
  spdlog::info("[M5][BcMPC] CPA resolved; handing back to Mid-MPC");
}
publish_health_(sol);  // 每 tick publish health
```

新增 `publish_health_` 方法:打包 `BcMpcHealth`(含 `consecutive_override_no_improve_`、`worst_case_cpa_m`、`predicted_short_horizon_cpa_m`、`consecutive_failures_`)publish 到 `/l3/m5/bc_mpc/health`(Reliable QoS 10)。

### Step 2.3 — validity 过期 escalate

**RED**: 测试 `ValidityExpired_NoSilentDeactivate`(mock validity 过期 → 期望 ASDR 记录 `validity_expired_re_resolve`,而非 silent `is_bc_active_=false`)。
**GREEN**: `on_validity_tick_` 改:
```cpp
if (remaining_validity_s_ <= 0.0) {
  // P6:不 silent deactivate,标记过期,等下次 on_world_state_ 重评
  validity_expired_ = true;
  spdlog::warn("[M5][BcMPC] validity expired; will re-resolve on next WorldState");
  // 不再 is_bc_active_ = false
  return;
}
```

加 ASDR 记录 `validity_expired_re_resolve` 事件。

### Step 2.4 — launch 正式版

**GREEN**: `launch/m5_mid_mpc.launch.py` 已在 T1 加 bc_mpc_node,T2 正式化(加注释、参数引用 yaml)。

### Step 2.5 — T2 完成判据

- [ ] `BcMpcHealth.msg` 创建 + CMakeLists 更新
- [ ] `test_bc_mpc_node_handover.cpp` 新测试 `HealthMetrics_PublishedOnOverride` 绿
- [ ] `test_bc_mpc_node_handover.cpp` 新测试 `OverrideNoImprove_CounterIncrements` 绿
- [ ] `test_bc_mpc_node_handover.cpp` 新测试 `ValidityExpired_NoSilentDeactivate` 绿
- [ ] 现有 2 个 ASDR signature 测试不回归
- [ ] colcon test 全绿

---

## Task 3: SafetyConcernEvent 枚举扩展 + committed_route 11 状态机

**目标**: 扩展 `SafetyConcernEvent` 枚举;`committed_route` 加 `HandoverNeutral`/`FinalDegrade` 2 状态;实装 hysteresis 计数器 + FinalDegrade 双条件判定方法。

### Step 3.1 — SafetyConcernEvent 枚举扩展

**RED**: 无 `CONCERN_BC_FINAL_DEGRADE`。
**GREEN**: `l3_msgs/msg/SafetyConcernEvent.msg` 加 `uint8 CONCERN_BC_FINAL_DEGRADE=4`(spec §6.2)。

### Step 3.2 — committed_route 枚举 + lifecycle_state_name 扩展

**RED**: `test_committed_route.cpp` 加测试 `StateTransitions_All11StatesCovered`(枚举 11 个状态都可通过 lifecycle_state_name 返回正确字符串)。
**GREEN**: `include/.../committed_route.hpp`:
```cpp
enum class LifecycleState : std::uint8_t {
  ...
  BcMpcFollow = 8U,
  HandoverNeutral = 9U,   // P6 新
  FinalDegrade = 10U,     // P6 新
};
```
`lifecycle_state_name()` 加 2 个 case。

### Step 3.3 — hysteresis 计数器 + handover inputs 方法

**RED**: `test_committed_route.cpp` 加测试:
- `HandoverHysteresis_TwoCycleThenCommit`:BcMpcFollow 状态,连续 2 周期双条件 → 转 HandoverNeutral → 第 3 周期 → Committed
- `HandoverHysteresis_ResetOnBreak`:BcMpcFollow,1 周期双条件 + 第 2 周期破坏 → 计数器重置 0,保持 BcMpcFollow

**GREEN**: `committed_route.hpp` 加成员 + 方法:
```cpp
std::uint32_t handover_hysteresis_count_{0U};
constexpr static std::uint32_t kHandoverHysteresisThreshold = 2U;

std::uint32_t bc_override_no_improve_count_{0U};
double bc_last_worst_case_cpa_m_{1.0e9};
bool bc_health_received_{false};
std::uint32_t mid_unrecovered_count_{0U};
bool last_mid_converged_{true};

void notify_bc_mpc_health(std::uint32_t override_no_improve_count,
                          double worst_case_cpa_m, bool override_active);
void notify_handover_inputs(bool mid_converged, double bc_predicted_cpa_m);
[[nodiscard]] bool should_enter_final_degrade() const;
void enter_final_degrade();
```

`committed_route.cpp` 实装:
- `notify_bc_mpc_health`:更新 `bc_override_no_improve_count_` 等
- `notify_handover_inputs`:更新 `last_mid_converged_`;在 BcMpcFollow 状态下评估 hysteresis(双条件满足 ++,达到 2 转 HandoverNeutral,破坏重置 0);HandoverNeutral 下评估(满足转 Committed + 清 bc_mpc_takeover_requested_,破坏回 BcMpcFollow 重置)
- `should_enter_final_degrade`:`bc_override_no_improve_count_ >= 5 && mid_unrecovered_count_ >= 3`(spec §7.1)
- `enter_final_degrade`:`current_.state = FinalDegrade; current_.safety_concern_event = "bc_final_degrade"`

### Step 3.4 — FinalDegrade 单条件不触发

**RED**: `test_committed_route.cpp` 加测试:
- `FinalDegrade_BCFailedAndMidUnrecovered`:条件 A(5)+ 条件 B(3)→ should_enter_final_degrade 返回 true
- `FinalDegrade_NotTriggeredWhenOnlyBCFailed`:仅条件 A(5),条件 B(0)→ 返回 false
- `FinalDegrade_NotTriggeredWhenOnlyMidUnrecovered`:仅条件 B(3),条件 A(0)→ 返回 false

**GREEN**: `should_enter_final_degrade` 实装 `&&`(双条件)。

### Step 3.5 — T3 完成判据

- [ ] `SafetyConcernEvent.msg` 加 CONCERN_BC_FINAL_DEGRADE=4
- [ ] `committed_route.hpp/cpp` 加 2 状态 + hysteresis + FinalDegrade 方法
- [ ] `test_committed_route.cpp` 6 个新测试全绿(2 hysteresis + 3 FinalDegrade + 1 全状态覆盖)
- [ ] 现有 committed_route 测试不回归
- [ ] colcon test 全绿

---

## Task 4: keep-last 彻底废除(publish_keep_last_ 重写)

**目标**: `mid_mpc_node.cpp` 的 `publish_keep_last_` 不再 republish stale active_geometry,所有非 Committed 状态发空 plan heartbeat + ASDR audit。

### Step 4.1 — 重写 publish_keep_last_

**RED**: `test_mid_mpc_solver.cpp`(或新 `test_mid_mpc_node_publish.cpp`)加测试:
- `KeepLastAbolished_AllNonCommittedEmitEmptyPlan`:遍历 KeepLast/Stale/DegradedHold/BcMpcFollow/HandoverNeutral/FinalDegrade 6 状态 → 期望发布的 AvoidancePlan `waypoints.empty()` + ASDR audit `decision_type` 含 `keep_last_abolished`
- `KeepLastAbolished_NoActiveGeometryRepublish`:DegradedHold 状态下,即使 `committed.active_geometry` 非空 → 发布的 plan `waypoints` 仍空

**GREEN**: `src/mid_mpc/mid_mpc_node.cpp` `publish_keep_last_` 重写:
```cpp
void MidMpcNode::publish_keep_last_(rclcpp::Time now, const std::string& reason) {
  // P6 Q2:彻底废除 keep-last republish。所有非 Committed 状态发空 plan heartbeat。
  l3_msgs::msg::AvoidancePlan plan;
  plan.schema_version = 116;
  plan.stamp = now;
  // 根据当前 lifecycle state 设置 status
  switch (committed_route_manager_.current().state) {
    case LifecycleState::BcMpcFollow:
      plan.status = "BCMPC_FOLLOW"; break;
    case LifecycleState::HandoverNeutral:
      plan.status = "HANDOVER_NEUTRAL"; break;
    case LifecycleState::FinalDegrade:
      plan.status = "FINAL_DEGRADE"; break;
    case LifecycleState::DegradedHold:
      plan.status = "DEGRADED"; break;
    case LifecycleState::KeepLast:
      plan.status = "KEEP_LAST"; break;
    case LifecycleState::Stale:
      plan.status = "STALE"; break;
    default:
      plan.status = "DEGRADED"; break;
  }
  plan.confidence = 0.0F;
  plan.command_source = "m5_keep_last_abolished";
  plan.commit_branch = l3_msgs::msg::AvoidancePlan::COMMIT_BRANCH_KEEP_LAST;
  plan.nlp_solver_status = l3_msgs::msg::AvoidancePlan::NLP_NONCONVERGED;
  plan.nlp_tail_gate_failed = true;
  plan.rationale = std::string{"keep_last_abolished ("} + reason + ")";
  plan.valid_until = now + rclcpp::Duration::from_seconds(kAvoidancePlanTtl_s);
  // waypoints 留空 —— 不再 republish active_geometry

  emit_empty_plan_handoff_asdr_(now, reason, plan.plan_id, plan.status);

  // P6 ASDR audit:keep_last_abolished 标记
  l3_msgs::msg::ASDRRecord audit;
  audit.stamp = now;
  audit.source_module = "M5_MID_MPC";
  audit.decision_type = "keep_last_abolished";
  audit.decision_json = std::string{"{\"state\":\""} + plan.status + "\"}";
  // signature ...
  pub_asdr_->publish(audit);

  publish_avoidance_plan_(plan, std::string{"keep_last_abolished:"} + reason);
}
```

删除原 L1455-1493 active_geometry republish 代码块。

### Step 4.2 — 现有 BcMpcFollow 特判合并

**GREEN**: 原 L1420-1453 的 BcMpcFollow 特判合并进新统一路径(状态分支已覆盖)。

### Step 4.3 — T4 完成判据

- [ ] `publish_keep_last_` 重写,删除 active_geometry republish
- [ ] 新测试 `KeepLastAbolished_AllNonCommittedEmitEmptyPlan` 绿
- [ ] 新测试 `KeepLastAbolished_NoActiveGeometryRepublish` 绿
- [ ] 现有 avoidance_plan 测试不回归
- [ ] colcon test 全绿

---

## Task 5: mid_mpc_node 编排(BC health 转发 + handover inputs + FinalDegrade publish)

**目标**: mid_mpc_node 订阅 `/l3/m5/bc_mpc/health`,每周期转发给 committed_route;评估 handover inputs;FinalDegrade 时发 `SafetyConcernEvent` 给 M7。

### Step 5.1 — 订阅 BC health + publish SafetyConcernEvent

**RED**: `test_mid_mpc_solver.cpp` 加测试 `SafetyConcernEvent_PublishedOnFinalDegrade`(mock committed_route 进 FinalDegrade → 期望 `/l3/safety/concern` 有消息,`concern_type=4`)。
**GREEN**: `mid_mpc_node.hpp` 加:
```cpp
#include "l3_msgs/msg/bc_mpc_health.hpp"
#include "l3_msgs/msg/safety_concern_event.hpp"

rclcpp::Subscription<l3_msgs::msg::BcMpcHealth>::SharedPtr sub_bc_health_;
rclcpp::Publisher<l3_msgs::msg::SafetyConcernEvent>::SharedPtr pub_safety_concern_;
std::mutex bc_health_mutex_;
l3_msgs::msg::BcMpcHealth last_bc_health_;
```

构造器加:
```cpp
sub_bc_health_ = create_subscription<l3_msgs::msg::BcMpcHealth>(
    "/l3/m5/bc_mpc/health", rclcpp::QoS(10).reliable(),
    [this](const l3_msgs::msg::BcMpcHealth::SharedPtr msg) {
      std::lock_guard<std::mutex> lk(bc_health_mutex_);
      last_bc_health_ = *msg;
    });
pub_safety_concern_ = create_publisher<l3_msgs::msg::SafetyConcernEvent>(
    "/l3/safety/concern", rclcpp::QoS(10).reliable());
```

### Step 5.2 — 每周期编排

**GREEN**: `on_timer_` 主循环,在现有 consecutive_failures publish 之后(spec §5.3):
```cpp
// P6:转发 BC health 到 committed_route
{
  std::lock_guard<std::mutex> lk(bc_health_mutex_);
  committed_route_manager_.notify_bc_mpc_health(
      last_bc_health_.override_no_improve_count,
      last_bc_health_.worst_case_cpa_m,
      last_bc_health_.override_active);
}

// P6:转发 handover 双条件
const bool mid_converged = (sol.status == MidMpcSolution::Status::Converged);
double bc_pred_cpa = 1.0e9;
{
  std::lock_guard<std::mutex> lk(bc_health_mutex_);
  bc_pred_cpa = last_bc_health_.predicted_short_horizon_cpa_m;
}
committed_route_manager_.notify_handover_inputs(mid_converged, bc_pred_cpa);

// P6:检查 FinalDegrade
if (committed_route_manager_.current().state == LifecycleState::BcMpcFollow
    && committed_route_manager_.should_enter_final_degrade()) {
  committed_route_manager_.enter_final_degrade();
  publish_safety_concern_final_degrade_();
}
```

### Step 5.3 — publish_safety_concern_final_degrade_

**GREEN**:
```cpp
void MidMpcNode::publish_safety_concern_final_degrade_() {
  l3_msgs::msg::SafetyConcernEvent evt;
  evt.stamp = this->get_clock()->now();
  evt.concern_type = l3_msgs::msg::SafetyConcernEvent::CONCERN_BC_FINAL_DEGRADE;
  evt.severity = 1.0F;
  evt.suggested_action = "MRM";
  evt.anchor_hdg = 0.0F;
  pub_safety_concern_->publish(evt);
  spdlog::critical("[M5][MidMPC] FINAL_DEGRADE → SafetyConcernEvent published (BC failed + Mid unrecovered)");
}
```

### Step 5.4 — T5 完成判据

- [ ] `mid_mpc_node` 订阅 `/l3/m5/bc_mpc/health` + publish `/l3/safety/concern`
- [ ] 新测试 `SafetyConcernEvent_PublishedOnFinalDegrade` 绿
- [ ] 新测试 `BcHealthForwarded_ToCommittedRoute` 绿(mock BC health → committed_route 收到)
- [ ] colcon test 全绿

---

## Task 6: SIL 端到端验证(8 验收门)

**目标**: 近距场景完整链验证(spec §9.2)。

### Step 6.1 — 启动 SIL(P6 G1)

```bash
source scripts/a4000-env.sh
npm run sys:start
./scripts/a4000-acceptance.sh  # 先确认基础栈绿
ros2 node list | grep -E "mid_mpc|bc_mpc"  # G1:含 /bc_mpc
```

### Step 6.2 — 近距场景注入(P6 G2, G3)

场景:head-on,target_y=1500m。

```bash
# 触发场景(scenario runtime 注入)
ros2 topic echo /l3/m5/mid_mpc/consecutive_failures  # G2:累积到 3
ros2 topic echo /l3/m5/reactive_override_cmd         # G3:有消息
ros2 topic echo /l3/m5/bc_mpc/health                 # 新主题有消息
```

验证 FCB Sim ASDR 记录 override 接收。

### Step 6.3 — 状态转换序列验证(P6 G4-G7)

捕获 ASDR 全链路,验证:
- `committed_route.state` 转换序列包含 BcMpcFollow
- BC override 后 CPA 改善 / 不改善两种 sub-scenario:
  - **场景 A(BC 成功)**:BC override 推开 CPA → 条件 A 不满足 → Mid 恢复 → hysteresis 2 周期 → HandoverNeutral → Committed(G7)
  - **场景 B(BC 失效,mock)**:mock BC health(条件 A=5 + 条件 B=3)→ FinalDegrade → SafetyConcernEvent 发出 → M7 收到(G6)

### Step 6.4 — 证据输出

```bash
# 写入 evidence JSON
cp /tmp/sil_capture.json runs/p6_sil_e2e_<scenario>_<timestamp>.json
```

包含:主题捕获、状态转换序列、ASDR 全链路、验收门 8 条 checklist。

### Step 6.5 — T6 完成判据

- [ ] G1:bc_mpc 在 SIL 启动
- [ ] G2:consecutive_failures 主题连通
- [ ] G3:ReactiveOverrideCmd 到 L4(FCB Sim ASDR 记录)
- [ ] G4:11 状态机转换序列正确(ASDR 可见)
- [ ] G5:keep-last 废除,非 Committed 状态发空 plan(主题捕获验证)
- [ ] G6:FinalDegrade 触发 + SafetyConcernEvent 到 M7
- [ ] G7:Handover hysteresis 2 周期(场景 A)
- [ ] G8:SIL 端到端完整链(场景 A + B 都验证)
- [ ] evidence JSON 写入 `runs/`

---

## Task 7: codex adversarial review + regression + 8 验收门

### Step 7.1 — 完整 regression

```bash
# M5 全测试套
colcon test --packages-select m5_tactical_planner
colcon test --packages-select l3_msgs  # BcMpcHealth + SafetyConcernEvent 扩展
# 受影响下游
colcon test --packages-select m7_safety_supervisor  # SafetyConcernEvent 消费者
# IPOPT 回归(P5 引入,确保 P6 不破坏)
# ...
```

**判据**: 全绿,无回归。

### Step 7.2 — codex 对抗评审(强制)

调用 codex 评审本 P6 全部 diff,关注:
- 11 状态机转换边死锁/不可达
- FINAL_DEGRADE 误触发路径
- keep-last 废除后 L4 行为退化(空 plan 是否被 GNC active_route_manager 正确处理)
- BC health 主题 QoS/时序竞态
- SafetyConcernEvent 枚举扩展对现有 M7 on_safety_concern switch 的影响(default 分支是否兜底)
- handover hysteresis 计数器重置时机
- ASDR audit 字段完整性

**判据**: 0 Critical 通过;Critical 全修后才算 P6 完成。

### Step 7.3 — 8 验收门复核(spec §8)

逐门验证 + evidence 汇总。

### Step 7.4 — 文档同步

- 更新 `M5-progress.md`
- 更新 roadmap §3.2 P6 状态("✅ 完成")
- 更新 `handoff/workspace_log.md`

### Step 7.5 — T7 完成判据(P6 最终 DONE)

- [ ] 全 regression 绿
- [ ] codex review 0 Critical
- [ ] 8 验收门全绿 + evidence
- [ ] 文档同步
- [ ] commit + push(经 acceptance gate)

---

## 验收门 ↔ Task 映射(spec §8)

| 验收门 | Task |
|---|---|
| G1 bc_mpc 在 SIL 启动 | T1 + T2 + T6 |
| G2 mid→BC consecutive_failures 连通 | T6 |
| G3 BC 发 ReactiveOverrideCmd 到 L4 | T2 + T6 |
| G4 11 状态机单测覆盖 | T3 |
| G5 keep-last 彻底废除 | T4 + T6 |
| G6 FINAL_DEGRADE 触发 + 报 M7 | T3 + T5 + T6 |
| G7 Handover hysteresis 2 周期 | T3 + T6 |
| G8 SIL 端到端 | T6 |

---

## 风险触发回退

| 触发 | 回退动作 |
|---|---|
| T1 SIL baseline 发现 BC-MPC 有阻塞性 bug | 停 P6,先修 BC-MPC bug(可能开 P6.5) |
| T6 FinalDegrade 误触发(M7 MRM 误启动) | 调高 `override_no_improve_threshold`(5→10)+ `mid_unrecovered_threshold`(3→5),重跑 SIL |
| T6 Handover hysteresis 2 周期太短(BC 恒向恒速风险) | 调高 `handover_hysteresis_cycles`(2→3),重跑 SIL |
| T7 codex 发现 Critical | 修后重跑 T7,不允许带 Critical 完成 |

---

## 实施纪律

- **TDD 强制**: 每个子任务先 RED(失败测试)再 GREEN(实现)再 REFACTOR
- **不引入 mock/skip/forced PASS**(AGENTS.md COLREGs full-chain rule)
- **每 task 自闭环**: 完成后 colcon test 全绿才能进下一 task
- **codex review 强制**(Task 7 Step 7.2),不可跳过
- **evidence 落盘**: T1 + T6 evidence JSON 写入 `runs/`,commit 引用路径
- **handoff 记录**: P6 完成后更新 `handoff/workspace_log.md`
