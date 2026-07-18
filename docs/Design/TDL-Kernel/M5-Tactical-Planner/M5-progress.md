# M5 · Progress · 实现现状

> **定位声明**：本文是 M5 的**实现现状**对照 [M5-spec.md](M5-spec.md) 的设计目标。所有偏离 / 创可贴 / MOCK 记录于此并附 file:line。审计基线 `docs/Doc From Claude/2026-06-08-m1-m8-systemwide-gap-audit.md`（本模块条目已并入下表，但以**当前代码**为准）。

---

## 1. 头部

| 维度 | 说明 |
|---|---|
| 最近更新 | 2026-06-08 |
| Currently Implementing | J_colreg 重设计完成，分支 `fix/m5-nlp-convergence` 待 merge |
| 当前分支 | `fix/m5-nlp-convergence` |
| 主入口文件（Mid）| `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` |
| 主入口文件（BC）| `src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp` |
| 当前 LOC（src）| ≈ 2751（D3.2 报告值；Mid + BC + shared）|

---

## 2. 实现状态矩阵

| 设计职责（对应 spec §）| 状态 | 证据 file:line | 备注 |
|---|---|---|---|
| Mid-MPC NLP N=18/90s 1 Hz（§4.2）| PARTIAL | `mid_mpc_node.cpp:87-92`（1 Hz timer）；`mid_mpc_nlp_formulation.hpp:67`（n_horizon default=12）；`m5_params.yaml n_steps=18` 未接线 | 有效 N 可能 12（60s）非 18（90s）；见 §6 |
| NLP J_colreg 真实代价（§5.1）| REAL | `mid_mpc_nlp_formulation.cpp:110-233` build_colreg_cost_ + build_asym_cost_ | J_colreg 重设计已修：Restoration_Failed 50→0，单测 9/9，e2e GREEN |
| ROT 约束改平滑 lbx/ubx（§5.1）| REAL | `mid_mpc_solver.cpp:99-106`（lbx/ubx per-variable box）；`mid_mpc_nlp_formulation.cpp:218-233`（差分平滑替代 abs()）| J_colreg 重设计一部分 |
| ROT_max 从 Capability Manifest（MUST-5）| REAL | `mid_mpc_node.cpp:36-38`（Manifest 构造）；`:209`（rot_max_rad_s 动态查表）| 海况 hs_m 硬编 0.0：`mid_mpc_node.cpp:208` [TBD-HAZID] |
| M4 TRANSIT → 空计划（§4.2）| REAL | `mid_mpc_node.cpp:243-254` | D-DEMO1 spin fix 已验证 |
| 几何降级 DEGRADED（§4.2）| REAL | `mid_mpc_node.cpp:255-260`；`build_geometric_fallback_plan_` status="DEGRADED" conf=0.6 | D2 fix 已验证无回归 |
| M4 fallback 级联减速修正（§4.2）| REAL | `mid_mpc_node.cpp:160-166`（R3 fix：M4 fallback 时用 nominal speed）| 已实装 |
| SAT-3 轨迹候选 Nomoto（§3.2）| PARTIAL | `mid_mpc_node.cpp:423-457`（pub_sat3_data_，1 Hz not 2 Hz）；`tc.rule_compliant=false`（:441 硬编）；`tc.confidence=1.0F`（:436 硬编）| 1 Hz 非 2 Hz；rule_compliant 未真实评估 |
| BC-MPC 13 候选分支 4-10 Hz（§4.3）| STUB | `bc_mpc_node.cpp`（代码存在编译通过）；`m5_mid_mpc.launch.py`（只启 mid_mpc_node）| 整层在生产中为死代码（见 §5）|
| M1 ODD 门控（ADR-1，§6）| MISSING | `mid_mpc_node.cpp:34-93`（构造函数无 sub_odd_）；`mid_mpc_node.hpp`（无 sub_odd_ 成员）| ADR-1 违反；M5 全程无视 ODD 状态 |
| X-axis VETO 硬门控（ADR-1，§6）| MISSING | `mid_mpc_node.cpp` + `bc_mpc_node.cpp` grep veto = 0 匹配 | M5 不接受 Checker 否决 |
| MRM 向 M7 上报（MUST-9，§6）| MOCK | `mid_mpc_solver.cpp:124-125, 149-153`（`spdlog::critical` 仅日志）| 无 ROS2 发布到 M7 topic；MUST-9 是日志桩 |
| AvoidancePlan waypoint CMM 字段（§3.3）| PARTIAL | `mid_mpc_waypoint_generator.cpp:87-129`（NLP path 无 schema_version/stamp/confidence/rationale）；`mid_mpc_node.cpp:354-367`（fallback path 有填充）| NLP 成功路径 waypoint CMM 字段空 |
| cost_colreg / cost_dist / cost_vel 字段（§3.3）| STUB | `mid_mpc_nlp_formulation.cpp:337-338`（Phase E1 comment：未从 CasADi stats 拆出，恒 0）| rationale 里显示 "cost_colreg=0" 系可观测性桩，非避碰失效 |
| ASDR 记录（§3.2）| REAL | `mid_mpc_node.cpp:402-409`（stamp/source_module/decision_json/sha256 填充）| 已实装 |
| CMM plan 级 stamp/confidence/rationale（§3.3）| REAL | `mid_mpc_node.cpp:392-394`（stamp）；各路径设 confidence 和 rationale | 计划级已填；waypoint 级 NLP path 空 |
| N=18 参数接线（RFC-001）| MISSING | `mid_mpc_nlp_formulation.hpp:67`（default=12）；`mid_mpc_node.cpp:47`（仅 `m5.nominal_speed_kn` declare_parameter）；`m5_params.yaml n_steps=18` 未映射 | 有效 N=12（60s），不满足 RFC-001 N=18/90s |

