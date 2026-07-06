# M5 Committed Route Spec (v2 — Post-Review)

- Date: 2026-06-30 (v2)
- Worktree: `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug`
- Branch: `codex/colregs-12probe-debug`
- Supersedes: `2026-06-30-m5-committed-route-design.md` (v1，保留作历史，不再作为执行依据)
- Review basis: 三方独立评审（COLREGs 合规 / 架构合理性 / 船级社认证可行性）+ 6 个决策点拍板 + 两路代码调研（NLP 凸性 / 现有 past-clear·CPA 判断）

## Revision History

| 版本 | 日期 | 变更摘要 |
| --- | --- | --- |
| v1 | 2026-06-30 | 初版：CommittedAvoidanceRoute + TailBuilder + 单 truth |
| v2 | 2026-06-30 | 评审后修订：(1) 新增 COLREGs 角色矩阵含 stand-on 分支；(2) M6 语义独占契约 + M6 msg 扩字段前置依赖；(3) s_clear 复用方案（消费 M6 信号 + M2 几何量，禁止重判语义）；(4) keep-last-route ≤45s 风险门控；(5) 承认 Mid-MPC 非凸 NLP + SOTIF/policing-function 认证论证（路径 A）；(6) confidence 量化定义；(7) 架构报告同步修订清单；(8) forecast(Δt)+uncertainty CMM 补全；(9) MRM 措辞对齐报告 §11.6 指挥链；(10) 频率契约 + TMR 对齐 |

## 0. Scope & How To Read This Spec

本 spec 是 M5 Tactical Planner 输出契约、航线承诺、GNC 移交、COLREG 恢复生命周期的**权威设计**。v2 是评审与决策固化后的执行依据；v1 仅作历史。

**阅读顺序建议**：
- 实现者：§3 Design Decision → §4 角色矩阵 → §9 NLP→完整航线链 → §14 M6 信号契约（前置）→ §19 Slices → §20 Acceptance。
- 认证/架构审查：§13 非凸论证 → §18 架构报告同步清单 → §25 决策记录。
- M6 维护者：§14 M6 信号契约（含 msg 扩字段）。

**自洽性约定**（全文统一，不得歧义）：
- `H_pred` = Mid-MPC 预测时域 = **90 s**（N=18, dt=5 s）。≠ 完整航线长度。
- `H_commit` = 不可变 prefix 边界 = GNC 最小未来更新距离以内的几何 + 已被本船越过的航点。
- `H_publish` = 发布心跳 = **60 s**（开阔水域 normal ODD）或 **10 s**（狭水道/高风险 emergency ODD profile）。
- `valid_until` ≥ **TMR = 60 s**（即使 `H_publish=10 s`，`valid_until` 不缩短；见 §6/§16）。
- `stale_route_max_age_s` = **45 s**（= H_pred/2，keep-last-route 上限，见 §9.12）。
- segment source 标签仅五个：`MID_MPC_OPTIMIZED` / `MID_MPC_TERMINAL_HOLD` / `REJOIN_TO_L2` / `L2_NOMINAL_SUFFIX` / `DEGRADED_CORRIDOR`。
- lifecycle 八态：`Idle` / `CandidateReady` / `CommittedAvoidance` / `ExecutingAvoidance` / `RevisingFutureSuffix` / `CommittedReturn` / `Completed` / `DegradedHold`。
- 语义权威分工：**M6 独占** COLREGs side/role/phase/past-clear 语义判断；**M2 独占** COLREG 几何预分类与 CPA/TCPA/covariance 数值；**M5** 只消费上述信号 + 做 route-frame 几何外推/GNC feasibility；**M7** 独立 checker veto；**GNC** 持 feasibility acceptance 最终权威。

## 1. Purpose

M5 必须停止作为两个独立的避让路径发布者，转为单一 M5 拥有的 committed route 生命周期：

- M5 为每个活动会遇维护**一份完整**的避让航线真相源。
- 完整航线含 Mid-MPC 避让几何、终端延伸、回归几何、下游 nominal 后缀，作为**一个版本化列表**。
- GNC 接收**完整活动航线版本**，而非 append-only 片段流。
- M5 内部可 append/replace 未来后缀，但**每次对 GNC 的发布都是全量快照 revision**。
- M5 可频繁重算候选，但**仅在**航线 commit、accepted revision 变更、release 转换、低频心跳时发布。
- 所有发给 GNC 的航线几何**在发布前必须通过 GNC 航线约束 preflight**。

v2 新增目标（来自评审）：
- **COLREGs 全角色合规**：give-way（Rule 13/14/15/16）与 stand-on（Rule 17 / 被追越）分支均须显式处理，stand-on 下不得生成 terminal-hold tail。
- **认证可过**：承认 Mid-MPC 为非凸 NLP，以 policing-function（独立确定性 Checker）架构论证安全边界（§13）。
- **稳定性可验证**：keep-last-route 受超时与风险门控约束（§9.12），不再无限期执行过期世界模型航线。

本 spec 替换当前 "path1 audit / path2 execution" 分裂为单一权威 M5 航线契约。

## 2. Current Source-Backed State（含调研发现）

当前 M5 有两个独立输出（`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`）：
- `/l3/m5/avoidance_plan`：`l3_msgs::msg::AvoidancePlan`，由 NLP / 几何 fallback / recovery / transit-empty 分支构建。
- `/l3/m5/avoidance_waypoints`：`l3_external_msgs::msg::AvoidanceWaypoints`，由独立 W4 corridor / return 分支 `publish_avoidance_waypoints_` 构建。

当前 solve loop 硬编码 1 Hz，每周期调两个输出路径。GNC bridge 当前**仅订阅** `/l3/m5/avoidance_waypoints` 并映射到 `ship_interfaces::msg::AvoidancePlan`。

### 2.1 Mid-MPC 当前输出非完整避让+回归航线
- NLP 默认 `n_horizon=12, dt_s=5.0`，除非 caller 显式 override。
- `m5_params.yaml` 声明 `horizon_s:90.0, n_steps:18, dt_s:5.0`，但当前 `MidMpcNode` 构造**未读入** `MidMpcNode::Config`。
- `MidMpcWaypointGenerator` 默认最多采样 4 个航点。
- 几何 fallback 发独立 10 航点 `DEGRADED` 弧。
- Recovery 发独立 6 航点 `RECOVERY` 回归计划。
- Path2 corridor 在冲突活动期发 encounter-anchored corridor，冲突清除后发独立 return route。

### 2.2 GNC 已能接受完整航线数组
- `ActiveRouteManager` 要求 ≥2 航点 + 匹配的 lat/lon/speed/mode 数组。
- 它把完整避让航线复制进 `RoutePlan`。
- `CoordinateTransform` 拒绝：无效几何、重复/反向航线、更新过频、横向 delta 过大、首次变更几何离船太近。
- 它忽略相同重复几何与 metadata。
- ⇒ 主要 gap 不在 GNC 容量，而在 M5 航线 ownership 与更新纪律。

### 2.3 调研新发现（v2 新增，须在实现时对齐）

| 发现 | 证据 | 对设计的影响 |
| --- | --- | --- |
| **M6 当前不输出 past-clear/release 信号** | `COLREGsConstraint.msg` 字段仅 `active_rules/phase/primary_role/preferred_direction/conflict_detected/confidence/rationale`，无 `past_clear/release/encounter_state`。M6 的 `past_and_clear_from_heading`@reasoner_node:185、`RuleLatch`、`EncounterStateMachine`（7 态 FSM 含 60 s dwell）全在内部 | §14 前置依赖：须扩 `COLREGsConstraint.msg` 让 M6 发布 `past_clear`/`encounter_state` |
| M2 CPA/TCPA 已带 UKF covariance | `CpaTcpaCalculator::compute`@cpa_tcpa_calculator.cpp:52，输出 `cpa_covariance_m2/tcpa_covariance_s2` 经 `TrackedTarget.msg:19-20` 发布 | TailBuilder covariance 3σ 膨胀判据**数据现成**，复用 |
| M5 自带 `CpaCalculator`（无 covariance）与 M2 重复 | `m5/shared/cpa_calculator.cpp:27/78`；NLP 输入 `mid_mpc_node.cpp:230-231` 用 M2 WorldState 值，trajectory-level 校验用 M5 自算 | §9.6 收敛：s_clear 数值统一来自 M2 WorldState；M5 自算仅留 trajectory-level 校验 |
| ship-domain / BCT / BCD 在 kernel **无实现** | grep `bow_crossing/BCT/BCD/ship_domain` 仅注释/scaler 命中；ship domain 仅以 `cpa_safe_m` 常量隐性表达 | §9.7 真缺口：ship-domain/BCT 显式距离进 M2 geometry 新建 |
| M6/M2 共享 floor 配置 | `odd_aware_thresholds.yaml` 的 `cpa_release_m/cpa_safe_m/cpa_hard_m`；M5 不应自定 | TailBuilder 须读同一 yaml，不自定 floor |
| NLP acceptance 仅检 heading target | `trajectory_reaches_colregs_target`@m5 types.hpp:533 只调 `trajectory_reaches_heading`；不证 rejoin/opening/no-crossing-ahead | §9.5/§9.7 须扩展 acceptance（tail-gate）|
| `_check_geometry_release` **不存在** | `grep` gnc_bridge 与 src/third_party 命中 0 | 项目 memory 过期，应更正；past-clear 在 gnc_bridge 无残留 |
| M5 已订阅 M6 constraint | `mid_mpc_node.cpp:139` 订阅 `/l3/m6/colregs_constraint`，:264-313 读 conflict_detected/preferred_direction/rule_id/role | §14 在此基础上扩字段消费 |

