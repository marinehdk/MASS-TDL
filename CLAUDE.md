# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> 本项目工作纪律继承全局 `~/.claude/CLAUDE.md`（Karpathy 四则 + 调研触发器 + 置信度标注 + 研究路由）。本文件只补充全局规则未覆盖的项目专属信息。**当本文件与全局规则冲突时，全局规则优先。**

---

## 1. 项目当前状态（强制阅读 — 2026-05-20 更新，对齐 v3.2-master 文档重构）

### 1.1 阶段
- **实现阶段进行中**：**计划 v3.2-master 文档重构已完成（2026-05-20）**；D0 must-fix sprint ✅ **已关闭**；当前处于 **Phase 1 中段**（D1.x 并行进行，目标 DEMO-1 Skeleton Live 6/15）
- **总账位置**：[docs/Design/00-master-plan.md](docs/Design/00-master-plan.md)（精简 ≤300 行，含进度快照 + 修订记录 + DEMO Charter 摘要）；原 1500 行 gantt 文件已归档到 `Architecture Design/gantt/archive/v3.2_2026-05-20_archived.md`
- **Phase 1 进度（截至 5/20）**：✅ D1.5 V&V Plan / D1.8 cert+ConOps stub / 22 Imazu 场景 frozen / MMG 4-DOF / foxglove + MapLibre + ToR ≥2s；🔴 stub 空壳 4 项（D1.3.1' Sim Qualification / D1.4 编码规范 / D1.6 场景 schema / D1.7 覆盖率方法论）；详见 [Phase 1/00-overview.md](docs/Design/Phase%201/00-overview.md)
- **编号重排（v3.2）**：D1.3a → D1.3.1 / D1.3b → D1.3.2 / D1.3c → D1.3.3 / D1.3b.{1,2,3} → D1.3.2.{1,2,3}（git 分支名保留旧 a/b/c 不改）
- 涉及"跑一下/编译/测试"的请求：参考新结构对应 D 编号；先读 [Phase N/00-overview.md](docs/Design/Phase%201/00-overview.md) 再到 D{x.y} 子文档
- **三档强制 DEMO**：DEMO-1 (6/15) / DEMO-2 (7/31) / DEMO-3 (8/31) — 不可取消

### 1.2 路线进度

```
架构设计 (v1.0 → v1.1 → v1.1.1 → v1.1.2)              ← ✅ 完成（接口跨团队锁定）
   │
   ├── 详细功能设计（M1–M8 全部正式）                    ← ✅ 完成
   ├── 跨团队接口对齐（6 RFC 全部已批准）                 ← ✅ 完成
   ├── 7 角度多角度评审（A/B/C/D/E/F/G）                 ← ✅ 完成 2026-05-07
   │       30 P0 / 52 P1 / 29 P2 → 124 finding 整合
   │       产物: docs/Design/Review/2026-05-07/00-consolidated-findings.md
   │
   ├── 8 月开发计划 v2.0 → v3.0 修订                     ← ✅ 完成 2026-05-08
   │       v2.0 归档 archive/v2.0_2026-05-07_archived.md
   │       v3.0 主文件: gantt/MASS_ADAS_L3_8个月完整开发计划.md
   │       含 D0–D4.7 共 32 个 D 编号 + 三档 DEMO milestone
   │
   ├── D0 Pre-Kickoff Must-Fix Sprint                   ← ✅ 完成 5/12（11 must-fix 关闭）
   ├── HAZID RUN-001 kickoff                            ← ⏳ 5/13 第 ① 次会议（进行中）
   ├── Phase 1 工程基础 + V&V 基线 (D1.1–D1.8)         ← ⏳ 5/13–6/15 → DEMO-1（中段）
   │       ✅ D1.5 V&V Plan（v0.1 + Phase1 artifacts）
   │       ✅ SIL v1.0-unified 4 文档套件（01/02/03/04，2026-05-15 起持续修订）
   │       ✅ Screen 1/2/3 spec+plan 吸收进 SIL 套件
   │       ⏳ D1.3a/D1.3b/D1.3c 并行（分支推进中）
   ├── Phase 2 M1/M2/M3/M6 + Cert + HF (D2.1–D2.8)     ← ⏳ 6/16–7/31 → DEMO-2
   ├── Phase 3 M4/M5/M7/M8 + SIL 1000 (D3.1–D3.9)      ← ⏳ 7/13–8/31 → DEMO-3
   ├── HAZID 完成 → v1.1.3 回填 (D3.5/D3.8)             ← 8/19 / 8/31
   │
   ├── HIL 集成 (D4.1/D4.2)                              ← Phase 4 展望 9–11月
   ├── SIL 2 第三方评估 (D4.3)                           ← 9–11月
   ├── CCS i-Ship AIP 提交 (D4.4)                        ← 11月
   ├── FCB 非认证级技术验证试航 (D4.5)                   ← 12月（用户决策 2026-05-07）
   ├── 船长/ROC 模拟器认证 (D4.5')                       ← 11月
   ├── RL 对抗 v1.0 (D4.6, B2 后移)                      ← 10–12月
   ├── 4 缺失模块完整 (D4.7, B4 contingency)             ← 9–10月（条件触发）
   │
   ├── HAZID RUN-002 拖船 (≥ 6 周)                       ← 10–12月
   ├── HAZID RUN-003 渡船 (≥ 6 周)                       ← 12月起
   └── 认证级实船试航 D5.x                               ← 2027 Q1/Q2 AIP 受理后
```

