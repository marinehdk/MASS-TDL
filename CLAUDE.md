# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> 本项目工作纪律继承全局 `~/.claude/CLAUDE.md`（Karpathy 四则 + 调研触发器 + 置信度标注 + 研究路由）。本文件只补充全局规则未覆盖的项目专属信息。**当本文件与全局规则冲突时，全局规则优先。**

---

## 1. 项目当前状态（强制阅读 — 2026-05-08 更新，对齐 8 月计划 v3.0）

### 1.1 阶段
- **实现阶段进行中**：**8 月计划 v3.0 已锁定（2026-05-08）**；D0 must-fix sprint（5/8–5/12）已启动源代码 + 测试 + CI 并行演进（`src/`、`tests/`、`.gitlab-ci.yml` 已建立）；5/13 起 Phase 1 D1.x 工程基础正式起跑
- 涉及"跑一下/编译/测试"的请求：参考 v3.0 主计划对应 D 编号判断（D0 范围：pure-logic Python；D1.1 起：ROS2 wrapping）
- **判定基准**：v3.0 plan 32 个 D 编号（D0–D4.7）是所有实现决策的权威参照
- **三档强制 DEMO**：DEMO-1 (6/15 Skeleton Live) / DEMO-2 (7/31 Decision-Capable) / DEMO-3 (8/31 Full-Stack with Safety + ToR) —— 不可取消；详见 v3.0 §10/§11/§12

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
   ├── D0 Pre-Kickoff Must-Fix Sprint                   ← ⏳ 5/8–5/12（关闭 11 must-fix）
   ├── HAZID RUN-001 kickoff                            ← ⏳ 5/13 第 ① 次会议
   ├── Phase 1 工程基础 + V&V 基线 (D1.1–D1.8)         ← ⏳ 5/13–6/15 → DEMO-1
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

### 1.3 当前权威架构文件

