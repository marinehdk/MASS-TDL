# 跨团队接口对齐 RFC 主计划

> **目的**：基于 v1.1.1 §15 接口契约总表，向 6 个跨团队（L1/L2/L4/Multimodal Fusion/Deterministic Checker/ASDR/Reflex Arc）发起接口对齐 RFC（Request for Comments）。每个 RFC 是**用户/项目经理推动跨团队会议**的结构化输入。
>
> **架构基线**：v1.1.1（`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告_v1.1.1.md`）
> **创建时间**：2026-05-05

---

## 1. RFC 清单

| RFC | 主题 | 责任团队 | 阻塞优先级 | 解锁工作 |
|---|---|---|---|---|
| **RFC-001** | M5 → L4 接口（方案 B AvoidancePlan + ReactiveOverrideCmd）| L3 + L4 Guidance | **高** | M5 详细设计启动 |
| **RFC-002** | M2 ← Multimodal Fusion（三话题聚合 + CPA/TCPA 计算责任）| L3 + Multimodal Fusion | 中 | M2 详细设计校准 |
| **RFC-003** | M7 ↔ X-axis Deterministic Checker（CheckerVetoNotification 协议）| L3 + Deterministic Checker | **高** | M7 详细设计启动 |
| **RFC-004** | L3 → ASDR（决策追溯日志 IDL）| L3 + ASDR | 低 | M2/M4/M6/M7 详细设计完整 |
| **RFC-005** | Y-axis Reflex Arc spec 量化（触发阈值 + 感知输入源）| L3 + Reflex Arc + Multimodal Fusion | **高** | M5 详细设计启动 + 系统层认证 |
| **RFC-006** | M3 → L2 反向 RouteReplanRequest（重规划请求协议）| L3 + L2 Voyage Planner | 中 | M3 详细设计完整 |

**合计**：6 个 RFC；3 高优 + 2 中优 + 1 低优

---

## 2. RFC 通用结构

每个 RFC 包含以下章节（强制）：

```
1. 背景（Context）
   - v1.1.1 中的相关章节锚点
   - 当前设计假设
   - 跨团队对齐的必要性

2. 提议（Proposal）
   - L3 团队的推荐方案
   - 接口 IDL（引用 v1.1.1 §15.1）
   - 频率 / 时延 / 错误处理

3. 备选方案（Alternatives）
   - 已考虑过的备选 + 弃用理由

4. 风险登记（Risk Register）
   - 实现复杂度
   - 时序冲突
   - 性能瓶颈
   - 法规合规

5. 决议项清单（Decision Items）
   - 须各团队确认的具体事项

6. 验收标准（Acceptance Criteria）
   - 跨团队对齐成功的明确判据

7. 时间表（Timeline）
   - kick-off 会议 / 决议 / 实现承诺日期

8. 参与方（Stakeholders）
   - 责任团队 + 角色 + 联系人

9. 参考（References）
   - v1.1.1 锚点 / 学术 / 工业实证
```

---

## 3. 推动节奏建议

### 3.1 第 1 周：Kick-off 跨团队对齐工作组

**会议议程**（建议 PM 召集）：
1. 介绍 v1.1.1 架构 + B 档复审落点（10 min）
2. 6 个 RFC 概览 + 优先级（15 min）
3. 各团队对 RFC 的初步反馈 + 阻塞确认（每团队 10 min × 7 团队 = 70 min）
4. 后续推动节奏（10 min）

**总时长**：约 2 小时

**输出物**：每个 RFC 的"初步态度"（接受 / 有保留 / 反对）+ 后续单 RFC 会议时间表

### 3.2 第 1–2 周：单 RFC 深度对齐会议

**会议节奏建议**：

| RFC | 推荐顺序 | 深度会议时长 |
|---|---|---|
| RFC-001（M5↔L4，**高优**）| 周 1 | 2 小时 |
| RFC-003（M7↔Checker，**高优**）| 周 1 | 1.5 小时 |
| RFC-005（Reflex Arc，**高优**）| 周 1 | 2 小时 |
| RFC-002（M2↔Fusion）| 周 2 | 1 小时 |
| RFC-006（L3→L2 反向）| 周 2 | 1 小时 |
| RFC-004（ASDR）| 周 2 | 0.5 小时（最简单）|

### 3.3 第 2–3 周：决议归档 + 实现承诺

每个 RFC 通过后须签署：

1. **决议文档**：跨团队对接口契约的最终决定（IDL 锁定 / 频率 / 协议）
2. **实现承诺时间表**：各团队的实现里程碑（含 v1.1.1 §15 IDL 实现 + L4 三模式改造 等具体工作量）

### 3.4 第 4 周以后：根据决议回填 v1.1.1 → v1.1.2

跨团队对齐通过后，可能引发 v1.1.1 的 patch（如 IDL 字段调整）。这些 patch 集中到 **v1.1.2**（不影响详细功能设计）。

---

## 4. 与 v1.1.1 详细设计的关系

| 关系 | 说明 |
|---|---|
| RFC-001 / 003 / 005 通过前 | M5 + M7 详细设计**不能启动** |
| RFC-002 / 004 / 006 通过前 | M2 / M3 / M4 / M6 / M8 详细设计**可以启动**，但接口字段须按 v1.1.1 §15 IDL 实现，跨团队对齐后可能微调 |
| 全部 RFC 通过后 | v1.1.1 → v1.1.2 patch；准备 CCS AIP 申请的接口契约证据 |

---

## 5. 共性设计约束（所有 RFC 须遵循）

1. **保持 v1.1.1 IDL 字段定义不变**（除非有强证据须修改）
2. **保持接口频率 + 时延 SLA 不变**（除非性能验证不可行）
3. **L3 团队提议的方案优先**（已经过 5 角色复审 + DNV 验证官独立评估）
4. **跨团队反对意见须有书面证据**（性能数据 / 实现成本估算 / 法规依据）

---

## 6. RFC 状态追踪

| RFC | 当前状态 | 阻塞 | 下一步 |
|---|---|---|---|
| RFC-001 | 草拟完成 | ⏳ 等待 L4 团队评审 | 周 1 kick-off |
| RFC-002 | 草拟完成 | ⏳ 等待 Multimodal Fusion 评审 | 周 2 |
| RFC-003 | 草拟完成 | ⏳ 等待 Deterministic Checker 评审 | 周 1 |
| RFC-004 | 草拟完成 | ⏳ 等待 ASDR 评审 | 周 2 |
| RFC-005 | 草拟完成 | ⏳ 等待 Reflex Arc + Fusion 评审 | 周 1（**最复杂**）|
| RFC-006 | 草拟完成 | ⏳ 等待 L2 评审 | 周 2 |

---

## 7. 文件清单

```
docs/Design/Cross-Team Alignment/
├── 00-rfc-master-plan.md（本文件）
├── RFC-001-M5-L4-Interface.md
├── RFC-002-M2-Multimodal-Fusion.md
├── RFC-003-M7-Checker-Coordination.md
├── RFC-004-ASDR-Interface.md
├── RFC-005-Reflex-Arc-Spec.md
└── RFC-006-L3-L2-RouteReplan.md
```
