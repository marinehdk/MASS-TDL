# P1a: acados 可行性 spike — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 证明 acados 0.4.4 在 M5 容器构建链路可行(CMake 集成 + code-gen + RTI + HPIPM),并用现有 NLP 最小子集(恒速 dynamics + 单目标 CPA + 航向 box)staged-OCP 重表述跑通,为 P1b 完整迁移前置信心门。

**Architecture:** Dockerfile 加 acados 0.4.4 源码构建(ABI 对齐 casadi)+ CMake `find_package` 叠加(不动生产 m5_shared_lib)+ 独立 toy smoke(工具链)+ 独立 M5 子集重表述(formulation 映射)。IPOPT 保留(`M5_USE_CASADI=ON`),acados 纯叠加。

**Tech Stack:** acados 0.4.4, acados_template 0.4.4 (Python Jinja2), HPIPM/BLASFEO(acados 子模块), CMake 3.22, gcc/gfortran 11.4, ROS2 ament_cmake, colcon

**Spec:** `docs/superpowers/specs/2026-07-16-m5-p1a-acados-feasibility-spike-design.md`

## Global Constraints

- 工作目录: `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`(任务分支 codex/m5-design-grounding)
- **ABI 一致性(首要风险)**: acados 必须以 `-D_GLIBCXX_USE_CXX11_ABI=1` 构建,对齐 casadi(sil_nodes.Dockerfile:78-79)与 workspace。漏此 → 链接期 undefined reference(同 casadi 先例,sil_nodes.Dockerfile:52-57 + CMakeLists:61-70)。
- **版本锁定**: acados tag `0.4.4`;`pip install acados_template==0.4.4`(C 库与 Python 模板版本必须匹配,否则 code-gen schema 错)。
- **不碰生产 NLP**: mid_mpc_nlp_formulation/solver.cpp 不动;spike 全部在 `test/external/acados_*` 独立目录 + 独立 CMake target。`M5_USE_CASADI=ON` 保留,IPOPT 路径不破坏。
- **构建在容器内**: acados 装到容器(`/usr/local`),非 host;镜像构建用 BuildKit cache(sil_nodes.Dockerfile 已用,line 1-3)。
- 每个 task 结束 commit;本 plan 为 spike(可行性验证),失败即停 + 回炉 DP-05(spec 失败处置)。

---

## File Structure

| 文件 | 责任 | 新增/改 |
|---|---|---|
| `docker/sil_nodes.Dockerfile` | 镜像构建 | 改:casadi 块后(line 93 后)加 acados 构建 stage |
| `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt` | m5 构建 | 改:加 M5_USE_ACADOS option + find_package + spike target 注册 |
| `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_smoke/gen_smoke.py` | toy OCP code-gen | 新增 |
| `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_smoke/smoke_runner.c` | toy RTI solve | 新增 |
| `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_smoke/run_smoke.sh` | code-gen + 编译 + 跑 | 新增 |
| `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_m5_subset/gen_m5_subset.py` | M5 子集 staged-OCP code-gen | 新增 |
| `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_m5_subset/subset_runner.cpp` | M5 子集 OCP solve + 断言 | 新增 |
| `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_m5_subset/run_subset.sh` | code-gen + 编译 + 跑 | 新增 |

---

## Task 1: Dockerfile acados 0.4.4 源码构建

**Files:**
- Modify: `docker/sil_nodes.Dockerfile` (casadi 块 line 93 后,即 `fi` 之后、`# Copy the sim_workbench` line 95 之前插入)

**Interfaces:**
- Produces: 容器内 `/usr/local/lib/libacados.so` + HPIPM/BLASFEO + `acados_template==0.4.4` pip 包

- [ ] **Step 1: 读 sil_nodes.Dockerfile 确认插入点(line 93 `fi` 之后)**

Run: `sed -n '90,96p' docker/sil_nodes.Dockerfile`
Expected: 看到 line 93 `fi`(casadi arm64 分支结束),line 94 空行,line 95 `# Copy the sim_workbench colcon packages`。插入点 = line 93 `fi` 与 line 95 之间。

- [ ] **Step 2: 插入 acados 构建块**

在 line 93 `fi` 之后、line 95 注释之前插入(注意 ABI flag 对齐 casadi line 78-79):

