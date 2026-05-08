# D1.1 ROS2 工作区 + IDL 消息 — 执行 Spec

| 字段 | 值 |
|---|---|
| D 编号 | D1.1 |
| 标题 | ROS2 工作区 + IDL 消息 |
| 阶段 | Phase 1 |
| 预计人周 | 1.5 pw |
| 目标完成 | 2026-05-24 |
| Owner | 架构师 |
| 基线 | v1.1.2-patch1（tag `707f1cd`） |
| 权威来源 | 架构报告 §15.1 + §15.2；v3.0 计划 §3 D1.1；consolidated findings F P1-F-04 + F P1-F-06 |
| spec 创建 | 2026-05-08（brainstorming 产出） |

---

## 1. 目标与 DoD

**目标**：基于 D0 baseline（v1.1.2-patch1）建立完整 colcon workspace；l3_msgs（25 条）+ l3_external_msgs（12 条）与架构报告 §15.1 完全对齐并加入 `schema_version` 字段；mock publisher 节点以 §15.2 规定频率发布全部外部接口消息；`colcon build --packages-select l3_msgs l3_external_msgs` 全绿。

**DoD（全部满足才宣布 D1.1 关闭）**：

- [ ] `colcon build --packages-select l3_msgs l3_external_msgs` exit 0，stderr 无 warning
- [ ] 25 + 11 条顶层 .msg 全含 `string schema_version  # default: "v1.1.2"`（TimeWindow 豁免，见 §3）
- [ ] 所有 ARCH-GAP 标注完整记录于本 spec §4（编号 001–004 + M4）
- [ ] mock publisher 运行：`ros2 topic hz /fusion/own_ship_state` 显示 ~50 Hz
- [ ] D0 CI 2 job（multi_vessel_lint + path_s_dry_run）验证不 break（本地 grep 确认）
- [ ] Finding F P1-F-04 + F P1-F-06 关闭证据记入 §7 closure checklist

---

## 2. 现状审计结论

### 2.1 l3_msgs（25 条）