## 3. Design Decision

M5 实现 `CommittedAvoidanceRoute` 模型。M5 拥有活动会遇的完整航线列表，含：

1. 已 commit 的 prefix——GNC 获准执行后**不可变**。
2. 当前执行段。
3. 未来避让段。
4. 回归到 nominal 航线的 return-to-route 段。
5. GNC/M7/M8/audit 所需 metadata。

M5 向 GNC 发布**完整活动航线版本**，不单独发布"未来 60 s"作为独立航线（独立后缀会让 GNC 把后缀起点当新航线起点，丢失 committed-prefix 上下文，增加 route-jump rejection 风险）。

Mid-MPC 是 normal 主航线生成器。它不必在一次 NLP 内优化到目的地的每个 nominal 航点，但必须**拥有活动战术时域内的避让机动**并提供一个可被确定性 tail 安全延伸的 terminal state。committed route manager 可采样、验证、commit、冻结、splice 航线 revision——它**不是 planner**，不得让 fallback 或 corridor 几何成为 COLREG 避让的 normal 源。

normal 航线几何按 source 标签分类（仅五个，见 §0）：
1. `MID_MPC_OPTIMIZED`：优化机动段，正常 `0–H_pred`，`H_pred=90 s` baseline。
2. `MID_MPC_TERMINAL_HOLD`：从 Mid-MPC terminal state 派生的确定性终端延伸（仅 give-way 角色；stand-on 无此段，见 §4）。
3. `REJOIN_TO_L2`：从 terminal hold 偏置到 L2 nominal 航线的曲率受限回归。
4. `L2_NOMINAL_SUFFIX`：rejoin station 之后的下游 L2 航线。
5. `DEGRADED_CORRIDOR`：仅当 Mid-MPC 不可用/infeasible/被拒且无可安全继续的 committed route 时。

### 3.1 语义权威分工契约（v2 新增，来自决策点 1）

为避免历史 M6/M4 role-collapse（重复判断导致执行矛盾）重演，COLREGs 语义判断**单一真相源**：

| 判断类 | 权威 | M5 行为 |
| --- | --- | --- |
| COLREGs side（starboard/port） | **M6** `primary_preferred_direction` | 只读，不自算 |
| COLREGs role（give-way/stand-on/overtaking/overtaken） | **M6** `primary_role` | 只读，不自算 |
| COLREGs phase / past-clear / release | **M6** `encounter_state`/`past_clear`（§14 扩字段后） | 只读，不自算 abaft-beam/along-axis/range-closing |
| CPA/TCPA/DCPA 数值 + covariance | **M2** `WorldState.targets[]` | 复用，不自算（trajectory-level 校验除外）|
| 阈值 floor（cpa_safe/cpa_release） | M6/M2 共享 `odd_aware_thresholds.yaml` | 读同一份，不自定 |
| route-frame 站位外推 / dwell / GNC feasibility | **M5** | 自身职责，非语义判断 |

**M5 不兜底 M6 语义错误**（决策点 1）。代价：M6 side/role/past-clear 正确性 = TailBuilder 合规上限。缓解：M6 须保证语义正确性（其 D-task），M7 独立 checker 在外侧 veto（§13）。M5 仍保留**非语义**的几何可行性检查（CPA floor 数值、turn radius、rejoin 段 crossing-ahead 重检、GNC preflight）——这是执行可行性，不违反语义独占。

### 3.2 与架构报告对齐（保留 v1，§18 列出 v2 需同步修订的措辞）
- 架构报告选 `Mid-MPC N=18/90s` 为默认 M5 路径——保留。
- 报告定义 `AvoidancePlan` 时域为典型 `60–120 s` 战术计划。本 spec 视 `H_pred=90 s` 为 Mid-MPC 优化时域，**非**完整 COLREG 生命周期。
- 报告将 `AvoidancePlan` 作 waypoint/speed 调整 guidance 发给 L4/GNC，L4 持 LOS/WOP 执行——保留，M5 不出 rudder/thrust。
- route manager + TailBuilder 非新主 planner，仅转换 Mid-MPC terminal 输出为 GNC/守卫所需的稳定全量快照。
- BC-MPC/reactive 路径保持 emergency-only，非 normal committed-route 契约一部分。

## 4. COLREGs Role × TailBuilder Behavior Matrix（v2 新增 — Tier-0 stand-on）

TailBuilder 行为由 M6 `primary_role` 驱动。**stand-on 角色下不生成 terminal-hold tail**（Rule 17 keep course and speed 义务）。

| M6 `primary_role` | 适用 Rule | TailBuilder 完整航线构成 | terminal-hold？ | rejoin？ | 关键约束 |
| --- | --- | --- | --- | --- | --- |
| `GIVE_WAY`（head-on） | Rule 14 | Mid-MPC opt → terminal hold(stbd) → rejoin → L2 suffix | 是（stbd） | 是 | 双方右转；terminal side = starboard（来自 M6）|
| `GIVE_WAY`（crossing） | Rule 15 | 同上，hold 侧 + pass astern | 是 | 是 | **禁 crossing-ahead**；s_clear 含 abaft 判据（消费 M6 Rule15 track release）|
| `GIVE_WAY`（overtaking） | Rule 13 | 同上，side 由 M6/M2 几何选 | 是 | 是 | side **不默认 starboard**；由 M6 `preferred_direction` 决定 |
| `STAND_ON`（crossing/head-on 中的被让路船） | Rule 17(a) | **= L2 nominal route**（无 hold/rejoin） | **否** | **否** | keep course & speed；Mid-MPC 受 M6 stand-on 约束不产偏置 maneuver |
| `STAND_ON`（被追越 overtaken） | Rule 13 | = L2 nominal route | **否** | **否** | 同上 |
| 无冲突 | — | = L2 nominal route（无 avoidance route） | 否 | 否 | lifecycle `Idle` |

### 4.1 stand-on → give-way 切换（role-flip）
stand-on 下 TailBuilder 输出 L2 nominal，直到 M6 报告 `primary_role` 由 `STAND_ON` 翻转为 `GIVE_WAY`。翻转触发条件（由 M6 判定，M5 消费）：
- Rule 17(a)(ii)：让路船未采取适当行动，迫近；
- Rule 17(b)：碰撞无法仅靠让路船避免。

翻转后 TailBuilder 进入 give-way 分支。**M5 不自判 17(a)(ii)/17(b) 触发**——这是 M6 语义职责。

### 4.2 NLP 在 stand-on 下的约束
当 M6 `primary_role=STAND_ON`，Mid-MPC NLP 须受 "keep heading" 约束（heading 接近 `psi_0`，相当于 Rule 17 box 已在 `constraint_compiler.cpp:216-234` 实现）。若 NLP 仍产出偏置 maneuver，acceptance（§9.4/§9.5）须 reject 该候选，**而非**生成 hold tail。

### 4.3 Acceptance 补强（对应 §20）
- stand-on 探针的 committed route **不含** `MID_MPC_TERMINAL_HOLD` / `REJOIN_TO_L2` 段。
- role-flip 后首个 give-way 候选须通过完整 tail-gate（§9.5）。

## 5. Required Route Contract

每个 committed route 须含：
- `plan_id`：一次 encounter 内稳定。
- `revision`：仅当航线几何或执行关键 metadata 变更时自增。
- `parent_route_id`：nominal 航线标识。
- `behavior_mode`：`collision_avoidance` / `return_to_route` / COLREG-protected 等价值（GNC 接受）。
- `command_source`：`m5_committed_route`。
- `valid_until`：heartbeat/hold 截止；**≥ TMR=60 s**（§6）。
- `latitude[], longitude[]`：完整活动航线几何。
- `command_speed_mps[]`：与几何等长。
- `navigation_mode[]`：与几何等长。
- `has_return_to_route_point, return_latitude, return_longitude`：已知 return point 时设。
- per-waypoint audit 字段（保留于 `l3_msgs::msg::AvoidancePlan`）：target speed、turn radius、safety corridor、confidence（§15 量化）、rationale。
- **v2 新增**：`segment_source[]`（每航点的五个标签之一）、`route_hash`、`stale_committed_at`（keep-last 进入时间戳，供 §9.12 门控）。

### 5.1 Message 级策略
- 扩展 `l3_msgs::msg::AvoidancePlan` 加 GNC 所需 metadata，不再保留 `l3_external_msgs::msg::AvoidanceWaypoints` 作执行真相。
- 保留现有 rich waypoint 字段供 M7/M8/认证 audit。
- `gnc_bridge` 将 `l3_msgs::msg::AvoidancePlan` 转 `ship_interfaces::msg::AvoidancePlan`。
- **schema_version bump**（v2 新增，Tier-2）：扩展字段为 breaking change，`schema_version` 显式升版（如 v113→v114，呼应 COLREGs_ConstraintMsg v114 role-carry），并在 Slice A 含旧 schema 消费者回归测试。deprecate `/l3/m5/avoidance_waypoints` 仅在 bridge + 测试证明 `/l3/m5/avoidance_plan` 可驱动 GNC 之后。

## 6. Publication Policy

M5 **计算与发布分离**。

M5 内部可按 1 Hz 或更快（ODD 要求时）持续监控。M5 **不应**以 1 Hz 发布未变更几何。

发布触发（仅）：
1. 新 encounter 航线首次 commit。
2. revision 经 M5 preflight 接受且改变了未来几何。
3. 航线从 avoidance 转入 return-to-route。
4. 当前航线 heartbeat 到期（默认 60 s 开阔 / 10 s 危险 by ODD profile）。
5. Emergency ODD 要求更高速率 direct maneuver 模式。

