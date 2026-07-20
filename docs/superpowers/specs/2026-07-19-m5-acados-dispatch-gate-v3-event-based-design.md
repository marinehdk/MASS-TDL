# M5 acados dispatch gate v3.1 — Event-based Composite Design Spec

> **Status:** DESIGN v3.1 (尚未实施)。v3 已通过 4 reviewer(M7 safety / GNC contract / code review / cert evidence)评审,全部 APPROVE_WITH_CONDITIONS;v3.1 吸收全部必修项 + 6 设计决策(B1-B6,用户 2026-07-19 拍板)。等用户最终接受后进实施。
> **产出**: 2026-07-19 v3 → 2026-07-19 v3.1
> **工作树**: `.worktrees/m5-design-grounding`(分支 `codex/m5-design-grounding` @ `c46e01045`)
> **前置证据**(必读):
> 1. `docs/superpowers/specs/2026-07-19-m5-acatos-ho-dynamic-convergence-probe.md`(ZCode 短路实验:acatos 在 ho 11/11 失败)
> 2. `docs/superpowers/research/2026-07-19-colregs-mpc-dispatch-threshold-literature.md`(codex 文献调研)
> 3. `docs/superpowers/specs/2026-07-19-ho-red-execution-chain-diagnosis.md`(**F2 ho RED 真根因诊断,v3.1 多处引用**)
> 4. `docs/superpowers/specs/2026-07-18-m5-p5-acados-convergence-design.md` §2/§5(P5 收敛边界)
> **v3.1 vs v3 changelog**(基于 4 reviewer 必修项 + 6 设计决策):
> - **B1**: C4 rot_max 锁 **4.7°/s(ship_config)+ v3 实施必须同步 F2 F-H(L4 preflight 3.5→4.7)**;否则 v3 失败模式跟 v2 一样(GNC B1 BLOCKING)
> - **B2**: C2 ENCOUNTER_ONSET 期(含 PREPLAN)放行 + 显式要求 NLP 遵循 `colregs_preferred_direction`(GNC B3)
> - **B3**: C4 `T_reserve = 60s`(M6 t_emergency_s)
> - **B4**: v3 实施时同步加最小 dispatch decision spdlog info + summary.json dispatch counters(cert B2)
> - **B5**: C5 `kAcatosAlignSinEpsilon = 0.01`(极严,几乎只挡精确 dead-ahead 0°;F5 sweep 后定真值)
> - **B6**: 复用 `tail_builder::EncounterState` enum + bool `has_m6_encounter_state` flag(M7 F-I1,避免 sentinel=3 与 RELEASE=3 冲突)
> - **A1**: §4.3.4 删除「多层防护完整」+ §0/§3/§4.2 修正 MRM-02 触发链(M7 F-B2:counter byte-identical 但 M7 不订阅,spdlog::critical 不是 trigger)
> - **A2**: §2.2 明确 C3 是 dispatch heuristic 不是 L4 executability(GNC B2)
> - **A3**: §3/§6 显式标注 BC-MPC Override profile=gnc dead letter + F2 F-C 链接(cert B1 + GNC S1)
> - **A4**: §2.5 加 PROJECT_EMPIRICAL calibration evidence register 表(cert S2)
> - **A5**: §3 加 v3 ODD scope = ODD-A only 明示 + Rule 19 映射(M7)
> - **A6**: §2.5 PROJECT_EMPIRICAL 阈值链接到 m5_params.yaml(M7 F-M1,避免 hardcoded binary)
> - **A7**: 新增 §5.1.T-MRM-1/T-MRM-2 RED 证据保留 + T-C4-ROT/T-C3-LINEAR/T-C2-FSM/T-C4-DEFAULT 等测试
> **范围**: M5 Mid-MPC dispatch gate(`mid_mpc_solver.cpp:147-167`)+ **L4 preflight ROT 校准(F2 F-H,因 B1)** + 最小 dispatch log/counters(B4)。**不**改 solver opts、warm-up、ROS2 IDL/topic/QoS、M6 publish 字段、M7/M8 代码。
> **硬约束**: AGENTS.md COLREGs full-chain debugging rule + 不凑绿 + vessel-agnostic + 不加 mock/skip/forced PASS/scenario-id 条件。

---

## 0. TL;DR

| 项 | 结论 |
|---|---|
| **v2 gate-2 的根本缺陷** | 用 M2 线性瞬时 `tgt.cpa_m < cpa_safe × 1.0`。ho DCPA=0 时恒触发,但**真实失败原因不是瞬时 CPA 而是 SQP 在该几何下先天不可解**(ZCode probe 实测 11/11 status=3)。 |
| **v3 设计哲学** | **Event-based composite,不是单 scalar gate**。codex 文献调研 §4 最强支持是 Thyri §4.7 的 critical-entry/exit + horizon + past-pass padding 模式;Eriksen §4.2.2 的 `dCPA+tCPA` state machine 模式。**不**用单瞬时几何量切 layer。 |
| **关键发现 — M6 已经实现了 v3 需要的 lifecycle** | `l3_msgs/COLREGsConstraint.msg` 已携带 `phase`(`T_standOn`/`T_act`/`T_postAvoid`)+ `encounter_state`(CLEAR/ONSET/ACTIVE/RELEASE)+ `past_clear` + `release_predicted`。M6 `odd_aware_thresholds.yaml` 已定义 `t_plan_s=720s` / `t_standOn_s=480s` / `t_act_s=240s` / `t_emergency_s=60s` / `cpa_hard_m=1852` / `cpa_release_m=1000`。ho trace 实测 M6 正常 populate 这些字段(encounter_state dist: CLEAR 41 / ONSET 527 / ACTIVE 1391;phase: PRESERVE_COURSE 568 / SOUND_WARNING 1031 / INDEPENDENT_ACTION 360)。**v3 不需要新 ROS2 字段,只需 M5 消费 M6 已有字段。** |
| **v3.1 dispatch 判据(composite)** | acatos 上场 iff **所有** target 满足:(C1) `tcpa_s > t_plan_s(720s)`(ample-time,M6 已定义)+ (C2) `encounter_state ∈ {CLEAR, ONSET}`(M5 不在 ACTIVE/RELEASE 期;**ONSET 含 PREPLAN,acatos 必须遵循 `colregs_preferred_direction`** — v3.1 B2)+ (C3) horizon-projected CPA gap ≤ 200m(**dispatch heuristic,非 L4 executability 合约** — v3.1 GNC B2)+ (C4) `R_reach ≤ 0.8`,**rot_max=4.7°/s ship_config + 同步 F2 F-H L4 preflight** — v3.1 B1;T_reserve=60s — v3.1 B3)+ (C5) 非 target-aligned(`abs(sin(rel_brg)) > 0.01` — v3.1 B5 极严,F5 sweep 后定真值) |
| **ho 上的行为(v3.1)** | ho TCPA=1620s 进入 → C1 通过;sim_t=291 M6 conflict → encounter_state=ACTIVE → **C2 挡**(全 fallback IPOPT)。v3.1 行为跟 v2 相同(ho 全 fallback),但**理由更对**:v3.1 因为「M6 ACTIVE」,v2 因为「CPA 太近」(错)。**ho RED 修复路径在 F2(独立工作线),v3.1 不试图修 ho RED**。 |
| **COLREGs 影响** | acatos 只在 ample-time + horizon-feasible + ROT-reachable + 非 Jacobian-singular 几何上场。这是「让 acatos 在它能力范围内被测」,不是「凑绿」。符合 AGENTS.md "先天不可实现场景不作为评价指标"。 |
| **M5/M7 影响(v3.1 修正)** | dispatch 行为改变但 MidMpcSolution shape 不变。**v3.1 修正:M5 `consecutive_failures_` counter byte-identical 保留;但 M7 MRM-02 不直接消费该 counter(M7 F-B2 核实:`mrm_selector.cpp:64-106` 走独立 SOTIF/CPA-trend/watchdog 路径)。spdlog::critical "M7 MRM-02 escalation" 是 audit log 字符串,不是 M7 trigger**。M7 hard-CPA checker 独立路径保留。**BC-MPC Override 在 profile=gnc 下当前是 dead letter(F2 §1.2 + cert B1),v3.1 不修复(F2 F-C 独立 work item)**。ROS2 wire 零改动。 |
| **风险等级** | 中-高。涉及 5 个判据的 composite,每个判据都有 project-empirical 阈值。需 M7 safety + GNC contract + code review + **新增 cert evidence reviewer**(composite gate 是 SIL2 auditability 边界)四方签字 |
| **验收** | 单测(每个 C1-C5 边界 + composite) + 12 标准 COLREGs 场景 SIL full sim + acatos 收敛场景验收(crossing ample-time)+ acatos 失败场景验收(ho + close-quarters) |

