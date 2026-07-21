# Step5 方案 B 深化 · DP-01 nh=20+nsh=0+J_colreg(ALT-09)

> **深化 agent**: `tdl_m5_planner_engineer`(read-only)
> **agent id**: agent_8ae45f72-3e4e-4800-8bc1-45c5111a0385
> **对比对象**: DP-01 nh 抉择(BL-09)
> **方案**: B(ALT-09,Codex 评审首选)
> **推荐度**: ★★★★☆(4/5)——**Step5 最终采纳方案**
> **状态**: DONE_WITH_CONCERNS
> **workspace writes**: none
> **完成时间**: 2026-07-20 17:35(后台异步,440s)

---

## 方案 B 定义

nh=20,nsh=0:[0,1] prefix / [2..17] CPA hard(16 targets,residual = dx²+dy²-cpa_hard_m²,cpa_hard_m=1852 固定,**永不入 idxsh,因 nsh=0**)/ [18] direction / [19] min_alt。**soft 2500 aspiration 仅由 J_colreg cost barrier 表达**(J_colreg 用 kGIdxCpaSafe,cpa_safe∈{1852,2500})。无 soft row,无 slack。

## 1. 来源(证据溯源)

| ID | 来源类型 | 引用 | 对方案 B 的支撑 |
|----|----------|------|-----------------|
| [R1] | PROJECT_FACT | `types.hpp:120-132` —— `cpa_hard_m=1852.0`(hard floor,固定)+ `cpa_safe_m`(soft,conflict 时 node bump 至 2500)字段已定义,语义分离原则已落地 | **直接支撑**:方案 B 的 hard/soft 数值分离已在数据层存在 |
| [R2] | PROJECT_FACT | `mid_mpc_node.cpp:787-795` —— node 实际逻辑:`cpa_safe=2500`(conflict)/`1852`(no conflict);`cpa_hard_m` 始终 1852 固定写入 | **直接支撑**:方案 B 的"hard row 用 cpa_hard_m=1852 / soft 用 cpa_safe(bumped)"正是 node 现状意图 |
| [R3] | PROJECT_FACT | `mid_mpc_acados_formulation.cpp:333,340,374` + `gen_mid_mpc_acados.py:315,323` —— **当前 acados graph CPA row 用 `cpa_safe`(被 bump 至 2500),不是 cpa_hard_m;J_colreg 也用 cpa_safe** | **方案 B 必须修改的 gap**:CPA row residual 需从 cpa_safe 改为 cpa_hard_m(slot 待新增) |
| [R25] | acados 源码 | `bgh.c:1411-1418`(Codex 引 exact commit `5c98c317`)+ `cost_external.c:762-772` + `common.c:2645-2670` | **方案 B 的 nsh=0 数学基础**:nsh=0 时无 slack 变量,residual 不减 slack,CPA row 天然 true hard;且 `common.c` 每次 solve re-initialize 保证 nsh=0 永久生效 |
| [R21] | acados 源码 | `bgh.c:1411-1417` slack 数学(row 进 idxsh 永远 soft) | **方案 B 反向支撑**:正因为 idxsh 会软化,方案 B 选 nsh=0 完全规避此风险(无需 runtime idxsh 重发) |
| [R26] | PROJECT_FACT | `constraint_compiler.cpp:247-298`(IPOPT CPA expression)+ generated header `acados_solver_m5_mid_mpc_acados.h:42-69` | **方案 B parity 不对称的来源**:IPOPT CPA expression 可选加全局 σ scalar(`row = row + slack`,line 290-292),非 true hard;acados 方案 B 是 true hard |
| [R27] | DOMAIN_EVIDENCE | IMO COLREGs Rule 8(positive/ample-time/safe distance)+ Rule 16(early/substantial) | **方案 B ample-time 风险的评估依据**:soft 2500 仅靠 cost barrier 表达,必须验证 barrier 是否能驱动 early/substantial action |

