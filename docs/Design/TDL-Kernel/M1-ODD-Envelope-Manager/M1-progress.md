# M1 · Progress · 实现现状

> **定位声明**：本文是 M1 的**实现现状**，对照 [M1-spec.md](M1-spec.md) 的设计目标。所有偏离 / 创可贴 / MOCK 记录于此并附 file:line。审计基线 `docs/Doc From Claude/2026-06-08-m1-m8-systemwide-gap-audit.md`（本模块条目已并入下表，但以**当前代码**为准）。

---

## 1. 头部

| 维度 | 说明 |
|---|---|
| 最近更新 | 2026-06-08 |
| Currently Implementing | D2.7（FMEDA M1 ≥20 失效模式；安全工程师外包）|
| 当前分支 | fix/m5-nlp-convergence（主要改动在 M6；M1 无本次 diff）|
| 当前 LOC | ~1669（odd_envelope_manager_node.cpp） |
| 数据更新规则 | PR 合并涉及 M1 时同步更新本表 |

---

## 2. 实现状态矩阵

| 设计职责（对应 spec §） | 状态 | 证据 file:line | 备注 |
|---|---|---|---|
| §4.1-1 Capability Manifest 加载（DDS 订阅）| PARTIAL | `parameter_loader.cpp:171-182` | ROT_max 从 YAML 静态加载；无 DDS 订阅；运行时参数更新路径缺失 |
| §4.1-2 E/T/H 三轴评分 + EMA 平滑 | REAL | `odd_envelope_manager_node.cpp:806-808`；`parameter_loader.cpp:95-116` | 三轴权重 w_e/w_t/w_h 从 YAML 加载；EMA tau 参数化；实测 A4000 运行 |
| §4.1-3 TMR/TDL 计算 + ToR 自适应矩阵（4 场景）| REAL | `parameter_loader.cpp:157-168`；`tmr_tdl_estimator.cpp`；`odd_envelope_manager_node.cpp:816` | current_operator_state_ 传入 compute()；表值来自 YAML |
| §4.1-4 ODD FSM 六状态（In/Edge/Out/MrCPrep/MrCActive/Overridden）| REAL | `odd_state_machine.cpp:164-201 compute_next()`；`:217-280 step()`；每 250ms 调用 | Zone C 收紧代码存在（`:174`）但不可达（zone 冻结 A）|
| §4.1-5 状态转移动作（mode_cmd + odd_state_event + asdr）| REAL | `odd_envelope_manager_node.cpp:717-737 handle_state_change()` | mode_cmd schema_version 未赋值（见缺陷表）|
| §4.1-6 ToR 触发（TDL≤TMR → MrCPrep + ToRRequest）| PARTIAL | `odd_envelope_manager_node.cpp:890-912` | 触发逻辑真实；但 `/l3/m1/tor_request` 无任何消费者（见缺陷表）|
| §4.1-7 M3 Stale Watchdog | REAL | `odd_envelope_manager_node.cpp:819-855` | anchor_hdg 硬编 0.0F（LOW 缺陷）|
| §4.1-8 CMM 三接口（SAT-1/2/3）| REAL | `odd_envelope_manager_node.cpp:1030-1051 publish_sat_data()` | 10 Hz 发布；sat1.active_alerts 硬编空列表（见缺陷表）|
| §3.1 /l3/m7/safety_alert 订阅 | REAL | `odd_envelope_manager_node.cpp:378-384, 796-800` | severity CRITICAL/MRC_REQUIRED 驱动 FSM |
| §3.1 /l3/m7/heartbeat 订阅 | REAL | `odd_envelope_manager_node.cpp:386-391, 522-534, 867-873` | 500ms 超时机制正常 |
| §3.1 X-axis /l3/checker/veto 订阅（硬 VETO 路径）| MISSING | M1 initialize_subscribers()（372-470 行范围）无 CheckerVetoNotification | M7 内部消费 `/l3/checker/veto`（`safety_supervisor_node.cpp:174-177`）但不转发给 M1 |
| §3.1 /l3/m8/operator_state 订阅 | REAL | `odd_envelope_manager_node.cpp:393-399, 614-617` | 读 assumed_operator_state；但 M8 无发布者（BROKEN_NO_PUBLISHER）|
| §5.3 ODD Zone 切换（A/B/C/D）| STUB | `odd_envelope_manager_node.cpp:244` | current_zone_ = ODD_ZONE_A，**从未被赋值**（零写路径） |

