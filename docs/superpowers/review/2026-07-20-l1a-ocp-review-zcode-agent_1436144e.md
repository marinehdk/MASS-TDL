# L1a OCP 建模独立评审 · ZCode tdl-m5-planner-engineer

> **评审 agent**: `tdl_m5_planner_engineer`(read-only)
> **agent id**: agent_1436144e-a0ae-4bce-92a1-d086d9588530
> **评审基线 HEAD**: `4fd37fd7e9fc435656e2154d92b859920a0eb646`(worktree `m5-design-grounding`)
> **评审范围**: L1a(DP-01 / DP-02 / DP-08)
> **状态**: DONE_WITH_CONCERNS
> **workspace writes**: none
> **完成时间**: 2026-07-20 16:30(后台异步,600s)

---

## 总体结论

| DP | 大方向 | 评审态度 |
|---|---|---|
| DP-01 (CPA hard slot nh=36) | 语义正确 | **部分同意**:nh=36 + 硬 row 永不入 idxsh + wrapper runtime 重发 idxsh,这三点强制必做。但草拟方案遗漏了一个前置(`kGIdxCpaHard` global slot 不存在) |
| DP-02 (runtime box 每 stage) | 语义正确 | **同意,但深化**:现状缺陷比草拟描述更深(codegen box 字面量绑定,graph 根本不读 live 参数) |
| DP-08 (heading box 可达 suffix) | 语义正确 | **部分同意**:L1a/L1b 拆分在此 DP 不干净(k_head 依赖 L1b VR-03 b' 保守因子) |

## BL-A 数学事实独立复核(通过)

acatos `bgh` 约束模块对进入 `idxsh` 的 row 在残差中减去 slack 变量(ocp_nlp_constraints_bgh.c ~1411-1417)。该 row 一旦在 idxsh 内,增大 `Zl` 只能放大 slack 代价,**永远不能强制 slack==0**。grep 验证 wrapper 全文无 idxsh/lsh/ush/Zl/zl 的 runtime set 调用(0 命中)。codegen `gen:510` 是 `idxsh = arange(NT)+2` 静态索引全部 CPA row。**结论成立,无反例**。只有把 hard row 排除出 idxsh 才能实现 true hard floor。

## 三个高优先级盲区

### 盲区 1:DP-01 隐含 `kGIdxCpaHard` global slot 新增,草拟方案未声明

- **事实**:acados graph 根本没有 `kGIdxCpaHard` 这个 global slot。`grep kGIdxCpaHard` 在 `formulation.cpp` + `gen_mid_mpc_acados.py` 0 命中。
- `cpa_hard_m` 字段在 `types.hpp:132` 存在,IPOPT 已消费(`mid_mpc_solver.cpp:600`),但 acados 从未 pack 进 global 参数向量。
- **影响**:nh=36 双 row 方案隐含必须新增 `kGIdxCpaHard` global enum + pack wiring + codegen SX 镜像 + `np_global` 维度变更(154→155)+ 三方 hash 重建。
- **失效形态**:实现者只改 nh 不改 global enum → 编译失败,或 silently 把 cpa_safe 当 hard threshold(回退到已弃用 ALT-02)。
- **新盲区 BL-08**:`kGIdxCpaHard` 新增的全链路 wiring,优先级**高**,L1a 实施前必须闭环。

### 盲区 2:DP-02 现状缺陷比草拟描述更深 —— codegen box 字面量绑定

- **事实**:`gen:481-482` `ocp.constraints.lbx = np.array([PSI_LB, -ROT_MAX, U_SURGE_MIN])`,`PSI_LB/UB = ±π`、`ROT_MAX=0.2094`、`U_SURGE_MIN/MAX=[0,15]` 全是 codegen 期常量。
- `kGIdxHeadingMin=6/kGIdxHeadingMax=7` slot 存在,wrapper 也 pack 了(`formulation.cpp:605-606`),**但 graph 里没有任何表达式读这两个 slot**(grep 只出现在 pack,不出现在消费)。
- **影响**:M4 的 heading box 被 pack 进 global 参数向量,然后被完全忽略。**stage0 也是字面量 ±π**(wrapper set stage0 lbx/ubx = x0 是 initial-state pinning,不是 heading box)。
- **对 VR-02 的修正**:草拟方案 wrapper set 数值会生效(idxbx=[2,3,4] 不变),但需要补两项 GATE 断言:
  - codegen `idxbx=[2,3,4]` 不变(psi/r/u_surge 三状态);
  - **heading box (idx 2) 与 ROT box (idx 3) 的 schedule 必须分离**(heading soften,ROT 不 soften),否则 ROT 被错误放宽。

### 盲区 3:L1a/L1b 拆分在 DP-08 上不干净 —— k_head 公式依赖 L1b 的 VR-03 b'

- **DP-08 的 k_head 公式三候选**:
  1. ROT-reach:`k_head = ceil(|heading_min - ψ₀| / (rot_max·Δt))` —— 但 BL-B 已证实 rot_max×Δt 是 surrogate-derived,差 5x(SC-03 live: max ROT 实测 0.983°/s vs 名义 4.7°/s)。**用名义 rot_max → k_head 偏小(乐观)→ stage k_head 仍 HARD-infeasible**。
  2. box-reach:`k_head = ceil(heading_box_reachable_from_psi0_deg / rot_step_deg)` —— 但 M4 合约调研(agent_b2f04b59)确认此字段**不携带方向符号**(始终 ≥0),M5 必须同时读 `colregs_preferred_direction` 判断 box 在左还是右。
  3. **VR-03 b' 保守因子**(BL-B 推荐):ROT-reach ÷ `kSurrogateFudgeFactor`(live 4.78x)。
- **关键发现**:**VR-03 b' 保守因子属 L1b DP-03**。所以 **DP-08 在 L1a 实施时实际上依赖 L1b 的 VR-03 b' 校准**。
- **机制C 默认最简版失效追问**:若实现者把 DP-08 做成最简版"只 soften stage0"(用 v2.1 ROT-only 公式 k_head=1),**这恰恰是复制 IPOPT 现状的 surrogate-derived 缺陷**,不是修复。
- **建议**:DP-08 要么挪到 L1b,要么 L1a 显式标注"k_head 用 v2.1 ROT-only 占位,L1b VR-03 b' 落地后回填"。

## 跨 DP 耦合风险(最大发现)—— DP-01 × DP-08 实际上把 DP-07 拉进 L1a

| 耦合 | 失效形态 | 评审建议 |
|---|---|---|
| **DP-01 hard CPA row × DP-08 heading schedule** | DP-08 soften 前 k_head stage 的 heading box(给船时间转)。**但 DP-01 的 hard CPA row 在这些 stage 仍硬**。若船在 k_head stage 内因 heading 未转而 CPA<1852,hard row 触发 infeasible。**这是 ample-time 语义的核心矛盾:给 heading 时间转 vs CPA 不能等** | 解法是 CPA 也要 suffix-hard schedule(IPOPT `cpa_hard_from_k` 已有,`row_registry::apply_cpa_suffix_hard_`)——**但这属于 DP-07(L1b,prefix/suffix CPA schedule)**。L1a 实施 DP-01+DP-08 但不实施 DP-07 时,前 k_head stage 的 CPA hard row 会与 heading schedule 冲突 → stage0-2 HARD-infeasible |
| **DP-02 box × DP-08 schedule** | DP-02 每 stage set live box,DP-08 soften 前 k_head stage 的 heading box。实现者若把 DP-02 的 per-stage box set 与 DP-08 的 schedule 分开实现(两个独立循环),容易出错 | 实现建议:DP-02 和 DP-08 共用一个 `build_stage_box_bounds(k, ...)` 函数,类似 IPOPT `derive_row_bound_config` |
| **DP-01 nh=36 × DP-08 schedule** | nh 从 20→36 后,`kRowDirection`/`kRowMinAlt` 索引变(DP-01 §2 已述)。若 DP-08 的 schedule 实现硬编码了 `kRowDirection=18`,nh=36 后变成 34,索引错位 → segfault 或 silent wrong row soften | GATE:T-DP01-4 dimension hash + 索引重算测试覆盖 |

## 关键文件路径(绝对路径)

- 设计日志(权威索引):`docs/superpowers/design-logs/2026-07-20-m5-acados-c1-semantic-ocp-design-log.md`
- acados build_con_h_:`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_formulation.cpp`(build_con_h_ line 320-354,只消费 cpa_safe line 333/340;global enum kGIdxCpaSafe=10 line 63,**无 kGIdxCpaHard**)
- codegen con_h_expr + idxsh 字面量:`src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/gen_mid_mpc_acados.py`(con_h_expr line 313-340;idxsh=arange(NT)+2 line 510;box 字面量 PSI_LB/UB line 179-181、481-486)
- wrapper runtime:`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp`(row layout 注释 line 66-82;build_stage_row_bounds line 152-195;per-stage lh/uh set line 984-990 stage0 lbx/ubx line 1006-1009;**全文无 idxsh/Zl/zl runtime set**)
- IPOPT row_registry + schedule:`src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp`(apply_colreg_prefix_soften_ line 256-270;apply_cpa_suffix_hard_ line 318-328;apply_direction_reachable_schedule_ line 303-312)
- IPOPT reachability:`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp`(box-reach bimodal line 462-506;cpa_hard_m 消费 line 599-609)
