# Phase 2 · M1/M2/M3/M6 业务模块 + Cert + HF + 架构 v1.1.3 stub · Overview

| 属性 | 值 |
|---|---|
| 时间 | 2026-06-16 → 2026-07-31（6.5 周）|
| 估计人周 | ~28.0（v3.1 baseline ~25 + D1.3.2.3 parity / D2.5 Web HMI patches +3）|
| 阶段目标 | 在 Phase 1 工程基础上交付 **M1/M2/M3/M6 决策级实装** + **Cert tracking 实例化（HARA v0.1 + FMEDA M1）** + **船长 HF ground truth** + **架构 v1.1.3 stub**；为 DEMO-2 提供"决策能力可视化"全栈基础 |
| **里程碑** | 🎬 **DEMO-2 Decision-Capable（2026-07-31）** |
| 进度日期 | 2026-05-21（Phase 2 启动前规划基线）|
| 当前阶段状态 | 🔴 **未启动**（Phase 1 关闭中；D2.x 全部 stub-only — 待 6/16 启动）|

> **L1 总账**：[../00-master-plan.md](../00-master-plan.md)（v3.2-master，2026-05-20）
> **架构权威**：[../Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md](../Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) v1.1.3-pre-stub
> **完整 DoD 细则**：[../Architecture Design/gantt/archive/v3.2_2026-05-20_archived.md](../Architecture%20Design/gantt/archive/v3.2_2026-05-20_archived.md) §Phase 2 D2.1–D2.8

---

## D 任务清单 + 状态

| D 编号 | 主题 | 目录 | 人周 | Window | Affects | 状态 |
|---|---|---|---|---|---|---|
| D2.1 | M1 ODD/Envelope Manager 决策级实装 + FMEDA stub | [D2.1-m1-odd-hardening/](D2.1-m1-odd-hardening/) | 5.0 | 6/16–7/6 | M1 | 🔴 未启 |
| D2.2 | M2 World Model 决策级实装 + ENC 集成 + 协方差链 | [D2.2-m2-world-model-enc/](D2.2-m2-world-model-enc/) | 5.5 | 6/16–7/6 | M2 | 🔴 未启 |
| D2.3 | M3 Mission Manager + L1/L2 双订阅 + Current error 升级 | [D2.3-m3-mission-manager/](D2.3-m3-mission-manager/) | 3.0 | 6/16–7/13 | M3 | 🔴 未启 |
| D2.4 | M6 COLREGs Reasoner + 6 维度评分 + SAT-2 colregs_chain[5] | [D2.4-m6-colregs-6d-scoring/](D2.4-m6-colregs-6d-scoring/) | 5.0 | 6/16–7/31 | M6, M8 | 🔴 未启 |
| D2.5 | M1–M6 SIL Integration Test + 50 场景批量 GIF + KPI 仪表 | [D2.5-sil-m1-m6-integration/](D2.5-sil-m1-m6-integration/) | 2.5 | 6/16–7/31 | M1–M6, V&V | 🔴 未启 |
| D2.6 | 船长 HF Ground Truth（5 访谈 + Figma + 可用性 + 培训矩阵）| [D2.6-captain-hf-ground-truth/](D2.6-captain-hf-ground-truth/) | 3.0 | 6/16–7/13 | M8, HF | 🔴 未启 |
| D2.7 | HARA Instantiation v0.1（≥30 危险源）+ FMEDA M1 完整化 | [D2.7-hara-fmeda-m1/](D2.7-hara-fmeda-m1/) | 2.5 | 6/16–7/13 | Cert, M1 | 🔴 未启 |
| D2.8 | 架构 v1.1.3 stub（4 缺失模块 + §17–§21 SIL/RL/scenario/scoring/HMI）| [D2.8-arch-v1.1.3-stub/](D2.8-arch-v1.1.3-stub/) | 3.0 | 6/16–7/31 | 全模块 | 🔴 未启 |

