# Step5 方案 A 深化 · DP-01 nh=36 双 row(ALT-08)

> **深化 agent**: `tdl_m5_planner_engineer`(read-only)
> **agent id**: agent_74180eb8-c979-45ac-9a0d-bd21128ac14d
> **对比对象**: DP-01 nh 抉择(BL-09)
> **方案**: A(ALT-08,ZCode 评审推荐)
> **推荐度**: ★★★☆☆(3/5)
> **状态**: DONE_WITH_CONCERNS
> **workspace writes**: none
> **完成时间**: 2026-07-20 17:30(后台异步,300s)

---

## 方案 A 定义

nh=36 双 row:[0,1] prefix / [2..17] CPA soft(16 targets,residual = dx²+dy²-cpa_safe²,入 idxsh)/ [18..33] CPA hard(16 targets,residual = dx²+dy²-cpa_hard_m²,永不入 idxsh)/ [34] direction / [35] min_alt。soft aspiration 同时由 soft row + J_colreg cost 表达(double-expressed)。

## 1. 来源(证据溯源)

| 证据 ID | 来源 | 对方案 A 的支撑 | 评审归属 |
|---|---|---|---|
| [R23] | L1a OCP 评审 ZCode agent_1436144e | **方案 A 的直接来源**。ZCode 评审明确"nh=36 + 硬 row 永不入 idxsh + wrapper runtime 重发 idxsh,这三点强制必做",但同时发现前置盲区 BL-08(`kGIdxCpaHard` slot 不存在) | **ZCode 更支持** |
| [R19] | BL-A 调研 agent_1e69aa0b | "路径 A(codegen nh 含 hard+soft CPA + wrapper runtime 每 cycle 每 stage 重发 idxsh)可行且推荐"——路径 A 的语义本身**不指定 nh=20 还是 36**(BL-A 剩余不确定性明示)。ZCode 把 BL-A 的"路径 A"解读为 nh=36;但 BL-A 原文 nh=20 也兼容路径 A | 部分支撑(被 ZCode 选择性解读) |
| [R21]/[R25] | acados `bgh.c:1411-1417` slack 数学 | 双重复核通过:row 进 idxsh 后残差减 slack,结构上永远 soft;hard row 必须排除 idxsh。**这是方案 A 与方案 B 的共同基础,不构成 A>B 的证据** | 双方一致 |
| [R3] | `formulation.cpp:333,340` + `gen:315,323` acados graph 只用 `cpa_safe` | 当前 graph **没有 hard row**,方案 A 需要从零新增 16 个 hard row | 中性(揭示返工面) |
| [R24] | L1a OCP 评审 Codex | **明确反对 nh=36**,推荐 nh=20+nsh=0+J_colreg(方案 B)。Codex 指出双 row 重复表达 2500、增加同梯度 row、m² slack 量级(~1e6-1e7) | 反对 |

**来源结论**:方案 A 几乎完全依赖单一评审 [R23](ZCode);[R19](BL-A)被选择性解读(BL-A 原文明示 nh=20 也兼容);[R24](Codex)明确反对。来源权威性单薄,缺乏独立交叉印证。

## 2. 工程验证

### 关键发现:IPOPT 现状**不**采用方案 A

经读源码核实,**IPOPT 不是方案 A 的工程先例**:

- IPOPT CPA hard 通过 `cpa_hard_from_k` suffix-hard schedule 实现(`mid_mpc_solver.cpp:559-622`),即**同一 CPA row 的 bound 在 k<deadline 时 relax 到 [-inf,+inf],k>=deadline 时 hard [0,+inf]**。
- 实现位置:`row_registry.hpp:318-328` `apply_cpa_suffix_hard_`,操作的是**同一组 CPA row**,不新增 row。
- IPOPT soft aspiration 通过 `build_colreg_cost_` 的 exp barrier(`mid_mpc_nlp_formulation.cpp:346-365`,用 `kIdxCpaSafe`)。
- **IPOPT 现状 = hard row + soft cost,无 soft row,无 dual row**。这与 Codex 评审 §DP-01 "7. 跨路径一致性" 的描述一致,与设计日志 [R13] IPOPT 7+1 适合镜像项也一致。

