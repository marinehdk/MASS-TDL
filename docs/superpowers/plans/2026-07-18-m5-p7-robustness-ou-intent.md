# M5 P7 实施计划 — 鲁棒性扩展(OU 不确定性 + Intent 缩放 + BC 加速度优化)

> **Spec**: `docs/superpowers/specs/2026-07-18-m5-p7-robustness-ou-intent-design.md`
> **基线 HEAD**: `74f67e365`(P6 完成)
> **工作树**: `.worktrees/m5-design-grounding`(分支 `codex/m5-design-grounding`)
> **范围**: 全量(Q1),8 TDD tasks
> **验收门**: spec §6 共 8 条
> **强制**: 每子任务 TDD(RED→GREEN→REFACTOR),Task 8 强制 codex 对抗评审 + 完整 markdown 收尾报告
> **MPC 重构定位**: P7 是 P0–P7 收尾,完成后生成完整报告

---

## Task 依赖图

```
T1 (TargetState 3 字段) ──► T2 (OU 参数推导)
                              │
                              ▼
                   T3 (pack stride 5→8 + per-stage σ_pos)
                              │
                   ┌──────────┴──────────┐
                   ▼                     ▼
              T4 (UT expected cost)   T5 (intent 缩放)
                   │                     │
                   └──────────┬──────────┘
                              ▼
                   T6 (BC 加速度优化,独立)
                              │
                              ▼
                   T7 (SIL 三场景 + P5 回归)
                              │
                              ▼
                   T8 (codex review + 8 门 + 完整报告)
```

**顺序**: T1 → T2 → T3 → T4 + T5(可并行)→ T6 → T7 → T8。T6 与 T1–T5 独立,可早期并行启动。

---

## Task 1: TargetState 加 3 字段 + M5 填充(Q7)

**目标**: `TargetState` 加 `intent_confidence`/`target_compliance`/`classification`;`mid_mpc_node.cpp` `on_world_state_` 从 WorldState 填充。

### Step 1.1 — TargetState 加字段

**RED**: `test_mid_mpc_solver.cpp`(或新 `test_target_state.cpp`)加测试 `TargetState_P7Fields_DefaultValues`(新字段默认值正确)。
**GREEN**: `include/m5_tactical_planner/common/types.hpp` `TargetState` 加:
```cpp
double intent_confidence{0.5};
double target_compliance{0.5};
enum class Classification : std::uint8_t {
  Unknown = 0u, Vessel = 1u, FixedObject = 2u,
};
Classification classification{Classification::Unknown};
```

### Step 1.2 — M5 从 WorldState 填充

**RED**: 测试 `TargetState_P7Fields_FilledFromWorldState`(mock WorldState 含 intent_confidence=0.7/classification="vessel" → TargetState 正确填充)。
**GREEN**: `src/mid_mpc/mid_mpc_node.cpp` `on_world_state_`(L520 附近,现有 `ts.cpa_sigma_m = ...` 之后)加:
```cpp
ts.intent_confidence = static_cast<double>(tgt.intent_confidence);
ts.target_compliance = static_cast<double>(tgt.target_compliance);
if (tgt.classification == "vessel") {
  ts.classification = TargetState::Classification::Vessel;
} else if (tgt.classification == "fixed_object") {
  ts.classification = TargetState::Classification::FixedObject;
}
```

### Step 1.3 — T1 完成判据

- [ ] TargetState 加 3 字段 + Classification enum
- [ ] mid_mpc_node 填充逻辑
- [ ] 2 个新测试绿
- [ ] 现有 mid_mpc 测试不回归
- [ ] colcon test 全绿

---

## Task 2: OU 参数推导(Q3)

**目标**: 新建 `ou_uncertainty.hpp` 实装 OU 过程 + `derive_ou_params` 动态推导。

### Step 2.1 — OU 结构 + 推导函数

**RED**: 新建 `test_ou_uncertainty.cpp` 加测试:
- `DeriveOuParams_FixedObject_LowSigma`(固定物 σ_0=5m)
- `DeriveOuParams_VesselHighSpeedLowIntent_MaxSigma`(高速低 intent σ_0=100m)
- `SigmaPos_BoundedAbove`(σ_pos(t→∞) = σ_0·√2)
- `SigmaPos_MonotonicIncreasing`(σ_pos 随 t 单调增)

**GREEN**: 新建 `include/m5_tactical_planner/mid_mpc/ou_uncertainty.hpp`(spec §4.3 完整代码)。

