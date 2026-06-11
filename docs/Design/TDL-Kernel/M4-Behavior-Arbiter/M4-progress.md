# M4 · Progress · 实现现状

> **定位声明**：本文是 M4 的**实现现状**，对照 `M4-spec.md` 的设计目标。所有偏离、创可贴、MOCK、断流均记录于此，附**当前代码** file:line（审计基线 `docs/Doc From Claude/2026-06-08-m1-m8-systemwide-gap-audit.md`，但以当前代码为准）。

---

## 1. 头部

| 维度 | 值 |
|---|---|
| 最近更新 | 2026-06-08 |
| Currently Implementing | — |
| 当前分支 | `fix/m5-nlp-convergence`（M4 代码无 uncommitted 修改，见 git status） |
| 当前 LOC（非 salvage） | behavior_arbiter_node.cpp 659 + ivp_solver.cpp 185 + behavior_activation.cpp 95 + behavior_dictionary.cpp 66 + behavior_priority.cpp 56 = ~1061 |

---

## 2. 实现状态矩阵

| 设计职责（spec §） | 状态 | 证据 file:line | 备注 |
|---|---|---|---|
| ROS2 node 启动，4 Hz 定时仲裁 | REAL | `behavior_arbiter_node.cpp:75-80` | interval_ms 可配，默认 250ms = 4Hz |
| 行为字典从 YAML 加载 | REAL | `behavior_arbiter_node.cpp:82-95` | `m4.config_dir` 为空时加最小 TRANSIT 规则 |
| IvP 多目标求解（grid search） | REAL | `ivp_solver.cpp:166-183` | WeightedSumCombination，timeout=50ms，relax 0-3 |
| 输入新鲜度跟踪与健康评估 | REAL | `behavior_activation.cpp:7-24` | 3 档健康状态，age_*_ms 计算 |
| MRC_DRIFT 最高优先快速路径 | REAL | `behavior_arbiter_node.cpp:219-224` | conf=1.0，全向零速 |
| M1 standby 守卫（无 M1/M2 时） | REAL | `behavior_arbiter_node.cpp:172-184` | conf=0.0 |
| M3 任务状态前置检查 | REAL | `behavior_arbiter_node.cpp:159-205` | m3_active_latch + failsafe TRANSIT |
| TRANSIT IvP 函数（5 段效用面） | REAL | `behavior_arbiter_node.cpp:248-297` | 目标方位由 M3.current_target_wp 计算 |
| COLREG_AVOID IvP 函数 | PARTIAL | `behavior_arbiter_node.cpp:300-398` | magnitude 来自 M6 numeric_value ✅；**方向硬编码右转** ❌（见缺陷 §4） |
| M6 `primary_preferred_direction` 消费（spec §5.2） | MISSING | COLREGsConstraint.msg:8 字段存在；`behavior_arbiter_node.cpp:301-398` 零引用 | **HIGH 缺陷**；PORT/REDUCE_SPEED 场景必错 |
| M6 `rule_assessment` 权重调整（ADR-1 合规） | PARTIAL | `behavior_arbiter_node.cpp:113-123` | 消费 M6 RuleAssessment，调整 `colreg_avoidance_weight_`；但 weighted_fns 构造**不读该权重**（hardcode 1.0/10.0）；同时违反 ADR-1（绕过 M1）|
| YAML behavior_weights 应用于 IvP weighted_fns | MISSING | `m4_params.yaml:11-26` 配置了权重；`behavior_arbiter_node.cpp:298`（weight=1.0）、`line:398`（weight=10.0）硬编码 | dictionary_.set_priority_weight() 为无效调用 |
| Restricted_Visibility 行为（ODD-D）| STUB/MASQ | `behavior_activation.cpp:42-48`；`behavior_arbiter_node.cpp:136` `world_visibility_nm=999.0` | 永远无法激活；compute_active_set 还将其映射到 DP_HOLD（enum 错误，`behavior_activation.cpp:84-85`）|
| Channel_Follow / BERTH 行为（ODD-B VTS）| STUB/MASQ | `behavior_activation.cpp:51-56`；`world_in_vts_zone` 永为 false | 无 IvP 函数构造；即使激活也产出空效用 |
| IvP 不可行降级输出 | PARTIAL | `behavior_arbiter_node.cpp:475-517` | fallback_anchor 逻辑 ✅；但降级方向仍偏右（M6 方向未消费），与 spec §6 不符 |
| M7 VETO 订阅（ADR-2 hard gate）| MISSING | `behavior_arbiter_node.hpp`，无 `/l3/m7/*` 订阅；发布前无安全门控 | **HIGH 断流** |
| CMM 三接口（current_state/rationale/forecast）| MISSING | `behavior_arbiter_node.hpp` 无 `create_service()` 调用；`behavior_arbiter_node.cpp` 无服务注册 | ADR-3 violation |
| ASDR 审计发布 | REAL | `behavior_arbiter_node.cpp:648-657` + 579-601 | schema_version=113，4 类事件完整 |
| SAT-2 贡献向量（8 方向）| PARTIAL | `behavior_arbiter_node.cpp:566-575` + `compute_ivp_contributions:614` | compute 产出 8 方向；SAT2Data.msg:17 是 `float32[6]`；循环 `for i<6` → **270°/315° 静默截断** |
| SafetyConcernEvent 发布（IvP 不可行） | REAL（PARTIAL_FIELDS） | `behavior_arbiter_node.cpp:487-494` | 仅在 fallback_anchor 首次锁定时发布；`SafetyConcernEvent.msg` 缺 `schema_version` + `rationale` CMM 字段 |
| `reasoning_latency_ms` 字段填充 | MISSING | `SAT2Data.msg:14`；`behavior_arbiter_node.cpp:551-575` 无赋值 | SAT2 字段空 |

