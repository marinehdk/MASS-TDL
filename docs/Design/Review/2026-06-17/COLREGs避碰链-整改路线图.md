# COLREGs 避碰链 — 整改路线图（整改 + 验证闭环）

| 项 | 值 |
|---|---|
| 文档日期 | 2026-06-17 |
| 文档定位 | COLREGs 避碰链整改的执行路线图：定义"怎么从当前态到目标态"。目标态见 `COLREGs避碰链-修正架构设计.md`。 |
| 配套文档 | `COLREGs避碰链-修正架构设计.md`（本文档引用其 §3 目标态章节） |
| 覆盖范围 | 全避碰链 + 返航 + 安全监督整改，含 WBS 子任务分解、逐项可观测断言、三段式 promotion 闭环 |
| WBS 编号 | D3.10.1 ~ D3.10.8（**仅作路线图内部 WBS 编号**，不登记到 `Phase 3/00-overview.md`；D3.1-D3.9 已占） |
| 约束 | 本轮为文档产出，不改代码/配置/launch；WBS 每项指向具体 file:line 改动点供后续执行 |
| 验证粒度 | 逐项可观测断言 + 三段式 promotion（local OrbStack → A4000 → GitHub/GitLab）+ 子任务 WBS |

---

## 1. 文档定位

本文档回答两个问题：
1. **改什么**：D1-D14 偏离项 → P0/P1/P2 整改项的映射（§2）。
2. **怎么改 + 怎么验**：每个整改项拆成 WBS 子任务（§3），配可观测断言（§4），走三段式 promotion（§5）。

**与 8-probe review 的关系**：`docs/Design/Review/2026-06-16/` 的多份 review 已诊断了大部分偏离（D1/D2/D7/D8/D9 等），但分散在各报告里且未形成可执行 WBS。本文档把诊断收敛为单一整改计划，8-probe/trace evaluator 作为验证工具（D3.10.8）。

**症状目标**：修完 P0（D3.10.1）后，避碰失败（症状 A）和返航失败（症状 B）应有肉眼可见改善；修完 P1（D3.10.2-3.10.6）后行为符合 COLREGs 语义；P2（D3.10.7-3.10.8）完成架构层升级与探针加固。

---

## 2. 偏离 → 整改项映射总表

双向追溯：D 编号（见修正架构 §2）↔ P 整改项 ↔ WBS 子任务 ↔ 修正架构目标态章节。

| 偏离 | 整改项 | WBS | 目标态章节 | 优先级 | 估时 |
|---|---|---|---|---|---|
| D1 ODD-A CPA 1000→1852m | P0-1 统一 CPA 阈值 | D3.10.1 | 架构 §3.1.2 | 🔴 P0 | 0.5d |
| D2 ODD-A min_alt 15→30°, ODD-D 10→30° | P0-1（同上） | D3.10.1 | 架构 §3.1.2 | 🔴 P0 | （含上） |
| D3 M4 STARBOARD→REDUCE_SPEED | P0-2 M4 Rule14 守卫 | D3.10.1 + D3.10.3 | 架构 §3.3.1 | 🔴 P0+P1 | 1d + 2d |
| D4 Rule17 未 launch | P0-3 rule_set 加 Rule17/19 | D3.10.1 | 架构 §3.2.2 | 🔴 P0 | 0.5d |
| D5 ODD-D 无规则集切换 | P0-3（加 Rule19）+ P1-1 硬切换 | D3.10.1 + D3.10.2 | 架构 §3.1.4/§3.2.3 | 🔴 P0+P1 | （含上）+ 2d |
| D6 M7 veto 关闭 | P0-4 启用 M7 | D3.10.1 + D3.10.6 | 架构 §3.6 | 🔴 P0+P1 | 0.5d + 2d |
| D7 RuleAssessment 硬编码错误 | P0-5 真实化 trigger_conditions | D3.10.1 | 架构 §3.2.5 | 🟡 P0 | 0.5d |
| D8 M6 幽灵冲突 | P1-2 RuleLatch 释放改 range+CPA 回升 | D3.10.2 | 架构 §3.2.4 | 🟡 P1 | 1d |
| D9 M5 solver 永不收敛 | P1-3 solver 收敛契约/fallback SLA | D3.10.4 | 架构 §3.4.1 | 🔴 P1 | 5d |
| D10 BC-MPC topic bug | P0-6 修 topic 前缀 | D3.10.1 | 架构 §3.4.4 | 🔴 P0 | 0.25d |
| D11 M4 权重 no-op | P1-4 权重机制修复 | D3.10.3 | 架构 §3.3.4 | 🟡 P1 | 1d |
| D12 无 RETURN_TO_ROUTE | P1-5 新增 behavior + M3 返航 | D3.10.3 + D3.10.5 | 架构 §3.3.5/§3.5 | 🟡 P1 | 2d + 2d |
| D13 fallback t=60s + CPA 污染 | P1-6 fallback 时间 + CPA 隔离 | D3.10.4 | 架构 §3.4.2/§3.4.3 | 🟡 P1 | 1.5d |
| D14 阈值未标定 + YAML/fallback 不一致 | P2-1 ODD 参数体系 + P0-1 fail-fast | D3.10.7 + D3.10.1 | 架构 §3.1.6 | 🟡 P2 | 5d |
| — 探针不可信 | P2-2 8-probe 加固 | D3.10.8 | — | 🟢 P2 | 3d |

