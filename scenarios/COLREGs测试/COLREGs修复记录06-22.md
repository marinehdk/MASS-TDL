写入 MemPalace 日记并运行 mine 脚本：

Ran command: `mempalace diary-write --wing MASS-L3 "## [2026-06-22] COLREGs M6 振荡修复 + 场景验证

### 任务目标
修复 COLREGs 探针剩余失败场景（rule13-ot, rule15-cs, rule15-cs-2, rule15-cs-intelligent），从 M6 振荡根因出发。

### 关键决策和发现
1. **M6 oscillation root cause**: encounter_fsms_ 未随 rule_latches_/give_way_latches_ 一起清除。projection_resolved 触发时虽删了 latches 但 FSM 残留，下轮立即重新 conflict→无限振荡。Fix: erase encounter_fsms_[rule13/14/15_key]。
2. **target_vessel_node 架构**: COLREGs 场景目标船走 target_vessel_node（不是 ais_replay_node）。configure 阶段读 default_targets_json，activate 后 10Hz sim-time 发 /sil/target_vessel_state → bridge → /fusion/tracked_targets → M2。configure-only 时 targets=0 属正常。
3. **YAML 坐标实验失败**: 尝试推远目标坐标提升 TCPA>720s，但每次 configure 失败或目标不可见。决策：恢复原始坐标，M6 fix 应已足够。
4. **M2 无距离门限**: track_buffer 无范围过滤，dynamic_horizon_nm=5nm 仅用于 ENC 查询。
5. **rule15-cs 几何**: target 63.461426N/10.437108E, COG=290, SOG=10.61kn; own 63.44N/10.38E, COG=0, SOG=12kn. TCPA=553s < t_plan_s=720s → ESM 直接 ACTIVE。

### 产出
- commit 82711a84 (colregs-behavior-fix branch): M6 fix + YAML restores
- 5 个已通过（rule14-ho, ho-port, ho-intelligent, rule17-cr-so, rule17-cr-so-target-giveway）
- 待验证：rule15-cs/cs-2/cs-intelligent/rule13-ot（M6 fix 后原始坐标）

### 下一步
重启 sil-nodes → 跑 rule15-cs 单场景验证 M6 fix → 如仍无避碰则查 M6 logs 排查 conflict_detected=false 原因"

echo "=== MemPalace diary written ===" && \
python3 ~/.claude/hooks/mine-antigravity.py 2>&1 | tail -10`

MemPalace 写入中，等待完成后给你最终完整提示词。先在这里整理：

---

## 📋 当前进度全景

### 12 场景状态

| 场景                          | 状态         | 说明                          |
| ----------------------------- | ------------ | ----------------------------- |
| `rule14-ho`                   | ✅ **PASS**   | 已通过                        |
| `rule14-ho-port`              | ✅ **PASS**   | 已通过                        |
| `rule14-ho-intelligent`       | ✅ **PASS**   | 已通过                        |
| `rule17-cr-so`                | ✅ **PASS**   | 已通过                        |
| `rule17-cr-so-target-giveway` | ✅ **PASS**   | 已通过                        |
| `rule13-ot`                   | ❓ **待验证** | M6 fix 后未跑                 |
| `rule13-ot-target-giveway`    | ❓ **待验证** | 未跑过                        |
| `rule15-cs`                   | 🔴 **待验证** | M6 fix 已提交，原始坐标已恢复 |
| `rule15-cs-2`                 | 🔴 **待验证** | 同上                          |
| `rule15-cs-intelligent`       | 🔴 **待验证** | 同上                          |
| `rule15-cs-edge`              | ❓ **未运行** | 不在标准8探针                 |
| `rule15-ot-boundary`          | ❓ **未运行** | 不在标准8探针                 |

### 本次 Session 完成的关键修复