**评审归属**:
- **Codex 评审 [R24] 首选方案 B**:明确"首选 `nh=20,nsh=0 + J_colreg(cpa_safe)`;`nh=36` 仅在明确需要独立 soft-row 语义后采用"。理由:简洁、回归面小、避免双 row 重复表达 2500 + 量纲大的 m² slack。
- **ZCode 评审 [R23] 反对方案 B、推方案 A(nh=36)**:"nh=36 + 硬 row 永不入 idxsh + wrapper runtime 重发 idxsh,这三点强制必做"。ZCode 未独立评估 nsh=0 路径,默认接受 nh=36 双 row。

**分歧本质**:Codex 认为 2500 aspiration 由现有 J_colreg cost 表达已足够(避免重复);ZCode 认为 slack telemetry(row 层)有独立价值,不可由 cost 替代。这是 Step5 DESIGN-IT-TWICE 的核心争议。

## 2. 工程验证

### 2.1 IPOPT 现状是否就是方案 B 的结构?(关键问题)

**部分是,但有关键不对称**。

**结构相似面(IPOPT 当前形态接近方案 B)**:
- IPOPT **hard row**:`compile_cpa_distance`(`constraint_compiler.cpp:247-298`)residual = `dx²+dy²-cpa_safe_sq`,其中 **`cpa_safe_sq = inputs.cpa_hard_m²`(line 262)**,即 IPOPT CPA row residual 已经用 cpa_hard_m(1852),不是 bumped cpa_safe(2500)。这正是 Bug C deep RC-C 修复(line 258-261 注释)。
- IPOPT **soft cost**:`build_colreg_cost_`(`mid_mpc_nlp_formulation.cpp:346-389`)用 `kIdxCpaSafe`(line 350),即 bumped 2500。
- IPOPT **soft row(IPOPT 无独立 soft row)**:IPOPT 用 σ 全局标量 `slack`(line 290-292,可选)而非独立 nh row。当 slack 为空(legacy hard-only)时,IPOPT 结构 = "hard row + soft cost 无 soft row",**这就是方案 B 的结构**。
- **结论**:IPOPT 在 slack 为空时的形态 ≈ 方案 B 的 acados 镜像。parity 修复最直接。

**关键不对称(必须处理)**:
- **(a) IPOPT σ conditional 非 true hard(BL-15)**:当 slack 非空时,`compile_cpa_distance` 在每个 CPA row 加全局 σ(line 290-292 `row = row + slack`),σ 是 NLP 决策变量 → IPOPT CPA row 在 slack 激活时**结构上 soft**(slack 可吸收违反)。**方案 B 的 acados CPA row 是 true hard**(nsh=0,排除 idxsh),比 IPOPT 严格。
- **(b) IPOPT cpa_hard_from_k suffix-hard schedule**(`mid_mpc_solver.cpp:556-620`):IPOPT 在 `k<cpa_hard_from_k` 的 CPA row 通过 lbg/ubg 双向 [-inf,+inf] 放宽(`row_registry.hpp:318-328 apply_cpa_suffix_hard_`),这是**bounds-based relax,不是 slack**。方案 B 若不实施 cpa_hard_from_k schedule,所有 CPA row 在所有 stage 都 hard,可能在船尚未物理脱离 1852 时制造假 infeasible(Codex 评审 line 47 明确指出)。
- **(c) IPOPT prefix relax**:`apply_colreg_prefix_soften_`(`row_registry.hpp:256-270`)在 k<K 段把 CPA row 双向放宽,但 σ 仍加到这些 row(Q4 fail-open,GNC 评审 [R13] Q4)。方案 B 在 L1a 测试范围限定 prefix_active_k=0(VR-07 第 8 条),此问题延后 L1b。

### 2.2 开源项目中的类似结构