**总估时**：P0 ≈ 3d，P1 ≈ 16d，P2 ≈ 8d（含 HAZID 准备）。串行约 5-6 周，并行（D3.10.2/3/4 并行）约 3-4 周。

---

## 3. WBS 子任务分解

每个子任务列：**依赖 / 改动文件（file:line 级）/ 估时 / 验收证据路径 / promotion 段**。

### D3.10.1 — P0 配置与单点修复（🔥 立竿见影）

**目标**：6 项单点修复，目标 1-2 天内让症状 A/B 有可见改善。

| 子项 | 偏离 | 改动 | 文件:行 |
|---|---|---|---|
| a | D1 | ODD-A `cpa_safe_m: 1000.0 → 1852.0` | `m6_colregs_reasoner/config/odd_aware_thresholds.yaml:8` |
| b | D2 | ODD-A `min_alteration_deg: 15.0 → 30.0`；ODD-D `10.0 → 30.0` | `odd_aware_thresholds.yaml:7,31` |
| c | D3（临时） | M4 `can_reduce_speed` 排除 `BothGiveWay`（P1 在 D3.10.3 做完整守卫） | `m4_behavior_arbiter/src/colregs_directive.cpp:151-153` |
| d | D4/D5 | launch `rule_set` 加 `Rule17`、`Rule19` | `launch/l3_params.yaml:33` |
| e | D6（临时） | `veto_enabled: false → true`（P1 在 D3.10.6 做完整启用） | `l3_params.yaml:39` + compose 启动 M7 |
| f | D10 | BC-MPC 订阅 `/m2/world_state → /l3/m2/world_state` | `m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp:35` |
| g | D7 | `RuleAssessment.trigger_conditions` 从 Rule14 evaluate() 动态生成，删除虚假 bearing_rate | `m6_colregs_reasoner/src/colregs_reasoner_node.cpp:928-931` |
| h | D14（临时） | ODD 参数加载 fail-fast：YAML 缺失键报错而非静默回退 | `colregs_reasoner_node.cpp:291`（`load_odd_thresholds`） |

- **依赖**：无（P0 起点）
- **估时**：1-2d（a/b/d/e/f/g/h 是配置/单行改，c 是 ~10 行守卫）
- **验收证据**：`runs/local_a4000_container_probe_*.json`（M7 在跑）、`runs/local_runtime_probe_*.json`（BC-MPC 收到 world_state）、8-probe colreg-rule14-ho 场景回放（D3.10.8 加固前用现有探针）
- **promotion 段**：**完整三段**（local → A4000 → push），因为触及 launch/配置，必须 A4000 验证

---

