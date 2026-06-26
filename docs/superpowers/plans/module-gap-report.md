# MASS L3 TDL 8 模块缺口分析报告

> **审计日期**：2026-05-19  
> **架构基线**：v1.1.3-pre-stub（基于 v1.1.2）  
> **代码基线**：Wave 1 实现中  
> **方法**：逐模块对照架构 §5–§12 与实现代码，标记 ✅/⚠️/❌

---

## 总览

| 模块 | Sloc | 严重缺口 | 中等缺口 | 轻微问题 |
|------|------|----------|----------|----------|
| M1 ODD Manager | 1669 | 1 | 1 | 1 |
| M2 World Model | 1860 | 0 | 1 | 1 |
| M3 Mission Manager | 1488 | 0 | 0 | 0 |
| M4 Behavior Arbiter | 476 | 1 | 1 | 0 |
| M5 Tactical Planner | 2553 | 2 | 1 | 1 |
| M6 COLREGs Reasoner | 1694 | 0 | 1 | 1 |
| M7 Safety Supervisor | 2007 | 2 | 0 | 0 |
| M8 HMI Bridge | 1157 | 0 | 1 | 0 |
| **总计** | **14924** | **6** | **6** | **4** |

---

## M1 — ODD/Envelope Manager (Src: 1669 sloc)

### 架构要求 (§5.3)
| 要求 | 代码状态 | 严重度 | 优先级 |
|---|---|---|---|
| 5 子模块：包络评估器 + 模式FSM + TMR/TDL + MRC控制器 + 置信度门控 | ✅ 5/5 实现 | — | — |
| Capability Manifest YAML 加载（船型参数 + 包络 + 水动力） | ❌ 缺失 | P1 | DEMO-2 |
| 3 正交状态维度 (AutoLevel × ODDZone × Health) | ✅ OddStateMachine 实现 | — | — |
| 5 状态 FSM (IN/EDGE/OUT/MRC_PREP/MRC_ACTIVE) | ✅ +1: OVVERRIDDEN 状态 | — | — |
| ODD_StateMsg 发布 @ 1 Hz | ✅ 1 Hz timer | — | — |
| Capability Manifest → envelope/ood_a/b/c/d 限速加载 | ❌ 在 M5 实现, M1 无 | P1 | DEMO-2 |
| 接口：odd_state / mode_cmd / mrc_request / safety_alert | ✅ 6 publishers/subscribers | — | — |

### 详细缺口

#### GAP-M1-001: Capability Manifest 加载位置错误 (P1)
- **架构要求** (§5.3.3)：M1 在系统启动时加载船型特定的 Capability Manifest（YAML 格式），含 vessel/envelope/hydrodynamics。
- **代码实现**：`m1_odd_envelope_manager/` 没有 Capability Manifest 加载代码。只加载 `m1_params.yaml`（评分权重/阈值）。完整的 Capability Manifest 在 `m5_tactical_planner/` 实现 (`capability_manifest.hpp` + `config/fcb_vessel_capability.yaml`)。
- **文件证据**：
  - `m1_odd_envelope_manager/include/m1_odd_envelope_manager/parameter_loader.hpp` — 只加载评分参数
  - `m5_tactical_planner/include/m5_tactical_planner/shared/capability_manifest.hpp` — Capability Manifest 在 M5
  - `m5_tactical_planner/config/fcb_vessel_capability.yaml` — 船型配置文件
- **影响**：M1 无法根据 Capability Manifest 中的 ODD 包络参数（`ood_a/b/c/d.max_speed_kn`, `min_cpa_nm`）约束自身状态判定。实践中 M1 从自己的 YAML 读取独立参数，可能和 Manifest 不同步。
- **修复**：Capability Manifest 加载应移到 `common/` 共享库供 M1+M5 共用，或 M1 从 Manifest 读取 ODD 包络参数。

#### GAP-M1-002: ODDState.msg 缺 OVERRIDDEN 状态值 (P2)
- **架构要求** (§3.5)：6 状态 FSM (IN/EDGE/OUT/MRC_PREP/MRC_ACTIVE/OVERRIDDEN)。
- **代码实现**：
  - `EnvelopeState` 枚举定义了 6 个值 (In/Edge/Out/MrCPrep/MrCActive/Overridden) ✅
  - `ODDState.msg` `envelope_state` 字段只有 5 个常量 (ENVELOPE_IN/EDGE/OUT/MRC_PREP/MRC_ACTIVE)，**缺少 OVERRIDDEN**。
