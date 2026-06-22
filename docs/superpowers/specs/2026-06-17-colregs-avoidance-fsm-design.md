# COLREGs 避碰决策逻辑 Spec：EncounterStateMachine

**日期**：2026-06-17
**状态**：Draft（待 writing-plans）
**范围**：COLREGs 避碰决策逻辑（M2/M4/M5/M6 链路），覆盖偏离 D-1~D-8，**不含目标船避碰实现**
**前置文档**：
- `docs/Design/Review/2026-06-17/COLREGs_Avoidance_Decision_Logic_Report.md`（偏离分析报告）
- `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` §3.3/§6.3/§8/§9/§10（架构权威）

**设计抉择记录**（brainstorming 会话 2026-06-17）：
- 范围：D-1~D-8 全含，不含目标船
- 方案：方案 3——显式状态机 + 阈值参数化
- 重构边界：完全重写 RuleLatch，用行为保留测试对冲回归
- PREPLAN 语义：影子求解 + ACTIVE 发布
- 阈值冻结：Rule8 ≥30° / C-12 ample time 写死（A 级判例）；CPA/ROT 标参考值[HAZID校准]

---

## 1. 背景与范围

### 1.1 问题陈述

仿真测试中本船避碰与返航行为经常失败/异常。偏离报告（§5 偏离总表）识别 8 处设计↔实现偏离：

| ID | 偏离 | 严重度 |
|---|---|---|
| D-1 | ODD-A `cpa_safe_m`：设计 1852m vs YAML 1000m | 🔴 高 |
| D-2 | M5 COLREGs 约束：设计硬约束 vs 实际 soft cost | 🔴 高 |
| D-3 | Rule14 缺显式 TCPA gate | 🟠 中高 |
| D-4 | ODD-A `min_alteration_deg`：设计 30° vs YAML 15° | 🔴 高 |
| D-5 | M4 可把 Rule14 STARBOARD 替代为 REDUCE_SPEED | 🔴 高 |
| D-6 | `max_turn_rate_deg_s`：设计 12°/s vs YAML 5°/s | 🟠 中 |
| D-7 | ConstraintCompiler（硬约束+TSS polygon）已写未接入 | 🔴 高 |
| D-8 | BC-MPC 已实现但话题命名空间不一致 | 🟠 中 |

### 1.2 范围内

- M6 EncounterStateMachine 完全重写（替换 RuleLatch）
- M4 Rule14 减速替代守卫
- M5 ConstraintCompiler 接线 + PREPLAN 影子求解
- M5 BC-MPC 话题命名空间统一
- 阈值参数表（A 级写死 + 参考值标注）

### 1.3 范围外

- **目标船避碰实现**（Imazu 场景 / Woerner scorer / 两船交互）——另立 Spec
- ODD-C 港内靠泊（ship-domain 距离门逻辑）
- M1 ODD 调度、M7 监督、M8 HMI 的非避碰职责
- HAZID RUN-001（2026-08-19）的阈值实测校准

---

## 2. 阈值来源与置信度

### 2.1 写死值（A 级源，进 requirement）

| 参数 | 值 | 来源 | 置信度 |
|---|---|---|---|
| `min_alteration_deg`(ODD-A) | **30°** | COLREG Rule 8(b) 字面 + *The Roseline* / *The Oden* / *The Hakki Deval* 判例 | 🟢 A 级 |
| `T_plan`(ODD-A) | **720 s (12 min)** | C-12 判例（*The Samco Europe* / *The Rickmers Genoa* / *The Topaz*）+ FCB 18kn 服务航速换算 | 🟢 A 级 |

**C-12 → FCB 换算**：
```
本船 18 kn + 典型目标 12 kn = 合接近速度 30 kn
C-12 (碰撞前 12 min 动作) → T_plan = 720 s
对应距离 = 30 kn × 12 min ÷ 60 = 6.0 nm
```