### D3.10.2 — M6 Rule 14 显式状态机 + RuleLatch 释放（P1）

**目标**：实现修正架构 §3.2.1 Rule 14 状态机（Candidate/PrePlan/Active/Commit/Release + TCPA 门）+ §3.2.4 RuleLatch 释放改 range+CPA 回升（修 D8 幽灵冲突）。

| 子项 | 偏离 | 改动 | 文件:行 |
|---|---|---|---|
| a | D5(部分)/设计升级 | Rule 14 状态机：新增 `Rule14StateMachine` 类，TCPA 门 T_preplan/T_action/T_commit | 新文件 `m6_colregs_reasoner/src/rules/colregs/rule14_state_machine.cpp` + 改 `rule14_head_on.cpp:11-63` 调用状态机 |
| b | D8 | RuleLatch 释放删除线性衰减定时器，改 `range_opening ∧ CPA≥safe ∧ dwell` | `include/m6_colregs_reasoner/rule_latch.hpp:43-80` + `colregs_release_policy.hpp` |
| c | D5 | ODD-D 规则集硬切换：`run_reasoning()` 加 `if(domain==ODD_D) skip Rules 11-18` | `colregs_reasoner_node.cpp:548,688` |
| d | — | 状态机参数（T_preplan/T_action/T_commit/dwell）写入 ODD YAML | `odd_aware_thresholds.yaml`（每 ODD 加 4 字段） |

- **依赖**：D3.10.1（参数基线先统一）
- **估时**：4d（状态机 2d + latch 1d + ODD-D 切换 1d）
- **验收证据**：M6 单测（`m6_colregs_reasoner/test/`）覆盖 5 状态转移；8-probe Rule 14 场景回放验证 TCPA 门触发时机；rosbag2 录制 RuleAssessment 状态序列
- **promotion 段**：local + A4000（M6 是决策核心，必须 A4000 端到端）

---

### D3.10.3 — M4 硬约束 + RETURN_TO_ROUTE + 权重修复（P1）

**目标**：实现修正架构 §3.3.1（禁止 STARBOARD→REDUCE_SPEED 完整守卫）+ §3.3.4（权重修复）+ §3.3.5（RETURN_TO_ROUTE behavior）。

| 子项 | 偏离 | 改动 | 文件:行 |
|---|---|---|---|
| a | D3（完整） | `apply_primary_risk_guidance` 完整守卫：`can_reduce_speed` 仅 GiveWay，减速仅附加 speed constraint 不改 direction | `m4_behavior_arbiter/src/colregs_directive.cpp:140-163` |
| b | D11 | IvP push 使用 dictionary 权重，删除 `:620` 硬编码 10.0；或改 COLREG_AVOID 为独立 hard constraint 通道 | `behavior_arbiter_node.cpp:618-620` |
| c | D12 | 新增 `BehaviorType::RETURN_TO_ROUTE`：trigger、恢复路径计算、priority_weight 0.6 | `behavior_definitions.yaml` + `behavior_arbiter_node.cpp`（新 case） |
| d | D12 | COLREG release → RETURN_TO_ROUTE 衔接逻辑 | `behavior_arbiter_node.cpp:278,285-290`（release dwell 后切 RETURN_TO_ROUTE） |

- **依赖**：D3.10.1c（临时守卫）+ D3.10.2a（Rule 14 Commit 阶段锁定信号）
- **估时**：4d（守卫 1d + 权重 1d + RETURN_TO_ROUTE 2d）
- **验收证据**：M4 单测覆盖 Rule14 STARBOARD 不被改写；返航场景回放验证冲突释放后切 RETURN_TO_ROUTE；SAT-2 行为标签正确
- **promotion 段**：local + A4000

---

### D3.10.4 — M5 planner SLA + fallback + CPA 隔离（P1）

**目标**：实现修正架构 §3.4.1（solver 收敛契约）+ §3.4.2（fallback 首航点 ≤15s）+ §3.4.3（CPA scaling 隔离）。

