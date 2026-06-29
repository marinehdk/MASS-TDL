# 新对话提示词：继续 COLREGs Speed-Envelope Contract 修复（W4 数据分析 → 模型 → 接入）

将以下内容作为新对话的首条用户消息粘贴。

---

继续 MASS-L3 TDL COLREGs speed-envelope contract 修复。Phase 1（W6+W1）+ Phase 2 W2 已完成并提交。本次进入 W4 数据分析（数据驱动定模型，不独断）。

默认要求：
- caveman full 中文，短句，信息密度高。
- 严格遵守 colregs-probe + systematic-debugging + test-driven-development + writing-plans 工作流。
- 不为单场景调参，不改 scorer/阈值/skip/mock/forced PASS，不加 scenario-id/vessel-specific 分支。
- COLREGs 缺陷按完整链路排查：L2 → M2 → M6 → M4 → M5 → L4/GNC → M7 → M8。
- 每个 Workstream 独立 commit，cohort 回归。
- Iron Law：跨模块集成修复必须亲自 trace 验证源码+数据，不依赖单次归因或 plan 草稿。本会话已多次证明 plan 草稿与源码/实测不符（见下"plan 草稿错误清单"），执行前必 trace。

会话开头必须：
1. 跑 MemPalace wake-up：
   - mempalace_diary_read（agent_name=zcode, last_n=10）
   - mempalace search "colregs speed envelope w2 w4" / "cs-edge own lateral 400m"
2. 读取并遵守 skills：
   - /Users/marine/.zcode/skills/colregs-probe/SKILL.md
   - subagent-driven-development / test-driven-development / systematic-debugging（superpowers 5.1.0）
3. 读核心文档（按顺序）：
   - handoff/workspace_log.md（顶部 2026-06-29 两个条目：Phase1 W6+W1 + Phase2 W2+W4发现）
   - 诊断报告：docs/Doc From Claude/2026-06-28-colregs-speed-envelope-contract-diagnosis.md（§0.1 表 + §5/§6 根因，注意：诊断"own 225m"已被推翻，见下 W4 发现）
   - spec：docs/superpowers/specs/2026-06-29-colregs-speed-envelope-complete-fix.md（6 Workstream，决策 #3 已定方案 a）
   - plan：docs/superpowers/plans/2026-06-29-colregs-speed-envelope-complete-fix.md（Phase 2 Task 2.4/2.5 是本次焦点，但 plan 运动学模型草稿有内在矛盾，需数据重定）

工作目录：
/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug

分支：
codex/colregs-12probe-debug

==== 已完成的提交（4 个，未 push）====

| commit | 内容 | 验证 |
|---|---|---|
| 4aefbce6 | W6 Rule13 移除 same-course 门（NLM high 确证 13(a) 不要求） | m6 全 21 binaries green |
| 5c67300c | W1 mock 填 RoutePlan.speed_limit_mps | live echo 铁证 [2.21, 2.21] |
| 668c8799 | W2 GncExecutionOdd.msg + arm 发布 + bridge 转发 + M5 订阅 | live echo domain42 完整 ODD |
| 5d65e7d0 | docs/handoff（诊断+spec+plan+handoff 全产物） | — |

==== 已完成的修复（验证通过）====

**W6（commit 4aefbce6）**：rule13_overtaking.cpp 移除 kSameCourseMaxDeg=45 硬门。COLREGs Rule 13(a) 不要求 same-course（NLM maritime_regulations 🟢 high）。course diff 改 rationale-only。TDD：2 新测试（60° course-diff overtaking + counterfactual）。

**W1（commit 5c67300c）**：gnc_route_mock_publisher._load 保留 target_sog_kn，_on_timer 填 speed_limit_mps（m/s）。ot-boundary 探针：own 起点 SOG 7.7kn=cruise_min floor（W1 生效），但中段飙 15kn（GNC 消费层独立行为，属 W3 scope）。trace 字段：own SOG 在 /sil/own_ship_state record 顶层（sog_kn），非 state 内。

