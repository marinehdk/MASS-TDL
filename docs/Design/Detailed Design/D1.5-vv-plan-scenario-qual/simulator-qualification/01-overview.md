# D1.3.1 仿真器鉴定报告 — 01 · 范围与验证策略

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-D1.3.1-01 |
| 版本 | v0.1-draft |
| 编制日期 | 2026-05-15 |
| 状态 | 在制（6/15 DEMO-1 前完成）|
| 上游 | SIL v1.0 统一设计套件 Doc 4 §11；V&V Plan v0.1 §8；8 月计划 v3.0 D1.3.1 |
| 下游 | 02-model-fidelity-report.md / 03-determinism-replay.md / 04-sensor-confidence.md / 05-orchestration-trace.md / 06-evidence-matrix.md / 07-ccs-mapping.md |
| 置信度基线 | 🟡 Medium — 验证策略已锁定，MMG 实测数据待 D1.3a 交付（5/31）后填充 |

---

## §1 鉴定目的

### 1.1 一句话定位

本文档及其附属报告（02–07）构成 **SIL 仿真器合格验证工具（qualified verification tool）鉴定包**，目标是在 CCS i-Ship（**Nx, Ri/Ai**）AIP 提交（D4.4，2026-11）前，证明仿真器输出的证据可用于：

1. CCS《智能船舶规范 2024》§9.1 **性能验证** 的模拟证据源
2. DNV-RP-0513 §4（模型保证）对仿真工具链的鉴定要求
3. IEC 61508-3 §7.4.4 对"已验证软件工具"的置信度论证

### 1.2 鉴定不是"认证"

仿真器本身不需要取得型式认可（Type Approval）。鉴定完成产生的法定产物是：

- **Evidence Pack**：4 项证明（P1–P4）的实测数据 + 分析报告，由 06-evidence-matrix.md 汇编
- **Tool Qualification Statement**：V&V 工程师签署的"验证工具合格声明"，入 C9 位证据
- **CCS surveyor 审阅记录**：07-ccs-mapping.md 逐条映射 + 沟通时间表（annex/）

### 1.3 适用边界

本鉴定仅覆盖 **D1.3.1 范围**内的仿真器组件（见 §2）。L3 kernel（M1–M8）的正确性由独立 V&V 策略覆盖（见 V&V Plan v0.1 §3–§4）；SIL 鉴定只证明**仿真器的行为足够精确、可重复、可审计**，以支撑 L3 kernel 的验证结论不被工具误差污染。

---

## §2 范围边界

### 2.1 鉴定包含（Qualified Scope）

| 组件 | 路径 / 包 | 鉴定要求 | 关键证明 |
|---|---|---|---|
| **Ship Dynamics** | `src/sim_workbench/ship_dynamics_node`（D1.3a 交付） | MMG 4-DOF 保真度 RMS ≤ 5%（P1） | 02-model-fidelity-report.md |
| **Sensor Mock** | `src/sim_workbench/sensor_mock_node` | 噪声/虚警/退化 vs CG-0264 §6 限值（P3） | 04-sensor-confidence.md |
| **Target Vessel** | `src/sim_workbench/target_vessel_node` | AIS 重放一致性 + NCDM 轨迹确定性 | 02 + 03 |
| **Tracker Mock** | `src/sim_workbench/tracker_mock_node` | God tracker 真值污染 ≤ 0（零偏差确认） | 03-determinism-replay.md |
| **Environment Disturbance** | `src/sim_workbench/env_disturbance_node` | Gauss-Markov 模型统计检验（均值/方差/自相关） | 03-determinism-replay.md |
| **libcosim Orchestration** | `src/sim_workbench/fmi_bridge`（D1.3c 交付） | FMU 时间步链可追溯（P4） | 05-orchestration-trace.md |
| **Scenario Authoring** | `src/sim_workbench/scenario_authoring` | YAML → ROS2 参数注入一致性 | 05-orchestration-trace.md |
| **Lifecycle Manager** | `src/sim_workbench/sil_lifecycle` | 状态迁移可靠（UNCONFIGURED→ACTIVE→INACTIVE 100% 成功） | 05 |

### 2.2 明确不鉴定（NOT Qualified）— 附理由

