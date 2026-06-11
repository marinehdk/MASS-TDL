# M3 · Mission Manager · Progress

> **定位声明**：本文是 M3 的**实现现状**对照 M3-spec.md 的设计目标。所有偏离 / 创可贴 / MOCK 记录于此并附 file:line。审计基线：docs/Doc From Claude/2026-06-08-m1-m8-systemwide-gap-audit.md（本模块条目已并入下表，但以**当前代码**为准）。

---

## 1. 头部

| 维度 | 说明 |
|---|---|
| 最近更新 | 2026-06-08 |
| Currently Implementing | — |
| 当前分支 | `fix/m5-nlp-convergence`（M3 文件未变动）|
| 当前 LOC | ~1488（`mission_manager_node.cpp`）|
| 审计代码版本 | `158bba9d`（当前 main `87315c82`，M3 未被后续 commit 触及）|

---

## 2. 实现状态矩阵

| 设计职责（对应 spec §） | 状态 | 证据 file:line | 备注 |
|---|---|---|---|
| **FSM 生命周期（§4.3）** 7 态完整 | REAL | `mission_state_machine.cpp:57-129` | Init→Idle→TaskValidation→AwaitingRoute→Active→ReplanWait→MrcTransit 全实现 |
| **VoyageTask 校验（§4.1）** 7 条件 | REAL | `mission_manager_node.cpp:392-393` validator_->validate() | VoyageTaskValidator 7 条件验证 |
| **L2 PlannedRoute 接收 + WP 进度（§4.1）** | REAL | `mission_manager_node.cpp:453-487`（路径接收）+ `:575-598`（WP 进度）| haversine 推进 WP 索引正常 |
| **ETA 投影（§4.1, §5.1）** | PARTIAL | `mission_manager_node.cpp:801-806` eta_projector_->project() | ETA 值计算正常，但 **speed_recommend_kn 永为 0**（下见缺陷 D2）|
| **task_validity 四条件门（§4.4）** | PARTIAL | `mission_state_machine.cpp:136-148` + `mission_manager_node.cpp:625` | 逻辑框架正确，但 **has_enc_check 硬编码 true**（缺陷 D3）|
| **RouteReplanRequest 触发（§5.2）** | PARTIAL | `mission_manager_node.cpp:967-1013` check_and_trigger_replan() | 触发链正常，但 **planned_eta_s 硬编码 0.0**（缺陷 D4）+ exclusion_zones 永为空（缺陷 D6）|
| **MissionGoal 发布 @0.5 Hz（§3.2）** | REAL | `mission_manager_node.cpp:252-254` setup_publishers() + `:331-338` timer | 发布正常；schema_version=121（实际代码是 121，非旧 progress 声称的 120）|
| **MissionState 发布（§3.2）** | MISSING | `mission_manager_node.cpp:250-267` setup_publishers() | **M3 无该 topic 的 publisher**；M1 在 `odd_envelope_manager_node.cpp:459` 订阅但永远收不到数据 |
| **CurrentErrorMonitor（§4.1）** | REAL | `mission_manager_node.cpp:693-723` check_current_error_severity_change() | XTE + 海流严重度三级分类正常 |
| **L1WatchdogMonitor（§4.1）** | PARTIAL | `mission_manager_node.cpp:654-660` evaluate_l1_watchdog() | 代码存在且正确，但 **l1_watchdog_bypass_=true（默认）** 使其在所有 SIL 部署中静默失效（缺陷 D5）|
| **ToR 请求（§3.2）** | PARTIAL | `mission_manager_node.cpp:264-267` tor_pub_ + `:953-964` publish_tor_request() | publisher 存在，但：1）watchdog bypass 使其从不触发；2）`/l3/m3/tor_request` 无生产订阅者（缺陷 D5）|
| **ASDR 审计流（§3.2）** | PARTIAL | `mission_manager_node.cpp:942-951` publish_asdr_record() | stamp/payload 有值，但 **schema_version 永为 0**（缺陷 D7）|
| **ENC 路径校验（§2.1）** | STUB | `mission_manager_node.cpp:625` bool has_enc_check = true | EncRouteValidator 类文件不存在（缺陷 D3）|
| **speed_recommend_kn（§5.1）** | MISSING | MissionGoal.msg:6；grep 0 匹配 | EtaProjector 无 compute_speed_recommendation() 方法（缺陷 D2）|
| **RouteReplanRequest.confidence 动态计算（§5.3）** | STUB | `mission_manager_node.cpp:937` msg.confidence = 1.0F 硬编码 | 缺陷 D8 |
| **ToRRequest.schema_version（§3.3 CMM）** | STUB | `mission_manager_node.cpp:953-964` 无 schema_version 赋值 | 缺陷 D7 变种 |

