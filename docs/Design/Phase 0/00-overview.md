# Phase 0 · Pre-Kickoff Must-Fix Sprint · Overview

| 属性 | 值 |
|---|---|
| 时间 | 2026-05-08 → 2026-05-12（3 工作日）|
| 估计人周 | 2.0 |
| 状态 | ✅ **全部关闭**（2026-05-12）|
| 阶段目标 | 在 5/13 HAZID kickoff + Phase 1 D1.x 开工前，关闭 7 角度评审暴露的 11 项 must-fix + 配套 RFC + 工时表 + HTML 同步 |
| 上游 | 2026-05-07 评审 124 项 finding（`Phase 0/Archive/Review/2026-05-07/00-consolidated-findings.md`）|
| 下游 | 解锁 Phase 1 D1.x 全部启动 |

---

## D 任务清单

| D 编号 | 主题 | 状态 | 主文档 |
|---|---|---|---|
| D0 | Must-Fix Sprint（surgical fix + RFC closure + 工时 + HTML 同步）| ✅ 全闭 | [D0-spec.md](D0-must-fix-sprint/D0-spec.md) / [D0-plan.md](D0-must-fix-sprint/D0-plan.md) / [D0-report.md](D0-must-fix-sprint/D0-report.md) |

> D0 物理上是 1 个 sprint，逻辑上分 3 子项（D0.1 surgical + D0.2 RFC + D0.3 工时/HTML），子项详情见 [D0-spec.md](D0-must-fix-sprint/D0-spec.md)。

---

## 关闭的 finding（11 项 must-fix）

MUST-1（M2 OVERTAKING 扇区）/ MUST-2（Mid-MPC N 一致性）/ MUST-3（IvP licensing）/ MUST-4（HAZID-FCB 船期备忘录）/ MUST-5（M5 ROT_max 硬编删除）/ MUST-6（M2 sog 校验改读 Manifest）/ MUST-7（M8 active_role 双角色 stub）/ MUST-8（PATH-S CI 时点）/ MUST-9（M5 MRM 走 M7）/ MUST-10（3 项 deep research 启动）/ MUST-11（M7 工时 6→9 拆 core+sotif）

详见 [D0-report.md](D0-must-fix-sprint/D0-report.md)。

---

## 辅助产物

| 文件 | 用途 |
|---|---|
| [D0-coding-standards.md](D0-must-fix-sprint/D0-coding-standards.md) | D0 sprint 期间产出的编码规范片段（完整版 D1.4 接续）|
| [D0-deep-research-synthesis.md](D0-must-fix-sprint/D0-deep-research-synthesis.md) | MUST-10 三项 deep research 综合（DNV-RP-0671 / FCB 6-DOF / Veitch+BNWAS）|
| [role-reviews/](D0-must-fix-sprint/role-reviews/) | 法务-hat / CCS-hat / M4-hat / M7-hat sign-off 记录 |

---

## 关联归档

`Phase 0/Archive/` 含 3 组历史档案，**只读不引用为权威**：

| 子目录 | 内容 | 引用规则 |
|---|---|---|
| `Archive/Cross-Team Alignment/` | 9 个 RFC（001-007/009 + decisions + master plan）| RFC 决议正本应保留可访问；后续 RFC 在 `docs/Design/Cross-Team Alignment/` 新建 |
| `Archive/HAZID/` | RUN-001 kickoff + 5 类参数清单 + FCB 数据替代备忘录 | HAZID 进行中文档仍引用 |
| `Archive/Review/2026-05-07/` | 7 角度评审 7 报告 + 124 findings 整合 + finding-closure | 仅审计回溯用 |

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-20 | 初版（从 v3.2 计划 §2 + Archive 内容索引化提炼）|