---

## 1. 证据基线(代码 + 实测 + 文献,分级标)

### 1.1 ZCode 短路实验 — acatos 在 ho 上先天失败(PROJECT_FACT)

文件: `docs/superpowers/specs/2026-07-19-m5-acatos-ho-dynamic-convergence-probe.md`

**实测结论**: 短路 gate-2 强制 acatos 上场 11 个 cycle,11/11 全部 `status=3 (NumericalFailure), sqp_iter=1, cpa_slack=1e-19`。HPIPM 在第 1 个 QP 就 reject。

**根因(数学)**: ho DCPA=0 + target dead-ahead 让 CPA 约束 `h = (x_own−x_tgt)² + (y_own−y_tgt)² − cpa_safe²` 在初始解附近 Jacobian 接近 0 → HPIPM KKT 矩阵条件数爆炸。

**结论**: ho DCPA=0 + target dead-ahead 是 acatos (NP_PER_STAGE=56 + SQP+MERIT_BACKTRACKING + 当前 codegen) 的先天不可解场景。AGENTS.md "先天不可实现场景不作为评价指标" 适用。

### 1.2 codex COLREGs 文献调研 — event-based composite 是主流 DOMAIN_EVIDENCE

文件: `docs/superpowers/research/2026-07-19-colregs-mpc-dispatch-threshold-literature.md`

**关键结论**(每条带文献引用):

1. **架构 [R17] 引用错误**(§1.2):架构 §9.3 的 `T_standOn/T_act` 数值在 Wang 2021 原文不存在。Wang 2021 真实给的是 encounter-specific:crossing 20min/6NM、overtaking 30min/3NM,**不支持 universal 20min ample-time 上限**。当前 gate-2 memo v2 引用的 "Wang 2021 ample-time" 来源链失效。

2. **Eriksen §4.2.2 真实门**(§2.1):mid-MPC encounter entry 是 `dCPA<900m AND 0≤tCPA≤270s`,exit hysteresis `dCPA<2000m AND −20≤tCPA≤290s`。**不是** raw TCPA>T_h,M5 v3 不能照搬。

3. **Thyri §4.7 真实模式**(§2.2):用 `t_enter_crit / t_exit_crit` 与 `T_horizon − T_after_pass_padding` 事件条件;`T_critical=140s`、`T_after_pass_padding=40s`、`T_horizon=600s`。这是 v3 设计的主要文献模板。

4. **DCPA=0 不是 universal MPC hardest**(§5.1):Eriksen 2019 BC-MPC(branching-course)三次实测解 pure reciprocal head-on,最小距离 197.8/100.8/132.5m,两例合规。**说明 ho 修复路径在 BC-MPC 那条线,不在 acatos**。

5. **G1/G2/G3/G5 文献支持度**(§4):G1 horizon-projected CPA 概念支持但数值无先例;G2 ROT reach ratio 原则支持但阈值无先例;G3 exact-penalty ratio 无领域支持;G5 post-solve trend 无领域 gate 先例。

6. **codex 阈值建议**(§6):G1 provisional `≤200m` 放行 / `200-250m` characterization / `>250m` fallback,严禁叫 COLREGs ample-time threshold,只能叫 `PROJECT_EMPIRICAL_ACADOS_GUARD`。G2 provisional `≤0.8` 放行 / `0.8-1.0` conservative / `>1.0` reject。

### 1.3 M6 encounter lifecycle 已实现(PROJECT_FACT,代码实测)

`l3_msgs/msg/COLREGsConstraint.msg:1-29` 已定义:
```msg
string phase                    # "T_standOn" | "T_act" | "T_postAvoid"
uint8 ENCOUNTER_CLEAR=0
uint8 ENCOUNTER_ONSET=1
uint8 ENCOUNTER_ACTIVE=2
uint8 ENCOUNTER_RELEASE=3
uint8 encounter_state
bool past_clear
bool release_predicted
```

`m6_colregs_reasoner/config/odd_aware_thresholds.yaml` 已定义 ODD-A 阈值:
- `t_plan_s=720s`(PREPLAN→ACTIVE gate,= ample-time floor)
- `t_standOn_s=480s`(8min)
- `t_act_s=240s`(4min)
- `t_emergency_s=60s`
- `cpa_hard_m=1852m` / `cpa_soft_m=2778m` / `cpa_release_m=1000m`

`colregs_phase_classifier.cpp:8-19` 已实现 phase 分类。
`encounter_state_machine.cpp` 已实现完整 FSM:`CLEAR → DETECTED → CANDIDATE → PREPLAN → ACTIVE ↔ MONITOR → RELEASE → CLEAR`。

**ho trace 实测 M6 字段 populate**(从 `runs/acatos_ho_probe/colreg-rule14-ho.trace_current.jsonl`):
- `encounter_state`: CLEAR 41 / ONSET 527 / ACTIVE 1391
- `phase`: PRESERVE_COURSE 568 / SOUND_WARNING 1031 / INDEPENDENT_ACTION 360

**结论**: v3 需要的所有 lifecycle 信号 M6 已经提供,M5 只需消费。零 ROS2 接口改动。

### 1.4 gate-2 v2 缺陷复述(PROJECT_FACT,被 v3 取代)

`mid_mpc_solver.cpp:147-167` + `mid_mpc_solver.hpp:144-153` 当前用 `compute_bc_mpc_territory(input, cpa_safe_m)`,判据 `tgt.cpa_m < cpa_safe_m × 1.0`。问题:
1. 用 M2 线性瞬时 CPA,与 BC-MPC trajectory minimax CPA 数值可差数百米(memo v2 §4.1 已记录)。
2. `cpa_safe_m` 在 `colregs_conflict_active` 时升到 2500(`mid_mpc_node.cpp:633-637`),gate 阈值也跟着升到 2500,**注释 `mid_mpc_solver.cpp:136` 写 1852 是 stale**。
3. 不能预测 acatos 是否会收敛(见 1.1)。
4. ho DCPA=0 时恒触发,无论 cpa_safe 是 1852 还是 2500。

---

## 2. v3 Composite Gate 设计

### 2.1 数学定义

