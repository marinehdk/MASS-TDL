# M5 P7 — 鲁棒性扩展(OU 不确定性 + Intent 缩放 + BC 加速度优化)设计

> **产出**: 2026-07-18,P7 brainstorming 落 spec。
> **工作树**: `.worktrees/m5-design-grounding`(分支 `codex/m5-design-grounding`)
> **前序**: P6(`74f67e365`,BC-MPC 激活 + 11 状态交接机,8 验收门全绿,codex 0 Critical)
> **来源裁决**: roadmap §P7 + colav-design-log VR-09 + 2026-07-18 brainstorming 9 个 Q&A
> **理论依据**: Rawlings-Mayne-Diehl《MPC: Theory, Computation, and Design》2nd Ed **Ch3 Robust and Stochastic MPC**(p193);**非 Eriksen 系列方法,属工程扩展**
> **MPC 重构定位**: P7 是 P0–P7 MPC 避碰重构的**收尾阶段**

---

## 1. 权威性与边界声明

### 1.1 文档分工

| 文档 | 管什么(权威) | 不管什么 |
|---|---|---|
| **本 spec(P7)** | OU 横向方差 + UT expected cost + intent 缩放 + BC 加速度优化 / 数据通道修复 / 验收门 | 论文图表/表格评价形式(实现后报告) |
| `specs/2026-07-17-m5-mpc-p0-p7-roadmap.md` §P7 | phase scope / 依赖 / 风险 | 工程细节(本 spec) |
| `specs/2026-07-16-m5-mpc-colav-solution-pack.md` VR-09 | "Mid 用 A+(OU+intent_confidence);BC Nominal" 裁决 | 工程实现(本 spec) |
| `design-logs/2026-07-16-m5-mpc-colav-design-log.md` | 决策树全流程 | phase 归属(roadmap) |

### 1.2 ⚠ 关键边界声明(避免方法混淆)

P7 的 OU/UT/intent 决策**不来自** Eriksen 系列 3 篇论文([E1][E2][E3]),而是来自 **Rawlings-Mayne-Diehl 教材 Ch3 Robust and Stochastic MPC**。这是在 Eriksen Mid-MPC 基础架构上的**鲁棒性工程扩展**。

| 方法 | 来源 | 归属 |
|---|---|---|
| Kinematic model + pseudo-Huber + ROT penalty + relative track | [E1][E2] Eriksen | P0–P6 已落地 |
| BC-MPC branching + 椭圆 COLREGs penalty + handover 状态机 | [E3] Eriksen | P6 已落地 |
| **OU 横向方差 + UT expected cost** | **[RMD] Ch3.7 Stochastic MPC** | **P7(本 spec)** |
| **intent_confidence 乘性缩放** | **[E3] time-dependent weighting 启发** | **P7(本 spec)** |
| **BC 加速度优化** | **[E1] Eq 13 SOG 约束** | **P7(本 spec)** |

P0–P6 是 Eriksen 方法落地;P7 是 [RMD] 鲁棒性扩展。两者在报告中分别说明,不混淆。

### 1.3 P7 是 MPC 重构收尾

P7 完成后,P0–P7 MPC 避碰重构**全量闭环**。收尾报告(spec 实现后生成)将以 4 篇参考文献的**图表/表格形式**(参数对收敛影响曲线、状态轨迹图、代价函数曲面等)作为评价依据,证明 MPC 模块有效且闭环。

---

## 2. P7 起点证据(2026-07-18 codegraph + 代码探索)

### 2.1 数据通道现状(关键发现)

| 字段 | M2 状态 | WorldState 传输 | M5 TargetState 状态 |
|---|---|---|---|
| `cpa_sigma_m`(CPA 1σ 不确定性) | ✅ `cpa_tcpa_calculator` 算好 | ✅ `cpa_covariance_m2` | ✅ 有(`mid_mpc_node.cpp:532`),**NLP 未消费** |
| `intent_confidence`(rule-based) | ✅ `world_state_aggregator.cpp:354`(0.05–0.95) | ✅ TrackedTarget 字段 | ❌ **M5 TargetState 无此字段** |
| `target_compliance`(CPA/range trend) | ✅ `world_state_aggregator.cpp:362` | ✅ TrackedTarget 字段 | ❌ **M5 TargetState 无此字段** |
| `classification`(vessel/fixed/unknown) | ✅ string | ✅ TrackedTarget 字段 | ❌ **M5 TargetState 无此字段** |
| `ship_type`(油轮/渔船等) | ❌ 不存在 | ❌ | ❌ |

