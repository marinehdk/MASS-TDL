# P2: Eriksen 相对跟踪 t_b + Huber 位置代价 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地 VR-07b(GNC 跨树修订)两项核心:① 相对跟踪 t_b(投影本船预测轨迹回 nominal route 最近点);② 位置代价纯二次 → Huber(防被障碍推开时指数回拉)。为 P4(长 horizon + 废终端 + TailBuilder 淘汰)奠基。

**Architecture:** 只改 acados formulation 的 route cost 部分 + 新增 t_b 投影纯函数。dynamics/state/control/terminal constraints 全不动(P1b-1b 已落地)。Huber 用 CasADi `if_else`(精确,非 soft 近似)。MX graph 与 SX codegen 须 parity(P1b-1 纪律)。

**Tech Stack:** C++17, CasADi MX(MX graph) + SX(codegen), acados 0.4.4, ROS2 ament_cmake, colcon, gtest

**Spec:** `docs/superpowers/specs/2026-07-17-m5-p2-eriksen-relative-track-huber-design.md`
**生产现状(只读参考):** `src/mid_mpc/mid_mpc_acados_formulation.{hpp,cpp}` + `mid_mpc_acados_solver.cpp` + `mid_mpc_node.cpp`

## Global Constraints

- 工作目录: `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`(分支 codex/m5-design-grounding,HEAD 406b78610)
- **P2 不动**:dynamics(Path B 双积分器)/ state(5 维 x=[px,py,ψ,r,u_surge])/ control(u=[δ,n])/ terminal constraints C10/C11(g_term_side/lo/hi,推 P4)/ TailBuilder(推 P4)/ VDM(推后)/ IPOPT formulation(本 P2 只改 acados)。
- **MX graph 与 SX codegen parity**:`mid_mpc_acados_formulation.cpp`(MX)与 `gen_mid_mpc_acados.py` 或等价 codegen(SX)必须表达一致;P1b-1 已有 parity assertion,P2 改 route cost 须同步两处。**先确认 codegen 脚本路径**(可能在 test/external 或独立 scripts)。
- **Huber 精确形式**(非 soft 近似):`if_else(|l|<=δ_h, 0.5*l*l, δ_h*(fabs(l)-0.5*δ_h))`。δ_h 初值 = l_scale(与现归一化一致),benchmark 调。
- **t_b 投影是纯函数**:点到线段最近点,不依赖 acados;放 shared 工具(header-only 或 small .cpp),单测覆盖退化 case。
- **测试标志**:用 `BUILD_TESTING` + 包名 `m5_tactical_planner`(P0/P1a 已修正)。
- **容器内执行**:`source scripts/a4000-env.sh`;独立 `COMPOSE_PROJECT_NAME=codex-m5-p2`,不碰 mass-l3-sil demo stack。
- **M5_USE_ACADOS**:P2 改在 acados formulation;OFF(IPOPT)路径不受影响。回归门两路都验。
- 每个 task 一个 commit;TDD(先写失败测试,再实现)。

---

## File Structure

| 文件 | 责任 | P2 改动 |
|---|---|---|
| `include/m5_tactical_planner/shared/relative_track.hpp` | t_b 投影纯函数(点到线段最近点 + 有符号横向偏差) | 新增 |
| `src/shared/relative_track.cpp` | t_b 投影实现 | 新增 |
| `test/unit/test_relative_track.cpp` | t_b 投影单测(含退化 case) | 新增 |
| `test/unit/test_huber_cost.cpp` | Huber 代价单测(连续/可导) | 新增 |
| `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp` | Config 加 `huber_delta_h`;声明 huber MX helper | 改 |
| `src/mid_mpc/mid_mpc_acados_formulation.cpp` | `build_route_cost_`(L354-365)+`build_terminal_cost_`(L390-413)改 t_b + Huber;新增 huber_mx_ helper | 改 |
| `src/mid_mpc/mid_mpc_acados_solver.cpp` | pack 时计算 per-stage t_b 投影结果(或 origin 改为 per-stage t_b) | 改 |
| codegen 脚本(SX,路径先确认) | 同步 route cost 改动(MX/SX parity) | 改 |
| `test/unit/test_mid_mpc_acados_*.cpp` | t_b + Huber 集成测试 + parity | 改/新增 |
| `CMakeLists.txt` | 注册 relative_track.cpp + 新测试 | 改 |