| 子项 | 偏离 | 改动 | 文件:行 |
|---|---|---|---|
| a | D9 | solver 收敛契约：接 CasADi/IPOPT（`use_ipopt: true`）或明确 fallback 为常态 + SLA | `mid_mpc_node.cpp:363-368` + `l3_params.yaml:29`；续 `fix/m5-nlp-convergence` 分支工作 |
| b | D13 | `geometric_fallback_waypoint_time_s` 首航点 60s → 15s | `m5_tactical_planner/include/.../types.hpp:385` |
| c | D13 | CPA/TCPA scaling 隔离：引入 `cost_cpa_m`/`cost_tcpa_s`，不原地突变 `tgt.cpa_m` | `mid_mpc_node.cpp:267-268` + types |

- **依赖**：D3.10.1f（BC-MPC topic 修复，提供短程兜底）
- **估时****：5d（solver 3d + fallback/CPA 2d）
- **验收证据**：M5 单测覆盖 solver 收敛率 ≥95% 或 fallback SLA；SAT-3 trajectory candidates 不再显示被 ×0.2 的 CPA；返航末段轨迹不再游荡
- **promotion 段**：local + A4000（M5 是 L4 上游，必须 A4000）
- **注意**：solver 工作量大，若不做（用户未勾选"允许动 M5 Mid-MPC solver"），则 a 改为明确 fallback SLA + 文档标注 known-limitation

---

### D3.10.5 — M3 返航一等行为（P1）

**目标**：实现修正架构 §3.5 返航作为一等行为，配合 D3.10.3 RETURN_TO_ROUTE。

| 子项 | 偏离 | 改动 | 文件:行 |
|---|---|---|---|
| a | D12 | M3 增加 `ReturningHome` 状态（或 mission phase 标记） | `m3_mission_manager/src/mission_state_machine.cpp:12-20` |
| b | D12 | 返航段遇冲突 → 避碰链 → 释放后 RETURN_TO_ROUTE 衔接，维持返航目标 | `mission_manager_node.cpp:29,788-823`（末段 lookahead 与 RETURN_TO_ROUTE 配合） |

- **依赖**：D3.10.3c/d（RETURN_TO_ROUTE behavior 存在）
- **估时**：2d
- **验收证据**：返航场景回放验证冲突前后返航目标不丢失；M3 状态机单测覆盖 ReturningHome 转移
- **promotion 段**：local + A4000

---

### D3.10.6 — M7 启用 + 端到端优先级验证（P1）

**目标**：实现修正架构 §3.6 M7 完整启用 + P1-P5 优先级端到端验证。

| 子项 | 偏离 | 改动 | 文件:行 |
|---|---|---|---|
| a | D6（完整） | M7 进程随 compose 启动，veto 通道端到端打通 | `l3_params.yaml:39` + compose 文件 + `m7_safety_supervisor/` |
| b | — | P1-P5 优先级端到端测试：Reflex（CPA<50m）/ M7 VETO / M4 / M5 / M6 触发顺序验证 | 新测试场景 |

- **依赖**：D3.10.1e（临时启用）+ D3.10.2/3/4（doer 链修复，否则 checker 一直否决）
- **估时**：2d
- **验收证据**：preflight gate_2 显示 M7 运行；gate_6 doer-checker 独立性通过；MRM 触发场景回放
- **promotion 段**：local + A4000（M7 是认证关键 IEC 61508 SIL2）

---

### D3.10.7 — ODD 参数体系重构（P2）

**目标**：实现修正架构 §3.1 完整 ODD 参数体系（4 层阈值 + 动态 CPA 函数 + ODD-C ship-domain）。这是架构层升级，需 HAZID 配合。

| 子项 | 偏离 | 改动 | 文件:行 |
|---|---|---|---|
| a | D14 | ODD YAML 单值 → 4 层阈值结构 | `odd_aware_thresholds.yaml`（每 ODD 加 detect/monitor/plan/action/emergency） |
| b | — | 动态 CPA_safe 函数 `max(CPA_ODD, k_LOA·LOA, k_stop·stop_dist, lane_margin, sensor_margin)` | M1/M6 新增计算（跨模块，需架构评审） |
| c | — | ODD-C 改 ship-domain 驱动 | M1 ODD-C 分类逻辑 + M6 ODD-C 规则集 |
| d | D14 | YAML 单一真相源 + 加载校验（已在 D3.10.1h 临时做 fail-fast） | `colregs_reasoner_node.cpp:load_odd_thresholds` |