**方案 A 的 IPOPT 对等性**:方案 A 比 IPOPT 现状**更严格**(IPOPT 的 σ 全局标量使 CPA 非 true hard,见 BL-15);方案 A 的 dual-row hard+soft 在 IPOPT 中**没有先例**。

### 其他工程先例

- acados 上游:`bgh` 模块支持 runtime idxsh/Zl 更新([R19][R25]),但**上游 example 未发现 dual CPA row(同梯度,只半径不同)的公开用例**。这是方案 A 的原创建模,不是 well-trodden pattern。
- 一般 MPC 文献:hard floor + soft aspiration 的 dual-expression(同约束在 cost 和 constraint 双写)在文献中**存在争议**——通常会引发 cost/constraint 权重协调困难(lexicographic 或 exact-penalty 调参敏感)。

**工程验证结论**:方案 A **无生产先例**,IPOPT 不采用、acados 上游无 example、文献无定论。这是原创建模,工程风险等级相应提高。

## 3. 技术分解(逐子模块打钩)

- [ ] **hard row 表达(cpa_hard_m,排除 idxsh)** —— 当前 `build_con_h_`(`formulation.cpp:320-354`)**无 hard row**,需新增 16 个 `dx²+dy²-cpa_hard_m²` row,且**必须确认这些 row 的索引不在 idxsh 集合内**。codegen `gen:308-340` 的 `con_h` vertcat 需重写。
- [ ] **soft row 表达(cpa_safe,入 idxsh,slack)** —— 当前 16 个 CPA row 用 cpa_safe 且已在 idxsh(`gen:510`);方案 A 下这些 row 语义不变,但位置从 [2..17] 挪到 [2..17](soft block 先),hard block [18..33] 跟在后面。**soft row 表达本身已存在**,改动是位置重排。
- [ ] **J_colreg cost barrier(用 cpa_safe)** —— 当前 `build_colreg_cost_`(`formulation.cpp:370-428`)用 `kGIdxCpaSafe`,**保留不变**。这是 double-expression 的来源。
- [ ] **kGIdxCpaHard global slot 新增** —— **0 命中**(grep 已确认)。需新增:enum(slot 26,挤占 `kGIdxTargets` 当前位置)+ pack(`formulation.cpp:609` 附近加一行)+ codegen SX 镜像(`gen` 的 G_ 枚举)+ `np_global` 154→155 + 三方 hash 重建。**这是 BL-08,与方案 A/B 都必做**(方案 B 也要 cpa_hard_m 进 graph 才能做 hard row)。但方案 A 因新增 dual row,对 slot 的依赖更显式。
- [ ] **codegen nh 20→36** —— `gen_mid_mpc_acados.py` 的 `con_h = ca.vertcat(...)` 需加 16 个 hard row;`nh` 计算重写;`idxsh` 从 `arange(NT)+2` 改为 `arange(NT)+2`(soft block,hard block 排除)。`NSH` 仍为 16(只 soft row 有 slack)。
- [ ] **wrapper runtime idxsh 排除 hard row** —— 当前 wrapper **无 runtime idxsh set**(grep 0 命中,只有 lh/uh set at 986-1009)。需新增每 solve 每 stage `ocp_nlp_constraints_model_set("idxsh", ...)`,将 hard row [18..33] 永久排除,soft row [2..17] 按 prefix_active_k 动态调整(BL-A 推荐路径 A)。**这是 BL-01 闭环的核心实现项**。
- [ ] **row index 重算(direction/min_alt)** —— `mid_mpc_acados_solver.cpp:79-82` `kRowDirection=18`/`kRowMinAlt=19` 需改为 `kRowDirection=34`/`kRowMinAlt=35`;`kRowCpaBase=2` 不变但需新增 `kRowCpaHardBase=18`;`static_assert(kRowMinAlt == kAcadosNh - 1)` 需更新为 `35 == 36 - 1`。**全链路常量重算**。
- [ ] **h_fn cache rebuild** —— `constraints_satisfied_`(`mid_mpc_acados_solver.cpp:555-568`)lazy-build `h_fn` from `formulation_.con_h_expr()`。con_h_expr 变更后 cache 自动失效(每次 formulation rebuild 都是新对象),但需确认 `h_val.size() == kAcadosNh` 检查(line 629)更新为 36,且 row 名映射(line 669-673)新增 "cpa_hard" 分支。
- [ ] **L4 constraints_satisfied_ 适配** —— 同上文件 line 537-672 的 row 遍历需区分 soft row(检查 h+xi>=-tol)和 hard row(检查 h>=-tol,无 slack);empty target slot relax(line 178-179)需对 soft 和 hard 两 block 各做一次。

