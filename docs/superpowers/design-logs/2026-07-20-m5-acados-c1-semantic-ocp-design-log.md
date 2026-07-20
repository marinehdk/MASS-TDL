# 设计日志: M5 acatos C1 语义 OCP 冻结

> **模式**: 重构        **创建**: 2026-07-20
> **关联 spec**: (待 Step6 产出) `docs/superpowers/specs/2026-07-20-m5-acados-c1-semantic-ocp-solution-pack.md`
> **关联证据**: `docs/superpowers/review/2026-07-20-m5-acados-root-cause-diagnosis.md`;`runs/m5_solver_diag/4fd37fd7e9fc435656e2154d92b859920a0eb646/`
> **状态**: Step2 进行中(L1a grilling)

## 0. 决策树状态(权威索引 · 可变快照)

### 0.1 决策点注册表 [DP]
| ID | 描述 | 类型 | 父/分解 | 状态 | 详见 |
|----|------|------|---------|------|------|
| DP-01 | F-02: acados graph 表达 hard 1852 floor + soft 2500 cost 分离 | 约束 | TD-01 | **VR-01 final**(Step5 采纳方案 B:nh=20+nsh=0+J_colreg;hard row 用 cpa_hard_m 永不入 idxsh;soft 2500 由 J_colreg cost barrier 表达) | VR-01 + Step2 + Step5 |
| DP-02 | F-03: runtime heading/speed/ROT boxes 每 stage 落地 | 约束 | TD-01 | **VR-02 修订中**(Step2 grilling;box live 落地保留,新增 stage0/terminal contract + ROT 来源修正) | VR-02 + Step2 |
| DP-03 | F-04: min-alt 首次硬化 stage 用独立 MMG reachability schedule | 约束 | TD-01 | 已裁决(Step0) | VR-03 |
| DP-04 | F-04-prefix: committed prefix 违反 hard floor 返回 NO_SAFE_PLAN+M7 | 约束 | TD-01 | 已裁决(Step0) | VR-04 |
| DP-05 | F-01: status/KKT fail-closed 映射(独立于 C1,并行 C3) | 接口 | TD-01 | 已裁决(Step0,后置 C3) | VR-05 |
| DP-06 | semantic parity: acados 路径消费 IPOPT 已消费的同一批合约字段 | 架构 | TD-01 | Step1 发现 + Step2 修正(IPOPT 不是 true hard oracle,BL-15) | — |
| DP-07 | prefix 不可控段的 CPA row 处理(prefix_active_k 语义) | 约束 | TD-01 | Step1 发现 + Step2 修正(DP-01×DP-08 耦合拉 DP-07 部分内容进 L1a,见跨 DP 耦合) | — |
| DP-08 | heading box 时间语义(可达 suffix schedule) | 约束 | TD-01 | **VR-06 修订中**(Step2 grilling;k_head 公式挪 Step3/Step5 作 BL-12,L1a 只做 grid physical-time map) | VR-06 + Step2 |

### 0.2 技术分解注册表 [TD]
| ID | 技术 | 分解子模块(→DP) | 触发步骤 |
|----|------|------------------|----------|
| TD-01 | M5 Mid-MPC acados OCP 语义冻结 | 状态量(DP-06 参考) · 约束层级(DP-01/02/03/04/07/08) · 失败回退(DP-04/05) · 接口(DP-05) | Step1 |

### 0.3 盲区注册表 [BL]
| ID | 问题 | 归属决策点 | 优先级 | 调研状态 |
|----|------|-----------|--------|----------|
| BL-01 | acados graph 改 hard 1852 后,idxsh slack 是否仍只对 prefix 段激活? slack penalty 是否需调整? | DP-01 | 高 | 未闭环(BL-A 答路径 A 可行) |
| BL-02 | IPOPT `minalt_box_infeasible=true` 时全 soft 的语义,acados 如何镜像(全 relax 还是保留 soft)? | DP-03 | 高 | 未闭环 |
| BL-03 | heading box 可达 suffix 的首次 harden stage 公式与 min-alt 是否同源(box_reach vs rot_reach)? | DP-02/08 | 高 | 未闭环 |
| BL-04 | committed prefix reachability witness 用什么独立模型(MMG/lattice)?与 reference oracle 是否同源? | DP-04 | 中 | 未闭环 |
| BL-05 | acados gen 重生成后 dimension/hash 三方校验的具体清单(nx/nu/nh/nsh/np_global/np_stage/.so/header/JSON) | DP-06 | 高 | 未闭环 |
| BL-06 | direction row 是否也需独立 reachability schedule(IPOPT §4.4 已对 k=0 soften)? | DP-07 | 中 | 未闭环 |
| BL-07 | prefix 段 hard row relax 后,与 VR-04 D1 witness 的同源性(acatos A + D1 与 IPOPT σ conditional + D1 是否同一 MMG/L0 几何 witness) | DP-01/04 | 中 | 未闭环(Step2 新增) |
| BL-08 | `kGIdxCpaHard` global slot 当前不存在(nh=36 双 row 隐含新增 global enum + pack + codegen SX 镜像 + np_global 154→155 + 三方 hash 重建);无论 nh=20/36 都需新增 | DP-01 | 高 | 未闭环(Step2 ZCode 新增) |
| BL-09 | DP-01 nh=20+nsh=0+J_colreg vs nh=36 双 row 的业务抉择;两份评审分歧(Codex 推 nh=20,ZCode 推 nh=36) | DP-01 | 高 | 未闭环,**挪 Step5 DESIGN-IT-TWICE**(用户裁决 2026-07-20 17:05) |
| BL-10 | terminal `NHN=0/NBXN=0` contract 定义:terminal x_N 是否纳入 CPA/heading/speed/ROT 约束?三 DP 共同缺口 | DP-01/02/08 | 高 | 未闭环(Step2 Codex 新增) |
| BL-11 | continuous/swept CPA(SC-07):node-only row 漏掉区间穿越,15s 网格内 swept CPA < 1852 但节点都 ≥ 1852;根因 oracle 已用连续线段 CPA | DP-01 | 高 | 未闭环(Step2 Codex 新增,LX 候选) |
| BL-12 | DP-08 k_head 正式公式 + ample-time 下界 `t_latest_safe`:Codex 指出仅 reachability 不足,需 `t_reach_upper ≤ t_hard ≤ t_latest_safe` 双量 | DP-08 | 高 | 未闭环,**挪 Step3/Step5**(L1a 不实施) |
| BL-13 | IPOPT/acados grid physical-time map + off-by-one:IPOPT psi[0] 是首个可控 heading,acados stage0 是 measured x0,stage1 才是 future;所有 schedule 必须先按秒定义再映射各自 grid | DP-08 + 全 schedule | 高 | 未闭环(Step2 Codex 新增,**L1a 必须立原则**) |
| BL-14 | `r0=0` 是假设还是合法 contract?acados 把 r0 固定为 0,首步 heading/ROT reach/box feasibility 可能错误 | DP-02/08 | 中 | 未闭环(Step2 Codex 新增) |
| BL-15 | IPOPT CPA σ 全局标量(`constraint_compiler.cpp:242-266`)不是 true hard;**parity 框架重定义**:不应把 IPOPT 当 oracle,两路径共同对齐到语义正确 OCP 规范 | DP-01/06 | 高 | 未闭环(Step2 Codex 新增,与 Q4 σ conditional 修复合并) |

