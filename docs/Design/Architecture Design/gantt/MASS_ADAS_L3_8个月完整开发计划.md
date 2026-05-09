# MASS ADAS L3 战术决策层 · 8 个月完整开发计划

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-PLAN-001 |
| 版本 | **v3.1**（基于 v3.0 + 2026-05-09 SIL 框架架构 patch）|
| 上一版本 | v3.0（2026-05-08，未归档；v3.1 是 v3.0 之上的 patch）→ v2.0（2026-05-07，已归档至 `archive/v2.0_2026-05-07_archived.md`）|
| 日期 | 2026-05-09 |
| 计划周期 | 2026-05-08 → 2026-12-31（≈ 8 个月，前 4 个月硬承诺）|
| 架构基线 | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` **v1.1.3-pre-stub**（2026-05-09 SIL 框架架构 patch；v1.1.3 stub 在本计划 D2.8 / 完整化在 D3.8）|
| v3.1 修订动因 | 2 次 SIL 深度研究（指导书 2026-05-09）+ 3 次 NLM Deep Research（silhil_platform 0 → 98 sources）+ 用户决策 Q1/Q2/Q3 全部确认 → 选项 D 混合架构锁定 + DNV 工具链 3 MUST + ROS2 Humble + RL 隔离三层 + maritime-schema + Imazu-22 + AIS-driven scenario + Hagen 2022 / Woerner 2019 评分 + Web HMI（详见 §0.4 + 决策记录 `docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md`）|
| 团队规模 | 2 人原班 + V&V 工程师 1 人 FTE（4 月，5/8 起）+ 安全工程师外包 + HF 咨询外包 |
| 修订动因 | 7 角度评审（[`docs/Design/Review/2026-05-07/`](../../Review/2026-05-07/)）暴露 30 P0/52 P1/29 P2，已在 v3.0 全量融入 |
| 用户决策 | A1（3 新角色）+ B2（RL 后移 Phase 4）+ B3（ALARP+SDLC 全表延 v1.1.4），8/31 不延期 |
| 前 4 月 | D0–D3，硬交付承诺（5/8 → 8/31）|
| 后 4 月 | D4 展望（9/1 → 12/31），不作硬承诺 |
| FCB 实船试航 | **降级为非认证级技术验证 + AIS 数据采集**（D4.5 修订）；认证级试航延 2027 Q1/Q2 AIP 受理后 |
| HIL 硬件 | 采购周期约 3 月，Phase 3 W12（7/13）提交需求单 |
| HTML 副本同步 | `MASS_ADAS_L3_8个月完整开发计划.html` v2.0 当前不与本 md 一致；**v3.0 HTML 同步是 D0.3 子项**（5/12 完成）|

---

## 0. 工时数学闭口（v3.0 核心）

### 0.1 工作量盘点（按附录 A 汇总）

| 阶段 | v2.0 人周 | v3.0 人周 | 净增 |
|---|---|---|---|
| D0 sprint（NEW）| — | 2.0 | +2.0 |
| Phase 1 | 8.5 | 18.0 | +9.5 |
| Phase 2 | 19.0 | 29.5 | +10.5 |
| Phase 3 | 28.0 | 37.5 | +9.5 |
| **前 4 月合计** | **55.5** | **87.0** | **+31.5** |

净增 31.5 人周来源（按主因归类）：

| 来源 | 人周 |
|---|---|
| D0 must-fix sprint（11 项 must-fix）| +2.0 |
| D1.3 拆分 + Simulator Qualification（5 → 8）| +3.0 |
| V&V infra（D1.5 V&V Plan + D1.6 场景 schema + D1.7 覆盖率方法论）| +5.5 |
| D1.8 cert tracking + ConOps stub | +1.0 |
| M1/M2/D2.5 上调（FMEDA + intent + env sanity + V&V 独立 owner）| +2.0 |
| D2.6 船长 ground truth（5 访谈 + Figma + 可用性 + 培训矩阵）| +3.0 |
| D2.7 HARA + FMEDA M1 | +2.5 |
| D2.8 v1.1.3 stub（§16/§15.0/§12.5/§12.3/§10.5/4 缺失模块）| +3.0 |
| M5 micro 调整（FM-4 删除 + MRM 走 M7）| -0.5 |
| M7 拆 6→9（MUST-11，M7-core 6 + M7-sotif 3）| +3.0 | <!-- v2.1: split 6pw M7-core + 3pw M7-sotif; see M7/02-effort-split-v2.1.md -->
| M8 上调（双角色 + 50Hz 三档 + ToR 矩阵 + BNWAS + Y-axis + ECDIS）| +1.5 |
| D3.5' 培训课程大纲 | +1.5 |
| D3.6 SIL 1000+ 上调（立方体覆盖 + Adversarial 比例）| +0.5 |
| D3.8 v1.1.3 完整化 | +2.5 |
| D3.9 RFC-007 cyber | +1.0 |
| **合计** | **+31.5** |

> B2 RL 后移 Phase 4 + B3 ALARP/SDLC 推 v1.1.4 这两条不直接出现在 Phase 1-3 工时表上 —— 它们让 v3.0 默认 Phase 1-3 不再为这些工作分配人周（避免 +5.5 进 Phase 1-3）。如果 B2/B3 不取，Phase 1-3 真实工作会是 87+5.5=92.5。

### 0.2 产能盘点

| 角色 | 在岗周数 | 周产能（人周/周）| 总人周 |
|---|---|---|---|
| 原 2 人团队 | 17 | 1.85 × 2 = 3.70 | 63.0 |
| V&V 工程师 FTE | 17 | 0.95 | 16.0 |
| 安全工程师外包 | 8（5/15–7/10）| 0.45 | 3.5 |
| HF 咨询外包 | 6（6/16–7/27）| 0.25 | 1.5 |
| **产能合计** | | | **84.0** |

### 0.3 缺口与闭环

- 工作 **87.0** vs 产能 **84.0** = **-3.0 人周缺口**（不是缓冲）
- 闭环路径（必选 1 条 + 备选）：
  1. **V&V 工程师延 2 周到 9/14**（+2 人周）+ **架构师/技术负责人 1 周受限加班**（+1 人周）= 闭口（**默认路径**，PM 5/8 与 V&V 工程师 onboard 时同步谈定）
  2. 备选：原 2 人团队 productivity 假设从 1.85 → 1.95（AI 辅助 + 评审整改激励）+ V&V 1 周延期 = 闭口（要求每月 productivity 实测验证）
  3. 紧急 fallback（contingency B4 触发）：4 缺失模块完全推到 Phase 4，D2.8 stub 工作压到 1.5 人周（节省 1.5），D3.8 工作压到 1.5（节省 1.0），合计 +2.5 闭口
- ⚠️ **零容忍**：任一 D 滑期 ≥ 3 工作日 → PM 立即在周五 sync 触发上述备选 1/2/3 之一（升级为强制）

> **D0 sign-off 工时修订注记（2026-05-08）**：RFC-009 法务-hat + M4-hat sign-off 确定 M4 IvP 自实现（方案 B），D2.1 M4 实装工时从 5.5pw 升至 **7.0pw**（+1.5pw），其中 +1.0pw 在 D2.1 窗口吸收，+0.5pw 分期至 D2.3。总工作量从 87.0pw 升至 **88.5pw**，产能 84.0pw 不变，**缺口从 -3.0 扩至 -4.5pw**。M7 FMEDA/SIL 测试 AC 降标（PM-hat Option A）维持 M7 总量 9.0pw 不变，不追加缺口。闭口路径：优先沿默认路径（V&V 延 2w + 架构师 1w 受限加班 = +3pw），剩余 -1.5pw 缺口由 D2.3 分期工时弥合（D2.3 窗口若满载则触发 B4-minor，在 D3.1 前吸收）。
- 严禁路径：(a) 牺牲 SIL 场景质量降通过率门槛 / (b) 跳过 V&V Plan / (c) 让 D-Charter 必填项空缺。这 3 条命中即项目 P0 escalation。

### 0.4 v3.1 修订摘要（2026-05-09）

**触发**：7 角度评审（2026-05-07）暴露 SIL 框架零散分布缺整体架构 → 2 次 SIL 深度研究产出 + 评审专家整合指导书（`docs/Doc From Claude/L3 TDL SIL 架构整合与修订指导书.md`）+ 3 次 NLM Deep Research（silhil_platform 笔记本 0 → 98 sources）+ 用户决策 2026-05-09 拍板 Q1/Q2/Q3。

**决策记录主文件**：`docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md`（含 33 个外部证据来源 [E1]–[E33] + 置信度标注 + 推翻信号）。

**架构 v1.1.3-pre-stub patch**：见架构报告附录 F + 附录 D'''''（2026-05-09 同步完成）。

**v3.1 计划增量**（按决策分组）：

| # | 决策 | 影响 D 任务 | 工时增量 (pw) |
|---|---|---|---|
| 1 | 选项 D 混合架构锁定（ROS2 Humble + maritime-schema + libcosim/OSP + farn）| D1.3a 注 / D1.3b 拆 b.1+b.2+b.3 / **D1.3c NEW** / D1.6 改 / D1.5 SIL latency budget | +6.0 (b.1/2/3) + 12-16 (D1.3c) |
| 2 | M7 严格留 ROS2 native 不过 FMI | D1.5 V&V plan 注 / D1.3c 边界规约 | 含上 |
| 3 | RL 隔离三层（即使 RL 推 Phase 4）| D1.3a/b 仓库结构 + CI lint / D1.5 RL artefact rebound / D2.8 §RL 章节 | +0.5 (lint) + 0.5 (D2.8) |
| 4 | Imazu-22 强制基线（22 全量进 D1.3b.1）| D1.3b.1 范围 | 含 D1.3b 拆分 |
| 5 | L1 三模 mock：synthetic + ais_replay 进 D1.3b；rosbag 推 D2.5 | D1.3b.2 / D2.5 | 含 D1.3b 拆分 |
| 6 | AIS-driven scenario authoring（colav-simulator 级）| D1.3b.2 NEW | 含 D1.3b 拆分（+4 pw 计在 b.2）|
| 7 | maritime-schema 替代内部 schema | D1.6 | +0.5 |
| 8 | 6 维度结构化 COLREGs 评分 + PASS/FAIL 并存 | D1.7 + D2.4 + D3.6 | +0.5 (D1.7) + 0.5 (D2.4) + 0.5 (D3.6) |
| 9 | Monte Carlo LHS/Sobol 10000 sample | D3.6 扩展（不另建任务）| 含上 D3.6 +0.5 |
| 10 | Web HMI + ENC + foxglove_bridge + IEC 62288 SA subset | D1.3b.3 NEW + D2.5/D2.6 增项 + D3.4 增项 | +5 (b.3) + 2 (D2.5/6) + 1.5 (D3.4) |
| 11 | maritime-schema CCS 接受度 早期发函 | D1.8 | +0.2 |
| 12 | D2.8 v1.1.3 stub 新章节（§SIL + §RL + §Scoring + §Schema）| D2.8 | +1.0 |
| 13 | D3.8 v1.1.3 完整化（含 RL/SIL/Scoring 完整）| D3.8 | +0.5 |
| **v3.1 累计增量** | | | **+26~30 pw** |

**v3.1 工作量总计**：v3.0 88.5 pw（D0 sign-off 修订后）+ v3.1 增量 ~28 pw = **116~118 pw**；产能 84.0 pw 不变；**累计缺口扩至 -32~-34 pw**。

**用户授权 2026-05-09**：「工时缺口不用担心，核心目标是完整实现避免重构，用户会并行开发」。

**v3.1 缺口闭环路径**（v3.0 §0.3 基础上叠加）：
1. **D3.6 1000 场景目标延 9/30**（出 Phase 1-3 主线，进 Phase 4 早期）→ 释放 ~5 pw
2. **D1.3c FMI bridge** 由 V&V FTE 全职 4w + 安全外包临时支援 1w → 占满 V&V FTE 该阶段
3. **dds-fmu 集成范围降级 — Phase 1 仅 own-ship 走 FMI**（target ships + L1 mock 仍走 ROS2 native topic），bridge 仅 1 个 FMU 接口；其余 Phase 3 扩展 → 省 ~6 pw
4. **B4 contingency 触发**（4 缺失模块完整推后）→ 释放 ~4 pw
5. **用户并行开发授权**：用户在 D 任务允许时开多 worktree 并行，吸收剩余缺口
6. 严禁路径：v3.0 §0.3 三条不动 + 新增 (d) 跳过 RL 隔离三层（等于 Phase 4 重构灾难）/ (e) 跳过 maritime-schema 直接自建 schema（等于放弃 CCS 互认）

---

## 1. 总览：8 个月月度地图（v3.0）

| 月份 | 阶段 | 核心内容 | Demo | 性质 |
|---|---|---|---|---|
| **5月** | D0 + Phase 1 起 | Pre-Kickoff Must-Fix Sprint（5/8–5/12）+ 工程基础 + V&V 基线 | — | 硬承诺 |
| **6月** | Phase 1 → 2 | SIL 框架 + V&V Plan + 场景 schema + Cert tracking + ConOps stub | **🎬 DEMO-1（6/15 Skeleton Live）** | 硬承诺 |
| **7月** | Phase 2 → 3 | M1/M2/M3/M6 + 船长访谈 + HARA + v1.1.3 stub | **🎬 DEMO-2（7/31 Decision-Capable）** | 硬承诺 |
| **8月** | Phase 3 | M4/M5/M7-core/M7-sotif/M8 + HAZID 8/19 + SIL 1000 + v1.1.3 完整化 | **🎬 DEMO-3（8/31 Full-Stack + Safety + ToR）** | 硬承诺 |
| **9–12月** | Phase 4 | HIL 搭建 + SIL 2 接洽 + AIP 提交 + RL 对抗（B2 启动）+ 4 缺失模块完整（B4 触发则启动）+ FCB 非认证级技术验证试航 | — | 展望 |

> **HAZID RUN-001**（平行轨道）：5/13 第 ① 次会议 → 8/19 完成；议程已重排（≥ 2 个 full-day workshop + CCS 持续介入），D3.5 v1.1.3 回填窗口拆"行为分支无关参数"在 7/31 前先锁定 + "行为分支相关参数"等 8/19。

---

## 2. D0 · Pre-Kickoff Must-Fix Sprint（5/8–5/12）

**时间**：2026-05-08 → 2026-05-12（3 工作日，含周末缓冲）  
**估计人周**：2.0 人周（不含在 Phase 1 计数）  
**阶段目标**：在 5/13 HAZID kickoff + Phase 1 D1.x 真正开工前，关闭评审 7 角度暴露的 11 项 must-fix 阻断项 + 配套 RFC + 工时表 + HTML 同步。否则下游 D1.x 起点不稳。

### D0 交付物

#### D0.1 算法/接口 surgical fix + multi-vessel cleanup

| 属性 | 值 |
|---|---|
| 预计人周 | 1.0（多人协作 1 天即完成单项；架构师 + 4 模块负责人）|
| 目标完成 | 2026-05-12 |
| Owner | 架构师（统筹）+ M2 + M5 + M8 模块负责人 |

**子项**：
- MUST-1：M2 OVERTAKING 扇区改为 [112.5°, 247.5°]（M2:387,417）+ 单元测试覆盖 4 边界
- MUST-2：Mid-MPC N 三处统一为 18 / 90s（架构 §10.3:850 / M5:184,272 / RFC-001 锁定值）
- MUST-5：M5 §7.2 FM-4 fallback `ROT_max=8°/s` 删除，改为 `OUT_of_ODD → MRM`（M5:674）
- MUST-6：M2 §2.2 校验改为 `f(Manifest.max_speed) × 1.2`（M2:48）+ Capability Manifest schema 加 max_speed 必填
- MUST-7：M8 详设增 active_role 对称双角色 stub（PRIMARY_ON_BOARD/PRIMARY_ROC/DUAL_OBSERVATION）（M8:111）
- MUST-9：M5 §7.2 FM-2 `MRM-01 → 通知 M7` 修订（M5:672）
- 多船型 P0：M5 / M2 PR review checklist 加 grep `FCB|45m|18 kn|22 kn|ROT_max\s*=\s*\d` = 0 检查

**DoD**：
- [x] 6 项 surgical fix PR merge（v1.1.2-patch1 git tag）
- [x] 多船型 lint 规则在 CI 启用（dry-run）
- [x] 关闭 finding ID：MUST-1/2/5/6/7/9 + MV-1/2/3/5/6/7

**Demo Charter**：
- Scenario：PR diff review 现场
- Audience × View：架构师 / 模块负责人 / 评审 reviewer
- Visible Success：6 PR all merged + lint dry-run 报告 0 violation
- Showcase Bundle：v1.1.2-patch1 tag URL + diff video（≤2 min）

---

#### D0.2 决策 / RFC closure

| 属性 | 值 |
|---|---|
| 预计人周 | 0.5 |
| 目标完成 | 2026-05-12 |
| Owner | 架构师 + M4 负责人 + 法务 |

**子项**：
- MUST-3：IvP 实现路径裁决（libIvP LICENSE 复审 → GPL/LGPL/BSD）→ RFC-009 提交并归档
- RFC-007：M7 ↔ X-axis Deterministic Checker 心跳契约（F P1-F-05）
- MUST-4：HAZID-FCB 船期裁决备忘录（HAZID 8/19 不依赖 FCB 实船数据 → 改用水池 + MMG 仿真 + 历史 AIS 回归作为校准证据；实船数据延 12 月作为补充）

**DoD**：
- [x] RFC-007 + RFC-009 commit 到 `docs/Design/Cross-Team Alignment/`
- [x] HAZID-FCB 备忘录 commit 到 `docs/Design/HAZID/RUN-001-fcb-data-substitute-memo.md`
- [x] 关闭 finding ID：MUST-3/4（法务-hat + M4-hat ✅；CCS-hat ✅）

**Demo Charter**：
- Scenario：架构师在 HAZID kickoff 5/13 现场宣读 RFC-007/009 + HAZID-FCB 备忘录
- Audience × View：HAZID 8 名干系人 + CCS 联络人
- Visible Success：备忘录现场签字
- Showcase Bundle：3 份 RFC/备忘录 PDF + kickoff 议程更新

---

#### D0.3 工时表 + D4.5 修订声明 + HTML 同步

| 属性 | 值 |
|---|---|
| 预计人周 | 0.5 |
| 目标完成 | 2026-05-12 |
| Owner | PM + 架构师 |

**子项**：
- MUST-11：M7 6 → 9 人周拆为 M7-core 6w + M7-sotif 3w；工时表 v2.1
- D4.5 修订声明：12 月 FCB 实船降级为"非认证级技术验证 + AIS 数据采集"
- MUST-8：D1.2 PATH-S CI 独立性检查 dry-run 在 D1.1 完成时即启用（warning 模式，不阻塞）
- MUST-10：3 项 deep research 启动（safety_verification: DNV-RP-0671 + ABS Autonomous Guide / ship_maneuvering: FCB 高速段 6-DOF / maritime_human_factors: Veitch dataset + BNWAS）
- HTML 同步：`MASS_ADAS_L3_8个月完整开发计划.html` v2.0 → v3.0（按本 md 全量替换）

**DoD**：
- [x] 工时表 v2.1 commit（M7-hat ✅ + PM-hat Option A AC降标确认）
- [x] D4.5 修订声明并入本计划 §1 + Phase 4 章节（v3.0 已含；7 项门槛清单补充；CCS-hat ✅）
- [x] PATH-S CI dry-run job 在 GitLab CI 配置（5/12 commit）
- [x] 3 项 deep research 报告完成 + sources 入对应 DOMAIN
- [ ] HTML 副本与 md 内容一致（人工 spot-check 5 关键 D 卡片）⚠️ 未同步（低优先级，Phase 1 前完成）
- [x] 关闭 finding ID：MUST-8/10/11 + 用户决策 §13.2/13.3

**Demo Charter**：
- Scenario：5/12 PM 现场展示工时表 v2.1 + 资源日历 + HTML 副本
- Audience × View：业主 / 项目高层
- Visible Success：业主签字资源到位（V&V 工程师 5/8 onboard 验证 + 安全/HF 外包合同 5/15 生效）
- Showcase Bundle：工时表 PDF + 资源日历 + HTML 截图 + 3 份 deep research 摘要

---

### D0 关键风险

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| R0.1 | 法务 5/12 内未完成 libIvP LICENSE 审核 | 中 | RFC-009 备选方案"先按自实现起手，libIvP 推到 Phase 2 末作可选优化" |
| R0.2 | V&V 工程师 5/8 未到岗 | **高** | A1 闭环刚性依赖，5/8 必须到位；不到则 Phase 1 D1.5/D1.6 推 1 周 + 安全/HF 外包合同同步推 |
| R0.3 | HAZID-FCB 备忘录 CCS 不接受 | 中 | 备选 plan：HAZID workshop ≥ 2 个 full-day（5/27 + 6/24）以提升量级，弥补无实船数据的工时不足 |

---

## 3. Phase 1 · 工程基础 + SIL 框架 + V&V 基线（5/13–6/15）

**时间**：2026-05-13 → 2026-06-15（4.5 周；**v3.1 注**：D1.3b.2 / D1.3b.3 / D1.3c 跨 Phase 1/2 边界至 7/15-7/31）  
**估计人周**：**~28.0**（v3.0 18.0 + v3.1 D1.3 拆分 + D1.3c FMI bridge 起步段，详见 §0.4 v3.1 修订摘要）  
**阶段目标**：在写任何业务模块代码前，建立"测试基础设施 + 质量门 + V&V 范式 + 认证证据骨架 + **SIL 框架架构（选项 D 混合 + DNV 工具链 3 MUST + RL 隔离三层）**"。SIL 框架是整个项目的技术基础（最高优先级），V&V Plan + 场景 schema + 覆盖率方法论 + DNV 工具链锁定 是认证基础。**Phase 1 末交付 DEMO-1：Skeleton Live**（哪怕用 mock 也要展示完整骨架，含 **Web HMI + ENC + foxglove_bridge 实时数据流**）。

### Phase 1 甘特（v3.1 修订）

| 工作流 | W0 5/13 | W1 5/18-24 | W2 5/25-31 | W3 6/1-7 | W4 6/8-15 |
|---|---|---|---|---|---|
| D1.1 ROS2 工作区（含 RL 隔离 L1 仓库拆分）| ▓▓ | ▓▓▓ | | | |
| D1.2 CI/CD 流水线 + PATH-S dry-run + cross-import lint | ▓ | ▓▓▓ | ▓▓ | | |
| D1.3a 4-DOF MMG（FMI 导出契约就绪 + RL 隔离 repo 拆分）| ▓▓▓ | ▓▓▓▓ | ▓▓▓ | | |
| D1.3b.1 YAML 场景 + Imazu-22 + Cerberus 双语言 + maritime-schema | | ▓ | ▓▓▓ | ▓▓▓▓ | ▓▓ |
| **D1.3b.2 AIS-driven scenario 工具（NEW v3.1）→ 跨 Phase 2 至 7/15** | | | ▓ | ▓▓ | ▓▓▓ |
| **D1.3b.3 Web HMI + ENC + foxglove_bridge（NEW v3.1）→ 跨 Phase 2 至 7/15** | | | ▓ | ▓▓ | ▓▓▓ |
| **D1.3c FMI bridge / libcosim / dds-fmu（NEW v3.1）→ 跨 Phase 2 至 7/15** | | | ▓▓ | ▓▓▓ | ▓▓▓ |
| D1.3.1 Simulator Qualification Report | | | | ▓▓ | ▓▓ |
| D1.4 编码规范 | | | ▓ | ▓▓ | |
| D1.5 V&V Plan v0.1（含 SIL latency budget + RL artefact rebound）[V&V] | ▓▓ | ▓▓▓ | ▓▓ | | |
| D1.6 场景 schema（maritime-schema 替代）+ 追溯 + farn case folder [V&V] | | | ▓▓ | ▓▓▓ | ▓ |
| D1.7 覆盖率方法论 + 6 维度评分 rubric + Monte Carlo 段 [V&V] | | | | ▓▓ | ▓▓▓ |
| D1.8 cert tracking + ConOps stub + maritime-schema CCS 发函 | | | | ▓▓ | ▓▓▓ |
| ⟋ HAZID RUN-001 | 第 ① 次会议★ | — | 第 ② 次 | — | 第 ③ 次 |
| 🎬 **DEMO-1 Skeleton Live** | | | | | ★ 6/15 |

### Phase 1 交付物

#### D1.1 ROS2 工作区 + IDL 消息

| 属性 | 值 |
|---|---|
| 预计人周 | 1.5 |
| 目标完成 | 2026-05-24 |
| Owner | 架构师 |

**Scope**：colcon workspace + 8 个 ROS2 package 骨架；`l3_msgs`（25 条 .msg）+ `l3_external_msgs`（12 条跨层 .msg），全部含 `schema_version` + `stamp` + `confidence`（v3.0 修订）；Mock publisher 节点（每个外部接口一个，可配置频率/字段）；`colcon build` 全绿。

**DoD**：
- [ ] 25 + 12 .msg 与 v1.1.2 §15.1 完全对齐 + schema_version 字段
- [ ] Mock publisher 以正确频率/字段发布所有外部接口消息
- [ ] `colcon build --packages-select l3_msgs l3_external_msgs` 无警告
- [ ] 关闭 finding ID：F P1-F-04（schema_version）+ F P1-F-06

**Demo Charter**：见 DEMO-1。Mock publisher 在 SIL HMI 上展示 IDL 流通即可。

---

#### D1.2 CI/CD 5 阶段流水线 + PATH-S CI dry-run

| 属性 | 值 |
|---|---|
| 预计人周 | 1.5 |
| 目标完成 | 2026-05-31 |
| Owner | 基础设施负责人 |

**Scope**：GitLab CI 5 阶段（lint / unit / static-analysis / integration / release）；Docker dev + CI 镜像（Ubuntu 22.04 + ROS2 Jazzy + GCC 14.3 + clang-tidy 20 + cppcheck）；`.clang-tidy`（PATH-D 主路径）+ `.clang-tidy.path-s`（M1/M7 PATH-S 严格路径，LineThreshold=40 + 禁动态分配）；`check-doer-checker-independence.sh` 独立性检查脚本（**v3.0 起从 D0 即 dry-run，本 D 升级为强制**）；首次 CI runner 全量绿。

**DoD**：
- [ ] CI 5 阶段全部通过（非 `|| true`）
- [ ] Docker 镜像可复现构建（固定版本号）
- [ ] PATH-S CI 独立性检查在 D1.x 全部通过 dry-run 后切强制（截止 6/15）
- [ ] 多船型 lint 规则（D0.1 引入）从 dry-run 升级为强制
- [ ] 关闭 finding ID：A P1-A8

**Demo Charter**：6/15 演示 CI pipeline 全绿截图 + PATH-S 独立性报告。

---

#### D1.3a · 4-DOF MMG 物理仿真器 + AIS 历史数据管道（D1.3 拆分 a）

| 属性 | 值 |
|---|---|
| 预计人周 | 4.0 |
| 目标完成 | 2026-05-31 |
| Owner | 技术负责人 |

**Scope**：FCB 4-DOF MMG 仿真器（Yasukawa 2015 标准参数 + RK4, dt=0.02s（代替 Euler；精度满足 D1.3.1 ≤5% 参考解误差门槛；现有单元测试基于 RK4，不改动）；初始 YAML 参数 [HAZID 校准前]；hull_class = SEMI_PLANING 标注）；AIS NMEA 0183 解析 → TrackedTargetArray；NOAA + ChAIS（**v3.1 修订**：Phase 1-3 改用 Kystverket + NOAA MarineCadastre 开放数据，对齐 colav-simulator 工程模式）数据集；可配置回放速率 1×/5×/10×；**v3.0 新增**：仿真器接口为 `ShipMotionSimulator`（abstract），FCB 是 plugin。**v3.1 新增**：(a) 仓库结构按 RL 隔离 L1 边界拆分 — `/src/l3_tdl_kernel/`（C++/MISRA, ROS2 native, frozen DNV-RP-0513 [R25] assured）vs `/src/sim_workbench/`（Python sim 工具 / D1.3a-b 共用）— 三 colcon 包独立，CI lint 检测 cross-import 报错（详见架构 v1.1.3-pre-stub 附录 F.4）；(b) `ShipMotionSimulator` 接口预留 FMI 2.0 导出契约（D1.3c 实现 dds-fmu 桥接时启用，Phase 1 不强制实装 FMU 封装）；(c) 决策记录见 `docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md` §4.

**DoD**：
- [ ] 仿真器连续 1h 无崩溃；停船误差 ≤ 10%（vs 解析参考解）
- [ ] AIS 历史回放 ≥ 1h 真实片段（Kystverket / NOAA MarineCadastre 开放数据），目标解析率 ≥ 95%
- [ ] `ShipMotionSimulator` 抽象 + FCB plugin（YAML `vessel_class: FCB` 切换）
- [ ] **v3.1 NEW**：`/src/l3_tdl_kernel/` 与 `/src/sim_workbench/` 独立 colcon 包；CI lint rule 拒绝 sim_workbench 内 `#include` l3_tdl_kernel 头文件
- [ ] **v3.1 NEW**：`ShipMotionSimulator::export_fmu_interface()` 抽象方法签名提交（实现延 D1.3c）
- [ ] 关闭 finding ID：G P0-G-1 (a) + 多船型 MV-4

