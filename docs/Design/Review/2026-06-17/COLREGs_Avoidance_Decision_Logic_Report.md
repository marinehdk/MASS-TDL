# COLREGs 避碰决策逻辑设计报告（含设计/实现偏离分析）

**生成日期**：2026-06-17
**范围**：仅限 COLREGs 避碰决策逻辑（M2/M4/M5/M6 链路），不含 M1 ODD 调度、M7 监督、M8 HMI 的非避碰职责
**方法**：架构设计报告 §3.3/§6.3/§8/§9/§10 通读 + 主 checkout 代码源码逐文件核查 + YAML 配置核查 + NLM 项目笔记本调研（`maritime_regulations` 🟢 / `colav_algorithms` 🟢）+ 网络核验（Wang 2021 / Cockcroft 四阶段）

---

## 核心结论（TL;DR）

1. **架构设计的避碰业务逻辑在原理上是合理的**（ODD 驱动 → M2 态势 → M6 判规 → M4 仲裁 → M5 执行 → M7 监督），与 COLREGs 法规层、Cockcroft 四阶段框架、Wang 2021 直航船四阶段理论方向一致。

2. **代码实现已比附件《COLREGs 避碰说明.md》描述的更成熟**：存在完整的 `RuleLatch`（onset-latched hysteresis）、`PhaseClassifier`、give-way duty latch、stand-on in-extremis hold、encounter reference heading、past-and-clear 释放逻辑。**附件 MD "状态机缺失" 的结论已部分过时**——onset-hold-release 状态机已实现。

3. **但存在多处实质性设计↔实现偏离**，它们直接解释仿真中"避碰行为和返航行为经常失败/异常"：

| ID | 偏离 | 严重度 |
|---|---|---|
| **D-1** | ODD-A `cpa_safe_m`：设计 1852m vs YAML 1000m | 🔴 高 |
| **D-2** | M5 COLREGs 约束：设计硬约束 vs 实际 soft cost（Phase E1）| 🔴 高 |
| **D-3** | Rule14 缺显式 TCPA gate（不符合 Rule16 "ample time"）| 🟠 中高 |
| **D-4** | ODD-A `min_alteration_deg`：设计 30° vs YAML 15°（违反 Rule8 判例 ≥30°）| 🔴 高 |
| **D-5** | M4 可把 Rule14 STARBOARD 替代为 REDUCE_SPEED | 🔴 高 |
| **D-6** | ODD-A `max_turn_rate_deg_s`：设计 12°/s vs YAML 5°/s | 🟠 中 |
| **D-7** | `ConstraintCompiler`（硬约束+TSS polygon）已写**但未接入 MidMpcNode 运行时** | 🔴 高 |
| **D-8** | BC-MPC 已实现**但话题命名空间与 M2 不一致**（`/m2/...` vs `/l3/m2/...`），运行时接线存疑 | 🟠 中 |

4. **目标船避碰（good seamanship target）完全未实现**：所有目标船是 constant course/speed dummy。要验证"本船在对方有良好船艺时是否执行合理"，需引入 Woerner-scoring 驱动的两船交互仿真或 DBN 意图推断。

---

## 第 1 章　法规与工程基线（高置信度来源）

### 1.1 COLREGs 法规层（固定值，不接受 HAZID 调整）

| 项目 | 法规/判例要求 | 来源 | 置信度 |
|---|---|---|---|
| Rule 8(b) "readily apparent" | 转向 ≥ **30°**，判例偏好 **40–60°** | *The Roseline* / *The Oden* / *The Hakki Deval*；COLREG Rule 8(b) | 🟢 A 级 |
| Rule 16 "ample time" | 让路船最迟 **C-12（碰撞前 12 min）** 动作 | *The Samco Europe* / *The Rickmers Genoa* / *The Topaz* | 🟢 A 级 |
| Rule 17 独立避让 | 直航船在 **C-9 ~ C-5** 区间独立动作 | 同上判例集 | 🟢 A 级 |
| Rule 14 对头 | 双方右转，几何即触发，无需 CPA 阈值 | COLREG Rule 14(a) 字面 | 🟢 A 级 |
| Rule 19 能见度不良 | 覆盖 Rule 13–17；避免对正横前方目标左转 | COLREG Rule 19(d) | 🟢 A 级 |

**关键区分**：COLREGs 原文**不给出统一的 CPA/TCPA 数值阈值**（Rule 7 要求"所有可用手段"判断风险，Rule 8 要求"安全通过距离"但无数值）。CPA/TCPA 数值属于**工程参数 / 公司 SMS / 船长 standing order**，不属于国际法固定值。🟢（来源：33 CFR 83.07/83.08；NLM `maritime_regulations` 笔记本，high confidence）

### 1.2 工程与学术基线（用于 FCB 初始校准）

