# D1.3.1 仿真器鉴定报告 — 03 · 确定性重放报告

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-D1.3.1-03 |
| 版本 | v0.1-draft |
| 编制日期 | 2026-05-15 |
| 状态 | 🟡 Projected — 重放工具链就绪；ROS2 Lifecycle 自动化待 D1.3b.3 交付（6/15） |
| 上游 | D1.3b.3 ROS2 Lifecycle 集成 · D1.3a MMG 模型 · D1.3c FMI 桥 |
| 下游 | P2 证明输出 → 06-evidence-matrix.md → CCS AIP C5.2 |
| 关键依赖 | `tools/check_determinism.py` · `sil_lifecycle` 节点 · 固定 seed 机制 · God tracker |

---

## §1 证明目标（P2）

**P2 — 确定性重放证明**：在相同的场景定义 + 相同的随机 seed + 相同的初始条件下，SIL 仿真器反复运行同一场景，自船轨迹的时间偏差、首向偏差和位置偏差均在规定阈值内。

| 指标 | 符号 | 阈值 | 判定逻辑 |
|---|---|---|---|
| 时间偏移 | \|Δt_shift\| | **< ±0.1s** | 连续 1300 次运行中任意两次轨迹曲线的最差时间对齐偏移 |
| 最大首向偏差 | max\|δ_ψ\| | **< ±0.5°** | 时间对齐后，所有运行在所有时间采样点的首向角绝对差最大值 |
| 最大位置偏差 | max\|δ_pos\| | **< ±0.5m** | 时间对齐后，所有运行在所有时间采样点的 2D 位置绝对差最大值 |

---

## §2 方法论

### 2.1 弱确定性定义

本 SIL 系统的确定性定义为**弱确定性**（Weak Determinism）：

> 场景 YAML + seed + 初始状态（包括 `t_start` 时间戳）三者全部相同 → **相同时间步长的自船运动学输出在数值上逐位一致**。

弱确定性与"强确定性"（Strong Determinism：所有内部变量、所有节点调度顺序、所有消息序列在每次运行时完全一致）的区别：

| 维度 | 弱确定性（本 SIL 目标） | 强确定性 |
|---|---|---|
| 数学计算 | 逐位一致（bit-identical） | 逐位一致 |
| 消息到达顺序 | 允许 DDS 重排（只要不会造成浮点累积误差） | 完全一致 |
| Lifecycle 迁移耗时 | 允许 ±5ms 抖动 | 完全一致 |
| 容器/OS 调度 | 不约束 | 完全约束（RTOS 级） |

**弱确定性选择的理由** 🟢 High：ROS2 Humble + DDS Fast-RTPS 默认使用 UDP 多播，不保证消息全局排序 [R-NLM:16]。强确定性在同节点多 publisher 场景（sensor_mock_node 同时发布 AIS + Radar topic）下原理上不可达，除非替换为单线程确定性调度器 [R-NLM:19]。弱确定性已足够支撑 V&V 证据要求 —— V&V 关心的是"同一场景跑两遍，两遍的结论是否相同"（即自船轨迹在图表上完全重合），不关心内部 DDS 包的精确到达 order。

### 2.2 重放协议

```
For each scenario S:
  1. 加载 S.yaml → 设置 seed = FIXED_SEED[group]
  2. 启动 lifecycle_mgr → UNCONFIGURED → INACTIVE → ACTIVE
  3. 运行至 t_end
  4. 录制 MCAP bag[run_id]
  5. 停止 lifecycle_mgr → INACTIVE → UNCONFIGURED
  6. 重复步骤 2–5 共 N 次（N = 组内运行次数）
  7. 提取自船轨迹 {time, x, y, psi, u, v, r}[run_id]
  8. 以 run_0 为参考轨迹，计算 run_i (i=1..N-1) 的对齐偏差
```

### 2.3 差异度量公式

**时间对齐**：以轨迹起始时间（第一个 `FilteredOwnShipState.header.stamp`）作为 t=0 参考点。所有运行对齐到同一 t₀。

**首向偏差**：

```
δ_ψ(t) = |ψ_run_i(t) − ψ_run_0(t)|  （mod 360°，取最短弧）
max|δ_ψ| = max_{i∈1..N-1, t∈[0, t_end]} δ_ψ(t)
```