不发布条件：
- 候选重算产出相同几何与 metadata。
- 候选重算仅改变已 committed prefix 点。
- 候选重算未过 M5 preflight。
- NLP 失败或漏 COLREG target，但已存在可执行的 committed route。

Heartbeat 发布可刷新 `valid_until`，但**不得**新建 revision（除非执行关键内容变更）。

### 6.1 TMR 对齐（v2 新增 — Tier-2）
- `valid_until` **始终 ≥ TMR = 60 s**。即使 `H_publish=10 s`（危险水域），每次 heartbeat 刷新时 `valid_until` 展延至 now+60 s（不缩 TMR 余量）。
- `valid_until` 到期 + 无新 revision + 无 ROC 接管 → 进入 `DegradedHold`（§10），随后 emit `safety_concern_event` 至 M7（§13.4）。
- `H_publish`（60 s 开阔 / 10 s 危险）仅用于 event-driven + 刷新 publication，**不**缩短系统独立安全处置余量（`valid_until` 始终 ≥ TMR 60 s）。

### 6.2 内部 append 语义
- M5 可在 `CommittedAvoidanceRoute` 内部 append/replace 未来段。
- GNC 非 append API。每次对 GNC 发布是**全量快照**（committed prefix + 当前段 + 未来 Mid-MPC 段 + rejoin/tail + nominal suffix）。
- 开阔水域发布节奏 = event-driven + 60 s heartbeat（危险水域 10 s）。Mid-MPC 预测时域与发布节奏**分离**。
- `H_pred=90 s` 下，M5 须比发布节奏更频繁地计算候选，使新未来段在本船接近旧 terminal tail 前就绪。

### 6.3 频率契约变更声明（v2 新增 — Tier-2）
本 spec 将 AvoidancePlan 发布从架构报告 §15.1/§15.2 的 "1–2 Hz" 改为 event-driven + 60 s/10 s heartbeat（开阔/危险 ODD）。这是**频率契约修订**，须走接口变更流程（AGENTS.md：design change complete only when interfaces list in/out messages, frequencies）。M7 SOTIF 监控独立周期（`safety_supervisor_node.cpp:236` timer = 250 ms = **4 Hz**；否决率滑窗 `sliding_window_15s.hpp` kCapacity=100 → 100 cycle @ 4 Hz = **25 s 窗**，文件名 "15s" 误导），不依赖 M5 发布周期，确认安全。GNC 侧须新增 M5 heartbeat watchdog（避免 60 s 静默期误判 M5 dead）——纳入 Slice A。

## 7. Route Revision Rules

M5 通过 prefix/suffix 规则保持航线稳定：
- 在本船后方或 GNC 最小未来更新距离内的航点 = committed，**不可变**。
- 未来后缀仅当首个变更点足够远（GNC 可接受）时才可替换。
- active 与候选航线的最大横向 delta 须在 GNC 动态航线更新限制内（COLREG 后 rejoin 保护例外）。
- 航线更新间隔须满足 GNC 更新守卫（除非 GNC 显式允许 active protected mode）。
- preflight 失败的 revision 不发布。
- 若无 replacement suffix 被接受，M5 继续当前 committed route 并在 ASDR/M8 报告候选失败原因。

这把 fallback 从"执行路径切换"转为"候选源健康状态"。

## 8. Candidate Sources

M5 route manager 可消费多候选源，但**只发布一个 committed route**。normal COLREG 操作下 Mid-MPC NLP 是主源。

候选源顺序：
1. 现有 committed route（若仍 valid 且安全）。
2. Mid-MPC NLP 主候选（过 COLREG target、tail-extension gate、terminal tail、GNC feasibility 检查后）。
3. 确定性 degraded 候选（仅当 NLP 不可用/infeasible/被拒且无可安全继续的 committed route）。
4. Degraded safe-hold 或 MRM request（无 feasible 候选时）。

### 8.1 Mid-MPC 主航线生成
1. M6/M4 提供 encounter role、COLREG direction、heading window、minimum alteration、risk floor、release/rejoin eligibility。
2. Mid-MPC 解战术机动时域，决策变量保持 heading/speed 序列（除非后续提升 position/cross-track 为显式决策变量）。
3. NLP 目标与硬约束须覆盖 CPA/risk clearance、COLREG direction、turn/yaw 限制、speed bounds、route-return pressure。
4. NLP 须含 tail-extension gate 的 terminal 检查（§9.5）。
5. waypoint generator 对 NLP 轨迹密集采样以满足 GNC turn-radius/segment-length 限制。
6. tail builder 从 Mid-MPC terminal state 延伸到预测 past-and-clear，再曲率受限回归 L2。
7. committed route builder 从 accepted rejoin station 起拼 nominal 计划航线（route 序列化，非 fallback 几何）。
8. 完整 committed route 经 preflight 后作为一次 active route revision 发布。

完整 committed route = `Mid-MPC 优化机动 → Mid-MPC terminal hold → 曲率受限 rejoin → nominal 后缀 → committed route revision`（give-way 角色；stand-on 见 §4）。

## 9. Mid-MPC NLP To Complete Route Chain

Mid-MPC NLP 不解整个航次。它输出须优化的战术部分：下一段 `H_pred` 机动 + 一个可安全延伸的 terminal state。M5 再从该 terminal state 构建确定性 tail，序列化机动/tail/rejoin/下游 nominal 为一条完整 committed route。

目标链：
1. Snapshot inputs → 2. Build route frame → 3. Build NLP problem → 4. Solve → 5. Validate tail-extension gate → 6. Convert to waypoints → 7. Build terminal extension & rejoin tail → 8. Append L2 nominal suffix → 9. Build complete candidate → 10. GNC preflight → 11. Commit & publish → 12. Rolling append policy。

### 9.1 Snapshot Inputs
M5 取一个时间戳 planning snapshot：
- M2 `WorldState`：本船 pose/speed/heading + tracked targets（含 `cpa_m/tcpa_s/cpa_covariance_m2/tcpa_covariance_s2`）。
- M4 `BehaviorPlan`：behavior、heading window、speed window。
- M6 `COLREGsConstraint`：active rules、phase、primary role、preferred direction、COLREG constraints、conflict flag、primary target id、**`past_clear`/`encounter_state`**（§14 扩字段后）。
- L2 `PlannedRoute`：完整 nominal `GeoPath`。
- L2 `SpeedProfile`：每段计划速度。
- GNC `GncExecutionOdd`：maneuverability 与执行限制。

snapshot 是该次 NLP solve 与候选评估的**唯一输入**。后续到达的消息启动后续候选，不得 mutate in-flight 候选。

### 9.2 Build Route Frame
M5 须从**完整 L2 航线**构建 route frame（非仅第一段 leg）。

输出：当前航线段 index；投影本船 station `s0_m`；cross-track `xte_m`；当前 tangent/bearing；`s0_m` 起的未来 nominal 采样；station→WGS84+speed 映射。

当前代码仅从前两个 `PlannedRoute` pose 推 bearing/XTE——不足以支撑完整 committed route（splice 点须在下游真实航线 polyline 上）。

### 9.3 Build NLP Problem
首版可保持当前决策变量 `psi[k]`（heading）+ `u[k]`（speed），本船位置在公式内由 `psi/u` 积分。后续可将 along-track/cross-track 提升为显式决策变量（若 tail-extension/route-return 约束太间接）。

NLP 须含：
- 每目标 CPA/risk clearance（全时域）。
- COLREG direction + minimum alteration。
- Rule 13/14/15/16/17 role-specific 约束（来自 M6 role；stand-on 下为 keep-heading box）。
- M4/M1 的 heading/speed bounds。
- 船能力 + GNC ODD 的 ROT/yaw 约束。
- route-return pressure（route-frame cross-track + bearing）。
- terminal tail-extension pressure/hard rows。

**此处 NLP 做功。确定性 corridor 逻辑不得成为 normal 航线形状源。**

> **非凸性说明**（§13 详述）：上述 NLP 客观为非凸（`J_colreg` exp-barrier + `sqrt(d²)`、`J_asym` softplus + give-way 离散切换），CPA 硬约束 `dx²+dy² ≥ cpa²`（`constraint_compiler.cpp:313`）使可行域非凸（"圆外部"）。这是架构 §10.4 行 919 规定的硬约束，非凸不可去。安全边界由独立确定性 Checker（M7 SOTIF + X-axis）policing 保证（§13）。

### 9.4 Solve Tactical Maneuver
solver 返回 `MidMpcSolution::trajectory`（时间索引机动时域）。**仍是候选**——IPOPT 收敛 ≠ committed。

trajectory 须作为 route-frame 内机动评估：
- signed lateral movement 匹配 COLREG preferred direction（when required）——**消费 M6 `preferred_direction`，M5 不自算 side 几何**；
- heading alteration 达到 COLREG target；
- CPA/risk floor 全时域成立（数值复用 M2 WorldState）；
- 本船不 crossing-ahead（give-way 须 pass astern 语义时）；
- speed profile 兼容 commanded route 与 GNC 减速限制。

### 9.5 Validate Tail-Extension Gate（v2 强化）
完整航线可用 Mid-MPC 作主源**仅当** NLP trajectory 达到一个可安全延伸的 terminal state。前 90 s 段不必到达最终 rejoin 点，但须证明其 terminal state 能喂给确定性 tail 而不违反 COLREG/安全/GNC 约束。