| 阶段框架 | 距离/时间阈值 | 适用对象 | 来源 |
|---|---|---|---|
| **Cockcroft 四阶段**（被引最广的工程框架）| Stage 2 give-way 动作 **5–8 nm**；Stage 3 直航船独立动作 **2–3 nm**；Stage 4 极近危险 | 开阔水域通用 | Cockcroft & Lameijer (2011) *A Guide to the Collision Avoidance Rules*；🟢 |
| **Wang 2021 直航船四阶段理论** | free sailing → risk detection → give-way action period → stand-on mandatory action period；用 TCPA 划分阶段 | 直航船决策时机 | Wang et al. (2021) JMSE 9(6):584 [R17]；🟢 A 级（网络核验确认论文存在与框架）|
| **集装箱船动作点实测** | 开阔海域 TCPA 均值 **30.1 min**；受限海域 **19.7 min** | 大型商船 | Appl. Sci. 11(16):7299；🟢 B 级 |
| **P&I 实操** | CPA ≤ **0.5 nm** 视为存在碰撞风险 | 值班官 standing order | North of England P&I；🟡 B 级 |
| **MASS 保守基准（KASS）** | 雷达 CPA **4.0 nm** 时移交人工 | MASS 现场试验 | KASS 项目；🟡 B 级 |
| **Kim 2023 LOA 域** | 最小安全距离 **9 × LOA** | 学术模型 | Kim (2023)；🟡 B 级 |
| **FCB (LOA=45m) 折算** | 9×LOA ≈ **405 m ≈ 0.22 nm** | 本船型 | 推算 |

### 1.3 FCB 船型特殊性（45m 高速半滑行）

- 服务航速 ~18 kn（vs 商船 12–15 kn）：相同 TCPA 下两船接近距离更远（18+12=30 kn 合速度时，TCPA=12min 对应 ~6 nm）
- ROT_max ≈ 12°/s（代码 `kDefaultRotMaxRadS = 0.2094`，架构 §10.4 确认）
- 制动性能：半滑行船停机-倒车非线性，架构 §10.6 明确"不能线性化近似"

**对本船型的阈值建议**（仅作 HAZID 输入初值，🟡 Medium，待 RUN-001 校准）：

| ODD | 监视 TCPA | 规划 TCPA | 动作 TCPA | 硬 CPA | soft CPA |
|---|---|---|---|---|---|
| ODD-A | 24–30 min | 18–20 min | **12–15 min**（≥C-12 判例）| **1.0 nm (1852m)** | 1.5 nm |
| ODD-B | 8–12 min | 6–8 min | 4–6 min | 0.3 nm (556m) | 0.6 nm |

来源：附件《COLREGs 参数建议.md》§工程落地阈值 + NLM 调研交叉确认；🟡（组合来源，非单点）。

---

## 第 2 章　架构设计中的避碰业务流程（权威来源：架构报告）

**架构文件**：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`（§3.3, §6.3, §8, §9, §10）

### 2.1 闭环决策链（设计意图）

```
M1 ODD → M2 WorldModel → M6 COLREGs Reasoner → M4 Behavior Arbiter → M5 Tactical Planner → L4/L5
                                                                  ↑
                                                                  M7 Safety Supervisor（独立监督）
```

### 2.2 设计中的 ODD-A 初始阈值（架构 §3.3 + §9.3）

| 参数 | 设计值（ODD-A）| 文件位置 | 标注 |
|---|---|---|---|
| CPA_safe | **1.0 nm (1852m)** | §3.3 表 + §10.4 | `[F-P1-D6-017] 初始设计值` |
| TCPA | **12 min** | §3.3 表 | 初始设计值 |
| T_standOn | 8 min | §9.3 表 | Wang 2021 [R17] |
| T_act | 4 min | §9.3 表 | Wang 2021 [R17] |
| min_alteration_deg | **30°** | §9.3 表 | "Rule 8 大幅工业实操量化 AoU 20–45°" |
| Rule 19 触发 | 1.0 nm | §9.3 表 | "固定，不接受 HAZID 调整" |

### 2.3 M6 设计五层推理（架构 §9.2 图9-1）

```
层1 适用规则判定（ODD 选规则集）
层2 会遇分类（Rule13/14/15 几何）
层3 责任分配（Rule16/17/18）
层4 行动方向（Rule8 大幅右转 ≥30°）
层5 时机判定（Rule17 三阶段：TCPA>T_standOn 保向 / T_standOn≥TCPA>T_act 警告 / TCPA≤T_act 独立避让）
```

### 2.4 M5 设计约束（架构 §10.4）

```
s.t. CPA(ψ_k) ≥ CPA_safe(ODD)    # ← 设计声明为硬约束
     speed_k ≤ speed_limit(ODD)
     |Δψ_k| ≤ ROT_max × Δt