### 0.4 证据矩阵 [EV]
| ID | 来源类型 | 引用 | 检索置信 | 来源权威 | 场景适用 | 归属 |
|----|----------|------|----------|----------|----------|------|
| [R1] | PROJECT_FACT | `types.hpp:120-132` cpa_hard_m 字段定义 + Bug C deep RC-C 注释 | 高 | 高 | 高 | DP-01,DP-06 |
| [R2] | PROJECT_FACT | `mid_mpc_solver.cpp:600-606` IPOPT 路径消费 cpa_hard_m | 高 | 高 | 高 | DP-01,DP-06 |
| [R3] | PROJECT_FACT | `mid_mpc_acados_formulation.cpp:333,340` + `gen_mid_mpc_acados.py:315,323` acados graph 只用 cpa_safe | 高 | 高 | 高 | DP-01 |
| [R4] | PROJECT_FACT | `mid_mpc_solver.cpp:462-502` IPOPT reachability schedule(k_minalt_rot + box_reach) | 高 | 高 | 高 | DP-03,DP-08,DP-06 |
| [R5] | PROJECT_FACT | `mid_mpc_node.cpp:557-564,664` M4 BehaviorPlan → MidMpcInput pack(heading_box_reachable/earliest_min_alt_k) | 高 | 高 | 高 | DP-06 |
| [R6] | PROJECT_FACT | `types.hpp:163-169` v2.2 §4.6 reachability 合约字段 + "M4 publish, M5 consume" 注释 | 高 | 高 | 高 | DP-03,DP-06,DP-08 |
| [R7] | PROJECT_FACT | `mid_mpc_acados_solver.cpp:99-111,1114-1128` status 4 fail-open 重映射 | 高 | 高 | 高 | DP-05 |
| [R8] | PROJECT_FACT | `mid_mpc_acados_solver.cpp:984-1009` wrapper 只 set stage0 lbx/ubx,stage≥1 静态 box | 高 | 高 | 高 | DP-02 |
| [R9] | PROJECT_FACT | `gen_mid_mpc_acados.py:331-335` min-alt row 每 stage 激活(无 reachability schedule) | 高 | 高 | 高 | DP-03 |
| [R10] | PROJECT_FACT | `runs/m5_solver_diag/4fd37fd7e.../fresh_production_config/*/verdict.json` 三 case REFERENCE_FEASIBLE + ACADOS_FAILURE | 高 | 高 | 高 | 全 DP |
| [R11] | DOMAIN_EVIDENCE | root-cause-diagnosis.md §6 MMG witness: live 30°→40.79s→k=3, benchmark 30°→49.95s→k=4 | 高 | 高 | 高 | DP-03 |
| [R16] | PROJECT_FACT | M4 合约调研(agent_b2f04b59):`heading_box_reachable_from_psi0_deg` 不携带方向符号;`earliest_min_alt_k` 是 IPOPT 公式 hint(advisory),非独立 MMG envelope | 高 | 高 | 高 | DP-03,DP-08 |
| [R17] | DOMAIN_EVIDENCE | 独立架构评审(agent_4e06e1e2):17 项中 12 同意/4 部分/1 不同意;Top 3 风险(Q4 形态 / VR-01 idxsh 耦合 / artifact 落盘) | 高 | 高 | 高 | DP-01/06 |
| [R18] | PROJECT_FACT | `gen_mid_mpc_acados.py:510` idxsh codegen 静态 + `mid_mpc_acados_solver.cpp` 全文无 runtime idxsh/Zl 更新 | 高 | 高 | 高 | DP-01 |
| [R19] | DOMAIN_EVIDENCE | BL-A 调研(agent_1e69aa0b):acatos v0.4.4 bgh 支持 runtime idxsh/Zl;路径 A+D1 推荐;否决 B/C/D2/D3/D4 | 高 | 高 | 高 | DP-01 |
| [R20] | DOMAIN_EVIDENCE | BL-B 调研(agent_610c745c):IPOPT ROT-reach surrogate-derived(差 5x);VR-03 选项 b' 推荐 | 高 | 高 | 高 | DP-03 |
| [R21] | PROJECT_FACT | acatos `ocp_nlp_constraints_bgh.c:1411-1417` slack 数学:row 进 idxsh 后结构上永远 soft(BL-A 引,ZCode + Codex 双重复核通过) | 高 | 高 | 高 | DP-01 |
| [R22] | PROJECT_FACT | `runs/.../fresh_production_config/*/reference_oracle.json` MMG witness:t_heading30_s live 40.79/benchmark 49.95;max_rot live 0.983/benchmark 0.782(BL-B 引) | 高 | 高 | 高 | DP-03 |
| [R23] | DOMAIN_EVIDENCE | L1a OCP 评审 ZCode(agent_1436144e):3 高优先级盲区(kGIdxCpaHard 未声明/DP-02 box 字面量绑定/L1a-L1b 拆分 DP-08 不干净);落盘 `docs/superpowers/review/2026-07-20-l1a-ocp-review-zcode-agent_1436144e.md` | 高 | 高 | 高 | DP-01/02/08 |
| [R24] | DOMAIN_EVIDENCE | L1a OCP 评审 Codex(NOT_READY):6 最小关闭项(nh 抉择/terminal contract/continuous CPA/k_head+t_latest_safe/backend physical-time map/speed reachability);落盘 `docs/superpowers/review/2026-07-20-l1a-ocp-review-codex.md` | 高 | 高 | 高 | DP-01/02/08 |
| [R25] | DOMAIN_EVIDENCE | acatos v0.4.4 exact commit `5c98c317416a9bb335a99d1bf7933a04712ea72b`:`bgh.c:526-538` model_set idxsh;`bgh.c:1411-1418` residual 减 slack;`cost_external.c:762-772` Z/z 只进 cost/gradient;`common.c:2645-2670` 每 solve 重新 initialize(Codex R23-R26) | 高 | 高 | 高 | DP-01 |
| [R26] | PROJECT_FACT | `constraint_compiler.cpp:242-266,287-292` IPOPT CPA expression 加全局 σ(非 true hard);`mid_mpc_solver.cpp:556-620` cpa_hard_from_k schedule;generated header `acados_solver_m5_mid_mpc_acados.h:44-70` NHN=0/NBXN=0(Codex 引) | 高 | 高 | 高 | DP-01/06,BL-10/15 |
| [R27] | DOMAIN_EVIDENCE | IMO COLREGs Rule 8(positive/ample-time/safe distance)+ Rule 16(early/substantial)+ MAIB 5/2026 §1.14 Polesie-Verity(Codex 引,一手源) | 高 | 高 | 中(场景依) | DP-01/08,BL-12 |
| [R28] | DOMAIN_EVIDENCE | NLM 检索复核(Codex):`maritime_regulations`/`colav_algorithms` 只读查询均未返回答案 payload;NLM retrieval 不能作为强证据,改用 [R27] 一手源 | 高 | 高 | 高 | 全 DP(证据规则修正) |
| [R29] | DOMAIN_EVIDENCE | Step5 方案 A 深化(agent_74180eb8,★★★☆☆):nh=36 双 row;无生产先例(IPOPT 不用 dual row,acados 上游无 example);返工面 250-400 行/4 文件;double-expression 权重协调 known-hard(slack 1e6-1e7 vs cost O(1));conditioning scaling 风险(关联 F-05);落盘 `docs/superpowers/review/2026-07-20-step5-plan-a-nh36-agent_74180eb8.md` | 高 | 高 | 高 | DP-01 |
| [R30] | DOMAIN_EVIDENCE | Step5 方案 B 深化(agent_8ae45f72,★★★★☆,**Step5 最终采纳**):nh=20+nsh=0+J_colreg;IPOPT parity 直接(IPOPT slack=空 时形态 ≈ 方案 B);返工面 100-150 行/2-3 文件;nsh=0 规避 runtime idxsh 复杂度;J_colreg barrier 数学 25x ratio + dominance 成立;落盘 `docs/superpowers/review/2026-07-20-step5-plan-b-nh20-agent_8ae45f72.md` | 高 | 高 | 高 | DP-01 |
| [R31] | PROJECT_FACT | IPOPT `compile_cpa_distance`(`constraint_compiler.cpp:247-298`)CPA row residual 用 `cpa_hard_m²`(line 262,Bug C deep RC-C 修复)+ 可选 σ 全局标量(line 290-292);IPOPT `build_colreg_cost_`(`mid_mpc_nlp_formulation.cpp:346-389`)用 `kIdxCpaSafe`(line 350)→ **IPOPT slack=空 时 = 方案 B 结构(hard row 用 cpa_hard + soft cost 用 cpa_safe 无 soft row)**(Step5 方案 A/B 深化独立读源码核实) | 高 | 高 | 高 | DP-01,DP-06 |

