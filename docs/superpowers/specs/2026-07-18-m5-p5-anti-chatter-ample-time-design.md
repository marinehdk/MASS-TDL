# P5 Spec: 反 chattering(warm-start shift-init + 转移代价)+ ample-time 验收门/ODD 边界 + IPOPT compiler 清理

> **产出**: brainstorming,2026-07-18
> **方案包**: `docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md`
> **决策树日志**: `docs/superpowers/design-logs/2026-07-16-m5-mpc-colav-design-log.md`
> **P0–P7 路线图**: `docs/superpowers/specs/2026-07-17-m5-mpc-p0-p7-roadmap.md` §P5
> **收敛性根因**: `docs/superpowers/specs/2026-07-18-m5-p5-acados-convergence-design.md`(P4 期间产出,ample-time 验证证据)
> **关联裁决**: DP-04(VR-04 移除硬编码 Rule14/15)+ TBD-7(VR-TBD7 反 chattering 三层组合)
> **前置**: P0+P1+P2+P3+P4 完成(HEAD 2c031bc49);acados 默认 ON;ample-time(目标当前>2000m)收敛已验证
> **范围**: M5 MPC 重构 P5。M6 几何/Huber 已由 P1b-1b/P2 落地,P5 = 反 chattering + ample-time 门 + IPOPT 清理。

---

## 目的

落地 TBD-7(VR-TBD7 反 chattering,选项 C 的前两层:warm-start shift-init + 转移代价混合范数;符号翻转检测推后)+ 基于 P4 期间收敛性核实的 ample-time 验收门/ODD 边界 + IPOPT compiler 硬编码 Rule14/15 清理(使两路径一致)。这是 VR-04/TBD-7 的剩余兑现:M6 几何 hard/Huber 已落地,反 chattering 是最后未实现的核心。

## 背景(来自 P5 权威范围 + 生产探索 + 收敛性核实)

- **VR-04(Step2 用户确认)**: 移除硬编码 Rule14/15 偏移;M6 几何 hard Rule13/14/15(preferred_direction/min_alteration)。**acados 路径已落地**(formulation L328-329 `pref_dir·l_k` + `pref_dir·(psi-own_psi)-min_alt`);IPOPT compiler 仍有硬编码(L166-237 compile_rule14/15/16/17 = 5°/5°/10°/5° 偏移)。
- **VR-TBD7(Step3 用户裁决选项 C)**: warm-start shift-init(首要)+ 转移代价混合范数(L2 航向 + L1 速度,对齐 Eriksen tran_χ/tran_U)+ 符号翻转检测(Tengesdal K_sgn·exp)。**完全未实现**。P5 做前两层(用户裁决符号翻转推后)。
- **Huber 位置代价(VR-07b)**: P2 已落地。
- **收敛性核实(P4 期间,本 spec 关键输入)**:
  - ample-time 场景(目标当前 >2000m,CPA~1500m)acados **收敛**(实测:5028m→237iter 收敛;2121m→130iter 收敛)。
  - 近距(目标当前 <2000m)**不收敛**(1802m/1581m→status=3)。交 BC-MPC(P6)。
  - 边界是**目标当前距离 ~2000m**,不是 CPA gap。
  - RhoCalibration 测试(目标当前 1500m)是**场景越界**(目标已进入 BC-MPC 紧急区),非求解器缺陷。

## 用户裁决(brainstorming 澄清,2026-07-18)

1. **P5 scope**: 反 chattering(warm-start + 转移代价)+ ample-time 验收门/ODD 边界 + IPOPT compiler 清理。M6 几何/Huber 已落地不重做。[用户]
2. **反 chattering 范围**: 先 warm-start shift-init + 转移代价混合范数;符号翻转检测(Tengesdal)推后。[用户]
3. **IPOPT compiler 清理**: 纳入 P5(使两路径一致,清理硬编码 Rule14/15/16/17 偏移)。[用户]
4. **ample-time 门**: 基于 P4 期间收敛性核实(目标当前 >2000m 必收敛),修正 RhoCalibration 测试为 ample-time 场景。[用户]

## 设计

### 改动范围