```

---

## 第 3 章　代码中的真实执行链路（实测）

**证据**：直接读取 `src/l3_tdl_kernel/m6_colregs_reasoner/`、`m4_behavior_arbiter/`、`m5_tactical_planner/` 源码 + `config/odd_aware_thresholds.yaml`。

### 3.1 实际运行频率（实测）

| 模块 | 频率 | 证据 |
|---|---|---|
| M6 reasoning | **2 Hz**（500ms）| `reasoning_period_ms = 500` |
| M4 arbitration | **4 Hz**（250ms）| `m4.arbitration.interval_ms = 250` |
| M5 Mid-MPC | 1 Hz（N=18, dt=5s, horizon 90s）| `mid_mpc_nlp_formulation.cpp` |
| M5 BC-MPC | **10 Hz**（100ms）| `bc_mpc_node.cpp:18 kTickInterval_s = 0.1` |
| M2 WorldState | 4 Hz（设计）| 架构 §6.4 |

### 3.2 M6 Rule14 实际判定逻辑（`rule14_head_on.cpp`）

**纯几何，无 TCPA/CPA gate**：

```cpp
// Check 1: 航向差接近 180°（±6°）
kReciprocalCourses = |courseDiff - 180°| < 6.0
// Check 2: 目标在正前方（相对方位 ±6°）
kHeadOnBearing = (relBearing < 6.0 || relBearing > 354.0)
// Check 3: 目标船首朝向我们（aspect ±10°）
kReciprocalAspect = aspect < 10.0 || aspect > 350.0

if (三者均成立):
  role = BOTH_GIVE_WAY
  preferred_direction = "STARBOARD"
  min_alteration_deg = params.min_alteration_deg   // ← 来自 YAML
```

**确认**：Rule14 的 `evaluate()` 本身不判断 TCPA/CPA。这与附件 MD 一致，也与架构 §9.2 层2（纯几何分类）一致。**时机判断不在 Rule14 本体，而在下游 latch + phase classifier**。

### 3.3 真正的"是否动作"靠 RuleLatch（`rule_latch.hpp` + `colregs_reasoner_node.cpp:548-685`）

**Onset 条件（全部 AND）**：
```cpp
rule_active (Rule14 几何命中)
  AND cpa_m < cpa_safe_m        // ← 关键 gate
  AND range_closing             // 当前距离 < 上一周期距离
  AND !past_and_clear           // 未让清
→ latched = true
→ 快照 onset 分类（role/encounter/direction）固定（Rule 13(d)）
```

**Release 条件**：
```cpp
// 主释放（Rule 16 finally past and clear）：
!range_closing AND past_and_clear AND cpa_m >= cpa_safe_m
// 投影备份释放（仅 give-way/BOTH_GIVE_WAY）：
!range_closing AND cpa_projection_past_and_safe
```

**这是实际的状态机**——附件 MD 称"状态机缺失"已过时。latch 实现了 onset-hold-release 迟滞，防止本船右转时 Rule14 几何脱落导致的 AVOID↔TRANSIT 抖动。

### 3.4 PhaseClassifier（`colregs_phase_classifier.cpp`）——存在但仅对 stand-on 起作用

```cpp
if (tcpa_s > t_standOn_s)  → PRESERVE_COURSE
if (tcpa_s > t_act_s)      → SOUND_WARNING
if (tcpa_s > t_emergency_s) → INDEPENDENT_ACTION
else                       → CRITICAL_ACTION
```

**关键**：这个 phase 分类用于 Rule17（stand-on 直航船三阶段）。对 Rule14（BOTH_GIVE_WAY），`requires_action()` 在 `constraint_generator.cpp:30-35` 只看 `give_way || standon_inextremis`，**不看 timing phase**。也就是说 Rule14 一旦 latch 就立即 requires_action，不受 TCPA 阶段门控。

### 3.5 实际 YAML 阈值（`config/odd_aware_thresholds.yaml`，主 checkout）

| 参数 | ODD-A 实际值 | 设计文档值 | 偏离 |
|---|---|---|---|
| **cpa_safe_m** | **1000.0** | **1852.0** | 🔴 **-46%** |
| t_standOn_s | 480 (8min) | 480 (8min) | ✅ 一致 |
| t_act_s | 240 (4min) | 240 (4min) | ✅ 一致 |
| t_emergency_s | 60 (1min) | 60 | ✅ 一致 |
| **min_alteration_deg** | **15.0** | **30.0** | 🔴 **-50%** |
| max_speed_kn | 20.0 | — | — |
| **max_turn_rate_deg_s** | **5.0** | **12.0** (架构 §10.4 ROT_max) | 🔴 -58% |

**注意**：代码 fallback（`get_current_rule_params()` 默认）是 `cpa_safe_m = 1852.0`，但 YAML 实际覆盖为 `1000.0`，注释写 "calibrated baseline for integration tests"。**运行时生效的是 1000m**。

### 3.6 M4 行为仲裁（`behavior_arbiter_node.cpp` + `colregs_directive.cpp`）

**Rule14 检测后权重提升**：
```cpp
if (applicable_rule == "Rule 14"):
  colreg_avoidance_weight_ = 0.85   // 从默认 0.70 提升
