# ho RED 真实执行链诊断(F2)

> **Status:** COMPLETE — F2 诊断产出,read-only。供 memo v3 reviewer + 后续 ho RED 修复 PoR 使用。
> **产出**: 2026-07-19
> **作者**: `tdl_m5_planner_engineer` (subagent) → ZCode TDL Lead 落地成文档
> **范围**: ho RED 真实修复路径诊断。**acatos 不是修复路径**(ZCode probe 已证 ho 上 acatos 先天失败)。
> **前置证据**:
> 1. `docs/superpowers/specs/2026-07-19-m5-acatos-ho-dynamic-convergence-probe.md`(ZCode acatos 短路实验)
> 2. `runs/gate2_ho_gnc/colreg-rule14-ho.trace_current.jsonl`(baseline ho trace,sim_t 0-989,profile=gnc)
> 3. `runs/acatos_ho_probe/`(acatos 短路实验 trace + summary)

## 0. TL;DR

| 项 | 结论 |
|---|---|
| **先前理解的 ho RED 根因链** | NLP 失败 → TailGate 拒 → GeoFallback turn_r=95m → L4 GNCPreflight 拒 |
| **F2 实测校正** | NLP 失败 → **TailGate 根本没跑**(`solver_failed=true` 在 `mid_mpc_node.cpp:930` 短路) → GeoFallback arc 被 corridor waypoint 覆盖(commit_branch=2 CORRIDOR)→ corridor 进 L4 preflight 拒 → empty heartbeat → **BC-MPC Override 触发 2118 次但 `gnc_bridge` 不订阅 → 零效果**(架构 gap)→ own 不动 → RED |
| **关键架构 gap** | **`gnc_bridge_node.cpp` 不订阅 `/l3/m5/reactive_override_cmd`**,只在 `fcb_simulator_node`(profile=fcb)订阅。profile=gnc 下 BC-MPC Override 是 dead letter。这是 ho RED 的「last line of defense falls」点。 |
| **acatos 角色** | ho DCPA=0 + dead-ahead 是 acatos 先天不可解场景(ZCode probe 11/11 status=3)。**ho RED 修复不能依赖 acatos**,必须靠 IPOPT/corridor/BC-MPC/L4 链。 |
| **最小修复 PoR** | F-A + F-H + F-C + F-D = 3.2d 工程 + 2-3d 验证 |

## 1. 真实失败链(实测,非推测)

### 1.1 时间线

```
sim_t=291     M6 conflict=True → cpa_safe bumped 1852→2500
sim_t=291-385 Mid-MPC NLP 全部 NONCONVERGED(IPOPT 7 cycle / acatos 11 cycle)
              → solver_failed=true → TailGate 短路(mid_mpc_node.cpp:930)→ 走 GeoFallback
sim_t≈385     GeoFallback arc(turn_r=95m, tgt_psi=30-53°)生成 10 wp
              → publish_committed_route_ 用 corridor waypoint 覆盖(commit_branch=2 CORRIDOR, 12 wp)
              → GeoFallback arc 从未到 L4
sim_t=425     own 298m past corridor anchor → corridor preflight borderline PASS → VALID 12-wp committed route
sim_t=491+    own 513m+ past frozen wp[1] → 全 route preflight 拒 first_turn_radius_too_small
              (required=50.5m hardcoded 3.5°/s,available=25.6m 3-point 曲率随 own 推进单调降)
              → publish_keep_last_("full_preflight_failed") empty heartbeat
              → L4 reject invalid_avoidance_route → GNC keep stale route
              → route_planner DEFERRED with avoidance_active 1132 cycles
sim_t=836     BC-MPC Override 触发(best_cpa < cpa_safe × 0.8 = 2000m)
sim_t=836-988 BC-MPC 发了 2118 次 reactive_override event(CONDITION_A ×244 + CONDITION_A_DECEL ×1874)
              → /l3/m5/reactive_override_cmd 发了 2118 次
              → gnc_bridge 不订阅 → 零效果(架构 gap)
sim_t=983     CPA floor breached,Min DCPA=2.36m → RED
```

### 1.2 实测证据(PROJECT_FACT)