依据：Rule 16 "ample time" 司法底线为 C-12（让路船最迟碰撞前 12 min 动作）。FCB 18kn 服务航速为架构 §1.4 确认的船型参数。

### 2.2 参考值（标 `[HAZID-2026-08-19校准]`，进参数表不进 requirement）

| 参数 | 参考值 | 来源 | Spec 处理 |
|---|---|---|---|
| `T_monitor`(ODD-A) | 1500 s (25 min) | 集装箱船动作点均值 30.1 min 下探（Appl. Sci. 11(16):7299）🟡 B 级 | 参数表 |
| `CPA_soft`(ODD-A) | 2778 m (1.5 nm) | Cockcroft give-way 动作 5-8 nm 下限换算 🟡 | 参数表 |
| `CPA_hard`(ODD-A) | 1852 m (1.0 nm) | 架构设计值 §3.3 🟡 | 参数表 |
| `CPA_safe`(ODD-A, release) | 1852 m (1.0 nm) | 同上 🟡 | 参数表 |
| `T_dwell`(ODD-A) | 60 s | 架构 §9.3 CPA 恢复确认时间 🟡 | 参数表 |
| `ROT_max`(ODD-A) | 12°/s | 架构 §10.4 声明 + 代码 `kDefaultRotMaxRadS=0.2094` 🟡（未见原始试航数据）| 参数表 |

### 2.3 ODD-aware 参数表（同结构不同值）

| 参数 | ODD-A 开阔 | ODD-B 狭水道 | ODD-D 能见度不良 |
|---|---|---|---|
| `T_plan` | **720 s** | 360 s | 900 s |
| `T_monitor` | 1500 s | 720 s | 1800 s |
| `min_alteration_deg` | **30°** | 20°（受限）| **30°** |
| `CPA_hard` | 1852 m [ref] | 926 m [ref] | 2778 m [ref] |
| 规则集 | Rule 13-17 | Rule 9 + 13-17 | Rule 19 覆盖 13-17 |

**ODD-B/D 的 T_plan/min_alteration 来源**：
- ODD-B `min_alteration 20°`：架构 §9.3（受限水域降幅度）
- ODD-D `T_plan 900s`：能见度不良需更早动作（Rule 19 + Cockcroft Stage 2 放大）

**ODD-C 排除**：港内靠泊用 ship-domain 距离门，非 TCPA/CPA 体系，不在本 Spec 范围。

---

## 3. EncounterStateMachine 设计

### 3.1 状态定义（7 状态）

| # | 状态 | 截图阶段 | 语义 |
|---|---|---|---|
| 1 | **CLEAR** | 初始 / 恢复航线 | 无约束，跟随航线（唯一始态/终态）|
| 2 | DETECTED | 1 识别 | 目标进 WorldState，未分类 |
| 3 | CANDIDATE | 2 规则判定 | Rule14 几何成立，无风险判断 |
| 4 | **PREPLAN** | 2→3 提前准备规避 | TCPA 进监视窗口，M5 影子求解 |
| 5 | ACTIVE | 3 及早动作 | TCPA≤T_plan，onset snapshot 冻结，发 give-way |
| 6 | MONITOR | 4 监视校核 | 动作已发，校核 CPA 回升 |
| 7 | RELEASE | 5 安全通过 | past-and-clear，准备解除约束 |

### 3.2 状态转换条件表

