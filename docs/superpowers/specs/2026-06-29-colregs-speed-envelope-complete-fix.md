# COLREGs Speed-Envelope Contract 完整修复 Spec

Date: 2026-06-29
Status: design draft, awaiting review
Branch: `codex/colregs-12probe-debug`
Spec ID: D-fix-speed-envelope-v1
Diagnosis basis: `docs/Doc From Claude/2026-06-28-colregs-speed-envelope-contract-diagnosis.md`（6 场景全新真实 trace 验证）

## 0. 目标

彻底修复 COLREGs rule14/15 cohort 的 speed-envelope contract 缺陷，使 own ship 的实际机动能力与 M5 避让几何、GNC 执行 ODD、scenario 设计速度三者自洽。**目标是最完整修复（架构级），不是最小补丁。**

完成判据（可验证）：
1. rule14+15 cohort（6 场景：ho/ho-port/cs/cs-2/cs-edge/ot-boundary）clean 8-probe overall_pass=true
2. own ship 实际速度跟随 scenario 设计速度（|actual - design| < 1kn），M5 命令与 GNC 执行一致
3. M5 避让几何在 own 实际机动能力（GNC ODD 参数下）内 reachable
4. cs-edge 不再近撞（CPA ≥ floor），cs/ho 系列 seamanship 通过
5. ot-boundary M6 能 onset（own 速度回归设计，几何 closing）
6. 单元测试覆盖每个修复点，回归锁（counterfactual：反转修复则对应场景转 RED）

## 1. 问题分门别类（4 类独立缺陷）

### 1.1 Class A — Mock 层 per-scenario speed 字段丢失（ot-boundary）

**根因**：`docker/gnc_route_mock_publisher.py:94-107` 读 scenario `nominalRoute` 只取 lat/lon，丢弃 `target_sog_kn`，不填 `RoutePlan.speed_limit_mps` → GNC 用全局 `max_transit_speed=8.0` → own 4.3kn 被拉高到 11-15kn → M6 silent。

**证据**：ot-boundary trace conflict_true=0，M5 全 EMPTY，own 11-15kn（设计 4.3kn）。

**断裂层**：mock publisher（非 TDL 核心、非 GNC）。

### 1.2 Class B/C — M5 横向 offset 未做 reachability 校验（ho/ho-port/cs/cs-edge）

**根因（2026-06-29 源码深挖精确化）**：速度对接已正确（M5 `command_speed_mps` 已 cap 到 3.2），但 M5 `generate_stable_avoidance_corridor_waypoints` 的横向 offset 是**纯几何**（默认峰值 270m，指数趋近），不校验 own 在 emergency cap 3.2 m/s + max_lateral_accel 0.25 m/s² 下、给定 TCPA 窗口内能否横向到达 → own 物理跟不上 → XTE 收敛慢（Class B）/ 近撞（Class C，cs-edge M5 要 400m own 只到 225m → CPA 0.2m）。

**证据**：
- 5 场景 emergency cap 贯穿（own 6.2-6.6kn），M5 `command_speed_mps` 已 cap（源码 line 759-761, 809）
- cs-edge M5 横向要求 ~400m，own 实际 225m（落后 180m）
- trace `wp0_target_speed_kn=10.8` 是 L3 msg（未 cap），非 GNC 收到的 `command_speed_mps`（已 cap）—— 诊断盲区

**断裂层**：M5 避让几何生成（`avoidance_waypoint_gen.hpp` 横向 offset 无物理约束），非速度对接（速度已正确）。

### 1.3 cs-2 phase 特例 — phase semantics gate（独立）

**根因**：cs-2 emergency cap 也命中（6.58kn），但 first_failure=L4_colregs_compliance（phase），seamanship PASS。phase gate 在 emergency cap 压低 own 后判定异常。

**证据**：cs-2 first_failure=L4，其余 seamanship 场景 first_failure=L6。

**断裂层**：phase semantics evaluator（与 speed 间接相关，需独立诊断）。

### 1.4 附带 — Rule13 same-course 门 contract bug（独立，非 cohort blocker）

**根因**：`rule13_overtaking.cpp:32 kSameCourseMaxDeg=45.0` 加了非 COLREGs 的 same-course 限制。COLREGs Rule 13(a) "any vessel overtaking any other" 不要求 same-course（maritime_regulations 🟢high）。