**结论**:OU/intent 的数据通道**一半已存在**(cpa_sigma 由 M2 算好),P7 需补全 M5 TargetState 字段 + 把它接入 NLP。

### 2.2 NLP 代价函数现状(P7 改造点)

`mid_mpc_acados_formulation.cpp:357-375` `build_colreg_cost_`:
```cpp
// 当前形式:smooth exp barrier,无不确定性,无 intent 缩放
cost = cost + tw * casadi::MX::exp(-zeta * (d - cpa));  // L372
// tw = range-ramp weight(L366);d = own-target 距离(L371);cpa = cpa_safe(L361)
```

**P7 改造**:
- `d` 改为 UT expected cost(5 sigma points 位置扰动均值)
- `tw` 乘以 intent 缩放因子 `(1 + k_intent·(1-intent_confidence))`
- target stride 从 5 扩到 8(加 intent_conf/compliance/classification 编码)

### 2.3 BC-MPC 现状(Q5 改造点)

`bc_mpc_solver.cpp:24-25`:
```cpp
sol.optimal_speed_mps  = input.own_ship.u_mps;  // Phase E1 speed hold(恒速)
sol.rot_cmd_rad_s      = 0.0;
```

**P7 改造**:BC-MPC 加速度优化(减速避让),与 [E1] Eq 13 SOG 约束一致。

---

## 3. 决策汇总(2026-07-18 brainstorming 9 个 Q&A)

| # | 决策 | 选项 | 依据 |
|---|---|---|---|
| Q1 | 范围 | **全量**(OU + intent + BC Nominal) | 用户明确 |
| Q2 | OU 接入形式 | **OU 横向方差 + UT expected cost**(5 sigma points) | [RMD] Ch3.7 stochastic + Unscented transform |
| Q3 | OU 参数来源 | **从 target 类型动态推导**(classification + intent_conf + sog;无 ship_type) | [E3] time-dependent weighting 启发 |
| Q4 | Intent 缩放形式 | **intent_confidence 乘性缩放 cost** | [E3] wi(t) |
| Q5 | BC Nominal 范围 | **加速度优化**(BC 加 speed 优化,不加 OU) | [E1] Eq 13 |
| Q6 | UT 实现方式 | **MX 原生 UT**(5 sigma points,保持可微 + codegen) | [RMD] Ch3.7 + acados MX |
| Q7 | 数据通道修复 | **M5 TargetState 加 3 字段**(intent_confidence + target_compliance + classification) | M2 已算,通道补全 |
| Q8 | SIL 验证场景 | **三场景对比 + P5 baseline** | [E1] Fig 6 评价形式 |
| Q9 | 认证 | **推后**,先保证 MPC 可控可用 | 用户明确 |

---

## 4. 架构与组件设计

### 4.1 总体架构(P7 在 Mid-MPC/BC-MPC 中的位置)

