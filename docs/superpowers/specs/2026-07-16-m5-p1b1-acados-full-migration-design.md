# P1b-1 Spec: 生产 NLP acatos 全量迁移 + Nomoto physics 升级

> **产出**: brainstorming,2026-07-16
> **方案包**: `docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md`
> **决策树日志**: `docs/superpowers/design-logs/2026-07-16-m5-mpc-colav-design-log.md`
> **关联裁决**: DP-05(VR-05 NLP→acatos)+ VR-02(Nomoto-扩展)+ VR-03(per-target ξ)+ VR-07(状态含 r)+ TBD-4(实测门)+ TBD-5(Nomoto 参数)+ TBD-6(混合 L1/L2)
> **前置**: P1b-0 staging spike 通过(commit 02ce2bec0,7/7 验收门,4 点 staging 可扩已验证)
> **范围**: M5 MPC 重构 P1b 的第 1 阶段(全量等价迁移 + Nomoto 升级)。P1b-2(增强)在本阶段通过后开。

---

## 目的

P1b-0 证明了 acatos 工具链 + M5 subset 的 4 个结构决策点(prefix equality / J_colreg per-stage EXTERNAL / 全局 σ slack / bound schedule)staging 可扩。P1b-1 在此基础上完成**生产 NLP 的全量 acatos 迁移 + VR-02 Nomoto 预测模型升级**,目标是**用更合适的预测模型(Nomoto 替代恒速运动学)提升避碰航线的求解精度和效率**。

这是 DP-05(VR-05)的实测落地:TBD-4 Rule14 HO benchmark 对比 IPOPT vs acatos。若 acatos 实测不达标,回炉 DP-05。

## 用户裁决(brainstorming 澄清,2026-07-16)

- **决策变量维度**: 双通道 psi[N]+u[N](生产忠实)[用户]
- **dynamics**: 实现 VR-02 完整 Nomoto `Tṙ+r=Kδ`,状态 x=[px,py,ψ,r],控制 u=[δ,n](舵角+转速)[用户]
- **Nomoto 参数来源**: VDM zigzag 辨识(对 45m FCB 的 4-DOF MMG 跑 10/10+20/20 → 最小二乘拟合 T,K)[用户]
- **solver 切换**: compile-time flag `M5_USE_ACADOS`(默认 OFF)[用户]
- **benchmark 判据**: 轨迹级行为等价(避让决策+CPA-feasible 一致;Nomoto vs 运动学非同 physics,不要求 bit-close)[用户]
- **失败处置**: 失败即停 + 分场景回炉(沿用 P1b-0 纪律)[用户]
- **slack**: per-target per-step ξ 混合 L1/L2(TBD-6 选项 B,ρ·ξ+½w·ξ²)[用户,VR-TBD6]
- **实施路径**: 方案 A(staging 验证 2 新点 → 生产 backend → benchmark)[用户]

## 关键解释点(诚实标注)

**VR-02 Nomoto vs 生产现状的差距(必须在设计中显式处理)**:
- VR-02/TS-12 决策日志的目标是 Nomoto-扩展 `Tṙ+r=Kδ`(状态 x=[x,y,ψ,r],控制 u=[δ,n])。
- 但生产代码**现状根本没实现 Nomoto**:[R6] 确认生产 NLP 用恒速直线运动学 `pos[k+1]=pos[k]+u[k]·dt·(cos,sin)(psi[k])`,psi/u 是扁平决策变量(非 Nomoto 微分形式);`nomoto_fallback.cpp` 存在但 r₀=0 退化为直线([R12]/ALT-04)。
- **因此 P1b-1 是"迁移 + physics 升级",不是"纯等价迁移"**。benchmark 判据据此调整为"轨迹级行为等价"(两 physics 本不同,不要求数值 bit-close)。

**Nomoto 参数精度边界**:
- VDM(`VesselDynamicsModel`)是 45m FCB 的 4-DOF MMG(Yasukawa-Yoshimura 简化线性型,L=45/B=8/draft=1.55,正是目标船型)。
- 但 VDM 自标"Simplified linear hull-force/rudder-force approximations,exact non-linear MMG awaits pool-test data [TBD-HAZID]"。
- **故 VDM zigzag 辨识的 T,K 是"45m FCB 简化 MMG 的拟合值"** —— 比纯缩律估算([R22] 2x 误差)更可信(因 VDM 编码了几何+桨舵),但**不是真海试值**。海试后仍需标定(TBD-5 最终闭环)。