| 转换 | 条件（全部 AND）| 阈值来源 |
|---|---|---|
| CLEAR→DETECTED | target 进 WorldState | M2 |
| DETECTED→CANDIDATE | Rule14 几何成立（`course_diff ∈ [174°,186°]` ∧ `\|rel_bearing\| < 6°` ∧ `aspect ∈ [0°,10°]∪[350°,360°]`）| COLREG Rule14 字面 🟢 |
| CANDIDATE→PREPLAN | `TCPA ≤ T_monitor` ∧ `CPA < CPA_soft` | T_monitor 参考值 |
| **PREPLAN→ACTIVE** | **`TCPA ≤ T_plan` ∧ `CPA < CPA_hard` ∧ `range_closing`** | **T_plan=720s 🟢 + CPA_hard 参考值** |
| ACTIVE→MONITOR | onset 已发 ∧ CPA 趋势改善（`dCPA/dt > 0` 持续 ≥2 周期）| — |
| MONITOR→ACTIVE | CPA 趋势恶化（`dCPA/dt < 0`）∧ `TCPA ≤ T_plan` | — |
| MONITOR→RELEASE | `past_and_clear` ∧ `!range_closing` ∧ `CPA ≥ CPA_safe` | Rule16 🟢 |
| RELEASE→CLEAR | 上述 RELEASE 条件持续 `T_dwell` | T_dwell 参考值 |
| 任意→CLEAR | target 丢失 / new-run 重置（sim time 回跳 >1s）| — |

**几何阈值**（DETECTED→CANDIDATE）来自现有 `rule14_head_on.cpp`，不变：
```
|course_diff - 180°| < 6.0
rel_bearing ∈ (-6°, 6°)
aspect ∈ [0°, 10°] ∪ [350°, 360°]
```

### 3.3 迟滞逻辑（从 RuleLatch 迁移，必须保留）

完全重写 RuleLatch，但以下 4 项行为契约必须保留（对应黄金测试 T1-T4）：

#### 3.3.1 Onset Snapshot（Rule 13(d)）
ACTIVE 进入时冻结 `role`/`encounter_type`/`preferred_direction`/`min_alteration_deg`。本船右转导致 Rule14 几何脱落（`rel_bearing` 超过 ±6°）时，FSM 不回退到 CANDIDATE，持有 onset 分类直到 RELEASE。

**理由**：否则 `conflict_detected` 由闪烁的 secondary rule 承载，M4 抖动 AVOID↔TRANSIT。

#### 3.3.2 Encounter Reference Heading（防误释放）
RELEASE 条件 `past_and_clear` 用 **onset 时的航向** 作 beam 参考，不用当前避让航向：
```
past_and_clear = |signed_rel_bearing(bearing, onset_reference_heading)| > 112.5°
```

**理由**：大角度右转后，用当前航向会把目标从船首转到船舷，假满足 abaft-beam 而误释放。

#### 3.3.3 Projection Release（give-way 备份释放）
对 give-way / BOTH_GIVE_WAY 角色，当几何无法满足 abaft-beam 测试时，用 CPA 投影作备份释放：
```
projection_release = !range_closing ∧ cpa_projection_past_and_safe
cpa_projection_past_and_safe = (TCPA ≤ epsilon) ∧ (CPA ≥ CPA_safe)
```

**理由**：某些对头几何在右转后目标始终在正横前，永远不会 abaft-beam，需要投影备份否则永久卡 ACTIVE。

**限制**：stand-on（Rule17 in-extremis）不允许 projection release——必须真 past-and-clear（见 3.3.4 理由）。

#### 3.3.4 Give-way Duty Latch + Stand-on 互斥 + Stand-on In-extremis Hold
跨规则的二级状态机（每 target 一个），保留现有逻辑：
- **give-way duty latch**：duty 未 latch 时，secondary give-way 载体（Rule16/18）被 gate 掉
- **stand-on in-extremis hold**（Rule17(b)）：stand-on 被迫独立动作时，锁存到 past-and-clear，不允许 projection release
- **互斥**：duty latch 与 stand-on latch 不可同时 active

**理由**：Rule17 的 own-ship 是因 give-way 失职才动作，projection CPA 释放会在目标真正让清前就交还航迹跟随。

### 3.4 PREPLAN 影子求解

