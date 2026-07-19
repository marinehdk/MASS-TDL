# M5 acados dispatch gate-2 改造 — 安全论证 memo

> **产出**: 2026-07-19(v1) → 2026-07-19(v2,吸收 3 reviewer 反馈)
> **工作树**: `.worktrees/m5-design-grounding`(分支 `codex/m5-design-grounding` @ `420dad9bd`)
> **触发**: §9.4 G7 dispatch 治理任务;本 memo 在任何代码改动**之前**产出,供用户 + 独立 reviewer 评审。
> **范围**: 仅 M5 Mid-MPC `mid_mpc_solver.cpp:133-167` 的 acados dispatch gate-2 改造。**不**改 solver opts、**不**改 warm-up、**不**改 ROS2 接口。
> **硬约束**: AGENTS.md COLREGs full-chain debugging rule —— 禁止"调阈值/场景几何/scorer gate 只为了让单 probe 绿"。本 memo 必须证明 gate 改动是基于 ample-time 工程语义,而非"让 acados 被测到"。
> **v2 修订来源**: M7 safety review(APPROVE-WITH-CONDITIONS)+ GNC contract review(APPROVE-WITH-CONDITIONS)+ code review(APPROVE-WITH-CONDITIONS)。综合结论见 §0.5。

---

## 0. TL;DR

| 项 | 结论 |
|---|---|
| gate-2 当前判据 | `tgt.tcpa_s < 2000.0 && tgt.cpa_m < cpa_safe_m*2.0`(line 139-140) |
| 问题 | TCPA 阈值 2000s = 33.3 min,超出 ample-time 文献上限(20min/1200s)800s,超出 ODD-A ample-time floor(12min/720s)1280s;12 个标准 COLREGs 场景的初始 TCPA(666-1659s)**全部被挡**,acados 在整个测试套件实质避让期 **0 次触达** |
| 根因(语义) | gate-2 用 TCPA 近似"近距",但近距的工程判据是 **CPA**(BC-MPC 已用 `worst_case_cpa < cpa_safe × override_multiplier` = 1852m 做近距接管);TCPA 与近距仅在特定相对速度下相关,高速大 DCPA 目标会被误判 |
| 推荐改造 | gate-2 判据改为 `tgt.cpa_m < cpa_safe_m × override_multiplier`(= 1852m),与 BC-MPC 接管边界对齐;同时修 dispatch 日志 bug(line 164-166 移进 `if (warm_up_succeeded()) {}` 块) |
| COLREGs 影响 | acados 在 ample-time 窗口(TCPA ≥ ample-time floor,CPA ≥ 1852m)可被触达,让 P0-P7 的 OU/UT/intent 在标准场景里可被验证;BC-MPC 仍是 < 1852m 紧急避碰层,职责边界**更清晰**而非模糊 |
| M5/M7 影响 | M5 dispatch 行为改变但生命周期/状态机不变;`consecutive_failures_` + MRM-02 escalation 路径 byte-identical;M7 veto 路径不变 |
| 实时性 | **不是阻塞**。Mid-MPC 60s replan(VR-06 Eriksen 实测),acados 稳态 3s/solve 占 5% 预算。§9.4 的"1Hz 阻塞"基于错误前提。 |
| 风险等级 | 中。改的是 dispatch 路径选择,不改求解逻辑/约束/接口。需独立 M7 safety + GNC contract + code review。 |
| 验收 | dispatch 决策测试(CPA 边界 ±1m)+ MRM-02 回归 + GNC 容器 ho/cs SIL(full sim,acados dispatch 计数 + KPI) |

---

## 0.5 Reviewer 综合结论(v2,3 个独立 reviewer 并行评审后修订)

3 个独立 read-only reviewer(M7 safety / GNC contract / code review)并行评审 v1 memo,全部 **APPROVE-WITH-CONDITIONS**。综合后的修订:

| Reviewer | Verdict | 关键发现 | memo v2 修订 |
|---|---|---|---|
| **M7 safety** | APPROVE-WITH-CONDITIONS | **F-1(必修)**:acatos dispatch 路径的 consecutive_failures + MRM-02 escalation 测试覆盖为零(`test_mid_mpc_solver` 现有测试只跑 IPOPT 路径)。N-1:multiplier 配置漂移监测建议。N-3:`last_minalt_box_infeasible_` 在 acatos 成功路径 stale(pre-existing,不在本范围)| §5.1 新增 T3(acatos 失败 → counter + MRM-02)、T4(单周期不重试 IPOPT)|
| **GNC contract** | APPROVE-WITH-CONDITIONS | **§4.1 重大错误**:memo 说"acatos 失败走 IPOPT fallback",实际一旦 dispatch(line 146)就 return,本周期**不重试 IPOPT**,downstream 处理非 Converged via 空计划 heartbeat + 3 周期 BC-MPC takeover。§4.2 字段列表只列 6 个(实际 11 个)。parity test 只覆盖直线无目标(close-quarters 未证实)| §4.1 改写、§4.2 列全 11 字段、§3 L4 行加 parity scope caveat、§5.1 新增 T4 |
| **code review** | APPROVE-WITH-CONDITIONS | **必修 1**:§2.2 snippet 只用注释说要修日志 bug,但代码结构没改;implementer 必须显式加 `else` 子句或重构 brace。**必修 2**:`kAcadosCpaGateMultiplier` 加跨引用注释指向 BC-MPC config。**必修 3**:边界精确测试 CPA=1852.0m(BC-MPC 用 `>=`,gate-2 用 `<`,语义一致)| §2.2 显式 `else` 子句、跨引用注释、§5.1 T1 含 1852.0m 精确边界 |