**位置偏差**：

```
δ_pos(t) = sqrt( (x_run_i(t) − x_run_0(t))² + (y_run_i(t) − y_run_0(t))² )
max|δ_pos| = max_{i∈1..N-1, t∈[0, t_end]} δ_pos(t)
```

**时间偏移**：

```
Δt_shift = argmin_τ ∫ [ψ_run_i(t+τ) − ψ_run_0(t)]² dt,  τ ∈ [−1s, +1s]
```

> 时间偏移指标用于检测系统性相位滞后（例如 FMU co-simulation step mismatch 积累）。若仿真器无系统性延迟，`Δt_shift` 应在数值精度范围内接近于零（< ±10⁻³ s）。

---

## §3 测试设计

### 3.1 4 组共 1300 次运行

| 组编号 | 场景 | 遭遇类型 | 运行次数 N | 固定 seed | 场景时长 (s) | 目标船舶数 | 总面积 (km²) |
|---|---|---|---|---|---|---|---|
| **G1** | head_on | Head-on (Imazu-01) | **1000** | 12345 | 300 | 2 | 5×5 |
| **G2** | imazu_01 | Head-on (Imazu-01 变体) | **100** | 23456 | 300 | 2 | 5×5 |
| **G3** | colreg_rule14 | Crossing give-way (COLREG Rule 14) | **100** | 34567 | 300 | 2 | 5×5 |
| **G4** | imazu_03 | Multi-vessel crossing (Imazu-03) | **100** | 45678 | 300 | 3+ | 5×5 |
| **合计** | — | — | **1300** | — | — | — | — |

**组选择理由**：

- **G1 (N=1000)**：Head-on 是最高频 COLREGs 遭遇类型，1000 次满足统计学显著性（α=0.01 下 Type I error ≤ 1%）
- **G2 (N=100)**：变体验证，确认重放结果与特定 YAML 参数无关
- **G3 (N=100)**：不同遭遇几何（crossing），确认重放与遭遇类型无关
- **G4 (N=100)**：多船场景，确认目标船舶数量不引入非确定性

### 3.2 环境控制

为隔离"仿真器本身非确定性"与"环境噪声非确定性"，**P2 测试在以下严格控制下进行**：

| 控制项 | 设定 | 理由 |
|---|---|---|
| **环境扰动节点** | **关闭**（零风/零流/零浪） | 消除 Gauss-Markov 随机过程的时序不确定性 |
| **故障注入节点** | **关闭**（零故障注入） | 消除异步故障事件的时序不确定性 |
| **Tracker** | **God tracker**（真值直接注入） | 消除 KF 随机初始化和过程噪声 |
| **Sensor mock** | **理想模式**（零噪声、零虚警、P_d=1.0） | 消除传感器随机采样偏差 |
| **目标船行为** | 固定轨迹（NCDM 确定性） | 消除 COLREGs 规避动作的路径分岔 |
| **随机 seed** | **固定**（每组不同，组内相同） | 消除任何 `std::mt19937` 或 `numpy.random` 的不确定性 |
| **积分步长** | dt=0.02s（固定，不依赖 wall clock） | 消除 wall-time 触发的自适应步长 |
| **ROS2 executor** | 单线程 `SingleThreadedExecutor` | 消除多线程并发调度不确定性 |
| **Docker 资源** | `--cpus=4 --memory=8g` 固定 | 消除宿主机资源竞争差异 |

### 3.3 执行环境锁定

| 条件 | 值 |
|---|---|
| ROS2 版本 | Humble Hawksbill（Ubuntu 22.04） |
| DDS 实现 | Fast-RTPS（默认，ROS2 Humble 自带） |
| MMG 模型 commit | D1.3a 交付 tag（TBD after 5/31） |
| libcosim 版本 | v0.10.x（固定 git tag） |
| CPU 架构 | x86_64（CI runner 固定） |
| 浮点模式 | IEEE 754 默认（不启用 FMA/FTZ/DAZ 优化） |

---

## §4 结果（🟡 Projected — 数据待 D1.3b.3 交付）

### 4.1 G1 Head-on 1000 次

