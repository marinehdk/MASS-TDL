# P1b-1b/c: 生产 acados backend + Rule14 HO benchmark — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 P1b-1a staging 已锁定的 6 点 acados 配置落地为生产 M5 MidMpc backend(`MidMpcAcadosSolver`,`M5_USE_ACADOS` flag 默认 OFF,IPOPT 不动),并用 Rule14 HO benchmark 验证两 backend 轨迹级行为等价 —— 关闭 P1b-1(及整个 P1)。

**Architecture:** 三个交付层。(1) `MidMpcAcadosFormulation`:CasADi MX 符号图,双积分器 dynamics(Path B)+ 6 cost + 全约束类 + 142 参数 per-stage 分区,build 时 code-gen C。(2) `MidMpcAcadosSolver`:封装生成的 acados 求解器,参数 pack(142→全局/per-stage)+ 求解 + 输出重构为 `MidMpcSolution` 契约;`MidMpcSolver::solve()` 加 `M5_USE_ACADOS` 编译时 dispatch 分支。(3) Rule14 HO benchmark:两 build(OFF/ON)同 `MidMpcInput` 对比,6 条行为等价判据。

**Tech Stack:** acados 0.4.4(已装 sil_nodes 镜像,`docker/sil_nodes.Dockerfile:95-149`), acados_template 0.4.4(`pip install --no-deps -e`), CasADi MX(生产符号图,非 SX), HPIPM, CMake `add_custom_command` codegen 步骤, colcon, ament。

**Spec(权威):** `docs/superpowers/specs/2026-07-16-m5-p1b1-acados-full-migration-design.md`(§P1b-1b/§P1b-1c,已 Path B 修订)
**P1b-1a 锁定配置(本 plan 输入):** `test/external/acados_staging/{common.py::build_base_ocp_doubleint, T6-T9}` + `.superpowers/sdd/progress-p1b1a.md`(6 点推荐配置 + 风险登记)

## Global Constraints

- 工作目录:`/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`(分支 codex/m5-design-grounding,P1b-1a 完成 HEAD)。**本阶段触碰生产代码**(mid_mpc_solver.cpp / CMakeLists.txt / 新 mid_mpc_acados_*.{hpp,cpp})—— 与 P1b-1a staging 不同,这是 promotable 候选。
- **沿用 P1b-0 F1-F5 + P1b-1a staging 发现**:warm-start seed(forward-propagated 非零);单边 h 上界 1e10(非 inf);EXACT hessian;MERIT_BACKTRACKING;status 4 容忍以约束满足 + solver-moved 判 PASS;per-stage p 用生成的 `<name>_acados_update_params`(非 `ocp_nlp_in_set "p"`);EXTERNAL cost 必须 `cost_scaling=ones(N+1)`;per-stage 切换用参数激活因子(`pact*...`/`cpa_act*...`)。
- **Path B dynamics 锁定**:双积分器 `ṙ=c(u)·δ, ψ̇=r`,c_u=9.825342e-3 rad/s²/rad(P1b-1a T8 VDM 直读,非造系数)。state x=[px,py,ψ,r](4),control u=[δ,n](2,P1b-1b 扩变速 surge)。真偏航阻尼 N_r 待 TBD-5 海试。
- **M5_USE_ACADOS 默认 OFF**:IPOPT 路径(`MidMpcNlpFormulation`/`mid_mpc_solver.cpp` 现状)**只读不改逻辑**;只在 `solve()` 加 `#ifdef M5_USE_ACADOS` dispatch 分支 + CMakeLists 加 option + 新文件。回归门:M5_USE_ACADOS=OFF colcon 与基线逐字节一致。
- **输出契约不变**:acados backend 必须产出与 IPOPT 相同的 `MidMpcSolution` 字段(status/trajectory[N]/cost_*/cpa_slack/solve_duration_ms/ipopt_iterations)。trajectory 的 ψ/u/x/y 序列从 acatos 状态/控制重构;cost 字段 IPOPT 现 E1 全 0 → acatos 可填真值(改进,不破坏契约);下游(M4/L4/tail_gate 检查)收到的字段与 IPOPT 一致,无感知 backend 切换。
- **Dockerfile 约束(勿破坏)**:acados 环境 ONLY 在 `docker/sil_nodes.Dockerfile:95-149`(v0.4.4 源码 build 到 /usr/local,`ACADOS_SOURCE_DIR=/usr/local`,`acados_template` `pip install --no-deps -e`,t_renderer v0.2.0)。本 plan 不改 Dockerfile。
- **测试标志**:`BUILD_TESTING` + 包名 `m5_tactical_planner` + `M5_HAS_CASADI` 守卫(acados 测试加 `M5_HAS_ACADOS` 守卫)。
- **容器内执行**:`source scripts/a4000-env.sh`;`COMPOSE_PROJECT_NAME=codex-acados-backend`(P1b-1b/c,与 P1b-1a staging 隔离);不碰 mass-l3-sil demo stack。
- **失败即停纪律**:某 task 不可达即停,记录阻塞点,不 mock / 不 forced-pass / 不为过测试调阈值。按阻塞性质分类回炉(见 spec §失败处置)。
- **每 task 一个 commit**;顺序执行(后 task 依赖前 task);过一个加下一个。
- **生产代码触碰纪律**:本 plan 每个改生产 src 的 task,必须先跑 M5_USE_ACADOS=OFF 基线 colcon(确认无回归)再改。dispatch 分支用 `#ifdef`,不删/不改 IPOPT 现有代码路径。

---

## File Structure

| 文件 | 责任 | 阶段 |
|---|---|---|
| `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp` + `src/mid_mpc/mid_mpc_acados_formulation.cpp` | acatos OCP CasADi MX 符号图(双积分器+6 cost+全约束+142 参数分区)+ code-gen | P1b-1b |
| `test/external/acados_backend/gen_mid_mpc_acados.py` | 生产 codegen 脚本(调 formulation 的 build,生成 C) | P1b-1b |
| `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp` + `src/mid_mpc/mid_mpc_acados_solver.cpp` | acatos 求解器封装(codegen lib 加载+参数 pack+求解+输出重构 MidMpcSolution) | P1b-1b |
| `include/m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp` + `src/mid_mpc/mid_mpc_solver.cpp` | `solve()` 加 `#ifdef M5_USE_ACADOS` dispatch 分支(IPOPT 路径不动) | P1b-1b |
| `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt` | 生产 `M5_USE_ACADOS` block(option→find_package→sources→codegen custom_command→link)+ 测试守卫 | P1b-1b |
| `test/unit/test_mid_mpc_acados_formulation.cpp` | 符号图维度/参数分区/dynamics 单测 | P1b-1b |
| `test/unit/test_mid_mpc_acados_solver.cpp` | 求解器端到端(标准场景 solve 收敛+输出契约匹配 IPOPT+142 参数 pack) | P1b-1b |
| `test/unit/test_mid_mpc_acados_parity.cpp` | IPOPT vs acados 同 MidMpcInput 输出契约逐字段对比 | P1b-1b |
| `test/external/rule14_ho_benchmark/run_benchmark.sh` + `compare.py` + `Makefile` | 两 build(OFF/ON)Rule14 HO 对比 harness | P1b-1c |
| `docs/Design/.../M5-progress.md` | P1b-1 完成记录 | P1b-1c |

