# MASS ADAS L3 战术决策层 · 总开发计划 · Master Plan

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-PLAN-MASTER |
| 版本 | **v3.2-master**（2026-05-20）|
| 性质 | **总账**（≤300 行精简版）；详细 D 任务规格在 Phase 0/1/2/3 子文档；模块设计在 [TDL-Kernel/](TDL-Kernel/) |
| 上一版本 | v3.2 单一 1500 行 gantt 文件（已归档 `Architecture Design/gantt/archive/v3.2_2026-05-20_archived.md`）|
| 计划周期 | 2026-05-08 → 2026-12-31（前 4 月硬承诺 + 后 4 月展望）|
| 架构基线 | [Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md](Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) **v1.1.3-pre-stub** |
| 团队 | 2 人原班 + V&V 工程师 FTE + 安全外包 + HF 外包 |

---

## 文档导航

```
docs/Design/
├── 00-master-plan.md ◄ 你在这里（总账）
│
├── Phase 0/00-overview.md           D0 sprint 索引 + finding 闭环
├── Phase 1/00-overview.md           D1.x + DEMO-1 Charter + 进度快照
│   └── D{x.y}-*/{spec,plan,report}.md  D 任务详情
├── Phase 2/00-overview.md           D2.x + DEMO-2 Charter + 进度快照（🟡 进行中）
│   └── D{x.y}-*/{spec,plan,report}.md  D 任务详情（D2.7/D2.8 ✅，D2.1-D2.5 🟡）
├── Phase 3/  (待建)                 同上
│
├── TDL-Kernel/                      第二套文档树（模块设计导向）
│   ├── 00-tdl-kernel-overview.md
│   └── M{1..8}-XXX/{spec, progress}.md
│
├── Architecture Design/             架构主文件 + 审计 + 历史归档
├── SIL/v1.0-unified/                SIL 4 文档套件（01 架构 / 02 后端 / 03 前端 / 04 联调）
├── Cert/  ConOps/  Safety/  HF/     跨阶段证据
├── Cross-Team Alignment/            RFC 决议
└── Archive/                         旧 Old Modules + 其他 archive（只读）
```

**文档双轨**：任务开发流（`Phase N/D{x.y}/`）↔ 模块设计流（`TDL-Kernel/M{n}/`），通过 [TDL-Kernel/M{n}/M{n}-progress.md](TDL-Kernel/) 的"Closed in / Currently Implementing / Blocks D 任务"联动表互联。详见 [CLAUDE.md](../../CLAUDE.md) §7.1 文档分层规则。

---

## 8 个月月度地图

| 月份 | 阶段 | 核心内容 | Demo | 性质 |
|---|---|---|---|---|
| 5 月 | D0 + Phase 1 起 | Pre-Kickoff Must-Fix Sprint（5/8–12）+ 工程基础 + V&V 基线 | — | 硬承诺 |
| 6 月 | Phase 1 → 2 | SIL 框架 + V&V Plan + 场景 schema + Cert tracking + ConOps stub | 🎬 **DEMO-1（6/15）** | 硬承诺 |
| 7 月 | Phase 2 → 3 | M1/M2/M3/M6 + 船长访谈 + HARA + v1.1.3 stub | 🎬 **DEMO-2（7/31）** | 硬承诺 |
| 8 月 | Phase 3 | M4/M5/M7/M8 + HAZID 8/19 + SIL 1000 + v1.1.3 完整化 | 🎬 **DEMO-3（8/31）** | 硬承诺 |
| 9–12 月 | Phase 4 | HIL + SIL 2 接洽 + AIP + RL 对抗 + 4 缺失模块 + 12 月 FCB 非认证级试航 | — | 展望 |

> **三档 DEMO 不可取消** — 任一不达标 = 项目 P0 escalation，详见 v3.2 archived plan §10。

---

## 工作量与产能（v3.0/v3.1/v3.2 闭口）

| 项目 | 人周 |
|---|---|
| v3.0 前 4 月工作量（D0+1+2+3）| 87.0 |
| D0 sign-off 修订（M4 IvP 自实现 +1.5）| +1.5 |
| v3.0 小计 | **88.5** |
| v3.1 增量（选项 D + DNV 工具链 + Web HMI + D1.3.3 FMI bridge 等）| +~28 |
| v3.1 小计 | **116~118** |
| 产能（原 2 人 63 + V&V FTE 16 + 安全 3.5 + HF 1.5）| 84.0 |
| **缺口** | **-32~-34** |

**用户授权（2026-05-09）**："工时缺口不用担心，核心目标完整实现避免重构"。闭环路径：D3.6 1000 场景目标延 9/30 / D1.3.3 限 own-ship FMI / B4 触发推 4 缺失模块到 Phase 4 / 用户并行开发。

---

## DEMO 三档（详见各 Phase overview Charter 章节）

