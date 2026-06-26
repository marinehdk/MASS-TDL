# 避碰修复 + 验证 工作交接（Handoff Brief）

- **日期**：2026-05-29
- **用途**：交给新对话，按 brainstorming→spec→writing-plans→execute 完成"避碰断链/回归"全部修复与测试
- **前序产物**：倍速确定性 spec `docs/superpowers/specs/2026-05-29-sim-speed-determinism-design.md` + 实现 `docs/superpowers/plans/2026-05-29-sim-speed-determinism.md`（分支 `feat/sim-speed-determinism`）
- **关键根因 memory**：`project_l3_avoidance_steering_broken` / `project_l3_no_route_return_root_cause` / `project_sim_speed_nondeterminism`

---

## 0. 当前状态快照（很重要，决定起点）

| 项 | 状态 |
|---|---|
| 倍速确定性 Phase 1 | ✅ 已做（feat/sim-speed-determinism）：L3 节点 wall→sim-time timer、lifecycle 固定增量+墙钟配速、M8 健康监控 sim-time。**实测 RTF(1x)=0.998**（改前 1.74） |
| 跨倍速 <1m 确定性 | ⚠️ 未达成：实测 10x 漂移 ~7.67m，根因=异步 DDS 延迟，需 Phase 2 锁步执行器（另立 spec） |
| **基准倍速决策（原 P0 复现门槛）** | ✅ **已定：用 1x**。1x 现已校准可信。**所有避碰验证一律 1x**；高倍速暂不用于定量对比 |
| 避碰断链（船不转） | ❌ 未修 |
| 回归航路（不回航线） | ❌ 未修 |
| XTE 闭合架构归属 | ❌ 未决（触 ADR） |

> ⚠️ 重要：原优先级列表里的"P0 复现门槛""P1 倍速确定性"已分别**解决/部分完成**。本交接聚焦剩余的**避碰三件事**。

---

## 1. 已确认 BUG 清单（根因 + 证据 + 文件锚点）

### BUG-1【P0】本船不执行避碰转向（船笔直走，舵恒 0）
- **实测**（1x 真值，imazu-01-ho）：heading 全程 0、rudder=0、rot=0，behavior 卡 COLREG_AVOID，conflict 不清。M4 正确发 COLREG_AVOID 窗口[16,60]°，但舵没动。
- **数据流断点**：`M5 /l3/m5/avoidance_plan → sil_topic_bridge（mock-L4 替身）→ /sil/actuator_cmd → ship_dynamics`。运行系统**无真实 L4/控制器节点**（scenario 声明的 `psbmpc_wrapper` 未实例化；`/l4/tracking_error` 发布者=0）。
- **精确根因**：实测 plan `turn_radius_m=0`、`schema_version=0` → 走的是 **NLP 路径 `wp_gen_.generate`**（NLP 实际收敛，非几何回退）。该路径**不填 turn_radius_m**：
  - `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp:91` → `wp.turn_radius_m = 0.0;  // Phase E1 placeholder`
  - 对照：几何回退 `mid_mpc_node.cpp:278,344` **会**算并填 turn_radius，但 `solver_failed||m4_geometric` 才走，运行时不走。
- **bridge 闸门**：`docker/sil_topic_bridge.py` `_on_avoidance_plan`（~L507-549）仅当 `abs(waypoints[0].turn_radius_m) > 1e-6` 才下舵；占位 0 → 复位 `_avoidance_target_heading_deg=None`、`last_cmd_deg=0` → **rudder 恒 0**。仓内有测试 `test_placeholder_turn_radius_does_not_command_hard_rudder` 印证这是设计行为。bridge 另有 DELAYED-LATCH/LATCHED 逻辑会从 M4 heading 窗口锁存目标航向（L328-340, L532-549）。
- **NLP 只转到 ~16°（窗口下限）**：距离代价 `Σ(psi-route_bearing)²` + M4 窗口下限，收敛到最小转向。

### BUG-2【P1】避碰后不回归航路（平行航行）
- **根因（实证）**：`MissionGoal.current_target_wp` **全 src 无写入方**（M3 从不填），实测恒 `(0,0)`。
  - 读取方仅 M4 `behavior_arbiter_node.cpp:233-238`；判据 `abs(lat)>1e-4` 恒假 → TRANSIT 的 `nominal_hdg` **永远退化为本船当前航向** → TRANSIT = "保持当前航向"而非"驶向航路点" → 避碰后锁死在偏航方向。
