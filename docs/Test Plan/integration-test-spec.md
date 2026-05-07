# MASS L3 TDL — Integration Test Specification

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-TEST-INT-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-07 |
| 状态 | 草稿 — TDD RED（所有 INT 场景为 stub，待 Wave 4a 实现） |
| 架构基线 | v1.1.2 §11–§15 |
| 上层文档 | `docs/Test Plan/00-test-master-plan.md` (SANGO-ADAS-L3-TEST-MASTER-001) |

---

## INT-001: M2→M6→M5 链路（Rule 14 对遇）

### 场景描述

- **本船**：18 kn @ 090°（正东）
- **目标**：12 kn @ 270°（正西，对遇）
- **CPA**：≈0 nm，TCPA ≈ 4 min
- **触发者**：`l3_external_mock_publisher` 发布包含对遇目标的 `WorldState`

### 消息流

```
[Mock]  → /l3/m2/world_state          (l3_msgs/WorldState, 4 Hz)
                 └─ EncounterClassification = HEAD_ON
[M6]    → /l3/m6/colregs_constraint   (l3_msgs/COLREGsConstraint, 2 Hz)
                 └─ rule_active = RULE_14, is_give_way = true
[M5]    → /l3/m5/avoidance_plan       (l3_msgs/AvoidancePlan, 1–2 Hz)
                 └─ maneuver_direction = STARBOARD
```

### 时序约束

| 消息 | 最大延迟（从 WorldState 起） |
|---|---|
| COLREGsConstraint | 500 ms |
| AvoidancePlan | 1000 ms |

### 通过判据

1. `COLREGsConstraint.rule_active == RULE_14`
2. `COLREGsConstraint.is_give_way == true`
3. `AvoidancePlan.maneuver_direction == STARBOARD`
4. 上述消息在规定时延内到达（使用 `rclcpp::WallRate` 等待循环验证）
5. `AvoidancePlan.confidence >= 0.7`

### 失败注入

Mock 注入 `WorldState.encounter_classification = HEAD_ON`（DCPA→0，TCPA<300s）即触发场景。

### 测试文件

`tests/integration/test_int_001_m2_m6_m5_chain.cpp`

---

## INT-002: M3→L2 航路重规划

### 场景描述

M3 因 COLREGs 冲突触发 `RouteReplanRequest`，Mock L2 返回 `ReplanResponse`。
测试所有 4 种响应状态码下 M3 的状态机行为。

### 消息流

```
[M3]    → /l3_external/m3/route_replan_request (l3_external_msgs/RouteReplanRequest)
                 └─ reason = COLREGS_CONFLICT
[Mock]  → /l3_external/l2/replan_response      (l3_external_msgs/ReplanResponse)
                 └─ status = {SUCCESS | PARTIAL | TIMEOUT | FAILURE}
[M3 内部状态机] ACTIVE → REPLAN_WAIT → ACTIVE (on SUCCESS)
                ACTIVE → REPLAN_WAIT → DEGRADED (on FAILURE)
```

### 时序约束

| 状态转换 | 最大时延 |
|---|---|
| ACTIVE → REPLAN_WAIT（收到 RouteReplanRequest 后）| 200 ms |
| REPLAN_WAIT → ACTIVE（收到 SUCCESS 后） | 500 ms |

### 通过判据

1. **SUCCESS**：M3 状态 ACTIVE → REPLAN_WAIT → ACTIVE；新 WP list 被接受
2. **PARTIAL**：M3 接受部分路线并发出警告（`/l3/asdr/record` 含 PARTIAL 原因）
3. **TIMEOUT**：M3 触发 MRC 降级（`/l3/m1/odd_state` 切换至 DEGRADED）
4. **FAILURE**：M3 进入 DEGRADED 并在 ASDR 中记录失败

### 失败注入

Mock 依次发布 4 种 `ReplanResponse.status` 值；每次注入独立子测试。

### 测试文件

`tests/integration/test_int_002_m3_l2_replan.cpp`

---

## INT-003: X-axis Checker Veto → M7 → M1 降级

### 场景描述

X-axis Deterministic Checker 发出 Veto 通知，M7 发布 `SafetyAlert`，M1 切换至 DEGRADED。
覆盖所有 6 种 veto_reason 枚举值。