---

## 3. 接口实现对照

| topic | 设计（spec §3） | 实际 file:line | 字段填充（CMM 四字段）| 状态 |
|---|---|---|---|---|
| `/l3/m3/mission_goal` | M4/M5/M1/M8 消费，0.5 Hz，schema=121 | `:252-254` + `:331-338` | stamp ✓ / schema_version=121 ✓（代码 `:737,766`）/ confidence ✓（动态）/ rationale ✓ | 连通；**speed_recommend_kn=0 字段空**；fsm_state ✓；task_validity ✓ |
| `/l3/m3/mission_state` | M1 消费 0.5 Hz，MRC 上下文 | **无 publisher** | — | **断流（publisher 缺失）**；M1 `odd_envelope_manager_node.cpp:459` 永收不到数据，MRC 选择回退默认值 |
| `/l3/m3/route_replan_request` | L2 消费，事件触发，schema=120 | `:256-258` + `:907-939` | stamp ✓ / schema_version=120 ✓ / confidence=1.0F 硬编码 / rationale ✓ | 连通；**exclusion_zones 永为空**；confidence 未动态计算 |
| `/l3/m3/tor_request` | M8 消费，事件触发 | `:264-267` + `:953-964` | stamp ✓ / schema_version 未赋值（=0）/ confidence 硬编码 1.0F / rationale ✓ | **死端（无生产订阅者）**；watchdog bypass 使触发路径被跳过 |
| `/l3/asdr/record` | ASDR 系统，事件触发 | `:260-262` + `:942-951` | stamp ✓ / **schema_version=0（从不赋值）** / confidence 缺 / rationale 有 | 部分连通；audit trail schema_version 字段缺失 |
| `/l1/voyage_task` 订阅 | L1 事件触发 | `:275-280` | — | 连通（生产侧为 mock_l2_publisher）|
| `/l2/planned_route` 订阅 | L2 事件触发 | `:282-287` | — | 连通（生产侧为 mock_l2_publisher）|
| `/l3/m1/odd_state` 订阅 | M1 0.1-1 Hz | `:303-308` + `:561` | — | 连通；**planned_eta_s 硬编码 0.0 传入 check_and_trigger_replan**（缺陷 D4）|
| `/l3/m2/world_state` 订阅 | M2 10-50 Hz | `:310-315` | — | 连通 |
| `/l4/tracking_error` 订阅 | L4 ~10 Hz | `:317-322` | — | 连通（生产侧为 SIL bridge 推送）|

---

## 4. 已知缺陷

