# P2 Spec: Eriksen 相对跟踪 t_b + Huber 位置代价

> **产出**: brainstorming,2026-07-17
> **方案包**: `docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md`
> **决策树日志**: `docs/superpowers/design-logs/2026-07-16-m5-mpc-colav-design-log.md`
> **P0–P7 路线图**: `docs/superpowers/specs/2026-07-17-m5-mpc-p0-p7-roadmap.md` §P2
> **GNC 跨树修订**: `docs/superpowers/specs/2026-07-17-l3-l4-gnc-contract-solution-pack.md` VR-03/VR-07b
> **关联裁决**: DP-07(VR-07 被 VR-07b 修订)+ VR-02(P1b-1b 已落地的 dynamics/state,本 P2 不动)
> **前置**: P0+P1 完成(HEAD 406b78610);P1b-1b 生产 acados backend(5 维 Path B)已落地,M5_USE_ACADOS 默认 OFF
> **范围**: M5 MPC 重构 P2(Eriksen 终端路线的非废除部分)。C10/C11 废除 + TailBuilder 淘汰 + horizon 延长推 P4(联动)。

---

## 目的

落地 VR-07b(GNC 跨树修订)的两项核心:① 相对跟踪 t_b(每周期投影回 nominal route 找最近点,reference 始终 nominal,废弃"人工参考"误读);② 位置代价纯二次 → Huber(防被障碍推开时指数回拉)。这是 Eriksen 范式的参考编码核心,为 P4(长 horizon 1200s + 废除终端 C10/C11 + TailBuilder 淘汰)奠基。

## 背景(来自 P0–P7 路线图 + GNC VR-07b + 探索)

- **VR-07b 修订(2026-07-17,GNC 跨树)**: 原 P2 TXT 草案含"人工参考轨迹(防过早归航)"。已废弃(因果倒置误读:Eriksen reference 始终是 nominal,用相对跟踪 t_b,从不切换避让参考)。改:相对跟踪 t_b + Huber 位置代价。废除终端 C10/C11 随 P4 长 horizon(避免 90s myopia 风险)。
- **生产现状(探索确认)**: acados formulation `build_route_cost_`(line 354-365)用**绝对 route frame**:`l = (px-ox)*nx + (py-oy)*ny`(ox/oy = route frame origin,nx/ny = normal),cost = `w_guard * (l/l_scale)²`(纯二次)。J_terminal(line 390-413)用同一绝对 lN。terminal 3 行 g_term_side/lo/hi(line 305-310)仍在(C10/C11,P2 不动,推 P4)。
- **P1b-1b 已落地**: Path B 双积分器 dynamics + 5 维 state `x=[px,py,ψ,r,u_surge]` + control `u=[δ,n]` + c_u=9.825e-3(VDM 直读)。**P2 不碰 dynamics/state/control**。
- **roadmap §6.1 开放项**: "相对跟踪 t_b 投影算法"是新增实现项,本 P2 落地。

## 用户裁决(brainstorming 澄清,2026-07-17)

经 5 次逐点确认,P2 scope 收敛:

1. **Path B 维持**: 不升级真一阶 Nomoto(需 TBD-5 真 N_r 阻尼,海试前没有)。Path B 是 TBD-5 海试前过渡状态。[用户]
2. **TailBuilder 淘汰推 P4**: 依赖 horizon 1200s 覆盖返航;90s 下淘汰会让轨迹不够长。[用户]
3. **carryover I-1/2/3/4 全推后**: 都是 acados OFF 下不激活的 forward-looking 项,切默认 ON 前补。[用户]
4. **VDM 删除推后**: Path B 仍读 VDM 系数(k_n_rudder/izz_e/k_prop/k_drag);TrajectoryPropagator::propagate_own 依赖 dynamics.step()。删 VDM 须先迁移系数源 + 处置 propagate_own,独立工作。[用户]
5. **废除终端 C10/C11 推 P4**: VR-07b 说"长 horizon 保收敛";90s horizon 下废除有 myopia 风险(roadmap §5.3 VR-06b)。随 P4 horizon 1200s 一并做。[用户]

## 设计

### 改动范围(P2 收敛后,只改 acados formulation 的 route cost + 新增 t_b 投影)

| 文件 | 改动 | 类型 |
|---|---|---|
| `mid_mpc/mid_mpc_acados_formulation.cpp` `build_route_cost_`(L354-365) | 绝对 route frame → 相对 t_b 的 cross-track;纯二次 → Huber | 改 |
| `mid_mpc/mid_mpc_acados_formulation.cpp` `build_terminal_cost_`(L390-413) | lN 从绝对 route frame 改相对 t_b(保持 softplus 形式,T1 降辅助在此体现为权重,不删) | 改(仅 lN 锚点变) |
| `mid_mpc/mid_mpc_acados_formulation.hpp` | 加 Huber δ_h 参数 + t_b 投影相关声明 | 改 |
| `mid_mpc/mid_mpc_acados_solver.cpp` | pack 时计算 t_b 投影(每周期本船→nominal route 最近点)+ 设 per-stage 参数 | 改 |
| `mid_mpc/mid_mpc_node.cpp`(若 route frame 组装在此) | t_b 投影输入组装(nominal route 线段数据) | 改(查证后定) |
| `test/unit/test_mid_mpc_acados_*.cpp` | t_b 投影单测 + Huber 代价单测 | 新增/改 |
| `test/external/acados_staging/T10_relative_track/`(可选) | t_b + Huber staging 验证(若需前置 spike) | 新增(见风险) |