- **纠正**：专家评审归咎"IvP 梯度太弱"是**错的**——锚点本身错，梯度无意义。

### BUG-3【架构】全栈无 XTE（横向偏差）闭合
- M5 距离代价 `mid_mpc_nlp_formulation.cpp:73` = `Σ(psi-route_bearing)²`（只罚航向、不罚横向）；几何回退无视航路。
- `xte_nm=-1`（L4 不可用）。循线/回归本设计委托 L4（注释 "aligned with L4 LOS period"），但 **L4 不存在**，L3 只发航向指令 → XTE 闭合掉进 L3↔L4 缝隙。
- **决策点（触 ADR）**：XTE 闭合放 L4（新建/补 mock-L4 LOS）还是临时落 L3？需单独对齐，**不要擅自决定**。

### BUG-4【验证工装·P0-blocker】采集脚本不可靠
- `tests/integration/sim_determinism/capture_imazu.py` **硬编码 `scenario_id="imazu-01-ho"`**（L143）、`ROUTE_LON=10.38`（L32，对 rule14 恰好也对）。
- 本会话实测：自驱采集**间歇返回 0 行**（尽管 `/sil/own_ship_state` 实测 ~26Hz 在发）；并发多个采集写同一文件会污染。
- 已写好参数化版本 `/tmp/capture_rule14.py`（带 `--scenario`），**需落库 + 修可靠性**。

### 附带发现（非阻塞，可顺手）
- 容器内堆积泄漏的 `ros2 topic hz /clock` 进程（疑似未回收的健康检查）。
- rule14/imazu 实测 sog 只到 ~4-6kn（非配置的 10-12kn），疑物理初始化/推进偏低，需确认是否影响结论。
- 倍速时钟用 `time.time()`（可 NTP 回拨），建议 `time.monotonic()`。

---

## 2. 修复优先级与具体方案

> 顺序原则：**先让能转 → 再让能回 → 最后看 XTE 架构**。每步独立可验证。

### 步骤 0【P0-blocker】先修验证工装（否则无法验证任何修复）
- 落库 `/tmp/capture_rule14.py` → `tests/integration/sim_determinism/`（带 `--scenario`）。
- 修 0-行可靠性：订阅后**先等到收到 ≥1 条 `/sil/own_ship_state` 再开始计时**；duration 用 **sim_t**（非墙钟）界定覆盖；确保单实例（运行前 pkill 残留 + lifecycle cleanup）。
- 验收：rule14 与 imazu 各跑一次 1x，稳定产出 >100 行、含完整 sim_t 序列。

### 步骤 1【P0】让船能避碰 —— 修 BUG-1
- **方案 A（推荐）**：在 M5 `wp_gen_.generate` 里**真实计算并填充 `turn_radius_m`**（参照几何回退 `mid_mpc_node.cpp:278` 的 `R=u/ω`，ω 来自 `rot_max_rad_s`），并设 `schema_version`。改动局限 M5，不碰 bridge、时序无关、不与倍速分支冲突。
- **方案 B（备选）**：改 bridge，让其在 turn_radius 占位时退而用 M4 heading 窗口（已锁存的 `_avoidance_target_heading_deg`）驱动 avoidance heading controller。**缺点**：碰 `sil_topic_bridge.py`（倍速分支也改过、时序敏感），冲突+漂移风险。
- **决策点**：A/B 二选一，建议 A。若 A 后仍不转，再查 bridge 的 latch/reset 时序（注意倍速已把 timer 改 sim-time，bridge HeadingController 时序已变）。
- **验收（1x, colreg-rule14-ho 优先，目标更近更快）**：相遇后 rudder 出现明显非零（右舷）、heading 右偏、CPA ≥ 配置阈值（rule14: cpa_min_m_ge=500）、conflict 在目标驶过后清除、behavior 回到 TRANSIT。