---

## 3. 接口实现对照

| topic | 设计（spec §3） | 实际 file:line | schema_version | confidence | rationale | 状态 |
|---|---|---|---|---|---|---|
| `/l3/m1/odd_state` | 1 Hz + 事件；ODDState | `odd_envelope_manager_node.cpp:926-970` | **0（未赋值）** | ✅ 1.0F | ✅ 填充 | PARTIAL — schema_version=0 |
| `/l3/m1/mode_cmd` | 事件（转移时）；ModeCmd | `odd_envelope_manager_node.cpp:1001-1014` | **0（未赋值）** | ✅ 1.0F | ✅ 填充 | PARTIAL — schema_version=0 + 无消费者覆盖 |
| `/l3/m1/tor_request` | 事件（TDL≤TMR）；ToRRequest | `odd_envelope_manager_node.cpp:619-634` | ✅ 121 | ✅ 1.0F | ✅ 填充 | 断流 — **0 消费者** |
| `/l3/asdr/record` | 0.5Hz + 事件 | `odd_envelope_manager_node.cpp:1016-1028` | ✅ | ✅ | ✅ | 连通 |
| `/l3/sat/data` | 10 Hz；SATData（sat1/2/3）| `odd_envelope_manager_node.cpp:1030-1051` | N/A | ✅ | ✅ | PARTIAL — sat1.active_alerts 硬编空 |
| `/l3/safety/concern` | 事件（M3 watchdog）| `odd_envelope_manager_node.cpp:832-838` | 缺字段 | 缺字段 | ✅ | PARTIAL — SafetyConcernEvent.msg 缺 schema_version/confidence |
| `/l3/m8/operator_state` 入 | M8 → M1 | `odd_envelope_manager_node.cpp:614-617` | — | — | — | BROKEN_NO_PUBLISHER（M8 不发布该 topic）|

---

## 4. 已知缺陷