**Demo Charter**：6/15 DEMO-1 展示 1h AIS 回放 + 仿真器 own-ship 运动 + ShipMotionSimulator 抽象切换 + RL 隔离 L1 仓库结构 + CI lint 报错 demo（故意 cross-import 触发）。

---

#### D1.3b.1 · YAML 场景管理基础 + Imazu-22 + Cerberus 双语言（v3.1 拆分自 D1.3b）

| 属性 | 值 |
|---|---|
| 预计人周 | 3.0 |
| 目标完成 | 2026-06-15 |
| Owner | 技术负责人 |

**Scope**：YAML 声明式场景管理（含 vessel_class / disturbance_model / PRNG seed 字段）；**v3.1 修订**：场景 schema 由内部 Pydantic 改为 **`maritime-schema` `TrafficSituation` 扩展**（FCB 项目专属字段 → `metadata.*` 节点；详见架构 v1.1.3-pre-stub 附录 F.5 + 决策记录 §5）— 证据 [R27]；**Imazu-22 强制基线**（22 canonical 2/3/4-ship encounters，[R38] Imazu 1987 / Sawada 2021）freeze 为 `imazu22_v1.0.yaml` SHA256 hash 化，每 PR fast gate 必跑；10 自编场景（Head-on × 3, Crossing × 4, Overtaking × 3）保留作 ODD 扩展；**Cerberus 双语言验证**（Python `cerberus` + `pydantic` / C++ `cerberus-cpp`，同 schema 文件）；批量运行 + JSON / Apache Arrow 双输出；COLREGs 覆盖率 HTML 报告（Rule 5/6/7/8/9/13-17/19 × 通过 / 失败矩阵）。

**DoD**：
- [ ] 22 Imazu + 10 自编场景全部 `maritime-schema` v0.2.x schema-compliant
- [ ] `imazu22_v1.0.yaml` SHA256 frozen，PR-trigger CI fast gate 必跑全 22
- [ ] Python `cerberus` + C++ `cerberus-cpp` 同 schema 双语言验证通过
- [ ] 32 场景批量自动运行 + HTML 报告生成
- [ ] 单场景运行 ≤ 60s wall-clock（≥ 600s sim time，P2-E1 整改）
- [ ] 22 Imazu 场景 CPA ≥ 200 m + COLREG-classification rate ≥ 95%（[E2] Stage 1 advance trigger）
- [ ] 关闭 finding ID：G P0-G-1 (b) + G P1-G-4 + G P1-G-5 + P2-E1

**Demo Charter**：6/15 DEMO-1 现场演示 PR 触发 → Imazu-22 fast gate 22/22 跑通 + maritime-schema 校验通过 + 覆盖率热图。

---

#### D1.3b.2 · AIS-driven scenario authoring 工具（NEW v3.1，跨 Phase 1/2 边界）

| 属性 | 值 |
|---|---|
| 预计人周 | 4.0 |
| 目标完成 | 2026-07-15 |
| Owner | 技术负责人 + V&V 工程师 |

**Scope**：参考 NTNU `colav-simulator` 工程模式 [R37]，实现 AIS-driven scenario authoring 工具（详见架构 v1.1.3-pre-stub 附录 F.7 + 决策记录 §7）：(a) AIS DB 接入（PostGIS / Kystverket / NOAA MarineCadastre 开放数据，**Phase 1-3 仅用开放数据**，用户决策 2026-05-09 Q2.1 = a）；(b) 5 阶段管线：MMSI 分组 + 重排去重 → 间隙拆段（>5 min）+ Δt=0.5s 重采样 → NE 线性插值 + COG 圆形插值（避 360° wrap）+ Savitzky-Golay / Kalman 平滑 SOG/COG → bbox + 时间窗 → DCPA < 500m / TCPA < N min 阈值过滤 → COLREG 几何分类（Head-on / Crossing / Overtaking）→ maritime-schema YAML 导出（含 `metadata.*` 扩展字段 + traceability hash）；(c) 坐标变换 WGS84 → local NE Cartesian (Transverse Mercator)；(d) **3 种 target 运动模式**：`ais_replay_vessel`（必）、`ncdm_vessel`（D2.4 引入 NCDM Ornstein-Uhlenbeck 外推 [R37]）、`intelligent_vessel`（D3.6 引入 VO/简化 MPC，multi-agent）；(e) **L1 三模 mock**：`l1_mode := {synthetic | ais_replay}` 通过 ROS2 launch arg 切换（synthetic 由 D1.3b.1 scenario YAML 驱动；ais_replay 由本 D 工具产出 trajectory 驱动）；rosbag 模式推 D2.5。