审计基准：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` §15.1（行 1382–1596）

#### 顶层消息（16 条，独立 pub/sub topic）

| 消息文件 | schema_version | confidence | rationale | stamp 字段 | D1.1 动作 |
|---|---|---|---|---|---|
| `ODDState.msg` | ❌ 缺 | ❌ 缺（有 conformance_score）| ❌ 缺（有 zone_reason）| ✅ stamp | 加 schema_version；confidence/rationale → `[ARCH-GAP-D1.1-001]` |
| `OwnShipState.msg` | ❌ 缺 | ❌ 缺 | ❌ 缺 | ✅ stamp | 加 schema_version；confidence → `[ARCH-GAP-D1.1-002]` |
| `SATData.msg` | ❌ 缺 | ❌ 缺 | ❌ 缺 | ✅ stamp | 加 schema_version；confidence → `[ARCH-GAP-D1.1-003]` |
| `ASDRRecord.msg` | ❌ 缺 | ❌ 缺（有 signature）| ❌ 缺 | ✅ stamp | 加 schema_version；confidence **永久豁免**（SHA-256 签名替代信任机制，见 §3.3）|
| `TrackedTarget.msg` | ❌ 缺 | ✅ confidence | ❌ 缺 | ✅ stamp | 加 schema_version；rationale → `[ARCH-GAP-D1.1-004]` |
| `ToRRequest.msg` | ❌ 缺 | ✅ confidence | ❌ 缺（有 context_summary + recommended_action）| ✅ stamp | 加 schema_version；rationale gap → D2.8 |
| `ReactiveOverrideCmd.msg` | ❌ 缺 | ✅ confidence | ❌ 缺 | ✅ trigger_time（§15.1 规定，非 stamp）| 加 schema_version；rationale gap → D2.8 |
| `ModeCmd.msg` | ❌ 缺 | ✅ | ✅ | ✅ stamp | 加 schema_version |
| `WorldState.msg` | ❌ 缺 | ✅ | ✅ | ✅ stamp | 加 schema_version |
| `BehaviorPlan.msg` | ❌ 缺 | ✅ | ✅ | ✅ stamp | 加 schema_version |
| `COLREGsConstraint.msg` | ❌ 缺 | ✅ | ✅ | ✅ stamp | 加 schema_version |
| `AvoidancePlan.msg` | ❌ 缺 | ✅ | ✅ | ✅ stamp | 加 schema_version |
| `MissionGoal.msg` | ❌ 缺 | ✅ | ✅ | ✅ stamp | 加 schema_version |
| `RouteReplanRequest.msg` | ❌ 缺 | ✅ | ✅ | ✅ stamp | 加 schema_version |
| `SafetyAlert.msg` | ❌ 缺 | ✅ | ✅ | ✅ stamp | 加 schema_version |
| `UIState.msg` | ❌ 缺 | ✅ | ✅ | ✅ stamp | 加 schema_version |

#### 子消息（9 条，embedded type，无独立 pub/sub）

**全部豁免** schema_version / confidence / rationale / stamp 强制要求。子消息由容器消息的字段覆盖。

| 子消息文件 | 备注 |
|---|---|
| `AvoidanceWaypoint.msg` | 嵌入 AvoidancePlan |
| `SpeedSegment.msg` | 嵌入 AvoidancePlan |
| `Constraint.msg` | 嵌入 COLREGsConstraint |
| `EncounterClassification.msg` | 嵌入 TrackedTarget |
| `ZoneConstraint.msg` | 嵌入 WorldState |
| `RuleActive.msg` | 嵌入 COLREGsConstraint；`rule_confidence` 命名不一致 → `[ARCH-GAP-D1.1-005]` D2.8 统一 |
| `SAT1Data.msg` | 嵌入 SATData |
| `SAT2Data.msg` | 嵌入 SATData；`system_confidence` 命名不一致 → `[ARCH-GAP-D1.1-006]` D2.8 统一 |
| `SAT3Data.msg` | 嵌入 SATData；`prediction_uncertainty` 语义不同（不确定度 vs 置信度），保留原名 |

### 2.2 l3_external_msgs（12 条）

审计基准：架构报告 §15.1（行 1513–1595）+ §15.2 接口矩阵（行 1598–1635）

| 消息文件 | schema_version | confidence | D1.1 动作 |
|---|---|---|---|
| `VoyageTask.msg` | ❌ 缺 | ✅ | 加 schema_version |
| `PlannedRoute.msg` | ❌ 缺 | ✅ | 加 schema_version |
| `SpeedProfile.msg` | ❌ 缺 | ✅ | 加 schema_version |
| `ReplanResponse.msg` | ❌ 缺 | ✅ | 加 schema_version |
| `TrackedTargetArray.msg` | ❌ 缺 | ✅ | 加 schema_version |
| `FilteredOwnShipState.msg` | ❌ 缺 | ✅ | 加 schema_version |
| `EnvironmentState.msg` | ❌ 缺 | ✅ | 加 schema_version |
| `CheckerVetoNotification.msg` | ❌ 缺 | ✅ | 加 schema_version |
| `EmergencyCommand.msg` | ❌ 缺 | ✅（trigger_time 非 stamp）| 加 schema_version |
| `ReflexActivationNotification.msg` | ❌ 缺 | ❌ 缺（§15.1 无）| 加 schema_version；confidence **外部团队待定**，见 §3.3 |
| `OverrideActiveSignal.msg` | ❌ 缺 | ❌ 缺（§15.1 无）| 加 schema_version；confidence **外部团队待定**，见 §3.3 |
| `TimeWindow.msg` | — | — | **完全豁免**（纯时间窗子消息，嵌入 VoyageTask） |

### 2.3 mock publisher 现状

文件：`src/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py`

| 检查项 | 状态 |
|---|---|
| 发布 topic 数量 | ✅ 11 个 topic（全部 l3_external_msgs 除 TimeWindow）|
| 频率对齐 §15.2 | ✅ 50Hz / 2Hz / 1Hz / 0.2Hz / 30s-event 各正确 |
| schema_version 字段赋值 | ❌ 全部缺失（需在 T4 补全）|
| ament_python 包结构 | ✅ package.xml + setup.py + resource/ 目录正确 |
| CMakeLists.txt | — 无（ament_python 不需要，正确）|

### 2.4 结构性 gap

| Gap | 路径 | 状态 | 处置 |
|---|---|---|---|
| `[ARCH-GAP-D1.1-M4]` m4 无 ROS2 manifest | `src/m4_behavior_arbiter/` | 有目录结构（config/src/test/include/launch）但无 package.xml + CMakeLists.txt | **延至 D2.1**；不影响 D1.1 colcon DoD |
| `m8_hmi_bridge` 非 ROS2 package | `src/m8_hmi_bridge/` | D0 pure-logic Python（active_role.py）| D1.1 不处理；M8 canonical ROS2 骨架 = `m8_hmi_transparency_bridge` |
| `fcb_simulator` + `m8_hmi_transparency_bridge` ruff violations（94 issues）| `src/fcb_simulator/` `src/m8_hmi_transparency_bridge/` | `.ruff.toml` 已 exclude | D1.2 决策是否纳入 ruff scope |

---

## 3. 关键设计决策

### 3.1 schema_version 决策

| 属性 | 决策 |
|---|---|
| 字段类型 | `string schema_version` |
| D1.1 默认值 | `"v1.1.2"` |
| 格式规范 | `"vMAJOR.MINOR.PATCH"` 匹配架构报告版本号 |
| 更新规则 | **统一随架构报告版本升级**。arch report v1.1.2 → v1.1.3 时，所有 .msg 同步改为 `"v1.1.3"`（不论 IDL 是否变化）。执行方式：全局 sed 替换 |
| 适用范围 | 所有顶层消息（pub/sub topic）；子消息豁免 |
| 来源 | consolidated findings F P1-F-04；NLM safety_verification 查询（🟢 High：string 在 on-vessel DDS LAN 场景带宽充裕，human-readable 更利于 ASDR 审计）|

### 3.2 confidence 覆盖规则（D1.1）

| 类别 | 规则 |
|---|---|
| l3_msgs 顶层消息，§15.1 已定义 confidence | **保留**，D1.1 不动 |
| l3_msgs 顶层消息，§15.1 未定义 confidence（ODDState / OwnShipState / SATData）| 标 ARCH-GAP，**D2.8 IDL 修订时补全**；D1.1 不加（超出 §15.1 对齐范围）|
| `ASDRRecord.msg` | **永久豁免**：SHA-256 `signature` 字段替代置信度机制；语义冲突（审计日志不是估计值）|
| l3_external_msgs，§15.1 无 confidence（OverrideActiveSignal / ReflexActivationNotification）| 标为**外部团队待定**；需等外部团队（Hardware Override Arbiter / Y-axis Reflex Arc）RFC 确认后补入；D1.1 不加 |
| 子消息（embedded type）| 全部豁免 |

### 3.3 Doer-Checker 独立性验证（IDL 层面）

以下两条消息须保持 M7 单向输出语义，D1.1 patch 不引入跨 M7 边界字段：

- `SafetyAlert.msg`：M7 → M1，仅 alert + MRM 索引，无轨迹字段 ✅
- `ModeCmd.msg`：M1 → M4，仅模式约束，无 M7 状态字段 ✅

---

## 4. ARCH-GAP 清单

| ID | 消息 | Gap 描述 | 关闭落点 |
|---|---|---|---|
| ARCH-GAP-D1.1-001 | ODDState.msg | 缺 `confidence` + `rationale`（§15.1 IDL 无；conformance_score 是功能字段非置信度）| D2.8 IDL 修订 |
| ARCH-GAP-D1.1-002 | OwnShipState.msg | 缺 `confidence`（嵌入 WorldState，WorldState 已有；但 OwnShipState 独立时无置信度来源）| D2.8 IDL 修订 |
| ARCH-GAP-D1.1-003 | SATData.msg | 缺容器级 `confidence`（子消息各自有但容器无聚合置信度）| D2.8 IDL 修订 |
| ARCH-GAP-D1.1-004 | TrackedTarget.msg | 缺 `rationale`（§15.1 IDL 无；M2 感知融合推理摘要缺失）| D2.8 IDL 修订 |
| ARCH-GAP-D1.1-005 | RuleActive.msg（子消息）| `rule_confidence` 命名与 v3.0 `confidence` 规范不一致 | D2.8 IDL 修订 |
| ARCH-GAP-D1.1-006 | SAT2Data.msg（子消息）| `system_confidence` 命名与 v3.0 `confidence` 规范不一致 | D2.8 IDL 修订 |
| ARCH-GAP-D1.1-M4 | m4_behavior_arbiter | 无 package.xml + CMakeLists.txt；RFC-009 方案B（Python IvP）build 类型未确认 | D2.1 M4 实装 sprint |
| ARCH-GAP-D1.1-EXT-1 | OverrideActiveSignal.msg | 外部层（Hardware Override Arbiter）是否提供 confidence 未 RFC 确认 | 外部团队 RFC 后补入 |
| ARCH-GAP-D1.1-EXT-2 | ReflexActivationNotification.msg | 外部层（Y-axis Reflex Arc）是否提供 confidence 未 RFC 确认 | 外部团队 RFC 后补入 |

---

## 5. Task 拆分

### 依赖图

```
[T0] schema_version 决策（本 spec §3.1，已完成）
 │
 ├─[T1a] l3_msgs IDL 审计报告（本 spec §2.1 已完成）
 ├─[T1b] l3_external_msgs 审计（本 spec §2.2 已完成）
 │         ↓ 并行，无依赖
 ├─[T2a] l3_msgs .msg patch        依赖 T1a
 ├─[T2b] l3_external_msgs .msg patch  依赖 T1b
 │
 ├─[T3]  CMakeLists.txt 验证       依赖 T2a + T2b
 │
 ├─[T4]  mock publisher schema_version 赋值  依赖 T2b
 │
 ├─[T5]  colcon build 验证         依赖 T3
 │
 └─[T6]  mock publisher 运行验证   依赖 T4 + T5