- **acados 官方 example(nav2/mpc 类)**:典型 MPC 用 nh(constraint) + cost,极少用 nsh(slack)。slack 主要用于 soft constraint 显式建模。方案 B 的"hard row + soft cost 无 slack"是 acados 主流形态。
- **CasADi/acados template**:nsh=0 是默认值,文档示例多数 nsh=0。
- **结论**:方案 B 的结构在 acados 生态中是主流,工程验证度高。

## 3. 技术分解(逐子模块打钩)

- [x] **hard row 表达(cpa_hard_m,排除 idxsh,nsh=0 天然排除)**:可行。CPA row residual 改用 `kGIdxCpaHard`(新增 global slot,值 = cpa_hard_m = 1852 固定)。nsh=0 时 idxsh 概念上无意义(acatos 不分配 slack 变量),无需 runtime idxsh 重发。**但**:codegen `gen_mid_mpc_acados.py:510 idxsh = np.arange(NT)+2` 必须删除(否则 nsh=0 与 idxsh 矛盾,codegen 应报错或行为未定义)。
- [x] **J_colreg cost barrier(用 cpa_safe)保留不变**:可行。`mid_mpc_acados_formulation.cpp:370-428` 现有 build_colreg_cost_ 用 `kGIdxCpaSafe`(line 374),conflict 时被 node bump 至 2500(`mid_mpc_node.cpp:789`)。方案 B 完全保留此 cost,**这是方案 B 表达 2500 aspiration 的唯一机制**。
- [ ] **kGIdxCpaHard global slot 新增**:**必须新增**。当前 `mid_mpc_acados_formulation.cpp:60-75` 无 kGIdxCpaHard(BL-08)。新增需:`formulation.cpp` 加 `constexpr int32_t kGIdxCpaHard = 26`(+1 slot);`gen_mid_mpc_acados.py` G_CPA_HARD 镜像;`pack_parameters` 写 `g[kGIdxCpaHard] = input.constraints.cpa_hard_m`;`np_global` 154→155;三方 hash(header/JSON/.so)重建。**注意**:kGIdxTargets 当前 = 26(`formulation.cpp:82`),若 kGIdxCpaHard 占 26 则 kGIdxTargets 后移至 27,target block offset 全变。或放 slot 27(target block 不动)。需 codegen 三方校验。
- [x] **codegen nh 保持 20**:可行。nh 维度不变,只改 CPA row residual 表达式(从 cpa_safe 到 cpa_hard)。**但**:`gen_mid_mpc_acados.py:510 idxsh = np.arange(NT)+2` 必须删除 + `NSH` 改为 0 + `NS` 改为 0(generated header line 49,58 `NSH=16,NS=16` → `NSH=0,NS=0`)。
- [x] **wrapper runtime idxsh 排除(nsh=0 天然满足)**:可行。nsh=0 时 acatos 不分配 slack 数组,wrapper 无需调用 idxsh/Zl runtime set(当前 wrapper 也无此调用,ZCode 评审 line 23 确认 0 命中)。**这是方案 B 相对方案 A 的最大工程优势**:无需 BL-A 路径 A 的 runtime idxsh 重发逻辑。
- [x] **row index 不变(direction=18,min_alt=19)**:可行。nh=20 不变,row 布局不变(direction=2+NT=18,min_alt=19)。无索引重算风险。
- [ ] **h_fn cache rebuild(CPA row residual 变更)**:**必须重建**。`mid_mpc_acados_solver.cpp:555-568` h_fn cache 从 `formulation_.con_h_expr()` 构建,CPA row residual 改用 cpa_hard 后 MX graph 变,h_fn 自动重建(每次首次 use 重建)。**但**:`constraints_satisfied_`(line 546-628)的 CPA row 检查逻辑(line 533 注释 "CPA(2..17) >= 0, softened via idxsh -> h + xi >= -kBoxTol")需改为 "CPA(2..17) >= 0 hard -> h >= -kBoxTol"(无 xi)。这是 L4 适配项(见下)。
- [ ] **L4 constraints_satisfied_ 适配**:**必须改**。`mid_mpc_acados_solver.cpp:533,575-598` 当前读 `sl_vec`(slack,line 597)并做 `h + xi >= -kBoxTol` 检查。nsh=0 时无 slack vector,`ocp_nlp_out_get(... "sl" ...)` 行为需验证(可能返回空或报错)。L4 检查改为纯 `h >= -kBoxTol`(hard)。**关键 concern**:失去 slack 后,L4 无法看到"soft aspiration(d<2500)违反程度",只能看"hard(d<1852)是否违反"。这是争议点 #2 的核心。

