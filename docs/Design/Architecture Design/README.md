# MASS L3 战术层架构设计 — 文件索引

> **当前权威版本**：v1.1.2（2026-05-06）
> **下次更新**：HAZID 校准结果回填 → v1.1.3（预计 2026-08-19）

---

## 1. 主权威文件（当前生效）

```
MASS_ADAS_L3_TDL_架构设计报告.md          ← 当前权威 v1.1.2，所有下游文档引用此路径
MASS_L3_TDL_架构设计展示.html             ← v1.0 时期演示稿（待 v1.1.2 同步更新）
MASS_ADAS_L3_第一阶段开发计划.html        ← v1.0 时期开发计划（待重写）
mass-adas-l3-tdl-architecture.pdf         ← v1.0 时期 PDF 导出（待重生成）
```

> **注**：主架构 markdown 始终保持文件名 `MASS_ADAS_L3_TDL_架构设计报告.md`（无版本后缀），版本信息在文件内部头表。下游文档（详细设计 / RFC / HAZID）引用此路径即永远指向当前权威版本。

---

## 2. 配套交付物（v1.1.2 配套）

```
audit/2026-04-30/                        ← 审计交付物（v1.0 → v1.1）
   ├── 00-executive-summary.md
   ├── 04-architecture-judgment.md       ← 13 对象三方对照
   ├── 05-audit-report.md                ← 40 finding 定稿
   ├── 08a-patch-list.md                 ← v1.1 patch（35 条）
   ├── 08c-adr-deltas.md                 ← ADR-001/002/003
   └── 10-v1.1-revision-audit.md         ← v1.1 复审（5 角色 + Phase 3+6）

../Detailed Design/                      ← 8 模块详细设计
   ├── 00-detailed-design-master-plan.md
   └── M1-M8 各模块/01-detailed-design.md

../Cross-Team Alignment/                 ← 6 RFC 跨团队接口对齐
   ├── 00-rfc-master-plan.md
   ├── RFC-001 ~ RFC-006
   └── RFC-decisions.md                  ← 2026-05-06 决议归档（接口锁定）

../HAZID/                                ← HAZID 工作包
   ├── 00-hazid-master-plan.md
   ├── 01-odd / 02-mrm / 03-sotif / 04-mpc / 05-colregs (5 类参数)
   └── RUN-001-kickoff.md                ← 第 1 次会议议程（2026-05-13）
```

---

## 3. 历史归档（archive/）

| 文件 | 版本 | 日期 | 摘要 |
|---|---|---|---|
| `archive/v1.0_2026-04-29_archived.md` | v1.0 | 2026-04-29 | 原始设计稿；触发审计 |
| `archive/v1.1_2026-05-05_archived.md` | v1.1 | 2026-05-05 | ADR-001/002/003 + 35 条 patch |
| `archive/v1.1.1_2026-05-05_archived.md` | v1.1.1 | 2026-05-05 | 6 条新 finding 关闭（5 角色复审）|

**当前主文件 = v1.1.2**（2026-05-06；含 RFC-006 ReplanResponseMsg + 跨团队接口锁定）。详见架构报告头表元数据。

---

## 4. 版本演进谱系

```
v1.0 (1168 行) ──── 2026-04-29 原始设计稿
   │ ↓ 审计（5 P0 / 18 P1 / 15 P2 / 2 P3）
v1.1 (1638 行) ──── 2026-05-05 ADR-001/002/003 + 35 patch + 接口对齐方案 B
   │ ↓ 5 角色复审 + Phase 3+6（6 新 finding）
v1.1.1 (1899 行) ── 2026-05-05 6 新 finding 关闭 + 附录 E HAZID 清单
   │ ↓ RFC 决议（6 RFC 全部已批准）
v1.1.2 (~1970 行) ─ 2026-05-06 ReplanResponseMsg + §15 矩阵增补 + 15s 滑窗锁定 + 跨团队接口锁定 ⬅ 当前权威
   │ ↓ HAZID 校准（5/13–8/19）
v1.1.3 (待发布) ── 2026-08-19 HAZID 校准结果回填（CPA/TCPA/MRM 参数固化）
   │ ↓ CCS i-Ship AIP 申请
v1.2.x (长期) ──── 实船试航迭代 / 多船型扩展（拖船 / 渡船）
```

---

## 5. 阅读入口推荐

**新读者首次接触本项目**：
1. 项目 `CLAUDE.md`（项目规则 + 当前阶段总览）
2. 本文件 `docs/Design/Architecture Design/README.md`（架构文件索引）
3. 当前权威 `MASS_ADAS_L3_TDL_架构设计报告.md`（v1.1.2 主文档）—— 重点读 §1 / §2 / §3 / §4 + 头表元数据
4. 详细设计模块 — `../Detailed Design/M{N}/01-detailed-design.md`

**审计师 / CCS 验船师 / DNV 验证官**：
1. 当前权威 `MASS_ADAS_L3_TDL_架构设计报告.md`（v1.1.2）
2. `audit/2026-04-30/00-executive-summary.md`（A 档复审落点）
3. `audit/2026-04-30/08c-adr-deltas.md`（3 条 ADR）
4. `../Cross-Team Alignment/RFC-decisions.md`（接口锁定证据）
5. `../HAZID/RUN-001-kickoff.md`（HAZID 工作进度）

**模块开发工程师**：
1. 当前权威架构 §X 对应模块章节（M1-M8 各 §5–§13）
2. `../Detailed Design/M{N}/01-detailed-design.md`（模块详细设计）
3. `../Cross-Team Alignment/RFC-XXX.md`（涉及的跨团队接口）

**历史回溯需求**：
- 查 v1.0 → v1.1 修订原因：`audit/2026-04-30/05-audit-report.md`
- 查 v1.1 → v1.1.1 修订原因：`audit/2026-04-30/10-v1.1-revision-audit.md`
- 查 v1.1.1 → v1.1.2 修订原因：`../Cross-Team Alignment/RFC-decisions.md`
- 查 v1.0/v1.1/v1.1.1 原文：`archive/v{X}_*_archived.md`