```

### T1a — l3_msgs IDL 审计（**已完成，本 spec §2.1 即产出**）

- **估时**：0h（本 brainstorming session 完成）
- **AC**：§2.1 表格完整，每条消息 gap 明确，ARCH-GAP 编号分配

### T1b — l3_external_msgs 审计（**已完成，本 spec §2.2 即产出**）

- **估时**：0h
- **AC**：§2.2 表格完整，外部团队待定项明确标注

### T2a — l3_msgs .msg patch

- **估时**：3h
- **范围**：向 16 条顶层 l3_msgs 添加 `string schema_version  # default: "v1.1.2"`；位置：文件顶部注释行之后、第一个字段之前
- **不做**：不改 confidence/rationale（gap 项延 D2.8）；不改子消息
- **AC**：
  - `grep -l "schema_version" src/l3_msgs/msg/*.msg | wc -l` = 16
  - `grep "schema_version" src/l3_msgs/msg/AvoidanceWaypoint.msg` → exit 1（子消息无）
  - `grep "schema_version" src/l3_msgs/msg/ASDRRecord.msg` → 存在（豁免 confidence，但 schema_version 仍加）

### T2b — l3_external_msgs .msg patch

- **估时**：2h
- **范围**：向 11 条顶层 l3_external_msgs 添加 `string schema_version  # default: "v1.1.2"`（TimeWindow 豁免）
- **不做**：不改 OverrideActiveSignal / ReflexActivationNotification 的 confidence（外部团队待定）
- **AC**：
  - `grep -l "schema_version" src/l3_external_msgs/msg/*.msg | wc -l` = 11
  - `grep "schema_version" src/l3_external_msgs/msg/TimeWindow.msg` → exit 1（豁免）