tail-extension gate（accepted point 近 NLP 机动末端）须满足：
- signed lateral offset 保持在 M6 要求侧（**消费 M6 `preferred_direction`**），除非 M6/M4 authorize release；
- heading/speed 对 constant-offset hold 或曲率受限 rejoin feasible；
- station 在本船前方 ≥ GNC 最小未来更新距离；
- primary target risk opening 或至少不恶化（terminal hold 下）——**用 M2 covariance 上界判**：`cpa_m - 3σ ≥ cpa_release_m`；
- 预测 CPA/ship-domain floor 在 tail lookahead 内保持高于安全 floor；
- 无 crossing-ahead violation（继续当前侧不引入）；
- 进入 terminal hold 的 turn radius/减速 feasible。

**stand-on 角色（v2 新增）**：`primary_role=STAND_ON` 时**无 tail-extension gate**——不生成 terminal tail，完整航线 = L2 nominal（§4）。NLP 在 stand-on 下受 keep-heading 约束，若产出偏置则 reject 候选（reason `stand_on_heading_violation`）。

首版可用固定 terminal gate 在 `k=N-1`。后续可扫描 horizon tail 附近候选 index 选最早可行 extension gate。

NLP 收敛但漏 tail-extension gate 时，M5 **不得**发布 normal route。可保留前一 committed route 或标记候选 rejected（reason `nlp_tail_gate_failed`）。

### 9.6 Convert NLP Trajectory To Maneuver Waypoints
M5 将 accepted NLP trajectory 转为密集机动段：
- 在 NLP step 分辨率或更密插值处积分 trajectory 位置；
- 局部 NED/route-frame 点转 WGS84；
- 算 per-segment distance、per-waypoint target speed、相邻航点的 feasible turn radius（非稀疏 4 点采样）；
- 设 navigation mode + behavior mode（COLREG-protected 执行）；
- 保留 audit metadata：source `MID_MPC_OPTIMIZED`、solver status、cost summary、active constraints、target id、tail-extension gate index。

替换当前 4 航点 `MidMpcWaypointGenerator` 行为。4 点可留可视化，GNC 执行需足够点过 turn-radius/segment-length 检查。

### 9.7 Build Terminal Extension And Rejoin Tail（v2 重写 — 复用 M6/M2，禁止重判语义）
NLP 优化段之后，M5 构建确定性 tail。tail 非独立 planner，非旧 path2 corridor。它是 Mid-MPC terminal state 的 route-frame 延伸。

**算法**：route-frame constant-offset hold + 曲率受限 rejoin（仅 give-way 角色）。

**输入**：terminal state `pN/psiN/uN`；L2 route frame + speed profile；M6 role/rule/side/release-past-clear；M2 target predictions + covariance；GNC ODD limits。

**复用规则（v2 核心，来自决策点 5，禁止重判语义）**：

| tail 步骤 | 数据源 | 是否新算 |
| --- | --- | --- |
| protected side | M6 `primary_preferred_direction` | 只读 |
| past-clear 触发（rejoin 起点 s_clear）| M6 `encounter_state==RELEASE` 或 `past_clear==true` | 只读 M6 信号，**不自算 abaft-beam/along-axis** |
| CPA/TCPA 数值 | M2 `WorldState.targets[].cpa_m/tcpa_s` | 复用，不自算 |
| covariance 膨胀 | M2 `cpa_covariance_m2`（3σ）| 复用 |
| floor 阈值 | `odd_aware_thresholds.yaml` `cpa_release_m/cpa_safe_m` | 读同一份，不自定 |
| route-frame 站位投影（M6 时序 past-clear → L2 station `s_clear`）| TailBuilder 自身 | **新算**（几何外推，非语义）|
| 空间 dwell margin（s_clear 后 N 船长 / T_dwell）| TailBuilder 自身 | **新算** |
| 滞回 k_hyst（rejoin 触发严于 re-engage）| TailBuilder 自身 | **新算** |
| ship-domain / BCT 显式距离 | M2 geometry（缺口新建）| 新建进 M2，TailBuilder 消费 |

**步骤**：
1. 投影 `pN` 进 L2 route frame 为 station `sN` + 横向 offset `lN`。
2. protected side 从 M6/M4。give-way Rule 14/15 正常 starboard（来自 M6），Rule 13 由 M6 决定。tail **不得**在无新 Mid-MPC-normal 候选 + M6/M4 authorization 下切换 side。
3. 算 `l_hold`：保持 `lN` 符号；clamp 到 min safe offset 与 max GNC-feasible offset；若 `lN` 太小不足以满足 COLREG apparent action，reject normal 候选（不静默用 path2 corridor）。
4. 生成 constant-offset hold 段（平行 L2 航线）从 `sN` 到预测 past-and-clear station `s_clear`。**`s_clear` = M6 报 `past_clear`/`encounter_state==RELEASE` 的时刻，映射到 L2 station**（本船以 hold 速度走到 s_clear 时仍满足 M6 释放 + covariance 上界 `cpa_m - 3σ ≥ cpa_release_m`）。
5. `s_clear` 之后加**空间 dwell margin**（N 船长，建议 N=3–5，或 T_dwell 等效）。
6. 生成曲率受限 rejoin：`l(s) = l_hold * (1 - smoothstep((s - s_clear - dwell) / L_rejoin))`。
7. `L_rejoin` 由 speed、min turn radius、yaw-rate、lateral-accel、GNC segment-length 选；若无法不违反限制地 rejoin，向下游延展 `L_rejoin`。
8. tail 航点按 GNS-friendly 间距采样（50–150 m by speed/turn/ODD）。
9. **每个** tail 航点校验 CPA/ship-domain（M2 数值）、COLREG side（M6）、no reverse segment、speed limit、turn radius、route-frame 连续性、**rejoin 段重检 no-crossing-ahead**（不止 terminal gate）。

tail source = `MID_MPC_TERMINAL_HOLD` 直到 `s_clear`+dwell，然后 `REJOIN_TO_L2` 直到 rejoin station。UI/ASDR 须显示这些标签，**不得**呈现为 `MID_MPC_OPTIMIZED`。

**Normal execution assumption**：tail 是安全延续（若后续 revision miss）；normal 下 M5 在本船接近旧 terminal tail 前解新 Mid-MPC 候选替换未来后缀；rolling 失败时 tail 仍是 GNC 的保守航线而非 abrupt stop。**但此假设受 §9.12 超时/风险门控约束**——tail 不具备重新避碰能力，target 机动/多目标时须转入 DegradedHold（§9.12）。

### 9.8 Append L2 Nominal Suffix
rejoin tail 到达 nominal 航线后，M5 拼接下游 L2 nominal 航线。

Splice 规则：找 accepted rejoin station 对应的 nominal station；按需插一个 explicit rejoin 航点；append 该 station 严格下游的 nominal 航点；应用 L2 speed profile（GNC ODD + M4 speed envelope clamp）；删除 splice 附近重复/过短点；验证 splice 后首段 nominal 从 rejoin tail terminal state feasible。

此 splice 非 fallback，是战术 NLP 机动成为完整航线的方式。

### 9.9 Build Complete Route Candidate
完整候选含：committed route metadata；`MID_MPC_OPTIMIZED` 机动段；`MID_MPC_TERMINAL_HOLD` 延伸段；`REJOIN_TO_L2` 段；`L2_NOMINAL_SUFFIX` 段；per-waypoint speed/navigation mode；return-to-route hint=rejoin 点；per-segment route-source metadata；route hash（几何 + 执行关键 metadata）。候选在 GNC preflight + commit 规则通过前不发布。

### 9.10 GNC Preflight
M5 用 GNC 将执行的相同约束 preflight 完整候选：route array lengths；finite WGS84；min segment length；reverse segment 检测；turn radius/yaw-rate/lateral-accel；speed/deceleration distance；first changed point distance ahead；max lateral delta from active route；min update interval；protected COLREG mode exceptions。

失败 reject 候选。**GNC 拒绝应是最后防线，非 M5 发现 infeasible 几何的 normal 方式。**

**角色定位（v2 新增 — Tier-2，对齐架构 §11.10）**：M5 preflight = P4 Doer-side 预合规自检，**不替代 GNC feasibility authority**。裁决优先级：GNC reject > M5 preflight reject > heartbeat。M5 preflight 失败仅"不发布"，不触发 MRC/MRM。约束集参数化引用 `GncExecutionOdd`，杜绝 M5/GNC 用不同数值漂移。

### 9.11 Commit And Publish
preflight 通过后 M5 commit：
- 无 active route：建 `plan_id`、`revision=1`、发布全量；
- active route 未变：仅 heartbeat，无新 revision；
- active route 变：保 committed prefix，替换未来 suffix，`revision+1`，发布全量；
- active route unsafe 且无 NLP route accepted：进 `DegradedHold` 或 emit degraded/MRM handoff（**对齐架构 §11.6**：emit `safety_concern_event` 至 M7，M5 不直接 publish MRM command）。

发布消息 = 完整活动航线版本。GNC bridge 映射为 `ship_interfaces::msg::AvoidancePlan`，GNC 转 active route。

### 9.12 Rolling Mid-MPC Append Policy + Keep-Last-Route Risk Gating（v2 强化 — Tier-0 #3）
Rolling Mid-MPC ≠ 拼接独立 60 s chunk 片段（会生 heading/curvature/route-frame 不连续）。Rolling = 每周期解 fresh finite-horizon 机动，route manager 内部仅 append/replace 本船前方足够远的未来几何。GNC 仍收全量快照 revision。