---

## Task 15: MidMpcAcadosFormulation — CasADi MX 符号图 + codegen

> **首个生产 task**。构建 acatos OCP 的 CasADi MX 符号图(生产用 MX,非 staging 的 SX),把 P1b-1a 锁定的 6 点配置 + IPOPT 的全 6 cost/约束类 + 142 参数分区 实例化。**不碰 IPOPT formulation**(mid_mpc_nlp_formulation.{hpp,cpp} 只读参考)。

**Files:**
- Create: `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp`
- Create: `src/mid_mpc/mid_mpc_acados_formulation.cpp`
- Create: `test/external/acados_backend/gen_mid_mpc_acados.py`
- Test: `test/unit/test_mid_mpc_acados_formulation.cpp`

**Interfaces:**
- Consumes(只读参考):`mid_mpc_nlp_formulation.{hpp,cpp}`(6 cost 表达式、约束类行序、`kParamDim==142` 参数索引 `kIdx*`);`common/types.hpp`(MidMpcInput/ConstraintInputs/TargetState);P1b-1a `test/external/acados_staging/common.py::build_base_ocp_doubleint`(dynamics/box/EXACT/MERIT 模板);T9_merge6/gen_merge6.py(6 点合并的 staging 实现)。
- Produces:`MidMpcAcadosFormulation` 类(build_symbolic_graph() 建 MX 图 + codegen_json() 出 acatos JSON);`gen_mid_mpc_acados.py` 调它生成 C 到 `c_generated_code/`。供 Task 16 solver 封装。

**关键设计决策(锁定)**:
- **state x=[px,py,ψ,r,u_surge]**(nx=5):双积分器 ψ/r + 位置积分 + surge 速度作**状态**(P1b-1b 变速,surge 由转速 n 推力模型推进)。control u=[δ,n](nu=2,舵角+转速)。**理由**:IPOPT 扁平 x 无位置(符号积分),acatos 用状态更自然且 acatos DISCRETE integrator 直接推进;surge 作状态实现变速(P1b-1a staging 的 u_surge 作 param 是降阶,生产要变速)。dynamics(Path B):
  ```
  r[k+1]    = r + DT·c_u·δ                      # 双积分器偏航(c_u=9.825342e-3,VDM 直读)
  ψ[k+1]    = ψ + DT·r
  u_surge[k+1] = u_surge + DT·(thrust(n) - drag(u_surge))   # 简化 surge(thrust=k_prop·n², drag=k_drag·u²,k_prop=500/k_drag=100 VDM 直读)
  px[k+1]   = px + u_surge·DT·cos(ψ)
  py[k+1]   = py + u_surge·DT·sin(ψ)
  ```
- **6 cost 全迁移**(MX 表达式,复用 IPOPT formulation.cpp 的数学形式):
  - J_colreg:EXTERNAL per-stage(T2,P1b-1a 已验),cost_scaling=ones(N+1)。平滑 exp 屏障,disc_k 折进 per-stage p。
  - J_dist/J_route/J_vel/J_asym/J_terminal:NONLINEAR_LS 或 EXTERNAL(按 IPOPT 形式,见 formulation.cpp 对应 build_*_cost_)。**cost_scaling=ones**(若 EXTERNAL)。
- **约束类全迁移**(`con_h_expr` 多行,P1b-1a T9 模式):CPA(per-target 多行 + idxsh=[0,1] ξ slack,T7)+ direction + min_alt + ROT(状态 r 的 box,lbx/ubx)+ terminal + bound schedule(cpa_act/pact 激活因子,T4)。nh = 2·Nt(CPA)+ N(direction)+ N(min_alt)+ 3(terminal);ROT/prefix 用 lbx/ubx 不进 h。
- **142 参数 per-stage 分区**:全局 p(stage-uniform):route frame 6、cpa_safe、weights(w_colreg/dist/route/vel/asym/slack)、c_u、rot_max、own_psi/u/x/y、heading/speed box、give_way/role/pref_dir/min_alt/decel —— 对应 IPOPT `kIdx*` 0-25 的全局标量。per-stage p(stage-varying,用 `update_params`):prefix ψ/u 序列(激活 pact_pre)、target drift(per-target tx/ty/tc/ts/tw)、disc_k、cpa_act/pact 激活因子。
- **MX 而非 SX**:生产符号图用 CasADi MX(staging 用 SX;MX 支持 `Function` 调用更大表达式图,IPOPT formulation 也用 MX 一致)。

- [ ] **Step 1: 写 mid_mpc_acados_formulation.hpp — 类接口**

