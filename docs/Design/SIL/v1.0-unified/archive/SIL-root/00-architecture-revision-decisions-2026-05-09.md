# SIL 架构修订决策记录（v1.1.3-pre-stub）

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-DR-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-09 |
| 状态 | 决策已确认（用户拍板 2026-05-09），驱动架构 v1.1.3-pre-stub patch + 计划 v3.1 修订 |
| 触发 | 7 角度评审（2026-05-07）+ 2 次 SIL 深度研究（指导书 2026-05-09）+ 3 次 Deep Research（NLM silhil_platform 笔记本，46→98 sources） |
| 适用范围 | 架构报告 v1.1.2 → v1.1.3-pre-stub 增量修订；开发计划 v3.0 → v3.1 增量修订 |
| 后续节点 | D2.8（7/31）合入 v1.1.3 stub 完整 §SIL/§RL/§Scoring 章节；D3.8（8/31）v1.1.3 完整化 |

---

## 0. 修订原则

每条决策含 4 要素：(a) 决策内容，(b) 来源/证据，(c) 来源等级（A=官方/规范/一作论文；B=高星 issue/权威博客/会议；C=SO/普通博客/二手），(d) 置信度（🟢 ≥3 个 A/B 独立来源一致 + <6 个月；🟡 2 个来源一致或主流共识有少数反对；🔴 来源 <2 / 过时 / 仅 C / 分裂；⚫ 不知道）。

未注明置信度的项视为🔴，须在 D2.8 v1.1.3 stub 前补来源或退回。

---

## 1. 顶层决策：选项 D 混合架构最终锁定

### 1.1 决策

L3 TDL SIL 框架采用 **选项 D 混合架构**：production C++/MISRA ROS2 Humble 节点直接运行于 SIL 内核（保证"测试目标即部署目标"），FMI 2.0 / OSP libcosim 仅在 ship dynamics + 未来 RL FMU 边界使用，DNV maritime-schema 作 scenario / output 互认 schema。**不重构为 Python orchestration 包装器**（拒绝选项 A/C 的"测试包装而非产品"反模式）；**不裸 ROS2**（拒绝选项 B 的"自定义 rosbag 评据格式"非互认困境）。

### 1.2 来源

- [E1] *SIL Framework Architecture for L3 TDL COLAV — CCS-Targeted Engineering Recommendation*（2 次深度研究合成报告，2026-05-09），§Q1 比较矩阵 + §Why D wins 段落。来源等级 **B**。
- [E2] *SIL Simulation Architecture for Maritime Autonomous COLAV Targeting CCS Certification*（同期独立报告），Stage 1-4 stage-gate roadmap。来源等级 **B**。
- [E3] *Technical Evaluation of DNV-OSP Hybrid SIL Toolchains for CCS i-Ship N Certification*（NLM Deep Research 2026-05-09，silhil_platform notebook，17 sources cited），§Recommendations for 45m FCB Submission。来源等级 **B**。
- [E4] PyGemini: *Unified Software Development towards Maritime Autonomy Systems*, Vasstein et al., arXiv:2506.06262 (2025) — Configuration-Driven Development 隔离模式 precedent。来源等级 **A**。
- [E5] NTNU SFI-AutoShip `colav-simulator`（github.com/NTNU-TTO/colav-simulator）+ Pedersen/Glomsrud et al. CCTA 2023。来源等级 **A**。

### 1.3 置信度

🟢 High — 3 份独立报告（含 1 份 NLM 深度调研）+ NTNU/PyGemini 工业先例 + DNV-Brinav MoU(2024) 商业验证全部指向同一结论。

### 1.4 推翻信号

- 若 CCS surveyor 明确要求 Modelica/FMI-pure 提交（>30% kernel 须移到 FMU）→ 重新评估选项 A
- 若 RL 上升为主测试驱动（>50% CI 时间）→ 重新评估选项 C
- 当前均无此信号

---

## 2. DNV 工具链锁定深度：3 MUST + 2 NICE-deferred

### 2.1 决策