**M6 振荡根因 + 修复** (commit `82711a84`)：
- 文件: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp` Lines 720-722
- 修复: `encounter_fsms_.erase(rule13/14/15_key)` 在 `projection_resolved` 触发时执行
- 没有这个 erase，FSM 残留 → 下轮立刻重新触发 conflict → 无限振荡

---



> 我有一个疑问，目前能输出一条避碰和回归航线给L4吗。因为马上需要和L4层级同事开发的代码进行集成，目前可能挑一个对头的场景 [colreg-rule14-ho.yaml](scenarios/COLREGs测试/colreg-rule14-ho.yaml) 完成验证。

短答：**能输出避碰航线给 L4；“回归航线”当前不是同一条可执行 AvoidancePlan 被 L4 跟踪，而主要由 L4 在 `BEHAVIOR_RECOVERY/TRANSIT` 后回到 L2 route。**

核实点：

- M5 输出 `AvoidancePlan`，msg 明确写了 “L4 在避让模式下用此覆盖 L2 PlannedRoute”：
  [/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/src/l3_tdl_kernel/l3_msgs/msg/AvoidancePlan.msg (line 1)](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/src/l3_tdl_kernel/l3_msgs/msg/AvoidancePlan.msg:1)
- M5 在避碰时生成 10 个几何 fallback waypoint；在 `BEHAVIOR_RECOVERY=7` 时也生成 6 个 `status="RECOVERY"` waypoint：
  [/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp (line 377)](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:377)
- L4 adapter 订阅 `/l3/m5/avoidance_plan` 和 `/l3/m4/behavior_plan`，发布 `/sil/actuator_cmd`：
  [/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py (line 111)](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py:111)

关键 caveat：

- M5 原始 publish topic 是 `/m5/avoidance_plan`，SIL Shell-B harness 用 remap 接到 `/l3/m5/avoidance_plan`：
  [/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/src/sim_workbench/shell_b_harness/shell_b_harness/simulator.py (line 377)](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/src/sim_workbench/shell_b_harness/shell_b_harness/simulator.py:377)
  如果 L4 同事不用这个 harness，而是直接 launch L3/L4，需要显式加 remap，否则 L4 看不到 M5 plan。
- L4 当前只把 `AvoidancePlan.status in ("NORMAL", "DEGRADED")` 当可执行避碰 plan。M5 的 `status="RECOVERY"` waypoint 当前不会被 L4 当避碰 plan 消费；`BEHAVIOR_RECOVERY=7` 更多是触发 latch release / route return。

所以集成建议：

1. 先用 `colreg-rule14-ho.yaml` 做最小闭环，合适。已有本地 evidence 显示 `overall_pass=True`, `cpa_ok=True`, `stability_pass=True`, min CPA 约 323-336m。

2. 给 L4 同事的接口先按现状定义：`/l3/m5/avoidance_plan` 用于避碰段；`/l3/m4/behavior_plan` 控制避碰/回归/直航状态；回归执行依赖 L4 route-follow。

3. 如果 L4 需要“一条连续避碰+回归航线”而不是状态机+AvoidancePlan，需要补一个明确 contract：要么 L4 支持 `status="RECOVERY"`，要么 M5 发布统一的 L4 route/trajectory topic。当前代码还不是这个形态。

   







---

> 目前12-probe场景的进度如何，之前是5/12 PASS，现在有没有好转呢？

目前没有新的 **12-probe 全量 batch 结果**，所以不能说已从 5/12 正式提升。

按已跑单场景证据看，有好转：

- 之前确认 5 个 GREEN：`rule14-ho`、`rule14-ho-port`、`rule14-ho-intelligent`、`rule17-cr-so`、`rule17-cr-so-target-giveway`
- 后续单跑又修到 GREEN：`rule15-cs`、`rule15-cs-2`、`rule15-cs-intelligent`
- 所以“单场景层面”可疑似到 **8/12 候选 GREEN**

但关键点：

- `rule13-ot` 仍 RED。
- 先前 L4 speed 修复后，速度链变好：本船不再卡在 6.67kn，能超过目标 7kn。
- 但 `rule13-ot` 仍因 M4/M6 AVOID↔RECOVERY 抖动、steering_reversals、seamanship、overtake_completed=False 失败。
- 刚开始做 M4 “active conflict 不得进 RECOVERY”修复，但被中断，尚未完整验证、也未重新跑 Rule13/12-probe。

结论：**还没有正式从 5/12 PASS 好转为某个新数字。保守说：12-probe 当前确认状态仍停留在 5/12；局部单场景证据显示 rule15 三个已改善，Rule13 仍是主要 blocker。**

> 你的意思是在目前的对话中新增了 rule15-cs、rule15-cs-2、rule15-cs-intelligent PASS了吗？

是的，按当前对话里已跑的单场景结果：

- `rule15-cs`：PASS
- `rule15-cs-2`：PASS
- `rule15-cs-intelligent`：PASS

但这是**单场景验证 PASS**，还不是重新跑完整 `12-probe --include-intelligent --restart-between-runs` 后的正式 12/12 batch 结果。

当前保守状态：

- 之前 batch：5/12 PASS
- 本轮单场景新增确认：3 个 Rule15 PASS
- `rule13-ot`：仍 RED
- 还缺完整 12-probe 重跑确认是否整体提升，以及是否有回归。

> 我知道了，那现在在调试的是哪个场景呢？

现在调的是 `colreg-rule13-ot`。

当前状态：

- 已修一层：L4 在 active avoidance regression 下不再丢 M5 的 `14 kn` 速度请求，本船速度能超过目标 `7 kn`。
- 但场景仍 RED：`overtake_completed=False`，并且 M4 在 M6 conflict 仍 active 时反复 `AVOIDANCE ↔ RECOVERY`，导致 `steering_reversals=23`、`seamanship_gate=False`、`stability=False`。
- 正在修的下一层是 M4 RECOVERY 触发条件：RECOVERY 应该只在 **M6 conflict 真正 release** 后进入，不能因为临时没有 turn direction 就进入 RECOVERY。

> 下一步的工作计划是什么呢？是将单个场景PASS吗，还是修复相关Moudle，完成4个Phase的泛化避碰呢？

下一步不是“把 `rule13-ot` 单场景调绿”。正确目标仍是：**修复相关 Module 的系统级契约，让 4 Phase 避碰链泛化收敛**。`rule13-ot` 只是当前暴露系统问题的 probe。

计划顺序：

1. **完成当前 M4 修复**
   - 问题：M6 conflict 仍 active 时，M4 因 turn direction 短暂消失误进 `RECOVERY`。
   - 目标：`RECOVERY` 只在 M6 conflict 真正 release 后触发。
   - 验证：M4 单测全绿，再跑 `rule13-ot`。
2. **复测 `rule13-ot`**
   - 看是否消除 `AVOIDANCE ↔ RECOVERY` 抖动。
   - 重点指标：`behavior_toggles`、`steering_reversals`、`overtake_completed`、`seamanship_gate`。
3. **若 Rule13 仍 RED，继续按根因分层**
   - 不改场景阈值。
   - 优先查 M6 release/latch、M5 overtake trajectory、L4 heading stability。
   - 每层 TDD：失败测试 → 实现 → 单测 → 单场景。
4. **Rule13 稳后，跑完整 12-probe**
   - `--include-intelligent --restart-between-runs`
   - 目标不是单场景，而是确认没有 Rule14/Rule15/Rule17 回归。
5. **再收敛剩余 RED**
   - 以 batch 结果分组修 Module。
   - 最后才谈 promotion gate。

所以方向：**Module-level 泛化修复优先；probe 只做验证和定位。**



> "选用 runs/trace_eval/local_clean8_strict_20260619_000145/colreg-rule14-ho.trace_current.jsonl：它有 matching PASS verdict，而且含完整 /sil/own_ship_state。现在生成两份产物：完整轨迹 CSV + 每 500m 抽样 waypoint CSV/GeoJSON。"
> 这个是否合适呢，不行还是它吧，能给出CSV文件，完整航路点就行。

可以，已按你指定的 PASS trace 生成：

- 完整本船航迹 CSV：[colreg-rule14-ho_local_clean8_strict_full_track.csv](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/runs/trajectory_exports/colreg-rule14-ho_local_clean8_strict_full_track.csv)
- 500m 抽样 waypoint CSV：[colreg-rule14-ho_local_clean8_strict_waypoints_500m.csv](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/runs/trajectory_exports/colreg-rule14-ho_local_clean8_strict_waypoints_500m.csv)
- 元数据 JSON：[colreg-rule14-ho_local_clean8_strict_track_metadata.json](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/runs/trajectory_exports/colreg-rule14-ho_local_clean8_strict_track_metadata.json)

来源：`runs/trace_eval/local_clean8_strict_20260619_000145/colreg-rule14-ho.trace_current.jsonl`
verdict：`overall_pass=true`，安全/任务/COLREGs/稳定全 PASS。

CSV 字段含：
`sim_t_s, cum_distance_m, lat, lon, east_m, north_m, sog_kn, heading_deg, rot_deg_s, behavior, avoidance_active`

统计：

- 完整轨迹点：6258
- 总航程：2685.5m
- 500m waypoint：7 个
- 已过滤 trace 开头旧坐标残留，只保留连续 63N/10E 本船航迹段。

> 请进一步绘制出此次仿真本船的航迹PNG图

已生成 PNG：

文件路径：[colreg-rule14-ho_local_clean8_strict_full_track.png](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/runs/trajectory_exports/colreg-rule14-ho_local_clean8_strict_full_track.png)

> 好的，请再输出一个航点CSV，仅保留图中的WP000到WP006共7个点的避碰航线规划航路点，我会用于给到L4同事进行仿真，看看是否在其开发的PID中执行成功。

已输出精简 7 点 CSV：

[colreg-rule14-ho_l4_pid_waypoints_wp000_wp006.csv](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix/runs/trajectory_exports/colreg-rule14-ho_l4_pid_waypoints_wp000_wp006.csv)

字段：
`index,name,lat,lon,sog_kn,heading_deg,cum_distance_m,east_m,north_m,sim_t_s`

包含 WP000-WP006 共 7 个点。