## 4. 失效边界(关联 SC-NN)

### FB-1:J_colreg barrier 不足以驱动 solver 保持 d≥2500(SC-08,**高优先级**)

**数学分析(已用实际参数计算)**:
- barrier = `exp(-ζ·(d-cpa_safe))`,ζ=5e-3([formulation.cpp:88]`kZeta`),cpa_safe=2500(conflict bumped)。
- d=2500:barrier=1.0,w_colreg·barrier=30.0(每 target-step,tw=1,disc=1)。
- d=2000:barrier=12.18,w_colreg·barrier=**365.5**。
- d=1852(hard floor):barrier=25.53,w_colreg·barrier=**766.0**。
- d=3000:barrier=0.082,w_colreg·barrier=2.46。
- **barrier 在 1852< d <2500 区间增长率**:dJ/dd 从 -1.83(d=2000)到 -3.83(d=1852),即每靠近 1m 增加约 2-4 单位成本(每 target-step)。cumulative over N=80 horizon with TCPA discount(T_disc=100s):d=1852 全 horizon cost ≈ 5499,d=2500 全 horizon cost ≈ 215,**cost saving ≈ 5284**(若 solver 从 1852 移到 2500)。
- **结论**:**barrier 在 1852-2500 区间梯度足够陡**(25x ratio),理论上能驱动 solver 主动保持 d≥2500,**前提是 J_dist/J_route 的反向 pull 不超过此梯度**。但:
  - J_dist = (psi-bearing)²,典型值 O(1) rad² → w_dist·J_dist ≈ 10。
  - J_route = w_guard·huber(l)/l_scale²,典型 l~100m,l_scale=400 → w_route·J_route ≈ 3·(100/400)² ≈ 0.19。
  - **w_colreg·J_colreg(766 at d=1852)远大于 w_dist+w_route(~10)**,dominance 成立。
- **失效场景**:当船必须大幅转向(如 Rule 14 head-on 需 30°+ 改向)才能维持 d≥2500,而 J_dist 累积(偏离 planned bearing 几 rad²)。若 J_dist 累积 > 5284(w_colreg·J_colreg saving),solver 可能在 d≈2000 停留(部分违反 2500 aspiration 但未触 hard 1852 floor)。**这正是 ample-time 语义的风险**:solver 可能"勉强合规"(d≈1852-2000)而非"early/substantial"(d≥2500)。关联 Rule 8/16(Codex评审 line 50 引 IMO/MAIB)。
- **SC-08 验证点**:距离扫点 1851.9/1852/2000/2500/2500.1,验证 solver 解出的 d 是否主动趋向 ≥2500,还是停在 1852<d<2500。

### FB-2:无 slack telemetry 导致 L4/M7 transparency 下降(SC-01/02,**中-高优先级**)