- **文件证据**：`l3_msgs/msg/ODDState.msg:22-27`
- **影响**：当系统处于 OVERRIDDEN 状态时，下游模块收到的 ODDState 消息会携带 `envelope_state = 5`（无法映射到任何命名常量），可能导致意外行为。
- **修复**：在 `ODDState.msg` 补充 `uint8 ENVELOPE_OVERRIDDEN = 5`。

#### GAP-M1-003: M1 参数 YAML 独立于 Capability Manifest (P3)
- **架构要求** (§5.3.3)：所有 ODD 包络参数（max_speed, min_cpa, min_tcpa）必须来自统一的 Capability Manifest。
- **代码实现**：`m1_params.yaml` 定义了 37 个 `[TBD-HAZID]` 参数，与 Capability Manifest 中的参数**部分重叠**（如 CPA/TCPA 阈值）。两者无自动同步机制。
- **影响**：添加新船型需要手动同步 m1_params.yaml 和 fcb_vessel_capability.yaml，增加了维护错误的风险。Phase-2 多船型扩展时会成为问题。

---

## M2 — World Model (Src: 1860 sloc)

### 架构要求 (§6.3)
| 要求 | 代码状态 | 严重度 | 优先级 |
|---|---|---|---|
| 三视图：静态(SV) + 动态(DV) + 自身(EV) | ✅ 3 视图实现 | — | — |
| OVERTAKING 扇区 [112.5°, 247.5°] | ✅ EncounterClassifier 配置 112.5/247.5 | — | — |
| CPA/TCPA 计算（M2 内部，非 Fusion） | ✅ CpaTcpaCalculator | — | — |
| 置信度字段 per target | ✅ AggregatedHealth + confidence | — | — |
| 2 Hz 输入聚合 → 4 Hz 输出 | ✅ 4 Hz timer | — | — |
| ENC 加载 (S-57) | ✅ EncLoader | — | — |
| TrackBuffer 时间对齐 | ✅ TrackBuffer | — | — |

### 详细缺口

#### GAP-M2-001: M2 输出缺少 per-target 置信度字段 (P2)
- **架构要求** (§6.3.2)：每个 tracked_target 应有 `confidence: { position, velocity, intent }` 三个独立置信度分量。
- **代码实现**：`TargetSnapshot` 只有 `confidence`（单一 float）和 `classification_confidence`，没有 position/velocity/intent 三个独立分量。`WorldState.msg` 的 `TrackedTarget[]` 字段也没有三层置信度。
- **文件证据**：
  - `m2_world_model/include/m2_world_model/types.hpp:37-48` — TargetSnapshot 只含 `float classification_confidence`
  - `l3_msgs/msg/TrackedTarget.msg` — 需确认字段
- **影响**：M7 SOTIF 假设违反检测需要 position/velocity/intent 分量的置信度来做细粒度监控。单值置信度降低了检测精度。
- **修复**：在 TrackedTarget.msg 补充 `float32 confidence_position`, `confidence_velocity`, `confidence_intent` 字段；M2 TargetSnapshot 补充对应字段。

#### GAP-M2-002: 缺少 ODD_D 场景的 M2 测试 (P3)
- **架构要求**：所有 4 个 ODD 子域应有测试覆盖。
- **代码实现**：M2 测试主要以开阔水域（ODD-A）为目标。缺少 ODD-D（能见度不良）条件下 track buffer 和 CPA 计算降质行为测试。
- **影响**：ODD-D 条件下 M2 输出质量不可验证。

---

## M3 — Mission Manager (Src: 1488 sloc)

