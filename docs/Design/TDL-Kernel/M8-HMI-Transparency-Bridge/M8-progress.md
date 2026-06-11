# M8 · Progress · 实现现状对照

> **定位声明**：本文是 M8 的**实现现状**对照 spec.md 的设计目标。所有偏离 / 创可贴 / MOCK 记录于此并附 file:line。审计基线 `docs/Doc From Claude/2026-06-08-m1-m8-systemwide-gap-audit.md`（本模块条目已并入下表，但以**当前代码**为准）。

---

## 1. 头部

| 维度 | 说明 |
|---|---|
| 最近更新 | 2026-06-08（系统审计重写）|
| Currently Implementing | — |
| 当前分支 | `fix/m5-nlp-convergence`（M6 节点有未提交工作树编辑）|
| 当前 LOC | C++ 节点 ~1220 行 src / Python web_server ~800 行 |
| 审计代码基线 | `158bba9d`（M8 相关文件在当前分支未改动，结论仍有效）|

---

## 2. 实现状态矩阵

| 设计职责（对应 spec §）| 状态 | 证据 file:line | 备注 |
|---|---|---|---|
| SAT 多源聚合（§4.1）| PARTIAL | `sat_aggregator.cpp:11-25`（ingest 正确）| 设计：M4/M5/M6 经 `/l3/sat/data` 发布。实际：M4/M5/M7 直接发布 `/sil/` topic，绕过聚合路径；SatAggregator 中 M4/M5 缓存永远为空 |
| 自适应 SAT 触发（§4.4）| REAL | `adaptive_sat_trigger.cpp` 4 条件完整实现 | 但 Condition 3（系统置信度检查）因 M4/M5 缓存为空而无法有效触发 |
| UIState 构建（§4.2）| PARTIAL | `ui_state_builder.cpp:19-51` | `ship_position` / `ship_heading` / `ship_sog` 恒 0.0（注释"populated externally from nav filter"，但 M8 无 nav-filter 订阅）；`schema_version` 恒 0（从未赋值）|
| Operator State 发布（spec §3.2）| MISSING | `hmi_transparency_bridge_node.hpp` 全文 | `pub_operator_state_` / `/l3/m8/operator_state` 字符串在整个 m8 包中 0 出现；M1 `on_operator_state()` 因此永远收不到操作员状态，TMR 计算基于默认值 Bridge_OnDuty |
| ToR 协议状态机（§4.3）| REAL | `tor_protocol.cpp`；`TorProtocol::Config{60s, 5s, 30.0, 1}` @ `hmi_transparency_bridge_node.cpp:23-24`；`on_tor_tick` 2 Hz @ :296-308 | 四状态机（kIdle / kRequested / kAcknowledged / kTimeoutMrc）真实；60 s 超时触发 MRC 准备 + ASDR 记录 |
| ToR 请求发布（spec §3.2）| REAL | `hmi_transparency_bridge_node.cpp:358-381`；`pub_tor_->publish(req)` | stamp / rationale / confidence 已设；但下游订阅者（M4/M7）确认是否消费有待验证 |
| ToR HTTP → C++ TorProtocol 联动（spec §4.3）| STUB | `ros_bridge.py:100-103` | `send_operator_action()` 有 TODO(Phase-E2) 注释，直接 `return True`，不发布 ROS2 消息。浏览器点击"已知悉"后 C++ TorProtocol 实际保持 kIdle |
| ToR 自适应矩阵 4 场景（spec §5）| MISSING | m8 包 grep BNWAS/tor_matrix: 0 hits | TorProtocol 单一固定 `tor_deadline_s_=60s`，无 M1 tor_matrix[] 接入 |
| ASDR 事件审计（§4.1）| REAL | `asdr_logger.cpp`；`sha256.cpp`；`emit_asdr_event` @ :383-388；`on_asdr_snapshot_tick` @ :326-331 | SHA-256 签名真实；周期快照 + 事件触发均实装 |
| 模块健康监测（§4.1）| REAL | `module_health_monitor.cpp`；`on_health_check_tick` @ :314-319 | M7 心跳超时检测实装；`is_m7_timed_out()` 用于 on_sil_stub_tick 分支 |
| SIL 前端桥接 /sil/sat2_data（§3.2）| STUB | `hmi_transparency_bridge_node.cpp:406-417`（on_sil_stub_tick）| 冷启动阶段（无 BehaviorPlan 到达前）以 rationale='sil_stub' / 空 ivp_contributions[] / confidence=1.0 发布；M4 同时直接发布同 topic（behavior_arbiter_node.cpp:70），双发布 race |
| SIL 前端桥接 /sil/sat3_data（§3.2）| STUB | `hmi_transparency_bridge_node.cpp:419-432` | 同上，M5 也直接发布（mid_mpc_node.cpp:82），空 trajectory_candidates[] / tdl_s=0 |
| SIL 前端桥接 /sil/sotif_metrics（§3.2）| STUB | `hmi_transparency_bridge_node.cpp:434-446`（M7 心跳超时后才触发）| 正常运行时 M7 心跳存活 → 此 stub 从不执行；M7 自身 SotifMetricsPublisher.stub_mode_=true（sotif_metrics_publisher.hpp:21），永久输出全零 |
| Web REST 后端（§4.1）| PARTIAL | `python/web_server/app.py`；`tor_endpoint.py` | FastAPI 端点存在；但 `/api/tor/acknowledge` → ROS2 TorProtocol 链路断（见 ToR HTTP stub 行）|
| 双角色 ActiveRoleStateMachine（§2a）| STUB | `active_role.py:29-65`（类存在但无调用者）| 类完整实现但从未在 app.py 或任何 endpoint 中实例化；UIState role 硬编码为 kRocOperator（hmi_transparency_bridge_node.cpp:275）；无 HTTP 端点暴露角色切换 |
| C++ LifecycleNode（旧 spec 声称）| MISSING | `hmi_transparency_bridge_node.hpp:39` | 继承 `rclcpp::Node`，非 `rclcpp_lifecycle::LifecycleNode`；无 on_configure / on_activate 回调 |
| UIState 推送至前端（spec §3.2）| BROKEN | `useFoxgloveLive.ts` TOPIC_MAP 全文 0 次订阅 /l3/m8/ui_state | UIState 50 Hz 发布，但前端无消费者；sil_topic_bridge.py 转发至 /sil/m8_ui_state，前端同样不订阅 |
| BNWAS-equivalent stub（v3.0 工时）| MISSING | grep BNWAS/bnwas in m8: 0 hits | — |
| Y-axis Reflex Arc 通告通路（v3.0）| MISSING | grep reflex/Y.axis in m8: 0 hits | — |
| ECDIS 集成 stub（D3.4 v3.1）| MISSING | grep ECDIS/S-100 in m8: 0 hits | — |
| UIState 3 档拆分（50 Hz / 4 Hz / 事件）| STUB | 单一 `timer_ui_` 20 ms（:127-132），无 4 Hz display_state timer，无 alert_burst_event publisher | 全部合并进一条 /l3/m8/ui_state 流 |
| M3 MissionGoal 集成（§3.1）| PARTIAL（死订阅）| `hmi_transparency_bridge_node.cpp:189-191`（latest_mission_ = *msg）| 存储但 `latest_mission_` 从未出现在 `on_ui_publish_tick` / `UiStateBuilder::BuildContext`；死订阅 |