---

## Task 1: t_b 投影纯函数 + 单测

**Files:**
- Create: `include/m5_tactical_planner/shared/relative_track.hpp`
- Create: `src/shared/relative_track.cpp`
- Create: `test/unit/test_relative_track.cpp`
- Modify: `CMakeLists.txt`(注册 relative_track.cpp 到 m5_shared_lib + 注册 test_relative_track)

**Interfaces:**
- Produces: `relative_track::project_to_segment(px, py, ax, ay, bx, by) -> {t, closest_x, closest_y, signed_lateral(normal_x, normal_y)}`;签名见 Step 3

- [ ] **Step 1: 写失败测试(test_relative_track.cpp)**

```cpp
#include "m5_tactical_planner/shared/relative_track.hpp"
#include <gtest/gtest.h>

using relative_track::project_to_segment;

TEST(RelativeTrack, PointInsideSegment_FootIsClosest) {
  // 线段 A=(0,0)→B=(10,0),点 P=(5,2):垂足 (5,0),横向偏差 +2(n_hat=(0,1))
  auto r = project_to_segment(/*px=*/5.0, /*py=*/2.0, 0.0, 0.0, 10.0, 0.0, 0.0, 1.0);
  EXPECT_NEAR(r.closest_x, 5.0, 1e-9);
  EXPECT_NEAR(r.closest_y, 0.0, 1e-9);
  EXPECT_NEAR(r.t, 0.5, 1e-9);
  EXPECT_NEAR(r.signed_lateral, 2.0, 1e-9);  // n_hat=(0,1) → +2
}

TEST(RelativeTrack, PointPastB_EndpointIsClosest) {
  // P=(12,1) 在 B=(10,0) 外延长线 → t clamp 到 1,最近点 B
  auto r = project_to_segment(12.0, 1.0, 0.0, 0.0, 10.0, 0.0, 0.0, 1.0);
  EXPECT_NEAR(r.t, 1.0, 1e-9);
  EXPECT_NEAR(r.closest_x, 10.0, 1e-9);
  EXPECT_NEAR(r.signed_lateral, 1.0, 1e-9);
}

TEST(RelativeTrack, PointBeforeA_EndpointIsClosest) {
  // P=(-2,1) 在 A=(0,0) 前 → t clamp 到 0
  auto r = project_to_segment(-2.0, 1.0, 0.0, 0.0, 10.0, 0.0, 0.0, 1.0);
  EXPECT_NEAR(r.t, 0.0, 1e-9);
  EXPECT_NEAR(r.signed_lateral, 1.0, 1e-9);
}

TEST(RelativeTrack, DegenerateZeroLength_Fallback) {
  // 零长线段 A==B → fallback 用 A 作最近点,横向 = (P-A)·n_hat
  auto r = project_to_segment(3.0, 4.0, 1.0, 1.0, 1.0, 1.0, 0.0, 1.0);
  EXPECT_NEAR(r.closest_x, 1.0, 1e-9);
  EXPECT_NEAR(r.signed_lateral, 3.0, 1e-9);  // (4-1)·1
}

TEST(RelativeTrack, NegativeLateral_PortSide) {
  // n_hat=(0,1),P=(5,-2) → 横向 -2(左舷)
  auto r = project_to_segment(5.0, -2.0, 0.0, 0.0, 10.0, 0.0, 0.0, 1.0);
  EXPECT_NEAR(r.signed_lateral, -2.0, 1e-9);
}
```