## 关键设计变更(2026-07-16,P1b-1a T8 执行中发现)—— Path B 双积分器 dynamics

> **本节覆盖原 VR-02 一阶 Nomoto 假设。T8 执行(2026-07-16)撞到真实物理墙,用户裁决(2026-07-16)采用 Path B:honest 双积分器 dynamics,不造 VDM 系数。下方所有 T6/T8/T9/P1b-1b/P1b-1c 条目以本节为准。**

**发现(实测 + 双重独立复核确认)**:`VesselDynamicsModel::compute_accelerations()` 的偏航加速度 `dr/dt = (k_n_rudder·u²·δ)/izz_e`(`vessel_dynamics_model.cpp:52-59`)**没有 `N_r·r` 偏航阻尼项**(全仓 grep 确认 yaw-damping 仅此一处且无 r 依赖)。

**后果**:VDM 偏航通道是**纯积分器**:`r(t)=r₀+∫c·δ dt`,`ψ(t)=ψ₀+∫r dt`(ψ 是 δ 的二重积分)——即**二阶系统**,一阶 Nomoto `Tṙ+r=Kδ` 结构上不可拟合。T8 实测最小二乘 `ṙ=a·δ+b·r` 得 `b≈-9.7e-4`(≈0)→ `T=-1/b≈1027s`(有效无穷),回代误差 35°(门 2°),`IDENT FAIL`。R²=0.97 不是好结果而是**确认 `ṙ≈a·δ`**(b·r≈0)的 integrator 指征。独立数值复核:60s 恒舵下 yaw rate 线性增长(前后半程增量 -0.1046 vs -0.1029 近等,非指数收敛),确认积分器。

**裁决(用户,2026-07-16;GNC reviewer + 主代理双重复核背书):Path B** —— T6/T9 dynamics 改为 honest 双积分器 `ṙ=c(u)·δ, ψ̇=r`,`c(u)=k_n_rudder·u²/izz_e` 直接读 VDM 源码(不造系数)。T8 改为辨识 `c(u)` + 积分器线性度诊断。

**为何选 Path B(否决 A/C)**:
- Path A(给生产 VDM 加 `N_r·r`)BLOCK:触碰 `[TBD-HAZID]` 生产系数(无海试数据),等于 M5 自造水动力证据过自己的可执行性门,违反 doer-checker 独立性。仅作 T8 局部合成 plant 才可,label 为合成,非生产路径。
- Path C(用 manifest DEMO-2 fallback T=6/K=0.3)违背本 spec 承诺"T,K 来自 VDM 辨识比缩律更可信"(§关键解释点),断言了 VDM 没有的阻尼时间尺度,诚实度差;仅作显式降级 label 才可。
- Path B 不碰任何 `[TBD-HAZID]` 生产系数 / VDM 行 / manifest 值,只改 staging/生产 **OCP dynamics 表达**(P1b-1 合法产出),保物理诚实,仍达成 spec 核心意图("ψ,u 扁平决策变量 → 舵驱动航向 dynamics"的 physics 升级)。

**对原 VR-02/TS-12 假设的偏差(须记录,路由 TDL Lead)**:VR-02/TS-12 假设一阶 Nomoto `Tṙ+r=Kδ`;生产 VDM 结构上是二阶双积分器。Path B 把 OCP 预测器定为双积分器,这是对 VR-02 的实测落地偏差,记录在此供后续裁决(真海试 TBD-5 给 VDM 补 `N_r` 阻尼后,可升级回真一阶 Nomoto)。

**已知 gap(诚实)**:纯双积分器极点在 z=1(临界稳定),靠 ROT box `|r|≤rot_max` + 代价 + 终端约束保持 r 有界(标准 kinematic-bicycle OCP 处理方式)。缺真实偏航阻尼意味着命令对扰动敏感 —— 对 staging(T6/T9)与 benchmark(P1b-1c 行为等价 vs IPOPT)可接受;真 FCB 可执行性仍待 TBD-5 海试闭环。