| 严重度 | 缺陷 | file:line | 类型 |
|---|---|---|---|
| **CRITICAL** | D1：`/l3/m3/mission_state` publisher 缺失——M1 MRC 上下文永远为空，water_depth_m=0 / in_anchorage_zone=false / is_moored=false 静默偏向 MRM-01 Drift | `mission_manager_node.cpp:250-267`（仅 4 个 publisher，无 MissionState）| 断流（publisher 完全缺失）|
| **HIGH** | D2：`speed_recommend_kn` 永为 0——M4/M5 速度指导完全缺失；MissionGoal.msg:6 字段声明但节点中无任何赋值（grep 0 匹配）| `mission_manager_node.cpp:729-851` publish_mission_goal() | MOCK 伪装真实（消费者无法区分 0 = 不限速 vs. 数据缺失）|
| **HIGH** | D3：`has_enc_check = true` 硬编码——ENC 校验桩使 task_validity 四条件门退化为三条件；消费者收到 TASK_VALIDITY_VALID 但航路从未经 ENC 验证 | `mission_manager_node.cpp:625` | MOCK 伪装真实（MASQ）|
| **HIGH** | D4：`planned_eta_s = 0.0` 硬编码传入 check_and_trigger_replan——MissionInfeasible 触发比较无意义；每次 ODD 更新若 current_eta_s > 0+600s 均会触发（cooldown 10s 压制大部分）| `mission_manager_node.cpp:561` on_odd_state() | MOCK 伪装真实（MASQ）|
| **HIGH** | D5：`l1_watchdog_bypass_=true`（默认）——L1 看门狗 + ToR 触发链在所有当前 SIL 部署中静默失效；IT-01~IT-06 测试结论在生产环境无法复现 | `mission_manager_node.cpp:54,64,118,654-660` | STUB（显式 bypass，非隐藏）|
| **MEDIUM** | D6：`RouteReplanRequest.exclusion_zones` 永为空——L2 重规划收不到 M3 的空间约束；RFC-006 锁定格式但字段从未填充 | `mission_manager_node.cpp:907-939` publish_replan_request() | 断流（字段未填）|
| **MEDIUM** | D7：`ASDRRecord.schema_version` 永为 0——审计流版本字段空；`ToRRequest.schema_version` 同样未赋值（M1 版本设为 121）| `mission_manager_node.cpp:942-951` + `:953-964` | CMM 字段空（schema_version）|
| **MEDIUM** | D8：`RouteReplanRequest.confidence` 硬编码 1.0F，`ToRRequest.confidence` 同 | `mission_manager_node.cpp:937,960` | STUB（confidence 动态计算未实现）|
| **LOW** | D9：haversine 实现在 `mission_manager_node.cpp` 与 `voyage_task_validator.cpp` 中重复——两处独立实现可维护性差（gap-fix-plan Task E）| `mission_manager_node.cpp:28-43` anonymous namespace | 代码质量 / 重复 |
| **LOW** | D10：`/l3/m3/tor_request` 无生产订阅者——M3 的 L1 dropout ToR 链是死端；M8 订阅 `/l3/m8/tor_request`，M1 订阅 `/l3/m1/tor_request`，M3 的 topic 只有测试订阅 | `mission_manager_node.cpp:265` + grep 结果 | 断流（topic 孤立，无消费者）|

---

## 5. 创可贴 / 越界逻辑

以下逻辑**本应在 M3 或 M5**，当前却在别处实现：

| 创可贴位置 | 内容 | 本应归属 | 证据 |
|---|---|---|---|
| `docker/sil_topic_bridge.py:1265-1286` | 回航 XTE 比例控制器（gain=0.10 deg/m，cap=±30°，速度提升至 19.5 kn when \|XTE\|>150m）| M5 BC-MPC 或 M4 Transit IvP | `_signed_xte_m()` + `_compute_transit_autopilot()` |
| `docker/sil_topic_bridge.py:145-182` | 完整 autopilot（HeadingController PD Kp=1.0 + SpeedController PI）| L4 Guidance / M5 → L4 接口 | `HeadingController` class + `SpeedController` class |
| `docker/mock_l2_publisher.py` | 完整 L2 接口：synthesizes VoyageTask + PlannedRoute + SpeedProfile + 永远返回 SUCCESS 的 ReplanResponse | L2 Voyage Planner（外部真实系统）| `_on_replan_request()` |

**正确归位方案**：
- 回航 XTE 控制 → M5 BC-MPC（短时 horizon MPC 已规划于 D3）
- Autopilot → 不应在 L3 内，应由 L4 Guidance 接收 (ψ_cmd, u_cmd) 后自行实现
- L2 mock → 保留为测试基础设施，但明确标记为 mock，不应混入生产部署路径

---

## 6. 设计-实现脱节（overclaim 修正）

