# TDL M1–M8 全栈断流 / MOCK / 设计-实现脱节 系统审计

> 生成：2026-06-08 · 方法：13 个 sonnet mapper（8 模块 + 5 跨切）+ 4 个 flow-gap 检测 + 40 条对抗验证 + completeness critic（codegraph 测绘）+ 主体 A4000 live 核对
> 审计代码版本：**`158bba9d`**（workflow 启动时的工作树）。当前 local=`87315c82` / A4000=`f9011287`，期间仅 **M5 J_colreg 重设计** + web Malacca 地图改动（见 §0.2 修正），其余被审文件未变 → 结论仍有效。
> 性质：**只读测绘，未改任何代码。** 本文是修复基线对照，扩展自 `2026-06-08-avoidance-design-vs-implementation-gap.md`（那篇只覆盖避碰链 D1–D6；本篇覆盖 M1–M8 全栈 + 前后端）。
> 原始数据：结构化 JSON `tasks/wbqc851jq.output`（504KB）；压缩 digest `handoff/_gap_audit_digest.md`（120KB）；workflow 脚本 `handoff/m1m8_gap_audit.workflow.js`。

---

## 0. 总裁决

**架构（分层 + 单一权威 + Doer-Checker + Backseat Driver）设计仍自洽；偏离全部在实现层，但比上一篇严重得多——不是 1 个 keystone，而是 11 条系统性主题。**

一句话：**L3 的「决策骨架」在跑（M2→M6→M4→M5 数据确在流，频率正确），但「安全骨架」是空的，「执行+回航」靠 SIL bridge 创可贴扛，CMM 契约普遍破裂，前端大量字段断流/硬编码。** 用户报告的三个症状全部能溯源到具体实现缺陷：

| 用户报告症状 | 根因主题（见 §2） |
|---|---|
| 模块上下断流 GAP | T1 安全链空 · T4 方向信息断流 · T5 回航链脆 · T8 前端断流 · T9 M2 threat_state 无发布 |
| MOCK 各种问题 | T11 mock 伪装真实（L2/diagnostic/SAT stub/external mock/dead sim 节点） |
| 设计-实现不同步 | T3 CMM 契约破裂 · T2 决策逻辑漏进 bridge · T6 BC-MPC 整层死 · 各模块 progress.md overclaim |
| 避碰行为异常 | T4（M4 无视 M6 方向，硬编码右转）· T1（M7 无否决权）· T2（bridge 才是真避碰控制器）|
| 回归航路异常 | T5（M3 无 mission_state、speed=0、ENC 桩）· T2（回航靠 bridge XTE 控制器，非 L3）· T11（L2 是 mock）|

**对抗验证统计**：127 候选 → top 40（全 CRITICAL+HIGH）逐条对抗复核 → **35 CONFIRMED + 5 PARTIALLY_CONFIRMED，0 REFUTED，0 STALE**。87 条 MEDIUM/LOW 未单独复核（已在 digest 列出）。本次专门拉黑了 `.salvage-d3.1/` + `.salvage-d3.3b/` 备份目录（上一篇误报 D2/D5/D6 的根源），故无 stale 误报。

### 0.1 健康项（别过度报警）

- **决策数据流主干在跑**：A4000 实测全 M1–M8 topic 发布，频率正确（m2 20Hz / m4 20Hz / m5 5Hz / m6 10Hz / actuator 5Hz）。
- **M4 IvP 仲裁真实**：behavior_plan 健康（schema=113，heading 窗口 M6 驱动，rationale 完整，conf=0.95）。
- **M2 世界模型核心真实**：CPA/TCPA、encounter 预分类、SAT 聚合在跑。
- **M6 规则推理真实**：Rule 13/14/15/17/19 真 evaluate，5 层链构建，D5/D6 已修。
- **own-ship 动力学真实**：MMG/RK4 + Gauss-Markov 环境扰动是真仿真。
- **D2/D5/D6 已修**（本次确认无回归）。

### 0.2 ⚠ Current-state 修正（审计版本 158bba9d → 当前）

审计跑在 `158bba9d`；之后上一会话做了 M5 J_colreg 重设计（commit 链至 `dce28680`+）。据此修正：