- **NLP 失败**:baseline trace 7 个 avoidance_plan 全部 `nlp_solver_status=1 (NLP_NONCONVERGED)`;acatos 短路 11/11 `status=3 NumericalFailure, sqp_iter=1`
- **TailGate 短路**:`mid_mpc_node.cpp:930` `solver_failed ? ... : accept_tail_gate(...)` — solver_failed=true 时整个 TailGate branch skip
- **GeoFallback 覆盖**:`publish_committed_route_` 用 `commit_branch=2 CORRIDOR` 的 12 个 corridor waypoint;GeoFallback 的 10 个 arc waypoint 不进 L4
- **L4 preflight 数值**:`required=50.5m` 来自 `gnc_avoidance_preflight.hpp:24-40` hardcoded `emergency_max_yaw_rate_deg_s=3.5`;`ship_config.yaml:632-641` 是 `max_yaw_rate_deg_s: 4.7`。`available=25.6m` 是 own 越过 frozen wp[1] 后 3-point 曲率
- **BC-MPC Override dead letter**:`src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp:13-61` 不订阅 `/l3/m5/reactive_override_cmd`;只 `src/sim_workbench/fcb_simulator/src/fcb_simulator_node.cpp:136-172` 订阅,且只在 profile=fcb 跑

## 2. Q1-Q5 详解(逐项回答 brief)

### Q1: IPOPT/TailGate 行为

IPOPT 在 ho 7 个 cycle 全部 `NLP_NONCONVERGED`。**TailGate 根本没运行**(`solver_failed=true` 在 `mid_mpc_node.cpp:930` 短路)。trace 里 `nlp_tail_gate_failed=True` 是 default 值,不是真实 reject。**修 TailGate 阈值对 ho RED 是 no-op**。

### Q2: GeoFallback 95m / 53.2° 怎么算的

- `turn_r = max(u / (4.7°·π/180), 50) = max(7.78/0.0821, 50) = 94.7m ≈ 95m`(u=6kn=3.09m/s,wait,实际 own sog=15.1kn=7.78m/s,用 cruise_max_yaw_rate=4.7°/s)
- `tgt_psi` = M6 强制 30° min alteration(warning entry 前),warning 后 h_max=53.2°
- **两者都是 GNC ODD + M6 约束的几何产物,不是激进度旋钮**
- GeoFallback 设计意图:DEGRADED 兜底,confidence=0.6
- GeoFallback 的 10 个 arc wp 在 `publish_committed_route_` 被 corridor wp 覆盖,**从未到 L4**。95m log 行是 red herring。

### Q3: L4 GNCPreflight `required=50.5 / available=25.6` 的含义

- **required=50.5m** 来自 `required_turn_radius_m(first_speed=3.086m/s=6kn, cfg)`,`emergency_max_yaw_rate_deg_s=3.5` hardcoded → `yaw_required = v / (3.5°·π/180) = 50.5m`。**不是** GeoFallback 的 turn_r。
- **available=25.6m** 是 3-point 局部曲率 `origin → wp[1] → wp[2]`,origin 是 current own position,wp[1]/wp[2] 是 frozen corridor wps。own 越过 wp[1] 时角度变尖 → available 单调降(实测从 35.6 → 25.3m)
- **L4 reject 行为**:M5 publish empty heartbeat → L4 reject `invalid_avoidance_route` → GNC keep stale route → route_planner DEFERRED

### Q4: BC-MPC Override 为什么没救 ho

- **触发**:sim_t=836 BC-MPC Override 触发(`best_cpa < cpa_safe × 0.8 = 2000m`;建模预测触发 sim_t≈726,实测 836 因 event-driven `on_world_state_` cadence)
- **2118 event** sim_t 836-988(CONDITION_A ×244 + CONDITION_A_DECEL ×1874)
- **13-branch uniform ±60°/10° span,无显式 port/starboard COLREGs 偏好,无 ROT command(rot_cmd_deg_s=0),validity_s=1.0**
- **profile=gnc 下零效果**:`gnc_bridge_node.cpp` 不订阅 `/l3/m5/reactive_override_cmd`。只 `fcb_simulator_node` 订阅(profile=fcb)。**这是真正的 last-line-of-defense 失效点,且是架构 gap 不是参数问题。**

### Q5: 修复候选路径