| 指标 | 阈值 | 实测值 | 通过？ |
|---|---|---|---|
| max\|δ_pos\| | < ±0.5m | 🟡 待填入 | 🟡 待判定 |
| max\|δ_ψ\| | < ±0.5° | 🟡 待填入 | 🟡 待判定 |
| \|Δt_shift\| | < ±0.1s | 🟡 待填入 | 🟡 待判定 |
| 失败次数 / 1000 | 0 | 🟡 待填入 | 🟡 待判定 |

> **🟡 Projected** — 当前 ship_dynamics 为 kinematic-only Python stub（`fcb_simulator` 包）。Python 单线程 + 固定 seed 的确定性预期为 🟢 High（逐位一致）。完整 C++ MMG + ROS2 Fast-RTPS DDS 后的 G1 测试须在 D1.3b.3 ROS2 lifecycle 自动化就绪后执行。

### 4.2 G2–G4 汇总

| 组 | 场景 | N | max\|δ_pos\| | max\|δ_ψ\| | \|Δt_shift\| | 通过？ |
|---|---|---|---|---|---|---|---|
| G2 | imazu_01 | 100 | 🟡 待填入 | 🟡 待填入 | 🟡 待填入 | 🟡 待判定 |
| G3 | colreg_rule14 | 100 | 🟡 待填入 | 🟡 待填入 | 🟡 待填入 | 🟡 待判定 |
| G4 | imazu_03 | 100 | 🟡 待填入 | 🟡 待填入 | 🟡 待填入 | 🟡 待判定 |

> **🟡 Projected** — 全部数据待 D1.3b.3 交付后执行。

### 4.3 轨迹对比图（模板）

> **🟡 Projected** — D1.3b.3 交付后，对 G1 组 1000 次运行取 run_0 / run_100 / run_500 / run_999 四条轨迹，叠加绘制在 `annex/plots/G1_trajectory_overlay.svg`。预期：四条曲线**肉眼不可分辨**（单线宽度内完全重合）。

---

## §5 劣化分析树（Degradation Analysis Tree）

若 P2 非 100% 通过，按下述优先级排查根因：

### 根因 1（优先级最高）：DDS 消息重排导致浮点累积差异

**现象**：单次运行的轨迹无误，但重放间的偏差随时间增长（累积误差）。

**机理**：ROS2 Humble Fast-RTPS 默认 UDP 不保证多 topic 间消息顺序。sensor_mock_node 同时发布 ~3 个 topic，若不同重放中消息到达 M2 World Model 的顺序不同，M2 的处理顺序不同，可能导致舍入误差累积。在单线程 executor 中此风险低，但仍需验证。

**排查方法**：
1. 在 M2 World Model 订阅端记录消息到达序列号 + 到达时间戳
2. 对比不同重放间的序列号排列是否一致
3. 若不一致：切换为 `rmw_cyclonedds_cpp`（Cyclone DDS 在单线程下消息排序更确定 [R-NLM:17]）

**修复方法**：
- 短期：强制 `--rmw-implementation rmw_cyclonedds_cpp`
- 长期：D1.3b.3 起在 `sil_lifecycle` 中增加"全节点 start 后再 topic"的 barrier 同步

### 根因 2：MMG 积分器 seed 透传失败

**现象**：G1 1000 次中的偏差在首次步就出现（Δt=0.02s 即非零）。

**机理**：MMG 模型在无环境扰动时理应是纯确定性微分方程。若存在隐式随机源（螺旋桨转速噪声模型、GPS 抖动模拟），seed 固定不生效。

**排查方法**：
1. 检查 `fcb_simulator_node` 的所有 `std::mt19937` / `numpy.random.RandomState` 初始化点
2. 确认是否全部通过 YAML `seed` 字段传入
3. 增加 `assert(diff_u_step1 == 0.0)` 单元测试

**修复方法**：补全所有随机源的 seed 透传。

### 根因 3：libcosim FMU step 非确定

**现象**：启用 FMI bridge（D1.3c）后偏差增大，禁用后偏差消失。

**机理**：libcosim v0.10.x 的 co-simulation master algorithm 在步长协商时可能使用 wall-clock 或 OS timer，导致不同运行的 step 序列微小时序差异 [R-NLM:25]。

