# 设计 Spec：仿真倍速确定性 + 1x 校准（Phase 1）

- **日期**：2026-05-29
- **状态**：已通过 brainstorming，待 writing-plans
- **方案**：方案 1（分阶段）——sim-time 定时器 + 固定增量节流时钟。吞吐/自由运行模式（方案 2）另立 spec。
- **作者**：marinehdk（solo，多角色）

---

## 1. 背景与症状

DEMO-1 修补后，HMI 可见本船避碰逻辑。用户报告两个相关现象：

1. **避碰后不回归航路**：本船右转避碰后与原航路平行航行，不切回。
2. **倍速异常**：10x/50x 达不到真实墙钟加速；且**同一场景不同倍速仿真结果不同**。

排查 `imazu-01-ho`（对遇，OS heading 0° / TS heading 180°，航路 = lon=10.38 子午线）时发现：**症状 1 是症状 2 的伪影**。必须先解决倍速确定性，才能得到可信的避碰验证基线。

### 实测证据（1x 真实运行，1482 行遥测）

| 观测 | 数据 |
|---|---|
| 本船 heading | 全程 0.0°（t=0→1020s），舵角恒 0，rot 恒 0 |
| 行为 / 冲突 | 全程 COLREG_AVOID，conflict=1，**从未切回 TRANSIT** |
| XTE | 0（笔直沿子午线北行，根本没转） |
| RTF @ rate=1.0 | **1.74**（21.2s 墙钟内仿真推进 37s，实测） |

→ **结论：1x 真值下本船压根不避碰**；用户截图里的"右转+平行"是高倍速产物。倍速 bug 不是量变是**质变（转 vs 不转）**。本 spec 只解决倍速确定性 + 1x 校准；避碰断链（M5 `turn_radius_m` 占位、M3 `current_target_wp=(0,0)`、无 L4/XTE 闭合）另行处理。

---

## 2. 根因（已验证：代码 + 实测）

### 2.1 控制节点跟墙钟、物理跟仿真钟（确定性破坏）

- 物理节点（`ship_dynamics`、`target_vessel`）`use_sim_time=True`，按 `/clock` 流逝量做 `steps=int(elapsed/dt)` 步进，RK4 `dt` 固定。
- L3 控制节点 **M1/M3/M4/M5/M6/M7/M8 全用 `create_wall_timer`（墙钟）**，不随仿真钟缩放。
- **唯一例外：M2** 已用 `rclcpp::create_timer(get_clock(), …)`（sim-time），是仓内已验证的正确范式。
- 后果：物理:控制 步数比随倍速漂移（1x≈25 物理步/控制拍 → 10x≈250 → 50x≈2500），控制器相对被控对象作用节奏全变 → **跨倍速轨迹不同**（物理真实差异，非浮点噪声）。

### 2.2 时钟"变增量"推进（RTF≠rate + 加剧确定性问题）

`lifecycle_mgr._clock_callback` 由 `create_wall_timer(1/tick_hz)` 驱动，`tick()` 每拍执行 `sim_time += (1/tick_hz) * sim_rate`（增量随 rate 变）。
- 实测 rate=1.0 时 RTF=1.74 → rclpy 墙钟定时器**超发**（~435Hz 而非额定 250Hz），固定增量假设失效。
- 高倍速时增量变大 → sim-time 定时器看到的时钟粒度变粗 → 进一步破坏确定性。

> 关键洞察：**校准（RTF≠rate）与确定性是同一根因**——时钟应按**固定增量**推进，倍速只改**发射节奏**，不改增量。

---

## 3. 目标 / 非目标

### 目标（Phase 1）
- **G1 跨倍速确定性**：同一场景在 1x / 10x（及 50x，CPU 允许时）产出逐点一致的轨迹（容差内）。
- **G2 1x 校准**：rate=1.0 时 RTF ∈ [0.95, 1.05]。
- **G3 回归基线**：新增确定性回归测试，作为本修复的 failing-test。