**证据**：源码 + COLREGs Rule 13(a) 原文。

**断裂层**：M6 Rule13 几何门（纯 M6）。

## 2. 架构级修复方案（彻底，分 5 个 Workstream）

### Workstream W1 — Mock 层 per-scenario speed 注入（Class A，低风险，先做）

**目标**：scenario `target_sog_kn` 完整传到 GNC RoutePlan。

**改动**：
- `docker/gnc_route_mock_publisher.py`：
  - `_load()` 保留每航点 `target_sog_kn`（m/s）
  - `_on_timer()` 填 `msg.speed_limit_mps`（与 latitude 等长）
  - 缺失 target_sog 时用 `ownShip.initial.sog` fallback，仍缺则不发 speed_limit（保持当前不限速行为，不引入回归）
- 单测：`tests/docker/test_gnc_route_mock_publisher.py`（新建）
  - 字段非空 + 长度匹配 latitude
  - target_sog_kn 正确转 m/s（×0.514444）
  - 缺失字段 fallback 行为

**验证**：ot-boundary own 速度回归设计（受 GNC cruise_min floor 3.8 限制，实际 ~7.4kn，非 4.3kn —— 需配合 W3）。

**注意**：W1 单独不够（cruise_min floor 仍拉高），必须配合 W3。

### Workstream W2 — GNC 执行 ODD 参数作为 contract 暴露（Class B/C 前置，中风险）

**目标**：GNC ODD 参数（emergency_avoidance_speed_cap、max_lateral_accel、max_decel、static_min_turn_radius、cruise_min_speed）作为可订阅 contract 暴露给 TDL，M5 不再硬编码或盲生成。

**改动**：
- `third_party/gnc_ws/.../ship_guidance`：新增 `GncExecutionOdd` msg（或复用现有 status msg 扩展），active_route_manager 发布当前 ODD 参数（latched，启动 + 参数变更时发）
- msg 字段：`emergency_avoidance_speed_cap_mps`, `max_lateral_accel_mps2`, `max_decel_mps2`, `static_min_turn_radius_m`, `cruise_min_speed_mps`, `max_transit_speed_mps`
- `src/sim_workbench/gnc_bridge`：订阅 GncExecutionOdd，转发到 L3 domain
- M5 订阅 GncExecutionOdd，缓存最新 ODD

**设计依据**：TDL-GNC contract spec（`docs/superpowers/specs/2026-06-27-tdl-gnc-avoidance-interface-contract.md`）应扩展加入 ODD 参数契约。

**单测**：GncExecutionOdd 序列化/反序列化 + bridge 转发。

### Workstream W3 — cruise_min_speed 与 emergency cap 的 per-scenario 适配（Class A + B/C，高风险，需评审）

**核心评审问题**：`emergency_avoidance_speed_cap=3.2`（GNC 安全设计，保舵效）与 `cruise_min_speed=3.8` 是否可按 scenario/船型动态化？

**两条路线（评审决策）**：

**路线 A（保守，推荐）**：保留 GNC 物理常量不变，让 scenario 设计速度 ≥ GNC 物理下限。
- 改 scenario：ot-boundary own 4.3kn → ≥7.4kn（cruise_min），cs-edge own 5.5kn → ≥7.4kn（需重算几何保证仍是 crossing/edge + DCPA≈0）
- 加 scenario 校验：`tools/sil/verify_colreg_tier12.py` 拒绝 own 设计速度 < cruise_min 的场景
- M5 生成几何用 emergency cap 速度（W4）

**路线 B（激进）**：GNC ODD 参数动态化，按 scenario/ODD cell 注入。
- 改 active_route_manager/ship_guidance 接受 per-scenario ODD override
- 高风险：违反 GNC 物理安全设计，需 source-backed 舵效数据（45m FCB 在 <3.8 m/s 是否有足够侧向力）。无数据则不可行。

**推荐路线 A**（物理安全，scenario 设计修正）。路线 B 留待有舵效实测数据后评估。

### Workstream W4 — M5 避让几何消费 GNC ODD（Class B/C 核心，中风险）

**源码实证精确根因（2026-06-29 深挖）**：