### 消息流

```
[Mock]  → /l3_external/checker/veto_notification
                 └─ CheckerVetoNotification.veto_reason = {枚举值}
[M7]    → /l3/m7/safety_alert       (l3_msgs/SafetyAlert)
                 └─ severity = CRITICAL
[M1]    → /l3/m1/odd_state          (l3_msgs/ODDState)
                 └─ mode = DEGRADED
```

### 时序约束

| 消息 | 最大延迟（从 VetoNotification 起） |
|---|---|
| SafetyAlert(CRITICAL) | 200 ms |
| ODDState(DEGRADED) | 500 ms |

### 通过判据

1. `SafetyAlert.severity == CRITICAL`
2. `SafetyAlert.veto_reason` 与 mock 注入值一致
3. `ODDState.mode == DEGRADED`（M1 正确响应 M7 告警）
4. 以上对 6 种 veto_reason 枚举值均成立：
   `COLREGS_VIOLATION_IMMINENT | CPA_THRESHOLD_BREACH | ENVELOPE_EXCEEDED |
    SENSOR_DEGRADED | COMMAND_INVALID | WATCHDOG_TIMEOUT`

### 失败注入

Mock 依次注入 6 种 `CheckerVetoNotification.veto_reason` 值。

### 测试文件

`tests/integration/test_int_003_checker_veto.cpp`

---

## INT-004: Y-axis Reflex Arc → M1 OVERRIDDEN 广播

### 场景描述

Y-axis Reflex Arc 触发（极近距离目标），发送 `ReflexActivationNotification`（`l3_freeze_required=true`）。
M1 必须在 50ms 内进入 OVERRIDDEN 状态并向所有 L3 节点广播冻结指令。

### 消息流

```
[Mock]  → /l3_external/reflex/activation_notification
                 └─ ReflexActivationNotification.l3_freeze_required = true
[M1]    → /l3/m1/odd_state          (l3_msgs/ODDState)
                 └─ mode = OVERRIDDEN
[M1]    → /l3/m1/freeze_broadcast   (l3_msgs/FreezeBroadcast)
                 └─ freeze = true, reason = REFLEX_ARC
```

### 时序约束

| 消息 | 最大延迟（从 ReflexActivationNotification 起） |
|---|---|
| ODDState(OVERRIDDEN) | 50 ms |
| FreezeBroadcast(freeze=true) | 50 ms |

### 通过判据

1. `ODDState.mode == OVERRIDDEN` 在 50ms 内发布
2. `FreezeBroadcast.freeze == true` 在 50ms 内发布
3. M5 停止发布 `AvoidancePlan`（无新消息超过 200ms）
4. M1 在 `OVERRIDDEN` 时拒绝来自 M3 的新 WP（无响应）

### 失败注入

Mock 注入 `ReflexActivationNotification.l3_freeze_required = true`，
DCPA = 0.05 nm（< Reflex Arc 激活阈值）。

### 测试文件

`tests/integration/test_int_004_reflex_override.cpp`

---

## INT-005: 硬件覆盖 → M1 OVERRIDDEN → M5 冻结

### 场景描述

Hardware Override 信号（硬连线 Z-BOTTOM 层）激活，M1 进入 OVERRIDDEN，M5 必须在
100ms 内停止输出轨迹指令。

### 消息流

```
[Mock]  → /l3_external/hardware/override_active_signal
                 └─ OverrideActiveSignal.active = true
[M1]    → /l3/m1/odd_state          └─ mode = OVERRIDDEN
[M5]    ← 停止发布 /l3/m5/avoidance_plan（100ms 内无新消息）
```

### 时序约束

| 动作 | 最大延迟（从 OverrideActiveSignal 起） |
|---|---|
| M1 进入 OVERRIDDEN | 50 ms |
| M5 停止输出 | 100 ms |

### 通过判据

1. `ODDState.mode == OVERRIDDEN` 在 50ms 内
2. M5 `AvoidancePlan` 发布停止（50ms 内无新消息）
3. Override 解除后（`OverrideActiveSignal.active = false`），M1 恢复
   ACTIVE，M5 在 500ms 内重新开始输出

### 失败注入

