# M8 · Progress · D 任务联动

| 维度 | 说明 |
|---|---|
| 数据更新规则 | PR 合并涉及 M8 时同步更新本表 |
| 最近更新 | 2026-06-03 |
| **Currently Implementing** | — |

| D 任务 | 关系 | 状态 | 详情 |
|---|---|---|---|
| D0.1 | Closed in | ✅ | MUST-7 active_role stub |
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：M8 HMI 裁剪集 |
| D1.3.2.3 (原 D1.3b.3) | Closed in | ✅ 2026-05-25 | Web HMI 部分（MapLibre + foxglove + ToR ≥2s + SAT handler stale detection）|
| D3.4 | Closed in | ✅ 2026-05-25 | M8 完整（1220 LOC src + 1595 LOC test）；SAT-2/3/SOTIF 桥接已实装；report: [D3.4-report.md](../../Phase%203/D3.4-m8-hmi-full/D3.4-report.md) |
| feat/d-demo1-bridge-deadstick | Closed in | ✅ 2026-06-03 | Avoidance Execution Refactor; report: [D1.3.2-avoidance-refactor-report.md](../../Phase%201/D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2-avoidance-refactor-report.md) |
| D2.5 | Blocks | 🟡 | M8 SAT-2/3/SOTIF topic 已发布；SIL 集成待验证 |
| D2.6 | Closed in | 🟡 2026-05-22（框架完成）| 船长 HF Ground Truth：框架/模板就绪；访谈数据 [TBD] |
| D3.8 | Closed in（Wave 1）| ✅ 2026-08-25 | §21.1.1 S-57 管线文档；§21.1.2 foxglove 基准框架（AWAIT-D3.4 锚点）；§21.3 合规矩阵完整化；§21.4 更新 |
| D3.8 | Wave 2 等待 | ⏳ 等 D3.4 | §21.1.2 foxglove 实测值填充（来自 D3.4 evidence/foxglove_benchmark.md）|

## DEMO-2 阻塞贡献

- ✅ **已解除**：M8 SAT-2/3/SOTIF 三 topic 已发布；前端 Engineer 视图 4 组件数据源就绪

---
## 参考 D 任务文档
- D3.4: [Phase 3/D3.4-m8-hmi-full/](../../Phase%203/D3.4-m8-hmi-full/)（✅ spec + report 已完成）
- D1.3.2.3: [Phase 1/D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2.3-web-hmi/](../../Phase%201/D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2.3-web-hmi/)