```

**heading window 生成**（右转区间）：
```cpp
STARBOARD: allowed = [base + required_deviation, base + 180°]
required_deviation = max(min_alteration_deg, asin(cpa_safe/range) × boldness_factor)
```

**关键偏离 D-5（附件 MD 指出，代码确认）**：`apply_primary_risk_guidance()` 在以下条件把 STARBOARD 改成 REDUCE_SPEED：
```cpp
can_reduce_speed (give-way/both-give-way)
  AND tcpa_s > 180.0 (3 min)      // ← ample_tcpa
  AND !is_danger_or_critical
  AND (speed_reduction_improves_margin OR speed_reduction_arrests_closing)
→ directive.direction = REDUCE_SPEED
```

对 Rule14 对头，这可能导致**本船减速而非右转**——违反 Rule14(a) "双方右转"的字面要求。

### 3.7 M5 Mid-MPC 实际实现（`mid_mpc_nlp_formulation.cpp`）

**🔴 D-2 关键发现**：COLREGs 在 Phase E1 是 **soft cost**，不是 hard constraint：

```cpp
// build_colreg_cost_(): per (target, step):
//   tw · disc_k · exp(-zeta · (d - cpa_safe))
// 注释明写："Phase E1: COLREGs rules handled as soft cost in J_colreg;
//            hard constraints deferred to Phase E2."
```

只有 ROT 微分（`|Δψ_k| ≤ rot_max·dt`）是 hard constraint。heading/speed 是 box bound（lbx/ubx）。

**starboard 偏置**（`build_asym_cost_()`）：softplus 港侧惩罚，乘 `give_way` flag，仅在 Rule14/15 give-way 时激活。这是对头右转的软引导。

**🔴 D-7 关键发现**：`ConstraintCompiler`（含 `compile_rule14/15/16/17` 硬约束 + `compile_zone_constraints` TSS polygon）**已完整实现**，但**未接入 MidMpcNode 运行时**——`grep ConstraintCompiler` 在 `src/mid_mpc/` 无任何引用。硬约束模块"已写未接线"。

### 3.8 M5 BC-MPC 实际实现（`bc_mpc_node.cpp`）

**D-8 核实**：BC-MPC（短程紧急避让层，架构 §10.5）已完整实现（branch formulation + collision detector + solver + override generator + node），10 Hz 运行，CMake 编译并安装 `m5_bc_mpc_node`。

**但话题命名空间不一致**：
- BC-MPC 订阅：`/m2/world_state`、`/m5/avoidance_plan`
- M2 实际发布：`/l3/m2/world_state`
- Mid-MPC 实际发布：`/m5/avoidance_plan` ✅（这条一致）

`/m2/world_state` vs `/l3/m2/world_state` 不一致意味着 BC-MPC **可能收不到 WorldState**（取决于 compose 是否有 remap）。需在 compose 层核实 BC-MPC 是否启动及话题映射。

### 3.9 M5 CPA 边界动态提升（附件 MD 此点正确）

`mid_mpc_node.cpp:258-272` 确认：
```cpp
if (inp.colregs_conflict_active):
  cpa_safe = 2500.0;                          // 从 fallback 提升到 2500m
  // 对主目标：
  tgt.cpa_m  = max(tgt.cpa_m * 0.2, 50.0);    // CPA ×0.2 放大 cost 权重
  tgt.tcpa_s = max(tgt.tcpa_s * 0.2, 10.0);   // TCPA ×0.2 放大 cost 权重