### 架构要求 (§7)
| 要求 | 代码状态 | 严重度 | 优先级 |
|---|---|---|---|
| L1 VoyageTask 订阅 + 校验 | ✅ VoyageTaskValidator | — | — |
| L2 PlannedRoute + SpeedProfile 订阅 | ✅ 3 subscribers | — | — |
| NO 航次规划逻辑（仅本地跟踪） | ✅ 合规 | — | — |
| NO 避碰决策（M4/M5 职责） | ✅ 合规 | — | — |
| RouteReplanRequest 触发（4 reason）| ✅ ReplanRequestTrigger | — | — |
| ReplanResponse 处理（RFC-006 4 status）| ✅ ReplanResponseHandler | — | — |
| 7 状态 FSM | ✅ MissionStateMachine | — | — |
| ETA 投影 | ✅ EtaProjector | — | — |

### 详细缺口
M3 是本审计中**最干净的模块**，所有架构要求均已实现。无 P0–P2 缺口。

---

## M4 — Behavior Arbiter (Src: 476 sloc)

### 架构要求 (§8.3)
| 要求 | 代码状态 | 严重度 | 优先级 |
|---|---|---|---|
| 8 行为字典条目 | ❌ 仅 7 条目 | P1 | DEMO-2 |
| 行为 ODD 绑定 | ⚠️ 部分缺失 | P2 | DEMO-2 |
| IvP 多目标优化求解器 | ✅ 自实现 IvPSolver | — | — |
| COLREGs 约束作为硬约束嵌入 IvP | ✅ COLREGsConstraint 输入 | — | — |
| NO 硬编码船型检查 (if vessel ==) | ✅ 合规 | — | — |
| BehaviorPlan 输出 @ 2 Hz | ✅ timer | — | — |

### 详细缺口

#### GAP-M4-001: 行为字典只有 7 条，架构要求 8 条 (P1)
- **架构要求** (§8.3)：8 行为字典：
  - Transit (ODD-A/B, w=0.3)
  - COLREGs_Avoidance (ODD-A/B/D, w=0.7)
  - Restricted_Visibility (ODD-D, w=0.6)
  - Channel_Following (ODD-B, w=0.5)
  - Approach (ODD-C, w=0.4)
  - DP_Hold (ODD-C, w=0.8)
  - Crew_Transfer_Standby (ODD-A/C, w=0.5)
  - MRC_Drift (any, w=1.0)
- **代码实现**：`BehaviorType` 枚举只有 7 个：
  - TRANSIT (0), COLREG_AVOID (1), DP_HOLD (2), BERTH (3), MRC_DRIFT (4), MRC_ANCHOR (5), MRC_HEAVE_TO (6)
- **缺少的行为**：Restricted_Visibility, Channel_Following, Approach, Crew_Transfer_Standby
- **多余的行为**：BERTH (代替 Approach), MRC_ANCHOR, MRC_HEAVE_TO (MRC 不应是 3 个独立行为，架构只定义 1 个 MRC_Drift)
- **文件证据**：`m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_dictionary.hpp:11-18`
- **影响**：
  1. ODD-D（能见度不良）时无法激活 Restricted_Visibility 行为 → 可能在不安全条件下继续全速航行
  2. ODD-B（狭水道）时无法激活 Channel_Following → 违反 Rule 9
  3. ODD-C（港内）时 Approach/Crew_Transfer_Standby 缺失 → 靠泊行为不完整
  4. 三个 MRC 行为在 IvP 中竞争（架构要求仅 1 个 MRC_Drift 权重 1.0）
- **修复**：对齐 8 行为字典。删除冗余 BERTH/MRC_ANCHOR/MRC_HEAVE_TO。添加 Restricted_Visibility, Channel_Following, Approach, Crew_Transfer_Standby。

#### GAP-M4-002: 行为定义可能来自 salvage-d3.1 旧分支 (P2)
- `m4_behavior_arbiter/.salvage-d3.1/include/` 包含旧版头文件，与 `m4_behavior_arbiter/include/` 并行存在。旧版本 (`salvage-d3.1`) 的 `behavior_definitions.yaml` 也定义了 BERTH 等行为。
- **影响**：存在混淆风险。salvage 目录应清理，或标注为已废弃。

---

## M5 — Tactical Planner (Src: 2553 sloc)