**3 个 reviewer 一致结论**:memo 的核心安全论证(CPA-based dispatch gate 修正 TCPA 语义错位,不削弱 4 层安全网)站得住,**未发现 blocking issue**。所有发现都是 memo 准确性 / 测试覆盖 / 代码可维护性层面的改进,v2 已全部吸收。

**仍待 TDL Lead(我)/用户决策的升级项**:
1. **L4 dual-publish arbitration**(GNC reviewer 升级项):若 M2 线性 CPA 与 BC-MPC trajectory CPA 在 1852m 边界数值分歧,可能 M5 committed_route(acatos-derived)与 BC-MPC Override 同时发布。**待确认 L4 仲裁规则**(L4 偏向 BC-MPC Override 还是 M5 committed_route)。这是 L4 侧问题,不在 M5 authority 内,但影响 gate-2 改造的边界正确性论证。**建议:阶段 3 SIL 验证时实测,若发现 dual-publish,L4 单独处理**。
2. **dispatch metric 上报**(M7 reviewer 建议,用户已决定"暂不加"):memo 接受用户决策,靠 spdlog warn 计数。M7 reviewer 指出这削弱 ASDR 对 acatos dispatch/fallback 事件的审计能力 —— 留作 follow-up。

---

## 1. 证据基线(代码 + 文献,全部 PROJECT_FACT / DOMAIN_EVIDENCE)

### 1.1 §9.4 两处错误结论(本次诊断推翻)

**错误 #1 — gate-1 warm_up_succeeded=false**:

§9.4 声称"生产节点 cold-capsule warm-up 未收敛"是 acados 0 次触达的两个根因之一。

**真实证据**(`codex-m5-p3-sil-nodes-1` 容器实测,ho run 2026-07-18 18:02-18:08 UTC):
- `cold-capsule warm-up did not converge after N solves` 这是 warm-up 失败时**唯一**会打的 ctor 警告(`mid_mpc_acados_solver.cpp:423-428`)。在整个容器日志里 **0 条** → ctor warm-up **确实收敛了** → `warm_up_succeeded_ = true`。
- ho run 前 3 个周期(target 远、实时 TCPA ≥ 2000s):acados **被 dispatch** 了 3 次,返回 `status=1 (acatos=1) sqp_iter=0`(SQP max_iter hit)。**这是 acados solve 自身的问题,不是 warm-up gate 挡的** —— gate-1 已经放行。
- 18:06:50 起的 18 个周期(target 接近 CPA、TCPA < 2000s):**gate-2 short-TCPA 触发**,18× `short TCPA (<2000s) detected`。

**误诊根因**:`mid_mpc_solver.cpp:164-166` 的 `warm-up did not converge` warn 在 `if (warm_up_succeeded()) {}` **块外**,所以无论 warm-up 是否成功,只要走到 line 164 就会打。当 warm-up 成功 + short-TCPA 命中时,**两条 warn 同时打**,误导 §9.4 的归因。**这是 dispatch 日志 bug,不是行为 bug**(fallback 行为正确)。

**错误 #2 — 1Hz 实时性阻塞**:

§9.4 声称"12.5s/solve 超 1Hz M5 循环实时预算",即便 dispatch 触达也跑不动。

**真实证据**:
- `mid_mpc_node.cpp:481`:`solve_timer_ = create_timer(..., std::chrono::seconds(60), ...)` —— **Mid-MPC replan 周期 = 60s**,不是 1Hz。注释原文:"P4 VR-06b: 60s replan (was 1s; 1Hz chattering with radar/AIS micro-noise, 60s opens wide waters ample-time, BC-MPC handles emergencies)"。
- 60s 出自 VR-06(依据 Eriksen 实测,见 sess_a3ceed81 P0-P7 设计 session)。
- acados profiling(见 §1.3):warm-up 后稳态 3s/solve,占 60s 预算的 **5%**。cold-capsule warm-up 24s 只在 ctor 一次性发生,**不进生产 replan 周期**。
- "12.5s/solve" 是 cold 24s + warm 9.7s + real 3s 三个连续 solve 的算术平均,不是生产稳态。

**因此 §9.4 的"G7 实时性能次生阻塞"判断无效**。本 memo 只处理 gate-2。

### 1.2 gate-2 与 12 标准 COLREGs 场景先天不匹配(独立复算)

用 `tools/sil/colregs_scenario_audit.py::_straight_line_cpa` 独立重算 12 个场景的初始直线 CPA 几何:

| 场景类 | 场景数 | TCPA 范围 | gate-2(2000s) | DCPA 范围 | BC-MPC 阈值(1852m) |
|---|---|---|---|---|---|
| rule14-ho 族 | 3 | 1620-1626s | ❌ 全挡 | 0-0m | ❌ 全挡(初始就在 BC-MPC 管辖,但本船未动) |
| rule15-cs 族 | 4 | 840-1659s | ❌ 全挡 | 0-2.3m | ❌ 全挡 |
| rule13-ot 族 | 2 | 666-1575s | ❌ 全挡 | 0-100m | ❌ 全挡 |
| rule17-cr-so 族 | 2 | 1002s | ❌ 全挡 | 0m | ❌ 全挡 |

**关键观察**:
1. **所有 12 场景的初始 TCPA < 2000s**(666-1659s) → gate-2 从 t=0 起就在多数周期触发。
2. **所有 12 场景的初始 DCPA < 1852m**(0-100m) → 即便用 BC-MPC 边界(1852m)做 gate,初始几何也会被挡。
3. 这意味着 12 个标准场景的初始几何设计本就是"**已经进入实质避让期**"(DCPA ≈ 0,TCPA ample-time 不足)。

**这与架构设计报告 §3.3 ODD-A ample-time 定义一致**:`CPA ≥ 1.0nm, TCPA ≥ 12min`。12 个场景的初始 TCPA 全部 < 27 min,DCPA 全部 ≈ 0,**没有一个落在"监测期"(TCPA > ample-time 上限,CPA 远)** —— 它们都是测避让行为的,不是测监测的。