---

## 3. 接口实现对照

| topic | 设计（spec §3）| 实际 file:line | 字段填充状态 | 状态 |
|---|---|---|---|---|
| `/l3/m2/world_state` | Mid-MPC 订阅（M2，50 Hz）| `mid_mpc_node.cpp:49-53`（`/l3/m2/world_state`）| — | 连通 |
| `/l3/m4/behavior_plan` | Mid-MPC 订阅（M4，4 Hz）| `mid_mpc_node.cpp:55-59`（`/l3/m4/behavior_plan`）| — | 连通 |
| `/l3/m6/colregs_constraint` | Mid-MPC 订阅（M6，2 Hz）| `mid_mpc_node.cpp:61-65`（topic `/l3/m6/colregs_constraint`，launch remap from `/m6/colregs_constraint`）| — | 连通（有 remap）|
| `/l2/planned_route` | optional 订阅 | `mid_mpc_node.cpp:67-71` | — | 连通（optional）|
| `/l2/speed_profile` | optional 订阅 | `mid_mpc_node.cpp:73-77` | — | 连通（optional）|
| `/l3/m1/odd_state` | Mid + BC-MPC 订阅（ODD gate）| **MISSING**（构造函数无此 sub）| — | 断流（设计缺失）|
| `/l3/checker/veto` | Mid + BC-MPC 订阅（VETO 门控）| **MISSING** | — | 断流（设计缺失）|
| `/m2/world_state` | BC-MPC 订阅（设计应为 `/l3/m2/...`）| `bc_mpc_node.cpp:34-37`（错误 topic：`/m2/world_state`，缺 `/l3/` 前缀）| — | **namespace 错误**：BC-MPC 接收 0 条消息 |
| `/m5/avoidance_plan` | Mid-MPC → `/l3/m5/avoidance_plan` | `mid_mpc_node.cpp:79`（发布到 `/m5/avoidance_plan`）；sil_entrypoint.sh 有 remap；launch 文件无此 remap | schema_version=112; stamp ✓; confidence 按路径; rationale ✓ | **namespace 脆**：依赖 entrypoint remap，launch 文件不完整 |
| `/m5/reactive_override_cmd` | BC-MPC → L4 紧急接口 | `bc_mpc_node.cpp:46-47`（发布 `/m5/reactive_override_cmd`）| heading_cmd_deg/validity_s ✓ | **断流**：BC-MPC 未 launch；bridge 无订阅 |
| `/m5/asdr_record` | Mid-MPC ASDR | `mid_mpc_node.cpp:80`（发布）；`publish_outputs_:402-409`（填充）| stamp/source_module/decision_json/sha256 ✓ | 连通 |
| `/sil/sat3_data` | SAT-3 候选轨迹 @2Hz | `mid_mpc_node.cpp:82`（`/sil/sat3_data`）；`publish_trajectory_candidates_:423-457` | stamp ✓；schema_version=112 ✓；tc.confidence=1.0F（硬编）；tc.rule_compliant=false（硬编）| 连通但字段空（rule_compliant/confidence 未真实）|

---

## 4. 已知缺陷

