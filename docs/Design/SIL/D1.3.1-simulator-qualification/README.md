# D1.3.1 Simulator Qualification Report

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-D1.3.1 |
| 版本 | v0.1-draft |
| 日期 | 2026-05-15 |
| 状态 | 在制（6/15 DEMO-1 前完成）|
| 上游 | SIL v1.0 统一设计套件 Doc 4 §11；V&V Plan v0.1 §8；8 月计划 v3.0 D1.3.1 |
| 下游 | CCS AIP 提交（D4.4 11 月）；DNV 验证官审核 |

## 定位

证明 SIL 仿真器是合格的验证工具（qualified verification tool），满足 DNV-RP-0513 模型保证要求，可作为 CCS《智能船舶规范 2024》§9.1 性能验证的模拟证据源。

## 交付物清单

| # | 文件 | 内容 | 状态 |
|---|---|---|---|
| 01 | `01-overview.md` | 范围 + 验证策略 + 数据可用性分级 | ⏳ 在制 |
| 02 | `02-model-fidelity-report.md` | MMG 4-DOF 保真度 vs 参考解（RMS ≤ 5%）| ⏳ 在制 |
| 03 | `03-determinism-replay.md` | 1000 次重放重复性（±0.1s, ±0.5°）| ⏳ 在制 |
| 04 | `04-sensor-confidence.md` | 传感器退化 vs CG-0264 §6 限值 | ⏳ 在制 |
| 05 | `05-orchestration-trace.md` | libcosim API trace + 通信步长审计 | ⏳ 在制 |
| 06 | `06-evidence-matrix.md` | 4 项证明 → DNV-RP-0513 条款映射 | ⏳ 在制 |
| 07 | `07-ccs-mapping.md` | CCS 智能船舶规范 §9.1 条款映射 | ⏳ 在制 |
| — | `annex/test-results/` | CI artifact dump | ✅ 目录已创建 |
| — | `annex/csv/` | 数据原件（CSV）| ✅ 目录已创建 |
| — | `annex/plots/` | 图表（PNG/SVG）| ✅ 目录已创建 |
| — | `annex/ccs-communication-schedule.md` | CCS surveyor 沟通时间表 | ⏳ 在制 |

## 数据可用性分级

| 符号 | 含义 |
|---|---|
| 🟢 Live Data | 从运行系统中实际采集 |
| 🟡 Projected | 仅含方法论文档 + 预期数据占位，待上游交付后填充 |
| 🔴 Blocked | 由于上游依赖或外部采购未完成，数据不可用 |

## 快速导航

- **开发者** → `01-overview.md` §1–§3（了解全貌）
- **V&V 工程师** → 按顺序读 01→02→03→04→05→06→07（验收顺序）
- **CCS surveyor** → `07-ccs-mapping.md` + `06-evidence-matrix.md`（认证核心）
- **DNV 验证官** → `06-evidence-matrix.md`（RP-0513 条款逐条对应）
