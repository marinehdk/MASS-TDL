# P4 Spec: horizon 1200s + 废终端 C10/C11 + TailBuilder 拼接淘汰 + timer 60s + 承诺前缀 180s

> **产出**: brainstorming,2026-07-18
> **方案包**: `docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md`
> **决策树日志**: `docs/superpowers/design-logs/2026-07-16-m5-mpc-colav-design-log.md`
> **P0–P7 路线图**: `docs/superpowers/specs/2026-07-17-m5-mpc-p0-p7-roadmap.md` §P4 + §5.2/5.3
> **GNC 跨树修订**: `docs/superpowers/specs/2026-07-17-l3-l4-gnc-contract-solution-pack.md` VR-02/VR-06b/VR-07b
> **关联裁决**: DP-06(VR-06 被 VR-06b 修订)+ DP-07(VR-07b 废终端)+ VR-02(淘汰 TailBuilder 拼接)
> **前置**: P0+P1+P2+P3 完成(HEAD 1c0a4ee3e);acados backend(5 维 Path B)+ t_b+Huber + per-target ξ 已落地;M5_USE_ACADOS 默认 OFF
> **范围**: M5 MPC 重构 P4(一次性全量,高风险)。头号回炉触发:acados 1200s 实时性。

---

## 目的

落地 VR-06b(horizon 1200s)+ VR-07b(废终端 C10/C11)+ VR-02(淘汰 TailBuilder 拼接)+ GNC 承诺前缀 180s。这是 Eriksen 长时域范式的完整兑现:1200s horizon 覆盖用户 SIL 观测的 900s 完整避碰生命周期(避让→保持→返航),NLP 内部端到端返航,消除 myopia(premature return/chattering)+ 消除尾段几何拼接(冗余补丁)。

## 背景(来自 P4 权威范围 + 生产探索)

- **VR-06b 修订(2026-07-17,GNC 跨树)**: horizon 360s→**1200s**。理由:用户 SIL 观测完整避碰生命周期最长 900s;360s<900s 致 myopia;Johansen SB-MPC 用 600-1200s 实证;45m FCB 18kn 巡航 20 分钟覆盖一般避碰+返航。
- **VR-07b 修订**: 废终端 C10/C11(长 horizon 1200s 保收敛,不需终端集);废弃人工参考→相对跟踪 t_b(P2 已落地)。
- **VR-02**: 淘汰 TailBuilder 尾段拼接(几何 hold+rejoin)。老 TailBuilder 是 VR-07"人工参考轨迹防过早归航"误读的补丁,根因(horizon 不足)消除后补丁亦消除。
- **生产现状(探索确认)**:
  - `kAcadosNDefault=18`(formulation.hpp L103)+ m5_params.yaml `horizon_s=90/n_steps=18/dt_s=5` = 90s。
  - solve_timer **1Hz**(node L519 `std::chrono::seconds(1)`),on_solve_cycle_ 每周期真 solve **无 replan-gating**。VR-06b 要求 replan=60s(1Hz 对雷达/AIS 微噪声过反应→舵 chattering)。
  - 终端 C10/C11:build_con_h_ L330-335 `g_term_side(pref_dir·l - l_min)`/`g_term_lo(l+l_max)`/`g_term_hi(l_max-l)`(linear lateral,3 行;nh=23)。
  - TailBuilder 拼接:`append_tail_waypoints_`(node L1570-1693)+ `TailBuilder::build()`(tail_builder.cpp L341)+ `feasible_rejoin_length_m`/`validate_tail_segment`。**注**:tb:: namespace 还提供**复用类型**(ColregSide/ColregRole/EncounterState/TargetSnapshot/RouteFrame projection),这些被 M6 集成用,**保留**;P4 只淘汰**尾段拼接逻辑**。
  - 承诺前缀:`committed_prefix_reproject` + `compute_frozen_prefix_count`(node L235)已存在;P4 延长到 180s + material-change version 语义。

## 用户裁决(brainstorming 澄清,2026-07-18)

