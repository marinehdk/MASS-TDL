# 独立架构评审:M5 Mid-MPC 业务流程分层架构

> **评审 agent**:tdl-spec-architect(agent_4e06e1e2)
> **评审日期**:2026-07-20
> **评审对象**:`docs/Design/Architecture Design/M5_MPC_业务流程分层架构.md`(7 层 L0-L5+LX)
> **工作区**:HEAD `4fd37fd7e9fc435656e2154d92b859920a0eb646`,分支 `codex/m5-design-grounding`
> **workspace writes**:none(read-only)
> **状态**:DONE_WITH_CONCERNS — 架构方向正确,可作为导航图,但 C1 启动前必须先处理 Top 3 风险

## 评审摘要

17 个评审点:**12 同意、4 部分同意、1 不同意**。技术证据(file:line)在抽样验证中全部成立(F-02/F-03/F-04/F-05/Q4),ablation 矩阵与 verdict.json 真实可复现。主 agent 无重大归因漂移。

---

## Top 3 风险(C1 启动前必须处理)

### 风险 1(severity: HIGH)— Q4 在 acados 路径下的形态被误描述为 IPOPT 形态

**问题**:架构文档 §1.5.c 描述 Q4 fail-open 时引用 IPOPT 的 `apply_colreg_prefix_soften_` + σ 全局标量。但 acados 路径形态完全不同 — `gen_mid_mpc_acados.py:510` 的 `idxsh = np.arange(NT) + 2` 在 codegen 阶段静态索引所有 CPA row(含 prefix 段),每 stage 每 target 独立 slack 天然吸收 prefix 违反,**不需要 σ 全局标量**。修复方向 A("σ expression 不加到 prefix row")对 IPOPT 适用,对 acatos **没有直接实现路径**。

**影响**:若 C1 实施时按文档字面理解,会发现"方向 A 在 acatos 上无法落地",Q4 决策返工。

**建议**:C1 L1.5.c 细化前,把 Q4 A/B/C 拆成 IPOPT 子项和 acatos 子项。acatos 修法候选:(a) idxsh wrapper 每 cycle 动态重算;(b) prefix 段 CPA row 不进 idxsh(codegen 分区);(c) wrapper 入口独立几何 witness。

### 风险 2(severity: HIGH)— VR-01 hard slot 与 idxsh 耦合未闭环(BL-01)

**问题**:VR-01 加 hard 1852 row slot(nh 保持 20),但 `gen:510 idxsh` 已把所有 CPA row 加入软化列表。若 VR-01 实施后 idxsh 不调整,hard 1852 仍可被 slack 软化 → **VR-01 静默退化为已弃用的 ALT-02**。

**影响**:VR-01 实施后看起来"hard 1852 已表达",但实际仍可被软化,L1 GATE "soft 不当 hard" 静默失败。

**建议**:L1 GATE 显式加"idxsh 在 hard slot 上的覆盖策略已验证:hard 1852 row 不在 idxsh 内(或 penalty = +inf 等效 hard)"。

### 风险 3(severity: MEDIUM)— [R13] GNC 评审 artifact 未落盘 + X4 自动化未前置

**问题**:(a) GNC 评审(agent_03ab040d)artifact 未在 docs/ 中找到(grep 全库只命中自引用),决策可追溯性 gap;(b) X4 Failure Classifier "待自动化",但 L1 GATE "独立 MMG witness 对每个新 hard row 满足"需要 X4 自动化才能规模化执行。

**建议**:(a) 把 agent_03ab040d 评审原文落盘到 `docs/superpowers/review/`(**已处理** → `2026-07-20-gnc-independent-review-agent_03ab040d.md`);(b) X4 自动化前置到 C1 之前或并行。

---

## 逐条评审结论(17 项)

### A. 分层架构本身

| # | 评审点 | 判定 | 关键意见 |
|---|---|---|---|
| 1 | L0-L5+LX 7 层覆盖完整性 | **同意** | 对应 NLM[R12] 四阶段,L0/LX 独立分出合理 |
| 2 | 层间依赖方向 | **部分同意** | L4.1 `constraints_satisfied_` 复用 L1 con_h_expr(`mid_mpc_acados_solver.cpp:555-568`),L4→L1 反向读取未标注。建议 §7 L4.1 加注"依赖 L1 con_h_expr,gen 重生成时必须同步 rebuild h_fn cache" |
| 3 | 三段法映射 | **同意** | 与根因报告 F-01~F-05 severity 一致 |
| 4 | Q4 归到 L1.5.c+L4.2 | **部分同意** | acados Q4 形态被误描述(见风险 1) |

### B. 关键决策点