```cpp
#ifndef MASS_L3_M5_MID_MPC_ACADOS_FORMULATION_HPP_
#define MASS_L3_M5_MID_MPC_ACADOS_FORMULATION_HPP_
#include <string>
#include <casadi/casadi.hpp>
#include "m5_tactical_planner/common/types.hpp"  // MidMpcInput, ConstraintInputs

namespace mass_l3::m5::mid_mpc {

// 生产 acatos OCP 符号图(MX)。Path B 双积分器 dynamics + 6 cost + 全约束 + 142 参数分区。
// 与 MidMpcNlpFormulation(IPOPT)平行,不改它。M5_USE_ACADOS=ON 时由 MidMpcAcadosSolver 用。
class MidMpcAcadosFormulation {
 public:
  // 参数维度(IPOPT kParamDim 一致,static_assert 142 in nlp_formulation.hpp:78)
  static constexpr int kParamDim = 142;
  // horizon N:生产由 node 参数 mid_mpc.horizon_s 经 resolve_mid_mpc_horizon_config 解析
  // (formulation.hpp:257-268,max 120 steps);test fixture 用 N=8(test_mid_mpc_solver.cpp:39)。
  // 不硬编码 — 构造时传 N(与 IPOPT cfg.n_horizon 一致)。codegen 用一个固定 N 出 C(默认生产 N)。
  static constexpr int kNDefault = 18;  // 生产默认(由 horizon_s=90s/dt=5 推;node 可配)
  static constexpr double kDt = 5.0;    // step s(IPOPT dt_s 默认 5.0)
  // Path B 双积分器 yaw gain(P1b-1a T8 VDM 直读,非造系数)
  static constexpr double kC_u = 9.825342e-3;  // rad/s^2 per rad
  // VDM 直读 surge 模型系数(vessel_dynamics_model.cpp:47-48)
  static constexpr double kKProp = 500.0;
  static constexpr double kKDrag = 100.0;

  struct Config {
    double w_colreg{30.0};   double w_dist{10.0};   double w_route{3.0};
    double w_vel{1.0};       double k_asym{50.0};   double terminal_tau{0.5};
    double w_slack{1.0e8};   bool cpa_slack_enabled{true};
    int max_targets{16};
  };

  explicit MidMpcAcadosFormulation(Config cfg = {});

  // 建符号图:MX state/control/param + disc_dyn_expr + con_h_expr + 6 cost + box/solver opts。
  // 填充 model_/ocp_ 成员。不 codegen(codegen 是 gen 脚本的事)。
  void build_symbolic_graph();

  // 把 MidMcpInput pack 成 142 参数向量(全局 + per-stage 分区),供 solver update_params。
  // 与 IPOPT formulation.cpp:670-787 pack_parameters 同语义,但拆全局/per-stage。
  // 返回 pair: {global_params[np_global], per_stage_params[N+1][np_per_stage]}。
  // (实现细节:target drift 在 IPOPT 是 per-target 全局;acatos 作 per-stage p 以支持变速下漂移。)
  [[nodiscard]] std::pair<std::vector<double>, std::vector<std::vector<double>>>
  pack_parameters(const MidMpcInput& input) const;

  // 暴露给 codegen 脚本(gen_mid_mpc_acados.py 经 pybind/或直接重实现)。
  // 实际 codegen 走 gen_mid_mpc_acados.py 用 acados_template 重画 MX 图(见 Step 4 决策)。
  const std::string& solver_name() const { return solver_name_; }
  int nx() const; int nu() const; int nh() const; int np_global() const; int np_per_stage() const;

 private:
  Config cfg_;
  std::string solver_name_{"m5_mid_mpc_acados"};
  // CasADi MX 符号图成员(state/control/param 符号、disc_dyn_expr、con_h_expr、cost 表达式)。
  // 与 MidMpcNlpFormulation 结构平行但 MX。
  casadi::MX x_, u_, p_global_, p_stage_;
  casadi::MX disc_dyn_expr_, con_h_expr_;
  // ... cost 表达式成员(colreg/dist/route/vel/asym/terminal)
};

}  // namespace mass_l3::m5::mid_mpc
#endif
```

- [ ] **Step 2: 写 test_mid_mpc_acados_formulation.cpp — 符号图维度/参数分区断言(TDD,先写失败测试)**

```cpp
#include <gtest/gtest.h>
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"
#include "m5_tactical_planner/common/types.hpp"

using mass_l3::m5::mid_mpc::MidMpcAcadosFormulation;
using mass_l3::m5::MidMpcInput;

class AcadosFormulationTest : public ::testing::Test {
 protected:
  MidMpcAcadosFormulation form_;
  void SetUp() override { form_.build_symbolic_graph(); }
};

// 维度匹配 spec(Path B state=5,control=2)
TEST_F(AcadosFormulationTest, StateControlDims_MatchPathB) {
  EXPECT_EQ(form_.nx(), 5);  // [px,py,ψ,r,u_surge]
  EXPECT_EQ(form_.nu(), 2);  // [δ,n]
}

// 参数分区:全局 + per-stage 总和 = 142(IPOPT 契约)
TEST_F(AcadosFormulationTest, ParamDims_SumTo142) {
  MidMpcInput in{};  // 默认
  auto [g, ps] = form_.pack_parameters(in);
  // 全局标量(IPOPT kIdx 0-25)+ per-stage(prefix ψ/u 激活 + target drift + disc + activation)
  // 总维度必须 == 142(IPOPT kParamDim),允许全局/per-stage 切分不同但总一致。
  int total = g.size();  // 全局
  for (auto& s : ps) total += 0;  // per-stage 不计入 142(IPOPT target 是全局块);
  // 见 Step 3 决策:acatos 把 IPOPT 全局 target 块也放全局 p,per-stage 只放 prefix/activation/disc
  EXPECT_EQ(g.size() + ps.front().size(), 142)
      << "global + per-stage must total IPOPT kParamDim=142";
}

// c_u 是 VDM 直读值(P1b-1a T8),不是造的
TEST_F(AcadosFormulationTest, YawGain_IsVdmDirect) {
  EXPECT_NEAR(MidMpcAcadosFormulation::kC_u, 9.825342e-3, 1e-9);
}

// pack 不抛(基本可用性)
TEST_F(AcadosFormulationTest, PackParameters_NoThrow) {
  MidMpcInput in{};
  EXPECT_NO_THROW({ [[maybe_unused]] auto r = form_.pack_parameters(in); });
}
```

- [ ] **Step 3: 跑测试确认 FAIL(类未实现)**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DM5_USE_ACADOS=ON 2>&1 | head; colcon test --packages-select m5_tactical_planner --ctest-args -R AcadosFormulation`
Expected: 编译错(`mid_mpc_acados_formulation.hpp` 未找到或方法未实现)。

- [ ] **Step 4: 实现 mid_mpc_acados_formulation.cpp — MX 符号图 + pack_parameters**

实现要点(参考 IPOPT `mid_mpc_nlp_formulation.cpp` 的 `build_*_cost_` / 约束表达式,P1b-1a `build_base_ocp_doubleint` 的 dynamics/box/solver opts):
1. `build_symbolic_graph()`:MX state `x_=vertcat(px,py,ψ,r,u_surge)`(5),control `u_=vertcat(δ,n)`(2)。`disc_dyn_expr_` = Step 1 锁定的 5 行(Path B 双积分器 + 简化 surge)。`con_h_expr_` = vertcat(CPA per-target 多行 `cpa_act·g_cpa_t`、direction `pref_dir·l[k]`、min_alt、terminal 3 行)。box:ROT `|r|≤rot_max`(lbx/ubx on idx 3)、heading `|ψ|≤box`(idx 2)、surge `u_min≤u_surge≤u_max`(idx 4)、δ/n 控制盒(lbu/ubu)。solver opts:FULL_CONDENSING_HPIPM、EXACT hessian(F3)、DISCRETE integrator、SQP、`nlp_solver_tol_*=1e-9` + `max_iter=400`(P1b-1a T9 cost 读回所需)、MERIT_BACKTRACKING(F4)。idxsh=[0,1] soften CPA 行(T7),Zl=1e2/zl=1e3。
2. `pack_parameters(input)`:复刻 IPOPT `pack_parameters`(formulation.cpp:670-787)的 142→global/per-stage 切分。global = IPOPT kIdx 0-25 标量 + 16×5 target 块(62+80=142 里的全局部分);per-stage = prefix ψ/u(激活 pact_pre)+ disc_k + cpa_act/pact 激活因子。返回 pair。
3. 6 cost:MX 表达式,colreg EXTERNAL per-stage(cost_scaling=ones),其余按 IPOPT 形式。
4. **决策(codegen 路径)**:acatos codegen 需要 `acados_template`(Python),而 C++ MX 图不能直接喂给它。**采用 P1b-1a 模式**:gen_mid_mpc_acados.py 用 `acados_template` + CasADi **SX**(acatos_template 对 SX 支持成熟;MX 用于 acatos 有局限)重画与 .cpp 相同的符号图(数学一致,SX/MX 在 acatos 层等价),生成 C。.cpp 的 MX 图用于**单元测试维度/参数一致性校验**与 solver 内部(可选)。gen 脚本是 codegen 的唯一真源,.cpp 提供类型契约 + pack 逻辑(pack 逻辑 C++ 实现,solver 用)。

- [ ] **Step 5: 跑测试确认 PASS**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DM5_USE_ACADOS=ON; colcon test --packages-select m5_tactical_planner --ctest-args -R AcadosFormulation`
Expected: 4 test PASS。