1. **P4 一次性全量**(horizon + 废终端 + TailBuilder + timer + 承诺前缀),不分阶段。[用户]
2. **timer 改 60s**:solve_timer 1Hz→60s(VR-06b 要求)。[用户]
3. **horizon 直接 1200s + dt 三档 benchmark**:不中间步(600s);benchmark 本身渐进(10/15/20s 取达标最大分辨率)。[用户]
4. **P3 ρ-gap 不在 P4 修**:CPA squared-distance → linear-distance 是独立工作(可开专项或留 P5);C10/C11 是 linear lateral 非 squared,与 ρ-gap 不同源。[用户]
5. **切 acados 默认 ON**(2026-07-18 补充裁决):P4 把 M5_USE_ACADOS 默认 OFF→ON(acados 成生产主路径,horizon 1200s)。IPOPT 保 90s 作非默认 fallback。**切 ON 前必须先补 carryover I-1~4**(评审 forward-looking,acados OFF 下不激活,切 ON 后成生产路径必须闭环):
   - **I-1 kMSge static_assert**:acados kMSge 硬编 152250 不读 manifest.config().mass_kg → P4 加 static_assert 锁定(kMSge == manifest mass·(1+surge_factor))。
   - **I-2 S2 escalation counter**:acados dispatch 绕过 consecutive_failures_ → 不触发 MRM-02 → P4 补 counter 在 acados 路径也递增。
   - **I-3 short-TCPA(<2000m)dispatch guard 缺失**:P4 补 guard(short-TCPA 回退 IPOPT 或 BC-MPC,因 acados staging 未验短距)。
   - **I-4 F2 colreg_prefix_softened 缺**:P4 补(documented gap 闭环)。

## 设计

### 改动范围(P4 一次性全量)

| 文件 | 改动 | 类型 |
|---|---|---|
| `include/mid_mpc/mid_mpc_acados_formulation.hpp` | `kAcadosNDefault` 18→(120\|80\|60 据 benchmark);nh 23→20(废终端 3 行);static_assert 更新 | 改 |
| `src/mid_mpc/mid_mpc_acados_formulation.cpp` | build_con_h_ 删 g_term_side/lo/hi(L330-335);RowRegistry terminal 行 3→0 | 改 |
| `src/mid_mpc/mid_mpc_acados_solver.cpp` | kAcadosNh 23→20;RowRegistry 偏移(kRowTermSide/Lo/Hi 删);terminal unpack 调整 | 改 |
| `config/m5_params.yaml` | horizon_s 90→1200;dt_s 5→(10\|15\|20);n_steps 18→(120\|80\|60) | 改 |
| `src/mid_mpc/mid_mpc_node.cpp` | solve_timer seconds(1)→seconds(60)(L519);删 append_tail_waypoints_ 调用 + 拼接逻辑(L1570-1693);committed prefix 180s + material-change version | 改 |
| `test/external/acados_backend/gen_mid_mpc_acados.py` | N/dt 同步;terminal 3 行删;code-gen 容器维度更新 | 改 |
| `src/tail_builder/tail_builder.cpp` + `include/tail_builder/*.hpp` | **保留**(复用类型 ColregSide/Role/EncounterState/TargetSnapshot/RouteFrame);**删** TailBuilder::build + feasible_rejoin + validate_tail(拼接逻辑) | 改(部分删) |
| `test/unit/test_tail_builder.cpp` + `test_midmpc_tail_gate.cpp` | 拼接相关测试删/改;复用类型测试保留 | 改 |
| `CMakeLists.txt` | **M5_USE_ACADOS 默认 OFF→ON**(切生产主路径) | 改 |
| `src/mid_mpc/mid_mpc_acados_formulation.hpp`/`solver.cpp`/`node.cpp` | **carryover I-1~4**(切 ON 前置):I-1 kMSge static_assert / I-2 S2 escalation counter / I-3 short-TCPA guard / I-4 F2 colreg_prefix_softened | 改 |
| dt benchmark 脚本 | 测 acados RTI 在 Np=120/80/60 实时性 | 新增(用现有工具) |

**明确排除**(已裁决):
- P3 ρ-gap(CPA squared-distance):独立工作
- Eriksen 同伦 K_ξ:P3 KEEP zl=1e3
- COLREGs 几何(P5)
- dynamics/state/route cost(P1b-1b/P2 已落地)
- Path B 升级 Nomoto(TBD-5 海试后)
- VDM 删除(推后)

### 组件 1:horizon 1200s + dt 三档 benchmark