```cpp
// All inputs come from MidMpcInput (no new ROS2 fields):
//   - input.targets[i]: x_m, y_m, cog_rad, sog_mps, cpa_m, tcpa_s (M2 linear)
//   - input.colregs_conflict_active, input.colregs_primary_role (M6)
//   - input.constraints.cpa_safe_m, cpa_hard_m
//
// M6 encounter_state: v3.1 reuses tail_builder::EncounterState enum (B6) +
// bool has_m6_encounter_state flag (avoid sentinel=3 vs RELEASE=3 conflict,
// per M7 F-I1). See §2.4.
//
// v3.1 evaluation order (S1 should-fix from code review): cheapest/most-
// frequent first. C2 (encounter_state) blocks ~70% of ho cycles; C5 (alignment)
// is dead-cheap sin() check; C1 (tcpa) cheap; C3 (horizon scan) expensive;
// C4 (ROT reach) depends on tcpa, run last.
//
// acatos dispatches iff ALL targets pass C1-C5:

bool compute_acatos_feasibility(const MidMpcInput& input) noexcept {
  // Empty targets → no threat → acatos dispatch (warm-up geometry / no-track
  // cycles). Explicit intent (code review S3).
  // NaN defense at loop top (code review S2): skip targets with non-finite
  // geometry fields so NaN doesn't silently pass C3-C5 via IEEE-754 false-
  // comparison semantics.

  // C2 first (cheapest, most-frequent fail in ACTIVE/RELEASE encounters).
  // PREPLAN is a sub-state of ONSET (M6 colregs_reasoner_node.cpp:1720-1739
  // maps DETECTED/CANDIDATE/PREPLAN → ENCOUNTER_ONSET). v3.1 B2: acatos IS
  // dispatched in PREPLAN, but the NLP MUST follow input.colregs_preferred_direction
  // (M6's mandated give-way heading). This constraint is enforced inside the
  // NLP via the existing colregs_preferred_direction MidMpcInput field, NOT
  // in this gate.
  if (!input.has_m6_encounter_state) {
    // No M6 data this cycle (startup / M6 down) → fail-closed: do NOT dispatch
    // acatos when we don't know the encounter state.
    return false;
  }
  const auto es = input.colregs_encounter_state;  // tail_builder::EncounterState
  if (es == EncounterState::Active || es == EncounterState::Release) {
    return false;
  }

  for (const auto& tgt : input.targets) {
    if (!std::isfinite(tgt.tcpa_s)) continue;  // skip non-threat targets
    if (!std::isfinite(tgt.x_m) || !std::isfinite(tgt.y_m) ||
        !std::isfinite(tgt.cog_rad) || !std::isfinite(tgt.sog_mps)) {
      continue;  // skip targets with non-finite geometry (code review S2)
    }

    // C5: target alignment — run before C1 because it's dead-cheap.
    // Reject if target nearly dead-ahead or dead-astern (rel_brg≈0 or π).
    // v3.1 B5: kAcatosAlignSinEpsilon = 0.01 (sin⁻¹(0.01) ≈ 0.57° half-cone,
    // i.e. only blocks nearly-exact dead-ahead/astern). ZCode probe §3.2 only
    // tested 0°; the 5°/10° boundaries are untested, so v3.1 is ultra-
    // conservative and lets C5 be nearly a no-op until F5 factorial sweep
    // calibrates the real Jacobian-singularity cone.
    const double rel_brg = relative_bearing_rad(input.own_ship, tgt);
    if (std::abs(std::sin(rel_brg)) < kAcatosAlignSinEpsilon) return false;

    // C1: ample-time (M6 t_plan_s = 720s for ODD-A; ODD-aware future work)
    if (tgt.tcpa_s < kAcatosAmpleTimeFloorS) return false;  // 720s

    // C3: horizon-projected CPA gap. DISPATCH HEURISTIC ONLY (GNC B2):
    // linear propagation of own+tgt over N stages does NOT model own-ship
    // turning, so gap_h=0 (pass) does NOT prove L4 can execute the trajectory
    // acatos generates. L4 executability is independently enforced by
    // gnc_avoidance_preflight with its own ROT/curvature envelope (which v3.1
    // aligns to 4.7°/s via F2 F-H, see §2.6 B1).
    const double gap_h = horizon_projected_cpa_gap(input, tgt);
    if (gap_h > kAcatosGapProjectEmpiricalM) return false;  // 200m provisional

    // C4: ROT reach ratio. v3.1 B1: rot_max LOCKED to ship_config
    // max_yaw_rate_deg_s = 4.7°/s (NOT NLP's cruise 4.7°/s default, NOT L4
    // preflight's hardcoded 3.5°/s). v3.1 implementation MUST ship with F2 F-H
    // (L4 preflight 3.5→4.7) — otherwise C4 passes cases L4 will reject,
    // reproducing v2's ho RED failure mode byte-identically (GNC B1 BLOCKING).
    // T_reserve = 60s = M6 odd_aware_thresholds.yaml t_emergency_s (B3).
    // rot_max_rad_s default in types.hpp:242 is 0.2094 (12°/s) — STALE; C4
    // MUST validate input.rot_max_rad_s > 0 AND derived from live ODD, else
    // fail-closed (GNC finding 5 + T-C4-DEFAULT-SENTINEL).
    const double r_reach = rot_reach_ratio(input, tgt, kAcatosRotReachReserveS);
    if (r_reach > kAcatosRotReachEmpirical) return false;   // 0.8 provisional
  }
  return true;
}
```

### 2.1.1 horizon_projected_cpa_gap 实现伪代码(code review C2 必修)

```cpp
// Returns max(0, cpa_hard_m − min_k CPA_pred(k)) for a single target.
// Linear propagation: own and target both maintain current sog/cog over the
// horizon (no turn modeled). Per-stage CPA via 2D Euclidean distance.
// Cost: O(N) per target = ~80 stages × ~10 FLOPs = ~800 FLOPs/target.
// 16 targets × 800 = 12800 FLOPs/cycle = ~10μs. Acceptable under 60s budget.
//
// INPUT fields consumed (all pre-existing in MidMpcInput):
//   input.own_ship.{x_m, y_m, cog_rad, sog_mps}
//   tgt.{x_m, y_m, cog_rad, sog_mps}
//   input.constraints.cpa_hard_m  (1852m, the un-bumped floor)
//   N (formulation_.config().n_horizon = 80), dt (15s)
//
// RETURNS: gap in meters; 0 if horizon CPA stays ≥ cpa_hard throughout.

double horizon_projected_cpa_gap(const MidMpcInput& input,
                                  const TargetState& tgt) noexcept {
  const int N = 80;        // from formulation config
  const double dt = 15.0;  // from formulation config
  const double cpa_hard = input.constraints.cpa_hard_m;  // 1852m

  // Linear velocity components
  const double own_vx = input.own_ship.sog_mps * std::cos(input.own_ship.cog_rad);
  const double own_vy = input.own_ship.sog_mps * std::sin(input.own_ship.cog_rad);
  const double tgt_vx = tgt.sog_mps * std::cos(tgt.cog_rad);
  const double tgt_vy = tgt.sog_mps * std::sin(tgt.cog_rad);

  double min_cpa = std::numeric_limits<double>::infinity();
  for (int k = 0; k <= N; ++k) {
    const double t = k * dt;
    const double ox = input.own_ship.x_m + own_vx * t;
    const double oy = input.own_ship.y_m + own_vy * t;
    const double tx = tgt.x_m + tgt_vx * t;
    const double ty = tgt.y_m + tgt_vy * t;
    const double dx = ox - tx;
    const double dy = oy - ty;
    const double cpa_k = std::sqrt(dx*dx + dy*dy);
    min_cpa = std::min(min_cpa, cpa_k);
    if (min_cpa <= cpa_hard) break;  // early exit once below floor
  }
  return std::max(0.0, cpa_hard - min_cpa);
}
```

### 2.2 五个判据逐项论证(v3.1 修订)

| 判据 | 数学 | 文献依据 | ho 行为 | 阈值标定 |
|---|---|---|---|---|
| **C1 ample-time** | `tcpa > t_plan_s=720s` | M6 `odd_aware_thresholds.yaml` A-level C-12 case law;Eriksen §4.2.2 entry `tCPA≤270s`(M5 必须 ample-time >> 270s) | ho TCPA=1620s > 720 → **放行** | ODD-A 720s 是项目 A-level;ODD-B/C/D 走 odd-aware(v3 ODD scope = ODD-A only,M7 要求明示) |
| **C2 encounter_state** | `state ∈ {CLEAR, ONSET}`,挡 ACTIVE/RELEASE | M6 FSM 设计(spec 2026-06-17-colregs-avoidance-fsm-design §3.2);Thyri §4.7 critical-entry 触发 reactive 层接管。**v3.1 B2: ONSET 含 PREPLAN(M6 colregs_reasoner_node.cpp:1720-1739 把 DETECTED/CANDIDATE/PREPLAN 都映射到 ONSET),acatos 在 PREPLAN 也上场但 NLP 必须遵循 `colregs_preferred_direction`**(MidMpcInput 已有字段) | ho sim_t=291 后 ACTIVE → **挡**(acatos 让位 BC-MPC) | M6 已经算好,M5 只消费 |
| **C3 horizon-projected gap** | `gap_h = max(0, cpa_hard − min_k CPA_pred(k)) ≤ 200m`。**v3.1 GNC B2: dispatch heuristic,不是 L4 executability 合约。L4 executability 由 `gnc_avoidance_preflight` 独立强制** | codex §6.1 provisional;Johansen 2016 + Thyri §4.7 horizon-prediction 先例;P5 §2 边界 252m 留 20% 余量 | ho gap_h=0(target 在 horizon 外)→ **放行**;但 acatos 仍因 C2(后期)/C5(全程 dead-ahead)被挡 | `kAcatosGapProjectEmpiricalM=200m`,**严禁叫 COLREGs ample-time**,只能叫 PROJECT_EMPIRICAL_ACADOS_GUARD |
| **C4 ROT reach** | `R_reach = Δ_req / (½·u·rot_max·T_eff²) ≤ 0.8`,`T_eff = min(T_h, max(0, tcpa_s − T_reserve))`。**v3.1 B1: rot_max = 4.7°/s ship_config**;**v3.1 B3: T_reserve = 60s(M6 t_emergency_s)** | codex §6.2 provisional;Eriksen 2017 steady U-r feasible set;Tsolakis 2022 反对强行 ROT 接近上限 | ho Δ_req=0(target horizon 外)→ R_reach=0 → **放行** | `kAcatosRotReachEmpirical=0.8`,20% 工程余量,无文献校准 |
| **C5 target alignment** | `abs(sin(rel_brg)) > 0.01`(v3.1 B5 极严,几乎只挡精确 dead-ahead) | ZCode probe §3.2 实测 CPA Jacobian 奇异(仅测 dead-ahead 0°);**C5 是 numerical-conditioning proxy,不是 COLREGs 安全边界**(M7 F-M2) | ho rel_brg≈0 → sin≈0 < 0.01 → **挡**(避开 Jacobian 奇异) | `kAcatosAlignSinEpsilon=0.01`(对应 ~0.57° half-cone,**F5 factorial sweep 后定真值**) |