| 审计 finding | 当前状态 |
|---|---|
| **sim CRITICAL「M5 NLP always fails → geometric fallback」**（PARTIALLY_CONFIRMED）| ✅ **已大幅修复**。`mid_mpc_nlp_formulation.cpp:218–233` ROT 约束改两条平滑线性行替代 abs()；J_ 现含真 `build_colreg_cost_` + `build_asym_cost_`（:247–250）。progress.md：Restoration_Failed **50→0**，单测 9/9，e2e GREEN。**避碰不再长期跑 DEGRADED 几何兜底。** |
| M5「cost_colreg≡0 即使 CPA 时刻」（旧 D1 第二半）| ⚠ 部分修：**目标函数 J_colreg 现真实**（优化器在用）；但 **rationale 里报告的 cost_colreg 仍恒 0**（:337–338「Phase E1 not split from CasADi stats」）= 纯可观测性桩，非避碰失效。 |
| 其余 M5 findings（BC-MPC 死、waypoint CMM 字段空、/m5/ namespace、无 ODD/veto gate）| 🔴 **仍成立**——这些文件（bc_mpc_node、waypoint_generator、mid_mpc_node 订阅）未被本轮重设计触及。 |
| web 前端 findings | 大体仍成立；近期 web 改动集中在 Malacca 地图 center/zoom（cosmetic），未触及 §T8 的 schema/订阅缺口。 |
| A4000 M6 节点 | 有一处未提交工作树编辑（审计读的是 158bba9d 版本，差异需单独 diff）。 |

---

## 1. CRITICAL 仪表盘（15 条，全部对抗验证通过）

| # | 模块/层 | 缺陷 | 证据 file:line | 裁决 |
|---|---|---|---|---|
| C1 | **M7** | `/l3/checker/veto` **从不发布**——M7 无否决权（D3 实锤）。节点仅 4 个 publisher，无 CheckerVetoNotification | `safety_supervisor_node.hpp:68-71` | CONFIRMED |
| C2 | **M7** | **6 个硬约束(HC) 全是死代码**：`run_hard_constraint_checks(){ (void)now; return; }` 一行空壳。CPA/ROT/Speed 函数存在但运行时从不调用 | `safety_supervisor_node.cpp:548-552` | CONFIRMED |
| C3 | **bridge** | M7 SafetyAlert **不是 actuator 硬门**——bridge 不订阅 `/l3/m7/safety_alert`，M7 报 CRITICAL 也拦不住舵令 | `sil_topic_bridge.py`（无该订阅）| CONFIRMED |
| C4 | **bridge** | **完整 autopilot（航向 PD + 速度 PI）在 bridge**，非 L3 | `sil_topic_bridge.py:145-182` | CONFIRMED |
| C5 | **bridge** | **避碰 arm/latch/teardown 状态机整个在 bridge** | `sil_topic_bridge.py:326,431,437` | CONFIRMED |
| C6 | **M1** | `current_zone_` **永久冻结 ZONE_A**——ADR-001 的 zone 维度是死信，FSM Zone-C 收紧不可达 | `odd_envelope_manager_node.cpp:244` (0 写路径) | CONFIRMED |
| C7 | **M5** | **BC-MPC 整层死亡**——节点存在可编译但从不 launch，短horizon 反应层在生产中不存在 | `m5_mid_mpc.launch.py:11-21` | CONFIRMED |
| C8 | **contracts** | M5 发布到 `/m5/avoidance_plan`，bridge+M7 订阅 `/l3/m5/avoidance_plan`——**只靠 entrypoint shell remap 救命**，canonical `l3_pipeline.launch.py` 启动则避碰静默断链 | `mid_mpc_node.cpp:79` vs `safety_supervisor_node.cpp:145` | CONFIRMED |
| C9 | **frontend** | SotifMetrics **wire 格式(结构化数组) vs HMI 类型(扁平命名字段) 完全 schema 不匹配**；且 `/sil/sotif_metrics` 根本不在 live topic 表 | `SotifMetrics.msg` vs HMI types | CONFIRMED |
| C10 | **M8** | M8 **是 sat2/sat3 的并行发布者，不是聚合者**——M4/M5 直发 `/sil/sat2_data`+`/sil/sat3_data`，M8 又发 1Hz stub，双发布者打架 | `behavior_arbiter_node.cpp:70` + `mid_mpc_node.cpp:82` + M8 stub | PARTIAL |
| C11 | **flow** | M5→M7 `/l3/m5/avoidance_plan`：plan 到了 M7 但所有 HC 死代码 + 提取的标量在用前被清零 | `safety_supervisor_node.cpp:297-301` | CONFIRMED |
| C12 | **flow** | M7→M1 veto/alert：veto 无发布者，HC 全死 | `safety_supervisor_node.cpp:548-552` | PARTIAL |
| C13 | **flow** | M8→`/sil/sat2_data`→HMI：6 元标量数组 vs HMI 期望 `IvpContribution[]` 对象 | `SAT2Data.msg:17` vs HMI | PARTIAL |
| C14 | **flow** | M7/M8→`/sil/sotif_metrics`→HMI：schema 不匹配 + topic 缺失双重失败 | 同 C9 | CONFIRMED |
| C15 | **sim/M5** | （已修，见 §0.2）M5 NLP 历史上长期失败→几何兜底 | `mid_mpc_node.cpp:237-261` | PARTIAL→✅FIXED |