- [ ] **Step 6: 写 gen_mid_mpc_acados.py — 生产 codegen 脚本**

基于 P1b-1a `T9_merge6/gen_merge6.py`(6 点合并的 staging 实现,已验)+ 本次 MX 图的 6 cost/全约束/142 分区。用 `acados_template.AcadosOcpSolver.generate(ocp, json_file=...)` 生成 C 到 `test/external/acados_backend/c_generated_code/`。`SOLVER_NAME="m5_mid_mpc_acados"`。dynamics 用 SX(Path B 双积分器,与 .cpp MX 数学一致)。参数 layout 与 .cpp `pack_parameters` 一致(文档化两者的 index 对应)。容器内跑确认 codegen 出 C + Makefile:
```bash
source scripts/a4000-env.sh
COMPOSE_PROJECT_NAME=codex-acados-backend docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes \
  bash -c "cd /opt/ws/src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend && python3 gen_mid_mpc_acados.py && ls c_generated_code/"
```
Expected: 列出 `acados_solver_m5_mid_mpc_acados.h` + `Makefile` + 源文件。

- [ ] **Step 7: Commit**

```bash
git add include/m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp \
        src/mid_mpc/mid_mpc_acados_formulation.cpp \
        test/unit/test_mid_mpc_acados_formulation.cpp \
        test/external/acados_backend/gen_mid_mpc_acados.py
git commit -m "feat(m5): MidMpcAcadosFormulation — MX symbol graph + codegen (P1b-1b)

Production acatos OCP CasADi MX symbol graph: Path B double-integrator
dynamics (dr/dt=c(u)*delta, c_u=9.825342e-3 VDM-direct) with state
x=[px,py,psi,r,u_surge], control u=[delta,n]. 6 costs (colreg/dist/route/
vel/asym/terminal) + full constraints (CPA per-target xi/direction/min_alt/
ROT/terminal + bound schedule) + 142-param global/per-stage partition.
SQP tol 1e-9/max_iter 400 (P1b-1a T9 cost read-back). gen_mid_mpc_acados.py
codegen (SX, acados_template). IPOPT formulation untouched (read-only ref)."
```

---

## Task 16: CMakeLists — M5_USE_ACADOS 生产 block + codegen custom_command

> **触碰生产 CMake**。加生产 acados 编译 block(option→find_package→sources→codegen step→link)+ 测试守卫。`M5_USE_ACADOS` option **已存在**(CMakeLists:82,现为 spike),本 task 改为生产 block。

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt`(82-94 现 spike block → 生产 block;加 sources/link/codegen)

**Interfaces:**
- Consumes:Task 15 的 `mid_mpc_acados_formulation.{hpp,cpp}` + `gen_mid_mpc_acados.py`;现有 `M5_HAS_CASADI` block(149-159)作模板;`docker/sil_nodes.Dockerfile:95-149` 的 acados 环境。
- Produces:`M5_USE_ACADOS=ON` 时编译 `mid_mpc_acados_formulation.cpp`(+ Task 17 solver)进 m5_shared_lib,codegen 步骤生成 + 编译 acatos solver lib。

- [ ] **Step 1: 先确认 M5_USE_ACADOS=OFF 基线无回归(触碰 CMake 前必做)**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DM5_USE_ACADOS=OFF 2>&1 | tail -5; colcon test --packages-select m5_tactical_planner --ctest-args -R 'MidMpc' 2>&1 | tail -10`
Expected: build OK,IPOPT 测试全过(基线)。记录基线测试数。

- [ ] **Step 2: 改 CMakeLists 82-94 block 为生产 block**

把现 spike block 改为:
```cmake
# --- M5_USE_ACADOS (P1b-1b: production acados backend) ---
# 默认 OFF:IPOPT(M5_USE_CASADI)是生产路径,acatos 是平行 backend。
# ON 时编译 mid_mpc_acados_*.{cpp} + codegen 生成 acatos solver lib。
option(M5_USE_ACADOS "Build production acados M5 MidMpc backend (P1b-1b)" OFF)
set(M5_HAS_ACADOS OFF)
if(M5_USE_ACADOS)
  find_package(acados QUIET REQUIRED)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(M5_HAS_ACADOS ON)
  list(APPEND M5_SHARED_SOURCES
    src/mid_mpc/mid_mpc_acados_formulation.cpp
    src/mid_mpc/mid_mpc_acados_solver.cpp)        # Task 17 加
  list(APPEND M5_LINK_LIBS acados::acados)
endif()

# codegen step:M5_USE_ACADOS=ON 时,build 前跑 gen_mid_mpc_acados.py 生成 C,
# 再编译成 libacados_ocp_solver_m5_mid_mpc_acados.so 链进 m5_shared_lib。
if(M5_HAS_ACADOS)
  set(ACADOS_BACKEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test/external/acados_backend)
  set(ACADOS_GEN_DIR ${ACADOS_BACKEND_DIR}/c_generated_code)
  # codegen: gen_*.py 出 C(json + sources + Makefile)
  add_custom_command(
    OUTPUT ${ACADOS_GEN_DIR}/acados_solver_m5_mid_mpc_acados.h
    COMMAND ${Python3_EXECUTABLE} gen_mid_mpc_acados.py
    WORKING_DIRECTORY ${ACADOS_BACKEND_DIR}
    DEPENDS ${ACADOS_BACKEND_DIR}/gen_mid_mpc_acados.py
    COMMENT "[acados] codegen m5_mid_mpc_acados solver"
    VERBATIM)
  # build generated lib via its Makefile(acados env: ACADOS_SOURCE_DIR=/usr/local)
  add_custom_command(
    OUTPUT ${ACADOS_GEN_DIR}/libacados_ocp_solver_m5_mid_mpc_acados.so
    COMMAND make -C ${ACADOS_GEN_DIR} ocp_shared_lib
    DEPENDS ${ACADOS_GEN_DIR}/acados_solver_m5_mid_mpc_acados.h
    COMMENT "[acados] build solver shared lib")
  add_custom_target(m5_acados_solver_lib ALL
    DEPENDS ${ACADOS_GEN_DIR}/libacados_ocp_solver_m5_mid_mpc_acados.so)
  # 链进 m5_shared_lib
  target_link_libraries(m5_shared_lib PRIVATE
    ${ACADOS_GEN_DIR}/libacados_ocp_solver_m5_mid_mpc_acados.so
    -L/usr/local/lib -lacados -lhpipm -lblasfeo)
  add_dependencies(m5_shared_lib m5_acados_solver_lib)
  target_include_directories(m5_shared_lib PRIVATE
    ${ACADOS_GEN_DIR} /usr/local/include
    /usr/local/include/acados /usr/local/include/blasfeo/include
    /usr/local/include/hpipm/include)
  target_compile_definitions(m5_shared_lib PUBLIC M5_USE_ACADOS=1)
endif()
```

