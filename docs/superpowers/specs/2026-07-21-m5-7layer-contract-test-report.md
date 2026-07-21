# M5 7 层回归基线 Contract Test — 诊断报告(Phase G)

> **生成时间**: 2026-07-21(Phase A-G 全部完成)
> **依据**: docs/superpowers/specs/2026-07-21-m5-7layer-contract-test-design.md
> **执行环境**: codex-m5-p3-sil-nodes-1(12h+ 运行,acados v0.4.4 + CasADi 3.7.2)
> **HEAD**: fb84701b1 + Phase A-G 改动(worktree codex/m5-design-grounding)

---

## 1. 执行摘要

| 阶段 | 测试数 | 通过 | 失败(有意 RED) | 状态 |
|---|---|---|---|---|
| L0 contract | 37 | 37 | 0 | ✅ GATE closed(纯函数契约成立) |
| L1 contract | 11 | 9 | **2 RED**(finding) | ⚠️ 新发现 LBX-len5 缺陷 |
| L2 contract | 5 | 3 | **2 RED**(finding) | ⚠️ 同一缺陷的下游表现 |
| Regression scan | 4 | 4 | 0 | ✅ 基础设施成立 |
| **总计** | **57** | **53** | **4** | **诊断完成** |

**关键发现**:之前的 "L3 GATE closed" 声明(commit fb84701b1)被证伪。本轮 contract test 揭示了一个**横跨 L1+L2 的生产 bug**(LBX 长度不匹配),以及 L0 degradation flag 仍是 write-only(F2)。

---

## 2. 新发现(本轮 contract test 揭示)

### 2.1 LBX-Len5 缺陷(L1-T4, L2-T1)— **HIGH 严重度**

**位置**: `src/mid_mpc/mid_mpc_acados_solver.cpp:1495-1498`

**现状**:
```cpp
double lbx[kAcadosNx] = {-kUhInf, -kUhInf, stage_hdg_min, -use_rot_max, use_spd_min};
double ubx[kAcadosNx] = {kUhInf,  kUhInf,  stage_hdg_max,  use_rot_max, use_spd_max};
ocp_nlp_constraints_model_set(impl_->cfg, impl_->dims, impl_->in, k, "lbx", lbx);
ocp_nlp_constraints_model_set(impl_->cfg, impl_->dims, impl_->in, k, "ubx", ubx);
```

**问题**: 传给 acatos 的 `lbx` 是长度为 5(kAcadosNx)的数组,但 acatos 的 `ocp_nlp_constraints_model_set("lbx", ...)` **只读取前 NBX=3 个 entry**(对应 `idxbx=[2,3,4]`,即 psi/rot/spd 三个 slot)。

**结果**: acatos 读到 `{lbx[0]=-kUhInf, lbx[1]=-kUhInf, lbx[2]=stage_hdg_min}`,按 idxbx 顺序映射 → state 2(psi)= `-kUhInf`,state 3(rot)= `-kUhInf`,state 4(spd)= `stage_hdg_min`。即:
- **psi bound 永远是 -∞**(intended `stage_hdg_min` 没落地)
- **rot bound 永远是 -∞**(intended `-use_rot_max` 没落地)
- **spd_lb 变成 heading_min**(完全错位)

**证据**(in-container probe 2026-07-21):
```
./test_l1_contracts:
L1AcadosFixture.BoxLive_LiveBoundsWrittenStages1ToN:
  stage 1 psi_lb = -1e10 (kUhInf), NOT -0.5 (expected live hdg_min)
L1AcadosFixture.BoxLive_DefaultBoundsAreCodegenDefaults:
  stage 1 lbx (compact, len 3) = [-π, -0.2094, 0]  ← codegen default,正确
```

**直接 probe**(C++ 程序直接调 `ocp_nlp_constraints_model_get(... "lbx")`):
```
lbx[0..4] = -3.14159 -0.2094 0 0 0      ← 只填了 3 个(后 2 个是 buffer 初值 0)
lbx3[0..2] = -3.14159 -0.2094 0          ← 同上,确认 NBX=3 紧凑数组语义
```