---

## 2. 十一条系统性主题（按用户三类组织）

### 🔴 类别 A：断流 GAP（数据流模块间断裂）

**T1 — 安全链是空的（Doer-Checker 运行时未实现）= 最严重，认证级**
- C1 M7 无 veto 发布；C2 6 个 HC 全死代码；C3 bridge 不订阅 M7 alert。
- M7 订阅 M4 behavior_plan 但丢弃内容（`safety_supervisor_node.cpp:285-290` `/*msg*/`）。
- M7 on_avoidance_plan 存了 plan 立即把提取标量清零（:297-301）。
- SOTIF assumption #5 (CommLink) 永远喂零输入（:432）。
- orchestrator Gate-6 veto 延迟测试硬编码 PASS（`checker_verification.py:123-125`）。
- → **ADR-2「Checker 同步硬门、独立路径」运行时不成立。** M7 当前是「写日志的旁观者」。**这是认证(IEC 61508 SIL2)阻塞项。**

**T4 — M6→M4→M5 避碰方向信息断流（避碰行为异常的核心）**
- M4 **无视 M6 `primary_preferred_direction`**，对所有遭遇**硬编码右转** starboard 窗口（`behavior_arbiter_node.cpp:301-398`，0 处引用该字段）。head-on/交叉让路 demo 正确；需左转/减速场景必错。
- M5 只读 M6 的二元「有遭遇」flag（mid_mpc_node.cpp:173-184），把主目标 CPA/TCPA 砍 80% 当代价乘子；不读 direction / numeric_value。
- M6 `rule_assessment` **只为 Rule14 latch 发布**——Rule13(追越)/15(交叉) latched 遭遇不发 → M4 对那些规则失语（colregs_reasoner_node.cpp:594-609）。
- → M6 算出丰富的 role/direction/min-alteration，下游基本丢弃。

**T5 — 回归航路链脆弱（回航异常的核心）**
- M3 **不发布 `/l3/m3/mission_state`**（topic 在 live 表但无源码 publisher）→ M1 MRC 选择饿死（M1 订阅 `odd_envelope_manager_node.cpp:459`）。
- M3 `speed_recommend_kn` 恒 0（never assigned）→ M4/M5 无速度引导。
- M3 `has_enc_check=true` 硬编码 → 4 条件 task_validity 退化为 3 条件（`mission_manager_node.cpp:625`，无 EncRouteValidator 类）。
- M3 `planned_eta_s=0.0` 硬传入 replan 触发器（:561）。
- **真正的回航控制是 bridge 的 XTE 控制器**（见 T2），不是 L3。L2 是 mock（见 T11）。
- → 回航现在「能跑」纯属 bridge XTE + mock_l2 恰好加载场景航线的偶合。

**T9 — M2 唯一世界视图但有洞**
- M2 **不发布 `/l3/m2/threat_state`**（world_model_node 仅 3 个 publisher）→ bridge 的 latch-release 条件1（on_threat_state）永不触发。
- bridge 本地重算 CPA/TCPA 而非消费 M2（`sil_topic_bridge.py:776-832`）。
- **bridge + shell_b_harness 双重把 TrackedTarget.cpa_m/tcpa_s 硬设 0**（`sil_topic_bridge.py:739-740` + `simulator.py:472-473`）→ M6 把每次遭遇当即时碰撞。
- M2 `r_dot_deg_s=0`、`nav_mode='OPTIMAL'` 硬编码、分类分支死、EnvSanity 仅 3/7 检查。

