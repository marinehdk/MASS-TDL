# 设计 Spec：确定性无头超实时仿真底座（MC/RL 支撑）— Approach 2/3

- **日期**：2026-05-30
- **状态**：已通过 brainstorming + Wave-1 调研（3 subagent），待 writing-plans
- **关系**：本 spec 是 [2026-05-29-sim-speed-determinism-design.md](2026-05-29-sim-speed-determinism-design.md) §3 明确推迟的「方案 2/3（自由运行锁步吞吐 + 物理性能）」的独立 spec。Approach 1（墙钟→仿真钟定时器 + 固定增量时钟）**已在分支 `feat/sim-speed-determinism` 落地**（main 无），本 spec 在其之上构建。
- **作者**：marinehdk（solo，多角色）

---

## 0. 已锁定决策（用户 2026-05-30 确认）

| 决策点 | 选择 | 架构含义 |
|---|---|---|
| 快循环里跑什么 | **真实 M1–M8 决策内核即 SUT** | MC 验证真实认证栈；RL 围绕真实栈调参/对标。无头快路径必须跑**真实 ROS2 节点**，不是物理复现品 → RSLCPP 式确定性单进程合成 |
| 确定性等级 | **从一开始就 bit-identical** | 单线程确定性 executor + 互斥回调组 + 固定浮点算子顺序（禁 `-ffast-math`）+ 跨主机可复现；不接受「仅容差匹配」 |
| 与 DEMO-1（6/15）关系 | **完整多阶段计划，不为 6/15 单独切分** | 写完整 shared-core dual-shell 计划，内部排期；不专门围栏 6/15 |

---

## 1. 背景与目标

### 1.1 为什么
当前仿真只在 ~1x 真实可信，验证一次场景需墙钟分钟级；蒙特卡洛（MC，COLREGs 覆盖立方）与强化学习（RL）需成千上万次重复运行，缺无头超实时底座无法支撑。Approach 1 修了「控制节点跟墙钟」的确定性根因（C++ 内核已转仿真钟），但**远不充分**：还有未播种 RNG、转向链墙钟、多进程 DDS 调度、逐回合 HTTP 生命周期重置（秒级）、`MAX_CATCHUP_TICKS=10` 静默节流、BEST_EFFORT QoS 丢包等阻断项。

### 1.2 Wave-1 调研结论（收敛）
两个独立研究 subagent 均收敛到 **「shared-core, dual-shell」**：
- **共享核**：纯物理已存在——`MMGModel.rk4_step`（`mmg_model.py`，无 DDS/无时钟，固定 dt RK4；离线测试 σ<1e-9 / 20×180k 步，**数学确定，图不确定**）。另有 C++ 仿真器 `src/sim_workbench/fcb_simulator/`（节点 `fcb_simulator_node.cpp` + plugin），但其 Python 绑定 **`fcb_sim_py` 当前疑为 mock**（仓内存在 `fcb_simulator/python/fcb_sim_py_mock.py` 与 `tools/sil/fcb_sim_py_mock.py`）——§3.4 P1（统一到 C++ 核）的前提**必须在 Phase-B spike 验证 `fcb_sim_py` 是真实绑定还是 mock**，若为 mock 则 P1 成本上升、需重估。
- **Shell A** = 现有 ROS2 SIL = 实时认证基线（HMI/ROC）。**保留、行为不变**。
- **Shell B** = 无头、自由运行、亚毫秒重置的确定性快路径，跑**同一批真实节点**。
- **关键先例：RSLCPP（TUM, 2026-01, arXiv 2601.07052）** [R1]——把**未修改的 rclcpp 节点**装进单进程、单线程确定性事件队列 executor，仿真钟推进到下一事件，**100 次运行 × 7 CPU bit-identical**，绕过 DDS 与墙钟，"enables sped-up simulations"。这是与本项目目标最贴合的可落地先例。海事超实时速度先例：MOOS-IvP / uSimMarineV22 **200–300x** [R2]。

