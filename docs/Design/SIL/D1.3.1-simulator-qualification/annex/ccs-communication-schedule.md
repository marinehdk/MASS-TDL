# Annex — CCS Surveyor 沟通时间表

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-D1.3.1-ANNEX-CCS |
| 版本 | v0.1-draft |
| 日期 | 2026-05-15 |
| 状态 | 在制 — 🟢 沟通策略与方法论完成 | ⚫ 联系人信息待 CCS surveyor 指派后填充（见 §5）|
| 上游 | D1.3.1 07-ccs-mapping.md；V&V Plan v0.1 §10 Demo Milestones |
| 下游 | D1.8 ConOps + Early Letter；D4.4 CCS AIP Formal Submission（11/1）|

---

## §1 沟通策略

### 1.1 三阶段递进

CCS surveyor 与项目的互动采用**三阶段递进**策略，确保 surveyor 在 AIP 正式提交前有充分审查和反馈机会：

| 阶段 | 时间窗 | 文档版本 | 沟通目标 |
|---|---|---|---|
| **Early Preview** | 2026-06-01 | v0.1 — D1.8 Early Letter | 让 CCS surveyor 提前了解仿真器鉴定计划全貌；获得初步反馈意向和关注点 |
| **Mid Calibration** | 2026-07-01 | v0.2 — HAZID 中期 | 交付 DNV-RP-0513 条款补齐 + MMG 实测数据；基于surveyor 反馈迭代 |
| **Formal Submission** | 2026-11-01 | v1.0 — 完整证据包 | D4.4 CCS AIP 正式提交，含全部 7 份交付物 + 原始数据 + audit trail |

### 1.2 沟通原则

- **早介入**（Early Engagement）：不在 D4.4 时才第一次联系 CCS，而是在 D1.8（ConOps v0.1）阶段即发送 Early Letter
- **逐步递进**（Incremental Delivery）：每个 DEMO milestone 产出可审查的文档版本，不给 surveyor 造成一次性审阅压力
- **有问必答**（Responsive Feedback）：每次沟通后 5 个工作日内回复 surveyor 提出的问题
- **追溯完整性**（Audit Trail）：所有与 CCS 的邮件/会议纪要/正式意见均归档至 `docs/Design/Cert/ccs-correspondence/`

---

## §2 沟通日历

| # | 日期 | Milestone | 沟通行动 | 交付物 | 预期 CCS 反馈 |
|---|---|---|---|---|---|
| 1 | **2026-06-01** | D1.8 Early Letter | 通过正式函件向 CCS 上海审图中心发送 D1.3.1 v0.1 预审包（含 7 份交付物目录 + 方法论摘要），请求 surveyor 指派并安排 DEMO-1 观摩 | `07-ccs-mapping.md` v0.1；`06-evidence-matrix.md` v0.1；`01-overview.md` v0.1；其余 `02`–`05` 标注 🟡 Projected 状态 | ① surveyor 是否接受 D1.3.1 交付物结构 ② 是否有补充条款要求 ③ 是否确认参加 DEMO-1（6/15）|
| 2 | **2026-06-15** | DEMO-1 Skeleton Live | 现场演示：turning reference comparison（02 证据）+ tool confidence report（06 证据）+ 场景清单（01 证据）；surveyor 可实时提问 | `02-model-fidelity-report.md`（turning circle 子集 🟢 Live）；`06-evidence-matrix.md`（PM-1/PM-2 子集 🟢 Live）；DEMO-1 演示幻灯片 | ① surveyor 对模型保真度方法的初步认可度 ② 是否需要补充特定工况的 CFD 对比 ③ 场景清单是否覆盖 CCS 关注的核心工况 |
| 3 | **2026-07-01** | HAZID 中期 + v0.2 发布 | 发送 v0.2 更新包：DNV-RP-0513 完整条款补齐 + MMG 参数标定实测数据（FCB 实船数据替代品进入）；同步告知 HAZID RUN-001 进展 | `02`–`06` 全部更新至 v0.2（🟡→🟢 转化率 ≥ 60%）；HAZID RUN-001 中期报告摘要 | ① surveyor 是否认可 DNV-RP-0513 等价映射 ② MMG 标定方法是否满足 CCS 要求 ③ HAZID 安全参数是否影响仿真器鉴定 scope |
| 4 | **2026-07-31** | DEMO-2 Decision-Capable | 现场演示：1000-run replay（03 证据）+ 完整传感器退化矩阵（04 证据）+ 编排追溯（05 证据）+ SOTIF 假设监控初版 | `01`–`07` 全部 v0.3（🟢 覆盖率 ≥ 85%）；DEMO-2 演示幻灯片；CCS audit checklist（07-ccs-mapping §4）预填 | ① surveyor 逐条核查 audit checklist ② 是否有条款仍标记 N/A 需补充 ③ v1.0 提交前还需补充的证据项 |
| 5 | **2026-08-31** | DEMO-3 Full-Stack with Safety + ToR | 发送 v1.0 完整证据包 + HAZID RUN-001 完整报告；所有 07 交付物 🟢 全覆盖；CCS surveyor 可在此 milestone 开始正式预审 | `01`–`07` 全部 v1.0（🟢 覆盖率 100%）；HAZID RUN-001 完整报告；CCS audit checklist（07-ccs-mapping §4）全部打钩 | ① surveyor 正式预审意见 ② 是否进入 AIP 正式提交流程 ③ 是否有需要 revision 的条款 |
| 6 | **2026-09~10** | CCS AIP 准备期 | 根据 surveyor 预审意见进行 revision cycles；安排 CCS 上海现场 pre-review meeting；完成所有 revision 后锁定 v1.1 | v1.1 修订版（含 surveyor 反馈闭合记录）；pre-review meeting 演示材料 | ① revision 是否通过 ② 是否可以正式提交 AIP |
| 7 | **2026-11-01** | **D4.4 CCS AIP 正式提交** | 向 CCS 提交完整 AIP 申请包（含 D1.3.1 全部交付物 v1.1 final）；附所有 surveyor 沟通记录 + 反馈闭合矩阵 | D1.3.1 全部 7 份交付物 final；反馈闭合矩阵；audit trail 完整归档 | CCS 受理函 → 进入正式审图流程 |

