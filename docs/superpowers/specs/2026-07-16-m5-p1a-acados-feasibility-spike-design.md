# P1a Spec: acados 可行性 spike(工具链 + 小型 formulation 重构验证)

> **产出**: brainstorming,2026-07-16
> **方案包**: `docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md`
> **决策树日志**: `docs/superpowers/design-logs/2026-07-16-m5-mpc-colav-design-log.md`
> **关联裁决**: DP-05(VR-05 NLP→acados)+ TBD-4(实测门)
> **范围**: M5 MPC 重构 7 子项目的 P1 第 1 阶段(可行性 spike)。P1b(完整 NLP 迁移 + benchmark)在 spike 通过后开。

---

## 目的

证明 acados 求解器在 M5 容器构建链路内可行(CMake 集成 + code-gen + RTI + HPIPM 后端),**并**用一个现有 NLP 的最小真实子集(恒速 dynamics + 单目标 CPA + 航向 box)重表述为 acados staged-OCP 跑通,验证 formulation 从 monolithic-NLP 到 staged-OCP 的映射可行。这是 P1b 完整迁移的前置信心门 —— 不通过则回炉 DP-05 重评。

## 背景(来自方案包 + 探索证据)

- **DP-05 裁决**: NLP 建模维持,求解器 IPOPT→acados(RTI+HPIPM),因 per-target ξ(M·N)/360s/x=[ψ,r,u] 在 IPOPT 下 O(n³) 实时性不可达 [R18]。
- **acados 现状(探索 Q1)**: 完全不存在(repo/系统/容器均无),需源码构建。
- **工具链齐备(探索 Q6)**: cmake 3.22 / gcc 11.4 / gfortran 11.4 在 host + container;BLASFEO/HPIPM 由 acados 自带子模块构建。
- **关键风险 — ABI 一致性(探索 Q2/Q3)**: casadi 已踩 `_GLIBCXX_USE_CXX11_ABI` 坑(sil_nodes.Dockerfile:52-57 + CMakeLists:61-70 有修补),acados 须同样 `-D_GLIBCXX_USE_CXX11_ABI=1` 构建。
- **关键风险 — Python acados_template(探索 spike inputs)**: acados code-gen 走 Python Jinja2 模板,须 `pip install acados_template`(版本匹配 C 库),新构建期依赖。
- **formulation 现状(探索 Q4)**: monolithic-NLP(扁平 `f/g` over `x=[psi;u;σ]`, 37 变量 + 142 参数 + RowRegistry 约束行布局);IPOPT 选项嵌在 formulation 层(formulation.cpp:648-664)。acados OCP interface 要 per-stage cost/constraint/dynamics → 需重构,非 solver swap。
- **Rule14 HO benchmark(探索 Q5)**: 已存在(colreg-rule14-ho.yaml + probe runners),TBD-4 IPOPT baseline 今天可跑(留 P1b)。

## 用户裁决(brainstorming 澄清)

- **P1 分阶段**: 先 spike(可行性,选项3)→ 通过后 P1b 完整迁移(选项2)[用户 2026-07-16]
- **acados 版本**: 0.4.4(最新稳定 release)[用户 2026-07-16]
- **spike 含 formulation 重构验证**: 是(不只 toy OCP)[用户 2026-07-16]
- **formulation 子集**: 最小 — 恒速 dynamics + 单目标 CPA + 航向 box [用户 2026-07-16]

## 设计

### 改动范围

P1a spike **不碰生产 NLP 代码**(mid_mpc_nlp_formulation/solver 不动),只新增 acados 集成 + 独立 smoke + 独立 formulation 子集验证。IPOPT 保留(`M5_USE_CASADI=ON`),acados 叠加。

| 文件 | 改动 | 类型 |
|---|---|---|
| `docker/sil_nodes.Dockerfile` | casadi 块后(line 93 之后)加 acados 0.4.4 源码构建 + acados_template pip + 断言 | 新增 stage |
| `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt` | 加 `option(M5_USE_ACADOS ON)` + `find_package(acados QUIET)` 块(克隆 M5_USE_CASADI 模式);注册 smoke + 子集验证测试 | 新增块 |
| `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_smoke/` | toy OCP smoke(mass-spring 或 kinematic bicycle)+ Python code-gen 脚本 + C runner | 新增目录 |
| `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_m5_subset/` | M5 formulation 最小子集(恒速 dynamics + 单目标 CPA + 航向 box)staged-OCP 重表述 + Python code-gen + C runner + 验证断言 | 新增目录 |

**明确排除**(推 P1b):
- formulation 全量重构(per-target ξ/x=[ψ,r,u]/360s/COLREGs 代价/转移代价)
- Rule14 HO benchmark(TBD-4 IPOPT baseline + acados 对比)
- IPOPT 移除(保留 `M5_USE_CASADI=ON`)
- 生产 mid_mpc_solver 切换 acados

### 组件 1:Dockerfile acados 集成