**参数变更**:
- `kAcadosNDefault`: 18 → 据 dt benchmark 定(1200s/dt=Np)
- m5_params.yaml: `horizon_s: 1200`, `dt_s: (10|15|20)`, `n_steps: (120|80|60)`

**dt 三档 benchmark**(渐进探底,取达标最大分辨率):
- dt=10s → Np=120
- dt=15s → Np=80
- dt=20s → Np=60
- 判据:acados RTI 单次 solve ≤ 求解预算(如 < 1s 或 < replan 60s 的合理比例);COLREGs ample-time 分钟级,dt=15-20s 足够。
- 取达标的**最大分辨率**(最小 dt)。

**头号回炉触发**: 若三档都超实时预算 → acados 在 1200s 不可行 → 回炉 DP-05 重评 SB-MPC+GPU。

### 组件 2:solve_timer 1Hz→60s

node L519 `std::chrono::seconds(1)` → `std::chrono::seconds(60)`。
VR-06b 理由:1Hz 对雷达/AIS 微噪声过反应→舵 chattering;60s 开阔水域合理(遭遇慢发展+L4 持续跟踪多分钟轨迹+事件触发带外重规划)。

### 组件 3:废除终端 C10/C11

build_con_h_ L330-335 删 3 行:
- `g_term_side = pref_dir * l_k - l_min`(C10 同侧)
- `g_term_lo = l_k + l_max`(C11 横向下界)
- `g_term_hi = l_max - l_k`(C11 横向上界)

RowRegistry:terminal 行 3→0;nh 23→20;g_dim 重算。solver kRowTermSide/Lo/Hi 删 + 偏移调整。
VR-07b:长 horizon 1200s 保收敛,不需终端集。

### 组件 4:TailBuilder 拼接淘汰

**淘汰(删)**:
- `append_tail_waypoints_`(node L1570-1693)调用点 + 拼接逻辑
- `TailBuilder::build()`(tail_builder.cpp L341)
- `feasible_rejoin_length_m` + `validate_tail_segment`(拼接专用)

**保留(复用类型,M6 集成用)**:
- `ColregSide`/`ColregRole`/`EncounterState`(enum)
- `TargetSnapshot`/`RouteFrame`(projection/sample)
- `route_frame_has_sharp_corner`(cross-leg guard 用)

**输出流程修订(VR-02)**:
```
原: NLP 解 → append_tail_waypoints_(几何 hold+rejoin 拼接) → wp_gen → publish
新: NLP 解[ψ,r,u,px,py](1200s horizon,相对跟踪 t_b+Huber,无终端集)
    → 直接 trajectory(单一真相,含避让+保持+返航完整生命周期)
    → wp_gen → publish
```
1200s horizon 覆盖 900s 完整生命周期 → NLP 内部端到端返航 → 无需尾段拼接。

### 组件 5:承诺前缀 180s + material-change version

- 现有 `compute_frozen_prefix_count` + `committed_prefix_reproject` 延长承诺前缀到 180s。
- material-change(role/direction/CPA risk/GNC reject/偏航超阈/M7 VETO/紧急升级)才更新 plan version;否则版本不变(L4 warm-start)。
- 承诺前缀 180s = L4 必跟踪 + M5 保证不推翻;预测尾段 1020s 参考用。

### 组件 6:RFC-001 推翻记录

90s 锁定正式推翻(2026-07-16 Step2 已授权),记入 m5_params.yaml 注释 + handoff。

### 数据流

```
m5_params(horizon=1200/dt=benchmark/n_steps)
  → acados(Np=60-120,废终端 C10/C11 nh=20)
    → solve_timer 60s 触发 on_solve_cycle_(无 TailBuilder 拼接)
      → NLP 解(1200s,端到端返航)→ wp_gen → publish
        → committed prefix 180s + material-change version → L4
```

### 错误处理

- **acados 1200s 实时性不达标**: dt benchmark 三档都超预算 → 头号回炉触发,回炉 DP-05。
- **废终端后求解不收敛**: 若 1200s horizon 仍不足以保收敛(极端场景)→ 评估保留 C10/C11 或调 horizon;但 VR-07b 裁决长 horizon 保收敛,预期不发生。
- **TailBuilder 拼接删除导致输出残缺**: 若 NLP 解的 1200s 轨迹在极端场景不覆盖返航 → wp_gen 输出短轨迹;评估 fallback(但 VR-02 裁决 NLP 端到端,horizon 1200s 覆盖 900s 生命周期)。
- **timer 60s 响应不足**: 若紧急场景需更快响应 → BC-MPC(5s,P6)接管;Mid-MPC 60s 是 ample-time 层,BC 是紧急层。
- **承诺前缀 180s SIL 校准**: roadmap §6.1 开放项,延后一周用户跑 900s 场景验证双 L4 跟踪稳定。