| 组件 | 范围外理由 |
|---|---|
| **L3 Kernel (M1–M8)** | 测试目标，非测试工具。正确性由独立 V&V 证明（V&V Plan v0.1 §4–§6）。SIL 仅作为其执行环境。 |
| **Foxglove Bridge** | Foxglove 为第三方 COTS 可视化工具。鉴定职责属供应商（Foxglove Studio / foxglove_bridge）。SIL 仅依赖其 topic relay 功能，不作数据变换。 |
| **Web HMI** | 前端 React/Zustand/MapLibre 为操作界面，不参与仿真计算。HMI 正确性由 D1.3b 独立验收。 |
| **FastAPI Orchestrator** | 控制面 REST API 仅编排生命周期 + 文件 I/O，不改变仿真时域数据。其正确性由集成测试覆盖（Doc 4），不作独立鉴定。 |
| **rosbag2 / MCAP Recorder** | 第三方工具（rosbag2）。鉴定职责属 ROS2 发行版质量保证。SIL 仅验证"录制内容 == 仿真产出的逐位匹配"（05 §附录）。 |
| **Docker / CI Pipeline** | 基础设施，不属于仿真器内核。合规性由 D1.4 编码标准 + CI gate 独立保证。 |

### 2.3 边界原理

边界划分依据：**"变换仿真数据的组件 → 须鉴定；仅透传/编排/展示的组件 → 不鉴定"**。此原则来自 DNV-RP-0513 §4.2.2（工具链中的"数据流关键路径"定义）🟡（DNV 全文未采购，GAP-032，见 §7 引用注释）。

---

## §3 验证策略

### 3.1 4 项核心证明（Core Proofs）

| 编号 | 证明名称 | 量化阈值 | 对应报告 | DoD 判据 | 置信度 |
|---|---|---|---|---|---|
| **P1** | MMG 模型保真度 | RMS 误差 ≤ 5%（3 种参考机动） | 02 | 3/3 机动通过；`colcon test` 断言 | 🟡 数据待 D1.3a |
| **P2** | 确定性重放 | `\|Δt_shift\| < ±0.1s` ∧ `max\|δ_ψ\| < ±0.5°` ∧ `max\|δ_pos\| < ±0.5m` | 03 | 1300 次运行 100% 通过 | 🟡 数据待 D1.3b.3 |
| **P3** | 传感器置信度 | 退化包络 ≤ CG-0264 §6 表 6-1/6-2 限值 | 04 | 9 种退化模式全在限值内 | ⚫ DNV 全文未采购 |
| **P4** | 编排可验证性 | libcosim observer trace 完整；步长链条无断点 | 05 | 全场景 trace 连续；FMU event 记录无 gap | 🟡 数据待 D1.3c |

### 3.2 数据可用性分级

| 层级 | 符号 | 含义 | 示例 |
|---|---|---|---|
| **Live Data** | 🟢 | 从运行系统实际采集 | 当前 ship_dynamics kinematic stub 的实时话题数据 |
| **Projected** | 🟡 | 方法论文档就绪，数据占位，待上游交付后填充 | 完整 MMG 4-DOF 保真度对比数据 |
| **Blocked** | 🔴 | 上游依赖或外部采购未完成，数据不可用 | DNV OSP `ospx` 尚未集成（D1.3c 阻塞） |
| **Blocked Pending** | ⚫ | 规范条款引用存在，但全文未采购 | DNV-RP-0513 / CG-0264 精确条款号（GAP-032） |

### 3.3 3 种解析参考解

| 机动 | 公式来源 | 关键参数（FCB 船型） | 阈值 |
|---|---|---|---|
| **直线停船** | Nomoto 一阶纵荡：`T_u · u̇ + u = 0`（推进关闭） | `T_u ≈ 12.5s`，理论停船距离 `S_stop ≈ 115.8m`（初速 9.26 m/s = 18 kn） | 实测 vs 数值参考解（dt=0.001s）偏差 ≤ 5%（约 ±5.8m） |
| **35° 满舵定常旋回** | Nomoto 一阶转首：`T · ṙ + r = K · δ`（定常解） | `K ≈ 0.085 s⁻¹`，`T ≈ 2.8s`，战术直径 `D_T ≈ 835m`，定常漂角 `β ≈ 8.5°`（δ=35°） | 实测 `D_T` vs 公式 ≤ 5%（约 ±42m） |
| **Zigzag 10°/10°** | Kempf 超调模型：一阶超调角 `OSA1 ≈ 3.2°`，一阶到达时间 `t_a ≈ 12s` | 同上 Nomoto `K, T`（δ=10° 操舵） | `OSA1` ± 1°，`t_a` ± 0.6s |

> **注**：Nomoto 参数 `K, T` 为简化线性估计值，实际值由 MMG 仿真器在 D1.3a 交付后从模拟旋回试验中提取（见 02-model-fidelity-report.md §3.1）。上表参数引用 Yasukawa 2015 [R-NLM:30] 表 3a 的 H 型船数据按 FCB 尺度缩放 [W-D1.3.1-1]。

### 3.4 场景覆盖矩阵（7 遭遇类型 × 4 配置维度）

