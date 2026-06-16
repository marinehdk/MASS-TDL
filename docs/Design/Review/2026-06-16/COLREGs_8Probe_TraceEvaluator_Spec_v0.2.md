# COLREGs Clean 8-Probe TraceEvaluator Spec v0.2

| 属性 | 值 |
|---|---|
| 状态 | Draft for implementation planning |
| 日期 | 2026-06-16 |
| 代码基线 | `codex/integration-20260615` / `1472faee` |
| 适用范围 | `scenarios/COLREGs测试` clean 8-probe |
| 不适用范围 | Rule9/Rule19/multi-ship/uncooperative extended probes; M4/M5/M6 算法修复实施 |

## 1. 设计结论

本 Spec 固化 clean 8-probe 的 trace 后处理验收口径。目标不是再增加一个实时控制模块，而是建立一个独立、可审计、可复跑的 `TraceEvaluator`，输入 scenario YAML、当前 run trace、no-action baseline，输出 7 层结果、四门 verdict、阈值来源、首个失败时刻。

专家评审确认后的收敛约束：

1. CPA 阈值必须以 `N x L`、ODD profile 和公式表达。米值只作为 FCB `L=45m` 的派生结果，不允许裸魔数。
2. Rule17 / in-extremis 不再使用固定秒数作为唯一依据，必须使用动力学最迟操纵点。
3. give-way 明显动作 full 门固定为 `30 deg`。restricted partial 可保留 `15 deg`，但只能表示受限水域或边界 probe 的 partial，不得等价 full。
4. YAML 是 clean 8-probe 的当前真源。`tools/sil/gen_colreg_tier12.py` 只能视为历史生成器，除非后续重构到与 YAML 等价。
5. 当前阶段只做 8 clean，不扩展到 8+1 或 12-probe。

## 2. 设计依据

### 2.1 架构边界

TraceEvaluator 是离线验收器，不能改变 TDL 运行时职责。运行时数据链仍保持：

```text
L2 route/corridor
-> M2 World Model: target geometry, CPA/TCPA, relative bearing
-> M6 COLREGs Reasoner: rule, role, phase, constraints
-> M4 Behavior Arbiter: behavior lifecycle
-> M5 Tactical Planner: avoidance plan
-> M7 Safety Supervisor: checker/veto
-> L4 Guidance: psi/u/ROT command
-> SIL dynamics and trace
-> TraceEvaluator
```

该链路对齐 `MASS_ADAS_L3_TDL_架构设计报告.md` 的 8 模块设计：M1 为 ODD authority，M2 统一世界模型，M6 负责规则推理，M4 仲裁行为，M5 生成计划，M7 独立检查，M8 只做透明度展示。

### 2.2 COLREGs 法规边界

COLREGs / 33 CFR Rules 不给固定 CPA 米值。法规只约束：

- Rule 8: action must be positive, ample time, readily apparent, safe distance, checked until finally past and clear.
- Rule 13: overtaking duty persists; doubt means treat as overtaking.
- Rule 15: crossing give-way vessel should avoid crossing ahead if circumstances admit.
- Rule 16: give-way vessel must take early and substantial action.
- Rule 17: stand-on vessel holds course/speed first, then may or must act when give-way action is insufficient.

因此，本 Spec 的数值阈值必须标注为 project ODD / vessel-scaled / engineering profile，不得写成 COLREGs 法定距离。

## 3. Clean 8-Probe 场景范围

clean 8-probe 固定为：

| ID | Rule | 角色 | 核心断言 | CPA profile |
|---|---|---|---|---|
| `colreg-rule14-ho` | R14 | give-way | 对遇右转，port-to-port，最终回归中心航线 | `corridor_close_start` |
| `colreg-rule14-ho-port` | R14 | give-way | 目标偏左 5 deg 仍按 R14 右转，不误判 R15，最终回归中心航线 | `corridor_close_start` |
| `colreg-rule13-ot` | R13 | give-way | 追越/安全跟随；Rule13(d) 不释放 | `corridor_follow_or_overtake` |
| `colreg-rule15-cs` | R15/R16 | give-way | 右舷穿越，早、大、明显，不 cross-ahead | `open_water_crossing` |
| `colreg-rule15-cs-2` | R15/R16 | give-way | 短 TCPA 穿越，仍需及时动作 | `open_water_crossing` |
| `colreg-rule15-cs-edge` | R15 boundary | give-way | R14/R15 边界分类稳定 | `corridor_boundary` |
| `colreg-rule15-ot-boundary` | R15/R13 boundary | give-way | R15/R13 边界分类稳定 | `corridor_boundary` |
| `colreg-rule17-cr-so` | R17/R15 | stand-on | 前期保向，末段 17(b) 动力学窗口内行动 | `standon_in_extremis` |