| 文件 | 改动 | 类型 |
|---|---|---|
| `src/mid_mpc/mid_mpc_acados_solver.cpp` | warm-start shift-init(上周期解 shift 一步作初值;首周期/失败回退 F1 seed) | 改 |
| `src/mid_mpc/mid_mpc_acados_formulation.{hpp,cpp}` | 转移代价 J_transition(L2 航向 + L1 速度,跨周期连续性) | 改 |
| `test/external/acados_backend/gen_mid_mpc_acados.py` | 转移代价 SX parity + 上周期解参数(per-stage) | 改 |
| `src/shared/constraint_compiler.cpp` + `include/shared/constraint_compiler.hpp` | 清理硬编码 compile_rule14/15/16/17(M6 几何驱动,非度数偏移) | 改 |
| `test/unit/test_mid_mpc_acados_solver.cpp` | ample-time 验收门(目标 5000m 外必收敛)+ 修正 RhoCalibration 为 ample-time 场景 | 改 |
| `test/unit/test_constraint_compiler.cpp` | Rule14/15/16/17 测试更新(M6 几何,非硬编码度数) | 改 |
| docs(ODD 边界 + 收敛边界) | ample-time ODD + 收敛边界(~2000m)记录 | 新增/改 |

**明确排除**(已落地或推后):
- M6 几何 hard Rule13/14/15(acados 已落地)
- Huber 位置代价(P2 已落地)
- 符号翻转检测 Tengesdal(推后,warm-start+转移代价可能够)
- R4 reformulate CPA exp barrier(独立工作,高风险)
- FSM hysteresis / neutral safe state(P6 四状态机)
- BC-MPC 激活(P6)
- R3 adaptive LM + funnel(收敛性核实后不急需,ample-time 收敛已证)

### 组件 1:warm-start shift-init(反 chattering 首要)

**现状**: acados solver 每周期用 F1 forward-propagated seed(L444-505,gentle straight-line hold)+ cold-capsule warm-up(L369)。每周期重新生成 seed,不用上周期解。

**P5 改**: 加 shift-init:
- 若有上周期收敛解(last_solution_ 有效)→ shift 一步作本周期初值(stage k 用上周期 stage k+1 的 x/u)。
- 首周期/上周期失败/约束结构变(sig 变)→ 回退 F1 seed(现逻辑)。
- 配合 cold-capsule(首周期 warm-up 不变)。

**理由(TBD-7 [R24])**: warm-start shift-init 是首要反 chattering 机制(保持同伦类,正交于代价项)。跨周期解连续 → 避免 port/starboard flip。

**实现**: solver 加 `last_converged_solution_` 缓存;solve() 入口判断 shift-init vs F1 seed。

### 组件 2:转移代价混合范数(L2 航向 + L1 速度)

**TBD-7 [R24] Eriksen 形式**:
- `tran_χ = K_Δχ · (χ_m - χ_{m,last})²`(L2 航向控制修改量,K_Δχ≈2.5)
- `tran_U = K_ΔU · |U_m - U_{m,last}|`(L1 速度修改量)

**P5 实现**: 加 J_transition 到 acados cost:
- 跨周期:本周期 stage k 的 ψ/u vs 上周期 stage k+1(shift)的 ψ/u
- `J_transition = w_trans · [ K_Δχ·Σ(ψ[k]-ψ_prev[k])² + K_ΔU·Σ|u[k]-u_prev[k]| ]`
- w_trans 相对权(0.2-5 vs 碰撞 40,差 1-2 数量级,确保风险压过惯性)
- 上周期解作 per-stage 参数传入(ψ_prev/u_prev 序列)

**实现**: formulation 加 J_transition(build_transition_cost_);gen script SX parity;上周期解参数 pack。

### 组件 3:ample-time 验收门 + ODD 边界

**基于 P4 期间收敛性核实(实测)**:
- **Mid-MPC ODD**: 目标当前距离 >2000m 时必收敛(ample-time 职责);<2000m 交 BC-MPC(P6)。
- **验收门**: ample-time 场景(目标 5000m 外,CPA~1500m)acados 必收敛(status=0)。
- **修正 RhoCalibration 测试**: 改为 ample-time 场景(目标当前 >2000m),不用 1500m stress-test(那是 BC-MPC 区)。
- **收敛边界记录**: 目标当前 ~2000m 是已知限制(BC-MPC 兜底,P6 激活后闭环)。

### 组件 4:IPOPT compiler 硬编码 Rule14/15/16/17 清理

**现状**: constraint_compiler.cpp L166-237 compile_rule14/15/16/17 用硬编码度数偏移(5°/5°/10°/5°)。
**P5 改**: 清理为 M6 几何驱动(与 acados 一致:direction/min_alt 行用 preferred_direction/min_alteration,非度数)。或:compile_rule14/15/16/17 改为空/标记(M6 几何在 formulation 层已处理,compiler 不再加度数行)。

**注**: IPOPT 是非默认 fallback(P4 切 acados ON 后)。清理使两路径一致,避免 IPOPT fallback 时硬编码度数偏移与 acados M6 几何行为不一致。

### 数据流