**推论**:gate-2 的设计意图("acados staging not validated for close-quarters → fallback IPOPT")与标准 COLREGs 测试套件的几何设计**根本性冲突** —— 测试套件本身就是 close-quarters。这不是"阈值要调",是"判据语义错位"。

### 1.3 ample-time 工程语义(DOMAIN_EVIDENCE + PROJECT_FACT)

**海事 ample-time 文献 TCPA 范围**(架构设计报告 §3.3 line 265 引用):
- Wang et al. 2021 [R17] + Frontiers 2021 综述 [R2]:TCPA **5–20 min(300–1200s)**

**本仓库 ODD ample-time 设计阈值**(架构设计报告 §3.3 line 260-263):

| ODD 子域 | CPA 阈值 | TCPA ample-time floor |
|---|---|---|
| ODD-A 开阔水域 | ≥ 1.0nm (1852m) | **≥ 12 min (720s)** |
| ODD-B 狭水道/VTS | ≥ 0.3nm | ≥ 4 min (240s) |
| ODD-D 能见度不良 | × 1.5 | × 1.5 (18 min) |

**gate-2 阈值对照**:
- gate-2 当前 = 2000s = **33.3 min**
- vs ample-time 文献上限 20 min = 1200s → gate-2 超出 **800s**
- vs ODD-A ample-time floor 12 min = 720s → gate-2 超出 **1280s**
- vs ODD-B ample-time floor 4 min = 240s → gate-2 超出 **1760s**

**结论**:gate-2 的 TCPA 阈值比 ample-time 文献上限还严格 33%,比本仓库 ODD-A 设计 ample-time floor 严格 2.78 倍。**它把 acados 锁在了"目标船还未进入监测期"的早期窗口,而 COLREGs 实质避让都发生在 TCPA < ample-time 上限的窗口内**。这正是用户指出的"先天不可实现场景成为评价指标"的硬证据。

### 1.4 BC-MPC 真实接管边界(PROJECT_FACT,代码实测)

**触发条件**(`bc_mpc_collision_detector.cpp:100-114`):
```cpp
const double threshold = input.cpa_safe_m * formulation_.config().override_cpa_multiplier;
sol.status = (best_cpa >= threshold) ? Status::Resolved : Status::Override;
```

**配置**:
- `bc_mpc_branch_formulation.hpp:45`:`override_cpa_multiplier{0.8}`(默认)
- `config/m5_params.yaml:14`:`override_cpa_multiplier: 1.0`(实际配置)
- `cpa_safe_m = 1852.0`(`mid_mpc_node.cpp:47` `kCpaSafeFallback_m`)
- **BC-MPC Override 触发 CPA = 1852 × 1.0 = 1852m(1nm)**

**语义**:`best_cpa` 是 `worst_case_cpa_m` = 基于 trajectory 的 minimax CPA(对所有 candidate heading 求最大最小 CPA,`bc_mpc_collision_detector.cpp:49-70`),不是线性 CPA。但触发阈值仍是 CPA-metric,**不是 TCPA**。

**BC-MPC replan 频率**:事件驱动(on_world_state_ 回调),M2 world_state publish 频率 = `aggregation_rate_hz = 4.0 Hz`(`m2_params.yaml:3`)→ BC-MPC 实际 4Hz(250ms 周期)。`kTickInterval_s = 0.1`(`bc_mpc_node.cpp:18`)只是 validity_timer tick,用于 health 检查和 remaining_validity 递减,**不是 solve 频率**。

**Mid-MPC 侧的 BC-MPC handover**(`mid_mpc_node.cpp:873-885`):
```cpp
constexpr int64_t kBcMpcTakeoverThreshold = 3;
const bool bc_mpc_should_take_over = compute_bc_mpc_take_over(
    solver_.consecutive_failures(), kBcMpcTakeoverThreshold,
    solver_.last_minalt_box_infeasible(), input.speed_gap_infeasible);
```
Mid-MPC 的 handover 触发是 `consecutive_failures ≥ 3 OR box_infeasible OR speed_infeasible`,**不是 CPA 阈值**。这是与 BC-MPC 自身 takeover 不同的第二层信号。

### 1.5 acados profiling(PROJECT_FACT,容器实测)

chrono-wrap `MidMpcAcadosSolver::solve()` 各阶段 + acados `time_*` 内部字段,3 次连续 solve(AmpleTime 单测):

| Solve | status | sqp_iter | wall | time_qp_call | qp_xcond | lin | reg | sim | glob | per_iter |
|---|---|---|---|---|---|---|---|---|---|---|
| warm #1 (cold) | 2 Infeasible | 400 (max) | 24077ms | **23673ms (98%)** | 89ms | 71ms | 0.1ms | 0ms | 237ms | 60.2ms |
| warm #2 | 0 Conv | 162 | 9726ms | 9564ms (98%) | 36ms | 29ms | 0ms | 0ms | 93ms | 60.0ms |
| real | 0 Conv | **50** | **3006ms** | 2960ms (98%) | 11ms | 9ms | 0ms | 0ms | 26ms | **60.1ms** |

**关键发现**:
- 98% 时间在 `time_qp_solver_call`(HPIPM QP condense + solve)
- per SQP iter 稳定 60ms,与 solve history 无关 → 瓶颈是单个 QP 求解
- warm 稳态 3s/solve(50 iter),占 60s Mid-MPC 预算 **5%**
- codegen 配置(`acados_ocp_m5_mid_mpc_acados.json` 实测):`nlp_solver_type = SQP`(不是 RTI),`qp_solver = FULL_CONDENSING_HPIPM`,`qp_solver_cond_N = 80`(全 horizon condense),`hessian_approx = EXACT`

