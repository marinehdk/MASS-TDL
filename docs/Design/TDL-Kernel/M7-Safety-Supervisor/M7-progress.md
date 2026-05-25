# M7 · Progress · D 任务联动

| 维度 | 说明 |
|---|---|
| 数据更新规则 | PR 合并涉及 M7 时同步更新本表 |
| 最近更新 | 2026-05-25 |
| **Currently Implementing** | — |

| D 任务 | 关系 | 状态 | 详情 |
|---|---|---|---|
| D0.3 | Closed in | ✅ | MUST-11 工时拆 6→9pw（core 6 + sotif 3）|
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：PATH-S 严格规则 |
| D3.3a | Closed in | ✅ 2026-05-25 | M7-core：6 硬约束 + FMEDA M7 v1.0 + MRM chain + ResumeHandler + PATH-S CI 通过；report: [D3.3a-report.md](../../Phase%203/D3.3a-m7-core/D3.3a-report.md) |
| D3.3b | Closed in | ✅ 2026-05-25 | M7-sotif：6 类假设违反检测 + CheckerVetoCounter + SlidingWindow15s + SotifMetricsPublisher；report: [D3.3b-report.md](../../Phase%203/D3.3b-m7-sotif/D3.3b-report.md) |
| D2.5 | Blocks | 🟡 | D2.5 依赖 M7 生产 `/sil/sotif_metrics` topic（M7 侧已发布 @10Hz）|
| D2.7 | Closed in | ✅ 2026-05-22 | HARA 32 危险源 + FMEDA M1 v1.0 |

## DEMO-2 阻塞贡献

- ✅ **已解除**：M7 `/sil/sotif_metrics` @10Hz 已实装；前端 `SotifMonitorStrip` 数据源就绪

---
## 参考 D 任务文档
- D3.3a: [Phase 3/D3.3a-m7-core/](../../Phase%203/D3.3a-m7-core/)（✅ spec + report 已完成）
- D3.3b: [Phase 3/D3.3b-m7-sotif/](../../Phase%203/D3.3b-m7-sotif/)（✅ spec + plan + report 已完成）