三时域（见 §0）：`H_pred=90 s` / `H_commit`=GNC guard / `H_publish`=60 s 开阔 / 10 s 危险 heartbeat。

每 rolling cycle：
1. 投影本船到 active committed route，标 immutable prefix。
2. 从当前 own-ship state 解 Mid-MPC NLP over `H_pred`。
3. 验证 COLREG target + tail-extension gate（stand-on 下 keep-heading）。
4. 转 accepted NLP 机动为密集航点。
5. 从 Mid-MPC terminal state 构建 terminal hold + 曲率受限 rejoin tail。
6. rejoin station 后 append L2 nominal suffix。
7. 比较完整候选与 active committed route。
8. 若首个变更点在 immutable prefix 或 GNC 更新守卫内 → reject revision，保 active route。
9. 若未来 suffix 变 + preflight 过 → commit 新 revision，发布全量。
10. 几何未变 → 除 heartbeat 不发布。

若 NLP 无法在 `H_pred` 内达 valid tail-extension gate，M5 **不得**发布该候选的 `NORMAL` 完整航线。选项：调整 `H_pred`/waypoint density/terminal costs；保留前一 valid committed route；进 `DegradedHold`/MRM。

**Keep-Last-Route 风险门控（v2 新增，来自决策点 2，对齐 nlm 🟢）**：
tail 是单次 NLP terminal state 的确定性外推，**不具备重新避碰能力**。NLP 不收敛时"keep last committed route"仅在 target 态势冻结时安全。门控：

- `stale_route_max_age_s = 45 s`（= H_pred/2）。keep 计时器在首次 NLP 失败 + 决定 keep 时启动（`stale_committed_at`）。
- keep 期间 M2 用**当前** target state 持续重算 CPA/risk floor（非 snapshot 时刻的旧值）。
- 进入 `DegradedHold` 的触发条件（任一）：
  - `now - stale_committed_at > 45 s`；
  - NLP 连续失败 N=3 次（对齐架构 §11.3 "连续 3 次失败"）；
  - target maneuver 检测：target heading 变化 > 15° 或 CPA 漂移 > 20%（相对 snapshot）；
  - 当前 CPA < `cpa_hard_m`（M6/M2 共享 floor）。
- 进入 `DegradedHold` 后：emit `safety_concern_event` 至 M7（§11.6 指挥链），由 M7/M1 决定 MRM；M5 **不直接 publish MRM**。

这保持 Mid-MPC 为 normal 避让几何源，同时防止系统盲目把不稳定短 chunk 缝进航线 / 沿过期世界模型航线盲驶。

## 10. Lifecycle State Machine

M5 航线 lifecycle 八态（见 §0）：
1. `Idle`：无 COLREG 冲突，无 active committed route。
2. `CandidateReady`：M5 为新冲突生成 ≥1 候选。
3. `CommittedAvoidance`：M5 已发布 accepted avoidance route。
4. `ExecutingAvoidance`：GNC 正跟随 committed route。
5. `RevisingFutureSuffix`：M5 有候选更新但 preflight 未过未发布。
6. `CommittedReturn`：冲突清除或 post-avoid 阶段；航线含 return-to-route 段。
7. `Completed`：本船已 rejoin nominal，avoidance route 可释放。
8. `DegradedHold`：无可安全 commit 的 revision；hold 最后 valid route 或请求高层 fallback/MRM（经 M7）。

**状态触发（v2 新增）**：
- `CommittedAvoidance/ExecutingAvoidance → DegradedHold`：§9.12 任一触发条件。
- `CommittedAvoidance → CommittedReturn`：M6 `encounter_state==RELEASE` 且 tail rejoin 段已发布。
- `CommittedReturn → Completed`：本船 cross-track 回到 nominal 阈值内，`revision` 终结。
- `* → Idle`：`Completed` 后释放 `plan_id`。

### 10.1 State Authority（保留 + 措辞对齐）
- M6 拥 COLREG rule/phase 证据。
- M4 拥 behavior authorization。
- M5 拥 route geometry + route commitment（**仅 route feasibility 与候选健康，非 safety authority**）。
- GNC 拥 route feasibility acceptance + execution feedback。
- M7 拥独立 safety veto/checking。

M5 **不得**维护隐藏的独立"safe/unsafe" behavior authority。safety context 仍由 ODD（M1）+ M7 VETO 决定（架构 §2.2/§11.2）。

## 11. GNC Feasibility Gate
（见 §9.10 preflight 清单，不重复。）M5 须消费 `ship_interfaces::msg::GncExecutionOdd` 或等价归一化：`min_turn_radius_m`/`emergency_min_turn_radius_m`/`max_lateral_accel_mps2`/`max_yaw_rate_deg_s`/update-guard distances/lateral-delta limits。GNC limit 不可用时 M5 用保守本地默认并在 ASDR 报告。

## 12. Mid-MPC Horizon Policy

Horizon 可调，但**非**航线稳定性的主要修复手段。

Required：
- wire ROS 参数进 `MidMpcNode::Config`。
- 使 `n_horizon`/`dt_s`/waypoint sampling count 在 ASDR/debug trace 可见。
- 同步 `MidMpcNlpFormulation::Config::dt_s` 与 `MidMpcWaypointGenerator::Config::dt_s`。
- 首个修复 baseline = `n_horizon=18, dt_s=5.0`（现有 config 已声明）。
- 不依赖单次超长 NLP solve 生成完整 encounter 生命周期。

### 12.1 Trace-Derived Horizon Evidence
完整 COLREG avoidance+return 生命周期通常远超当前 NLP horizon（trace 表：`active_s` 987–3965 s，含 donut 3965 s 案例）。决策：`H_pred` ≠ 完整 committed route 长度；当前 NLP 是短时域局部机动优化器；M5 须**分别**持有完整 route 对象（committed prefix + Mid-MPC 段 + terminal hold/rejoin tail + nominal suffix）；≥10 min 航线由 route state 装配，非单次 IPOPT solve。

### 12.2 当前 Mid-MPC NLP 能力边界
- 决策向量 `psi[0..N-1]` + `u[0..N-1]`；无显式 route-frame `s/l`/segment id/terminal route-capture state。
- 位置由 `psi/u/dt` 积分；route-frame tail-extension/rejoin 当前非 terminal NLP 约束。
- runtime path 重建 symbolic graph，2 s CPU cap。
- 约束行随 horizon + target count 增长；dt=5s 下 600 s ≈ N=120，240 决策变量（未含 target/rule/CPA/zone 约束）。
- ⇒ 长航线稳定须来自 `CommittedAvoidanceRoute` + rolling future-suffix revision，非扩 N 跨整个 encounter。

## 13. Non-Convexity Argument & SOTIF / Policing-Function（v2 新增 — 路径 A）

本节承认 Mid-MPC 非凸 NLP 并给出认证论证。这是决策点 3 拍板（路径 A：修文档承认非凸，补 SOTIF argument，**不改算法**）。

### 13.1 事实认定（逐行可溯）
- 架构报告 §4.5 行 461 / §10.1 行 860 / §10.4 行 910：现登记 Mid-MPC 为"凸优化 / 解析解可追溯 / 约束直接入 QP / 线性化 MPC"。
- 代码实际（`mid_mpc_nlp_formulation.cpp:285` `casadi::nlpsol(...,"ipopt",...)`，L-BFGS，2 s cap）：非凸 NLP。
- 非凸源（公式项级）：
  - `J_colreg` exp-barrier + `sqrt(d²)`，d 经三角积分（`mid_mpc_nlp_formulation.cpp:128-177`）；
  - `J_asym` softplus + give-way 离散切换（`:189-201`）；
  - CPA 硬约束 `dx²+dy² ≥ cpa²`（"圆外部"非凸集）（`constraint_compiler.cpp:313`）——**这是架构 §10.4 行 919 直接规定的硬约束**；
  - Zone union（TSS lane）（`constraint_compiler.cpp` `decompose_polygon`）。
- 结论：当前 NLP 客观非凸，**非凸源是架构自己规定的 CPA 硬约束**，去掉即不再是 COLREG-compliant planner。

### 13.2 SOTIF / Policing-Function Argument（nlm 🟢 High）
核心论点：传统 IEC 61508 为确定性软件设计；非凸 NLP 的局部最优/不可复现性使 Doer 无法直接过 SIL2 白盒。但 **policing-function（Doer-Checker）架构允许非确定 Doer**，只要独立 Checker 用降维确定性算法（CPA/TTC/ship-domain 几何）设硬边界 + veto + MRC。**仅 Checker 须过 IEC 61508 SIL2**，Doer 不需要。

本项目对应物：
- Doer = M5 Mid-MPC（非凸 NLP）+ M4/M6。
- Checker = **M7 SOTIF 监控**（架构 §11.3 L3 Checker 否决率、CPA 趋势 PERF 监控）+ **X-axis Deterministic Checker**（§11.7，硬 VETO + `CheckerVetoNotification` enum）。复杂度比 = **设计目标 50-100:1（非规范强制）**，ADR-001 §1 `08c-adr-deltas.md:36-38` 已把早期 "1:100" 降级为此措辞。
- 架构已内置此分层（决策四 §2.5、ADR-001、ADR-002）。本 spec 只把"非凸 NLP 由 M7+X-axis policing"写显式。

先例（nlm 🟢 High）：MAXCMAS（Rolls-Royce 等）用非确定 swarm optimization + policing function 过 IEC 61508 / ISO 26262 论证。DNV 通过 Technology Qualification (TQ) / AiP + AROS notation 风险路径接受这类系统。