### 1.3 目标（G）
- **G1 真栈无头超实时**：真实 M1–M8（+物理）在无头模式自由运行，RTF ≫ 1（目标 ≥10x，争取 ≥50x，受 CPU/单线程吞吐约束，无墙钟封顶）。
- **G2 bit-identical 确定性**：同场景同 seed，跨倍速（1x/10x/50x，因已无墙钟封顶，倍速对 Shell B 仅是观测/采样语义）**且跨主机**逐位一致；至少逐点容差（pos<1m / hdg<0.1° / behavior+conflict 序列一致，沿用 `test_determinism.py`）作为下限门，bit-identical 作为目标门。
- **G3 亚毫秒回合重置**：`reset(seed)` 原地恢复状态，不走 HTTP 生命周期、不重启进程/Docker。
- **G4 RL 接口**：暴露 Gymnasium `Env`（`reset(seed,options)->(obs,info)` / `step(action)->(obs,reward,terminated,truncated,info)` / `close`）[R3]，框架无关（SB3/RLlib/CleanRL 均消费 Gymnasium）。
- **G5 MC 复现**：进程级并行 MC，独立 RNG 流（`SeedSequence.spawn` 按 `(root, episode, worker)`）[R4]，复用 `tools/sil/coverage_cube.py`（1100 格覆盖立方）+ `fmi_bridge/` farn 扫描。
- **G6 认证可辩护**：Shell B 与 Shell A **跑同一份决策逻辑**（仅传输/调度不同，RSLCPP 论证）；IEC 61508 T2 工具鉴定 + 1x Shell B↔Shell A 交叉校验 [R5][R6]。

### 1.4 非目标
- ✗ 改避碰算法 / M5 NLP 收敛 / M3 航路（属并行的 avoidance-chain 修复）。
- ✗ GPU 并行物理（MJX/Genesis 类，远期）。
- ✗ 触碰 ADR（见 §4 硬约束）。
- ✗ 重写决策逻辑（必须跑真实节点）。

---

## 2. 当前基线（Wave-1 forensic，带 file:line）

### 2.1 进程/执行模型（吞吐瓶颈）
- 4 容器，`network_mode: host`，**RMW=`rmw_cyclonedds_cpp`，ROS2 Humble**（`docker-compose.yml`，`sil_nodes.Dockerfile:4,27`）。
- `sil-nodes` 容器内 **~13 进程**：主进程 `MultiThreadedExecutor(num_threads=8)` 装 8 个 Python SIL 节点；`scenario_lifecycle_mgr` 独立 2 线程 executor（单线程会被定时器抢占致 SetParameters 饿死）；**M1–M8 各为独立 `ros2 run` 子进程**（`-p use_sim_time:=True`），**M7 独立子进程**（Doer-Checker 隔离："shares no executor, no GIL, no shared data"）；外加 `sil_topic_bridge.py` / `mock_l2_publisher.py` / `fsm_aggregator_node.py` / `diagnostic_mock_publisher.py`（`sil_entrypoint.sh` Stage 3）。
- 物理为**纯 Python**：`ship_dynamics`（50Hz，RK4 `mmg_model.py:83-116`，墙钟节流发布 ~40Hz `node.py:313-318`）、`target_vessel`（10Hz，墙钟节流 ~25Hz `node.py:246-263`）。

### 2.2 时钟（已修，但有残留）
- `lifecycle_mgr.py`：`tick()` 固定增量 `sim_time += 1/tick_hz`（**已无 `*sim_rate`**，182-185）；`_clock_callback` 墙钟配速 `target_sim = wall_elapsed*sim_rate`，封顶 `_MAX_CATCHUP_TICKS=10`（213, 391-408），每 tick 发一条 `/clock`+`/sim_clock`（395-404）。`tick_hz=250`（276，docstring 1kHz 已陈旧）。clock 发射器是**唯一刻意保留**的 `create_wall_timer`（配速源，设计正确；Shell B 需替换为自由运行）。