### 1.3 当前权威文件

- **总开发计划主文件**：[docs/Design/00-master-plan.md](docs/Design/00-master-plan.md)（**v3.2-master 当前权威，2026-05-20**，精简总账 + 文档导航）
- **架构主文件**：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`（**v1.1.3-pre-stub 当前权威，2026-05-09**）
- **8 月开发计划主文件**：`docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`（**v3.0 当前权威，2026-05-08，已锁定**）
- **SIL 设计套件 v1.0-unified**（D1.3 系列工程权威，2026-05-15 起持续修订）：
  - `docs/Design/SIL/v1.0-unified/01-sil-architecture.md`（v1.0，2026-05-15）
  - `docs/Design/SIL/v1.0-unified/02-sil-backend-design.md`（**v1.0.3 当前**）
  - `docs/Design/SIL/v1.0-unified/03-sil-frontend-design.md`（**v1.0.4 当前**）
  - `docs/Design/SIL/v1.0-unified/04-sil-scenario-integration-test.md`（**v1.0.3 当前**）
  - 整合指导：`docs/Doc From Claude/L3 TDL SIL 架构整合与修订指导书.md`（SIL 套件演进路线图）
- **V&V 基线**：`docs/Design/V&V_Plan/00-vv-strategy-v0.1.md` + `2026-05-12-vv-phase1-artifacts.md`（D1.5 ✅）
- v1.1.3 进度：D2.8（7/31）出 stub（§16 cyber + §15.0 时基 + §12.5 培训 + §12.3 心智 + §10.5 4-DOF 边界 + 4 缺失模块 stub）→ D3.8（8/31）完整化 → D3.5（8/31）HAZID 132 [TBD] 回填
- 历史版本（v1.0 / v1.1 / v1.1.1）已归档到 `archive/`，开发计划 v2.0 已归档到 `gantt/archive/v2.0_2026-05-07_archived.md`，**不要从归档读取，永远引用主文件**
- 文件索引：`docs/Design/Architecture Design/README.md`

### 1.4 架构文档可质疑性

v1.1.2 已通过 5 角色复审 + DNV 验证官独立挑战 + 6 RFC 跨团队评审 + 7 角度多角度独立评审（2026-05-07），**但仍非"终态"**：
- 仍有 **132 项 [TBD-HAZID]** 标注的参数须 HAZID RUN-001 校准（8/19 完成 → v1.1.3 回填）；行为分支结构敏感参数在 7/31 前先锁定初值（D3.5）
- 跨团队改造完成后可能引发 IDL 微调 → v1.1.3 或 v1.2
- **2026-12 实船试航降级**：FCB 仅作"非认证级技术验证 + AIS 数据采集"（用户决策 2026-05-07，详见 `docs/Design/Review/2026-05-07/00-consolidated-findings.md` §13.3）；认证级试航延 2027 Q1/Q2 AIP 受理后
- v3.0 计划闭口数学：87.0 人周 vs 84.0 产能 = -3.0 缺口（详见 v3.0 §0.3 闭环路径）
- 发现矛盾 / 过时引用 / 内部不一致时，按 Karpathy 第 1 条（Think First）显式提出

## 2. 系统坐标系

本仓库是 MASS（Maritime Autonomous Surface Ship）完整系统中**仅 L3 战术层**的设计与开发。MASS 完整架构遵循 **v3.0 Kongsberg-Benchmarked Industrial Grade**（基线见 `docs/Init From Zulip/‼️mass_adas_architecture_v3_industrial（Kongsberg Benchmark).html`）：

```
Z-TOP    网络安全墙 + DMZ              IACS UR E26/E27（IT/OT 隔离 + Data Diode + DDS-Security）

Multimodal Fusion 多模态感知融合        独立感知子系统（不属 L1-L5 主决策栈）
                                       Sensors（GNSS/Gyro/IMU/Radar/AIS/Camera/LiDAR 冗余）
                                       → Fusion Pipeline → Nav Filter（15-state EKF，统一自船状态源）
                                       Feeds: L3、L4、Reflex Arc

主决策栈：
  L1  任务层 Mission Layer              [hrs~days]    — 不在本仓库（航次令、气象路由、ETA/油耗优化）
  L2  航路规划层 Voyage Planner         [min~hrs]     — 不在本仓库（ENC、WP 生成、速度剖面、安全门）
  L3  战术层 Tactical Layer             [sec~min]    ⬅⬅ 本仓库（D2/D3/D4 自主等级下 COLREGs 实时决策）
  L4  引导层 Guidance Layer             [100ms~1s]   — 不在本仓库（LOS 跟踪、漂移补偿、look-ahead）
  L5  控制分配层 Control & Allocation    [10ms~100ms] — 不在本仓库（PID/Backstepping、推力分配）

X-axis   Deterministic Checker         独立确定性验证器；对 L2/L3/L4/L5 决策具 VETO 权
                                       Doer 不能绕过；VETO 后回退 nearest compliant
Y-axis   Emergency Reflex Arc          Perception 极近距离 → bypass L3/L4 → 直达 L5（<500ms）
Z-BOTTOM Hardware Override + ASDR      零软件硬连线急停 + Extended VDR（IMO MASS 4-level 模式指示）

