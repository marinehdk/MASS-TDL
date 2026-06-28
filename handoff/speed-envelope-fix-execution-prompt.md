# 新对话提示词：执行 COLREGs Speed-Envelope Contract 完整修复

将以下内容作为新对话的首条用户消息粘贴（或参考结构调整）。

---

继续 MASS-L3 TDL COLREGs speed-envelope contract 完整修复。上几会话已完成：诊断（6 场景全新真实 trace）+ spec + implementation plan。本次执行 plan。

默认要求：
- caveman full 中文，短句，信息密度高。
- 严格遵守 colregs-probe + systematic-debugging + test-driven-development + writing-plans 的工作流。
- 不为单场景调参，不改 scorer/阈值/skip/mock/forced PASS，不加 scenario-id 分支，不加 vessel-specific 分支。
- COLREGs 缺陷按完整链路排查：L2 → M2 → M6 → M4 → M5 → L4/GNC → M7 → M8。
- 每个修复点 TDD：先写失败测试，再实现，counterfactual 回归锁。
- 每个 Workstream 独立 commit，cohort 回归。
- Iron Law（上几会话教训）：跨模块集成修复必须亲自 trace 验证，不依赖单次归因。Class B 两次轻信 Agent 归因（plan_id churn → return-to-route）导致 fix 失败回退，浪费实施+rebuild。本次 plan 已基于亲自 trace 的源码实证，执行时若发现实测与 plan 不符，先 trace 再改。

会话开头必须：
1. 跑 MemPalace wake-up：
   - mempalace_diary_read（agent_name=zcode, last_n=8）
   - mempalace search "colregs speed envelope reachable offset" / "gnc execution odd"
2. 读取并遵守：
   - /Users/marine/.zcode/skills/colregs-probe/SKILL.md
   - /Users/marine/.zcode/cli/plugins/cache/zcode-plugins-official/superpowers/5.1.0/skills/subagent-driven-development/SKILL.md（或 executing-plans）
   - /Users/marine/.zcode/cli/plugins/cache/zcode-plugins-official/superpowers/5.1.0/skills/test-driven-development/SKILL.md
3. 读核心文档（按顺序）：
   - 诊断报告：docs/Doc From Claude/2026-06-28-colregs-speed-envelope-contract-diagnosis.md（§0.1 6 场景验证表 + §5/§6 根因）
   - spec：docs/superpowers/specs/2026-06-29-colregs-speed-envelope-complete-fix.md（6 Workstream + 4 评审决策点）
   - plan：docs/superpowers/plans/2026-06-29-colregs-speed-envelope-complete-fix.md（Phase 1-4 task-by-task）
   - handoff（重点看 2026-06-29 顶部条目 + 之前的 Class A/B arc）：handoff/workspace_log.md

工作目录：
/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug

分支：
codex/colregs-12probe-debug（已有 Class A Rule5 fix f0ebfc2e + 诊断/spec/plan docs，未 push）

==== 已确诊的 4 类缺陷（6 场景全新真实 trace，2026-06-29）====

干净 GNC stack（codex-gnc-validation，修复 GNC 容器 Exited137 OOM 后），gnc profile + restart-between-runs + sim-rate 10：

| 场景 | overall | first_failure | own 设计 | AVOID 实际 | 分类 |
|---|---|---|---|---|---|
| rule14-ho | RED | L6_seamanship | 6.0 | 6.36 | Class B |
| rule14-ho-port | RED | L6_seamanship | 6.0 | 6.35 | Class B |
| rule15-cs | RED | L6_seamanship | 10.8 | 6.58(被压) | Class B |
| rule15-cs-2 | RED | L4_colregs_compliance | 12.0 | 6.58(被压) | phase 特例 |
| rule15-cs-edge | RED | L2_safety_floor 近撞 | 5.5 | 6.18 | Class C |
| rule15-ot-boundary | RED(sim 卡死) | M6 silent | 4.3 | 11-15(拉高) | Class A |

四分类：
- Class A（ot-boundary）：mock 丢字段 + cruise_min floor 拉高 own → M6 silent。独立。
- Class B（ho/ho-port/cs）：emergency_avoidance_speed_cap 3.2 压 own → seamanship FAIL。同源。
- Class C（cs-edge）：同 B + target 高速 → 避让未完成近撞 CPA 0.2m。
- phase 特例（cs-2）：emergency cap 命中但 first_failure=L4 phase，独立。

==== 精确根因（源码实证，2026-06-29 深挖）====