**T6/T8/T9 修订要点**(详见下方各节修订):
- **T8**:从"拟合 T,K"改为"辨识速度相关增益 `c(u)=k_n_rudder·u²/izz_e` + 报积分器线性度诊断(60s 恒舵前后半程 r 增量比∈[0.95,1.05] 作 model-class 正向证据)"。`ident_runner.cpp` zigzag 仿真不变;`ident_nomoto.py`/`verify_nomoto.py` 改为 c(u) 估计 + 线性度门(去一阶回代 2° 门)。
- **T6**:dynamics 从一阶 Nomoto 改双积分器 `disc_dyn_expr`: `r[k+1]=r[k]+dt·c(u)·δ[k]`,`ψ[k+1]=ψ[k]+dt·r[k]`,`px/py` 运动学积分。state x=[px,py,ψ,r](4),control u=[δ](1,surge 作 param)。forward-match 门 1e-9 仍适用且可达(多项式离散更新)。
- **T9**:6 点合并的 dynamics 行改双积分器;其余(prefix/J_colreg/per-target ξ/bound schedule)不变。
- **P1b-1c benchmark**:ψ 序列 max|Δ|<0.1 rad 容差须对**双积分器预测器**(非一阶 Nomoto)重新论证;两 physics 本不同仍成立。

## 架构

### 三阶段分解(方案 A)

```
P1b-0 已验证 4 点 (prefix/J_colreg/σ/bound schedule, 单 dpsi 通道, commit 02ce2bec0)
   │
   ├──► P1b-1a: staging 验证 2 新 physics/staging 点 (test/external/, 不碰生产)
   │      T6: Nomoto 变速 dynamics staging (x=[px,py,ψ,r], u=[δ,n], Tṙ+r=Kδ)
   │      T7: per-target per-step ξ 高维 slack staging (per-target, 混合 L1/L2)
   │      T8: Nomoto 参数 VDM zigzag 辨识 (45m FCB → T,K)
   │      T9: 6 点合并共存 (Nomoto + P1b-0 4 点 + per-target ξ)
   │
   ├──► P1b-1b: 生产 MidMpcAcadosSolver (M5_USE_ACADOS flag)
   │      全 6 cost + 全 142 参数 per-stage 分区 + 全约束类
   │
   └──► P1b-1c: Rule14 HO benchmark (IPOPT 运动学 vs acatos Nomoto)
```

### solver 切换机制

- **Compile-time flag `M5_USE_ACADOS`**(默认 OFF),新增于 `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt`。
- ON 时编译 acatos backend(`MidMpcAcadosSolver`),OFF 时维持现有 IPOPT(`MidMpcNlpFormulation`/`mid_mpc_solver.cpp`)。
- `MidMpcSolver` 内 dispatch:`M5_USE_ACADOS` → acatos,else → IPOPT。两 backend 共用 `MidMpcInput`/输出契约,下游(M4/L4)无感知。
- benchmark:两次 build(OFF/ON)对比。

### 改动范围

| 文件 | 改动 | 类型 |
|---|---|---|
| `test/external/acados_staging/common.py` | 扩 `build_base_ocp_nomoto(...)`(双通道+Nomoto),保留 `build_base_ocp` 作对照 | 改 |
| `test/external/acados_staging/T6_nomoto/` | T6 Nomoto dynamics staging(gen+runner+run) | 新增 |
| `test/external/acados_staging/T7_xi/` | T7 per-target ξ staging | 新增 |
| `test/external/acados_staging/T8_ident/` | T8 VDM zigzag 辨识 | 新增 |
| `test/external/acados_staging/T9_merge6/` | T9 6 点合并 | 新增 |
| `mid_mpc/mid_mpc_acados_formulation.{hpp,cpp}` | acatos OCP 符号图(Nomoto+6 cost+全约束+142 参数) | 新增 |
| `mid_mpc/mid_mpc_acados_solver.{hpp,cpp}` | acatos code-gen+求解+参数 pack/unpack+输出 | 新增 |
| `mid_mpc/mid_mpc_solver.cpp` | dispatch 分支(M5_USE_ACADOS) | 改 |
| `CMakeLists.txt` | option(M5_USE_ACADOS)+ 条件编译+code-gen 步骤 | 改 |
| `test/unit/test_mid_mpc_acados_*.cpp` | acatos backend 单元测试 | 新增 |