在 `docker/sil_nodes.Dockerfile` casadi 块后(line 93 之后)新增(镜像 casadi 断言同款模式):

```dockerfile
# --- acados 0.4.4 (P1a spike: solver migration target DP-05) ---
# ABI consistency: must match casadi's -D_GLIBCXX_USE_CXX11_ABI=1 (see sil_nodes.Dockerfile:52-57)
ARG ACADOS_TAG=0.4.4
RUN git clone --depth 1 --branch ${ACADOS_TAG} https://github.com/acados/acados.git /tmp/acados && \
    cd /tmp/acados && \
    git submodule update --init recursive && \
    mkdir -p build && cd build && \
    cmake -DCMAKE_C_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=1" \
          -DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=1" \
          -DACADOS_WITH_QPOASES=OFF \
          -DACADOS_WITH_OSQP=OFF \
          -DBLASFEO_INSTALL_HEADERS=ON \
          -DACADOS_INSTALL_DIR=/usr/local .. && \
    make install -j$(nproc) && \
    rm -rf /tmp/acados
# Post-build assertion (mirror casadi assertion pattern)
RUN test -f /usr/local/lib/libacados.so || (echo "FATAL: libacados.so missing" && exit 1)
# Python code-gen dependency (version must match ACADOS_TAG)
RUN pip install --no-cache-dir acados_template==0.4.4
```

**ABI 一致性**: `-D_GLIBCXX_USE_CXX11_ABI=1` 强制对齐 casadi + workspace(sil_nodes.Dockerfile:52-57 已为 casadi 设此 ABI)。漏此 → 链接期 undefined references(同 casadi 先例)。

**构建时间**: acados + BLASFEO + HPIPM 源码构建加在 casadi(15-20min)之上,镜像构建时间增长。考虑 BuildKit `--mount=type=cache,target=/tmp/acados/build`(Dockerfile 已用 BuildKit cache mounts,line 1-3)。

### 组件 2:CMake 集成

在 `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt` 克隆 M5_USE_CASADI 块(L50-75 模式):

```cmake
option(M5_USE_ACADOS "Build acados solver integration (P1a spike)" ON)
if(M5_USE_ACADOS)
  find_package(acados QUIET)
  if(acados_FOUND)
    set(M5_HAS_ACADOS ON)
    message(STATUS "acados found (P1a spike)")
  else()
    message(WARNING "acados not found (M5_USE_ACADOS=ON); skipping acados spike targets")
  endif()
endif()
```

**不动 m5_shared_lib** —— spike 用独立可执行/测试,避免污染生产库。

### 组件 3:Toy OCP smoke(工具链验证)

`test/external/acados_smoke/`:
- `gen_smoke.py`: 用 `acados_template` 生成一个 acados getting_started toy OCP(mass-spring chain 或 2-state kinematic bicycle)的 C 代码;调用 `acados_ocp_formulation` + `render_templates`
- `smoke_runner.c`: 链接生成的 C 代码 + libacados,跑 RTI `solve()`,断言返回 HPIPM 收敛(状态码 0)
- CMake 注册为 `m5_add_gtest` 或独立可执行( gated `M5_HAS_ACADOS`)

**验证**: code-gen(Python Jinja2)→ C 编译 → RTI solve(HPIPM)全链路 + `-Werror` 新 ABI 干净链接。

### 组件 4:M5 formulation 子集 staged-OCP 重表述(关键 — 映射可行性验证)

`test/external/acados_m5_subset/`: 现有 NLP 最小子集重表述为 acados staged-OCP。**子集来源(从代码读出的确切表达式)**:

**Dynamics**(恒速直线,formulation.cpp:360-361):
```
状态 x_k = [px, py, psi, u]  (位置 + 艏向 + 速度)
dynamics: px[k+1] = px[k] + u[k]·dt·cos(psi[k])
          py[k+1] = py[k] + u[k]·dt·sin(psi[k])
          psi[k+1] = psi[k]   (P1a 恒速子集不含 ROT;P2 加 Nomoto)
          u[k+1]   = u[k]     (恒速)
```

**约束**:
- 单目标 CPA(constraint_compiler.cpp:353): `g_cpa[k] = (px[k]-tx)² + (py[k]-ty)² - cpa_hard² ≥ 0`(tx,ty 目标位置,常数)
- 航向 box: `lbx ≤ psi[k] ≤ ubx`(bounds)

**代价**: 最简 stage cost `J = Σ_k (psi[k]-psi_ref)²`(航向参考跟踪),证 stage cost 映射。

**文件**:
- `gen_m5_subset.py`: 用 acados_template 把上述 staged-OCP(dynamics 作为 explicit discrete dynamics,CPA 作为 nonlinear path constraint,box 作为 bounds,stage cost)生成 C 代码
- `subset_runner.cpp`: 链接 + 跑 OCP solve(非 RTI,完整 SQP),断言:
  1. 求解收敛(acados status 0)
  2. CPA 约束满足(g_cpa ≥ 0 全步)
  3. 航向 box 满足(lbx ≤ psi ≤ ubx 全步)
  4. 解合理(避让方向正确)
