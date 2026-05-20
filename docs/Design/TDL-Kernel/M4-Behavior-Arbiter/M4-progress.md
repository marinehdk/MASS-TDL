# M4 · Progress · D 任务联动

| D 任务 | 关系 | 状态 | 详情 |
|---|---|---|---|
| D0.2 | Closed in | ✅ | RFC-009 IvP 实现路径 = 自实现 |
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：全模块适用 |
| D3.1 | Currently Implementing | ⏳ | `feat/d3.1-m4-behavior-arbiter` 分支（Tasks 1-6 + Task 6 quality fixes 已 commit）|
| D2.5 | Blocks | ⏳ | SIL 集成需 M4 真发 SAT-2 |

## DEMO-2 阻塞贡献

- 🔴 **P0 高阻塞**：Engineer 视图 IvpRiskGradientLayer 数据源完全依赖 M4 SAT-2 输出；当前消费端壳已 mount，生产端未到位 = Screen 3 双端真空
- 建议 7/26 前提供 IvP 贡献 stub（哪怕固定权重表）