- **依赖**：D3.10.1 + D3.10.2（Rule 14 状态机用新阈值结构）+ HAZID RUN-001 准备
- **估时**：5d（结构 2d + 动态 CPA 2d + ODD-C 1d）
- **验收证据**：4 层阈值单测；动态 CPA 在不同 SOG/LOA 下的输出曲线；ODD-C 场景回放
- **promotion 段**：local + A4000 + **HAZID 前用临时值 + 敏感度扫描**（用户选项）
- **注意**：b/c 跨 M1/M6，属架构变更，建议单独 RFC

---

### D3.10.8 — 8-probe / trace evaluator 加固（P2）

**目标**：让探针可信，作为 D3.10.1-3.10.7 的验证工具。

| 子项 | 偏离 | 改动 | 文件:行 |
|---|---|---|---|
| a | —（探针） | CPA 阈值单一真相源：`gen_colreg_tier12.py` 恢复为唯一生成器，删除手动 YAML 编辑 | `scenarios/COLREGs测试/colreg-rule14-ho.yaml` + generator |
| b | —（探针） | README (275m) 与 YAML (185.2m) CPA 不一致修复 | `scenarios/colreg-rule14-ho/README.md`（若存在）+ YAML |
| c | —（探针） | trace evaluator 加 TCPA 门检查（D3.10.2 状态机）+ ≥30° 幅度检查（Rule 8） | 续 `docs/Design/Review/2026-06-16/8-Probe Trace Evaluator Spec.md` |
| d | —（探针） | 场景 CPA 验收线与设计 1852m 对齐（当前 4×LOA=180m 是 corridor profile，非 CPA safe） | `colreg-rule14-ho.yaml:68-85` |

- **依赖**：D3.10.1（参数统一后探针才有意义）
- **估时**：3d
- **验收证据**：8-probe 全绿；trace evaluator 报告含 TCPA/幅度断言
- **promotion 段**：local（探针工具，不走 A4000）

---

## 4. 逐项可观测断言

每个整改项配一个可被 8-probe / trace evaluator / 单测 / 运行时观测断言的条件。这是 promotion 的硬门。

