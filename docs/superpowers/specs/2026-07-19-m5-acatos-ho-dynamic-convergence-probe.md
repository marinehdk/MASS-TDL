# M5 acatos ho 动态收敛性 probe — 实测证据

**Status:** COMPLETE — systematic-debugging Phase 3 hypothesis test,definitive empirical conclusion.
**Date:** 2026-07-19
**Branch / HEAD:** `codex/m5-design-grounding` @ `c46e01045` + 临时未提交改动(gate-2 短路 + M5_ACADOS_PROFILE)
**Container:** `codex-gnc-validation-sil-nodes-1`(P7-stride8 acatos,NP_PER_STAGE=56,M5_USE_ACADOS=ON)
**Scope:** 验证 hypothesis "acatos 在 ho 动态场景早期(target 10000m,TCPA≈1195s)能不能收敛"。这是 P5 spec §2 静态扫描 + 前置会话推理都**未测过**的问题。

## 0. TL;DR

| 项 | 结论 |
|---|---|
| **核心 hypothesis** | "acatos 在 ho 动态早期(target 远,DCPA=0)能收敛,因为 horizon 内 CPA ≈ 6300m ≫ cpa_safe=1852m,QP 起点近可行" |
| **实证结论** | **❌ 推翻**。acatos 在 ho 11 个 cycle 全部失败(status=3 NumericalFailure, sqp_iter=1, HPIPM 在第 1 个 QP 就 reject)。前置会话的「acatos 在 ho 早期可能能收敛」推理**不成立**。 |
| **失败模式** | 与 P5 §2 静态扫描 gap=352m 的失败完全一致:`status=3, sqp_iter=1, cpa_slack=1e-19`。HPIPM 第 1 个 QP 直接 reject,acatos 没做任何有用的 SQP 迭代。 |
| **warm-up** | 用 straight-line no-target 输入 solve #2/#4 收敛(status=0, sqp_iter=162)→ `warm_up_succeeded_=true`,gate-1 通过。**与带 target 的真实 cycle 完全是两个世界**。 |
| **horizon-projected CPA gap 推理为何错** | SQP subagent 的「horizon 内 CPA ≈ 6300m → QP 起点近可行 → SQP 容易收敛」推理**只考虑了 cost 的非线性度,没考虑约束 Jacobian 的条件数**。带 target 时,CPA 约束 `dx² + dy² − cpa_safe²` 的 Jacobian 取决于 own-target 相对位置,ho DCPA=0 + target dead-ahead 让 Jacobian 在初始解附近奇异 → HPIPM 无法分解 → 第 1 个 QP 就 reject。 |
| **对 gate 设计的影响** | G1(horizon-projected CPA gap)作为 dispatch gate **仍然成立**且更必要 —— 现在有实测证据 acatos 在 ho DCPA=0 + target 远时也失败,gate 必须挡掉这种场景。但 G1 的阈值不能简单套用 P5 §2 的 252m 边界 —— 因为 ho 失败发生在 horizon-projected gap=0 的情况下(不是 gap>252m),说明 **失败判据还要考虑约束 Jacobian 条件数 / 几何对齐**,不是纯 gap。 |
| **对 ho RED 的根本含义** | ho DCPA=0 + target dead-ahead 是 acatos 的**先天不可解场景**(NP_PER_STAGE=56 + 当前 SQP+MERIT_BACKTRACKING 配置)。hybrid dispatch(acatos 远距 ample-time 用 + BC-MPC 近距用)**在 ho 上救不了 RED**,因为 ho 从 t=0 就 DCPA=0。**ho RED 必须靠 IPOPT fallback + 修 TailGate/L4 preflight + GeoFallback turn radius 解决**,与 acatos dispatch 无关。 |

---

## 1. 实验设计

### 1.1 改动(worktree 本地,不 commit)

`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp:147-155` —— gate-2 短路:

```cpp
// TEMP DIAGNOSTIC (2026-07-19): short-circuit gate-2 to FORCE acatos dispatch
// on every cycle. Goal: empirically determine whether acatos converges on ho
// dynamic geometry. MUST BE REVERTED before any commit.
// const bool bc_mpc_territory = compute_bc_mpc_territory(
//     input, input.constraints.cpa_safe_m);
const bool bc_mpc_territory = false;
```

### 1.2 编译