**ho 上的 v3.1 行为**:
- t=0~291s:TCPA=1195-1620s,C1 通过;encounter_state=CLEAR/ONSET,C2 通过;gap_h=0,C3 通过;R_reach=0,C4 通过;**C5 挡(dead-ahead sin≈0)** → fallback IPOPT
- t=291s+:encounter_state=ACTIVE,**C2 挡** → fallback IPOPT

**所以 v3.1 在 ho 全程都走 fallback IPOPT,acatos 0 dispatch**。这与 v2 的行为相同,**但理由更对**:v2 是因为「CPA 太近」(错),v3.1 是因为「CPA Jacobian 奇异 + encounter ACTIVE」(对,符合 acatos 能力边界)。

### 2.3 v3.1 不会让 acatos 在 ho 上被测,这是 honest 的

ZCode 实测证明 acatos 在 ho 上先天失败(§1.1)。v3.1 接受这个事实,**ho 不作为 acatos 验收场景**。acatos 的验收场景应该是:
- crossing-give-way ample-time(target range > 5000m + TCPA > 1200s + rel_brg 远离 0/π)
- ot-port / ot-target-giveway(同上 + 非对齐)
- F5 follow-up:选 acatos 能收敛的标准场景集

ho RED 的修复路径在 F2(IPOPT/TailGate/L4 preflight + BC-MPC Override consumer,见 `2026-07-19-ho-red-execution-chain-diagnosis.md`),与 acatos dispatch 正交。

### 2.4 Interface change — M5 消费 M6 encounter_state/phase(v3.1 B6 复用 tail_builder enum)

v3.1 在 `MidMpcInput` 新增 3 个字段(从 M6 COLREGsConstraint 读入):

```cpp
// In MidMpcInput (types.hpp), add:
// v3.1 B6: reuse tail_builder::EncounterState enum (single source of truth).
// The bool flag avoids sentinel-vs-RELEASE=3 collision (M7 F-I1).
tail_builder::EncounterState colregs_encounter_state{tail_builder::EncounterState::Clear};
bool has_m6_encounter_state{false};  // false until first M6 msg received
std::string colregs_phase;           // "T_standOn" | "T_act" | "T_postAvoid" (M6 phase string)
```

在 `mid_mpc_node.cpp` 的 `assemble_input_`(读 M6 `colregs_constraint_`)添加:

```cpp
inp.has_m6_encounter_state = (colregs_constraint_ != nullptr);
if (colregs_constraint_) {
  // Map M6 msg uint8 → tail_builder::EncounterState
  // M6 ENCOUNTER_CLEAR=0/ONSET=1/ACTIVE=2/RELEASE=3 (l3_msgs/COLREGsConstraint.msg)
  // tail_builder::EncounterState has matching enum values (see tail_builder.hpp:25).
  using ES = tail_builder::EncounterState;
  switch (colregs_constraint_->encounter_state) {
    case l3_msgs::msg::COLREGsConstraint::ENCOUNTER_CLEAR:   inp.colregs_encounter_state = ES::Clear; break;
    case l3_msgs::msg::COLREGsConstraint::ENCOUNTER_ONSET:   inp.colregs_encounter_state = ES::Onset; break;
    case l3_msgs::msg::COLREGsConstraint::ENCOUNTER_ACTIVE:  inp.colregs_encounter_state = ES::Active; break;
    case l3_msgs::msg::COLREGsConstraint::ENCOUNTER_RELEASE: inp.colregs_encounter_state = ES::Release; break;
    default: inp.has_m6_encounter_state = false;  // unknown value → fail-closed
  }
  inp.colregs_phase = colregs_constraint_->phase;
}
```

**这是新内部 field,不改 ROS2 IDL/M6 publish 字段**(M6 早就 publish 了,M5 之前没消费)。tail_builder.hpp:25 已有 `EncounterState` enum 定义,v3.1 不新建第二套(M7 F-I1)。

### 2.5 阈值命名规范 + calibration evidence register(codex + cert S2 强制要求)

**命名规范**:

```cpp
// mid_mpc_solver.hpp
// WARNING: these thresholds are PROJECT_EMPIRICAL_ACADOS_GUARD values, NOT
// COLREGs ample-time thresholds. They are calibrated against P5 §2 acatos
// convergence boundary (gap 252m) and ZCode ho probe (target-alignment
// Jacobian singularity). They MUST NOT be cited as COLREGs literature values
// (codex research 2026-07-19-colregs-mpc-dispatch-threshold-literature.md §6).
//
// Update only after factorial SIL sweep (F5): dead-ahead, ±lateral offset,
// varying TCPA/relative speed, single/multi-target, cold/warm seed.
//
// v3.1 A6 (M7 F-M1): these constants SHOULD be exposed via m5_params.yaml
// (config-driven) to avoid hardcoded binary numbers. For v3.1 first impl,
// they are static constexpr with this comment; config migration is a
// follow-up. The override_cpa_multiplier precedent (m5_params.yaml:14)
// already exists.
static constexpr double kAcatosAmpleTimeFloorS       = 720.0;   // M6 ODD-A t_plan_s
static constexpr double kAcatosGapProjectEmpiricalM  = 200.0;   // codex §6.1 provisional
static constexpr double kAcatosRotReachEmpirical     = 0.8;     // codex §6.2 provisional
static constexpr double kAcatosRotReachReserveS      = 60.0;    // B3 = M6 t_emergency_s
static constexpr double kAcatosAlignSinEpsilon       = 0.01;    // B5 ultra-strict, F5 sweep pending
static constexpr double kAcatosRotMaxDegS            = 4.7;     // B1 ship_config max_yaw_rate_deg_s
```

**Calibration evidence register**(cert S2 必修):

| Threshold | Value | 来源 | 校准数据 | 置信度 | 重新校准 trigger |
|---|---|---|---|---|---|
| `kAcatosAmpleTimeFloorS` | 720.0s | M6 `odd_aware_thresholds.yaml::t_plan_s` A-level C-12 case law | M6 A-level spec(项目溯源) | 🟢 High | HAZID RUN-001(2026-08-19)可能调 |
| `kAcatosGapProjectEmpiricalM` | 200.0m | codex §6.1 provisional;P5 §2 acatos 收敛边界 252m | P5 §2 单场景 bracket `(252, 352)m`;200m 留 20.6% 余量 | 🟡 Medium | F5 factorial sweep(dead-ahead / ±offset / TCPA-var / single/multi / cold/warm) |
| `kAcatosRotReachEmpirical` | 0.8 | codex §6.2 provisional;"20% 工程余量" | **无校准数据点 — 纯工程推断** | 🔴 Low | F5 factorial sweep |
| `kAcatosRotReachReserveS` | 60.0s | M6 `t_emergency_s`(B3) | M6 A-level spec | 🟢 High | HAZID RUN-001 |
| `kAcatosAlignSinEpsilon` | 0.01 | v3.1 B5 ultra-strict;ZCode probe §3.2 dead-ahead 0° 实测 | **单点几何(dead-ahead 0°)**;5°/10° 未测 | 🔴 Low | F5 factorial sweep(0°/1°/3°/5°/10°/15° × TCPA-var) |
| `kAcatosRotMaxDegS` | 4.7 | `third_party/gnc_ws/.../ship_config.yaml:632` | ship_config 生产配置 | 🟢 High | ship_config 改动需同步 |

**CCS surveyor challenge 应答**(cert N3):"200m 相对唯一已知 acatos 收敛通过点(P5 §2: 252m)留 20.6% 余量;提升到 250/252m 被 F5 factorial sweep 阻塞。0.8 / 0.01 是 provisional,标 🔴 Low confidence,在 ASDR dispatch event payload 里记录(F4 落地后)。"

### 2.6 B1 ROT 源锁定 + F2 F-H 同步要求(GNC B1 BLOCKING)

v3.1 B1 决策:C4 rot_max 锁 **`ship_config.yaml::max_yaw_rate_deg_s = 4.7°/s`**(用户拍板)。