速度对接已正确：M5 ship_interfaces AvoidancePlan.command_speed_mps = min(planned, 3.2)（mid_mpc_node.cpp:759-761,809），已 cap。
trace 的 wp0_target_speed_kn=10.8 是 L3 msg（avoidance_waypoint_gen，未 cap），非 GNC 收到的 command_speed_mps —— 诊断盲区，勿重复踩。

真正断裂点 = M5 横向 offset 纯几何，无 reachability 校验：
- avoidance_waypoint_gen.hpp:127 generate_stable_avoidance_corridor_waypoints
- max_lateral_offset_m 默认 kDefaultStableCorridorPeakOffsetM = 3×90 = 270m（line 37-38）
- lateral 用指数趋近 cap×(1-exp(-d/approach_distance))，纯几何，无 own 速度/加速度/TCPA 约束
- cs-edge M5 要 own 横向 400m，own 3.2m/s+0.25accel 下只到 225m → 近撞 CPA 0.2m

GNC ODD 参数（active_route_manager_node.cpp:92-99 + overlay:304-306,392,433-434）：
- emergency_avoidance_speed_cap=3.2, max_lateral_accel=0.25, max_decel=0.08
- cruise_min_speed=3.8, max_transit_speed=8.0, static_min_turn_radius=45
- 拒绝条件：turn_radius_too_small/yaw_rate_too_high/decel_distance_not_enough
- M5 allow_degraded_execution=true（mid_mpc_node.cpp:830），故 GNC 不 reject 只 degrade

==== 执行 plan（6 Workstream，4 Phase）====

Phase 1（独立低风险，先做建立信心）：
- W6 Rule13 same-course 门修正（Task 1.1，纯 M6）
- W1 mock per-scenario speed 注入（Task 1.2，mock）

Phase 2（核心）：
- W2 GNC ODD 参数作 contract msg 暴露（Task 2.1 GNC 发布 + 2.2 bridge 转发 + 2.3 M5 订阅缓存）
- W4 M5 横向 offset reachability 校验（Task 2.4 reachable_lateral_offset_m 纯函数 + 2.5 接入）

Phase 3（防护闭环）：
- W5 M5↔M6↔M7 reachability 协同（Task 3.1 plan 字段 + 3.2 M6 onset 提前 + 3.3 M7 MRM）

Phase 4（需评审决策）：
- W3 scenario 速度适配（路线 A 改 scenario ≥7.4kn vs 路线 B GNC ODD 动态化，推荐 A）

执行顺序：Phase 1（W6+W1 并行）→ Phase 2（W2→W4）→ Phase 3（W5）→ Phase 4（W3 评审）。

==== 4 个待评审决策点（spec §5）====

1. W3 路线 A vs B（推荐 A，B 需 source-backed 舵效数据）
2. W5 MRM 触发阈值（own 偏离 M5 几何多少/多久，需安全分析）
3. W2 GncExecutionOdd 发布点（active_route_manager 单点 vs ship_guidance 分散，推荐单点）
4. cs-2 phase gate 独立性（speed 修完后复测，若仍 RED 单独诊断）

==== 关键源码坐标 ====

- M5 avoidance gen：src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp:127（横向 offset，W4 核心）
- M5 plan 发布：src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:749-833
- M5 preflight cfg（GNC ODD 硬编码副本）：src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp:15-22
- M5 geometric fallback turn radius：common/types.hpp:385
- M6 Rule13：src/l3_tdl_kernel/m6_colregs_reasoner/src/rules/colregs/rule13_overtaking.cpp:32（W6）
- GNC active_route_manager ODD + 拒绝：third_party/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp:92-99,323-423
- GNC ship_guidance emergency cap：ship_guidance_node.cpp:339,4414-4424
- GNC overlay：docker/gnc-ship-config-overlay.yaml:304-306,392,433-434
- mock publisher（W1）：docker/gnc_route_mock_publisher.py:74-107
- RoutePlan msg：plugins/l2_external/ros2_ws/src/platform/ship_interfaces/msg/RoutePlan.msg（speed_limit_mps）
- AvoidancePlan msg（ship_interfaces，给 GNC）：同目录（command_speed_mps, navigation_mode）
- AvoidanceWaypoint msg（l3_msgs，给 trace/HMI）：src/l3_tdl_kernel/l3_msgs/msg/AvoidanceWaypoint.msg（target_speed_kn, turn_radius_m）

==== 探针/验证命令 ====

GNC 栈启停（task-scoped codex-gnc-validation project）：
  bash scripts/gnc-profile-start.sh --down
  bash scripts/gnc-profile-start.sh up

