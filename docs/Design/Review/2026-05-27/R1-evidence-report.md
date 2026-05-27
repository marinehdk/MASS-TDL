---
title: R1 — DEMO-1 SIL 联调活体证据采集报告
date: 2026-05-27
author: Main agent (Claude Opus 4.7) via systematic-debugging Phase 1
scenario_under_test: imazu-01-ho
scope: 4 个耦合问题的根因证据（heading/speed 漂移 / M1-M8 决策链可观测性 / 避碰行为缺失 / L2 缺位）
methodology: docker exec ros2 topic hz + topic echo on live SIL stack
status: 完成 — 提供给 R2/R3/R4/R5 plan 写作的输入证据
---

# 摘要（TL;DR）

DEMO-1 联调失败的**真正根因**不是模块未实现，而是 **5 个链路断点 + 1 个 M4 IvP 求解器结构性 bug**：

| ID | 断点 | 严重度 | 关联 Issue |
|---|---|---|---|
| **F-R1-01** | M4 IvP solver 永远返回 `IvP infeasible fallback`，无法产出有效行为目标 | 🔴 P0 | #1 #3 |
| **F-R1-02** | `/sil/actuator_cmd` 在 imazu 激活后沉默；ship_dynamics 无指令输入 → ownship 漂移 SOG 10kn→5.3kn | 🔴 P0 | #1 #3 |
| **F-R1-03** | `/l3/m3/mission_state` 永远无人发布（Publisher count: 0），M3 卡在 AWAITING_ROUTE | 🔴 P0 | #4 |
| **F-R1-04** | `/l2/planned_route` 沉默（SIL 无 L2 mock publisher），M3 无任务输入 | 🔴 P0 | #4 |
| **F-R1-05** | M5 `avoidance_plan` 输出 `target_speed_kn` 跟随 ownship 当前 SOG（自反馈回路），非 YAML 标称 10kn | 🟡 P1 | #1 #3 |
| **F-R1-06** | 后端无 `/l3/fsm_state` topic；前端 `fsmStore.ts` 仍是孤立本地状态 | 🟡 P1 | #2 |

**重要修正**（推翻 Part A/A3 推断）：TS 发布链**已经工作**（`/sil/target_vessel_state` @ 20 Hz、`/fusion/tracked_targets` @ 20 Hz）；M2 已接收并识别目标（`targets=1 agg_c=1`）。R3 修复重点从"TS publisher 缺失"转为"M4 IvP infeasible + bridge actuator_cmd 转发断"。

---

# 1. 环境与方法

## 1.1 SIL 运行环境

- 容器栈：`docker compose ps` 显示 4 个服务在线（uptime 8m+）：
  - `mass-l3-tacticallayer-sil-orchestrator-1` (REST API @ :8000)
  - `mass-l3-tacticallayer-sil-nodes-1` (ROS2 nodes M1-M8)
  - `mass-l3-tacticallayer-foxglove-bridge-1`
  - `mass-l3-tacticallayer-martin-tile-server-1`
- 前端：`pm2 list` 显示无 PM2 进程（前端可能由 Vite dev server 在另一窗口运行）

## 1.2 Scenario 激活

通过 REST API 显式重新激活：

```bash
curl -k -X POST https://localhost:8000/api/v1/lifecycle/cleanup
curl -k -X POST https://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" -d '{"scenario_id":"imazu-01-ho"}'
curl -k -X POST https://localhost:8000/api/v1/lifecycle/activate
```

激活后 `GET /lifecycle/status` 返回：

```json
{"current_state":"active","scenario_id":"imazu-01-ho","run_id":"run-19e652a1b2d","effective_backend":"ros2"}
```

✅ 场景确认为 imazu-01-ho。

> **副发现**：orchestrator 重启会丢失 lifecycle 状态。首次 probe 时拿到的 ownship 位置是 (30.5N, 122E)（长江口默认 fallback），不是 imazu 的 (63.44N, 10.38E)（特隆赫姆）。这是一个独立的运维 bug，建议在 D-DEMO1-R3 内顺手记录给 SIL Integrator。