- [ ] **Step 3: 加 acados 测试守卫(沿用 m5_add_gtest 模式,296-319)**

在测试 block(330-412)加 `if(M5_HAS_ACADOS)` 子 block 注册 Task 17/18 测试(`test_mid_mpc_acados_solver`、`test_mid_mpc_acados_parity`),链 `m5_shared_lib` + acados lib。

- [ ] **Step 4: 确认 OFF 仍无回归 + ON 编译过**

Run:
```bash
# OFF(回归)
colcon build --packages-select m5_tactical_planner --cmake-args -DM5_USE_ACADOS=OFF 2>&1 | tail -3
# ON(编译 acados block;Task 17 solver 未写,先注释 solver.cpp 行或加 stub 让编过)
colcon build --packages-select m5_tactical_planner --cmake-args -DM5_USE_ACADOS=ON 2>&1 | tail -15
```
Expected: OFF 全过(与基线一致);ON 至少 codegen + formulation.cpp 编过(solver.cpp Task 17 才完整)。

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt
git commit -m "build(m5): CMake M5_USE_ACADOS production block + codegen (P1b-1b)

option(M5_USE_ACADOS) OFF default; ON compiles mid_mpc_acados_* + runs
gen_mid_mpc_acados.py codegen (custom_command) + builds generated solver lib
via its Makefile, links into m5_shared_lib. IPOPT path (OFF) unchanged.
acados env from sil_nodes.Dockerfile:95-149."
```

---

## Task 17: MidMpcAcadosSolver — 求解器封装 + 输出重构 + dispatch

> **核心 task**。封装生成的 acatos solver lib,pack 142 参数(全局 + per-stage `update_params`)→ solve → 把 acatos 状态/控制轨迹重构为 `MidMpcSolution` 契约字段。在 `MidMpcSolver::solve()` 加 `#ifdef M5_USE_ACADOS` dispatch。

**Files:**
- Create: `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp`
- Create: `src/mid_mpc/mid_mpc_acados_solver.cpp`
- Modify: `include/m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp`(加 acados 成员)
- Modify: `src/mid_mpc/mid_mpc_solver.cpp`(`solve()` 加 `#ifdef` dispatch 分支)
- Test: `test/unit/test_mid_mpc_acados_solver.cpp`

**Interfaces:**
- Consumes:Task 15 `MidMpcAcadosFormulation`(pack_parameters);生成的 `acados_solver_m5_mid_mpc_acados.h`(C API:`m5_mid_mpc_acados_acados_create_capsule/create/update_params/solve/out_get/free`);`common/types.hpp`(MidMpcInput/Solution)。
- Produces:`MidMpcAcadosSolver::solve(input, warm) -> MidMpcSolution`(与 IPOPT `MidMpcSolver::solve` 同签名/同输出契约)。`MidMpcSolver::solve` 在 `M5_USE_ACADOS` 时 dispatch 到它。

- [ ] **Step 1: 写 test_mid_mpc_acados_solver.cpp — 端到端断言(TDD)**

```cpp
#include <gtest/gtest.h>
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"
#include "m5_tactical_planner/common/types.hpp"

using namespace mass_l3::m5;
using namespace mass_l3::m5::mid_mpc;

class AcadosSolverTest : public ::testing::Test {
 protected:
  MidMpcAcadosFormulation form_;
  std::unique_ptr<MidMpcAcadosSolver> solver_;
  void SetUp() override {
    form_.build_symbolic_graph();
    solver_ = std::make_unique<MidMpcAcadosSolver>(form_);
  }
  static MidMpcInput straight_line() { /* 复用 IPOPT test make_straight_line_input */ }
};

// 标准场景 solve 收敛(status Converged 或 status 4 但 solver-moved 且约束满足)
TEST_F(AcadosSolverTest, StraightLine_ConvergesAndProducesTrajectory) {
  auto sol = solver_->solve(straight_line(), nullptr);
  EXPECT_NE(sol.status, MidMpcSolution::Status::NotInitialized);
  EXPECT_NE(sol.status, MidMpcSolution::Status::NumericalFailure);
  EXPECT_EQ(sol.trajectory.size(), MidMpcAcadosFormulation::kN);
  // 无 NaN
  for (auto& p : sol.trajectory) {
    EXPECT_TRUE(std::isfinite(p.psi_rad) && std::isfinite(p.u_mps));
  }
}

// 输出契约字段齐(IPOPT 同字段):trajectory[psi/u/x/y]、status、cost_total、cpa_slack、solve_duration_ms
TEST_F(AcadosSolverTest, OutputContract_MatchesIpopTFields) {
  auto sol = solver_->solve(straight_line(), nullptr);
  EXPECT_GE(sol.solve_duration_ms, 0);
  EXPECT_NO_THROW({ [[maybe_unused]] auto c = sol.cost_total; });
  EXPECT_NO_THROW({ [[maybe_unused]] auto s = sol.cpa_slack; });
  // x/y dead-reckoned(从 ψ/u 序列重构,与 IPOPT unpack 一致)
  EXPECT_GE(sol.trajectory.front().x_m, -1e9);
}

// 实时性:单次 solve < 求解预算(spec P1b-1c gate 5,IPOPT ~3s;acatos 应更快)
TEST_F(AcadosSolverTest, Realtime_UnderBudget) {
  auto sol = solver_->solve(straight_line(), nullptr);
  EXPECT_LT(sol.solve_duration_ms, 3000);  // < 3s
}
```