**4 个 ROT 源对照**(GNC reviewer B1 + F2 §1.2 核实):

| 来源 | 值 | 用途 | 一致性 |
|---|---|---|---|
| `ship_config.yaml:632` | 4.7°/s | 生产 ship dynamics config | 🟢 source of truth |
| M5 NLP `effective_gnc_odd_().cruise_max_yaw_rate_deg_s` | 4.7°/s(收到 ODD 时)/ 1.2°/s(fallback) | NLP ROT envelope | 🟢 live ODD 时与 ship_config 一致;🔴 ODD 没收到时用 stale fallback 1.2 |
| **L4 preflight `gnc_avoidance_preflight.hpp:28`** | **3.5°/s hardcoded** | L4 GNCPreflight required_turn_radius_m | 🔴 **与 ship_config 4.7 不一致(F2 实测 ho RED 根因之一)** |
| `types.hpp:242 MidMpcInput::rot_max_rad_s` default | 0.2094 rad/s = 12°/s | stale C++ default | 🔴 stale(GNC finding 5) |

**v3.1 实施要求**:
1. C4 `rot_reach_ratio()` 必须用 `kAcatosRotMaxDegS = 4.7°/s`(B1),不接受 `input.rot_max_rad_s` 的 stale default 12°/s(GNC finding 5)。如果 `input.rot_max_rad_s` 来自 `effective_gnc_odd_()`(收到 ODD),则等于 4.7°/s,一致;否则 C4 必须用 `kAcatosRotMaxDegS` 常数 fail-safe。
2. **v3.1 实施必须同步 F2 F-H(L4 preflight 3.5→4.7)**,否则 C4 通过的 case L4 会拒,v3.1 失败模式跟 v2 一样(GNC B1 BLOCKING)。
3. F2 F-H 是 0.3d small fix(GNC reviewer 风险 Green),独立 PR 但必须与 v3.1 同一 promotion gate。

### 2.7 B2 PREPLAN 处理(GNC B3 BLOCKING)

`colregs_reasoner_node.cpp:1720-1739` 把 M6 内部 FSM 的 `DETECTED / CANDIDATE / PREPLAN` 三个状态都映射到 `ENCOUNTER_ONSET` msg 值。`PREPLAN` 是 M6 已经算出 `preferred_direction` 并 commit give-way heading 的窗口(`t_monitor_s=1500s`,720 < tcpa < 1500 都在 PREPLAN)。

**v3.1 B2 决策**(用户拍板):**C2 在 ENCOUNTER_ONSET 期(含 PREPLAN)放行 acatos**,但 NLP 必须遵循 `MidMpcInput.colregs_preferred_direction`。

**实施保证**:`colregs_preferred_direction` 字段已存在(`types.hpp:232`),已经在 `mid_mpc_node.cpp:584` 从 M6 publish 读入,已经在 NLP `cost_preferred_direction` 里被消费。**v3.1 不需要新代码来强制 acatos 遵循 preferred_direction**,只需在 §3 stage table 标注「acatos 在 PREPLAN 上场依赖 NLP 已有 preferred_direction cost term」。

**剩余风险**(GNC B3):M6 msg 不暴露 DETECTED/CANDIDATE/PREPLAN 区分,M5 无法在 dispatch gate 区分。v3.1 B2 接受此风险,因为 acatos 遵循 preferred_direction 时与 M6 不冲突。若未来需要更细粒度,改 M6 FSM 暴露 internal phase 是独立 work item(不在 v3.1)。

---

## 3. COLREGs 全链影响(AGENTS.md mandatory,v3.1 修订)

| Stage | v2 行为 | v3.1 行为 | 影响 |
|---|---|---|---|
| L2 route/speed | 不变 | 不变 | 无 |
| M2 world/CPA | M2 提供 `tgt.cpa_m/tcpa_s` | 不变;v3.1 还需 M2 提供 `x_m/y_m/cog/sog` 做 horizon propagation(已有) | 无 |
| **M6 rule/role/lifecycle** | M6 publish `phase`/`encounter_state`,**M5 未消费** | **M5 新增消费**(MidMpcInput 新增 3 字段:encounter_state/has_m6_encounter_state/phase) | M6 publish 不变,M5 内部消费 |
| M4 behavior FSM | 不变 | 不变 | 无 |
| **M5 trajectory** | acatos 0 dispatch(ho 全挡)→ IPOPT fallback → GeoFallback → EMPTY | acatos 0 dispatch(ho C2 ACTIVE / C5 dead-ahead 挡)→ IPOPT fallback(unchanged)。**非 ho 场景**:crossing ample-time + ONSET → acatos 上场,但 NLP 必须遵循 `colregs_preferred_direction`(B2) | **核心改变**:acatos 在 crossing ample-time 等场景能上场,ho 仍 fallback |
| L4 guidance | 接收 IPOPT/GeoFallback 的 plan;**L4 preflight 用 hardcoded 3.5°/s** | ho 不变;非 ho 场景若 acatos dispatch + 收敛,接收 acatos 的 plan。**v3.1 同步 F2 F-H:L4 preflight 3.5→4.7** | 接口 byte-identical,MidMpcSolution 11 字段不变 |
| **M7 veto/MRM**(v3.1 修正) | consecutive_failures + MRM-02 byte-identical(原 v3 表述) | **v3.1 修正(M7 F-B2):counter byte-identical 保留,但 M7 `mrm_selector.cpp:64-106` 走独立 SOTIF/CPA-trend/watchdog 路径,不直接消费 M5 consecutive_failures。spdlog::critical "M7 MRM-02 escalation" 是 audit log 字符串,不是 M7 trigger** | counter 路径 byte-identical;trigger 链间接(M5 失败 → CPA 恶化 → M7 cpa_trend_degrading → 可能 MRM-02) |
| **BC-MPC Override**(v3.1 修正) | "独立路径 byte-identical"(原 v3 表述) | **v3.1 修正(cert B1 + GNC S1):Override 触发逻辑确实 byte-identical,但 profile=gnc 下 `gnc_bridge_node.cpp:13-61` 不订阅 `/l3/m5/reactive_override_cmd`,2118 次 override 零效果(F2 §1.2 实测)。profile=fcb 下 Override 正常** | Override 触发逻辑不变;**profile=gnc 下 Override 当前是 dead letter,等 F2 F-C 修**(独立 work item) |
| Rule 19(restricted visibility,M7 要求) | 未在 v3 明示 | **v3.1 明示**:Rule 19 场景由 M6 phase / encounter_state 标记,v3.1 跟随 M6;M5 不独立做 Rule 19 判断。ODD-D 阈值(M6 odd_aware_thresholds.yaml)与 ODD-A 不同,但 v3.1 C1 当前硬编码 720s = ODD-A,**v3.1 ODD scope = ODD-A only**,ODD-B/D 需 odd-aware(未来工作) | v3.1 在 ODD-B/D 下 acatos 永远 0 dispatch(scope limit,非缺陷) |
| M8 evidence | ASDR 不带 solver identity | **v3.1 B4 同步加**:每 cycle spdlog info dispatch decision + summary.json dispatch counters(cert B2 最小化) | ASDR wire 仍不带 solver identity(F4 独立 work item,cert B2 要求 owner+deadline) |

**关键安全保证**(byte-identical 不变量 + v3.1 修正):
1. `consecutive_failures_` counter byte-identical 保留(`mid_mpc_solver.cpp:147-158`);**但 M7 MRM-02 不直接消费** — 走独立 SOTIF/CPA-trend/watchdog 路径(M7 F-B2)
2. IPOPT fallback 路径完全不动
3. MidMpcSolution shape 全部 11 字段不变
4. ROS2 topic/msg/IDL/QoS 零改动(M5 内部新增 MidMpcInput 字段,不上 wire)
5. BC-MPC Override **触发条件**(独立路径,`bc_mpc_collision_detector.cpp:100-114`)完全不依赖 Mid-MPC dispatch;**但 profile=gnc 下 Override 是 dead letter**(等 F2 F-C)
6. M7 hard-CPA checker(`hard_constraint_cpa.cpp`)独立路径,完全不依赖 Mid-MPC dispatch(M7 F-M3 核实通过)

---

## 4. 风险量化与失效边界(v3.1 修订)

### 4.1 风险矩阵