`derive_ou_params` 实装 spec §4.3 推导规则表:
```cpp
OuUncertainty derive_ou_params(TargetState::Classification c, double sog, double intent_conf) {
  if (c == TargetState::Classification::FixedObject) {
    return {5.0, 1.0e9};  // 固定物:小 σ,τ=∞ 退化为常数
  }
  const bool high_speed = (sog > 5.0);
  const bool low_intent = (intent_conf < 0.3);
  if (c == TargetState::Classification::Vessel) {
    if (high_speed && low_intent) return {100.0, 300.0};
    if (high_speed)                return {50.0,  500.0};
    if (low_intent)                return {60.0,  400.0};
    return {30.0, 600.0};  // 低速高 intent
  }
  return {80.0, 400.0};  // Unknown 默认保守
}
```

### Step 2.2 — T2 完成判据

- [ ] `ou_uncertainty.hpp` 创建
- [ ] 4 个新测试绿(推导规则 + 有界性 + 单调性)
- [ ] colcon test 全绿

---

## Task 3: pack_parameters target stride 5→8 + per-stage σ_pos(Q7/G5)

**目标**: target stride 从 5 扩到 8;新增 per-stage per-target σ_pos slot;OU 参数 pack。

### Step 3.1 — 常量更新

**RED**: 测试 `PackParameters_TargetStride8`(np_global=154;target stride 字段正确)。
**GREEN**: `include/.../mid_mpc_acados_formulation.hpp`:
```cpp
static constexpr int32_t kGTargetStride = 8;  // P7: was 5
// np_global = 26 + 16*8 = 154
static constexpr int32_t kPSIdxSigmaPos = 37;  // P7: per-stage σ_pos 起点(现有 37 tb_x 之后)
// 每 stage 16 个 σ_pos(per-target),per-stage slot 总数 = 37 + 16 = 53
```

### Step 3.2 — pack_parameters 改造

**GREEN**: `src/mid_mpc/mid_mpc_acados_formulation.cpp` `pack_parameters`:
- target 循环加 3 字段(intent_confidence/target_compliance/classification)
- 新增 per-stage per-target σ_pos 循环(spec §4.5 代码)

### Step 3.3 — T3 完成判据

- [ ] `kGTargetStride=8`,`np_global=154`
- [ ] per-stage σ_pos slot(16 per stage)
- [ ] 测试 `PackParameters_TargetStride8` 绿
- [ ] colcon test 全绿

---

## Task 4: UT expected cost MX 实现(Q2/Q6,G3/G7)

**目标**: `build_colreg_cost_` 重写为 UT 5 sigma points expected cost。

### Step 4.1 — UT cost 实现

**RED**: `test_mid_mpc_acados_formulation.cpp` 加测试 `UT_ExpectedCost_MatchesOracle`:
- 固定 σ_pos 下,UT 5-point cost vs 解析均值(数值近似,容差 ±1e-3)
- σ_pos=0 时,UT cost == 确定性 cost(退化验证)

**GREEN**: `src/mid_mpc/mid_mpc_acados_formulation.cpp` `build_colreg_cost_` 重写(spec §4.4 完整代码)。

### Step 4.2 — codegen SX parity

**RED**: 测试 `CodegenSX_MX_Parity_P7`(SX codegen cost == MX cost,±1e-6)。
**GREEN**: 同步更新 `test/external/acados_backend/gen_mid_mpc_acados.py`(SX 表达式与 MX 一致)。

### Step 4.3 — T4 完成判据

- [ ] `build_colreg_cost_` UT 5 sigma points 实现
- [ ] 测试 `UT_ExpectedCost_MatchesOracle` 绿
- [ ] 测试 `CodegenSX_MX_Parity_P7` 绿
- [ ] colcon test 全绿

---

## Task 5: intent 乘性缩放(Q4,G4)

**目标**: colreg cost 加 intent_confidence 乘性缩放因子。

### Step 5.1 — intent 缩放实现

**RED**: 测试 `IntentScale_LowConfHigherCost`(同 target,intent_conf=0.1 cost > intent_conf=0.9 cost)。
**GREEN**: `build_colreg_cost_` 的 `tw` 改为(spec §4.4):
```cpp
const casadi::MX tw = tw_base * (1.0 + k_intent * (1.0 - intent_conf));
```
`kIntentScale=1.0` 加入 config。

### Step 5.2 — T5 完成判据

- [ ] intent 缩放实现
- [ ] 测试 `IntentScale_LowConfHigherCost` 绿
- [ ] colcon test 全绿

---

## Task 6: BC-MPC 加速度优化(Q5,G6)