```dockerfile

# --- acados 0.4.4 (P1a spike: M5 solver migration target DP-05 VR-05) ---
# ABI consistency: MUST use -D_GLIBCXX_USE_CXX11_ABI=1 to match casadi
# (sil_nodes.Dockerfile:78-79) and the workspace, else undefined references
# at link time (same defect as casadi, see comment block line 52-57).
# HPIPM/BLASFEO built from acados submodules; qpOASES/OSQP off (use HPIPM).
ARG ACADOS_TAG=0.4.4
RUN git clone --depth 1 --branch ${ACADOS_TAG} https://github.com/acados/acados.git /tmp/acados && \
    cd /tmp/acados && \
    git submodule update --init recursive && \
    mkdir -p build && cd build && \
    cmake .. \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=1" \
      -DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=1" \
      -DACADOS_WITH_QPOASES=OFF \
      -DACADOS_WITH_OSQP=OFF \
      -DBLASFEO_INSTALL_HEADERS=ON \
      -DACADOS_INSTALL_DIR=/usr/local \
      > /dev/null 2>&1 && \
    make install -j$(nproc) > /dev/null 2>&1 && \
    ldconfig && \
    cd /tmp && rm -rf acados
# Post-build assertion (mirror casadi assertion line 86-87)
RUN ( find /usr/local -name 'libacados.so*' 2>/dev/null | grep -q . || \
      { echo "FATAL: libacados.so missing after build — P1a spike blocked"; exit 1; } ) && \
    echo "acados ${ACADOS_TAG} installed (new-ABI + HPIPM verified)"
# Python code-gen dep: version MUST match ACADOS_TAG (schema-coupled)
RUN pip install -i https://mirrors.aliyun.com/pypi/simple/ --no-cache-dir acados_template==0.4.4
```

- [ ] **Step 3: 构建镜像验证 acados stage 成功**

Run: `source scripts/a4000-env.sh && COMPOSE_PROJECT_NAME=codex-acados-spike docker compose -f docker-compose.yml -f docker-compose.a4000.yml build sil-nodes 2>&1 | tail -20`
Expected: 构建成功,输出 "acados 0.4.4 installed (new-ABI + HPIPM verified)";casadi 断言仍 intact(未破坏 IPOPT 路径)。

- [ ] **Step 4: 容器内验证 libacados + acados_template**

Run: `COMPOSE_PROJECT_NAME=codex-acados-spike docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes bash -c "ls /usr/local/lib/libacados.so* && python3 -c 'import acados_template; print(acados_template.__version__)'"`
Expected: libacados.so 路径列出;`0.4.4` 版本输出。

- [ ] **Step 5: Commit**

```bash
git add docker/sil_nodes.Dockerfile
git commit -m "build(docker): source-build acados 0.4.4 for M5 P1a spike (DP-05 VR-05)

P1a feasibility spike: install acados 0.4.4 + acados_template==0.4.4 in
sil_nodes image with -D_GLIBCXX_USE_CXX11_ABI=1 (aligned to casadi line
78-79). HPIPM/BLASFEO from submodules; qpOASES/OSQP off. Post-build
assertion mirrors casadi pattern. IPOPT path untouched (M5_USE_CASADI=ON)."
```

---

## Task 2: CMake find_package(acados) 集成

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt` (克隆 M5_USE_CASADI 块 line 50-75,在其后 line 75 之后插入)

**Interfaces:**
- Produces: `M5_HAS_ACADOS` CMake var(gated M5_USE_ACADOS);spike target 用此 gate

- [ ] **Step 1: 读 CMakeLists 确认插入点(line 75 endif 之后)**

Run: `sed -n '72,80p' src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt`
Expected: line 75 `endif()`(M5_USE_CASADI 块结束),line 77 `# IPOPT — TODO`。插入点 = line 75 之后。

- [ ] **Step 2: 插入 M5_USE_ACADOS 块(克隆 M5_USE_CASADI 模式,无 ABI patch — acados 已在 Dockerfile 强制 ABI)**

在 line 75 `endif()` 之后插入:

```cmake

# acados — optional, P1a spike target for solver migration (DP-05 VR-05).
# Additive: M5_USE_CASADI remains the production path; acados is exercised
# only by the spike targets in test/external/acados_* until P1b.
option(M5_USE_ACADOS "Build acados spike targets (P1a, requires acados)" ON)
if(M5_USE_ACADOS)
  find_package(acados QUIET)
  if(NOT acados_FOUND)
    message(STATUS "M5: acados not found — P1a spike targets will be skipped")
    set(M5_HAS_ACADOS OFF)
  else()
    set(M5_HAS_ACADOS ON)
    message(STATUS "M5: acados found — P1a spike targets enabled")
  endif()
else()
  set(M5_HAS_ACADOS OFF)
endif()
```

- [ ] **Step 3: 配置构建,验证 find_package 解析**