| 风险 | 等级 | 来源 | 失效边界 | 缓解 |
|---|---|---|---|---|
| C3 gap_h 用 linear propagation,不建模 own turn | 中 | own/target linear,CPA 用直线公式 | gap_h=0(放行)但 own 实际需大 turn → acatos 失败或返回不可执行轨迹 | **GNC B2 必修:C3 是 dispatch heuristic 不是 L4 executability 合约**;失败方向是 fallback IPOPT,有 consecutive_failures 兜底;L4 preflight 独立做真实 executability check |
| C4 ROT frame mismatch(若不同步 F2 F-H) | **高(GNC B1 BLOCKING)** | C4 用 4.7°/s,L4 preflight 用 3.5°/s hardcoded | C4 通过 case → acatos 算出 NLP 可行轨迹(4.7°/s)→ L4 emergency preflight 拒(3.5°/s)→ empty heartbeat → L4 stale → **失败模式跟 v2 byte-identical(F2 §1.2)** | **v3.1 B1 同步 F2 F-H:L4 preflight 3.5→4.7,与 v3.1 同一 promotion gate** |
| C5 alignment sin=0.01 极严,几乎只挡精确 dead-ahead | 低(B5 选 0.01 后)| ZCode probe 只测 dead-ahead 0° | 5°/10° 边界 acatos 仍可能失败(Jacobian 接近奇异但不精确奇异)→ consecutive_failures 兜底 | F5 factorial sweep 标定真值;0.01 是 ultra-strict initial,几乎不挡任何 production 场景(除精确 dead-ahead) |
| C2 ACTIVE 期完全不让 acatos 上 | 低 | M6 ACTIVE 期是「需要避让」期;acatos 在 ACTIVE 期先天失败(ho probe)| acatos 在 ho 全程 fallback,与 v2 行为相同 | 接受。ho RED 由 F2 修 |
| PROJECT_EMPIRICAL 阈值无 COLREGs 文献背书 | 中 | codex §6 明确 200m/0.8 无文献;0.01 单点几何 | CCS surveyor challenge | §2.5 calibration evidence register 表 + ASDR dispatch event payload 记录(F4 落地后)+ F5 sweep 后校准 |
| M6 encounter_state 算错 → M5 dispatch 错 | 中 | M6 phase 基于 tcpa_s 阈值;tcpa_s 来自 M2 linear CPA(可能 churn) | M5 在不该 dispatch 时 dispatch acatos → 失败 → consecutive_failures 兜底 | consecutive_failures counter 兜底;**BC-MPC Override 在 profile=gnc 下当前不兜底(dead letter,F2 F-C 修)**;M6 phase 错与 M5 dispatch 解耦 |
| 5 个判据组合产生未预期边界 | 中-高 | composite gate 边界 cases 多 | acatos 在未预期场景上场失败 | 12 标准 COLREGs 场景 SIL full sim + 每场景 acatos dispatch 计数 + sqp_iter + status |
| `MidMpcInput::rot_max_rad_s` default 0.2094 (12°/s) stale | 中 | types.hpp:242 stale default;GNC finding 5 | ODD 没收到时 C4 用 12°/s → 错误放行 | **C4 必须用 `kAcatosRotMaxDegS = 4.7` 常数 fail-safe,不接受 input.rot_max_rad_s 的 stale default**(B1 + GNC T-C4-DEFAULT-SENTINEL)|

### 4.2 不变量(byte-identical 保证)

1. consecutive_failures counter 递增/重置逻辑 byte-identical(M5 内部)
2. IPOPT fallback 路径 byte-identical
3. MidMpcSolution 11 字段 shape byte-identical
4. ROS2 wire format 零改动
5. BC-MPC Override **触发条件** byte-identical(独立路径)
6. M7 hard-CPA checker 独立路径 byte-identical
7. M6 publish 字段 + 算法 byte-identical(M5 只新增消费)
8. tail_builder::EncounterState enum 定义 byte-identical(v3.1 B6 复用,不新建)

### 4.3 ample-time 安全论证(AGENTS.md COLREGs rule,v3.1 修正)

v3.1 不是「为让 acatos 在 ho 上被测到而调阈值」。安全论证链:
1. **acatos 能力边界实测**:ZCode probe 证明 acatos 在 ho DCPA=0 + target-aligned 几何下先天失败(11/11 status=3)。这是 solver 物理能力边界,不是阈值能改的。
2. **v3.1 在 acatos 能力范围内 dispatch**:C1-C5 composite 等价于「只在 acatos 有合理收敛概率的几何上场」。符合 AGENTS.md "让 acatos 在它能力范围内被测"。
3. **不依赖测试通过**:即便 12 标准 COLREGs 场景 v3.1 后 acatos 仍 0 dispatch,v3.1 安全论证也成立 — 它修的是「dispatch 判据的工程正确性」,不是凑绿。
4. **多层防护实际状态(v3.1 修正,M7 F-B1 + cert B1 必修)**:
   - ✅ gate-1(warm-up)保留 — 但 warm-up 用 no-target 收敛**不能预测**带 target 行为(ZCode probe §2.1)
   - ✅ v3.1 composite gate(C1-C5)— 本 memo 新增
   - ✅ M5 `consecutive_failures_` counter byte-identical — 但只 BC-MPC 订阅
   - ⚠️ BC-MPC takeover from Mid-MPC(consecutive_failures ≥ 3)— 触发后 publish Override
   - ❌ **BC-MPC Override 在 profile=gnc 下是 dead letter**(F2 §1.2),2118 次 override 零效果。profile=fcb 下正常。**等 F2 F-C 修**
   - ❌ **M7 MRM-02 不直接消费 M5 consecutive_failures**(M7 F-B2)。spdlog::critical "M7 MRM-02 escalation" 是 audit log 字符串不是 trigger。M7 MRM 走独立 SOTIF/CPA-trend/watchdog 路径,acatos 失败 → CPA 恶化 → M7 cpa_trend_degrading → 间接可能 MRM-02(但未在 ho 上 SIL 实证)
   - ✅ M7 hard-CPA checker 独立工作(`hard_constraint_cpa.cpp`,M7 F-M3 核实)
   - **结论**:v3.1 后 ho 上 acatos 失败的实际兜底链 = M7 hard-CPA checker(独立 ✅)+ L4 stale route / GNC keep last(被动)。**M5 主推的「consecutive_failures + MRM-02」兜底实际不存在机械链路**。ho RED 必须靠 F2 修(IPOPT/TailGate/L4 preflight/BC-MPC Override consumer)。

---

## 5. 测试与验收(v3.1 修订,加 reviewer 必修测试)

### 5.1 单元测试(必跑)

新增 15 个测试 + 现有回归(v3.1 在 v3 的 T1-T9 基础上加 T10-T15):

| 测试 | 验证点 | 期望 | 来源 |
|---|---|---|---|
| **[T1]** C1 ample-time 边界 | tcpa=719s → fallback;tcpa=720s(精确)→ 通过 | 边界 PASS | v3 |
| **[T2]** C2 encounter_state | ACTIVE → fallback;RELEASE → fallback;CLEAR/ONSET → 通过;**has_m6_encounter_state=false → fallback(fail-closed)** | 4 状态 + fail-closed PASS | v3 + v3.1 B6 |
| **[T3]** C3 horizon gap 边界 | gap_h=199m → 通过;gap_h=200m(精确)→ 通过;gap_h=201m → fallback | 3 边界 PASS | v3 |
| **[T4]** C4 ROT reach 边界 | r_reach=0.79 → 通过;0.80(精确)→ 通过;0.81 → fallback | 3 边界 PASS | v3 |
| **[T5]** C5 alignment 边界 | rel_brg=0°(dead-ahead,sin=0)→ 挡;0.5°(sin=0.0087<0.01)→ 挡;0.6°(sin=0.0105>0.01)→ 通过 | 3 边界 PASS(v3.1 B5 用 0.01)| v3.1 B5 |
| **[T6]** composite ho | ho 几何(tcpa=1620, ACTIVE, gap=0, R_reach=0, dead-ahead)→ fallback;第一挡 C5(dead-ahead,t=0~291)→ 后续 C2(ACTIVE) | ho 全挡,fallback IPOPT | v3 |
| **[T7]** composite crossing ample-time | crossing 几何(tcpa=1500, ONSET, gap=0, R_reach=0.3, rel_brg=60°)→ acatos dispatch | crossing 放行 | v3 |
| **[T8]** NaN 防御 | tcpa=NaN target + tgt.x_m=NaN target + 另一正常 target | 不崩溃,正常判据;NaN targets skipped | v3 + code review S2 |
| **[T9]** empty targets | 0 targets → acatos dispatch(无威胁) | 通过 | v3 |
| **[T10]** v2-vs-v3.1 dispatch 行为差异 | 枚举 12 标准 COLREGs 场景 input,断言每个 dispatch outcome(acatos vs IPOPT);与 v2 行为对比 | 行为差异清单生成 | code review C3 |
| **[T11]** **T-MRM-1** MRM-02 触发链 SIL | ho 场景跑 SIL,断言 M5 consecutive_failures=11,spdlog critical 6 次,**M7 实际未发 MRM-02 alert**(M7 F-B2 实证)| RED 证据保留(作为 F-M7 follow-up baseline)| M7 BLOCKING |
| **[T12]** **T-MRM-2** BC-MPC Override consumer SIL | profile=gnc ho 场景,断言 BC-MPC publish reactive_override_cmd ≥1000 次,**own trajectory 不变**(dead letter)| RED 证据保留(作为 F2 F-C baseline)| M7 BLOCKING |
| **[T13]** **T-C4-ROT** ROT 源合约 | rot_max = {2.0, 3.5, 4.7, 12.0}°/s 参数化,断言 C4 用 `kAcatosRotMaxDegS=4.7`,**与 L4 preflight F-H 修复后的 ROT 源自同一常量** | 4 ROT 值 PASS | GNC B1 |
| **[T14]** **T-C4-DEFAULT-SENTINEL** stale default | `input.rot_max_rad_s = 0.2094 (12°/s)` stale default 时,**C4 不通过**(fail-closed,用 kAcatosRotMaxDegS 常数 fail-safe)| fail-closed PASS | GNC finding 5 |
| **[T15]** **T-C2-FSM** FSM mapping | encounter_state ∈ {CLEAR, ONSET, ACTIVE, RELEASE} 参数化;**ONSET 期 NLP 遵循 colregs_preferred_direction** | 4 状态 + preferred_direction 遵循 PASS | GNC B3 + M7 F-I1 |
| 现有 `test_mid_mpc_solver` 全套 | IPOPT 路径不动 | byte-identical PASS | regression |
| 现有 `test_mid_mpc_acados_solver` 全套(13 case) | acatos 行为本身不变 | 全 PASS | regression |