**对 gate-2 改造的影响**:实时性**不是** gate-2 改造的约束。warm-up 后稳态 solve 在 Mid-MPC 60s 预算内绰绰有余。QP 优化(FULL→PARTIAL CONDENSING)是独立优化项,留待链路跑通后单独决策。

---

## 2. gate-2 改造方案(CPA 判据,对齐 BC-MPC 边界)

### 2.1 当前代码(`mid_mpc_solver.cpp:133-167`)

```cpp
if (acados_solver_->warm_up_succeeded()) {              // gate-1: 实测未触发(warm-up 收敛)
  bool short_tcpa = false;                              // gate-2: 本 memo 改造对象
  for (const auto& tgt : input.targets) {
    if (std::isfinite(tgt.tcpa_s) && tgt.tcpa_s < 2000.0 &&
        std::isfinite(tgt.cpa_m) && tgt.cpa_m < input.constraints.cpa_safe_m * 2.0) {
      short_tcpa = true;
      break;
    }
  }
  if (!short_tcpa) {
    MidMpcSolution sol = acados_solver_->solve(input, warm_start);
    // ... consecutive_failures_ + MRM-02 escalation (不变)
    return sol;
  }
  spdlog::warn("[M5][MidMPC] short TCPA (<2000s) detected ...");  // (line 161-162)
}
spdlog::warn("[M5][MidMPC] acados backend installed but warm-up did not "
             "converge ...");                                    // (line 164-166, 日志 BUG)
```

### 2.2 改造后(拟)

```cpp
if (acados_solver_->warm_up_succeeded()) {              // gate-1: 不变
  // gate-2(改造):用 CPA 判据对齐 BC-MPC Override 边界(cpa_safe × override_multiplier)。
  // 当任一 target 的预测 CPA < BC-MPC 接管边界时,本周期 fallback IPOPT,
  // 让 BC-MPC 反应层(4Hz,基于 trajectory 的 worst_case_cpa)主导紧急避碰;
  // 否则 acados 在 ample-time 窗口内执行战术 replan。
  //
  // 注意:这是 **dispatch gate**(选 backend),不是 takeover 决策。gate-2 用 M2 的
  // 线性 tgt.cpa_m,BC-MPC Override 用 trajectory minimax worst_case_cpa,两者动态几何
  // 下可差数百米(M7/GNC reviewer 升级项)。M7 hard-constraint CPA checker 是独立的
  // 真实 backstop,不依赖本 gate。
  //
  // 替换理由(见 safety memo §1.3-1.4):
  //   - 旧 TCPA<2000s 判据超出 ample-time 文献上限(20min)800s,与 ODD-A ample-time
  //     floor(12min)冲突 1280s,让 acados 在标准 COLREGs 测试套件实质避让期 0 次触达。
  //   - 近距的工程判据是 CPA,不是 TCPA(BC-MPC 已用 worst_case_cpa < cpa_safe ×
  //     override_multiplier 做接管)。TCPA 与近距仅在特定相对速度下相关。
  //   - 与 BC-MPC 边界对齐后,职责清晰:CPA ≥ 1852m → acados 战术;CPA < 1852m →
  //     BC-MPC 反应层,Mid-MPC 该周期 fallback IPOPT 兜底(不抢 BC-MPC 的主导权)。
  //
  // kAcadosCpaGateMultiplier 必须与 BC-MPC override_cpa_multiplier 保持一致
  // (bc_mpc_branch_formulation.hpp:45 默认=0.8;m5_params.yaml:14 配置=1.0)。
  // 当前硬编码 1.0;未来若 BC-MPC multiplier 改 config,本处需同步(或走同一 config 源)。
  constexpr double kAcadosCpaGateMultiplier = 1.0;
  const double cpa_gate_m = input.constraints.cpa_safe_m * kAcadosCpaGateMultiplier;
  bool bc_mpc_territory = false;
  for (const auto& tgt : input.targets) {
    if (std::isfinite(tgt.cpa_m) && tgt.cpa_m < cpa_gate_m) {
      bc_mpc_territory = true;
      break;
    }
  }
  if (!bc_mpc_territory) {
    MidMpcSolution sol = acados_solver_->solve(input, warm_start);
    // I-2 (P4 T7): S2 escalation counter — 不变(line 147-158 byte-identical)
    if (sol.status != MidMpcSolution::Status::Converged &&
        sol.status != MidMpcSolution::Status::NotInitialized) {
      ++consecutive_failures_;
      if (consecutive_failures_ > kConsecutiveFailureEscalation) {
        spdlog::critical("[M5][MidMPC][acados] {} consecutive failures; M7 MRM-02 escalation",
                         consecutive_failures_);
      }
    } else {
      consecutive_failures_ = 0;
    }
    return sol;
  }
  spdlog::warn("[M5][MidMPC] target CPA < {:.0f} m (BC-MPC territory) — "
               "acados dispatch skipped, falling back to IPOPT.", cpa_gate_m);
} else {
  // 日志 BUG 修复(TDL-code-reviewer 必修项):原 line 164-166 在 if 块外,
  // 导致 gate-2 命中时同时误打"warm-up did not converge"。现移进 else 子句,
  // 仅在 warm_up_succeeded() == false 时打。
  spdlog::warn("[M5][MidMPC] acados backend installed but warm-up did not "
               "converge (warm_up_succeeded=false); falling back to IPOPT for "
               "this cycle.");
}
```

**关键结构变化(对照当前代码 line 133-167)**:
- 当前:`if (warm_up_succeeded()) { gate-2 }` + 块外无条件 warn(line 164-166)
- 改造后:`if (warm_up_succeeded()) { gate-2 } else { warn }` —— warn 进 else 子句,语义正确
- gate-2 内部 `if (!bc_mpc_territory) { dispatch + counter + return }` byte-identical 保留
- consecutive_failures_ + MRM-02 escalation 块 byte-identical 保留