### 0.5 场景注册表 [SC]
| ID | 场景描述 | 约束/边界 | 驱动决策点 |
|----|----------|-----------|-----------|
| SC-01 | target2500_exact case(reference-feasible, raw4) | 重建诊断输入 | DP-01,DP-06 |
| SC-02 | rule14_ho_5000_ab_canonical(committed A/B fixture, raw4) | 5000m 标准 Rule14 | DP-01,DP-03,DP-06 |
| SC-03 | rule14_ho_live_dispatch_749728000002(real dispatcher capture, raw4) | live heading box 23.2°..53.2°, x₀≈0° | DP-02,DP-03,DP-08 |
| SC-04 | committed prefix 段几何冻结,独立 witness 发现违反 hard 1852 | k<prefix_active_k | DP-04 |
| SC-05 | M4 未升级(sentinel=0)→ M5 退化 v2.1 ROT-only | box_reach_deg=0 | DP-03,DP-08 |
| SC-06 | stand-on/HOLD/ReduceSpeed(direction/min-alt 双禁用) | lateral_active=false | DP-03 |
| SC-07 | 所有网格节点 CPA ≥1852,但两个节点之间 swept CPA <1852(连续区间穿越) | 15s 网格离散化漏检 | DP-01(LX 候选) |
| SC-08 | 距离扫点:1851.9、1852、2000、2500、2500.1m | hard/soft 阈值边界 | DP-01 |
| SC-09 | 仅 terminal `x_N` 出现 CPA/heading/speed/ROT 违反 | NBXN=0 缺口 | DP-01/02/08 |
| SC-10 | own speed 已在新 M4 speed box 外,受 decel limit 无法一步进入 | speed box reachability | DP-02 |
| SC-11 | heading wrap:359°→[5°,35°]及 1°→[325°,355°] | 角度跨 ±π 边界 | DP-02/08 |
| SC-12 | 低 ROT/rudder slew,进入 heading box 所需时间超过 horizon | k_head>N 不可达 | DP-08 |
| SC-13 | `prefix_active_k > k_head`,冻结 prefix 仍在 heading box 外 | prefix × schedule 冲突 | DP-04/07/08 |

### 0.6 裁决注册表 [VR]
| ID | 裁决对象 | 结论 | 采纳/弃用 | 理由 | 时间 |
|----|----------|------|-----------|------|------|
| VR-01 | DP-01 | **FINAL(Step5 采纳方案 B,2026-07-20 18:00)**:nh=20+nsh=0+J_colreg cost barrier 表达 soft。布局:[0,1] prefix eq / [2..17] CPA hard(16 targets,residual=dx²+dy²-cpa_hard_m²,cpa_hard_m=1852 固定,**nsh=0 天然排除 idxsh,无 slack**)/ [18] direction / [19] min_alt。soft 2500 aspiration **仅由 J_colreg cost barrier 表达**(J_colreg 用 kGIdxCpaSafe,conflict 时 bumped 2500)。新增 `kGIdxCpaHard` global slot(BL-08)。L4 失去 slack telemetry,补救:constraints_satisfied_ 去 sl_vec 读,新增 d_min + soft violation_m telemetry。prefix 段 hard row relax 依赖 L1b D1 witness(BL-07)。 | **采纳(方案 B)** | [R21][R25] hard-never-in-idxsh 数学;[R29][R30] 两方案深化对比;[R31] IPOPT parity 直接(IPOPT slack=空 时 = 方案 B 结构);[R24] Codex 首选 + 工程先例(acados 主流 nsh=0) | Step0 + Step2 修订 + **Step5 final** |
| VR-01-altA | DP-01(方案 A nh=36 双 row,ALT-08) | **FINAL 弃用**:nh=36 双 row(soft 16 + hard 16,soft row 入 idxsh)。 | **弃用** | [R29] 方案 A 深化:无生产先例(IPOPT 不用 dual row,acados 上游无 example);double-expression 权重协调 known-hard(slack 1e6-1e7 vs cost O(1));返工面 2-3x 于 B(250-400 行 vs 100-150 行);conditioning scaling 风险(关联 F-05);来源单薄(仅 ZCode 单评审) | Step0(原 ALT-01)→ Step5 正式弃用 |
| VR-02 | DP-02 | **修订中(Step2)**:wrapper 每 solve 每 stage 重发 lbx/ubx(消费 live heading/speed/ROT)保留;新增:stage0 保持 x0 equality 不被 box 覆盖、terminal contract 显式定义(BL-10)、ROT 来源修正为 GNC ODD 而非 M4、heading box 与 ROT box schedule 分离 | 部分采纳 | [R23][R24] 两份评审共识:box live 落地 + stage contract 三段化 | Step0 + Step2 修订 |
| VR-03 | DP-03 | earliest_min_alt_k 用选项 b'(保守因子 + oracle cross-check),不与 acados surrogate 共享 | 采纳 | [R4][R11][R20] IPOPT ROT-reach 是 surrogate-derived(差 5x);b' 是 F-01 教训推广 | Step0 + Step1.8 |
| VR-04 | DP-04 | committed prefix 违反 hard floor → NO_SAFE_PLAN + M7 | 采纳 | 架构不变量"NO_SAFE_PLAN 必须显式,不能用 penalty 掩盖";不重蹈 F-01 fail-open | Step0 |
| VR-05 | DP-05 | raw 0..7 fail-closed 映射;raw 4 绝不重映射 Converged | 采纳(后置 C3) | [R7] 当前 fail-open;独立于 C1,并行 C3,必须在 C4 前 | Step0 |
| VR-06 | DP-08 | **修订中(Step2)**:heading box = 可达 suffix + schedule 概念保留;但 k_head 公式 + ample-time 下界 `t_latest_safe` 挪 Step3/Step5(BL-12);L1a 立原则:所有 schedule 必须先按物理秒定义再映射各自 backend grid(BL-13,IPOPT/acados off-by-one) | 部分采纳 | [R4][R23][R24] 共识:概念正确但算法未冻结;Codex 指出 reachability 不等价 ample-time | Step0 + Step2 修订 |
| VR-07 | L1a 范围(用户裁决) | L1a = 规格冻结 + 不依赖 k_head 的子项:hard-never-in-idxsh 原则、kGIdxCpaHard slot、box live 落地、grid physical-time map、terminal contract 定义、nh 抉择(Step5)。k_head 公式/CPA suffix-hard schedule/prefix witness 挪 L1b | 采纳 | [R23][R24] 两份评审共识:L1a 完全独立不成立 | Step2(用户 AskUserQuestion 2026-07-20 17:05) |
| VR-08 | parity 框架(Step2 修正) | 不应把 IPOPT 现状当 true hard oracle;两路径共同对齐到语义正确 OCP 规范;IPOPT σ 全局标量(BL-15)修复合并到 C1b Q4 | 采纳 | [R24][R26] Codex 发现 IPOPT CPA expression 加全局 σ 非 true hard | Step2 |
| VR-09 | evidence 规则(Step2 修正) | NLM retrieval 不能作为强证据(Codex 验证 maritime_regulations/colav_algorithms 未返回 payload);改用 IMO/MAIB 一手源([R27]);NLM [R12] 降级为辅助参考 | 采纳 | [R24][R28] Codex 复核 NLM 失效 | Step2 |