PREPLAN 期间 Mid-MPC 每周期（1 Hz）正常求解，但**不发布** AvoidancePlan，只缓存到 `shadow_plan_`。ACTIVE 进入时立即发布最新缓存（0 延迟），后续 ACTIVE 周期持续刷新发布。

**设计理由**：
1. 态势变化由每周期重解自然处理（目标减速/转向 → 下周期 shadow_plan_ 更新）
2. ACTIVE 首帧零延迟（无需等 MPC 求解的 ~25ms）
3. L4 是否持续执行 AvoidancePlan 不影响本设计——ACTIVE 首帧必定有轨迹

**M5 接口新增**：
```cpp
// MidMpcNode 新增
bool publish_avoidance_plan_ = true;  // PREPLAN 时设 false
AvoidancePlanMsg shadow_plan_;        // PREPLAN 缓存
// ACTIVE 进入时：publish_avoidance_plan_ = true; pub->publish(shadow_plan_);
```

---

## 4. 模块改动

### 4.1 M6: EncounterStateMachine 替换 RuleLatch（D-3 + D-1）

**新文件**：
- `m6_colregs_reasoner/include/m6_colregs_reasoner/encounter_state_machine.hpp`
- `m6_colregs_reasoner/src/encounter_state_machine.cpp`

**替换**：
- 删除 `rule_latch.hpp` / 引用（RuleLatch 类完全移除）
- `colregs_reasoner_node.cpp` 的 onset/latch/release 内联逻辑（当前 548-890 行）抽进 `EncounterStateMachine::transition()`

**FSM 接口**：
```cpp
class EncounterStateMachine {
 public:
  EncounterStateMachine(const EncounterParams& params);
  // 每周期调用，返回当前状态
  EncounterState transition(const TargetSnapshot& target, bool range_closing,
                            bool past_and_clear, const RuleEvaluation* raw_eval);
  EncounterState state() const;
  const OnsetSnapshot& onset() const;  // onset 分类快照
  bool requires_action() const;        // ACTIVE/MONITOR 时 true
  bool conflict_detected() const;      // requires_action 或 stand-on in-extremis
};
```

**Rule14 触发链路变更**（D-3）：
- 旧：`rule14_geometric_hit ∧ cpa<cpa_safe ∧ range_closing → latch`
- 新：`rule14_geometric_hit → CANDIDATE; TCPA≤T_plan ∧ CPA<CPA_hard ∧ closing → ACTIVE`

### 4.2 M4: Rule14 减速替代守卫（D-5）

**文件**：`m4_behavior_arbiter/src/colregs_directive.cpp`

**改动**：`apply_primary_risk_guidance()` 加前置守卫：
```cpp
// 新增：Rule14 对头禁止方向全替代
const bool rule14_both_give_way =
    directive.primary_role == kRoleBothGiveWay && directive.rule14_active;
if (rule14_both_give_way) {
  // 减速仍可作 speed_max 辅助约束（speed_reduction_preferred 可设）
  // 但 direction 保持 STARBOARD，禁止改成 REDUCE_SPEED
  return;  // 跳过 directive.direction = REDUCE_SPEED 赋值
}
```

**Rule14_active 判定**：M6 COLREGsConstraint 新增字段或在 `active_rules` 含 rule_id=14。

**保留**：Rule15 crossing give-way 仍允许减速替代（交叉场景减速更合理）。

### 4.3 M5: ConstraintCompiler 接线（D-2 + D-7）

**文件**：`m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` + `mid_mpc_nlp_formulation.cpp`

**分两阶段**：

**Phase E2a（方向硬约束）**：
- MidMpcNode 构造期实例化 `ConstraintCompiler`
- 每周期 `compile()` 生成 Rule14/15 方向硬约束（`compile_rule14/15`）
- NLP formulation 的 `g_` 追加 ConstraintCompiler 输出
- 保留现有 `build_colreg_cost_()` soft cost 作为 backup（双保险），但权重降低

