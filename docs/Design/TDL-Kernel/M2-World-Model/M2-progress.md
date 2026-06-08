# M2 · World Model · 实现现状（Progress）

> **定位声明**：本文是 M2 的**实现现状**，对照 `M2-spec.md` 的设计目标。所有偏离 / 创可贴 / MOCK / STUB 记录于此并附 file:line。
> 审计基线：`docs/Doc From Claude/2026-06-08-m1-m8-systemwide-gap-audit.md`（本模块条目并入下表，**以当前代码为准**，审计提交 158bba9d 后代码可能已变，codegraph 已复核标注）。

---

## 1. 头部信息

| 维度 | 说明 |
|---|---|
| 最近更新 | 2026-06-08 |
| Currently Implementing | — |
| 当前分支 | fix/m5-nlp-convergence |
| 当前 LOC | ~1860（含 9 个 .cpp：world_model_node / world_state_aggregator / cpa_tcpa_calculator / encounter_classifier / env_sanity_checker / view_health_monitor / track_buffer / enc_loader / coord_transform）|
| 节点入口 | `src/l3_tdl_kernel/m2_world_model/src/world_model_node.cpp` |

---

## 2. 实现状态矩阵

| 设计职责（对应 spec §）| 状态 | 证据 file:line | 备注 |
|---|---|---|---|
| ROS2 node + 4 Hz 发布（spec §4.2）| REAL | `world_model_node.cpp:414` `on_aggregation_timer()` → `publish_world_state()` | 配置 4 Hz；spec §时间尺度写 10-50 Hz，架构报告 §6.3 实际定义 4 Hz，无矛盾 |
| 三视图维护（DV/EV/SV）（spec §2.1）| PARTIAL | DV/EV 实现完整；SV EnvSanity 仅 3/7 检查 | 详见 §2/§4 |
| CPA/TCPA 权威计算（Eigen）（spec §5.1）| REAL | `world_state_aggregator.cpp:212-235` 调用 `cpa_calc_->compute()`；结果填 wt.cpa_m / wt.tcpa_s / wt.cpa_covariance_m2 / wt.tcpa_covariance_s2 | UKF-σ 协方差传播，参数 `m2_params.yaml cpa_covariance.method=ukf_sigma` |
| COLREG 几何预分类（spec §5.2）| REAL | `encounter_classifier.cpp:38-100` classify()；`m2_params.yaml:13-14` overtaking 112.5/247.5；调用 `world_state_aggregator.cpp:252-257` | MUST-1 参数化已落地，单元测试 4 边界 |
| OVERTAKING 扇区 [112.5°, 247.5°]（MUST-1）| REAL | `encounter_classifier.cpp:70-71` + `m2_params.yaml:13-14` + `test_must1_overtaking_sector.cpp` | 参数化，非硬编码 |
| SOG 校验 f(Manifest.max_speed×1.2)（MUST-6）| STUB/MASQ | `test_must6_sog_validation.cpp:13-16` file-local 匿名函数，**never called** from production；`world_state_aggregator.cpp:77` `snap.sog_kn = msg.sog_kn` 无任何校验 | **MASQ 伪装真实**：test passes 但 production 死代码；见 §6 overclaim |
| ODD 感知阈值选择（spec §2.1）| REAL | `world_state_aggregator.cpp:204-206` odd_cache_ 空时 fallback OddZone::A；`world_model_node.cpp:260-264` 订阅 /l3/m1/odd_state | 冷启动期用最保守 A 区 1852 m 阈值 |
| EnvSanity 校验（spec §2.1）| PARTIAL | `env_sanity_checker.cpp:14-73` 实现：staleness / current_speed / zone_transition 3 项；`env_sanity_checker.hpp:45-50` TODO 明确列出 4 项未实现（visibility / Hs / 跨源温度 / zone-current）| 3/7 检查，大多数环境输入通过无校验 |
| ViewHealthMonitor（spec §4.3）| REAL | `view_health_monitor.cpp:1-106`；DV/EV/SV 三视图健康均实现 | confidence 聚合 = min(dv, ev, sv) |
| BRG/RNG per-target（spec §4.1）| REAL | `world_state_aggregator.cpp:323` wt.brg_deg；lines 327-339 haversine wt.rng_m；`TrackedTarget.msg:26-27` | ARPA 表字段已落地 |
| intent_confidence scalar（spec §4.1）| PARTIAL | `world_state_aggregator.cpp:342-355`；`TrackedTarget.msg:22-23` float32 scalar | **仅标量**；spec §3.2 所写 intent_distribution[] 数组 **不存在**（WorldState.msg 无此字段）|
| ThreatState 发布（spec §3.2）| MISSING | `world_model_node.hpp:130-132` 仅 3 个 publisher；全 src/ grep 'threat_state' 零命中 C++ | bridge `sil_topic_bridge.py:418-420` 订阅此 topic，永远收不到；**avoidance teardown path 断流** |
| schema_version=112 填写（spec §3.3）| MISSING | `world_state_aggregator.cpp:463-483` build WorldState，全文 grep 'schema_version' 零命中；A4000 live sample schema_version=0 | CMM ADR-3 违规；TrackedTarget.schema_version 同样恒 0 |
| OwnShipState.r_dot_deg_s（spec §5.3）| STUB | `world_state_aggregator.cpp:372` `os_msg.r_dot_deg_s = 0.0`，注释"not available from OwnShipSnapshot — use defaults"；`world_model_node.hpp:107` OwnShipSnapshot 无 r_dot 字段 | M5 MPC 初始状态 yaw rate 恒 0 |
| OwnShipState.nav_mode（spec §2.2）| MASQ | `world_state_aggregator.cpp:381` `os_msg.nav_mode = "OPTIMAL"` 硬编码 | ADR-4 违规；应从 M1 ODDState 派生 |
| target classification（fishing/passenger…）| MASQ/DEAD | `world_state_aggregator.cpp:280-295` 代码存在但 `parameter_loader.hpp:48` default false；`m2_params.yaml` 无此 key → **分类分支在所有部署中是死代码** | 审计 [HIGH] |
| CMM current_state() + rationale()（spec §3.3）| PARTIAL | SAT topic（/l3/sat/data）携带 sat2.reasoning_chain + system_confidence；WorldState.rationale 字符串 populate | WorldState.schema_version 恒 0；无显式 current_state() RPC 接口 |
| CMM forecast(Δt)+uncertainty()（spec §3.3）| REAL | `world_model_node.cpp:546-560` compute_sat3_forecast() | SAT3 预测实现 |
| ASDR 记录（spec §3.2）| REAL | `world_model_node.cpp:571-583` publish_asdr_record()；stamp / source_module / decision_type / decision_json 均 populate | signature 字段有 TODO 清空占位（v1.1 标注）|