**返工面量化**:9 个子模块需改,涉及 4 个文件(formulation.cpp / gen_mid_mpc_acados.py / mid_mpc_acados_solver.cpp / row_registry.hpp 的 IPOPT 参考),核心改动约 **250-400 行代码**(其中 codegen ~80 行、wrapper runtime idxsh ~60 行、row 常量+constraints_satisfied_ ~120 行、formulation hard row ~40 行)。**相比方案 B(约 100-150 行,主要新增 kGIdxCpaHard slot + idxsh runtime 排除逻辑,不新增 row),方案 A 返工面大 ~2-3x**。

## 4. 失效边界(关联 SC-NN)

### FB-A1:soft aspiration double-expression 失效(关联 SC-08)
**场景**:1852<d<2500 区间,soft row(残差 `dx²+dy²-cpa_safe²`,cpa_safe=2500)+ J_colreg barrier(用同一 cpa_safe)同时活跃。
**失效方式**:
- 若 soft row 的 slack cost(Zl·xi + 0.5·Zl·xi²)与 J_colreg 的 exp barrier 权重不协调,两者会**竞争驱动力**。slack 是 quadratic(convex,gradient 单调),barrier 是 exponential(尾部发散)。在 d≈2400-2500 区间,slack gradient 可能压倒 barrier,导致 solver 选"违反 slack 一点点"而非"靠 barrier 推开";反之在 d≈1852-2000 区间,barrier 量级可能爆炸(σ=5e-3,exp(5e-3·500)=12.2)压倒 slack。
- **量级问题**:slack 变量 xi 的量纲是 m²(cpa²-d²),在 d=1852 时 xi = 2500²-1852² = 2.8e6 m²。Zl·xi 若 Zl=1.0 则贡献 2.8e6,与 J_colreg(归一化后 O(1))**完全不在一个量级**。这是 Codex 评审指出的"m² slack 量级 ~1e6-1e7"问题的具体来源。必须做 slack 归一化(如改残差为 `(d²-cpa²)/cpa²` 无量纲),但归一化后又需重新调 Zl。
- **关联 SC-08**:1852 应 hard-feasible、2000 应 hard-feasible+soft-cost active、2500 边界。double-expression 下 2000 处的 slack 行为可能与 J_colreg 不一致,出现"slack 妥协但 barrier 不妥协"的振荡。

### FB-A2:nh=36 conditioning 恶化(关联 F-05,SC-01/02/03)
**场景**:F-05 已确认 EXACT Hessian + R=0(no regularization)数值脆弱。nh 翻倍 → h row 总数翻倍 → 首 QP Hessian 维度增加。
**失效方式**:
- 见 §5 数学论证:soft+hard dual row 的 state gradient **完全相同**(只半径常数 cpa_safe vs cpa_hard 不同),Hessian contribution 的 `∇²h = 2·I₂` 完全相同。dual row 使 Hessian block `∇h·∇hᵀ` 出现**两个相同的外积项叠加**(`2·(2I₂)ᵀ·(2I₂)` 量级),增加冗余信息但**不增加 condition number**(rank 不变,只是 singular value 重叠)。
- **但** slack 引入额外维度。soft row 的 slack cost Hessian 是 `Zl`(标量,在 slack 维度),cross-term `∇h·∂h/∂xi` = `2·[dx,dy]ᵀ`(state-slack coupling)。slack 维度增加 16 个(每 stage),整个 KKT 系统维度膨胀,EXACT Hessian 的 sparse structure 更复杂。F-05 的脆弱性会被放大。
- **关联 F-05 + SC-01/02/03**:target2500_exact/reference-feasible→acados-failure 的三个 case,本身就在 EXACT+R=0 下挂掉。nh=36 可能使其中部分 case 更难收敛(更多 slack 维度 = 更多 active-set 切换)。