**Phase 边界拉取（DEMO-2 P0 GAP，从 Phase 3 提前到 7/31）** — 不属 D2.x，但 DEMO-2 强依赖：

| 拉取项 | 来源 D 任务 | 人周 | 提前截止 | 阻塞 |
|---|---|---|---|---|
| M8 增发 `/sil/sat2_data` + `/sil/sat3_data` + `/sil/sotif_metrics` + IDL | 拆自 D3.4 | 1.5 | 7/31 前 | D2.5 SIL 联调 / 前端三 handler |
| 前端 useFoxgloveLive 增 3 handler（IvP / Trajectory / SOTIF）| 归 D1.3.2.3 后段 | 0.5 | 7/31 前 | D2.4 / D2.5 |
| M4 IvP solver stub（4 行为 + ivp_contributions[]）| 提前 D3.1 | 4.0 | 7/26 前 | D2.5 闭环 |
| M5 BC-MPC stub（线性化 Nomoto 兜底 + trajectory_candidates[]）| 提前 D3.2 | 6.5 | 7/27 前 | D2.5 闭环 |

合计 P0 ≈ **12.5 pw 拉取**；详见 [../00-master-plan.md](../00-master-plan.md) §DEMO-2 P0 冲刺 GAP。

---

## DEMO-2 Charter（2026-07-31）

**Scenario**（端到端约 8 min）：

1. 50 综合场景批量回放（Imazu-22 + AIS-derived ≥5 + ncdm_vessel OU 模式 ≥5）→ Web HMI 时间轴 scrubber 任意点 < 100 ms 跳转
2. ODD-A → ODD-B → ODD-C → ODD-D **实时切换** + ToR 自适应矩阵动画（ROC 60s / 桥楼 30s / 餐厅 90s / 睡舱 120s）
3. SAT-2 全展：M6 5 层 colregs_chain（Rule 13/14/15/16/17 决策链可视化）+ M4 IvP 8 方向贡献图 + M5 13 弧 BC-MPC 候选轨迹（trajectory_candidates[]）
4. 船长访谈片段 ≤2 min 剪辑 + Figma 原型 side-by-side + 资深船长现场反馈（≥1 签字"可用性通过"）
5. HARA v0.1 文档现场评审 + 1 条危险源 → SIF → SIL 全链路 demo
6. 架构 v1.1.3 stub 全文 walkthrough（§17 SIL framework + §18 RL isolation + §19 scenario library + §20 6D scoring + §21 Web HMI）
7. KPI 仪表盘（端到端时延 P95/P99 / first-run 通过率 ≥90% / 6 维度分数分布）

**Audience × View**：

- 业主（决策能力进展）
- PM（进度 vs DEMO-3 路径）
- **CCS 中期意见会议**（HARA + ConOps + v1.1.3 stub + AIP 11 月路径 — 头号必需件）
- DNV 验证官（SIL framework §17 + RL isolation §18 + maritime-schema §19 — 工具链 3 MUST 确认）
- 资深船长 ≥3 名（SAT-2 决策透明性直觉反馈）
- HAZID 干系人（v1.1.3 stub 132 [TBD-HAZID] 参数初值 + 8/19 RUN-001 闭口路径）

**Visible Success**：

- D2.1–D2.8 全部 DoD 通过
- **CCS 中期意见**回执（接受 HARA v0.1 + ConOps v0.1 + v1.1.3 stub 作为 AIP 申报基础）≥ 1 份
- **船长签字**"DEMO-2 可用性通过" ≥ 1 份（资深 FCB 船长）
- **首跑通过率 ≥ 90%**（50 场景）+ 6 维度评分分布报告
- 端到端时延 **P95 ≤ 800 ms / P99 ≤ 1200 ms**（V&V Plan §SIL latency budget）
- 50 场景批量 GIF/PNG evidence pack 一键导出（Puppeteer headless）
- **DNV 验证官**maritime-schema acceptance feedback 收到