| 原声称（旧 progress / spec）| 实际状态 | 证据 |
|---|---|---|
| "D1.4 closed ✅ 2026-05-20" | M3 基础框架合并，但当时 `mission_state` publisher、`speed_recommend`、ENC 校验均未实现。7 项 GAP 在 M3-gap-fix-plan.md（2026-06-08 创建）中全部未关闭（所有复选框未勾）。 | `M3-gap-fix-plan.md`（v1.0）所有 `- [ ]` 未勾 |
| "D2.3 closed ✅ 2026-05-21: IDL v1.2.0 schema_version=120" | 实际代码使用 schema_version=121（W3 BUMP，`mission_manager_node.cpp:737,766`）；旧 progress 声称的 120 已过时；ASDRRecord schema_version 从不赋值（永为 0）。 | `mission_manager_node.cpp:737,766` |
| "D2.3 closed: Closes F P1-F-01 (L1/L2 independence IT-01~IT-06)" | `l1_watchdog_bypass_=true`（默认值）使看门狗在 SIL 生产部署中完全静默。IT-01~IT-06 结果在 `bypass=false` 环境下有效，生产环境无法复现。 | `mission_manager_node.cpp:54,64,118,654-660` |
| "ODD-B → RouteReplanRequest chain: verified ≤ 2s" | 触发链本身有效，但 `planned_eta_s=0.0` 使 MissionInfeasible 条件永远成立（靠 10s cooldown 压制），ETA 比较语义完全错误。 | `mission_manager_node.cpp:561` |
| "speed_recommend 已实现（D1.4 spec 中列为 ✅）" | EtaProjector 无 `compute_speed_recommendation()` 方法；`publish_mission_goal()` 中 speed_recommend_kn 从不赋值；grep 0 匹配。 | `mission_manager_node.cpp:729-851` |

---

## 7. D 任务联动表

| D 任务 | 关系 | 状态 | 偏差说明 |
|---|---|---|---|
| D1.4 | Closed in | ⚠ 部分完成 | FSM 框架 + 接口合并；但 mission_state publisher、speed_recommend、ENC 校验、confidence 动态计算均未实现（见 M3-gap-fix-plan.md 7 GAP 全开）|
| D2.3 | Closed in | ⚠ 部分完成 | CurrentErrorMonitor + L1WatchdogMonitor 代码实现正确；但 l1_watchdog_bypass_=true 使生产侧 IT 保证失效；IDL version 声称 120 但实际代码已升 121；ASDRRecord schema_version 未填 |
| D2.8（GAP-1 ENC 校验）| 待启动 | 未开始 | M3-gap-fix-plan.md Task Group A 全部步骤未勾 |
| D2.9（GAP-2 speed_recommend）| 待启动 | 未开始 | M3-gap-fix-plan.md Task Group B 全部步骤未勾 |
| D2.10（GAP-4 confidence 动态 + GAP-5 timeout 参数）| 待启动 | 未开始 | M3-gap-fix-plan.md Task Group C/D 全部步骤未勾 |

---

## 8. DEMO 阻塞贡献

| 阻塞项 | DEMO 影响 | 优先级 |
|---|---|---|
| D1：`/l3/m3/mission_state` publisher 缺失 | M1 MRC 类型选择恒为默认值（MRM-01 Drift）——任何 DEMO 场景中 MRC 执行路径错误 | P0 CRITICAL |
| D3：has_enc_check 硬编码 | task_validity 语义不完整，下游 M4/M5 收到虚假 VALID | P0 HIGH |
| D2：speed_recommend_kn=0 | M4/M5 速度指导缺失——DEMO 中速度控制全靠 bridge 创可贴 | P0 HIGH |
| D4：planned_eta_s=0.0 | MissionInfeasible 触发语义错误——cooldown 在高压场景可能被穿透，造成虚假重规划 | P1 MEDIUM |
| D5：watchdog bypass | L1 dropout 场景（DEMO safety scenario）无法激活 ToR | P1 MEDIUM |
| D10：ToR 死端 | 即使 watchdog 激活，M3 ToR 也无消费者 | P1 MEDIUM |

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-21 | 初版（D1.4 + D2.3 合并记录）|
| 2026-06-08 | 依系统审计重写 progress（实现状态矩阵 + 接口对照 + Gap 矩阵 + 创可贴 + overclaim 修正）；更新 D 任务联动表真实状态 |