M5 与 GNC 对接**已有**正确的转弯半径/速度约束，问题在**横向 offset reachability**：

1. **速度对接已正确**：M5 发 ship_interfaces `AvoidancePlan.command_speed_mps`（line 809 `speeds(wps.size(), anchor.command_speed_mps)`），`command_speed_mps = gnc_avoidance_command_speed_mps(planned, overtake) = min(planned, 3.2)`，**已 cap 到 emergency speed**。GNC 收到的速度是 3.2 m/s，转弯半径按 3.2 算，一致。

2. **trace 字段误导**：trace writer 记的 `wp0_target_speed_kn`（line 404）来自 **L3 `l3_msgs/AvoidanceWaypoint.target_speed_kn`**（line 516/590，未 cap 的设计速度），**不是** GNC 收到的 `command_speed_mps`。M5 发两个不同 msg：ship_interfaces 给 GNC（capped）、l3_msgs 给 trace/HMI（未 capped）。诊断时误读 trace 导致表面矛盾。

3. **真正断裂点 — 横向 offset 纯几何，无 reachability**：
   - `avoidance_waypoint_gen.hpp:127 generate_stable_avoidance_corridor_waypoints` 的 `max_lateral_offset_m` 默认 `kDefaultStableCorridorPeakOffsetM = 3×90 = 270m`（line 37-38）
   - lateral 用指数趋近 `cap × (1 - exp(-d/approach_distance))`（line 181-182），approach_distance 由几何 slope 推
   - **完全没有 own 速度/加速度/TCPA 约束**：纯几何峰值偏移，不校验 own 在 emergency cap 3.2 m/s + max_lateral_accel 0.25 m/s² 下、给定 TCPA 窗口内能横向移动多远
   - cs-edge M5 要 own 横向偏到 ~270-400m，own 实际只到 225m → 近撞 CPA 0.2m

**目标**：M5 横向 offset 生成时做 reachability 校验，own 物理跟不上时降级。

**改动**：
- `avoidance_waypoint_gen.hpp`：
  - `generate_stable_avoidance_corridor_waypoints` / `generate_avoidance_waypoints` 签名增加 `GncExecutionOdd odd`（或一组 ODD 参数：emergency_speed_mps, max_lateral_accel_mps2, tcpa_remaining_s）
  - 新增纯函数 `reachable_lateral_offset_m(odd, tcpa_remaining_s)`：
    - own 横向速度上限：`v_lat_max = min(emergency_speed, sqrt(2 × max_lateral_accel × lateral_distance))`
    - 简化可达模型：横向位移 ≈ `0.5 × max_lateral_accel × t²`（加速段）+ `v_lat_max × (t - t_accel)`（匀速段），t = tcpa_remaining × 安全系数（如 0.6，留 recovery 余量）
    - `reachable_offset = min(max_lateral_offset_m, 上述位移)`
  - `lateral_cap = min(default_lateral_cap, reachable_offset)`
  - offset 降级时，同步缩小 along 方向的趋近距离（让 own 更快达到可达 offset）
- `mid_mpc_node.cpp`：传入 ODD（来自 W2 订阅缓存，或 fallback 用 `GncAvoidancePreflightConfig` 硬编码）+ M6 TCPA 到 gen 函数
- offset 不可达（即使最大 reachable 仍不足以满足 CPA floor）时，在 plan rationale 标记 + 触发 W5 协同（提前 onset / MRM）

**单测**（`test_avoidance_waypoint_gen.cpp`）：
- 给定 ODD + TCPA=700s，reachable_offset 计算正确（own 3.2 m/s + 0.25 accel 下 ~XXXm）
- offset 超 reachable 时正确降级（lateral_cap 收缩）
- counterfactual：用纯几何 270m offset → cs-edge 几何复现（own 跟不上 → 近撞，回归锁）
- 边界：TCPA 极小（<60s）时 reachable 极小，offset 应大幅收缩

**验证**：cs-edge own 横向 offset 收缩到 reachable（如 ~150-200m），CPA ≥ floor；cs/ho 系列 seamanship 通过（offset 可达，own 跟得上，XTE 收敛）。