| 遭遇类型 | P1 保真度 | P2 确定性 | P3 传感器 | P4 编排 | Imazu 编号 | YAML 文件名 |
|---|---|---|---|---|---|---|
| **Head-on** | ✅ | ✅ (1000 次) | ✅ | ✅ | Imazu-01 | `head_on.yaml` |
| **Crossing (give-way)** | ✅ | ✅ | ✅ (Radar 退化) | ✅ | Imazu-03 | `crossing_give_way.yaml` |
| **Crossing (stand-on)** | ✅ | ✅ (100 次 COLREG-14) | ✅ | ✅ | Imazu-02 | `crossing_stand_on.yaml` |
| **Overtaking** | ✅ | ✅ | ✅ (AIS 失联) | ✅ | Imazu-04 | `overtaking.yaml` |
| **Multi-vessel (3+)** | ✅ | ✅ (100 次 Imazu-03) | ✅ | ✅ | Imazu-05 | `multi_vessel.yaml` |
| **Restricted waters** | ✅ | — | ✅ (Clutter) | ✅ | Imazu-06 | `restricted_waters.yaml` |
| **Environmental (wind/current)** | ✅ | — | ✅ (GNSS 降级) | ✅ | Imazu-07 | `environmental.yaml` |

> 当前 35 YAML 场景均基于 NTNU colav-simulator schema（GAP-003），DNV `maritime-schema` 迁移待 D1.3b.3+ 执行。上表 YAML 文件名指向迁移后的目标命名 [W-D1.3.1-2]。

### 3.5 工具链版本锁定

| 工具 / 库 | 锁定版本 | 用途 | 锁定理由 | 置信度 |
|---|---|---|---|---|
| **ROS2** | **Humble Hawksbill** | 仿真运行时 | 决策记录 2026-05-09 §3 🟢；Jazzy 引用均为 GAP-001 | 🟢 |
| **OS** | Ubuntu 22.04 + PREEMPT_RT | 宿主机 + CI | 同上；PREEMPT_RT 为实船 AGX 一致性 | 🟢 |
| **maritime-schema** | `dnv-opensource/maritime-schema` **v0.2.x** | 场景 / 输出 schema | 决策记录 2026-05-09 §2 | 🟢 |
| **libcosim** | `open-simulation-platform/libcosim` **v0.10.x** | FMU 协仿真编排 | 同上；v0.11+ 须经 D1.3c 升级验证 | 🟢 |
| **farn** | `dnv-opensource/farn` **v0.9.x** | 后处理（CPA/碰撞风险） | 同上 | 🟢 |
| **FMI** | **FMI 2.0**（非 3.0） | FMU 接口 | D1.3c 以 FMI 2.0 为目标；FMI 3.0 尚未在 OSP 生态成熟 [R-NLM:45] | 🟡 |
| **Python** | **3.10** | Orchestrator + 脚本 | Ubuntu 22.04 默认；3.11+ 不允许（README 锁定） | 🟢 |
| **colcon** | **0.20.1** | 构建系统 | CI image 锁定 | 🟢 |

---

## §4 验收准则

| ID | 准则 | 量化阈值 | 判定方法 | 依赖 |
|---|---|---|---|---|
| **AC-1** | MMG 保真度 | 3 种机动 RMS ≤ 5% | `colcon test --packages-select fcb_simulator` + 02 报告 | D1.3a (5/31) |
| **AC-2** | 确定性重放 | 1300 次运行 100% 通过 ±0.1s / ±0.5° | 03 报告自动化脚本 `tools/check_determinism.py` | D1.3b.3 (6/15) |
| **AC-3** | 传感器退化包络 | 9 种退化模式在 CG-0264 §6 限值内 | 04 报告 | ⚫ 全文未采购 |
| **AC-4** | 编排可追溯性 | libcosim trace 连续无断点 | 05 报告 + `tools/check_orchestration_trace.py` | D1.3c (5/31) |
| **AC-5** | 证据链完整性 | 06-matrix 全部条款有覆盖声明 | 人工审阅（V&V 工程师） | AC-1 至 4 |
| **AC-6** | CCS 映射完备性 | 07-mapping 覆盖 CG-0264 §9.1 全部子条款 | CCS surveyor 审阅 | AC-5 |

---

## §5 风险登记