### FB-A3:prefix 段 hard row relax 与 L1b D1 依赖(关联 SC-04/SC-13)
**场景**:committed prefix(k<prefix_active_k)的 hard row [18..33] 如何处理?
**失效方式**:
- 方案 A 声明 hard row "永不入 idxsh",但 prefix 段几何冻结、NLP 无法改变,若 target 移动使 prefix 段 CPA<1852,hard row 触发 HARD-infeasible。
- BL-07/SC-04 的解法是 L1b D1 witness(独立 MMG/L0 几何校验),违反→NO_SAFE_PLAN+M7。但 **L1a 范围(VR-07)只做原则层,L1a 测试限定 prefix_active_k=0**。
- 若 L1a 实施方案 A 但 L1b D1 未落地,prefix_active_k>0 场景下 hard row 会与冻结 prefix 冲突 → SC-13(prefix_K>k_head 冲突)重现。
- **关联 SC-04/SC-13**:这是 ample-time 语义在 prefix 段的核心矛盾,方案 A 不解决,只是把矛盾从"单 row bound relax"变成"dual row 都需 relax"。

### FB-A4:slack telemetry 误用导致 false-confidence(关联 SC-01/02)
**场景**:slack telemetry(soft row 的 xi 值)被解读为"soft 违背量的精确量化"。
**失效方式**:
- slack xi 是 NLP 内部变量,其值依赖 Zl/zl 权重。**低 Zl 时 xi 偏大(solver 倾向用 slack),高 Zl 时 xi 偏小(solver 倾向违反 barrier)**。xi 不是物理违背量的无偏估计。
- 若 GNC/L4 把 xi 当作"COLREGs soft 违背严重度"指标做降级决策(如 xi>threshold → BC-MPC dispatch),会因为权重调参而漂移。
- **slack telemetry 有用场景**:同 Zl 下跨 cycle 的相对趋势(xi 增大 = soft 越来越紧)可用作 early warning;**slack telemetry 无用场景**:绝对值跨场景对比、与 J_colreg cost 直接换算、作为物理 CPA 违背米数。
- **关联 SC-01/02**:reference-feasible case 的 slack 值需与 IPOPT σ 行为对照,不能直接当 ground truth。

### FB-A5:hard+soft row 索引错位静默失效(关联 SC-01/02)
**场景**:实现者改 nh 但漏改 `kRowDirection`/`kRowMinAlt`/`static_assert`。
**失效方式**:`static_assert(kRowMinAlt == kAcadosNh - 1)`(line 81)会捕获 nh=20→36 后 kRowMinAlt 未改的情况,**编译期 fail**。但若实现者改了常量却漏改 codegen 的 vertcat 顺序,或漏改 wrapper 的 idxsh set,solver 会**静默 harden 错误的 row**(direction row 被当 CPA hard,或反之)。Codex 评审明确列此为 SC-01/02 失效形态。
- **关联 SC-01/02**:dimension hash 三方校验(header/JSON/.so)是唯一防线。

## 5. 实现风险

### conditioning 数学论证(回答关键争议点 3)

设 soft row 残差 `h_s = dx²+dy²-r_s²`(r_s=cpa_safe=2500),hard row 残差 `h_h = dx²+dy²-r_h²`(r_h=cpa_hard=1852)。