### 2.3 Approach-1 状态（已落地）
- `src/l3_tdl_kernel/` 中 `create_wall_timer` **零命中**；M1–M8 全部 `rclcpp::create_timer(get_clock(),…)`（M2 是原范式）。`use_sim_time:=True` 启动时设好 → `get_clock()` 返回仿真钟。提交：`716b995e`(clock) / `39996028`(M1) / `a18cb51b`(M3) / `1678dd26`(M4) / `78179b7e`(M5) / `00fc525d`(M6) / `3a82c06f`(M7) / `12d392ec`(M8) / `bbd4442f`(回归测试)。

### 2.4 残留确定性风险（bit-identical 的真正障碍，排序）
1. **未播种活跃 RNG**：`target_vessel/node.py:78`（NCDM `random.gauss`）、`sensor_mock/node.py:44,66-70`、`env_disturbance:138-139`、`fault_injection`（`FAULT_TYPES:34`）——全局 `random.`、无 seed。（`imazu-01-ho` 靠 replay 目标+零环境绕过，故现有测试能过。）M2 用 `mt19937 gen(42)` 固定（`cpa_tcpa_calculator.cpp:252`）。
2. **转向链墙钟**：`sil_topic_bridge.py` LATCH 航向衰减 `_compute_latch_offset` 跑 `time.monotonic()`（450,465）、actuator 节流 `time.monotonic()`（568-572）——避碰航向指令演化依赖墙钟，**Approach-1 未覆盖**。
3. **多进程/多线程回调顺序**：分进程 M1–M8 + `MultiThreadedExecutor(8)` → 即使时钟固定，消息到达/回调交错不可复现；CycloneDDS loopback 抖动。
4. **BEST_EFFORT QoS 丢包**：`/sil/actuator_cmd`（bridge:234）、`/sil/own_ship_state`、`/sil/target_vessel_state`（physics QoS 168-173/191-196）高倍速丢包 → 每步输入随机。
5. **ship_dynamics dt 不一致**：积分用 `mmg_model.c.dt`（`mmg_model.py:94`），步数/钟进用硬编码 `0.02`（`node.py:268,288`）；若注入 `dt≠0.02` 则物理时长≠钟时长。
6. **`/clock` vs `/sim_clock` QoS 不对称**（reliable sim_clock vs best-effort-ish clock，`lifecycle_mgr.py:39-51`）。
7. **软重置后 L3 内部状态残留**：M1–M8 子进程不被生命周期重置（`lifecycle_bridge._SIL_LIFECYCLE_NODES:33-42` 不含 M1–M8），回合 N 被 N-1 污染。

### 2.5 可复用资产
- `tools/sil/batch_runner.py` + `coverage_cube.py`（11 规则 × 4 ODD × 5 扰动 × 5 seed = 1100 格，`seed_index_from_filename`），但当前 `simulate.py` 用 `fcb_sim_py` **不跑真实 M1–M8**（开环脚本避碰，几何可解性检查）——本 spec 要求改为驱动 Shell B（真栈）。
- `src/sim_workbench/fmi_bridge/`（MMG 打成 FMI 2.0 FMU + farn 扫描）。
- `tests/integration/sim_determinism/`（`capture_imazu.py`/`capture_rule14.py`/`test_determinism.py`，真栈 docker-bound 回归）。
- `src/rl_workbench/` **空**（greenfield）。

---

## 3. 设计：shared-core, dual-shell + 确定性锁步

### 3.1 核心原则
> Shell A（实时 DDS）与 Shell B（无头确定）**装载同一批真实节点二进制/源**，区别仅在「传输 + 调度 + 时钟驱动」。bit-identical 来自：单线程确定性 executor + 固定回调/消息顺序 + 固定浮点算子顺序 + 播种 RNG + 可靠/进程内传输。倍速来自：移除墙钟配速（自由运行）+ 移除 DDS 序列化（进程内）+ 原地重置（免 HTTP/重启）。