**明确排除**(已裁决推后):
- Path B 升级真一阶 Nomoto → TBD-5 海试后
- dynamics/state/control 重构 → P1b-1b 已落地,不动
- 废除终端 C10/C11(g_term_side/lo/hi)→ P4(与 horizon 1200s 同步)
- TailBuilder 淘汰 → P4(依赖 horizon 1200s)
- VDM 4-DOF MMG 删除 → 推后(Path B 仍读系数)
- carryover I-1(kMSge static_assert)/I-2(S2 counter)/I-3(short-TCPA guard)/I-4(F2)→ 推后(acados OFF 下不激活)
- horizon 延长 → P4
- COLREGs 几何 → P5

### 组件 1:相对跟踪 t_b(VR-07b 新增)

**概念(roadmap术语表)**: 每周期投影本船预测轨迹回 nominal route 找最近点 → 参考 anchor。reference 始终是 nominal route(从不切换成"避让参考")。

**现状(绝对 route frame)**: `l = (px - ox)*nx + (py - oy)*ny`,ox/oy 是 route frame origin(固定)。问题:避让时本船偏离 nominal route,l 持续增大,cost 持续拉回 → 过早归航倾向(这正是原 VR-07"人工参考"误读想解决但用错方法的问题)。

**t_b 相对跟踪**: 每步 k 把预测位置 (px[k],py[k]) 投影回 nominal route 当前线段,找最近点 t_b[k]。l[k] = 横向偏差 = (px[k],py[k]) 到 t_b[k] 的有符号距离。这样参考随本船沿 route 前进(纵向),只惩罚横向偏差(不惩罚纵向偏离)。

**t_b 投影算法**(新增实现,点到线段最近点):
- 输入:本船预测位置 (px[k],py[k]) + nominal route 当前线段端点 (A_x,A_y)→(B_x,B_y)
- 输出:最近点 t_b[k] + 有符号横向偏差 l[k]
- 算法:标准点到线段投影(t = clamp(((P-A)·(B-A))/|B-A|², 0, 1);t_b = A + t·(B-A);l = (P - t_b)·n_hat,n_hat = route normal)
- 边界 case:t=0/t=1(投影落在线段端点外,用端点);共线/零长线段(退化处理)
- 每步独立投影(k=0..N-1);t_b[k] 是 per-stage 量

**route frame 参数调整**: 现绝对 origin/normal 仍用(作 route 线段定义);新增 per-stage t_b 投影结果作 l[k] 基准。或:origin 改为 per-stage t_b[k](动态),normal 不变。

### 组件 2:Huber 位置代价(VR-07b 联动)

**现状(纯二次)**: `J_route = w_guard * (l/l_scale)²`。问题:远处(大 l)二次增长过快,被障碍推开时代价爆炸式拉回(Eriksen Eq20-21 的反面)。

**Huber(VR-07b)**: 近原点二次/远处线性,C∞ smooth(适合 SQP):
```
Huber(l, δ_h) = (1/2)·l²              if |l| ≤ δ_h
              = δ_h·(|l| - δ_h/2)     if |l| > δ_h
```
- δ_h = Huber 半径(近原点二次区边界)。**初值提议 δ_h = l_scale**(与现 l_scale 归一化一致),benchmark 调。
- J_route = w_guard * Huber(l, δ_h) / l_scale²(保持量纲一致,或重新归一化 —— benchmark 定)
- 连续可导:δ_h 处一阶导连续(二次导有跳但 SQP 只需一阶)。

**实现(CasADi MX)**: 用 `if_else` 表达 Huber(CasADi 支持,生成平滑条件)。或用 soft-Huber 近似(处处可导二次,但偏离 Huber 精确形式 —— 不推荐,精确 Huber + if_else 更可预测)。

### 组件 3:terminal cost lN 锚点(仅改基准,不废约束)

**现状**: `build_terminal_cost_`(L390-413)用绝对 lN = (px-ox)*nx + (py-oy)*ny。
**P2 改**: lN 改相对 t_b[N-1](终端步的 t_b 投影)。softplus 形式不变(wrong_side/l_max 等),只改 lN 锚点从绝对 origin → t_b[N-1]。T1 softplus 降辅助在此体现为权重(P2 不大改权重,P4 废除 C10/C11 时一并调)。