Mock 注入 `OverrideActiveSignal.active = true`，随后 `active = false`。

### 测试文件

`tests/integration/test_int_005_006_override_dual_interface.cpp`

---

## INT-006: M5 三模式切换 + RFC-001 scheme B 双接口

### 场景描述

M5 在 `normal_LOS` / `avoidance_planning` / `reactive_override` 三种模式之间切换。
验证 RFC-001 scheme B 双输出接口（`/l3/m5/avoidance_plan` 和 `/l3/m5/ψ_cmd`）
在每种模式下的正确行为。

### 消息流

```
初始状态: normal_LOS
[Mock 注入 AvoidancePlan 触发信号]
M5 → avoidance_planning  (切换 <100ms)
[Mock 注入 ReactiveOverrideCmd]
M5 → reactive_override   (切换 <100ms)
```

### 时序约束

每次模式切换 < 100ms（`rclcpp::WallTimer` 测量）。

### 通过判据

1. `normal_LOS` 模式：仅发布 `/l3/m5/ψ_cmd`（无 `avoidance_plan`）
2. `avoidance_planning` 模式：发布 `/l3/m5/avoidance_plan`（含 waypoints）
3. `reactive_override` 模式：发布 `/l3/m5/ψ_cmd`（直接姿态指令，优先级最高）
4. 三次模式切换均 < 100ms
5. RFC-001 scheme B：`avoidance_plan` 和 `ψ_cmd` topic 同时存在（不互斥）

### 失败注入

依次注入：触发 `avoidance_planning` 的 `BehaviorPlan`；
触发 `reactive_override` 的 `ReactiveOverrideCmd`。

### 测试文件

`tests/integration/test_int_005_006_override_dual_interface.cpp`

---

## INT-007: ASDR 全模块覆盖（M1–M8）

### 场景描述

在全栈运行 5 秒内，验证所有 8 个 L3 模块均产出至少 1 条合规的 ASDR 记录。
每条记录须携带有效 SHA-256 签名（来自 M7 Task T7 SHA-256 ASDR 实现）。

### 消息流

```
[全栈运行]
M1–M8  → /l3/asdr/record   (l3_msgs/ASDRRecord, 事件 + 定期)
[Mock ASDR 订阅者收集所有记录]
```

### 时序约束

| 约束 | 要求 |
|---|---|
| 从触发到各模块发布 ASDRRecord | ≤ 5 秒（全 8 模块） |
| ASDRRecord.timestamp 与触发时间差 | ≤ 2 秒（本地时钟漂移容忍） |
| SHA-256 签名长度 | 精确 32 字节（uint8[32]） |

### 通过判据

对每个 `source_module` 值（M1 至 M8）：

1. 在 5s 内收到 ≥1 条 `ASDRRecord`
2. `ASDRRecord.source_module` 字段非空，值在 `{M1, M2, M3, M4, M5, M6, M7, M8}` 中
3. `ASDRRecord.sha256_signature` 长度 = 32 字节（256 bits）且非全零
4. `ASDRRecord.timestamp` 有效（`sec > 0`，`nanosec < 1e9`）
5. 记录总数 ≥ 8（每模块至少 1 条）

### 失败注入

全栈正常启动即可触发；无需额外 mock 注入。
若某模块未产出记录，测试失败并输出缺失模块列表。

### 测试文件

`tests/integration/test_int_007_asdr_full.cpp`

---

## INT-008: M8 SAT-2 分类 + IMO MASS Code C ToR 握手

### 场景描述

触发 4 种 SAT-2 条件，验证 M8 `AdaptiveSatTrigger` 的正确分类，以及
`ToRRequest` 消息的完整性（含 SAT-1 摘要 + 60s deadline）。
验证 IMO MASS Code C 的接管请求握手流程。

### 消息流