### 3.2 Shell B 执行模型（确定性锁步组）
不做「单进程吞下全部」（会违反 M7 ADR，见 §4）。改为**少量确定性执行组 + 仿真钟锁步屏障**：

```
┌─────────────────────────────────────────────────────────┐
│  Shell B 无头确定性 harness（自由运行仿真钟，无墙钟）       │
│                                                           │
│  Group-DOER（单进程, rclcpp 单线程确定 executor）          │
│    物理(共享核) → M1 M2 M3 M4 M5 M6 M8                     │
│    + Python 物理/桥 节点的确定性处理（见 §3.4）             │
│         │  仿真钟 tick N：组内按固定拓扑顺序跑完所有回调      │
│         ▼  （互斥回调组；进程内/zero-copy 传输）            │
│  ╞════ LOCKSTEP BARRIER（仿真钟 tick 边界，确定性消息交换）══╡
│         ▲                                                 │
│  Group-CHECKER（独立进程, M7）  ← ADR：不共享 executor/GIL/数据 │
│                                                           │
│  时钟驱动器：advance-to-next-event；两组都处理完 tick N      │
│  才推进到 N+1（屏障）。无 MAX_CATCHUP 封顶、无墙钟门。       │
└─────────────────────────────────────────────────────────┘
```
- **DOER 组**：RSLCPP 式——把 M1–M6+M8（rclcpp）合成进一个进程，单线程 `SingleThreadedExecutor`（或 EventsExecutor FIFO，[R7]），全部 **MutuallyExclusive 回调组**，进程内通信（intra-process / zero-copy `unique_ptr`，[R8]）消除 DDS 序列化与回调线程非确定。callback 触发顺序由仿真钟事件队列固定。
- **CHECKER 组（M7）**：保持独立进程（**ADR 硬约束**）。与 DOER 经一个**确定性 sim-clock 锁步屏障**交换消息：tick N 边界双向定序交换，时钟仅当两组都完成 tick N 才进 N+1。屏障实现需保证消息顺序确定（非 DDS 异步）。
- **时钟**：`advance-to-next-event` 自由运行（无 `wall_elapsed*sim_rate`，无 `MAX_CATCHUP_TICKS` 封顶）。Shell B 的"倍速"不再是墙钟比，而是「跑多快算多快」；1x/10x/50x 在 Shell B 等价为同一确定轨迹的不同观测采样率（这正是 bit-identical 的来源）。

### 3.3 Shell A 不变
现有 ROS2 SIL（多进程 DDS、墙钟配速时钟、HMI/WebSocket 节流发布）保持，作为认证实时基线。Approach-1 的仿真钟定时器改动已在其中。

### 3.4 Python 物理/桥 节点（开放设计点，需 writing-plans/brainstorm 定夺）
RSLCPP 仅适用 rclcpp（C++）。物理（`ship_dynamics`/`target_vessel`）与桥（`sil_topic_bridge` 等）是 rclpy（Python，GIL）。三条候选路径，**writing-plans 前需定**：
- **[TBD-physics-path] 路径 P1（推荐倾向）**：Shell B 中物理改走已存在的 C++ `fcb_sim_py`/`fcb_sim` 核（与 DOER 组同进程，纯 C++ 单线程确定）。**阻塞度：中**。关闭路径：需证明 C++ 核与 Python `mmg_model.py` 在 SIL 场景下等价（≤容差），否则 Shell A(Python)/Shell B(C++) 物理不一致破坏 G6 交叉校验。
- 路径 P2：物理保 Python，置于独立 rclpy 单线程确定 executor，与 DOER 组锁步。**阻塞度：低**，但跨语言 bit-identical 跨主机更难（浮点/库差异），且多一个锁步组。
- 路径 P3：物理 Python 经 pybind 嵌入 DOER 进程作纯函数调用（非 ROS 节点）。**阻塞度：中**，最快但需把 `ship_dynamics`/`target_vessel` 的 ROS 包装剥成纯 step 函数（`mmg_model.py` 已是纯核，目标/传感器/环境节点需同样剥离）。
- 桥节点（`sil_topic_bridge` 转向链）：无论哪条，**必须移除 `time.monotonic()` 墙钟 LATCH 调度，改仿真钟**（§2.4#2），否则转向不确定。