### 数据流

```
每周期 assemble_input_:
  nominal route 线段(从 L2 waypoint/M4 route frame)
    → per-stage t_b 投影(本船预测轨迹每步 → route 最近点)  [新增]
      → l[k] = 横向偏差(相对 t_b,非绝对 origin)  [改]
        → J_route = w_guard * Huber(l, δ_h)  [改,纯二次→Huber]
        → J_terminal lN 锚 t_b[N-1]  [改]
          → acados solve(其余全不变:dynamics/state/CPA/direction/ROT/terminal constraints C10/C11)
```

### 错误处理

- **t_b 投影退化**: 共线/零长 route 线段 → fallback 用绝对 route frame origin(P2 兼容旧逻辑)。单测覆盖退化 case。
- **Huber δ_h 过大/过小**: 过大 → 退化为纯二次(失去 Huber 优势);过小 → 近原点线性区太窄(过早线性拉回)。benchmark 调,初值 l_scale。
- **acados codegen 与 MX graph 一致**: build_route_cost_ 改后,gen_mid_mpc_acados.py(codegen SX)须同步改(P1b-1 纪律:MX graph 与 SX codegen 必须一致)。parity assertion 捕获。
- **warm-start seed**: t_b 改基准后,warm-start seed 仍用 P1a F1(forward-propagated);l[k] 基准变不影响 seed 本身(只影响 cost 计算)。

## 测试

### 新增测试
1. **t_b 投影单测**(纯函数,不依赖 acados):
   - 点在线段内 → 最近点是垂足
   - 点在线段延长线 → 最近点是端点(t=0/t=1)
   - 共线/零长线段 → fallback 正确
   - 有符号横向偏差(n_hat 方向)正确
2. **Huber 代价单测**(纯函数):
   - |l|<δ_h → 0.5·l²(二次区)
   - |l|>δ_h → δ_h·(|l|-δ_h/2)(线性区)
   - |l|=δ_h → 两公式一致(连续)
   - 一阶导 δ_h 处连续(数值验证)
3. **集成(acados)**:
   - t_b + Huber 后 acados 求解收敛(status 0 或容忍 status 4,P1a F5)
   - 轨迹行为:相对跟踪下避让不过早归航(l 不爆炸式拉回)
   - parity:MX graph vs SX codegen 一致

### 回归测试(不破坏现有)
- IPOPT 路径全绿(M5_USE_ACADOS=OFF,本 P2 只改 acados formulation,IPOPT formulation 不动)
- acados 现有测试全绿(test_mid_mpc_acados_*)
- P1b-1b benchmark 基线行为不回归(除非 t_b/Huber 预期改变行为)

### 验收边界(P2 自闭环门)
- [ ] t_b 投影单测全绿(含退化 case)
- [ ] Huber 代价单测全绿(连续/可导)
- [ ] acados build_route_cost_ 改相对 t_b + Huber,codegen 同步,parity 一致
- [ ] build_terminal_cost_ lN 锚 t_b[N-1]
- [ ] acados 求解收敛 + 轨迹行为合理(避让不过早归航)
- [ ] IPOPT 路径无回归(M5_USE_ACADOS=OFF 全绿)
- [ ] acados 现有测试全绿

## 风险

- **中-低**(只改 cost 表达式 + 加投影算法,不动求解器结构/dynamics/state)
- 主要风险:t_b 投影算法正确性(退化 case)+ Huber δ_h 参数(benchmark 调)+ MX/SX codegen 一致(parity 捕获)
- **是否需 staging spike(可选)**: 若 t_b 投影在 acados per-stage 参数下复杂,可先在 `test/external/acados_staging/T10_relative_track/` 验证(类似 P1b-0 模式)。但 P1b-0 已验 per-stage 参数模式,P2 可能不需单独 spike —— 实施时若撞墙再加。

## 出 P2 范围(后续)
- **P4**: 废除终端 C10/C11 + horizon 1200s + TailBuilder 淘汰(三者联动)+ dt 三档 benchmark
- **TBD-5 海试后**: Path B 升级真一阶 Nomoto(补 N_r 阻尼)+ VDM 删除 + 系数迁移
- **切 acados 默认 ON 前**: carryover I-1(kMSge static_assert)/I-2(S2 counter)/I-3(short-TCPA guard)/I-4(F2)
- **P5**: COLREGs 几何 + 反 chattering 三层组合 + 位置代价 Huber 联动(P2 先做 Huber,P5 受益)

## 关联
- 方案包组件 3(DP-07 被 VR-07b 修订)+ GNC solution-pack VR-03/VR-07b
- P0–P7 路线图 §4 P2 + §5.2(P2 修订详情)
- P1b-1 spec(Path B 双积分器 + 5 维 state,P2 不动)
- GNC VR-02(TailBuilder 淘汰,P4)+ VR-06b(horizon 1200s,P4)