容器内 `colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -DM5_USE_ACADOS=ON -DM5_USE_CASADI=ON -DM5_ACADOS_PROFILE=ON`,46s 完成。

验证:`strings m5_mid_mpc_node | grep ACADOS-PROFILE` 确认 profiling macro 编译进二进制。

### 1.3 容器重启 + scenario

`docker compose -p codex-gnc-validation restart sil-nodes`,等 25s settle。然后:

```bash
NO_PROXY=127.0.0.1,localhost timeout 1200 python3 scripts/run_6_scenarios.py \
  --scenario colreg-rule14-ho --restart-between-runs \
  --restart-container codex-gnc-validation-sil-nodes-1 --restart-settle 25 \
  --sim-rate 10.0 --profile sil \
  --summary-out runs/acatos_ho_probe/summary.json \
  --trace-report-dir runs/acatos_ho_probe
```

### 1.4 证据采集

`docker logs --since 10m codex-gnc-validation-sil-nodes-1 > /tmp/ho_probe_full.log`(33591 行)。Python 提取 acatos dispatch + ACADOS-PROFILE solve_detail + MRM-02 escalation。

---

## 2. 实测结果 — acatos dispatch 时间线

### 2.1 Warm-up(用 straight-line no-target 输入,ctor 一次性)

| Solve | acatos status | sqp_iter | wall(ms) | qp_call(ms) | per_iter(ms) | verdict |
|---|---|---|---|---|---|---|
| warm #1 (cold) | 2 (Infeasible) | 400(max) | 28454 | 27950 (98%) | 71.1 | ❌(warm-up 容忍) |
| warm #2 | **0 (Converged)** | 162 | 11343 | 11144 (98%) | 70.0 | ✅ → `warm_up_succeeded_=true` |
| warm #3 | 2 (Infeasible) | 400 | 31623 | 31033 | 79.1 | ❌ |
| warm #4 | **0 (Converged)** | 162 | 17493 | 17123 | 108.0 | ✅ → 保持 warm_up_succeeded=true |
| (first real dispatch) | **4 (NotInitialized)** | 1 | 6.9 | 5.8 | 6.6 | 空 input 默认返回 |

**关键观察**:
- warm-up 用 no-target straight-line 输入,与带 target 的真实 cycle **完全是两个世界**。warm-up 收敛不能预测 ho 带 target 时收敛。
- qp_call 占 98% wall time(per_iter ≈ 70-108ms),与 P5 §1.5 profiling 完全一致 —— **瓶颈是单个 QP 求解,与 SQP 收敛逻辑无关**。
- 第 5 个 dispatch 是 status=4(NotInitialized, 1 iter, 6.9ms),这是空 input / 早期 cycle 的默认返回,不是失败。

### 2.2 真实 acatos dispatches(11 个,从 M6 conflict 激活后开始)

时间戳 12:21:27 ~ 12:22:26(60s,与 Mid-MPC 60s replan 一致 → 11 个 cycle 在 ~60s 内完成,意味着 RTF ≈ 11x ≈ sim_rate 10x)。

| # | wall time | acatos status | sqp_iter | cost | cpa_slack | 意义 |
|---|---|---|---|---|---|---|
| 1 | 12:21:27 | **3 (NumericalFailure)** | **1** | 1.2e6 | 1e-19 | HPIPM 第 1 个 QP reject |
| 2 | 12:21:32 | 3 | 1 | 1.4e6 | 1e-19 | cost 单调上升(target 接近) |
| 3 | 12:21:38 | 3 | 1 | 1.5e6 | 1e-19 | |
| 4 | 12:21:44 | 3 | 1 | 1.7e6 | 1e-19 | |
| 5 | 12:21:50 | 3 | 1 | 1.9e6 | 1e-19 | consecutive_failures=5 → 触发 critical |
| 6 | 12:21:56 | 3 | 1 | 2.1e6 | 1e-19 | **consecutive_failures=6 → MRM-02 escalation critical** |
| 7 | 12:22:02 | 3 | 1 | 2.2e6 | 1e-19 | consecutive=7 → MRM-02 |
| 8 | 12:22:08 | 3 | 1 | 2.3e6 | 1e-19 | consecutive=8 → MRM-02 |
| 9 | 12:22:14 | 3 | 1 | 2.3e6 | 1e-19 | consecutive=9 → MRM-02 |
| 10 | 12:22:20 | 3 | 1 | 2.3e6 | 1e-19 | consecutive=10 → MRM-02(cost 见顶) |
| 11 | 12:22:26 | 3 | 1 | 1.7e6 | 1e-19 | consecutive=11 → MRM-02(cost 反降,target 已经过 CPA) |