- **架构主文件**：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`（**v1.1.2 当前权威，2026-05-06**）
- **8 月开发计划主文件**：`docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`（**v3.0 当前权威，2026-05-08，已锁定**）
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
- 架构报告（**v1.1.2 当前权威**）与早期研究稿（`docs/Doc From Claude/2026-04-2*`）+ 归档版本（`archive/v{X}_*_archived.md`）冲突时，以**当前主文件**为准；但若主文件本身有内部矛盾，**停下来询问用户**，不要静默择一。
- 引用编号 `[Rx]` 是架构文档的硬约束。新增引用须分配新编号并加入参考文献章节，**禁止**裸贴 URL 或 "据某研究"。
- **修订版本管理**（v1.0 → v1.1 → v1.1.1 → v1.1.2 → 未来 v1.1.3）：
  - 主架构文件名永远是 `MASS_ADAS_L3_TDL_架构设计报告.md`（无版本后缀）；版本信息在文件内部头表
  - 旧版本归档到 `archive/v{X.Y.Z}_{YYYY-MM-DD}_archived.md`（不可改）
  - 每次升级版本须在文件附录补 v{X-1} → v{X} 修订记录

## 8. 文件夹语义（决定哪里可写）

| 路径 | 性质 | 可改？ |
|---|---|---|
| `docs/Design/Architecture Design/` | 架构主文件 v1.1.2 + README + audit/ | ✅ 主战场 |
| `docs/Design/Architecture Design/archive/` | 历史归档 v1.0/v1.1/v1.1.1 | ❌ 只读历史 |
| `docs/Design/Architecture Design/gantt/` | 8 月开发计划 v3.0（主文件）+ HTML 副本 | ✅ 主战场 |
| `docs/Design/Architecture Design/gantt/archive/` | 计划历史归档 v2.0 | ❌ 只读历史 |
| `docs/Design/Detailed Design/` | 8 模块详细设计（M1–M8）+ D0/D1.x/D2.x/D3.x sprint spec（v3.0 起）| ✅ 主战场 |
| `docs/Design/Cross-Team Alignment/` | RFC 决议（v3.0 起 RFC-007/008/009 加入，预计 7+ RFC）| ✅ 主战场 |
| `docs/Design/HAZID/` | HAZID 工作包 + 运行包 + RUN-001-fcb-data-substitute-memo | ✅ 主战场 |
| `docs/Design/Review/2026-05-07/` | 7 角度评审报告 + 跨角度整合 + 用户决策 §13 | ⚠️ 仅追加修订记录，**不改主体** |
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

## 10. 阅读入口推荐顺序（v3.0 锁定后）

新会话开始时，按需读取，**不要全部读完**——v3.0 主计划 ~1500 行，架构报告 v1.1.2 ~30k tokens，仅按当前任务读对应章节：

### 10.1 顶层强制（任何 D 任务的 brainstorm/writing-plans/executing-plans 都必读）

1. 本文件 CLAUDE.md（始终在上下文中）
2. **8 月开发计划主文件 v3.0**：`docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`，重点读：
   - §0 工时数学（87.0 vs 84.0 闭口路径）
   - 当前任务对应的 D 章节（§2 D0 / §3 Phase 1 / §4 Phase 2 / §5 Phase 3 / §6 Phase 4）
   - §10 Demo Milestones / §11 D-task Demo Charter 模板 / §12 Demo-Driven Sync
   - 附录 A（工作量汇总）+ B（里程碑）+ D（Findings Closure Map）+ E（角色 + 资源日历）
3. **评审整合清单**：`docs/Design/Review/2026-05-07/00-consolidated-findings.md`（含 §13 用户决策：8/31 不延期 / 12 月实船降级 / 创建 2 个 global notebooks）

### 10.2 通用入口（按需）

4. `docs/Design/Architecture Design/README.md`（架构文件索引 + 当前权威指引）
5. 架构报告 v1.1.2 **目录与第 1–4 章**（背景、决策、ODD 框架、模块全景）— 约 850 行；**注意读取头表元数据 + 修订记录**
6. 当前任务涉及模块的架构章节（M1=§5, M2=§6, M3=§7, M4=§8, M6=§9, M5=§10, M7=§11, M8=§12）

### 10.3 详细设计入口

7. **8 模块详细设计**：`docs/Design/Detailed Design/M{N}/01-detailed-design.md`
8. **跨团队接口契约**：`docs/Design/Cross-Team Alignment/RFC-decisions.md`（6 RFC 决议；v3.0 D0.2/D3.9 起加 RFC-007/008/009）
9. **HAZID 校准任务**：`docs/Design/HAZID/RUN-001-kickoff.md` + `RUN-001-fcb-data-substitute-memo.md`（D0.2 产出）

### 10.4 评审与决策回溯入口（v3.0 起新增）

10. **7 角度评审报告**：`docs/Design/Review/2026-05-07/{A-G}-*.md`（按当前任务关联 angle 选读）
11. **架构 v1.1.3 在制**：D2.8 stub（7/31）/ D3.8 完整（8/31）的产出会在 `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` 内迭代；同时关注 `docs/Design/V&V_Plan/` `docs/Design/SIL/` `docs/Design/HF/` `docs/Design/Cert/` `docs/Design/ConOps/` `docs/Design/Safety/` `docs/Design/Cybersecurity/` 7 个 v3.0 NEW 子目录

### 10.5 审计追溯入口（CCS / DNV / 审计师）

12. `docs/Design/Architecture Design/audit/2026-04-30/00-executive-summary.md`（A 档复审落点）
13. `docs/Design/Architecture Design/audit/2026-04-30/08c-adr-deltas.md`（ADR-001/002/003）
14. `docs/Design/Architecture Design/audit/2026-04-30/10-v1.1-revision-audit.md`（5 角色复审）

### 10.6 历史回溯入口（仅审计需要时）

15. `docs/Design/Architecture Design/archive/v1.0_*_archived.md` / `v1.1_*` / `v1.1.1_*`（架构历史快照）
16. `docs/Design/Architecture Design/gantt/archive/v2.0_2026-05-07_archived.md`（计划 v2.0 历史）
17. `docs/Init From Zulip/MASS ADAS L3 Tactical Layer 战术层/`（v1.0 之前的早期 4 模块原始输入）

---

## 11. 当前工作流状态（2026-05-08）

| 工作流 | 状态 | 关键产出位置 |
|---|---|---|
| **架构设计 v1.1.2** | ✅ 当前权威 | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` |
| **架构 v1.1.3** | ⏳ 在制（D2.8 stub 7/31 → D3.8 完整 8/31）| 主文件迭代修订 |
| **审计** | ✅ v1.0–v1.1.2 全链路完成 | `docs/Design/Architecture Design/audit/2026-04-30/` |
| **详细设计**（8 模块）| ✅ 全部正式 | `docs/Design/Detailed Design/M{1-8}/01-detailed-design.md` |
| **跨团队 RFC** | ✅ 6 RFC 已批准；v3.0 起加 RFC-007/008/009 | `docs/Design/Cross-Team Alignment/RFC-decisions.md` |
| **7 角度多角度评审** | ✅ 完成 2026-05-07（30 P0 / 52 P1 / 29 P2 → 124 finding 整合）| `docs/Design/Review/2026-05-07/00-consolidated-findings.md` |
| **8 月开发计划 v3.0** | ✅ 锁定 2026-05-08 | `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` |
| **D0 must-fix sprint** | ⏳ 5/8–5/12（11 must-fix + RFC-007/009 + 工时表 v2.1 + HTML 同步）| `docs/Design/Detailed Design/D0-*` |
| **HAZID RUN-001** | ⏳ 5/13 第 ① 次会议；议程已重排（≥ 2 个 full-day workshop + CCS 持续介入）；8/19 完成 | `docs/Design/HAZID/` |
| **Phase 1 (D1.1–D1.8)** | ⏳ 5/13–6/15 → **DEMO-1 Skeleton Live 6/15** | `docs/Design/{V&V_Plan,SIL,Cert,ConOps}/` |
| **Phase 2 (D2.1–D2.8)** | ⏳ 6/16–7/31 → **DEMO-2 Decision-Capable 7/31** | 各模块 + `docs/Design/{HF,Safety/HARA,Safety/FMEDA}/` |
| **Phase 3 (D3.1–D3.9)** | ⏳ 7/13–8/31 → **DEMO-3 Full-Stack with Safety + ToR 8/31** | 各模块 + `docs/Design/Cybersecurity/` |
| **HIL 测试 (D4.1/D4.2)** | ⏳ 9–11月，硬件 7/13 下单（800h+ 目标）| 待 `docs/Test Plan/HIL/` |
| **SIL 2 第三方 (D4.3)** | ⏳ TÜV/DNV/BV 接洽 9 月起 | 待 `docs/Cert/SIL2/` |
| **CCS AIP 提交 (D4.4)** | ⏳ 11月 | 待 `docs/Cert/AIP/` |
| **FCB 实船 (D4.5)** | ⏳ 12月 **非认证级技术验证**（用户决策 2026-05-07）| 待 `docs/Test Plan/SeaTrial/` |
| **船长/ROC 训练 (D4.5')** | ⏳ 11月模拟器认证 | `docs/Design/HF/` 延伸 |
| **RL 对抗 (D4.6, B2 后移)** | ⏳ 10–12月 | 待 `docs/Design/SIL/RL/` |
| **4 缺失模块完整 (D4.7, B4)** | ⏳ 9–10月条件触发 | 待 `docs/Design/{ENC,ParamStore,EnvValidator,BNWAS}/` |
| **多船型扩展 RUN-002/003** | ⏳ 拖船 ≥6w / 渡船 ≥6w（10–12月）| `docs/Design/HAZID/RUN-002,003/` |
| **认证级实船 D5.x** | ⏳ 2027 Q1/Q2 AIP 受理后启动 | 不在本计划 |

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

### 12.2 8 月开发计划谱系（v3.0 起）

```
v1.0 ────────────── 2026-04-20 原始 4 阶段计划
   │
v2.0 (671 行) ──── 2026-05-07 8 个月完整计划，含 v1.1.2 接口锁定 [gantt/archive/v2.0_*]
   │ ↓ 7 角度评审 124 finding 全量整合
   │ ↓ 用户决策: A1+B2+B3 + 8/31 不延期 + 12 月实船降级 + 2 个新笔记本
v3.0 (~1500 行) ── 2026-05-08 ⬅ 当前权威 [gantt/主文件]
                    32 个 D 编号（D0–D4.7）+ 三档 DEMO + Demo Charter 模板
                    + Demo-Driven Sync + 3 新角色（V&V FTE / 安全外包 / HF 外包）
                    + 87.0 人周工作 / 84.0 产能 / -3.0 闭口
```

### 12.3 累计 finding 状态

**架构 v1.1.2 时点**（5 角色复审 + 6 RFC 后）：
- P0 = 0（5 全部关闭）/ P1 = 0（21 全部关闭）/ P2 = 0（18 全部关闭）/ P3 = 2（deferred 99-followups）
- 跨团队 = 6 RFC 全部批准

**7 角度评审 2026-05-07 后**（叠加新发现）：
- 30 P0 / 52 P1 / 29 P2 → 跨角度去重 15 个根因簇 → 124 finding 入 v3.0 D-list 闭环
- 12 项 must-fix 在 D0（5/8–5/12）关闭 / 12 项 phase-1 fix 在 D1.x 关闭 / 其余在 D2.x/D3.x 闭环
- 详见 `docs/Design/Review/2026-05-07/00-consolidated-findings.md` §10 owner 表 + v3.0 附录 D Findings Closure Map