- [ ] **Step 2: 跑测试确认失败(函数未实现)**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON 2>&1 | grep -E "relative_track|error" | head`
Expected: 编译错误 `'project_to_segment' is not a member`(头文件未实现)。

- [ ] **Step 3: 实现 relative_track.hpp + .cpp**

`include/m5_tactical_planner/shared/relative_track.hpp`:
```cpp
#pragma once

namespace mass_l3::m5::shared::relative_track {

struct Projection {
  double t;              // 参数 t ∈ [0,1](clamp)
  double closest_x;      // 最近点 x
  double closest_y;      // 最近点 y
  double signed_lateral; // 有符号横向偏差 (P-closest)·n_hat
};

// 点 (px,py) 到线段 (ax,ay)→(bx,by) 的最近点投影。
// n_hat=(nx,ny) 是 route 法向量(单位),用于算有符号横向偏差。
// 退化(零长线段)fallback: closest=A, lateral=(P-A)·n_hat。
Projection project_to_segment(double px, double py,
                              double ax, double ay, double bx, double by,
                              double nx, double ny) noexcept;

}  // namespace
```

`src/shared/relative_track.cpp`:标准点到线段投影实现(t = clamp(dot(P-A, B-A)/|B-A|², 0, 1);退化判 |B-A|²<eps)。signed_lateral = (px-closest_x)*nx + (py-closest_y)*ny。

- [ ] **Step 4: 注册 CMakeLists + 跑测试确认通过**

CMakeLists:加 `src/shared/relative_track.cpp` 到 m5_shared_lib 源列表;加 `m5_add_gtest(test_relative_track test/unit/test_relative_track.cpp spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp)`(参考现有 m5_add_gtest 模式,P1a/L301-307)。

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && colcon test --packages-select m5_tactical_planner --pytest-name test_relative_track --event-handlers console_direct+ 2>&1 | tail -5`
Expected: test_relative_track PASSED(5 case 全绿)。

- [ ] **Step 5: Commit**

```bash
git add include/m5_tactical_planner/shared/relative_track.hpp \
        src/shared/relative_track.cpp \
        test/unit/test_relative_track.cpp \
        CMakeLists.txt
git commit -m "feat(m5): relative_track project_to_segment pure function (P2 T1)

Point-to-segment projection for Eriksen relative tracking t_b (VR-07b).
Returns closest point + signed lateral deviation wrt route normal.
Degenerate zero-length segment fallback. 5 unit tests including edge cases."
```

---

## Task 2: Huber 代价纯函数 + 单测

**Files:**
- Create: `test/unit/test_huber_cost.cpp`
- Modify: `include/m5_tactical_planner/shared/relative_track.hpp`(或新建 `huber_cost.hpp`,把 huber 纯函数 double 版放此,便于单测;MX 版在 formulation)

**Interfaces:**
- Produces: `huber_cost(l, delta_h) -> double`(纯函数 double 版,单测用);MX 版在 Task 3 formulation 内联

- [ ] **Step 1: 写失败测试**

```cpp
#include "m5_tactical_planner/shared/huber_cost.hpp"
#include <gtest/gtest.h>
#include <cmath>

TEST(HuberCost, QuadraticRegion_NearZero) {
  // |l| < δ_h: 0.5*l²
  EXPECT_NEAR(huber_cost(0.5, 1.0), 0.125, 1e-9);   // 0.5*0.25
  EXPECT_NEAR(huber_cost(-0.5, 1.0), 0.125, 1e-9);
  EXPECT_NEAR(huber_cost(0.0, 1.0), 0.0, 1e-9);
}

TEST(HuberCost, LinearRegion_Far) {
  // |l| > δ_h: δ_h*(|l| - δ_h/2)
  EXPECT_NEAR(huber_cost(2.0, 1.0), 1.5, 1e-9);   // 1*(2-0.5)
  EXPECT_NEAR(huber_cost(-2.0, 1.0), 1.5, 1e-9);
}

TEST(HuberCost, ContinuousAtDelta) {
  // |l|=δ_h 两公式一致
  double d = 1.0;
  EXPECT_NEAR(huber_cost(d, d), 0.5*d*d, 1e-9);          // 二次:0.5
  EXPECT_NEAR(huber_cost(d, d), d*(d-0.5*d), 1e-9);      // 线性:0.5
}

TEST(HuberCost, DerivativeContinuous_Numerical) {
  // 一阶导 δ_h 处连续(数值):huber'(δ_h+ε)≈huber'(δ_h-ε)
  double d = 1.0, eps = 1e-6;
  double dp_pos = (huber_cost(d+eps, d) - huber_cost(d, d)) / eps;
  double dp_neg = (huber_cost(d, d) - huber_cost(d-eps, d)) / eps;
  EXPECT_NEAR(dp_pos, dp_neg, 1e-4);
  // 导数幅值 = δ_h(线性区斜率)
  EXPECT_NEAR(dp_pos, d, 1e-4);
}
```