### T3 — CMakeLists.txt 验证

- **估时**：2h
- **范围**：确认 l3_msgs + l3_external_msgs CMakeLists.txt 无 warning；验证 `find_package(l3_msgs REQUIRED)` 在 l3_external_msgs 中正确；确认 `ament_export_dependencies` 完整
- **不做**：不改 m4 / fcb_simulator / m8_hmi_transparency_bridge 的 CMakeLists.txt
- **AC**：
  - `colcon build --packages-select l3_msgs` exit 0
  - `colcon build --packages-select l3_external_msgs` exit 0（自动拉 l3_msgs）
  - stderr 无 CMake warning

### T4 — mock publisher schema_version 赋值

- **估时**：2h
- **范围**：在 `external_mock_publisher.py` 所有 `_publish_*` 方法中，对每条消息赋值 `msg.schema_version = "v1.1.2"`；OverrideActiveSignal / ReflexActivationNotification 同样加（schema_version 不豁免）
- **不做**：不改发布频率；不改字段值；不改 YAML 配置结构
- **AC**：
  - `grep -c 'schema_version = "v1.1.2"' src/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py` ≥ 11

### T5 — colcon build 验证

- **估时**：1h
- **范围**：本地执行 `colcon build --packages-select l3_msgs l3_external_msgs`；截图 exit 0 + stderr 空
- **AC**：
  - exit 0
  - stderr 无 warning（`colcon build ... 2>&1 | grep -i warning` → 空）
  - D0 CI grep 保护：`grep -rE "(\bFCB\b|45\s*m\b)" src/l3_msgs/ src/l3_external_msgs/` → exit 1