`safe_route-left-encounter` 不属于 clean 8-probe。它应归入 L2 integration / safe-route regression，不得混入 clean 8 summary。

## 4. 阈值体系

### 4.1 基本定义

所有距离阈值以船长 `L` 为主表达。FCB 当前配置 `L=45m`，派生米值如下：

| 名称 | 公式 | FCB 派生值 | 用途 | 置信度 |
|---|---:|---:|---|---|
| `emergency_floor` | `4.0L` | `180m` | 物理/紧急红线；close-start 与 in-extremis 最低线 | Medium: project emergency floor |
| `corridor_boundary_floor` | `6.0L` | `270m` | corridor/boundary/Rule13 safe-follow acceptance | Medium-Low: project engineering profile |
| `ideal_corridor_domain` | `9.0L` | `405m` | 船艺质量线；非硬 fail | Medium: project ship-domain reference |
| `open_water_crossing_floor` | `20.0L` | `900m` | open-water R15/R16 crossing acceptance | Medium-High: project open-water baseline |

说明：

- `4L` 是 emergency/corridor close-start 最低线，不得再由 nautical-mile 常量替代。
- `6L` 是 corridor/boundary/follow-or-overtake floor，不得再用包络取整魔数替代。
- `405m=9L` 是 quality / ideal domain，不作为 clean 8 的统一 CPA hard floor。
- `20L` 是 open-water crossing probe 的 CPA floor，不得用于受限航道或 corridor boundary probe。

### 4.2 Profile 决策树

```text
if profile == open_water_crossing:
    threshold = 20L
elif profile in [corridor_boundary, corridor_follow_or_overtake]:
    threshold = 6L
elif profile in [corridor_close_start, standon_in_extremis]:
    threshold = 4L
else:
    threshold = 6L
```

### 4.3 YAML 字段要求

clean 8-probe YAML 是当前真源。每个 scenario 的 `metadata.expected_outcome.cpa_acceptance` 必须包含：

```yaml
cpa_acceptance:
  profile: corridor_boundary
  threshold_formula: "6.0L"
  threshold_m: 270.0
  loa_m: 45.0
  loa_multiplier: 6.0
  nm_equivalent: 0.146
  emergency_floor_m: 180.0
  ideal_domain_m: 405.0
  source_confidence: project_profile_medium_low
  basis: "corridor boundary probe; avoid open-water profile masking rule behavior"
```

Evaluator 必须检查 `threshold_m` 与公式派生值在容差内一致。容差建议 `<= 1m`，用于浮点与单位转换误差，不用于工程包络取整。

## 5. 动力学最迟操纵点

### 5.1 目的

Rule17 / in-extremis 不能只用 `CPA < 4L` 或固定 `TCPA < 40s` 判定。应根据 FCB 当前操纵能力、目标相对速度、系统延迟计算最迟可行动时间。

### 5.2 定义

```text
T_last_maneuver_s =
    T_system_delay_s
  + T_actuator_delay_s
  + T_heading_change_s
  + T_hydrodynamic_response_s
  + T_safety_margin_s

T_heading_change_s = required_heading_change_deg / max_effective_rot_deg_s
```

推荐初始参数只作为 profile 参数，不写死在算法中：

| 参数 | 初始值 | 来源等级 |
|---|---:|---|
| `required_heading_change_deg` | `30 deg` full / `15 deg` restricted partial | Rule8 readily apparent engineering mapping |
| `max_effective_rot_deg_s` | from vessel capability manifest or trace calibration | vessel capability |
| `T_system_delay_s` | from measured trace pipeline latency | project measured |
| `T_actuator_delay_s` | from FCB actuator/hydro model | vessel capability |
| `T_hydrodynamic_response_s` | from FCB turning response | vessel capability |
| `T_safety_margin_s` | `>= 10s` initial | project safety margin |