```
[Mock 触发条件 1: COLREGs conflict]
M6 → /l3/m6/colregs_constraint  (rule_active=RULE_14, give_way=true, CPA<0.3nm)
M8 ← 检测到 COLREGs 冲突 → SAT-2 触发

[Mock 触发条件 2: system confidence drop]
M2 → /l3/m2/world_state  (confidence=0.3 < threshold)
M8 ← 检测到系统置信度下降 → SAT-2 触发

[Mock 触发条件 3: operator request]
Mock → /l3_external/roc/sat_request  (type=OPERATOR_INITIATED)
M8 ← 收到操作员请求 → SAT-2 触发

[Mock 触发条件 4: TDL compression]
M1 → /l3/m1/odd_state  (mode=DEGRADED, compression_active=true)
M8 ← 检测到 TDL 压缩 → SAT-2 触发

[每次 SAT-2 触发后]
M8 → /l3_external/m8/tor_request  (l3_msgs/ToRRequest)
       └─ sat1_summary: 非空
       └─ deadline_seconds: 60
       └─ trigger_reason: 对应的 4 种原因之一
```

### 时序约束

| 约束 | 要求 |
|---|---|
| SAT-2 触发 → AdaptiveSatTrigger 决策 | ≤ 100 ms |
| ToRRequest 发布后 deadline 字段 | = 触发时间 + 60s |
| ROC 操作员确认窗口 | 55 秒（剩余 5 秒缓冲） |
| M8 UI_State 渲染延迟 | ≤ 20 ms（50 Hz 周期内） |

### 通过判据

对 4 种触发条件各自：

1. `AdaptiveSatTrigger` 分类正确（条件 1→COLREGS_CONFLICT，
   条件 2→CONFIDENCE_DROP，条件 3→OPERATOR_REQUEST，条件 4→TDL_COMPRESSION）
2. `ToRRequest.sat1_summary` 非空（≥10 字符）
3. `ToRRequest.deadline_seconds == 60`（IMO MASS Code C §4.2 规定的 TMR ≥ 60s）
4. `ToRRequest.trigger_reason` 与注入条件一致

### IMO MASS Code C ToR 握手验证

5. Mock ROC 在 55s 内发布 `ToRResponse.status = ACCEPTED`
6. M8 在收到 ACCEPTED 后停止重发 `ToRRequest`
7. Mock ROC 未响应（模拟超时）时，M8 在 60s 后发布 `SafetyAlert(CRITICAL)`
   并通知 M1 切换至 DEGRADED（TMR 超时保护）

### 失败注入

依次注入 4 种触发条件（见上方消息流）；ToR 超时通过 mock ROC 不响应实现。

### 测试文件

`tests/integration/test_int_008_m8_sat_tor.cpp`

---

## 附录 A：消息 topic 索引

| Topic | 消息类型 | 方向 | 频率 |
|---|---|---|---|
| `/l3/m1/odd_state` | `l3_msgs/ODDState` | M1 发布 | 1 Hz + 事件 |
| `/l3/m1/freeze_broadcast` | `l3_msgs/FreezeBroadcast` | M1 发布 | 事件 |
| `/l3/m2/world_state` | `l3_msgs/WorldState` | M2 发布 | 10–50 Hz |
| `/l3/m5/avoidance_plan` | `l3_msgs/AvoidancePlan` | M5 发布 | 1–2 Hz |
| `/l3/m5/ψ_cmd` | `l3_msgs/PsiCmd` | M5 发布 | 4–10 Hz |
| `/l3/m6/colregs_constraint` | `l3_msgs/COLREGsConstraint` | M6 发布 | 2 Hz |
| `/l3/m7/safety_alert` | `l3_msgs/SafetyAlert` | M7 发布 | 事件 |
| `/l3/asdr/record` | `l3_msgs/ASDRRecord` | 各模块发布 | 事件 + 定期 |
| `/l3_external/checker/veto_notification` | `l3_external_msgs/CheckerVetoNotification` | Mock 发布 | 事件 |
| `/l3_external/reflex/activation_notification` | `l3_external_msgs/ReflexActivationNotification` | Mock 发布 | 事件 |
| `/l3_external/hardware/override_active_signal` | `l3_external_msgs/OverrideActiveSignal` | Mock 发布 | 事件 |
| `/l3_external/m8/tor_request` | `l3_msgs/ToRRequest` | M8 发布 | 事件 |
| `/l3_external/roc/sat_request` | `l3_external_msgs/SatRequest` | Mock ROC 发布 | 事件 |

---

*各 INT 场景当前为 TDD RED 状态（stub）。Wave 4a 实现阶段（Task E2-2 起）逐步填充。*