| ID | 候选 | Surface | 风险 | 工程量 | 独立 reviewer |
|---|---|---|---|---|---|
| **F-A** | own 越过 wp[1]+wp[2] 中点时 skip `first_turn_radius` 检查,或返回 inf available;从 along-track projection 动态选 first_idx | `gnc_avoidance_preflight.hpp:294-301` | Yellow(preflight 契约改) | 0.5d | GNC contract + M7 |
| **F-H** | preflight cfg 用 `effective_gnc_odd_()` live values(4.7°/s → required=v/yaw≈37m)替换 hardcoded 3.5°/s | `mid_mpc_node.cpp:1760, 1940` + `gnc_avoidance_preflight.hpp:24-40` | Green(本地) | 0.3d | GNC |
| **F-B** | own 移动 > N·seg_len 时 roll corridor anchor;暴露 stale-anchor metric | `mid_mpc_node.cpp:1681-1695` `need_new_anchor` | Yellow(committed-route hash 稳定性) | 1d | Committed-route + GNC |
| **F-C** | 让 `gnc_bridge` 订阅 `/l3/m5/reactive_override_cmd` → 翻译成 GNC domain command(新 `ship_interfaces` msg 或扩展 RouteExecutionStatus override field);发 ASDR `decision_type=gnc_override_translated` | `gnc_bridge_node.cpp` + `translators.hpp/.cpp` + `ship_interfaces` msg | Red(L3↔L4 契约 + M7 audit 链) | 2-3d | TDL Lead → `tdl_gnc_contract_reviewer` + M7 |
| **F-D** | `override_cpa_multiplier` 0.8→0.5 提前触发(post F-C only,否则无 consumer) | `bc_mpc_branch_formulation.hpp:45` | Green(本地) | 0.1d | COLREGs ample-time |
| **F-E** | `ReactiveOverrideCmd` 加 ROT command + Eriksen 2019 branching-course 显式 port/starboard 偏好(替换 uniform ±k/2·delta_psi span) | `bc_mpc_branch_formulation.cpp:24-48` + `bc_mpc_override_generator.cpp` + `ReactiveOverrideCmd.msg` | Yellow(ROS2 msg schema) | 2d | COLREGs + msg schema |
| **F-F** | NLP 收敛(IPOPT option tuning / warm-start seeding / acatos dual-track on non-ho) | `mid_mpc_solver.cpp` + `mid_mpc_acados_formulation.cpp` | Red(NLP safety-critical) | 5-10d | NLP + GNC + COLREGs |
| F-G | 放松 ho scenario(DCPA≠0) | `scenarios/COLREGs测试/colreg-rule14-ho.yaml` | Red — **rejected**(违反 AGENTS.md COLREGs full-chain rule) | — | — |

### 推荐顺序

1. **F-A + F-H**(0.8d):unblock corridor preflight on ho without touching solver。验证:baseline trace replay + 新 SIL run
2. **F-C**(2-3d):wire reactive_override into GNC bridge。**这是真正的 last-line-of-defense 修复,唯一不能跳过的架构改动**。必须 TDL Lead 路由到 `tdl_gnc_contract_reviewer`
3. **F-D**(0.1d,post F-C):override 有 consumer 后才有意义
4. **F-B + F-E**(3d):anchor roll + branching-course 质量提升;长期稳定窗口
5. **F-F**(5-10d):NLP 收敛。即便 1-4 都做,NLP 不收敛意味着 corridor fallback 是稳态路径,不能假设 OPTIMIZED branch 跑。先 profile IPOPT options 再决定 dual-track acatos-on-non-ho

**最小可验证 PoR 翻 ho RED → GREEN**:F-A + F-H + F-C + F-D = 3.2d 工程 + 2-3d 验证,加 ASDR audit on F-C translator。

## 3. acatos 角色(必须明示)

acatos 在 ho 上先天失败(ZCode independent probe:11/11 status=3 NumericalFailure, sqp_iter=1, cpa_slack=1e-19,on ho DCPA=0 + dead-ahead geometry)。**ho RED 的修复不能依赖 acatos**。必须靠 IPOPT/corridor/BC-MPC/L4 链:F-A(corridor preflight frame fix)+ F-C(Override consumer)+ F-D(override cadence)+ F-F(IPOPT convergence, dual-track with acatos only on non-ho)。

任何 acatos dispatch-gate 或 option-tuning 修复对 ho RED 都不够,因为 corridor preflight 和 Override consumer gap 即便 acatos 收敛也会导致 RED。

## 4. Remaining risks / 开放问题

1. acatos "congenital failure" 结论是 ZCode 的;F2 没独立验证 HPIPM Jacobian condition-number 假设。建议 F5 follow-up。
2. BC-MPC `predicted_short_horizon_cpa_m` 读 M2 linear CPA 不是 trajectory CPA,所以 ample-time 期间 urgency 饱和到 1。ho trace 没看到 false trigger,但 crossing give-way 长 ample-time 可能 misfire。需 COLREGs ample-time reviewer 确认。
3. F-C `validity_s=1.0` 可能对 GNC `active_route_manager` route-switch guards(tens-of-second scale)太短。HAZID needed before commit。
4. IPOPT per-cycle status(NONCONVERGED vs timeout)在 trace 里不直接暴露;F-F 工程量估计假设先 option profiling。

## 5. 独立 review 状态

F2 是 read-only 任务,未派子 reviewer(no-chain rule)。推荐路由(由 TDL Lead 派):
- F-C → `tdl_gnc_contract_reviewer`(L3↔L4 契约改动)
- COLREGs ample-time / direction semantics → `tdl_colregs_m6_reasoner`
- F-A / F-B / F-H → GNC contract reviewer
- F-F → NLP solver reviewer + COLREGs full-chain reviewer
