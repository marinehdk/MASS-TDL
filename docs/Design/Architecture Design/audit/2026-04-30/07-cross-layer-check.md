# 阶段 5.2 · 跨层对照（vs `docs/Init From Zulip/` + v3.0 Kongsberg）

> 验证 v1.0 §15 接口契约总表与各跨层子系统（L1/L2/L4/L5/Multimodal Fusion/Deterministic Checker/ASDR/Hardware Override Arbiter）的实际设计是否一致。
>
> 完成时间：2026-05-05
>
> 对照源：`docs/Init From Zulip/MASS ADAS *` 各层技术设计文档 + CLAUDE.md §2 v3.0 Kongsberg 基线

---

## §15 接口契约总表 vs 各层实际契约

### 1. 接口契约对照矩阵

| 发布者 → 订阅者 | v1.0 §15.2 | 实际跨层设计 | 状态 |
|---|---|---|---|
| **Multimodal Fusion → M2** | "World_StateMsg @ 4 Hz, M2 → M3/M4/M5/M6（含目标列表 + 自身状态 + ENC）" | `TrackedTargetArray` @ **2 Hz** + `FilteredOwnShipState` @ **50 Hz** + `EnvironmentState` @ 0.2 Hz **三个独立话题** | ❌ **frequency 不匹配 + schema 拆分 + CPA/TCPA 缺失** |
| **L1 Voyage Order → M3** | 未在 §15 列出（v1.0 §7 称"L2 战略层"） | `VoyageTask`（事件触发，含 departure/destination/eta_window/optimization_priority/exclusion_zones） | ❌ v1.0 完全未承认 L1 入口 |
| **L2 WP_Generator + Speed_Profiler → M3/M5** | 未在 §15 详列（v1.0 称"L2 航次计划"） | `PlannedRoute`（含 wp_distance、turn_radius、safety_corridor 等）+ `SpeedProfile`（含 phase=accel/cruise/decel、constraint_source）；规划时（1 Hz 或更低）| ⚠️ 与 v1.0 抽象描述不一致 |
| **M5 → L4** | "Trajectory_CmdMsg @ 10 Hz, M5 → **L2** | (ψ_cmd, u_cmd, ROT_cmd)" | L4 自己生成 (ψ_cmd, u_cmd, ROT_cmd) via LOS+WOP，**消费 L2 PlannedRoute + SpeedProfile**，不直接接收 M5 的轨迹三元组 | ❌ **层级架构错位**（不仅是 §15 误标 L2 → L4） |
| **L4 → L5** | 未在 §15.2 矩阵列出 | L4 (LOS + WOP) 输出 (ψ_cmd, u_cmd, ROT_cmd) → L5 Heading_Controller / Speed_Controller / Thrust_Allocator | ⚠️ §15 矩阵漏行 |
| **M7 ↔ X-axis Deterministic Checker** | 未定义 | Checker（独立 L2/L3/L4/L5 四层）输出 `CheckerOutput {approved, nearest_compliant, veto_reason}`；M7 双轨 IEC 61508 + SOTIF；**两者无相互通知协议** | ❌ **协调机制缺失（即 F-P0-D3-002）** |
| **L3 → ASDR** | 未在 §15 列出 | ASDR doc §3.2 + §13 定义订阅 COLREGs / Avoidance / Risk / Checker / Target Tracker（事件 + 2 Hz）；JSON schema `AiDecisionRecord` | ❌ §15 接口契约不闭包 |
| **Reflex Arc → L5（旁路 L3/L4）** | 未在 §15 列出 | Hardware Override Arbiter §3.1 P2 优先级通过 DDS 到 L5；**L3 是否被通知未定义；触发阈值未量化（"<500 ms"无 CPA/距离阈值）** | ❌ 跨层协议设计不完整 |
| **L3 → L2 反向（重规划请求）** | 未在 §15 列出 | 实际需求：M3 重规划触发须能向 L2 发起 `route_replan_request`；早期稿（裁决汇总 §B-01）已识别 | ❌ 缺失通道 |
| **Hardware Override Arbiter → L3** | 未定义 | Arbiter §7.1 通知 L3 `override_active`；L3 应"暂停所有 AI 控制输出"；M5 是否冻结 vs 继续运行未定义 | ⚠️ 协调不充分 |

