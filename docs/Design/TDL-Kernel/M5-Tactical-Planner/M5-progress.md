# M5 · Progress · D 任务联动

| 维度 | 说明 |
|---|---|
| 数据更新规则 | PR 合并涉及 M5 时同步更新本表 |
| 最近更新 | 2026-05-20 |
| **Currently Implementing** | D3.2（6.5pw，CasADi 集成在）|

| D 任务 | 关系 | 状态 | 详情 |
|---|---|---|---|
| D0.1 | Closed in | ✅ | MUST-2 / MUST-5 / MUST-9 三 surgical fix |
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：全模块适用 |
| D1.3.1 (原 D1.3a) | Blocks reverse | ⏳ | M5 ROT_max 参数曲线读 Manifest 依赖 D1.3.1 仿真器侧 |
| D3.2 | Currently Implementing | 🟡 | M5 双 MPC 完整实装（CasADi 集成在）；目标 8/10 |

## DEMO-2 阻塞贡献

- 🔴 **P0 高阻塞**：Engineer 视图 MpcTrajectoryLayer 数据源完全依赖 M5 SAT-3 输出；前端组件壳已 mount，生产端未到位
- 建议 7/27 前提供 trajectory_candidates stub（13 几何分支，无完整最优化）

---
## 参考 D 任务文档
- D3.2: [Phase 3/D3.2-m5-tactical-planner/](../../Phase%203/D3.2-m5-tactical-planner/)（待建）
