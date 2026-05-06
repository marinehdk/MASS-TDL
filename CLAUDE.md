# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> 本项目工作纪律继承全局 `~/.claude/CLAUDE.md`（Karpathy 四则 + 调研触发器 + 置信度标注 + 研究路由）。本文件只补充全局规则未覆盖的项目专属信息。**当本文件与全局规则冲突时，全局规则优先。**

---

## 1. 项目当前状态（强制阅读 — 2026-05-06 更新）

### 1.1 阶段
- 仍是**设计阶段**：仓库 100% 为 `docs/`，无源代码、无构建、无测试、无 CI、无运行时
- 任何"跑一下/编译一下/测试一下"的请求都必须先回到设计文档

### 1.2 路线进度

```
架构设计 (v1.0 → v1.1 → v1.1.1 → v1.1.2)              ← ✅ 完成（接口跨团队锁定）
   │
   ├── 详细功能设计（M1–M8 全部正式）                    ← ✅ 完成
   ├── 跨团队接口对齐（6 RFC 全部已批准）                 ← ✅ 完成
   ├── HAZID 校准启动（RUN-001 kickoff 包就位）          ← ⏳ 进行中（5/13 第 1 次会议）
   │
   ├── 实现阶段（C++/Python 编码 + 单元测试 + HIL）       ← ⏳ 8 个团队即将开工
   ├── 实船试航（FCB ≥ 50 nm + ≥ 100 h）                 ← 计划周 5–6（2026-06）
   ├── CCS i-Ship AIP 申请（v1.1.3 + HAZID 完成后）      ← 计划 2026-08–09
   └── 多船型扩展（拖船 / 渡船）                          ← 计划 2026 Q4
```

### 1.3 当前权威架构文件