---

## 3. 接口实现对照

| topic | 设计（spec §3）| 实际 file:line | 字段填充（schema_version / confidence / rationale）| 状态 |
|---|---|---|---|---|
| `/l3/m8/ui_state` | 50 Hz，含 ship_position / ship_heading / ship_sog | `hmi_transparency_bridge_node.cpp:288-289`（stamp 设，confidence 从 behavior_plan 取，rationale 从 scenario 字符串建）| schema_version **恒 0**（从未赋值）；ship_position / heading / sog **恒 0.0**（无 nav-filter 订阅）| PARTIAL（字段空缺）|
| `/l3/m8/operator_state` | 事件驱动，操作员状态 | **不存在** | — | MISSING（无 publisher）|
| `/l3/m8/tor_request` | 事件驱动，ToR 触发时 | `hmi_transparency_bridge_node.cpp:107-108, 358-381` | stamp ✅；rationale ✅；confidence ✅；schema_version [需确认]| 连通（但下游订阅者不明确）|
| `/l3/asdr/record` | 2 Hz + 事件 | `:109-110, 326-331, 383-388` | stamp ✅；schema_version 需确认；SHA-256 ✅ | 连通 |
| `/sil/sat2_data` | 从 SAT 聚合器转发 M4 真实数据 | M8 stub @ `:406-417`；M4 真实 @ `behavior_arbiter_node.cpp:70` | confidence=1.0（stub）/ schema_version=114（stub）/ rationale='sil_stub' | 双发布 RACE；stub 伪装真实 |
| `/sil/sat3_data` | 从 SAT 聚合器转发 M5 真实数据 | M8 stub @ `:419-432`；M5 真实 @ `mid_mpc_node.cpp:82` | confidence=1.0（stub）/ rationale='sil_stub' | 双发布 RACE |
| `/sil/sotif_metrics` | 从 SAT 聚合器转发 M7 真实 SOTIF 指标 | M7 @ `sotif_metrics_publisher.cpp`（stub_mode_=true）；M8 stub 仅在 M7 超时后触发 | M7 stub_mode_=true → violation_score 全 0（sotif_metrics_publisher.hpp:21）| MASQ 伪装真实（永久绿）|