---

## §3 正式函件模板（中文）

### 3.1 D1.8 Early Letter（2026-06-01 发送）

```
致：中国船级社（CCS）上海审图中心

事由：MASS ADAS L3 战术决策层仿真器鉴定（D1.3.1）预审材料提交

尊敬的 CCS surveyor：

我司三港集团（SANGO Group）正在进行 MASS ADAS L3 战术决策层（Tactical Decision
Layer）系统开发，目标申请 CCS 智能船舶附加标志 i-Ship (N1, N2, Ri, Ai)，计划于
2026 年 11 月正式提交图纸审批（AIP）申请。

作为认证证据体系的重要组成部分，我们正在执行仿真器鉴定项目（项目编号：
SANGO-ADAS-L3-SIL-D1.3.1），旨在证明所用 SIL 仿真器满足 CCS《智能船舶规范 2024》
§9.1 性能验证的要求。现随函附上 D1.3.1 预审材料 v0.1，供贵方提前审阅。

本次提交包含以下 7 份交付物（附件）：

  1. 01-overview.md — 范围、验证策略与数据可用性分级（🟡 Projected）
  2. 02-model-fidelity-report.md — MMG 4-DOF 保真度报告（🟡 Projected）
  3. 03-determinism-replay.md — 确定性重放与统计显著性（🟡 Projected）
  4. 04-sensor-confidence.md — 传感器模型退化 vs DNV-CG-0264（🟡 Projected）
  5. 05-orchestration-trace.md — 仿真编排追溯审计链（🟡 Projected）
  6. 06-evidence-matrix.md — 4 项证明 → DNV-RP-0513 MAL-2 映射（🟡 Projected）
  7. 07-ccs-mapping.md — CCS §9.1 条款逐条映射 + surveyor 审核清单（🟢 Methodology）

注：当前 v0.1 版本中，第 1–6 份交付物标注为 "Projected" 状态，即方法论文档已
完成但实测数据待上游交付后填充。第 7 份（CCS 条款映射）已完成方法论部分，待 CCS
§9.1 完整 PDF 送达后补充条款原文引用。

恳请贵方：

  (a) 指派 1–2 名 surveyor 对接本项目的 AIP 审核；
  (b) 确认 2026-06-15 是否可以参加 DEMO-1 现场演示（线上/线下均可）；
  (c) 就 D1.3.1 交付物结构提出初步意见（是否需要补充其他条款覆盖）。

如蒙回复，不胜感激。

此致
敬礼

三港集团 MASS ADAS 项目组
技术负责人：[TBD]
V&V 工程师：[TBD]
日期：2026 年 6 月 1 日
联系方式：[TBD]
```

### 3.2 中期校准函（2026-07-01 发送）