**Phase E2b（CPA hard constraint）**：
- `compile_colregs_rules` 的 CPA 约束接线
- 移除 soft cost backup
- IPOPT 参数重新调参（soft→hard 切换可能触发 Restoration_Failed，需验证）

**min_alteration 传入**（D-4）：ConstraintCompiler 用 `min_alteration_deg=30°`（来自参数表，A 级写死）。

### 4.4 M5: BC-MPC 话题统一（D-8）

**文件**：`m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp`

**改动**：
```cpp
// 旧：sub_world_ = create_subscription("/m2/world_state", ...);
// 新：
sub_world_ = create_subscription<l3_msgs::msg::WorldState>(
    "/l3/m2/world_state", 10, ...);
```

**BC-MPC 启动验证任务**（D-8 完整范围）：
1. 检查 `docker-compose.yml` / `docker-compose.a4000.yml` 是否含 `m5_bc_mpc_node` 服务定义
2. 若未启动：在 compose 的 sil-nodes 服务追加 BC-MPC 启动，作为 D-8 的第二子任务
3. 验证标准：`docker compose ps` 显示 m5_bc_mpc_node running，且 `/m5/reactive_override_cmd` 话题存在（即便无 trigger）

### 4.5 阈值参数表落地（D-1 + D-4 + D-6）

**文件**：`m6_colregs_reasoner/config/odd_aware_thresholds.yaml`

**改动**（ODD-A 为例）：
```yaml
odd_a:
  t_plan_s: 720.0              # [A级 C-12 判例] 写死，HAZID 不调整
  t_monitor_s: 1500.0          # [ref-2026-08-19] 参考值
  min_alteration_deg: 30.0     # [A级 Rule8 判例] 写死
  cpa_hard_m: 1852.0           # [ref-2026-08-19] 参考值，PREPLAN/ACTIVE 门
  cpa_safe_m: 1852.0           # [ref-2026-08-19] 参考值，RELEASE 门
  cpa_soft_m: 2778.0           # [ref-2026-08-19] 参考值，CANDIDATE→PREPLAN 门
  t_dwell_s: 60.0              # [ref-2026-08-19] 参考值
  max_turn_rate_deg_s: 12.0    # [ref-2026-08-19] 参考值
  # Rule17 stand-on 直航船三阶段字段（本 Spec 不改，FSM 的 PhaseClassifier 仍用）
  t_standOn_s: 480.0           # Rule17 保向阈值（不变）
  t_act_s: 240.0               # Rule17 独立避让阈值（不变）
  t_emergency_s: 60.0          # Rule17 紧急阈值（不变）
  max_speed_kn: 20.0           # Rule6 安全航速上限（不变）
  rule_9_weight: 0.0           # ODD-B Rule9 权重（不变）
```

---

## 5. 测试要求（黄金测试集）

### 5.1 行为保留测试（T1-T7，回归）

新 EncounterStateMachine 必须通过，验证 RuleLatch 现有行为不丢失：

| # | 测试 | 防 regression |
|---|---|---|
| T1 | onset snapshot：本船右转致 Rule14 几何脱落，FSM 持有 onset 的 BOTH_GIVE_WAY/STARBOARD | 防 ACTIVE→CANDIDATE 误回退 |
| T2 | encounter reference heading：用 onset 航向作 beam 参考 | 防大角度转向后假释放 |
| T3 | projection release（give-way 备份）| 防某些几何永久卡 ACTIVE |
| T4 | range_closing 门：距离不缩小时不进 ACTIVE | 防分离目标误触发 |
| T5 | new-run 重置：sim time 回跳清空所有 FSM | 防跨场景污染 |
| T6 | give-way duty latch + stand-on 互斥 | 防 Rule16 闪烁 |
| T7 | stand-on in-extremis hold（Rule17(b)）| 防 stand-on 动作抖动 |

