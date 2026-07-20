# L0 Grilling 结论:上游场景与任务输入层

> **关联**:决策日志 Step1.8;架构文档 §3 L0
> **状态**:L0 grilling 完成,修复方案确定,待实施
> **L0 定性**:功能基本健康(数据能 pack 进 MidMpcInput),不阻塞 C1。3 类防御性缺陷需现在修(用户决策:每层合理可闭环,每个来源都有 guard + 一致性)。

---

## 1. L0 输入内容完整梳理(5 adapter 全字段)

### L0.1 World Model Adapter(M2 NavFilter + targets)

| 字段 | 来源 | 转换 | 现有 guard | 缺失 guard |
|---|---|---|---|---|
| own_ship.x_m/y_m | kIdxX0/Y0 固定 (0,0) | own-relative | — | — |
| own_ship.psi_rad | M2 `heading_deg` | ×kRadPerDeg | — | **NaN/Inf 无 guard**(:514) |
| own_ship.u_mps | M2 `sog_kn` 或 `speed_kn` | ×kMsPerKn | sog_kn<0 时退 speed_kn(:518) | **负 sog 无 guard** |
| targets[].x_m/y_m | M2 target lat/lon | lat/lon 差×kEarthRadius | — | **NaN lat/lon 无 guard**(:526-528) |
| targets[].sog_mps | M2 `sog_kn` | ×kMsPerKn | — | **负 sog 无 guard**(:529) |
| targets[].cog_rad | M2 `cog_deg` | ×kRadPerDeg | — | — |

### L0.2 Behavior Intent Adapter(M4 BehaviorPlan)

| 字段 | 来源 | 转换 | 现有 guard | 缺失 guard |
|---|---|---|---|---|
| heading_min/max_rad | M4 `heading_min/max_deg` | ×kRadPerDeg + `resolve_heading_box_bounds` | full-circle→[-π,+π];保证 lb≤ub(:551-554) | — |
| speed_min_mps | M4 `speed_min_kn` | ×kMsPerKn | — | **负值无 guard**(:566) |
| speed_max_mps | M4 `speed_max_kn` | ×kMsPerKn | M4 fallback 时用 nominal(:572-576) | **≤0 或 <speed_min 无 guard**(:578) |
| heading_box_reachable_from_psi0_deg | M4 schema 113 | 直通 | sentinel=0 退化 v2.1(下游) | **方向 vs pref_dir 一致性无 assert(GNC Q3)**(:557-558) |
| rot_step_deg | M4 schema 113 | 直通 | — | **≤0 无 guard**(:559-560) |
| min_alt_required_rad | M4 schema 113 | 直通 | — | **≤0 无 guard**(:561-562) |
| earliest_min_alt_k | M4 schema 113 | 直通 | sentinel=0 退化(下游) | **负值/超 N 无 guard**(:563-564) |

### L0.3 COLREGs Semantic Adapter(M6 COLREGsConstraint)

| 字段 | 来源 | 转换 | 现有 guard | 缺失 guard |
|---|---|---|---|---|
| colregs_conflict_active | M6 `conflict_detected` | 直通 | nullptr 检查(:581) | — |
| colregs_primary_role | M6 `primary_role` | 直通 | — | — |
| colregs_preferred_direction | M6 `primary_preferred_direction` | parse | — | — |
| colregs_encounter_state | M6 `encounter_state` | switch by logical name | unknown→fail-closed(:603-606) | ✓ 已有 |
| applicable_rules | M6 `active_rules` | filter 13-17 + Rule17 STAND_ON gate | Rule17 仅 STAND_ON(:622-623) | ✓ 已有 |
| colregs_min_alteration_rad | M6 constraints | max + ×kRadPerDeg | 仅取 >0(:633) | ✓ 已有 |

### L0.4 Route/Mission Adapter(L2 planned_route + speed_profile)

| 字段 | 来源 | 转换 | 现有 guard | 缺失 guard |
|---|---|---|---|---|
| planned_route_bearing_rad | L2 polyline projection | project_own_onto_polyline | proj.valid 检查(:706-708) | ✓ 已有 |
| route_frame_origin/normal | L2 active leg | 几何计算 | — | — |
| route_weight | cross-leg guard | evaluate_cross_leg_guard | crosses_corner→0(:741) | ✓ 已有 |
| lateral_scale_m | 硬编码 400.0 | — | — | **[TBD-HAZID] 未接 ODD msg**(:727) |
| planned_speed_mps | L2 speed_profile | ×kMsPerKn | 无 profile→kDefaultPlannedSpeed(:753-755) | **负 target_speed 无 guard** |

### L0.5 Vessel Capability Adapter(GNC ODD)