横向支持：Parameter Database（操纵系数/停船/吃水/风流/推进配置/降级回退）、Shore Link via DMZ（遥测+遥控接管+自主等级仲裁）
```

**本仓库 L3 的接口边界**（按 v3.0）：
- 上游消费：L2（WP list + speed profile）+ Multimodal Fusion（Track 级目标 + Nav Filter 自船状态）+ Parameter Database
- 下游输出：→ L4（Avoidance WP + speed adj；或 v1.0 设计中的 ψ_cmd/u_cmd/ROT）
- 横向接受：X-axis Deterministic Checker 的 VETO；Y-axis Reflex Arc 在极端场景跳过 L3
- 横向输出：ASDR 决策日志；Shore Link 透明性数据

L1/L2/L4/L5、Multimodal Fusion、Deterministic Checker、Cybersecurity、Sim 等其他层/轴的设计文档作为**接口参考**存在 `docs/Init From Zulip/` 内，可以读但**不要修改**——它们是其他团队的产物，本仓库职责仅是消费它们的输出契约。

> **注**：v1.0 架构文档（`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`）§1.2 / §6 / §7 中残留"L1 感知层"、"L2 战略层"等旧术语，是审计的待修补条目（参见 `docs/Design/Architecture Design/audit/`），不在本文件修订范围内。

## 3. 架构骨架（八模块三层）

完整设计见架构文档第 4–12 章。这里只列必须先于任何模块级讨论建立的**全局事实**：

| 模块 | 职责一句话 | 时间尺度 |
|---|---|---|
| **M1** ODD/Envelope Manager | 整个 TDL 的调度枢纽，唯一的"当前安全语境"权威 | 长时（0.1–1 Hz） |
| **M2** World Model | 唯一权威世界视图（静态/动态/自身），含 COLREG 几何预分类 | 短时（10–50 Hz） |
| **M3** Mission Manager | 航次计划、ETA、重规划触发 | 长时 |
| **M4** Behavior Arbiter | 行为字典 + IvP 多目标仲裁 | 中时（1–4 Hz） |
| **M5** Tactical Planner | Mid-MPC（≥90s）+ BC-MPC，输出 (ψ, u, ROT) 至 L4 | 中时 + 短时 |
| **M6** COLREGs Reasoner | 规则推理（ODD-aware 参数） | 中时 |
| **M7** Safety Supervisor | Doer-Checker 中的 Checker，IEC 61508 + SOTIF 双轨 | 短时 |
| **M8** HMI/Transparency Bridge | 唯一对 ROC/船长说话的实体，SAT-1/2/3 透明性 | 实时（50–100 Hz） |

**模块通信**：发布-订阅（推荐 ROS2 DDS），消息强类型 + `stamp` + `schema_version` + `confidence ∈ [0,1]` + `rationale` 字段强制（v3.0 D2.8 / D3.8 IDL 修订）。任何模块设计若绕过总线直接调用其他模块，须在 PR 描述里显式辩护。

### 3.1 v3.0 新增角色（5/8 起，4 个月期）

| 角色 | 在岗期 | 主要承担 |
|---|---|---|
| **V&V 工程师**（FTE）| 5/8–8/31（默认延 2 周到 9/14 闭口）| D1.5 V&V Plan / D1.6 场景 schema / D1.7 覆盖率方法论 / D1.3.1 仿真器鉴定 / D2.5 SIL 集成 / D3.6 SIL 1000 |
| **安全工程师**（外包）| 5/15–7/10 | D2.7 HARA + FMEDA M1 / D3.3a Doer-Checker 三量化矩阵 / D3.3b SOTIF / D3.9 RFC-007 cyber |
| **HF 咨询**（外包）| 6/16–7/27 | D2.6 船长 ground truth (5 访谈 + Figma + 可用性) / D3.5' 培训课程 |

## 4. 顶层架构决策（不可让步项）

以下四项在架构文档第 2 章已被定为顶层决策。修改它们等于推翻整个架构，须显式标记 **ADR breaking change** 并提交独立讨论：

1. **ODD 是组织原则，不是监控模块** — M1 ODD 状态变化是行为切换的**唯一权威来源**。算法不许各自维护"当前是否安全"的判断。
2. **Doer-Checker 双轨** — M1–M6 是 Doer，M7 是 Checker。Checker 的逻辑必须比 Doer 简单 **100×** 以上，且**实现路径独立**（不共享代码/库/数据结构）。
3. **CMM 通过 SAT-1/2/3 接口对外可见，不在系统内实现状态机** — 每模块须实现 `current_state()` / `rationale()` / `forecast(Δt)+uncertainty()` 三个调用，由 M8 聚合。
4. **多船型 = Capability Manifest + PVA 适配 + 水动力插件**（Backseat Driver 范式） — 决策核心代码零船型常量。**严禁** "if vessel == FCB" 之类的判断潜入 A 层。

## 5. 强制约束（认证驱动）

| 约束 | 含义 | 触发讨论的场景 |
|---|---|---|
| CCS《智能船舶规范》入级 | i-Ship (Nx, Ri/Ai) 标志，决策须**白盒可审计** | 引入黑箱 ML / 隐式状态 / 不可解释优化目标时 |
| IMO MASS Code（MSC 110/111） | 系统须能识别自身越出 Operational Envelope | 任何会让"是否在 ODD 内"的判断变模糊的设计 |
| IEC 61508 SIL 2（核心安全功能） | M1 模式仲裁、M7 仲裁、MRC 触发为核心安全功能 | 这些路径上的依赖、第三方库、状态共享 |
| ISO 21448 SOTIF | 感知降质、ML 模型功能不足按 SOTIF 处理 | 引入感知不确定度、长尾场景假设 |
| TMR ≥ 60s（Veitch 2024）| ROC 接管时窗设计基线 | 任何会压缩可用接管时间的方案 |

## 6. 设计阶段的"完成"判据（Karpathy 第 4 条本地化）

设计阶段没有 `pytest`、没有 CI，但仍要可验证。每个设计交付物的"完成"必须满足：

- [ ] **追溯**：每条架构断言指向 (a) 仓库内已有文档章节，或 (b) `docs/` 外部引用 `[Rx]`，或 (c) 全局/领域 NLM 笔记本的具体来源
- [ ] **接口契约**：模块设计文档须有"输入消息 / 输出消息 / 频率 / 置信度字段"四项最小契约（架构报告 §15 是模板）
- [ ] **降级路径**：每个模块须说明在 DEGRADED / CRITICAL / OUT-of-ODD 时的行为
- [ ] **CCS 映射**：决策模块须映射到 DMV-CG-0264 的 9 个子功能之一（覆盖矩阵在架构文档第 14 章）
- [ ] **置信度标注**：网络/推断结论按全局规则用 🟢🟡🔴⚫ 标注，**不能写"我记得"** 或 "应该是"

未满足上述任一项的"设计完成"声明，按 Karpathy 第 1 条退回。

## 7. 文档编辑规则（Karpathy 第 3 条本地化）

- 改一个模块的设计 = 只改那一章 + 必要的接口契约表更新。**不要顺手统一格式、改其他章节的措辞、补充其他模块的"小问题"**。
- 发现其他模块有问题：在 PR/会话里**指出**，不要自己改。
- 架构报告（**v1.1.3-pre-stub 当前权威**）与早期研究稿（`docs/Doc From Claude/2026-04-2*`）+ 归档版本（`archive/v{X}_*_archived.md`）冲突时，以**当前主文件**为准；但若主文件本身有内部矛盾，**停下来询问用户**，不要静默择一。
- 引用编号 `[Rx]` 是架构文档的硬约束。新增引用须分配新编号并加入参考文献章节，**禁止**裸贴 URL 或 "据某研究"。
- **修订版本管理**（v1.0 → v1.1 → v1.1.1 → v1.1.2 → v1.1.3-pre-stub → 未来 v1.1.3）：
  - 主架构文件名永远是 `MASS_ADAS_L3_TDL_架构设计报告.md`（无版本后缀）；版本信息在文件内部头表
  - 旧版本归档到 `archive/v{X.Y.Z}_{YYYY-MM-DD}_archived.md`（不可改）
  - 每次升级版本须在文件附录补 v{X-1} → v{X} 修订记录

### 7.1 文档分层规则（v3.2-master 重构 2026-05-20）

项目文档采用 **L1 总账 + L2 Phase + L3 双轨（D 任务 + M 模块）** 三层结构，外加跨阶段证据文档夹。新建任何设计/计划/报告类文档时**必须按本规则归属**，不许散落到 `docs/` 根目录或 `docs/Doc From Claude/`。

| 层级 | 路径 | 用途 |
|---|---|---|
| **L1 总账** | [docs/Design/00-master-plan.md](docs/Design/00-master-plan.md) | 工时数学 / 三档 DEMO milestone / 修订记录 / 文档导航 ≤300 行 |
| **L2 阶段** | `docs/Design/Phase {0,1,2,3}/00-overview.md` | 阶段目标 + 甘特 + D 任务索引 + DEMO Charter + 进度快照 |
| **L3a D 任务（开发流）** | `docs/Design/Phase N/D{x.y}-XXX/D{x.y}-{spec,plan,report}.md` | 单个 D 任务的 spec / plan / report；含 `evidence/` 子目录 |
| **L3b M 模块（系统流）** | `docs/Design/TDL-Kernel/M{n}-XXX/M{n}-{spec,progress}.md` | M1–M8 模块设计要点 + D 任务联动表（Closed in / Currently Implementing / Blocks）|
| 跨阶段证据 | `docs/Design/{Architecture Design, SIL/v1.0-unified, V&V_Plan, Cert, ConOps, Safety, HF, Cross-Team Alignment, HAZID}/` | 跨多 Phase / 多 D 任务的稳定 artifact（架构 / SIL 套件 / V&V / 认证 / RFC 等）|

**双轨联动规则**：
- 每个 PR 合并到对应 M 模块时，**同步更新** `TDL-Kernel/M{n}/M{n}-progress.md` 的联动表
- 每个 D 任务在其 `D{x.y}-spec.md` 头部声明 `Affects: M2/M5/M8`（涉及哪些模块）
- 每个 M 模块在其 `M{n}-spec.md` 头部声明 `Currently Implementing: D{x.y}`（当前活跃 D 任务）
- 这样从任一方向查询都能 1 跳跳到对应另一侧

**新 D 任务启动流程**：
1. 在 `docs/Design/Phase N/` 下建 `D{x.y}-{kebab-slug}/` 目录
2. 启动 `superpowers:brainstorming` → 产出 `D{x.y}-spec.md`
3. 启动 `superpowers:writing-plans` → 产出 `D{x.y}-plan.md`
4. 启动 `superpowers:executing-plans` → 产出 `evidence/` + 闭口后写 `D{x.y}-report.md`
5. 同步更新涉及模块的 `TDL-Kernel/M{n}/M{n}-progress.md`

**编号规则**：
- D 任务统一用纯数字 `.` 分隔，禁字母：D1.1 / D1.3.1 / D1.3.2.3 / D2.4
- 旧 D1.3a/b/c → D1.3.1/2/3 重排映射详见 [00-master-plan.md](docs/Design/00-master-plan.md) §编号映射
- git 分支名 **保留原 a/b/c**（如 `feat/d1.3b.3-*`）不强制改名

**Archive 规则**：
- `Archive/` / `archive/` / `Phase 0/Archive/` 所有归档子目录 **只读**，**不引用为权威**
- 想引用历史决策时优先找当前权威主文件（master-plan / 架构主文件 / RFC-decisions）
- 把当前文件归档时 `git mv` 到 `archive/v{X.Y}_{YYYY-MM-DD}_archived.md`，并在新文件修订记录中标"已归档"

## 8. 文件夹语义（决定哪里可写 — v3.2-master 重构后）

| 路径 | 性质 | 可改？ |
|---|---|---|
| **`docs/Design/00-master-plan.md`** | **L1 总账（v3.2-master 当前权威）** | ✅ 主战场（只在大节奏变更时更新）|
| **`docs/Design/Phase {0,1}/00-overview.md`** | **L2 阶段索引（含 D 任务列表 + 进度快照）** | ✅ 主战场（每 Phase 启动 + 进度变化时更新）|
| **`docs/Design/Phase {0,1}/D{x.y}-*/D{x.y}-{spec,plan,report}.md`** | **L3a D 任务开发流文档** | ✅ 主战场（每 D 任务 brainstorm/plan/execute 产出）|
| **`docs/Design/TDL-Kernel/M{1..8}-*/M{n}-{spec,progress}.md`** | **L3b M 模块系统流文档** | ✅ 主战场（PR 合并涉及对应 M 模块时同步更新 progress）|
| `docs/Design/Architecture Design/` | 架构主文件 v1.1.3-pre-stub + README + audit/ | ✅ 主战场 |
| `docs/Design/Architecture Design/archive/` | 历史归档 v1.0/v1.1/v1.1.1/v1.1.2 | ❌ 只读历史 |
| `docs/Design/Architecture Design/gantt/archive/v3.2_2026-05-20_archived.md` | 原 1500 行计划归档（含完整 D 任务 DoD + finding 闭环表 + 工时附录 A-E，作权威细节参考）| ❌ 只读 |
| `docs/Design/SIL/v1.0-unified/` | SIL 4 文档套件（01 架构 / 02 后端 / 03 前端 / 04 联调）| ✅ 主战场 |
| `docs/Design/SIL/0{1,2,3}-*.md` | scenario schema / coverage metrics / sim qualification（当前 stub，DEMO-1 前必须填充）| ✅ 主战场 |
| `docs/Design/Cross-Team Alignment/` | RFC 决议（新 RFC 在此创建）| ✅ 主战场 |
| `docs/Design/V&V_Plan/` | V&V 策略（D1.5 已在 Phase 1/D1.5/V&V_Plan/，此根目录预留 v1.1.3 完整版）| ✅ 主战场 |
| `docs/Design/HF/` | 船长访谈 / Figma / 可用性 / 培训矩阵 | ✅ 主战场 |
| `docs/Design/Cert/` | cert-evidence-tracking + ConOps stub | ✅ 主战场 |
| `docs/Design/ConOps/` | ConOps v0.1 → 完整版 | ✅ 主战场 |
| `docs/Design/Safety/HARA/` | HARA ≥30 危险源 → SIF → SIL | ✅ 主战场 |
| `docs/Design/Safety/FMEDA/` | M1 / M7 FMEDA 表 | ✅ 主战场 |
| `docs/Design/Safety/ALARP/` | ALARP demonstration（B3 推 v1.1.4）| ⏳ Phase 4 起 |
| `docs/Design/SDLC/` | IEC 61508-3 §7 SDLC plan（B3 推 v1.1.4）| ⏳ Phase 4 起 |
| `docs/Design/Cybersecurity/` | RFC-007 L3↔Z-TOP/Cybersec | ✅ 主战场 |
| `docs/Design/Phase 0/Archive/` | D0 时期归档 RFC/HAZID/Review | ❌ 只读历史 |
| `docs/Design/Archive/Old Modules/M{1..8}-*/01-detailed-design.md` | 老 M 模块详设（v3.2 起作 TDL-Kernel/M{n}-spec.md 的指针目标）| ❌ 只读历史 |
| `docs/Design/Detailed Design/` | ⚠️ **已废弃**（被 Phase 1/D{x.y}/ 替代）| 不要写新文件 |
| `docs/Design/V&V_Plan/` | V&V 策略 / SIL→HIL→实船 entry-exit gates / 端到端 KPI（D1.5 起）| ✅ 主战场（v3.0 NEW）|
| `docs/Design/SIL/` | scenario schema / coverage metrics / simulator qualification report（D1.3.1/D1.6/D1.7）| ✅ 主战场（v3.0 NEW）|
| `docs/Design/HF/` | 船长访谈纪要 / Figma 链接 / 可用性测试 / 培训胜任力矩阵（D2.6/D3.5'）| ✅ 主战场（v3.0 NEW）|
| `docs/Design/Cert/` | cert-evidence-tracking + ConOps stub（D1.8 起）| ✅ 主战场（v3.0 NEW）|
| `docs/Design/ConOps/` | ConOps v0.1 → 完整版（CCS AIP 头号必需件）| ✅ 主战场（v3.0 NEW）|
| `docs/Design/Safety/HARA/` | HARA 实例化 v0.1（≥30 危险源 → SIF → SIL → 缓解）| ✅ 主战场（v3.0 NEW）|
| `docs/Design/Safety/FMEDA/` | M1 / M7 FMEDA 表 | ✅ 主战场（v3.0 NEW）|
| `docs/Design/Safety/ALARP/` | ALARP demonstration（B3 推 v1.1.4）| ⏳ Phase 4 起 |
| `docs/Design/SDLC/` | IEC 61508-3 §7 SDLC plan（B3 推 v1.1.4）| ⏳ Phase 4 起 |
| `docs/Design/Cybersecurity/` | RFC-007 L3↔Z-TOP/Cybersec 接口 + IACS UR E26/E27 责任划分（D3.9）| ✅ 主战场（v3.0 NEW）|
| `docs/Doc From Claude/` | 早期 Claude 研究稿 | ⚠️ 仅追加，**不改旧文件** |
| `docs/Init From SINAN/` | 外部团队 SINAN 输入 | ❌ 只读 |
| `docs/Init From Zulip/` | 其他层（L1/L2/L4/L5/Fusion/Checker/ASDR/CyberSec）设计 | ❌ 只读，仅作接口参考 |
| `.nlm/` | NotebookLM 配置（5 个 DOMAIN + 2 个 v3.0 NEW global notebooks）| 由 nlm-* 技能管理，不要手动编辑 |
| `.claude/settings.local.json` | 本机权限配置 | 由 update-config 技能管理 |

## 9. 调研路由（项目专属补充）

全局已规定调研路由 + NLM 多笔记本路由（见 `~/.claude/CLAUDE.md`）。本项目额外：

- **概念查询、规范条款、算法对比** 先用 `/nlm-ask` 查本项目笔记本：
  - **7 个 DOMAIN**（按 `.nlm/config.json` `domain_notebooks` 路由，可用 `--notebook <key>` 直接命中）：
    - `safety_verification`（64 sources）— Safety analysis, formal verification, IEC 61508, SOTIF, FMEDA
    - `ship_maneuvering`（0 sources，B/G angle 的实际研究在 `local_notebook`）— MMG, hydrodynamics, motion control
    - `maritime_human_factors`（19 sources，偏稀建议升级到 `/nlm-research --depth deep`）— HMI, ToR, BNWAS
    - `maritime_regulations`（89 sources）— COLREGs, SOLAS, IMO, MASS Code
    - `colav_algorithms`（91 sources）— MPC, IvP, VO, RRT*, MPPI
    - `silhil_platform`（0 sources，v3.0 NEW）— SIL/HIL, FMI 2.0, OSP, scenario coverage, simulator V&V, DNV OSP, Kongsberg HIL
    - `cybersecurity`（0 sources，v3.0 NEW）— IACS UR E26/E27, OT zoning, DDS-Security, ECDIS signing, ASDR HMAC
  - 用法示例：`/nlm-ask --notebook silhil_platform "FMU 2.0 跨平台等价契约"`
- 命中本地笔记本但置信度 🟡 时，按全局规则问用户是否升级到 `/nlm-research --depth deep`
- ship_maneuvering 在 v3.0 评审过程已注入 89 sources（B/G angle deep research 结果）；maritime_human_factors 仍偏稀，HF 类查询建议优先升级到 `/nlm-research --depth deep`
- 船舶项目专属纪律（E1-E7 证据分级、引用格式硬约束、合规 DoD、入级影响表）见 `~/.claude/templates/marine-project-CLAUDE.md`，开始写正式 ADR 时按需引入对应章节到本文件

## 10. 阅读入口推荐顺序（v3.2-master 重构后）

新会话开始时，按需读取，**不要全部读完**。总账精简了，单文件读取成本大幅下降；按当前任务分层读：

### 10.1 顶层强制（任何 D 任务的 brainstorm/writing-plans/executing-plans 都必读）

1. 本文件 CLAUDE.md（始终在上下文中）
2. **L1 总账**：[docs/Design/00-master-plan.md](docs/Design/00-master-plan.md)（≤300 行，含进度快照 + DEMO Charter 摘要 + 文档导航 + 修订记录）
3. **L2 当前阶段 overview**：根据任务所属阶段读 [Phase 0/00-overview.md](docs/Design/Phase%200/00-overview.md) 或 [Phase 1/00-overview.md](docs/Design/Phase%201/00-overview.md)（含 D 任务索引 + 状态表 + DEMO Charter）

### 10.2 L3 双轨入口（按任务驱动）

**任务驱动**（当前在做 D{x.y} 任务）：
4. **L3a D 任务文档**：`docs/Design/Phase N/D{x.y}-*/D{x.y}-{spec,plan,report}.md`（spec → plan → report 顺序）
5. **L3b 涉及模块的 progress**：`docs/Design/TDL-Kernel/M{n}-*/M{n}-progress.md`（D 任务联动表，确认本任务对哪些模块 add Closed in）

**模块驱动**（当前调研某个 M 模块）：
4'. **L3b M 模块 spec + progress**：`docs/Design/TDL-Kernel/M{n}-*/M{n}-{spec,progress}.md`
5'. **关联 D 任务**：通过 M{n}-progress.md 表的"Currently Implementing"反向找到 L3a D 任务文档

### 10.3 架构 / 跨阶段证据入口（按需）

6. 架构主文件 v1.1.3-pre-stub：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`（按章节读，目录 + §1-4 是骨架，M1=§5, M2=§6, M3=§7, M4=§8, M6=§9, M5=§10, M7=§11, M8=§12）
7. **SIL 设计套件 v1.0-unified**：[SIL/v1.0-unified/](docs/Design/SIL/v1.0-unified/) 4 文档（01 架构 / 02 后端 / 03 前端 / 04 联调）
8. **V&V Plan**：[Phase 1/D1.5-vv-plan-scenario-qual/V&V_Plan/00-vv-strategy-v0.1.md](docs/Design/Phase%201/D1.5-vv-plan-scenario-qual/V%26V_Plan/00-vv-strategy-v0.1.md)
9. **跨团队 RFC**：`docs/Design/Cross-Team Alignment/RFC-decisions.md` + 历史 RFC 在 `docs/Design/Phase 0/Archive/Cross-Team Alignment/`
10. **HAZID**：`docs/Design/Phase 0/Archive/HAZID/RUN-001-kickoff.md` + `RUN-001-fcb-data-substitute-memo.md`