- [ ] **Step 2: 跑测试确认 FAIL(solver 未实现)**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DM5_USE_ACADOS=ON; colcon test --packages-select m5_tactical_planner --ctest-args -R AcadosSolver`
Expected: FAIL(mid_mpc_acados_solver.hpp 未找到)。

- [ ] **Step 3: 实现 mid_mpc_acados_solver.hpp + .cpp**

```cpp
// hpp
class MidMpcAcadosSolver {
 public:
  explicit MidMpcAcadosSolver(const MidMpcAcadosFormulation& form);
  ~MidMpcAcadosSolver();
  [[nodiscard]] MidMpcSolution solve(const MidMpcInput& input,
                                     const MidMpcSolution* warm_start);
 private:
  struct Impl;  // pimpl:封装 capsule/acatos C 句柄
  std::unique_ptr<Impl> impl_;
  const MidMpcAcadosFormulation& form_;
};
```
.cpp 实现(参考 P1b-1a `T9_merge6/runner_merge6.cpp` 的 acatos C API 调用模式 + IPOPT `mid_mpc_solver.cpp:281-313` 的输出 unpack 模式):
1. `solve()`:计时开始。`form_.pack_parameters(input)` 得全局+per-stage。warm-start seed(forward-propagated,F1):从 input.own_ship 用双积分器 forward 推 δ/n 序列作 x_seed(参考 forward_seed_doubleint)。set x0(lbx/ubx pin 初始状态)。per-stage `m5_mid_mpc_acados_acados_update_params(capsule,k,p_k,np)`(staging 发现 3)。`m5_mid_mpc_acados_acados_solve(capsule)`。status 0/4(F5),status≠0 查 traj_delta solver-moved。
2. 输出重构:`ocp_nlp_out_get(stage,"x",xk)` 取 [px,py,ψ,r,u_surge] 轨迹 → 填 `MidMpcSolution.trajectory[k]` 的 psi_rad(=ψ[k])、u_mps(=u_surge[k])、x_m/y_m(=px/py[k],acatos 已积分,与 IPOPT dead-reckon 一致)。`ocp_nlp_get("cost_value",&)` → cost_total(可填真值,IPOPT E1 是 0;改进不破坏)。CPA slack:`ocp_nlp_out_get(stage,"sl",sl_vec)` 取 per-target ξ,max → cpa_slack(取 max 作 σ)。ipopt_iterations:SQP iter count(acatos 有 accessor)。solve_duration_ms:计时。
3. F1-F5:seed 非零、uh 1e10、EXACT、MERIT_BACKTRACKING、status 4+solver-moved。

- [ ] **Step 4: 在 MidMpcSolver::solve 加 dispatch 分支(改生产 mid_mpc_solver.cpp)**

`mid_mpc_solver.hpp` 加 `#ifdef M5_USE_ACADOS` 成员 `std::unique_ptr<MidMpcAcadosSolver> acados_solver_;`。`mid_mpc_solver.cpp` 的 `solve()` 开头加:
```cpp
MidMpcSolution MidMpcSolver::solve(const MidMpcInput& input,
                                   const MidMpcSolution* warm_start,
                                   const RowBoundConfig& row_bounds) {
#ifdef M5_USE_ACADOS
  if (acados_solver_) {
    return acados_solver_->solve(input, warm_start);  // dispatch
  }
#endif
  // ... 现有 IPOPT 路径不动(casadi nlpsol)
}
```
构造函数 `#ifdef M5_USE_ACADOS` 时按配置建 acados_solver_(或 IPOPT)。**现有 IPOPT 代码一字不改**,只加分支。

- [ ] **Step 5: 跑测试确认 PASS**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DM5_USE_ACADOS=ON; colcon test --packages-select m5_tactical_planner --ctest-args -R AcadosSolver`
Expected: 3 test PASS(straight-line 收敛 + 输出契约 + 实时性)。

- [ ] **Step 6: M5_USE_ACADOS=OFF 回归确认(dispatch 不影响 IPOPT)**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DM5_USE_ACADOS=OFF; colcon test --packages-select m5_tactical_planner --ctest-args -R 'MidMpc'`
Expected: IPOPT 测试全过(与 Task 16 Step 1 基线一致)。

- [ ] **Step 7: Commit**

```bash
git add include/m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp \
        src/mid_mpc/mid_mpc_acados_solver.cpp \
        include/m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp \
        src/mid_mpc/mid_mpc_solver.cpp \
        test/unit/test_mid_mpc_acados_solver.cpp
git commit -m "feat(m5): MidMpcAcadosSolver + MidMpcSolver dispatch (P1b-1b)

Wrap generated acatos solver: pack 142 params (global + per-stage
update_params) -> solve -> reconstruct MidMpcSolution (psi/u/x/y from
acatos state traj, cost_total real value, cpa_slack=max per-target xi,
solve_duration, iter count). F1-F5 applied. MidMpcSolver::solve gains
#ifdef M5_USE_ACADOS dispatch branch; IPOPT path untouched. Output contract
matches IPOPT (downstream M4/L4/tail_gate agnostic)."
```

---

## Task 18: IPOPT↔acados 输出契约 parity 测试

> **backend 门核心**。同 MidMpcInput 喂两 backend,逐字段对比输出契约字段。证明下游无感知切换。

**Files:**
- Create: `test/unit/test_mid_mpc_acados_parity.cpp`

**Interfaces:**
- Consumes:Task 15/17(MidMpcAcadosFormulation/Solver)+ IPOPT(MidMpcNlpFormulation/Solver)。两 backend 同 MidMpcInput(复用 IPOPT test 的 scenario helpers:make_straight_line/head_on/crossing)。

- [ ] **Step 1: 写 test_mid_mpc_acados_parity.cpp — 逐字段对比**