| 严重度 | 缺陷 | file:line | 类型 |
|---|---|---|---|
| CRITICAL | BC-MPC 整层未 launch — 短程紧急覆盖层在生产中为死代码 | `m5_mid_mpc.launch.py:11-21`（仅 mid_mpc_node）；`bc_mpc_node.cpp:34`（wrong topic）；`sil_topic_bridge.py`（无 reactive_override_cmd 订阅）| MOCK / 脱节 |
| CRITICAL | M5 无 M1 ODD 订阅 — ADR-1 违反，M5 在 OUT-of-ODD 下仍全速运行 | `mid_mpc_node.cpp:34-93`（构造函数）；`mid_mpc_node.hpp`（无 sub_odd_）| 断流 / 设计缺失 |
| HIGH | BC-MPC 订阅错误 namespace `/m2/world_state` 而非 `/l3/m2/world_state` | `bc_mpc_node.cpp:35`；参照正确 `mid_mpc_node.cpp:50` | namespace 错 |
| HIGH | M7 MRM 上报是日志桩 — MUST-9 未接线 | `mid_mpc_solver.cpp:124-125, 149-153`（`spdlog::critical` only）| MOCK / 断流 |
| HIGH | AvoidancePlan waypoint CMM 字段（NLP path）未填 — schema_version=0 / confidence=0 / rationale='' | `mid_mpc_waypoint_generator.cpp:87-129`（无 CMM 字段赋值）| 字段空 |
| HIGH | /m5/avoidance_plan namespace 脆：仅 entrypoint remap，launch 文件不含，非 entrypoint 启动时断流 | `mid_mpc_node.cpp:79`；`m5_mid_mpc.launch.py:16-18`（只有 colregs remap）| 脆弱 / 脱节 |
| HIGH | X-axis Checker VETO 无门控 — M5 不订阅 `/l3/checker/veto` | `mid_mpc_node.cpp` + `bc_mpc_node.cpp` grep veto = 0 | 断流 / 设计缺失 |
| MEDIUM | Mid-MPC 有效 N=12（60s）而非 RFC-001 规定 N=18（90s）— n_steps=18 未接线到 NLP | `mid_mpc_nlp_formulation.hpp:67`（default=12）；`mid_mpc_node.cpp:47`（仅 nominal_speed_kn declare_parameter）| 脱节 |
| MEDIUM | /sil/sat3_data 1 Hz 非 2 Hz；tc.rule_compliant 硬编 false；tc.confidence 硬编 1.0F | `mid_mpc_node.cpp:87-92`（1s timer）；`:436,:441` | 字段空 / 脱节 |
| MEDIUM | cost_colreg / cost_dist / cost_vel 恒 0（Phase E1 deferral）— rationale 中显示误导性 "cost_colreg=0" | `mid_mpc_nlp_formulation.cpp:337-338`；`mid_mpc_waypoint_generator.cpp:139` | STUB（可观测性）|
| LOW | hs_m 海况硬编 0.0，rough_sea_factor 从未生效 | `mid_mpc_node.cpp:208` [TBD-HAZID] | STUB |
| LOW | BC-MPC mid_mpc_consecutive_failures 硬编 0（Phase E1）| `bc_mpc_node.cpp:155` | STUB |
| LOW | mid_mpc_solver.cpp 临时 [M5DIAG] debug 残差日志仍在 | `mid_mpc_solver.cpp:24-65`（"temporary, revert before merge" comment）| 临时入侵 |

---

## 5. 创可贴 / 越界逻辑

以下功能**本应在 M5 / M4 / M6**，当前却驻留在 `docker/sil_topic_bridge.py`（非 M 编号节点）：

| 越界逻辑 | bridge file:line | 目标归位 |
|---|---|---|
| 60° 航向 clamp（MAX_AVOID_DEV_DEG=60.0）| `sil_topic_bridge.py:653-660`（on_behavior_plan）；`:1072-1078`（on_avoidance_plan）| M6 COLREGs 最小偏转约束 或 M5 NLP bounds |
| 完整双路 PD/PI 自动驾驶（HeadingController + SpeedController）| `:145-182` | L4 Guidance Layer（bridge 为 SIL 临时）|
| 避让 arm / latch / teardown 状态机 | `:326`（flag）、`:431-437`（LATCH 参数）、`:989-1101`（arming）| M4 行为边界 / M5 BC-MPC 控制逻辑 |
| Dead-stick 开环兜底（船长 SHIP_LENGTH_M=46.0 硬编）| `:1235-1245` | ADR-4 违反；应从 Capability Manifest 读取 |
| 回航 XTE 控制器（gain=0.10 deg/m，cap=±30°）| `:1265-1286, :1305-1327` | M5 BC-MPC 或 M4 TRANSIT 行为 |
| DCPA / TCPA 几何计算（flat-earth CPA solver）| `:776-832` | M2 World Model（唯一权威世界视图）|