**明确排除(推 P1b-2 或更后)**:
- 360s horizon(P4)
- COLREGs 几何 P5 移除硬编码 Rule14/15 行
- zone 约束(非平滑)
- 海试标定真 Nomoto 参数(TBD-5 最终闭环)
- x=[x,y,ψ,r,u] 5 维状态(VR-07 后续,本阶段 4 维)

## P1b-1a: staging task 验证点

沿用 P1b-0 模式(独立目录、不碰生产、失败即停、沿用 F1-F5 配置 + P1b-0 staging 发现)。基于 P1b-0 `common.build_base_ocp` 增量扩展。

**架构升级(不重验 P1b-0 已验点)**:P1b-0 的 prefix equality(T1)、bound schedule(T4)参数激活因子在 Nomoto 双通道下同样适用(只作用于 h-row,与 dynamics 无关)。T6-T9 只验 2 新点 + 合并。

### T6: 双积分器 heading dynamics staging(Path B 修订,2026-07-16)
- **验证点**:honest 双积分器 dynamics `r[k+1]=r[k]+dt·c(u)·δ[k]`,`ψ[k+1]=ψ[k]+dt·r[k]`,`px[k+1]=px[k]+u·dt·cos(ψ[k])`,`py[k+1]=py[k]+u·dt·sin(ψ[k])`(c(u)=k_n_rudder·u²/izz_e,从 T8 辨识或 VDM 源码直读)能否映射到 acatos `disc_dyn_expr`,且 SQP 收敛。state x=[px,py,ψ,r](4),control u=[δ](1,surge 恒定作 stage param)。**不是一阶 Nomoto**(见上方 Path B 变更节)。
- **场景**:舵角阶跃(±10°),surge 恒定 u0。
- **断言**:dynamics 数值正确(forward 模拟 vs acatos 积分一致 < 1e-9,多项式离散更新可达)+ 求解收敛或 status 4(F5)+ ROT `|r|≤rot_max`/舵角 box 满足。**风险(诚实)**:纯积分器极点 z=1,临界稳定,靠 ROT box 保 r 有界;须在 T6 显式 stress-test 大 rot_max 激活下 SQP 收敛。

### T7: per-target per-step ξ 高维 slack staging
- **验证点**:从 P1b-0 单标量 σ(idxsh=[0])升级到 per-target per-step ξ(每 target 每 stage 一个 slack,Nt×N 维)。acatos `idxsh` 软化多行 h + `Zl/zl` 混合 L1/L2(线性 ρ 保精确性 + 二次 w 保 Hessian 正定,TBD-6 VR-TBD6 选项 B)。
- **场景**:2 target,各自 CPA 独立软化。
- **断言**:per-target ξ 独立精确性(可行 ≈0,不可行 >0 松弛各自 CPA)+ 混合 L1/L2 penalty `ρ·ξ+½w·ξ²` 数值正确。

### T8: VDM zigzag 辨识 → c(u) 增益 + 积分器 model-class 诊断(Path B 修订,2026-07-16)
- **验证点**:对仓库内 45m FCB 的 `VesselDynamicsModel`(4-DOF MMG)跑 10/10 + 20/20 zigzag 仿真 → 估计速度相关偏航增益 `c(u)=k_n_rudder·u²/izz_e` 并**诊断 VDM 偏航是积分器(二阶)而非一阶 Nomoto**(model-class 正向证据)。
- **产出**:`c_u`(在 cruise u 下,rad/s² per rad 舵) + 积分器线性度指标(60s 恒舵前后半程 r 增量比) + IMO MSC.137(76) 参考(一超调,仅参考不门)。
- **门**:积分器线性度正向证据(前后半程 r 增量比 ∈ [0.95, 1.05],确认 `ṙ≈a·δ` 积分器结构)。**不再有一阶 Nomoto 回代 2° 门**(那是 Path B 取代掉的)。`c_u` 用于 T6/T9 双积分器 dynamics 的 `c(u)` 项。
- **精度标注**:"VDM 直读的偏航增益,无造系数;真偏航阻尼 N_r 待 TBD-5 海试给 VDM 补后可升级回一阶 Nomoto"。

### T9: 6 点合并共存
- **验证点**:**双积分器 dynamics**(Path B,T6)+ prefix(T1)+ J_colreg(T2)+ per-target ξ(T7)+ bound schedule(T4)全共存一个 acatos OCP。
- **断言**:6 点共存求解收敛或 status 4(F5)+ 全约束正确。