### 架构要求 (§10)
| 要求 | 代码状态 | 严重度 | 优先级 |
|---|---|---|---|
| 双层 MPC (Mid-MPC + BC-MPC) | ✅ MidMpcNode + BcMpcNode | — | — |
| Mid-MPC N=18, 步长 5s, 总时域 90s | ❌ N=12 默认 | P1 | DEMO-1 |
| BC-MPC k 分支树 (k=7, δψ=10°) | ✅ k_high=7, delta_psi_rad=10° | — | — |
| ROT_max 从 Capability Manifest 读取 | ✅ CapabilityManifest YAML 加载 | — | — |
| AvoidancePlan (WP[] + speed_adj) @ 1-2 Hz 主接口 | ✅ 实现 | — | — |
| ReactiveOverrideCmd 紧急接口 | ✅ 实现 | — | — |
| FM-4 → safety_concern_event → M7 MRM | ❌ 仅 spdlog 日志 | P0 | DEMO-1 |
| NO FCB 硬编码（多船型） | ✅ CapabilityManifest 模式合规 | — | — |
| 4-DOF MMG 完整模型 (§10.5) | ⚠️ 简化线性模型 | P2 | DEMO-2 |
| TSS (Rule 10) 多边形约束 | ✅ ZoneConstraint | — | — |
| 人工接管 M5 冻结行为 | ⚠️ 未完整验证 | P2 | DEMO-2 |

### 详细缺口

#### GAP-M5-001: safety_concern_event 未实现 (P0 — CRITICAL)
- **架构要求** (§11.6)：M5（Doer）在碰撞紧迫（FM-2）或 ODD 越界（FM-4）等失效场景下，**发送 `safety_concern_event` 至 M7（Checker）**，而**不直接下发 MRM 命令**。M7 作为 Checker 唯一权限者评估事件、仲裁响应。
- **代码实现**：`mid_mpc_solver.cpp:101-104` 在连续求解失败时仅调用 `spdlog::critical("[M5][MidMPC] ... M7 MRM-02 escalation")`。**没有实际的 ROS2 消息发送到 M7**。也没有 `SafetyConcern` 消息类型。
- **文件证据**：
  - `mid_mpc/mid_mpc_solver.cpp:100-131` — 故障时只日志不发送消息
  - `l3_msgs/msg/` 下无 `SafetyConcern.msg` 或类似消息
  - M5 的 publishers 中无 safety_concern 相关
- **影响**：Doer-Checker 指挥链在此断裂。M5 理论上直接升级到 MRM（绕过 M7），违背架构核心安全原则（决策四）。
- **修复**：
  1. 创建 `l3_msgs/msg/SafetyConcernEvent.msg`（含 failure_mode 枚举 + context）
  2. M5 MidMpcNode 添加 publisher
  3. M7 SafetySupervisorNode 添加 subscriber
  4. 故障时：M5 发 SafetyConcernEvent → M7 评估 → M7 发 SafetyAlert → M1 仲裁 MRM

#### GAP-M5-002: Mid-MPC n_horizon 默认值 = 12 ≠ 架构要求 N=18 (P1)
- **架构要求** (§10.3)：N=18（预测步数，步长 5s，总时域 90s）。
- **代码实现**：`mid_mpc_nlp_formulation.hpp:67` — `int32_t n_horizon{12}`。时域 = 12×5s = 60s，非 90s。
- **文件证据**：`m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp:67`
- **影响**：缩短的时域可能导致 Mid-MPC 无法在 60s 范围内规划足够远的避让轨迹，尤其对于高速 FCB 船型（18 kn = ~9.26 m/s，60s 行程 ~555m），低于 COLREGs 要求的"大幅早行动"时间窗口。
- **修复**：改为 `int32_t n_horizon{18}` 对齐架构要求。如果计算性能吃紧，通过在 [TBD-HAZID] 配置中可调。

#### GAP-M5-003: 4-DOF MMG 模型为简化线性近似 (P2)
- **架构要求** (§10.5)：45m FCB 在高速段须使用 Yasukawa & Yoshimura (2015) 的**完整 4-DOF MMG 模型**，含舵效降低修正、制动性能建模、波浪扰动模型。
- **代码实现**：`vessel_dynamics_model.hpp:69` 注释说明 `"Simplified linear model: exact non-linear MMG awaits pool-test data [TBD-HAZID]"`。
- **文件证据**：`m5_tactical_planner/include/m5_tactical_planner/shared/vessel_dynamics_model.hpp:68-69`
- **影响**：高速段（>15 kn）的操纵预测不准确，可能影响 MPC 轨迹质量和 COLREGs 合规性。
- **修复**：Phase E2 根据 FCB 水池/实船数据实现完整非线性 4-DOF MMG。