```
┌─────────────────────────────────────────────────────────────┐
│ M2 World Model(已落地)                                      │
│  cpa_tcpa_calculator → cpa_sigma_m                            │
│  world_state_aggregator → intent_confidence, target_compliance│
│  TrackedTarget.classification                                  │
└──────────────┬──────────────────────────────────────────────┘
               │ WorldState msg(已有字段,P7 只修 M5 侧消费)
               ▼
┌─────────────────────────────────────────────────────────────┐
│ M5 Mid-MPC(P7 改造)                                          │
│  TargetState 加 3 字段(Q7)                                   │
│  ↓                                                             │
│  pack_parameters:target stride 5→8                            │
│  ↓                                                             │
│  OU 参数推导(Q3):σ_pos(t),τ_OU 从 classification+sog 算     │
│  ↓                                                             │
│  build_colreg_cost_(P7 重写):                                 │
│    UT expected cost(5 sigma points,Q2/Q6)                    │
│    × intent 乘性缩放(Q4)                                      │
└──────────────┬──────────────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────────┐
│ M5 BC-MPC(P7 Q5 改造)                                        │
│  bc_mpc_solver:加速度优化(减速避让)                          │
│  branch_formulation:加 speed branch                           │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 数据通道修复(Q7)

**改动文件**:
- `include/m5_tactical_planner/common/types.hpp`(`TargetState` 加 3 字段)
- `src/mid_mpc/mid_mpc_node.cpp`(`on_world_state_` 填充新字段)

**TargetState 加字段**:
```cpp
struct TargetState {
  // ... 现有字段 ...
  double cpa_sigma_m{0.0};      // 已有,M2 算好
  Intent predicted_intent{Intent::Unknown};  // 已有

  // P7 新增(Q7)
  double intent_confidence{0.5};     // [0,1],M2 rule-based
  double target_compliance{0.5};     // [0,1],M2 CPA/range trend
  enum class Classification : std::uint8_t {
    Unknown     = 0u,
    Vessel      = 1u,
    FixedObject = 2u,
  };
  Classification classification{Classification::Unknown};
};
```

**mid_mpc_node.cpp on_world_state_ 填充**:
```cpp
ts.intent_confidence = static_cast<double>(tgt.intent_confidence);
ts.target_compliance = static_cast<double>(tgt.target_compliance);
if (tgt.classification == "vessel") {
  ts.classification = TargetState::Classification::Vessel;
} else if (tgt.classification == "fixed_object") {
  ts.classification = TargetState::Classification::FixedObject;
}  // else Unknown
```

### 4.3 OU 参数动态推导(Q3)

**新建**:`include/m5_tactical_planner/mid_mpc/ou_uncertainty.hpp`

```cpp
// P7:Ornstein-Uhlenbeck 横向位置不确定性([RMD] Ch3.7 stochastic)
// σ_pos²(t) = σ_0² · (1 - exp(-2t/τ_OU)),有界上限 σ_0·√2
struct OuUncertainty {
  double sigma_0_m;     // 初始横向位置不确定性 [m]
  double tau_OU_s;      // OU 时间常数 [s]

  // σ_pos(t):OU 过程横向位置标准差
  [[nodiscard]] double sigma_pos_m(double t_s) const noexcept {
    return sigma_0_m * std::sqrt(1.0 - std::exp(-2.0 * t_s / tau_OU_s));
  }
};

// P7 Q3:从 target 类型动态推导 OU 参数
// classification + sog + intent_confidence → (sigma_0, tau_OU)
[[nodiscard]] OuUncertainty derive_ou_params(
    TargetState::Classification classification,
    double sog_mps,
    double intent_confidence) noexcept;