### 13.3 认证路径
从"传统 IEC 61508 确定性白盒路径"转为"DNV TQ/AiP + AROS 风险路径 + IEC 61508 仅覆盖 Checker"。CCS 验船师审查重点从"M5 NLP 是否凸/可复现"转移到"M7+X-axis Checker 是否真正独立 + 确定 + 覆盖所有 NLP 失效模式"。

### 13.4 路径 A 前提证据（Slice H 验证结果 2026-06-30）

Slice H（Task #9）代码层验证结论：**理论框架 🟢 / 代码独立性 🟢 / 确定性 🟢 / 运行时覆盖 🔴**。

| 前提 | verdict | 证据 |
| --- | --- | --- |
| policing 先例（MAXCMAS/DNV AROS）| 🟢 | nlm High |
| Checker 独立（决策四 §2.5）| 🟢 | M7 零引用 M1-M6 内部头；CI 强制 `tools/ci/check-doer-checker-independence.sh`。🟡 `l3_risk_model` 共享库是 CI 盲点 |
| Checker 确定 | 🟢 | 全 enum + 几何阈值，零 ML/随机/LLM（`VetoReasonClass` 6 值，不解析自由文本）|
| 复杂度比 | 🟡 | ADR-001 已降级 "1:100"→"50-100:1 设计目标（非强制）"（`08c-adr-deltas.md:36-38`）；实际 6.4:1（M7 含 MRM 全栈，非纯 Checker 逻辑）|
| Checker 覆盖 NLP 失效 | 🔴 | **见下 gap**——policing 当前是空头支票 |
| MRC 时延 < TMR 60s | 🟡 | X-axis veto 链最坏 ≈25 s < 60s ✅；NLP-internal-fail 路径未接线 = ∞ |

**🔴 运行时覆盖 gap（Slice K 须接线，否则 policing 无法演示、认证过不了）**：
1. **HC-1~HC-6 硬约束全死代码**：`check_cpa_consistency`/`check_colregs_geometry`/`check_rot_limit`/`check_speed_limit`/`evaluate_dc_constraint` 判定函数写好但运行时从未调用——`run_hard_constraint_checks`@`safety_supervisor_node.cpp:548-552` 是空 stub，仅单测覆盖。
2. **M5 输入被置 0**：`on_avoidance_plan`@`:298-301` 把 `last_avoidance_dcpa_/heading_change_/speed_/rot_` 全置 0。即使硬约束接线，输入永远 0 → CPA 一致性/COLREGs 几何检查退化。
3. **NLP solver status 完全不消费**：M7 不订阅 IPOPT 收敛标志/KKT 残差/`Restoration_Failed`/`tail_gate_failed`。NLP 不收敛/局部最优无任何 Checker 直接检测。
4. 运行时实际只跑：3 粗检（ODD∈[0,1]/CPA非负/rules⇒targets）+ X-axis veto 事件回调 + watchdog + SOTIF proxy。

**MRC 时延真值**：M7 主循环 4 Hz（`safety_supervisor_node.cpp:236`）；否决率滑窗 100 cycle @ 4Hz = 25 s（`sliding_window_15s.hpp` kCapacity=100，文件名误导）；否决率 ≥20% → SOTIF 升级 → `mrm_selector` 1 violation 即 MRM-01。X-axis veto 链最坏 ≈25.25 s < TMR 60s ✅。NLP-internal-fail 链时延不可量化（路径未接线，须 Slice K）。

> **结论**：路径 A 理论框架 + 代码独立性可过 CCS 文档审查；但运行时 policing NLP 失效模式须 **Slice K 接线**才能演示。CCS 接受度 🟡——policing-function 立场须与 CCS 直接沟通（nlm：MAXCMAS + DNV AROS 是通用框架 + 先例，CCS 具体立场未确认，列为外部沟通项）。

> **v2.2 §13.4 更新**（2026-07-04）：tail-gate 定位为 NLP publish gate（非 SIL2 independent checker）。详见 `docs/superpowers/specs/2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md` §13.4。

## 14. M6 → M5 Semantic Signal Contract（v2 新增 — 前置依赖）

本节定义 M6 须向 M5 输出的 COLREGs 语义信号。**这是 TailBuilder s_clear 复用方案的前提（决策点 1 + 5）**。当前 `COLREGsConstraint.msg` 缺这些字段（§2.3），须扩。

### 14.1 msg 扩字段（M6 侧 D-task，前置）
扩展 `l3_msgs::msg::COLREGsConstraint`（或新 msg，经架构 §4.4 强类型流程）：

| 字段 | 类型 | 含义 | M6 填充源 |
| --- | --- | --- | --- |
| `past_clear` | `bool` | primary target 已 past-and-clear | `EncounterStateMachine.state()==RELEASE/CLEAR` 或 `RuleLatch.released()` |
| `encounter_state` | `uint8` (enum) | CLEAR/ONSET/ACTIVE/RELEASE | `EncounterStateMachine.state()` |
| `release_predicted` | `bool` | 预测将 release（供 tail 提前规划）| `give_way_*_release_safe` 投影 |
| `primary_role` | `uint8` (enum) | GIVE_WAY/STAND_ON/OVERTAKING/OVERTAKEN | 已有，保留 |
| `primary_preferred_direction` | `int8` | -1 port / +1 stbd / 0 none | 已有，保留 |
| `confidence` | `float32[0,1]` | M6 语义判断置信度 | 已有，保留 |
| schema_version | — | bump（如 v114）| breaking change |

M6 已有内部计算（`past_and_clear_from_heading`、`RuleLatch`、`EncounterStateMachine` 60 s dwell），仅须**发布**给下游。

### 14.2 M5 消费规则（禁止重判语义清单）
TailBuilder / Mid-MPC acceptance 须：
- ✅ 读 `primary_preferred_direction` 定 tail side；
- ✅ 读 `primary_role` 选 §4 角色矩阵分支；
- ✅ 读 `encounter_state==RELEASE` / `past_clear==true` 触发 rejoin（s_clear）；
- ✅ 读 M2 `WorldState.targets[].cpa_m/tcpa_s/cpa_covariance_m2` 作几何量数值；
- ✅ 读 `odd_aware_thresholds.yaml` 共享 floor；
- ❌ **禁** 自算 abaft-beam / along-axis past-clear（重蹈 role-collapse）；
- ❌ **禁** 自定 cpa floor（M6 已 release 而 TailBuilder 仍 hold）；
- ❌ **禁** 自判 side/role；
- ❌ **禁** s_clear 用 M5 自带 `CpaCalculator`（无 covariance）重算 CPA/TCPA 数值——s_clear 数值统一来自 M2 WorldState；M5 `CpaCalculator` 仅留 trajectory-level（hold 段采样）最小距离校验，且输入 own/tgt 速度来自 M2 snapshot。

### 14.3 M6 msg 不可用时的降级
若 M6 msg 扩字段未就绪，TailBuilder **不得**自算语义替代。降级：进 `DegradedHold`，emit `safety_concern_event`。这是硬契约——宁可降级，不重判语义。Slice 顺序须保证 M6 msg 扩字段先于 TailBuilder s_clear 实装。

## 15. confidence Quantification（v2 新增 — Tier-2）

per-plan confidence（§5 contract）：
```
confidence_plan = min(conf_solver, conf_tail_gate, conf_target)
  conf_solver   = 1 - clamp(KKT_residual / KKT_ref, 0, 1)   # IPOPT KKT 残差归一化
  conf_tail_gate ∈ {0, 1}                                    # §9.5 gate 通过与否
  conf_target   = min over primary targets of M2 target_confidence
```
per-waypoint confidence：继承 `confidence_plan`，叠加局部 CPA margin 项 `clamp((cpa_m - cpa_hard_m) / (cpa_safe_m - cpa_hard_m), 0, 1)`。

confidence 须在 ASDR/M8 可审计，供 M7 SOTIF 假设验证（架构 §4.4）。

## 16. Normal And Emergency ODD Modes

**Open-water normal**：内部候选监控可 1 Hz；航线发布 event-driven + 60 s heartbeat（危险水域 10 s）；GNC 收完整活动航线版本。

**Narrow-water / emergency**：M5 可更频繁发布 revision（若 GNC 更新守卫允许）；route-level guidance 太慢时，M5/BC-MPC 可经独立 emergency control contract 请求 direct maneuver；direct rudder/thrust 非 normal committed-route 契约一部分。

**v2 新增（Tier-2）— emergency 触发权威**：emergency mode 由 **M1 ODD 状态机**触发（架构 §3.5），M5 **不自决** mode 切换；M5 按 ODD profile 选 heartbeat 频率；direct maneuver 仅经 BC-MPC `ReactiveOverrideCmd`（架构 §15.1），M5-Mid 不发姿态指令。

## 17. Observability

每 M5 cycle 暴露：active `plan_id`/`revision`/route hash；lifecycle state；候选源 selected/rejected reasons；NLP solver status + COLREG target check；本周期是否发布及原因；committed prefix length；revision 首 changed index；GNC preflight 结果；heartbeat 剩余；fallback health（不发布 fallback jumps）；**`stale_route_age`（keep-last 计时，§9.12）**；**M6 `encounter_state`/`past_clear` 消费值 vs M2 几何推导（一致性监控，喂 M7 §11.3）**。

CMM 三要素（v2 补全 — Tier-2）：
- `current_state()` = §10 lifecycle 八态 ✓
- `rationale()` = §14 候选源/NLP 状态/前缀长度/heartbeat 剩余 ✓
- `forecast(delta_t)` = committed route 前 delta_t 秒预测执行位置 + uncertainty（来自 NLP 解算残差 + M2 target prediction 协方差），供 M8 SAT-3（架构 §12.2）✓