### DEMO-1 Skeleton Live（2026-06-15）
端到端 5 min：AIS 回放 + Mock M2 + Crossing 场景 + SAT-1 wireframe + Smoke 10 + ConOps/V&V Plan/Sim Qual PDF。详见 [Phase 1/00-overview.md](Phase%201/00-overview.md#demo-1-charter2026-06-15)。

### DEMO-2 Decision-Capable（2026-07-31）
端到端 8 min：50 综合场景 + ODD 切换 + SAT-2 全展（M6 5 层 + M4 IvP）+ 船长访谈 + HARA + v1.1.3-stub + KPI 仪表盘。**当前 P0 风险**：Engineer 视图 4 组件双端真空，见下 §当前进度。

### DEMO-3 Full-Stack with Safety + ToR（2026-08-31）
端到端 12 min：1000 场景 + 8h 集成 + M7 Doer-Checker veto + ToR 接管 + MRC + v1.1.3 完整 + 培训矩阵。CCS AIP 11 月提交。

---

## 当前进度快照（2026-05-22）

### Phase 0
✅ **全闭**（D0 11 项 must-fix + RFC-007/009 + 工时表 v2.1）。详见 [Phase 0/00-overview.md](Phase%200/00-overview.md)。

### Phase 1（DEMO-1 还 ~3.5 周）
- ✅ 完整：D1.1 ROS2 ws / D1.2 CI-CD / D1.3.1 MMG+AIS / **D1.3.1' Sim Qual（3 参考解 + 100次重跑 + TCL-3 PASS）** / D1.3.2-integration L3 pipeline / **D1.4 编码规范 v1.2** / D1.5 V&V Plan / D1.8 cert+ConOps stub
- 🟡 部分：D1.3.2.1（22 Imazu ✅，Cerberus mock known gap）/ D1.3.2.3（Web HMI SAT-1 ✅，SAT-2/3 handler 待 D3.4）/ D1.3.3（Humble 容器 ✅，dds-fmu 未集成）/ **D1.6**（schema baseline ✅，traceability-matrix.csv 缺）/ **D1.7**（6维度 rubric ✅，Group A1 关闭前升 ✅）
- 🔴 未启：D1.3.2.2 AIS-driven（**v3.2 决策推迟 Phase 4**）

### Phase 2（DEMO-2 还 ~10 周）
- ✅ 完整：**D2.7**（HARA 32 危险源 + FMEDA M1 v1.0 20 失效模式 + SIF 全覆盖；C P0-C-1(b) + C P1-C-8 关闭）/ **D2.8**（架构 v1.1.3-stub §16–§22 全部到位，附录 F 退役）
- 🟡 进行中：**D2.3**（M3 实装+测试✅，缺 report）/ **D2.4**（IDL+Arrow评分管线✅，缺 chain截图）/ **D2.5**（tools/vv 11脚本+前端4组件✅，KPI全待SIL stack实测）
- 🟡 spec+plan 完整：**D2.1**（M1 ODD FSM 设计+FMEDA v0.1，缺 evidence）/ **D2.2**（M2 5轨道设计详尽，缺 evidence）
- 🔴 框架就绪等启动：**D2.6**（16文件骨架✅，访谈实数据等 6/16 HF 外包 onboard）
- 详见 [Phase 2/00-overview.md](Phase%202/00-overview.md)

### TDL Kernel 模块（DEMO-2 视角）
- ✅ M1 / M2 / M3 / M6 / M7 / M8 有真实 ROS topic 发布（SAT-1 级）；M1(D2.1)/M2(D2.2)/M3(D2.3) 已完成 Phase 2 决策级实装设计
- 🔴 **M4 未发 SAT-2 ivp_contributions** / **M5 未发 SAT-3 trajectory_candidates** / **M8 未发 SAT-2/SAT-3/SOTIF metrics 三桥接 topic** / **前端 useFoxgloveLive 缺 3 handler**

### DEMO-2 P0 冲刺 GAP（按急迫度，2026-05-22 更新）

**🔴 未完成（仍需执行）**

| # | GAP 项 | 工时 | 截止 | 备注 |
|---|---|---|---|---|
| 1 | **M4 IvP 提前出 stub**（分段线性兜底，ivp_contributions[]）| 4.0 pw | **7/26** | D3.1 提前；7/13 必须开工 |
| 2 | **M5 BC-MPC 提前出 stub**（线性 Nomoto + 13 弧 trajectory_candidates[]）| 6.5 pw | **7/27** | D3.2 提前；7/13 必须开工；初版允许 Nomoto 兜底 |
| 3 | **M8 增发 SAT-2/3/SOTIF 3 topic + IDL**（从 D3.4 拆出）| 1.5 pw | 7/31 | 前端 4 组件双端真空根因 |
| 4 | **前端 useFoxgloveLive 增 3 handler**（IvP / Trajectory / SOTIF）| 0.5 pw | 7/31 | 归 D1.3.2.3；依赖 #3 |
| 5 | **D2.4 chain_screenshots ≥10 场景**（Playwright T10）| 0.5 pw | 7/31 | DEMO-2 ColregsRationaleTree P0；新增项 |
| 6 | **M6 colregs_chain C++ 填充 + colcon 测试通过**（SIL-6 关闭）| 0.5 pw | 7/31 | D2.4 内吸收 |
| 7 | **D1.7 6 维度 rubric Group A1 关闭**（D2.4 评分验收依赖）| 1.5 pw | 7/31 | 🟡 rubric 文档已产出，待 D2.4 Group A1 执行 |
| 8 | **D1.6 场景 schema traceability-matrix.csv**（HAZID 干系人展示）| 2.0 pw | 7/31 | 🟡 schema baseline ✅，matrix 缺失 |

**✅ 已关闭（从 GAP 移除）**

| # | GAP 项 | 关闭日期 | 关闭方式 |
|---|---|---|---|
| — | D2.7 HARA ≥30 危险源 + FMEDA M1 | 2026-05-22 | HARA 32 + FMEDA 20 + SIF 全覆盖 ✅ |
| — | D1.3.1' Sim Qual 3 参考解（CCS 现场展示）| 2026-05-20 | 3 参考解 pytest + 100 次重跑 + TCL-3 PASS ✅ |
| — | D2.8 架构 v1.1.3 stub | 2026-05-22 | §16–§22 全部到位 ✅ |

**合计剩余**：~16.5 pw；距 7/31 剩 ~10 周；**关键路径：M4/M5 必须 7/13 开工，否则 10.5 pw 无法收口**。

---

## 编号映射（v3.2 重排：原 a/b/c → 数字）

| 旧 | 新 |
|---|---|
| D1.3a | D1.3.1 |
| D1.3b | D1.3.2 |
| D1.3b.1 | D1.3.2.1 |
| D1.3b.2 | D1.3.2.2 |
| D1.3b.3 | D1.3.2.3 |
| D1.3c | D1.3.3 |

> git 分支名（`feat/d1.3a-*` / `feat/d1.3b.*` / `feat/d1.3c-*`）**保留不改**；仅文档使用新编号。

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-04-20 | 初版（4 阶段开发计划，归档）|
| v2.0 | 2026-05-07 | 8 月完整计划（归档 `gantt/archive/v2.0_2026-05-07_archived.md`）|
| v3.0 | 2026-05-08 | 7 角度评审 124 项 finding 整合；新增 D0 + 13 个新 D 任务；3 新角色；三档 DEMO；D4.5 降级非认证级试航 |
| v3.1 | 2026-05-09 | SIL 框架架构 patch：选项 D 混合 + DNV 工具链 3 MUST + ROS2 Humble + RL 隔离 + maritime-schema + Imazu-22 + Web HMI；累计缺口 -32~-34 pw（用户授权"完整实现优先"）|
| **v3.2** | **2026-05-20** | Phase 1 进度快照 + DEMO-2 冲刺 GAP（实测核查 12 D 任务关闭率 + Screen 3 双端真空诊断）；M4/M5/M8 SAT 桥 + D1.7 + D2.7 + D1.3.1' Sim Qual + D1.6 列为 P0；D1.3.2.2 推 Phase 4 |
| **v3.2-master** | **2026-05-20** | **文档重构：1500 行单文件 → 总账 + Phase overview + D 任务子文档 + TDL-Kernel 模块文档双轨树**；编号重排 D1.3a/b/c → D1.3.1/2/3 数字层级；原 1500 行归档 |
| **v3.2-master** | **2026-05-22** | Phase 2 进度评审更新：D2.7/D2.8 ✅；D2.1–D2.5 🟡（report.md 全部补全）；D2.6 🔴 框架就绪；DEMO-2 P0 GAP #7 关闭；Phase 1 快照修正（D1.3.1'/D1.4 从 🔴 stub 改正为 ✅ 实际完成）|

---

## 历史归档与权威指针

- 原 1500 行 v3.2 计划：[Architecture Design/gantt/archive/v3.2_2026-05-20_archived.md](Architecture%20Design/gantt/archive/v3.2_2026-05-20_archived.md)（含完整 D 任务 DoD + finding 闭环表 + 风险表 + 工时附录 A-E）
- 架构 v1.1.3-pre-stub：[Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md](Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md)
- v3.1 决策记录：[SIL/00-architecture-revision-decisions-2026-05-09.md](SIL/00-architecture-revision-decisions-2026-05-09.md)
- 评审 124 findings：[Phase 0/Archive/Review/2026-05-07/00-consolidated-findings.md](Phase%200/Archive/Review/2026-05-07/00-consolidated-findings.md)
- HAZID RUN-001：[Phase 0/Archive/HAZID/](Phase%200/Archive/HAZID/)
- 7 RFC 决议：[Phase 0/Archive/Cross-Team Alignment/](Phase%200/Archive/Cross-Team%20Alignment/)（注：本目录已归档，新 RFC 在 `docs/Design/Cross-Team Alignment/` 创建）