---

## 4. 已知缺陷

| 严重度 | 缺陷 | file:line | 类型 |
|---|---|---|---|
| CRITICAL | `/l3/m8/operator_state` 缺失 publisher — M1 TMR 计算永远使用默认 Bridge_OnDuty，操作员接管确认不传递 | m8 包全文 0 hits for `operator_state` publisher | MISSING |
| CRITICAL | M8 stub 与 M4/M5 真实数据**双重发布** `/sil/sat2_data` / `/sil/sat3_data`，冷启动窗口 stub 覆盖真实数据，可能导致前端 Engineer 视图错误渲染 | `hmi_transparency_bridge_node.cpp:406-432`；`behavior_arbiter_node.cpp:70`；`mid_mpc_node.cpp:82` | 断流/MOCK |
| CRITICAL | `/l3/m8/ui_state` **前端无消费者**——50 Hz UIState 完整黑洞，HMI operator_state / sat_decision / scenario 字段全部暗流 | `useFoxgloveLive.ts` TOPIC_MAP（无该订阅）| 断流 |
| HIGH | `ship_position` / `ship_heading` / `ship_sog` 恒 0.0 — M8 无 `/fusion/own_ship_state` 或 `/l3/m2/world_state` nav-filter 字段提取 | `ui_state_builder.cpp:48`（注释确认）| 字段空 |
| HIGH | ToR 浏览器点击 → C++ TorProtocol 链路断：`send_operator_action()` 直接 return True 不发 ROS2 消息 | `ros_bridge.py:100-103` | STUB/断流 |
| HIGH | `schema_version` 恒 0 —— 违反 CMM 契约（ADR-3）| `ui_state_builder.cpp:19-51`（无赋值）| 字段空 |
| HIGH | M7 SotifMetricsPublisher `stub_mode_=true` 永久输出全零 SOTIF 指标 | `sotif_metrics_publisher.hpp:21`（set_stub_mode(false) 0 calls）| MASQ 伪装真实 |
| MEDIUM | `ActiveRoleStateMachine` 实现完整但从未实例化；UIState role 硬编码 kRocOperator | `active_role.py:29-65`；`hmi_transparency_bridge_node.cpp:275` | 死代码 / STUB |
| MEDIUM | 类声明 `rclcpp::Node` 非 `rclcpp_lifecycle::LifecycleNode`，无生命周期回调 | `hmi_transparency_bridge_node.hpp:39` | 脱节 |
| MEDIUM | ToR 自适应矩阵（M1 tor_matrix[]）未接入 TorProtocol，单一固定 60 s | `hmi_transparency_bridge_node.cpp:23-24`；grep BNWAS: 0 | MISSING |
| MEDIUM | `latest_mission_`（M3 MissionGoal）存储后从未用于 UIState 构建 | `hmi_transparency_bridge_node.cpp:189-191`（死订阅）| 死代码 |
| LOW | M8 SAT2/SAT3 stub 以 `confidence=1.0` 发布空数据，前端无法区分 stub 与真实 | `:409,411`（stub confidence=1.0）| MASQ |