ASDR/M8 须区分：no route needed / route unchanged / route heartbeat / route revised / candidate rejected / fallback used as candidate / degraded hold / GNC preflight reject。

## 18. Architecture Report Sync Checklist（v2 新增）

实现本 spec 须同步修订架构报告（路径 A 落地 + 措辞对齐）。**这些是文档变更，走设计变更流程（traceable claims）。**

| 架构章节 | 现措辞 | 改为 | 依据 |
| --- | --- | --- | --- |
| §4.5 行 461 | Mid-MPC "凸优化，解析解可追溯"，CCS ✅ 高 | "非凸 NLP（CasADi/IPOPT，局部最优）；安全边界由独立 M7 SOTIF + X-axis Checker 保证（policing-function，§11.3/§11.7）"；CCS 🟡 中-高 | §13 |
| §10.1 行 860 | "约束直接入 QP" | "约束直接入 NLP（IPOPT）" | §13.1 |
| §10.4 行 910 | "Mid-MPC 采用线性化 MPC" | "Mid-MPC 采用非线性 MPC（NLP）；位置由 ψ/u 积分；目标含 CPA 非凸项；CPA 硬约束使可行域非凸" | §13.1 |
| §10.6 行 960 | IPOPT 限 Phase 4 高速 6-DOF 预留 | 澄清 IPOPT 是当前主求解器 | §13.1 |
| §10 新增小节 | — | "§10.x Mid-MPC 非凸性论证与 SOTIF 兜底" | §13.2 |
| §15.1/§15.2 | AvoidancePlan "1–2 Hz" | "event-driven + 60 s/10 s heartbeat（开阔/危险）" | §6.3 |
| §11.6 指挥链 | （保留）| 显式 "M5 不直接 publish MRM command，emit `safety_concern_event` 至 M7" | §9.11/§10.1 |
| §10.3 / §15.1 | （保留）| 补 "M5 可对 AvoidancePlan 做 GNC-constraint preflight before publish（P4 Doer 自检）" | §9.10 |
| §4.4 / 新 msg | — | M6 `COLREGsConstraint` 扩 `past_clear/encounter_state/release_predicted` | §14 |
| §4.5 / §11.7 | "1:100 复杂度比" | "50-100:1 设计目标（非规范强制）"（ADR-001 §1 `08c-adr-deltas.md:36-38`）| §13.4 |
| §11.3 | "~6.7 Hz / 15s 滑窗" | "4 Hz / 25s（100-cycle @ 4Hz）"（`safety_supervisor_node.cpp:236` + `sliding_window_15s.hpp` kCapacity=100）；`sliding_window_15s` 文件名误导须注释澄清 | §13.4 |

Task #10（同步修订架构报告）。

## 19. Implementation Slices

- **Slice A — Interface unification**：扩 `l3_msgs::msg::AvoidancePlan`（+ `segment_source`/`route_hash`/`stale_committed_at`/schema_version bump）；M5 publisher 使 `/l3/m5/avoidance_plan` 为执行真相；`gnc_bridge` 订阅之；前端 `telemetryStore.ts` 迁移；GNC heartbeat watchdog；`avoidance_waypoints` 暂作 compatibility shadow。
- **Slice B — Mid-MPC primary route generation**：wire Mid-MPC 参数进 Config；加 COLREG target + tail-extension gate（含 stand-on keep-heading reject）到 acceptance；强化 NLP objective/constraints；增 waypoint 采样密度；terminal state 转 route-frame tail。
- **Slice C — Terminal tail builder**（v2 复用方案）：route-frame constant-offset hold + 曲率受限 rejoin；**消费 M6 `past_clear`/`encounter_state` + M2 几何量 + covariance 3σ 膨胀**；segment source 标签；dwell + 滞回 k_hyst；禁 path2 corridor 作 normal tail。**依赖 Slice G（M6 msg）**。
- **Slice D — Committed route manager**：`CommittedAvoidanceRoute` 结构 + lifecycle 状态机 + route hash/revision/heartbeat + prefix/suffix 更新 + keep-last-route `stale_route_max_age_s=45 s` 门控（§9.12）+ DegradedHold 触发。
- **Slice E — Degraded candidate adapters**：W4 corridor → degraded 候选段；return-to-route → 候选段（仅 Mid-MPC/tail 不可用/被拒时）；fallback 从直接发布转候选生成。
- **Slice F — GNC feasibility preflight**：集中 GNC route preflight in M5；用 `GncExecutionOdd`；unit-test GNC guard 边界。
- **Slice G — M6 msg extension（v2 新增前置）**：扩 `COLREGsConstraint.msg`（`past_clear`/`encounter_state`/`release_predicted`/schema bump）；M6 从 `EncounterStateMachine`/`RuleLatch` 填充；M5 消费（§14）。**须先于 Slice C s_clear 实装**。
- **Slice H — M7/X-axis independence verification（v2 新增前置，✅ 2026-06-30 完成）**：codegraph 验证结论见 §13.4——独立性 🟢 / 确定性 🟢 / 复杂度比 🟡（ADR-001 已降级非强制）/ **运行时覆盖 🔴 → Slice K**。
- **Slice I — Architecture report sync（v2 新增）**：§18 清单（Task #10）。
- **Slice J — Runtime verification**：rebuild M5 + GNC bridge；targeted unit tests；GNC-profile COLREG probes（含 stand-on 探针）sim-rate 5 restart-between-runs；local 证据后才考虑 A4000。
- **Slice K — M7 policing 接线（v2 新增，路径 A 认证前置，决策点 7 纳入本 spec）**：(1) 接线 `run_hard_constraint_checks`@`safety_supervisor_node.cpp:548-552`（当前空 stub）调 `build_safety_alert_from_hard_constraints` + 6 个 check_*（HC-1~6）；(2) `on_avoidance_plan`@:298-301 提取真实 `dcpa/heading_change/speed/rot`（当前全置 0）；(3) M5 AvoidancePlan 扩 NLP solver status 字段（收敛标志/KKT 残差/`Restoration_Failed`/`tail_gate_failed`），M7 订阅接入 `assumption_monitor`；(4) CI 脚本 `tools/ci/check-doer-checker-independence.sh` 补 `l3_risk_model` 检测（forbidden 或 allowlist + 理由）；(5) `sliding_window_15s.hpp` 注释澄清真值 25s@4Hz。**完成后 policing-function 方可在运行时演示 NLP-fail→veto→MRM**。

## 20. Acceptance Criteria

**Unit-level**：
- `CommittedAvoidanceRoute` 在 suffix revision 上保 committed prefix。
- 重复几何不建新 revision。
- Heartbeat 刷新 validity 不改几何 revision。
- Mid-MPC 主候选含优化机动 + tail-extension gate + terminal hold + 曲率受限 rejoin + nominal suffix（give-way）。
- **stand-on 探针 committed route 不含 `MID_MPC_TERMINAL_HOLD`/`REJOIN_TO_L2`**（§4）。
- TailBuilder 保 Mid-MPC terminal side（消费 M6），拒 `NORMAL` 候选若需 path2 corridor 才安全。
- **TailBuilder 不自算 past-clear/side（消费 M6 msg）**——单元测试用 M6 msg 缺字段时进 DegradedHold。
- NLP 失败保最后 feasible committed route，**但 §9.12 任一触发条件满足即转 DegradedHold**（stale>45 s / 连续 3 次 / target maneuver / CPA<hard）。
- Fallback 候选不在 guard distance 内发布 discontinuous replacement。
- Degraded corridor/return 候选序列化为 `AvoidancePlan` 仅带 degraded source 标签。
- GNC preflight 拒绝过短段/reverse 段/turn-radius 违规/横向 delta 过大/首变更点过近。
- **confidence 量化**（§15）：`confidence_plan = min(conf_solver, conf_tail_gate, conf_target)` 可计算且在 ASDR 可见。
- **forecast(Δt)+uncertainty** 暴露供 M8 SAT-3。

**Integration-level**：
- GNC profile 消费 `/l3/m5/avoidance_plan`，非 `/l3/m5/avoidance_waypoints`。
- Path2 removal/shadow 不改 M7/M8 audit visibility（前端 `telemetryStore.ts` 迁移验证）。
- 航线发布几何未变时非 1 Hz。
- 重复 heartbeat 被 GNC 忽略/接受无 path reset 不稳。
- **`valid_until` 始终 ≥ 60 s**（TMR 对齐），即使 `H_publish=10 s`（危险水域）。
- 冲突清除发布 return-to-route 作完整活动航线版本。
- normal COLREG avoidance trace 显示 `segment_source=MID_MPC_OPTIMIZED`（active 优化段）+ `MID_MPC_TERMINAL_HOLD`/`REJOIN_TO_L2`（保守 tail）。
- Degraded corridor/fallback source 仅 NLP 不可用/infeasible/被拒时出现。
- ASDR trace 显示 route hash/revision 稳态稳定 + `stale_route_age` + M6 信号消费记录。

**Certification-level（v2 新增，路径 A policing 演示，依赖 Slice K）**：
- M7 hard_constraint（HC-1~6）运行时被调用（非死代码），从 M5 AvoidancePlan 提取真实 `dcpa/heading_change/speed/rot`。
- NLP 不收敛（注入 IPOPT `Restoration_Failed`）→ M7 检测 → veto/SOTIF 升级 → MRM 端到端时延 < TMR 60s（最坏 ≈25s）。
- CPA 穿透（注入实际 CPA < `cpa_hard_m`）→ `check_cpa_consistency` 触发 safety_alert。
- CI `check-doer-checker-independence.sh` 覆盖 `l3_risk_model` 共享检测。