- ∇h_s = ∇h_h = `[2dx, 2dy, 0, 0, 0]`(对 state [px,py,psi,r,u])——**完全相同**。
- ∇²h_s = ∇²h_h = `diag(2,2,0,0,0)` ——**完全相同**。
- 首 QP 的 constraint Hessian contribution(对拉格朗日):`λ_s·∇²h_s + λ_h·∇²h_h = (λ_s+λ_h)·diag(2,2,0,0,0)`。
- 关键:dual row 的二阶贡献**线性叠加**,不引入新方向(rank 仍为 2),不增加 condition number。**但** `λ_s` 和 `λ_h` 的相对量级会影响 KKT 系统的 scaling。若 `λ_h >> λ_s`(hard row binding,soft row slack-active),整体 Hessian 在 px/py 方向的权重变大,但 condition number(最大/最小特征值)**理论上不变**,因为最小特征值仍由其他 row(direction/min_alt 的 0 贡献)和 slack 维度的 Zl 决定。
- **结论**:dual row 对首 QP Hessian 的**结构影响是良性的**(rank 不变,只增加冗余),但**scaling 影响是恶性的**(m² 量级残差使 λ 量级偏大,与 J_colreg 的 exp barrier cost Hessian 量级 O(1) 不匹配,需权重重平衡)。
- **关联 F-05**:EXACT+R=0 脆弱性的根因是无 regularization。nh=36 不直接使 condition number 翻倍,但**增加 active-set 切换频率**(soft/hard row 各自的 active 状态组合 = 2^16 × 2^16 vs 单 row 的 2^16),使 SQP 收敛路径更复杂,间接放大 F-05 风险。

### 量化返工概率

| 风险来源 | 返工概率 | 理由 |
|---|---|---|
| nh 翻倍触发全链路索引重算 | **中** | `static_assert` 捕获编译期错误,但 codegen/wrapper/constraints_satisfied_ 三处需手工同步,dimension hash GATE 是防线。漏改一处 = silent wrong row harden。ZCode 评审已在 DP-01×DP-08 耦合矩阵列出此风险。 |
| soft row + J_colreg 权重协调 | **高** | double-expression 在文献中是 known-hard。slack 量级 m²(~1e6-1e7)与 cost 量级 O(1) 不匹配,必须归一化残差或调 Zl。Zl 调参会引发 exact-penalty vs soft-aspiration 的语义漂移(Zl 太大 → soft 变近似 hard,重现 ALT-02;Zl 太小 → slack 失控)。Codex 评审 §DP-01 明列此项。 |
| 未来移除 J_colreg 的返工成本 | **中** | 若决定 soft row 已足够表达 2500 aspiration,移除 J_colreg 需删 `build_colreg_cost_`(~60 行)+ cost 权重重平衡 + ample-time 语义重新论证(见下)。但此时 nh=36 已冻结,soft row 保留,改动局部。 |
| ample-time 语义弱化(若移除 J_colreg) | **高(条件性)** | J_colreg 的 exp barrier 提供**渐进推开**(d 越小 cost 越大,无硬阈值),对应 Rule 8 ample-time。soft row 的 slack cost(quadratic)只提供**硬阈值后的惩罚**(d<2500 才有 xi>0),无渐进性。移除 J_colreg 后 ample-time 语义退化为"2500 处硬切换",与 Rule 16 early/substantial 冲突。这是 ample-time 语义对 J_colreg 的**强依赖**。 |

## 6. 可测性(关联 SC-NN)

### T-A1:slack telemetry 验证(SC-08 距离扫点)
**场景**:固定 single target,扫点 d = 1851.9 / 1852 / 2000 / 2500 / 2500.1m,记录 soft row slack xi、hard row residual h_h、J_colreg cost。
**期望**:
- d=1851.9:hard row h_h<0 应触发 HARD-infeasible(若 k>=cpa_hard_from_k);xi 不参与。
- d=1852:h_h=0 边界;hard-feasible。
- d=2000:hard-feasible;soft row h_s=2000²-2500²<0,xi=0(soft row satisfied);J_colreg active。
- d=2500:h_s=0 边界;xi=0。
- d=2500.1:h_s>0;**xi>0**(soft row violated,slack 吸收);J_colreg 大幅 active。
**证伪**:若 d=2000 时 xi>0(soft row 不应被违反),或 d=2500.1 时 hard row 触发(误把 soft 违背当 hard 违背),方案 A 的 row 布局错。

### T-A2:double-expression 一致性测试(1852<d<2500 区间)
**场景**:扫点 d=1852..2500 每 50m,记录 soft row 的 `xi` 和 J_colreg 的 cost 值,验证两者**单调一致**(d 减小 → xi 增 + cost 增)。
**期望**:两者单调相关,但**不要求线性**(slack 是 quadratic cost、barrier 是 exp cost)。
**证伪**:若 xi 和 cost 出现反向变化(某区间 slack 增但 cost 减),说明权重协调失败,double-expression 互相抵消,方案 A 失效。