### T6 — mock publisher 运行验证

- **估时**：1h
- **范围**：source 已构建 workspace；`ros2 run l3_external_mock_publisher external_mock_publisher`；分别验证各频率 topic
- **AC**：
  - `ros2 topic hz /fusion/own_ship_state` ≈ 50 Hz（±5%）
  - `ros2 topic hz /fusion/tracked_targets` ≈ 2 Hz
  - `ros2 topic hz /l1/voyage_task` ≈ 1 Hz
  - `ros2 topic hz /fusion/environment_state` ≈ 0.2 Hz
  - `ros2 topic echo /fusion/own_ship_state --once | grep schema_version` → `"v1.1.2"`

---

## 6. Owner-by-day 矩阵（5/13–5/24）

| 日期 | 上午 | 下午 | Task |
|---|---|---|---|
| **5/13**（周三）| T2a 开始：l3_msgs .msg patch（16 条顶层）| T2a 完成；T2b 开始：l3_external_msgs patch（11 条）| T2a + T2b |
| **5/14**（周四）| T2b 完成；T3 CMakeLists 验证 | T4 mock publisher schema_version 赋值 | T2b + T3 + T4 |
| **5/15**（周五）| T5 colcon build 验证 | T6 mock publisher 运行验证 | T5 + T6 |
| **5/19**（周二）| buffer：D0 CI 2 job 保护验证；ARCH-GAP 文档收尾 | spec §7 closure checklist 填写 | buffer |
| **5/20–5/24** | contingency（R1.1/R1.4 风险处置备用时间）| — | — |

---

## 7. Demo Charter（服务 DEMO-1 6/15 Skeleton Live）

| 段 | 内容 |
|---|---|
| **Scenario** | 本地 colcon workspace 启动 mock publisher；`ros2 topic list` 显示全部 11 个外部接口 topic；`ros2 topic hz` 验证频率与 §15.2 一致 |
| **Audience × View** | 架构师：IDL 字段正确性（schema_version 存在）；V&V 工程师：频率合规证据；基础设施：colcon build 无警告截图 |
| **Visible Success** | `ros2 topic echo /fusion/own_ship_state --once` 输出含 `schema_version: "v1.1.2"`；`colcon build --packages-select l3_msgs l3_external_msgs` exit 0 截图 |
| **Showcase Bundle** | ① colcon build 截图；② `ros2 topic hz` 输出（50Hz/2Hz/1Hz/0.2Hz 各一条）；③ ARCH-GAP 清单（§4，共 9 条） |
| **Contribution Map** | D1.1 IDL 基线 → D1.3a（仿真器依赖 TrackedTargetArray + FilteredOwnShipState）→ D1.5（V&V Plan IDL baseline = "v1.1.2"）→ DEMO-1 IDL 流通验证 |

---

## 8. Risk + Contingency