**影响**:
- 所有 "live bounds differ from default" 的 production 求解场景(M4 给出 narrow heading box 时)psi 实际无 bound,NLP 失去航向约束 → QP 错误(status=3/4)。
- 解释了 L3 HEAD gap=+52 → raw=2(MAX_ITER)退化:QP 在 unbounded psi 下震荡,最终 hit max_iter。
- **这正是 commit fb84701b1 "L3 GATE closed" 声称已修复但实际未修复的根因**。

**建议修复**(下一 task,本轮不动手):
```cpp
// 应该传长度 3 的紧凑数组(顺序对应 idxbx=[2,3,4]=psi/rot/spd):
double lbx[3] = {stage_hdg_min, -use_rot_max, use_spd_min};
double ubx[3] = {stage_hdg_max,  use_rot_max, use_spd_max};
ocp_nlp_constraints_model_set(impl_->cfg, impl_->dims, impl_->in, k, "lbx", lbx);
ocp_nlp_constraints_model_set(impl_->cfg, impl_->dims, impl_->in, k, "ubx", ubx);
```

### 2.2 F2 持续:L0 degradation flag write-only

**位置**: `src/mid_mpc/mid_mpc_acados_solver.cpp` 全文

**证据**: `grep -c "degradation\." src/mid_mpc/mid_mpc_acados_solver.cpp == 0`。

`InputDegradation` 的 6 个 flag(own_psi/own_u/target/speed_box/reachability/planned_speed)在 L0 由 assemble_input_ 设置,但 **L1+ (solver) 完全不读**。

**影响**:
- L4/LX 无法区分 "真实输入" vs "fallback 值",因为 solver 输出没有携带 degraded 信号。
- 一旦发生 silent substitution(如 own_psi NaN → 0.0),solver 仍报 Converged,操作员无信号。
- 违反 ARCH-DECISION-03 "NEVER silently substitute"。

**L0-T4 RED test**: 保留为显式证伪,当前 PASS(确认 F2 仍未修复),一旦 solver dispatch 真正消费 flag 会变 FAIL → 提示修复完成。

### 2.3 soft_aspiration telemetry 仅在 Converged 时填充(L2-T2)

**位置**: `src/mid_mpc/mid_mpc_acados_solver.cpp:1091-1103`(`constraints_satisfied_` 内)

**现状**: `soft_aspiration_d_min_m` / `soft_aspiration_violation_m` 在 `constraints_satisfied_` 内填充,但 `constraints_satisfied_` 仅在 status=0 (Converged) 或 status=4 (QP error recovered) 路径调用。

**问题**: 当 solve 返回 NumericalFailure(status=3)时,这两个字段保持 0,操作员失去 "d_min 在 2500 软带内违反程度" 的信号。在 LBX-len5 缺陷下,大部分带 target 的场景都是 status=3,所以 telemetry 几乎总是 0。

**建议**: 在 solve() 的所有非 Converged 出口路径都调用一次 `constraints_satisfied_` 填充 telemetry(即使返回的判定为失败,telemetry 仍要传出去)。

---

## 3. 架构文档纠正(commit fb84701b1 "L3 GATE closed" 证伪)

### 3.1 §6 标题与状态(原 "✅ L3 GATE 已关闭")

**纠正为**: `⚠️ L3 GATE 重开(LBX-len5 缺陷揭示,2026-07-21)`

### 3.2 §6 "F-05 完成状态" 表(原 gap≤252m 收敛)

**纠正**:
- 原 "L3 阶段(on correct OCP, post-L1+L2 GATE)" 结论基于 cherry-picked 测试,**没有做 LBX 写入路径的契约测试**。
- L1-T4 contract test 揭示: live bounds 写入实际**没有落地**(psi_lb 永远 -∞)。
- 因此原 "三 case 验证 A/B/C 全 Converged" 是在 psi-bound 失效下的伪收敛(NLP 无 heading 约束,seed碰巧接受)。
- 真正的回归基线需要先修 LBX-len5,再重跑 14-arm 消融。

### 3.3 §10 GATE 阶段门管控 — L3 状态改回 ❌

(详见下文 §5 文档 patch)

### 3.4 §12 实施总表 — 新增 LBX-len5 缺陷条目

(详见下文 §5 文档 patch)

---

## 4. 测试基础设施交付物

### 4.1 新增文件

