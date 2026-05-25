# M7 · Progress · D 任务联动

| 维度 | 说明 |
|---|---|
| 数据更新规则 | PR 合并涉及 M7 时同步更新本表 |
| 最近更新 | 2026-05-22 |
| **Currently Implementing** | D3.3a/b（SOTIF 5 类假设违反代码在）|

| D 任务 | 关系 | 状态 | 详情 |
|---|---|---|---|
| D0.3 | Closed in | ✅ | MUST-11 工时拆 6→9pw（core 6 + sotif 3）|
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：PATH-S 严格规则（LineThreshold=40, 禁 malloc, 禁全局变量, 独立性检查） |
| D3.3a | Currently Implementing | 🟡 | M7-core：6 类硬约束 + FMEDA + Doer-Checker 三量化在途；目标 8/10 |
| D3.3b | Currently Implementing | ⏳ | `feat/d3.3b-m7-sotif`（1 commit）；目标 8/16 |
| D2.5 | Blocks | ⏳ | D2.5 依赖 M7 生产 `/sil/sotif_metrics` topic；DEMO-2 阶段可用 stub 值占位，7/31 前须打通 |
| D2.7 | Blocks reverse | ✅ 2026-05-22 | HARA 32 危险源 + FMEDA M1 v1.0 20 失效模式 + SIF 全覆盖已完成（M7 FMEDA 在 D3.3a）|

## DEMO-2 阻塞贡献

- 🟡 中阻塞：SotifMonitorStrip 6 行进度条数据源依赖 `/sil/sotif_metrics`；DEMO-2 阶段可用 stub 值占位

---
## 参考 D 任务文档
- D3.3a: [Phase 3/D3.3a-m7-core/](../../Phase%203/D3.3a-m7-core/)（待建）
- D3.3b: [Phase 3/D3.3b-m7-sotif/](../../Phase%203/D3.3b-m7-sotif/)（待建）；分支: `feat/d3.3b-m7-sotif`