- [ ] **Step 2: 跑确认失败 → 实现 huber_cost.hpp → 注册 → 跑通过**

`include/m5_tactical_planner/shared/huber_cost.hpp`(header-only inline):
```cpp
#pragma once
#include <cmath>

namespace mass_l3::m5::shared {

// Huber loss: 0.5*l² if |l|<=δ_h, else δ_h*(|l|-0.5*δ_h). C0 continuous,
// C1 smooth at δ_h (derivative = δ_h in linear region). For SQP-friendly
// position cost (VR-07b): near-zero quadratic, far linear (no exponential
// pull-back when pushed off-route by an obstacle).
inline double huber_cost(double l, double delta_h) noexcept {
  double a = std::fabs(l);
  return (a <= delta_h) ? 0.5 * l * l : delta_h * (a - 0.5 * delta_h);
}

}  // namespace
```
CMakeLists:加 `m5_add_gtest(test_huber_cost test/unit/test_huber_cost.cpp ...)`(header-only,无需额外源文件链接)。

Run: `colcon test --packages-select m5_tactical_planner --pytest-name test_huber_cost 2>&1 | tail -3`
Expected: PASSED。

- [ ] **Step 3: Commit**

```bash
git add include/m5_tactical_planner/shared/huber_cost.hpp test/unit/test_huber_cost.cpp CMakeLists.txt
git commit -m "feat(m5): huber_cost pure function (P2 T2, VR-07b)

Header-only Huber loss for position cost (VR-07b): quadratic near zero,
linear far. C0/C1 continuous at delta_h. 4 unit tests including derivative
continuity (numerical)."
```

---

## Task 3: acados formulation build_route_cost_ 改 t_b + Huber(MX)

**Files:**
- Modify: `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp`(Config 加 `huber_delta_h`)
- Modify: `src/mid_mpc/mid_mpc_acados_formulation.cpp` `build_route_cost_`(L354-365)+ 新增 `huber_mx_` helper

**Interfaces:**
- Consumes: Task 1 t_b 概念(但 MX 内联投影,不调纯函数 — MX 符号图不调 runtime 函数)+ Task 2 Huber 形式
- Produces: 改后的 `build_route_cost_` 返回 `w_guard * huber_mx_(l, δ_h) / l_scale²`

**关键**: MX 符号图内不能调 `relative_track::project_to_segment`(runtime C++ 函数)。t_b 投影须用 CasADi MX 表达式内联(或 origin 参数 per-stage 预计算后传入)。两种策略,选 A:

- **策略 A(per-stage origin 预计算,simpler)**: solver 在 pack 时用 Task 1 纯函数算每 stage 的 t_b[k] 最近点,作为 per-stage origin 参数传入;MX graph 用 origin(参数)算 l,与现状一致(只改 origin 来源:固定 route_origin → per-stage t_b)。**推荐**:MX graph 几乎不变,只改 solver pack。
- **策略 B(MX 内联投影)**: MX graph 内联投影公式,route 线段端点作参数。复杂,易出符号 bug。

- [ ] **Step 1: 写失败测试(formulation 单测,t_b + Huber)**