```

**推导规则**(基于 [E3] time-dependent weighting + 物理直觉,可调):

| classification | sog | intent_conf | σ_0 | τ_OU | 理由 |
|---|---|---|---|---|---|
| FixedObject | — | — | 5m | ∞(退化为常数) | 固定物无运动不确定性 |
| Vessel | <5 m/s(低速) | >0.7 | 30m | 600s | 低速+高 intent = 可预测 |
| Vessel | <5 m/s | <0.3 | 60m | 400s | 低速+低 intent = 不可预测 |
| Vessel | >5 m/s(高速) | >0.7 | 50m | 500s | 高速+高 intent |
| Vessel | >5 m/s | <0.3 | 100m | 300s | 高速+低 intent = 最不可预测 |
| Unknown | — | — | 80m | 400s | 默认保守 |

**默认值来源**:工程经验 + [RMD] Ch3.7 OU 过程典型参数范围;**SIL 校准 + 海试 TBD 补全**。

### 4.4 UT expected cost(Q2/Q6)

**改动文件**:`src/mid_mpc/mid_mpc_acados_formulation.cpp` `build_colreg_cost_`

**当前**(L357-375):
```cpp
cost = cost + tw * exp(-zeta * (d - cpa));
```

**P7 重写**(MX 原生 UT,5 sigma points):
```cpp
casadi::MX MidMpcAcadosFormulation::build_colreg_cost_() const {
  const int32_t Nt = std::max(cfg_.max_targets, 1);
  const casadi::MX px = x_(0);
  const casadi::MX py = x_(1);
  const casadi::MX cpa = gslot_(kGIdxCpaSafe);
  const casadi::MX zeta = casadi::DM(kZeta);
  casadi::MX cost(0.0);

  for (int32_t t = 0; t < cfg_.max_targets; ++t) {
    const int32_t base = kGIdxTargets + t * kGTargetStride;  // P7: stride=8
    const casadi::MX tw_base = gslot_at(p_global_, base + 4);  // range-ramp
    const casadi::MX tx_at_k = target_x_at_k_slot_(t);
    const casadi::MX ty_at_k = target_y_at_k_slot_(t);

    // P7 Q4:intent 乘性缩放(intent_confidence 低 → cost 增加)
    const casadi::MX intent_conf = gslot_at(p_global_, base + 5);  // [0,1]
    const casadi::MX k_intent = casadi::DM(kIntentScale);
    const casadi::MX tw = tw_base * (1.0 + k_intent * (1.0 - intent_conf));

    // P7 Q2/Q6:UT expected cost(5 sigma points)
    // σ_pos(t,k) 从 OU 参数算,pack 阶段写入 per-stage per-target slot
    const casadi::MX sigma_pos = gslot_at(p_per_stage_, kPSIdxSigmaPos + t);  // per-target at this stage
    // Sigma points(2D 位置,n=2 → 2n+1=5 points):
    //   point 0: (tx, ty) weight w0=2/(n+λ)
    //   point 1: (tx+σ, ty) weight 1/(2(n+λ))
    //   point 2: (tx-σ, ty) weight 1/(2(n+λ))
    //   point 3: (tx, ty+σ) weight 1/(2(n+λ))
    //   point 4: (tx, ty-σ) weight 1/(2(n+λ))
    // λ = α²(n+κ) - n, α=1e-3, κ=0(标准 UT 参数)
    const casadi::DM lambda = casadi::DM(1.0e-6);  // α²(n+κ)-n, n=2, α=1e-3, κ=0
    const casadi::DM w0 = lambda / (2.0 + lambda);
    const casadi::DM w1 = 1.0 / (2.0 * (2.0 + lambda));
    const double scale = std::sqrt(2.0 + lambda);  // √(n+λ)

    auto d_at = [&](const casadi::MX& sx, const casadi::MX& sy) {
      const casadi::MX dx = px - sx;
      const casadi::MX dy = py - sy;
      return casadi::MX::sqrt(dx * dx + dy * dy + kSqrtGuard);
    };

    const casadi::MX d0 = d_at(tx_at_k, ty_at_k);
    const casadi::MX d1 = d_at(tx_at_k + scale * sigma_pos, ty_at_k);
    const casadi::MX d2 = d_at(tx_at_k - scale * sigma_pos, ty_at_k);
    const casadi::MX d3 = d_at(tx_at_k, ty_at_k + scale * sigma_pos);
    const casadi::MX d4 = d_at(tx_at_k, ty_at_k - scale * sigma_pos);

    const casadi::MX exp_cost = w0 * casadi::MX::exp(-zeta * (d0 - cpa))
                              + w1 * (casadi::MX::exp(-zeta * (d1 - cpa))
                                    + casadi::MX::exp(-zeta * (d2 - cpa))
                                    + casadi::MX::exp(-zeta * (d3 - cpa))
                                    + casadi::MX::exp(-zeta * (d4 - cpa)));
    cost = cost + tw * exp_cost;
  }
  return cost / casadi::DM(static_cast<double>(Nt));
}
```

**关键设计点**:
- UT 5 sigma points(n=2 位置维度):中心 + ±σ 双轴
- `σ_pos` 是 per-stage 参数(OU 随时间增长),通过新 per-stage slot `kPSIdxSigmaPos` 传入
- intent 缩放乘在 base weight `tw` 上
- MX 原生实现保持 acados codegen 可微性

### 4.5 pack_parameters 改造(target stride 5→8)

**改动文件**:`src/mid_mpc/mid_mpc_acados_formulation.cpp` `pack_parameters`

```cpp
// P7:target stride 5→8
// [0] x_m, [1] y_m, [2] cog_rad, [3] sog_mps, [4] w_range
// [5] intent_confidence(新), [6] target_compliance(新,备 Q4 扩展), [7] classification(新)
for (int32_t t = 0; t < n_t; ++t) {
  const auto& tgt = input.targets[static_cast<std::size_t>(t)];
  const std::size_t base = static_cast<std::size_t>(kGIdxTargets + t * kGTargetStride);
  g[base + 0u] = tgt.x_m;
  g[base + 1u] = tgt.y_m;
  g[base + 2u] = tgt.cog_rad;
  g[base + 3u] = tgt.sog_mps;
  // w_range(现有)
  const double rng0 = std::hypot(tgt.x_m - input.own_ship.x_m, tgt.y_m - input.own_ship.y_m);
  const double pwt_inner = input.constraints.cpa_safe_m;
  const double span = std::max(kPwtOuterM - pwt_inner, 1.0);
  g[base + 4u] = std::clamp((kPwtOuterM - rng0) / span, 0.0, 1.0);
  // P7 新增
  g[base + 5u] = tgt.intent_confidence;  // [0,1]
  g[base + 6u] = tgt.target_compliance;  // [0,1]
  g[base + 7u] = static_cast<double>(static_cast<std::uint8_t>(tgt.classification));
}