**目标**: BC-MPC 加减速避让(Override + CPA 低时减速)。

### Step 6.1 — BC 加速度决策

**RED**: `test_bc_mpc_solver.cpp` 加测试:
- `AccelOpt_DecelOnOverrideLowCpa`(Override + worst_cpa < 0.7·cpa_safe → optimal_speed = 0.5·u)
- `AccelOpt_HoldOnResolved`(Resolved → optimal_speed = u)
- `AccelOpt_HoldOnOverrideHighCpa`(Override + worst_cpa ≥ 0.7·cpa_safe → optimal_speed = u)

**GREEN**: `src/bc_mpc/bc_mpc_solver.cpp` `solve`(spec §4.6 代码):
```cpp
if (sol.status == BcMpcSolution::Status::Override) {
  const double cpa_threshold = input.cpa_safe_m * cfg_.decel_trigger_ratio;
  if (sol.worst_case_cpa_m < cpa_threshold) {
    sol.optimal_speed_mps = input.own_ship.u_mps * cfg_.decel_factor;
    sol.trigger_reason = "CONDITION_A_DECEL";
  } else {
    sol.optimal_speed_mps = input.own_ship.u_mps;
  }
} else {
  sol.optimal_speed_mps = input.own_ship.u_mps;
}
```

### Step 6.2 — 配置参数

**GREEN**: `m5_params.yaml` `bc_mpc.ros__parameters` 加:
```yaml
decel_trigger_ratio: 0.7
decel_factor: 0.5
```

### Step 6.3 — T6 完成判据

- [ ] BC 加速度优化实现
- [ ] 3 个新测试绿
- [ ] 现有 BC 测试不回归(P6 handover/health 不变)
- [ ] colcon test 全绿

---

## Task 7: SIL 三场景验证 + P5 回归(Q8,G8)

**目标**: 三场景对比 + P5 ample-time baseline 回归。

### Step 7.1 — 场景 1:低 σ ample-time(P5 回归)

```bash
# target_y=2500m,Vessel 高 intent(intent_conf=0.8)
# 验证:σ_pos 小 → UT cost ≈ 确定性 → 收敛性与 P5 一致
ros2 topic echo /l3/m5/avoidance_plan  # 轨迹
# solve duration,SQP iter,收敛状态
```

**判据**:status=Converged,sqp_iter 与 P5 baseline 差异 < 20%。

### Step 7.2 — 场景 2:高 σ 近距(OU 膨胀效果)

```bash
# target_y=2500m,Vessel 低 intent(intent_conf=0.2)
# 验证:σ_pos 大 → UT cost 高 → solver 主动远离 → lateral offset 比 baseline 大
```

**判据**:Converged,轨迹 lateral offset > 场景 1(OU 膨胀生效)。

### Step 7.3 — 场景 3:多船高速(UT 计算可行性)

```bash
# 3 targets,混合 classification/sog/intent
# 验证:5 sigma points × 3 targets × N stages 实时性
```

**判据**:solve duration < SLA(参考 P6 IPOPT max 8.5s),所有 target cost 正确。

### Step 7.4 — 证据输出

```bash
# 写入 evidence JSON(三场景)
cp /tmp/sil_capture.json runs/p7_sil_scenario_<n>_<timestamp>.json
```

包含:
- 主题捕获(avoidance_plan/reactive_override_cmd/bc_mpc/health)
- 轨迹序列(lat/lon per step)
- solve duration + sqp_iter + status
- σ_pos 序列(per target per stage)
- intent_confidence/target_compliance

### Step 7.5 — T7 完成判据

- [ ] 场景 1:P5 ample-time 不回归(sqp_iter 差异 < 20%)
- [ ] 场景 2:OU 膨胀生效(lateral offset > 场景 1)
- [ ] 场景 3:多船 UT 实时性(solve < SLA)
- [ ] evidence JSON 三份写入 `runs/`

---

## Task 8: codex adversarial review + 8 验收门 + 完整 markdown 报告

### Step 8.1 — 完整 regression

```bash
colcon test --packages-select m5_tactical_planner
colcon test --packages-select l3_msgs  # 如有 msg 改动
# IPOPT 回归(P6 参数修复不破坏)
# acados codegen parity
```

**判据**:全绿,无回归。

### Step 8.2 — codex 对抗评审(强制)

调用 codex 评审 P7 全部 diff,关注:
- UT 5 sigma points 数值稳定性(α=1e-3 中心权重极小)
- target stride 5→8 对现有 codegen/IPOPT parity 的影响
- OU 参数推导规则物理合理性
- intent 缩放可被压过(risk cost 总能压过 intent 权重?)
- BC 加速度优化与 P6 FINAL_DEGRADE/takeover 条件的交互
- per-stage σ_pos pack 时序竞态
- MX 原生 UT 在 acados codegen 的可微性