```
致：中国船级社（CCS）上海审图中心
收件人：CCS surveyor [TBD]

事由：MASS ADAS L3 仿真器鉴定（D1.3.1）v0.2 更新 + HAZID 中期进展通报

尊敬的 [TBD] surveyor：

感谢贵方对 D1.3.1 v0.1 预审材料的初步审阅及宝贵反馈。

现呈上 v0.2 更新包，主要变更如下：

  1. DNV-RP-0513 完整条款补齐（06-evidence-matrix.md）：MAL-2 全部 4 项证明的
     条款映射已完成，附逐条合规性检查表。
  2. MMG 4-DOF 参数标定实测数据（02-model-fidelity-report.md）：已完成 FCB
     实船数据替代品 [W3] 建模，turning circle + zigzag 工况初步 RMS 结果见附件。
  3. 传感器退化矩阵 v0.2（04-sensor-confidence.md）：GNSS/Radar/AIS 退化模型
     已按 CG-0264 §6 Table 6-1 逐条对齐。
  4. HAZID RUN-001 中期进展：第一次 full-day workshop 已完成，识别 ≥ 30 项
     危险源；安全参数 [TBD-HAZID-xxx] 将在 8/19 完整报告发布后回填至 D1.3.1。

本次 v0.2 更新后，所有交付物的 🟡→🟢 转化率已超过 60%。下一里程碑为 2026-07-31
DEMO-2 Decision-Capable，届时将提供 1000-run 确定性重放数据、完整传感器退化
矩阵与编排追溯审计链。

恳请贵方：

  (a) 就 v0.2 更新内容提出意见（尤其是 DNV-RP-0513 等价映射路径是否满足 CCS
      工具鉴定要求）；
  (b) 确认是否参加 2026-07-31 DEMO-2 现场演示；
  (c) 如 CCS §9.1 完整 PDF 已可提供，请告知获取方式，以便我们补齐条款原文引用。

附件：
  - D1.3.1 v0.2 全部 7 份交付物（PDF）
  - HAZID RUN-001 中期报告摘要（1 页）

此致
敬礼

三港集团 MASS ADAS 项目组
技术负责人：[TBD]
V&V 工程师：[TBD]
日期：2026 年 7 月 1 日
```

---

## §4 会议计划

| # | 会议 | 日期 | 形式 | 参与方 | 议程要点 | 产出 |
|---|---|---|---|---|---|---|
| M1 | **DEMO-1 Skeleton Live** | 2026-06-15 | 线上/线下（TBD）| V&V 工程师、技术负责人、CCS surveyor、DNV 验证官（观察员）| 演示 turning reference comparison（02 证据）+ tool confidence report（06 证据）；surveyor 实时提问 | 会议纪要 + surveyor 初步反馈记录 |
| M2 | **DEMO-2 Decision-Capable** | 2026-07-31 | 线上/线下（TBD）| V&V 工程师、技术负责人、CCS surveyor | 演示 1000-run replay（03）+ 完整传感器退化矩阵（04）+ 编排追溯（05）；surveyor 逐条核查 audit checklist（07-ccs-mapping §4）| 会议纪要 + CCS audit checklist 预填结果 |
| M3 | **DEMO-3 Full-Stack with Safety + ToR** | 2026-08-31 | 线上/线下（TBD）| V&V 工程师、技术负责人、安全工程师、CCS surveyor | 完整 v1.0 证据包交付；HAZID RUN-001 完整报告通报；surveyor 正式预审意见 | 会议纪要 + surveyor 正式预审意见 + revision list |
| M4 | **CCS 上海现场 Pre-Review** | 2026-09~10（TBD）| 线下（CCS 上海审图中心）| 技术负责人、V&V 工程师、CCS surveyor、DNV 验证官 | 正式预审汇报；逐条过 CCS §9.1 条款；回复 surveyor 全部预审意见 | Pre-review 会议纪要 + 最终 revision 清单 + AIP 提交确认 |

---

## §5 联系人名录

| 角色 | 姓名 | 单位 | 邮箱 | 电话 | 备注 |
|---|---|---|---|---|---|
| 技术负责人 | TBD | SANGO Group | TBD | TBD | 项目总负责，AIP 提交签字人 |
| V&V 工程师 | TBD | SANGO Group（FTE）| TBD | TBD | D1.3.1 主要执行人与 CCS 日常对接人 |
| CCS Surveyor | TBD | CCS 上海审图中心 | TBD | TBD | CCS 指派后填写 |
| DNV 验证官 | TBD | DNV | TBD | TBD | 观察员角色，不直接参与 AIP 审图但可提供第三方意见 |

**注**：所有联系人信息在 CCS surveyor 正式指派后填充。在此之前，Early Letter（§3.1）以"CCS 上海审图中心"为收件方，无需指定个人。