### 5.2 SIL 验收(阶段 3)

| 场景 | 期望 acatos dispatch | 验收 |
|---|---|---|
| `colreg-rule14-ho` | 0(C5 dead-ahead 挡 t=0~291;C2 ACTIVE 挡 t=291+) | ho RED 由 F2 修,**T-MRM-1 + T-MRM-2 RED 证据在此场景采集** |
| `colreg-rule15-cs`(crossing) | > 0(若 ample-time + crossing geometry 通过) | acatos dispatch 计数 > 0 + sqp_iter > 0 + status=0 |
| `colreg-rule13-ot` | > 0(若 ample-time) | 同上 |
| `colreg-rule17-cr-so` | 0(stand-on,M5 不主动) | acatos 不上场,BC-MPC 主导 |

### 5.3 验收门(AGENTS.md promotion rule)

1. 单测全 PASS(15 新 + 现有回归);**T-MRM-1 + T-MRM-2 显式以 RED 保留作为 F-M7 / F2 F-C 修复 baseline**
2. 4 reviewer 签字:**全部已签 APPROVE_WITH_CONDITIONS**(2026-07-19,M7 safety / GNC contract / code review / cert evidence)
3. **v3.1 + F2 F-H 同一 promotion gate**(B1 要求)
4. GNC 容器 12 标准 COLREGs 场景 SIL full sim
5. 证据路径:`runs/gate_v3_1_*`(trace + summary + dispatch 计数 + per-criterion reject counters)

---

## 6. 范围边界(v3.1 修订)

### 6.1 在 v3.1 范围内

| 项 | 状态 |
|---|---|
| M5 Mid-MPC dispatch gate(mid_mpc_solver.cpp:147-167)composite C1-C5 | ✅ 实施 |
| MidMpcInput 新增 encounter_state/has_m6_encounter_state/phase(B6 复用 tail_builder enum) | ✅ 实施 |
| **F2 F-H: L4 preflight ROT 3.5→4.7(gnc_avoidance_preflight.hpp:28)** | ✅ **同步实施(B1 BLOCKING,与 v3.1 同 promotion gate)** |
| **最小 dispatch decision spdlog info + summary.json dispatch counters(B4)** | ✅ 同步实施 |
| **F4 ASDR dispatch audit trail:扩展现有 `decision_json` payload 加 nlp_backend / dispatch_reason / per-criterion 结果**(用户 2026-07-19 决定 "直接本对话实施,不拖延") | ✅ **同步实施**(不改 ASDR msg schema,只扩 decision_json free-form JSON) |
| 15 个新单测(T1-T15) | ✅ 实施 |

### 6.2 不在 v3.1 范围(explicit out-of-scope)

| 项 | 状态 | 理由 |
|---|---|---|
| acatos solver opts 优化(FULL→PARTIAL CONDENSING / FUNNEL+adaptive LM) | F3 follow-up | 实时性不是阻塞(60s 预算);先把 v3.1 dispatch 链路跑通 |
| gate-1 warm-up 改造 | 不动 | ZCode probe 证明 warm-up 用 no-target 收敛不能预测带 target;改 warm-up 是 P5/P7 范围 |
| BC-MPC Override consumer(F2 F-C:gnc_bridge 订阅 reactive_override_cmd) | **F2 独立 work item** | cert B1 + GNC S1 + M7 F-B1 升级项;Red risk L3↔L4 契约改动,需 tdl_gnc_contract_reviewer 独立评审 |
| ho RED 完整修复(F2 F-A/F-B/F-C/F-D) | **F2 独立工作线** | ho RED 跟 acatos dispatch 正交;**只有 F-H 与 v3.1 同 promotion gate** |
| ASDR 加 M5 solver identity wire event(F4 ASDR wire) | **✅ 已拉入 v3.1 范围**(用户 2026-07-19 决定) | 通过扩展现有 `decision_json` payload 实现,不改 ASDR msg schema,RFC-004 Resolution #6 不阻塞 |
| M7 直接消费 M5 consecutive_failures(F-M7) | F-M7 follow-up | M7 F-B2 发现;独立 M7 改造,需 tdl_m7_safety_reviewer + M7 maintainer 评估 |
| ODD-aware C1 floor(ODD-B 240s / ODD-D 300s) | 未来工作 | M7 要求明示;v3.1 ODD scope = ODD-A only |
| 架构 [R17] 引用勘误(§3.3 + §9.3 + §9.4) | **独立 PR**(cert S1) | 不阻塞 v3.1;架构 owner 单独处理 |
| ROS2 IDL/topic 改动 | 零 | M5 内部消费 M6 已有字段 |
| L4 executability 契约改动(MidMpcSolution shape) | 零 | byte-identical |
| acatos 验收场景集建立 + factorial sweep | F5 follow-up | v3.1 后才知道 acatos 能在哪些场景上场;**sweep 是 0.01/0.8/200m 阈值校准的前提** |
| SOTIF 性能限制登记(ho acatos 先天失败) | **escalation to safety engineer**(cert S4) | 新建 `docs/Design/Safety/SOTIF/` 或扩展 D3.3b;不阻塞 v3.1 |

---

## 7. 工作分解(v3.1 实施)

| 阶段 | 任务 | 工作量 | 依赖 |
|---|---|---|---|
| 7.1 | MidMpcInput 新增 `colregs_encounter_state` + `has_m6_encounter_state` + `colregs_phase`(types.hpp + assemble_input_,**B6 复用 tail_builder::EncounterState**) | 0.5 天 | 无 |
| 7.2 | `compute_acatos_feasibility()` 5 判据实现(mid_mpc_solver.hpp + .cpp),含 §2.1.1 horizon_projected_cpa_gap + rot_reach_ratio(用 kAcatosRotMaxDegS=4.7 fail-safe) | 2 天 | 7.1 |
| 7.3 | 替换 `compute_bc_mpc_territory` 调用为 `compute_acatos_feasibility`(mid_mpc_solver.cpp:147)+ 删除 kAcadosCpaGateMultiplier | 0.5 天 | 7.2 |
| 7.4 | **F2 F-H: L4 preflight 3.5→4.7(gnc_avoidance_preflight.hpp:28 + cfg builder)** | 0.3 天 | 独立,但同 promotion gate |
| 7.5 | **B4 最小 dispatch decision spdlog info + summary.json dispatch counters** | 0.5 天 | 7.3 |
| 7.6 | **F4 ASDR dispatch audit trail**:扩展现有 `decision_json` payload 加 `nlp_backend`(acatos/ipopt)+ `dispatch_reason`(all_pass / c1_ample_time_fail / c2_active / ...)+ per-criterion booleans(`c1_pass / c2_state / c3_gap_h / c4_r_reach / c5_align`)+ dispatch counter。**不改 ASDR msg schema,只扩 JSON** | 0.5 天 | 7.3 |
| 7.7 | 单测 T1-T15(test_mid_mpc_solver.cpp,含 v2-vs-v3.1 行为差异 + FSM + ROT + sentinel + ASDR payload 校验) | 2 天 | 7.3 + 7.4 + 7.6 |
| 7.8 | **T-MRM-1 + T-MRM-2 SIL RED 证据采集**(ho 场景跑 SIL,保留 RED 作为 F-M7/F2 F-C baseline) | 0.5 天 | 7.7 |
| 7.9 | 12 标准 COLREGs SIL full sim 验收(acatos dispatch 计数 + status + ASDR dispatch audit) | 0.5 天 | 7.8 |
| 7.10 | 文档 + handoff + commit | 0.5 天 | 7.9 |
| **合计** | | **7.8 天** | (v3 是 6 天;v3.1 加 F-H + B4 log + F4 ASDR payload + 多个新测试) |