---

## 3. 接口实现对照

| topic | 设计（spec） | 实际 file:line | schema_version | confidence | rationale | 状态 |
|---|---|---|---|---|---|---|
| `/l3/m1/odd_state` | 订阅，ODD zone 行为激活 | `behavior_arbiter_node.cpp:50-52` | — | — | — | 连通（PARTIAL：MODE_LIMITED/MODE_DEGRADED 无行为区分） |
| `/l3/m1/mode_cmd` | 订阅，EMERGENCY → MRC | `behavior_arbiter_node.cpp:56-58` | — | — | — | 连通（PARTIAL：仅 EMERGENCY 消费） |
| `/l3/m2/world_state` | 订阅，本船/目标 | `behavior_arbiter_node.cpp:53-55` | — | — | — | 连通（PARTIAL：intent_distribution[] 字段不存在 WorldState.msg） |
| `/l3/m3/mission_goal` | 订阅，任务状态+目标 WP | `behavior_arbiter_node.cpp:59-61` | — | — | — | 连通 |
| `/l3/m6/colregs_constraint` | 订阅，含 primary_preferred_direction | `behavior_arbiter_node.cpp:62-64` | — | — | — | **断流（方向字段零读取）** |
| `/l3/m6/rule_assessment` | 未在 spec 接口表声明（ADR-1 issue） | `behavior_arbiter_node.cpp:65-67` | — | — | — | 连通但架构违规；权重调整为无效 no-op |
| `/l3/m4/behavior_plan` | 发布，4 Hz，含 CMM 字段 | `behavior_arbiter_node.cpp:69,539-549` | ✅ 113 | ✅ 填充 | ✅ 填充 | 连通 ✅ |
| `/sil/sat2_data` | 发布，4 Hz，8 方向 | `behavior_arbiter_node.cpp:70,551-576` | ✅ 113 | ✅ 填充 | ✅ 填充 | **部分（6/8 截断；reasoning_latency_ms=0）** |
| `/l3/asdr/record` | 发布，事件驱动 | `behavior_arbiter_node.cpp:71,648-657` | ✅ 113 | — | ✅ decision_json | 连通 ✅ |
| `/l3/safety/concern` | 发布，IvP 不可行时 | `behavior_arbiter_node.cpp:72-73,487-494` | ❌ msg 字段缺失 | ❌ msg 字段缺失 | ❌ msg 字段缺失 | 连通但 CMM 字段未填 |
| CMM 服务接口 | 应暴露 3 个 ROS2 服务 | 无 `create_service()` 调用 | — | — | — | **断流（未实现）** |

---

## 4. 已知缺陷