| WBS | 断言 | 验证工具 |
|---|---|---|
| D3.10.1a | ODD-A `cpa_safe_m == 1852.0`（YAML 加载后） | M6 单测 + runtime param dump |
| D3.10.1b | ODD-A `min_alteration_deg == 30.0` ∧ ODD-D `== 30.0` | 同上 |
| D3.10.1c/d3.10.3a | **Rule 14 命中 ∧ TCPA>180s 时，M4 输出 `direction ≠ ReduceSpeed`** | M4 单测 + 8-probe trace |
| D3.10.1d | launch 后 M6 加载的 rule_set 含 `Rule17` ∧ `Rule19` | runtime param dump + M6 日志 |
| D3.10.1e/d3.10.6 | M7 进程运行 ∧ `veto_enabled == true` ∧ gate_2 M7 liveness=OK | preflight gate_2 + gate_6 |
| D3.10.1f | **BC-MPC 在 CPA<250m ∧ TCPA<30s 时发布 `ReactiveOverrideCmd`** | 8-probe trace + rosbag2 |
| D3.10.1g | `RuleAssessment.trigger_conditions` 含 `"course_diff<6°"` ∧ **不含** `"bearing_rate"` | M6 单测 + SAT-2 快照 |
| D3.10.2a | **Rule 14 Active 阶段在 TCPA ≤ T_action(ODD-A≈720s) 时进入**，不在 TCPA>T_preplan 时进入 | M6 状态机单测 + 8-probe |
| D3.10.2b | **M6 Rule 14 release 后 ≤2 周期内 `RuleAssessment.applicable_rule` 清空**（无 60s 幽灵冲突） | M6 单测 + rosbag2 |
| D3.10.2c | ODD-D 下 M6 不评估 Rules 13-18（仅 Rule 19） | M6 单测 + rule_assessment 话题无 Rule13-18 标签 |
| D3.10.3b | M4 COLREG_AVOID IvP 权重 == dictionary 值（非硬编码 10.0） | M4 单测 |
| D3.10.3c | **返航末段 COLREG 冲突释放后，M4 在 ≤1s 内切回 RETURN_TO_ROUTE/TRANSIT** | 返航场景回放 + SAT-2 行为标签 |
| D3.10.4a | Mid-MPC solver 收敛率 ≥95%（或 fallback SLA 明确标注） | M5 单测 + 统计 |
| D3.10.4b | 几何 fallback 首航点时间 ≤15s | M5 单测 |
| D3.10.4c | **M5 输入 `targets[].cpa_m` == M2 原始值（未被 ×0.2）**；scaling 仅在 cost 字段 | M5 单测 + SAT-3 数据 |
| D3.10.5a | M3 状态机含 `ReturningHome` 状态 ∧ 转移正确 | M3 单测 |
| D3.10.6b | P1 Reflex（CPA<50m）触发优先于 P2 M7 VETO 优先于 P3 M4 | 端到端场景 + rosbag2 时序 |
| D3.10.7a | ODD YAML 每层 4 阈值齐全 ∧ 加载 fail-fast | 配置单测 |
| D3.10.8a | `gen_colreg_tier12.py` 是唯一生成器 ∧ README/YAML CPA 一致 | repo 检查脚本 |
| D3.10.8c | trace evaluator 报告含 TCPA 门断言 ∧ ≥30° 幅度断言 | evaluator 运行 |

**断言优先级**：标 **粗体** 的是直接对应症状 A/B 的硬门，promotion 必须全绿。

---

## 5. 三段式 promotion 闭环

严格遵循 AGENTS.md 的 local-first deployment gate。每个 WBS 子任务标注走哪一段。

### 段 1：local OrbStack gate（所有子任务必走）
```bash
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```
- 证据：`runs/local_a4000_container_probe_*.json` + `runs/local_runtime_probe_*.json`
- 失败处理：本地修，不进段 2

### 段 2：A4000 acceptance（触及决策核心/launch/compose 的子任务必走）
```bash
ssh a4000
# rsync 仅 touched paths（AGENTS.md 禁 --delete）
source scripts/a4000-env.sh
npm run sys:start
./scripts/a4000-acceptance.sh
```
- 证据：`runs/a4000_*_probe_*.json` + rosbag2
- **必走段 2 的子任务**：D3.10.1（launch/配置）、D3.10.2（M6 决策）、D3.10.3（M4）、D3.10.4（M5）、D3.10.5（M3）、D3.10.6（M7 认证关键）
- **可跳过段 2**：D3.10.7d（配置校验）、D3.10.8（探针工具）

### 段 3：push（local + A4000 双绿后）
- GitHub target：`main`
- GitLab target：`l3-tdl`
- PR/handoff notes 必须含 local evidence path + A4000 evidence path

### 子任务 promotion 矩阵

| WBS | 段1 local | 段2 A4000 | 段3 push | 说明 |
|---|---|---|---|---|
| D3.10.1 | ✅ | ✅ | ✅ | 触及 launch/配置 |
| D3.10.2 | ✅ | ✅ | ✅ | M6 决策核心 |
| D3.10.3 | ✅ | ✅ | ✅ | M4 仲裁 |
| D3.10.4 | ✅ | ✅ | ✅ | M5 L4 上游 |
| D3.10.5 | ✅ | ✅ | ✅ | M3 返航 |
| D3.10.6 | ✅ | ✅ | ✅ | M7 SIL2 |
| D3.10.7 | ✅ | ✅（a/b/c）| ✅ | d 仅 local |
| D3.10.8 | ✅ | — | ✅ | 探针工具 |