### 2. 关键审计点深度分析

#### 2.1 M2 输入 vs Multimodal Fusion 输出（F-P1-D4-031）

**实际 Fusion 输出**：
```
TrackedTargetArray @ 2 Hz (/perception/targets):
  TrackedTarget[]: track_id, mmsi, lat/lon, cog/sog, heading/rot,
                   length/beam, position/velocity_covariance,
                   confidence, classification, source_sensors

FilteredOwnShipState @ 50 Hz (/nav/filtered_state):
  lat/lon, sog/cog, u/v (船体), heading/yaw_rate, roll/pitch,
  position/heading_uncertainty, nav_mode, current_speed/direction

EnvironmentState @ 0.2 Hz (/perception/environment):
  visibility, sea_state, traffic_density, zone_type/in_tss,
  SensorCoverage, BlindSector
```

**v1.0 期望**：World_StateMsg @ 4 Hz 包含三方信息 + CPA/TCPA 预计算（§6.3 行 470–471）。

**差异**：
1. **频率**：实际 Fusion 给的是 2 Hz；v1.0 期望 4 Hz。M2 输出频率受限于源数据，可能影响 M5 决策刷新率。
2. **CPA/TCPA 缺失**：实际 Fusion 输出**不包含** CPA/TCPA。v1.0 §6.3 假设 M2 计算 CPA/TCPA 用于 COLREG 预分类，但源数据本身无此字段——M2 须自行计算（这是合理的，但 §15 未明示）。
3. **自身状态分离**：FilteredOwnShipState @ 50 Hz vs World_StateMsg @ 4 Hz 频率差异显著；M2 须做时间对齐策略。
4. **ENC 约束分散**：在 EnvironmentState 而非 World_StateMsg；M2 须从两个话题聚合。
5. **坐标系不明**：v1.0 未指定对地速度 vs 对水速度选用，可能影响 M5 操纵模型输入。

#### 2.2 M5 → L4 层级架构错位（F-P1-D4-032，extends F-P1-D5-012）

**v1.0 §10 + §15 描述**：M5 输出 `(ψ_cmd, u_cmd, ROT_cmd)` @ 10 Hz 直接送下游（v1.0 §15 错写 L2，本应 L4）。

**实际 L4 设计**：
- L4 订阅 L2 的 `PlannedRoute` + `SpeedProfile`
- L4 内部 LOS Guidance + WOP Module 自行生成 `(ψ_cmd, u_cmd, ROT_cmd)` @ 10 Hz → L5
- **L4 不消费 M5 的轨迹三元组**

**根本问题**：
- 如果 v1.0 M5 仍坚持输出 (ψ, u, ROT) @ 10 Hz，与 L4 LOS+WOP 输出**重复 / 冲突**——谁的指令最终到 L5？
- 如果 v1.0 M5 实际应输出**避让 WP 偏移 + 速度调整**（即 CLAUDE.md §2 描述的"Avoidance WP + speed adj"），则 M5 的输出 schema 需要重新定义，不是 (ψ, u, ROT)。

**建议方向**：
- **方案 A（保留 v1.0）**：M5 输出 (ψ, u, ROT) @ 10 Hz，L4 在避让模式下旁路自身 LOS、直接转发 M5 指令。需 L4 支持模式切换（normal LOS vs avoidance bypass）。
- **方案 B（与 v3.0 对齐）**：M5 输出 `Avoidance_WP + speed_adj`，L4 在避让模式下消费这些覆盖原 PlannedRoute。需 v1.0 §10 + §15 重写 M5 输出 schema。

#### 2.3 M7 vs X-axis Deterministic Checker（F-P0-D3-002 强化 + F-P2-D3-036）

**Checker 实际设计**（`MASS_ADAS_Deterministic_Checker_技术设计文档.md`）：
- 分 L2/L3/L4/L5 四层独立 Checker
- L3 Checker 订阅 ColregsDecision + AvoidanceCommand + OwnShipState + EnvironmentState
- 输出 `CheckerOutput {approved, nearest_compliant, veto_reason}`
- VETO 时替代 Doer 输出（不是仅"通知"，是直接覆盖）
- T_check_max 内完成校验，超时则维持上一周期指令