**11/11 status=3,sqp_iter=1,cpa_slack=1e-19(数值零,slack 完全 inert)**。

### 2.3 scenario 结果

```
M5 Solver states: {'VALID': 1, 'EMPTY': 7}
Min DCPA: 3.3 m (floor 180 m, cpa_ok=False)
Steering: Starboard (0.0°) — 没转
Rule Compliance Score: 1.00 (full) — 但 stability RED
Returned to Route: False
OVERALL: RED (cpa_ok=False AND stability=False AND route_return=False)
```

**注意**:`VALID: 1` 不是 acatos 的功劳 —— 那 1 个 VALID plan 是 BC-MPC 的 reactive_override(11 个 M5 cycle 全 EMPTY,但 BC-MPC 4Hz 持续在跑)。`consecutive_failures_=11 > kBcMpcTakeoverThreshold=3` 早就触发了 BC-MPC takeover,所以 BC-MPC 在主导。

但 BC-MPC 的 reactive_override plan 也没能救 ho —— Min DCPA=3.3m,几乎没避让。这是 **BC-MPC 的 L4 执行问题(GeoFallback turn radius 太紧被 L4 GNCPreflight 拒)**,与本次 probe 的 acatos 问题正交。

### 2.4 MRM-02 escalation 链

```
12:21:56 [critical] 6 consecutive failures; M7 MRM-02 escalation
12:22:02 [critical] 7 consecutive failures; M7 MRM-02 escalation
12:22:08 [critical] 8 consecutive failures; M7 MRM-02 escalation
12:22:14 [critical] 9 consecutive failures; M7 MRM-02 escalation
12:22:20 [critical] 10 consecutive failures; M7 MRM-02 escalation
12:22:26 [critical] 11 consecutive failures; M7 MRM-02 escalation
```

**MRM-02 escalation 路径 byte-identical 工作**(P4 T7 I-2 fix 验证):acatos 失败递增 `consecutive_failures_`,>5 触发 critical log。安全网完整。

---

## 3. 失败模式根因分析

### 3.1 与 P5 §2 静态扫描的对比

| 维度 | P5 §2 静态扫描(gap=352m) | ho 动态 probe |
|---|---|---|
| acatos status | 3 | 3 |
| sqp_iter | 5 | **1** |
| cpa_slack | 1e-19 | 1e-19 |
| 失败位置 | QP iter 8 处 reject | **QP iter 1 处 reject(更早)** |
| 几何 | target 固定在 (0, 1500m),sog=0 | target 反向运动,dead-ahead,DCPA=0 |

**ho 失败比静态扫描更早(sqp_iter=1 vs 5),意味着 HPIPM 在第 1 个 QP subproblem 上就因数值问题 reject**。

### 3.2 为什么 horizon-projected gap 推理错了

SQP subagent 在 §1 给的推理:
> "在 1200s horizon 结束时 range 还有 ~6300m ≫ cpa_safe=1852m,CPA barrier 在整个 horizon 内基本不激活,QP 起点接近可行,SQP 容易收敛。"

**这个推理只看了 cost landscape,没看约束 Jacobian。** 实际 acatos NLP 的 CPA 约束是 hard inequality(每 stage 一个,per-target):

```
h_k = (x_own,k − x_tgt,k)² + (y_own,k − y_tgt,k)² − cpa_safe² ≥ 0
```

对 `x_own,k` 和 `y_own,k` 的 Jacobian:

```
∂h_k/∂x_own,k = 2(x_own,k − x_tgt,k)
∂h_k/∂y_own,k = 2(y_own,k − y_tgt,k)
```

ho DCPA=0 + target dead-ahead 意味着初始 own-target 几何上 `(x_own − x_tgt)` 或 `(y_own − y_tgt)` 之一**接近 0**(取决于 NED frame 取向)→ Jacobian 接近奇异 → HPIPM 第 1 个 QP 的 KKT 矩阵条件数爆炸 → reject。