| 文件 | 行数 | 内容 |
|---|---|---|
| `include/m5_tactical_planner/common/l0_guards.hpp` | 122 | 9 纯函数声明 + 公开常量 `kCpaSafeFallback_m` / `kCpaSafeConflictBump_m` |
| `src/common/l0_guards.cpp` | 108 | 9 纯函数实现(从 assemble_input_ 行为保持提取) |
| `test/unit/test_l0_contracts.cpp` | 297 | 37 L0 contract tests(全部 PASS) |
| `test/unit/test_l1_contracts.cpp` | 451 | 11 L1 contract tests(9 PASS + 2 RED finding) |
| `test/unit/test_l2_contracts.cpp` | 199 | 5 L2 contract tests(3 PASS + 2 RED finding) |
| `test/unit/test_regression_scan.cpp` | 207 | 4 G+H scan tests(全部 PASS) |

### 4.2 生产代码改动(行为保持)

| 文件 | 改动 |
|---|---|
| `src/mid_mpc/mid_mpc_node.cpp` | assemble_input_ 9 处内联验证 → 调用 l0_guards 纯函数;删除匿名 namespace 的 `kCpaSafeFallback_m` 副本(改用 `mass_l3::m5::kCpaSafeFallback_m`)。行为严格不变(每处保留 spdlog::warn 日志)。 |
| `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp` | 加 2 个 TEST-ONLY debug 接口:`debug_set_max_iter_diagnostic(int)` + `debug_get_stage_bounds(int) -> StageBounds` |
| `src/mid_mpc/mid_mpc_acados_solver.cpp` | 实现上述 2 接口;`debug_get_stage_bounds` 读 NBX=3 紧凑数组(psi/rot/spd) |
| `CMakeLists.txt` | 注册 l0_guards.cpp 到 m5_shared_lib;注册 4 个新 test binary |

### 4.3 scan 时间预算验证

| 阶段 | 时间 |
|---|---|
| Codegen(make_target_scenario 无 codegen fixture) | 0s(已 hot) |
| SharedSolverEnv SetUp(1 warm-up) | ~25s |
| S-T1(8 solves,MAX_ITER=100) | ~10s(全部 status=3 fast-fail) |
| S-T2~T4(3 solves) | ~1s |
| **总** | **~36s** ≤ 120s budget ✅ |

(原 spec 预估 ~85s 是 worst-case;实际 ~36s 因为 MAX_ITER=100 cap 让 fail-fast 路径快很多。)

---

## 5. 建议的下一步(超出本轮范围,留给下个 task)

### 5.1 P0: 修 LBX-len5 缺陷

```diff
- double lbx[kAcadosNx] = {-kUhInf, -kUhInf, stage_hdg_min, -use_rot_max, use_spd_min};
- double ubx[kAcadosNx] = {kUhInf,  kUhInf,  stage_hdg_max,  use_rot_max, use_spd_max};
+ // Compact array order matches idxbx=[2,3,4] = (psi, rot, spd). px/py (idx 0,1)
+ // are not in idxbx and stay unbounded (kUhInf sentinel set by codegen).
+ double lbx[3] = {stage_hdg_min, -use_rot_max, use_spd_min};
+ double ubx[3] = {stage_hdg_max,  use_rot_max, use_spd_max};
```

修复后 L1-T4 / L2-T1 应直接转 GREEN,且 production 的 live heading box 会真正约束 NLP。

### 5.2 P1: 重跑 P4 14-arm 消融

修复 LBX-len5 后,重新跑 spec §2 描述的 8 点 scan(target_y = 1852+{-548..+452})对比 P4 baseline (commit 2c031bc49)。预期 gap=+52 转回 raw=0 收敛。

### 5.3 P2: L0 degradation flag 下游消费

在 solver dispatch 路径读 `InputDegradation::any()`,把 degraded 信号映射到 `MidMpcSolution.status`(如 Converged → DegradedConverged)或在 rationale 字符串中附 `L0:{summary}`。完成后 L0-T4 RED → GREEN。

### 5.4 P3: soft_aspiration telemetry 全出口填充

把 `constraints_satisfied_` 的 `last_soft_aspiration_*` 计算提取为独立函数,在 solve() 的 NumericalFailure 出口也调用一次,确保 L2-T2 在 status!=0 时也有信号。

---