**M7 实际设计**（v1.0 §11）：
- L3 内部双轨：IEC 61508（功能安全）+ SOTIF（功能不足）
- 含 ARBITRATOR 直接向 M5 注入安全轨迹（F-P0-D3-001 已识别违规）
- 触发 MRC 请求 → M1

**两者冲突点**：
1. **VETO 重叠无仲裁**：Checker 否决 M5 轨迹的同时，M7 ARBITRATOR 也可注入轨迹——若两者都激活，谁优先？
2. **信息流单向**：Checker 否决事件**未通知 M7**——M7 无法将"Checker 否决率"作为 SOTIF 假设违反指标
3. **M7 SOTIF 假设违反检测（v1.0 §11.3）**未涵盖"L3 Checker 否决率 > 阈值"这一明显信号

**修订需求（已被 F-P0-D3-002 涵盖，新增子项 F-P2-D3-036）**：
- 在 §11.3 假设违反清单加 "L3 Checker 否决率 > 20%" → 升级 SOTIF 告警
- 在 Checker doc §17 加"Checker → M7 否决事件通知"消息

#### 2.4 ASDR 接口（F-P1-D4-033）

**实际 ASDR 设计**：ASDR 文档 §3.2 + §13 明确订阅来自 L3 各模块的事件：
- COLREGs Engine：会遇分类、责任判定、行动推荐（2 Hz）
- Avoidance Planner：避让 WP 生成（事件）
- Risk Monitor：状态机转换、CPA 趋势（事件 + 2 Hz）
- Checker：校验结果（与各层同频）
- Target Tracker：威胁评分（2 Hz）
- 消息 schema：`AiDecisionRecord` JSON（含 timestamp / module / decision_type / inputs / decision / checker_result / signature）

**v1.0 §15.2 矩阵**：完全未列 ASDR，对应"接口契约总表"原则违规（任何系统交互应在 §15 显式定义）。

**修订建议（finding F-P1-D4-033）**：
v1.0 §15.1 IDL 增加：
```
# ASDR_RecordMsg (发布者: 所有 L3 模块, 频率: 事件 + 2 Hz)
message ASDR_RecordMsg {
    timestamp   stamp;
    string      source_module;     // "COLREGs_Engine"|"Risk_Monitor"|...
    AiDecision  decision;
    string      decision_json;     // JSON 序列化（向后兼容 ASDR §5.2）
    bytes       signature;         // SHA-256 防篡改
}
```
§15.2 矩阵增加 4–5 行：M2/M4/M6/M7 → ASDR。

#### 2.5 Reflex Arc 设计不完整（F-P1-D4-034）

**实际 Hardware Override Arbiter 设计**：
- §3.1 P2 优先级（Emergency Reflex Arc）通过 DDS 到 L5
- §14.1 提到 "<500 ms" 旁路时延要求
- **触发条件未量化**：无 CPA 阈值、无距离阈值、无时间窗
- **感知输入源不明**：是 Multimodal Fusion 极近距离检测？独立 LiDAR emergency brake？
- **L3 协调缺失**：L3 是否被通知 Reflex Arc 激活？M5 应否暂停？无定义

**v1.0 §15.2 矩阵**：完全未列 Reflex Arc。

**修订建议（finding F-P1-D4-034）**：
1. 跨团队补充 Reflex Arc 设计 spec（量化触发阈值 / 感知源 / 时序）
2. v1.0 §15.1 增加：
```
# EmergencyCommand (发布者: Reflex Arc → L5, 频率: 事件)
message EmergencyCommand {
    timestamp   trigger_time;
    float32     cpa_at_trigger_m;
    float32     range_at_trigger_m;
    string      sensor_source;
    enum action; // STOP|REVERSE|HARD_TURN
    float32     confidence;
}

# ReflexActivationNotification (发布者: Reflex Arc → L3 (M1), 频率: 事件)
message ReflexActivationNotification {
    timestamp   activation_time;
    string      reason;
    bool        l3_freeze_required;
}
```

#### 2.6 L3 → L2 反向通道缺失（F-P1-D4-035）

**实际需求**：
- 当 M1 检测 ODD 越界且 M3 触发重规划时，L3 须能向 L2 发起 `route_replan_request`
- 当 M5 持续无法生成可行轨迹时，应升级请求 L2 重新规划
- 早期稿（`L3 实现框架裁决汇总.md` §B-01）已识别此通道缺失