### 5.3 Rule17 Gate

For `colreg-rule17-cr-so`:

```text
hold_phase_pass =
    before independent-action window:
      abs(heading_deviation) <= standon_hold_limit_deg

must_act_window_open =
    predicted_cpa_m < rule17b_cpa_trigger_m
    OR give_way_non_action_detected == true

latest_action_pass =
    action_onset_tcpa_s >= T_last_maneuver_s

rule17_pass =
    hold_phase_pass
    AND independent_action_occurs_when_window_open
    AND latest_action_pass
```

`rule17b_cpa_trigger_m` 初始可用 `corridor_boundary_floor`，即 FCB 派生 `6L=270m`，但报告必须标注这是 project profile，不是法规固定值。

## 6. Trace 输入契约

TraceEvaluator 输入记录至少包含：

```text
time:
  sim_t, stamp

ownship:
  x_m, y_m, lat, lon, heading_deg, cog_deg, sog_kn, rot_deg_s

target:
  target_id, x_m, y_m, cog_deg, sog_kn

geometry:
  range_m, rel_bearing_deg, dcpa_m, tcpa_s, closing_speed_mps

route:
  cross_track_error_m, active_wp, route_progress, final_xte_m, max_route_xte_m

M6:
  rule, role, phase, conflict_detected, primary_threat_id, confidence, rationale

M4:
  behavior, avoidance_active

M5:
  plan_valid, solver_status, waypoints

M7:
  veto_state, safety_margin, critical_veto

L4:
  mode, psi_cmd_deg, u_cmd_kn, rot_cmd_deg_s
```

当前 `scripts/run_6_scenarios.py` 的 `risk_trace` 缺 `tcpa_s`、`rel_bearing_deg`、`range_m`，因此不能完成严格 post-pass 评估。实现 TraceEvaluator 前必须补 trace 字段或在 evaluator 内从 ownship/target 轨迹重算。

## 7. 七层评估器

### Layer 1: Scenario Validity

目标：证明该 probe 确实有冲突。

输入：

- YAML expected profile
- no-action baseline trace
- initial geometry

Gate：

```text
no_action_min_cpa_m < scenario_acceptance_m
no_action_tcpa_s > 0
initial_role_or_rule matches expected
ODD profile matches scenario declaration
```

如果没有 no-action baseline，Layer 1 结果为 `UNKNOWN`，不能标 `PASS`。CI 阶段应把 `UNKNOWN` 视为不可发布，但开发阶段可作为 warning。

### Layer 2: Safety Floor

目标：几何安全红线。

Gate：

```text
danger_floor_pass = min_separation_m >= emergency_floor_m
scenario_floor_pass = min_separation_m >= scenario_acceptance_m
```

输出必须区分：

- `RED`: 低于 `emergency_floor`
- `FAIL`: 高于 emergency 但低于 scenario acceptance
- `WARN`: 高于 scenario acceptance 但低于 ideal/quality domain
- `PASS`: 高于 scenario acceptance，且 quality acceptable

### Layer 3: Dynamic Risk

目标：区分接近风险与过船后近距质量。

Phase 分类：

```text
active_collision_threat =
  (tcpa_s >= 0 OR closing_speed_mps > 0)
  AND NOT past_and_clear

post_pass_clearance =
  tcpa_s < 0
  AND closing_speed_mps <= 0
  AND target_abaft == true
  AND range_increasing == true
```

Rule13 特例：

```text
if rule == Rule13:
  past_and_clear requires:
    ownship no longer constrained by overtaken vessel
    range_m > scenario_acceptance_m
    range_increasing
    relative bearing confirms finally past and clear
```

输出：

- `approach_warning_exposure_s`
- `approach_danger_exposure_s`
- `post_pass_domain_exposure_s`
- `post_pass_clearance_min_m`
- `risk_recovery_time_s`

`post_pass_domain_exposure_s` 不作为 collision threat hard fail；它进入 Layer 6 quality。

### Layer 4: COLREGs Compliance

