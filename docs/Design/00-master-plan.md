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
├── Phase 2/  (待建)                 Phase 2 启动时建：D2.x/00-overview + 各 D{x.y}/
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

## 当前进度快照（2026-05-20）

### Phase 0
✅ **全闭**（D0 11 项 must-fix + RFC-007/009 + 工时表 v2.1）。详见 [Phase 0/00-overview.md](Phase%200/00-overview.md)。

### Phase 1（DEMO-1 还 ~3.5 周）
- ✅ 完整：D1.5 V&V Plan（含 SIL latency budget + RL rebound + DNV toolchain entry）/ D1.8 cert+ConOps stub / 22 Imazu frozen / MMG 4-DOF / foxglove + MapLibre + ToR ≥2s
- 🟡 部分：D1.1 / D1.2 / D1.3.1 / D1.3.2.1 / D1.3.2.3 / D1.3.3
- 🔴 stub 空壳：**D1.3.1' Sim Qualification / D1.4 编码规范 / D1.6 场景 schema / D1.7 覆盖率方法论**
- 🔴 未启：D1.3.2.2 AIS-driven（**v3.2 决策推迟 Phase 4**）

### TDL Kernel 模块（DEMO-2 视角）
- ✅ M1 / M2 / M3 / M6 / M7 / M8 有真实 ROS topic 发布（SAT-1 级）
- 🔴 **M4 未发 SAT-2 ivp_contributions** / **M5 未发 SAT-3 trajectory_candidates** / **M8 未发 SAT-2/SAT-3/SOTIF metrics 三桥接 topic** / **前端 useFoxgloveLive 缺 3 handler**

### DEMO-2 P0 冲刺 GAP（按急迫度）
1. **M8 增发 SAT-2/3/SOTIF 3 topic + IDL**（1.5 pw，从 D3.4 拆出 7/31 前）
2. **前端 useFoxgloveLive 增 3 handler**（0.5 pw，归 D1.3.2.3）
3. **M4 IvP 提前 7/26 出 stub**（4 pw，D3.1 不可滑期）
4. **M5 BC-MPC 提前 7/27 出 stub**（6.5 pw，D3.2 不可滑期；初版允许线性化 Nomoto 兜底）
5. **M6 5 层 colregs_chain SAT-2 序列化**（0.5 pw，D2.4 内吸收）
6. **D1.7 6 维度评分 rubric 完整化**（1.5 pw，D2.4 评分依赖）
7. **D2.7 HARA ≥30 危险源 + FMEDA M1**（2.5 pw，CCS 中期意见会议必需）
8. **D1.3.1 Sim Qualification 真实跑 3 参考解**（1 pw，CCS 现场展示）
9. **D1.6 场景 schema maritime-schema 完整化**（2 pw，DEMO-2 50 场景前置）

合计 P0+P1 ≈ 20 pw；距 7/31 剩 ~10 周；双人 ~18.5pw/周 产能，理论可吸收，前提是 M4/M5 owner 7/13 准时开工。

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

---

## 历史归档与权威指针

- 原 1500 行 v3.2 计划：[Architecture Design/gantt/archive/v3.2_2026-05-20_archived.md](Architecture%20Design/gantt/archive/v3.2_2026-05-20_archived.md)（含完整 D 任务 DoD + finding 闭环表 + 风险表 + 工时附录 A-E）
- 架构 v1.1.3-pre-stub：[Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md](Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md)
- v3.1 决策记录：[SIL/00-architecture-revision-decisions-2026-05-09.md](SIL/00-architecture-revision-decisions-2026-05-09.md)
- 评审 124 findings：[Phase 0/Archive/Review/2026-05-07/00-consolidated-findings.md](Phase%200/Archive/Review/2026-05-07/00-consolidated-findings.md)
- HAZID RUN-001：[Phase 0/Archive/HAZID/](Phase%200/Archive/HAZID/)
- 7 RFC 决议：[Phase 0/Archive/Cross-Team Alignment/](Phase%200/Archive/Cross-Team%20Alignment/)（注：本目录已归档，新 RFC 在 `docs/Design/Cross-Team Alignment/` 创建）