### 10.4 历史归档入口（仅审计 / 决策回溯需要时）

11. **原 1500 行 v3.2 计划归档**：`docs/Design/Architecture Design/gantt/archive/v3.2_2026-05-20_archived.md`（含完整 D 任务 DoD + finding 闭环表 + 工时附录 A-E，作权威细节参考）
12. **评审 124 findings**：`docs/Design/Phase 0/Archive/Review/2026-05-07/00-consolidated-findings.md`
13. **架构历史 v1.0-v1.1.2**：`docs/Design/Architecture Design/archive/`
14. **老 M 模块详设**：`docs/Design/Archive/Old Modules/M{n}-*/01-detailed-design.md`（TDL-Kernel/M{n}-spec.md 的指针目标）

### 10.4 审计追溯入口（CCS / DNV / 审计师）

15. `docs/Design/Architecture Design/audit/2026-04-30/00-executive-summary.md`（A 档复审落点）
16. `docs/Design/Architecture Design/audit/2026-04-30/08c-adr-deltas.md`（ADR-001/002/003）
17. `docs/Design/Architecture Design/audit/2026-04-30/10-v1.1-revision-audit.md`（5 角色复审）
18. `docs/Init From Zulip/MASS ADAS L3 Tactical Layer 战术层/`（v1.0 之前早期 4 模块原始输入）