扩 `test/unit/test_mid_mpc_acados_formulation.cpp`(若不存在则参考 P1b-1b 测试模式新建)。测试:
```cpp
// build_route_cost_ 在给定 l 下返回 Huber(非纯二次)
// 构造 formulation,input 使 l=0.5*l_scale(二次区)/ l=2*l_scale(线性区),
// 断言 J_route = w_guard * huber(0.5,δ_h)/l_scale² vs huber(2,δ_h)/l_scale²
// 用 Task 2 的 huber_cost 纯函数作 oracle
```

- [ ] **Step 2: 跑确认失败(build_route_cost_ 还是纯二次)**

- [ ] **Step 3: Config 加 huber_delta_h**

`mid_mpc_acados_formulation.hpp` Config struct 加:
```cpp
double huber_delta_h{400.0};  // Huber radius [m], default = lateral_scale (VR-07b); benchmark
```
(400.0 = lateral_scale_m 默认值,roadmap 初值提议)

- [ ] **Step 4: 加 huber_mx_ helper + 改 build_route_cost_(策略 A)**

`mid_mpc_acados_formulation.cpp` 加私有 helper:
```cpp
casadi::MX MidMpcAcadosFormulation::huber_mx_(const casadi::MX& l,
                                                double delta_h) const {
  // MX Huber: if_else(|l|<=δ_h, 0.5*l², δ_h*(|l|-0.5*δ_h))
  // fabs 用 MX::fabs; if_else 是 CasADi 条件(平滑切换,codegen 出 piecewise)
  const casadi::MX a = casadi::MX::fabs(l);
  const casadi::MX quad = 0.5 * l * l;
  const casadi::MX lin = delta_h * (a - 0.5 * delta_h);
  return casadi::MX::if_else(a <= delta_h, quad, lin);
}
```
改 `build_route_cost_`(L354-365):
```cpp
casadi::MX MidMpcAcadosFormulation::build_route_cost_() const {
  const casadi::MX px = x_(0);
  const casadi::MX py = x_(1);
  // 策略 A: origin 是 per-stage t_b 投影(solver pack 时算),normal 不变。
  // l = (px - t_b_x)*nx + (py - t_b_y)*ny  (相对 t_b,非绝对 route origin)
  const casadi::MX ox = gslot_(kGIdxRouteFrameOriginX);  // 现语义改:per-stage t_b.x
  const casadi::MX oy = gslot_(kGIdxRouteFrameOriginY);  // per-stage t_b.y
  const casadi::MX nx = gslot_(kGIdxRouteFrameNormalX);
  const casadi::MX ny = gslot_(kGIdxRouteFrameNormalY);
  const casadi::MX l_scale = gslot_(kGIdxLateralScale);
  const casadi::MX w_guard = gslot_(kGIdxRouteWeight);
  const casadi::MX l = (px - ox) * nx + (py - oy) * ny;
  // VR-07b: 纯二次 (l/l_scale)² → Huber(l, δ_h)/l_scale²
  const casadi::MX hub = huber_mx_(l, cfg_.huber_delta_h);
  return w_guard * hub / (l_scale * l_scale);
}
```
**注**: `kGIdxRouteFrameOriginX/Y` 语义从"绝对 route origin"改为"per-stage t_b 投影最近点"(策略 A,字段名暂不改以减少 churn,加注释说明)。若需改名为 `kGIdxTbX/Y`,brainstorming 后续定,本 plan 保字段名 + 注释。

- [ ] **Step 5: 跑 formulation 测试确认通过**

Run: `colcon test --packages-select m5_tactical_planner --pytest-name test_mid_mpc_acados_formulation 2>&1 | tail -5`
Expected: PASSED(Huber 数值对)。

- [ ] **Step 6: Commit**