| 严重度 | 缺陷 | file:line | 类型 |
|---|---|---|---|
| **CRITICAL** | M6 `primary_preferred_direction` 零读取——右转硬编码：PORT/REDUCE_SPEED 场景 M4 输出方向错误 | `behavior_arbiter_node.cpp:301-398`（无引用 `primary_preferred_direction`）；`COLREGsConstraint.msg:8` 字段存在 | 断流 |
| **HIGH** | CMM 三接口（current_state / rationale / forecast）完全缺失：M8 无法查询 M4 决策链（ADR-3 violation） | `behavior_arbiter_node.hpp` 无 service server | 缺失 |
| **HIGH** | M7 VETO 未作为 behavior_plan 发布前置门控（ADR-2 violation） | `behavior_arbiter_node.hpp` 无 `/l3/m7/*` 订阅 | 断流 |
| **HIGH** | `Restricted_Visibility` 行为死代码：`world_visibility_nm` 永 999.0；同时 `compute_active_set` 将其映射到 `BehaviorType::DP_HOLD`（enum 错误） | `behavior_arbiter_node.cpp:136`；`behavior_activation.cpp:42-48,84-85` | MOCK/MASQ |
| **HIGH** | `Channel_Follow/BERTH` 行为死代码：`world_in_vts_zone` 永 false；无对应 IvP 函数 | `behavior_activation.cpp:51-56`；`behavior_arbiter_node.cpp` 无 BERTH IvP 段 | MOCK/MASQ |
| **HIGH** | IvP weighted_fns 忽略 YAML 权重（hardcode 1.0/10.0）：dictionary_.set_priority_weight() 是 no-op | `behavior_arbiter_node.cpp:298`（Transit=1.0）、`line:398`（Avoid=10.0）；`m4_params.yaml:11-26` | MOCK/MASQ |
| **HIGH** | M6 rule_assessment 直接修改权重绕过 M1 ODD 权威（ADR-1 violation），且修改本身因 #6 而无效 | `behavior_arbiter_node.cpp:113-123` | 脱节 |
| **MEDIUM** | SAT2Data ivp_contributions 8 方向仅发布 6（270°/315° 静默截断） | `behavior_arbiter_node.cpp:567`（`for i<6`）；`SAT2Data.msg:17`（`float32[6]`）；`compute_ivp_contributions:616`（kDirections[8]） | 断流 |
| **MEDIUM** | `SAT2Data.reasoning_latency_ms` 永为 0，未填充 | `SAT2Data.msg:14`；`behavior_arbiter_node.cpp:551-575` 无赋值 | 字段空 |
| **MEDIUM** | `SafetyConcernEvent.msg` 缺 `schema_version`/`confidence`/`rationale` CMM 字段 | `l3_msgs/msg/SafetyConcernEvent.msg`（7 行，仅含 concern_type/anchor_hdg/suggested_action/severity/stamp） | 合约缺陷 |
| **MEDIUM** | MODE_LIMITED / MODE_DEGRADED 模式无差异行为处理（仅 EMERGENCY 消费） | `behavior_arbiter_node.cpp:140-143` | 脱节 |
| **LOW** | `reactive_override_cmd` topic（旧 spec 遗留项）：当前设计中该 topic 归属存疑——若需要，应明确归属 M5 还是 M4 | `behavior_arbiter_node.cpp:69-73`（4 个 pub，无 reactive_override）；旧 `M4-spec.md:26`（已清除） | [TBD-reactive-override：旧 DEMO-2 P0 要求，待重新设计归属] |

---

## 5. 创可贴 / 越界逻辑

下表列出当前**错误归属于 bridge** 但**应然在 M4 或 M5** 的逻辑，及应然归位目标。