### 0.7 备选/弃用方案 [ALT]
| ID | 方案 | 弃用理由 | 对比于 |
|----|------|----------|--------|
| ALT-01 | F-02 方案1: 双 row(soft 16 + hard 16,nh 20→36) | [R3] 全部 h-row indexing 重算(direction 18→34, min-alt 19→35),回归面大;soft 已在 cost 无需重复 —— **但 Step2 Codex 重新评估为 nh=20+nsh=0+J_colreg 的有力候选,挪 Step5 DESIGN-IT-TWICE 重新对比(BL-09)** | DP-01 |
| ALT-02 | F-02 方案3: hard+slack+post-gate | slack 可软化 → 非 true hard floor;safety claim 弱;任务包 §8 明确不建议 | DP-01 |
| ALT-03 | F-04 prefix: prefix-only slack 软化 | 与 F-01 fail-open 同类缺陷;静默吞安全违规 | DP-04 |
| ALT-04 | heading box: terminal maneuver band(仅 stage N) | 太弱:避碰过程 heading 无硬保证;与 hard CPA floor 语义不对称 | DP-08 |
| ALT-05 | heading box: M4 行为目标转 cost | 丧失硬安全保证;与 hard CPA floor 不一致;F-03 要求 runtime box 必须真实应用 | DP-08 |
| ALT-06 | min-alt reachability: acados surrogate | 与 solver 共享 surrogate,独立验证失效;F-01 教训 | DP-03 |
| ALT-07 | min-alt reachability: 二者 max(保守) | acados surrogate 独立性问题仍在;增复杂度 | DP-03 |
| ALT-08 | DP-01 nh=36 双 row(soft+hard 各 16) | **Step5 正式弃用**:无生产先例(IPOPT 不用 dual row,acados 上游无 example,文献有争议);double-expression 权重协调 known-hard(slack 1e6-1e7 vs cost O(1),需归一化或 Zl 精调);返工面 2-3x 于 B(250-400 行 vs 100-150 行,4 文件 vs 2-3 文件);conditioning scaling 风险(关联 F-05 EXACT+R=0);来源单薄(仅 ZCode 单评审 [R23] 支撑,BL-A [R19] 被选择性解读,Codex [R24] 反对)。深化见 [R29] | DP-01 |
| ALT-09 | DP-01 nh=20+nsh=0 + J_colreg cost 表达 soft(2500 仅在 cost) | **Step5 正式采纳**(VR-01 final):IPOPT parity 直接(IPOPT slack=空 时 = 方案 B 结构 [R31]);回归面最小;J_colreg barrier 数学 25x ratio + dominance 成立;acados 主流 nsh=0;Codex 首选。两个核心风险(J_colreg barrier ample-time 驱动 + L4 slack telemetry 补救)有明确测试/补救路径。深化见 [R30] | DP-01 |
| ALT-10 | DP-08 k_head 用 v2.1 ROT-only 占位(L1a 实施时) | BL-B 已证伪 surrogate gap 5x;占位公式复制缺陷,GATE 验收时仍 HARD-infeasible;L1a 不实施 k_head 实数值,挪 L1b | DP-08 |

### 0.8 技术规约注册表 [TS]
| ID | 类别 | 规约内容 | 单位/定义 | 来源 | 关联DP/接口 | 与现状差异 |
|----|------|----------|-----------|------|-------------|-----------|
| TS-01 | 物理量单位 | cpa_hard | m,固定 1852(1 NM) | [R1] odd_aware_thresholds.yaml | DP-01 | 一致(字段已存在) |
| TS-02 | 物理量单位 | cpa_safe(soft aspiration) | m,conflict 时 2500 | [R1] | DP-01 | 一致(字段已存在) |
| TS-03 | 符号约定 | heading_min/max_rad | rad,M4 BehaviorPlan 下发 | [R1] types.hpp:148-149 | DP-02,DP-08 | 一致(字段已存在) |
| TS-04 | 符号约定 | rot_max_rad_s | rad/s,右转正 | [R4] | DP-02,DP-03 | 一致 |
| TS-05 | 数值边界 | earliest_min_alt_k | stage index(int),0 sentinel=M4 未升级 | [R6] v2.2 §4.6 | DP-03 | acados 未消费,需落实 |
| TS-06 | 数值边界 | heading_box_reachable_from_psi0_deg | deg(float),0 sentinel=M4 未升级 | [R6] | DP-08 | acados 未消费,需落实 |
| TS-07 | 时序约定 | reachability schedule 激活 | k >= k_minalt 后 min-alt row harden | [R4] IPOPT 公式 | DP-03 | acados 缺,需移植 |
| TS-08 | 接口语义 | prefix_active_k | k<prefix_active_k 段几何冻结 | 现有 | DP-04,DP-07 | 一致 |

---

## 参考文献
- [R1] `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp:120-169` ConstraintInputs 字段定义 + Bug C deep RC-C 注释 + v2.2 §4.6 reachability 合约
- [R2] `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp:600-606` IPOPT 路径消费 cpa_hard_m
- [R3] `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_formulation.cpp:333,340` + `test/external/acados_backend/gen_mid_mpc_acados.py:315,323` acados graph 只用 cpa_safe
- [R4] `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp:462-502` IPOPT reachability schedule(k_minalt_rot + box_reach + minalt_box_infeasible)
- [R5] `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:557-564,664` M4 BehaviorPlan → MidMpcInput pack
- [R6] `types.hpp:163-169` v2.2 §4.6 reachability 合约字段注释
- [R7] `mid_mpc_acados_solver.cpp:99-111,1114-1128` status 4 fail-open 重映射
- [R8] `mid_mpc_acados_solver.cpp:984-1009` wrapper stage0 lbx/ubx + 静态 box
- [R9] `gen_mid_mpc_acados.py:331-335` min-alt 每 stage 激活
- [R10] `runs/m5_solver_diag/4fd37fd7e9fc435656e2154d92b859920a0eb646/fresh_production_config/*/verdict.json` 三 case 四分类
- [R11] `docs/superpowers/review/2026-07-20-m5-acados-root-cause-diagnosis.md` §6 MMG witness 数据
- [R12] NLM colav_algorithms domain(高置信):OCP formulation 是"离线独立设计步骤",求解是"嵌入求值";离散化/condensing 独立可验证;SQP/QP 强耦合不可单独调;COLREGs hard/soft 分层;fallback 链(reuse→relax→BC-MPC→emergency)
- [R13] GNC 独立评审(agent_03ab040d,PASS_WITH_FINDINGS):IPOPT 路径 7 项适合镜像(CPA 字段拆分/min_alt reachability/CPA suffix-hard schedule/direction k=0 soften/单位 frame/prefix pinning/fail-closed size check);1 项需两路径共修(Q4 prefix-CPA fail-open:σ 全局标量加到 k<K prefix CPA row,与 VR-04/ALT-03 冲突)
- [R14] `row_registry.hpp:256-270` apply_colreg_prefix_soften_ prefix 段 CPA relax,但 σ 全局标量仍加到这些 row(证据 [R13] Q4)
- [R15] `mid_mpc_solver.cpp:462-502` IPOPT box-reach bimodal 实现 + epsilon 0.005 rad 容差;Q3 隐患:box_reach 方向 vs pref_dir 一致性是隐性 M4 合约,无 sanity assert(证据 [R13] Q3)

---

## 演进日志(append-only · 时序 · 不可覆盖)

### Step0 · 用户前置裁决(AskUserQuestion)  [2026-07-20 13:50]
- 用户在 design-grounding 之前已对 DP-01/04/08/03 做了相当于 Step4/5 的裁决(VR-01/04/06/03),全部选语义最清晰、与 F-01 fail-open 教训一致的方向
- 主线收敛:所有 hard 约束激活时机由独立 MMG envelope 决定,acados graph 只表达硬安全语义,不与 solver 共享可达性论证

### Step1 · 行业调研·发现决策点  [2026-07-20 14:05]
- 模式判定: **重构**(M5 已有完整 IPOPT 实现 + acados 实现,F-02/03/04 是 acados 路径未消费 IPOPT 已消费的同一批合约字段)
- 关键发现(重构模式特有): F-02/F-03/F-04 不是"新增合约",而是 **acados 路径与 IPOPT 路径的 semantic parity 缺口**。证据:
  - [R1][R2] cpa_hard_m 字段已定义且 IPOPT 已消费,acados 未消费
  - [R4][R5][R6] v2.2 §4.6 reachability 合约 M4 已 publish、M5 node 已 pack、IPOPT solver 已消费,acados 未消费
- 新增决策点: DP-06(semantic parity 架构原则)、DP-07(prefix 不可控段 CPA row)
- 新增盲区: BL-01..BL-06
- 新增场景: SC-01..SC-06
- 触发技术分解: TD-01(M5 Mid-MPC acados OCP 语义冻结)→ DP-01..DP-08