```bash
git add include/m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp \
        src/mid_mpc/mid_mpc_acados_formulation.cpp \
        test/unit/test_mid_mpc_acados_formulation.cpp
git commit -m "feat(m5): acados build_route_cost_ relative t_b + Huber (P2 T3, VR-07b)

Route cost changes (strategy A: per-stage t_b projection as origin):
- origin (kGIdxRouteFrameOriginX/Y) semantics: absolute route origin ->
  per-stage t_b closest-point (solver packs t_b via project_to_segment).
- cost: pure quadratic (l/l_scale)^2 -> Huber(l, delta_h)/l_scale^2
  (VR-07b: quadratic near zero, linear far; no exponential pull-back).
huber_mx_ MX helper (if_else piecewise). Config.huber_delta_h default=400."
```

---

## Task 4: build_terminal_cost_ lN 锚 t_b + solver pack t_b

**Files:**
- Modify: `src/mid_mpc/mid_mpc_acados_formulation.cpp` `build_terminal_cost_`(L390-413)
- Modify: `src/mid_mpc/mid_mpc_acados_solver.cpp`(pack 时算 per-stage t_b 投影)

**Interfaces:**
- Consumes: Task 1 project_to_segment(纯函数,solver pack 调)+ Task 3(terminal lN 用同一 origin 语义)

- [ ] **Step 1: 改 build_terminal_cost_ lN 锚(同 Task 3 origin 语义)**

L390-413 `build_terminal_cost_`:lN 计算同 build_route_cost_ 的 l,用 origin(现在语义是 t_b[N-1])+ normal。softplus 形式(wrong_side/l_max/J_lower/J_upper)全不变,只 lN 基准从绝对 → t_b。加注释说明。

- [ ] **Step 2: solver pack 算 per-stage t_b 投影**

`mid_mpc_acados_solver.cpp` pack_parameters 或设置 per-stage origin 处:
```cpp
// 每周期用 project_to_segment 算 per-stage t_b:
// 输入:nominal route 当前线段 (A,B) + 本船预测轨迹每步 (px[k],py[k])
//   px[k],py[k] 从 warm-start seed 或上周期解(forward 模拟)
// 输出:per-stage origin = t_b[k] 最近点 (closest_x, closest_y)
// 设到 kGIdxRouteFrameOriginX/Y(per-stage 参数,P1b-1 已有 per-stage param 机制)
```
**关键决策点**: t_b 投影需要本船预测位置 (px[k],py[k])。但 pack 在 solve 之前 —— 没有 current 解。两条路:
- (a) 用 warm-start seed 的 (px[k],py[k])(上周期解 shift,或 forward 模拟 own_psi/own_u)算 t_b。推荐(seed 已有,P1a F1)。
- (b) 用当前 own position + planned route bearing 线性外推算 t_b(更粗)。

选 (a): 用 seed 的预测位置算 t_b。若 seed 不可用(首周期),fallback 用绝对 route origin(Task 1 退化逻辑)。

- [ ] **Step 3: 写/改集成测试(t_b 锚 terminal + solver pack)**

测试 solver pack 在给定 nominal route + 预测轨迹下,per-stage origin = t_b 投影结果(用 Task 1 纯函数作 oracle 验证)。

- [ ] **Step 4: 跑测试 + 求解收敛验证**

Run: `colcon test --packages-select m5_tactical_planner --pytest-name test_mid_mpc_acados_solver 2>&1 | tail -5`
Expected: PASSED(pack 正确 + 求解收敛)。

- [ ] **Step 5: Commit**

```bash
git add src/mid_mpc/mid_mpc_acados_formulation.cpp \
        src/mid_mpc/mid_mpc_acados_solver.cpp \
        test/unit/test_mid_mpc_acados_solver.cpp
git commit -m "feat(m5): terminal lN anchor t_b + solver pack per-stage t_b (P2 T4)

build_terminal_cost_ lN uses same origin semantics as route cost (per-stage
t_b). Solver pack_parameters computes per-stage t_b projection via
project_to_segment on warm-start seed trajectory. Fallback: absolute route
origin on first cycle (no seed)."
```

---

## Task 5: codegen parity(MX ↔ SX)