## 测试

### 新增/改测试
1. **dt benchmark 脚本**:三档(10/15/20s)测 acados RTI 实时性,取达标最大分辨率。
2. **废终端测试**:nh=20 验证 + 求解收敛(1200s horizon 保收敛)+ 终端不漂出 ODD。
3. **TailBuilder 拼接删除回归**:输出轨迹完整(1200s 覆盖返航);复用类型(ColregSide/EncounterState 等)单测保留。
4. **timer 60s 验证**:on_solve_cycle_ 60s 触发(非 1Hz)。
5. **承诺前缀 180s + material-change**:frozen_prefix_count 延长 + version 语义。

### 回归测试
- IPOPT 路径(M5_USE_ACADOS=OFF):horizon/timer 改动对 IPOPT 也生效(IPOPT 也要 1200s?或 IPOPT 保 90s?)—— **关键决策点**:IPOPT 在 1200s 下 O(n³) 不可行,P4 须明确 IPOPT 路径如何处理(保 90s fallback?还是 IPOPT 路径不延 horizon 只 acados 延?)。
- acados 路径:全测试 + 新测试全绿。

### 验收边界(P4 自闭环门)
- [ ] horizon 1200s 落地 + dt benchmark 三档测 + 取达标最大分辨率
- [ ] solve_timer 60s(非 1Hz)
- [ ] 废终端 C10/C11(nh 23→20)+ 求解收敛(1200s 保)
- [ ] TailBuilder 拼接删除 + 输出轨迹完整(1200s 覆盖返航)+ 复用类型保留
- [ ] 承诺前缀 180s + material-change version
- [ ] RFC-001 推翻记录
- [ ] acados RTI 1200s 实时性达标(头号回炉门)
- [ ] **M5_USE_ACADOS 默认 ON** + **carryover I-1~4 全闭环**(切 ON 前置:kMSge static_assert / S2 counter / short-TCPA guard / F2)
- [ ] 回归:IPOPT 路径保 90s fallback 可用 + acados 全测试全绿

## 风险

- **高**(horizon 13× 跳跃 + 废终端 + TailBuilder 删除 + timer 改 + 承诺前缀,全一次性)
- **头号回炉**: acados 1200s/Np60-120 实时性(dt benchmark 探底)
- **关键决策点(须 spec 明确)**: IPOPT 路径(M5_USE_ACADOS=OFF)在 horizon 1200s 下 O(n³) 不可行 —— P4 须明确 IPOPT 是保 90s fallback 还是同步延 horizon(后者不可行)。**推荐**:IPOPT 保 90s 作应急 fallback,acados 是 1200s 主路径;M5_USE_ACADOS 切默认 ON 是 P4 后的事(切 ON 前须 carryover I-1~4 补)。
- **mitigant**: acados RTI O(n) 结构利用;dt benchmark 渐进探底;复用类型保留(不破坏 M6 集成);承诺前缀 SIL 校准延后。

## 出 P4 范围(后续)
- **P5**: COLREGs 几何 + 反 chattering 三层组合 + Huber 联动(P2 已做 Huber)
- **P6**: BC-MPC 激活 + 四状态机 + 回退链
- **切 acados 默认 ON 前**: carryover I-1(kMSge static_assert)/I-2(S2 counter)/I-3(short-TCPA guard)/I-4(F2)
- **TBD-5 海试后**: Path B 升级 Nomoto + VDM 删除
- **CPA linear-distance**(修 P3 ρ-gap):独立工作或 P5
- **承诺前缀 180s SIL 校准**:延后一周

## 关联
- 方案包组件 3(DP-06/VR-06b)+ GNC solution-pack VR-02/VR-06b/VR-07b
- P0–P7 路线图 §4 P4 + §5.2/5.3
- P1b-1 spec(acados 实时性基础)+ P2(t_b+Huber)+ P3(ξ+L1/L2)
- 头号回炉触发:roadmap §6.2