### Step1.5 · GNC 独立评审 + DP-06 修正 + 4 层结构决策  [2026-07-20 14:20]
- **GNC 评审结论(证据 [R13][R14][R15])**:IPOPT 路径作为参照系"部分适合"。7 项结构正确应镜像;**Q4 prefix-CPA fail-open** 需两路径共修(IPOPT 现有 `apply_colreg_prefix_soften_` relax prefix CPA row 但 σ 全局标量仍加到这些 row → 与 VR-04/ALT-03 冲突,正是 ALT-03 明令拒绝的形态)。
- **DP-06 修正**:原"acados 镜像 IPOPT"改为"两路径共同对齐到语义正确的 OCP 规范;IPOPT 在 7 项上是正确参照,Q4 需两路径同时修正"。VR-04 范围扩大:从"acados 新增 NO_SAFE_PLAN"改为"IPOPT 现有 prefix-CPA σ 吸收行为也要同时修掉"。
- **新增 ARCH-DECISION-01(用户决策)**:采用逐层实施模式(非一口气讨论完再实施、非逐个 row 单独实施)。依据:systematic-debugging Phase 4.5(3+ fixes failed → question architecture,14-arm 消融命中此模式)+ NLM[12](离散化独立可验证、SQP/QP 强耦合不可单独调)。每层"讨论→设计→实施→测试→GATE→打钩→下一层",层内所有 row 一起设计保证内部一致(σ 策略/reachability/prefix 耦合),整层 GATE 验证。
- **新增 ARCH-DECISION-02(待用户审查)**:MPC 完整业务流程分 4 层 —— 第 0 层 OCP 规格(离线,对应 C1)、第 1 层 离散化转录(独立可验证,C1/C2 边界)、第 2 层 求解数值(SQP 强耦合,对应 C2)、第 3 层 结果接受与执行(独立校验,对应 C3/C4)。此结构待用户逐层审查确认后落盘为独立"MPC 业务流程总纲"文档。
- **OPEN 架构决策(Q4)**:prefix-CPA fail-open 修复方向 A(σ 不加 prefix row)/B(solve 入口独立 witness)/C(两者,defense-in-depth)。此决策阻断 DP-04/DP-07 的 grilling,需用户拍板。

### Step1.6 · 最终分层方案融合(4 层 + ChatGPT 6+LX + NLM + GNC)  [2026-07-20 14:35]
- **ARCH-DECISION-02 确认(修正)**:采用 **7 层结构**(L0 上游输入 / L1 OCP 建模 / L2 求解准备 / L3 数值求解 / L4 解复核 / L5 输出降级 / LX 横向诊断)。比原 4 层更细,采纳 ChatGPT 的 L0 独立分出、L4 独立分出、LX 横向诊断层。三段法归位(L0-L2 题写错 / L3 题难求 / L4-L5 解不能用)作为快速排障口诀。
- **映射证据**:每层映射到 F-01~F-05 finding + NLM[R12] + GNC 评审[R13]。
  - L0 上游输入 → GNC Q3 box-reach 隐性合约、F-03 runtime box 未落地
  - L1 OCP 建模 → F-02(CPA hard/soft)、F-04(min-alt 不可控)、DP-01/03/07/08
  - L2 求解准备 → F-03 wrapper box 落地、Step 5 seed
  - L3 数值求解 → F-05(EXACT+R=0)、Step 6/7/9
  - L4 解复核 → F-01(status fail-closed)、DP-05、GNC 独立校验分层
  - L5 输出降级 → NLM fallback 链、DP-04(NO_SAFE_PLAN)
  - LX 诊断 → 根因报告 runs/m5_solver_diag/ 本身就是 LX 的实例
- **新增文件**:`docs/Design/Architecture Design/M5_MPC_业务流程分层架构.md` 作为架构设计主文档,后续对话逐层填充。
- **逐层实施映射**:第 0 层 OCP 规格 = L1+L2(C1);第 2 层数值 = L3(C2);第 3 层接受执行 = L4+L5(C3+C4);L0 和 LX 贯穿所有层。

### Step1.7 · 独立架构评审 + M4 合约调研 + 2 盲点派发  [2026-07-20 15:00]
- **codex exec 失败**:在非交互/后台环境下 `codex exec` 两次尝试均 0 行产出(TTY/auth 问题)。按用户指示改用 ZCode 侧 tdl-spec-architect 做对等独立评审。
- **tdl-spec-architect 评审(agent_4e06e1e2,DONE_WITH_CONCERNS)**:17 项中 12 同意/4 部分同意/1 不同意。架构方向正确,可作为导航图,但 C1 启动前必须处理 Top 3 风险。artifact 已落盘 `docs/superpowers/review/2026-07-20-arch-review-agent_4e06e1e2.md`。
  - **风险 1(HIGH)**:Q4 在 acados 路径形态被误描述为 IPOPT 形态。acados Q4 是 `gen:510 idxsh` codegen 静态索引,不是 σ 全局标量。方向 A 对 acatos 无直接实现路径。需拆 IPOPT/acatos 子项。
  - **风险 2(HIGH)**:VR-01 hard slot 与 idxsh 耦合(BL-01)未闭环。hard 1852 仍可被 slack 软化 → VR-01 静默退化为已弃用 ALT-02。L1 GATE 必须加 idxsh 覆盖策略验证。
  - **风险 3(MEDIUM)**:GNC 评审 artifact 落盘(**已处理** → `docs/superpowers/review/2026-07-20-gnc-independent-review-agent_03ab040d.md`)+ X4 自动化前置。
- **M4 合约调研(agent_b2f04b59,DONE)**:`heading_box_reachable_from_psi0_deg` **不携带方向符号**(始终 ≥0),M5 必须同时读 `colregs_preferred_direction`。M4 `apply_primary_risk_guidance` 可能覆盖 direction 为 ReduceSpeed,M4/M5 两边对 pref_dir 看法可能不一致(但 ReduceSpeed 场景 direction_disabled,no-op,实际风险低)。`earliest_min_alt_k` 是 IPOPT 公式 hint(advisory),**不是独立 MMG envelope**。附带发现:`BehaviorPlan.msg:25` 注释漂移(未同步 v2.2 方向感知语义)。
- **用户决策(ARCH-DECISION-03)**:L0 全部缺陷现在修(含方向 assert);`input_degraded` 追溯机制同意;5 adapter 不拆只梳理。
- **2 盲点派发(用户指示专项调研 + 测试计划)**:
  - **BL-A**(agent_1e69aa0b,tdl-m5-planner-engineer):acados codegen 静态 slack 下 true hard floor 实现路径(甲/乙/丙/丁)+ prefix_active_k 动态性 + 测试计划。
  - **BL-B**(agent_610c745c,tdl-gnc-contract-reviewer):VR-03 独立 MMG envelope 实现选项(a/b/c)+ ROT-reach/box-reach 物理合理性 + 测试计划。
- **评审建议采纳**:L1 GATE 拆 L1a(DP-01/02/03/08,不依赖 Q4)+ L1b(DP-04/07,依赖 Q4)。但 L1a 含 VR-01(idxsh 耦合),风险 2 必须先闭环 → BL-A 调研结果决定。
- **新增证据**:[R16] M4 合约调研(agent_b2f04b59);[R17] 独立架构评审(agent_4e06e1e2);[R18] `gen_mid_mpc_acados.py:510` idxsh codegen 静态 + `mid_mpc_acados_solver.cpp` 无 runtime idxsh/Zl 更新(主 agent 验证)。

### Step1.8 · BL-A + BL-B 专项调研结论 + VR 修正 + IPOPT 边界更新  [2026-07-20 15:30]

#### BL-A 调研结论(agent_1e69aa0b,tdl-m5-planner-engineer,DONE,置信度高)
- **路径 A(codegen nh 含 hard+soft CPA + wrapper runtime 每 cycle 每 stage 重发 idxsh)可行且推荐**。BL-A 从 acados v0.4.4 上游源码验证:`bgh` 模块支持 runtime 每 stage 更新 idxsh/lsh/ush 和 Zl/zl(`ocp_nlp_constraints_bgh.c` model_set + `ocp_nlp_cost_external.c`)。当前 wrapper 未调用是疏漏,非 acados 限制。
- **关键数学事实(证实风险 2)**:一旦某 row 进入 idxsh,acados 在残差里减 slack(`bgh.c:1411-1417`),该 row **结构上永远 soft**。增大 Zl 只能让 slack 代价变大,**永远不能强制 slack==0**。**只有把 row 排除出 idxsh 才能实现 true hard floor**。巨大 Zl 是近似硬,非真硬 → VR-01 若不调 idxsh 会静默退化为已弃用 ALT-02。
- **prefix_active_k 动态性确认**:runtime 变化(0→12→K_max,范围 0..N-8),codegen 静态分区失败。wrapper 已每 cycle 每 stage 计算 prefix_K 并调 build_stage_row_bounds,加 idxsh 重发仅 O((N+1)·NT) 整数写入,增量代价可忽略。
- **路径 A + D1(防御纵深)推荐**:A = gen nh 含 hard 1852 + soft 2500 双 row + wrapper runtime idxsh 排除 hard;D1 = committed prefix CPA 由独立 MMG/L0 几何 witness(prefix 冻结几何,NLP 无法改变),违反→NO_SAFE_PLAN+M7。
- **否决**:B(多 solver 预生成,代价高/冷启动假阴性)、C(全 idxsh+L4 witness = ALT-02 已弃用)、D2(BGP)/D3(巨大 Zl)/D4(移除 CPA row)均不能 true hard。
- **测试计划 7 类(T1-T7)**:T1 hard 不可软化、T2 idxsh 排除 hard row(BL-01 闭环)、T3 adversarial slack、T4 三冻结 case、T5 prefix_active_k 转换、T6 D1 prefix witness、T7 IPOPT/acados 对等。
- **BL-A 剩余不确定性**:VR-01 布局(nh=20 单 hard+soft cost vs nh=36 双 row)两者都兼容路径 A,需 TDL Lead 确认。