| 创可贴逻辑 | 当前位置 | 应然归属 | 证据 |
|---|---|---|---|
| 避碰 arm/latch/teardown 状态机（`_avoidance_active` / `_LATCH_MIN_HOLD_S=8s` / `_AVOID_TRANSIT_RELEASE_S=3s`）| `docker/sil_topic_bridge.py:326,431,437` | M5（BC-MPC 层）持有 latch 语义；M4 的 TRANSIT 恢复信号触发 teardown | `_bandaids.md` [CRITICAL/LEAKED_LOGIC] |
| 60° 最大避碰航向偏差 clamp（`MAX_AVOID_DEV_DEG=60.0`）| `docker/sil_topic_bridge.py:653-660,1072-1078` | M6（COLREGs 约束生成器）或 M5（NLP bounds）| `_bandaids.md` [HIGH/LEAKED_LOGIC] |
| DCPA/TCPA 几何计算（bridge 独立版）| `docker/sil_topic_bridge.py:776-832` | M2（唯一权威世界视图）| `_bandaids.md` [HIGH/LEAKED_LOGIC] |
| Dead-stick 开环 fallback（`SHIP_LENGTH_M=46.0` 硬编码 FCB 常量，违反 ADR-4）| `docker/sil_topic_bridge.py:1235-1245` | M5（BC-MPC 最短动态 fallback）| `_bandaids.md` [HIGH/LEAKED_LOGIC] |

---

## 6. 设计-实现脱节（overclaim 修正）

旧 progress/spec 声称与实际不符的条目：

| 条目 | 旧声称 | 实际（当前代码证据） |
|---|---|---|
| D3.1 "M4 IvP 完整实现（813 LOC src + 916 LOC test）"（`M4-progress.md:13`）| ✅ 完整实现 | IvP grid search 真实 ✅；但：(a) `primary_preferred_direction` 零消费；(b) YAML 权重未应用于 weighted_fns（hardcode）；(c) Restricted_Visibility 永死；(d) DP_HOLD/BERTH/MRC_DRIFT 无 IvP 函数构造。**"完整"声称夸大。** |
| D2.5 "M4 侧已发布 `/sil/sat2_data.ivp_contributions[]` @4Hz"（`M4-progress.md:14`）| ✅ 就绪 | 4 Hz 发布存在 ✅；但 `float32[6]` 截断 8 中的 2 方向；`reasoning_latency_ms` 永为 0；`SafetyConcernEvent` CMM 字段缺失。**"就绪"过于乐观。** |
| 旧 spec 状态表行："/sil/sat2_data publication 红色未做"（`M4-spec.md:53`）| 🔴 未做 | 与 progress 矛盾——publication 已存在（尽管有 6/8 截断）。旧 spec 状态表未随 D3.1 merge 更新。 |

---

## 7. D 任务联动表

| D 任务 | 关系 | 当前真实状态 | 偏差备注 |
|---|---|---|---|
| D0.2 | Closed in | ✅ 关闭 | RFC-009 IvP 自实现路径锁定，无偏差 |
| D1.4 | Closed in | ✅ 关闭 | 编码规范 v1.2 全模块适用 |
| D3.1 | Closed in | ⚠️ 关闭但有已知偏离 | IvP solver 真实，但 M6 方向消费缺失、YAML 权重未接入、Restricted_Vis/Channel_Follow 死代码——旧 report 标注"完整实现"夸大（见 §6）|
| D2.5 | Blocks | ⚠️ 部分完成 | SAT2Data @4Hz 已发布，但 6/8 截断 + reasoning_latency_ms=0；FE IvpRiskGradientLayer 获取 6 方向而非 8 方向 |

---

## 8. DEMO 阻塞贡献

| 阻塞类型 | 详情 |
|---|---|
| **P0 正确性阻塞** | M4 硬编码右转忽略 M6 `primary_preferred_direction`：PORT 场景（Rule 13 追越从后方 / Rule 17 直航船最终避让需左舵）M4 输出方向错误，M5 输入错误，整个避碰链失效 |
| **SAT-2 数据完整性** | 8 方向截断为 6，前端 IvpRiskGradientLayer 缺少 270°/315° 方向数据 |
| **透明性 gap** | CMM 三接口缺失：M8 无法向 ROC 展示 M4 决策依据（SAT-2/SAT-3 完整透明性受阻） |

---

## 参考 D 任务文档

- D3.1: [Phase 3/D3.1-m4-behavior-arbiter/](../../Phase%203/D3.1-m4-behavior-arbiter/)（spec + report）

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-25 | D3.1 合并后更新，标注"完整实现" |
| 2026-06-08 | 依系统审计重写 progress（状态矩阵 + gap + 创可贴 + overclaim 修正）；修正 D3.1 夸大声明；修正 SAT-2 截断记录；新增 M6 方向断流、CMM 缺失、M7 VETO 缺失等 HIGH 缺陷 |
