# P4: horizon 1200s + 废终端 + TailBuilder 拼接淘汰 + timer 60s + 承诺前缀 + 切 acados ON — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 一次性落地 VR-06b(horizon 1200s)+ VR-07b(废终端 C10/C11)+ VR-02(淘汰 TailBuilder 拼接)+ timer 60s + 承诺前缀 180s + 切 acados 默认 ON(含 carryover I-1~4 前置)。

**Architecture:** acados 成生产主路径(horizon 1200s,dt benchmark 定);IPOPT 保 90s fallback。TailBuilder 拼接删(保留复用类型)。终端 C10/C11 废(horizon 保收敛)。切 ON 前必补 carryover I-1~4。

**Tech Stack:** C++17, acados 0.4.4, ROS2 ament_cmake, colcon, gtest, colregs-probe skill(benchmark + SIL)

**Spec:** `docs/superpowers/specs/2026-07-18-m5-p4-horizon-terminal-tailbuilder-design.md`

## Global Constraints

- 工作目录: `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`(分支 codex/m5-design-grounding,HEAD 1c0a4ee3e)
- **头号回炉触发**: acados 1200s/Np60-120 实时性 benchmark 不达标 → 回炉 DP-05 重评 SB-MPC+GPU(roadmap §6.2)。dt 三档 benchmark 是渐进探底。
- **TailBuilder 部分删**: 只删**拼接逻辑**(TailBuilder::build + append_tail_waypoints_ + feasible_rejoin + validate_tail);**保留复用类型**(ColregSide/ColregRole/EncounterState/TargetSnapshot/RouteFrame projection/sample/route_frame_has_sharp_corner)—— M6 集成用,不破坏。
- **切 ON 前置**: carryover I-1~4(kMSge static_assert / S2 counter / short-TCPA guard / F2)必须先补,M5_USE_ACADOS 默认 ON 才安全。
- **测试标志**: `BUILD_TESTING` + 包名 `m5_tactical_planner`。
- **容器内执行**: `source scripts/a4000-env.sh`;`COMPOSE_PROJECT_NAME=codex-m5-p4`,不碰 mass-l3-sil。
- 诚实纪律:dt benchmark 真跑三档(非声称);废终端后真验收敛(1200s horizon);TailBuilder 删后输出轨迹真完整;carryover I-1~4 真补(非 stub)。
- 每 task 一 commit;TDD。

---

## File Structure

| 文件 | 责任 | P4 改动 |
|---|---|---|
| `include/mid_mpc/mid_mpc_acados_formulation.hpp` | kAcadosNDefault 18→benchmark;nh 23→20;carryover I-1 kMSge static_assert | 改 |
| `src/mid_mpc/mid_mpc_acados_formulation.cpp` | build_con_h_ 删终端 3 行(L330-335) | 改 |
| `src/mid_mpc/mid_mpc_acados_solver.cpp` | kAcadosNh 23→20;RowRegistry 偏移;carryover I-2 S2 counter / I-3 short-TCPA guard | 改 |
| `src/mid_mpc/mid_mpc_node.cpp` | solve_timer 60s(L519);删 append_tail_waypoints_(L1570-1693);committed prefix 180s;carryover I-4 F2 | 改 |
| `config/m5_params.yaml` | horizon_s/dt_s/n_steps | 改 |
| `CMakeLists.txt` | M5_USE_ACADOS 默认 ON | 改 |
| `test/external/acados_backend/gen_mid_mpc_acados.py` | N/dt 同步 + terminal 3 行删 + 维度更新 | 改 |
| `src/tail_builder/tail_builder.cpp` + `include/tail_builder/*.hpp` | 删 build/feasible_rejoin/validate_tail;保留复用类型 | 改(部分删) |
| `test/unit/test_tail_builder.cpp` + `test_midmpc_tail_gate.cpp` | 拼接测试删;复用类型测试保留 | 改 |
| dt benchmark 脚本 | 三档测 acados RTI 实时性 | 新增 |

---

## Task 1: dt benchmark(头号回炉门,先做)

**Files:**
- 新增 benchmark 脚本(用 colregs-probe skill 或独立)

**关键**: 头号回炉触发。先测 acados 在 1200s 下三档 dt 实时性,达标才继续;不达标回炉 DP-05。

- [ ] **Step 1: 构造 benchmark 场景(Rule14 HO 或 imazu-01-ho)**