**关键设计权衡**：reachability 收缩 offset 会减小 CPA margin。若 reachable offset 不足以满足 CPA floor（cs-edge 高速场景典型），必须配合 W5 提前 onset（给 own 更多时间）或纵向减速，**不能单靠 W4 硬收缩**（否则 CPA 更差）。因此 W4 与 W5 强耦合，W4 负责"生成 reachable 几何"，W5 负责"reachable 不足时升级策略"。

### Workstream W5 — M5↔M6 协同：避让 onset 提前 + MRM 触发（Class C 防护，中风险）

**目标**：当 W4 判定几何不可达（即使最大 reachable offset 仍不足以满足 CPA floor），M5/M6 协同提前触发避让或升级 MRM。

**改动**：
- M5 在 avoidance plan 附带 `estimated_reachability` / `required_time_s` / `cpa_floor_achievable` 字段（AvoidancePlan msg 扩展）
- M6 conflict onset gate 消费 reachability：若 own 在当前 onset 时机无法达成 CPA floor，提前 onset（放宽 t_plan gate）
- M7 safety supervisor：监听 M5 reachability + GNC execution status，若执行中检测到不可达（own 实际偏离 M5 几何 > 阈值持续），触发 MRM
- 反馈闭环：M5 plan 的 feasibility（速度可执行）vs reachability（几何可达）分离报告

**单测**：
- reachability 字段计算
- M6 onset 提前条件
- M7 MRM 触发条件

### Workstream W6 — Rule13 same-course 门修正（附带，低风险，独立）

**目标**：移除非 COLREGs 的 same-course 限制。

**改动**：
- `rule13_overtaking.cpp:32`：移除 `kSameCourseMaxDeg=45.0` 门，或改为仅作 confidence 软因子（不影响 is_active）
- Rule13 判定回归 COLREGs Rule 13(a)/(b)：abaft beam >112.5° + closing 即 overtaking，不论 course diff
- 单测：course diff 60° 的 overtaking 几何正确分类

**验证**：不影响 cohort（ot-boundary 是 crossing 非 overtaking），但修正真正 course-diff 追越场景。

## 3. 执行顺序与依赖

```
W6 (Rule13, 独立) ──────────────────────► 可并行先做
W1 (mock speed 注入) ──────────────────► Class A 前置
W2 (GNC ODD 暴露) ──┐
                    ├─► W4 (M5 消费 ODD) ──► W5 (M5↔M6↔M7 协同)
W3 (scenario/参数适配, 评审决策) ─┘
```

- **Phase 1（独立可交付）**：W6（Rule13）+ W1（mock）
- **Phase 2（核心，需 W2 前置）**：W2 → W4
- **Phase 3（防护闭环）**：W5
- **Phase 4（需评审）**：W3

## 4. 风险与回归保护

- **每个 Workstream 独立 spec/plan/TDD**，不混修（Class B 教训：错误根因 fix 回退）
- **counterfactual 回归锁**：每个修复点写"反转则 RED"测试
- **cohort 回归**：每完成一个 Workstream 跑 rule14+15 cohort，确认无回归
- **GNC 物理安全不破坏**：W3 路线 A 保留物理常量；路线 B 需 source-backed 数据，默认不选
- **A4000 验证**：本地 cohort 全 GREEN 后，sync + A4000 acceptance（CLAUDE.md promotion rule）

## 5. 待评审决策点

1. **W3 路线 A vs B**：scenario 设计速度修正（A，推荐）vs GNC ODD 动态化（B，需舵效数据）
2. **W5 MRM 触发阈值**：own 偏离 M5 几何多少/多久触发 MRM（需安全分析）
3. **W2 msg 设计**：新建 GncExecutionOdd msg vs 扩展现有 RouteExecutionStatus（接口契约评审）
4. **cs-2 phase gate（§1.3）**：是否纳入本 spec 或独立诊断（建议先独立诊断 phase，speed 修完后看是否消失）

## 6. 不做的事（Out of Scope）

- Rule17 stand-on、Rule13-ot 普通追越场景（非 cohort，独立验证）
- intelligent 场景（ho-intelligent/ot-target-giveway）—— 已知 intelligent target FSM 导致 sim 卡死，独立缺陷
- GNC 物理模型重写（舵效、动力学）—— 仅消费现有 ODD 参数
- seamanship 阈值调整（违反 no-threshold-tuning）