详见 [../00-master-plan.md](../00-master-plan.md) §DEMO 三档 + v3.2 archived plan §Phase 2。

---

## 依赖图

```
Phase 1 出口：
  D1.1 ROS2 workspace ─┐
  D1.5 V&V Plan ────────┤
  D1.7 6 维度 rubric ───┼─→ D2.1 / D2.2 / D2.3 / D2.4 (业务模块决策级实装)
  D1.3.2.1 22 Imazu ────┤
  D1.3.2.3 Web HMI ─────┤
  D1.3.3 FMI bridge ────┘
                          ↓
                    D2.1–D2.4 完成
                          ↓
            D2.5 (SIL M1–M6 集成测试)
                          ↓
          ┌───────────────┼───────────────┐
          ↓               ↓               ↓
    D2.6 HF 访谈   D2.7 HARA + FMEDA   D2.8 v1.1.3 stub
    (HF 咨询)     (Safety 咨询)        (Architect)
          ↓               ↓               ↓
          └───────────────┼───────────────┘
                          ↓
                   🎬 DEMO-2 (7/31)
                          ↓
                  CCS 中期意见会议
                          ↓
                  Phase 3 (D3.x)
```

**外部依赖**：
- HF 咨询 6/16 onboard，7/27 出可用性测试报告
- 安全工程师 5/15 onboard（早 1 个月，为 D2.7 准备 HARA 方法学）
- V&V 工程师 5/8 onboard，D2.5 主owner
- DNV 验证官 — D2.8 §17 SIL framework 联审

---

## 关键风险（2026-05-21 规划基线）

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| R2.A | M4 IvP solver 自实现路径（RFC-009 method B），6/16 启动到 7/26 出 stub 仅 ~6 周；solver 算法成熟度不足导致 D2.5 集成失败 | 🔴 高 | 初版允许"分段线性 IvP"兜底；CasADi 真求解推到 Phase 3 D3.1；DEMO-2 只要 ivp_contributions[] 可视化即可 |
| R2.B | M5 BC-MPC CasADi 集成未启动，7/27 stub 仅 5 周；trajectory_candidates[] 排序逻辑无现成算法 | 🔴 高 | DEMO-2 允许"线性化 Nomoto + 13 弧枚举"兜底；CasADi IPOPT 真求解推 Phase 3 D3.2 |
| R2.C | M8 SAT-2/3 双端真空（生产端 topic 缺 + 消费端 handler 缺）DEMO-2 P0 阻塞 | 🔴 高 | D2.5 owner 7/15 前先打通 stub topic + 假数据 handler 链路，确保 D2.4/D2.5 输出可见 |
| R2.D | HARA ≥30 危险源在 4 周内（6/16–7/13）由 0.45 pw 外包安全工程师独立完成，方法学 ground truth 缺乏 | 🟡 中 | 安全工程师 5/15 onboard，比 D2.7 启动早 1 个月做准备；D2.7 仅要求 v0.1，完整化在 D3.3 |
| R2.E | 船长访谈 5 人在 4 周内安排（FCB ≥3 + 拖船 1 + 渡船 1），FCB 船长难约 | 🟡 中 | PM 5/20 起预约；备选用 R/V Gunnerus 公开访谈资料兜底（HF 咨询 fallback） |
| R2.F | DNV 验证官 maritime-schema 联审 feedback 来得晚（7/31 后），D2.8 §17 SIL framework 可能需返工 | 🟡 中 | D2.8 设计预留"DNV feedback patch" hook；D3.8 完整化时再吸收 |
| R2.G | 132 [TBD-HAZID] 参数 8/19 才闭口，D2.1/D2.4 必须用初值；初值偏离实际可能导致 D3.6 1000 场景大面积返工 | 🟡 中 | D2.1 + D2.4 行为分支结构敏感参数在 7/31 前先锁定初值（D3.5 路径）；非结构敏感参数沿用 v1.1.3 stub 默认 |