#### GAP-M5-004: Mid-MPC 默认艏向参数使用固定值 (P2)
- `mid_mpc_node.hpp:34-36` — `own_ship_lat_deg{30.0}` / `own_ship_lon_deg{122.0}` 是硬编码的 FCB 母港默认值。注释说 "Replace with dynamic own-ship state in Phase E2"。
- **影响**：当动态自身状态可用前，Mid-MPC 的 NED 转换使用固定原点，在远离母港的区域有定位误差。

---

## M6 — COLREGs Reasoner (Src: 1694 sloc)

### 架构要求 (§9)
| 要求 | 代码状态 | 严重度 | 优先级 |
|---|---|---|---|
| 5 层推理架构 | ✅ 实现 | — | — |
| 11 条规则 (5-8, 13-19) | ✅ 全部实现 | — | — |
| ODD-aware 参数切换 | ✅ odd_aware_thresholds.yaml | — | — |
| 5 层均含独立单元测试 | ✅ 16 test files | — | — |
| Rule 9 (狭水道) 推理 | ⚠️ 仅权重参数 | P2 | DEMO-2 |
| CEVNI 内河规则插件接口 | ✅ CEVNI 基础架构 | — | — |
| COLREGsConstraint 输出 | ✅ 含 conflict_detected | — | — |

### 详细缺口

#### GAP-M6-001: Rule 9 (Narrow Channel / TSS) 未作为独立规则实现 (P2)
- **架构要求** (§9.2)：ODD-B 时 Rule 9 应有"额外权重"。ODD-A/B 时 Rule 9（狭水道）是 ODD-B 主导规则。
- **代码实现**：`types.hpp` 中有 `rule_9_weight` 字段，但在 `colregs_reasoner_node.cpp:181` 和 `:629` 处硬编码为 `0.0`。**没有独立的 Rule 9 推理类**。Rule 9 的 COLREGs 合规推理由 M2 的 ZoneConstraint + M6 的权重参数间接处理。
- **文件证据**：
  - `m6_colregs_reasoner/include/m6_colregs_reasoner/types.hpp:46`
  - `m6_colregs_reasoner/src/colregs_reasoner_node.cpp:181,629`
  - Rule 9 没有对应的 `src/rules/colregs/rule9_*` 文件
- **影响**：狭水道场景下系统可能无法正确推理 Rule 9 的"靠右航行"和"不得妨碍深水航道"义务。
- **修复**：实现 `Rule9_NarrowChannel` 类，使用 M2 WorldState 的 `zone_type/in_narrow_channel` 字段。

#### GAP-M6-002: conflict_detected 字段未充分使用 (P3)
- **架构要求**：`COLREGsConstraint.msg` 的 `bool conflict_detected` 应被 M4 和 M8 用于触发 SAT-2 展示和避让优先级提升。
- **代码实现**：constraint 生成器中 `conflict_detected` 字段存在但 SAT-2 触发逻辑和 IvP 约束处理中对该字段的使用未显式验证。

---

## M7 — Safety Supervisor (Src: 2007 sloc)

### 架构要求 (§11)
| 要求 | 代码状态 | 严重度 | 优先级 |
|---|---|---|---|
| NO M1-M6 header includes | ✅ CI 0 violation | — | — |
| 预定义 MRM 命令集 (4 种) | ✅ MrmCommandSet | — | — |
| SOTIF 假设违反监控 | ✅ AssumptionMonitor | — | — |
| 双轨 (IEC 61508 + SOTIF) | ✅ 两条轨道 | — | — |
| 15s 滑窗 (RFC-003) | ✅ SlidingWindow15s | — | — |
| CheckerVetoNotification enum 处理 | ✅ VetoHandler | — | — |
| Doer-Checker SLOC 比 ≥ 100:1 | ❌ 0.79:1 | P0 | DEMO-1 |
| M5 safety_concern_event → M7 | ❌ 缺失 | P0 | DEMO-1 |

### 详细缺口