---

## 11. 当前工作流状态（2026-05-19）

| 工作流 | 状态 | 关键产出位置 |
|---|---|---|
| **架构设计 v1.1.3-pre-stub** | ✅ 当前权威 | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` |
| **架构 v1.1.3 完整** | ⏳ 在制（D2.8 stub 7/31 → D3.8 完整 8/31）| 主文件迭代 |
| **总开发计划 v3.2-master** | ✅ 当前权威（精简总账）| [docs/Design/00-master-plan.md](docs/Design/00-master-plan.md) |
| **Phase 0/1 阶段索引** | ✅ 当前权威 | [Phase 0/00-overview.md](docs/Design/Phase%200/00-overview.md) + [Phase 1/00-overview.md](docs/Design/Phase%201/00-overview.md) |
| **TDL Kernel 模块设计** | ✅ M1-M8 spec+progress stub 完成（v3.2 重构）| [TDL-Kernel/](docs/Design/TDL-Kernel/) |
| **进度详情** | — | 见 [00-master-plan.md](docs/Design/00-master-plan.md) §当前进度快照 + [Phase 1/00-overview.md](docs/Design/Phase%201/00-overview.md) D 任务表 |
| **Phase 2/3 documents** | ⏳ 未建（D2.x/D3.x 实际启动时再建对应目录）| — |
| **Phase 4 展望（D4.1-D4.7 + D5.x）** | ⏳ 9-12月 + 2027 Q1/Q2 | 待 |

## 12. 文件版本谱系

### 12.1 架构主文档谱系

```
v1.0 (1168 行) ──── 2026-04-29 原始设计稿 [archive/]
   │ ↓ 审计（5 P0 / 18 P1 / 15 P2 / 2 P3）+ ADR-001/002/003