目标：按规则评估过程，不只看最终 CPA。

通用 Rule8 / Rule16：

```text
action_onset = first time ROT >= 0.5 deg/s OR heading deviation >= 5 deg
action_tcpa_s = tcpa at action_onset
full_magnitude = peak_heading_change_in_avoidance_window >= 30 deg
restricted_partial = peak_heading_change_in_avoidance_window >= 15 deg
```

`30 deg` 是 full 门。`15 deg` 只允许：

- restricted / corridor / boundary profile 的 partial；
- 或作为 R17 independent action 已启动但幅度不足的 partial。

R14：

```text
must turn starboard
must not significant port turn
must pass port-to-port
must not be reclassified to Rule15 due to small port bias
```

R13：

```text
keep-clear duty persists until past_and_clear
relative bearing crossing beam does not release duty
safe_following is acceptable only if:
  range_m >= corridor_boundary_floor
  no hunting/chasing
  route recovery remains possible
```

R15/R16：

```text
must take early and substantial action
must avoid crossing ahead if circumstances admit
must pass astern or open CPA by starboard alteration
```

R17：

```text
hold course/speed in stand-on phase
no premature give-way
must take independent action when give-way action insufficient
action must occur before T_last_maneuver_s
```

### Layer 5: Route Recovery

目标：避碰后任务恢复。

沿用 clean 8 当前 gate：

```text
returned_to_route == true
final_xte_m < 150m
final_heading_dev_deg < 10deg
max_route_xte_m < 500m
max_route_xte_m < route_corridor_half_width_m
```

报告还应输出：

- `release_time_s`
- `route_recovery_time_s`
- `transit_after_avoidance_s`

### Layer 6: Seamanship / Efficiency

目标：解释“安全但难看”的 trace。

指标：

```text
path_length_ratio <= 1.35
integrated_abs_xte_m_s normalized by scenario duration
route_crossing_overshoot_count <= 1
post_pass_clearance_quality
no_hunting_or_chasing
no_succession_of_small_alterations
```

Layer 6 可输出 quality score。严重 hunting/chasing 可以 fail；一般 post-pass close domain 只扣分。

### Layer 7: Stability / Solver Health

复用现有 stability scorer，但阈值修订：

| KPI | 初始阈值 | 说明 |
|---|---:|---|
| `behavior_toggles` | `<=2` | 保留 |
| `plan_valid_segments` | `<=2` | 保留 |
| `steering_reversals_giveway` | `<=4` | 保留，后续校准 |
| `steering_reversals_standon` | `<=5` | 当前 clean 8 保留 |
| `role_onset_changes` | `0` | Rule13(d) |
| `min_give_way_turn_full_deg` | `30 deg` | full 门 |
| `restricted_partial_turn_deg` | `15 deg` | partial 门 |
| `premature_giveway_deg` | `<10 deg` | 当前保留，后续可校准到 8 deg |
| `rot_deadband_dps` | `[TBD-calibration]` | 需从无冲突直航 trace 校准 |
| `rot_hold_std_dps` | `[TBD-calibration]` | 需从无冲突直航 trace 校准 |

`[TBD-calibration]` 关闭路径：用 FCB 无冲突直航场景和 clean 8 stable trace 统计正常 yaw-rate 噪声后定值。阻塞度：不阻塞 v0.2 Spec，但阻塞最终认证口径。

## 8. Verdict

TraceEvaluator 输出四门：

```text
safety_pass =
  Layer2.danger_floor_pass
  AND Layer2.scenario_floor_pass
  AND NOT M7.critical_veto
  AND Layer3.approach_danger_exposure_s <= scenario_limit

mission_pass =
  Layer5.returned_to_route
  AND Layer5.route_corridor_ok

colregs_pass =
  Layer4.rule_status in [full, acceptable_partial]
  AND Layer4.role_lifecycle_ok
  AND Layer4.past_and_clear_ok

stability_pass =
  Layer7.stability_pass

overall_pass =
  safety_pass
  AND mission_pass
  AND colregs_pass
  AND stability_pass
```

报告必须同时输出：

- `first_failure_layer`
- `first_failure_t_s`
- `failure_reason`
- `trace_artifact_path`
- `threshold_provenance`