### 3.5 确定性硬化清单（bit-identical 必做，跨 Shell A/B）
1. **播种全部活跃 RNG**：`target_vessel`/`sensor_mock`/`env_disturbance`/`fault_injection` 弃全局 `random.`，改各持 `np.random.Generator`，由 `SeedSequence.spawn` 按 `(root_seed, episode_id, node_id, worker_id)` 派生独立流 [R4]；action space 单独 seed。seed 经场景注入/`reset(seed)` 贯穿。
2. **传输可靠化**：Shell B 内 SIL 数据话题（`/sil/own_ship_state`、`/sil/actuator_cmd`、`/sil/target_vessel_state`、env）改 RELIABLE 或进程内（消除丢包非确定）。
3. **回调顺序固定**：单线程 + MutuallyExclusive 回调组（§3.2）。
4. **浮点确定**：固定算子顺序；C++ 编译 `-fno-fast-math`（禁 `-ffast-math`），按需 `-ffp-contract=off`/`-fexcess-precision=standard`，同架构验证；RSLCPP 已证单线程定序可跨 7 CPU bit-identical [R1][R9]。
5. **修 dt 不一致**：`ship_dynamics` 钟进/步数与积分 dt 统一（§2.4#5）。
6. **回合状态清零**：Shell B `reset()` 原地清 M1–M8 内部状态/积分器/滤波器/协方差（§2.4#7），不靠重启。

### 3.6 吞吐设计
- 无墙钟门（§3.2）；进程内/zero-copy 消除 DDS（§3.2，[R8]）；原地 `reset()` 免 HTTP 生命周期（当前秒级 [R10]）；**进程级并行 MC**：N 个 Shell B 进程，各 `SeedSequence` 独立流；复用 `coverage_cube`/farn 派发。
- GIL：若物理留 Python（P2）仍受 GIL 限；P1/P3（C++ 核）解此瓶颈。

### 3.7 RL/MC 接口层
- **Gymnasium `Env`** 包装 Shell B（`reset`/`step`/`close`，5-tuple，`super().reset(seed)` 首行）[R3]；向量化 `AsyncVectorEnv`/SB3 `SubprocVecEnv` 用进程级并行。
- **MC harness**：`coverage_cube` + farn 扫描 → 派发 Shell B（真栈），聚合 CPA/DCPA/TCPA、碰撞率、COLREG 合规分。
- `src/rl_workbench/` 落地骨架。

---

## 4. ADR / 认证硬约束（不可让步）

| 约束 | 本设计如何遵守 |
|---|---|
| **ADR#2 Doer-Checker 双轨**：M7 比 Doer 简单 100×，实现路径独立，**不共享代码/库/数据结构** | M7（CHECKER 组）保持独立进程，经确定性 sim-clock 屏障通信；**不并入 DOER 进程**。任何「把 M7 合进单进程」= ADR breaking change，须独立讨论，本 spec 禁止。 |
| **ADR#4 Backseat Driver**：决策核零船型常量 | 共享核/物理参数走注入，不入决策 A 层；不引入 `if vessel==…`。 |
| **CCS i-Ship 白盒可审计** | Shell B 单线程确定事件循环比 DDS 调度更可审计；跑同一份决策逻辑。 |
| **IEC 61508 T2 工具鉴定** | Shell B 作离线验证支持工具：操作规格 + 仿真器 FMEA/HazOp + TQSK 验证套件；对接现有 `docs/Design/Phase 1/D1.5-vv-plan-scenario-qual/`。 |
| **认证证据二路径风险** | shared-core（同决策逻辑，RSLCPP 论证）+ 1x Shell B↔Shell A 抽样交叉校验（qualified-by-correlation），Shell A 仍为认证基线。 |
| **TMR≥60s / SIL2 路径** | 不改 M1 仲裁/M7/MRC 逻辑，仅改其执行容器/时钟。 |