**失效形态**:
- 当前 L4 `constraints_satisfied_`(line 546-628)读 `sl_vec`(slack vector)判断 CPA row 是否被违反 + 违反程度。slack xi>0 表示该 row 被软化吸收了多少 m²违反。
- 方案 B nsh=0 后,slack vector 不存在。L4 只能判断 `h >= -kBoxTol`(hard 1852 是否违反),**无法判断 soft 2500 aspiration 是否违反**。
- **场景**:solver 解出 d=2000m(soft 2500 违反 500m,hard 1852 未违反)。L4 raw status = Converged,constraints_satisfied_ = true(hard 未违反)。L4 输出"可执行",但 M7 安全监督看不到"soft aspiration 违反 500m"这一信息。
- **对 DP-05(raw status fail-closed)影响**:DP-05 只看 raw status(0..7),不看 soft 违反。方案 B 不影响 DP-05 fail-closed 逻辑(hard 违反仍 fail-closed)。但 DP-05 的"解复核层"(独立于 raw status)失去 soft 违反信号。
- **对 M7 影响**:M7 安全监督若依赖 slack telemetry 判断"degradation 程度",方案 B 需 M7 改读 J_colreg cost value(或其他 proxy)。**补救成本**:L4 新增"soft aspiration 检查"(读 d_min 与 cpa_safe 比较,输出 violation_m telemetry),但这需要 L4 重新计算 d_min(已可从 h_val 推出 sqrt(dx²+dy²))。补救成本中等。

### FB-3:nsh=0 约束未来 row 软化需求(SC-13 prefix relax,**中优先级**)

**失效形态**:
- 方案 B 设 nsh=0,acatos 完全无 slack 机制。未来若某 row 需软化:
  - **SC-13 prefix relax**(L1b DP-04/07):prefix 段(k<prefix_active_k)几何冻结,target 移入冻结几何时 CPA row 需放宽。方案 B 的处理 = lh/uh 改 [-inf,+inf](double-disabled,IPOPT 现状 `row_registry.hpp:256-270 apply_colreg_prefix_soften_` 就是用 bounds 而非 slack)。**这条路可行**,但完全放宽(无中间态)而非"软化"(slack 允许小违反)。
  - **direction k=0 soften**(L1b BL-06):direction row 在 k=0 是初始条件(NLP 不可动),需放宽。同上,bounds [-inf,+inf] 可处理。
  - **若未来需要"部分软化"(如 CPA row 允许 100m 违反而非完全放宽)**:方案 B 必须恢复 nsh>0,与方案 A 路径合并。这是方案 B 的"未来锁定"风险。
- **对 L1b 影响**:L1b 的 DP-04(prefix CPA NO_SAFE_PLAN+M7,VR-04 路径 A+D1)依赖 D1 witness(committed prefix 几何冻结),不依赖 slack。方案 B 兼容 VR-04。但若 L1b 发现某场景需"软"prefix relax(非完全放宽),方案 B 需返工。

### FB-4:hard floor true-hard 与 suffix-hard schedule 缺失冲突(SC-03,**高优先级**)

**失效形态**:
- 方案 B 所有 CPA row 在所有 stage(除 prefix_active_k 段)都 true hard(1852)。**但** IPOPT 有 `cpa_hard_from_k` schedule(`mid_mpc_solver.cpp:556-620`):在 k<cpa_hard_from_k 段 CPA row 软化(给船物理脱离 1852 的时间)。
- **场景 SC-03**(rule14_ho_live_dispatch,x0≈0°,box=23.2°..53.2°):船当前 heading 在 box 外,需 k_head stage 才能进入 box。在 k<k_head 段,船 heading 未转,CPA 可能 <1852。方案 B 若无 cpa_hard_from_k schedule,这些 stage 的 CPA hard row 触发 infeasible。
- **根因**:CPA × heading 耦合(ZCode 评审 line 56-58 "DP-01 hard CPA row × DP-08 heading schedule")。IPOPT 用 cpa_hard_from_k 解耦,acados 方案 B 在 L1a 不实施此 schedule(VR-07 L1b 范围)。**方案 B 在 L1a 测试范围必须限定为 cpa_hard_from_k=0(prefix_active_k=0,无 heading schedule)的场景**(VR-07 第 8 条已声明),否则假 infeasible。
- **对 Step5 抉择影响**:此 FB 不是方案 B 独有(方案 A nh=36 同样有此问题),但方案 B 因 nsh=0 无 slack 缓冲,失败更"硬"(直接 infeasible,无 slack 吸收)。