#### GAP-M7-001: M7 复杂度过高 — Doer-Checker SLOC 比远低于要求 (P0 — CRITICAL)
- **架构要求** (§2.5 决策四)：Checker（M7）的逻辑必须比 Doer 简单 **100×** 以上（"100×"措辞来自 ADR-001 修订，SLOC 比 ≥ 100:1 是认证级目标）。
- **实际数据**：
  - M7（Checker）: 2007 sloc
  - M5（代表 Doer）: 2553 sloc
  - 比例: **0.79:1**（即 M7 几乎和 M5 一样大）
- **与 Doer 对比**：按架构要求 M7 应 ≤ 25 sloc（如果 M5=2553 为基准）。2007 sloc 是要求的 **80× 以上**。
- **影响**：
  1. CCS i-Ship SIL 2 认证面临风险 — Checker 与 Doer 的复杂度比是认证审查关键指标
  2. M7 本身可能包含未发现的 bug（复杂 Checker 失去"可形式化验证"优势）
  3. Polyspace Code Prover 分析 2000+ sloc 的安全关键代码覆盖难度大
- **修复**：
  1. **立即**：将 M7 中非安全关键逻辑移出（如 ASDR 记录器、SAT 数据发布 → M8）
  2. **短期**：将 SOTIF 假设监控中的"复杂推理"部分简化，改为由 M2/M6 提供预评估结论，M7 只做阈值比较
  3. **长期**：重构 M7 为真正轻量的 Checker（~50-100 sloc 纯逻辑），将监控细节下沉到各 Doer 模块的 self-diagnostics

#### GAP-M7-002: safety_concern_event 未实现 (P0 — CRITICAL)
- 与 GAP-M5-001 相同。M7 缺少接收 M5 `safety_concern_event` 的接口。
- 目前 M5 故障时仅写日志，M7 对此完全不知。
- **修复**：参见 GAP-M5-001 修复方案。M7 端需添加 subscriber 和处理逻辑。

#### GAP-M7-003: 降级监测线程在接管期间的实现 (P1)
- **架构要求** (§11.9.1)：接管期间 M7 暂停主仲裁但**保留降级监测线程**，出现通信中断/传感器降质/新威胁时通过 M8 显示告警。
- **代码实现**：`safety_supervisor_node.hpp` 和 `safety_supervisor_node.cpp` 中的 `on_override_signal` 回调需要确认是否真的保留了降级监测。从节点设计看主循环 `on_main_loop_tick()` 在 override 时可能跳过评估。
- **影响**：接管期间可能出现安全监控真空间隙。
- **修复**：在 override 路径中明确实现两条路径：(a) 暂停主仲裁、(b) 保留降级监测线程。

---

## M8 — HMI/Transparency Bridge (Src: 1157 sloc)

### 架构要求 (§12)
| 要求 | 代码状态 | 严重度 | 优先级 |
|---|---|---|---|
| SAT-1/2/3 三层聚合 | ✅ SatAggregator | — | — |
| 自适应触发 (§12.2) | ✅ AdaptiveSatTrigger 4 conditions | — | — |
| ToR 协议（IMO MASS Code C 交互验证）| ✅ TorProtocol (IDLE→REQUESTED→ACK→TIMEOUT) | — | — |
| SAT-1 ≥ 5s 强制等待 | ✅ sat1_min_display_s{5.0} | — | — |
| 60s ToR 超时 → MRC | ✅ deadline_s{60.0} | — | — |
| ASDR 日志记录含 SHA-256 | ✅ AsdrLogger + Sha256 | — | — |
| 模块健康监控 | ✅ ModuleHealthMonitor | — | — |
| 差异化视图（角色×场景）| ⚠️ UiStateBuilder 实现 | P3 | DEMO-2 |

### 详细缺口

#### GAP-M8-001: ModuleHealthMonitor 只覆盖 5/8 模块 (P2)
- **架构要求** (§12)：M8 应对所有上游模块（M1–M7）进行健康监控。
- **代码实现**：`SatAggregator::SourceModule` 枚举只有 M1, M2, M4, M6, M7（5 个），缺少 M3（Mission Manager）和 M5（Tactical Planner）。
- **文件证据**：`m8_hmi_transparency_bridge/include/m8_hmi_transparency_bridge/sat_aggregator.hpp:25-32`
- **影响**：若 M3 或 M5 失效，M8 无法向上报告其心跳丢失，ROC 无法获得"M3/M5 异常"告警。
- **修复**：在 `SourceModule` 枚举中添加 kM3 和 kM5；`ModuleHealthMonitor::Thresholds` 添加对应超时；M8 节点订阅 M3 的 ASDR 或专用心跳主题。