---

## 5. 受影响文件（writing-plans 锚点）

**改（Shell B / 确定性硬化）：**
- `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py`（Shell B 自由运行时钟模式 + 移除 catchup 封顶，**保 Shell A 配速模式**，双模式）
- `src/sim_workbench/sil_nodes/{target_vessel,sensor_mock,env_disturbance,fault_injection}/.../node.py`（RNG → 播种 Generator）
- `src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/node.py`（dt 一致；Shell B 去墙钟节流）
- `docker/sil_topic_bridge.py`（转向 LATCH 去 `time.monotonic()` → 仿真钟）
- 物理/桥 Python 路径（依 §3.4 P1/P2/P3 决定改动面）
- Shell B harness（新增）：确定性合成 + 锁步屏障 + advance-to-next-event 驱动 + 原地 reset
- Gymnasium Env + MC harness（新增，复用 `coverage_cube`/farn）
- `src/rl_workbench/`（新增骨架）
- QoS：Shell B SIL 数据话题 RELIABLE/进程内
- 编译标志（colcon.meta / CMake：`-fno-fast-math` 等）

**不改：**
- Shell A 行为、HMI、Approach-1 已落地的仿真钟定时器、M2 范式、物理 RK4 数学
- 避碰逻辑 / M5 NLP / M3 航路（另行）
- M7 隔离边界（ADR）

---

## 6. 验证与完成判据（DoD）

- [ ] **bit-identical 回归**：真栈 `imazu-01-ho`（及一个含 RNG 的场景，如 NCDM 目标）Shell B 跑 ≥100 次同 seed → 逐位一致；跨主机（≥2 架构）一致。
- [ ] **容差下限门**：Shell B vs Shell A @1x，pos<1m / hdg<0.1° / behavior+conflict 序列一致（沿用 `test_determinism.py` 阈值）。
- [ ] **吞吐**：Shell B 无头 RTF ≥10x（争取 ≥50x），实测 steps/s，无墙钟封顶。
- [ ] **重置**：`reset(seed)` 原地、亚毫秒级（实测），不走 HTTP / 不重启。
- [ ] **Gym 冒烟**：`reset`/`step`/`close` + 一个随机策略跑通；向量化多进程跑通。
- [ ] **MC 复现**：同 seed 矩阵两次 MC 批 → 结果逐位一致；并行 worker 流独立。
- [ ] **Shell A 不回归**：现有单测 + 场景网关（`.preflight/gate_*.json`）+ Shell A 行为不变。
- [ ] **ADR 守门**：M7 仍独立进程；无 `if vessel==`；交叉校验通过。
- [ ] **colcon build 通过**（BuildKit cache 规范见 CLAUDE.md §12，勿删 cache mount）。

---

## 7. 建议分阶段（多阶段，无 6/15 切分；P=可并行）

- **Phase A 确定性硬化**（基础，多数可并行）：A1 播种全部 RNG（P）；A2 转向链去墙钟（P）；A3 ship_dynamics dt 统一（P）；A4 Shell B SIL 话题 RELIABLE/进程内 QoS（P）；A5 编译浮点标志。**门**：现有 `test_determinism.py` 仍绿 + 新增 RNG 复现单测。
- **Phase B 确定性合成 + M7 锁步**（关键路径，串行为主）：B1 DOER 组单进程合成（M1–M6+M8，单线程 executor，互斥回调组）；B2 M7 锁步屏障（确定性消息交换，保隔离）；B3 §3.4 物理路径落地（P1/P2/P3 决策）。**门**：DOER+CHECKER 锁步跑通 1x 与 Shell A 容差一致。
- **Phase C 无头自由运行 + 原地重置**：C1 lifecycle_mgr 双模式（Shell B advance-to-next-event）；C2 原地 `reset()`（清 M1–M8 内部态）；C3 无头模式（去 HMI/节流）。**门**：bit-identical 回归绿 + RTF≥10x。
- **Phase D MC harness**：D1 Gymnasium Env 包装；D2 coverage_cube/farn → 真栈派发；D3 进程级并行 + SeedSequence 流；D4 结果聚合。**门**：MC 复现 + 向量化跑通。
- **Phase E RL 工作台 + 认证**：E1 `rl_workbench` 骨架（Gym + SB3 冒烟）；E2 T2 工具鉴定文档 + 1x 交叉校验套件。**门**：DoD 全绿。

