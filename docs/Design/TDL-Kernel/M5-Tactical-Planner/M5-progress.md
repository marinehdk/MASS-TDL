# M5 · Progress · D 任务联动

| 维度 | 说明 |
|---|---|
| 数据更新规则 | PR 合并涉及 M5 时同步更新本表 |
| 最近更新 | 2026-05-25 |
| **Currently Implementing** | — |

| D 任务 | 关系 | 状态 | 详情 |
|---|---|---|---|
| D0.1 | Closed in | ✅ | MUST-2 / MUST-5 / MUST-9 三 surgical fix |
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：全模块适用 |
| D1.3.1 (原 D1.3a) | Blocks reverse | 🟡 | M5 ROT_max 参数曲线读 Manifest 依赖 D1.3.1 仿真器侧 |
| D3.2 | Closed in | ✅ 2026-05-25 | M5 双 MPC 完整实装（2751 LOC src + 2218 LOC test）；report: [D3.2-report.md](../../Phase%203/D3.2-m5-tactical-planner/D3.2-report.md) |

## DEMO-2 阻塞贡献

- ✅ **已解除**：M5 SAT-3 `/sil/sat3_data.trajectory_candidates[]` @2Hz 已实装；前端 `MpcTrajectoryLayer` 数据源就绪

---
## 参考 D 任务文档
- D3.2: [Phase 3/D3.2-m5-tactical-planner/](../../Phase%203/D3.2-m5-tactical-planner/)（✅ spec + report 已完成）