**Scenario-level**：
- `colreg-rule15-cs-edge` 不再 NORMAL/DEGRADED 几何跳变；避让目标、不 donut、回归航线。
- **stand-on 探针（Rule 17 / 被追越）保向保速，无 hold tail**。
- Rule13 overtake side 由 M6/M2 几何选，保 raw-route/XTE 契约改进。
- Clean COLREG cohort 以 GNC profile、sim-rate 5、restart-between-runs、trace-report 运行。

## 21. Non-Goals

本 spec 不：
- tune scenario geometry；relax evaluator/scorer thresholds；force PASS paths；add vessel-specific branches。
- 把确定性 degraded 源作 Mid-MPC NLP 的 normal 替代。
- 使 route manager 成发明 normal avoidance 几何的第二个 planning model。
- 用 path2 corridor 作 Mid-MPC 后的 normal tail。
- 替换 GNC guidance 内部。
- 为 normal 开阔水域避让实现 direct rudder/thrust allocation。
- 推 A4000 前无 local 证据。
- **v2 新增**：M5 自算 COLREGs 语义（side/role/past-clear）；M5 自定 cpa floor；扩 NLP horizon 跨整个 encounter。
- **v2 暂不纳入**（决策点 6）：Monte Carlo 千级 / 故障注入 V&V 归属（留独立 D-task）。

## 22. Known Issues To Fix During Implementation

1. `m5_params.yaml` Mid-MPC 参数声明但未 wire 进 `MidMpcNode::Config`。
2. 部分 code comment 仍称 NLP 为 Phase-3 stub——stale，仅在触碰附近逻辑时更正。
3. NLP acceptance 无 tail-extension gate——须证 COLREG side/target opening/risk trend/route-frame 连续性/no-crossing-ahead 后才能建 terminal tail。
4. Fallback/recovery 发独立 route family——须成 degraded 候选或 lifecycle 转换。
5. Path2 当前在 GNC profile 驱动 GNC——migration 须含 bridge tests before deleting compatibility。
6. 现有 trace 混 W4/W6/U1 spike artifacts——新验证须 tag route source/hash/revision/profile。
7. GNC 更新守卫是真实下游契约——M5 须发布前证 revision，不依赖 GNC 拒绝作 normal 控制流。
8. **v2 新增**：`_check_geometry_release()`（原 `docker/sil_topic_bridge.py` ~L1138）所在文件已被 commit `f138b0d9`（refactor a5c "delete sil_topic_bridge.py, wire 3 C++ adapters"）删除；整仓 `git grep geometry_release` 当前 main（HEAD `12760711`）无命中。项目 memory `l3-m6-onset-latch-no-generalize` 的 "ot bridge flap" remaining-issue 已过期（已加 UPDATE 标注）。⚠️ 等价逻辑是否迁入 3 个 C++ adapter 未验证，实现时须重测确认，不要假设彻底消失。
9. **v2 新增**：M5 自带 `CpaCalculator`（无 covariance）与 M2 `CpaTcpaCalculator`（UKF）重复——本次收敛到数值统一来自 M2 WorldState。
10. **v2 新增**：架构 §4.5/§10.1/§10.4/§10.6 凸性措辞与代码 IPOPT NLP 矛盾——须 §18 同步修订。
11. **v2 新增**：M6 `COLREGsConstraint.msg` 缺 past-clear/release 字段——Slice G 前置。
12. **v2 新增**：ship-domain/BCT/BCD 在 kernel 无实现——进 M2 geometry 新建。
13. **v2 新增（Slice H）**：M7 HC-1~HC-6 硬约束判定逻辑死代码——`run_hard_constraint_checks`@`safety_supervisor_node.cpp:548-552` 空 stub，运行时从未调用（仅单测）。Slice K 接线。
14. **v2 新增（Slice H）**：M7 `on_avoidance_plan`@`:298-301` 把 M5 输出 `dcpa/heading_change/speed/rot` 全置 0——硬约束即使接线输入也永远退化。Slice K 修。
15. **v2 新增（Slice H）**：M7 不消费 NLP solver status（IPOPT 收敛/KKT/`Restoration_Failed`/`tail_gate_failed`）——NLP 不收敛/局部最优无 Checker 直接检测。Slice K 接入（须 M5 AvoidancePlan 扩字段）。
16. **v2 新增（Slice H）**：`l3_risk_model` 被 M4/M5/M7/M8 共享，但 CI 独立性脚本 `tools/ci/check-doer-checker-independence.sh:36-43` FORBIDDEN_INTERNAL 未含它——CI 盲点。Slice K 补。
17. **v2 新增（Slice H）**：`sliding_window_15s.hpp` kCapacity=100 @ 4Hz = 25s 窗，文件名 "15s" 误导；spec/architecture 引用 "6.7Hz/15s" 须改真值 4Hz/25s。

## 23. Source Coordinates

- M5 1 Hz solve timer / dual publish：`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
- NLP config defaults：`include/.../mid_mpc/mid_mpc_nlp_formulation.hpp`
- NLP 实现（J_colreg/J_asym/constraints/nlpsol IPOPT）：`src/.../mid_mpc/mid_mpc_nlp_formulation.cpp`
- Constraint compiler（CPA 硬约束/rule14-17/zone）：`src/.../shared/constraint_compiler.cpp`
- Waypoint sampling defaults：`include/.../mid_mpc/mid_mpc_waypoint_generator.hpp`
- Current YAML params：`config/m5_params.yaml`
- M5 自带 CPA（重复）：`src/.../shared/cpa_calculator.cpp`
- M5 NLP acceptance（heading-only）：`include/.../m5 types.hpp:533 trajectory_reaches_colregs_target`
- GNC bridge：`src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp` + `translators.cpp`
- GNC active route manager：`third_party/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp`
- GNC coordinate transform/update guard：`third_party/gnc_ws/src/gnc/ship_guidance/src/coordinate_transform_node.cpp`
- **M2 CPA/TCPA + covariance**：`src/.../m2_world_model/.../cpa_tcpa_calculator.cpp`、`encounter_classifier.hpp`
- **M6 past-clear / release / latch / FSM**：`src/.../m6_colregs_reasoner/.../colregs_reasoner_node.cpp`、`colregs_release_policy.hpp`、`rule_latch.hpp`、`encounter_state_machine.hpp`
- M6/M2 共享 floor：`config/odd_aware_thresholds.yaml`
- 离线 scorer（s_clear 语义模板）：`scripts/run_6_scenarios.py`（C1/C5/C7 tri-condition + dwell）

## 24. Implementation Completion Definition

实现完成当：
- M5 有一个执行真相源。
- GNC 消费该源。
- M5 内部持有完整 avoidance-plus-return committed route。
- M5 仅在 meaningful event 或 heartbeat 发布完整航线版本。
- fallback/corridor/recovery 不再创建互相独立的执行输出。
- **stand-on 全角色合规**（§4 矩阵）。
- **M6 msg 扩字段就绪 + M5 消费不重判语义**（§14）。
- **keep-last-route ≤45 s + 风险门控**（§9.12）。
- **架构报告凸性措辞同步修订**（§18）。
- **M7/X-axis 独立性代码验证通过**（路径 A 前提）。
- local unit + GNC-profile scenario 证据证明航线稳定、COLREG avoidance、return-to-route。

## 25. Decision Record（v2 新增 — 评审 6 决策点追溯）

| # | 议题 | 决策 | 依据 | spec 落地 |
| --- | --- | --- | --- | --- |
| 1 | M6 side/role/past-clear 真相归属 | **M6 独占**，M5 消费不兜底（避免重复判断→执行矛盾，防 role-collapse 重演） | 用户拍板；memory `l3-colregs-m6-m4-role-collapse` | §3.1 契约、§14 msg 扩字段、§9.7 复用规则、§21 Non-Goal |
| 2 | keep-last-route 超时基线 | **≤45 s**（H_pred/2）+ 风险门控 | 用户同意；nlm 🟢 stale-route policy | §9.12、§5 `stale_committed_at`、§20 |
| 3 | 凸/非凸矛盾 | **路径 A**：承认非凸 + 修文档 + 补 SOTIF/policing argument（不改算法）+ **Slice K M7 接线**（运行时 policing 当前死代码，须接线才能认证演示） | 用户拍板；nlm 🟢 IEC61508/SOTIF/MAXCMAS/DNV AROS；Slice H 验证暴露代码层 gap | §13、§13.4 Slice H 结论、§18、Slice H✅/I/K |
| 4 | stand-on 分支 | **补齐所有 COLREGs 角色**（give-way/stand-on/overtaking/overtaken） | 用户同意；COLREGs Rule 17 review C1 | §4 角色矩阵、§9.5 stand-on reject、§20 |
| 5 | s_clear 量化判据 | **复用现有判断不自写**（消费 M6 信号 + M2 几何量，TailBuilder 仅加 dwell/站位外推/滞回） | 用户要求；调研 §2.3 | §9.7 复用规则表、§14、Slice G 前置 |
| 6 | V&V（Monte Carlo/故障注入）归属 | **暂不纳入本 spec**，留独立 D-task | 用户暂不考虑 | §21 Non-Goal |
| 7 | Slice K（M7 policing 接线）归属 | **纳入本 spec**（M5+M7 联合，认证前置一体化） | 用户拍板（选项 A）；Slice H 暴露 policing 死代码 | §19 Slice K、§13.4、§22 #13-17 |
