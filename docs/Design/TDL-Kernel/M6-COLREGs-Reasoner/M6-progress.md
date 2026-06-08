# M6 · COLREGs Reasoner · Progress（实现现状）

> **定位声明**：本文是 M6 的**实现现状**对照 [M6-spec.md](M6-spec.md) 的设计目标。所有偏离 / 创可贴 / MOCK 记录于此并附 file:line。审计基线 `docs/Doc From Claude/2026-06-08-m1-m8-systemwide-gap-audit.md`（本模块条目已并入下表，但以**当前代码**为准）。注：A4000 工作树有一处未提交编辑（审计读的是 158bba9d），以**当前工作树文件**为准。

---

## 1. 头部

| 维度 | 值 |
|---|---|
| 最近更新 | 2026-06-08 |
| Currently Implementing | D2.4（Rule 5–19 完整推理 + SAT-2 colregs_chain 序列化）|
| 当前分支 | `fix/m5-nlp-convergence`（含 M6 相关未提交编辑）|
| 当前 LOC | ~990（`colregs_reasoner_node.cpp` + headers）|

---

## 2. 实现状态矩阵

| 设计职责（对应 spec §）| 状态 | 证据 file:line | 备注 |
|---|---|---|---|
| ROS2 node 初始化（§1）| REAL | `colregs_reasoner_node.cpp:210–223` | 构造函数完整，规则库加载后才启动 |
| ODD-aware 参数加载（§4.1）| REAL | `colregs_reasoner_node.cpp:248–288`（`load_odd_thresholds()`）| 从 `odd_aware_thresholds.yaml` 加载四 ODD 域参数 |
| 目标几何状态转换（§4.1）| REAL | `colregs_reasoner_node.cpp:765–821`（`convert_world_state()`）| bearing / aspect / relative_speed 计算正确；cpa_m / tcpa_s 直接从 M2 取（当前 M2 置 0.0，影响推理质量） |
| 目标缓存管理（§4.1）| REAL | `m6_colregs_reasoner/src/target_state_cache.cpp:14` | TargetStateCache，max_targets=50 |
| Rule 5–19 推理引擎（§4.1）| PARTIAL | 各 rule*.cpp | Rules 5,6,7,8,13,14,15,16,17,18,19 均有 `evaluate()`；但 Rule 16 是独立 CPA 阈值代理（非真 Rule 16 义务），Rules 13–18 geometry sector 不随 ODD 域调整（仅阈值参数 ODD-aware）|
| risk_of_collision 门控（§4.1）| REAL | `colregs_reasoner_node.cpp:571–575` | 非锁定规则：`tcpa_s>=0 && cpa_m<cpa_safe_m` 才激活，防已通过目标 stuck |
| RuleLatch 滞后 Rule 13(d)（§4.3）| REAL | `rule_latch.hpp:18–28`；`colregs_reasoner_node.cpp:526–562` | Rule 14 / Rule 15 per-target-key 锁定；release = opening && (past_and_clear \|\| CPA>1.5×safe）；D6 fix 158bba9d |
| 5 层 chain 构建（§4.2）| REAL | `colregs_reasoner_node.cpp:856–981`（`build_colregs_chain()`）| 5 层全构建；注入 `COLREGsConstraint.colregs_chain` (line 587)；chain 计算结果**不**复制到 `SATData.sat2.colregs_chain` |
| conflict_detected 角色驱动（§5.4）| REAL | `colregs_constraint_generator.cpp:30–35` | 纯 role 判断，无 rule-ID 过滤；D5 fix d8b0c608 |
| primary_preferred_direction + numeric_value（§2.1）| REAL | `colregs_constraint_generator.cpp:98–106` | preferred_direction ≠ HOLD 时发 Constraint；c.numeric_value = min_alteration_deg |
| CMM current_state（§3.3）| PARTIAL | `colregs_reasoner_node.cpp:728–729` | `sat1.state_summary` 近似（target_count + odd_domain），非 enum 状态 |
| CMM rationale（§3.3）| PARTIAL | `colregs_reasoner_node.cpp:590`（constraint.rationale）| constraint 级 rationale 存在；rule_assessment rationale 仅 trigger_conditions 列表 |
| CMM forecast SAT-3（§3.3）| STUB | `colregs_reasoner_node.cpp:737–741` | `predicted_state="nominal"` hardcoded；`prediction_uncertainty=0.5` hardcoded；`tmr_s=60.0` hardcoded（非 ODD-aware）|
| SAT-2 colregs_chain 序列化（§3.2，DEMO-2 P0）| STUB | `colregs_reasoner_node.cpp:732` | `sat2.reasoning_chain=""`；`sat2.colregs_chain` 数组从未赋值；D2.4 目标 |
| schema_version 填充（§3.3）| MISSING | `colregs_constraint_generator.cpp:43`；`colregs_reasoner_node.cpp:569–576` | `COLREGsConstraint.schema_version` 设计值 114，当前恒 0；从未在任何路径赋值 |
| RuleAssessment 覆盖（§3.2）| PARTIAL | `colregs_reasoner_node.cpp:608–624` | 仅 Rule14 latch 活跃时发布；Rule 13 / Rule 15 锁定会遇**不发** RuleAssessment |
| Rule16 独立 CPA 门控（§5.1）| PARTIAL | `rule16_give_way.cpp:26` | Rule16_GiveWay.evaluate() 仅检查 cpa < 1.5×cpa_safe，不依赖 Rule13/14/15 角色判定；可能与 Rule14/15 latch 状态不一致，造成 chattering |
| ASDR 记录（§2.1）| REAL | `colregs_reasoner_node.cpp:750–758` | stamp / source_module / decision_type / decision_json 均填；signature 空（Phase E1 明确注释）|

---

## 3. 接口实现对照

| topic | 设计（spec §3.2）| 实际 file:line | schema_version | confidence | rationale | 状态 |
|---|---|---|---|---|---|---|
| `/l3/m6/colregs_constraint` | 1 Hz，包含 5 层 chain + role + conflict_detected | `colregs_reasoner_node.cpp:569–590`；发布 2 Hz | **恒 0**（设计 114）| ws_confidence ✓ | constraint.rationale ✓ | **PARTIAL_FIELDS**（schema_version=0）|
| `/l3/m6/rule_assessment` | 所有活跃规则（Rule13/14/15/17）均发 | `colregs_reasoner_node.cpp:608–624`；仅 Rule14 | N/A（msg 缺字段）| 0.91 hardcoded | trigger_conditions 列表 | **PARTIAL**（Rule13/15 漏发）|
| `/l3/sat/data` | sat2.colregs_chain[5] + tmr_s ODD-aware | `colregs_reasoner_node.cpp:714–743`；10 Hz | 恒 0 | ws_confidence(sat2) / **0.5 hardcoded**(sat3) | **sat2.reasoning_chain="" stub** | **STUB**（colregs_chain 空、tmr_s 60.0 hardcoded）|
| `/l3/asdr/record` | snapshot + 事件 | `colregs_reasoner_node.cpp:693–708`（snapshot）、:750–758（helper）| N/A | N/A | decision_json ✓ | **REAL** |

---

## 4. 已知缺陷（按严重度）

| 严重度 | 缺陷 | file:line | 类型 |
|---|---|---|---|
| CRITICAL | cpa_m/tcpa_s 来自 M2 但当前 M2 收到 SIL bridge 置零值（tgt.cpa_m=0.0, tgt.tcpa_s=0.0）→ M6 Rule 评估使用 CPA=0，所有目标恒触发 risk_of_collision，conflict_detected 恒 true | `docker/sil_topic_bridge.py:739–740`（bridge 负责）；`colregs_reasoner_node.cpp:804–806`（M6 直接消费）| 断流（bridge 创可贴问题，根因在 bridge 层）|
| HIGH | SAT-2 colregs_chain 从未填充 → M8 ColregsRationaleTree 前端渲染空 | `colregs_reasoner_node.cpp:732`（sat2.reasoning_chain=""）；`sat2.colregs_chain` 无赋值语句 | STUB（DEMO-2 P0）|
| HIGH | rule_assessment 仅 Rule14 发布，Rule13（追越）/ Rule15（交叉）锁定会遇无 assessment → M4 `latest_rule_assessment_` 在这两类场景为空 | `colregs_reasoner_node.cpp:608–624`（仅检查 key \| 14ULL）；`behavior_arbiter_node.cpp:113–114`（M4 存储）| 断流（Rule13/15 覆盖缺口）|
| HIGH | M5 不消费 M6 heading direction（primary_preferred_direction / numeric_value），仅用 active_rules.empty() 作二值标志，CPA/TCPA 权重砍 80%——M6 方向性约束对 MPC 轨迹无影响 | `mid_mpc_node.cpp:173–183`（3 行，无 direction/alteration 读）| 脱节（T4 方向信息断流）|
| HIGH | schema_version 恒 0 → ASDR 版本校验失效、下游无法检测 msg 格式变更 | `colregs_constraint_generator.cpp:43`；`colregs_reasoner_node.cpp:569–576` | 字段空 |
| MEDIUM | Rule16_GiveWay.evaluate() 独立于 Rule13/14/15 角色判定，仅检查 CPA < 1.5×cpa_safe → 可在 Rule14/15 latch 持有期间独立 chattering | `rule16_give_way.cpp:26`（cpa threshold only）；latch block `colregs_reasoner_node.cpp:530`（仅 rid==14\|\|15）| 脱节（Rule16 语义不准）|
| MEDIUM | SAT-3 predicted_state="nominal" hardcoded，不反映活跃相位（INDEPENDENT_ACTION / CRITICAL_ACTION）→ CMM forecast 失效 | `colregs_reasoner_node.cpp:737` | MOCK（伪装真实）|
| MEDIUM | SAT-3 tmr_s=60.0 hardcoded，非 ODD-aware（ODD-D 应 90 s，ODD-B 应 45 s）| `colregs_reasoner_node.cpp:741` | MOCK |
| LOW | M6 实际运行 2 Hz（reasoning_period_ms=500），spec 设计目标 1 Hz | `colregs_reasoner_node.cpp:231`（default 500 ms）、:448（注释 "2 Hz"）| 脱节（轻微，2 Hz 不低于设计频率）|
| LOW | Rule13 追越判断用 `relative_speed_kn > 0.0` 二值符号，近等速场景分类噪声大 | `rule13_overtaking.cpp:52–74` | 脱节（无滞后阈值）|
| LOW | `RuleAssessment.msg` 缺 schema_version / rationale CMM 字段 | `l3_msgs/msg/RuleAssessment.msg`（6 行，无这两字段）| 字段空（msg 层设计缺陷）|

---

## 5. 创可贴 / 越界逻辑

以下逻辑**本应在 M6 或 M5 实现**，当前错误地在 SIL bridge 或其它非 M 编号节点实现：

| 逻辑 | 当前错误位置 | 应归位 | 证据 |
|---|---|---|---|
| 60° 航向偏差 clamp（`MAX_AVOID_DEV_DEG=60.0`）| `docker/sil_topic_bridge.py:653–660` + `:1072–1078`（重复两处）| M6 ConstraintGenerator（作为 `constraints[].numeric_value` 上限）或 M5 MPC 状态约束 | bridge:653 |
| CPA/TCPA 几何计算（`_compute_dcpa_tcpa()`）| `docker/sil_topic_bridge.py:776–832`（flat-earth kinematic engine）| M2 World Model（唯一权威世界视图）；M6 直接消费 M2 TrackedTarget.cpa_m | bridge:776 |
| `fsm_aggregator_node` hardcode `active_rule='Rule 14 head-on'`（所有 COLREG_AVOIDANCE 场景）| `docker/fsm_aggregator_node.py:184–188` | 应读 M6 发布的真实 `applicable_rule` 字段 | aggregator:184 |
| `scoring_routes.py rule_chain=[]`（M6 rule chain 未接入评分）| `docker/orchestrator/scoring_routes.py:102` | 应复用 marzip_builder.py:175–186 `build_verdict()` 中已存在的 rule_chain 构建逻辑 | scoring_routes:102 |

---

## 6. 设计-实现脱节（overclaim 修正）

| 旧声称 | 实际 | 证据 |
|---|---|---|
| `M6-spec.md line 25`: "M6 推理已真实，仅缺 schema 序列化"（暗示 SAT-2 chain 基本 ready）| SAT-2 colregs_chain **从未赋值**；`publish_sat_data()` 的 sat2.colregs_chain 从未有一行赋值代码；`build_colregs_chain()` 结果仅注入 COLREGsConstraint，与 SATData 发布路径完全隔离 | `colregs_reasoner_node.cpp:732`（sat2.reasoning_chain=""）；build_colregs_chain 结果仅在 :587 注入 constraint，无 sat2 路径 |
| `M6-progress.md line 12`: "IDL + Arrow评分管线 ✅"（D2.4 状态）| COLREGsConstraint IDL 定义存在且 5 层链已计算并注入 constraint。但 SAT2Data.colregs_chain 序列化路径（DEMO-2 P0 指定目标）完全缺失；schema_version 恒 0。"IDL pipeline ✅" 表述误导性，应标 PARTIAL | `colregs_reasoner_node.cpp:718`（sat2.colregs_chain 无赋值）；`colregs_constraint_generator.cpp:43`（schema_version 默认 0）|
| "M6 推理已真实"（progress.md 旧行）| Rule 16 是 CPA 阈值代理（非真 Rule 16 义务检查）；Rules 13–18 geometry sector 不随 ODD 域调整；SAT-3 forecast 恒 nominal；Rule13 追越使用二值 relative_speed 符号无滞后 | `rule16_give_way.cpp:26`；all Rule13–18 有 `(void)odd`；`colregs_reasoner_node.cpp:737` |

---

## 7. D 任务联动表

| D 任务 | 关系 | 真实状态 | 备注 |
|---|---|---|---|
| D1.4 | Closed in | ✅ 2026-05-20 | M6 基础推理节点上线，规则库可加载，COLREGsConstraint 发布 |
| D2.4 | Currently Implementing | 🟡 PARTIAL | COLREGsConstraint 5 层 chain 真实计算 ✓；SAT-2 colregs_chain 序列化 ❌（核心 gap）；schema_version=0 ❌；Rule16 语义不准 ⚠️；目标 7/31 |
| D2.5 | Blocks | ⏳ BLOCKED | 前端 ColregsRationaleTree 依赖 D2.4 SAT-2 序列化完成才可接入 |
| D1.7 | Blocks | 🟡 PARTIAL | 6 维度评分 rubric 文档 6051 字已产出；scoring_routes 的 rule_chain 仍为 `[]`，M6 chain 未接入评分端点 |

---

## 8. DEMO 阻塞贡献

| 阻塞等级 | 说明 |
|---|---|
| **CRITICAL（DEMO-1 运行正确性）** | CPA/TCPA 全零（bridge 创可贴根因）→ M6 所有目标恒判 risk_of_collision → conflict_detected 恒 true → M4 持续 COLREG_AVOID → 无路线回航。根因在 bridge，M6 是受害者。 |
| **HIGH（DEMO-2 透明度）** | SAT-2 colregs_chain 从未填充 → M8 ColregsRationaleTree 渲染空 → COLREGs 5 层溯源链透明度功能整体不可演示（DEMO-2 P0）|
| **MEDIUM（决策一致性）** | M5 不消费 M6 heading direction → 避碰方向完全由 M4 IvP 硬编码右转决定，M6 Rule 8 "大幅早行动"方向输出被静默丢弃（T4 断流）|

---

## 参考 D 任务文档

- D2.4: [Phase 2/D2.4-m6-colregs-6d-scoring/](../../Phase%202/D2.4-m6-colregs-6d-scoring/)
- D1.7: [Phase 1/D1.7-coverage-metrics/](../../Phase%201/D1.7-coverage-metrics/)

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-22 | 初版 progress stub，D 任务表 |
| 2026-06-08 | 依系统审计重写 progress（状态矩阵 + gap + 创可贴 + overclaim 修正）；同步 D5/D6 修复事实；标注 CPA 零值根因（bridge 创可贴）；补充 DEMO 阻塞贡献 |