**T8 — 前端 HMI 断流 + schema 不匹配（前后端联调断流）**
- C9/C13/C14 schema 不匹配（SotifMetrics、SAT2 ivp_contributions）。
- **5 个 HMI 订阅的 foxglove topic 无后端发布者**：`/sil/sensor_status` `/sil/commlink_status` `/sil/fault_status` `/sil/control_cmd` `/sil/target_vessel_state`（`useFoxgloveLive.ts:77-95,36`）→ SensorStatusRow/CommLinkStatusRow 永远空。
- **后端发了 HMI 不订阅**：`/l3/m8/ui_state`、`/l3/m1/odd_state`、`/l3/m2/world_state`、`/l3/m7/safety_alert` 均无 HMI 订阅 → UIState 50Hz 喂送在前端全黑（`useFoxgloveLive.ts` TOPIC_MAP 仅 16 topic）。
- 大量硬编码占位：M1 ODD「OPEN_WATER 92%」、M2 XTE「0.02nm」、M5「右舵转向 15°」（`SimulationMonitor.tsx:1253-1671`）、TCPA 单位标 `m` 实为分钟、ConningBar sparkline 永空、ASDR ledger 是 stub。
- ⚠ 我 live 实测补充：底栏 WS 显示 `ws://127.0.0.1:8765`（远程访问指浏览器本机、且 8765 是被占端口，应 18765）。

### 🔴 类别 B：MOCK 伪装真实

**T2 — 决策逻辑漏进 bridge（D4 确认并扩大）= 架构图里 bridge 不存在**
| 漏进 bridge 的逻辑 | 本应在 | 证据 |
|---|---|---|
| 完整 autopilot（航向 PD Kp=1.0 + 速度 PI）| L4/M5 | `sil_topic_bridge.py:145-182` |
| 避碰 arm/latch/teardown 状态机 | M4/M5 | `:326,431,437` |
| DCPA/TCPA 几何计算 | M2 | `:776-832` |
| 60° 航向 clamp | M6（最小转向量）| `:653-660` |
| **Cross-track-error 回航控制器** | M5 BC-MPC / M4 transit | `:1265-1327` |
| Dead-stick 开环兜底（固定舵）| —（不该存在）| `:1235-1245` |
| 硬编码 FCB 常量 SHIP_LENGTH_M=46.0 / CPA_SAFE_M=1000.0 | —（ADR-4 违反）| `:100,108` |
- → **bridge 才是事实上的 COLAV + 回航控制器。** 架构的「M5 直出 L4」是虚构。bridge 含船型常量 = **ADR-4 Backseat Driver 违反**。

**T11 — Mock 节点伪装真实数据**
- `mock_l2_publisher`：合成整个 L1/L2（voyage_task/route/speed/replan），对任何 replan 永远返回 SUCCESS（`mock_l2_publisher.py:186-197`）。
- `diagnostic_mock_publisher`：恒发 OK → **永久屏蔽 M1 传感器降级检测**（`diagnostic_mock_publisher.py:89-110`）→ SOTIF/HAZID 注入测试无效。
- M8 `on_sil_stub_tick`：SAT2/3/SOTIF stub（rationale='sil_stub'，空数组）。
- M7 `SotifMetricsPublisher` `stub_mode=true` 永不关 → 6 个 violation 全 0 **伪装成绿色 SOTIF**（`sotif_metrics_publisher.hpp:21`）。
- `ExternalMockPublisher` 发 `/fusion/*` 坐标硬编码 22.5N/114E（深圳）vs 实际 Trondheim 63.4N/10.4E。
- KF tracker 绕过 Kalman gain 直接覆盖状态（`tracker_mock/node.py:45-51`）。
- **死代码（scaffold 从不 launch）**：FMI bridge（dds_fmu）、FcbSimulatorNode（订阅旧 `/m5/` namespace）、AIS bridge。