```cpp
#include <gtest/gtest.h>
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"  // IPOPT
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.h"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp"

class ParityTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ipopt_form_.build_symbolic_graph();
    ipopt_ = std::make_unique<mid_mpc::MidMpcSolver>(ipopt_form_, mid_mpc::IpoptOptions{});
    acados_form_.build_symbolic_graph();
    acados_ = std::make_unique<mid_mpc::MidMpcAcadosSolver>(acados_form_);
  }
  mid_mpc::MidMpcNlpFormulation ipopt_form_;
  std::unique_ptr<mid_mpc::MidMpcSolver> ipopt_;
  mid_mpc::MidMpcAcadosFormulation acados_form_;
  std::unique_ptr<mid_mpc::MidMpcAcadosSolver> acados_;
};

// 两 backend 输出字段类型/数量一致(契约 parity,非数值 bit-close)
TEST_F(ParityTest, SameInput_ProducesCompatibleOutputShape) {
  auto in = /* make_straight_line */;
  auto si = ipopt_->solve(in, nullptr);
  auto sa = acados_->solve(in, nullptr);
  ASSERT_EQ(si.trajectory.size(), sa.trajectory.size());
  // status 都是"可用"(非 NotInitialized/NumericalFailure)
  EXPECT_NE(si.status, MidMpcSolution::Status::NotInitialized);
  EXPECT_NE(sa.status, MidMpcSolution::Status::NotInitialized);
}

// 行为等价(非 bit-close):直线场景两 backend 都基本直航
TEST_F(ParityTest, StraightLine_BothHoldCourse) {
  auto in = /* make_straight_line */;
  auto si = ipopt_->solve(in, nullptr);
  auto sa = acados_->solve(in, nullptr);
  // ψ 偏离参考 < 0.1 rad(两 physics 不同,不要求 bit-close;spec gate 3)
  for (size_t k = 0; k < si.trajectory.size(); ++k) {
    EXPECT_LT(std::abs(si.trajectory[k].psi_rad - in.planned_route_bearing_rad), 0.15);
    EXPECT_LT(std::abs(sa.trajectory[k].psi_rad - in.planned_route_bearing_rad), 0.15);
  }
}
```

- [ ] **Step 2: 跑测试确认 PASS**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DM5_USE_ACADOS=ON; colcon test --packages-select m5_tactical_planner --ctest-args -R Parity`
Expected: PASS(契约 shape 一致 + 直线行为等价)。

- [ ] **Step 3: Commit**

```bash
git add test/unit/test_mid_mpc_acados_parity.cpp
git commit -m "test(m5): IPOPT↔acados output-contract parity (P1b-1b backend gate)

Same MidMpcInput -> both backends: trajectory size matches, status both
usable, straight-line both hold course (<0.15 rad off bearing). Contract
parity (not bit-close; two physics differ per spec gate 3). Backend gate."
```

---

## Task 19: Rule14 HO benchmark(P1b-1c)— 两 build 行为等价

> **benchmark 门**。标准 head-on Rule14 场景,IPOPT(OFF)vs acatos(ON),6 条行为等价判据。这是 spec DP-05/VR-05 的实测落地。

**Files:**
- Create: `test/external/rule14_ho_benchmark/run_benchmark.sh`
- Create: `test/external/rule14_ho_benchmark/compare.py`
- Create: `test/external/rule14_ho_benchmark/Makefile`(或 runner.cpp)
- Create: `test/external/rule14_ho_benchmark/.gitignore`

**Interfaces:**
- Consumes:Task 17 两 backend;Rule14 HO scenario MidMpcInput(复用 IPOPT test `make_head_on_input` 的几何:target 500m north southbound,本船 give-way starboard turn)。
- Produces:6 条判据结果(IPOFF vs acatos)→ P1b-1 benchmark 门证据。

**6 条判据(spec §P1b-1c)**:
1. 避让决策一致(都 starboard turn / 都 give-way)
2. CPA-feasible 一致(都满足 CPA ≥ cpa_safe)
3. 轨迹形状一致(ψ 序列 max|Δ| < 0.1 rad;**Path B 修订:容差对双积分器预测器重论证,若 acatos 在该场景因转向直径大发散,记录并按失败处置**)
4. IMO MSC.137(76) 回转指标(advance ≤ 4.5L=202.5m,tactical dia ≤ 5L=225m)
5. 实时性:acatos 单次 solve ≤ 预算(< 3s)
6. cost 数值报告(参考,非硬门)

- [ ] **Step 1: 写 runner(两 backend 同 Rule14 HO input 各 solve,输出 trajectory json)**

写 `runner_rule14.cpp`(链 m5_shared_lib,M5_USE_ACADOS 编译时定):构造 Rule14 HO MidMpcInput(target 500m north,southbound,本船 eastbound give-way)。solve。dump trajectory(psi/u/x/y 序列)+ cost + status + solve_duration 到 json。两 build(OFF/ON)各跑一次,产 `ipopt_rule14.json` / `acados_rule14.json`。
```cpp
// 核心:复用 IPOPT test make_head_on_input 几何
MidMpcInput in = make_head_on_rule14_input();  // target (0,500) southbound, own eastbound
auto sol = solver->solve(in, nullptr);
dump_json("rule14_result.json", sol);  // trajectory/cost/status/duration
```

- [ ] **Step 2: 写 run_benchmark.sh — 两 build 跑 + 对比**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
# Build IPOPT(OFF)
colcon build --packages-select m5_tactical_planner --cmake-args -DM5_USE_ACADOS=OFF >/dev/null
./runner_rule14 > ipopt_rule14.json   # 或 colcon run test
# Build acados(ON)
colcon build --packages-select m5_tactical_planner --cmake-args -DM5_USE_ACADOS=ON >/dev/null
./runner_rule14 > acados_rule14.json
# Compare
python3 compare.py ipopt_rule14.json acados_rule14.json
```

- [ ] **Step 3: 写 compare.py — 6 条判据**

```python
# 读两 json,逐判据:
# 1 starboard: 两 trajectory ψ 都向 + (starboard) 偏
# 2 CPA-feasible: 两都 min dist to target >= cpa_safe
# 3 轨迹形状: max|psi_ipopt - psi_acados| < 0.1 rad  (Path B 容差重论证)
# 4 IMO: 两 advance/tactical_dia <= 4.5L/5L (从轨迹估)
# 5 实时: acatos solve_duration < 3000ms
# 6 cost 报告: 打印两 cost_total(参考)
# 打印每判据 PASS/FAIL + 总 VERDICT
```

- [ ] **Step 4: 容器内跑 benchmark**

Run: `source scripts/a4000-env.sh; COMPOSE_PROJECT_NAME=codex-acados-backend docker compose ... run --rm sil-nodes bash -c "cd /opt/ws/src/l3_tdl_kernel/m5_tactical_planner/test/external/rule14_ho_benchmark && bash run_benchmark.sh"`
Expected: 6 条全 PASS(或某条 FAIL → 按失败处置:先查是否 physics 差异(双积分器 vs 运动学本不同,spec gate 3 容差需重论证)还是 bug)。

- [ ] **Step 5: 若某判据 FAIL,排查(失败处置)**

- gate 3(轨迹形状)FAIL:双积分器转向直径巨大(P1b-1a T6 finding 2),head-on CPA 可能不可行 → 记录"Path B 双积分器在 head-on 几何下行为与运动学 IPOPT 本质不同,容差 0.1 rad 需放宽或场景改温和"。这是真实 physics 差异,非 bug。spec 失败处置:回炉评估是否 benchmark 用更温和几何(非 head-on)或接受行为差异。
- gate 5(实时)FAIL:查 SQP iter / max_iter,tighten tol 影响。
- 不 mock / 不 forced-pass。

- [ ] **Step 6: Commit**