### 非目标（明确排除）
- ✗ 真·10x/50x 墙钟吞吐（CPU/GIL 受限，属方案 2/3）。
- ✗ 自由运行锁步模式（方案 2，另立 spec）。
- ✗ 避碰断链 / 回归航路修复（用户要求先深挖）。
- ✗ 触碰 ADR / 模块职责 / 避碰算法。
- ✗ 物理引擎性能优化（方案 3）。

---

## 4. 设计

### 核心原则
> 倍速只改变 tick 的**发射节奏**，不改变 tick 的**增量**。所有决策/控制节点由仿真钟驱动；时钟按固定增量推进、按倍速节流到墙钟。固定增量 = 确定性来源；节流 = 倍速。

### 4.1 时钟：固定增量 + 墙钟配速发射（`lifecycle_mgr.py`）

- `dt_tick` 固定为 `1/tick_hz`（如 1/250s），**`tick()` 不再乘 `sim_rate`**：`sim_time += dt_tick`。
- `_clock_callback` 改为**墙钟配速循环**（realtime 模式）：
  ```
  wall_elapsed = now_wall - run_start_wall
  target_sim   = wall_elapsed * sim_rate
  emitted = 0
  while sim_time < target_sim and emitted < MAX_CATCHUP_TICKS:
      sim_time += dt_tick
      emitted += 1
  publish /clock, /sim_clock  (publish once per callback, latest sim_time)
  ```
  - `MAX_CATCHUP_TICKS`：单次回调最大补发拍数，防"追赶风暴"（落后过多时）。
- `set_sim_rate(rate)`：只改 `sim_rate`（配速目标），不改 `dt_tick`。
- 效果：平均 RTF=rate（精确，免疫墙钟定时器超发）；增量恒定 → 跨倍速确定。
- **待 writing-plans 决策点**：`/clock` 每个补发拍是否各发一条（让 sim-time 定时器精确触发）vs 每回调发一条最新值。建议**每个 dt_tick 发一条 `/clock`**，保证下游 sim-time 定时器逐拍触发、不漏拍（代价：高倍速下 /clock 发布频率升高）。此点需在 plan 中定夺并测。

### 4.2 控制节点：墙钟 → 仿真钟定时器

将下列 `create_wall_timer(...)` 改为 `rclcpp::create_timer(get_clock(), period, cb)`（照抄 M2 [world_model_node.cpp:297](../../../src/l3_tdl_kernel/m2_world_model/src/world_model_node.cpp) 范式）。`use_sim_time:=True` 已在 [sil_entrypoint.sh:297](../../../docker/sil_entrypoint.sh) 启动时设好，故 `get_clock()` 返回仿真钟。

**完整清单（精确锚点）：**

| 模块 | 文件 | 行 | 定时器 | 确定性关键? |
|---|---|---|---|---|
| M1 | `m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp` | 474 | main_loop_timer_ | ✅ 是 |
| M1 | 同上 | 478 | odd_publish_timer_ | 次要 |
| M1 | 同上 | 482 | asdr_periodic_timer_ | 次要 |
| M1 | 同上 | 486 | sat_timer_ | 次要 |
| M3 | `m3_mission_manager/src/mission_manager_node.cpp` | 310 | mission_goal_timer_ | ✅ 是 |
| M3 | 同上 | 316 | asdr_timer_ | 次要 |
| M3 | 同上 | 320 | replan_deadline_timer_ | ⚠️ 见 4.4 |
| M3 | 同上 | 324 | heartbeat_timer_ | ⚠️ 见 4.4 |
| M3 | 同上 | 328 | l1_watchdog_timer_ | ⚠️ 见 4.4 |
| M4 | `m4_behavior_arbiter/src/behavior_arbiter_node.cpp` | 74 | timer_ | ✅ 是 |
| M5 | `m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` | 87 | solve_timer_ | ✅ 是 |
| M5 | `m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp` | 51 | validity_timer_ | ✅ 是 |
| M6 | `m6_colregs_reasoner/src/colregs_reasoner_node.cpp` | 383 | reasoning_timer_ | ✅ 是 |
| M6 | 同上 | 385 / 387 / 389 | health / asdr / sat | 次要 |
| M7 | `m7_safety_supervisor/src/safety_supervisor_node.cpp` | 232 | timer_main_ | ✅ 是 |
| M7 | 同上 | 237 / 242 / 247 | sat / asdr_periodic / heartbeat | 次要 / ⚠️ |
| M8 | `m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp` | 127–131 | ui / tor / health / asdr_snapshot / sil_stub | 次要（HMI 节流） |