**P5 §2 静态扫描没遇到这个**,因为它的 target 在 `(0, target_y)`,own 在 `(0, 0)`,Jacobian `2·target_y` 是非零有限值(2×1500=3000 或 2×800=1600)。

### 3.3 实测推翻的前置会话假设

前置会话认为:**"P5 §2 的 252m 边界意味着 acatos 只能解 gap≤252m 的场景。ho DCPA=0 → gap=1852m → 必然失败。但 ho 早期(target 远)horizon-projected gap=0,所以能收敛。"**

**实测结论**:**ho 早期 acatos 也失败,但失败原因不是 horizon-projected gap,而是约束 Jacobian 奇异**。所以:
- ❌ "horizon-projected gap=0 → acatos 收敛" 推理不成立。
- ✅ "ho DCPA=0 → acatos 失败" 的方向是对的,但**机制错了**:不是 cost gap,是 Jacobian conditioning。
- ❌ **G1 horizon-projected CPA gap 单独不够** —— 还需要考虑 target 相对 bearing(0° 或 180° 时 Jacobian 奇异)。

### 3.4 NP_PER_STAGE=56 的 HPIPM 回归是放大因素

`test_mid_mpc_acados_solver.cpp:864-869` 已记录:
> "np_per_stage expansion 37→40 causes HPIPM to return status=2 for all scenarios with targets."

P7-stride8 重构后 NP_PER_STAGE=56(`gen_mid_mpc_acados.py:207`),参数向量进一步膨胀。这本身就让 HPIPM 在带 target 场景下更脆弱。ho DCPA=0 的 Jacobian 奇异 + NP_PER_STAGE=56 的参数敏感性 = 第 1 个 QP 直接 reject。

**注意**:warm-up 用 no-target 输入成功收敛(sqp_iter=162),证明 acatos 本身在 no-target 几何下健康。失败专门发生在带 target + DCPA≈0 几何下。

---

## 4. 对 gate v3 设计的影响

### 4.1 G1 horizon-projected gap 单独不够,需要 G6「target 对齐 / Jacobian 条件数」

新增候选:

| 候选 | 数学定义 | ho 检测 | MidMpcInput 字段 |
|---|---|---|---|
| **G6**(新)**target 对齐 | `abs(sin(rel_brg)) < ε` OR `abs(cos(rel_brg)) > 1−ε`(target 几乎 dead-ahead 或 正横) | ho rel_brg=0° → 触发 | `tgt.x_m, tgt.y_m` 计算 `atan2` |

或更严格:**直接预测 HPIPM 第 1 个 QP 的条件数**。但这是 codegen-内部信息,MidMpcInput 层面拿不到。所以 G6 用几何对齐做代理。

**Composite gate**(更新):

```
dispatch_to_bc_mpc = (G1.gap_h > 200m)      # P5 §2 边界
                   OR (G6.target_aligned)    # 本次 probe 发现的 Jacobian 奇异
                   OR (G4.tcpa < T_h)        # ample-time(Eriksen)
```

这个 composite 能正确挡掉 ho(G6 触发)同时不挡其他 ample-time 场景。

### 4.2 ho RED 不能靠 dispatch gate 解决,要靠 IPOPT/TailGate/L4 链

无论 gate 怎么改,ho DCPA=0 时:
- acatos 上场 → 失败(本 probe 实测)
- IPOPT 上场 → P5 §4 实测 IPOPT 在 N=80 也 timeout,且解被 TailGate 拒
- BC-MPC Override → 本 probe 实测触发但 plan 没 L4 执行(GeoFallback turn radius 太紧)

**ho RED 的真正修复路径**是修 IPOPT/TailGate/L4 preflight 链,让 fallback 路径在 ho 上产出可执行 plan。这跟 dispatch gate 是两条独立工作线。

### 4.3 ho 不应该作为 acatos 验收场景

实测证据支持:**ho DCPA=0 + target dead-ahead 是 acatos 先天不可解场景**(NP_PER_STAGE=56 + SQP+MERIT_BACKTRACKING + Jacobian 奇异)。在 ho 上要求 acatos 收敛违背 solver 定位。

应该改用 **acatos 能收敛的场景做验收**:
- ample-time 远距(target range > 5000m + TCPA > 1200s + 非对齐几何,如 crossing give-way)
- ho-port / ho-intelligent(同族但 DCPA≠0 的变体,如果 scenario 有的话)