**W2（commit 668c8799）**：GNC 执行 ODD 作 latched contract msg 暴露给 TDL。决策 #3 方案 a：active_route_manager 单点发布 + 重复 declare 速度参数（两节点独立进程，sim_launch.py:144 ship_guidance / :173 active_route_manager，参数不共享）。
- ship_interfaces/GncExecutionOdd.msg（7 字段：emergency_avoidance_speed_cap_mps/cruise_min_speed_mps/max_transit_speed_mps/max_lateral_accel_mps2/max_decel_mps2/emergency_min_turn_radius_m/emergency_max_yaw_rate_deg_s + schema_version）
- active_route_manager_node.cpp 发布 /gnc/execution_odd（transient_local），publish_execution_odd() 初始发布
- gnc_bridge 跨 domain 50→42 转发（GncToL3 handoff 加 execution_odd + GncSideNode 订阅 + L3PublisherNode 转发）
- M5 mid_mpc_node 订阅 + 缓存 latest_gnc_odd_ + effective_gnc_odd_() fallback（gnc_avoidance_preflight.hpp 默认值）
- live echo 铁证（domain 42）：emergency_cap=3.2, cruise_min=3.8, max_transit_speed=3.0, lateral_accel=0.25, decel=0.08, turn_radius=45, yaw_rate=2.0, schema=1.0
- msg 注册两份 ship_interfaces：third_party/gnc_ws/src/platform/ship_interfaces/（GNC build）+ src/ship_interfaces/（sil-nodes build）。第三份 plugins/l2_external 当前 gnc profile 用不到，留 TODO。

==== 待评审决策点（spec §5）====

1. ~~W2 发布点~~ → 已定方案 a（arm 单点 + 重复 declare）
2. W3 路线 A（改 scenario ≥7.4kn）vs B（GNC ODD 动态化），推荐 A
3. W5 MRM 触发阈值（需安全分析）
4. ~~reachable 运动学模型~~ → **本次焦点，需 6 场景数据驱动**

==== 本次焦点：W4 reachable_lateral_offset 模型（数据驱动）====

**不要直接按 plan Task 2.4 的 accel+cruise 模型草稿实现**——本会话已证明它有内在矛盾。

W4 TDD RED 已验证（reachable_lateral_offset_m 未定义编译失败），但 trace 发现：
1. plan accel+cruise 模型代入 cs-edge（0.5×0.25×480²=28800m）→ 不 cap（W4 对 cs-edge 无效）
2. plan 自己的 test 预期（短 TCPA 60s <50m）与实现草稿（94.7m）不一致
3. turn-radius 模型 r(1-cosθ) 给 45m 过保守，且与"长 TCPA 回 geometric"test 语义冲突

**关键数据发现（推翻诊断"own 225m"假设）**：
cs-edge fresh trace（runs/trace_eval/20260629_000517_cs_edge_single）：
- own 起点 lat=63.882（非 63.44）
- own 横向位移（east of start）：t=17s→173m（fast onset）、t=276s→295m、t=631s→380m、t=844s→397m、t=985s→402m（peak）
- M5 wp0 横向要求（/l3/m5/avoidance_plan 的 wp0_lat/lon）：t=242s→280m、t=476s→357m、t=725s→566m、t=984s→619m
- **own 实际能横移 ~400m（非诊断 225m 单时点误读）**。near-collision 根因不是"own 物理跟不上"，是 **M5 offset 几何增长快于 own 跟随**（M5 619m vs own 402m @ t=985s）+ CPA 最低点可能对应最大 gap。

**本次任务（用户要求）**：完整分析 rule14+15 场景簇 6 场景数据，再决定 W4 模型：
1. 对 6 场景（ho/ho-port/cs/cs-2/cs-edge/ot-boundary）提取：
   - own 横向位移 vs sim_t（从 /sil/own_ship_state lat/lon 投影到 route 法向）
   - M5 wp0 横向要求 vs sim_t（/l3/m5/avoidance_plan wp0_lat/lon）
   - CPA vs sim_t（从 /sil/scoring 或 /l3/m2/world_state）
   - own SOG vs sim_t
2. 对比"own 实际横向 vs M5 要求横向"的 gap，看 CPA 最低点对应多大 gap
3. 从数据归纳：own 横向跟随能力的真实特征（是 lag、是增长速率上限、还是 onset 后 plateau？）
4. 基于数据特征定 W4 模型方向（可能是：cap M5 offset 增长率而非峰值；或 onset 提前；或别的）
5. 定模型后 TDD 实现 reachable_lateral_offset_m + 接入 generate_stable_avoidance_corridor_waypoints（Task 2.5）+ cs-edge 探针校准