v1.1 (1638 行) ──── 2026-05-05 修订 [archive/]
   │ ↓ 5 角色复审 + Phase 3+6（6 新 finding）
v1.1.1 (1899 行) ── 2026-05-05 复审关闭 [archive/]
   │ ↓ 6 RFC 决议（跨团队接口锁定）
v1.1.2 (~1970 行) ─ 2026-05-06 ⬅ 当前权威 [主文件]
   │ ↓ 7 角度评审（124 finding 整合）+ v3.0 计划锁定
   │ ↓ D2.8 stub: §16 cyber + §15.0 时基 + §12.5 培训 + §12.3 心智 + §10.5 4-DOF + 4 缺失模块
v1.1.3 stub ────── 2026-07-31 D2.8 产出
   │ ↓ HAZID 8/19 完成 → 132 [TBD] 回填 + 9 章节完整化 + 算法选型矩阵 + 仲裁优先级
v1.1.3 完整 ────── 2026-08-31 D3.8 + D3.5 产出
   │ ↓ ALARP 完整 + SDLC 全表（B3 推迟到 v1.1.4）
v1.1.4 ────────── 2027 Q1（AIP 反馈后）
   │ ↓ 实船试航迭代 / 多船型扩展（拖船 RUN-002 + 渡船 RUN-003）