详见 [../00-master-plan.md](../00-master-plan.md) §DEMO-2 P0 冲刺 GAP。

---

## Findings 闭环映射（Phase 2 关闭 ~32 finding）

| 角度 | Finding ID | 关闭于 D 任务 |
|---|---|---|
| B (M2 World) | B P0-B-03 / B P1-B-02 / B P1-B-05 | D2.2 |
| C (Cert/Safety) | C P0-C-1(b) / C P0-C-3 partial / C P1-C-3 / C P1-C-5 partial / C P1-C-8 | D2.1 + D2.7 |
| D (Decision/M1) | D P0-D-01 partial / D P0-D-02 / D P0-D-03 / D P0-D-05 / D P1-D-04 / D P1-D-05 / D P1-D-08 / D P2-D-04 | D2.1 + D2.6 + D2.8 |
| E (V&V) | E P0-E3 / E P0-E5 / E P1-E5 | D2.5 |
| F (M3 Mission/Field) | F P0-F-01 / F P0-F-02 / F P0-F-04 stub / F P1-F-01 / F P1-F-03 | D2.3 + D2.8 |
| SIL | SIL P0 SIL-1 ~ SIL-7 | D2.8 §17–§21 |
| MV (Multi-vessel) | MV-7 / MV-8 / MV-9 / MV-10 / MV-11 | D2.8 stub scope |

详见 [../Architecture Design/gantt/archive/v3.2_2026-05-20_archived.md](../Architecture%20Design/gantt/archive/v3.2_2026-05-20_archived.md) §附录 D Findings Closure Map。

---

## 关联

- **L1 总账**：[../00-master-plan.md](../00-master-plan.md)
- **架构权威**：[../Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md](../Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) v1.1.3-pre-stub
- **SIL 设计套件**：[../SIL/v1.0-unified/](../SIL/v1.0-unified/) 4 文档
- **V&V Plan**：[../Phase 1/D1.5-vv-plan-scenario-qual/V&V_Plan/00-vv-strategy-v0.1.md](../Phase%201/D1.5-vv-plan-scenario-qual/V%26V_Plan/00-vv-strategy-v0.1.md)
- **TDL Kernel 模块视角**：[../TDL-Kernel/](../TDL-Kernel/) M1–M8 模块设计 + 进度联动（PR 合并时同步更新 M{n}-progress.md "Currently Implementing"）
- **旧 M 模块详设**（参考意图）：[../Archive/Old Modules/](../Archive/Old%20Modules/) — ❌ 只读历史，与 v1.1.3-pre-stub 不一致处以主架构为准
- **跨阶段证据**：[../Cert/](../Cert/) / [../ConOps/](../ConOps/) / [../Safety/HARA/](../Safety/HARA/) / [../Safety/FMEDA/](../Safety/FMEDA/) / [../HF/](../HF/) / [../Cybersecurity/](../Cybersecurity/)

---

## 启动流程（D2.x brainstorm → spec → plan → execute → report）

每个 D2.x 按 CLAUDE.md §7.1 双轨流程：

1. 在 `Phase 2/D2.X-*/` 目录下用对应 brainstorming prompt 启动 `superpowers:brainstorming` → 产出 `D2.X-spec.md`
2. 启动 `superpowers:writing-plans` → 产出 `D2.X-plan.md`
3. 启动 `superpowers:executing-plans` → 产出 `evidence/` + 闭口后写 `D2.X-report.md`
4. 同步更新涉及模块的 [../TDL-Kernel/M{n}-*/M{n}-progress.md](../TDL-Kernel/) "Closed in / Currently Implementing"

Brainstorming prompt 集中在本会话产出，按 D2.X 逐个粘到独立会话。

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-21 | 初版（Phase 2 启动前规划基线：D2.1–D2.8 目录 + DoD 摘要 + DEMO-2 Charter + 风险 + Findings 映射；从 v3.2 archived plan §Phase 2 + master-plan §当前进度快照提炼）|