---

## 3. 接口实现对照

| topic | 设计（spec §3.x）| 实际 file:line | schema_version | confidence | rationale | 状态 |
|---|---|---|---|---|---|---|
| `/fusion/own_ship_state` | FilteredOwnShipState @ ~50 Hz | `world_model_node.cpp:239-243` | — 入消息 — | — | — | 连通 |
| `/fusion/tracked_targets` | TrackedTargetArray @ ~2 Hz | `world_model_node.cpp:246-250` | — 入消息 — | — | — | 连通 |
| `/fusion/environment_state` | EnvironmentState @ ~0.2 Hz | `world_model_node.cpp:253-257` | — 入消息 — | — | — | 连通 |
| `/l3/m1/odd_state` | ODDState（事件）| `world_model_node.cpp:260-264` | — 入消息 — | — | — | 连通 |
| `/l3/m2/world_state` | WorldState @ 4 Hz，schema=112 | `world_model_node.cpp:485` publish | **恒 0，未赋值** | 填写（health.aggregated）| 填写（健康摘要字符串）| **字段空（schema_version=0）**|
| `/l3/m2/world_state`.targets[i] | TrackedTarget，schema=112 | `world_state_aggregator.cpp:209-355` | **恒 0，未赋值** | 填写 | **rationale 未赋值，恒 ""** | **字段空（schema_version + rationale）**|
| `/l3/m2/threat_state` | ThreatState（应然），cpa_status / target_relative_position | 无 publisher；`world_model_node.hpp:130-132` 仅 3 个 pub | — | — | — | **断流：MISSING publisher** |
| `/l3/sat/data` | SATData @ 10 Hz | `world_model_node.cpp:495-568` | — （SAT 无此字段）| sat2.system_confidence 填写 | sat2.reasoning_chain 填写 | 连通 |
| `/l3/asdr/record` | ASDRRecord（2 Hz + 事件）| `world_model_node.cpp:571-583` | — | — | decision_json 填写 | 连通 |

---

## 4. 已知缺陷（按严重度）