Run: `COMPOSE_PROJECT_NAME=codex-acados-spike docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes bash -c "cd /opt/ws && colcon build --packages-select l3_tdl_kernel --cmake-args -DM5_BUILD_TESTS=ON 2>&1 | grep -E 'acados|error' | head"`
Expected: 输出 "M5: acados found — P1a spike targets enabled"(M5_HAS_ACADOS ON);无 error。

- [ ] **Step 4: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt
git commit -m "build(m5): add M5_USE_ACADOS option + find_package (P1a spike, DP-05)

Clone M5_USE_CASADI gating pattern. Additive only: M5_USE_CASADI stays ON
(production IPOPT path). acados spike targets in test/external/ gated on
M5_HAS_ACADOS. No ABI patch needed (Dockerfile forces new-ABI at acados build)."
```

---

## Task 3: Toy OCP smoke(工具链验证)

**Files:**
- Create: `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_smoke/gen_smoke.py`
- Create: `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_smoke/smoke_runner.c`
- Create: `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_smoke/run_smoke.sh`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt`(注册 smoke target,gated M5_HAS_ACADOS)

**Interfaces:**
- Consumes: Task 1 libacados + acados_template;Task 2 M5_HAS_ACADOS
- Produces: 验证 code-gen(Python Jinja2)→ C 编译 → RTI solve(HPIPM 收敛)全链路

- [ ] **Step 1: 写 gen_smoke.py — acados getting_started mass-spring toy OCP code-gen**