| # | 评审点 | 判定 | 关键意见 |
|---|---|---|---|
| 5 | DP-06 修正 | **同意** | 两路径共修方向正确,IPOPT Q4 是真实 bug |
| 6 | VR-01 双独立 slot | **同意** | 最优表达,但 BL-01(idxsh 耦合)未闭环(见风险 2) |
| 7 | VR-03 独立 MMG envelope | **同意,非过度保守** | F-01 教训证明 solver 自报告不可信。但提示:L1 GATE 应加"MMG witness 计算耗时 ≤ X ms,或离线 precompute + 在线 lookup" |
| 8 | VR-04 + Q4 A/B/C | **部分同意** | A/B/C 未拆 IPOPT/acados 子项(见风险 1) |

### C. 逐层实施顺序

| # | 评审点 | 判定 | 关键意见 |
|---|---|---|---|
| 9 | C1→C2→C3→C4 + C3 并行 | **同意(1 串行补丁)** | C3 与 C1 并行但有隐藏串行:C1 改 con_h → C3 h_fn rebuild 测试 |
| 10 | L1 GATE 定义 | **部分同意(漏 2 项)** | 漏 idxsh 覆盖策略验证(BL-01)+ L4.1 h_fn rebuild 同步 |
| 11 | Q4 阻断 L1 GATE | **同意** | 但建议拆 L1a(DP-01/02/03/08,不依赖 Q4)+ L1b(DP-04/07,依赖 Q4),C1 能在 Q4 前推进 70%。**注意 L1a 含 VR-01(idxsh 耦合),风险 2 必须先闭环** |

### D. 归因漂移风险

| # | 评审点 | 判定 | 关键意见 |
|---|---|---|---|
| 12 | 数值 vs 语义修复互相掩盖 | **未发现重大漂移** | 潜在风险:Q4 选方向 B 时 witness 数值稳定性 GATE(假阳/假阴率)需定义 |
| 13 | 14-arm 消融结论使用 | **同意(未滥用)** | 但 `cpa_rows_relaxed` 全 raw4 暗示 F-03/F-04 可能比 F-02 更关键,建议补分析 |
| 14 | IPOPT 参照系边界 | **同意(7+1 清晰)** | 但 artifact 缺失(见风险 3,**已处理**) |

### E. 遗漏检查

| # | 评审点 | 判定 | 关键意见 |
|---|---|---|---|
| 15 | 未被任何层覆盖的缺陷 | **发现 3 遗漏** | (1) ROS2 4 字段合约(stamp/schema_version/confidence/rationale)未在 L5.2 显式;(2) encounter lifecycle 阶段切片未在 L0 显式;(3) 扰动/不确定性(OU σ_pos)未被任何 GATE 覆盖 |
| 16 | LX 诊断覆盖度 | **部分同意** | X3 缺 prefix 段 pact_pre activation trace;X4 应前置到 C1 |
| 17 | L5 fallback + BC-MPC 边界 | **部分同意** | L5 缺"Last-Safe-Maneuver Envelope Computer"子模块(根因报告 §10 明确缺失) |

---

## 总评

这份架构**适合作为后续逐层实施的导航图**,但 C1 启动前必须补 3 项(风险 1/2/3)。如果补齐,可作为 C1 实施的权威导航图。

## 评审使用的关键证据

- `analysis/ablation_matrix.csv`(53 行全 raw4,真实可复现)
- `fresh_production_config/rule14_ho_live_dispatch_749728000002/verdict.json`(min_alt stage1 row 19 = -0.523 rad = -30°)
- 源码:`mid_mpc_acados_formulation.cpp:63,333,340,374,609`;`mid_mpc_acados_solver.cpp:99-111,546-589,984-1009,1100-1132`;`mid_mpc_solver.cpp:240-268,455-580,600-606`;`row_registry.hpp:200-335`;`gen_mid_mpc_acados.py:300-340,475-515`;`mid_mpc_node.cpp:450,2099`;`bc_mpc_node.cpp:41,59`;`gnc_bridge_node.cpp:14`;`safety_supervisor_node.cpp:280-311,473-516`

## 相关文件路径

- 架构主文档:`/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/docs/Design/Architecture Design/M5_MPC_业务流程分层架构.md`
- 根因报告:`/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/docs/superpowers/review/2026-07-20-m5-acados-root-cause-diagnosis.md`
- 决策日志:`/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/docs/superpowers/design-logs/2026-07-20-m5-acados-c1-semantic-ocp-design-log.md`
- GNC 评审(已落盘):`/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/docs/superpowers/review/2026-07-20-gnc-independent-review-agent_03ab040d.md`