### T-A3:conditioning 测试(EXACT Hessian eigenvalue,nh=20 vs nh=36)
**场景**:对 target2500_exact case(SC-01),dump 首 QP 的 KKT 矩阵,计算 eigenvalue spread(λ_max/λ_min)。
**期望**:nh=36 的 condition number **不超过 nh=20 的 2x**(rank 不变论证,见 §5)。
**证伪**:若 condition number 恶化 >10x,说明 dual row 的 scaling 问题比理论预期严重,方案 A 与 F-05 EXACT+R=0 不兼容,需切 GAUSS-NEWTON 或加 R>0。

### T-A4:hard-never-in-idxsh adversarial(BL-A T2/ZCode T-DP01-1)
**场景**:设 Zl=zl=0(adversarial slack 权重),target d=1800(<1852),验证 hard row [18..33] 不被 slack 软化。
**期望**:solver 返回 Infeasible(hard floor 触发),不返回 Converged with slack。
**证伪**:若 solver 返回 Converged 且 hard row slack>0,说明 hard row 误入 idxsh,方案 A 静默退化为 ALT-02。

### T-A5:三 case 回归(SC-01/02/03)
**场景**:target2500_exact / rule14_ho_5000_ab_canonical / rule14_ho_live_dispatch_749728000002 三个 reference-feasible case。
**期望**:方案 A 下三 case 均 Converged 且 CPA hard floor 满足(k>=cpa_hard_from_k 段)。
**证伪**:任一 case 从 reference-feasible 退化为 acados-failure,说明 nh=36 + double-expression 破坏现有可行性。

### T-A6:dimension hash 三方校验(BL-05)
**场景**:codegen 后比对 header `M5_MID_MPC_ACADOS_NH=36` / JSON `nh=36` / `.so` 实际 row 数。
**期望**:三方一致,kRowDirection=34/kRowMinAlt=35,idxsh=[2..17](soft block),hard block [18..33] 不在 idxsh。
**证伪**:任一方不一致 = 索引重算漏改。

## 7. 推荐度

**★★★☆☆**(3/5)

**加分**:
- slack telemetry 在相对趋势场景(同 Zl 跨 cycle)有诊断价值,对 BC-MPC dispatch 的 early warning 有用。
- dual row 使 hard/soft 语义在 NLP 层**显式分离**(hard row 不依赖 bound relax schedule,语义比 IPOPT suffix-hard 更清晰)。
- hard-never-in-idxsh 原则在 dual row 下实现路径直接(hard row 索引固定,排除 idxsh 即可)。

**减分**:
- **无工程先例**(IPOPT 不采用、acados 上游无 example),是原创建模。
- **double-expression 权重协调返工概率高**(slack 量级 ~1e6-1e7 vs cost 量级 O(1)),需残差归一化或 Zl 精调,exact-penalty vs soft-aspiration 语义漂移风险。
- **返工面 2-3x 于方案 B**(250-400 行 vs 100-150 行,4 文件 vs 2-3 文件)。
- **conditioning scaling 风险**(虽 rank 不变,但 m² 量级残差使 λ 量级与 cost Hessian 不匹配,放大 F-05)。
- **ample-time 语义对 J_colreg 强依赖**(soft row 的 quadratic slack 无渐进性,移除 J_colreg 会退化 Rule 16 early/substantial)。
- **来源单薄**(几乎仅 [R23] ZCode 单评审支撑,[R19] BL-A 被选择性解读,[R24] Codex 反对)。

**关键不确定性**(主 agent 裁决时需权衡):
1. double-expression 是否真的需要?slack telemetry 的工程价值是否 > 权重协调成本?(ZCode vs Codex 核心分歧)
2. nh=36 的 conditioning scaling 问题是否可控?(需 T-A3 实测)
3. ample-time 语义是否接受退化为"2500 硬切换"?(若移除 J_colreg)