## 1.3 探测方法

```bash
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  ros2 topic hz <topic> --window 5  # 频率
  ros2 topic echo --once <topic>    # 单帧内容
  ros2 topic info <topic> -v        # 发布订阅拓扑
'
```

每个 topic 4s timeout；`SILENT` = 4s 内无消息。

---

# 2. Topic 频率矩阵（imazu-01-ho 激活后）

| Topic | 频率 | 状态 | 解读 |
|---|---|---|---|
| `/sil/own_ship_state` | 33.2 Hz | ✅ | ship_dynamics 节点 50Hz 内循环、25/40Hz 节流到前端 |
| `/sil/target_vessel_state` | 19.9 Hz | ✅ | `ais_replay_vessel` 模型在发 TS 状态 |
| `/sil/tracked_targets` | **SILENT** | 🟡 | 与 /fusion/tracked_targets 并行命名；可能未使用 |
| `/fusion/tracked_targets` | 20.0 Hz | ✅ | M2 输入流通 |
| `/fusion/own_ship_state` | 25.1 Hz | ✅ | M2 输入流通 |
| `/l3/m1/odd_state` | 1.0 Hz | ✅ | M1 心跳 |
| `/l3/m2/world_state` | 8.0 Hz | ✅ | M2 输出，rationale 显示识别到 1 个目标 |
| `/l3/m3/mission_state` | **SILENT** | 🔴 | Publisher count = 0；M3 不发 mission_state |
| `/l3/m4/behavior_plan` | 4.0 Hz | ✅ | M4 发，但内容是 fallback |
| `/l3/m5/avoidance_plan` | 1.0 Hz | ✅ | M5 在发 plan |
| `/l3/m6/colregs_constraint` | 2.0 Hz | ✅ | M6 输出 |
| `/l3/m7/safety_alert` | 4.0 Hz | ✅ | M7 周期性 |
| `/l3/m7/heartbeat` | 10.0 Hz | ✅ | Doer-Checker 心跳 |
| `/l3/m8/ui_state` | 49.9 Hz | ✅ | HMI bridge |
| `/sil/actuator_cmd` | **SILENT** | 🔴 | imazu 激活后反而沉默（默认场景下 1 Hz） |
| `/l2/planned_route` | **SILENT** | 🔴 | 无 L2 mock，M3 永远等不到 |
| `/l1/voyage_task` | **SILENT** | 🔴 | 同上 |
| `/sil/module_pulse` | 5.0 Hz | ✅ | bridge 聚合所有模块心跳 |

---

# 3. 关键内容 dump（带证据解读）

## 3.1 `/sil/own_ship_state` — 漂移定量

```yaml
stamp: { sec: 1342, nanosec: 259999995 }
lat: 63.44579461335378       # ✅ 接近 YAML 初值 63.44
lon: 10.379996911956699      # ✅ 接近 YAML 初值 10.38
heading: 6.282920584645765   # = 2π → 0° wrap，等价于 0° ✅
sog: 2.7281618491837634      # 🔴 ≈ 5.3 kn（YAML 初值 10 kn → 已损失 47%）
cog: 6.282920584645863       # 0°
rot: -1.75e-14               # ✅ ROT ~0
u: 2.7281618491837634        # 同 sog
v: -2.6e-13                  # ≈ 0
r: 1.75e-14                  # ≈ 0
rudder_angle: 0.0            # ✅ 无舵
throttle: 0.10665046768922178 # 🟡 非零，但远低于 cruise（u0=5.144 m/s 对应应是高得多）
```

**解读**：
- 位置 + heading 暂时还对（航向只漂移到 2π wrap），SOG 严重衰减（10→5.3 kn）
- 12s 内速度 ↓ 47%，约 -0.18 m/s² 减速率
- `throttle: 0.107` 是某种残值（actuator_cmd silent，ship_dynamics 大概率在用上次值或内部默认）