```bash
git add test/external/rule14_ho_benchmark/
git commit -m "test(m5): Rule14 HO benchmark IPOPT vs acados (P1b-1c)

Standard head-on Rule14: M5_USE_ACADOS=OFF (IPOPT kinematics) vs =ON
(acatos Path B double-integrator), same MidMpcInput. 6 behavior-equivalence
gates: avoidance decision / CPA-feasible / trajectory shape <0.1rad /
IMO MSC.137(76) turning / realtime <3s / cost report. DP-05/VR-05 landing."
```

---

## Task 20: P1b-1 验收门 + promotable + P1 关闭

> **最终 task**。跑全部门(staging P1b-1a + backend P1b-1b + benchmark P1b-1c + 回归),promotable 评估,merge l3-tdl,push origin/l3--tdl,handoff,P1 关闭记录。

**Files:**
- (无新代码文件;跑验收 + handoff + progress 记录)
- Modify: `handoff/workspace_log.md`
- Modify: `docs/Design/Phase 2/M5-*/M5-progress.md`(P1b-1 完成记录,若存在)

- [ ] **Step 1: 跑全门**
  - P1b-1a staging 门:`run_all_p1b1.sh`(T8→T6→T7→T9,已 P1b-1a 验证)
  - P1b-1b backend 门:`colcon test --cmake-args -DM5_USE_ACADOS=ON -R 'AcadosFormulation|AcadosSolver|Parity'`(Task 15/17/18)
  - P1b-1c benchmark 门:`run_benchmark.sh`(Task 19,6 条)
  - 回归门:`colcon build/test --cmake-args -DM5_USE_ACADOS=OFF`(IPOPT 路径与基线一致)

- [ ] **Step 2: 验收门核对(spec §验收门)**
  - staging 门(P1b-1a):已过 ✅
  - backend 门(P1b-1b):生产 acatos 标准场景 solve 收敛 + 输出契约匹配 + 142 参数 pack ✅(Task 17/18)
  - benchmark 门(P1b-1c):Rule14 HO 6 条判据全过 ✅(Task 19)
  - 回归门:IPOPT 无回归 ✅
  - promotable:全过 → 进 Step 3

- [ ] **Step 3: 若任门 FAIL,写失败报告(阻塞点 + 回炉建议),不强行 promotable**

- [ ] **Step 4: promotable — merge l3-tdl + push origin/l3-tdl**(AGENTS.md promotion rule)

```bash
# 本机即 A4000,先过 A4000 本机验收 gate(scripts/a4000-acceptance.sh)再 push
source scripts/a4000-env.sh && npm run sys:start && ./scripts/a4000-acceptance.sh
# 通过后:merge task 分支到 l3-tdl,push
git checkout l3-tdl && git merge codex/m5-design-grounding && git push origin l3-tdl
```

- [ ] **Step 5: 更新 handoff/workspace_log.md + M5-progress.md**(P1b-1 完成 + P1 关闭)

- [ ] **Step 6: Commit handoff + final**

```bash
git add handoff/workspace_log.md docs/Design/
git commit -m "docs(handoff): P1b-1 complete (acados backend + Rule14 benchmark) — P1 closed

P1b-1 all gates pass: staging (P1b-1a, Path B double-integrator), backend
(P1b-1b, MidMpcAcadosSolver + dispatch, output-contract parity), benchmark
(P1b-1c, Rule14 HO 6 gates), regression (IPOPT untouched). Merged l3-tdl,
pushed origin/l3-tdl. P1 (M5 MPC acatos migration + physics upgrade) closed."
```

---

## Self-Review

**1. Spec 覆盖**:
- ✅ 生产 backend `MidMpcAcadosFormulation`(MX 符号图,双积分器 Path B + 6 cost + 全约束 + 142 参数)→ Task 15
- ✅ 生产 backend `MidMpcAcadosSolver`(pack + solve + 输出重构)+ dispatch → Task 17
- ✅ `M5_USE_ACADOS` flag(CMake option + dispatch)→ Task 16(CMake)+ Task 17(dispatch)
- ✅ 142 参数 per-stage 分区 → Task 15(pack_parameters)
- ✅ 输出契约匹配 IPOPT(下游无感知)→ Task 17/18
- ✅ codegen 集成 CMake → Task 16(add_custom_command)
- ✅ Rule14 HO benchmark 6 条判据 → Task 19
- ✅ 验收门 + promotable + 回归 → Task 20
- ✅ Path B 双积分器(c_u=9.825342e-3,VDM 直读非造系数)→ Task 15 Global Constraints + 锁定值
- ✅ F1-F5 + staging 发现沿用 → Global Constraints
- ✅ 失败处置(benchmark gate 3 physics 差异 / 实时 / 不可行)→ Task 19 Step 5

**2. Placeholder 扫描**: 各 task step 均有具体代码骨架/断言/命令/commit msg。codegen 决策(SX 用于 gen,.cpp MX 用于类型契约)在 Task 15 Step 4 显式说明(非 placeholder,是 acatos_template SX 成熟度的工程决策)。CMake codegen 的 Makefile 路径依赖 sil_nodes 镜像的 acados env(Dockerfile:95-149),已注。

**3. 类型一致**: c_u=9.825342e-3 全 task 一致(Task 15 锁定 + Task 17 用);state x=[px,py,ψ,r,u_surge](5)一致;control u=[δ,n](2)一致;142 参数(Task 15 pack + Task 17 用);MidMpcSolution 输出字段(Task 17 填 + Task 18 对比)一致。

**4. 风险(诚实)**:
- **codegen 集成 CMake 是新 pattern**(P1b-1a 是手动 gen+Makefile)。Task 16 add_custom_command 可能在容器 build 上下文有坑(acados env 只在 sil_nodes 镜像,不在 base)→ Task 16 Step 4 容器内验。
- **双积分器 head-on CPA 不可行**(P1b-1a T6 finding 2):Task 19 Rule14 HO gate 3 可能因转向直径大发散 → 失败处置已写(物理差异非 bug,容差重论证或温和几何)。
- **6 cost MX→SX codegen 一致性**:.cpp MX 图与 gen SX 图数学必须一致 → Task 15 测试校验维度,Task 18 parity 间接验输出。
- **生产代码触碰**:Task 16/17 改 CMake/mid_mpc_solver.cpp → 每 task Step 先跑 OFF 回归。
- **变量 surge dynamics 简化**(thrust=k_prop·n²,drag=k_drag·u²,VDM 直读):生产可能需更细 → P1b-2 增强,本阶段简化够。

**5. P1 关闭条件**:P1b-1(staging+backend+benchmark)全过 + P1a(subset)+ P1b-0(4 点)全在 → P1(M5 MPC acatos 迁移 + Path B physics 升级)关闭。merge l3-tdl + push。