**DoD**：
- [ ] AIS 预处理管线：1h Kystverket 数据 → 提取 ≥ 10 个有效 encounter (DCPA < 500m)
- [ ] 单 encounter → maritime-schema YAML 导出 + Cerberus 验证通过
- [ ] `ais_replay_vessel` 模式实装 + 50Hz 实时位置发布到 `/world_model/tracks` topic
- [ ] L1 launch arg `l1_mode := {synthetic | ais_replay}` 切换无缝
- [ ] `ncdm_vessel` + `intelligent_vessel` 接口 stub（实现延 D2.4 / D3.6）
- [ ] 关闭 finding ID：G P0-G-1 (c, NEW v3.1) + AIS 历史数据 G P1-G-x

**Demo Charter**：DEMO-2 (7/31) 中：在 SF Bay AIS 数据切片中选 1 个真实 encounter（user 在 web HMI 选 bbox + 时间窗）→ 工具自动产出 maritime-schema YAML → SIL run → ais_replay 目标船与历史完全吻合（误差 < 5m）→ HMI 海图同步显示。

---

#### D1.3b.3 · Web HMI + ENC + foxglove_bridge（NEW v3.1，跨 Phase 1/2 边界）

| 属性 | 值 |
|---|---|
| 预计人周 | 5.0 |
| 目标完成 | 2026-07-15（Phase 2 增项延 7/31） |
| Owner | M8 前端负责人 + 技术负责人 |

**Scope**：替代当前静态 SVG 雷达原型 `L3_TDL_SIL_Interactive.html`，构建 production-grade web HMI（详见架构 v1.1.3-pre-stub 附录 F.9 + 决策记录 §9）：(a) **MapLibre GL JS** 海图引擎（WebGL，1000+ vessel @60FPS via symbol layers + S-52 expression styling）[R35]；(b) **S-57 → MVT 管线**：GDAL `ogr2ogr` → GeoJSON → Tippecanoe → MVT vector tiles（或 `manimaul/s57tiler`）— ENC demo 双区域：**Trondheim Fjord（NTNU paper 复现）+ NOAA San Francisco Bay（高 AIS 密度）**（用户决策 Q2.5.d）；(c) **ROS2 ↔ Web 桥**：`foxglove_bridge`（C++, Protobuf）替代 rosbridge_server (Python/JSON 50Hz 撑不住)；(d) **HMI 标准**：IEC 62288 [R35] + IMO S-Mode [R36] **SA subset**（不全 ECDIS）— own-ship + targets + COG/SOG vector + CPA ring + grounding hazard 高亮；(e) 现 HTML 视觉信息块全迁移（CPA / TCPA / Rule / Decision / M1-M8 pulse / ASDR log，沿用截图视觉风格）；(f) **框架**：React + MapLibre GL（用户决策 Q2.5.c）；(g) 1 场景 live demo（DEMO-1 中 R14 head-on 跑通）。

**DoD**：
- [ ] S-57 → MVT 管线对 Trondheim + SF Bay 出 vector tiles，浏览器加载 < 2s
- [ ] `foxglove_bridge` 接 ROS2 → 50Hz own/target state 实时推 web，无丢包
- [ ] MapLibre HMI 加载 + own-ship sprite + 1 target sprite 同屏 @60FPS
- [ ] CPA/TCPA/Rule/Decision 信息块视觉风格与原 HTML 一致
- [ ] M1-M8 pulse + ASDR 日志实时显示
- [ ] 1 场景（R14 head-on）DEMO-1 现场 live demo 通过
- [ ] **Phase 2 增项延 7/31**：Apache Arrow replay + GSAP scrubber + Puppeteer GIF/PNG batch + 多 target + grounding hazard 高亮 + TLS/WSS（详见 D2.5 + D2.6 增项）
- [ ] 关闭 finding ID：HMI G P1-G-x（NEW v3.1）

**Demo Charter**：6/15 DEMO-1 现场跑 1 个 R14 head-on 场景（FCB own-ship + 1 target，Trondheim Fjord ENC 底图）；7/31 DEMO-2 演示 50 综合场景批量 GIF evidence pack 一键产出 + 时间线 scrubber 任意点回放。

---

#### D1.3c · OSP libcosim FMI bridge + dds-fmu mediator（NEW v3.1，跨 Phase 1/2 边界）⭐

| 属性 | 值 |
|---|---|
| 预计人周 | **12-16**（v3.1 最大增量项；FMI 2.0 协同 + 自定义 async_slave + jitter buffer，详见决策记录 §2.4 + §11）|
| 目标完成 | 2026-07-15 |
| Owner | **V&V 工程师 FTE**（全职 4w）+ 安全工程师外包（临时支援 1w）+ 架构师（设计审）|

**Scope**：实现选项 D 混合架构的 FMI 边界（详见架构 v1.1.3-pre-stub 附录 F.1-F.2 + 决策记录 §2 + [R28] OSP libcosim 文档）：(a) `libcosim` C++ 库集成 + `ospx` 系统结构 author；(b) **`dds-fmu` mediator**（DDS ↔ FMI 桥接）+ 自定义 `libcosim::async_slave` C++ 实现（ROS2 节点作为异步组件参与 co-sim，不破坏物理求解器时间确定性）；(c) **own-ship MMG FMU 封装**（`ShipMotionSimulator::export_fmu_interface()` D1.3a 抽象方法 → `pythonfmu` / `mlfmu` 打包 → `.fmu`）；(d) jitter buffer + 软件插值滤波器（应对 dds-fmu 2-10ms latency + ROS2 best-effort jitter）；(e) **关键边界规则**：M7 Safety Supervisor 严格留 ROS2 native，不过 FMI 边界（M7 KPI < 10 ms，dds-fmu 单次 exchange 即吃 KPI）；**Phase 1 范围降级**（v3.1 §0.4 闭环路径 #3）：仅 own-ship dynamics 走 FMI（target ships + L1 mock 仍走 ROS2 native topic），bridge 仅 1 个 FMU 接口；(f) Python 3.11 容器（Humble 默认 3.10 → 容器内升 3.11，DNV 工具链兼容）。

**DoD**：
- [ ] `libcosim` + `ospx` 安装 + 编译通过（Ubuntu 22.04 + ROS2 Humble + Python 3.11 容器）
- [ ] `dds-fmu` mediator 集成 + own-ship MMG FMU loaded by libcosim 在 SIL 运行 1h 无崩溃
- [ ] dds-fmu latency 实测 P95 ≤ 10 ms / P99 ≤ 15 ms（V&V Plan 注 [R25] DNV-RP-0513 §4 model assurance）
- [ ] M7 KPI 不退化（< 10 ms 端到端，因 M7 严格 ROS2 native 不过 FMI）
- [ ] FMU 自鉴定证据集（pytest + RP-0513 §4 模板 + tool confidence 论证）→ 接 D1.3.1 报告
- [ ] target ships + L1 mock 接口预留 FMI（实装延 Phase 3 D3.6 intelligent_vessel）
- [ ] 关闭 finding ID：SIL P0 SIL-1 (NEW v3.1) + V&V P0 E1 SIL latency budget

**Demo Charter**：DEMO-2 (7/31) 演示：own-ship MMG 走 FMI/OSP libcosim → ROS2 native M5 BC-MPC 控制 → M7 (ROS2 native) checker 验证；现场 live 显示 dds-fmu latency 计量 + jitter buffer 工作状态。

**风险注**：本 D 是 v3.1 最大单项增量。若 dds-fmu 实测 latency > 10 ms 或 jitter 不可控，触发推翻信号 → 退回纯 ROS2 native + maritime-schema/Arrow 序列化层（详见决策记录 §2.5）；不阻塞 DEMO-1（D1.3a + D1.3b.x 已可独立 demo）。

---

#### D1.3.1 · Simulator Qualification Report（NEW v3.0）

| 属性 | 值 |
|---|---|
| 预计人周 | 1.0 |
| 目标完成 | 2026-06-15 |
| Owner | 技术负责人 + M5 负责人 |

**Scope**：仿真器自身 V&V plan + 实测报告。3 个解析参考解（直线减速 / 标准旋回 / Z 字操舵）误差 ≤ 5%；同 seed 100 次重跑确定性误差 < 1e-9；MMG 参数 ±20% 范围内不发散；HAZID 8/19 后参数变更 > 10% 强制重跑全 1000 场景的回归脚本；ISO 26262 TCL-3 类比的 tool confidence 论证。

**DoD**：
- [ ] `docs/Design/SIL/01-simulator-qualification-report.md` v0.1 commit
- [ ] 3 参考解 + 100 次重跑 + ±20% 不发散全部数据 commit
- [ ] CCS / DNV 可接受作为 verification tool 鉴定证据
- [ ] 关闭 finding ID：G P0-G-3 + P2-E5

**Demo Charter**：6/15 DEMO-1 现场跑 1 个参考解（旋回）并对比；展示 tool confidence 报告。

---

#### D1.4 · 编码规范 + 静态分析工具链

| 属性 | 值 |
|---|---|
| 预计人周 | 0.5 |
| 目标完成 | 2026-06-15 |
| Owner | 架构师 |

**Scope**：MISRA C++:2023 规则裁剪（PATH-D 179 / PATH-S 严格 / M8 裁剪 ~120）；clang-tidy 9 大类配置；cppcheck Premium 或 OSS；`coding-standards.md`（含 50 个常见违规修复模式）。

**DoD**：见 v2.0 D1.4，未变更。

**Demo Charter**：6/15 演示 coding-standards.md 文档 + clang-tidy 强制运行截图。

---

#### D1.5 · V&V Plan v0.1（NEW v3.0）⭐

| 属性 | 值 |
|---|---|
| 预计人周 | 2.0 |
| 目标完成 | 2026-05-31 |
| Owner | **V&V 工程师**（5/8 起 FTE）+ 架构师 |

**Scope**：DNV-CG-0264 范式的 V&V Plan，先于任何 SIL 测试启动审批。包含：(a) SIL→HIL→实船 entry/exit gates 完整定义；(b) 端到端 KPI 矩阵（AvoidancePlan P95 ≤ 1.0s / ReactiveOverrideCmd P95 ≤ 200ms / Mid-MPC < 500ms / BC-MPC < 150ms / M7 < 10ms）；(c) functional × performance × failure-response 三类测试覆盖；(d) Adversarial:Nominal:Boundary = 60:25:15 比例声明 + 辩护（**v3.1 注**：明标"内部启发式，非外部标准"，CCS 提交时不引为外部规范，详见架构 v1.1.3-pre-stub 附录 F.6）；(e) ASDR 跨场地一致性约束。**v3.1 新增**：(f) **SIL latency budget** 子节 — 量化 dds-fmu / OSP libcosim 桥接延迟（P95 ≤ 10 ms / P99 ≤ 15 ms 实测目标）+ jitter buffer 补偿策略 + M7 严格 ROS2 native 不过 FMI 的强制规约（详见架构 F.2 + 决策记录 §2.4）；(g) **RL artefact rebound path** 子节 — 规约 Phase 4 RL 启动后 ONNX → mlfmu → FMU → libcosim 回注流程（即使 Phase 1-3 不执行，规约现在锁，避免 Phase 4 临时设计灾难，详见架构 F.4 + 决策记录 §4 + [R30] DNV-RP-0671）；(h) **DNV 工具链 entry conditions** 子节 — `maritime-schema` v0.2.x + `libcosim` MPL-2.0 + `farn` MIT 三 MUST 工具的版本锁定 + license 验证表 + RP-0513 §4 自鉴定证据收集流程 [R25]。

**DoD**：
- [ ] `docs/Design/V&V_Plan/00-vv-strategy-v0.1.md` commit（含 v3.1 新增 §SIL latency budget + §RL rebound + §DNV toolchain entry）
- [ ] CCS 项目组邮件回执"V&V Plan 框架可接受"
- [ ] 关闭 finding ID：E P0-E1 + E P1-E1/E4/E5 + E P1-E3 + SIL P0 SIL-2 (NEW v3.1)

**Demo Charter**：
- Scenario：6/15 DEMO-1 + CCS 现场 review；entry-gate-checker 脚本 live demo
- Audience × View：CCS 验船师 + 业主 + 技术负责人
- Visible Success：CCS 邮件回执 + entry-gate 自动校验脚本对 D1.x 全部跑通
- Showcase Bundle：V&V Plan PDF + entry-gate-checker demo video + cert-evidence-tracking.md

---

#### D1.6 · 场景库 schema + 追溯矩阵 + CI 三层集（NEW v3.0）

| 属性 | 值 |
|---|---|
| 预计人周 | 2.0 |
| 目标完成 | 2026-06-15 |
| Owner | **V&V 工程师** + 技术负责人 |

**Scope**：**v3.1 修订**：场景 schema 由"内部 JSON Schema / Pydantic 强类型"改为"**`dnv-opensource/maritime-schema` v0.2.x `TrafficSituation` 扩展**"（详见架构 v1.1.3-pre-stub 附录 F.5 + 决策记录 §5；证据 [R27]）— FCB 项目专属字段 `requirements_traced[]` / `hazid_id[]` / `rule_branch_covered[]` / `vessel_class_applicable[]` / `expected_outcome` / `pass_criteria` 进 `metadata.*` 节点（schema 允许 additional properties）；场景独立 Git repo（或 monorepo 子目录）；CI 三层集（PR-trigger Smoke 10 / Nightly 200 / Weekly Full 1000+）；命名规范 `<rule>-<odd>-<encounter>-<seed>-<version>.yaml`；场景 ↔ Rule ↔ HAZID ↔ ODD 子域 ↔ 预期 PASS 矩阵。**v3.1 新增**：(a) `dnv-opensource/farn` MIT [R29] 集成作 1100-cell case folder generator（替代手工 case folder），LHS / Sobol / fixed-grid 三 sampling 策略；(b) `dnv-opensource/ospx` 集成作 OSP 系统结构 author（FMU 拓扑配置）；(c) 双语言 schema 验证（Python `cerberus` + `pydantic` / C++ `cerberus-cpp`，同 schema 文件）；(d) maritime-schema v0.2.x 版本锁定 + breaking change 监控（PyPI release watch）。

**DoD**：
- [ ] `docs/Design/SIL/02-scenario-schema.md`（含 maritime-schema 扩展规约 + `metadata.*` 字段表）+ `scenario-traceability-matrix.csv` commit
- [ ] 场景独立 Git repo（或子目录）+ maritime-schema validation 在 CI 强制
- [ ] D1.3b.1 22 Imazu + 10 自编场景全部 maritime-schema-compliant
- [ ] `farn` v0.4.2+ 安装 + 1100-cell case folder dry-run（实际跑量 D3.6）
- [ ] `ospx` 集成 + own-ship FMU 系统结构配置可生成
- [ ] CI 三层集配置在 GitLab CI 启动（runner 资源 SLA）
- [ ] 关闭 finding ID：G P0-G-2 + G P1-G-6 + P2-G-2 + E P1-E2 + SIL P0 SIL-3 (NEW v3.1)

**Demo Charter**：6/15 DEMO-1 现场展示 PR 触发 Smoke 10（Imazu 子集）跑通 + maritime-schema 校验 + farn 1100-cell case folder dry-run 截图 + 场景缺陷流（fail → issue → 回归断言）演示。

---

#### D1.7 · 覆盖率方法论文档（NEW v3.0）

| 属性 | 值 |
|---|---|
| 预计人周 | 1.5 |
| 目标完成 | 2026-06-15 |
| Owner | **V&V 工程师** + M6 负责人 + 安全工程师 |