## P1b-1b: 生产 backend(MidMpcAcadosSolver)

### 决策变量映射(IPOPT 扁平 → acatos staged)
- IPOPT:`x = vertcat(psi_[N], u_[N], [sigma])` 扁平 + 位置前向积分(无状态)。
- acatos:状态 x=[px,py,ψ,r](4),控制 u=[δ,n](2),**双积分器 dynamics(Path B)**`ṙ=c(u)·δ, ψ̇=r` 推进(c(u)=k_n_rudder·u²/izz_e,VDM 直读,非 Nomoto T/K)。psi/u 序列从 acatos 状态/控制轨迹 per-stage 重构。
- 这是最大的映射工作:扁平决策变量 → staged 状态/控制。

### 142 参数 per-stage 分区
- stage-uniform 量(route frame, cpa_safe, weights, **偏航增益 c(u)**(Path B,替代 Nomoto T/K)、rot_max)作全局 p。
- stage-varying 量(prefix psi/u 序列、target drift、disc_k、activation 因子 pact/cpa_act)作 per-stage p,用生成的 `<name>_acados_update_params(capsule, stage, vals, NP)`(P1b-0 验证的 C API,非 `ocp_nlp_in_set "p"`)。

### 6 cost 全迁移
- colreg(T2 EXTERNAL 已验)、dist(航向参考)、route(横迹 R1)、vel(速度跟踪,新因变速)、asym(Rule14/15 gated)、terminal(终端横迹 softplus T1)。
- **cost_scaling=ones(N+1)**(T2 关键发现,否则离散求和被 ×DT 偏)。

### 约束类全迁移
- CPA(per-target,带 ξ slack)、direction、min_alt、ROT、terminal + bound schedule(T4 激活因子已验:per-stage 参数激活是 stage-uniform-bounds 的唯一 per-stage 杠杆)。

### code-gen 策略
- acatos OCP 在 build 时 code-gen C(类似 P1a/P1b-0 gen_*.py),集成进 CMake build(code-gen → 编译 → 链接)。Dockerfile 约束沿用(acados_template `--no-deps`、ACADOS_SOURCE_DIR=/usr/local)。

### 输出契约(与 IPOPT 一致)
- 同输入 `MidMpcInput` + 142 参数 → 同输出契约字段(psi 航向序列、u surge 速度序列、cost、status、CPA/box 满足标志)。
- acatos backend 内部状态/控制是 x=[px,py,ψ,r]/u=[δ,n],但**输出时重构为 IPOPT 契约字段**(从 acatos 状态轨迹取 ψ 序列;从 δ/n 控制 + 双积分器 dynamics 重构 surge u 序列;ROT r 作为附加诊断字段)。下游(M4/L4)收到的字段与 IPOPT 一致,无感知 backend 切换。

## P1b-1c: Rule14 HO benchmark

- **场景**:标准 head-on Rule14(两船对遇,本船 give-way 让路)。可扩 2-3 变体(不同速度比/接近角)。
- **对比**:`M5_USE_ACADOS=OFF`(IPOPT 运动学)vs `=ON`(acatos **双积分器 Path B**),同 MidMpcInput。
- **判据(轨迹级行为等价 + 物理更优)**:
  1. 避让决策一致(都 starboard turn / 都 give-way)
  2. CPA-feasible 一致(都满足 CPA ≥ cpa_safe)
  3. 轨迹形状一致(psi 序列 max|Δ| < 0.1 rad;不要求 bit-close,两 physics 不同 —— **Path B 修订:此容差须对双积分器预测器(非一阶 Nomoto)重新论证;T6 stress-test 收敛后再定**)
  4. IMO MSC.137(76)回转指标对齐(advance ≤ 4.5L,tactical dia ≤ 5L)
  5. 实时性:acatos 单次 solve ≤ 求解预算(vs IPOPT 3s)
  6. cost 数值报告(参考,非硬门)

## 验收门(promotable 进 l3-tdl)

