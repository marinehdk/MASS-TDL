# M7 · Progress · D 任务联动

| D 任务 | 关系 | 状态 | 详情 |
|---|---|---|---|
| D0.3 | Closed in | ✅ | MUST-11 工时拆 6→9pw（core 6 + sotif 3）|
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：PATH-S 严格规则（LineThreshold=40, 禁 malloc, 禁全局变量, 独立性检查） |
| D3.3a | Currently Implementing（计划）| 🔴 未启 | M7-core：6 类硬约束 + FMEDA + Doer-Checker 三量化；目标 8/10 |
| D3.3b | Currently Implementing | ⏳ | `feat/d3.3b-m7-sotif`（1 commit）；目标 8/16 |
| D2.7 | Blocks reverse | 🔴 未启 | HARA + FMEDA M1（M7 FMEDA 在 D3.3a）|

## DEMO-2 阻塞贡献

- 🟡 中阻塞：SotifMonitorStrip 6 行进度条数据源依赖 `/sil/sotif_metrics`；DEMO-2 阶段可用 stub 值占位