## 6. 实事求是声明

- **本报告基于 codegen-verified 在容器内实际跑出的测试结果**,不是凭直觉。
- **LBX-len5 缺陷通过 in-container C++ probe 直接验证**(不是猜的 acatos 语义)。
- **未做任何 bug 修复**(spec §7 明文禁止);4 个 RED test 是有意保留的 finding。
- **未改 production solve() 行为**: max_iter override 只走 diagnostic path(`debug_set_max_iter_diagnostic`),production solve() 路径完全不变。
- **scan 时间 36s 远低于 120s budget**,因为 MAX_ITER cap 让 fail-fast 路径加速。
- **L3 GATE closed 声明证伪**是基于 L1-T4 contract test 的代码证据,**不是借口**。

---

## 附录 H — LBX-len5 修复 + scan 诚实 RED 状态(2026-07-21 task 续)

### H.1 已落地的 production 修复

#### H.1.1 LBX-len5 production fix(`mid_mpc_acados_solver.cpp:1562-1565`)

**改动**: 把 stage 1..N 的 `lbx/ubx` 写入从长度 5(kAcadosNx)改为长度 3 紧凑数组(对齐 `idxbx=[2,3,4]`)。

```diff
- double lbx[kAcadosNx] = {-kUhInf, -kUhInf, stage_hdg_min, -use_rot_max, use_spd_min};
- double ubx[kAcadosNx] = {kUhInf,  kUhInf,  stage_hdg_max,  use_rot_max, use_spd_max};
+ constexpr int kNbx = 3;  // NBX at path stages (idxbx=[2,3,4]=psi,rot,spd).
+ double lbx[kNbx] = {stage_hdg_min, -use_rot_max, use_spd_min};
+ double ubx[kNbx] = {stage_hdg_max,  use_rot_max, use_spd_max};
```

**Phase 1 验证**(systematic-debugging Iron Law 满足):
- In-container C++ probe 直接测 `ocp_nlp_constraints_model_set("lbx", ptr)` 语义:
  - 写长度 5 数组 `[-1e10, -1e10, -0.5, -0.123, 0.7]` → 读回 `[-1e10, -1e10, -0.5]`(只取前 3 个)
  - 写长度 3 数组 `[-0.5, -0.123, 0.7]` → 读回 `[-0.5, -0.123, 0.7]` ✅
- Codegen 写入(`acatos_solver.c:687-700`)也是长度 NBX=3 紧凑数组 → 修复后 production 与 codegen 语义一致

**效果**:
- L1-T4 / L2-T1 contract test 从 RED 转 GREEN(live heading box 现在正确落在 psi slot)
- L1 全部 11/11 PASS
- L2-T1(GREEN)+ L2-T3(T1 fix 后可验证 warm-start)+ L2-T4/T5(layout)= 4/5 PASS,只剩 L2-T2 RED(soft_aspiration telemetry,与 LBX-len5 无关)

**未达成**: 没有让 Regression scan 完整 PASS。原因见 H.2。

### H.2 Scan 真实根因(超出 LBX-len5 范围)

#### H.2.1 scan 场景设计错误(已修)

原 scan 设计有 3 个错误:
1. 误激活 `colregs_conflict_active + primary_role=1(GIVE_WAY) + pref_dir=Starboard/Port` → `lateral_active=true` → 触发 min_alt schedule constraint row。P4 baseline 场景**不激活** COLREGs lateral(纯 CPA cost probe)。
2. target 放置坐标轴混淆(用 `x_m` 而非 P4 baseline 的 `y_m`)。
3. `max_iter=100` cap 扭曲数据: solver 跑到 cap 时把污染轨迹(traj_delta~162000 m)留在 shared capsule,影响后续 solve → 同输入跑两次结果不同(raw=2 vs raw=3)。

**修复**: 全部对齐 P4 baseline(test_mid_mpc_acados_solver.cpp `P5_ConvergenceBoundary_ScanTargetDistance`,commit 2c031bc49):
- 不设 colregs_*字段(lateral_active=false)
- target 放 `y_m`(north axis in test convention)
- 不 cap max_iter(用 default 400,scan 时间从 ~36s 涨到 ~170s 但数据可信)

#### H.2.2 真正的阻塞 — L3 HEAD production solver 退化