**Files:**
- Modify: codegen 脚本(SX,路径先确认 — 可能在 `test/external/` 或 `scripts/` 或 `tools/`)

**关键**: P1b-1 纪律要求 MX graph 与 SX codegen 表达一致。P2 改 build_route_cost_/build_terminal_cost_ 后,codegen SX 须同步。

- [ ] **Step 1: 定位 codegen 脚本**

Run: `grep -rln "build_route_cost\|J_route\|RouteFrameOrigin\|acados_template\|AcadosOcp" --include="*.py" src/ test/ scripts/ tools/ 2>/dev/null | head`
Expected: 找到 gen_mid_mpc_acados.py 或等价。

- [ ] **Step 2: codegen SX 同步 route cost 改动(t_b origin + Huber)**

读 codegen 脚本的 route cost SX 表达,改为与 Task 3/4 MX 一立(per-stage t_b origin + Huber if_else)。Huber SX:`ca.if_else(ca.fabs(l)<=δ_h, 0.5*l*l, δ_h*(ca.fabs(l)-0.5*δ_h))`。

- [ ] **Step 3: parity assertion 验证**

P1b-1 应有 parity assertion(MX vs SX 数值一致)。跑之确认 route cost 改动后仍 parity。
Run: `colcon test --packages-select m5_tactical_planner --pytest-name test_mid_mpc_acados_parity 2>&1 | tail`(或 P1b-1 parity 测试名)
Expected: PASSED(MX/SX route cost 一致)。

- [ ] **Step 4: Commit**

```bash
git add <codegen script path>
git commit -m "feat(m5): codegen SX route cost parity t_b + Huber (P2 T5)

Sync SX codegen route cost with MX graph (Task 3/4): per-stage t_b origin +
Huber if_else. Parity assertion passes."
```

---

## Task 6: 集成 + benchmark + 回归 + 验收门

**Files:**
- (无新文件;集成测试 + benchmark + 回归)

- [ ] **Step 1: acados 端到端集成测试(t_b + Huber 求解收敛 + 行为合理)**

场景:Rule14 HO 或单目标避让。验证:
- 求解收敛(status 0 或容忍 status 4,F5)
- 轨迹行为:相对跟踪下避让不过早归航(l 不爆炸式拉回,Huber 远处线性)
- t_b 投影每步正确(用 Task 1 oracle)

- [ ] **Step 2: 回归门 — IPOPT 路径无回归**

Run: `COMPOSE_PROJECT_NAME=codex-m5-p2 docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes bash -c "cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -DM5_USE_ACADOS=OFF && colcon test --packages-select m5_tactical_planner --event-handlers console_direct+ 2>&1 | grep -E 'PASSED|FAILED' | tail -10"`
Expected: IPOPT 路径全绿(本 P2 只改 acados formulation,IPOPT formulation 不动)。

- [ ] **Step 3: 回归门 — acados 路径无回归**

Run: 同上但 `-DM5_USE_ACADOS=ON`。
Expected: acados 现有测试全绿 + P2 新测试(t_b/Huber/integration)全绿。

- [ ] **Step 4: benchmark(vs P1b-1b 基线)**

对比 P2 前(commit 406b78610)vs P2 后:
- 同 MidMpcInput,轨迹行为对比(相对跟踪 vs 绝对 route frame)
- l 曲线:Huber 远处应线性(非二次爆炸)
- 求解时间不应显著退化(t_b 投影是 O(N) 纯函数,Huber MX 简单)

- [ ] **Step 5: 验收门核对(spec 7 条)**

- [ ] t_b 投影单测全绿(Task 1)
- [ ] Huber 代价单测全绿(Task 2)
- [ ] acados build_route_cost_ 改 t_b + Huber,codegen 同步 parity(Task 3+5)
- [ ] build_terminal_cost_ lN 锚 t_b(Task 4)
- [ ] acados 求解收敛 + 轨迹合理(Task 6 Step 1)
- [ ] IPOPT 路径无回归(Task 6 Step 2)
- [ ] acados 现有测试全绿(Task 6 Step 3)