| 严重度 | 缺陷 | file:line | 类型 |
|---|---|---|---|
| CRITICAL | `/l3/m2/threat_state` 无 publisher → bridge 解锁路径（`_on_threat_state`）永久死链，避碰退出依赖 bridge 自身重算几何逻辑 | `world_model_node.hpp:130-132`；`sil_topic_bridge.py:418-420, 867-877` | 断流 / MISSING |
| HIGH | `WorldState.schema_version` 恒 0（未赋值）→ CMM ADR-3 违规，M7 SOTIF 假设验证输入字段失效 | `world_state_aggregator.cpp:463-483`；`WorldState.msg:3` | 字段空 |
| HIGH | `TrackedTarget.schema_version` 恒 0、`TrackedTarget.rationale` 恒 ""（未赋值）| `world_state_aggregator.cpp:209` wt 初始化后从未赋 schema_version / rationale | 字段空 |
| HIGH | MUST-6 SOG 校验是 test-only 死代码，production 无 Manifest 注入，`update_own_ship()` 无条件赋值 | `world_state_aggregator.cpp:77`；`test_must6_sog_validation.cpp:13-16` | MASQ 伪装真实 |
| HIGH | `nav_mode='OPTIMAL'` 硬编码 → ADR-4 违规 | `world_state_aggregator.cpp:381` | MASQ |
| HIGH | target classification 分支永远不执行（default false，yaml 无 key）| `world_state_aggregator.cpp:280`；`parameter_loader.hpp:48`；`m2_params.yaml` | MASQ/DEAD |
| HIGH | BC-MPC 订阅 `/m2/world_state`（无 `/l3/` 前缀）→ 与 M2 实际发布 namespace 不匹配，BC-MPC 永远收不到 WorldState | `mid_mpc_node.cpp` BC-MPC subscription；`world_model_node.cpp:271` 发布 `/l3/m2/world_state` | 断流（namespace 错）|
| MEDIUM | `OwnShipState.r_dot_deg_s` 硬编码 0.0 → M5 MPC 初始 yaw rate 恒 0，影响 MPC 精度 | `world_state_aggregator.cpp:372` | STUB |
| MEDIUM | EnvSanityChecker 仅 3/7 检查（缺 visibility / Hs / 跨源温度 / zone-current）| `env_sanity_checker.hpp:45-50` TODO；`env_sanity_checker.cpp:14-73` | PARTIAL |
| MEDIUM | bridge 重算 CPA/TCPA（`_compute_dcpa_tcpa`），与 M2 已算值并存无调和 → 双份 CPA 源 | `sil_topic_bridge.py:775-832, 789`（bridge 注释明确标为 interim）| 创可贴 |
| LOW | intent_distribution[] 数组字段缺失（`WorldState.msg` / `TrackedTarget.msg` 无此字段），仅 intent_confidence scalar | `WorldState.msg`（全文无 intent_distribution）；`TrackedTarget.msg:22-23` | 脱节 |
| LOW | ASDR signature 字段显式清空（TODO v1.1）| `world_model_node.cpp:583` | TODO |

---

## 5. 创可贴 / 越界逻辑

以下逻辑**本应在 M2**，当前错位在 `docker/sil_topic_bridge.py`：

| 逻辑 | 当前位置 file:line | 正确归属 | 影响 |
|---|---|---|---|
| CPA/TCPA 二次计算（`_compute_dcpa_tcpa` 静态方法）| `sil_topic_bridge.py:775-832` | M2 ThreatState（cpa_status / target_relative_position）| 两份 CPA 数值并存，bridge 用自算值做解锁决策；M2 已算值被忽略 |
| `CPA_SAFE_M = 1000.0`（硬编码安全阈值）| `docker/sil_topic_bridge.py:108` | M6 COLREGs 约束参数 或 M2 ODD 阈值 yaml | ADR-4 违规 |
| avoidance latch-release 条件 1（`_on_threat_state`）| `sil_topic_bridge.py:867-877` | M2 → M4（通过 ThreatState 驱动行为切换）| 目前永远不触发（topic 无 publisher）|
| shell_b_harness 发布 cpa_m=0.0 给 M2 | `shell_b_harness/simulator.py:472-473` | 应由 M2 自行计算 | RL loop 中 M5/M6 CPA 输入恒 0 |

---

## 6. 设计-实现脱节（Overclaim 修正）

以下为旧 progress.md / D 任务报告中声称 ✅ 但实际有偏差的条目：