| 严重度 | 缺陷 | file:line | 类型 |
|---|---|---|---|
| CRITICAL | **current_zone_ 冻结 ODD_ZONE_A**：zone 维度的 ADR-1 形同虚设。M2 CPA horizon（`world_state_aggregator.cpp:399-404`）永远使用 idx=0 开阔水域参数；FSM Zone-C 收紧代码不可达 | `odd_envelope_manager_node.cpp:244`（init），`:878`（read），`:929`（read）；零写路径 | MOCK/脱节 |
| HIGH | **ODDState / ModeCmd schema_version 恒 0**：CMM 契约 DoD#7 声称已填；实际两消息均未赋值；仅 ToRRequest（`:622`）有 121 | `odd_envelope_manager_node.cpp:926-970 on_odd_state_publish_tick`；`:1001-1014 publish_mode_cmd` | 字段空 |
| HIGH | **/l3/m1/tor_request 零消费者**：M1 计算的安全关键 TDL≤TMR 触发发布后被丢弃；M8 独立生成自己的 `/l3/m8/tor_request`（`hmi_transparency_bridge_node.cpp:107-108`）| 全库 grep：src/ + docker/ 排除 m1_odd_envelope_manager → 0 match | 断流 |
| HIGH | **MODE_LIMITED / MODE_DEGRADED 无消费者**：M4 仅消费 MODE_EMERGENCY（`behavior_arbiter_node.cpp:140-143`）；M5/M6/M3 完全不订阅 /l3/m1/mode_cmd | `behavior_activation.cpp:61`；M5/M6/M3 src/ grep mode_cmd → 0 | 断流 |
| HIGH | **M7→M1 VETO 路径为软通道**：M1 仅读 SafetyAlert severity；X-axis /l3/checker/veto（`safety_supervisor_node.cpp:174-177`）M7 内部消费，未转发 M1 | M1 initialize_subscribers() 无 CheckerVetoNotification 订阅 | 断流/创可贴 |
| HIGH | **/l3/m8/operator_state 无发布者**：M1 on_operator_state()（`:614-617`）读 assumed_operator_state 驱动 TMR 查表；M8 不发布该 topic → TMR 永远使用默认 Bridge_OnDuty | M8 hmi_transparency_bridge_node.cpp — 无 OperatorState 发布 | 断流 |
| MEDIUM | **CapabilityManifest DDS 订阅缺失**：ROT_max 静态 YAML 加载，运行时船型参数更新无法到达 M1 | `parameter_loader.cpp:171-182`；M1 initialize_subscribers() 无 CapabilityManifest | 脱节 |
| LOW | **sat1.active_alerts 硬编空列表**：SAT-1 运营商威胁列表永为空，无论 CRITICAL health 还是 M7 告警 | `odd_envelope_manager_node.cpp:1035-1036` | MOCK |
| LOW | **SafetyConcernEvent.msg 缺 schema_version / confidence / rationale 字段**：CMM 四字段合规需消息层支持 | `l3_msgs/msg/SafetyConcernEvent.msg`（7 行）| 脱节 |
| LOW | **anchor_hdg 硬编 0.0F**：M3 stale watchdog 发布 SafetyConcernEvent 时 anchor_hdg=0.0 | `odd_envelope_manager_node.cpp:835` | 死代码 |

---

## 5. 创可贴 / 越界逻辑

以下逻辑**不应在** SIL bridge 中，目标归位见下表（摘自 `_bandaids.md`）：

| 越界逻辑 | 当前位置 file:line | 应然归属（M1 视角）| 严重度 |
|---|---|---|---|
| diagnostic_mock_publisher 永发 OK 状态，屏蔽 M1 传感器降质检测 | `docker/diagnostic_mock_publisher.py:89-110` | 该 mock 应在 SIL 测试时可注入真实退化信号；M1 评分器假设 T 轴输入真实 | HIGH |
| M7 SafetyAlert 不是 bridge 执行器的硬门控 | `docker/sil_topic_bridge.py` 不订阅 `/l3/m7/safety_alert` | M7→M1 软通道问题；bridge 实际上绕过了 M1 MRC 路径 | CRITICAL |
| sil_topic_bridge DCPA/TCPA 自行计算，传给 M2 的 cpa_m/tcpa_s 硬编 0 | `docker/sil_topic_bridge.py:739-740` | M2 是唯一权威世界视图；M1 从 M2 world_state 读 tcpa 驱动 TMR；bridge 这里导致 M1 TMR 计算基于零 TCPA | HIGH |

---

## 6. 设计-实现脱节（overclaim 修正）