// P7:per-stage per-target σ_pos slot(OU 参数推导 + 每个 stage 每 target 的 σ_pos(t))
// 每 stage 写入 16 个 σ_pos(每 target 一个),位于 p_per_stage 的 kPSIdxSigmaPos 偏移
for (int32_t k = 0; k <= cfg_.n_horizon; ++k) {
  const double t_s = static_cast<double>(k) * cfg_.dt_s;
  for (int32_t t = 0; t < n_t; ++t) {
    const auto& tgt = input.targets[static_cast<std::size_t>(t)];
    const auto ou = derive_ou_params(tgt.classification, tgt.sog_mps, tgt.intent_confidence);
    ps[k][kPSIdxSigmaPos + t] = ou.sigma_pos_m(t_s);  // per-target σ_pos at stage k
  }
  // 未使用的 target slot 填 0(σ=0 退化为单点 = 确定性 cost)
}
```

**常量更新**:
- `kGTargetStride`: 5 → **8**
- `kAcadosNpGlobalHeadScalars` 不变(26)
- `np_global` = 26 + 16·8 = **154**(was 26+16·5=106)
- 新增 per-stage slot:`kPSIdxSigmaPos`(per-stage target σ_pos,16 个/舞台)

### 4.6 BC-MPC 加速度优化(Q5)

**改动文件**:
- `src/bc_mpc/bc_mpc_solver.cpp`(加速度决策)
- `src/bc_mpc/bc_mpc_branch_formulation.cpp`(加 speed branch)
- `include/.../bc_mpc/bc_mpc_collision_detector.hpp`(evaluator 返回 optimal speed)

**当前**(恒速):
```cpp
sol.optimal_speed_mps = input.own_ship.u_mps;  // Phase E1 speed hold
sol.rot_cmd_rad_s = 0.0;
```

**P7 改造**(减速避让):
```cpp
// bc_mpc_solver.cpp
BcMpcSolution BcMpcSolver::solve(const BcMpcInput& input) {
  BcMpcSolution sol = detector_.evaluate(input);  // evaluate 现在返回 heading + speed
  // ... timing ...

  // P7 Q5:加速度优化(减速避让)
  // 当 worst_case_cpa < cpa_safe · kDecelTriggerRatio 时,减速到 kDecelFactor · u
  if (sol.status == BcMpcSolution::Status::Override) {
    const double cpa_threshold = input.cpa_safe_m * cfg_.decel_trigger_ratio;
    if (sol.worst_case_cpa_m < cpa_threshold) {
      sol.optimal_speed_mps = input.own_ship.u_mps * cfg_.decel_factor;
      sol.trigger_reason = "CONDITION_A_DECEL";  // 减速避让标记
    } else {
      sol.optimal_speed_mps = input.own_ship.u_mps;  // 仅 heading override
    }
  } else {
    sol.optimal_speed_mps = input.own_ship.u_mps;  // Resolved/Normal 保持
  }
  sol.rot_cmd_rad_s = 0.0;  // ROT 仍由 heading override 表达(不变)
  // ...
}
```

**新参数**(`m5_params.yaml` `bc_mpc` 节):
```yaml
bc_mpc:
  ros__parameters:
    # ... 现有 ...
    # P7 Q5:加速度优化
    decel_trigger_ratio: 0.7   # worst_cpa < cpa_safe·0.7 时触发减速
    decel_factor: 0.5          # 减速到 50% 当前速度