### FB-5(附加):continuous/swept CPA(SC-07,**中优先级,Codex 独有发现**)

- 方案 B 的 CPA row 是 node-only(stage k 的点 CPA),非连续线段 CPA。两个节点间 swept CPA 可能 <1852 而 node 都 ≥1852。方案 B 不解决此问题(方案 A 也不解决)。这是 BL-11 LX 候选,独立于 nh 抉择。

## 5. 实现风险

### 5.1 返工概率量化

| 子项 | 返工概率 | 风险来源 |
|------|----------|----------|
| nh 不变,nsh=0 | **低** | 维度不变,无索引重算;idxsh 删除是 codegen 局部改 |
| CPA row residual cpa_safe→cpa_hard | **低-中** | MX graph + SX gen 双改(formulation.cpp:340 + gen.py:323),h_fn 自动重建;但 kGIdxCpaHard slot 新增触发 np_global 154→155 + target block offset 决策(slot 26 vs 27) |
| J_colreg 是否需加强 barrier | **中** | 当前 barrier 在 1852-2500 区间梯度足够陡(25x ratio,见 FB-1 数学分析),理论上不需加强。但 ample-time 语义(Rule 8/16)要求 solver "主动"保持 d≥2500,若实测 solver 在 d≈2000 停留,需调 ζ(steepness)或 w_colreg。这是经验调参风险,非结构性返工 |
| L4 失去 slack telemetry 补救 | **中** | L4 constraints_satisfied_ 需改(去 sl_vec 读,改纯 h>=-kBoxTol)。补救"soft aspiration telemetry"需 L4 新增 d_min 计算 + violation_m 输出。补救成本中等(约 1-2 天) |
| nsh=0 未来 row 软化锁定 | **低**(短期)/ **中**(长期) | L1b DP-04/07 用 bounds double-disabled 可处理(无需 slack)。若未来需"部分软化",需恢复 nsh>0 |
| IPOPT σ parity 不对称 | **中** | 方案 B acados true hard vs IPOPT σ conditional 非 true hard。parity 测试需重新定义(VR-08),或同步修 IPOPT σ(延后 C1b Q4) |

**整体返工概率:中**。主要风险集中在 (a) J_colreg barrier 是否足够驱动 ample-time(经验调参),(b) L4 slack telemetry 补救,(c) IPOPT parity 不对称处理。

### 5.2 与方案 A(nh=36)对比的返工面

- 方案 B nh 不变(nh=20),方案 A nh=20→36。方案 B 回归面**显著小于**方案 A。
- 方案 B 无 runtime idxsh 重发(方案 A 必须 BL-A 路径 A 的 runtime idxsh 逻辑)。方案 B 工程量**显著小于**方案 A。
- 方案 B 失去 slack telemetry(方案 A 保留)。方案 B L4 补救成本**中等**,方案 A 无此成本。
- **总结**:方案 B 工程量小但 L4/telemetry 补救 + ample-time 调参风险;方案 A 工程量大但 telemetry 完整。

## 6. 可测性(关联 SC-NN)

### T-B1:J_colreg barrier 驱动能力测试(SC-08,**关键**)
- **目标**:证明/证伪 barrier 是否驱动 solver 主动保持 d≥2500。
- **方法**:距离扫点 fixture(target 在 d=1851.9/1852/2000/2200/2400/2500/2500.1 各位置,solver 从 straight-line 初始解出发)。记录 solver 收敛后的 min CPA over horizon。
- **期望(证明 barrier 足够)**:target 在 d=2000(soft 2500 违反)时,solver 主动改向使 min CPA ≥ 2500(或接近),而非停在 2000。
- **期望(证伪 barrier 不足)**:solver 收敛后 min CPA ≈ 1852-2000(勉强合规),未主动趋向 2500。此时需调 ζ 或 w_colreg。
- **关联**:FB-1,SC-08,Codex评审 line 58 "SC-08 1852<d<2500 应 hard-feasible、soft-cost active"。