| 位置 | 旧声称 | 实际状态 | 证据 |
|---|---|---|---|
| M1-progress.md D2.1 行（旧）| `✅ 2026-05-21 — zone/health-aware FSM + EMA + ToR adaptive matrix + Capability Manifest ROT_max + M7 VETO + FMEDA v0.1 (11 modes); PR merge 后同步关闭` | 4 项具体 GAP 仍存：(1) schema_version=0；(2) zone 永 A；(3) CapabilityManifest 静态 YAML；(4) tor_request 0 消费者 | `odd_envelope_manager_node.cpp:244, 622, 926-970, 1001-1014`；全库 grep |
| M1-progress.md D2.1 FMEDA（旧）| `✅ FMEDA v0.1 (11 失效模式)` 作为完成项 | M1-spec 要求 ≥20 模式；已交付 11 = 55%；D2.7（≥20 模式）**未启** | `docs/Design/Safety/FMEDA/M1-FMEDA-v0.1.md`（11 条）|
| M1-spec（旧）关于 Capability Manifest | 列为 upstream 订阅 IDL | 无 DDS 订阅；参数从 YAML 静态加载 | M1 initialize_subscribers() 372-470 行，无 CapabilityManifest |
| M1-spec（旧）关于 ToR 矩阵"✅ 实现" | tor_request 发布真实 | 发布真实，但**消费者为零**，安全关键信号静默丢失 | grep -rn `/l3/m1/tor_request` 排除 m1 → 0 |

---

## 7. D 任务联动表

| D 任务 | 关系 | 状态 | 详情 |
|---|---|---|---|
| D0.1 | Closed in | ✅ | MUST-6（sog 校验 → 与 M2 协同）+ MUST-7（active_role stub → 仅 M8）。M1 本体未触及 |
| D1.3.2-integration | Closed in | ✅ 2026-05-20 | SIL L3 pipeline integration：ODD state 真发布到 /sil/odd_state（l3_pipeline.launch.py 落地）|
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：PATH-S 严格规则（LineThreshold=40, 禁 malloc, 禁全局变量）+ 50 修复模式 + clang-tidy/cppcheck/CI 集成 |
| D2.1 | Closed in | 🟡 **部分完成**（旧 ✅ 有误）| FSM/score/ToR 矩阵/ROT_max 主体实现；但 4 项 GAP 未闭：schema_version=0 / zone 冻结 / CapabilityManifest DDS 缺 / tor_request 断流。D2.1-report 状态"🟡 设计完成，验证待执行"。PR merge 2026-05-21 但 DoD 未完全满足 |
| D2.7 | Currently Implementing（计划）| 🔴 **未启** | FMEDA M1 表 ≥20 失效模式；Owner 安全工程师外包；当前 v0.1=11 条 |
| D3.5 | Blocks | ⏳ | M1 ODD 4 子域热加载参数 132 [TBD-HAZID] 回填依赖本模块完整化（HAZID 8/19）；zone 冻结问题须先解决 |

---

## 8. DEMO 阻塞贡献

| 阻塞级别 | 说明 |
|---|---|
| 🟡 中阻塞（DEMO-2 7/31）| M1 ODD-A→ODD-D 切换 live 是 DEMO-2 核心场景：当前 zone 冻结 A，该场景不可演示 |
| 🔴 高阻塞（安全认证）| tor_request 零消费者 → 安全关键 ToR 触发路径静默断流；FMEDA 11/20 = 55%；CCS 中期意见会议前须闭合 |
| 🟡 中阻塞（调试 / 前端）| M1 ODD state 未被 HMI 订阅（useFoxgloveLive.ts TOPIC_MAP 无 /l3/m1/odd_state）→ 前端 M1 popovers 全为硬编字符串 |

---

## 参考 D 任务文档

- D1.3.2-integration: [Phase 1/D1.3-sil-framework/D1.3.2-integration/](../../Phase%201/D1.3-sil-framework/D1.3.2-integration/)
- D2.1: [Phase 2/D2.1-m1-odd-hardening/](../../Phase%202/D2.1-m1-odd-hardening/)
- D2.7: [Phase 2/D2.7-hara-fmeda-m1/](../../Phase%202/D2.7-hara-fmeda-m1/)

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-22 | 初版 progress（v3.2 重构时新建）|
| 2026-06-08 | 依系统审计（2026-06-08-m1-m8-systemwide-gap-audit.md）+ 代码对抗验证重写 progress：状态矩阵 / 接口对照 / 缺陷表 / 创可贴 / overclaim 修正 / D 任务状态更正（D2.1 旧 ✅ 改 🟡）|
