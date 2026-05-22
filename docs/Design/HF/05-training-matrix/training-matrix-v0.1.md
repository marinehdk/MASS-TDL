# 培训胜任力矩阵 v0.1 · D2.6

| 版本 | 2026-07-13 v0.1 |
|---|---|
| 基线 | STCW Table A-II/1（OOW 二副/值班驾驶员，Manila Amendments 2010）|
| 扩展 | MASS D2/D3 ROC 新增能力项 6 条（ROC-01–ROC-06）|
| **状态** | **骨架草案；"初始训练方法 / 复训周期 / 评估准则"三列均为 [TBD-D3.5']** |
| 消费方 | D3.5' 培训课程大纲（Phase 3，8/31 截止）|
| Finding | D P1-D-05（本文档 commit 后关闭）|

> **注：** `[TBD-D3.5']` 不是缺失内容，是**正式接口占位符**。D3.5' 的 HF 咨询 Owner 负责填写；D2.6 不填。

## Part A — STCW A-II/1 基线能力项（MASS 语义变化标注）

| 条目 | 原 A-II/1 能力描述 | MASS D2/D3 语义变化 | 初始训练方法 | 复训周期 | 评估准则 |
|---|---|---|---|---|---|
| §1 瞭望 | 维持有效瞭望（视觉 + 听觉）| 岸基 ROC 通过远程传感器瞭望；船长可能不在桥楼 | [TBD-D3.5'] | [TBD-D3.5'] | [TBD-D3.5'] |
| §1 COLREG | 遵守碰撞规则 | 系统自动执行；人员须验证并可 override | [TBD-D3.5'] | [TBD-D3.5'] | [TBD-D3.5'] |
| §2 安全航行 | 确定船位、保持安全航速 | ODD 边界识别替代传统情境感知 | [TBD-D3.5'] | [TBD-D3.5'] | [TBD-D3.5'] |
| §3 值班交接 | 桥面资源管理（BRM）| ROC-ROC 交接 + 岸基→船桥接管协议 | [TBD-D3.5'] | [TBD-D3.5'] | [TBD-D3.5'] |
| §4 紧急操纵 | 对紧急情况采取行动 | 60s ToR 时窗内接管（[R4] Veitch 2024）| [TBD-D3.5'] | [TBD-D3.5'] | [TBD-D3.5'] |

## Part B — MASS D2/D3 ROC 新增能力项

| # | 能力项 | 对应 STCW 基线 | MASS D2/D3 语义变化 | 初始训练方法 | 复训周期 | 评估准则 |
|---|---|---|---|---|---|---|
| ROC-01 | SAT-2 决策链解读 | §1（看）→ §2（判）| 新增：从规则链（Rule N → action）推断系统意图 | [TBD-D3.5'] | [TBD-D3.5'] | [TBD-D3.5'] |
| ROC-02 | ToR 启动判断与交互验证 | §4 紧急操纵 | 新增：60s 时窗内 + "已知悉 SAT-1" 完整流程 | [TBD-D3.5'] | [TBD-D3.5'] | [TBD-D3.5'] |
| ROC-03 | 自动化偏见识别与主动验证 | §1 瞭望 | 新增：识别 AI 过度信任/不足信任场景并主动核实 | [TBD-D3.5'] | [TBD-D3.5'] | [TBD-D3.5'] |
| ROC-04 | BNWAS 等效确认操作 | §3 值班交接 | 新增：heartbeat 响应 / SAT-1 等效交互合规执行 | [TBD-D3.5'] | [TBD-D3.5'] | [TBD-D3.5'] |
| ROC-05 | ODD 边界识别与降级响应 | §2 安全航行 | 新增：识别 ODD-C/D → 触发 MRC 准备判断流程 | [TBD-D3.5'] | [TBD-D3.5'] | [TBD-D3.5'] |
| ROC-06 | 网络安全事件下的手动接管 | — | 新增（IACS UR E26/E27 要求）[TBD-D3.9] | [TBD-D3.5'] | [TBD-D3.5'] | [TBD-D3.5'] |

## Part C — 接口声明（D3.5' 输入）

D3.5'（HF 咨询，Phase 3，8/31 截止）须：
1. 填充所有 `[TBD-D3.5']` 列
2. 将 ROC-06 `[TBD-D3.9]` 与 D3.9 RFC-007 cyber 结论对齐后填写
3. 升版为 v0.2 并在 `docs/Design/Phase 3/D3.5/training-matrix-v0.2.md` 继续迭代

## 参考文献

- STCW Table A-II/1 (2010 Manila Amendments) — OOW 胜任能力框架
- [R4] Veitch et al. (2024) — ToR 60s 接管时窗
- IMO MASS Code MSC 110/111 — 自主等级人员要求
- [NLM-STCW] 🟢 High — STCW/MASS ROC competency hybrid framework