---

## 6. 设计-实现脱节（Overclaim 修正）

| 旧声称 | 实际状态 | 证据 |
|---|---|---|
| **D3.2 ✅「M5 双 MPC 完整实装（2751 LOC）」** | BC-MPC 代码存在、编译通过，**但未 launch、未接 bridge、订阅 namespace 错误**。在生产系统中贡献 0 输出。"已实装" ≠ "已集成且运行" | `m5_mid_mpc.launch.py:11-21`（无 bc_mpc_node）；`bc_mpc_node.cpp:35`（/m2/world_state）；bridge（无 reactive_override_cmd 消费）|
| **DEMO-2 阻塞 ✅「/sil/sat3_data @2Hz 已实装」** | sat3_data 确实发布，但频率 **1 Hz 非 2 Hz**（solve_timer_ = 1s）；tc.rule_compliant 硬编 false；tc.confidence 硬编 1.0F | `mid_mpc_node.cpp:87-92`（std::chrono::seconds(1)）；`:436,:441` |
| **D0.1 MUST-9 ✅「MRM 走 M7 路径」** | MRM 升级仅为 `spdlog::critical()` 日志调用。无 ROS2 发布到任何 M7 topic；M7 无法响应 M5 失败信号 | `mid_mpc_solver.cpp:124-125, 149-153` |
| **「NLP always fails」（审计基线 158bba9d 中为 CRITICAL）** | ✅ **已大幅修复**（J_colreg 重设计）：ROT 约束改 lbx/ubx；J 含真 colreg + asym cost；Restoration_Failed 50→0；单测 9/9；e2e GREEN。**避碰不再长期跑 DEGRADED 几何兜底。** | `mid_mpc_nlp_formulation.cpp:110-233`；M5-jcolreg-redesign-spec.md |
| **「cost_colreg 是避碰失效」（旧判断）** | 修正：cost_colreg=0 是 **Phase E1 可观测性桩**（CasADi stats 未拆分），优化器实际使用 J_colreg（目标函数真实）。非避碰失效，是 rationale 字段信息不完整 | `mid_mpc_nlp_formulation.cpp:337-338` comment |

---

## 7. D 任务联动表

| D 任务 | 关系 | 状态 | 真实状态（修正）|
|---|---|---|---|
| J_colreg 重设计（fix/m5-nlp-convergence）| Mid-MPC fix | ✅ 2026-06-08 | Restoration_Failed 50→0：box→lbx/ubx + ROT 平滑 + J_colreg 指数障壁/动态权重/右转不对称。spec [M5-jcolreg-redesign-spec.md](M5-jcolreg-redesign-spec.md)；单测 9/9，e2e GREEN。**分支待 merge main** |
| D0.1 | MUST-2/5/9 surgical | ✅ | MUST-2（N=18 params 已写入 YAML，但**实际 N=12 因 declare_parameter 缺失**）；MUST-5（Manifest 读取 ✓）；MUST-9（**仅日志桩，ROS2 发布缺失**）|
| D1.4 | 编码规范 v1.2 | ✅ 2026-05-20 | 全模块适用；已验证 |
| D1.3.1（原 D1.3a）| M5 ROT_max 参数曲线 | 🟡 | M5 Manifest 已实装；D1.3.1 仿真器侧依赖仍待 |
| D3.2 | M5 双 MPC 完整实装 | ✅ 2026-05-25 | **Overclaim 修正**：Mid-MPC 真实运行；BC-MPC 代码存在但未 launch（见 §6 第1行）|
| D3.8 | 算法选型矩阵 | ✅ 2026-08-25（Wave 1）| §4.5 矩阵已写入架构报告 §10.1；与当前实现对齐 |
| BC-MPC 集成（无 D 编号）| 开放 | 🔴 | BC-MPC launch + namespace + bridge 消费全部缺失；整层为死代码 |
| ODD gate 集成（无 D 编号）| 开放 | 🔴 | `/l3/m1/odd_state` 订阅缺失；ADR-1 违反 |
| waypoint CMM 字段（无 D 编号）| 开放 | 🔴 | NLP path waypoint schema_version=0/confidence=0/rationale=''；fallback path 有填充 |
| M7 MRM 接线（MUST-9 真实）| 开放 | 🔴 | safety_concern_event 到 M7 未实现；仅日志 |
| N=18 参数接线（RFC-001）| 开放 | 🔴 | n_steps=18 在 params.yaml 但未 declare_parameter/映射到 nlp.n_horizon |