| 字段 | 来源 | 转换 | 现有 guard | 缺失 guard |
|---|---|---|---|---|
| rot_max_rad_s | GNC ODD `cruise_max_yaw_rate_deg_s` | ×π/180 | std::max(...,1e-3)(:770) | ✓ 已有 floor |
| decel_max_mps2 | GNC ODD `max_decel_mps2` | 直通 | std::max(...,1e-6)(:771) | ✓ 已有 floor |

### L0 跨来源硬编码常量

| 常量 | 值 | 来源 | 风险 |
|---|---|---|---|
| `kCpaSafeFallback_m` | 1852.0 | 硬编码 :47 | **应与 odd_aware_thresholds.yaml cpa_hard_m 同步** |
| `kDefaultPlannedSpeed_mps` | 5.14 | 硬编码 :51 | 合理 fallback |
| `nominal_speed_kn_` | 10.0(可配) | ROS param :401 | ✓ 可配 |
| `lateral_scale_m` | 400.0 | 硬编码 :727 | [TBD-HAZID] 等接 GncExecutionOdd.max_lateral_offset_m,不阻塞 L0 GATE |

---

## 2. L0 修复方案(3 类)

### 修复 L0-A:输入值 guard(防御 NaN/Inf/负值/越界)+ input_degraded 追溯

**原则**:检测非法值 → spdlog::warn + 用 fallback + 标记 `input_degraded`(追溯机制,利于根因定位)。

**input_degraded 追溯机制设计**:
- `MidMpcInput` 新增 `InputDegradation degraded`(结构体,含 bitmask + 字段名列表)。
- bitmask 定义:`OWN_PSI_DEGRADED | OWN_U_DEGRADED | TARGET_DEGRADED | SPEED_BOX_DEGRADED | REACHABILITY_DEGRADED | PLANNED_SPEED_DEGRADED`。
- L4/L5 消费:degraded 非空时,solution 的 `rationale` 引用 degraded 字段;严重 degraded(如 OWN_PSI_DEGRADED)触发降级评估。
- LX 记录:每次 degraded 写入 X1 Problem Snapshot,供根因追溯。

| 字段 | guard 规则 | fallback | 严重度 | degraded 标记 |
|---|---|---|---|---|
| own_ship.psi_rad | isfinite | 0.0 | 高 | OWN_PSI_DEGRADED |
| own_ship.u_mps | isfinite && ≥0 | 0.0 | 高 | OWN_U_DEGRADED |
| targets[].x_m/y_m | isfinite | 跳过该 target | 中 | TARGET_DEGRADED |
| targets[].sog_mps | isfinite && ≥0 | 0.0 | 中 | TARGET_DEGRADED |
| speed_min/max_mps | isfinite && ≥0 && max≥min | nominal | 高 | SPEED_BOX_DEGRADED |
| rot_step_deg | isfinite && >0 | 退化 v2.1(下游) | 中 | REACHABILITY_DEGRADED |
| min_alt_required_rad | isfinite && ≥0 | 0.0 | 中 | REACHABILITY_DEGRADED |
| earliest_min_alt_k | isfinite && ∈[0,N] | 退化 v2.1 | 中 | REACHABILITY_DEGRADED |
| planned_speed_mps | isfinite && ≥0 | kDefaultPlannedSpeed | 中 | PLANNED_SPEED_DEGRADED |

### 修复 L0-B:box-reach 方向一致性 assert(M4 合约已确认)

**M4 合约调研结论**(agent_b2f04b59):
- `heading_box_reachable_from_psi0_deg` **不携带方向符号**(始终 ≥0,是 preferred-direction 侧的可达偏差幅值,M4 `compute_heading_box_reachability` direction-aware)。
- M5 不能仅凭这个字段做方向 assert,**必须同时读 `colregs_preferred_direction`**。
- M4 `apply_primary_risk_guidance` 可能覆盖 direction 为 ReduceSpeed,M4/M5 两边对 pref_dir 看法可能不一致(但 ReduceSpeed 场景 direction_disabled,no-op,实际风险低)。
- 附带发现:`BehaviorPlan.msg:25` 注释漂移(未同步 v2.2 方向感知语义)→ 需修注释。

**L0-B assert 实现**(基于 M4 合约):
- **只在 lateral COLREG active**(pref_dir ∈ {Starboard, Port})且 `reachability > 0` 时触发。
- 检查"reachability 值合理 + 与 pref_dir 配对一致"(值 ≥0 + pref_dir 有效),**不尝试从 reachability 值反推方向符号**(值始终 ≥0 无符号)。
- 这更像"sanity check"而非真正的方向一致性 assert(GNC Q3 的"隐性合约风险"在实际代码里比 GNC 评审认为的要小)。
- **GNC Q3 风险实际较低**:ReduceSpeed 场景下 M4/M5 都不用 reachability 做 hard bound 决策,direction_disabled 路径是 no-op。
- **真实风险**:`BehaviorPlan.msg:25` 注释漂移 → L0-B 修复含注释同步。