```

---

## 5. 关键参数与阈值

### 5.1 OU 参数(Q3,可调)

| 参数 | 默认值 | 含义 | 来源 |
|---|---|---|---|
| σ_0(Vessel 低速高 intent) | 30m | 初始横向位置 σ | §4.3 推导规则 |
| σ_0(Vessel 高速低 intent) | 100m | 最不可预测 | §4.3 |
| τ_OU | 300–600s | OU 时间常数 | §4.3 |
| σ_pos 有界上限 | σ_0·√2 | OU 性质 | [RMD] Ch3.7 |

### 5.2 UT 参数(Q6)

| 参数 | 默认值 | 含义 |
|---|---|---|
| n | 2 | 位置维度(2D) |
| α | 1e-3 | UT spread(标准) |
| κ | 0 | UT secondary(标准) |
| λ = α²(n+κ)−n | 1e-6 | UT scaling |
| sigma points | 5(2n+1) | n=2 |
| w0(中心) | λ/(n+λ) | UT weight |
| w1(偏移) | 1/(2(n+λ)) | UT weight |

### 5.3 Intent 缩放(Q4)

| 参数 | 默认值 | 含义 |
|---|---|---|
| `kIntentScale` | 1.0 | intent_conf=0 时 cost 翻倍;=1 时无影响 |

### 5.4 BC 加速度(Q5)

| 参数 | 默认值 | 含义 |
|---|---|---|
| `decel_trigger_ratio` | 0.7 | worst_cpa < cpa_safe·0.7 触发减速 |
| `decel_factor` | 0.5 | 减速到 50% |

### 5.5 Target stride(Q7)

| 参数 | 值 | 含义 |
|---|---|---|
| `kGTargetStride` | 8(was 5) | per-target global slot 数 |
| `np_global` | 154(was 106) | 全局参数数 |

---

## 6. 验收门(8 条,P7 自闭环)

| # | 门 | 验证方式 |
|---|---|---|
| **G1** | TargetState 加 3 字段 + M5 从 WorldState 填充 | unit test:mock WorldState → TargetState.intent_confidence/compliance/classification 正确 |
| **G2** | OU 参数推导正确 | unit test:`derive_ou_params` 各 classification/sog/intent 组合返回预期 (σ_0, τ_OU);σ_pos(t) 有界 |
| **G3** | UT expected cost 数值正确 | unit test(oracle):固定 σ_pos 下,UT 5-point 均值 vs 解析期望(Jensen 不等式验证) |
| **G4** | intent 缩放生效 | unit test:intent_conf=0.1 cost > intent_conf=0.9 cost(同 target) |
| **G5** | pack_parameters target stride 8 正确 | unit test:stride 8 字段正确打包;np_global=154 |
| **G6** | BC 加速度优化生效 | unit test:Override + worst_cpa < 0.7·cpa_safe → optimal_speed = 0.5·u;Resolved → 保持 |
| **G7** | codegen SX/MX parity | formulation test:SX codegen cost == MX cost(±1e-6) |
| **G8** | SIL 三场景对比 + P5 baseline | 三场景收敛性 + 轨迹对比,P5 ample-time 不回归 |

**额外自闭环要求**(与 P0–P6 一致):
- 所有 touched 代码 unit test 绿
- P5 ample-time 场景不回归(target_y>2000m 仍收敛)
- P6 BC-MPC 测试不回归
- Mandatory codex adversarial review

---

## 7. 测试矩阵

### 7.1 Unit tests

| 测试文件 | 新增测试 | 覆盖门 |
|---|---|---|
| `test_target_state.cpp`(新或加到现有) | `TargetState_P7Fields_FilledFromWorldState` | G1 |
| `test_ou_uncertainty.cpp`(新) | `DeriveOuParams_AllCombinations` | G2 |
| `test_ou_uncertainty.cpp` | `SigmaPos_BoundedAbove` | G2 |
| `test_mid_mpc_acados_formulation.cpp` | `UT_ExpectedCost_MatchesOracle` | G3 |
| `test_mid_mpc_acados_formulation.cpp` | `IntentScale_LowConfHigherCost` | G4 |
| `test_mid_mpc_acados_formulation.cpp` | `PackParameters_TargetStride8` | G5 |
| `test_mid_mpc_acados_formulation.cpp` | `CodegenSX_MX_Parity_P7` | G7 |
| `test_bc_mpc_solver.cpp` | `AccelOpt_DecelOnOverrideLowCpa` | G6 |
| `test_bc_mpc_solver.cpp` | `AccelOpt_HoldOnResolved` | G6 |

### 7.2 SIL 三场景验证(Q8)

参考 [E1] Fig 6(轨迹图)+ [RMD] Ch2(参数对收敛影响)评价形式:

**场景 1:低 σ 近距 ample-time**(与 P5 baseline 对比)
- target_y=2500m(ample-time 边界内),classification=Vessel 高 intent
- 验证:OU σ_pos 小 → UT expected cost ≈ 确定性 cost → 收敛性与 P5 一致(不回归)

**场景 2:高 σ 近距**(验证 OU 膨胀效果)
- target_y=2500m,classification=Vessel 低 intent(intent_conf=0.2)
- 验证:σ_pos 大 → UT expected cost 高 → solver 主动远离 → 轨迹 lateral offset 比 P5 baseline 大

**场景 3:多船高速**(验证 UT 计算可行性 + 实时性)
- 3 targets,混合 classification/sog/intent
- 验证:5 sigma points × 3 targets × N stages 计算实时性(solve < SLA);所有 target cost 正确

**证据输出**:
- `runs/p7_sil_scenario_<n>_<timestamp>.json`(主题捕获 + 轨迹 + solve duration + σ_pos 序列)
- 轨迹图(参考 [E1] Fig 6 形式)
- 参数对收敛影响表(参考 [RMD] Ch2 评价形式:σ_0/intent_conf/decel_factor vs sqp_iter)

---

## 8. 排除项(P7 不做)

| 项 | 理由 |
|---|---|
| OU 接入 BC-MPC | Q5 裁决:BC 短 horizon(60s)OU 价值有限(σ_pos 有界 ≈ σ_0·√2,与 dead-reckoning 误差同量级) |
| target_compliance 接入 cost | Q4 裁决:只用 intent_confidence;compliance 留作 Q4 扩展备选 |
| 完整 ROT/SOG-derivative penalty([E1] Eq 16) | P5 transition cost 已防 chattering;完整 penalty 推后 |
| 椭圆 COLREGs penalty([E3] Eq 33) | 与 OU 正交,推后到独立增强 |
| 认证证据(IEC 61508 SIL2 / SOTIF) | Q9 裁决:推后,先保证 MPC 可控可用 |
| ship_type 粒度 OU | M2 无 ship_type,只有 classification;推后 |

---

## 9. 风险与缓解

| 风险 | 等级 | 缓解 |
|---|---|---|
| UT 5 sigma points 增加 NLP 计算量,影响实时性 | 高 | 场景 3 实测 solve duration;超 SLA 则降为 3 sigma points(仅中心 ± 单轴) |
| target stride 5→8 破坏现有 codegen + IPOPT parity | 中 | G7 SX/MX parity 强制;codegen Python 脚本同步改 |
| OU 参数(σ_0/τ_OU)默认值不准 | 中 | SIL 校准 + 海试 TBD;§4.3 规则为可调起点 |
| intent_confidence M2 rule-based 启发式不准 | 中 | M2 已有 0.05–0.95 clamp;P7 只消费不改进 M2 |
| BC 加速度优化与 P6 FINAL_DEGRADE 交互 | 中 | 减速不改 takeover 条件(consecutive failures 不变);G6 验证 |
| P5 ample-time 回归(σ=0 时行为不一致) | 高 | σ=0 时 UT 退化为单点 = 确定性 cost;场景 1 验证不回归 |
| MX 原生 UT 在 acados codegen 中数值不稳定 | 中 | G3 oracle test + G7 parity;α=1e-3 标准 UT 参数 |

---

## 10. 实施顺序(P7 plan 概要)

> 详细 plan 见 `plans/2026-07-18-m5-p7-robustness-ou-intent.md`

1. **T1**: TargetState 加 3 字段 + M5 填充(Q7,G1)
2. **T2**: OU 参数推导 + `ou_uncertainty.hpp`(Q3,G2)
3. **T3**: pack_parameters target stride 5→8 + per-stage σ_pos(Q7/G5)
4. **T4**: UT expected cost MX 实现 + oracle test(Q2/Q6,G3/G7)
5. **T5**: intent 乘性缩放(Q4,G4)
6. **T6**: BC-MPC 加速度优化(Q5,G6)
7. **T7**: SIL 三场景验证 + P5 baseline 回归(Q8,G8)
8. **T8**: codex adversarial review + 8 验收门 + 完整 markdown 报告

---

## 11. 收尾报告(P7 实现后生成)

P7 实现完成后,生成**完整 MPC 收尾报告**(覆盖 P0–P7),参考 4 篇文献的**图表/表格形式**:

| 报告章节 | 评价形式来源 | 内容 |
|---|---|---|
| 决策点验证 | [E1][E2][E3][RMD] 方程 | 11 VR 决策 ↔ 论文方程 ↔ 代码对照 |
| 参数对收敛影响 | [RMD] Ch2 + [E1] Fig 5 | σ_0/intent_conf/decel_factor vs sqp_iter 曲线 |
| 轨迹对比 | [E1] Fig 6 | P5 baseline vs P7 三场景轨迹图 |
| ample-time 边界 | [E2] §V | 收敛边界表(目标距离 vs 收敛性) |
| 测试覆盖 | [RMD] Ch2 | 9 维度闭环 + 测试矩阵 |
| 鲁棒性分析 | [RMD] Ch3 | OU 有界性 + UT 精度 + intent 缩放效果 |

**报告路径**:`docs/superpowers/specs/2026-07-18-m5-mpc-p0-p7-implementation-report.md`(P7 实现后生成)

---

## 12. 文档交叉引用

- **roadmap §P7**: `specs/2026-07-17-m5-mpc-p0-p7-roadmap.md` §4 P7 + §3.2 状态表
- **VR-09 裁决依据**: `specs/2026-07-16-m5-mpc-colav-solution-pack.md` + `design-logs/2026-07-16-m5-mpc-colav-design-log.md`
- **P6 完成**: commit `74f67e365`(BC-MPC 激活 + 11 状态机)
- **P5 ample-time 边界**: `specs/2026-07-18-m5-p5-anti-chatter-ample-time-design.md`(~2000m)
- **参考文献**:
  - [E1] Eriksen & Breivik 2017 CCTA
  - [E2] Eriksen et al. 2020 Frontiers
  - [E3] Eriksen & Breivik 2019 JFR
  - [RMD] Rawlings-Mayne-Diehl, MPC textbook 2nd Ed(Ch3 p193)