**注意**：W4 RED 测试已回滚（git checkout），分支可编译。m5 test 在 test/unit/（非 plan 写的 test/）。avoidance_waypoint_gen.hpp namespace=mass_l3::m5，header-only 纯函数。

==== plan 草稿错误清单（执行前必 trace，勿照抄）====

本会话验证 plan 草稿与源码/实测不符处：
1. plan test 代码 `Rule13Overtaking rule` → 实际 `Rule13_Overtaking`（带下划线），且假设 test 文件不存在（实际已有 9 测试）
2. plan Task 2.4 test 路径写 `test/` → 实际 `test/unit/`
3. plan 评审点 #3 假设参数在同一节点 → 实际两节点独立进程，参数不共享
4. plan Task 2.4 accel+cruise 模型 cs-edge 不收缩（W4 无效）
5. plan Task 2.4 自己的 test 预期与实现草稿不一致（<50 vs 94.7m）
6. 诊断报告"own 225m"是单时点读，实际 own 横移 ~400m

==== 4 类缺陷（6 场景，2026-06-29 fresh trace）====

| 场景 | overall | first_failure | own 设计 | AVOID 实际 | 分类 |
|---|---|---|---|---|---|
| rule14-ho | RED | L6_seamanship | 6.0 | 6.36 | Class B |
| rule14-ho-port | RED | L6_seamanship | 6.0 | 6.35 | Class B |
| rule15-cs | RED | L6_seamanship | 10.8 | 6.58(被压) | Class B |
| rule15-cs-2 | RED | L4_colregs_compliance | 12.0 | 6.58(被压) | phase 特例 |
| rule15-cs-edge | RED | L2_safety_floor 近撞 | 5.5 | 6.18 | Class C |
| rule15-ot-boundary | RED(sim 卡死) | M6 silent | 4.3 | 11-15(拉高) | Class A |

- Class A（ot-boundary）：mock 丢字段（W1 已修）+ cruise_min floor 拉高 own。独立。W1 后 own 起点 7.7kn=floor 但中段飙 15kn，需 W3。
- Class B（ho/ho-port/cs）：emergency_avoidance_speed_cap 3.2 压 own → seamanship FAIL。同源。
- Class C（cs-edge）：同 B + M5 offset 增长远超 own 跟随 → 近撞。W4 焦点。
- phase 特例（cs-2）：emergency cap 命中但 first_failure=L4 phase。

==== 关键源码坐标 ====

- M5 avoidance gen：src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp:127（generate_stable_avoidance_corridor_waypoints，W4 接入点）
- M5 plan 发布：src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp（effective_gnc_odd_ 已加，W4 在此传 ODD 给 gen）
- M5 preflight cfg：src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp:15-26（fallback 默认值来源）
- M5 测试：src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp（W4 测试加这里）
- GNC ODD 发布：third_party/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp（publish_execution_odd，已加）
- GNC ship_guidance emergency cap：third_party/gnc_ws/src/gnc/ship_guidance/src/ship_guidance_node.cpp:339,4414-4424（emergency_avoidance_speed_cap 3.2 实际执行处）
- GNC overlay：docker/gnc-ship-config-overlay.yaml
- msg 两份：third_party/gnc_ws/src/platform/ship_interfaces/msg/ + src/ship_interfaces/msg/（GncExecutionOdd.msg 同步）

==== 探针/验证命令 ====

GNC 栈启停（task-scoped codex-gnc-validation project）：
  bash scripts/gnc-profile-start.sh --down
  bash scripts/gnc-profile-start.sh up

注意：改 GNC 源码（active_route_manager 等）需 rebuild gnc 镜像：
  docker compose -f docker-compose.yml -f docker-compose.gnc.yml --profile gnc --project-name codex-gnc-validation build gnc-nodes
改 sil-nodes 源码（m5/bridge/ship_interfaces）在容器内 colcon build（src/ 挂载 /opt/ws/src）。

单场景探针（每场景 ~5-7 分钟）：
  source scripts/local-a4000-env.sh
  PROBE_STUCK_LIMIT=220 python3 scripts/run_colregs_clean_8probe.py \
    --profile gnc --scenario colreg-rule15-cs-edge \
    --restart-between-runs --restart-settle 24 --sim-rate 10 \
    --summary-out runs/<name>_$(date +%Y%m%d_%H%M%S).json \
    --trace-report-dir runs/trace_eval/$(date +%Y%m%d_%H%M%S)_<name>

  注意：intelligent 场景会无限 sim 卡死，跳过。ot-boundary 也可能卡死但有 raw trace。