| ID | 风险 | 概率 | 影响 | 缓解措施 | 状态 |
|---|---|---|---|---|---|
| **R1** | MMG 参数偏差 > 5%（FCB 无水池试验数据） | 🟡 中 | 🔴 高 — P1 不合格 | D1.3a 提供数值参考解（dt=0.001s）替代水池；备选 CFD 路径（02 §4.3） | 🟡 监控 |
| **R2** | 确定性不足（ROS2 DDS 非确定性路由） | 🟡 中 | 🟡 中 — P2 失败重跑 | 弱确定性定义（03 §2.1）；固定 seed + 零扰动 + God tracker；A/B 差分分析 | 🟡 监控 |
| **R3** | DNV 规范全文未采购 | 🔴 高 | 🟡 中 — 条款号不确定 | 引用公开摘要（如 `maritime-schema` README）+ 标注 ⚫；CCS surveyor 可接受"等效论证"路径 | 🔴 待处理 |
| **R4** | D1.3b.3 ROS2 lifecycle 延迟 | 🟡 中 | 🟡 中 — P2 自动化推迟 | P2 可先以手动重放 100 次验证；自动化排 D1.3b.3 完成后执行 | 🟡 监控 |
| **R5** | libcosim v0.10.x API breaking change | 🔴 低 | 🔴 高 — P4 阻断 | D1.3c 锁定 libcosim git tag；CI 构建任务每日检查兼容性 | 🟢 已缓解 |

---

## §6 CCS AIP 证据链定位

CCS AIP 提交包（D4.4）中，本鉴定报告的证据链位置如下：

```
C4  Simulation Environment Description
  └── C4.1  Simulation tool chain architecture  ← 本文档 §2–§3 + 01-sil-architecture.md (Doc 1)
  └── C4.2  Vessel model description            ← 02-model-fidelity-report.md §2
  └── C4.3  Sensor model description            ← 04-sensor-confidence.md
  └── C4.4  Environment model description       ← 03-determinism-replay.md §3.1

C5  Model Validation Evidence
  └── C5.1  MMG fidelity vs reference           ← 02-model-fidelity-report.md §3–§5
  └── C5.2  Determinism replay evidence         ← 03-determinism-replay.md §4

C6  Tool Qualification Statement
  └── C6.1  4-proof summary                     ← 06-evidence-matrix.md
  └── C6.2  DNV-RP-0513 clause mapping          ← 06-evidence-matrix.md §2

C7  Simulation Test Results
  └── C7.1  Scenario run logs                   ← annex/test-results/
  └── C7.2  Coverage matrix                     ← 本文档 §3.4

C8  CCS Rule Compliance Mapping
  └── C8.1  CCS §9.1 sub-clause mapping         ← 07-ccs-mapping.md

C9  Tool Qualification Declaration (signed)
  └── C9.1  V&V Engineer sign-off               ← annex/ 签署页（6/15 DEMO-1 前）
```

---

## §7 参考文献

| 编号 | 来源 | 描述 |
|---|---|---|
| [R-NLM:5] | NLM Deep Research 2026-05-12 silhil_platform | DNV-RP-0513 framework for simulation model assurance |
| [R-NLM:8] | 同上 | IEC 61508-3 §7.4.4 requirements for software tool qualification |
| [R-NLM:13] | 同上 | ROS2 Humble Composition node zero-copy IPC benchmark |
| [R-NLM:14] | 同上 | Nav2 ROS2 Lifecycle-based architecture reference |
| [R-NLM:16] | 同上 | ROS2 Design — Managed Nodes (Lifecycle) |
| [R-NLM:17] | 同上 | ROS2 Lifecycle Node C++ API documentation |
| [R-NLM:18] | 同上 | DMV-CG-0264 Section 3 — Verification Plan structure |
| [R-NLM:20] | 同上 | Foxglove lifecycle management blog |
| [R-NLM:22] | 同上 | DNV Open Source maritime-schema v0.2.x |
| [R-NLM:23] | 同上 | rosbag2 MCAP storage plugin 0.15.15 |
| [R-NLM:25] | 同上 | OSP libcosim v0.10.x documentation |
| [R-NLM:26] | 同上 | OSP farn v0.9.x documentation |
| [R-NLM:30] | 同上 | Yasukawa 2015 — MMG standard model 4-DOF derivation |
| [R-NLM:44] | 同上 | OSP Maritime Toolbox — FMI 2.0 co-simulation patterns |
| [R-NLM:45] | 同上 | FMI 3.0 adoption status in marine simulation (2025 landscape) |
| [R-NLM:47] | 同上 | Kongsberg K-Sim product architecture (non-developer) |
| [E4] | 决策记录 2026-05-09 | RL 三层隔离边界 |
| [W-D1.3.1-1] | 本文档内部计算 | FCB Nomoto 参数缩放估算（基于 Yasukawa 2015 H 型船数据） |
| [W-D1.3.1-2] | 本文档内部约定 | D1.3b.3 maritime-schema 迁移后目标 YAML 命名 |
| [W1] | SIL 套件 Doc 1 §2 | ROS2 Humble Composition + Foxglove lifecycle |
| [GAP-032] | 全局 gap | DNV-RP-0513 / CG-0264 全文未采购，条款引用 ⚫ pending |

---

*文档版本 v0.1-draft · 2026-05-15 · V&V 工程师（marinehdk）编制。*