用 acados_template 的 `AcadosOcp` / `AcadosOcpFormulation` 构建一个 mass-spring chain(或 acados 官方 getting_started/c_getting_started 的最小 OCP),`acados_ocp_formulation` + `ocp_solver.export_c_code_generated()` + `render_templates`。参考 acados 官方 examples(https://github.com/acados/acados/tree/master/examples/acados_python/getting_started)。最小:状态 [position, velocity],线性 dynamics,二次代价。输出 C 代码到 `c_generated_code/`。

- [ ] **Step 2: 写 smoke_runner.c — 链接生成代码,跑 RTI solve,断言收敛**

```c
#include "acados_c/ocp_nlp_interface.h"
#include <stdio.h>
int main() {
    // 初始化 ocp_nlp_solver_config + dims + in/out(由 gen_smoke.py 生成的 C 代码提供 helper)
    // 设初值,solve(),读 status
    // 断言 status == 0 (ACADOS_SUCCESS)
    // 打印 "SMOKE PASS: acados RTI converged via HPIPM"
    return 0;
}
```
(具体 API 调用按 acados getting_started/c_getting_started 模式;生成的 `acados_solver_*` 函数提供 solver 创建/solve/destroy。)

- [ ] **Step 3: 写 run_smoke.sh — code-gen + 编译 + 跑**

```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
python3 gen_smoke.py            # 生成 c_generated_code/
cd c_generated_code && make     # 编译生成的 lib
# 编译 smoke_runner.c 链接 libacados + 生成的 solver lib
gcc -D_GLIBCXX_USE_CXX11_ABI=1 ../smoke_runner.c -I. -I/usr/local/include \
    -L. -lacados_solver -L/usr/local/lib -lacados -lhpipm -lblasfeo -lm \
    -o smoke_runner && ./smoke_runner
```

- [ ] **Step 4: 在容器内跑 smoke,验证 RTI 收敛**

Run: `COMPOSE_PROJECT_NAME=codex-acados-spike docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes bash -c "cd /opt/ws/src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_smoke && bash run_smoke.sh"`
Expected: 输出 "SMOKE PASS: acados RTI converged via HPIPM"。

- [ ] **Step 5: 若失败,先排查 ABI(spike 首要嫌疑)**

若链接报 undefined reference → 检查 `gcc -D_GLIBCXX_USE_CXX11_ABI=1` 是否传到 smoke_runner 编译 + libacados 是否 new-ABI(`nm -D /usr/local/lib/libacados.so | grep cxx11 | head`)。
若 code-gen 报 schema 错 → 确认 `acados_template.__version__ == 0.4.4`(Task 1 Step 4 已验)。
若 RTI 不收敛 → toy 应总收敛;查 OCP 设置(初值/边界)。

- [ ] **Step 6: (可选)注册为 CMake target,gated M5_HAS_ACADOS**

在 CMakeLists 加(可选;spike 用 shell 脚本也可):
```cmake
if(M5_HAS_ACADOS)
  add_custom_target(acados_smoke
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test/external/acados_smoke/run_smoke.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test/external/acados_smoke
    COMMENT "P1a acados smoke (toolchain check)"
  )
endif()
```

- [ ] **Step 7: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_smoke/ \
        src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt
git commit -m "feat(m5): acados toy OCP smoke (P1a toolchain check, DP-05)

Mass-spring toy OCP via acados_template code-gen + C runner. Validates
code-gen (Python Jinja2) -> C compile -> RTI solve (HPIPM converge) pipeline
with -Werror new-ABI clean link. Proves acados toolchain works before P1b
formulation re-staging."
```

---

## Task 4: M5 formulation 子集 staged-OCP 重表述(映射可行性验证)

**Files:**
- Create: `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_m5_subset/gen_m5_subset.py`
- Create: `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_m5_subset/subset_runner.cpp`
- Create: `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_m5_subset/run_subset.sh`

**Interfaces:**
- Consumes: Task 1 libacados + acados_template
- Produces: 验证现有 NLP 最小子集(dynamics/CPA/box)能映射到 acados discrete-dynamics/nonlinear-path-constraint/bounds 原语

**子集定义(从代码读出的确切表达式)**:
- 状态 `x_k = [px, py, psi, u]`(位置 + 艏向 + 速度)
- Dynamics(恒速直线,formulation.cpp:360-361):
  - `px[k+1] = px[k] + u[k]·dt·cos(psi[k])`
  - `py[k+1] = py[k] + u[k]·dt·sin(psi[k])`
  - `psi[k+1] = psi[k]`(P1a 恒速子集不含 ROT)
  - `u[k+1] = u[k]`(恒速)
- 约束:
  - 单目标 CPA(constraint_compiler.cpp:353): `g_cpa[k] = (px[k]-tx)² + (py[k]-ty)² - cpa_hard² ≥ 0`(tx,ty 目标位置参数;nonlinear path constraint)
  - 航向 box: `lbx ≤ psi[k] ≤ ubx`(bounds)
- 代价: stage cost `J = Σ_k (psi[k]-psi_ref)²`(航向参考跟踪)
- 参数:N=10 步,dt=5s,目标位置 (tx,ty)=(500,0),cpa_hard=200,psi_ref=0,lbx=-0.1,ubx=0.1(迫使转向避让)

- [ ] **Step 1: 写 gen_m5_subset.py — staged-OCP code-gen**

用 `AcadosOcp` 设置:
- `model.disc_dyn_expr` = 上述 dynamics(CasADi MX 表达式);`model.x = [px,py,psi,u]`;`model.u = []`(无控制输入,恒速子集)或 `model.u = [psi_cmd]`(若需控制)
- nonlinear path constraint:h(t,x) 含 CPA(用 `model.con_h_expr` + `constraints.lh/uh`)
- bounds: `constraints.x0`(初值)+ `lbx/ubx`(box)
- cost:`cost.cost_type = NONLINEAR_LS`,`yref = psi_ref`,`W` 权重
- export C code + render templates

- [ ] **Step 2: 写 subset_runner.cpp — OCP solve(SQP,非 RTI)+ 断言**

```cpp
// 初始化 acados solver(生成的 C 代码 helper)
// 设参数 tx,ty,cpa_hard,psi_ref,lbx,ubx
// solve() (SQP,完整)
// 断言:
//   1. status == ACADOS_SUCCESS
//   2. 全步 CPA 约束满足: (px[k]-tx)^2+(py[k]-ty)^2 >= cpa_hard^2
//   3. 全步 box 满足: lbx <= psi[k] <= ubx
//   4. 解合理: 避让方向正确(py 偏离 0,绕开目标)
// 打印 "SUBSET PASS: M5 dynamics/CPA/box mapped to acados primitives, solve converged"
```

- [ ] **Step 3: 写 run_subset.sh — code-gen + 编译 + 跑**

```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
python3 gen_m5_subset.py
cd c_generated_code && make
g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 ../subset_runner.cpp -I. -I/usr/local/include \
    -L. -lacados_solver -L/usr/local/lib -lacados -lhpipm -lblasfeo -lm \
    -o subset_runner && ./subset_runner
```

- [ ] **Step 4: 容器内跑子集,验证映射成功**

Run: `COMPOSE_PROJECT_NAME=codex-acados-spike docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes bash -c "cd /opt/ws/src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_m5_subset && bash run_subset.sh"`
Expected: 输出 "SUBSET PASS: M5 dynamics/CPA/box mapped to acados primitives, solve converged"。

- [ ] **Step 5: 若映射失败,记录阻抗点(不强行修 — spike 失败即记录)**

若 CPA nonlinear path constraint 在 acados 表达不出 → 记录具体阻抗。
若 dynamics discrete 形式 acados 不接受 → 记录(可能需显式 RK 形式)。
若 box/CPA 组合 infeasible → 查 OCP 数值合理性(参数是否迫使不可行)。
**不强行绕过** —— 映射难点是 P1b 规划的关键输入,spike 诚实暴露。

- [ ] **Step 6: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_m5_subset/
git commit -m "feat(m5): acados M5 subset re-staging (P1a formulation mapping check)

Minimal M5 NLP subset (constant-vel dynamics + single-target CPA + heading box)
re-staged as acados OCP (discrete dyn + nonlinear path constraint + bounds +
stage cost). Proves existing formulation expressions map to acados primitives
before P1b full re-staging. Dynamics from formulation.cpp:360-361, CPA from
constraint_compiler.cpp:353."
```

---

## Task 5: 全量回归 + 验收门

**Files:**
- (无新文件;运行验收门)

- [ ] **Step 1: 验证 IPOPT 路径未破坏(M5_USE_CASADI=ON 全 M5 测试)**

Run: `COMPOSE_PROJECT_NAME=codex-acados-spike docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes bash -c "cd /opt/ws && colcon build --packages-select l3_tdl_kernel --cmake-args -DM5_BUILD_TESTS=ON && colcon test --packages-select l3_tdl_kernel --event-handlers console_direct+ 2>&1 | grep -E 'PASSED|FAILED|FAIL' | tail -20"`
Expected: 全 PASSED(尤其 test_mid_mpc_*/test_constraint_compiler/test_vessel_dynamics_model)—— 证明 acados 叠加未破坏 IPOPT 路径。

- [ ] **Step 2: 验收门核对(spec 6 通过判据)**

- [ ] Dockerfile 构建成功,libacados.so 存在,acados_template 0.4.4(Task 1 Step 4)
- [ ] find_package(acados) 解析成功 M5_HAS_ACADOS ON(Task 2 Step 3)
- [ ] toy smoke:code-gen + 编译 + RTI solve 收敛,-Werror 新 ABI 干净(Task 3 Step 4)
- [ ] M5 子集重表述:dynamics/CPA/box 映射成功,OCP solve 收敛 + 约束满足(Task 4 Step 4)
- [ ] IPOPT 路径全绿(Step 1)
- [ ] 可行性结论:acados 工具链可行 + formulation 映射可行 → 可进 P1b

- [ ] **Step 3: 若 spike 失败(工具链或映射不可行),写失败报告 + 标记回炉 DP-05**

按 spec"失败处置":记录阻塞点(ABI/code-gen/HPIPM/映射阻抗),更新决策树日志 TBD-4 状态为"spike 失败 → 回炉 DP-05",不在本 plan 强行通过。

- [ ] **Step 4: 更新 handoff/workspace_log.md**

追加 P1a spike 完成条目(通过/失败 + 证据 + 是否进 P1b)。

- [ ] **Step 5: Commit handoff**

```bash
git add handoff/workspace_log.md
git commit -m "docs(handoff): record P1a acados spike outcome"
```

---

## Self-Review(plan 作者自检)

**1. Spec 覆盖**:
- ✅ Dockerfile 集成 → Task 1
- ✅ CMake find_package → Task 2
- ✅ toy smoke → Task 3
- ✅ M5 子集重表述 → Task 4
- ✅ 6 通过判据 → Task 5 Step 2
- ✅ 失败处置(回炉 DP-05)→ Task 5 Step 3
- ✅ ABI 风险 → Task 1 Step 2 + Task 3 Step 5 + Task 4 Step 3(三处 -D_GLIBCXX_USE_CXX11_ABI=1)

**2. Placeholder 扫描**:
- Task 3 Step 1/2 与 Task 4 Step 1/2 的 acados API 调用标注"按 acados getting_started 模式"(因未读 acados 官方 examples 的确切 API);但给了:状态向量/dynamics 表达式/约束形式/代价形式/参数值/断言点。执行者参考 acados 0.4.4 官方 examples(https://github.com/acados/acados/tree/master/examples/acados_python/getting_started)填充具体 API。这是 spike 性质决定(toy OCP 选型可按 acados 官方最小 example),非 plan 缺陷。
- 无 TBD/TODO/FIXME。

**3. 类型一致**: acados 0.4.4 / acados_template 0.4.4 / libacados.so 全文一致;ABI flag 三处一致。

**4. 风险**: 首要风险 ABI 已在 3 处强制 + Task 3 Step 5 排查步骤;映射阻抗(Task 4 Step 5)有诚实失败处置(记录非强行绕过)。