**T6 — M5 BC-MPC 短horizon层整层死亡**
- C7 节点从不 launch；订阅错 namespace（`/m2/world_state` 缺 `/l3/`，`bc_mpc_node.cpp:35`）；`/m5/reactive_override_cmd` 0 发布者。
- → DEMO-2「双 MPC 完整实装」overclaim；反应式安全层运行时不存在。

### 🔴 类别 C：设计-实现脱节

**T3 — CMM 契约普遍破裂（schema_version/confidence/rationale 未填充）**
- `schema_version` 恒 0：M1 ODDState/ModeCmd、M2 WorldState/TrackedTarget、M5 avoidance_plan waypoints、M6 colregs_constraint、M7 safety_alert、M8 UIState（A4000 live 实测确认 m5 waypoint 全 0）。
- **msg 定义本身缺字段**：`RuleAssessment.msg`、`SafetyConcernEvent.msg` 完全没有 schema_version/rationale 字段。
- M5 waypoint：NLP 成功路径 4 个 CMM 字段全空，几何兜底路径填了 3/4（缺 wp.stamp）→ 路径不对称。
- → 违反 CLAUDE.md §3「每消息强制 stamp+schema_version+confidence+rationale」。CMM 透明性聚合(M8 SAT)因此普遍拿到空。

**T7 — M5 topic namespace 脆弱**（见 C8）+ M5 horizon N=18 参数未生效（默认 12，`mid_mpc_nlp_formulation.hpp:67`）+ MRM 走 M7 仅 spdlog 日志非真发布（`mid_mpc_solver.cpp:169-210`）。

**T10 — M1 ODD 权威弱化（ADR-1）**
- C6 zone 冻结；M5 完全不订阅 ODD state（无 envelope gate）；ModeCmd LIMITED/DEGRADED 无人消费（仅 EMERGENCY）；`/l3/m1/tor_request` 0 消费者；CapabilityManifest 订阅缺失（运行时参数更新路径无）。

**progress.md / spec overclaims（设计-实现脱节的文档面）**——每模块都有，典型：
- M5「双 MPC 完整实装」「sat3@2Hz」「MUST-9 走 M7」全 overclaim。
- M7「6 硬约束 + FMEDA v1.0 + PATH-S CI 通过」vs 6 HC 全断连、无 FMEDA 文件。
- M8「SAT-2/3/SOTIF 桥接已实装」「LifecycleNode」vs 是并行 stub、是 plain rclcpp::Node。
- M2「MUST-6 SOG 校验闭环」「CPA/TCPA 真发布」vs test-only 死代码、bridge 重算。
- M3「IT-01~06 L1/L2 独立性测试」vs l1_watchdog_bypass 默认 true。

---

## 3. 每模块一行记分卡

| 模块 | 核心健康度 | 最严重缺陷 |
|---|---|---|
| **M1** ODD | 🟡 FSM/score 真实，但 zone 冻结 ZONE_A（C6），ModeCmd 多数模式无人消费，tor_request 无消费者 |
| **M2** World | 🟡 CPA/TCPA/预分类真实，但 threat_state 不发、schema=0、cpa_m 被 bridge/harness 清零、EnvSanity 3/7 |
| **M3** Mission | 🔴 mission_state 不发、speed_recommend=0、ENC 桩、回航依赖 mock_l2 |
| **M4** Behavior | 🟢 IvP 真实健康，但无视 M6 方向硬编码右转、reactive_override_cmd 不发 |
| **M5** Tactical | 🟡 Mid-MPC 已修收敛(§0.2)，但 BC-MPC 整层死、waypoint CMM 空、无 ODD/veto gate、/m5/ namespace 脆 |
| **M6** COLREGs | 🟢 规则推理真实(D5/D6 已修)，但 rule_assessment 仅 Rule14、colregs_chain 不入 SAT2、schema=0 |
| **M7** Safety | 🔴🔴 **整个 Checker 空壳**：无 veto、6 HC 死代码、SOTIF 永 stub、丢弃 M4 plan = 认证阻塞 |
| **M8** HMI | 🔴 并行 stub 非聚合、UIState 位置/航向恒 0、operator_state 不发、SAT 聚合拿空、role 硬编码 ROC |
| **bridge** | 🔴 事实上的 COLAV+回航控制器（autopilot/latch/XTE/CPA/clamp 全在此）+ 船型常量 ADR-4 违反 |
| **orchestrator** | 🟡 lifecycle/scenario 主路真实，但 ASDR cache 空、backup-autostop split-brain、3 个 Gate 是 PASS 桩 |
| **frontend** | 🔴 SotifMetrics/SAT2 schema 不匹配、5 topic 无后端、UIState/ODD/world_state 不订阅、大量硬编码 |
| **sim** | 🟡 own-ship MMG/RK4 真实，但 target 线性无 MMG、KF 绕 gain、FMI/FCB/AIS 死代码、cpa_m=0 |