**Scope**：MC/DC（modified condition decision coverage）on Rule 决策树 + Rule × ODD × 扰动立方体（11 Rule × 4 ODD × 5 扰动级别 = 220 cell × 5 seed = **1100 concrete cases**，详见架构 v1.1.3-pre-stub 附录 F.6 + 决策记录 §6）+ ISO 21448 SOTIF triggering condition 覆盖（≥ 50 条）+ failure response 覆盖（M7 6 类硬约束 × 10 场景）。**v3.1 新增**：(a) **6 维度结构化 COLREGs 评分 rubric**（Hagen 2022 [R33] / Woerner 2019 [R34]）规约 — Safety score（CPA-based 连续）/ Rule compliance score（per-rule {full/partial/violated} 加权）/ Delay penalty / Action magnitude penalty / Phase score（give-way / stand-on）/ Trajectory implausibility；权重 w_s / w_r / w_p + per-rule 准则细节须引 Hagen 2022 / Woerner 2019 原文；PASS/FAIL 二元 verdict 保留并存（详见架构 F.8 + 决策记录 §8）；(b) **Monte Carlo LHS / Sobol 大样本扫描方法论** — 在 1100-cell deductive cube 之外，对关键参数（target ship 初始 bearing、SOG、感知噪声 σ、风流强度）跑 LHS / Sobol 抽样，10000 sample 输出 pass rate 95% CI + CPA min 分布 + Rule violation 频率（实际跑量在 D3.6 扩展）；证据 [R32] Hassani 2022 Sobol + COLREG geometric filter；(c) **Adversarial / Nominal / Boundary = 60/25/15 比例**明标"内部启发式，非外部标准"，CCS 提交时不引为外部规范。

**DoD**：
- [ ] `docs/Design/SIL/03-coverage-metrics.md` commit（含 v3.1 新增 §6维度评分 rubric + §Monte Carlo 段 + §60:25:15 自我辩护）
- [ ] 覆盖率自动计算脚本（在 D1.3b.1 覆盖率报告基础上扩展，支持 1100-cell + LHS sample）
- [ ] 6 维度评分 rubric 算法规约文档（含权重 w 系数 + per-rule 准则表，引 [R33] [R34]）
- [ ] LHS / Sobol sample generator 配置（farn 集成）
- [ ] 关闭 finding ID：E P0-E3 + G P0-G-4 + SIL P0 SIL-4 (NEW v3.1)

**Demo Charter**：6/15 DEMO-1 展示当前 32 场景（22 Imazu + 10 自编）的立方体覆盖率热图（1100 cell 中亮 ~32 个，合理 baseline）+ 6 维度评分输出样例（每场景 score breakdown）。

---

#### D1.8 · cert-evidence-tracking.md + ConOps v0.1 stub（NEW v3.0）

| 属性 | 值 |
|---|---|
| 预计人周 | 1.0 |
| 目标完成 | 2026-06-15 |
| Owner | **架构师 + PM** + CCS 联络人 |

**Scope**：CCS 9 类证据 ↔ D 编号跟踪表；ConOps v0.1 stub（5–10 页核心场景 + 角色 + 边界 + 操作模式描述，AIP 阶段一头号必需件）。**v3.0 注**：ALARP 完整论证 + SDLC 全表 推到 v1.1.4（B3）；本 D 仅出 ConOps stub + HARA 占位（HARA 实例化在 D2.7）。**v3.1 新增**：**maritime-schema CCS 接受度早期发函** — D1.8 末向 CCS 技术中心 + Brinav 联络人发函确认 `dnv-opensource/maritime-schema` v0.2.x 作 evidence container 的接受度（路径：CCS-DNV-Brinav 2024 MoU + Brinav Armada 78 03 案例先例）；若 CCS 要求中文专用格式，maritime-schema 退为内部表示 + 加导出器（详见决策记录 §5.5 推翻信号）。证据：[R27] [R25] DNV-RP-0513 + DNV-Brinav 2024 MoU。

**DoD**：
- [ ] `docs/Design/Cert/cert-evidence-tracking.md` commit
- [ ] `docs/Design/ConOps/01-conops-v0.1.md` commit
- [ ] CCS 项目组收到 ConOps stub + tracking 表邮件回执
- [ ] **v3.1 NEW**：CCS 技术中心 / Brinav 联络人收到 maritime-schema 接受度查询邮件（含 schema 样例 + DNV-RP-0513 自鉴定证据预览）+ 邮件回执存档
- [ ] 关闭 finding ID：C P0-C-1 (a) + E P1-E8 + SIL P1 SIL-5 (NEW v3.1)

**Demo Charter**：6/15 DEMO-1 展示 ConOps 文档评审现场 + tracking 表对应矩阵。

---

### 🎬 DEMO-1 · Skeleton Live（6/15）

**Scenario**（端到端，约 5 min）：
1. AIS 历史数据回放（NOAA 1h 片段）→ Mock M2（D1.1 mock publisher）→ 仿真器 own-ship 运动
2. 1 个 Crossing 场景脚本 live → SIL HMI 显示 own-ship 接近目标 → ODD 评估（Mock M1） → SAT-1/2/3 视图（wireframe 级，但 SAT-1 状态文字真实）
3. PR-trigger Smoke 10 跑通 + 覆盖率立方体热图（10 cell 亮）
4. ConOps PDF + V&V Plan PDF + cert-evidence-tracking.md 现场展示
5. 仿真器参考解（旋回）实测 + tool confidence 报告

**Audience × View**：
- 业主：项目可视化 + 资源到位证明
- PM：进度可视化
- CCS 早期试映：V&V Plan + ConOps + cert tracking 框架可接受性
- 资深船长：SAT-1 wireframe 第一次直觉性反馈（即使 mock 也能挑毛病）
- HAZID 干系人：场景库 schema + traceability 矩阵的工作方式

**Visible Success**：
- D1.1–D1.8 + D1.3.1 全部 DoD 通过 ✅
- CCS 邮件回执 ≥ 1 份（V&V Plan 或 ConOps）
- 资深船长现场反馈 ≥ 3 条（不论正负，都入 D2.6 访谈输入）
- 业主签字资源到位

**Showcase Bundle**：
- DEMO-1 视频（≤ 5 min，剪辑版）
- 场景 YAML（5 个 demo 场景）
- ASDR JSONL（即使 mock 也要展示完整 schema）
- ConOps + V&V Plan + Sim Qualification 三份 PDF
- "What this means for TDL" 200 字（PM 起草）

**Phase 1 关闭 finding 累计**：MUST-1..11（D0）+ A P1-A8 + E P0-E1/E3 + E P1-E1/E2/E4/E5/E8 + F P1-F-04/F-06 + G P0-G-1/G-2/G-3/G-4 + G P1-G-4/G-5/G-6 + P2-E1/E5 + P2-G-2 + C P0-C-1 (a) + 多船型 MV-1/2/3/5/6/7。

### Phase 1 关键风险

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| R1.1 | DEMO-1 mock M2/M6 被业主误读为"进度空心化" | 中 | DEMO-1 narrative 强调 "skeleton + scenario schema 走通"是 Phase 1 的真实里程碑；CCS 邮件回执 + 资深船长签字反馈作为客观证据 |
| R1.2 | V&V 工程师 5/8 onboard 失败 | **高** | A1 闭环刚性依赖；R0.2 已识别；不到位则 D1.5/D1.6 推 1 周 + DEMO-1 拆为 6/15 简版 + 6/22 完整版 |
| R1.3 | CasADi/IPOPT 编译复杂阻塞 SIL 框架 | **解决** | D1.3a 已用 RK4 积分 + 标准 Yasukawa MMG（不简化）；单元测试验证 ≤5% 参考解误差；CasADi/IPOPT 留 Phase 3 M5 优化 |
| R1.4 | ChAIS 授权未到位 | 中 | NOAA AIS 优先（格式相同）；ChAIS 授权同步申请 |
| R1.5 | CI 首次启用 PATH-S 检查暴露大量 violations | 高 | D0.3 已让 dry-run 提前 1 周收集 violation 清单；本 D 仅升级强制 |
| R1.6 | 资深船长 6/15 不愿到场 | 中 | PM 5/13 起预约；备选业主船员部门代理船长 |

---

## 4. Phase 2 · M1/M2/M3/M6 + Cert + HF + 架构 stub（6/16–7/31）

**时间**：2026-06-16 → 2026-07-31（6.5 周）  
**估计人周**：30.0  
**阶段目标**：实现 4 个 L3 拓扑前端模块 + Cert 4 件中的 HARA + 船长 ground truth 工作流 + 架构 v1.1.3 stub。每模块完成后立即进入 SIL 验证。**Phase 2 末交付 DEMO-2：Decision-Capable**（M1-M6 决策真实，50 场景 ≥ 95%）。

### Phase 2 甘特

| 工作流 | W6 6/16-22 | W7 6/23-29 | W8 6/30-7/6 | W9 7/7-13 | W10 7/14-20 | W11 7/21-31 |
|---|---|---|---|---|---|---|
| D2.1 M1 | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓ | | | |
| D2.2 M2 | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓ | | |
| D2.3 M3 | | | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓ | |
| D2.4 M6 | | ▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ | |
| D2.5 SIL 集成 (M1-M6) | | | | ▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ |
| D2.6 船长 ground truth [HF 咨询] | ▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓ | | |
| D2.7 HARA + FMEDA M1 [安全工程师] | ▓▓ | ▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓ | | |
| D2.8 v1.1.3 stub | | | ▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓ |
| ⟋ HAZID RUN-001 | 第 ④ 次 | — | 第 ⑤ 次 | — | 第 ⑥ 次 + ★ full-day | 第 ⑦ 次 |
| 🎬 **DEMO-2 Decision-Capable** | | | | | | ★ 7/31 |

### Phase 2 交付物

#### D2.1 · M1 ODD/Envelope Manager

| 属性 | 值 |
|---|---|
| 预计人周 | 5.0（v2.0 4.0 → +1.0：FMEDA 表 stub + ToR 自适应矩阵接口）|
| 目标完成 | 2026-07-06 |
| Owner | M1 负责人 |

**Scope（v3.0 修订）**：原 M1 v2.0 全功能 + (a) FMEDA 表 stub（≥ 10 失效模式 / 表）+ (b) `tor_deadline_s` 改为场景自适应矩阵（ROC 已坐席 60s / 船长在桥楼 30s / 餐厅 90s / 睡舱 120s [HAZID 校准]）+ (c) `ToR_RequestMsg` 加 `assumed_operator_state` 字段 + (d) `ROT_max(u)` 曲线读 Capability Manifest（不硬编）。

**DoD**：v2.0 D2.1 全部 + FMEDA M1 表 v0.1 + ToR 自适应矩阵 + Capability Manifest 集成。

**Demo Charter**：DEMO-2 中 ODD-A→D 切换 live + ToR 4 场景矩阵动画。

**关闭 finding**：D P0-D-02 + D P1-D-04（部分）+ MV-7（部分）+ C P1-C-8（FMEDA 部分）。

---

#### D2.2 · M2 World Model

| 属性 | 值 |
|---|---|
| 预计人周 | 5.5（v2.0 5.0 → +0.5：env sanity check + intent_distribution）|
| 目标完成 | 2026-07-06 |
| Owner | M2 负责人 |

**Scope（v3.0 修订）**：v2.0 全功能 + (a) sog 校验 `f(Manifest.max_speed × 1.2)`（D0.1 stub → 本 D 实现）+ (b) OVERTAKING 扇区 [112.5°, 247.5°]（D0.1 已修订）+ (c) `intent_distribution` 字段（B P1-B-02 + RFC-007/008）+ (d) 环境字段 sanity check（visibility/Hs/current 范围 + 跨源 + staleness）+ (e) confidence ∈ [0,1] 全字段。

**DoD**：v2.0 D2.2 全部 + sog/扇区/intent/env 4 项 v3.0 修订全部测试通过。

**Demo Charter**：DEMO-2 中 10 目标场景的 CPA/TCPA 实时计算 + COLREG 几何预分类标签。

**关闭 finding**：B P0-B-03 + B P1-B-02 + B P1-B-05 + F P0-F-04（部分）+ MV-2。

---

#### D2.3 · M3 Mission Manager

| 属性 | 值 |
|---|---|
| 预计人周 | 3.0 |
| 目标完成 | 2026-07-13 |
| Owner | M3 负责人 |

**Scope（v3.0 修订）**：v2.0 全功能 + (a) 海流误差等级 中→高（F P0-F-04 整改）+ (b) L1 VoyageTask + L2 PlannedRoute 双订阅 RFC（F P1-F-01）决议执行。

**DoD**：v2.0 D2.3 全部 + 海流误差告警链路验证 + L1/L2 双订阅独立性测试。

**Demo Charter**：DEMO-2 中 ODD-B 进入触发 RouteReplanRequest live。

**关闭 finding**：F P0-F-04（部分，海流）+ F P1-F-01。

---

#### D2.4 · M6 COLREGs Reasoner

| 属性 | 值 |
|---|---|
| 预计人周 | 5.0 |
| 目标完成 | 2026-07-31 |
| Owner | M6 负责人 |

**Scope（v3.0 修订）**：v2.0 全功能 + DoD 分级（首跑 ≥ 90% / 修缮 ≥ 95% / 仅 D3.6 1000 场景才要 ≥ 98%）。500+ 场景目标不变（用户未取 B1）。**v3.1 新增**：(a) **6 维度结构化 COLREGs 评分实施**（D1.7 规约 → 本 D 实装）— Safety / Rule compliance / Delay penalty / Action magnitude / Phase / Trajectory implausibility 6 维度连续评分输出，与 PASS/FAIL 二元 verdict 并存（详见架构 v1.1.3-pre-stub 附录 F.8 + 决策记录 §8；证据 [R33] [R34]）；(b) **AIS-derived encounter 真实重现**（消费 D1.3b.2 工具产出的 maritime-schema YAML，从 Kystverket / NOAA 真实 AIS 切片提取的 encounter）；(c) **`ncdm_vessel` 模式实装**（NCDM Ornstein-Uhlenbeck 短时外推 [R37]）— 用于 target ship track 用尽后的延续；(d) **`dnv-opensource/ship-traffic-generator` (`trafficgen`)** 集成 [R27] — 50 场景扩展至 200 场景（trafficgen 参数化扫频 head-on / crossing / overtaking + 相对方位 sweep）。

**DoD**：v2.0 D2.4 全部，首次跑通门槛 ≥ 90%（避免假绿灯）+ 6 维度评分 per-scenario 输出（Apache Arrow 列存）+ ≥ 5 个 AIS-derived encounter 在 50 场景中重现验证 + `ncdm_vessel` 模式至少 5 场景跑通 + trafficgen 扩展 50→200 场景全部 maritime-schema-compliant。

**Demo Charter**：DEMO-2 (7/31) 中 50 综合场景 live + Rule 13-17 决策链可视化 + COLREGs 覆盖率立方体热图 + **6 维度评分分布报告**（每场景 score breakdown） + AIS-derived encounter 真实重现 vs SIL run 双线对比。

**关闭 finding**：E P1-E6 + SIL P0 SIL-6 (NEW v3.1, 6维度评分实装)。

---

#### D2.5 · M1-M6 SIL 集成测试报告（v3.0 修订）

| 属性 | 值 |
|---|---|
| 预计人周 | 2.5（v2.0 2.0 → +0.5：covering matrix + 端到端 KPI）|
| 目标完成 | 2026-07-31 |
| Owner | **V&V 工程师**（独立 owner，不再是技术负责人）|

**Scope（v3.0 修订）**：50 综合场景含 ODD-A/B/C/D × 5 类扰动 cell；端到端延迟 KPI 实测；ASDR 同格式输出（跨 SIL/HIL/实船）。**v3.1 新增**：(a) **L1 三模 mock `rosbag` 模式实装**（D1.3b.2 已含 synthetic + ais_replay；本 D 加 rosbag 模式 — 复用 ROS2 `bag_record` 录制工具链 + 公开数据集如 NTNU R/V Gunnerus rosbag + 转录 Kystverket AIS 至 rosbag 格式）；(b) **Web HMI Phase 2 增项**（D1.3b.3 后续）— Apache Arrow IPC scenario replay + GSAP timeline scrubber（任意时间点 < 100ms 跳转）+ Puppeteer headless GIF/PNG batch render（50 场景每场景 1 GIF + 关键帧 PNG）+ 多 target 同屏（≥ 5 船 multi-encounter）+ Grounding hazard 高亮（own-ship 预测路径 vs ENC 陆地/浅水 polygon 碰撞检测）+ TLS/WSS 安全；(c) **dds-fmu latency 实测验证**（D1.3c 集成验证）— P95 ≤ 10 ms / P99 ≤ 15 ms，与 V&V Plan §SIL latency budget 对齐；(d) **D1.3c FMI bridge 端到端集成测试** — own-ship MMG FMU + ROS2 native M1-M6 + M7 (ROS2 native) checker 联合 SIL run 1h 无崩溃。

**DoD**：v2.0 D2.5 全部 + cell 覆盖矩阵 ≥ 80% + 端到端延迟 KPI 通过 + ASDR schema 一致 + **v3.1 NEW**：L1 三模 mock 全部跑通（synthetic / ais_replay / rosbag）+ Web HMI Apache Arrow replay + 50 场景 GIF evidence pack 一键产出 + dds-fmu latency P95 ≤ 10 ms 实测达标 + own-ship FMI bridge 1h SIL 不崩溃。

