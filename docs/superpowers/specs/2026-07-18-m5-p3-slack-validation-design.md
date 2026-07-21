# P3 Spec: per-target ξ 行为验证 + ρ 校准 + 测试缺口填补

> **产出**: brainstorming,2026-07-18
> **方案包**: `docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md`
> **决策树日志**: `docs/superpowers/design-logs/2026-07-16-m5-mpc-colav-design-log.md`
> **P0–P7 路线图**: `docs/superpowers/specs/2026-07-17-m5-mpc-p0-p7-roadmap.md` §P3
> **关联裁决**: DP-03(VR-03 per-target per-step ξ)+ TBD-6(VR-TBD6 混合 L1/L2)
> **前置**: P0+P1+P2 完成(HEAD c88165fb8);P1b-1b 已落地 per-target per-step ξ + 混合 L1/L2;M5_USE_ACADOS 默认 OFF
> **范围**: M5 MPC 重构 P3。**formulation 不改**(ξ 结构 + L1/L2 已落地),P3 = 验证 + 校准 + 测试缺口填补。

---

## 目的

VR-03/VR-TBD6 的核心实现(per-target per-step ξ∈R^{M·N} + 混合 L1/L2)已被 P1b-1b 落地。但存在三个缺口未闭环:① ρ(zl=1e3)是否满足 Kerrigan exact-penalty 条件(ρ>‖λ*‖∞)未验证;② per-target ξ 独立性 + 精确性单测缺;③ ξ 可观测性(现只 publish 标量 max)。P3 闭环这三个缺口,为认证(CCS i-Ship ξ 行为可追溯)+ 切 acados 默认 ON 前的信心门奠基。

## 背景(来自 P3 权威范围 + 生产探索)

- **VR-03(Step2 用户确认)**: per-target per-step slack ξ∈R^{M·N}(废单标量 σ,消除 masking/free-riding [R2])。
- **VR-TBD6(Step3 用户裁决选项 B)**: 混合 L1/L2 惩罚 ρ·ξ+½w·ξ²(线性 ρ 保精确性 + 二次 w 保 Hessian 正定);acados zl/Zl 原生。
- **生产现状(探索确认)**:
  - kAcadosNsh=16 per-target × N stages = per-target per-step ξ∈R^{16·N}(acados per-stage slacks)。**已落地**。
  - gen_mid_mpc_acados.py L154-162:Zl=1e2(quad W_QUAD)+ zl=1e3(linear RHO_LIN L1),Zu=1e2,zu=0。**已落地**。
  - solver 读 per-stage "sl"(L582-588),publish cpa_slack 标量 max(L931/1000/1205)。**已落地但只标量**。
- **关键 gap(诚实)**: zl=1e3 是**固定值,未验证是否满足 ρ>‖λ*‖∞**(Kerrigan exact-penalty 条件 [R23])。若不满足 → feasible 时 ξ>0(安全边距缩水,非 exact)。
- **SIL 多船场景(探索确认存在)**: scenarios/IMAZU标准测试/imazu-{06,10,15,17,18,21}-ms.yaml("ms"=multi-ship)。P3 SIL 实测有现成场景。

## 用户裁决(brainstorming 澄清,2026-07-18)

1. **P3 scope 收敛**: formulation 不改(ξ+L1/L2 已 P1b-1b 落地),P3 = 验证+校准+测试缺口填补。[用户]
2. **ρ 校准策略**: 先 SIL 实测 ξ 行为(feasible 时是否 ξ≈0)再决定固定/调大/同伦。实证驱动。[用户]
3. **ξ 可观测性增强**: 纳入 P3(加 per-target ξ trace,M8/ASDR 可见,支撑认证)。[用户]

## 设计

### 改动范围(P3 收敛后,formulation 不改,只加测试 + 可观测性 + 可能调 zl)

| 文件 | 改动 | 类型 |
|---|---|---|
| `test/unit/test_mid_mpc_acados_solver.cpp` | 加 multi-target MidMpcInput builder + ξ 独立性/精确性单测 | 改 |
| `test/unit/test_p3_slack_behavior.cpp`(或扩 acados solver 测试) | ξ 独立性(masking 消除)+ 精确性(feasible ξ=0)专项测试 | 新增/改 |
| `mid_mpc/mid_mpc_acados_solver.cpp` | ξ 可观测性:publish per-target ξ(max + per-target breakdown) | 改 |
| `mid_mpc/mid_mpc_node.cpp`(若 publish 在此) | per-target ξ 加到 SAT3/ASDR diagnostic | 改(查证后定) |
| `test/external/acados_backend/gen_mid_mpc_acados.py` | **可能**调 RHO_LIN(若 SIL 实测 zl=1e3 不满足 exact-penalty) | 改(条件性,SIL 后定) |
| SIL 实测脚本(用 colregs-probe skill 或 run_colregs_*.py) | 跑 imazu-*-ms 多船场景,提取 ξ 行为 | 新增(用现有工具) |