```

附件 MD 此点描述准确。

---

## 第 4 章　Rule14 对头流程的 6 阶段对照（截图 vs 代码）

截图显示 Rule14 典型流程 6 阶段：识别 → 规则判定 → 及早动作 → 监视校核 → 安全通过 → 恢复航线。

| 截图阶段 | 代码实现 | 状态 | 证据 |
|---|---|---|---|
| **1. 识别**（远距发现对向目标）| M2 几何预分类（`bearing < 6° + heading_diff > 170°`）+ 进入 WorldState | ✅ 实现 | 架构 §6.3.1；M6 `convert_world_state()` |
| **2. 规则判定**（Rule14 几何成立）| `rule14_head_on.cpp` 三条件判定 → BOTH_GIVE_WAY/STARBOARD | ✅ 实现 | §3.2 |
| **3. 及早动作**（进入避让）| **RuleLatch onset**: 几何命中 AND `cpa<cpa_safe` AND closing → latch → conflict_detected | ⚠️ 部分实现：无显式 TCPA≤12min 门（D-3）；靠 CPA+closing 触发 | §3.3 |
| **4. 监视校核**（CPA 是否回升）| RuleLatch 持有 onset 分类到 past_and_clear；M4 release dwell（4 cycles）| ✅ 实现 | `rule_latch.hpp`；`behavior_arbiter_node.cpp:278` |
| **5. 安全通过**（finally past and clear）| `past_and_clear_from_heading()` (bearing > 112.5° abaft beam) AND `!closing` AND `cpa>=cpa_safe` | ✅ 实现（含 encounter reference heading 防误释放）| §3.3；`colregs_reasoner_node.cpp:568-618` |
| **6. 恢复航线** | latch release → conflict_detected=false → M4 回 TRANSIT → M5 回归 planned route | ✅ 实现 | latch release 逻辑 |

**截图 vs 代码的差距**：截图阶段 3 的 TCPA 阈值门（"TCPA≤12min 或 CPA<1nm 才进入动作"）在代码中**不存在**。代码是"几何+CPA+closing 即动作"，可能导致：
- 目标很远但 CPA≈0 且 closing → **过早动作**（与 MD §结论一致）
- 目标晚进入 WorldState → **过晚动作**

---

## 第 5 章　设计偏离与实现偏离分析（核心章节）

### 偏离总表

| ID | 偏离项 | 设计文档 | 代码实际 | 影响 | 严重度 |
|---|---|---|---|---|---|
| **D-1** | ODD-A CPA_safe | 1852m (1.0nm) | 1000m | latch 比设计晚触发；FCB 高速下动作距离不足 | 🔴 高 |
| **D-2** | M5 COLREGs 约束性质 | hard constraint `CPA≥CPA_safe` | soft cost barrier | CPA 可能被代价权衡穿透；MPC 可能选"贴近"轨迹 | 🔴 高 |
| **D-3** | Rule14 触发 TCPA gate | §9.2 层5 时机判定（TCPA 分段）| 无 TCPA gate，靠 CPA+closing | 过早/过晚触发；不符合 Rule16 "ample time" | 🟠 中高 |
| **D-4** | ODD-A min_alteration_deg | 30° | 15° | 动作不明显，违反 Rule8 判例 ≥30° | 🔴 高 |
| **D-5** | M4 give-way→减速替代 | （未明确禁止）| `tcpa>180s` 时 STARBOARD→REDUCE_SPEED | Rule14 可能变成纯减速，违反 Rule14(a) 右转 | 🔴 高 |
| D-6 | max_turn_rate_deg_s | 12°/s | 5°/s | ROT 受限，大角度转向慢 | 🟠 中 |
| **D-7** | ConstraintCompiler 硬约束+TSS | §10.4 硬约束 + §10.7 TSS polygon | 已写**未接入 MidMpcNode** | D-2 的根因；硬约束层形同虚设 | 🔴 高 |
| **D-8** | BC-MPC 接线 | §10.5 短程紧急层 | 已实现**但话题命名空间 `/m2/...` vs `/l3/m2/...` 不一致** | 紧急避让层可能收不到 WorldState | 🟠 中 |

### D-1 CPA_safe 偏离详析

**证据**：
- 设计：架构 §3.3 `CPA ≥ 1.0nm` + §10.4 `CPA_safe(ODD-A) = 1.0 nm`
- 代码 fallback：`get_current_rule_params()` 默认 `1852.0`（正确）
- **运行时 YAML**：`odd_aware_thresholds.yaml:8 cpa_safe_m: 1000.0  # calibrated baseline for integration tests`

**根因推测**：YAML 为集成测试调小到 1000m 以让场景更快触发，但**未在 HAZID 后回填**。注释仍标 `[TBD-HAZID]`。

**影响**：FCB 18kn 对 12kn 目标，合速 30kn。CPA_safe=1000m 对应 TCPA≈1.1min 才到 CPA 点——Latch 实际触发靠 range（远大于 CPA），但 release 靠 `cpa>=cpa_safe`，1000m 会让 release 过早，导致**返航时二次触发**（仿真观察到的"返航行为异常"可能部分源于此）。

### D-2 soft vs hard constraint 详析

**证据**：`mid_mpc_nlp_formulation.cpp:126-128` 注释：
```
// Phase E1: COLREGs rules handled as soft cost in J_colreg;
// hard constraints deferred to Phase E2.
```

**设计声明**（架构 §10.4）：`CPA(ψ_k) ≥ CPA_safe(ODD)` 写在 `s.t.`（约束）块。

**根因关联（D-7）**：硬约束编译器 `ConstraintCompiler` 已写好（含 `compile_rule14/15/16/17`），但 MidMpcNode 仍用旧的 `mid_mpc_nlp_formulation.cpp`（soft cost），**未切换到 ConstraintCompiler**。这是 Phase E1→E2 迁移未完成的产物。