**来源**：现有 `test_rule_latch.cpp`（20 测试）+ `test_colregs_release_policy.cpp`（16 测试）的行为用例迁移为 `test_encounter_state_machine.cpp`，断言从 `RuleLatch.latched()` 改为 `FSM.state() == ACTIVE` 等显式状态。原 RuleLatch 测试文件删除（类已移除）。

### 5.2 新功能测试（T8-T9）

| # | 测试 | 验证 |
|---|---|---|
| T8 | TCPA gate：目标 CPA≈0 但 `TCPA>T_plan` 时停在 PREPLAN 不进 ACTIVE | D-3 新门控 |
| T9 | D-5 守卫：Rule14 ACTIVE 时 M4 `directive.direction` 不被改 REDUCE_SPEED | D-5 新约束 |

### 5.3 单元测试命令
```bash
colcon test --packages-select m6_colregs_reasoner
colcon test --packages-select m4_behavior_arbiter
colcon test --packages-select m5_tactical_planner
```

---

## 6. 验证条件（Spec 的"完成"定义）

| 验证项 | 命令/产物 | 通过标准 |
|---|---|---|
| FSM 单测 | `colcon test --packages-select m6_colregs_reasoner` | T1-T9 全绿 |
| M4 守卫单测 | `colcon test --packages-select m4_behavior_arbiter` | T9 绿 |
| M5 硬约束单测 | `colcon test --packages-select m5_tactical_planner` | ConstraintCompiler 接线后 CPA hard constraint 不可穿透 |
| ODD-A 8-probe | `colregs-clean-8probe` 技能 | TCPA gate 生效（预期：基线会变，远距 CPA≈0 不触发算正确行为，非 regression）|
| local OrbStack gate | `./scripts/local-a4000-acceptance.sh` | 通过 |

**8-probe 基线变更预期**（非 regression）：
- TCPA gate 引入后，远距 CPA≈0 但 TCPA>T_plan 的场景不再触发 ACTIVE——这是**正确行为**（符合 Rule16 ample time）
- 现有 8-probe 证据基线会失效，需重新生成
- 这在偏离报告 §7 已预警，用户已确认接受

---

## 7. 风险与缓解

| 风险 | 缓解 |
|---|---|
| 完全重写 RuleLatch 引入回归 | T1-T7 行为保留测试先行，新 FSM 必须全绿才合并 |
| M5 soft→hard IPOPT 不收敛（Restoration_Failed）| Phase E2a/E2b 分阶段；E2a 保留 soft backup；IPOPT 参数重调 |
| 8-probe 基线失效 | 预期行为，非 regression；重新生成证据 |
| CPA_hard 参考值 1852m 可能在 HAZID 后变化 | 参数表化，HAZID 后只改 YAML 不动逻辑 |
| BC-MPC 未在 compose 启动 | D-8 核实 compose；若未启动则标注"接线+启用"双任务 |

---

## 8. 参考来源汇总

**A 级（写死值依据）**：
- COLREG Rule 8(b) 字面 + *The Roseline* / *The Oden* / *The Hakki Deval*（min_alteration ≥30°）
- *The Samco Europe* / *The Rickmers Genoa* / *The Topaz*（C-12 ample time）
- 33 CFR 83.07/83.08/83.14/83.19

**B 级（参考值依据）**：
- Cockcroft & Lameijer (2011) *A Guide to the Collision Avoidance Rules*（四阶段，give-way 5-8nm）
- Appl. Sci. 11(16):7299（集装箱船动作点均值 30.1 min）
- 架构 §3.3/§9.3/§10.4（CPA/ROT 设计值）

**项目内部**：
- 偏离报告 `docs/Design/Review/2026-06-17/COLREGs_Avoidance_Decision_Logic_Report.md`
- 架构报告 `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`
- NLM 笔记本 `maritime_regulations`（124 sources, high）/ `colav_algorithms`（140 sources, high）

---

**Spec 结束。下一步：writing-plans 生成实现计划。**