**Demo Charter**：DEMO-2 中是核心舞台。**v3.1 增量**：50 场景批量 GIF evidence pack 一键产出 + 时间线 scrubber 任意点回放 + L1 rosbag mode 跑历史真实 R/V Gunnerus 数据。

**关闭 finding**：E P0-E5（V&V 独立 owner）+ E P0-E3（部分）+ E P1-E5 + SIL P0 SIL-7 (NEW v3.1, rosbag mode + Apache Arrow replay)。

---

#### D2.6 · 船长 Ground Truth 工作流（NEW v3.0）

| 属性 | 值 |
|---|---|
| 预计人周 | 3.0 |
| 目标完成 | 2026-07-13 |
| Owner | **HF 咨询外包**（主持）+ M8 负责人 + PM |

**Scope**：(a) 5 名船长访谈（≥ 3 FCB + 1 拖船 + 1 渡船；视线分布 / 扫视模式 / 注意优先级）+ (b) Figma 半保真原型（船长视图 + ROC 视图，对称双角色）+ (c) 桌面可用性测试 n ≥ 5 + (d) ECDIS 集成草案（IHO S-100 / IEC 61174）+ (e) 培训胜任力矩阵草案（基于 STCW 二副 + ROC 增项）+ (f) BNWAS 等价机制设计（IMO MSC.282(86) 对齐）。**v3.1 新增**：(g) **Figma 原型对齐 D1.3b.3 Web HMI 设计**（IEC 62288 SA subset + IMO S-Mode + 现 HTML 视觉风格延续），保证可用性测试与最终 HMI 一致；(h) **可用性测试新增"决策透明性理解度"问询**：在系统做出决策时，向船长询问"您是否理解系统为何如此行动？"并记录理解分数（直接对应 IMO MASS Code "有意味的人为干预"要求 + IEC 62288 [R35]）；(i) **船长访谈纪要进 v1.1.3 stub §12.3 心智模型 ground truth**（详见架构 v1.1.3-pre-stub 附录 F.9 + 决策记录 §9）。

**DoD**：
- [ ] 5 份访谈纪要 commit 到 `docs/Design/HF/01-captain-interviews/`
- [ ] Figma 链接 + 截图 commit
- [ ] 可用性测试报告（n ≥ 5）commit
- [ ] ECDIS 集成草案 + BNWAS 设计入 D2.8 v1.1.3 stub
- [ ] 培训胜任力矩阵 v0.1 commit（D3.5' 基础）
- [ ] ≥ 1 名资深船长签字"可用性认可"
- [ ] 关闭 finding：D P0-D-01（部分）+ D P0-D-03 + D P0-D-05 + D P1-D-04（BNWAS）+ D P1-D-05 + D P2-D-04 + MV-3（部分）

**Demo Charter**：
- Scenario：DEMO-2 中"船长访谈视频片段"（剪辑 ≤ 2 min）+ Figma 原型同屏 + 资深船长 live 反馈
- Audience × View：业主 / CCS / 全员
- Visible Success：船长签字 + Figma 链接 + 培训矩阵草案
- Showcase Bundle：5 份访谈纪要 PDF + Figma 链接 + 可用性报告 + ECDIS 集成草案

---

#### D2.7 · HARA 实例化 v0.1 + FMEDA M1（NEW v3.0）

| 属性 | 值 |
|---|---|
| 预计人周 | 2.5 |
| 目标完成 | 2026-07-13 |
| Owner | **安全工程师外包** + 架构师 |

**Scope**：HARA 逐危险源建表 ≥ 30 项；每项 → SIF → SIL 等级 → 缓解 → 残余风险；与 ADR-001/002/003 + ODD 4 子域 × 3 健康态对齐；FMEDA M1 表 ≥ 20 失效模式（D2.1 stub 升级为完整表）。**v3.0 注**：ALARP 完整论证 + SDLC 全表 推 v1.1.4（B3）；本 D 仅出 HARA + FMEDA M1。

**DoD**：
- [ ] `docs/Design/Safety/HARA/01-hara-v0.1.md`（≥ 30 危险源）commit
- [ ] `docs/Design/Safety/FMEDA/M1-fmeda-v1.0.md` commit
- [ ] 与 v1.1.2 §11.4 SIL 分配表对齐
- [ ] 关闭 finding ID：C P0-C-1 (b)（HARA）+ C P1-C-8（M1 FMEDA）+ C P0-C-3（部分，独立性矩阵在 D3.3a）

**Demo Charter**：DEMO-2 中 HARA 文档评审现场 + 1 个危险源到 SIL 评估的全链路演示。

---

#### D2.8 · 架构 v1.1.3 stub（NEW v3.0）

| 属性 | 值 |
|---|---|
| 预计人周 | 3.0 |
| 目标完成 | 2026-07-31 |
| Owner | 架构师 + M2/M3/M5/M7/M8 模块负责人各支援 |

**Scope（v3.0 新章节 stub）**：
- §16 Cybersecurity Spec（trust zone 划分 + §15.2 接口加 4 列 auth/integrity/replay/dds-security profile + ASDR HMAC 升级 stub）
- §15.0 时基与同步（IEEE 1588 PTPv2 grandmaster + sync error budget + 退化路径）
- §12.5 人员资质 / 培训 / 演练（D2.6 培训矩阵入此章节）
- §12.3 船长心智模型 ground truth（D2.6 访谈结果入此章节）
- §10.5 4-DOF 适用性边界声明（hull_class = SEMI_PLANING 触发 6-DOF 预留）
- §13' 测试策略章节（V&V Plan 引入）
- §10.1 算法选型矩阵（MPC vs RRT* vs VO vs MPPI 4×4）
- §11.x L3 仲裁优先级矩阵（Reflex/Override/Checker，升入主文档）
- §15 IDL `schema_version` + QoS 4 列
- 4 缺失模块 stub-only（B4 standby）：ENC Manager / Parameter Store / Environment Cross-Source Validator / BNWAS-equivalent
- ToR 自适应矩阵入 §3.4

**v3.1 新增（合并 v1.1.3-pre-stub 附录 F 主体迁入正章主体）**：
- **新 §17 SIL 框架架构**（合 v1.1.3-pre-stub 附录 F.1-F.3）：选项 D 混合架构图 + DNV 工具链 3 MUST + 2 NICE-deferred + ROS2 Humble + Ubuntu 22.04 + PREEMPT_RT 锁定 + M7 严格 ROS2 native 边界规则 + dds-fmu latency budget + L1 三模 mock 策略
- **新 §18 RL 隔离架构**（合 v1.1.3-pre-stub 附录 F.4）：L1 Repo / L2 Process / L3 Artefact 三层强制边界 + Phase 4 启动 audit checklist + DNV-RP-0671/0510/0513 引用
- **新 §19 场景库与覆盖立方体方法论**（合 v1.1.3-pre-stub 附录 F.5-F.7）：maritime-schema 扩展 schema 规约 + Imazu-22 强制基线 + 1100-cell 立方体 + Monte Carlo LHS/Sobol + AIS-driven scenario 工程模式 + 3 target 模式
- **新 §20 结构化 COLREGs 评分**（合 v1.1.3-pre-stub 附录 F.8）：6 维度 rubric + 权重系数 + per-rule 准则表（D1.7 + D2.4 实施回填）
- **新 §21 Web HMI 与 ENC 集成**（合 v1.1.3-pre-stub 附录 F.9）：MapLibre + S-57 MVT + foxglove_bridge + IEC 62288 SA subset + S-Mode + Apache Arrow replay + ToR 倒计时 + Doer-Checker verdict badge
- §16 参考文献顺延为 §22；§17/§18/.../§22 章节顺延（详见 D3.8 完整化时统一编号）

**DoD**：
- [ ] 架构 v1.1.3-stub commit（D3.8 完整化）
- [ ] 4 缺失模块各 ≥ 1 章节 stub（接口位 + 责任声明，实现推 Phase 4）
- [ ] CCS / DNV review 邀请发出
- [ ] **v3.1 NEW**：v1.1.3-pre-stub 附录 F 全部 9 节迁入正章主体（§17-§21），决策记录 `docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md` 仅作历史归档
- [ ] **v3.1 NEW**：CCS / DNV review 邀请加入 maritime-schema 接受度反馈（继 D1.8 早期发函）
- [ ] 关闭 finding ID：F P0-F-01/F-02/F-04 (stub) + C P1-C-3/C-5（部分）+ B P1-B-06 + F P1-F-03 + D P1-D-08（Y-axis 通告）+ MV-7/8/9/10/11 + SIL P0 SIL-1~7 全部入正章 (v3.1)

**Demo Charter**：DEMO-2 中 v1.1.3-stub 全文 review + 4 缺失模块接口图 + **v3.1 NEW**：§17 SIL 框架 + §18 RL 隔离 + §19 场景库 + §20 评分 + §21 Web HMI 全部章节现场 walkthrough。

---

### 🎬 DEMO-2 · Decision-Capable, Single-Ship（7/31）

**Scenario**（端到端，约 8 min）：
1. 50 综合场景 live（Head-on / Crossing / Overtaking / Restricted-vis × ODD-A/B/C/D × 5 扰动）
2. ODD-A → ODD-D 切换 live + COLREGs Rule 13-17 决策链可视化（SAT-2 全展开）
3. 船长访谈视频片段（5 名 ≤ 2 min 剪辑）+ Figma 原型同屏 + 资深船长现场反馈
4. HARA 文档评审 + 1 个 SIF 全链路 live
5. v1.1.3-stub 4 缺失模块接口图 + cyber §16 + 时基 §15.0
6. 端到端延迟 KPI 实测仪表盘（AvoidancePlan P95 / Mid-MPC / M2→M6）
7. ASDR JSONL 完整签名链 demo（schema_version + stamp + confidence + rationale）

**Audience × View**：
- 资深船长（≥ 1 人）：SAT-1 是否合直觉 / Figma 原型可用性
- ROC 操作员代表：SAT-2 决策链是否可读
- CCS 中期意见会议：HARA + V&V Plan + cert tracking + v1.1.3-stub
- 业主决策层：Phase 2 进度 + Phase 3 资源决策点
- DNV/TÜV/BV 接洽（早期试映 Phase 4 SIL 2 评估）

**Visible Success**：
- D2.1–D2.8 全部 DoD 通过
- 50 场景 ≥ 95%（首跑 D2.4 + 修缮 D2.5）
- CCS 中期意见邮件回执
- 船长 ≥ 1 签字"可用性认可"
- 端到端延迟 KPI 全部 PASS

**Showcase Bundle**：DEMO-2 视频（≤ 8 min）+ 50 场景 YAML + ASDR 全集 + 船长访谈纪要 + Figma 链接 + HARA + v1.1.3-stub PDF。

**Phase 2 关闭 finding 累计**：B P0-B-03 + B P1-B-02/B-05 + C P0-C-1 (b)/C-3（部分）/C-8（M1）/P1-C-3/C-5（部分）+ D P0-D-01（部分）/D-02/D-03/D-05/P1-D-04/D-05/D-08 + E P0-E3/E-5 + F P0-F-01/F-02/F-04（stub）/P1-F-01/F-03 + 多船型 MV-7..11。

### Phase 2 关键风险

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| R2.1 | M6 500+ 场景首跑 ≥ 90% 不达标 | 中 | DoD 已分级（首跑 ≥ 90% / 修缮 ≥ 95%）；不达 90% 则 D2.4 顺延 1 周到 8/7，触发 contingency |
| R2.2 | HF 咨询 6/16 onboard 失败 | **高** | A1 闭环依赖；R0.2 已识别；不到位则 D2.6 推 1 周 + DEMO-2 简化船长访谈环节 |
| R2.3 | 6/22 端午节压缩 W6 时间 | 低 | W6 主要是模块独立工作 |
| R2.4 | v1.1.3-stub 4 缺失模块设计争议 | 中 | B4 standby（推 Phase 4 完整实现）已是默认路径；本 D 仅设计接口位 |

---

## 5. Phase 3 · M4/M5/M7/M8 + SIL 1000 + 架构终稿（7/13–8/31）

**时间**：2026-07-13 → 2026-08-31（7.5 周，与 Phase 2 重叠 2.5 周）  
**估计人周**：37.5  
**阶段目标**：完成 4 个剩余模块（M4 行为仲裁 / M5 双 MPC / M7 拆 core+sotif / M8 HMI 完整）。HAZID 8/19 完成 → v1.1.3 回填。SIL 扩展至 1000+ 作为 CCS 认证证据 + RFC-007 cyber 接口 commit + v1.1.3 完整化。**Phase 3 末交付 DEMO-3：Full-Stack with Safety + ToR**。

### Phase 3 甘特

| 工作流 | W12 7/13-19 | W13 7/20-26 | W14 7/27-8/2 | W15 8/3-9 | W16 8/10-16 | W17 8/17-23 | W18 8/24-31 |
|---|---|---|---|---|---|---|---|
| D3.1 M4 | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓ | | | | |
| D3.2 M5 | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓ | | | |
| D3.3a M7-core | | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓ | | |
| D3.3b M7-sotif | | | ▓▓ | ▓▓▓▓▓ | ▓▓▓▓ | | |
| D3.4 M8 完整 | | | ▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓ | |
| D3.5 v1.1.3 回填 | | | | | | ▓▓▓ | ▓▓ |
| D3.5' 培训课程大纲 | | | | ▓ | ▓▓▓ | ▓ | |
| D3.6 SIL 1000+ | | | | scenario 扩展 | ▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ |
| D3.7 8 模块全集 | | | | | ▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ |
| D3.8 v1.1.3 完整 | | | | | ▓▓ | ▓▓▓ | ▓▓▓ |
| D3.9 RFC-007 cyber | | | | ▓▓ | ▓▓ | | |
| ⟋ HAZID RUN-001 | 第 ⑧ 次 | — | 第 ⑨ 次 + ★ full-day | — | 第 ⑩ 次 | **★ 8/19 完成** | → v1.1.3 回填 |
| 📋 HIL 采购需求 | **★提交** | | | | | | |
| 🎬 **DEMO-3 Full-Stack** | | | | | | | ★ 8/31 |

### Phase 3 交付物

#### D3.1 · M4 Behavior Arbiter（v3.0 修订）

| 属性 | 值 |
|---|---|
| 预计人周 | 4.0 |
| 目标完成 | 2026-07-26 |
| Owner | M4 负责人 |

**Scope（v3.0 修订）**：v2.0 全功能 + IvP 路径按 D0.2 RFC-009 决议执行（libIvP / 自实现二选一）+ confidence 度量定义（P2-B-02 整改）。

**DoD**：v2.0 D3.1 全部 + RFC-009 决议路径实现 + confidence 字段 ASDR 写入。

**Demo Charter**：DEMO-3 中 8 行为切换 live + IvP 求解时间分布。

**关闭 finding**：B P0-B-01 + P2-B-02。

---

#### D3.2 · M5 Tactical Planner（v3.0 修订）

| 属性 | 值 |
|---|---|
| 预计人周 | 6.5（v2.0 7.0 → -0.5：FM-4 删除 + MRM 走 M7 简化）|
| 目标完成 | 2026-08-10 |
| Owner | M5 负责人 |

**Scope（v3.0 修订）**：v2.0 全功能 + (a) Mid-MPC N=18 / 90s 锁定（D0.1 已对齐）+ (b) BC-MPC SLA 改 < 150ms / 8Hz（实测后再固化，B P0-B-04）+ (c) ROT_max 曲线读 Capability Manifest（D0.1 stub → 本 D 实现）+ (d) FM-4 hardcoded fallback 删除（D0.1 已删，本 D 验证）+ (e) MRM 调用走 M7 路径（D0.1 已修订，本 D 实现）+ (f) IPOPT KKT residual 入 ASDR + (g) urgency_level > 0.95 时 BC-MPC 扩 ±60°（P2-B-01）。

**DoD**：v2.0 D3.2 全部 + 6 项 v3.0 修订全部测试通过 + p99 实测数据 commit。

**Demo Charter**：DEMO-3 中 Mid-MPC + BC-MPC 双层切换 live + 紧急转向 ±60° 兜底 demo。

**关闭 finding**：B P0-B-02/B-04 + B P1-B-01/B-08 + P2-B-01/B-04 + MV-1。

---

#### D3.3a · M7-core（v3.0 拆分）

| 属性 | 值 |
|---|---|
| 预计人周 | 6.0 |
| 目标完成 | 2026-08-10 |
| Owner | M7 负责人 + 安全工程师外包 |

**Scope**：6 类硬约束（< 10ms）+ IEC 61508 watchdog + ASDR SHA-256 + Doer-Checker 三可量化矩阵（LOC ≥ 50:1 / 圈复杂度 ≥ 30:1 / SBOM ∩ = ∅）+ FMEDA M7 表（v3.0 NEW，从 D2.7 思路复用）+ PATH-S 独立性 0 violation。