用 colregs-probe skill,跑 acados(M5_USE_ACADOS=ON)在 horizon=1200s 下三档 dt:
- dt=10s → Np=120
- dt=15s → Np=80
- dt=20s → Np=60

- [ ] **Step 2: 测每档 acados RTI 单次 solve 时间**

记录三档 solve_ms。判据:solve_ms ≤ 求解预算(如 < 1000ms 或 < replan 60s 的合理比例 10s)。

- [ ] **Step 3: 取达标最大分辨率(最小 dt)**

若 dt=10s(Np120)达标 → 用 dt=10s;若不达标试 15s;再不达标 20s。取达标的最小 dt(最大分辨率)。

- [ ] **Step 4: 若三档都不达标 → 回炉 DP-05**

记录阻塞证据(solve_ms 超预算),标"回炉 DP-05 重评 SB-MPC+GPU",P4 停。

- [ ] **Step 5: 记录 benchmark 决策 + Commit**

记录选定 dt/Np + 三档数据。若改了 m5_params 测 dt,commit。

```bash
git commit -am "perf(m5): P4 dt benchmark — acados 1200s Np=X dt=Ys (RTI Zms < budget)"
```

---

## Task 2: horizon 1200s + dt 参数落地

**Files:**
- `include/mid_mpc/mid_mpc_acados_formulation.hpp`(kAcadosNDefault)
- `config/m5_params.yaml`(horizon_s/dt_s/n_steps)
- `test/external/acados_backend/gen_mid_mpc_acados.py`(N/dt 同步)

**Interfaces:**
- Consumes: Task 1 benchmark 选定的 dt/Np

- [ ] **Step 1: 改 kAcadosNDefault 18→benchmark 选定 Np**

formulation.hpp L103。

- [ ] **Step 2: 改 m5_params.yaml**

```yaml
mid_mpc:
    horizon_s: 1200.0   # P4 VR-06b (was 90); RFC-001 90s 推翻 (2026-07-16 Step2)
    n_steps: <benchmark Np>   # was 18
    dt_s: <benchmark dt>      # was 5.0
```

- [ ] **Step 3: gen_mid_mpc_acados.py 同步 N/dt**

codegen 脚本的 N/dt 常量同步;维度 static_assert 更新(NpGlobal/NpPerStage 不随 N 变,但 N 影响总 stage 数 → codegen 容器维度)。

- [ ] **Step 4: code-gen 容器重生 + 编译**