```
上周期收敛解(last_solution_)
  ├ 有 → shift-init(stage k 用上周期 k+1)+ J_transition(对比上周期)  [P5 新增]
  └ 无/失败/sig变 → F1 seed(现逻辑)+ J_transition=0(无对比基准)

acados solve(warm-start shift-init + J_transition + 现有 cost/约束)
  → ample-time 场景(目标>2000m)收敛
  → 反 chattering(跨周期连续,port/starboard 不 flip)
```

### 错误处理

- **shift-init 与 cold-capsule 冲突**: 首周期 cold-capsule warm-up 用 F1 seed(无上周期解);后续周期 shift-init。须正确区分。
- **上周期解失效**: 若上周期 status≠0 或 sig 变(约束结构变)→ 回退 F1 seed,J_transition=0。
- **J_transition 权重过大**: 锁死前一计划无法反应(过保守)→ w_trans 须小于碰撞代价(差 1-2 数量级)。
- **ample-time 门失败**: 若 ample-time 场景(目标 5000m)不收敛 → 这是 Mid-MPC 职责范围,失败须查(不是 BC-MPC 区)。
- **IPOPT 清理后行为变**: 清理硬编码度数后,IPOPT fallback 行为可能与清理前不同 → 须回归测试 + benchmark。

## 测试

### 新增/改测试
1. **ample-time 验收门**(组件 3):目标 5000m 外 + CPA~1500m,acados 必收敛(status=0)。基于 P4 期间实测证据。
2. **RhoCalibration 修正**(组件 3):改为 ample-time 场景(目标当前 >2000m),不用 1500m stress-test。
3. **warm-start shift-init 单测**(组件 1):连续两周期 solve,第二周期用 shift-init;验证第二周期收敛 + 轨迹连续(ψ 序列跨周期差小)。
4. **J_transition 数值单测**(组件 2):构造已知 ψ_prev/u_prev,验证 J_transition = w_trans·[K_Δχ·Σ(ψ-ψ_prev)² + K_ΔU·Σ|u-u_prev|] 数值正确。
5. **IPOPT compiler Rule14/15 清理回归**(组件 4):清理后 compile_rule14/15 不产生硬编码度数行;M6 几何行仍正确。

### 回归测试
- acados 全测试全绿(含 P5 新增)+ ample-time 门过。
- IPOPT 路径(M5_USE_ACADOS=OFF)清理后回归测试。
- 反 chattering 效果:连续多周期 SIL 场景,验证 port/starboard 不 flip(轨迹连续)。

### 验收边界(P5 自闭环门)
- [ ] warm-start shift-init 实现(有上周期解用 shift,无回退 F1 seed)
- [ ] 转移代价 J_transition(L2 航向 + L1 速度)实现 + 数值单测
- [ ] ample-time 验收门:目标 5000m 外 acados 必收敛
- [ ] RhoCalibration 测试修正为 ample-time 场景
- [ ] IPOPT compiler 硬编码 Rule14/15/16/17 清理 + 回归
- [ ] ODD 边界 + 收敛边界(~2000m)记录
- [ ] 反 chattering 效果:连续多周期 port/starboard 不 flip
- [ ] acados + IPOPT 回归全绿

## 风险

- **中**(反 chattering 是新增机制,但 P1a F1 seed 基础已有;转移代价是 cost 项;ample-time 门基于已验证证据;IPOPT 清理是约束 refactor)
- 主要风险:shift-init 与 cold-capsule 配合(首/失败周期回退);J_transition 权重调(过保守/过松);IPOPT 清理后行为变(回归);反 chattering 效果验证(须多周期 SIL)
- **不急 R3/R4**: ample-time 收敛已验证(目标>2000m),收敛性核实后 R3 adaptive LM + R4 reformulate 不急需

## 出 P5 范围(后续)
- **P6**: BC-MPC 激活 + 四状态机(近距<2000m 兜底)+ FSM hysteresis/neutral safe state
- **符号翻转检测 Tengesdal**: 若 warm-start+转移代价不足以防 chattering,再开
- **R4 reformulate CPA exp barrier**: 若 ample-time 外场景须扩(独立工作)
- **P7**: A+ 不确定性 + 意图建模

## 关联
- 方案包组件 3(DP-04/VR-04)+ VR-TBD7(反 chattering 选项 C)
- P0–P7 路线图 §4 P5 + §5.4(P5 修订:Huber 已 P2 做)
- 收敛性根因文档 `2026-07-18-m5-p5-acados-convergence-design.md`(ample-time 证据)
- colav design-log [R24](转移代价 Eriksen 混合范数)
- P2(t_b+Huber)+ P3(ξ)+ P4(horizon+acados ON)
