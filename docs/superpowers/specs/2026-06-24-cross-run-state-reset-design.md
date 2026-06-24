# 跨场景状态自愈与无状态清理机制设计

- **Spec ID**: 2026-06-24-cross-run-state-reset
- **分支**: `codex/cross-run-state-reset`（从 `main` @ f1a6254b 派生，在 `.worktrees/main-runtime` 实现）
- **状态**: 待实现
- **日期**: 2026-06-24

## 1. 背景与根因

### 1.1 问题现象

COLREGs clean-8/12 probe 批量运行必须 `--restart-between-runs`（每个场景前 `docker restart sil-nodes`），否则第二个场景的 verdict/轨迹被第一个场景污染。强制重启每场景耗时 ~60-70s（cold start 32s + settle 24s），12 场景 ≈ 12-14 min 纯重启开销。

### 1.2 根因（已确证）

对照实验铁证（同场景 rule14-ho 不重启连跑两次）：

| 指标 | RUN-1 | RUN-2 | 差异 |
|------|-------|-------|------|
| behavior 转换序列 | 7 次切换 | 5 次切换 | 不同 |
| 避让 onset 时刻 | sim_t=110.8s | sim_t=53.0s | 早 58 秒 |
| max heading deviation | 52.3° | 63.2° | 多 11° |
| final heading dev | 28.7° | 2.6° | 完全不同 |

### 1.3 根因链

1. **M6（colregs_reasoner）已自愈** — `colregs_reasoner_node.cpp:500-520` 检测 world_state 的 sim-time 回跳（`dt_s < -1.0`）自动清 7 类 latch。
2. **M4/M5/M2/bridge/L4 未自愈** — 大量跨场景累积态（FSM latch / MPC warm state / track history / 控制指令残留）无任何清状态机制。
3. **orchestrator cleanup 清不到 L3 模块** — `lifecycle_bridge.py:39-48` 的 `_SIL_LIFECYCLE_NODES` 只含 8 个无状态 SIL 仿真节点；M1-M8 的 C++ 节点 + bridge + L4 全是普通 `rclcpp::Node`/`rclpy.Node`（非 LifecycleNode），无 `on_cleanup` 钩子。
4. 因此 L3 决策层状态跨场景残留，第二个场景继承第一个场景的 FSM/滤波器/latch 起始状态。

## 2. 设计目标

1. **健壮自愈**：每个模块在检测到新场景时自动清自己的跨场景累积态，等效于冷启动。
2. **无状态清理**：清理的是"累积态"（latch/history/cache/warm-state），不是"配置态"（params/config，由 SetParameters 注入）。
3. **统一信号源**：所有模块用同一显式信号触发清理，可审计（CCS auditability）。
4. **surgical**：不把节点改成 LifecycleNode（改动过大）；最小化 orchestrator 改动。

## 3. 触发机制：`/sil/scenario_loaded` topic

### 3.1 选择理由

- **已是项目既定模式**：`gnc_route_mock_publisher.py`、`mock_l2_publisher.py`、`route_ingest_node.py` 三个节点已订阅 `/sil/scenario_loaded`，L3 模块沿用是统一现有模式而非发明新机制。
- **显式语义**：scenario_id 是明确信号，比 sim-time 回跳推断更健壮、可审计。
- **零核心逻辑改动**：orchestrator 已在 configure/activate 时发布（`lifecycle_bridge.py:438,449`）。

### 3.2 唯一 orchestrator 改动：QoS 升级为 TRANSIENT_LOCAL

**问题**：当前 `_scenario_loaded_pub` 用 `QoSProfile depth=10`（volatile）。L3 的 C++ 节点由 entrypoint 在 Stage 3 串行 launch（每个 sleep 1s），可能在 orchestrator 首次发布 scenario_loaded（configure 时）之后才启动，volatile QoS 会让它们错过首次信号。

**改动**：`lifecycle_bridge.py:144-146` 的 publisher QoS 改为 `TRANSIENT_LOCAL`（latched），保证后启动的订阅者收到最近一次发布的 scenario_id。这是 1 行改动。

## 4. 各模块改动清单

每个模块新增：① `/sil/scenario_loaded` 订阅（`std_msgs/String`）② `_reset_cross_run_state()` 方法，在回调中调用。

### 4.1 M4 behavior_arbiter（残留主因，优先级最高）

**文件**: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_arbiter_node.hpp`, `src/.../behavior_arbiter_node.cpp`

**清理字段**（`behavior_arbiter_node.hpp:116-134`）:
- `prev_primary_` — 上一个主行为
- `m3_active_latch_` — M3 曾激活标记
- `colregs_rule15_commit_active_` — Rule15 give-way 责任锁存
- `colregs_inactive_cycles_` — 释放计数器
- `recovery_active_` + `recovery_dwell_cycles_` — RECOVERY 状态机
- `risk_ranking_state_` — 风险排序历史（`mass_l3::risk::RankingState`）
- `last_active_colregs_` — 上一个 COLREGs 约束（reset 到 nullopt）
- `prev_odd_zone_`、`prev_health_` — 上一周期 ODD/health 快照

**reset 语义**：清零到构造时的"未观测"初值（`prev_primary_` 回到默认/无效，latch 清 false，`risk_ranking_state_` 重置）。

### 4.2 M5 tactical_planner（mid_mpc_node）

**文件**: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp`, `src/.../mid_mpc/mid_mpc_node.cpp`

