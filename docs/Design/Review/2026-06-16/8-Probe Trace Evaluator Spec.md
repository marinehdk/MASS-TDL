下面是可给专家复核的 **Spec 草案**。未写入文件，未改仓库。

# COLREGs Clean 8-Probe Trace Evaluation Spec v0.1

Status: Draft for expert review
Date: 2026-06-16
Worktree baseline: `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/integration-20260615`
Scope: 8-probe 场景、trace 采集、7层评估、CPA 阈值来源解释
Non-scope: 暂不改算法、暂不改测试代码、暂不写实现计划

## 1. 核心问题

当前 8-probe 能用旧口径通过，但前端 replay 显示仍有绕行、追逐、回归失败、风险阶段误判等问题。根因之一：评估器把“几何 CPA”“动态接近风险”“过船后近距”“COLREG 行为”“航线回归”混在一个 pass/fail 里，导致测试指标和真实场景感知不一致。

Spec 目标：用 trace 过程客观评估仿真，而不是只看最终 summary。

## 2. 基本原则

1. COLREGs 不给固定 CPA 数值。Rule 7/8/6 只要求风险判断、安全速度、及时明显动作、安全距离、直到 past-and-clear 持续检查。官方来源：Rule 7 / Rule 8 / Rule 6。
   来源：[33 CFR §83.07](https://www.ecfr.gov/current/title-33/chapter-I/subchapter-E/part-83/subpart-B/subject-group-ECFRc711a0393c57020/section-83.07)、[33 CFR §83.08](https://www.ecfr.gov/current/title-33/chapter-I/subchapter-E/part-83/subpart-B/subject-group-ECFRc711a0393c57020/section-83.08)、[33 CFR §83.06](https://www.ecfr.gov/current/title-33/chapter-I/subchapter-E/part-83/subpart-B/subject-group-ECFRc711a0393c57020/section-83.06)
2. CPA floor 是几何红线，不等于完整风险 gate。
3. 动态风险必须区分：approach risk、encounter risk、post-pass clearance。
4. Heading-on 里目标船已在身后、TCPA < 0、远离、满足 past-and-clear 后，不应继续按“正在发展的碰撞威胁”处罚；只能计入 post-pass clearance 质量。
5. Rule 13 例外：追越义务持续到 finally past and clear，不能只因相对方位变化或 TCPA < 0 自动释放。
6. 所有阈值必须来源可解释：法规、项目 ODD、船域模型、场景画像、工程折中，不能裸写魔数。

## 3. 8-Probe 场景集

当前 clean 8-probe 场景应固定为：

| ID                          | Rule             | 角色     | 核心行为                      |
| --------------------------- | ---------------- | -------- | ----------------------------- |
| `colreg-rule14-ho`          | R14              | give-way | 对遇右转，回归航线            |
| `colreg-rule14-ho-port`     | R14              | give-way | 目标偏左仍右转，不误判穿越    |
| `colreg-rule13-ot`          | R13              | give-way | 追越/安全跟随，不能掉头追逐   |
| `colreg-rule15-cs`          | R15/R16          | give-way | 右舷来船让路                  |
| `colreg-rule15-cs-2`        | R15/R16          | give-way | 短 TCPA 右舷穿越，早动作      |
| `colreg-rule15-cs-edge`     | R15 boundary     | give-way | 正遇/穿越边界稳定分类         |
| `colreg-rule15-ot-boundary` | R15/R13 boundary | give-way | 穿越/追越边界稳定分类         |
| `colreg-rule17-cr-so`       | R17/R15          | stand-on | 前期保向，末段 17(b) 独立行动 |

命名要求：`scripts/run_6_scenarios.py` 名称已过时。后续应改为 `run_colregs_clean_8probe.py` 或等价名称，并保留兼容 wrapper，避免历史脚本名误导评审。

## 4. CPA Threshold Model

当前数字来源不是法规，是 scenario profile。必须在报告中显式输出。

FCB 当前船长 `L=45.0m`，来源：[fcb_simulator_plugin.cpp (line 76)](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/integration-20260615/src/sim_workbench/fcb_simulator/src/fcb_simulator_plugin.cpp:76)。

| Threshold | 来源                               | 船长倍数 | 适用 profile                              | 置信度          |
| --------- | ---------------------------------- | -------- | ----------------------------------------- | --------------- |
| `185.2m`  | `0.1 NM` 紧急下限                  | `4.1L`   | close-start、Rule17 in-extremis、受限航道 | 🟡 项目工程阈值  |
| `300m`    | `0.1 NM` 与 `9L=405m` 之间折中     | `6.7L`   | 追越/边界/受限航道                        | 🔴 需补证据      |
| `405m`    | `9 x 45m LOA` 船域参考             | `9.0L`   | ideal domain reference                    | 🟡 项目船域参考  |
| `926m`    | `0.5 NM` 开放水域 warning baseline | `20.6L`  | open-water crossing                       | 🟡 项目 ODD 基线 |

本仓库现状来源：[README (line 29)](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/integration-20260615/scenarios/COLREGs测试/README.md:29)、[rule14-ho YAML (line 69)](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/integration-20260615/scenarios/COLREGs测试/colreg-rule14-ho.yaml:69)、[rule13-ot YAML (line 69)](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/integration-20260615/scenarios/COLREGs测试/colreg-rule13-ot.yaml:69)、[rule15-cs YAML (line 69)](/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/integration-20260615/scenarios/COLREGs测试/colreg-rule15-cs.yaml:69)。

Requirement: evaluator output must include `threshold_m`、`profile`、`basis`、`nm_equivalent`、`loa_multiplier`、`source_confidence`。

## 5. Trace Input Contract

每个 scenario 必须输出统一 trace：

- ownship: `t_s, x, y, sog, cog/hdg, rudder/rot/u_cmd/psi_cmd`
- target: `id, x, y, sog, cog/hdg`
- geometry: `range_m, rel_bearing_deg, dcpa_m, tcpa_s, closing_speed_mps`
- route: `active_wp, cross_track_error_m, route_progress, final_xte_m, max_xte_m`
- M2/M6: `rule, role, conflict_id, phase, confidence, rationale`
- M4: `behavior_state, selected_behavior, toggles`
- M5: `plan_valid, route_points, solver_status`
- M7: `veto_state, safety_margin`
- L4 output: `psi_cmd, u_cmd, rot_cmd, mode`

## 6. Seven-Layer Evaluator

Layer 1 Scenario Validity
检查 no-action DCPA/TCPA、初始相对方位、角色、ODD、是否真有冲突。无冲突场景不能用来证明避碰能力。

Layer 2 Safety Floor
检查全程 `min_separation_m >= cpa_threshold_m`。这是硬红线，但只回答“有没有擦得太近”。

Layer 3 Dynamic Risk
拆分风险暴露：

- `approach_warning_exposure_s`
- `approach_danger_exposure_s`
- `post_pass_domain_exposure_s`
- `recovery_time_s`

Gate 只应严罚 approach danger。post-pass close domain 单独计质量，不等价 collision threat。

Layer 4 COLREG Compliance
按规则独立评估：

- R14: 对遇右转，不左转穿越
- R13: 追越义务持续到 past-and-clear
- R15/R16: give-way 早、大、明显动作
- R17: stand-on 前期保向，必要时 17(b) 独立动作

Layer 5 Route Recovery
检查 `returned_to_route`、`final_xte_m`、`max_route_xte_m`、是否出 L2 corridor。当前 8-probe 应继续把 `max_route_xte <= 500m` 作为核心 gate。

Layer 6 Seamanship / Efficiency
检查 path ratio、integrated XTE、overshoot、过度绕行、掉头追逐。目标：不只“安全”，还要“像船艺正确动作”。

Layer 7 Stability / Solver Health
检查 steering reversals、behavior toggles、primary threat switches、M5 solver valid、M7 veto。目标：防 fishtail、flap、假稳定。

## 7. Overall Verdict

推荐输出三层结论，不再只有一个 PASS：

- `safety_pass`: CPA floor + no collision + M7 no critical veto
- `mission_pass`: route return + corridor + no excessive detour
- `colregs_pass`: rule behavior + role lifecycle + past-and-clear
- `overall_pass = safety_pass && mission_pass && colregs_pass && stability_pass`

同时输出 `risk_quality_score`，但 score 不替代硬 gate。

## 8. Heading-On Post-Pass Rule

对遇场景中，目标船已在本船身后时：

Active collision threat 条件：
`TCPA >= 0` 或 `closing_speed > 0`，且未 `past_and_clear`

Post-pass clearance 条件：
`TCPA < 0`，`closing_speed <= 0`，目标已 abaft，距离开始增加

判定：

- active collision threat: 可计 warning/danger exposure
- post-pass clearance: 不计 approach danger；计 clearance quality
- 若未满足 past-and-clear：仍保留风险观察
- Rule13: 追越按特殊义务处理，不因 TCPA < 0 自动释放

## 9. Required Report Output

每个 scenario 报告必须含：

- scenario profile + threshold provenance
- min CPA and threshold multiplier
- approach/post-pass risk split
- COLREG rule lifecycle timeline
- route return timeline
- final L4 command behavior summary
- failed gates with first-failure timestamp
- trace artifact path

## 10. Reviewer Questions

1. `300m` 是否接受为工程折中，还是必须替换为公式：`max(0.1NM, k*LOA)`？
2. `9L=405m` 应作为 warning domain、ideal domain，还是部分场景硬 floor？
3. `0.5NM=926m` 是否只适用于 open-water，不能用于受限航道？
4. Rule17 in-extremis 用 `0.1NM` 是否过低，是否需加入“剩余操纵空间/时间”条件？
5. Heading-on post-pass close domain 是否只做质量扣分，不作为 collision threat fail？
6. 8-probe 是否应补“no-action baseline trace”，证明每个 probe 原始冲突有效？