### 2.3 关键设计决策(逐项论证)

| 决策 | 选择 | 理由 | 风险 |
|---|---|---|---|
| **判据类型** | CPA(linear,M2 提供)| 近距的工程语义;BC-MPC 已用 CPA 做接管;COLREG Rule 8 ample-time + Rule 6 safe-speed 都基于距离余量 | 线性 CPA vs BC-MPC 的 trajectory CPA 在动态几何下有差异,但作为 dispatch gate(非 takeover 决策)够用 |
| **阈值倍数** | 1.0(= override_cpa_multiplier 配置值)| 与 BC-MPC Override 边界 byte-aligned;避免出现"acatos 上场但 BC-MPC 同时接管"的职责模糊区 | 倍数低于 1.0 会让 acatos 进 BC-MPC 管辖区,职责模糊;高于 1.0 会让 acatos 在 ample-time 窗口外被挡,重蹈 TCPA 覆辙。**注意**(GNC/M7 reviewer):gate-2 用 M2 线性 CPA,BC-MPC 用 trajectory minimax CPA,动态几何下两者可差数百米;本对齐是 **dispatch gate** 对齐,不是 **takeover decision** 对齐 |
| **阈值是否参数化** | 是(走 cpa_safe × multiplier,不硬编码 m)| cpa_safe 已是 input 字段,multiplier 走 config(未来可移到 m5_params.yaml 与 override_cpa_multiplier 同源) | 当前先硬编码 1.0,与 override_cpa_multiplier 解耦,避免依赖 BC-MPC 配置加载顺序 |
| **warm-up gate-1 是否保留** | 保留 | gate-1 是 T17 review-fix 的安全网,即便实测不触发也应保留(cold-capsule 真坏时仍能挡) | 零行为变化(实测 warm-up 收敛),只是日志 bug 修掉 |
| **dispatch 日志 bug 是否同修** | 是(line 164-166 移进 if 块)| 这是 §9.4 误诊的直接根因,不修将来还会误导 | 零行为变化,纯日志 |

---

## 3. COLREGs 全链影响(AGENTS.md mandatory)

按 AGENTS.md COLREGs full-chain debugging rule,逐 stage 分析:

| Stage | 改造前行为 | 改造后行为 | 影响 |
|---|---|---|---|
| **L2 route/speed** | 不变(60s replan,VR-06) | 不变 | 无 |
| **M2 world/CPA/geometry** | M2 提供 `tgt.cpa_m`(linear CPA)和 `tgt.tcpa_s` | 不变;gate-2 改读 cpa_m 而非 tcpa_s | 无(cpa_m 字段早已存在) |
| **M6 rule/role/direction/release** | 不变 | 不变 | 无 |
| **M4 behavior FSM** | 不变 | 不变 | 无 |
| **M5 trajectory/status** | acados 0 次触达 → IPOPT fallback → GeoFallback → 4/5 EMPTY plan | acados 在 ample-time 窗口被触达 → 收敛则输出 acados trajectory;不收敛仍走 IPOPT fallback | **核心改变**。P0-P7 OU/UT/intent 逻辑终于能在标准场景里被验证 |
| **L4 guidance/execution** | 接收 IPOPT/GeoFallback 的 psi_cmd/u_cmd | 接收 acatos 的 psi_cmd/u_cmd(若收敛);若 acatos 非 Converged,本周期不重试 IPOPT,downstream 发空计划 heartbeat,连续 3 周期后 BC-MPC takeover | 接口契约 byte-identical(MidMpcSolution shape 11 字段不变)。**注意**:`test_mid_mpc_acados_parity` 只覆盖直线无目标场景(避开 CrossingGiveWay 因 IPOPT container 环境 fail),L4 在 CPA-active acatos dispatch 下的可执行性**未证实**,留阶段 3 SIL(GNC reviewer 升级项) |
| **M7 veto/MRM** | `consecutive_failures_` + MRM-02 路径不变 | 不变(acados 失败仍递增 consecutive_failures_,line 147-158) | 无 |
| **M8 evidence** | ASDR 里 acados 0 次触达 | ASDR 里 acados 实际 dispatch 计数 + sqp_iter + status | 改善(可观测性提升) |

**关键安全保证**:
1. acados 失败时 `consecutive_failures_` 仍递增 → BC-MPC takeover 信号(line 873-885)仍正确触发 → MRM-02 escalation 路径 byte-identical。
2. gate-2 改造后,CPA < 1852m 周期 fallback IPOPT → BC-MPC Override 仍基于自己的 worst_case_cpa 判据(独立路径),不依赖 Mid-MPC 的 dispatch 选择。**Mid-MPC 不抢 BC-MPC 的主导权**。
3. acados 的 MidMpcSolution 输出 shape 与 IPOPT 完全一致(P1b-1b Task 18 parity test 已验证),L4 guidance 无感知。

---

## 4. 风险量化与失效边界

### 4.1 风险矩阵