**清理字段**:
- `last_solution_` — 上一个 MPC 解（warm start 用），reset 到 `std::nullopt`
- `nomoto_fallback_` 内部状态 — fallback 控制器的累积态
- `wp_gen_` waypoint 生成器的跨场景缓存
- `solver_`/`formulation_` — 求解器若有跨周期 warm state

**reset 语义**：MPC 回到"无历史解、冷启动求解"状态。

### 4.3 M6 colregs_reasoner（统一迁移）

**文件**: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`

**改动**: 新增 scenario_loaded 订阅作为主触发，**保留**现有 sim-time 回跳（`colregs_reasoner_node.cpp:500-520`）作为防御性兜底。

**清理字段**（复用现有逻辑，即 sim-time 回跳里那 7 个 `.clear()`）:
- `rule_latches_`、`give_way_latches_`、`standon_latches_`
- `encounter_reference_heading_`、`resolved_targets_`
- `prev_target_range_`、`prev_target_bearing_`

**设计**：把现有回跳检测里的清理逻辑**提取成 `_reset_cross_run_state()` 私有方法**，scenario_loaded 回调和 sim-time 回跳都调用它（单一清理实现，两个触发点）。

### 4.4 M2 world_model

**文件**: `src/l3_tdl_kernel/m2_world_model/include/m2_world_model/world_model_node.hpp`, `src/.../world_model_node.cpp`

**清理字段**: target track buffer/history（卡尔曼滤波轨迹历史、disappearance 计数器）。具体字段名实现时从 pimpl impl 类确认（node 用 pimpl 模式，成员在 impl）。

**reset 语义**：清空所有 target track，回到"未观测任何目标"状态。

### 4.5 sil_topic_bridge（Python）

**文件**: `docker/sil_topic_bridge.py`

**改动**: 该 node 已有 `reset()` 方法（line 267）和 `_reset_state()` 逻辑。新增 scenario_loaded 订阅，回调调用已有的 `reset()`。

**清理字段**（line 394-398 等）: `last_cmd_deg`、`last_cmd`、`_last_valid_plan_time`、`_last_avoidance_waypoint`、`_last_avoidance_waypoints`、`_last_odd_state`、`_last_behavior_plan`、避让 latch 系列。

### 4.6 L4 guidance adapter（Python）

**文件**: `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py`

**改动**: 该 node 是普通 `Node`（非 LifecycleNode，line 74），已有 `_reset_state(clear_route=...)` 方法（line 147）。新增 scenario_loaded 订阅，回调调用 `_reset_state(clear_route=False)`（route 由单独的 route 注入管理，不在跨场景清理范围）。

**清理字段**: `_latch_release_*` 系列、`_last_actuator_publish_time`、`_safety_gate_*`。

## 5. 不在范围内（YAGNI）

- 不把节点改成 LifecycleNode（违反 surgical changes）
- 不修改 orchestrator 的 `_SIL_LIFECYCLE_NODES` 列表（L3 靠 topic 触发，不走 lifecycle）
- 不动 sim_rate/settle/CPU cap（治标项，独立处理）
- 不改 bridge/L4 已有的 reset 方法实现（只接触发点）

## 6. 设计不变量与安全约束

1. **幂等**：`_reset_cross_run_state()` 可在任何时刻安全调用，不依赖特定状态，重复调用无副作用。
2. **不清配置态**：reset 只清"累积态"。params/config 由 SetParameters 在 configure 阶段注入，不在 reset 范围。
3. **线程安全**：C++ 节点的 reset 回调与正常 timer 回调可能并发，reset 必须在已有 `state_mutex_` 保护下进行（M6 已有此模式）。
4. **QoS 一致性**：所有 scenario_loaded 订阅用 TRANSIENT_LOCAL（对齐 publisher），保证不错过首帧。
5. **M6 双保险不矛盾**：scenario_loaded（主）和 sim-time 回跳（兜底）都调用同一个 `_reset_cross_run_state()`，重复触发由幂等性保证安全。

## 7. 验证标准

### 7.1 单元测试（每模块）

每个模块新增单元测试：构造 → 注入残留状态 → 调 `_reset_cross_run_state()` → 断言所有跨场景字段回到初值。

### 7.2 集成验证（对照实验，硬性 gate）

复用诊断阶段的 `/tmp/residual_probe.py` 方法（不重启连跑同场景两次）：

**通过条件**（改完后）：
- behavior 转换序列 RUN-1 == RUN-2（完全一致）
- max heading deviation |RUN-1 - RUN-2| < 2°
- 避让 onset 时刻 |RUN-1 - RUN-2| < 5s

对照改前数据（onset 差 58s、dev 差 11°），改后必须收敛到一致。

### 7.3 全量回归

去掉 `--restart-between-runs`，用 batch runner 跑完整 clean-8，确认 verdict 与带 restart 的基线一致（不引入新 RED）。

## 8. 预期收益

- 消灭重启需求 → 每场景省 ~50s（cold start + settle）
- 12 场景 clean-8 ≈ 12 min 纯重启开销消除
- 仿真总时长 30 min → ~18-20 min
- 架构收益：L3 决策层具备 CCS 可审计的跨场景状态隔离语义

## 9. 引用

- 根因证据：本仓库诊断会话 2026-06-24（mempalace drawer `colregs-sim-perf-rootcause`）
- M6 自愈实现：`colregs_reasoner_node.cpp:500-520`
- scenario_loaded 现有订阅者：`docker/gnc_route_mock_publisher.py:50`, `docker/mock_l2_publisher.py:16`, `docker/route_ingest_node.py:90`
- orchestrator cleanup 边界：`lifecycle_bridge.py:39-48`