## 3.2 `/sil/actuator_cmd` — 在默认场景下的内容（imazu 激活后沉默）

激活 imazu **之前**（默认场景）的样本：

```yaml
stamp: { sec: 930, nanosec: 8000002 }
lat: 0.0
lon: 0.0
heading: 0.0
sog: 0.0
cog: 0.0
rudder_angle: 0.0
throttle: 0.03544619588143196   # 与 ownship throttle 0.0355 几乎相同（echo）
```

激活 imazu **之后**：完全沉默（`SILENT` 4s+）。

**解读**：
- 消息类型重用 `sil_msgs/msg/OwnShipState`（既是状态又是命令）
- 默认场景下 bridge 在产出"零命令 + 当前 throttle"，本质是空操作
- imazu 激活后转发链断裂 — 推测 `_on_avoidance_plan` 的转发分支存在场景敏感性 bug

## 3.3 `/l3/m2/world_state` — M2 能看见目标

```yaml
schema_version: 0
stamp: { sec: 1342 }
targets:                    # ✅ 非空（之前默认场景下是 [])
- schema_version: 0
  stamp: { ... }
  ... (1 target)
own_ship:
  position: { lat: 63.44, lon: 10.38 }
  sog_kn: 5.30...
  heading_deg: 0.0
  ...
rationale: 'DV=Full/c=1 EV=Full/c=1 SV=Full/c=1 targets=1 agg_c=1'  # ✅ M2 confidence 高
```

**解读**：
- M2 **接收到 1 个目标**，置信度 1.0
- DV (Dynamic Vessel) / EV (Environment) / SV (Static Vessel) 三个置信度均为 Full
- 之前的判断（TS publisher 缺失）被推翻

## 3.4 `/l3/m4/behavior_plan` — **IvP 永远 infeasible**（核心 P0）

```yaml
schema_version: 113
stamp: { sec: 1342 }
behavior: 1                              # 1 = ?（需查 IDL；默认场景下是 0）
heading_min_deg: 269.9848327636719       # ≈ 270°
heading_max_deg: 89.98483276367188       # ≈ 90°
speed_min_kn: 0.0
speed_max_kn: 0.883266806602478          # 🔴 echoes ownship 当前 SOG
confidence: 0.30000001192092896          # 🔴 低
rationale: IvP infeasible fallback       # 🔴 P0 — 永远是 fallback
```

**解读**：
- `rationale: "IvP infeasible fallback"` 是 M4 行为字典 IvP 求解器找不到可行解时的兜底
- 在 imazu 标准对遇场景（最简单的 Rule14）也求解失败 → M4 实现存在结构性 bug
- 输出的 heading window [270°, 90°] 跨过 0° = 180° 宽，等于"任何方向都行"，是 fallback 默认值
- `speed_max_kn` 跟踪当前 SOG → 形成 **降速循环**：M4 给的最大速度 = 当前速度 → 永不加速

## 3.5 `/l3/m5/avoidance_plan` — 自反馈 plan

```yaml
schema_version: 0
waypoints:
- position: { lat: 63.44, lon: 10.38, altitude: 0.0 }
  wp_distance_m: 0.0
  safety_corridor_m: 500.0
  target_speed_kn: 0.8843765273268112    # 🔴 echoes ownship 当前 SOG
  turn_radius_m: 0.0
- position: { ... small offset ... }
  wp_distance_m: 6.824438869205225       # 🔴 6.8 米的 wp 间距 — 极短
  target_speed_kn: 0.8843765273268112
- ...
```

**解读**：
- 所有 wp 都用 `target_speed_kn ≈ 当前 SOG` → ownship 越慢、目标越慢、形成正反馈降速
- `wp_distance_m: 6.8m` 是 ownship 当前位置附近的微步 waypoints → M5 没生成航段，只生成"原地踏步"
- `safety_corridor_m: 500` 来自 YAML `cpa_min_m_ge: 500.0` ✅ 配置正确