**判据**:0 Critical 通过。

### Step 8.3 — 8 验收门复核(spec §6)

逐门验证 + evidence 汇总。

### Step 8.4 — ⭐ 完整 markdown 收尾报告(P0–P7)

**这是 P7 作为 MPC 重构收尾的核心交付物**。生成 `docs/superpowers/specs/2026-07-18-m5-mpc-p0-p7-implementation-report.md`,参考 4 篇文献的**图表/表格形式**(spec §11):

报告结构:
1. **执行摘要**(11 VR 决策全落地,0 悬空)
2. **参考文献评价形式映射**([E1][E2][E3][RMD] 分工)
3. **P0–P7 决策点全景**(11 VR → phase → commit → 依据)
4. **决策点逐项验证**(对照论文方程,"论文方法 → 落地实现 → 验证结论")
5. **P7 鲁棒性扩展单独说明**(明确 [RMD] Ch3 依据,非 Eriksen 方法)
6. **参数对收敛影响**(参考 [RMD] Ch2 + [E1] Fig 5 形式:σ_0/intent_conf/decel_factor vs sqp_iter 曲线)
7. **轨迹对比**(参考 [E1] Fig 6:P5 baseline vs P7 三场景轨迹图)
8. **ample-time 边界**(收敛边界表)
9. **测试覆盖**(9 维度闭环 + 测试矩阵)
10. **待办与开放项**(海试/认证)

**评价形式来源**:
- 轨迹图:[E1] Fig 6(North-East 坐标 + ownship/obstacle 轨迹)
- 参数影响曲线:[E1] Fig 5(ROT vs cost)+ [RMD] Ch2(参数收敛性)
- 状态轨迹:[E2] 状态机转换图
- ample-time 边界表:[E2] §V 完整遭遇生命周期

### Step 8.5 — 文档同步

- 更新 `M5-progress.md`
- 更新 roadmap §3.2 P7 状态("✅ 完成")
- 更新 `handoff/workspace_log.md`

### Step 8.6 — T8 完成判据(P7 + MPC 重构最终 DONE)

- [ ] 全 regression 绿
- [ ] codex review 0 Critical
- [ ] 8 验收门全绿 + evidence
- [ ] **完整 markdown 收尾报告生成**(P0–P7,含论文图表/表格评价形式)
- [ ] 文档同步(M5-progress + roadmap + handoff)
- [ ] commit + push(经 acceptance gate)

---

## 验收门 ↔ Task 映射(spec §6)

| 验收门 | Task |
|---|---|
| G1 TargetState 3 字段 + 填充 | T1 |
| G2 OU 参数推导正确 | T2 |
| G3 UT expected cost 数值正确 | T4 |
| G4 intent 缩放生效 | T5 |
| G5 pack stride 8 正确 | T3 |
| G6 BC 加速度优化生效 | T6 |
| G7 codegen SX/MX parity | T4 |
| G8 SIL 三场景 + P5 回归 | T7 |

---

## 风险触发回退

| 触发 | 回退动作 |
|---|---|
| T4 UT 5 sigma points 数值不稳定 | 降为 3 sigma points(中心 + 单轴 ±σ) |
| T4 codegen parity 失败 | 检查 SX 表达式顺序;必要时 UT 在 codegen 静态展开 |
| T7 场景 3 UT 实时性超 SLA | 减少 sigma points 或限制 max_targets < 16 |
| T7 场景 1 P5 ample-time 回归 | σ=0 退化验证;OU 参数 σ_0 下调 |
| T7 场景 2 OU 膨胀不生效 | kIntentScale 上调;σ_0 上调 |
| T8 codex Critical | 修后重跑,不允许带 Critical 完成 |

---

## 实施纪律

- **TDD 强制**: 每子任务先 RED 再 GREEN 再 REFACTOR
- **不引入 mock/skip/forced PASS**(AGENTS.md COLREGs full-chain rule)
- **每 task 自闭环**: colcon test 全绿才能进下一 task
- **codex review 强制**(Task 8 Step 8.2)
- **evidence 落盘**: T7 三场景 evidence JSON 写入 `runs/`
- **完整收尾报告强制**(Task 8 Step 8.4):P0–P7 markdown,含论文图表/表格评价形式
- **handoff 记录**: P7 完成后更新 `handoff/workspace_log.md`