> 注：`m4_behavior_arbiter/.salvage-d3.1/...` 是备份目录，**不改**。

**原则**：为消除节点内 wall/sim 混用，**全部转 sim-time**；但 ⚠️ 标记项（watchdog/heartbeat）语义可能绑定真实墙钟存活性，见 4.4。

### 4.3 Python 仿真节点

`docker/*.py`（`sil_topic_bridge` / `mock_l2_publisher` / `fsm_aggregator_node` / `diagnostic_mock_publisher`）**未用 `create_wall_timer`**（grep 零命中），用 `create_timer` + 启动带 `use_sim_time:=True` → **大概率已是 sim-time**。
- **plan 动作**：逐个复核 `create_timer` 是否真按仿真钟触发；`sil_topic_bridge` 的 HeadingController 配速若依赖 wall（如 `time.time()`）需改为 sim-clock。**预期改动小或无**。

### 4.4 watchdog / heartbeat 定时器处理（决策点）

`l1_watchdog`（M3:328）、各 heartbeat（M3:324 / M7:247）语义 = 检测上游消息是否在 N 秒内到达。
- **建议**：SIL 内一律改 sim-time（确定性优先；超时阈值也按仿真钟），使 watchdog 行为可复现。
- **风险**：若某 watchdog 旨在检测"节点真死/墙钟卡顿"，sim-time 下仿真暂停会误判——需在 plan 中逐个确认语义。**默认 sim-time，例外需显式论证。**

### 4.5 确定性回归测试（新增，failing-test 基线）

- 复用本轮 `capture_imazu.py`（见附录 B），落库到合理位置（如 `tests/integration/` 或 `tools/sim/`，由 plan 定）。
- 测试流程：
  1. 跑 `imazu-01-ho` @rate=1，采轨迹 CSV_A（sim_t, lat, lon, heading, rudder, behavior, conflict）。
  2. 跑同场景 @rate=10，采 CSV_B。
  3. 在相同 sim_t 网格上对齐，断言：位置偏差 < 1m、heading 偏差 < 0.1°、behavior/conflict 序列一致。
  4. 测 RTF(rate=1) ∈ [0.95,1.05]。
- 改前跑 → 红；改后跑 → 绿。
- **环境依赖**：需 docker 栈（`docker compose up` + lifecycle API，HTTPS:8000，证书跳过校验）。测试需可在 CI/本地以脚本驱动 lifecycle cleanup→configure→rate→activate→采集→deactivate。

---

## 5. 受影响文件清单

**改：**
- `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py`（时钟固定增量 + 配速循环 + set_sim_rate）
- 7 个 L3 C++ 节点 timer 创建处（见 4.2 表，共 ~24 处 create_wall_timer）
- `docker/*.py` 4 个节点（复核，预期小改/无改）
- 新增确定性回归测试 + 落库捕获脚本

**不改：**
- 物理 RK4（`ship_dynamics`/`target_vessel`，已 sim-time）
- M2（已正确，作范式参考）
- 避碰逻辑 / M5 plan 生成 / M3 航路点 / bridge 舵转换（另行处理）
- ADR / 模块职责 / 避碰算法

---

## 6. 验证与完成判据