---

## 跨模块反模式检查

### 反模式 1: FCB/45m 硬编码 (§13.1 — 禁止)

| 搜索模式 | 路径 | 结果 |
|---|---|---|
| `FCB\|45m\|18 kn\|22 kn` 在源码 (非配置/测试/注释) | 🚫 **ZERO 命中** | ✅ 合规 |
| `if.*vessel.*==` 在 C++ 源码 | 🚫 **ZERO 命中** | ✅ 合规 |
| `switch.*vessel\|case.*FCB` 在 C++ 源码 | 🚫 **ZERO 命中** | ✅ 合规 |

### 反模式 2: 船型分支判断 — 结果

| 搜索模式 | 结果 |
|---|---|
| `if.*vessel.*=` 在所有源码 | ✅ 仅合法使用（Capability Manifest vessel_id 读取） |
| `if.*ship.*=` 在所有源码 | ✅ 仅合法使用（`classification = "vessel"` 等） |

### 反模式 3: TypeScript 抑制注释 — 结果

| 搜索模式 | 结果 |
|---|---|
| `as any` | ✅ 0 命中（C++ 项目，不适用） |
| `@ts-ignore` | ✅ 0 命中 |
| `@ts-expect-error` | ✅ 0 命中 |

### 反模式 4: M7 Doer-Checker 独立性

| 检查项 | 结果 |
|---|---|
| `#include.*m[1-6]_` 在 M7 src | 🚫 **ZERO 命中** ✅ |
| `#include.*m[1-6]_` 在 M7 include | 🚫 **ZERO 命中** ✅ |

---

## 场景 YAML 与 ODD 对齐检查

### 检查文件
- `scenarios/IMAZU标准测试/imazu-01-ho.yaml`
- `scenarios/IMAZU标准测试/imazu-02-cr-gw.yaml`
- `scenarios/COLREGs测试/colreg-rule14-ho.yaml`
- `scenarios/COLREGs测试/colreg-rule15-cs.yaml`

### 结果

| 检查项 | imazu-01-ho | imazu-02-cr-gw | colreg-rule14-ho | colreg-rule15-cs |
|---|---|---|---|---|
| `metadata.vessel_class` | FCB | FCB | FCB | FCB |
| `metadata.odd_cell.domain` | `open_sea_offshore_wind_farm` | `open_sea_offshore_wind_farm` | `open_sea_offshore_wind_farm` | `open_sea_offshore_wind_farm` |
| ODD-A/B/C/D 映射 | ⚠️ 无直接映射 | ⚠️ 无直接映射 | ⚠️ 无直接映射 | ⚠️ 无直接映射 |
| `encounter.rule` | Rule14 ✅ | Rule15 ✅ | Rule14 ✅ | Rule15_Stbd ✅ |

### 详细缺口

#### GAP-SCENARIO-001: 场景 ODD 域未映射到架构 ODD-A/B/C/D 分类 (P2)
- **架构要求** (§3.3)：4 个 ODD 子域 — ODD-A（开阔水域）、ODD-B（狭水道/VTS）、ODD-C（港内/靠泊）、ODD-D（能见度不良）。
- **场景实现**：所有场景使用 `odd_cell.domain: open_sea_offshore_wind_farm`，与架构的 ODD-A/B/C/D 分类**无直接映射**。也缺少明确的 `odd_cell.category: ODD_A` 等字段。
- **影响**：
  1. 场景 runner 无法从 YAML 直接推断应使用的 ODD 子域参数
  2. 所有现有场景仅覆盖 ODD-A 开阔水域
  3. **没有 ODD-B、ODD-C、ODD-D 的场景**
  4. 1100-cell 覆盖立方体中的 ODD 维度无法充填