**影响**：soft barrier `exp(-ζ(d-cpa_safe))` 在 d<cpa_safe 时指数增长但**不强制**。若 destination cost / velocity cost 权重大，MPC 可能选一条"擦边"轨迹。这是 SIL 测试中避碰失败的核心嫌疑之一。

**置信度**：🟢（代码注释 + 公式直接证据 + ConstraintCompiler 未接入 grep 证据）

### D-3 Rule14 缺 TCPA gate 详析

**证据**：
- 设计 §9.2 层5：Rule17 三阶段用 TCPA（T_standOn/T_act）。
- 代码：`rule14_head_on.cpp` 无 TCPA；`PhaseClassifier` 存在但 `requires_action()`（`constraint_generator.cpp:30`）对 give-way 不查 phase。
- RuleLatch onset 条件：`rule_active AND cpa<cpa_safe AND range_closing`——**无 TCPA**。

**影响**：无法保证 Rule16 "ample time"（判例 C-12）。目标远距但 CPA≈0 + closing 时立即 latch。

**置信度**：🟢（代码直接证据）

### D-4 min_alteration 15° vs 判例 30°

**证据**：
- YAML：`min_alteration_deg: 15.0`
- 设计 §9.3：30°，依据 "Rule 8 大幅工业实操量化 AoU 20-45°"
- 判例法：≥30°（*The Roseline* 等），🟢 A 级

**影响**：15° 在雷达/视觉上可能不够"readily apparent"（Rule 8(b)），且 FCB ROT_max 若按 YAML 5°/s，15° 需 3s 完成——对方可能误判为航向修正而非避让。

### D-5 give-way 减速替代详析

**证据**：`colregs_directive.cpp:129-163 apply_primary_risk_guidance()`：
```cpp
if (can_reduce_speed && ample_tcpa(>180s) && outside_danger &&
    (improves_margin || arrests_closing))
  directive.direction = REDUCE_SPEED;
```

**影响**：Rule14（BOTH_GIVE_WAY）满足 `can_reduce_speed`。若 `tcpa>3min` 且减速能改善 margin，**STARBOARD 被覆盖为 REDUCE_SPEED**。Rule14(a) 要求双方右转——减速可作为辅助但不应替代右转方向。

**置信度**：🟢（代码直接证据）

### D-7 ConstraintCompiler 未接入详析

**证据**：
- `shared/constraint_compiler.hpp` + `.cpp` 完整实现 `compile_rule14/15/16/17` + `compile_zone_constraints`（TSS polygon，含 `decompose_polygon` 凸分解 + `point_inside_convex` 半平面测试）
- 单元测试 `test_constraint_compiler.cpp` 覆盖凸/非凸 polygon、Zone 约束
- **但** `grep ConstraintCompiler` 在 `src/mid_mpc/` 零命中——MidMpcNode 仍用 `mid_mpc_nlp_formulation.cpp` 自带的 soft cost

**影响**：这是 D-2 的直接根因。硬约束层"已写未接线"，意味着 Phase E1→E2 迁移停在半路。Rule14 的 starboard 硬偏置、Rule16 的"前 N/2 步转 10°"硬约束、TSS lane polygon 全部未生效。

**置信度**：🟢（grep 零命中 + 文件存在 + 单测存在）

### D-8 BC-MPC 话题命名空间详析

**证据**：
- `bc_mpc_node.cpp:34` 订阅 `/m2/world_state`
- M2 实际发布 `/l3/m2/world_state`（见 M6/M4 订阅均为 `/l3/m2/...`）
- BC-MPC 订阅 `/m5/avoidance_plan` ✅ 与 Mid-MPC 发布一致

**待核实**：compose 启动文件是否有 remap 把 `/m2/world_state` 映射到 `/l3/m2/world_state`。若无，BC-MPC 收不到 WorldState，紧急避让层形同虚设。

**置信度**：🟡（命名空间不一致已确认；remap 存在与否待查 compose）

### 设计意图 vs 实现的成熟度对照

| 附件 MD 的结论 | 代码实际 | 更新结论 |
|---|---|---|
| "状态机目前还缺失" | RuleLatch + PhaseClassifier + duty latch + standon latch 实现 | ❌ **MD 过时**：onset-hold-release 状态机已实现 |
| "CPA_safe=1000m 小于设计 1nm" | YAML 确认 1000m | ✅ MD 正确 |
| "Rule14 无 TCPA=12min 门控" | 确认无 TCPA gate | ✅ MD 正确 |
| "min_alteration=15°" | YAML 确认 15° | ✅ MD 正确 |
| "M4 可能把右转改成减速优先" | `apply_primary_risk_guidance` 确认 | ✅ MD 正确 |
| "M5 把 CPA 安全边界临时提高到 2500m" | `mid_mpc_node.cpp:261` 确认 | ✅ MD 正确 |
| "BC-MPC 未实现" | **已实现**（branch + detector + solver + override + node）| ❌ **MD 过时**：BC-MPC 已实现，但接线存疑 |
| "TSS polygon 未实现" | **ConstraintCompiler 已写**（含 polygon 分解），但未接入 | ⚠️ MD 部分过时：已写未接线 |