- [ ] 确定性回归测试绿：imazu-01-ho 1x vs 10x 轨迹逐点匹配（容差内）
- [ ] RTF(rate=1.0) ∈ [0.95, 1.05]（实测，当前 1.74）
- [ ] imazu-01-ho @1x 改后行为与改前 1x **定性一致**（不引入新行为回归——注意：改后仍"不避碰"是预期，避碰断链不在本 spec 范围）
- [ ] 现有单测 + 场景网关（`.preflight/gate_*.json`）不破
- [ ] colcon build 通过（Docker BuildKit cache 规范见 CLAUDE.md §12，勿删 cache mount）

---

## 7. 风险与缓解

| 风险 | 缓解 |
|---|---|
| rclpy/rclcpp sim-time 定时器在 `/clock` 大跳变时只触发一次（不 catch-up） | 固定小增量 + 每 dt_tick 发一条 /clock（4.1 决策点），避免大跳 |
| 配速循环"追赶风暴"（落后过多一次发太多拍） | `MAX_CATCHUP_TICKS` 上限；落后超限则降级（记 WARN，接受短暂 RTF<rate） |
| 仿真暂停时 sim-time 定时器停 | 符合预期；watchdog 语义需 4.4 复核 |
| 高倍速 /clock 发布频率升高增加 DDS 负载 | 属吞吐范畴；本 spec 只保确定性，CPU 不足时优雅降速但结果仍确定 |
| watchdog 改 sim-time 后误判节点存活 | 4.4 逐个确认；默认 sim-time，例外显式论证 |
| 容器内 ~9 个泄漏 `ros2 topic hz /clock` 进程（附带发现） | 非本 spec 范围，记录待查 |

---

## 8. 附录

### A. 当前实测证据
- 1x 完整遥测：1482 行（本轮采于 host `/tmp/imazu_capture.csv`，run-19e716c5a3f）。
- RTF 实测：rate=1.0 → 1.74（run-19e718e0f85）。
- 完整决策链路与断点分析见会话记录 / `docs/Design/Review/2026-05-29/`（专家评审，注意其结论基于改前代码，部分已被本轮实测修正）。

### B. 捕获脚本（确定性测试可复用）
路径（本轮临时）：host `/tmp/capture_imazu.py` → 容器 `/tmp/`。订阅 `/sil/own_ship_state`(sil_msgs/OwnShipState)、`/l3/m4/behavior_plan`(l3_msgs/BehaviorPlan)、`/l3/m6/colregs_constraint`(l3_msgs/COLREGsConstraint)、`/clock`，2Hz 合并 CSV，XTE 按 lon=10.38 子午线算 easting。plan 应将其规范化落库。

### C. 关键代码引用
- 时钟：`lifecycle_mgr.py` `tick()` (L158-161)、`on_activate` 定时器 (L279-299)、`_clock_callback` (L336-352)、`on_configure` 参数 (L240-262, tick_hz 默认 250)。
- 范式参考：M2 `world_model_node.cpp:297-321`（`rclcpp::create_timer(get_clock(),…)`）。
- 启动 use_sim_time：`docker/sil_entrypoint.sh:102, 297-299`。

### D. 如何运行仿真（供测试/复现）
```
# 栈已在 docker（network_mode: host），orchestrator API 走 HTTPS:8000（自签证书，校验跳过）
# 容器内驱动（python urllib + ssl CERT_NONE）：
#   POST /api/v1/lifecycle/cleanup
#   POST /api/v1/lifecycle/configure {"scenario_id":"imazu-01-ho"}
#   POST /api/v1/lifecycle/rate      {"rate":1.0}
#   POST /api/v1/lifecycle/activate
#   ... 采集 ...
#   POST /api/v1/lifecycle/deactivate
# 容器内采 ROS2：docker exec mass-l3-tacticallayer-sil-nodes-1，source ROS + /opt/ws/install/setup.bash
```