#### BL-B 调研结论(agent_610c745c,tdl-gnc-contract-reviewer,DONE_WITH_CONCERNS,置信度中-高)
- **VR-03 核心假设被推翻**:IPOPT 的 ROT-reach 公式 `ceil(min_alt/rot_step)-1` 是 **surrogate-derived**(rot_max×dt,rot_max=4.7°/s GNC ODD 名义),不是独立 envelope。MMG oracle 实测 max ROT 仅 0.983°/s(live)/0.782°/s(benchmark),**差 5x**。IPOPT 公式给 k=1,物理实际 k=3。根因:rudder slew 2°/s 限制下从 delta=0 转饱和需 11.5s,这段时间 ROT 远未达名义值。
- **VR-03 推荐选项 b'(保守修正 ROT-reach + 离线 MMG oracle 回归网)**:
  - 在线路径保留 max(ROT,box) 结构,但 rot_step 除以保守因子 kSurrogateFudgeFactor(由离线 oracle 校准:live 4.78x、benchmark 6.01x、target2500 3.57x,速度相关)。
  - 离线 MMG witness 保留为回归 oracle,CI 强制每个 COLREG scenario 通过 `witness_reach_time/dt_NLP ≤ minalt_hard_from_k` cross-check。在线 k < oracle k → CI RED。F-01 教训推广。
  - "独立"边界:独立于 acados solver surrogate(通过保守因子+oracle cross-check),独立于 M4 hint(advisory),不需独立于 M4 box_reach(几何信息)。
- **否决**:(a) 新增独立 MMG 模型(工程代价极高/秒级实时性不满足);纯 (b) 镜像 IPOPT(就是问题本身)。
- **测试计划 6 类(T1-T4)**:T1 ROT-reach 公式、T2 在线 vs MMG oracle cross-check(当前应 RED)、T3 边界、T4a adversarial surrogate 乐观(必须新增)。
- **BL-B escalation**:保守因子速度分段(单标量 vs 速度表,需 NLM ship_maneuvering);node fallback 1.2°/s vs live ODD 4.7°/s ROT 源不一致(独立 finding);MMG witness 实时性上限(out of scope)。

#### VR 裁决修正
- **VR-03 修正**:从"用独立 MMG envelope,不与 acados surrogate 共享"明确为"**选项 b'(保守因子 + oracle cross-check)**"。理由:BL-B 证明 IPOPT ROT-reach 是 surrogate-derived,纯镜像 (b) 就是复制缺陷;新增独立 MMG (a) 工程代价极高且实时性不满足。b' 通过保守因子堵 surrogate gap + oracle cross-check 防 fail-open,是 F-01 教训的直接推广。

#### IPOPT 参照系边界更新(从 7+1 → 5+3)
- ✅ **5 项适合镜像**:CPA 字段拆分(cpa_hard_m vs cpa_safe_m)、CPA suffix-hard schedule、direction k=0 soften(§4.4)、单位 frame(rad/m/s/s)、prefix pinning + WGS84 重投影 + fail-closed row-registry size check。
- ❌ **3 项需两路径共修**:
  - **Q4 prefix-CPA fail-open**:IPOPT σ 全局标量 / acados idxsh 静态索引(两路径形态不同,BL-A 确认 acados 修法 = 路径 A,IPOPT 修法 = σ expression conditional)
  - **Q8-reach ROT-reach surrogate gap**(BL-B 新发现):rot_max×dt 乐观,与 MMG oracle 差 5x。IPOPT + acados 都用同一错误公式,都需引入 b' 保守因子 + oracle cross-check。
  - **runtime heading/speed/ROT box 落地**:IPOPT 静态全 horizon + acados stage≥1 静态,两路径都需 wrapper 每 stage 写 live box + reachability schedule。

#### Q4 决策(基于 BL-A,待用户确认)
- **acados 侧**:路径 A(gen nh 含 hard+soft CPA + wrapper runtime 每 cycle 每 stage 重发 idxsh 排除 hard row)+ D1(committed prefix 独立几何 witness)。
- **IPOPT 侧**:σ expression conditional(不加到 k<K prefix CPA row)+ 独立 prefix witness(与 acados D1 同源)。
- 即原 A/B/C 选项中的 **方向 C(防御纵深)**,但具体实现由 BL-A 细化为 acados 路径 A + IPOPT σ conditional + 两路径共享 D1 witness。

#### L1 GATE 拆分修正(评审建议 + BL-A/B 补充)
- **L1a(DP-01/02/08,不依赖 Q4 但依赖 BL-A/B 结论)**:CPA hard slot(VR-01,路径 A idxsh 排除)、runtime box(VR-02)、heading box 可达 suffix(VR-06)。**前置**:VR-01 idxsh 策略已验证(BL-A T2 测试)、VR-03 b' 保守因子已校准(BL-B T2 cross-check)。
- **L1b(DP-03/04/07,依赖 Q4)**:min-alt reachability b'(VR-03 修正)、prefix CPA NO_SAFE_PLAN+M7(VR-04,路径 A+D1)、direction row reachability schedule。
- **L1 GATE 新增验收项**(评审风险 2 + BL-A):idxsh 在 hard slot 上的覆盖策略已验证(hard row 不在 idxsh);C1 h_fn rebuild 同步(con_h_expr 变更后 h_fn cache 重建);MMG witness 计算耗时 ≤ budget 或离线 precompute + 在线 lookup;VR-03 b' 在线 k ≥ oracle k cross-check CI RED→GREEN。

#### 新增证据
- [R19] BL-A 调研(agent_1e69aa0b):acados v0.4.4 bgh 支持 runtime idxsh/Zl;路径 A+D1 推荐;否决 B/C/D2/D3/D4。
- [R20] BL-B 调研(agent_610c745c):IPOPT ROT-reach surrogate-derived(差 5x);VR-03 选项 b' 推荐;测试计划 T1-T4。
- [R21] acados `ocp_nlp_constraints_bgh.c:1411-1417` slack 数学:row 进 idxsh 后结构上永远 soft(BL-A 引)。
- [R22] `runs/.../fresh_production_config/*/reference_oracle.json` MMG witness:t_heading30_s live 40.79/benchmark 49.95;max_rot live 0.983/benchmark 0.782(BL-B 引)。

### Step2 · L1a grilling · 两份独立评审 + 用户裁决  [2026-07-20 17:05]

#### 触发:用户要求独立评审
用户在 L1 grilling 开始时明确"L1的OCP问题我没有概念,请你派个subagent对目前需要的决策点进行评审,并给我一份提示词,我将在CODEX中仅同步进行评审"。主 agent 派 ZCode `tdl_m5_planner_engineer` 后台异步 + 给用户 Codex 提示词同步跑,两份独立评审同源同标准。

#### 评审产出(artifact 已落盘)
- **ZCode 评审**(agent_1436144e,DONE_WITH_CONCERNS):`docs/superpowers/review/2026-07-20-l1a-ocp-review-zcode-agent_1436144e.md`。证据 [R23]。
- **Codex 评审**(DONE_WITH_CONCERNS,NOT_READY):`docs/superpowers/review/2026-07-20-l1a-ocp-review-codex.md`。证据 [R24]。

#### BL-A 数学事实双重复核通过
两份评审**独立复核** BL-A 的数学事实:acatos `bgh` 一旦 row 进 idxsh,残差减 slack([R25] Codex 引上游 exact commit `5c98c317416a9bb335a99d1bf7933a04712ea72b` `bgh.c:1411-1418`),row 结构上永远 soft;增大 Zl 只放大 slack 代价,永远不能强制 slack==0(`cost_external.c:762-772`)。runtime 更新 idxsh 有效但 `nsh` 数量固定(`common.c:2645-2670` 每 solve 重新 initialize)。**双方置信 🟢,无反例**。结论:hard row 必须排除 idxsh 是不可妥协的硬约束。