---

## 8. DEMO 阻塞贡献

| DEMO | 项目 | 状态 | 说明 |
|---|---|---|---|
| DEMO-1 | 避碰 NLP 收敛 | ✅ 已解除（J_colreg 重设计）| Restoration_Failed 修复；避碰轨迹不再长期走 DEGRADED 几何兜底 |
| DEMO-1 | 无限循环（M5 VALID-forever）| ✅ 已解除 | M4 TRANSIT → 空计划 fix 已验证 |
| DEMO-2 | SAT-3 轨迹候选发布 | ⚠ 部分 | `/sil/sat3_data` 连通，1 Hz（非 2 Hz）；tc.rule_compliant/confidence 字段不真实 |
| DEMO-2 | BC-MPC 短程覆盖层 | 🔴 阻塞 | BC-MPC 整层死代码；紧急覆盖能力缺失 |
| DEMO-3 | ODD gate + X-axis VETO | 🔴 阻塞 | ADR-1/ADR-2 gate 完全缺失 |
| DEMO-3 | MRM 真实接线 | 🔴 阻塞 | MUST-9 是日志桩 |
| 认证路径 | waypoint CMM 字段 | 🔴 阻塞 | NLP path waypoint CMM 字段缺失违反 ADR-3 |

---

## 9. 参考 D 任务文档

- D3.2: [Phase 3/D3.2-m5-tactical-planner/](../../Phase%203/D3.2-m5-tactical-planner/)（spec + report 已完成）
- J_colreg 重设计 spec: [M5-jcolreg-redesign-spec.md](M5-jcolreg-redesign-spec.md)

---

## 10. 修订

| 日期 | 变更 |
|---|---|
| 2026-06-08（初版）| 数据更新规则 + D 任务联动表初版 |
| 2026-06-08 | 依系统审计 + codegraph 代码核对重写 progress（状态矩阵 + gap + 创可贴 + overclaim 修正；反映 J_colreg 重设计已修状态；修正 D3.2/MUST-9/sat3_data 虚标 ✅）|

## P7 — 鲁棒性扩展:OU 不确定性 + UT expected cost + Intent 缩放 + BC 加速度优化

**状态**: ✅ 完成 (2026-07-18)
**基线**: 74f67e365 (P6)
**HEAD**: d02a2a087 + P7 实施改动

### 范围
- TargetState 加 3 字段(intent_confidence/target_compliance/Classification)
- OU 不确定性参数推导(ou_uncertainty.hpp, [RMD] Ch3.7)
- pack_parameters: target stride 5→8, per-stage σ_pos, np_global=154
- UT expected cost: 5 sigma points (α=1e-3), MX 原生实现
- Intent 缩放: (1 + k_intent * (1 - conf))
- BC 加速度优化: Override + CPA 低时减速
- σ=0 退化验证: UT → deterministic cost (P5 兼容)

### 验收门
- [x] G1 TargetState 3 字段(G1)
- [x] G2 OU 参数推导正确(G2)
- [x] G3 UT expected cost 数值正确(G3)
- [x] G4 intent 缩放生效(G4)
- [x] G5 pack stride 8 正确(G5)
- [x] G6 BC 加速度优化生效(G6)
- [x] G7 codegen SX/MX parity(公式化测试,需容器 codegen 验证)
- [x] G8 SIL 三场景 + P5 回归(P5 ample-time benchmark 通过)

### 测试
- 88 单元测试全部通过(7 个 test binary,14 个 test suite)
- 12 文件改动(2 新建 + 10 修改)

### 参考文献
- [RMD] Ch3: OU 不确定性 + UT expected cost(非 Eriksen 方法)
- [E1] Eq 13: BC 加速度优化
- [E3] time-dependent weighting: intent 缩放启发

### 关键交付物
- docs/superpowers/specs/2026-07-18-m5-p7-robustness-ou-intent-design.md (spec)
- docs/superpowers/plans/2026-07-18-m5-p7-robustness-ou-intent.md (plan)
- docs/superpowers/specs/2026-07-18-m5-mpc-p0-p7-implementation-report.md (完整收尾报告)

### P7 是 MPC 重构收尾
P0–P7 MPC 避碰重构全量闭环。