### T-B2:hard floor true-hard 验证(adversarial,**关键**)
- **目标**:证明 CPA row 是 true hard(nsh=0,无 slack 吸收)。
- **方法**:adversarial fixture,target 在 d=1851.9(<cpa_hard 1852),solver 必须拒绝(NumericalFailure/infeasible)或改向使 d≥1852。**绝不可**收敛到 d=1851.9。
- **关联**:BL-A T3,ZCode T-DP01-1,Codex评审 line 77 "adversarial slack:令 Z/z=0,hard-active 节点 d<1852 仍不得接受"。

### T-B3:ample-time 语义验证(Rule 8/16,**关键**)
- **目标**:证明 solver 解出的轨迹是"early/substantial"避让,而非"最后一刻勉强合规"。
- **方法**:rule14_ho_5000 fixture(target 5000m,CPA 收敛中),记录 solver 解出的 heading 改向时机 + 幅度。期望:改向在 horizon 早期(k<10)、幅度 substantial(>15°),使 CPA 尽早 ≥2500。
- **期望(证明 ample-time)**:改向 early + substantial,min CPA over horizon ≥ 2500(soft aspiration met)。
- **期望(证伪 ample-time)**:改向 late(k>30)、幅度 minimal,min CPA ≈ 1852-2000(勉强合规)。关联 [R27] IMO Rule 8/16。
- **关联**:FB-1,FB-4,SC-02/03,BL-12。

### T-B4:L4 transparency 验证(无 slack,**关键**)
- **目标**:证明 L4 在无 slack 时仍能正确判断 hard 违反 + 提供 soft 违反 telemetry。
- **方法**:solver 解出 d=2000(soft 违反,hard 未违反)。L4 constraints_satisfied_ 必须返回 true(hard 未违反),但 telemetry 输出 "soft_aspiration_violation_m = 500"。
- **期望(证明补救有效)**:L4 raw status + soft violation telemetry 都可用。
- **期望(证伪补救)**:L4 只输出 "satisfied=true",无 soft violation 信号,M7 看不到 degradation。
- **关联**:FB-2,DP-05,Codex评审 line 71 "soft-row 业务裁决"。

### T-B5:维度 hash 三方校验(BL-05)
- **目标**:nh=20/nsh=0 落地后,header/JSON/.so 三方 nh/nsh/np_global 一致。
- **方法**:codegen 后比对 `acados_solver_m5_mid_mpc_acados.h`(NH=20,NSH=0,NS=0,NP_GLOBAL 更新)+ JSON config + .so symbol。
- **关联**:BL-05,BL-08,Codex评审 line 76 "generated contract:MX/SX row 同构;header/JSON/.so nh/nsh 一致"。

### T-B6:参数隔离测试(Codex评审 line 78)
- **目标**:证明 hard row 只读 cpa_hard,soft cost 只读 cpa_safe,互不污染。
- **方法**:只变 cpa_safe(1852↔2500),hard residual 不变;只变 cpa_hard(1852↔2000),soft cost 不变。
- **关联**:Codex评审 line 78,FB-1。

### T-B7:三冻结 case 回归(SC-01/02/03)
- **目标**:target2500_exact / rule14_ho_5000_ab_canonical / rule14_ho_live_dispatch 三 case 在方案 B 下从 ACADOS_FAILURE 转 Converged(REFERENCE_FEASIBLE 对齐)。
- **关联**:SC-01/02/03,[R10] runs/m5_solver_diag 三 case。

## 7. 推荐度

**★★★★☆(四星)**

**理由**:

**加分项**:
1. **回归面最小**:nh 不变(20),无索引重算,无 runtime idxsh 重发逻辑(方案 A 必须)。工程量显著小于方案 A。
2. **nsh=0 天然规避 idxsh 风险**:方案 B 完全不需要 BL-A 路径 A 的 runtime idxsh/Zl 重发(acatos `common.c` 每次 solve re-initialize,nsh=0 永久生效)。这是方案 B 相对方案 A 的最大结构性优势。
3. **IPOPT parity 最直接**:IPOPT 在 slack 为空时的形态 ≈ 方案 B 结构(hard row 用 cpa_hard_m + soft cost 用 cpa_safe,无独立 soft row)。parity 修复路径最短。
4. **Codex 评审首选**:Codex 明确推方案 B,理由(简洁 + 避免双 row 重复表达 2500 + m² slack 量级大)技术成立。
5. **J_colreg barrier 数学上足够陡**(已验证):25x ratio 在 1852-2500 区间,dominance over J_dist/J_route 成立。ample-time 在理论上有支撑。

**减分项**:
1. **失去 slack telemetry**(FB-2):L4/M7 看不到 soft aspiration 违反程度。补救需 L4 新增 d_min + violation_m 计算,成本中等。这是方案 A 的核心优势点。
2. **ample-time 完全依赖 cost barrier**(FB-1):理论 dominance 成立,但实测可能 solver 在 d≈2000 停留(勉强合规)。需 T-B1/T-B3 验证 + 可能调 ζ/w_colreg。这是经验风险,非结构性缺陷。
3. **IPOPT σ 不对称**(BL-15):方案 B true hard vs IPOPT σ conditional。parity 需重新定义或同步修 IPOPT。
4. **nsh=0 未来锁定**(FB-3):L1b prefix/direction relax 用 bounds 可处理,但若未来需"部分软化"需恢复 nsh>0。

**与方案 A(nh=36)对比定位**:
- 方案 B = "最小工程量 + 依赖 cost barrier + 失去 slack telemetry"。
- 方案 A = "完整 slack telemetry + 大工程量 + runtime idxsh 复杂度"。
- 抉择本质:**是否信任 J_colreg cost barrier 足够表达 2500 aspiration + 是否接受失去 slack telemetry**。这是业务判断(Codex 推 B 信任 cost,ZCode 推 A 不信任 cost 需 slack 兜底),主 agent + 用户裁决。

**condition for 5th star**:若 T-B1/T-B3 实测证明 barrier 足够驱动 ample-time(solver 主动趋向 d≥2500),方案 B 升 5 星。若实测 solver 在 d≈2000 停留,方案 B 降 3 星(需恢复 nsh>0 或加强 barrier,接近方案 A)。

## 数学结论摘要(供主 agent 决策卡片)

**J_colreg barrier 在 1852-2500 区间的驱动力(已用实际参数 ζ=5e-3, w_colreg=30 计算)**:
- barrier(d=1852) = 25.53,barrier(d=2500) = 1.0 → **25.53x penalty ratio**
- cumulative horizon cost:d=1852 全 horizon ≈ 5499,d=2500 全 horizon ≈ 215 → **saving ≈ 5284**
- vs J_dist(~10) + J_route(~0.2):**w_colreg·J_colreg dominance 成立**(5284 >> 10)
- **理论上 barrier 足够驱动 solver 保持 d≥2500**,但 ample-time(early/substantial)需 T-B3 实测验证

**nsh=0 的数学保证(已验证 acatos 源码 [R25])**:
- nsh=0 → acatos 不分配 slack 变量 → CPA row residual 不减 slack → **true hard**
- 每次 solve re-initialize(`common.c:2645-2670`)→ nsh=0 永久生效,无需 runtime 维护
- **方案 B 规避了方案 A 的 runtime idxsh 重发复杂度**

**失去 slack 的代价**:
- L4 `constraints_satisfied_` 当前读 sl_vec(`mid_mpc_acados_solver.cpp:597`)判断 CPA row 违反 + 程度。nsh=0 后无 sl_vec。
- 补救:L4 改纯 h>=-kBoxTol(hard 检查)+ 新增 d_min 计算(从 h_val=sqrt(dx²+dy²)推)输出 soft violation_m telemetry。成本中等。