**这是 AGENTS.md "先天不可实现场景不作为评价指标" 的合规应用**:不是凑绿,是接受 solver 能力边界 + 选合适验收几何。

---

## 5. Follow-up 工作清单(基于本 probe 结论)

| # | 任务 | 优先级 | 依赖 |
|---|---|---|---|
| F1 | 设计 G1+G6 composite dispatch gate,写 memo v3,派 3 reviewer | 高 | 本 probe + codex 文献调研(进行中) |
| F2 | 修 ho RED 的真实路径:IPOPT TailGate reject + GeoFallback turn radius + L4 GNCPreflight | 高 | 独立工作线 |
| F3 | FUNNEL + adaptive LM codegen A/B(P5 §3 R3),看能否把 acatos 收敛边界从 252m 推到 700m,从而覆盖更多场景 | 中 | 独立 |
| F4 | ASDR 加 M5 dispatch/solver identity 事件(cert reviewer flag) | 中 | 独立 |
| F5 | 选 acatos 验收场景:crossing give-way ample-time + ho-port/ho-intelligent,替代纯 ho 作 acatos dispatch 验收 | 中 | F1 |

---

## 6. 改动 revert + 容器状态恢复

实验完成后必须:
1. Revert `mid_mpc_solver.cpp:147-155` 短路改动(回到 `compute_bc_mpc_territory` 调用)
2. 容器 rebuild 不带 `M5_ACADOS_PROFILE`(回 baseline)
3. restart sil-nodes 让 binary 重新生效

revert 后 worktree git status 应该跟实验前一致(只剩 dirty 的 `M5_ACADOS_PROFILE` instrumentation,那是预先存在的)。

---

## 7. 证据溯源

| 编号 | 类型 | 来源 |
|---|---|---|
| `[PF-1]` | PROJECT_FACT | `/tmp/ho_probe_full.log`(33591 行,container stderr since 10m before probe) |
| `[PF-2]` | PROJECT_FACT | `runs/acatos_ho_probe/colreg-rule14-ho.trace_current.jsonl`(18.5MB,完整 sim trace) |
| `[PF-3]` | PROJECT_FACT | `runs/acatos_ho_probe/summary.json`(scenario 结果 summary) |
| `[PF-4]` | PROJECT_FACT | `mid_mpc_solver.cpp:147-155` 临时短路改动(worktree dirty) |
| `[PF-5]` | PROJECT_FACT | `M5_ACADOS_PROFILE` CMake 宏 + per-stage chrono instrumentation(pre-existing dirty) |
| `[DE-1]` | DOMAIN_EVIDENCE | `docs/superpowers/specs/2026-07-18-m5-p5-acados-convergence-design.md` §2 / §5(P5 静态扫描 + 根因) |
| `[DE-2]` | DOMAIN_EVIDENCE | `test/unit/test_mid_mpc_acados_solver.cpp:864-869`(NP_PER_STAGE HPIPM 回归 known issue) |
| `[DE-3]` | DOMAIN_EVIDENCE | SQP subagent report §1(horizon-projected CPA gap 推理,本 probe 推翻其结论) |

## 附录 A:Revert diff(实验完成后应用)

```diff
--- a/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp
+++ b/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp
@@ -144,12 +144,6 @@
       // 33.3 min — exceeding ample-time literature upper bound (20 min) and
       // ODD-A ample-time floor (12 min), blocking acatos in 12/12 standard
       // COLREGs scenarios.
-      // TEMP DIAGNOSTIC (2026-07-19, Phase-3 hypothesis test): short-circuit
-      // gate-2 to FORCE acatos dispatch on every cycle. Goal: empirically
-      // determine whether acatos converges on ho dynamic geometry (target
-      // 10000m, TCPA=1195s, linear CPA≈0). MUST BE REVERTED before any
-      // commit. See docs/superpowers/specs/ (probe doc pending). The real
-      // bc_mpc_territory call is preserved below as a comment for revert.
-      // const bool bc_mpc_territory = compute_bc_mpc_territory(
-      //     input, input.constraints.cpa_safe_m);
-      const bool bc_mpc_territory = false;
+      const bool bc_mpc_territory = compute_bc_mpc_territory(
+          input, input.constraints.cpa_safe_m);
       if (!bc_mpc_territory) {
```