### 步骤 2【P1】让船能回归 —— 修 BUG-2
- **方案**：M3 `mission_manager_node.cpp` 真正填充 `MissionGoal.current_target_wp` = 当前航路腿的下一个航路点（来自已订阅的 `/l2/planned_route`）；含航路点推进逻辑（到达半径切换下一个）。
- 解锁后 M4 TRANSIT 的 `nominal_hdg` 指向真实航路点 → 朝航路点的航向制导**天然减小横偏**（只要航路点在前方）。
- **验收（1x）**：避碰结束后 XTE 单调收敛回 ~0（船切回黄色航线），heading 回到航路方位；imazu 与 rule14 都验。

### 步骤 3【架构·需对齐】XTE 闭合归属 —— 决策 BUG-3
- 先验证：步骤 2 的"朝航路点制导"是否已让 XTE 足够收敛。**若够 → 本步可降级/暂缓**。
- 若不够（如大偏差下切入角太平、收敛慢），再决策：
  - 选项 1：补 mock-L4 LOS/循线（在 bridge 或新节点）——把 XTE 闭合放回设计意图的 L4 层。
  - 选项 2：M5 距离代价加 XTE 项（`mid_mpc_nlp_formulation`）——但这是 L3 越界承担 L4 职责，**触 ADR，须先对齐**。
- **不要在没对齐前改 ADR 相关层职责。**

### 步骤 4【可选/另立】倍速 Phase 2 锁步执行器
- 仅当需要高倍速定量验证时再做。当前 1x 已满足避碰调试。独立 spec。

---

## 3. 验证策略
- **一律 1x**（RTF 已校准 0.998 可信）。高倍速结果不可信（跨倍速漂移未解决）。
- **场景**：快速回归用 `colreg-rule14-ho`（2nm 对遇，TCPA~327s，转向~200s，total 600s）；回归充分性用 `imazu-01-ho`（7nm）。两者航路均 lon=10.38 子午线 → **XTE = 经度 easting**。
- **每步 failing-test 先行**：先用工装抓"修前"基线（红），改后抓"修后"（绿）。关注量：rudder 峰值、heading 轨迹、XTE 轨迹、behavior/conflict 序列、CPA_min。
- **运行方式**：栈在 docker（network_mode host）。orchestrator HTTPS:8000（自签证书，`ssl.CERT_NONE`；HTTP 会被秒断）。容器内驱动 lifecycle：cleanup→configure{scenario_id}→rate{1.0}→activate→采集→deactivate（从 orchestrator 容器内 python urllib）。采 ROS2：`docker exec ...sil-nodes-1`，source `/opt/ros/*/setup.bash` + `/opt/ws/install/setup.bash`。
- **环境注意**：避免并发多个采集（会争 lifecycle + 污染同名 CSV）；运行前 pkill 残留 capture；docker exec 命令**串行**执行（并行易被 SIGKILL）。

---

## 4. Git / 分支策略
- 倍速工作在 `feat/sim-speed-determinism`（已验收，建议先合入 main）。
- 避碰修复**新建分支**：建议从已合入倍速的 main 切 `feat/avoidance-steering-return`（或拆两个：`feat/d-m5-turn-radius`、`feat/d-m3-target-wp`）。
- 若倍速未合入：从 `feat/sim-speed-determinism` 切，避免在旧 main（墙钟时序）上验证（行为不一致）。
- main 禁止直接 commit；一个修复一个分支，merge 后删。

## 5. 约束与耦合（CLAUDE.md）
- 倍速已把 timer 改 sim-time，**bridge 时序已变**——任何碰 bridge 的避碰改动须在新栈上验证。
- 不碰 ADR/模块职责（XTE 归属须对齐）；不顺手重构；引用编号 `[Rx]` 硬约束；Docker BuildKit cache 勿删（§12）。
- 禁止 fake-stub/xfail 推卸（`project_no_fake_stubs`）；每步有可验证完成条件。
- 实测 sog 偏低、0-行采集等异常**先查清再下结论**，别绕过。

## 6. 验证完成的总判据（DoD）
- [ ] 工装可靠：rule14/imazu 1x 稳定采集
- [ ] BUG-1 绿：1x 下船明显右转避碰，CPA≥阈值
- [ ] BUG-2 绿：避碰后 XTE 收敛回航线、behavior 回 TRANSIT
- [ ] BUG-3：已验证步骤2是否足够；不足则架构决策已对齐
- [ ] imazu + rule14 双场景 1x 通过；现有单测/网关不破；colcon build 过