#### 两份评审对照(14 项议题)
| # | 议题 | ZCode | Codex | 对照结论 |
|---|---|---|---|---|
| 1 | BL-A 数学事实 | ✅ 成立 | ✅ 成立(exact commit 三方) | 双方一致采纳 |
| 2 | DP-01 nh=36 方向 | 部分同意(补 kGIdxCpaHard) | **质疑 nh=36,推荐 nh=20+nsh=0+J_colreg** | **重大分歧 → BL-09 挪 Step5** |
| 3 | DP-01 prefix 段 hard 安全边界 | 依赖 L1b D1(高) | SC-13 prefix_K>k_head(中) | 双方一致 |
| 4 | DP-02 box 字面量 vs 参数绑定 | graph 不读 kGIdxHeadingMin/Max | stage0/terminal 两段独立失效 | 双方一致(Codex 更深) |
| 5 | DP-02 ROT 来源 | M4 BehaviorPlan | **GNC ODD**(`:902-916`) | Codex 纠正草拟 VR-02 |
| 6 | DP-02 terminal NBXN=0 | 未发现 | **三 DP 共同缺口** | Codex 独有发现 → BL-10 |
| 7 | DP-08 k_head 公式 | ROT-reach/box-reach/b' 三选一 | **物理秒定义 + t_latest_safe 双量** | Codex 更严 → BL-12 |
| 8 | DP-08 L1a/L1b 拆分 | DP-01×DP-08 拉 DP-07 | L1a 不能宣称完全独立 | 双方一致 |
| 9 | DP-08 IPOPT/acados off-by-one | 未发现 | **psi[0] vs stage0,系统性差一 stage** | Codex 独有 → BL-13 |
| 10 | r0=0 假设 | 未提及 | **首步 heading/ROT reach 可能错误** | Codex 独有 → BL-14 |
| 11 | terminal NHN=0/NBXN=0 | 未发现 | **三 DP 共同高优先级** | Codex 独有 → BL-10 |
| 12 | continuous/swept CPA | 未提及 | **SC-07 节点漏区间穿越** | Codex 独有 → BL-11 |
| 13 | IPOPT 非 true hard | 未提及 | **σ 全局标量,不应把 IPOPT 当 oracle** | Codex 独有 → BL-15,VR-08 |
| 14 | NLM 检索可信度 | 引 [R12] | **NLM 未返回 payload,改一手源** | Codex 纠正 → VR-09,[R27][R28] |

#### 9 个新盲区 + 7 个新场景(已落 §0.3 / §0.5)
- BL-07(prefix 段 hard row + D1 同源)、BL-08(kGIdxCpaHard slot 不存在)、BL-09(nh=20 vs 36 抉择 → Step5)、BL-10(terminal contract)、BL-11(continuous CPA)、BL-12(k_head + t_latest_safe)、BL-13(grid physical-time map off-by-one)、BL-14(r0=0)、BL-15(IPOPT σ 非 true hard)。
- SC-07(swept CPA)、SC-08(距离扫点)、SC-09(terminal 违反)、SC-10(speed box 外)、SC-11(heading wrap)、SC-12(reach>horizon)、SC-13(prefix_K>k_head)。

#### 用户裁决(AskUserQuestion 2026-07-20 17:05)
- **DP-01 nh 抉择**:**挪到 Step5 DESIGN-IT-TWICE**(BL-09)。L1a 暂不实施 DP-01 row 布局,只先实施不依赖此抉择的子项(kGIdxCpaHard slot、IPOPT σ 问题归类、hard-never-in-idxsh 原则)。
- **L1a 范围**:**L1a = 规格冻结 + 不依赖 k_head 的子项**(VR-07)。L1a 范围:hard-never-in-idxsh 原则、kGIdxCpaHard slot、box live 落地、grid physical-time map、terminal contract 定义、nh 抉抉。k_head 公式/CPA suffix-hard schedule/prefix witness 挪 L1b。

#### VR 修订(已落 §0.6)
- VR-01 修订:hard/soft 分离 + hard-never-in-idxsh 确认;nh 抉择挪 Step5;kGIdxCpaHard slot 必新增;prefix 段依赖 L1b D1。
- VR-02 修订:box live 落地保留;新增 stage0/terminal contract 三段化 + ROT 来源修正(GNC ODD)+ heading/ROT schedule 分离。
- VR-06 修订:可达 suffix 概念保留;k_head 公式 + ample-time 下界挪 Step3/Step5;L1a 立原则(grid physical-time map)。
- VR-07 新增:L1a 范围。
- VR-08 新增:parity 框架修正(IPOPT 不是 true hard oracle)。
- VR-09 新增:evidence 规则修正(NLM 降级,改一手源)。

#### ALT 修订(已落 §0.7)
- ALT-01 重新评估:Codex 把 nh=20+nsh=0+J_colreg 列为有力候选,与原 nh=36 双 row 形成 Step5 对比候选。
- ALT-08 新增:nh=36 双 row(Step5 候选 A)。
- ALT-09 新增:nh=20+nsh=0+J_colreg(Step5 候选 B,Codex 首选)。
- ALT-10 新增:DP-08 k_head v2.1 ROT-only 占位(BL-B 已证伪,L1a 不实施)。

#### 新增证据
- [R23] L1a OCP 评审 ZCode(agent_1436144e)。
- [R24] L1a OCP 评审 Codex(NOT_READY)。
- [R25] acatos v0.4.4 exact commit slack 数学(Codex 引上游源码)。
- [R26] IPOPT σ 全局标量 + cpa_hard_from_k + generated NHN/NBXN=0(Codex 引)。
- [R27] IMO COLREGs Rule 8/16 + MAIB 5/2026 Polesie-Verity(Codex 引一手源)。
- [R28] NLM 检索失效复核(Codex 验证 maritime_regulations/colav_algorithms 未返回 payload)。

#### L1a GATE 重新定义(取代 Step1.8 的旧 L1a/L1b 拆分)
L1a 通过条件(全部满足才进 L1b):
1. **hard-never-in-idxsh 原则**已落 codegen + wrapper runtime idxsh 排除策略 + adversarial 测试(BL-A T2/ZCode T-DP01-1)。
2. **`kGIdxCpaHard` global slot** 新增完整 wiring(global enum + pack + codegen SX 镜像 + np_global + 三方 hash),BL-08 闭环。
3. **box live 落地**:wrapper 每 solve 每 stage 重发 lbx/ubx;stage0 x0 equality 不被 box 覆盖;terminal contract 显式定义(BL-10 闭环,至少声明"terminal 不在安全 claim 内"或"纳入 NBXN")。
4. **ROT 来源修正**:ROT 来自 GNC ODD 而非 M4。
5. **heading/ROT schedule 分离**:heading soften,ROT 全 stage hard(T-DP02-3 adversarial)。
6. **grid physical-time map 原则**:所有 schedule 先按秒定义再映射各自 backend grid,禁裸 k parity(BL-13 原则层闭环,公式层 L1b)。
7. **nh 抉择 Step5 裁决后**才能实施 DP-01 row 布局(Step5 DESIGN-IT-TWICE 对比 ALT-08 vs ALT-09)。
8. **prefix 段 hard row relax 安全边界显式标注**:依赖 L1b D1 witness,L1a 测试范围限定 prefix_active_k=0 case。

L1b 范围(DP-03/04/07/08 完整实施 + L1a 留的公式回填):
- k_head 公式 + ample-time 下界 `t_latest_safe`(BL-12)。
- CPA suffix-hard schedule(DP-07 部分内容,与 DP-01×DP-08 耦合解耦)。
- min-alt reachability b'(VR-03,保守因子 + oracle cross-check)。
- prefix CPA NO_SAFE_PLAN + M7(VR-04,路径 A+D1)。
- Q4 σ conditional(IPOPT)+ D1 witness(两路径同源)。
- direction row reachability schedule(BL-06)。

### Step5 · DESIGN-IT-TWICE · DP-01 nh 抉择(BL-09)  [2026-07-20 18:00]