| ID | Risk | 概率 | 影响 | Contingency |
|---|---|---|---|---|
| R1.1 | `geographic_msgs` 在 ROS2 Jazzy 包名变更 | 🟡 中 | colcon build 失败 | T3 前先 `apt search geographic` 确认包名；CMakeLists 加 Jazzy-specific 注释 |
| R1.2 | l3_external_msgs 依赖 l3_msgs install 顺序错误导致 symbol not found | 🟡 中 | build warning/error | 用 `colcon build --packages-up-to l3_external_msgs` 触发自动拓扑；确认 `ament_export_dependencies(... l3_msgs)` |
| R1.3 | m8_hmi_transparency_bridge / fcb_simulator ruff violations 污染 D0 CI job | 🟢 低 | CI break | `.ruff.toml` 已 exclude；T5 验证时确认 exclude 规则未被 .msg patch 覆盖 |
| R1.4 | mock publisher `__pycache__` 残留 cpython-314 bytecode（已见 .pyc）与 ROS2 Jazzy Python 3.11 不兼容 | 🟡 中 | import 失败 | T6 前清除 `find src/l3_external_mock_publisher -name "*.pyc" -delete`；确认 CI 使用 Python 3.11 |
| R1.5 | `string schema_version` 在 FilteredOwnShipState（50 Hz）增加 DDS 序列化开销 | 🔴 影响极低 | 微量延迟 | 接受；on-vessel LAN DDS 带宽充裕；D2.8 IDL 修订时可评估 uint32 切换 |

---

## 9. 下游接口

| 下游 D | 消费 D1.1 产出 | 关键约束 |
|---|---|---|
| **D1.2** CI/CD | l3_msgs + l3_external_msgs colcon build 状态；.ruff.toml exclude 范围 | D1.1 不改 ruff exclude；D1.2 决策 fcb_simulator/m8_hmi_transparency_bridge 是否纳入 |
| **D1.3a** 4-DOF MMG 仿真器 | `TrackedTargetArray.msg` + `FilteredOwnShipState.msg` 字段结构 | schema_version 已加；msg 字段与 §15.1 对齐后 D1.3a 仿真器可订阅 |
| **D1.5** V&V Plan | IDL schema_version 引用基线 | V&V Plan 写入 `"v1.1.2"` 作为 IDL baseline；V&V 场景 schema 引用此版本 |
| **D1.6** 场景 schema | `vessel_class` 字段（来自 capability manifest，非 IDL）| 不直接依赖 D1.1 IDL |
| **D2.8** IDL 修订 | ARCH-GAP 001–006 清单 | confidence/rationale 补全；sub-msg 命名统一（rule_confidence → confidence） |
| **DEMO-1** 6/15 | mock publisher 运行 + colcon build 截图 | D1.1 必须在 5/24 关闭才能为 DEMO-1 提供 IDL 流通证据 |

---

## 10. Closure Checklist（5/24 EOD 填写）

| 判据 | 证据 | 状态 |
|---|---|---|
| colcon build exit 0 无 warning | 截图路径：`docs/Design/Detailed Design/D1.1-ros2-workspace/evidence/colcon-build-ok.png` | ⬜ |
| 25 + 11 条顶层 .msg 含 schema_version | `grep -rl "schema_version" src/l3_msgs/msg src/l3_external_msgs/msg \| wc -l` = 27 | ⬜ |
| TimeWindow 豁免确认 | `grep "schema_version" src/l3_external_msgs/msg/TimeWindow.msg` → exit 1 | ⬜ |
| mock publisher 50 Hz | `ros2 topic hz /fusion/own_ship_state` 截图 | ⬜ |
| D0 CI job 不 break | 本地 `grep -rE "(\bFCB\b|45\s*m\b)" src/l3_msgs/ src/l3_external_msgs/` → exit 1 | ⬜ |
| F P1-F-04 关闭证据 | 本 spec §3.1 + T2a/T2b 完成记录 | ⬜ |
| F P1-F-06 关闭证据 | T5 colcon build 截图 | ⬜ |
| ARCH-GAP 9 条完整记录 | 本 spec §4 | ✅ |