单场景探针（每场景 ~5-7 分钟，单后台 timeout 够；不要一次跑多场景会超 10 分钟）：
  source scripts/local-a4000-env.sh
  PROBE_STUCK_LIMIT=220 python3 scripts/run_colregs_clean_8probe.py \
    --profile gnc --scenario colreg-rule15-cs-edge \
    --restart-between-runs --restart-settle 24 --sim-rate 10 \
    --summary-out runs/<name>_$(date +%Y%m%d_%H%M%S).json \
    --trace-report-dir runs/trace_eval/$(date +%Y%m%d_%H%M%S)_<name>

  注意：intelligent 场景（ho-intelligent）会无限 sim 卡死（intelligent target 回航永不达成），跳过。
  ot-boundary 也可能 sim 卡死（own 超速回航问题），但有 raw trace 可分析。

module oracle（per-module functional test）：
  python3 scripts/run_colregs_module_oracle.py \
    --trace runs/trace_eval/<TD>/<sid>.trace_current.jsonl --scenario <sid> \
    --out runs/module_oracle_<sid>.json

容器内单测（先 source 环境）：
  docker exec codex-gnc-validation-sil-nodes-1 bash -c \
    "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
     cd /opt/ws && colcon build --packages-select <pkg> --cmake-args -DBUILD_TESTING=ON && \
     colcon test --packages-select <pkg> --event-handlers console_direct+"
  M5 测试 binary：/opt/ws/build/m5_tactical_planner/test/test_avoidance_waypoint_gen
  M6 测试 binary：/opt/ws/build/m6_colregs_reasoner/test/test_<name>

Python 单测：
  PYTHONPATH=src python3 -m pytest tests/docker/ -q

==== 执行建议 ====

推荐用 subagent-driven-development：
- 每个 Task 派 fresh subagent 执行（TDD：失败测试→实现→通过→commit）
- Task 间 review，确认无回归再下一个
- Phase 间 cohort 验证（6 场景探针）

从 Phase 1 Task 1.1（W6 Rule13）开始 —— 最独立、最低风险、纯 M6 单测，建立 TDD 节奏。
然后 Task 1.2（W1 mock），Phase 1 完成后 cohort 回归确认不破坏。
再进 Phase 2 核心（W2→W4）。

每个 Workstream 完成后：
- cohort 回归（至少跑受影响场景 + 1 个 GREEN 基线）
- handoff/workspace_log.md 追加条目
- mempalace drawer 记关键决策
- context 紧张前 mempalace_diary_write 收尾

==== 完成判据（spec §0）====

1. 6 场景 clean 8-probe overall_pass=true
2. own 实际速度跟随设计（|actual-design|<1kn）
3. M5 避让几何在 GNC ODD 下 reachable
4. cs-edge 不近撞（CPA≥floor），cs/ho seamanship 通过
5. ot-boundary M6 能 onset
6. 单测覆盖每个修复点 + counterfactual 回归锁

==== 重要警告（勿重复踩坑）====

1. trace 的 wp0_target_speed_kn 是 L3 msg（未 cap），不是 GNC 收到的 command_speed_mps（已 cap）。诊断 own 速度看 /sil/own_ship_state.sog_kn，不看 M5 plan 的 wp0_target_speed_kn。
2. emergency_avoidance_speed_cap=3.2 是 GNC 安全设计（保舵效，overlay:304 注释"tactical low-speed steering"），非 bug。是否可调需 source-backed 舵效数据。
3. reachable_lateral_offset_m 运动学模型是初始保守版，需 cs-edge 实测校准（plan Task 2.4 Step 3 注释）。
4. W4 单独对 cs-edge 可能不足（reachable offset 太小 → CPA 更差），必须配合 W5 提前 onset。W4+W5 强耦合。
5. intelligent 场景 sim 卡死是独立缺陷，out of scope。
6. seamanship 阈值不调（违反 no-threshold-tuning）。
7. ho 之前误判 GREEN（单跑部分指标），完整 verdict 是 RED（L6_seamanship）。看 verdict.json 的 overall_pass，不看 log 的部分行。

==== mempalace 关键 drawer（可检索）====

- a0c94c9a：6 场景全新 trace 验证 + 四分类
- 659a0320：Class C + GNC ODD 调研
- 982cf62a：Class B 跨场景根因
- 3a2a90e8/c4732e95/a5923544：ot-boundary 诊断

请从 Phase 1 Task 1.1（W6 Rule13 same-course 门修正）开始执行。先 wake-up + 读文档，再 TDD。