## 3.6 `/l3/m3/mission_state` — 无人发布

```
ros2 topic info /l3/m3/mission_state -v

Type: l3_msgs/msg/MissionState
Publisher count: 0                       # 🔴 P0 — M3 节点存在但不发布
Subscription count: 1
  Node name: m1_odd_envelope_manager     # M1 在等 M3 的 mission_state
```

**解读**：
- M3 节点活着（之前 `M3-progress.md` 标 D2.3 ✅）但**没注册 publisher** 或**注册了但 never publishes**
- 根因：M3 状态机卡在 AWAITING_ROUTE（无 L2 planned_route 输入）
- 链式后果：M1 ODD manager 收不到 mission_state → ODD envelope 计算可能也降级

## 3.7 `/l2/planned_route` — 无 publisher

```
ros2 topic info /l2/planned_route
Publisher count: 0
Subscription count: 1 (m3_mission_manager)
```

**解读**：✅ 与 RFC-006 一致 — L2 责任在另一支团队；SIL 内无 mock。这是 R4 的目标修复点。

## 3.8 `/fusion/tracked_targets` — 拓扑分析

```
Publisher count: 1
  Node name: sil_topic_bridge      # bridge 转发 sim → fusion
Subscription count: 1
  Node name: m2_world_model        # M2 消费
```

**解读**：
- bridge 把 `/sil/target_vessel_state` (20Hz) 桥接到 `/fusion/tracked_targets` (20Hz) ✅
- M2 真在订阅消费 ✅

---

# 4. 因果链重建（updated from Part A）

```
[L1 缺位] /l1/voyage_task SILENT
    ↓
[L2 缺位] /l2/planned_route SILENT  ────────── F-R1-04（R4 修）
    ↓
[M3 卡死 AWAITING_ROUTE] /l3/m3/mission_state SILENT (Publisher count=0) ── F-R1-03
    ↓
[M1 ODD envelope 缺 mission 上下文]
    ↓
[M4 IvP 求解 infeasible，输出 fallback]                        ────── F-R1-01（R3 修）
    │       behavior=1, heading 270°–90°(无约束), speed_max=current SOG
    ↓
[M5 拿到 fallback 行为 → 输出微步原地 wp，target_speed=current SOG]  ─── F-R1-05
    ↓
[bridge 不转发 actuator_cmd（或转发为零命令）]                  ────── F-R1-02（R3 修）
    ↓
[ship_dynamics 无 throttle 指令 → 慢慢降速到 0]                  ────── Issue 1 漂移
    │
    └── 即使 TS 真的过来，整条决策链都在 fallback 模式，没法触发右舷转向
        → Issue 3 避碰失效
```

### 并行的可观测性断点（不在主因果链）

```
[后端无 FSM 单源节点]
    ↓
[/l3/fsm_state 不存在]
    ↓
[前端 fsmStore.ts 孤立维护]                                    ────── F-R1-06（R5 修）
    ↓
[Issue 2 看不出决策链是否激活]
```

---

# 5. 给 R2/R3/R4/R5 的输入

## 5.1 R2（TRANSIT 自动驾驶仪）必须解决

- 在 M4 fallback 状态下也要让 ship 维持 YAML 初值（10 kn / 0°）
- 当 M4 求出有效 plan 或 M5 发出真 avoidance_plan 时，平滑让出 actuator 写权
- 不能依赖 M5 plan 的 `target_speed_kn`（被 F-R1-05 自反馈污染）

## 5.2 R3（避碰链路修复）应聚焦