**排查方法**：
1. 提取 libcosim observer trace 中每个 FMU 的 `doStep(t, dt)` 时间戳序列
2. 对比两次重放间的 step 序列是否逐项一致
3. 若不一致：检查 libcosim 是否配置为 `fixed-step` 模式（非 `variable-step`）

**修复方法**：D1.3c 强制 libcosim `fixed-step-master` + 禁用 wall-clock synchronization。

### 根因 4：浮点非结合性（FMA / SIMD）

**现象**：偏差在运行中后期出现，量级为 10⁻⁶~10⁻⁴，不随时间增长。

**机理**：不同运行中 CPU 的 SIMD 指令路径（AVX2 vs SSE）或 FMA（Fused Multiply-Add）启用状态不同，导致 `(a×b)+c` 与 `FMA(a,b,c)` 的舍入差异。在 IEEE 754 默认模式下此差异存在但极小。

**排查方法**：在 Docker 容器中固定编译选项 `-mno-fma -mno-avx2 -msse4.2` 后重跑。

**修复方法**：
- 若偏差 ≤ 10⁻³ m / 10⁻³ °：标记为"浮点噪声"→ 不影响 ≤ P2 判定（阈值 0.5m/0.5°）
- 若偏差 > 10⁻² m / 10⁻² °：CI 编译选项中增加 `-ffp-contract=off -mno-fma`

---

## §6 验收判定（Acceptance Checkboxes）

| # | 检查项 | 自动化工具 | 状态 | 依赖 |
|---|---|---|---|---|
| AC-P2-1 | G1 1000 次 Head-on max\|δ_pos\| < ±0.5m | `tools/check_determinism.py --group G1` | 🟡 待 D1.3b.3 | D1.3a + D1.3b.3 |
| AC-P2-2 | G1 1000 次 max\|δ_ψ\| < ±0.5° | 同上 | 🟡 待 D1.3b.3 | 同上 |
| AC-P2-3 | G1 1000 次 \|Δt_shift\| < ±0.1s | 同上 | 🟡 待 D1.3b.3 | 同上 |
| AC-P2-4 | G2 100 次全通过 | `tools/check_determinism.py --group G2` | 🟡 待 D1.3b.3 | 同上 |
| AC-P2-5 | G3 100 次全通过 | `tools/check_determinism.py --group G3` | 🟡 待 D1.3b.3 | 同上 |
| AC-P2-6 | G4 100 次全通过（多船） | `tools/check_determinism.py --group G4` | 🟡 待 D1.3b.3 | 同上 |
| AC-P2-7 | 轨迹对比图肉眼不可分辨 | `tools/plot_trajectory_overlay.py` | 🟡 待 D1.3b.3 | AC-P2-1 至 6 |
| AC-P2-8 | 自动化脚本 CI 集成 | `.gitlab-ci.yml` job `determinism_check` | 🟡 待 D1.3b.3 | AC-P2-1 至 7 |

---

## §7 参考文献

| 编号 | 来源 | 描述 |
|---|---|---|
| [R-NLM:16] | NLM Deep Research 2026-05-12 silhil_platform | ROS2 Design — Managed Nodes (Lifecycle) and DDS determinism considerations |
| [R-NLM:17] | 同上 | Cyclone DDS vs Fast-RTPS determinism comparison in single-threaded executor |
| [R-NLM:19] | 同上 | Deterministic scheduling in ROS2 — `rclcpp::executors::SingleThreadedExecutor` guarantees |
| [R-NLM:25] | 同上 | OSP libcosim v0.10.x — fixed-step vs variable-step master algorithm determinism |
| [R-NLM:45] | 同上 | FMI 2.0 co-simulation determinism requirements (SSP standard) |
| [W-D1.3.1-4] | 本文档 §2.3 | 差异度量公式（时间对齐、首向偏差、位置偏差、时间偏移） |
| [W-D1.3.1-5] | 本文档 §3.2 | 环境控制矩阵（9 项零扰动约束） |
| [W-D1.3.1-6] | 本文档 §5 | 劣化分析树（4 根因 + 排查 + 修复路径） |

---

*文档版本 v0.1-draft · 2026-05-15 · 🟡 Projected — 所有 §4 结果数据待 D1.3b.3 ROS2 Lifecycle 自动化交付（6/15）后填充。*