---

## 4. 修复优先级建议（依赖排序，给用户定方向）

> 每步走 systematic-debugging + TDD + verification-before-completion，A4000 复现（本地无 ROS2）。**不在 bridge 加新创可贴。**

| 优先 | 目标 | 涉及 | 为什么 |
|---|---|---|---|
| **P0-认证** | 接通 M7 真硬门：HC 函数从死代码接入 `run_hard_constraint_checks` + 发布 `/l3/checker/veto` + M5/bridge 订阅 gate | M7, bridge, M5 | C1/C2/C3，ADR-2 阻塞 SIL2，且是「避碰失控无人能拦」的安全底线 |
| **P0-正确性** | M4 消费 M6 `primary_preferred_direction`（去掉硬编码右转）+ M6 rule_assessment 覆盖 Rule13/15 | M4, M6 | T4，直接修「避碰行为异常」（非右转场景） |
| **P1-回航** | M3 发布 mission_state + speed_recommend + 真 ENC 校验；把回航控制从 bridge XTE 收回 L3 | M3, M5/M4, bridge | T5+T2，直接修「回航异常」+ 去 bridge 创可贴 |
| **P1-契约** | 全模块填 schema_version/confidence/rationale；补 RuleAssessment/SafetyConcernEvent msg 字段 | 全部 | T3，CMM 是认证白盒可审计前提；M5 waypoint NLP 路径补 CMM 字段 |
| **P2-反应层** | 启动 BC-MPC + 修 namespace + 接 reactive_override_cmd | M5, launch | T6，恢复短horizon 安全层 |
| **P2-前端** | 修 SotifMetrics/SAT2 schema 对齐 + 订阅 UIState/ODD/world_state + 去硬编码占位 | frontend, M8 | T8，前后端联调断流 |
| **P2-去 mock** | 关 SOTIF stub_mode、关 diagnostic_mock 屏蔽、统一 cpa_m 由 M2 喂、修 ExternalMock 坐标 | M7, sim, bridge | T11，让降级/故障测试有效 |
| **P3-架构回收** | bridge 决策逻辑（autopilot/latch/clamp/CPA）逐步回收进 L3；M5 直出 L4 | bridge, M2/M4/M5 | T2，恢复架构真身（D1 已修后几何 clamp 应可移） |
| **P3-ODD** | 接通 zone 状态机写路径 + M5 订阅 ODD gate + ModeCmd 全模式消费 | M1, M5, M4 | T10，ADR-1 落地 |

---

## 5. 附录

- **CRITICAL 全文 + 每条 current_code_quote**：`tasks/wbqc851jq.output`（504KB JSON，`result.verified[]`）
- **全模块/跨切/flow digest**：`handoff/_gap_audit_digest.md`（120KB，本文证据来源）
- **可复现 workflow**：`handoff/m1m8_gap_audit.workflow.js`（13 mapper + 4 flow + 验证 + critic，全 sonnet，只读）
  - 重跑：`Workflow({scriptPath, resumeFromRunId:"wf_bfc239a9-ace"})` 缓存命中
- **completeness critic 补充的未覆盖区**（已并入 §2）：fsm_aggregator（active_rule 硬编码 Rule14）、shell_b_harness（重复 autopilot + cpa=0 + M3/M7 缺席 RL loop）、ScoringNode（colregs_active 无发布者 + t_action=0）、fault_injection（fault topic 无消费者，整链断）、useSchemaValidation 404。
- **方法论纪律**：本次拉黑 `.salvage-d3.1/`、`.salvage-d3.3b/`、`archive/`（上一篇误报源）；40 条对抗验证 0 STALE 0 REFUTED，置信度 🟢 High（≥A 级来源=当前仓库代码 file:line + A4000 live 实测）。