- **主文件**：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`（**v1.1.2**，2026-05-06）
- 历史版本（v1.0 / v1.1 / v1.1.1）已归档到 `archive/`，**不要从归档读取，永远引用主文件**
- 文件索引：`docs/Design/Architecture Design/README.md`

### 1.4 架构文档可质疑性

v1.1.2 已通过 5 角色复审 + DNV 验证官独立挑战 + 6 RFC 跨团队评审，**但仍非"终态"**：
- 仍有 [HAZID 校准] 标注的参数（25+ 项）须 FCB 实船试航后回填 → v1.1.3
- 跨团队改造完成后可能引发 IDL 微调 → v1.1.3 或 v1.2
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

**模块通信**：发布-订阅（推荐 ROS2 DDS），消息强类型 + `stamp` + `confidence ∈ [0,1]` + `rationale` 字段强制。任何模块设计若绕过总线直接调用其他模块，须在 PR 描述里显式辩护。

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
| `docs/Design/Architecture Design/` | 架构主文件 + README + 配套（audit/）| ✅ 主战场（含 v1.1.2 主架构文件）|
| `docs/Design/Architecture Design/archive/` | 历史归档 v1.0/v1.1/v1.1.1 | ❌ 只读历史，不修改 |
| `docs/Design/Detailed Design/` | 8 模块详细设计（M1–M8 各目录）| ✅ 主战场 |
| `docs/Design/Cross-Team Alignment/` | 6 RFC + RFC 决议归档 | ✅ 主战场 |
| `docs/Design/HAZID/` | HAZID 工作包 + 运行包 | ✅ 主战场 |
| `docs/Doc From Claude/` | 早期 Claude 研究稿（v1.1/v1.2/patch-d 等）| ⚠️ 仅追加新版本，**不改旧文件** |
| `docs/Init From SINAN/` | 外部团队（SINAN）的输入 | ❌ 只读 |
| `docs/Init From Zulip/` | 其他层（L1/L2/L4/L5/Multimodal Fusion/Checker/ASDR/CyberSec）的设计 | ❌ 只读，作为接口参考 |
| `.nlm/` | NotebookLM 配置（含 5 个领域笔记本 ID）| 由 nlm-* 技能管理，不要手动编辑 |
| `.claude/settings.local.json` | 本机权限配置 | 由 update-config 技能管理 |

## 9. 调研路由（项目专属补充）

全局已规定调研路由 + NLM 多笔记本路由（见 `~/.claude/CLAUDE.md`）。本项目额外：

- **概念查询、规范条款、算法对比** 先用 `/nlm-ask` 查本项目的 5 个领域笔记本（safety_verification / ship_maneuvering / maritime_human_factors / maritime_regulations / colav_algorithms），命中则不必触发外网调研
- 命中本地笔记本但置信度 🟡 时，按全局规则问用户是否升级到 `/nlm-research --depth deep`
- 船舶项目专属纪律（E1-E7 证据分级、引用格式硬约束、合规 DoD、入级影响表）见 `~/.claude/templates/marine-project-CLAUDE.md`，开始写正式 ADR 时按需引入对应章节到本文件

## 10. 阅读入口推荐顺序（v1.1.2 后更新）

新会话开始时，按需读取，**不要全部读完**——架构报告（v1.1.2）约 30k tokens，仅在需要某模块细节时读对应章节：

### 10.1 通用入口（所有读者）

1. 本文件 CLAUDE.md（始终在上下文中）
2. `docs/Design/Architecture Design/README.md`（架构文件索引 + 当前权威指引）
3. 架构报告**目录与第 1–4 章**（背景、决策、ODD 框架、模块全景）— 约 850 行；**注意读取头表元数据 + 修订记录**
4. 当前任务涉及模块的章节（M1=§5, M2=§6, M3=§7, M4=§8, M6=§9, M5=§10, M7=§11, M8=§12）

### 10.2 详细设计阶段入口（v1.1.2 之后新增）

5. **8 模块详细设计**：`docs/Design/Detailed Design/M{N}/01-detailed-design.md`（每模块 ~1000 行 spec）
6. **跨团队接口契约锁定证据**：`docs/Design/Cross-Team Alignment/RFC-decisions.md`（6 RFC 决议）
7. **HAZID 校准任务**：`docs/Design/HAZID/RUN-001-kickoff.md`（参数清单 + 12 周时间表）

### 10.3 审计追溯入口（CCS / DNV / 审计师）

8. `docs/Design/Architecture Design/audit/2026-04-30/00-executive-summary.md`（A 档复审落点）
9. `docs/Design/Architecture Design/audit/2026-04-30/08c-adr-deltas.md`（ADR-001/002/003）
10. `docs/Design/Architecture Design/audit/2026-04-30/10-v1.1-revision-audit.md`（5 角色复审）

### 10.4 历史回溯入口（仅审计需要时）

11. `archive/v1.0_2026-04-29_archived.md` / `v1.1_*_archived.md` / `v1.1.1_*_archived.md`（历史快照）
12. `docs/Init From Zulip/MASS ADAS L3 Tactical Layer 战术层/`（v1.0 之前的早期 4 模块原始输入：Risk_Monitor / COLREGs_Engine / Avoidance_Planner / Target_Tracker）

---

## 11. 当前工作流状态（2026-05-06）

| 工作流 | 状态 | 关键产出位置 |
|---|---|---|
| **架构设计** | ✅ v1.1.2（接口跨团队锁定）| `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` |
| **审计** | ✅ v1.0 → v1.1 → v1.1.1 → v1.1.2 全链路完成 | `docs/Design/Architecture Design/audit/2026-04-30/` |
| **详细设计**（8 模块）| ✅ 全部正式 | `docs/Design/Detailed Design/M{1-8}/01-detailed-design.md` |
| **跨团队对齐**（6 RFC）| ✅ 全部已批准 | `docs/Design/Cross-Team Alignment/RFC-decisions.md` |
| **HAZID 校准** | ⏳ RUN-001 启动包就位（5/13 第 1 次会议）| `docs/Design/HAZID/RUN-001-kickoff.md` |
| **实施阶段**（编码）| ⏳ 8 个团队即将开工 | 待 `docs/Implementation/` |
| **HIL 测试** | ⏳ 测试 plan 未启动 | 待 `docs/Test Plan/` |
| **CCS AIP 申请** | ⏳ 待 HAZID 完成（计划 2026-08–09）| 待 `docs/Certification/` |
| **多船型扩展** | ⏳ 待 FCB 完成 | 待 `docs/Vessel Adaptations/` |

## 12. 文件版本谱系（v1.0 → v1.1.2 → 未来）

```
v1.0 (1168 行) ──── 2026-04-29 原始设计稿 [archive/]
   │ ↓ 审计（5 P0 / 18 P1 / 15 P2 / 2 P3）+ ADR-001/002/003
v1.1 (1638 行) ──── 2026-05-05 修订 [archive/]
   │ ↓ 5 角色复审 + Phase 3+6（6 新 finding）
v1.1.1 (1899 行) ── 2026-05-05 复审关闭 [archive/]
   │ ↓ 6 RFC 决议（跨团队接口锁定）
v1.1.2 (~1970 行) ─ 2026-05-06 ⬅ 当前权威 [主文件]
   │ ↓ HAZID 校准（5/13–8/19）
v1.1.3 (待发布) ── 2026-08-19 HAZID 校准回填
   │ ↓ CCS i-Ship AIP 申请
v1.2.x (长期) ──── 实船试航迭代 / 多船型扩展（拖船 / 渡船）
```

**累计 finding 状态**（v1.1.2）：
- P0 = 0（5 全部关闭）
- P1 = 0（21 全部关闭）
- P2 = 0（18 全部关闭）
- P3 = 2（deferred 到 99-followups）
- 跨团队接口 = 6 RFC 全部批准