**DoD**：v2.0 D3.3 主体（IEC 61508 + 6 类硬约束）+ Doer-Checker 三量化矩阵 commit + FMEDA M7 表（≥ 20 失效模式）+ PATH-S CI 0 violation。

**Demo Charter**：DEMO-3 中 6 类故障注入 live + Doer-Checker 独立性证明现场展示。

**关闭 finding**：C P0-C-3 + C P1-C-8（M7 FMEDA）。

---

#### D3.3b · M7-sotif（NEW v3.0，MUST-11 拆分）

| 属性 | 值 |
|---|---|
| 预计人周 | 3.0 |
| 目标完成 | 2026-08-16 |
| Owner | M7 负责人 + 安全工程师外包 |

**Scope**：5 类假设违反检测（AIS/Radar 不一致 / 预测残差超限 / 感知盲区超比 / COLREGs 多次不可解析 / 通信链路失效）+ 100 周期 = 15s 滑窗（RFC-003）+ enum-only veto + SOTIF area 完整性 mapping（C P1-C-2）+ ISO 21448 §6 三类穷举证据。

**DoD**：v2.0 D3.3 SOTIF 子项 + SOTIF area mapping + 6 类假设统一（消除 4/6/6 三处不一致，C P1-C-1）。

**Demo Charter**：DEMO-3 中 AIS 目标丢失场景触发 SOTIF 告警 + 滑窗动画。

**关闭 finding**：C P1-C-1/C-2 + RFC-003 决议执行。

---

#### D3.4 · M8 HMI/Transparency Bridge 完整（v3.0 修订）

| 属性 | 值 |
|---|---|
| 预计人周 | 5.5（v2.0 4.0 → +1.5：双角色完整 + 50Hz 拆 3 档 + ToR 矩阵 + BNWAS + Y-axis + ECDIS）|
| 目标完成 | 2026-08-24 |
| Owner | M8 负责人 |

**Scope（v3.0 修订）**：v2.0 全功能 + (a) active_role 对称双角色完整实现（D0.1 stub → 本 D 实现）+ (b) UI 50Hz 拆 3 档（data_stream_50hz / display_state_4hz / alert_burst_event）+ (c) ToR 自适应矩阵（4 场景）+ (d) BNWAS-equivalent stub（B4 standby）+ (e) Y-axis Reflex Arc → M8 通告通路（ReflexArcActivatedMsg + REFLEX_ACTIVE 子态）+ (f) ECDIS 集成 stub（D2.6 草案落地）+ (g) URGENT_TOR < 30s 子分支（D P1-D-01）+ (h) BackupChannel_DisplayMsg（船端独立 backup，D P1-D-06）+ (i) 5s 强制等待条件分支（D P1-D-03）。**v3.1 新增（Web HMI Phase 3 增项 — 详见架构 v1.1.3-pre-stub 附录 F.9 + 决策记录 §9）**：(j) **Trajectory ghosting** — M5 BC-MPC 提议路径（虚线）vs L2 计划路径（实线）双轨叠加 + visual diff 高亮（[R35] IEC 62288）；(k) **ToR 倒计时 panel 完整可视化**（独立非雷达内 — 防 out-of-loop syndrome）— 4 操作员状态联动 D2.1 适配矩阵（ROC 已坐席 60s / 桥楼 30s / 餐厅 90s / 睡舱 120s）；(l) **M7 Doer-Checker verdict badge** + 决策链文字 rationale — 每帧 PASS/VETO + 引用规则文字（[R34] Woerner 2019 风格）；(m) **S-Mode 完整对齐**（IMO MSC.1/Circ.1609 [R36]）— colour palette + symbol set + control workflow 全套；(n) **1000 场景 evidence pack 全自动产出**（与 D3.6 联动）— 每场景 GIF + Apache Arrow log + traceability CSV 一键生成；(o) **Doer-Checker 独立可视化通道** — M7 verdict 单独通过 `/m7/sil_observability` 只读 ROS2 topic（G P1-G-1 整改），HMI 订阅但不影响 M7 决策；(p) **ECDIS 完整集成**（IHO S-100 / IEC 61174 + S-57 双兼容）— D2.6 草案落地。

**DoD**：v2.0 D3.4 全部 + 9 项 v3.0 修订 + **v3.1 NEW**：7 项 Web HMI Phase 3 增项全部测试通过（trajectory ghosting / ToR countdown / M7 verdict badge / S-Mode 完整 / 1000 evidence pack / Doer-Checker 独立通道 / ECDIS）+ 资深船长签字 ≥ 1（含决策透明性理解度评分）。

**Demo Charter**：
- DEMO-3 核心场景：操作员卧躺 → ToR 唤醒（**HMI 完整倒计时 panel + 4 状态适配 live**） → 走桥楼 → 60s/120s 内接管 → ASDR 签名 + **trajectory ghosting** 同步显示 M5 提议 vs L2 计划路径
- 6 类 ToR 场景矩阵 live demo
- BNWAS 三级告警（30s 闪灯 → 60s 二副 → 90s 船长）
- Y-axis Reflex Arc 模拟触发 + 红屏全屏蜂鸣
- **v3.1 NEW**：1000 场景 evidence pack 一键产出 demo（5 min 内完成 1000 GIF + Arrow log + CSV）+ M7 Doer-Checker verdict badge 实时显示

**关闭 finding**：D P0-D-04 + D P1-D-01/D-03/D-06/D-08 + D P2-D-01/D-04 + MV-3/12 + F MISS-9/10/11 + SIL P0 SIL-8 (NEW v3.1, Web HMI Phase 3 完整化) + G P1-G-1 (M7 独立观测通道)。

---

#### D3.5 · v1.1.3 HAZID 回填（v3.0 修订）

| 属性 | 值 |
|---|---|
| 预计人周 | 2.0 |
| 目标完成 | 2026-08-31 |
| Owner | 架构师 |

**Scope（v3.0 修订）**：132 [TBD-HAZID] 拆"安全热加载白名单"vs "需重跑回归列表"两类（E P1-E7 整改）；行为分支结构敏感参数（IvP pieces / T_standOn / Rule 19 触发能见度）在 7/31 前先锁定初值（不等 8/19，避免 12 日窗口压力）；其余热加载参数 8/19 后补。

**DoD**：v2.0 D3.5 全部 + 132 参数分类清单 + 重跑回归脚本（D1.3.1 配套）+ CCS v1.1.3 邮件记录。

**Demo Charter**：DEMO-3 中 v1.1.3 文档评审 + 5 个代表参数从 [TBD] 到 calibrated 的对比展示。

**关闭 finding**：E P1-E7。

---

#### D3.5' · 模拟器训练课程大纲（NEW v3.0）

| 属性 | 值 |
|---|---|
| 预计人周 | 1.5 |
| 目标完成 | 2026-08-23 |
| Owner | PM + HF 咨询外包 |

**Scope**：≥ 30 学时模拟器训练课程大纲（基于 STCW MASS 草案 + DNV HTW11）+ ToR drill ≥ 10 次脚本 + 透明度悖论案例库 + MRC 触发认知重建练习 + 培训胜任力矩阵 v1.0（D2.6 草案升级）。

**DoD**：
- [ ] `docs/Design/HF/02-training-curriculum-v1.0.md` commit
- [ ] 培训胜任力矩阵 v1.0 commit
- [ ] D4.5'（试航准入）的"≥ 2 名船长 + 2 名 ROC 模拟器认证"标准定义
- [ ] 关闭 finding：D P0-D-01（完整）。

**Demo Charter**：DEMO-3 中训练课程大纲评审 + 1 个 ToR drill 脚本现场演练。

---

#### D3.6 · SIL 1000+ 场景 COLREGs 覆盖率报告（v3.0 修订）

| 属性 | 值 |
|---|---|
| 预计人周 | 2.5 |
| 目标完成 | 2026-08-31 |
| Owner | **V&V 工程师**（独立 owner，v3.0 修订）|

**Scope（v3.0 修订）**：v2.0 全功能 + (a) Adversarial:Nominal:Boundary = 60:25:15 比例（D1.5 V&V Plan 锁定，**v3.1 注**：明标"内部启发式，非外部标准"）+ (b) HAZID 回填后仅跑"行为分支无关参数"全集（行为分支相关参数推 Phase 4 全集）+ (c) 立方体覆盖（11 Rule × 4 ODD × 5 扰动 × 5 seed = **1100 cell** ≥ 80% cell hit）+ (d) SOTIF triggering condition 覆盖 ≥ 80%。**v3.1 新增**（详见架构 v1.1.3-pre-stub 附录 F.6 + 决策记录 §6 §8）：(e) **Monte Carlo LHS / Sobol 大样本扫描**：在 1100-cell deductive cube 之外，对关键参数（target ship 初始 bearing / SOG / 感知噪声 σ / 风流强度）跑 LHS / Sobol 抽样 **10000 sample**（farn 集成），输出 pass rate 95% CI + CPA min 分布 + Rule violation 频率（[R32] Hassani 2022 工业先例）；(f) **6 维度结构化评分立方体**：traceability.csv 加列 `safety_score / rule_score / delay_pen / mag_pen / phase_score / total_score`（D1.7 + D2.4 实施统一在本 D 全量回填）；(g) **`intelligent_vessel` 模式（VO/简化 MPC）实装**：multi-agent encounter 场景（own + intelligent target 互动），support Rule 16 give-way / Rule 17 stand-on 动力学评估；(h) **Adversarial 60% 由 RL fuzzer 后置生成**（FREA / AuthSim 风格）— 本 D 仅 stub，实际 RL 对抗推 Phase 4 D4.6（B2 启动）；(i) **失败场景 → CCS 评据**：每失败场景产 GIF + traceability + 根因分析（D3.4 Web HMI 联动）+ public `report.failures.csv`（不掩饰）。

**DoD**：v2.0 D3.6 全部 + 1100 cell 立方体覆盖 ≥ 80% + SOTIF 覆盖 ≥ 80% + **v3.1 NEW**：LHS Monte Carlo 10000 sample 完成 + 95% CI 报告 + 6 维度评分全场景 traceability.csv + intelligent_vessel ≥ 50 multi-agent 场景跑通 + 1000 失败场景 evidence pack 一键产出（与 D3.4 HMI 联动）+ 失败场景根因 ≥ 80% 落到具体 Rule 子准则。

**Demo Charter**：DEMO-3 中 1000+ 场景批量结果 + 立方体覆盖热图 + 失败场景根因分析 + **v3.1 NEW**：LHS Monte Carlo 10000 sample 统计仪表盘 + 6 维度评分分布直方图 + intelligent_vessel multi-agent 场景 live + 1000 GIF evidence pack 一键产出。

**关闭 finding**：E P0-E3（完整）+ E P0-E2（部分窗口）+ E P1-E3 + G P0-G-4（完整）+ SIL P0 SIL-9 (NEW v3.1, Monte Carlo + 6维度立方体 + intelligent_vessel)。

---

#### D3.7 · 8 模块 SIL 全系统集成测试报告（v3.0 修订）

| 属性 | 值 |
|---|---|
| 预计人周 | 3.0 |
| 目标完成 | 2026-08-31 |
| Owner | 技术负责人 |

**Scope（v3.0 修订）**：v2.0 全功能 + (a) 8h 期间注入 ≥ 50 high-stakes 场景（MRC / Reflex / SOTIF 假设违反，G P1-G-8 整改）+ (b) M7 PATH-S 独立性 0 violation 自动验证 + (c) 只读 ROS2 topic `m7/sil_observability`（G P1-G-1）+ (d) §15.2 接口矩阵 24 行端到端对齐 + (e) ASDR 跨场地一致性。

**DoD**：v2.0 D3.7 全部 + 50 high-stakes 注入 + PATH-S CI 自动验证。

**Demo Charter**：DEMO-3 中是核心舞台。

**关闭 finding**：G P0-G-5（部分，HIL 等价契约在 Phase 4 完整）+ G P1-G-1/G-8 + E P0-E2（部分）。

---

#### D3.8 · 架构 v1.1.3 完整化（NEW v3.0）

| 属性 | 值 |
|---|---|
| 预计人周 | 2.5 |
| 目标完成 | 2026-08-31 |
| Owner | 架构师 + 各模块负责人 |

**Scope**：在 D2.8 stub 基础上完整化 9 个章节扩展 + 4 缺失模块 stub-only（B4 standby 触发则 Phase 4 完整）+ 算法选型矩阵 + L3 仲裁优先级矩阵 + IDL schema_version + QoS 列。**v3.1 新增**（继 D2.8 v1.1.3 stub 章节合入正章 §17-§21 后，本 D 完整化各章 — 详见架构 v1.1.3-pre-stub 附录 F 全 9 节）：(a) **§17 SIL 框架架构完整化**：选项 D 混合架构图（含 dds-fmu mediator + libcosim async_slave + jitter buffer 实测数据）+ DNV 工具链 license 验证表 + RP-0513 自鉴定证据全集（来自 D1.3.1 + D1.3c）+ ROS2 Humble + RT 性能基线；(b) **§18 RL 隔离架构完整化**：3 层边界图 + Phase 4 启动 audit checklist + DNV-RP-0671 RL FMU 鉴定流程 + git history audit 工具产出；(c) **§19 场景库与覆盖立方体方法论完整化**：maritime-schema 完整字段映射表 + Imazu-22 frozen hash 表 + 1100-cell 完整热图（D3.6 产出）+ Monte Carlo 10000 sample 95% CI 报告 + AIS-driven scenario 工程模式完整文档 + 3 target 模式实装报告（D2.4 + D3.6）；(d) **§20 结构化 COLREGs 评分完整化**：6 维度权重系数最终值（基于 D2.4 + D3.6 实测数据校准）+ per-rule 准则完整表（11 Rule × 子准则） + Hagen 2022 [R33] / Woerner 2019 [R34] 引用对齐；(e) **§21 Web HMI 与 ENC 集成完整化**：MapLibre + S-57 完整管线文档 + foxglove_bridge 性能基线 + IEC 62288 / S-Mode 完整 compliance matrix + 1000 evidence pack 自动化流程；(f) **决策记录归档**：`docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md` 标"已迁入正章 §17-§21，本文档作历史归档"；(g) **架构版本号升级 v1.1.3-pre-stub → v1.1.3**（完整版，HAZID 132 [TBD] 回填进 D3.5 + 本 D 合并）。

**DoD**：v1.1.3 主文档 commit + 各模块详设同步 + CCS v1.1.3 邮件记录 + **v3.1 NEW**：§17-§21 五新章节完整化 + Hagen/Woerner 评分权重最终值 + 决策记录归档 + 版本号 v1.1.3-pre-stub → v1.1.3 + DNV-RP-0513 自鉴定证据集完整提交。

**Demo Charter**：DEMO-3 中 v1.1.3 主文档全文展示 + 仲裁优先级图 + 4 缺失模块接口图 + **v3.1 NEW**：§17 SIL 框架 + §18 RL 隔离 + §19 场景库 + §20 评分 + §21 Web HMI 全章节正章版本展示。

**关闭 finding**：F P0-F-01/F-02/F-03/F-04（完整）+ B P1-B-06 + F P1-F-02/F-03/F-05/F-06 + 多船型 MV 全部归档 + SIL P0 SIL-1~9 全部 v3.1 (架构 v1.1.3 完整化合入正章)。

---

#### D3.9 · RFC-007 L3 ↔ Z-TOP/Cybersec 接口（NEW v3.0）

| 属性 | 值 |
|---|---|
| 预计人周 | 1.0 |
| 目标完成 | 2026-08-16 |
| Owner | 架构师 + CCS 联络人 + 安全工程师外包 |

**Scope**：L3 ↔ Z-TOP/Cybersec 边界 IACS UR E26/E27 责任划分 RFC（M8 ↔ ROC mTLS / Multimodal Fusion DDS-Security profile / ASDR HMAC 升级 stub / IACS UR E27 30 项 OT 安全能力中 L3 承担清单）。

**DoD**：
- [ ] `docs/Design/Cross-Team Alignment/RFC-007-L3-Cyber-Boundary.md` commit
- [ ] 7 RFC 总数（v2.0 6 + v3.0 RFC-007）入 RFC-decisions.md
- [ ] 关闭 finding：C P1-C-5 + F P0-F-01（cyber 边界部分）。

**Demo Charter**：DEMO-3 中 RFC-007 评审 + Cyber 边界图。

---

### 🎬 DEMO-3 · Full-Stack with Safety + ToR（8/31）