live echo 验证 ODD topic：
  docker exec codex-gnc-validation-sil-nodes-1 bash -c \
    "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
     export ROS_DOMAIN_ID=42 && ros2 topic echo /gnc/execution_odd --once"

容器内单测：
  docker exec codex-gnc-validation-sil-nodes-1 bash -c \
    "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
     cd /opt/ws && colcon build --packages-select <pkg> --cmake-args -DBUILD_TESTING=ON && \
     colcon test --packages-select <pkg> --event-handlers console_direct+"

==== trace 字段路径（关键，勿踩）====

- own_ship_state 字段在 record 顶层：sog_kn, lat, lon, heading_deg, rot_deg_s, sim_t（无 xte_m，横向位移需从 lat/lon 算）
- /l3/m5/avoidance_plan 有 wp0_lat/wp0_lon/wp0_target_speed_kn/wp0_turn_radius_m/n_waypoints/plan_status/solver_status（M5 几何要求）
- /l2/planned_route 只存 route_hash（slim trace）
- GNC domain 50 topics（/route_planning/gnc_route_plan, /ship/geo_position）不在 trace（trace 只录 domain 42）
- wp0_target_speed_kn 是 L3 msg（未 cap），非 GNC 收到的 command_speed_mps（已 cap 3.2）。诊断 own 速度看 /sil/own_ship_state.sog_kn

==== 6 场景 fresh trace 路径（W4 数据分析用）====

- rule14-ho: runs/trace_eval/20260628_212718_ho_single/
- rule14-ho-port: runs/trace_eval/20260628_213255_rule14_cohort_fresh2/
- rule15-cs: runs/trace_eval/20260628_233839_cs_single/
- rule15-cs-2: runs/trace_eval/20260628_234938_cs2_single/
- rule15-cs-edge: runs/trace_eval/20260629_000517_cs_edge_single/
- rule15-ot-boundary: runs/trace_eval/20260629_001256_ot_boundary_single/

==== 完成判据（spec §0）====

1. 6 场景 clean 8-probe overall_pass=true
2. own 实际速度跟随设计（|actual-design|<1kn）
3. M5 避让几何在 GNC ODD 下 reachable（W4 本次）
4. cs-edge 不近撞（CPA≥floor），cs/ho seamanship 通过
5. ot-boundary M6 能 onset（W3）
6. 单测覆盖每个修复点 + counterfactual 回归锁

==== 重要警告（勿重复踩坑）====

1. trace 的 wp0_target_speed_kn 是 L3 msg（未 cap），不是 GNC 收到的 command_speed_mps。诊断 own 速度看 /sil/own_ship_state.sog_kn。
2. emergency_avoidance_speed_cap=3.2 是 GNC 安全设计（保舵效），非 bug。是否可调需 source-backed 舵效数据。
3. 诊断"own 225m"已推翻——实际 own 横移 ~400m。W4 不是"own 物理跟不上"，是"M5 offset 增长远超 own 跟随"。
4. plan Task 2.4 运动学模型草稿有内在矛盾，勿照抄。先做 6 场景数据分析。
5. intelligent 场景 sim 卡死是独立缺陷，out of scope。seamanship 阈值不调。
6. 改 GNC 源码要 rebuild gnc 镜像；改 sil-nodes 源码容器内 colcon build。
7. ship_interfaces 有两份（gnc_ws + src），msg 改动要同步。

==== mempalace 关键 drawer（可检索）====

- 53b74b5a：W6 Rule13 + Iron Law
- d8f102ee：W1 mock + 消费链
- 06436be5：W1 ot-boundary probe（own 中段飙 15kn）
- 56253fcf：W2 决策 #3（两节点独立进程）
- 22bf87b3：W4 cs-edge 数据（推翻 225m，own 实际 ~400m）

请先完整分析 6 场景数据（own 横向 + M5 wp0 横向 + CPA + SOG vs 时间），归纳 own 横向跟随特征，再定 W4 模型方向。数据驱动，不独断运动学公式。