| 风险 | 等级 | 来源 | 失效边界 | 缓解 |
|---|---|---|---|---|
| acatos 在 ample-time 窗口(CPA ≥ 1852m)求解不收敛 | 中 | acatos SQP + FULL_CONDENSING 在大 horizon 下可能 max_iter | **本周期不重试 IPOPT**:`mid_mpc_solver.cpp:146-159` 一旦 dispatch 就直接 return acatos 的 MidMpcSolution(可能是非 Converged)。downstream `committed_route` 拒收非 NORMAL plan → `mid_mpc_node.cpp:1409-1463` 发空计划 heartbeat;连续 3 个非 Converged 周期后触发 BC-MPC takeover(line 873-885);连续 >5 触发 MRM-02(line 152-154) | `consecutive_failures_` ≥ 3 → BC-MPC takeover;`consecutive_failures_` > 5 → MRM-02 escalation;BC-MPC + M7 双层兜底。**不是 IPOPT retry**(GNC reviewer 修正:此前 memo 误述) |
| **新增:acatos dispatch 引入更长的降级窗口 vs IPOPT-always** | 中 | gate-2 改造前 ample-time 窗口跑 IPOPT(可能收敛),改造后跑 acatos(若失败则空计划) | 单周期至 3 周期空计划 heartbeat 期间 L4 走 corridor / BC-MPC takeover | 这是改造的固有代价。安全网完整(consecutive_failures + BC-MPC + MRM-02),但 L4 continuity 比之前差。阶段 3 SIL 必须验证 acatos 在 CPA ≥ 1852m 的实际收敛率 |
| 线性 CPA vs BC-MPC trajectory CPA 差异导致 dispatch 与 takeover 边界不对齐 | 低-中 | M2 线性 `tgt.cpa_m` vs BC-MPC trajectory minimax `worst_case_cpa_m`(`bc_mpc_collision_detector.cpp:100-114`) | gate-2 可能 dispatch acatos(线性 CPA ≥ 1852m)而 BC-MPC 同时 Override(trajectory CPA < 1852m),反之亦然。动态几何下数值可差数百米 | gate-2 是 **dispatch gate**(选 backend),**不是 takeover 决策**;两者职责不同。M7 hard-constraint CPA checker(`m7_safety_supervisor/.../hard_constraint_cpa.hpp`)是独立的真实 backstop。**待 TDL Lead 确认**:L4 在 M5 committed_route 与 BC-MPC Override 同时发布时的仲裁规则(GNC reviewer 升级项) |
| `last_minalt_box_infeasible_` 在 acatos 成功路径下 stale(pre-existing) | 极低(pre-existing) | `mid_mpc_solver.cpp:262` 只在 IPOPT 路径更新 | BC-MPC takeover OR-condition 的 box_infeasible 项可能读到 stale 值 | **pre-existing 行为,本 memo 不引入也不修复**。值得开 follow-up ticket,但不在本次范围 |
| gate-2 触发频率改变导致日志噪音变化 | 极低 | CPA < 1852m 命中频率与 TCPA<2000s 不同 | 运维监控告警阈值可能需调整 | 监控 metric 在阶段 3 验证后校准 |
| 改 dispatch 日志 bug 后,旧监控依赖"两条 warn 同时打"的逻辑失效 | 极低 | §9.4 期间可能有人写了基于误诊日志的监控 | 没找到此类监控(查 runs/ + scripts/) | 阶段 3 验证时确认 |
| warm-up gate-1 真坏时(cold-capsule 未收敛)未被及时发现 | 极低 | 改日志 bug 后,warm-up 失败只剩 ctor 警告(line 423-428) | 现在已经能看到 ctor 警告(0 条 = 收敛);日志 bug 修复不影响这个信号 | ctor 警告是 warm-up 失败的唯一权威信号,不依赖 dispatch 路径的 warn |
| acatos dispatch 在 CPA-active 场景的 L4 可执行性未证实 | 中 | `test_mid_mpc_acados_parity` 只覆盖直线无目标场景(避开 CrossingGiveWay 因 IPOPT container 环境 fail) | acatos 在 close-quarters 的输出可能 shape-valid 但物理不可执行 | 阶段 3 SIL 是唯一能 demonstration 的测试;是 production-readiness 前置,不是 memo-approval 前置 |

### 4.2 不变量(byte-identical 保证)

以下路径**必须**改造前后 byte-identical,通过单测 + SIL 回归验证:
1. `consecutive_failures_` 递增/重置逻辑(line 147-158)
2. MRM-02 critical 日志 + escalation(line 153-154)
3. IPOPT fallback 路径(line 169+,完全不动)
4. **MidMpcSolution 输出 shape 全部 11 字段**(`types.hpp:306-335`):`status`, `trajectory`, `cost_total`, `cost_colreg`, `cost_dist`, `cost_vel`, `cpa_slack`, `cpa_slack_per_target[16]`, `solve_duration_ms`, `ipopt_iterations`, `stamp_ns`(GNC reviewer 修正:此前 memo 只列了 6 个)
5. ROS2 topic / message / IDL / QoS(零改动)
6. BC-MPC Override 触发条件(独立路径,完全不依赖 Mid-MPC dispatch)
7. **gate-2 一旦 dispatch,本周期不重试 IPOPT**(`mid_mpc_solver.cpp:146-159` 直接 return acatos 的 MidMpcSolution;downstream 处理非 Converged 输出 via `committed_route` 拒收 + 空计划 heartbeat + 3 周期 BC-MPC takeover + 5 周期 MRM-02)。此行为改造前后一致,但 memo 此前误述为"失败走 IPOPT fallback"。

### 4.3 ample-time 安全论证(AGENTS.md COLREGs full-chain rule)

本改造**不是**"为了 acados 在 ho 上能被测到而调阈值"。安全论证链:

1. **现状违反 ample-time 设计**:gate-2 的 TCPA<2000s 比本仓库 ODD-A ample-time floor(720s)严 2.78 倍,比 ample-time 文献上限(1200s)严 1.67 倍。它在工程语义上是**错的** —— ample-time window 之外就把 acados 挡了。
2. **改造对齐 ample-time 工程**:CPA < cpa_safe × 1.0 = 1852m 是 BC-MPC 接管边界(已部署,已测试)。把 gate-2 对齐这个边界,等价于"acados 只在 BC-MPC 不接管时上场",职责清晰。
3. **不依赖测试通过**:即便 12 个标准场景改造后 acados 仍然失败(IPOPT fallback + GeoFallback 链不变),本改造的安全论证也成立 —— 因为它修的是工程语义错位,不是凑测试。
4. **保留多层防护**:gate-1(warm-up)+ consecutive_failures_/MRM-02 + BC-MPC Override + M7 veto,四层独立防护都在。gate-2 改造不削弱任何一层。