acados code-gen 重生 .so(维度变);编译确认。
Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -DM5_USE_ACADOS=ON 2>&1 | tail -5`

- [ ] **Step 5: acados 求解收敛(1200s horizon)**

跑现有 acados 测试 + Rule14 HO,确认 1200s 下收敛。

- [ ] **Step 6: Commit**

```bash
git commit -am "feat(m5): horizon 90s->1200s + dt benchmark (P4 T2, VR-06b)"
```

---

## Task 3: 废除终端 C10/C11

**Files:**
- `src/mid_mpc/mid_mpc_acados_formulation.cpp`(build_con_h_ L330-335)
- `include/mid_mpc/mid_mpc_acados_formulation.hpp`(nh 23→20)
- `src/mid_mpc/mid_mpc_acados_solver.cpp`(kAcadosNh + RowRegistry 偏移)
- `test/external/acados_backend/gen_mid_mpc_acados.py`(terminal 3 行删 + parity)

- [ ] **Step 1: 写失败测试(nh=20 + 终端不漂出 ODD)**

扩 acados formulation 测试:断言 nh==20(终端 3 行废)+ 1200s horizon 下求解收敛 + 终端 lateral 不漂出 ODD(靠 horizon + stage cost,非终端约束)。

- [ ] **Step 2: build_con_h_ 删终端 3 行**

formulation.cpp L330-335 删:
```cpp
// 删:rows.push_back(pref_dir * l_k - l_min);   // g_term_side
// 删:rows.push_back(l_k + l_max);              // g_term_lo
// 删:rows.push_back(l_max - l_k);              // g_term_hi
```

- [ ] **Step 3: nh 23→20 + RowRegistry 偏移**

hpp: nh 常量 23→20。solver.cpp:kRowTermSide/Lo/Hi 删;kRowDirection/MinAlt 偏移调整(terminal 占 3 行删后,direction/min_alt 仍是 nh 内,偏移重算)。static_assert 更新。

- [ ] **Step 4: gen script 同步 terminal 删 + parity**

gen_mid_mpc_acados.py 删 terminal 3 行 SX 表达;MX/SX parity 验证。

- [ ] **Step 5: 跑测试 + 求解收敛验证**

Run: `colcon test --packages-select m5_tactical_planner --pytest-name test_mid_mpc_acados_formulation 2>&1 | tail -5`
Expected: PASSED(nh=20 + 收敛)。

- [ ] **Step 6: Commit**

```bash
git commit -am "feat(m5): abolish terminal C10/C11 (nh 23->20, P4 T3, VR-07b)"
```

---

## Task 4: solve_timer 60s

**Files:**
- `src/mid_mpc/mid_mpc_node.cpp`(L519)

- [ ] **Step 1: 改 timer**

L519 `std::chrono::seconds(1)` → `std::chrono::seconds(60)`。加注释 VR-06b 理由。

- [ ] **Step 2: 验证(单测或集成)**

on_solve_cycle_ 60s 触发(非 1Hz)。集成测试或 SIL 观察。

- [ ] **Step 3: Commit**

```bash
git commit -am "feat(m5): solve_timer 1Hz->60s (P4 T4, VR-06b)"
```

---

## Task 5: TailBuilder 拼接淘汰(保留复用类型)

**Files:**
- `src/mid_mpc/mid_mpc_node.cpp`(删 append_tail_waypoints_ L1570-1693 + 调用点)
- `src/tail_builder/tail_builder.cpp` + `include/tail_builder/tail_builder.hpp`(删 build/feasible_rejoin/validate_tail;保留复用类型)
- `test/unit/test_tail_builder.cpp` + `test_midmpc_tail_gate.cpp`(拼接测试删;复用类型测试保留)

- [ ] **Step 1: 写失败测试(输出轨迹完整 1200s 覆盖返航,无尾段拼接)**

- [ ] **Step 2: 删 append_tail_waypoints_ 调用 + 函数体**

node.cpp L1570-1693 删 append_tail_waypoints_ 函数 + 其在 publish 流程的调用点(L1778-1781 附近)。输出流程改:NLP 解 → 直接 wp_gen → publish(无拼接)。

- [ ] **Step 3: 删 TailBuilder::build + feasible_rejoin + validate_tail**

tail_builder.cpp/hpp 删拼接逻辑函数。**保留**:ColregSide/ColregRole/EncounterState/TargetSnapshot/RouteFrame(route_frame_has_sharp_corner/project/sample)。

- [ ] **Step 4: 改测试(拼接测试删,复用类型保留)**

test_tail_builder.cpp / test_midmpc_tail_gate.cpp:删拼接相关测试;保留 RouteFrame projection / EncounterState enum 等复用类型测试。

- [ ] **Step 5: 跑测试 + 输出轨迹完整性验证**

Run: `colcon test --packages-select m5_tactical_planner 2>&1 | grep -E "tail|PASSED|FAILED" | tail`
Expected: 拼接测试删后无残留引用;复用类型测试绿;输出轨迹完整(1200s 覆盖返航)。

- [ ] **Step 6: Commit**

```bash
git commit -am "feat(m5): retire TailBuilder splicing, keep reusable types (P4 T5, VR-02)"
```

---

## Task 6: 承诺前缀 180s + material-change version

**Files:**
- `src/mid_mpc/mid_mpc_node.cpp`(committed_prefix 延长 180s + version 语义)
- `include/m5_tactical_planner/committed_route/*.hpp`(若 frozen_prefix_count 需改)

- [ ] **Step 1: 延长承诺前缀到 180s**

compute_frozen_prefix_count 或调用处,承诺前缀从当前值→180s(NLM 推荐上端)。

- [ ] **Step 2: material-change version 语义**

plan version 只在 material-change(role/direction/CPA risk/GNC reject/偏航超阈/M7 VETO/紧急升级)时更新;否则不变(L4 warm-start)。

- [ ] **Step 3: 测试 + Commit**

```bash
git commit -am "feat(m5): committed prefix 180s + material-change version (P4 T6, VR-06)"
```

---

## Task 7: carryover I-1~4(切 ON 前置,强制)

**Files:**
- `include/mid_mpc/mid_mpc_acados_formulation.hpp`(I-1 kMSge static_assert)
- `src/mid_mpc/mid_mpc_acados_solver.cpp`(I-2 S2 counter / I-3 short-TCPA guard)
- `src/mid_mpc/mid_mpc_node.cpp`(I-4 F2 colreg_prefix_softened)

**关键**: 切 M5_USE_ACADOS 默认 ON 前必须补齐。这些都是评审 forward-looking,acados OFF 下不激活,切 ON 后成生产路径。

- [ ] **Step 1: I-1 kMSge static_assert**

formulation.hpp 加 `static_assert(kMSge == manifest_mass_kg * (1+surge_factor))`(锁定 kMSge 与 manifest 一致,防漂移)。

- [ ] **Step 2: I-2 S2 escalation counter**

acados dispatch 路径补 consecutive_failures_ 递增(现绕过 → 不触发 MRM-02)。

- [ ] **Step 3: I-3 short-TCPA(<2000m)dispatch guard**

补 guard:short-TCPA 回退 IPOPT 或 BC-MPC(acados staging 未验短距)。

- [ ] **Step 4: I-4 F2 colreg_prefix_softened**

补 documented gap 闭环(colreg_prefix_softened 在 acados 路径)。

- [ ] **Step 5: 测试每项 + Commit**

```bash
git commit -am "fix(m5): carryover I-1~4 before acados default ON (P4 T7)"
```

---

## Task 8: 切 M5_USE_ACADOS 默认 ON

**Files:**
- `CMakeLists.txt`

- [ ] **Step 1: 改默认**

`option(M5_USE_ACADOS ... ON)`(原 OFF)。

- [ ] **Step 2: 全测试(默认 ON 路径)**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && colcon test --packages-select m5_tactical_planner 2>&1 | tail`
Expected: 全绿(acados 是默认主路径,IPOPT 仍可 =OFF 显式编译)。

- [ ] **Step 3: Commit**

```bash
git commit -am "build(m5): M5_USE_ACADOS default ON (P4 T8, acados production main)"
```

---

## Task 9: 回归 + 验收门 + codex 评审 + handoff

- [ ] **Step 1: IPOPT fallback 验证(M5_USE_ACADOS=OFF 保 90s)**

IPOPT 路径保 90s(不延 horizon),作应急 fallback。验证 =OFF 编译 + 运行。
Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -DM5_USE_ACADOS=OFF 2>&1 | tail -3`

- [ ] **Step 2: acados 默认 ON 路径全测试**

Run: `colcon test --packages-select m5_tactical_planner 2>&1 | grep -E "PASSED|FAILED" | tail`

- [ ] **Step 3: 验收门核对(spec 9 条)**

- [ ] horizon 1200s + dt benchmark(Task 1-2)
- [ ] solve_timer 60s(Task 4)
- [ ] 废终端 C10/C11 nh=20 + 收敛(Task 3)
- [ ] TailBuilder 拼接删 + 输出完整 + 复用类型保留(Task 5)
- [ ] 承诺前缀 180s + material-change(Task 6)
- [ ] RFC-001 推翻记录
- [ ] acados RTI 1200s 实时性达标(头号回炉门,Task 1)
- [ ] M5_USE_ACADOS 默认 ON + carryover I-1~4 闭环(Task 7-8)
- [ ] 回归:IPOPT 90s fallback + acados 全测试(Task 9 Step 1-2)

- [ ] **Step 4: codex 对抗评审(强制,用户要求)**

调用 codex 对照 P4 spec + plan 严格评审:spec/plan 符合性(9 验收门)+ 诚实性(benchmark 真跑三档/废终端真验收敛/TailBuilder 删后输出真完整/carryover 真补非 stub)+ 范围合规 + 切 ON 安全性。0 Critical 才算 P4 完成。

- [ ] **Step 5: handoff 更新 + Commit**

```bash
git commit -am "docs(handoff): record P4 completion (horizon 1200s + acados ON)"
```

---

## Self-Review

**1. Spec 覆盖**: T1 benchmark / T2 horizon / T3 废终端 / T4 timer / T5 TailBuilder / T6 承诺前缀 / T7 carryover I-1~4 / T8 切 ON / T9 验收+评审。9 验收门全覆盖。

**2. Placeholder**: dt benchmark 选定值标"据 Task 1 定"(执行者跑三档后填);carryover I-1~4 具体实现标补 gap(评审已 documented)。无 TBD/TODO/FIXME。

**3. 类型一致**: nh 23→20 全 plan 一致;horizon 1200/dt benchmark 一致;M5_USE_ACADOS ON 一致。

**4. 风险**: 头号回炉(Task 1 benchmark);废终端后收敛(Task 3);TailBuilder 删后输出完整(Task 5);切 ON 安全性(Task 7-8 carryover)。Task 9 codex 评审是最终门。