#### 触发:Step2 用户裁决 nh 抉择挪 Step5
Step2 grilling 中两份评审(ZCode 推 nh=36 / Codex 推 nh=20)分歧明显,用户裁决"挪到 Step5 DESIGN-IT-TWICE"。Step5 用并行 subagent 深化两方案,返回七维方案刻画不做裁决。

#### Step5.3 并行 subagent 深化(两份 artifact 已落盘)
- **方案 A 深化**(agent_74180eb8,★★★☆☆):`docs/superpowers/review/2026-07-20-step5-plan-a-nh36-agent_74180eb8.md`。证据 [R29]。
- **方案 B 深化**(agent_8ae45f72,★★★★☆):`docs/superpowers/review/2026-07-20-step5-plan-b-nh20-agent_8ae45f72.md`。证据 [R30]。

#### Step5.4 决策卡片对比(七维)

| 维度 | 方案 A:nh=36 双 row(ALT-08) | 方案 B:nh=20+nsh=0+J_colreg(ALT-09) |
|---|---|---|
| 来源 | 仅 ZCode [R23] 单评审支撑;BL-A [R19] 被选择性解读;Codex [R24] 反对 | Codex [R24] 首选;IPOPT parity 直接 [R31];acados 主流 nsh=0 |
| 工程验证 | **无生产先例**(IPOPT 不用 dual row,acados 上游无 example)。原创建模。 | **IPOPT slack=空 时 = 方案 B 结构** [R31];acados nsh=0 是主流默认。**有先例**。 |
| 技术分解 | 9 子模块需改(hard row 新增/J_colreg 保留 double-expression/kGIdxCpaHard/nh 20→36/runtime idxsh 重发/row index 全重算/h_fn rebuild/L4 区分 soft/hard) | 7 子模块需改(hard row residual 改 cpa_hard/J_colreg 保留(唯一 soft)/kGIdxCpaHard/nh 不变/idxsh 删除+NSH=0/row index 不变/h_fn rebuild/L4 去 sl_vec) |
| 失效边界 | ① soft aspiration double-expression 权重协调失效(量级 1e6-1e7 vs O(1))② nh 翻倍 conditioning scaling(关联 F-05)③ prefix 段 hard row relax 依赖 L1b D1(SC-04/13)④ slack telemetry 误用(Zl 调参漂移)⑤ 索引错位静默失效 | ① **J_colreg barrier 不足以驱动 ample-time**(理论 25x ratio + dominance 成立,需实测 T-B1/T-B3)② 无 slack telemetry → L4/M7 看不到 soft 违反(补救中等成本 T-B4)③ nsh=0 未来"部分软化"需返工(prefix/direction 用 bounds 可处理)④ hard floor × heading schedule 冲突(SC-03,与方案 A 共有) |
| 实现风险 | **中-高**。返工面 2-3x 于 B(250-400 行 vs 100-150 行);double-expression 权重协调 known-hard;conditioning scaling 风险 | **中**。返工面最小(nh 不变无索引重算);但 ample-time 经验调参风险 + L4 telemetry 补救成本 + IPOPT σ 不对称 |
| 可测性 | T-A1 slack telemetry / T-A2 double-expression 一致性 / T-A3 conditioning eigenvalue / T-A4 hard-never-in-idxsh / T-A5 三 case 回归 / T-A6 dimension hash | T-B1 **barrier 驱动 ample-time** / T-B2 hard true-hard adversarial / T-B3 ample-time Rule 8/16 / T-B4 L4 transparency 补救 / T-B5 dimension hash / T-B6 参数隔离 / T-B7 三 case 回归 |
| 推荐度 | **★★★☆☆**(3/5) | **★★★★☆**(4/5) |

#### Step5.5 关键差异化分析(裁决依据)

7 个判别点对比:

| 判别点 | 方案 A | 方案 B | 胜方 |
|---|---|---|---|
| IPOPT parity | 不一致(IPOPT 无 dual row) | **一致**(IPOPT slack=空 时就是方案 B 结构 [R31]) | **B** |
| 回归面 | 大(nh 20→36,250-400 行,4 文件) | **小**(nh 不变,100-150 行,2-3 文件) | **B** |
| conditioning / F-05 风险 | 中-高(rank 不变但 m² scaling 恶化) | **低**(nh 不变) | **B** |
| slack telemetry | **有**(可量化 soft 违背,但 Zl 调参漂移) | 无(需 L4 补救 d_min+violation_m,中等成本) | **A** |
| ample-time 语义 | double-expression 可能 slack-barrier 竞争 | 单 barrier,理论 25x dominance,**但需实测** | 平 |
| 工程先例 | 无(原创建模) | **有**(acados 主流 + IPOPT parity) | **B** |
| nsh=0 未来锁定 | nsh=16,slack 机制保留 | nsh=0,L1b prefix/direction 需用 bounds(可行) | A(轻微) |

**综合 7 个判别点:方案 B 胜 5、方案 A 胜 2(其中 1 个轻微)**。

#### Step5.6 裁决(用户确认 2026-07-20 18:00)

**采纳方案 B(nh=20+nsh=0+J_colreg),弃用方案 A(nh=36 双 row)**。理由按 Step5 裁决标准(优先级):

1. **工程验证**(标准 2):方案 B 有 IPOPT parity [R31] + acados 主流先例;方案 A 无生产先例,是原创建模。
2. **失效边界已知且可测**(标准 3):方案 B 两个核心风险(J_colreg barrier 驱动力 + L4 telemetry 补救)都有明确测试路径(T-B1/T-B3/T-B4);方案 A 的 double-expression 权重协调是 known-hard,失效边界更模糊。
3. **实现风险低**(标准 4):方案 B 返工面 2-3x 小于 A,无 runtime idxsh 复杂度,无 conditioning scaling 风险。
4. **技术分解完整性**(标准 1):两者都完整覆盖,但方案 B 与 IPOPT 现状结构对齐 [R31],子模块改动更局部。

**方案 B 的两个核心风险 + 缓解措施**(L1a GATE 必须验证):
- **风险 1:J_colreg barrier 是否足够驱动 ample-time** → **T-B1/T-B3 实测**(SC-08 距离扫点 + Rule 8/16 ample-time 时机)。若实测 solver 在 d≈2000 停留(勉强合规),调 ζ(steepness)或 w_colreg。
- **风险 2:失去 slack telemetry** → **L4 补救**(constraints_satisfied_ 去 sl_vec 读,新增 d_min + soft violation_m telemetry,成本中等约 1-2 天)。此补救属 L1a GATE 范围。

**弃用方案 A 的理由**(ALT-08 final):
- 无生产先例(IPOPT 不用 dual row、acados 上游无 example)
- double-expression(slack 量级 1e6-1e7 vs cost O(1))权重协调 known-hard,返工概率高
- 回归面 2-3x 于 B,与 F-05 EXACT+R=0 数值脆弱性叠加,conditioning scaling 风险
- slack telemetry 价值可被 L4 d_min+violation_m 替代(方案 B 补救路径明确)

**条件性升级条款**(写入 Step5 GATE):若 T-B1/T-B3 实测证明 barrier 不足以驱动 ample-time(solver 在 d≈2000 停留且调参无效),则方案 B 升级到方案 A(恢复 nsh>0 + dual row),此时方案 A 的 slack telemetry 成为必需。

#### VR/ALT 落盘(已更新 §0.6/§0.7)
- VR-01 转 **FINAL**(采纳方案 B)。新增 VR-01-altA 记录方案 A 弃用。
- ALT-08 转 **FINAL 弃用**。ALT-09 转 **FINAL 采纳**。
- BL-09 闭环。

#### 新增证据
- [R29] Step5 方案 A 深化(agent_74180eb8,★★★☆☆)。
- [R30] Step5 方案 B 深化(agent_8ae45f72,★★★★☆,**采纳**)。
- [R31] IPOPT compile_cpa_distance + build_colreg_cost_ 源码核实:CPA row 用 cpa_hard_m² + soft cost 用 kIdxCpaSafe,IPOPT slack=空 时 = 方案 B 结构。

#### 进入 L1a-spec-freeze
nh 抉择闭环后,L1a-spec-freeze 可完整实施(不再有"待 Step5"阻塞)。L1a-spec-freeze GATE 8 条(VR-07)全部就绪可执行。