- **staging 门(P1b-1a)**:T6 双积分器 dynamics 可扩(Path B)/ T7 per-target ξ 可扩 / T8 c(u) 辨识 + 积分器 model-class 诊断完成(Path B)/ T9 6 点合并共存。
- **backend 门(P1b-1b)**:生产 acatos backend 标准场景求解收敛 + 输出契约匹配 + 142 参数正确 pack。
- **benchmark 门(P1b-1c)**:Rule14 HO 6 条判据全过。
- **回归门**:IPOPT 路径无回归(M5_USE_ACADOS=OFF colcon 与基线一致)+ acatos 路径新测试全绿。
- **promotable**:全过 → merge l3-tdl + push origin/l3-tdl。

## 失败处置(沿用 P1b-0 纪律,严把)

- 某 task/scenario 不可达即**停**,记录阻塞点,**不强行绕过、不 mock、不 forced-pass、不为过测试调阈值**。
- 按阻塞性质分类回炉:
  - **Path B 已落地**(2026-07-16):原 "一阶 Nomoto 辨识失败" 阻塞已由 Path B 双积分器 dynamics 解决(去 T→∞ 病态);双积分器本身的临界稳定(极点 z=1)靠 ROT box 保 r 有界,T6 须显式 stress-test。
  - 双积分器 dynamics 数值病态(若 T6 SQP 不收敛)→ 查 c(u) 辨识值、离散化稳定性、warm-start、ROT box 是否够紧(TBD-5 真 N_r 补后可升级一阶)
  - per-target ξ staging 不可扩 → 回 P3 TBD-6(降级或外层 σ)
  - QP solver(HPIPM error 3)→ 评估装 qpOASES(active-set)
  - Rule14 HO 超容差 → 先查是否 physics 差异(双积分器 vs 运动学本就不同)还是 bug
- **IPOPT 路径始终在**(M5_USE_ACADOS 默认 OFF),生产不受任何阻塞影响。

## 沿用配置(P1b-0 F1-F5 + staging 发现)

- **F1** warm-start seed(forward-propagated,非零初值,否则首 QP 病态)
- **F2** 单边 h 上界用 1e10(非 np.inf,t_renderer 拒 JSON Infinity)
- **F3** EXACT hessian(非线性 CPA 需要)
- **F4** MERIT_BACKTRACKING globalization(CPA-active 起点必需)
- **F5** status 4(QP error during refinement)容忍,以约束满足判 PASS
- **staging 发现 1**:acatos `lh/uh`/`lbx/ubx`/`idxsh`/`Zl/zl` 全 stage-uniform → per-stage 切换用参数激活因子(`pact*...`)
- **staging 发现 2**:cost_scaling=ones(N+1)(否则 EXTERNAL cost ×DT 偏)
- **staging 发现 3**:per-stage p 用生成的 `<name>_acados_update_params`(非 `ocp_nlp_in_set "p"`)
- **staging 发现 4**:σ 在 future-violation 激活(非起点在 disc 内,HPIPM 无法从不可行种子迭代)

## 风险

- **高**:Nomoto dynamics 是全新 physics(P1b-0 未验),变速积分 + ROT 状态可能引入数值病态。T6 staging 前置 + T8 参数辨识质量是关键。
- **中**:per-target ξ 是 16× 维度跃升(P1b-0 只验单标量),exact-penalty 在高维下可能需调 ρ。T7 staging 前置。
- **中**:benchmark 是"不同 physics 比行为"(Nomoto vs 运动学),判据主观性高于数值等价。轨迹容差 0.1 rad 需实证校准。
- **低**:142 参数 per-stage 分区工作量大但模式已验(P1b-0 idxsh/pact)。

## 出 P1b-1 范围(后续)

- **P1b-2**:增强(360s horizon P4 / COLREGs 几何 P5 移除硬编码 / zone 约束 / x=[x,y,ψ,r,u] 5 维 VR-07)
- **海试标定**:真 Nomoto 参数(TBD-5 最终闭环)
- **per-target ξ 升级**:若 P1b-1 单标量等价够用,per-target 可推 P3

## 关联

- 方案包组件 3(DP-05)+ 组件 8(TBD-4):本 P1b-1 是 DP-05 实测落地
- VR-02(Nomoto)+ VR-03(per-target ξ)+ VR-07(状态含 r)+ TBD-5(Nomoto 参数)+ TBD-6(混合 L1/L2):本 spec 落地这些裁决
- P1b-0 spec/plan + F1-F5 + staging 发现:本 spec 沿用并扩展
- P1a 起点(subset 模板):acatos 工具链基础