---

## 第 6 章　目标船避碰扩展（回答问题 1）

### 6.1 现状

当前所有目标船是 **constant course/speed dummy**（M2 只接收 TrackedTargetArray，不做目标船行为建模）。这意味着：
- 本船的 stand-on（Rule17）行为**无法被正确验证**——因为目标船（give-way）永远不会主动让路
- Rule14 双方右转的"双方"语义只能验证本船一半
- 无法测试"本船动作是否把目标船逼入不安全姿态"（good seamanship 的双向性）

### 6.2 扩展方案（高置信度来源：NLM `colav_algorithms` 🟢 + 网络核验）

| 方案 | 描述 | 验证能力 | 实现成本 |
|---|---|---|---|
| **A. Imazu problem table / Imazu-22** | 标准化测试几何（对头/交叉/追越），目标船预设轨迹 | 基线合规性 | 低（场景脚本）|
| **B. Woerner 2019 scoring** | 对 Rule7/8/13-17 的量化评分，可在线评估目标船合规度 | 客观打分 | 中（需实现 scorer）|
| **C. NTNU 逆向生成 + DBN 意图推断** | 从预期碰撞点反推目标初态；DBN 推断目标意图（合规/良好船艺/异常）| 最强验证 | 高 |
| **D. 两船交互仿真（同协议）** | 目标船也跑 COLREGs 避碰协议（如 PS-MPC / DSSA），双方各执行职责 | 真实双向验证 | 高 |

**推荐路径（🟡 Medium，组合来源）**：
1. **短期**：方案 A（Imazu 场景）+ 方案 B（Woerner scorer）——用标准化几何 + 客观打分验证本船单边合规
2. **中期**：方案 D 轻量版——目标船跑简化 Rule14/15 决策器（复用 M6 逻辑），实现"双方各让"
3. **长期**：方案 C——DBN 意图推断，让本船能区分"目标在让路"vs"目标异常"

**关键阈值参考**（Cockcroft 四阶段，🟢）：give-way 在 **5–8 nm** 动作，stand-on 在 **2–3 nm** 独立动作。这直接定义了目标船 give-way 行为的触发距离。

---

## 第 7 章　建议的最小修补点（优先级排序）

基于偏离分析，按 ROI 排序：

| 优先级 | 修补项 | 改动位置 | 预期效果 |
|---|---|---|---|
| **P0** | D-1: YAML `cpa_safe_m` ODD-A 从 1000m 改回 **1852m** | `odd_aware_thresholds.yaml:8` | latch/release 距离回归设计；返航二次触发缓解 |
| **P0** | D-4: `min_alteration_deg` ODD-A 从 15° 改回 **30°** | `odd_aware_thresholds.yaml:7` | 符合 Rule8 判例；动作明显性 |
| **P1** | D-7/D-2: MidMpcNode 接入 `ConstraintCompiler`，COLREGs 从 soft cost 切到 **hard constraint**（Phase E2 完成）| `mid_mpc_node.cpp` + `mid_mpc_nlp_formulation.cpp` | 防止 CPA 被穿透；SIL 失败核心嫌疑 |
| **P1** | D-5: M4 对 Rule14（BOTH_GIVE_WAY）**禁止 STARBOARD→REDUCE_SPEED 全替代**；减速仅作 speed constraint 附加 | `colregs_directive.cpp:146-162` | 保证 Rule14 右转方向不被吞 |
| **P2** | D-3: Rule14 onset 增 TCPA gate（如 `tcpa ≤ 720s` 才允许 latch 进入 action）| `rule_latch.hpp` 或 reasoner node | 符合 Rule16 ample time |
| **P2** | D-6: `max_turn_rate_deg_s` ODD-A 从 5°/s 校准到 **12°/s**（FCB 实测）| `odd_aware_thresholds.yaml:10` | 大角度转向及时完成 |
| **P2** | D-8: 核实 BC-MPC compose 启动与话题 remap（`/m2/...` → `/l3/m2/...`）| compose / launch 文件 | 确保紧急避让层在线 |

**HAZID 校准提醒**：所有阈值仍标 `[TBD-HAZID]`。架构附录 E 已把 ODD-A/B/C/D CPA/TCPA 列入 HAZID RUN-001（2026-08-19）校准任务。**在 HAZID 前，上述 P0/P1 修补是让代码回到设计基线，不是冻结认证值。**