v1.2.x (长期) ──── 多船型成熟 + 认证级实船 D5.x 启动
```

### 12.2 8 月开发计划谱系（v3.0 → v3.2-master）

```
v1.0 ────────────── 2026-04-20 原始 4 阶段计划
   │
v2.0 (671 行) ──── 2026-05-07 8 个月完整计划 [gantt/archive/v2.0_*]
   │ ↓ 7 角度评审 124 finding 整合
v3.0 ──────────── 2026-05-08 32 D 编号 + 三档 DEMO + 3 新角色
   │ ↓ SIL 框架架构 patch + 选项 D 混合 + Web HMI
v3.1 ──────────── 2026-05-09 累计缺口 -32~-34 pw（用户授权）
   │ ↓ Phase 1 进度快照 + DEMO-2 GAP 标注
v3.2 (~1500 行) ── 2026-05-20 [gantt/archive/v3.2_2026-05-20_archived.md]
   │ ↓ 文档重构：1500 行 → 总账 + Phase overview + D 任务子文档 + TDL-Kernel 模块文档双轨树
   │ ↓ 编号重排 D1.3a/b/c → D1.3.1/2/3
v3.2-master ───── 2026-05-20 ⬅ 当前权威 [docs/Design/00-master-plan.md]
                    精简 ≤300 行；详细 D 任务规格散布于 Phase N/ 子目录