## 9. Required Report Schema

```json
{
  "scenario_id": "colreg-rule14-ho",
  "clean_8probe": true,
  "verdict": {
    "safety_pass": true,
    "mission_pass": true,
    "colregs_pass": true,
    "stability_pass": true,
    "overall_pass": true
  },
  "threshold_provenance": {
    "profile": "corridor_close_start",
    "threshold_formula": "4.0L",
    "threshold_m": 180.0,
    "loa_m": 45.0,
    "loa_multiplier": 4.0,
    "source_confidence": "project_profile_medium"
  },
  "layers": {
    "L1_scenario_validity": {},
    "L2_safety_floor": {},
    "L3_dynamic_risk": {},
    "L4_colregs_compliance": {},
    "L5_route_recovery": {},
    "L6_seamanship": {},
    "L7_stability": {}
  },
  "first_failure": null,
  "trace_artifact_path": "runs/trace_current.jsonl",
  "no_action_trace_path": "runs/no_action_trace_current.jsonl"
}
```

## 10. Current Baseline Gap

基于 `integration-20260615` 当前代码，差距如下：

| 项 | 当前状态 | Spec 要求 |
|---|---|---|
| clean 8 list | 已固定在 runner | 保留 |
| route/corridor gate | 已进入 `compute_overall_pass` | 保留并纳入 `mission_pass` |
| risk/seamanship gate | 已有基础整体 gate | 拆为 Layer3/Layer6 |
| no-action baseline | 缺失 | 新增 Layer1 |
| threshold provenance | YAML 有基础 profile | 补 `N x L`、formula、confidence |
| Rule8 timing | 缺失 | 新增 |
| Rule8 magnitude | 5 deg stability 门过低 | full 30 deg / restricted partial 15 deg |
| Rule15 cross-ahead | 缺失 | 新增 |
| Rule17 dynamic latest maneuver | 缺失 | 新增 |
| post-pass phase | 缺失 | 新增 |
| generator truth | generator 与 YAML 不一致 | YAML 为真源 |
| script naming | `run_6_scenarios.py` 过时 | 后续改名，不阻塞本 Spec |

## 11. Implementation Acceptance Criteria

本 Spec 的实现完成条件：

1. 每个 clean 8 scenario 生成一份 `TraceEvaluationReport`。
2. 报告包含 7 层、四门 verdict、threshold provenance、首个失败时刻。
3. no-action baseline 可运行；无法运行时 Layer1 为 `UNKNOWN`，CI 不得标全绿。
4. CPA floor 不再以裸米值出现于 evaluator 决策逻辑，只能由 `4L`、`6L`、`9L`、`20L` profile 公式派生。
5. Rule17 使用 `T_last_maneuver_s`，报告中输出参与计算的各项参数。
6. `min_give_way_turn_full_deg=30`；`restricted_partial_turn_deg=15` 只能产生 partial。
7. `safe_route-left-encounter` 不进入 clean 8 summary。

## 12. Open Calibration Items

以下项保留为校准项，不影响 v0.2 进入实现计划：

| 项 | 关闭路径 | 阻塞 |
|---|---|---|
| `rot_deadband_dps` | 无冲突直航 trace 统计 | 不阻塞 P0 |
| `rot_hold_std_dps` | 无冲突直航 + clean 8 stable trace 统计 | 不阻塞 P0 |
| `max_effective_rot_deg_s` | FCB hydro/capability manifest 或 trace 反推 | 阻塞最终 Rule17 动力学定值 |
| `T_system_delay_s` | trace 中 M6->M4->M5->L4 延迟统计 | 阻塞最终 Rule17 动力学定值 |
| `T_hydrodynamic_response_s` | FCB turning response 标定 | 阻塞最终 Rule17 动力学定值 |

## 13. Non-Goals

- 不在本 Spec 中实现 M4 `RETURN_TO_ROUTE`。
- 不在本 Spec 中实现 M5 fallback 三段回归航线。
- 不在本 Spec 中替换 M6 release 逻辑。
- 不扩展 Rule9/Rule19/multi-ship/uncooperative probes。
- 不把 open-water crossing profile 强行应用到受限航道。