**明确排除**(已落地或推后):
- ξ 结构 + L1/L2 形式(P1b-1b 已落地,formulation 不改)
- Eriksen 同伦 K_ξ 序列(若 SIL 实测 zl=1e3 够则不引入;若不够再开专项)
- dynamics/state/route cost(P1b-1b/P2 已落地)
- horizon 延长(P4)
- COLREGs 几何(P5)
- TailBuilder/horizon/C10C11 废除(P4)

### 组件 1:ρ exact-penalty SIL 实测(决定 zl 校准)

**目的**: 验证 zl=1e3 是否满足 Kerrigan exact-penalty(ρ>‖λ*‖∞ [R23])。满足 → feasible 时 ξ≈0;不满足 → ξ>0(安全边距缩水)。

**方法(实证驱动)**:
1. 跑 SIL 多船场景(imazu-06-ms 或 imazu-10-ms),M5_USE_ACADOS=ON。
2. 提取每周期 per-target ξ(用组件 3 加的可观测性,或直接读 solver "sl")。
3. 分析:CPA feasible(目标远/无冲突)时 ξ 是否 ≈0?CPA active(避让中)时 ξ 是否合理(仅不可达时 >0)?
4. 判据:
   - **PASS**: feasible 时 ξ < tol(如 1e-3)→ zl=1e3 满足 exact-penalty,固定值够。
   - **FAIL/PARTIAL**: feasible 时 ξ > tol → zl 不够大;调大(如 1e4/1e5)重测,或评估 Eriksen 同伦。
5. 据 SIL 结果决定:固定 zl / 调大 / 引入同伦(回用户裁决)。

**‖λ*‖∞ 下界估算(辅助)**: 若需理论验证,估 CPA 约束的 Lagrange 乘子 ‖λ*‖∞ 下界(CPA 代价权重 w_colreg=30 + cpa_safe 尺度 → λ* 量级)。zl=1e3 应 > 此下界。

### 组件 2:per-target ξ 独立性 + 精确性单测(缺口填补)

**参考现有模式**: `test_mid_mpc_acados_solver.cpp` 的 `AcadosSolverTest` fixture + `straight_line()` MidMpcInput builder(L105)。P3 加 multi-target builder。

**测试用例**:

1. **ξ 独立性(masking 消除,SC-02)**:
   - 构造 2-target MidMpcInput:目标 A CPA 不可达(极近,迫使 ξ_A>0),目标 B CPA 可行(远)。
   - solve 后断言:ξ_A > 0(松弛),ξ_B ≈ 0(不受 A 拖累)。
   - 对照:若是单标量 σ(旧),σ>0 会使 B 的 CPA 也放宽(masking)。per-target ξ 消除此(B 独立)。
   - 断言:B 的 CPA 约束仍满足(g_B ≥ 0,未被 A 的松弛拖累)。

2. **ξ 精确性(feasible 时 ξ=0)**:
   - 构造 1-target MidMpcInput:目标 CPA 可行(远,无冲突)。
   - solve 后断言:ξ ≈ 0(< tol,exact-penalty 满足)。
   - 对照 infeasible:目标 CPA 不可达(近)→ ξ > 0 松弛。

3. **混合 L1/L2 penalty 数值**:
   - 断言 J_slack = ρ·ξ + ½w·ξ² 数值正确(用 Zl=1e2/zl=1e3 作 oracle)。

**测试位置**: 扩 `test_mid_mpc_acados_solver.cpp` 加 multi-target builder + 3 case,或新建 `test_p3_slack_behavior.cpp`。

### 组件 3:ξ 可观测性增强(支撑 SIL 实测 + 认证)

**现状**: solver publish `cpa_slack` = max over (target, stage) of per-target ξ(标量,L931/1000/1205)。问题:看不到 per-target breakdown,无法认证 ξ 行为可追溯。

**P3 改**: 加 per-target ξ breakdown 到 diagnostic:
- solver 输出:除 cpa_slack(max),加 per-target ξ_max 数组(每目标 max over stage)。
- node publish:加到 SAT3/ASDR diagnostic JSON(per-target ξ,认证可见)。
- 用于:SIL 实测分析(组件 1)+ CCS i-Ship 认证(ξ 行为可追溯)。