1. **核心修复点 ≠ TS publisher**（已工作）；改为：
   - **M4 IvP 求解器**：诊断为何在 imazu 简单 Rule14 场景下也 infeasible（输入约束矛盾？目标函数 unbounded？lookup table 缺值？）
   - **bridge `_on_avoidance_plan` → `_on_actuator_cmd` 转发**：为什么 imazu 激活后沉默（默认场景下能发），找出 scenario-sensitive 分支
   - **M5 输出污染**：`target_speed_kn` 自反馈来自 M4 `speed_max_kn`，最终源是 M3/L2 缺失 → 在 R4 落地前 R3 内可加一个 "若 L2 无 input 则取 YAML initial sog" 的兜底
2. **次要**：M6→M5 反馈回路（仍是 P1，DEMO-1 不阻断）
3. **R1 副发现**：orchestrator 重启不持久化 lifecycle state — 顺手记入 R3 的 SIL Integrator deliverable

## 5.3 R4（mock L2）必须解决

- 发布 `/l1/voyage_task` (1 Hz) + `/l2/planned_route` (1 Hz) + `/l2/speed_profile` (1 Hz) + `/l2/replan_response` (event)
- 输入来源：scenario YAML 新增 `ownShip.nominalRoute` 字段；无则按 `ownShip.initial.heading + sog` 延伸 N 海里生成单段直线
- 解锁 M3 状态机 → 触发 M1 envelope 计算 → M4 IvP 可能因此变 feasible

## 5.4 R5（FSM + HMI）必须解决

- 后端新增 FSM 聚合节点（建议挂在 M8 旁），订阅 M1 ODDState + M4 behavior_plan + M7 safety_alert，发 `/l3/fsm_state` @ 10 Hz
- 前端 `LeftDrawer` 加第三块面板，订阅该 topic，废弃 `fsmStore.ts` 本地状态
- DEMO-1 验收：能在 HMI 上直接看到 TRANSIT / AVOID 状态切换

---

# 6. 复测命令清单（plan 作者参考）

```bash
# 重置并激活 imazu
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/cleanup
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" -d '{"scenario_id":"imazu-01-ho"}'
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/activate
sleep 10

# 进入容器测 topic
docker exec -it mass-l3-tacticallayer-sil-nodes-1 bash
source /opt/ros/humble/setup.bash
source /opt/ws/install/setup.bash

# 完整证据探针
for t in /sil/own_ship_state /sil/target_vessel_state /fusion/tracked_targets \
         /l3/m2/world_state /l3/m4/behavior_plan /l3/m5/avoidance_plan \
         /l3/m6/colregs_constraint /l3/m7/safety_alert /sil/actuator_cmd \
         /l3/m3/mission_state /l2/planned_route /l3/fsm_state; do
  echo "=== $t ==="
  timeout 4 ros2 topic hz $t --window 5 2>&1 | grep "average rate" || echo "SILENT"
done
```

---

# 7. 不确定项 / TODO

- ⚫ M4 IvP infeasible 的具体原因（约束矛盾？lookup table 缺？）— R3 plan 作者需查源码
- ⚫ bridge actuator_cmd 在 imazu 激活后沉默的具体代码路径 — R3 plan 作者需查 `_on_avoidance_plan` callback
- ⚫ orchestrator lifecycle state 持久化策略 — R3 deliverable 顺带
- 🟡 `/sil/tracked_targets` 与 `/fusion/tracked_targets` 命名空间分裂 — 是否设计如此？需澄清

---

# 8. 证据完整度

| Issue | 证据完整度 | 备注 |
|---|---|---|
| #1 漂移 | 🟢 完整 | SOG 量化损失、actuator_cmd silent、M5 自反馈 三条独立证据互证 |
| #2 可观测性 | 🟢 完整 | `/l3/fsm_state` 不存在、HMI LeftDrawer 缺槽位证据已在 Part A2 |
| #3 避碰失效 | 🟢 完整 | M4 IvP infeasible 是核心证据，TS 链已工作的反证至关重要 |
| #4 L2/穿岛 | 🟢 完整 | `/l2/planned_route` Pub=0、M3 不发 mission_state、enc_loader deprecated 互证 |

---

**报告完成。task #1 standby for closure，task #2-5 已具备 unblock 条件。**