---

## 8. 风险与缓解

| 风险 | 缓解 |
|---|---|
| 跨语言（Py/C++）物理破坏 bit-identical/交叉校验 | §3.4 优先 P1（统一 C++ 核）；若 P2 则 Shell A 也统一 RNG/dt 并接受跨语言仅容差门 |
| M7 锁步屏障引入非确定（DDS 异步残留） | 屏障走非 DDS 确定性通道；tick 边界定序；测：M7 输入序列逐位可复现 |
| 单线程 DOER 吞吐不及 50x | 接受 RTF<50x 但确定；C++ 核 + 进程内已大幅提速；GPU 留远期非目标 |
| 阻塞回调/数据触发回调环卡死仿真钟（RSLCPP 已知坑 [R1]） | 回调预算/超时检测；CI 死锁守门 |
| 合成 M1–M8 触碰 M7 隔离（误并入） | §4 硬守门 + spec-reviewer 检查；M7 永远独立进程 |
| Shell A/B 双模式代码分叉漂移 | 同源节点 + 仅传输/调度/时钟差异；CI 双 Shell 行为对账 |
| `reset()` 漏清某节点内部态 → 回合污染 | 每节点显式 `reset_state` 钩子 + 跨回合复现测（回合 N 独立于 N-1） |

---

## 9. 参考文献

- [R1] J. Otto et al., *RSLCPP: Reproducible Single-process Lockstep for ROS 2 C++*, arXiv:2601.07052 (2026-01)。单进程合成 + 单线程 FIFO 离散事件 sim-clock，100 运行 × 7 CPU bit-identical，未改 rclcpp 节点。🟢 A（n=1 首发论文，bit-identical 声称 🟡 待自复现）
- [R1b] J. Otto, MSc thesis *Enabling Reproducibility in ROS 2 by Ensuring Sequence Deterministic Callback Execution* (jonasotto.com)。🟢 A
- [R2] MIT MOOS-IvP uSimMarineV22 docs（200–300x time-warp，speed 先例非确定）。🟢 A speed
- [R3] Gymnasium (Farama) Env/Vector API docs。🟢 A
- [R4] NumPy `SeedSequence.spawn` 并行可复现 RNG docs。🟢 A
- [R5] IEC 61508-3 §7.4.4 T2 离线支持工具鉴定。🟢 A
- [R6] DNV-CG-0264 / OSP(libcosim) FMI 2.0 固定步主算法（海事共仿确定性先例）。🟢 A
- [R7] ROS2 Executors 概念文档（SingleThreaded 确定 / MultiThreaded 非确定 / EventsExecutor FIFO）。🟢 A
- [R8] ROS2 Composition / Intra-Process Comm / zero-copy docs。🟢 A
- [R9] ROS2 design `clock_and_time.md`（仿真钟大跳只触发一次 → 细增量每 tick 一条 /clock）。🟢 A
- [R10] ROS2 逐回合重启图为已知反模式（DDS discovery 风暴，秒级）；RSLCPP/社区。🟡 B
- [R11] 项目内：Approach-1 spec `2026-05-29-sim-speed-determinism-design.md`；记忆 `project_sim_speed_nondeterminism`。
- 注：本 spec 结论的「机制」均 A/B 源；「合成到本项目架构」为基于源 + 代码的推理（🟡 中高），最强经验锚 RSLCPP 为单篇首发，bit-identical 跨主机声称待本项目自复现验证。