**⚠️ 8-probe 测试基线影响**：P0 改 YAML（1852m/30°）会改变现有 8-probe 场景的触发时机与避让幅度，可能导致当前 8-probe 证据基线失效。**建议先在独立 worktree 验证 P0 影响后再合并**，不要直接在主 checkout 改。

---

## 第 8 章　置信度声明与来源清单

### 高置信度（🟢）结论
- 所有"代码实际行为"结论：直接读源码 + YAML，A 级
- Rule8 ≥30°、Rule16 C-12、Rule14 双方右转：COLREGs 字面 + 判例法，A 级
- Cockcroft 四阶段 5-8nm/2-3nm：Cockcroft & Lameijer (2011)，A 级，网络核验
- Wang 2021 四阶段理论存在：JMSE 9(6):584，A 级，网络核验论文存在
- D-7 ConstraintCompiler 未接入：grep 零命中 + 文件/单测存在，A 级
- D-8 BC-MPC 已实现：文件树 + CMake + main 入口，A 级

### 中置信度（🟡）结论
- FCB 具体阈值建议表（§1.3）：组合来源（Cockcroft + P&I + 学术），非单点；待 HAZID
- 目标船扩展推荐路径：NLM `colav_algorithms` high confidence，但选型含工程判断
- D-8 BC-MPC 话题 remap 存在与否：命名空间不一致已确认，compose remap 待查

### 低置信度 / 待核实（🔴）
- Wang 2021 具体 TCPA 数值（分钟）：**未取到原文具体数字**，只确认四阶段框架。架构 §9.3 引用的 "Wang 2021 + Imazu 1987, AoU ODD-A 6-12 min" 需取原文核实
- D-8 compose 是否 remap `/m2/...` → `/l3/m2/...`：需读 docker-compose / launch 文件

### 来源清单

**法规/判例（A 级）**：
- 33 CFR 83.07 (Rule 7), 83.08 (Rule 8), 83.14 (Rule 14), 83.19 (Rule 19) — eCFR
- *The Samco Europe* / *The Rickmers Genoa* / *The Topaz*（C-12 判例）— via NLM `maritime_regulations`
- *The Roseline* / *The Oden* / *The Hakki Deval*（Rule8 ≥30° 判例）— via NLM

**工程/学术（A-B 级）**：
- Cockcroft & Lameijer (2011) *A Guide to the Collision Avoidance Rules* — 四阶段框架
- Wang et al. (2021) JMSE 9(6):584 [R17] — 直航船四阶段
- Woerner et al. (2019) — COLREGs 协议评分
- Eriksen, Bitar, Breivik et al. (2020) Frontiers in Robotics & AI 7:11 [R20] — BC-MPC
- Appl. Sci. 11(16):7299 — 集装箱船动作点实测

**项目内部**：
- 架构报告 `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` §3.3/§6.3/§8/§9/§10
- 代码 `src/l3_tdl_kernel/m6_colregs_reasoner/`（rule14/latch/phase_classifier/constraint_gen/node）
- 代码 `src/l3_tdl_kernel/m4_behavior_arbiter/`（colregs_directive/behavior_arbiter_node）
- 代码 `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/`（nlp_formulation/node）
- 代码 `src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/`（node/solver/detector）
- 代码 `src/l3_tdl_kernel/m5_tactical_planner/src/shared/constraint_compiler.cpp`（未接入）
- 配置 `src/l3_tdl_kernel/m6_colregs_reasoner/config/odd_aware_thresholds.yaml`

**NLM 笔记本**：`domain:maritime_regulations`（124 sources, high）、`domain:colav_algorithms`（140 sources, high）

**网络核验**：
- [Wang 2021 JMSE 9(6):584](https://www.mdpi.com/2077-1312/9/6/584)
- [Cockcroft 四阶段（ResearchGate 图）](https://www.researchgate.net/figure/Collision-situation-stages-adapted-from-Cockcroft-and-Lameijer-2011-The-permissible_fig1_352902492)
- [Blue Moment COLREGS 4 stages PDF](https://www.bluemoment.com/downloads/COLREGS_close_encounters.pdf)
- [33 CFR 83.07 Rule 7](https://www.ecfr.gov/current/title-33/chapter-I/subchapter-E/part-83/subpart-B/subject-group-ECFRc711a0393c57020/section-83.07)
- [33 CFR 83.08 Rule 8](https://www.ecfr.gov/current/title-33/chapter-I/subchapter-E/part-83/subpart-B/subject-group-ECFRc711a0393c57020/section-83.08)
- [Appl. Sci. 11(16):7299 动作点研究](https://www.mdpi.com/2076-3417/11/16/7299)

---

**报告结束。**

下一步可选：(a) 执行 P0 修补（独立 worktree）；(b) 核实 D-8 compose remap；(c) 落实目标船扩展方案 A+B。建议先 (b) 补全 D-8，再决定 P0——因为 P0 改 YAML 会影响现有 8-probe 测试基线。