```

## 13. Git 分支与 Worktree 规范（2026-05-08 确立）

### 13.1 默认分支

| 分支 | 角色 | 规则 |
|---|---|---|
| `main` | 集成分支（GitHub default） | **禁止直接 commit**；所有改动通过 feat/* 分支 merge 进入 |

### 13.2 功能分支命名

每个 D-task 对应一个分支，格式：

```
feat/d{阶段}.{序号}-{短描述}
```

示例：`feat/d1.3b-scenario-mgmt` / `feat/d2.1-m1-odd-manager` / `feat/d3.1-m4-behavior-arbiter`

- **一个 D-task = 一个 branch**，不同 D-task 不共用
- 从 `main` 切出，merge 回 `main` 后立即删除（本地 + remote）
- 禁止保留"备用"分支——已 merge 历史在 git log 中，不需要分支指针

### 13.3 Worktree 规范

- 路径统一：`.worktrees/{branch-slug}/`（slug = 分支名 `/` → `-`）
- 由 `superpowers:using-git-worktrees` 统一创建和清理；**不要手动 mkdir/rm**
- Worktree 与分支同生命周期：分支删除时同步 `git worktree remove`

### 13.4 subagent 临时分支

- `superpowers:subagent-driven-development` 自动创建 `claude/{random-name}` 分支 + worktree
- 任务完成后由 `superpowers:finishing-a-development-branch` 自动清理
- **禁止手动保留 `claude/*` 分支**；若发现残留且有独立 commit，重命名为 `feat/d*` 再评估

### 13.5 何时可以删分支（清理判据）

```bash
git log --oneline {branch} ^main   # 输出为空 → 0 个独立 commit → 可直接删除
```

### 13.6 当前活跃分支（2026-05-25）

| 分支 | 状态 | 对应 D-task |
|---|---|---|
| `main` | ✅ 集成，GitHub default | D0–D2.8 全部已入；D2.1–D2.4 report+evidence 已入 |
| `remotes/gitlab/l3-tdl` | ✅ 与 main 同步 | GitLab mirror |

> 所有 Phase 0/1 feature 分支（d1.3b.1 / d1.3b.3 / d1.3b.3-web-hmi / sil-demo1-head-on / d3.1-m4-behavior-arbiter / d3.3b-m7-sotif / d2.1-m1-odd-hardening）均已合入 main 并删除。远程 d2.7/d2.8 分支已清理。
> 另存在 `worktree-agent-*` 临时分支若干，由 subagent 框架自动管理，**不要手动操作**。

---

### 12.3 累计 finding 状态

**架构 v1.1.2 时点**（5 角色复审 + 6 RFC 后）：
- P0 = 0（5 全部关闭）/ P1 = 0（21 全部关闭）/ P2 = 0（18 全部关闭）/ P3 = 2（deferred 99-followups）
- 跨团队 = 6 RFC 全部批准

**7 角度评审 2026-05-07 后**（叠加新发现）：
- 30 P0 / 52 P1 / 29 P2 → 跨角度去重 15 个根因簇 → 124 finding 入 v3.0 D-list 闭环
- 12 项 must-fix 在 D0（5/8–5/12）关闭 / 12 项 phase-1 fix 在 D1.x 关闭 / 其余在 D2.x/D3.x 闭环
- 详见 `docs/Design/Review/2026-05-07/00-consolidated-findings.md` §10 owner 表 + v3.0 附录 D Findings Closure Map

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