### 修复 L0-C:硬编码常量一致性

| 常量 | 修复 |
|---|---|
| `kCpaSafeFallback_m` | 启动时从 `odd_aware_thresholds.yaml` 读 `cpa_hard_m`,运行时缓存;yaml 读取失败才用硬编码 1852 + warn |
| `lateral_scale_m` | [TBD-HAZID] 保留硬编码 400,加注释明确"等 GncExecutionOdd.max_lateral_offset_m publish 后接入";不阻塞 L0 GATE |

---

## 3. L0 GATE 定义

| 验收项 | 状态 |
|---|---|
| L0-A:所有上游来源字段有 isfinite/range guard,非法值 fail-closed + `input_degraded` 追溯标记 | ⬜ 待实施 |
| L0-B:box-reach 标量 guard(>0)+ pref_dir 配对 sanity check(lateral active 时);`BehaviorPlan.msg:25` 注释同步 | ⬜ 待实施 |
| L0-C:`kCpaSafeFallback_m` 从 yaml 读 + 缓存;yaml 失败用硬编码 + warn | ⬜ 待实施 |
| L0 输入内容完整梳理(本文档 §1) | ✓ 已完成 |
| L0 不阻塞 C1(L0 功能健康,缺陷是防御性) | ✓ 已确认 |
| `input_degraded` 追溯机制设计(本文档 §2 L0-A) | ✓ 已设计 |

---

## 4. L0 grilling 三视角记录(design-grounding Step2)

| 视角 | L0 上游场景与任务输入层 |
|---|---|
| **专家** | MPC 标准流程(NLM[R12])Problem Setup 阶段。L0 的 5 adapter 对应 M2/M4/M6/L2/GNC ODD,职责边界清晰。COLREGs MPC 文献明确:输入数据坐标系/单位一致性是经典 bug 源,必须 OCP 前标准化。 |
| **新手** | L0 独立分出正确(ChatGPT 方案 + GNC Q3 证明)。5 adapter 不拆类(用户决策),集中在 `assemble_input_`,逻辑边界清晰。 |
| **悲观** | 失效边界 1(GNC Q3):box-reach 方向隐性合约,无 sanity assert → M4 future 变化时 M5 静默接受错误符号。失效边界 2:`cpa_hard_m` 硬编码 fallback 与 yaml 漂移。失效边界 3:M4 publish 异常值(speed_max≤0 等)无 guard → OCP infeasible 或数值爆炸。 |
| **机制C** | L0.5 Vessel Capability Adapter 默认最简版失效:默认"直接信任 M4 publish 值无 guard" → M4 异常值(0/负/NaN)直接进 OCP。**修复**:L0-A guard + input_degraded 追溯。 |
| **盲区** | BL-L0-1(5 adapter 拆类,已决策不拆);BL-L0-2(box-reach 方向 assert,M4 调研已确认实现方式);BL-L0-3(cpa_hard_m fallback 同步,L0-C 修复);BL-L0-4(M4 异常值 guard,L0-A 修复)。全部已闭环。 |

---

## 5. L0 测试计划

| ID | 测试 | 输入 | 期望 | Pass/Fail |
|---|---|---|---|---|
| T-L0-1 | own_ship.psi_rad NaN guard | M2 publish heading_deg=NaN | psi_rad=0.0 + OWN_PSI_DEGRADED + warn | guard 触发 + degraded 标记 |
| T-L0-2 | speed_max≤0 guard | M4 publish speed_max_kn=0 | speed_max=nominal + SPEED_BOX_DEGRADED + warn | guard 触发 + degraded 标记 |
| T-L0-3 | speed_max<speed_min guard | M4 publish speed_min=10,speed_max=5 | speed_max=nominal + SPEED_BOX_DEGRADED | guard 触发 |
| T-L0-4 | earliest_min_alt_k 越界 guard | M4 publish earliest_min_alt_k=100(N=80) | 退化 v2.1 + REACHABILITY_DEGRADED | guard 触发 |
| T-L0-5 | box-reach sanity(lateral active) | pref_dir=Starboard + box_reach=0 | 退化 v2.1 ROT-only(现有逻辑) | 不 infeasible |
| T-L0-6 | cpa_hard_m yaml 一致性 | yaml cpa_hard_m=2000 | M5 读 2000 而非硬编码 1852 | 值一致 |
| T-L0-7 | cpa_hard_m yaml 缺失 | yaml 无 cpa_hard_m | M5 用 1852 + warn | fallback + warn |
| T-L0-8 | input_degraded 追溯 | 多字段同时 degraded | solution.rationale 引用 degraded 字段名 | LX 记录可追溯 |