**MUST（Phase 1 必须采用）**：
- `dnv-opensource/maritime-schema` v0.2.x（scenario YAML + output Apache Arrow schema）— MIT
- `open-simulation-platform/libcosim`（FMI 2.0 co-sim master，C++）— MPL-2.0
- `dnv-opensource/farn` + `dnv-opensource/ospx`（n-dim case folder generator + OSP system structure）— MIT

**NICE-deferred（Phase 2/4 引入）**：
- `dnv-opensource/ship-traffic-generator` (`trafficgen`)—Phase 2 D2.4 用于 50→200 场景扩展
- `dnv-opensource/mlfmu`—Phase 4 D4.6 RL 启动时引入（B2 时间窗）

### 2.2 来源

- [E3] §Recommendations: "MUST-have Subset for CCS Credibility = maritime-schema + libcosim/OSP + farn"
- [E1] §7 Open-source licensing 验证表：DNV 工具链全部 MIT/MPL-2.0 商业可用
- [E6] DNV-RP-0513 (2024 ed.) *Assurance of simulation models* — 唯一海事仿真模型保证规范，maritime-schema 治理者明确对齐。来源等级 **A**。
- [E7] `farn` v0.4.2 (2025-late) GitHub Releases — Python 3.11–3.14 兼容；活跃维护。[https://github.com/dnv-opensource/farn/releases](https://github.com/dnv-opensource/farn/releases)。来源等级 **A**。
- [E8] `maritime-schema` 与 `ship-traffic-generator` 2025-02 仓库合并 — 治理稳定，PyPI v0.2.x 已发。[https://github.com/dnv-opensource/maritime-schema/releases](https://github.com/dnv-opensource/maritime-schema/releases)。来源等级 **A**。

### 2.3 置信度

🟢 High（MUST 三件）/ 🟡 Medium（NICE 二件 — mlfmu RL stateful 导出有已知 wrap 复杂度，Phase 4 启动前需重新评估）。

### 2.4 ROS2 ↔ FMI 桥接边界

- **bridge mediator**：`dds-fmu` + custom `libcosim::async_slave` 实现
- **延迟实测预算**：单次 exchange 2-10 ms（[E3] 引 [https://open-simulation-platform.github.io/libcosim](https://open-simulation-platform.github.io/libcosim) 文档 + 类比项目数据）
- **关键边界规则**（用户拍板 2026-05-09）：**M7 Safety Supervisor 严格留 ROS2 native，不过 FMI 边界**。理由：M7 端到端 KPI < 10 ms（架构 §11.4 + 计划 D1.5），dds-fmu 单次 exchange 即可吃掉 KPI。仅 ship dynamics（own + targets）+ 未来 RL FMU 走 OSP/FMI。
- 工时估算：Phase 1 D1.3c bridge 单层 12-16 pw（dds-fmu 集成 + async_slave 自定义 + jitter buffer + Phase 1 仅 own_ship FMU 接通）

### 2.5 推翻信号

- 若 dds-fmu 实测 latency >10 ms 或 jitter 不可控 → 退回纯 ROS2 native + 在边界处加 maritime-schema/Arrow 序列化层（保留 schema 互认，放弃 FMI 协同）
- 若 mlfmu Phase 4 启动后 stateful RL（LSTM/GRU）wrap 不可行 → RL 仅作 scenario adversary 不进部署环路，B2 不上升为 in-loop policy

---

## 3. ROS2 版本与操作系统锁定

### 3.1 决策

**ROS2 Humble + Ubuntu 22.04 + PREEMPT_RT**（用户拍板 2026-05-09）。**不切 Jazzy / Ubuntu 24.04**。

### 3.2 来源

- [E9] 用户实船 OT 部署约束：FCB onboard 计算单元规划 Ubuntu 22.04 + PREEMPT_RT 实时内核（项目内部约束，源等级 **A**）
- [E3] §ROS2 Jazzy and Ubuntu Compatibility for 2026："ROS2 Humble (Ubuntu 22.04): Offers the highest stability for the libcosim and dds-fmu tools"。来源等级 **B**。
- [E10] ROS2 Jazzy 官方目标 Ubuntu 24.04（Noble），22.04 用 Humble 是 LTS 路径。[https://docs.ros.org/](https://docs.ros.org/)。来源等级 **A**。

### 3.3 置信度

🟢 High — 实船部署约束 + 工具链稳定性 + RT 兼容性三重证据收敛。

### 3.4 风险注

DNV 工具链最新版部分需 Python 3.11+；Humble 默认 Python 3.10 — 需在容器内安装 3.11，作为 D1.3c sub-task 验证。

---

## 4. RL 隔离架构（三层强制边界）

### 4.1 决策

即使 B2 RL 推 Phase 4（10–12 月）启动，**RL 隔离 3 层边界须在 v1.1.3-pre-stub（2026-05-09）锁定**，并在 D1.3a/b 仓库结构 + CI lint rule 现在落地，避免 Phase 4 启动时回退污染已审认证内核。

| 层 | 边界 | 强制实现 |
|---|---|---|
| **L1 Repo** | `/src/l3_tdl_kernel/**` (C++/MISRA, ROS2 nodes M1-M8, frozen, DNV-RP-0513 assured) vs `/src/sim_workbench/**`（Python sim 工具）vs `/src/rl_workbench/**`（Phase 4 启动；Python, Gymnasium, SB3） | 三 colcon 包独立；CI lint 检测 cross-import 即报错 |
| **L2 Process** | RL 训练独立 Docker container；通过 OSP libcosim FMI socket 调相同 MMG FMU + scenario YAML，**绝不**触 C++ 代码 | docker-compose 隔离 namespace；只读挂载 certified binaries |
| **L3 Artefact** | 训练完毕 ONNX → mlfmu (Phase 4) → `target_policy.fmu` → libcosim 加载到 certified SIL；**Python/PyTorch 永不入 certified loop** | `mlfmu build` 是边界；FMU 进 evidence store 须经 DNV-RP-0671 鉴定 |

### 4.2 来源

- [E1] §5 RL boundary architecture: "Boundary rule: No RL artefact ... is permitted to import or be imported by any ROS2 node that is part of the L3 certified kernel. The boundary is enforced at three levels: Repo-level, Process-level, Artefact-level"
- [E2] §Stage-3 RL 段落 + Sim2Sea (Cui et al., AAMAS 2026, arXiv:2603.04057) 工业先例
- [E4] PyGemini Configuration-Driven Development 容器隔离模式 precedent
- [E11] DNV-RP-0671 (2024) *Assurance of AI-enabled systems* — RL FMU 回注后的鉴定规范。来源等级 **A**。
- [E12] DNV-RP-0510 *Data-driven algorithms*。来源等级 **A**。

### 4.3 置信度

🟢 High — NTNU/PyGemini/Sim2Sea 三独立工业先例 + DNV-RP-0671/0510 规范支撑。

### 4.4 推翻信号

- 若 CCS surveyor 明示接受 Python 在 certified loop 内（极不可能，违反 IEC 61508-3 SIL 2 SDLC 要求）
- 若 mlfmu 被废弃（当前活跃维护，无废弃信号）

---

## 5. 场景库 schema：DNV maritime-schema 替代内部 schema

### 5.1 决策

D1.6 场景 schema 由"内部 Pydantic 强类型"改为"**`maritime-schema` `TrafficSituation` 扩展**"。FCB 项目专属字段（hazid_refs, colregs_rules, odd_cell, disturbance, expected_outcome, seed, vessel_class）放入 `metadata.*` 扩展节点（schema 允许 additional properties）。

### 5.2 来源

- [E1] §4 Scenario library architecture: "extend, don't replace ... maritime-schema permits additional properties"
- [E3] §Maritime Schema Stability and Governance: maritime-schema v0.2.x emerging stable + DNV 治理
- [E13] Hassani et al. (2022) *Automatic traffic scenarios generation for autonomous ships collision-avoidance system testing*, Ocean Eng., DOI:10.1016/j.oceaneng.2022.111864 — Sobol sampling + COLREG geometric filter + risk vector clustering 工业先例。来源等级 **A**。

### 5.3 置信度

🟢 High — DNV 主推 + Brinav MoU(2024) 商业验证 + Hassani 2022 学术先例。

### 5.4 双语言验证

- Python 用 `cerberus` ([E14] cerberus.readthedocs.io) + `pydantic`（maritime-schema 原生）
- C++ 用 `cerberus-cpp` ([E15] github.com/dokempf/cerberus-cpp) — 同 schema 文件，避免 Python/C++ 两套验证逻辑

### 5.5 CCS 接受度（未解低置信度）

- 🔴 Low：无公开 Chinese AiP 案例使用 maritime-schema 作 evidence container
- D1.8 早期发函 CCS 技术中心确认（路径：CCS-DNV-Brinav 2024 MoU + Brinav Armada 78 03 案例）
- 退路：若 CCS 要求中文专用格式，maritime-schema 退为内部表示，加导出器至 CCS 要求格式

---

## 6. 场景生成与覆盖立方体

### 6.1 决策

- **Imazu-22 强制基线**（每 PR fast gate 必跑），freeze 为 `imazu22_v1.0.yaml` SHA256 hash 化，进 D1.3b.1 范围
- **覆盖立方体** = 11 COLREG Rules × 4 ODD subdomains × 5 disturbance bins × 5 seeds = **1100 cells**（D3.6）
- **Adversarial / Nominal / Boundary = 60 / 25 / 15 比例** 明确标注"内部启发式，非外部标准"，CCS 提交时不引为外部规范
- **Monte Carlo LHS / Sobol 10000 sample** 进 D3.6 扩展（pass rate 95% CI + CPA min 分布）

### 6.2 来源

- [E16] Imazu, S. (1987). 引 Sawada, R., Sato, K., Majima, T. (2021). *Automatic ship collision avoidance using deep reinforcement learning with LSTM in continuous action spaces*. J. Mar. Sci. Technol. 26 — Imazu 22 canonical encounters。来源等级 **A**。
- [E1] §4 Scenario library architecture + concrete YAML example
- [E2] §Stage 1: "Imazu 22-case set + 200 ODD-extended scenarios as v1.0 release. Trigger to advance: all 22 Imazu cases passing with CPA ≥ 200 m and COLREG-classification rate ≥ 95%"
- [E13] Hassani 2022 — Sobol sampling 工业先例
- [E17] Krasowski et al. (2025) *Intelligent Sailing Model for Open Sea Navigation*, arXiv:2501.04988 — 4049 critical scenarios + ~97% goal-reach benchmark。来源等级 **A**。
- [E18] Frey et al. (2025) *Assessing Scene Generation Techniques for Testing COLREGS-Compliance of Autonomous Surface Vehicles*, ISSTA, DOI:10.1145/3728919。来源等级 **A**。

### 6.3 置信度

🟢 High（Imazu-22 + cube）/ 🟡 Medium（60:25:15 比例 — 用户已认可标"内部启发式"，CCS surveyor 反弹风险中等）。

---

## 7. AIS-driven 场景授权工具（colav-simulator 级能力）

### 7.1 决策

D1.3b.2（NEW）实现 colav-simulator 级 AIS-driven scenario authoring：
- AIS DB 接入（PostGIS / Kystverket / NOAA / MarineCadastre 开放数据）
- bbox + 时间窗 → encounter 提取（DCPA < 500m 阈值，TCPA 时间窗）
- AIS 预处理管线：MMSI 分组 + 间隙拆段（>5 min）+ Δt=0.5s 重采样 + NE 线性插值 + COG 圆形插值 + Savitzky-Golay / Kalman 平滑
- WGS84 → local NE Cartesian (Transverse Mercator)
- COLREG 几何分类（Head-on / Crossing / Overtaking）+ maritime-schema YAML 导出
- 3 种 target 运动模式：`ais_replay_vessel`（D1.3b.2，必），`ncdm_vessel`（NCDM Ornstein-Uhlenbeck 外推，D2.4），`intelligent_vessel`（VO/简化 MPC，D3.6）

### 7.2 来源

- [E5] NTNU `colav-simulator` (autumn 2025 release) + Pedersen, Glomsrud et al. (2020) *Towards simulation-based verification of autonomous navigation systems*, Safety Science 129:104799, DOI:10.1016/j.ssci.2020.104799。来源等级 **A**。
- [E19] *Simulation Framework and Software Environment for Evaluating Automatic Ship Collision Avoidance Algorithms* (NTNU paper, [https://torarnj.folk.ntnu.no/colav_simulator.pdf](https://torarnj.folk.ntnu.no/colav_simulator.pdf))。来源等级 **A**。
- [E20] *Exploration of COLREG-relevant Parameters from Historical AIS-data* (NTNU, [https://torarnj.folk.ntnu.no/AIS_param_paper.pdf](https://torarnj.folk.ntnu.no/AIS_param_paper.pdf))。来源等级 **A**。
- [E21] *Long-term Vessel Prediction Using AIS Data* (NCDM 方法, [https://edmundfo.folk.ntnu.no/msc2019-2020/dalsnesLTP.pdf](https://edmundfo.folk.ntnu.no/msc2019-2020/dalsnesLTP.pdf))。来源等级 **A**。
- [E22] *Risk-based Traffic Rules Compliant Collision Avoidance for Autonomous Ships* (Hagen 2022 NTNU thesis, [https://torarnj.folk.ntnu.no/trym_thesis_final.pdf](https://torarnj.folk.ntnu.no/trym_thesis_final.pdf))。来源等级 **A**。
- [E23] NLM Deep Research 2026-05-09 (silhil_platform notebook, 30 sources cited) — *Architectural Patterns for AIS-Driven Scenario Generation and Target Ship Instantiation in Maritime Autonomous Simulation*。

### 7.3 数据源决策（用户拍板 2026-05-09）

- Phase 1-3 用 **Kystverket + NOAA MarineCadastre 开放数据**（核心验证链路 + COLAV 逻辑）
- Phase 4 实船试航前若 CCS 要求中国海域 AIS，再切商业 / 合作路径
- ENC demo 双区域：**Trondheim Fjord**（colav-simulator paper 复现）+ **NOAA San Francisco Bay**（高 AIS 密度 + 公开 ENC）

### 7.4 置信度

🟢 High — 4 篇 NTNU 一作论文 + 工业开源代码可复用。

---

## 8. 结构化 COLREGs 评分（Hagen 2022 / Woerner 2019）

### 8.1 决策

D2.4 / D3.6 **PASS / FAIL 二元 verdict 保留**，**新增 6 维度连续评分**作为 CCS surveyor 论据：

| 维度 | 含义 | 算法 |
|---|---|---|
| **Safety score** | f(CPA_min / CPA_target) 连续 | [0,1]，CPA ≥ target → 1.0；线性退化到 0 at CPA=0 |
| **Rule compliance score** | 每条适用 Rule 5/6/7/8/13-17/19 子准则离散评分 → 加权求和 | per-rule {full=1.0 / partial=0.5 / violated=0.0} |
| **Delay penalty** | 决策启动相对 TCPA 阈值的延迟 | `P_delay = max(0, t_action - t_target_action) × λ_1` |
| **Action magnitude penalty** | 转向幅度不足或过激（Rule 8 "大幅"） | <30° 或 >90° 扣分；2nd-order in deviation |
| **Phase score** | 让路船 / 直航船角色行为合规度 | give-way 应早期大动作；stand-on 应保持课速直至 in extremis |
| **Trajectory implausibility** | 物理可行性（避免 RL "作弊"） | M5 BC-MPC 解算约束自动满足；外部 target 检查曲率 + 加速度上限 |

`total_score = w_s · safety + w_r · rule - p_delay - p_mag + w_p · phase`，w 系数在 D1.7 规约（待 Hagen 2022 / Woerner 2019 原文细节填）。

### 8.2 来源

- [E22] Hagen, T. (2022) *Risk-based Traffic Rules Compliant Collision Avoidance for Autonomous Ships*, NTNU MS thesis — Section II.C 评分系统。来源等级 **A**。
- [E24] Woerner, K. (2019) *COLREGS-Compliant Autonomous Surface Vessel Navigation*, MIT PhD thesis — per-rule pass/fail/score rubric。来源等级 **A**。
- [E25] Smart Testing of Collision Avoidance Systems Using Signal Temporal Logic, ResearchGate publication 396521132 — STL-based scoring extension。来源等级 **B**。

### 8.3 置信度

🟡 Medium — 维度结构 NTNU/MIT 学术圈公认；具体权重 λ 系数 + per-rule 准则细节需 D1.7 补 Hagen 2022 / Woerner 2019 原文细节。当前对决策影响小（结构 vs 系数）。

### 8.4 推翻信号

- 若 D1.7 实施时发现 Hagen / Woerner 评分维度与 CCS《智能船舶规范 2024》§9.1 性能验证条款不可对齐 → 改为按 CCS 条款重构评分（保留维度数量但重命名）

---

## 9. Web 端 SIL HMI + ENC 集成

### 9.1 决策

替代当前 `L3_TDL_SIL_Interactive.html` 静态 SVG 雷达原型，构建 production-grade web HMI：

| 项 | 选择 | 理由来源 |
|---|---|---|
| 海图引擎 | **MapLibre GL JS**（WebGL）| [E26] MapLibre docs `large-data` guide — 1000+ vessel @60FPS via symbol layers |
| S-57 管线 | **GDAL → Tippecanoe → MVT vector tiles**（或 `manimaul/s57tiler`）| [E27] GDAL S-57 driver docs + [E28] s57tiler GitHub |
| ROS2 ↔ Web 桥 | **`foxglove_bridge`**（C++, Protobuf）— 不用 rosbridge_server (Python/JSON) | [E29] foxglove-sdk/ros docs — 50Hz 性能 |
| HMI 标准 | **IEC 62288 SA subset + IMO S-Mode**（不全 ECDIS）| [E30] IEC 62288:2021+AMD1:2024 + [E31] IMO MSC.1/Circ.1609 S-Mode |
| Replay | **Apache Arrow IPC**（与 maritime-schema 对齐）+ GSAP timeline scrubber | [E32] Apache Arrow JS docs — 零拷贝 timeline scrub |
| Evidence GIF | **Puppeteer headless 浏览器** + ffmpeg | [E33] CI batch 自动产出 |
| 框架 | **React** + MapLibre GL | 用户拍板 2026-05-09（生态 + 招人友好）|

### 9.2 来源

- [E26] MapLibre docs: [https://maplibre.org/maplibre-gl-js/docs/guides/large-data/](https://maplibre.org/maplibre-gl-js/docs/guides/large-data/)
- [E27] GDAL S-57 driver: [https://gdal.org/en/stable/drivers/vector/s57.html](https://gdal.org/en/stable/drivers/vector/s57.html)
- [E28] s57tiler: [https://github.com/manimaul/s57tiler](https://github.com/manimaul/s57tiler)
- [E29] foxglove_bridge: [https://github.com/foxglove/foxglove-sdk/blob/main/ros/src/foxglove_bridge/README.md](https://github.com/foxglove/foxglove-sdk/blob/main/ros/src/foxglove_bridge/README.md)
- [E30] IEC 62288:2021 — Maritime navigation and radiocommunication equipment and systems – General requirements – Methods of testing and required test results。来源等级 **A**。
- [E31] IMO MSC.1/Circ.1609 (2019) *Guidelines for the standardization of user interface design for navigation equipment* (S-Mode)。来源等级 **A**。
- [E32] Apache Arrow JS: [https://arrow.apache.org/docs/3.0/js/index.html](https://arrow.apache.org/docs/3.0/js/index.html)
- [E33] [E23] NLM Deep Research 2026-05-09 (silhil_platform 98 sources)

### 9.3 置信度

🟢 High — 工业链成熟 + 官方文档 + 标准对齐。

### 9.4 三阶段路线（用户拍板 2026-05-09）

- **D1.3b.3**（5/27–6/15, ~5 pw, DEMO-1 6/15）：MapLibre 骨架 + S-57 MVT 管线 + foxglove_bridge + IEC 62288 SA subset + 1 场景 live + 沿用现 HTML 视觉风格（CPA/TCPA/Rule/Decision/M1-M8 pulse/ASDR log 信息块全迁移）
- **D2.5 / D2.6 增项**（6/16–7/31, ~3 pw, DEMO-2 7/31）：Apache Arrow replay + GSAP scrubber + Puppeteer GIF/PNG batch + 多 target + grounding hazard + TLS/WSS
- **D3.4 增项**（7/13–8/31, ~1.5 pw, DEMO-3 8/31）：trajectory ghosting + ToR 倒计时 panel（4 操作员状态联动 D2.1）+ M7 Doer-Checker verdict badge + S-Mode 完整对齐 + 1000 场景 evidence pack 一键产出

---

## 10. 场景文件结构（NTNU + maritime-schema 双兼容样例）

参考 [E5] colav-simulator + [E1] maritime-schema TrafficSituation 扩展，FCB 项目场景 YAML 模板：

```yaml
# yaml-language-server: $schema=schemas/fcb_traffic_situation.schema.json
title: "Crossing-from-port, FCB own ship, two targets, Beaufort 5"
description: "Coverage cube cell rule15 × open-sea × disturbance-D3 × seed-2"
startTime: "2026-05-09T08:00:00Z"
metadata:                            # FCB 项目扩展（schema 允许 additional properties）
  scenario_id: "FCB-OSF-CR-PORT-018"
  hazid_refs: ["HAZ-NAV-014","HAZ-NAV-022"]
  colregs_rules: ["R15","R16","R8"]
  odd_cell:
    domain: "open_sea_offshore_wind_farm"
    daylight: "twilight"
    visibility_nm: 1.5
    sea_state_beaufort: 5
  disturbance:
    wind: {dir_deg: 235, speed_mps: 12.0}
    current: {dir_deg: 90, speed_mps: 0.6}
    sensor: {ais_dropout_pct: 5, radar_range_nm: 6.0, radar_pos_sigma_m: 25}
  seed: 2
  vessel_class: FCB-45m
  expected_outcome:
    cpa_min_m_ge: 300
    rule15_compliance: required
    rule8_visibility: "early_and_substantial"
    grounding: forbidden
simulation_settings:
  dt: 0.5
  total_time: 1200
  enc_path: "data/enc/trondheim_fjord"
  coordinate_origin: [63.43, 10.39]
ownShip:
  static: {shipType: "Fast Crew Boat", length: 45.0, width: 9.0, mmsi: 999000018}
  initial: {position: {latitude: 53.5000, longitude: 7.5000}, sog: 18.0, cog: 0.0, heading: 0.0}
  waypoints: [{position: {latitude: 53.6500, longitude: 7.5000}}]
  model: "fcb_mmg_vessel"
  controller: "psbmpc_wrapper"
targetShips:
  - id: "MMSI_257123456"
    static: {shipType: "Cargo", length: 120.0, width: 18.0}
    model: "ais_replay_vessel"            # D1.3b.2 模式 1
    trajectory_file: "trajectories/TS1_track.csv"
    initial: {position: {latitude: 53.5400, longitude: 7.4500}, sog: 12.0, cog: 90.0, heading: 90.0}
  - id: "MMSI_258987654"
    static: {shipType: "Fishing", length: 28.0, width: 7.0}
    model: "intelligent_vessel"           # D2.4 / D3.6 模式 3
    controller: "vo_controller"
    initial: {position: {latitude: 53.5800, longitude: 7.5200}, sog: 6.0, cog: 200.0, heading: 200.0}
environment:
  wind: {dir_deg: 235, speed_mps: 12.0}
  current: {dir_deg: 90, speed_mps: 0.6}
  visibility_nm: 1.5
```

---

## 11. 工时影响与缺口处理

用户决策 2026-05-09："工时缺口不用担心，核心目标是完整实现避免重构"。

工时增量估算（vs v3.0 基线）：

| D 任务 | 增量 (pw) | 备注 |
|---|---|---|
| D1.3b 拆分（→ b.1 / b.2 / b.3） | +6.0 | b.1 维持 ~3 / b.2 NEW ~4 / b.3 NEW ~5（HMI 重写）|
| D1.3c FMI bridge NEW | +12-16 | dds-fmu + async_slave + jitter buffer + own-ship FMU |
| D1.6 maritime-schema 替代 | +0.5 | Pydantic 适配 |
| D1.7 评分 rubric + Monte Carlo 段 | +0.5 | 文档 + 算法规约 |
| D2.4 6 维度评分实施 | +0.5 | M6 集成 |
| D2.5 / D2.6 HMI 增项（Arrow + GIF）| +2.0 | replay + evidence pack |
| D2.8 v1.1.3 stub 新章节 | +1.0 | §SIL/§RL/§Scoring |
| D3.4 HMI 增项（ToR + ghosting + S-Mode）| +1.5 | M8 完整化 |
| D3.6 6 维度评分立方体 + Monte Carlo 10000 | +1.0 | 评分扩展 + LHS sweep |
| D3.8 v1.1.3 完整化 | +0.5 | 完整 §SIL/§RL/§Scoring |
| RL 隔离（D1.3a/b 仓库结构 + CI lint）| +0.5 | 主要文档 + 1 个 lint rule |
| **小计** | **+26~30 pw** | |

v3.0 基线缺口 -4.5 pw → v3.1 累计缺口 **-30~-34 pw**。用户授权"完整实现优先"，缺口处理路径在 v3.1 §0.4 详述（D3.6 1000 场景延 9/30 / V&V FTE 期延 / D1.3c 由 V&V FTE 全职 + 安全外包临时支援 / B4 触发 4 缺失模块完整推后）。

---

## 12. 修订交付物清单

| 路径 | 动作 | 责任 |
|---|---|---|
| `docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md`（本文档）| 新建 | 架构师 |
| `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` | v1.1.2 → v1.1.3-pre-stub patch（新增附录 F SIL Framework + §16 References [R25]-[R32] + 附录 D''''' 修订记录 + 头表版本号）| 架构师 |
| `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` | v3.0 → v3.1（§0.4 v3.1 摘要 + D1.3 拆分 + D1.3c NEW + D1.5/1.6/1.7/1.8 增量 + D2.4/2.5/2.6/2.8 增量 + D3.4/3.6/3.8 增量）| 架构师 |
| `docs/Design/Architecture Design/MASS_L3_TDL_Architecture_v1.1.2.html` + `.../MASS_L3_TDL_架构设计展示.html` | v1.1.3-pre-stub HTML 同步（D2.8 时统一更新，本次不动）| D2.8 owner |
| `docs/Design/Architecture Design/L3_TDL_SIL_Interactive.html` | 保留作 D1.3b.3 视觉参考；不删；D1.3b.3 Web HMI 替代物上线后再标"deprecated, see SIL_HMI_v2/"| D1.3b.3 owner |

D2.8（7/31）合入 v1.1.3 stub 时，本文档的 §1-§9 决策与对应附录 F 章节将整体迁移至架构报告主体（本文档保留作历史归档）。

---

## 13. 历史追溯

- 2026-05-07：7 角度多角度评审（A/B/C/D/E/F/G）+ 跨角度整合 → 124 finding（30 P0 / 52 P1 / 29 P2）→ 8 月计划 v3.0 锁定
- 2026-05-08：v3.0 计划锁定 + D0 must-fix sprint 启动
- 2026-05-09：2 次 SIL 深度研究产出（`docs/Doc From Claude/SIL Simulation Architecture for Maritime Autonomous COLAV Targeting CCS Certification.md` + `.../SIL Framework Architecture for L3 TDL COLAV — CCS-Targeted Engineering Recommendation.md`）+ 评审专家整合指导书（`.../L3 TDL SIL 架构整合与修订指导书.md`）+ 3 次 NLM Deep Research（silhil_platform notebook 0 → 98 sources）+ 用户决策 Q1/Q2/Q3 全部确认 → 本决策记录 + v1.1.3-pre-stub patch + v3.1 计划修订

---

*文档版本 v1.0。决策已锁定，证据链完整。所有架构 v1.1.3-pre-stub patch + 计划 v3.1 修订须引本文档对应章节为依据。文档变更须经架构师 sign-off。*