| 声称 | 来源 | 实际 | 证据 |
|---|---|---|---|
| "D0.1 Closed: MUST-6 sog 校验改读 Manifest" | M2-progress.md D0.1 行 | validate_sog() 仅在 `test_must6_sog_validation.cpp:13-16` 匿名命名空间，是 test-only 死代码；production `update_own_ship()` 无任何 Manifest 引用 | `world_state_aggregator.cpp:74-100`；`test_must6_sog_validation.cpp:1-44` |
| "D1.3.2.3 Closed: CPA/TCPA 真发布到 /sil/cpa_tcpa（foxglove_bridge 消费端落地）" | M2-progress.md D1.3.2.3 行 | bridge **不消费** M2 的 cpa_m / tcpa_s，而是在 `sil_topic_bridge.py:775-832` 本地重算；bridge 仅在 `sil_topic_bridge.py:399-400` 记录 M2 pulse（不提取 CPA 值）；未找到 /sil/cpa_tcpa publisher | `sil_topic_bridge.py:789`（明确注释 "interim measure"）|
| "D2.2 Closed: intent_confidence 字段已落地（B P1-B-02 决议闭环）" | M2-progress.md line 21 | **部分真实**：intent_confidence scalar（float32）已落地；但 spec §3.2 所写 intent_distribution[] 数组字段不存在于 WorldState.msg 或 TrackedTarget.msg；B P1-B-02 的数组要求**未闭环** | `WorldState.msg`（全文）；`TrackedTarget.msg:22-23` |
| "D2.2 Closed: env sanity 落地（~900 LOC C++ + 18 Python tests）" | M2-progress.md line 14 | EnvSanityChecker 代码存在且在 production 路径被调用，但 `env_sanity_checker.hpp:45-50` 明确 TODO 4 项未实现（visibility / Hs / 跨源温度 / zone-current）；声称"落地"掩盖了 4/7 检查缺失 | `env_sanity_checker.hpp:45-50`；`env_sanity_checker.cpp:14-73` |
| "不阻塞：intent_confidence 字段已落地" | M2-progress.md §DEMO-2 阻塞贡献 | scalar 已落地；array 未落地，如 M4 需要 multi-modal intent 分布则数据缺失 | 同上 |
| "不阻塞：BRG/RNG 已落地" | M2-progress.md §DEMO-2 阻塞贡献 | ✅ 正确：brg_deg / rng_m 字段实际在 `world_state_aggregator.cpp:323, 327-339` 真实计算 | 此项 **无 overclaim**，保留 ✅ |

---

## 7. D 任务联动表

| D 任务 | 关系 | 状态 | 真实情况 / 偏差 |
|---|---|---|---|
| D0.1 | Closed in | ⚠️ PARTIAL | MUST-1（OVERTAKING 扇区）✅ 真实落地，参数化+测试；MUST-6（SOG 校验）**MASQ**：test-only 死代码，production 无效 |
| D1.3.2.3 | Closed in | ⚠️ PARTIAL | Web HMI CPA/TCPA 显示链路：HMI 从 ASDR 事件读 cpa_nm/tcpa_min，bridge **重算** CPA 而非消费 M2 output；"CPA/TCPA 真发布"描述不准确 |
| D1.4 | Closed in | ✅ | 编码规范 v1.2 全模块适用，M2 无违反迹象 |
| D2.2 | Closed in | ⚠️ PARTIAL | UKF 协方差链 ✅；BRG/RNG ✅；intent_confidence scalar ✅；intent_distribution[] array ❌；EnvSanity 3/7 ⚠️；MUST-1+6 验证：MUST-1 ✅，MUST-6 MASQ ❌；schema_version 恒 0 ❌ |
| D2.5 | Blocks | ⏳ | SIL 集成依赖 M2 真实输出；intent_distribution[] 缺失 + threat_state 断流均可能影响集成验收 |

---

## 8. DEMO 阻塞贡献

| 评估 | 项目 | 说明 |
|---|---|---|
| 🔴 **DEMO 阻塞** | `/l3/m2/threat_state` 断流 | bridge 解锁路径 `_on_threat_state` 永远不触发；依赖此路径的 avoidance teardown 条件 1 死链；当前 DEMO-1 靠 bridge 几何释放条件 2 兜底，但体系不完整 |
| 🔴 **DEMO 阻塞** | BC-MPC namespace 断流 | BC-MPC 订阅 `/m2/world_state` 而非 `/l3/m2/world_state`，BC-MPC 侧无世界视图输入 |
| 🟡 **潜在阻塞** | `schema_version=0` | CMM 合规性问题，M7 SOTIF 若依赖此字段做版本校验则失效 |
| 🟡 **潜在阻塞** | MUST-6 dead code | SOG 越限目标被无条件接受，极端场景下影响 CPA 计算可信度 |
| 🟢 **不阻塞** | BRG/RNG | ARPA 表字段已落地 |
| 🟢 **不阻塞** | CPA/TCPA 主功能 | M2 计算正确，M4/M5/M7 消费正常（4 Hz 4 个方向连通）|
| 🟢 **不阻塞** | UKF 协方差链 | 协方差传播已落地 |

---

## 参考 D 任务文档

- D2.2: [Phase 2/D2.2-m2-world-model-enc/](../../Phase%202/D2.2-m2-world-model-enc/)（spec + plan）
- D1.3.2.3: [Phase 1/D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2.3-web-hmi/](../../Phase%201/D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2.3-web-hmi/)

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-21 | 初版 progress，D2.2 关闭 |
| 2026-06-08 | 依 2026-06-08 系统审计（基线 158bba9d）重写 progress：新增实现状态矩阵 / 接口对照 / 缺陷表 / 创可贴清单 / overclaim 修正；更新 D 任务联动表（修正 D0.1 MUST-6、D1.3.2.3、D2.2 intent_distribution 等虚标 ✅）；DEMO 阻塞贡献更新至真实状态 |