**实现细节(查证后定)**: MidMpcSolution 加 `std::vector<double> cpa_slack_per_target`(或 array<max_targets>);node publish 加 JSON 字段。

### 数据流

```
SIL 多船场景(imazu-*-ms,M5_USE_ACADOS=ON)
  → acados solve(per-target ξ per-stage)
    → solver 提取 per-target ξ(组件 3 加 breakdown)
      → 分析:feasible ξ≈0? active ξ 合理?(组件 1)
        → 决定 zl 校准(固定/调大/同伦)

单测(组件 2):
  构造 multi-target MidMpcInput → solve → 断言 ξ 独立性 + 精确性 + penalty 数值
```

### 错误处理

- **SIL ξ 提取失败**: 若 per-target ξ 读不出(solver "sl" 维度问题)→ 查 kAcadosNsh/Ns 与 Nt 匹配;空 target slot 处理(L142 "empty slots RELAXED")。
- **ξ feasible 时不归零**: 若 SIL 实测 feasible 时 ξ > tol → zl 不够大,组件 1 Step 5 调大重测。
- **multi-target builder 构造难**: 若 MidMpcInput 多 target 字段复杂 → 参考 node.cpp assemble_input_ 的 target pack 模式。
- **可观测性加 JSON 字段破坏现有 consumer**: 加新字段(非改现有),M8/ASDR 向后兼容。

## 测试

### 新增测试
1. **ξ 独立性单测**(组件 2 case 1):2-target,A 不可达/B 可行 → ξ_A>0, ξ_B≈0, B 的 CPA 不被拖累。
2. **ξ 精确性单测**(组件 2 case 2):1-target feasible → ξ≈0;infeasible → ξ>0。
3. **混合 L1/L2 penalty 数值**(组件 2 case 3):J_slack = ρ·ξ+½w·ξ² oracle 验证。
4. **SIL 实测**(组件 1):imazu-*-ms 跑 + ξ 行为分析报告(feasible ξ≈0 / active ξ 合理)。

### 回归测试
- acados 现有测试全绿(test_mid_mpc_acados_solver/formulation/per_stage_tb/parity)+ IPOPT 路径无回归(M5_USE_ACADOS=OFF)。
- 可观测性改动:现有 cpa_slack 标量 publish 不破坏(只加 per-target 字段)。

### 验收边界(P3 自闭环门)
- [ ] ρ SIL 实测完成:feasible ξ 行为判定(PASS zl 够 / FAIL 调大)+ 报告
- [ ] ξ 独立性单测:多船一目标松弛不拖累其他(masking 消除,SC-02)
- [ ] ξ 精确性单测:feasible ξ≈0 / infeasible ξ>0
- [ ] 混合 L1/L2 penalty 数值单测:ρ·ξ+½w·ξ² 正确
- [ ] ξ 可观测性:per-target ξ breakdown publish + 认证可见
- [ ] IPOPT 路径无回归 + acados 现有测试全绿
- [ ] ρ 校准决策记录(固定/调大/同伦,据 SIL 结果)

## 风险

- **低-中**(formulation 不改,只加测试 + 可观测性 + 可能调 zl 一个数值)
- 主要风险:SIL ξ 提取链路(solver "sl" → publish → 分析)是否顺;multi-target MidMpcInput builder 构造复杂度;ρ 校准若需同伦则范围扩大(但 SIL 实测驱动,先看结果)
- **若 SIL 实测 zl=1e3 已满足 exact-penalty**: P3 范围最小(只测试 + 可观测性)。
- **若 SIL 实测 zl 不够**: P3 含 zl 调大(简单)或引入同伦(范围扩大,回用户裁决)。

## 出 P3 范围(后续)
- **Eriksen 同伦 K_ξ**(若 SIL 实测 zl 不够且调大仍不满足):专项,多轮 solve + 实时性影响。
- **P4**: horizon 1200s + 废终端 C10/C11 + TailBuilder 淘汰
- **P5**: COLREGs 几何 + 反 chattering + Huber 联动
- **认证阶段**: CCS i-Ship ξ 行为可追溯(P3 可观测性奠基)

## 关联
- 方案包组件 3(DP-03/VR-03)+ VR-TBD6(混合 L1/L2)
- P0–P7 路线图 §4 P3
- P1b-1 spec(P1b-1a T7 staging 验证 per-target ξ 可行 + P1b-1b 生产落地)
- colav design-log [R2](masking)+ [R23](Kerrigan exact-penalty)
- SIL 工具:colregs-probe skill / run_colregs_*.py / scenarios/IMAZU标准测试/imazu-*-ms.yaml