- CMake gated `M5_HAS_ACADOS`

**验证目标**: 证明现有 formulation 的 dynamics/CPA/box 能映射到 acados 的 discrete-dynamics/nonlinear-path-constraint/bounds 原语。这给 P1b 全量重构信心。

### 数据流

```
Dockerfile 装 acados 0.4.4 + acados_template
  → CMake find_package(acados) 解析
    → toy smoke: Python gen → C 编译 → RTI solve(HPIPM 收敛)
    → M5 子集: Python gen → C 编译 → OCP solve(收敛 + CPA/box 满足)
```

### 错误处理

- **ABI 不匹配**: 若 smoke 链接报 undefined reference → 检查 acados/casadi/workspace 的 `_GLIBCXX_USE_CXX11_ABI` 是否一致(三处都得 =1)。spike 失败的首要嫌疑。
- **acados_template 版本不匹配**: 若 code-gen 报 schema 错 → 确认 `acados_template==0.4.4` pip 包匹配 `ACADOS_TAG=0.4.4` C 库。
- **find_package 解析失败**: 若 CMake 报 acados not found → 检查 `ACADOS_INSTALL_DIR=/usr/local` + `pkg-config` 或 `acados_DIR` hint。
- **smoke 不收敛**: 若 toy/子集 solve 不收敛 → 先排除 acados bug(toy 应总收敛),再查子集 OCP 数值合理性(CPA/box/dynamics 表达式)。

## 测试

### 新增测试(spike 自身验证)
1. **Dockerfile 构建断言**: 镜像构建后 `libacados.so` 存在(casadi 断言同款);`pip show acados_template` 版本 0.4.4。
2. **CMake 解析测试**: `find_package(acados)` 成功(M5_HAS_ACADOS ON)。
3. **toy smoke**: RTI solve 返回收敛(状态码 0),证明 code-gen/编译/HPIPM/RTI 全链路。
4. **M5 子集重表述**: OCP solve 收敛 + CPA 约束满足 + box 满足 + 解合理。

### 回归测试(不破坏现有)
- `M5_USE_CASADI=ON` 下,现有全 M5 测试(test_mid_mpc_*/test_constraint_compiler 等)全绿 —— 证明 acados 叠加未破坏 IPOPT 路径。
- 镜像构建 casadi 断言仍 intact(libcasadi_nlpsol_ipopt.so 存在)。

### 通过判据(P1a spike pass criteria — 进 P1b 的门)
- [ ] Dockerfile 构建成功,libacados.so 存在,acados_template 0.4.4 装好
- [ ] find_package(acados) 在 m5 CMake 解析成功(M5_HAS_ACADOS ON)
- [ ] toy smoke:code-gen + 编译 + RTI solve 收敛,-Werror 新 ABI 干净链接
- [ ] M5 子集重表述:dynamics/CPA/box 映射到 acados 原语成功,OCP solve 收敛 + 约束满足 + 解合理
- [ ] 现有 IPOPT 路径全绿(M5_USE_CASADI=ON 下全 M5 测试 + casadi 断言 intact)
- [ ] **可行性结论**: acados 工具链可行 + formulation 映射可行 → 可进 P1b

### 失败处置
- 工具链不可行(ABI/code-gen/HPIPM 阻塞)→ 记录阻塞点,**回炉 DP-05** 重评(是否仍转 acados,还是留 IPOPT + 优化)。
- formulation 映射不可行(子集重表述跑不通)→ 记录映射难点,**回炉 DP-05** 评估 monolithic-NLP 是否有替代路径(如 IPOPT + 结构利用优化,不转 acados)。

## 风险

- **高**(新依赖源码构建 + ABI + code-gen + formulation 映射多风险点)
- 主要风险:ABI 一致性(已识别有修补模式)/ acados_template 版本匹配 / formulation 映射难点(spike 子集可能暴露 acados OCP interface 与现有 monolithic 结构的阻抗)
- spike 的价值正是**前置暴露这些风险**,在 P1b 大投入前决定是否继续

## 出 P1a 范围(后续)
- **P1b**(spike 通过后): formulation 全量 staged-OCP 重构(per-target ξ/x=[ψ,r,u]/360s/COLREGs/转移代价)+ Rule14 HO benchmark IPOPT baseline + acados 对比 + 生产 mid_mpc_solver 切换 + IPOPT 移除决策
- **回炉 DP-05**(spike 失败): 带 acados 不可行新证据重评 SB-MPC 或 IPOPT 优化路径

## 关联
- 方案包组件 3(DP-05 裁决)+ 组件 8(TBD-4):本 P1a 是 TBD-4 实测门的前置可行性
- 决策树日志 VR-05 + TBD-4:spike 通过 → P1b benchmark → DP-05 最终定案或回炉