---

## 5. 创可贴 / 越界逻辑

以下是**当前错误地在 docker/sil_topic_bridge.py 中**，本应归属 M8 或其他 L3 模块的逻辑（依据 _bandaids.md 与审计报告）：

| 越界逻辑 | 当前位置 | 正确归属 | 严重度 |
|---|---|---|---|
| 完整航向 PD 控制器（HeadingController, Kp=1.0, max_rate=5°/s）+ 速度 PI 控制器 | `docker/sil_topic_bridge.py:145-182` | L4 Guidance（不属于 M8 或 L3）| CRITICAL |
| 避碰 arm/latch/teardown 状态机（`_avoidance_active`、`_LATCH_MIN_HOLD_S=8.0s`、`_AVOID_TRANSIT_RELEASE_S=3.0s`）| `docker/sil_topic_bridge.py:326,431,437` | M4 Behavior Arbiter + M5 | CRITICAL |
| 60° 最大航向偏差 clamp（`MAX_AVOID_DEV_DEG=60.0`）| `docker/sil_topic_bridge.py:653-660, 1072-1078` | M6 COLREGs Constraint Generator / M5 NLP bounds | HIGH |
| DCPA/TCPA 几何计算 `_compute_dcpa_tcpa()`，含 `CPA_SAFE_M=1000.0m` / `SHIP_LENGTH_M=46.0m`（ADR-4 违反）| `docker/sil_topic_bridge.py:776-832` | M2 World Model | HIGH |
| Cross-track error 回航控制器（XTE gain=0.10 deg/m, ±30° clamp, 速度提升至 19.5 kn）| `docker/sil_topic_bridge.py:1265-1327` | M5 BC-MPC / M4 Transit 行为 | HIGH |
| 死区开环舵令（avoidance_active + target=None 时固定舵令计算 `atan2(L, R)`，`SHIP_LENGTH_M=46.0` 硬编码）| `docker/sil_topic_bridge.py:1235-1245` | M5 / M4 | HIGH |

> 上述所有逻辑均属 ADR-4 Backseat Driver 模式的违反。M8 spec 明确不拥有这些职责。迁移路线：创建独立 D 任务，将各逻辑迁移至正确模块，删除 bridge 中的对应实现。

---

## 6. 设计-实现脱节（overclaim 修正）

| 旧 spec/progress 声称 | 实际状态 | 证据 |
|---|---|---|
| `C++ ROS LifecycleNode: ✅`（旧 M8-spec.md §当前实现状态）| 继承 `rclcpp::Node`，无 LifecycleNode，无 on_configure/on_activate | `hmi_transparency_bridge_node.hpp:39` |
| D3.4 `✅ SAT-2/3/SOTIF 桥接已实装；前端 Engineer 视图 4 组件数据源就绪` | M8 自身发布的 /sil/sat2_data + /sil/sat3_data 是空 stub（empty ivp_contributions[]、empty trajectory_candidates[]、rationale='sil_stub'）。真实内容来自 M4/M5 直接发布，M8 既非聚合方也非正确桥接方 | `hmi_transparency_bridge_node.cpp:406-432`（stub）；`behavior_arbiter_node.cpp:70`（M4 真实）；`mid_mpc_node.cpp:82`（M5 真实）|
| D0.1 `✅ MUST-7 active_role stub` — 隐含 stub 已功能性接入 | `ActiveRoleStateMachine` 类存在但**零调用者**，无任何 endpoint 实例化它，UIState role 硬编码 | `active_role.py:29`（类）；`app.py`（无实例化）；`hmi_transparency_bridge_node.cpp:275`（硬编码 kRocOperator）|
| D3.4 report `ToR 协议（自适应超时）: ✅` + `ToR 自适应矩阵: 🟡 当前基于 YAML 静态配置` | M1 parameter_loader 确实加载 tor_matrix[]（`m1/parameter_loader.cpp:158-168`），但 M8 从不订阅 M1 参数输出，TorProtocol 使用单一 60 s 固定值，无任何自适应矩阵接入 | `hmi_transparency_bridge_node.cpp:23-24`；grep tor_matrix in m8 package: 0 hits |
| `12 个 SAT-1 级 topic 真实发布: ✅`（旧 M8-spec.md）| /l3/m8/operator_state 从未存在（MISSING），/l3/m8/ui_state 前端无消费（断流），/sil/sotif_metrics 永久全零（stub_mode_=true）| 审计全文 §5 |