---

## 5. 测试与验收

### 5.1 单元测试(必跑)

**新增测试**(3 个 reviewer 一致要求):

| 测试 | 验证点 | 期望 | 来源 |
|---|---|---|---|
| **[T1] dispatch CPA 边界精确**:cpa_safe=1852,单 target | cpa_m = 1851.0m → fallback;cpa_m = 1852.0m(精确边界)→ acatos dispatch(与 BC-MPC `best_cpa >= threshold → Resolved` 语义一致);cpa_m = 1853.0m → acatos dispatch | 3 个边界点全过 | code-reviewer 必修 |
| **[T2] dispatch 多 target 混合**:cpa_safe=1852 | (a) 一个 target CPA=1851m + 另一个 CPA=5000m → fallback(loop break on first match);(b) 全部 target CPA > 1852m → acatos dispatch | 两 case 全过 | code-reviewer should-add |
| **[T3] acatos 失败 → consecutive_failures + MRM-02 链**(M7 F-1 关键 gap)| 注入 mock acatos 返回非 Converged → `consecutive_failures_ == 1`;连续 6 次非 Converged → `consecutive_failures_ == 6` 且 critical 日志 fire;第 7 次返回 Converged → reset 为 0 | 全过;**当前测试覆盖为零(M7 F-1),必须补** | M7 reviewer 必修 |
| **[T4] acatos 失败单周期不重试 IPOPT**(GNC 必修)| 注入 mock acatos 返回非 Converged → 验证本周期 `MidMpcSolver::solve()` 返回的就是 acatos 的非 Converged sol(IPOPT 未跑),不是 IPOPT retry 的结果 | 全过 | GNC reviewer 必修 |
| **[T5] 日志 bug 修复验证** | warm_up_succeeded=true + bc_mpc_territory=true → 只打"BC-MPC territory"warn,**不打**"warm-up did not converge";warm_up_succeeded=false → 只打"warm-up did not converge" | stderr 抓取验证 | code-reviewer |
| **[T6] NaN CPA 防御** | target.cpa_m = NaN + 另一 target.cpa_m = 1851m → fallback(不被 NaN 干扰) | 不崩溃,行为正确 | code-reviewer nice-to-have |

**现有测试回归**(必须 byte-identical PASS):

| 测试 | 验证点 | 期望 |
|---|---|---|
| `test_mid_mpc_solver::ConsecutiveFailuresResetOnSuccess`(line 421-430) | IPOPT 路径 counter 行为 | PASS(IPOPT 路径不动)|
| `test_mid_mpc_acados_solver` 全套(13 case) | acatos 行为本身不变(改的是 dispatch gate,不是 acatos) | 全 PASS |
| `test_mid_mpc_acados_parity`(3 case) | IPOPT/acados 输出契约 parity(shape + 字段 + 直线 psi 偏差) | 全 PASS |
| `test_ou_uncertainty`(11 case) | P7 OU 不确定性逻辑 | 全 PASS |
| `test_mid_mpc_acados_formulation`(16 case) | P7 stride-8 formulation | 全 PASS |

### 5.2 SIL 验证(阶段 3,GNC profile)

| 场景 | 验证点 | 验收 |
|---|---|---|
| `colreg-rule14-ho`(full sim) | acados 实际被 dispatch(日志 status=0/sqp_iter>0 至少出现一次)+ CPA / 转向 / plan KPI 改善 vs §9.4 RED baseline | acados dispatch 计数 > 0;CPA > floor 180m;转向 ≥ 5°;M5 plan VALID |
| `colreg-rule15-cs`(full sim) | 同上,不同 COLREGs 类 | 同上 |
| IPOPT fallback 路径回归 | 在 acados dispatch 命中后,人为注入 acatos 失败(若可),验证 consecutive_failures_ + BC-MPC takeover + MRM-02 链完整 | 链路完整 |

### 5.3 验收门(AGENTS.md promotion rule)

1. 单测全 PASS(现有 + 新增 dispatch case)
2. 独立 reviewer 签字:
   - `tdl_m7_safety_reviewer`:M7 doer-checker 独立性 + MRM 路径 + fail-safe 行为
   - `tdl_gnc_contract_reviewer`:L4 executability 契约(MidMpcSolution shape 不变)
   - `tdl_code_reviewer`:dispatch 路径正确性 + 日志 bug 修复 + 回归风险
3. GNC 容器 ho/cs SIL full sim,acados 实际 dispatch + KPI 改善
4. 证据路径:`runs/gate2_cpa_redesign_*`(trace + summary + dispatch 计数)

---

## 6. 不在本 memo 范围(explicit out-of-scope)

| 项 | 状态 | 理由 |
|---|---|---|
| acados solver opts 优化(FULL→PARTIAL CONDENSING / max_iter / tol / warm-start shift-init 接入) | **暂不动**(用户指示) | 实时性不是阻塞(60s 预算,3s/solve 占 5%);先把链路跑通再决定优化路线 |
| gate-1 warm-up 改造 | **不动** | 实测 warm-up 收敛,gate-1 不触发;只修相关日志 bug |
| BC-MPC Override 边界调参 | **不动** | 独立路径,职责已清晰 |
| G3 ample-time / G7 codegen parity 重跑 | **不触发** | 本改造不改 codegen / 不改 solver opts,G3/G7 baseline 仍有效 |
| ROS2 topic/message/IDL 改动 | **零** | 纯内部 dispatch 逻辑 |
| L4 executability 契约改动 | **零** | MidMpcSolution shape byte-identical |