**v1.0 §15.2 矩阵**：完全无 L3 → L2 反向消息。

**修订建议（finding F-P1-D4-035）**：
v1.0 §15.1 增加：
```
# RouteReplanRequest (发布者: M3, 订阅者: L2, 频率: 事件)
message RouteReplanRequest {
    timestamp   stamp;
    enum reason; // ODD_EXIT|MISSION_INFEASIBLE|MRC_REQUIRED|CONGESTION
    float32     deadline_s;
    string      context_summary;
    Position    current_position;
    Position[]  exclusion_zones; // 须避开区域（如冲突 TSS）
}
```

---

## 关键审计 Finding 摘要

> 12 条新 finding 同步到 `_temp/findings-raw.md`（编号继续 030 之后的全局序列）

| ID | 严重 | 概述 |
|---|---|---|
| F-P1-D4-031 | P1 | M2 ← Multimodal Fusion 频率不匹配（2 vs 4 Hz）+ schema 拆分（3 话题）+ CPA/TCPA 缺失 |
| F-P1-D4-032 | P1 | M5 → L4 层级架构错位（L4 自生 ψ/u/ROT，M5 输出与 L4 重叠 / 冲突） |
| F-P1-D4-033 | P1 | §15 矩阵缺 ASDR 接口（实际 ASDR doc 已订阅 L3 多模块） |
| F-P1-D4-034 | P1 | §15 矩阵缺 Reflex Arc 协议 + L3 通知机制（触发阈值未量化） |
| F-P1-D4-035 | P1 | §15 矩阵缺 L3 → L2 反向 route_replan_request 通道 |
| F-P2-D3-036 | P2 | M7 ↔ Checker 信息流缺失（Checker 否决率应作 M7 SOTIF 输入） |
| F-P2-D6-037 | P2 | Hardware Override 激活时 M5 冻结 vs 继续运行未定义 |
| F-P2-D4-038 | P2 | M3 上游漏 L1 VoyageTask 消费（v1.0 §7 仅称"L2"） |
| F-P2-D1-039 | P2 | Stage 1–4 → Module M1–M8 映射文档化缺失（术语漂移） |
| F-P2-D9-040 | P2 | 多目标冲突消解算法（CSP）从早期 spec 删除，v1.0 M4 仅 IvP |
| F-P2-D9-041 | P2 | TSS Rule 10 多边形约束在 §10 MPC 缺失（早期裁决 §B-02 已识别） |
| F-P3-D9-042 | P3 | Vessel Dynamics Plugin 在 FCB 高速段无在线 GP/SI 参数辨识（早期 spec §3.3） |

---

## 总结

**阶段 5.2 整体观察**：

1. **§15 接口契约总表完整性是 v1.0 最大的跨层缺口**：5 条 P1 finding（031/033/034/035 + 032）全部围绕 §15 矩阵缺失或错位展开。这是认证层（CCS i-Ship 白盒可审计）必须修复的——因为审查员追问"系统接口完整性"时，§15 是唯一答卷。

2. **M5 → L4 层级错位是 F-P1-D5-012 的深化**：原 finding 仅识别 "L2 应为 L4" 文字错误；阶段 5 跨层对照发现实际 L4 已自行生成 (ψ, u, ROT)，**v1.0 M5 输出可能是与 L4 重叠的冗余指令**。此问题须在 v1.1 patch 时与 L4 团队对齐架构。

3. **F-P0-D3-002（M7 vs X-axis Checker 协调缺失）通过外部文档实证强化**：实际 Deterministic Checker 文档 §1.3 + §17 已定义独立的 VETO 协议但完全无 M7↔Checker 桥接。F-P0-D3-002 升级为"已确认 P0"（不再是仅基于 CLAUDE.md §2 的推断）。

4. **跨文档（5.1）+ 跨层（5.2）合计新生 18 条 finding**（5.1 = 7 条；5.2 = 12 条；其中 F-P2-D3-036 是 5.1+5.2 共同识别）：
   - P1：5 条（全部来自 5.2 §15 接口缺口）
   - P2：8 条
   - P3：1 条

5. **Phase 6 决策树预测仍走 A 路径（v1.1 patch）**：阶段 5 新增 finding 全部可通过 v1.1 patch 修复（接口契约补充 + 协调协议补充），无需 v2.0 替代或 C 路径重写。