---

## 7. D 任务联动表

| D 任务 | 关系 | 状态 | 详情 |
|---|---|---|---|
| D0.1 | Closed in | ⚠️ PARTIAL（非 ✅）| MUST-7 active_role 类存在，但从未实例化/接入系统。功能断链，不视为完成 |
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：M8 HMI 裁剪集（代码规范本身已满足）|
| D1.3.2.3（原 D1.3b.3）| Closed in | ✅ 2026-05-25 | Web HMI 框架（MapLibre + foxglove + ToR ≥2s + SAT stale detection）——框架存在，但 ToR HTTP→C++ 链路断 + UIState 前端无消费为遗留偏离 |
| D3.4 | Claimed closed | ⚠️ PARTIAL（非 ✅）| 声称"SAT-2/3/SOTIF 桥接已实装"实为 stub；LifecycleNode 实为 rclcpp::Node；active_role 零接入；/l3/m8/operator_state MISSING；UIState 前端无消费。实质上是骨架在，功能性接口大部分断链 |
| D2.5 | Blocks | 🔴 阻塞（非 🟡）| M8 /sil/ topic 双发布 race + stub masquerade 导致 SIL 集成验证无效 |
| D2.6 | 联动 | 🟡 2026-05-22（框架完成）| 访谈数据 [TBD-D2.6]（§12.3 视图细节依赖）|
| D3.8 | Closed in（Wave 1）| ✅ 2026-08-25（Wave 1）| §21 S-57 管线文档 + foxglove 基准框架（AWAIT-D3.4 锚点仍有效）|
| D3.8 | Wave 2 等待 | ⏳ 等 D3.4 真正完成 | §21.1.2 foxglove 实测值填充（D3.4 evidence 文件）|

---

## 8. DEMO 阻塞贡献

| 阻塞 | 严重度 | 说明 |
|---|---|---|
| UIState 50 Hz 前端无消费 | HIGH | HMI 看不到任何 M8 聚合的态势信息；operator_state / sat_decision 字段全暗 |
| /l3/m8/operator_state MISSING | CRITICAL | M1 TMR 无法感知操作员接管确认，安全链中"接管已确认"信号永远不到位 |
| ToR 浏览器点击不通 C++ | HIGH | 操作员在 HMI 点击"已知悉"但 C++ TorProtocol 仍 kIdle，DEMO 演示接管流程为假流程 |
| /sil/sotif_metrics 永久绿 | MEDIUM | 前端 SotifMonitorStrip 无法演示 SOTIF 监测能力（D3.4 阻塞）|
| SAT-2 前端 schema mismatch | HIGH | `SAT2Data.msg` 定义 `float32[6] ivp_contributions`；HMI 期望 `IvpContribution[]` 对象（含 direction_deg 字段）→ Engineer 视图 IvP 雷达图空白或运行时报错 |

旧 progress 声称的 `✅ 已解除：M8 SAT-2/3/SOTIF 三 topic 已发布；前端 Engineer 视图 4 组件数据源就绪` **已修正为 PARTIAL/阻塞**（见 §6 overclaim 修正）。

---

## 9. 修订

| 日期 | 变更 |
|---|---|
| 2026-05-25 | 初版（D3.4 完成后更新，含 overclaim）|
| 2026-06-08 | 依系统审计 `158bba9d` 全面重写：状态矩阵、接口对照、gap 清单、创可贴越界逻辑列表、overclaim 修正（LifecycleNode / active_role / SAT 桥接 / ToR 自适应矩阵）；更新 D 任务联动表真实状态 |