**Scenario**（端到端，约 12 min）：
1. 1000 场景 live 批量结果总览（≥ 98% pass + 立方体覆盖 ≥ 80% + SOTIF ≥ 80% + Adversarial 比例验证）
2. 8h 全模块集成 live 节选（≥ 50 high-stakes 场景注入）
3. M7 Doer-Checker veto live demo + 6 类硬约束故障注入 + Doer-Checker 独立性证明
4. ToR 接管 live：操作员从睡舱 → 120s 内接管 → ASDR 完整签名链
5. MRC 触发 live：M7 → M5 紧急轨迹注入（绕过 M4）
6. Y-axis Reflex Arc 模拟触发 + M8 红屏全屏蜂鸣
7. v1.1.3 完整文档全文展示 + cyber RFC-007 + 算法选型矩阵 + 仲裁优先级
8. 132 [TBD-HAZID] 全部回填的 5 个代表参数对比
9. 培训课程大纲 + ToR drill 脚本 + 培训胜任力矩阵 v1.0
10. 端到端延迟 KPI 实测仪表盘 (vs DEMO-2 进步)
11. 失败场景根因分析（不掩饰）

**Audience × View**：
- CCS 验船师正式查看：HARA + V&V Plan + 1000 场景 + cert tracking + v1.1.3 + 培训
- DNV/TÜV/BV SIL 2 评估初步会议：M7 PATH-S + Doer-Checker 三量化 + FMEDA + SOTIF 6 类
- 业主决策层：Phase 4 资源决策（HIL + 实船降级）+ 2027 Q1/Q2 认证级试航规划
- 资深船长 + ROC 操作员：完整 SAT-1/2/3 + ToR drill + ECDIS 集成预览

**Visible Success**：
- D3.1–D3.9 全部 DoD 通过 ✅
- 1000 场景 ≥ 98% + 立方体 ≥ 80% + SOTIF ≥ 80%
- CCS 项目组邮件回执"v1.1.3 + 1000 场景 + HARA + 培训矩阵 完整收到，准备 11 月 AIP 提交"
- DNV/TÜV/BV 至少 1 家书面意向继续 SIL 2 评估
- 资深船长 ≥ 2 签字"可用性认可"
- 8h 集成 0 P0 崩溃

**Showcase Bundle**：DEMO-3 视频（≤ 12 min 剪辑版）+ 1000 场景全集 ZIP + ASDR 全集 + v1.1.3 完整 PDF + HARA + FMEDA M1+M7 + 培训课程 + cert tracking + RFC-007 + 失败根因报告。

**Phase 3 关闭 finding 累计**：B P0-B-01/B-02/B-04 + B P1-B-01/B-08 + C P0-C-1 (b)/C-3 + C P1-C-1/C-2/C-5/C-8（M7）+ D P0-D-04 + D P1-D-01/D-03/D-06 + D P2-D-01/D-04 + E P0-E2/E-3 + E P1-E3/E-7 + F P0-F-01/F-02/F-03/F-04 + F P1-F-02/F-03/F-05/F-06 + G P0-G-4/G-5（部分）+ G P1-G-1/G-8 + 多船型 MV 全部归档 + P2-B-01/B-04。

### Phase 3 关键风险

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| R3.1 | M5 IPOPT 求解 p99 > 500ms | **高** | 先用线性化 Nomoto 一阶模型 + N 从 18 → 12；最坏 0.5 Hz；D1.3.1 + V&V Plan 已含实测计划 |
| R3.2 | M7 PATH-S CI 发现跨边界引用 | 高 | D0.3 + D1.2 已 dry-run 提前曝光；D3.3a 第一周修复所有 violation |
| R3.3 | HAZID 8/19 滑期 → D3.5 v1.1.3 回填窗口紧 | 中 | D3.5 已拆"行为分支无关参数 7/31 锁"+ "相关参数 8/19 后"，部分先行 |
| R3.4 | DEMO-3 12 min 节奏太赶 | 中 | 8/24 起 V&V 工程师每日 dry-run；8/29 完整 dry run；8/30 业主彩排 |
| R3.5 | HIL 硬件 9–11 月到货 | 中 | 7/13 提交需求单 + 主板/FPGA 即下；备选 SIL Cluster 横向扩展为"准 HIL" |
| R3.6 | DNV/TÜV/BV 8/31 前未接洽完成 | 中 | D3.7 + D3.8 完成时 PM 同步发邀请；DEMO-3 至少 1 家到场 |

---

## 6. Phase 4 展望（9/1–12/31）

> ⚠️ 不作硬交付承诺。实际内容将在 Phase 3 完成后的阶段评审（2026-08-31 DEMO-3 后）根据实际情况细化。

### Phase 4 展望甘特

| 工作流 | 9月 | 10月 | 11月 | 12月 |
|---|---|---|---|---|
| HIL 硬件到货 + 搭建（D4.1）| ▓▓▓▓▓ | ▓▓▓ | | |
| HIL 集成测试（D4.2）| | ▓▓▓▓▓ | ▓▓▓▓▓ | |
| SIL 2 第三方评估（D4.3）| 接洽 | ▓▓▓ | ▓▓▓▓▓ | 意见回复 |
| CCS AIP 提交（D4.4）| ▓▓▓▓▓ | | ▓▓▓▓▓ | ▓▓▓▓▓ |
| **D4.5 FCB 非认证级技术验证** | | | | ▓▓▓▓▓ |
| **D4.5' 船长/ROC 模拟器认证** | | ▓▓▓▓▓ | ▓▓▓ | |
| **D4.6 RL 对抗 v1.0**（B2 后移）| | ▓▓▓ | ▓▓▓▓▓ | ▓▓▓ |
| **D4.7 4 缺失模块完整**（B4 触发则 9 月启）| ▓▓▓ | ▓▓▓▓▓ | | |
| ⟋ HAZID RUN-002 拖船（≥ 6 周）| | ▓▓▓ | ▓▓▓▓▓ | ▓▓ |
| ⟋ HAZID RUN-003 渡船（≥ 6 周）| | | | ▓▓▓ |

### Phase 4 关键交付物

#### D4.1 HIL 测试台搭建验收报告
- 目标日期：2026-09-30（展望）
- Scope：HIL 硬件安装验收 + TDL ↔ HIL 双向接口 + 控制回路端到端 < 200ms

#### D4.2 HIL 综合测试报告
- 目标日期：2026-11-30（展望）
- Scope：8 模块 HIL 联调 ≥ 800h 无致命故障（v3.0 修订：v2.0 200h → 架构 §14.4 800h，E P1-E1 整改）+ COLREGs 场景 HIL 实测 + MRC 端到端

#### D4.3 SIL 2 第三方初步评估报告
- 目标日期：2026-11-30（展望）
- Scope：基于 D3.7 + DEMO-3 证据包提交 TÜV/DNV/BV；MRC 触发链路 + COLREGs SIL 2 初步意见

#### D4.4 CCS i-Ship AIP 申请提交（受理回执）
- 目标日期：2026-11-30（展望）
- Scope：完整认证材料包（ConOps + ODD-Spec v1.1.3 + 系统架构 + HARA + FMEA + SIL/HIL 报告 + COLREGs 覆盖率 + 网络安全 + ASDR + 培训矩阵）

#### D4.5 FCB **非认证级**技术验证试航（v3.0 修订）
- 目标日期：2026-12-31（预期）
- Scope：≥ 50 nm 自主航行 + ≥ 10 次 ROC 接管演示 + 实测 AIS 数据回收（作为 SIL 场景库真实数据补充）+ ASDR 日志完整。**v3.0 关键修订**：CCS 验船师**不到场**；不作为 i-Ship 证据；2027 Q1/Q2 AIP 受理后**启动认证级实船试航**（独立 D5.x 计划，首次认证级试航）。

**D4.5 实船准入门槛（v3.0 修订后，CCS-hat 强制补充）**

原拟 7 项准入门槛，经用户决策 §13.3（2026-05-07）降级为非认证级试航后调整为 4 项保留。

| # | 门槛内容 | 状态 | CCS 刚性程度 |
|---|---|---|---|
| 1 | D3.7 8h 无崩溃 + 0 P0 bug（系统可靠性基线）| ✅ **保留** | 🟢 非认证级最低可接受性基线 |
| 2 | D4.2 HIL ≥ 50h 无致命故障（Hardware-in-Loop 验证）| ✅ **保留** | 🟢 SIL 2 路径必要依据 |
| 3 | Hs ≤ ODD-A 边界（海况 ODD 约束，Hs < 2.5m）| ✅ **保留** | 🟢 IMO MASS Code ODD 要求 |
| 4 | ROC 接管链路独立验证 ≥ 60s TMR（接管时窗验证）| ✅ **保留** | 🟢 Veitch 2024 TMR 基线，CCS 需 |
| 5 | CCS AIP（原则性批准，i-Ship 前置）| ❌ **删除** | 🔴 刚性（认证级试航必须）；非认证级可删 |
| 6 | SIL 2 第三方评估意见（TÜV/DNV/BV）| ❌ **删除** | 🔴 刚性（认证级试航必须）；D4.3（11月前）完成后补入 |
| 7 | CCS 验船师至少出具"中期意见"或派员见证 | ❌ **删除** | 🟡 中等刚性；非认证级内部试验不强制，但数据升级须 ≥6 周前通报 |

**说明**：
- 门槛 5/6 仅对认证级试航（D5.x，2027 Q1/Q2）恢复为强制。
- 门槛 7 通过 RUN-001-fcb-data-substitute-memo.md §4 第 5 点（CCS 协商安排条款）完成风险隔离。
- D4.5' 船长/ROC 模拟器认证（≥2 名船长 + 2 名 ROC）作为第 8 个门槛独立列出（见下节），CCS 刚性程度 🟡 中等（要求认证机构须经 CCS 认可或等效国际机构）。

**CCS-hat Sign-off — D4.5 修订声明**（2026-05-08）

**总体判断**：有条件接受（存在一处程序逻辑需要正式澄清）

**问题4 — HIL+SIL 2 能否支撑 AIP**：🟡 可支撑，但有前提条件。CCS i-Ship AIP 是设计概念批准，不要求所有试验数据已到位，关注的是系统架构是否具备可认证性、V&V 路径是否合理规划。HIL (D4.1/D4.2) + SIL 2 第三方评估 (D4.3) + V&V Plan (D1.5) + HARA/FMEDA (D2.7) 组合原则上足以支撑 AIP 申请。**重要前提**：D4.3 SIL 2 评估报告必须由 CCS 认可机构（TÜV/DNV/BV）出具。当前"9 月接洽"过晚，建议提前到 6 月确认机构资质，避免 11 月 AIP 提交时证据不被认可。

**问题5 — AIP 前实船试航的程序逻辑**：🟢 程序逻辑合理，顺序符合 CCS 实践。CCS i-Ship AIP 是设计阶段批准（建造前或首次系统集成前），实船数据是 AIP 受理后入级证书颁发的输入，而非 AIP 的输入。本项目路径（AIP 11月 → 认证级实船 2027 Q1/Q2）在程序上正确。**表述修订**："重启认证级试航"改为"启动认证级实船试航（独立 D5.x 阶段，首次认证级试航）"，消除"重启"暗示曾有认证级试航的歧义。

**问题6 — 被删除准入门槛的刚性程度**：✅ 已关闭 — 7 项门槛完整清单已在本节上方"D4.5 实船准入门槛"表中列出。被删除的 3 项（AIP / SIL 2 第三方意见 / CCS 中期意见）均属认证级试航强制要求，非认证级内部试验可豁免；豁免逻辑在 RUN-001-fcb-data-substitute-memo.md §4 第 5 点（CCS 协商安排条款）完成风险隔离。

**风险提示**（若 CCS 不接受此修订声明）：(1) AIP 提交被退回，最坏情形延期 1–2 个季度；(2) December 数据全部失效，连带 D1.3a 场景库质量下降；(3) 船长/ROC 认证不被认可（若 D4.5' 课程未经 CCS 认可机构鉴定）。最保守建议：AIP 提交前（2026-10 月前），安排一次与 CCS 的非正式技术会谈（pre-submission meeting），就 D4.5 删减门槛和 December 数据定位取得书面认可意见。

签字：CCS-hat ✅ 2026-05-08（条件已关闭）

条件关闭记录：
- ✅ 问题6 条件：7 项门槛清单已补充（含 4 保留 + 3 删除 + 刚性程度评级）
- ✅ AIP pre-submission meeting 建议已在风险提示中明文记录

#### D4.5' 船长/ROC 模拟器认证（NEW v3.0）
- 目标日期：2026-11-30
- Scope：≥ 2 名船长 + 2 名 ROC 操作员通过 D3.5' 课程认证；作为 D4.5 试航准入门槛

#### D4.6 RL 对抗场景生成 v1.0（NEW v3.0，B2 后移）
- 目标日期：2026-11-30
- Scope：从 stable-baselines3 (PPO) 起手；≥ 5 个对真实 M5+M6 出错的边界场景；新增场景入 D1.6 schema 库

#### D4.7 4 缺失模块完整实现（NEW v3.0，B4 触发条件）
- 触发条件：D2.8/D3.8 stub 不被 CCS 接受，或 Phase 1-3 出现 ≥ 3 工作日滑期
- Scope：ENC Manager + Parameter Store + Environment Cross-Source Validator + BNWAS-equivalent 完整实现

---

## §10 Demo Milestones（v3.0 NEW）

### 10.1 三档 Demo 不可取消

DEMO-1 / 2 / 3 是 Phase 1/2/3 的**强约束 milestone**，不是可选的 nice-to-have。任一 DEMO 不达标 = 项目 P0 escalation 信号，PM 立即触发 contingency。

### 10.2 Demo 角色

每个 DEMO 强制邀请下列角色（如缺席需 PM 书面备案）：
- **业主**：项目可视化 + 资源决策
- **PM**：进度可视化
- **CCS 联络人**：合规证据可接受性
- **资深船长（≥ 1）**：心智模型 + 直觉性反馈
- **ROC 操作员代表**：操作流程可读性
- **架构师 + V&V 工程师 + 安全工程师 + HF 咨询**：技术问答

### 10.3 Demo 不允许的 anti-pattern

- ❌ 静态 PPT 汇报（必须 live 系统演示）
- ❌ 仅展示代码量 / 测试覆盖率 / 单元测试通过率（无业务场景）
- ❌ 只展示 happy path（必须含 ≥ 1 个 failure 场景）
- ❌ "下次再演示"（DEMO 不接受 reschedule，最多顺延 1 周触发 contingency 评估）
- ❌ DEMO 期间 mock 数据假装真实（v3.0 允许 mock 但必须明示）

### 10.4 Demo 准备节奏

每次 DEMO 前 7 天：
- D-7：技术负责人 + V&V 工程师 dry run #1（内部）
- D-3：dry run #2（PM + 架构师 + 各 owner 旁观）
- D-1：业主彩排 #3（业主先看 ≤ 30 min 简版）
- D-day：正式 DEMO

---

## §11 D-task Demo Charter Template（v3.0 NEW）

每个 D 任务的详细设计 / 实现 PR review 必须含以下 4 段：

```markdown
## §13 Demo Charter

### Scenario
<一句话；引用 scenario.yaml ID 或 demo 演练脚本 ID>

### Audience × View
<角色 1> × <看什么>
<角色 2> × <看什么>
...

### Visible Success
- <2-3 个具体可见结果>
- <可由非工程师识别的成功标志>

### Showcase Bundle
- [ ] Demo video (≤3 min) — 路径：<git path>
- [ ] scenario.yaml — 路径：<git path>
- [ ] ASDR / JSON 日志 — 路径：<git path>
- [ ] Before/after diff — 架构 / 性能 / 合规证据
- [ ] "What this means for TDL" 200 字 — 路径：<git path>

### Contribution Map
- 关闭 finding：<P0/P1 ID 列表>
- 推进 CCS 子功能：<DMV-CG-0264 §x.y>
- 解锁下游 D：<D 编号列表>
- 推进 capability matrix：<行/列>
```

### 11.1 Charter 不允许的 anti-pattern
- ❌ Visible Success 用 "覆盖率 90%" / "通过率 95%" 等纯数字（必须有业务可读的副本）
- ❌ Audience × View 全是工程师角色（必须含至少 1 个非工程师）
- ❌ Scenario 写"暂无场景"或"待定"
- ❌ Showcase Bundle 5 项中缺 ≥ 2 项

### 11.2 Charter review 节奏

每个 D 任务的 PR：
- merge 前必须包含完整 Charter
- review 不通过 = PR 不能 merge
- 由 PM 或 V&V 工程师 任 1 人复核

---

## §12 Demo-Driven Weekly Sync（v3.0 NEW）

每周五 16:00–16:30（30 min，PM 主持，全 owner 出席）：

```
00:00–10:00  本周完成 D-task 各自 2 min mini-demo
             — 没 demo 不能说"本周完成"
             — owner 现场分享屏幕 ≤ 60s 关键效果
05:00 标记   下周 D-task 的 Demo Charter 预演（各 owner 2 min）
             — 答 4 段（Scenario / Audience / Visible Success / Bundle）
20:00–30:00  跨 D 接口 + risk burndown + 下周 escalation
             — 任一 owner 标 🔴 即触发 contingency 评估
```