- [ ] **Step 6: 更新 handoff/workspace_log.md**

追加 P2 完成条目(改动文件/测试结果/验收门/benchmark)。

- [ ] **Step 7: Commit handoff**

```bash
git add handoff/workspace_log.md
git commit -m "docs(handoff): record P2 relative-track + Huber completion"
```

---

## Task 7: codex 对抗评审(强制,开发完成后)

**Files:**
- (评审,无代码改动;除非评审发现须修)

> **强制要求(用户)**: 所有开发完成(Task 1-6)后,必须调用 codex 对照 P2 spec + plan 做严格完成情况评审。评审前不算 P2 完成。

- [ ] **Step 1: 调用 codex 评审**

用 codex(或 tdl-code-reviewer agent)对照:
- `docs/superpowers/specs/2026-07-17-m5-p2-eriksen-relative-track-huber-design.md`(spec 验收门 7 条 + 排除项)
- `docs/superpowers/plans/2026-07-17-m5-p2-eriksen-relative-track-huber.md`(本 plan Task 1-6)

评审范围:
1. **spec 符合性**:7 验收门逐条核对(t_b/Huber/parity/收敛/回归)
2. **plan 符合性**:Task 1-6 各 step 是否完成,代码与 plan 描述一致
3. **诚实性**:无 mock/forced-pass/调阈值过测试;t_b 退化 case 真覆盖(非 tautology);Huber 数值真验(非恒等)
4. **范围合规**:P2 未越界(dynamics/state/terminal C10C11/TailBuilder/VDM/carryover 全未动)
5. **MX/SX parity 真验**(非跳过)
6. **回归真跑**(IPOPT + acados 两路,colcon test 实际输出,非声称)

- [ ] **Step 2: 评审发现分类处置**

- **Critical**(spec/plan 违反/forced-pass/范围越界):必须修,修后重审。
- **Important**(forward-looking/边界 case 缺):记录,评估是否 P2 修或推后。
- **Minor**(文档/注释):记录,可选修。

- [ ] **Step 3: 评审通过后,P2 算完成**

记录评审结论(0 Critical / N Important / M Minor)+ 处置到 handoff。

---

## Self-Review(plan 作者自检)

**1. Spec 覆盖**:
- ✅ t_b 投影 → Task 1(纯函数 + 单测含退化)
- ✅ Huber 代价 → Task 2(纯函数 + 单测含导数连续)
- ✅ build_route_cost_ 改 → Task 3(MX,策略 A per-stage origin)
- ✅ build_terminal_cost_ lN 锚 → Task 4
- ✅ solver pack t_b → Task 4
- ✅ MX/SX parity → Task 5
- ✅ 集成 + benchmark + 回归 + 7 验收门 → Task 6
- ✅ codex 强制评审 → Task 7

**2. Placeholder 扫描**:
- codegen 脚本路径标"先确认"(Task 5 Step 1 grep 定位)—— 因未读全 codegen 脚本位置;执行者 grep 找到后填充。给了 grep 命令。
- Task 3 策略 A vs B 选 A(给了理由:solver pack 改 origin 来源比 MX 内联投影简单,MX graph 几乎不变)。
- Task 4 t_b 投影需预测位置,选 (a) warm-start seed(给了理由:fallback 逻辑清晰)。
- 无 TBD/TODO/FIXME(除引用的 TBD-5/6/7)。

**3. 类型一致**: huber_delta_h(double,默认 400= lateral_scale)全 plan 一致;kGIdxRouteFrameOriginX/Y 字段名保持(语义改,加注释)全 plan 一致。

**4. 风险**: codegen parity(Task 5,若 codegen 脚本结构复杂可能难同步 — 先确认路径);t_b 投影需预测位置(Task 4 seed/fallback);Huber MX if_else 在 SQP 下行为(Task 6 benchmark 验)。Task 7 codex 评审是最终门。