scan 修复后,8 点 sweep 数据(诚实):

| target_y | gap | P4 sqp_iter | P4 status | HEAD sqp_iter | HEAD status | 判定 |
|---|---|---|---|---|---|---|
| 2400 | -548 | 109 | 0 ✅ | 400 (MAX_ITER) | 2 | **退化** |
| 2100 | -248 | 135 | 0 ✅ | 400 (MAX_ITER) | 2 | **退化** |
| 1900 | -48 | 152 | 0 ✅ | 400 (MAX_ITER) | 2 | **退化** |
| 1800 | +52 | 129 | 0 ✅ | 400 (MAX_ITER) | 2 | **退化(S-T2 RED)** |
| 1700 | +152 | 112 | 0 ✅ | 400 (MAX_ITER) | 2 | **退化** |
| 1600 | +252 | 12 | 0 ✅ | 1 (QP fail) | 3 | **退化(S-T3 RED)** |
| 1500 | +352 | 5 | 3 ❌ | 1 (QP fail) | 3 | 持平(P4 也 fail) |
| 1200 | +652 | 1 | 3 ❌ | 1 (QP fail) | 3 | 持平 |

**核心事实**:
- P4 baseline 在 gap ≤ +152 时 ~110-150 iter 收敛;**HEAD 在 400 iter 都不收敛**(收敛带反转)。
- 既有 production test `PerTargetBreakdown_OneTargetSlackPositive`(test_mid_mpc_acados_solver.cpp:207, 同 P4 场景)也跑出 status=2 MAX_ITER,只是因为断言写成 `if (Converged) {...} else { SUCCEED(); }` 才 PASS。
- 这与 spec §0 提示词说的 "L3 HEAD 退化真实: 用 verified-correct codegen(NSH=0/NP=211)重跑,退化仍在(gap -548~+152 全 raw=2 sqp=400,+252/+352 raw=4)" **完全吻合**。

**根因不在 LBX-len5**: LBX-len5 修复让 L1-T4 转 GREEN,但 scan 结果**完全没变**(因为 scan 场景用 default heading box ±π,box live 块整个跳过,根本不走 LBX 写入路径)。

**根因定位**: L3 HEAD 的 SQP 收敛行为退化了。可能因素(未在本 task 调查):
- cost weight 变化(J_colreg / J_dist / J_route 的相对权重)
- QP tolerance
- seed strategy(F1 forward-propagated 是否还合适)
- Step5 方案 B(nsh=0 + J_colreg barrier)本身的收敛性

**调查这些超出 spec §7 诊断阶段范围**("❌ 未完成诊断前改 codegen default 或 solver production 路径")。

### H.3 当前 scan 状态(诚实)

| Test | 断言 | 当前状态 | 含义 |
|---|---|---|---|
| S-T1 EightPoints_LogOnly | 无(只 log) | PASS | 基础设施成立,数据捕获完整 |
| S-T2 Gap52_MustConverge | **raw==0 严格** | **RED** | L3 HEAD 退化的可执行证据(gap=+52 应收敛但 MAX_ITER) |
| S-T3 Gap252_MustConverge | **raw==0 严格** | **RED** | L3 HEAD 退化的可执行证据(gap=+252 边界失稳) |
| S-T4 Gap352_StatusDocumented | 任意 raw(文档化) | PASS | P4 也 fail 的 known boundary,仅记录 |

**scan 总状态**: 2/4 PASS, 2/4 RED。**这是诚实的、可执行的 L3 HEAD 退化证据**。

### H.4 下一 task 建议(P0 升级)

原 P0(LBX-len5)已完成。新的 P0 是 **调查 L3 HEAD SQP 收敛退化**:
1. 用 git bisect 找出哪个 commit 让 gap=+52 从 raw=0 转 raw=2
2. 对比 P4 baseline commit (2c031bc49) 与 L3 HEAD (fb84701b1) 的 solver/formulation diff
3. 怀疑方向: Step5 方案 B 的 nsh=0 + J_colreg barrier 是否在所有 gap 区域都收敛?P5 报告 §3 提到 14-arm 消融全部 fail,可能 Step5 方案 B 本身在 cost landscape 上引入了新的病态。

这个调查需要专门的 task,不是 mechanical fix。