---

## 8. v3.1 决策状态(全部已拍板)

**6 设计决策(用户 2026-07-19 拍板)**:

| # | 决策 | 选择 | 影响 |
|---|---|---|---|
| **B1** | C4 ROT 源 | **4.7°/s ship_config + 同步 F2 F-H** | §2.6;v3.1 实施必须同步 F2 F-H |
| **B2** | C2 PREPLAN | **ONSET 放行 + NLP 遵循 colregs_preferred_direction** | §2.7;依赖 NLP 已有 cost term |
| **B3** | C4 T_reserve | **60s(M6 t_emergency_s)** | §2.5 kAcatosRotReachReserveS |
| **B4** | F4 audit trail | **同步加最小 spdlog info + summary.json counters** | §3 + §6.1;F4 ASDR wire 独立 work item |
| **B5** | C5 cone | **0.01 极严 + 等 F5 sweep** | §2.5 kAcatosAlignSinEpsilon;C5 几乎是 no-op 除精确 dead-ahead |
| **B6** | EncounterState enum | **复用 tail_builder::EncounterState + bool flag** | §2.4;避免 sentinel=3 vs RELEASE=3 冲突 |

**4 reviewer 评审结果(2026-07-19,全部 APPROVE_WITH_CONDITIONS)**:

| Reviewer | 必修项 | v3.1 处理 |
|---|---|---|
| code | C1 T_reserve 未定义 / C2 horizon_gap pseudocode 缺 / C3 IPOPT behavioral regression test 缺 | ✅ B3 赋值 60s + §2.1.1 加 pseudocode + T10 加 v2-vs-v3.1 行为差异测试 |
| cert | B1 §4.3.4 BC-MPC Override profile=gnc dead letter 必须限定 / B2 F4 必须有 owner+deadline + 加最小 dispatch log | ✅ §3 + §4.3 + §6 多处限定 + B4 加最小 log + §6.2 F4 owner TBD by user |
| M7 safety | F-B1 §4.3.4 多层防护论证错 / F-B2 M7 MRM-02 不直接消费 consecutive_failures / F-I1 EncounterState enum 双消费一致性 | ✅ §3 + §4.3 修正 MRM-02 触发链 + B6 复用 enum + T15 FSM 测试 + T-MRM-1/T-MRM-2 RED 证据 |
| GNC contract | B1 C4 rot_max 源不确定 / B2 C3 不能作 L4 executability / B3 ENCOUNTER_ONSET 合并 PREPLAN | ✅ B1 锁 4.7 + F2 F-H + B2 NLP 遵循 preferred_direction + §2.2/§4.1 标 C3 dispatch heuristic |

**仍待用户拍板**(实施前):

1. **本 v3.1 spec 是否接受**:接受后进 7.1-7.9 实施。
2. **F4 ASDR wire integration 的 owner + deadline**(cert B2 要求):v3.1 加最小 spdlog/counters 是 in-scope,但 ASDR wire 上报需要 RFC-004 Resolution #6 跨团队决议。建议用户指定 F4 owner + 日期(例如 owner=TBD, deadline=2026-08-15)。
3. **F2 完整修复(F-A/F-B/F-C/F-D)的优先级与 v3.1 实施的并行/串行**:F2 F-H 必须 v3.1 同步,但 F-A/F-B/F-C/F-D 是独立工作线(可并行)。

**escalation 给 TDL Lead / 用户**(4 reviewer 升级项,不在 v3.1 范围):

1. **F2 F-C**(gnc_bridge 订阅 reactive_override_cmd + L3↔L4 契约 + ASDR audit)→ SIL2 H-05/H-11 audit 阻塞项,需 `tdl_gnc_contract_reviewer` 独立评审
2. **架构 [R17] 勘误**(§3.3 + §9.3 + §9.4 Wang 2021 引用错误)→ 独立 PR,架构 owner 处理
3. **SOTIF 性能限制登记**(acatos ho 先天失败)→ 新建 `docs/Design/Safety/SOTIF/` 或扩展 D3.3b,safety engineer 处理
4. **RFC-004 Resolution #6**(ASDR 加 M5/M3 mandatory event)→ ASDR team + L3 architect 跨团队
5. **F-M7**(M7 直接消费 M5 consecutive_failures)→ M7 maintainer + tdl_m7_safety_reviewer 评估 AssumptionMonitor 改造

---

## 附录 A:证据溯源

| 编号 | 类型 | 来源 |
|---|---|---|
| `[PF-1]` | PROJECT_FACT | `docs/superpowers/specs/2026-07-19-m5-acatos-ho-dynamic-convergence-probe.md`(ZCode 短路实验) |
| `[PF-2]` | PROJECT_FACT | `runs/acatos_ho_probe/`(完整 trace + summary) |
| `[PF-3]` | PROJECT_FACT | `mid_mpc_solver.cpp:147-167` 当前 gate-2 v2 实现 |
| `[PF-4]` | PROJECT_FACT | `mid_mpc_node.cpp:633-637` cpa_safe 升 2500 |
| `[PF-5]` | PROJECT_FACT | `l3_msgs/msg/COLREGsConstraint.msg:1-29` M6 encounter lifecycle 字段 |
| `[PF-6]` | PROJECT_FACT | `m6_colregs_reasoner/config/odd_aware_thresholds.yaml` ODD-A 阈值 |
| `[PF-7]` | PROJECT_FACT | `m6_colregs_reasoner/src/colregs_phase_classifier.cpp` phase 分类实现 |
| `[PF-8]` | PROJECT_FACT | `m6_colregs_reasoner/src/encounter_state_machine.cpp` FSM 实现 |
| `[DE-1]` | DOMAIN_EVIDENCE | `docs/superpowers/research/2026-07-19-colregs-mpc-dispatch-threshold-literature.md`(codex 文献调研,Eriksen/Thyri/Johansen 等同行评审) |
| `[DE-2]` | DOMAIN_EVIDENCE | `docs/superpowers/specs/2026-07-18-m5-p5-acados-convergence-design.md` §2/§5(P5 acatos 收敛边界) |
| `[DE-3]` | DOMAIN_EVIDENCE | Eriksen et al. 2019 BC-MPC(branching-course 解 pure reciprocal head-on) |

## 附录 B:v2 → v3 改动总览

| 文件 | v2 | v3 | 改动类型 |
|---|---|---|---|
| `types.hpp::MidMpcInput` | 无 encounter_state/phase | 新增 2 field | 加字段 |
| `mid_mpc_node.cpp::assemble_input_` | 不读 M6 encounter_state/phase | 新增 2 行读入 | 加消费 |
| `mid_mpc_solver.hpp` | `compute_bc_mpc_territory` + `kAcadosCpaGateMultiplier` | 删除 + 新增 `compute_acatos_feasibility` + 4 个 PROJECT_EMPIRICAL 常数 | 替换函数 |
| `mid_mpc_solver.cpp:147-167` | 调用 `compute_bc_mpc_territory` | 调用 `compute_acatos_feasibility` | 替换调用 |
| `test_mid_mpc_solver.cpp` | 现有 IPOPT/consecutive_failures 测试 | 现有 + 新增 T1-T9 composite gate 测试 | 加测试 |
| ROS2 IDL / topic | 不动 | 不动 | 零改动 |
| M6 / BC-MPC / M7 / L4 / M8 | 不动 | 不动 | 零改动 |