#### GAP-SCENARIO-002: 缺少 ODD-B/C/D 多域场景 (P1)
- 目前所有 33 个场景都是开阔水域（ODD-A 等价）。狭水道（ODD-B）、港内（ODD-C）、能见度不良（ODD-D）场景**完全缺失**。
- **影响**：M1 的 ODD-B/C/D 状态机转换、M6 的 ODD-aware 参数切换、M4 的 ODD 绑定行为激活无法在仿真中验证。
- **修复**：D1.6 阶段须为每个 ODD 子域创建 ≥3 个代表性场景。

---

## 缺口分级总结

### P0 — DEMO-1 Critical（必须修复，否则 DEMO-1 安全评估不可通过）

| # | 模块 | 缺口 | 类型 |
|---|---|---|---|
| M5-001 | M5→M7 | safety_concern_event 未实现 — Doer-Checker 断开 | 安全关键 |
| M7-001 | M7 | SLOC 比 0.79:1 远低于 100:1 要求 | 认证关键 |
| M7-002 | M7→M5 | safety_concern_event 接收端缺失 | 安全关键 |

### P1 — DEMO-1/DEMO-2 Important（功能完整性）

| # | 模块 | 缺口 | 类型 |
|---|---|---|---|
| M1-001 | M1 | Capability Manifest 加载位置错误（在 M5 不在 M1）| 架构合规 |
| M4-001 | M4 | 行为字典 7/8 条目（缺 Restricted_Visibility 等 4 条）| 功能完整 |
| M5-002 | M5 | Mid-MPC N=12 非 18，时域 60s 非 90s | 功能完整 |
| M7-003 | M7 | 接管期间降级监测线程实现需确认 | 安全完整 |
| GAP-SCENARIO-002 | 场景 | 缺 ODD-B/C/D 场景 | 测试完整性 |

### P2 — DEMO-2/Phase3 Improvement（质量提升）

| # | 模块 | 缺口 | 类型 |
|---|---|---|---|
| M1-002 | M1 | ODDState.msg 缺 OVERRIDDEN 枚举值 | 接口完整 |
| M2-001 | M2 | 缺少 per-target 分分量置信度 (pos/vel/intent) | 功能完整 |
| M5-003 | M5 | 4-DOF MMG 为简化线性模型 | 算法精度 |
| M5-004 | M5 | Mid-MPC 固定默认坐标 | 实现质量 |
| M6-001 | M6 | Rule 9 未独立实现 | 规则完整 |
| M8-001 | M8 | 健康监控缺 M3/M5 | 监控完备 |
| GAP-M1-003 | M1 | 参数 YAML 与 Capability Manifest 不同步 | 维护质量 |
| GAP-SCENARIO-001 | 场景 | ODD 域映射缺失 | 测试质量 |
| GAP-M4-002 | M4 | salvage-d3.1 旧代码残留 | 工程整洁 |

---

## 摘要统计

| 严重度 | 数量 | 主要影响 |
|---|---|---|
| P0 (Critical) | 3 | DEMO-1 安全评估、认证信号完整性 |
| P1 (Important) | 6 | DEMO-1/DEMO-2 功能完整性 |
| P2 (Improvement) | 7 | DEMO-2/Phase3 质量提升 |
| P3 (Minor) | 2 | 后续迭代 |
| **总计** | **18** | |

### 关键行动建议

1. **立即 (D1.x sprint)**：
   - 实现 safety_concern_event 消息 + M5→M7 通信链路（GAP-M5-001 / GAP-M7-002）
   - M7 SLOC 评估：确认真实认证要求（100× 是否认证硬性要求），如需要则启动简化重构
   - 修复 M4 行为字典对齐 8 架构要求（GAP-M4-001）
   - M5 Mid-MPC N=12→18（GAP-M5-002）

2. **DEMO-2 前**：
   - Capability Manifest 加载位置对齐到 common/ 或 M1（GAP-M1-001）
   - ODD_Cell → ODD-A/B/C/D 映射（GAP-SCENARIO-001）
   - 补 ODD-B/C/D 场景 + Rule 9 独立实现
   - ODDState.msg OVERRIDDEN 枚举补充

3. **Phase 3 前**：
   - M2 per-target 三维置信度
   - 4-DOF MMG 完整非线性实现
   - M8 ModuleHealthMonitor 补充 M3/M5

---

*报告完毕 — 2026-05-19*
