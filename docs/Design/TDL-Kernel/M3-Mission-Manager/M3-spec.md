# M3 · Mission Manager · Spec

| 属性 | 值 |
|---|---|
| 模块代号 | M3 |
| 职责一句话 | 航次计划、ETA、重规划触发 |
| 时间尺度 | 长时（分钟级）|
| SIL 等级 | SIL 1 |
| 实现路径分类 | PATH-D |
| colcon 包 | `src/l3_tdl_kernel/m3_mission_manager` |
| 当前 LOC | ~1488 |
| 真实发布 topic | `/l3/m3/mission_goal` |
| 完整详设 | [Archive/Old Modules/M3-Mission-Manager/01-detailed-design.md](../../Archive/Old%20Modules/M3-Mission-Manager/01-detailed-design.md) |

---

## 接口契约

### 上游订阅（v3.0 RFC-006 决议后）
- L1 VoyageTask（航次令、气象路由、ETA/油耗优化）
- L2 PlannedRoute（WP list + speed profile）
- M1 ODD_StateMsg（触发 ODD-B 进入时 → RouteReplanRequest）

### 下游发布
- `l3_msgs/MissionGoalMsg`（M4 / M5 消费）
- `l3_msgs/RouteReplanRequest`（→ L2）

---

## v3.0 修订要点

- 海流误差等级 中→高（F P0-F-04 整改）
- L1/L2 双订阅独立性测试（RFC-006）

---

## 当前实现状态（2026-05-20）

| 子能力 | 状态 |
|---|---|
| ROS2 node + topic 发布 | ✅ |
| 海流误差告警链路 | ⚫ 未验 |
| L1/L2 双订阅独立性 | ⚫ 未验 |

---

## 关联 D 任务（详见 [M3-progress.md](M3-progress.md)）

- **Closed in**：（无 v3.0 修订）
- **计划中**：D2.3 M3 v3.0 修订（Phase 2，目标 7/13）

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-20 | 初版 spec stub |
