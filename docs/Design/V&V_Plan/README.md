# V&V_Plan 目录索引

MASS-L3 TDL V&V（Verification & Validation）计划文件夹。

| 文件 | 版本 | 描述 | 状态 |
|---|---|---|---|
| [00-vv-strategy-v0.1.md](00-vv-strategy-v0.1.md) | v0.1 | V&V 计划主文档：Phase Gate 判据 / KPI 矩阵 / SIL 2 Safety Function Registry / 工具链 / 角色 / CCS 证据骨架 / 风险 | ✅ D1.5 产出（2026-05-12）|

## 文档演进计划

| 版本 | 触发 D 任务 | 预计日期 | 主要变更 |
|---|---|---|---|
| v0.1 | D1.5 | 2026-05-31 | 初版：Phase 1–4 Gate + KPI + §2.5 SIL 2 Registry + 工具链 + CCS 骨架 |
| v0.2 | D2.8 | 2026-07-31 | [TBD-HAZID-SFF-001] / [TBD-HAZID-MRC-001] 初值填入（HAZID 中期）；dds-fmu 实测数据更新 |
| v1.0 | D3.8 | 2026-08-31 | HAZID RUN-001 完整回填；Phase 3 Gate 全部数字确认；Phase 4 Gate 细化 |

## 自动校验

```bash
# Phase 1 Gate 自动校验（DEMO-1 6/15 现场）
python tools/check_entry_gate.py --phase 1 --artifacts-dir test-results/
```

详见 [tools/check_entry_gate.py](../../tools/check_entry_gate.py)。

## 关联文件

- 架构报告（权威）：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`
- SIL 决策记录：`docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md`
- 8 月开发计划 v3.1：`docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`
- 场景 schema（D1.6）：`docs/Design/SIL/`（待产出）
- HARA + FMEDA（D2.7）：`docs/Design/Safety/HARA/`（待产出）