---

## 7. 待确认事项(给 reviewer / 用户)

**已决定(用户 2026-07-19 拍板)**:
1. ✅ gate-2 阈值倍数 = **1.0**(对齐 `m5_params.yaml` override_cpa_multiplier 配置值)
2. ✅ dispatch metric 上报 = **暂不加**(靠 spdlog warn 计数)
3. ✅ commit 粒度 = **合并为一个 commit**(gate-2 + 日志 bug,同一误诊根因)
4. ✅ GNC profile stack = **等阶段 3 再搭**

**仍待用户在 memo v2 接受时拍板**:
5. **memo v2 是否接受**:3 个 reviewer 全部 APPROVE-WITH-CONDITIONS,v2 已吸收全部必修项。接受后进实施。
6. **L4 dual-publish arbitration**(GNC reviewer 升级项):若发现 M5 committed_route 与 BC-MPC Override 同时发布,L4 仲裁规则需明确。**建议作为阶段 3 SIL 的观察项,不阻塞 memo 接受**。

**reviewer 建议但本 memo 不采纳(已记录)**:
- N-1(M7):`kAcadosCpaGateMultiplier` init-time assert 与 BC-MPC config 相等 —— 暂不做(用户决定硬编码 1.0,跨引用注释足够)。
- N-3(M7):`last_minalt_box_infeasible_` stale —— pre-existing,开 follow-up ticket。
- M7 metric 建议:ASDR 加 acatos dispatch/fallback count —— 用户已决定暂不加。

---

## 附录 A:证据溯源

| 编号 | 类型 | 来源 |
|---|---|---|
| `[PF-1]` | PROJECT_FACT | `mid_mpc_solver.cpp:133-167` dispatch gate-2 + 日志 bug |
| `[PF-2]` | PROJECT_FACT | `mid_mpc_acados_solver.cpp:379-429` warm_up_capsule_(含 line 423-428 唯一 warm-up 失败 warn) |
| `[PF-3]` | PROJECT_FACT | `mid_mpc_node.cpp:481` 60s replan timer(VR-06) |
| `[PF-4]` | PROJECT_FACT | `bc_mpc_collision_detector.cpp:100-114` Override 触发条件 |
| `[PF-5]` | PROJECT_FACT | `bc_mpc_branch_formulation.hpp:45` override_cpa_multiplier=0.8 默认 |
| `[PF-6]` | PROJECT_FACT | `config/m5_params.yaml:14` override_cpa_multiplier=1.0 配置 |
| `[PF-7]` | PROJECT_FACT | `mid_mpc_node.cpp:47` kCpaSafeFallback_m=1852.0 |
| `[PF-8]` | PROJECT_FACT | `m2_params.yaml:3` aggregation_rate_hz=4.0 → BC-MPC 4Hz |
| `[PF-9]` | PROJECT_FACT | `mid_mpc_node.cpp:873-885` BC-MPC takeover via consecutive_failures≥3 |
| `[PF-10]` | PROJECT_FACT | `acados_ocp_m5_mid_mpc_acados.json` SQP + FULL_CONDENSING_HPIPM + EXACT |
| `[PF-11]` | PROJECT_FACT | `gen_mid_mpc_acados.py:454-463` solver opts 来源 |
| `[PF-12]` | PROJECT_FACT | `tools/sil/colregs_scenario_audit.py::_straight_line_cpa` 12 场景 TCPA 复算 |
| `[PF-13]` | PROJECT_FACT | acados profiling(本 worktree `M5_ACADOS_PROFILE` 宏,容器实测)|
| `[DE-1]` | DOMAIN_EVIDENCE | 架构设计报告 §3.3 line 260-265 ODD CPA/TCPA ample-time 阈值 |
| `[DE-2]` | DOMAIN_EVIDENCE | Wang et al. 2021 [R17] + Frontiers 2021 [R2]:TCPA 5-20 min |
| `[DE-3]` | DOMAIN_EVIDENCE | 架构设计报告 §3.4 TDL = min(TCPA×0.6, T_comm, T_health),Veitch 2024 TMR≥60s |
| `[SD-1]` | SESSION_DOC | sess_a3ceed81 P0-P7 设计 session:VR-06 60s replan 依据 Eriksen 实测 |

## 附录 B:§9.4 错误结论的勘误建议(独立 PR)

`docs/superpowers/specs/2026-07-18-m5-mpc-p0-p7-implementation-report.md` §9.4 需勘误:

| §94 原文 | 勘误 |
|---|---|
| "gate-1: `warm_up_succeeded() == false`(生产节点 cold-capsule warm-up 未收敛)" | 实测 warm-up 收敛(0 条 ctor 警告);"warm-up did not converge" dispatch warn 是日志 bug(line 164-166 在 if 块外),非实际失败 |
| "单次 solve 实测耗时 ~12.5 s/solve...即便 dispatch 触达也会超 1 Hz 实时预算" | Mid-MPC replan = 60s(VR-06),不是 1Hz;warm 稳态 3s/solve 占 5% 预算;"12.5s" 是 3-solve 算术平均(cold 24s+warm 9.7s+real 3s) |
| "ho RED 根因链:1. acados backend 被 dispatch gate 挡 → 0 次实际 solve" | 部分正确:gate-2 short-TCPA<2000s 是真因;gate-1 实测未触发 |
| "12.5 s/solve 的实时性能(否则即便 dispatch 触达也跑不动 1 Hz 循环)" | 实时性不是阻塞(60s 预算);QP 优化是独立可选项 |

勘误建议作为独立 docs commit(不改 §9.4 的 G8 RED 结论本身 —— ho RED 是真实的,根因链里"acados 0 触达"成立,只是归因到 gate-1 是错的)。