---

## 6. 证据产物清单

每次 promotion 收集以下证据，路径写入 PR/handoff：

| 产物 | 路径 | 用途 |
|---|---|---|
| local container probe | `runs/local_a4000_container_probe_<ts>.json` | 段 1 基础设施 |
| local runtime probe | `runs/local_runtime_probe_<ts>.json` | 段 1 运行时 |
| A4000 acceptance probe | `runs/a4000_acceptance_probe_<ts>.json` | 段 2 |
| A4000 external adapter probe | `runs/a4000_external_adapter_probe_<ts>.json` | 段 2（若触及 adaptor） |
| 8-probe trace evaluator 报告 | `runs/8probe_trace_<scenario>_<ts>.json` | §4 断言验证 |
| rosbag2 | `runs/bag_<scenario>_<ts>/` | 时序回放（状态机/优先级） |
| SAT-2/3 快照 | `runs/sat_<ts>.json` | 行为标签/轨迹候选 |
| M6/M4/M5 单测报告 | CI artifacts | §4 单测断言 |

---

## 7. 风险与回滚

### 7.1 参数回滚点
- D3.10.1a/b（CPA/min_alt 阈值）：改回旧值即可，无依赖。保留旧 YAML 注释便于回滚。
- D3.10.2d（状态机参数）：T_preplan/T_action/T_commit 写入 YAML，可独立调整。

### 7.2 Feature flag
- D3.10.2a（Rule 14 状态机）：加 `rule14_state_machine_enabled` flag，默认 false，灰度开启。
- D3.10.2c（ODD-D 硬切换）：加 `odd_d_rule19_strict` flag。
- D3.10.3a（M4 守卫）：加 `m4_rule14_starboard_lock` flag。
- D3.10.7b（动态 CPA）：加 `dynamic_cpa_safe_enabled` flag。

### 7.3 灰度顺序（风险从低到高）
1. D3.10.1（P0 单点）→ 验证症状改善
2. D3.10.8（探针加固）→ 让后续验证可信
3. D3.10.2（M6 状态机）+ D3.10.3（M4 守卫）并行 → 决策核心修复
4. D3.10.4（M5）+ D3.10.5（M3 返航）并行 → 执行/返航修复
5. D3.10.6（M7）→ 安全兜底
6. D3.10.7（ODD 体系）→ 架构升级，HAZID 配合

### 7.4 已知大风险
- **D3.10.4a solver**：真 MPC 接入工作量大，可能引入新收敛问题。缓解：先做 fallback SLA（方案 B），solver 单独立项。
- **D3.10.7 ODD 体系**：跨 M1/M6 架构变更，可能触发回归。缓解：feature flag + HAZID 前用临时值 + 敏感度扫描。
- **D3.10.2c ODD-D 硬切换**：现有 ODD-D 测试场景可能依赖 Rule 13-18 评估。缓解：先跑现有 ODD-D 场景回归。

---

## 8. 与修正架构文档的交叉引用

| 本路线图章节 | 修正架构目标态章节 |
|---|---|
| §2 D1-D14 映射 | 架构 §2 偏离基线 |
| §3 D3.10.1 | 架构 §3.1.2 / §3.2.5 / §3.4.4 / §3.6 |
| §3 D3.10.2 | 架构 §3.2.1 / §3.2.3 / §3.2.4 |
| §3 D3.10.3 | 架构 §3.3.1 / §3.3.4 / §3.3.5 |
| §3 D3.10.4 | 架构 §3.4.1 / §3.4.2 / §3.4.3 |
| §3 D3.10.5 | 架构 §3.5 |
| §3 D3.10.6 | 架构 §3.6 |
| §3 D3.10.7 | 架构 §3.1 |
| §4 断言 | 架构 §3 各节目标态 |

---

*本路线图为整改执行计划。目标态设计见 `COLREGs避碰链-修正架构设计.md`。WBS 编号 D3.10.x 仅作本文档内部追踪，不登记到 Phase 3/00-overview.md。*