**强制规则**：
- sync 不在 30 min 内结束的，PM 当场停会
- 不允许"本周做了 X 但还没 demo"——这等于"本周没完成"
- mini-demo 失败现场，不补救（不浪费同步时间），由 owner 24h 内单独发短视频补

---

## 附录 A · 工作量汇总表（v3.0）

| D 编号 | 阶段 | 目标日期 | 预计人周 | Owner | 关闭 finding 数 |
|---|---|---|---|---|---|
| D0.1 surgical fix | Pre | 2026-05-12 | 1.0 | 架构师 + M2/M5/M8 | 11 |
| D0.2 RFC closure | Pre | 2026-05-12 | 0.5 | 架构师 + M4 + 法务 | 3 |
| D0.3 工时 + HTML 同步 | Pre | 2026-05-12 | 0.5 | PM + 架构师 | 4 |
| **D0 小计** | | | **2.0** | | 18 |
| D1.1 ROS2 工作区 | Phase 1 | 2026-05-24 | 1.5 | 架构师 | 2 |
| D1.2 CI/CD 流水线 | Phase 1 | 2026-05-31 | 1.5 | 基础设施 | 1 |
| D1.3a MMG + AIS | Phase 1 | 2026-05-31 | 4.0 | 技术负责人 | 2 |
| D1.3b 场景 + HMI | Phase 1 | 2026-06-15 | 3.0 | 技术负责人 | 4 |
| D1.3.1 Sim Qualification | Phase 1 | 2026-06-15 | 1.0 | 技术负责人 + M5 | 2 |
| D1.4 编码规范 | Phase 1 | 2026-06-15 | 0.5 | 架构师 | 0 |
| D1.5 V&V Plan | Phase 1 | 2026-05-31 | 2.0 | V&V 工程师 + 架构师 | 5 |
| D1.6 场景 schema | Phase 1 | 2026-06-15 | 2.0 | V&V 工程师 + 技术负责人 | 4 |
| D1.7 覆盖率方法论 | Phase 1 | 2026-06-15 | 1.5 | V&V 工程师 + M6 + 安全 | 2 |
| D1.8 cert tracking + ConOps | Phase 1 | 2026-06-15 | 1.0 | 架构师 + PM + CCS | 2 |
| **Phase 1 小计** | | | **18.0** | | 24 |
| D2.1 M1 | Phase 2 | 2026-07-06 | 5.0 | M1 负责人 | 4 |
| D2.2 M2 | Phase 2 | 2026-07-06 | 5.5 | M2 负责人 | 4 |
| D2.3 M3 | Phase 2 | 2026-07-13 | 3.0 | M3 负责人 | 2 |
| D2.4 M6 | Phase 2 | 2026-07-31 | 5.0 | M6 负责人 | 1 |
| D2.5 SIL 集成 | Phase 2 | 2026-07-31 | 2.5 | V&V 工程师 | 3 |
| D2.6 船长 ground truth | Phase 2 | 2026-07-13 | 3.0 | HF + M8 + PM | 7 |
| D2.7 HARA + FMEDA M1 | Phase 2 | 2026-07-13 | 2.5 | 安全工程师 + 架构师 | 3 |
| D2.8 v1.1.3 stub | Phase 2 | 2026-07-31 | 3.0 | 架构师 + 各模块 | 11 |
| **Phase 2 小计** | | | **29.5** | | 35 |
| D3.1 M4 | Phase 3 | 2026-07-26 | 4.0 | M4 负责人 | 2 |
| D3.2 M5 | Phase 3 | 2026-08-10 | 6.5 | M5 负责人 | 7 |
| D3.3a M7-core | Phase 3 | 2026-08-10 | 6.0 | M7 + 安全 | 2 |
| D3.3b M7-sotif | Phase 3 | 2026-08-16 | 3.0 | M7 + 安全 | 3 |
| D3.4 M8 完整 | Phase 3 | 2026-08-24 | 5.5 | M8 负责人 | 9 |
| D3.5 v1.1.3 回填 | Phase 3 | 2026-08-31 | 2.0 | 架构师 | 1 |
| D3.5' 培训课程 | Phase 3 | 2026-08-23 | 1.5 | PM + HF | 1 |
| D3.6 SIL 1000+ | Phase 3 | 2026-08-31 | 2.5 | V&V 工程师 | 4 |
| D3.7 8 模块全集 | Phase 3 | 2026-08-31 | 3.0 | 技术负责人 | 3 |
| D3.8 v1.1.3 完整 | Phase 3 | 2026-08-31 | 2.5 | 架构师 + 各模块 | 13 |
| D3.9 RFC-007 cyber | Phase 3 | 2026-08-16 | 1.0 | 架构师 + CCS + 安全 | 2 |
| **Phase 3 小计** | | | **37.5** | | 47 |
| **前 4 月 D0+1+2+3 合计** | | | **87.0** | | **124** |

> 注：与 §0.1 / §0.2 一致 —— 工作 87.0 vs 产能 84.0 = -3.0 人周缺口，按 §0.3 默认路径闭环（V&V 工程师延 2 周 + 架构师/技术负责人受限加班 1 周）。任一 owner 申请触发 contingency 必须周五 sync 公开评估。

---

## 附录 B · 关键里程碑（v3.0）

| 日期 | 里程碑 | 意义 |
|---|---|---|
| 2026-05-08 | D0 sprint 启动 + V&V 工程师 onboard | 评审整改正式开工 |
| 2026-05-12 | **D0 全部关闭** | 11 must-fix 全闭 + RFC-007/009 + 工时表 v2.1 |
| 2026-05-13 | HAZID RUN-001 第①次会议 + Phase 1 开工 | — |
| 2026-05-15 | 安全工程师外包合同生效 | A1 闭环完整 |
| 2026-05-31 | D1.5 V&V Plan v0.1 commit + CCS 邮件回执 | V&V 范式确立 |
| 2026-06-15 | **🎬 DEMO-1 Skeleton Live** | Phase 1 全部完成 |
| 2026-06-16 | HF 咨询外包合同生效 | A1 闭环完整 |
| 2026-07-13 | Phase 3 并行启动（M4/M5）| — |
| 2026-07-13 | HIL 硬件采购需求单提交 | — |
| 2026-07-31 | **🎬 DEMO-2 Decision-Capable** | Phase 2 全部完成 + 50 场景 ≥ 95% |
| 2026-08-10 | M5 + M7-core 完成 | 核心安全链路就绪 |
| **2026-08-19** | **HAZID RUN-001 完成** | 132 [TBD-HAZID] 校准 |
| **2026-08-31** | **🎬 DEMO-3 Full-Stack** | Phase 3 全部完成 + 1000 场景 ≥ 98% |
| 2026-10（预期）| HIL 硬件到货 | 展望 |
| 2026-11（预期）| CCS AIP 提交 + DNV/TÜV/BV SIL 2 评估意见 | 展望 |
| **2026-12（预期）** | **D4.5 FCB 非认证级技术验证试航** | 展望 |
| 2027 Q1/Q2 | AIP 受理 + 认证级试航 D5.x 启动 | 不在本计划 |

---

## 附录 C · HTML / Markdown 同步对应（v3.0 修订）

本 Markdown 与 `MASS_ADAS_L3_8个月完整开发计划.html` 内容须严格对应。**v3.0 → v2.0 HTML 同步是 D0.3 子项**，于 2026-05-12 完成，含：

- 头表 v2.0 → v3.0 全量替换
- Phase 卡片增加 D0 + D1.5/D1.6/D1.7/D1.8 + D2.6/D2.7/D2.8 + D3.3a/D3.3b/D3.5'/D3.8/D3.9
- 三档 DEMO milestone 在甘特图突出显示（橙色 ★）
- 附录 A/B 工作量表 + 里程碑表更新
- 新增 §10/§11/§12 三章 Demo 机制可折叠模块

> **修改建议**：对应关系详见 v2.0 附录 C；v3.0 元素映射：`.dc-demo-charter`（NEW），`.demo-milestone`（NEW，三档橙色横幅），`.contingency-banner`（NEW，B4/3.5 人周 reserve 提示）。

---

## 附录 D · Findings Closure Map（v3.0 NEW）

| Finding ID | 来源 angle | 关闭于 D | Phase |
|---|---|---|---|
| MUST-1 (M2 OVERTAKING) | B P0-B-03 | D0.1 + D2.2 验证 | D0 |
| MUST-2 (Mid-MPC N) | B P0-B-02 | D0.1 + D3.2 验证 | D0 |
| MUST-3 (IvP licensing) | B P0-B-01 | D0.2 RFC-009 + D3.1 实现 | D0 |
| MUST-4 (HAZID-FCB 船期) | A P0-A2 | D0.2 备忘录 | D0 |
| MUST-5 (M5 ROT_max 硬编) | B FCB-01 + F P0-F-03 | D0.1 + D3.2 验证 | D0 |
| MUST-6 (M2 sog 硬编) | F P0-F-03 + MV-2 | D0.1 + D2.2 验证 | D0 |
| MUST-7 (M8 active_role) | D P0-D-04 + MV-3 | D0.1 stub + D3.4 完整 | D0 |
| MUST-8 (PATH-S CI 时点) | A P1-A8 | D0.3 dry-run + D1.2 强制 | D0 |
| MUST-9 (M5 自触 MRM) | B P1-B-08 | D0.1 + D3.2 验证 | D0 |
| MUST-10 (NLM 扩充) | A/B/C/D/F NLM 缺口 | D0.3 deep research | D0 |
| MUST-11 (M7 6→9w) | A P0-A3 + 用户 §13.2 | D0.3 工时表 + D3.3a/b 拆分 | D0 |
| A P0-A1 (8/31 单点压力) | A | 用户 §13.2 决策 + 3 新角色 owner 分离 | D0–D3 全程 |
| A P0-A3 (M7 effort) | A | MUST-11 关闭 | D0 |
| A P1-A1 (HAZID 工时) | A + C P0-C-2 | D0.2 + HAZID 议程重排 ≥ 2 full-day | 5/13 起 |
| A P1-A4 (实船准入) | A | 用户 §13.3 D4.5 降级 | D0.3 |
| B P0-B-01..05 | B | D0.1/0.2 + D3.1/3.2 实现 | D0 + D3 |
| B P1-B-01..08 + P2 | B | D2.x + D3.x 各 D 关闭 | D2-D3 |
| C P0-C-1 (4 件 cert evidence) | C | D1.8 (ConOps stub) + D2.7 (HARA + FMEDA M1) + D3.3a (Doer-Checker) + Phase 4/v1.1.4 (ALARP+SDLC 完整) | D1-D3 + 跨期 |
| C P0-C-2 (HAZID 工时) | C | D0.2 + HAZID 重排 | 5/13 起 |
| C P0-C-3 (Doer-Checker 100×) | C | D3.3a Doer-Checker 三量化矩阵 | D3.3a |
| C P1-C-1..8 | C | D2.7 + D3.3b + D3.4 + D3.9 | D2-D3 |
| D P0-D-01..05 + P1-D-01..08 | D | D2.6 + D3.4 + D3.5' | D2-D3 |
| E P0-E1..5 + P1-E1..8 | E | D1.5 + D1.6 + D1.7 + D2.5 + D3.6 + D3.7 | Phase 1-3 |
| F P0-F-01..04 + P1-F-01..06 | F | D2.8 stub + D3.8 完整 + D3.9 cyber | Phase 2-3 |
| G P0-G-1..5 + P1-G-1..8 + P2 | G | D1.3a/b + D1.3.1 + D1.6 + D3.7 + D4.6 (B2 后移) | Phase 1-4 |
| MV-1..14 (多船型) | 跨 angle | D0.1 + D2.x + D3.x + D4.7 (B4 触发) | 全程 |

完整 finding ↔ D 矩阵共 124 项闭环，详见 [`docs/Design/Review/2026-05-07/00-consolidated-findings.md`](../../Review/2026-05-07/00-consolidated-findings.md) §10 owner 表。

---

## 附录 E · 角色 + 资源日历（v3.0 NEW）

### E.1 角色清单

| 角色 | 在岗期 | 工时投入 | 主要 D 承担 |
|---|---|---|---|
| 架构师（原 1 人）| 5/8–8/31 | 35h/周 × 17 周 | D0.1/0.2/0.3, D1.1/1.4/1.8, D2.7/2.8, D3.5/3.8/3.9 |
| 技术负责人（原 1 人）| 5/8–8/31 | 35h/周 × 17 周 | D1.3a/3b/3.1, D3.7（核心模块也参与）|
| 模块负责人 M1–M8 | 5/13–8/31 | 兼任，不单算 | D2.x + D3.x（各模块）|
| **V&V 工程师 FTE**（新 5/8 起）| 5/8–8/31 | 35h/周 × 17 周 | D1.5/1.6/1.7, D1.3.1, D2.5, D3.6 |
| **安全工程师外包**（新 5/15–7/10）| 5/15–7/10 | ~16h/周 × 8 周 | D2.7, D3.3a/b, D3.9 |
| **HF 咨询外包**（新 6/16–7/27）| 6/16–7/27 | ~10h/周 × 6 周 | D2.6, D3.5' |
| PM | 全程 | 兼任 + Demo sync | D0.3, D1.8, D2.6, D3.5', 全程 sync |
| 法务 | 5/8–5/12 | 5h（单次）| D0.2 IvP LICENSE 复审 |
| CCS 联络人 | 全程 | 兼任 | D0.2, D1.8, D2.7, D3.5/3.8/3.9, AIP 11 月 |
| 资深船长（≥ 1）| 6/16–8/31 | 1d/月 × 3 + DEMO-1/2/3 | D2.6 访谈 + 3 DEMO 反馈 |

### E.2 资源日历关键节点

```
5/8 (Fri) ─────── V&V 工程师 onboard ★
5/12 (Tue) ────── D0 全闭
5/13 (Wed) ────── HAZID kickoff + Phase 1 开工
5/15 (Fri) ────── 安全工程师外包合同生效 ★
6/15 (Mon) ────── DEMO-1 ★
6/16 (Tue) ────── HF 咨询外包合同生效 ★
7/13 (Mon) ────── Phase 3 并行 + HIL 采购单
7/27 (Mon) ────── HF 咨询合同到期 → 续签或不续
7/31 (Fri) ────── DEMO-2 ★
8/19 (Wed) ────── HAZID RUN-001 完成 ★
8/31 (Mon) ────── DEMO-3 ★
```

### E.3 假期 / 缓冲

- 5/1–5/5：劳动节假期（D0 排在 5/8 起）
- 6/22：端午节（D2.x W6 单日，已计入）
- 9/1 起：Phase 4 展望，无硬假期假设
- 3.5 人周 contingency reserve 由 PM 集中管理；任一 owner 申请触发必须周五 sync 公开评估

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-04-20 | 初版（4 阶段开发计划）|
| v2.0 | 2026-05-07 | 8 个月完整开发计划，含 v1.1.2 接口锁定 |
| **v3.0** | **2026-05-08** | **整合 7 角度评审 124 项 finding；新增 D0/D1.5/D1.6/D1.7/D1.8/D2.6/D2.7/D2.8/D3.3a/D3.3b/D3.5'/D3.8/D3.9/D4.5'/D4.6/D4.7；3 新角色（V&V 工程师 FTE + 安全/HF 外包）；三档 DEMO milestone（不可取消）；D-task Demo Charter 模板强制；Demo-Driven Sync；D4.5 降级非认证级试航；ALARP+SDLC 完整版推 v1.1.4（B3）；RL 后移 Phase 4（B2）；4 缺失模块 stub-only + B4 standby contingency；工作量 84.0 人周 vs 产能 84.0，3.5 人周 reserve（PM 集中管理）；HTML 副本 D0.3 同步**|

---

## 附录 F · 后续动作（5/8 next steps）

1. **本日**（5/8 Fri）：用户 OK 本 v3.0 → architect/PM 触发 D0.1/0.2/0.3 启动；V&V 工程师 onboard formality 完成
2. **5/8–5/12 D0 sprint**：3 工作日内完成 11 must-fix + 3 子 D 全部 commit
3. **5/13 第①次 HAZID 会议**：D0.2 备忘录现场宣读 + 议程重排
4. **5/13 起**：每个 D 任务的详细执行各自走 `/brainstorming → /writing-plans → /executing-plans` 三段闭环；模板见 §11；D-Charter 必填
5. **每周五 16:00–16:30**：Demo-Driven Sync（§12）

---

*文档版本 v3.0 · 2026-05-08 · 变更须经技术负责人 + PM 双签 + V&V 工程师工时影响评估*
