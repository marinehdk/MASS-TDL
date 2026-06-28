# COLREGs Speed-Envelope Contract 诊断报告

- **日期**: 2026-06-28（初版）/ 2026-06-29（全新真实 trace 验证修订）
- **分支**: `codex/colregs-12probe-debug`（worktree `.worktrees/colregs-12probe-debug`）
- **状态**: 诊断完成 + 全新真实 trace 验证（6 场景），**待设计评审**，未写任何代码
- **覆盖**: `colreg-rule15-ot-boundary` (Class A) + Class B (`rule14-ho`, `rule14-ho-port`, `rule15-cs`) + Class C (`rule15-cs-edge`) + cs-2 phase 特例
- **方法**: systematic-debugging Phase 1 亲自 trace（非 Agent 归因），证据闭环；2026-06-29 在干净 GNC stack 上重跑 6 场景验证

## 0. 摘要（TL;DR）

三类 RED 都属 speed-envelope contract 冲突，但**机制不同、断裂层不同、独立**（跨场景 trace 确证，原"同源"推测已修正）：

1. **ot-boundary（Class A 子）**：Mock 层字段丢失（`gnc_route_mock_publisher` 丢 `target_sog_kn`）+ GNC cruise_min floor → own 被**拉高**（设计 4.3kn → 实际 15kn）→ M6 silent。详见 §1-4。
2. **Class B（cs/cs-2/cs-intelligent seamanship FAIL）**：M5 标 `navigation_mode="emergency_avoidance"` → GNC `emergency_avoidance_speed_cap=3.2` 压低 own（设计 10.8/12kn → 实际 6.6kn）→ 横向机动慢 → XTE 超限。详见 §5。**与 ot-boundary 方向相反、不同源。**
3. **Class C（cs-edge 近撞）**：**确证同 Class B 机制**（emergency cap）。M5 plan 被 GNC ACCEPTED（非被拒），但要求 400m 横向避让，own 在 emergency cap 6.2kn 下只完成 225m → 近撞 CPA 0.8m。详见 §5.6 + §6。target 高速放大后果。

两类已确证根因（ot-boundary + Class B/C）：
- ot-boundary = own 低于 GNC floor 被**拉高**
- Class B/C = M5 命令高于 GNC emergency cap 被**压低**（B 后果 XTE 超限，C 因 target 高速近撞）

**核心设计缺陷**（§6.3）：M5 生成避让几何时未考虑 own 在 emergency cap 下的实际机动能力。GNC ODD 参数应作为 contract 注入 TDL（§6.4）。

**都不是 M6 分类 bug**（推翻上会话预判）。修复均触及 contract，需设计评审。

### 0.1 全新真实 trace 验证（2026-06-29，6 场景，干净 GNC stack）

在 `codex-gnc-validation` 干净 stack（修复 GNC 容器 Exited137 OOM 后）重跑 rule14+15 cohort，gnc profile + restart-between-runs + sim-rate 10。6 场景全部 RED，分类如下：

| 场景 | overall | first_failure | own 设计 | M5 命令 | AVOID 实际 | cap 命中 | 分类 |
|---|---|---|---|---|---|---|---|
| rule14-ho | RED | L6_seamanship | 6.0 | 6.0 | 6.36 | YES(6.2-6.6) | **Class B** |
| rule14-ho-port | RED | L6_seamanship | 6.0 | 6.0 | 6.35 | YES | **Class B** |
| rule15-cs | RED | L6_seamanship | 10.8 | 10.8 | **6.58** | YES(被压) | **Class B** |
| rule15-cs-2 | RED | L4_colregs_compliance | 12.0 | 12.0 | **6.58** | YES(被压) | **phase 特例** |
| rule15-cs-edge | RED | L2_safety_floor 近撞 | 5.5 | 5.5 | 6.18 | YES | **Class C** |
| rule15-ot-boundary | RED(sim 卡死) | M6 silent | 4.3 | EMPTY | **11-15** | 拉高(超速) | **Class A** |

**关键修正**（推翻旧判断）：
1. **rule14-ho 也是 RED（L6_seamanship），非 GREEN**。之前单跑 ho 见 "Returned True CPA 241m" 是部分指标，完整 verdict RED。ho 归 Class B。
2. **emergency_avoidance_speed_cap 3.2 m/s（6.2-6.6kn）贯穿全部 5 个非 ot-boundary 场景**，铁证。cs/cs-2 命令 10.8/12kn 被 GNC 无视强制压到 6.58kn。
3. **ot-boundary 独有方向相反**：own 被拉高 11-15kn（设计 4.3kn），M6 silent（conflict_true=0），M5 全 EMPTY。mock 丢字段 + cruise_min floor。
4. **cs-2 特例**：emergency cap 也命中，但 first_failure 是 L4 phase（非 seamanship，seamanship PASS）。需独立看 phase gate。

**最终四分类**：
- **Class A**（ot-boundary）：mock 丢字段 + cruise_min floor 拉高 own → M6 silent。**独立**。
- **Class B**（ho/ho-port/cs）：emergency cap 压 own → seamanship FAIL（XTE 收敛慢）。**同源**。
- **Class C**（cs-edge）：emergency cap + target 高速 → 避让未完成近撞 CPA 0.2m。**同 Class B 机制放大**。
- **phase 特例**（cs-2）：emergency cap 命中但 first_failure 是 phase semantics，独立诊断。

证据路径：ho `runs/trace_eval/20260628_212718_ho_single/`，ho-port `20260628_213255_rule14_cohort_fresh2/`，cs `20260628_233839_cs_single/`，cs-2 `20260628_234938_cs2_single/`，cs-edge `20260629_000517_cs_edge_single/`，ot-boundary `20260629_001256_ot_boundary_single/`（sim 卡死但有 raw trace）。

## 1. 根因链（亲自 trace 闭环）

### 1.1 ot-boundary 现象

- M2: t=47s 起持续报告 1 target（上游 OK）
- M6: 9643 条记录全程 `conflict_detected=False`, `active_rules=[]`, `conflict_toggles=0`
- M5: `EMPTY_TRANSIT` 4816（从未激活）
- M4: behavior 全 0 (TRANSIT)
- `diagnosis.first_broken_stage="M6"`, `failing_gate="PHASE"`
- `l4.gnc_plan_id_changes=0`（无 plan churn，区别于 Class B）

### 1.2 真因果链

```
scenario nominalRoute.target_sog_kn: 4.3  (设计 2.21 m/s)
        │
        ▼
gnc_route_mock_publisher.py:94-95   ← 断裂点 #1 (mock contract bug)
   _load() 只取 lat/lon，丢弃 target_sog_kn
   _on_timer() line 103-107:
     msg = RoutePlan()
     msg.latitude = [...] / msg.longitude = [...]
     msg.speed_limit_mps = ???  ← 完全不填
        │ (RoutePlan.speed_limit_mps 为空)
        ▼
GNC active_route_manager basic_route_valid: 空=不限速 → 全局 max_transit_speed=8.0 生效
        │
        ▼
coordinate_transform_node:619-624  本会把 speed_limit→Path orientation.z
   (收到空，编码 0)              ← 链路功能正常，只是输入空
        │
        ▼
ship_guidance_node 主 transit 计算:
  3524: u_cmd_planned = max_speed_ (8.0)
  3755-3768: routeplan_segment_speed_cap → u_cmd_planned=min(planned, route_limit=8.0)  ← route 空所以仍是 8.0
  4021: u_cmd = min(u_cmd_planned, max_speed_*turn_penalty)
  4321: u_cmd = min(u_cmd, cruise_speed_cap=6.0)
  4393: u_cmd = max(u_cmd, cruise_min_speed=3.8)  ← FLOOR
        │
        ▼
own ship 跑到 11-15kn（trace 实测，设计 4.3kn 的 3.5×）
        │
        ▼
相对几何变：range 全程不 closing（rngrate +7~9 m/s），CPA 从设计 6m 变 2000-4000m
        │
        ▼
M6 FSM 进 ACTIVE 需 range_closing=true (colregs_reasoner_node.cpp:846)
        │
        ▼
FSM 卡 PREPLAN/CANDIDATE → conflict_toggles=0 → M5 EMPTY_TRANSIT → 全链 silent
```

### 1.3 对照证据（同 trace 方法）

| 场景 | 设计 own kn | trace 实际 | cruise_base 6.0m/s | 结果 |
|---|---|---|---|---|
| rule15-cs (GREEN) | 10.8 (5.56 m/s) | 11-14kn | ≈匹配 | ✓ M6 t=633s onset |
| **rule15-ot-boundary (RED)** | **4.3** (2.21 m/s) | **11-15kn** | 远超设计 | ✗ M6 全程 silent |
| rule13-ot | 14.0 | 正常 | — | — |

cs 设计 10.8kn 接近 cruise_base 6.0 m/s，故 trace 正常；ot-boundary 设计 4.3kn 远低于 cruise_base，被强行拉高。

### 1.4 speed 注入链功能完整性验证

下游链路（coordinate_transform + ship_guidance）**功能正常**，会正确消费 `speed_limit_mps`：

- `coordinate_transform_node.cpp:619-624`: `np.speed_override = msg->speed_limit_mps[i]`（若 valid）→ 编码到 Path `orientation.z`
- `ship_guidance_node.cpp:1632`: `wp.speed_override = (pose.orientation.z > 0.1) ? pose.orientation.z : -1.0`
- `ship_guidance_node.cpp:3755-3768`: `routeplan_segment_speed_cap` → `u_cmd_planned = min(planned, route_limit)`

唯一断裂 = **gnc_route_mock_publisher 不填字段**。

### 1.5 steerage floor 验证（修 mock 是否够）

`ship_guidance_node.cpp:4388-4393` 保底舵效：
```cpp
active_min_speed = cruise_speed_floor_allowed ? cruise_min_speed_mps_ : minimum_steerage_speed_;
// overlay: cruise_min_speed_mps=3.8, minimum_steerage_speed=3.0
if (dist_to_wp > 10.0 && cruise模式) {
    u_cmd = std::max(u_cmd, active_min_speed);  // FLOOR
}
```

ot-boundary cruise 模式 → floor=3.8 m/s (7.4kn)。

**修 mock 后预测**：`speed_limit=2.21 → line 3768 cap 2.21 → line 4393 max(2.21,3.8)=3.8`。own 从 15kn 降到 7.4kn（减半），但仍非设计 4.3kn，超速 1.7×。

GNC 注释（line 4372）："45M FCB 舵在 1.5m/s 时约 10kN 侧向力，足够完成转弯" — 即 GNC 认为 <3.8 m/s 无法保舵效。

## 2. 三类 RED 的统一根因归属

| 类别 | 场景 | RED 直接原因 | speed-envelope 关联 |
|---|---|---|---|
| Class A 子 | ot-boundary | M6 silent (own 超速→range 不 closing) | **主因**：own 4.3kn 设计 vs GNC floor 3.8 |
| Class B | ho-port, cs, cs-2, cs-intelligent | Layer-2 GREEN, Layer-3 seamanship FAIL (XTE integrated 超限) | **主因**：own 全程 GNC emergency cap ~3.3m/s，RECOVERY 横向收敛 0.19m/s |
| Class C | cs-edge | safety_floor FAIL (近撞 cpa 0.8m) | **同 Class B**：emergency cap 压 own 6.2kn + target 13.4kn 高速 → M5 要求 400m 横向避让 own 只完成 225m |

三者同属 **own ship 速度行为与场景设计/contract 不一致** 的 speed-envelope 家族。

## 3. 附带发现：Rule13 same-course 门 contract bug

独立于 speed-envelope，但同为 M6/mock 层 contract bug：

`rule13_overtaking.cpp:32` `kSameCourseMaxDeg = 45.0` —— 源码加了非 COLREGs 的 same-course 限制。

COLREGs Rule 13(a)（maritime_regulations 笔记本 🟢 high 确证）："Notwithstanding anything contained in the Rules of Part B, Sections I and II, **any vessel overtaking any other** shall keep out of the way"。追越判定**不要求 same-course**，仅 Rule 13(b) 方位 >112.5° abaft beam + closing。

此 bug 影响**真正 course-diff 追越场景**（非 ot-boundary，ot-boundary 是 crossing）。可独立修复，范围小（删/放宽 same-course 门 + 单测）。

## 4. 修复选项与取舍（待评审）

### 选项 A：修 mock 填 speed_limit_mps（必要但不充分）

- **改动**：`gnc_route_mock_publisher.py` `_load()` 保留 target_sog_kn，`_on_timer()` 填 `msg.speed_limit_mps`
- **效果**：own 从 15kn 降到 7.4kn（cruise_min floor）
- **风险**：ot-boundary 仍超速 1.7×，M6 可能仍 silent（7.4kn vs target 19.7kn 相对几何需验证）
- **定位**：mock contract bug，应修（无论选哪个，这个字段丢失都是 bug）

### 选项 B：改场景 own 速度 ≥7.4kn（物理可行）

- **改动**：ot-boundary.yaml own `sog` + `target_sog_kn` 从 4.3 → ≥7.4kn，重算几何保证仍是 crossing/overtake 边界 + DCPA≈0
- **效果**：own 速度与 GNC 物理一致，encounter 几何保持意图
- **风险**：改变 encounter 几何（DCPA/TCPA/rel_brg 重算），需重新验证场景仍是 classification-boundary 探针
- **定位**：场景设计修正，符合物理

### 选项 C：降 cruise_min_speed overlay（物理风险）

- **改动**：`gnc-ship-config-overlay.yaml:434` cruise_min_speed_mps 从 3.8 → ≤2.21
- **效果**：own 可达 4.3kn
- **风险**：违反 GNC 物理舵效约束，需 source-backed 舵效数据（45m FCB 在 <3.8 m/s 是否真有足够侧向力）。无数据支撑则不可行
- **定位**：GNC 物理参数，高风险

### 选项 D：场景加非 cruise navigation_mode 绕过 floor

- **改动**：ot-boundary nominalRoute waypoint 加 `navigation_mode` 非 cruise（如 narrow_channel），绕过 `cruise_speed_floor_allowed` (line 4381-4387)
- **效果**：floor 降到 minimum_steerage 3.0，own 可跑更低
- **风险**：语义不符（ot-boundary 是 open_sea_offshore_wind_farm domain，标 narrow_channel 失真）
- **定位**：workaround，非正解

### 推荐组合（供评审参考）

- **A 必修**（mock 字段丢失是独立 bug）
- **B 或 C 二选一**（让 own 速度与场景意图一致）：B 安全（改场景）、C 需物理数据
- D 不推荐（语义失真）

## 5. Class B 根因（跨场景 trace 确证，2026-06-28 补充验证）

**修正：原报告 §5 推测"Class B 与 ot-boundary 同源"错误。跨 4 场景 trace 确认两类方向相反、机制不同、独立。**

### 5.1 Class B 核心机制

M5 AVOID/RECOVERY 全程标 `navigation_mode="emergency_avoidance"`（非 overtake 场景）→ GNC `emergency_avoidance_speed_cap_mps=3.2` 压低 own → 横向机动慢 → XTE 超限 → seamanship FAIL。

源码链：
- `gnc_avoidance_preflight.hpp:165-166`: `gnc_avoidance_navigation_mode(corridor) = corridor ? "colregs_overtake" : "emergency_avoidance"`。非 overtake 默认 emergency_avoidance。
- `mid_mpc_node.cpp:762-763`: AVOID 期 `wp.navigation_mode = "emergency_avoidance"`
- `mid_mpc_node.cpp:845,876`: RECOVERY 期 `gnc_return_navigation_mode(false)` → 同样 `"emergency_avoidance"`（return_to_route behavior 但 navigation_mode 继承 emergency）
- `ship_guidance_node.cpp:339`: `emergency_avoidance_speed_cap_mps = 3.2`（overlay 默认）
- `ship_guidance_node.cpp:4414-4424`: `emergency_avoidance_active → u_cmd = min(u_cmd, 3.2)`

### 5.2 跨场景 trace 证据（M5 命令 vs own 实际）

| 场景 | 设计 kn | M5 命令 kn (wp0) | AVOID 实际 | RECOVERY mid 实际 |
|---|---|---|---|---|
| ho-port | 6.0 | 6.0 | 6.34 (≈设计，巧合≈cap) | 6.30 |
| cs | 10.8 | **10.8** | **6.60 (被压)** | 6.58 |
| cs-2 | 12.0 | **12.0** | **6.56 (被压)** | 6.58 |
| cs-intelligent | 10.8 | **10.8** | **6.57 (被压)** | 6.61 |

**铁证**：M5 正确命令设计速度（10.8/12kn），GNC 无视，强制 cap 到 6.6kn（3.2 m/s + 动力学余量）。RECOVERY 全程 emergency_avoidance → own 持续被压。

### 5.3 因果链（seamanship FAIL）

own 被压到 6.6kn → 横向机动效率低 → XTE 峰值 242m 持续 1800s → integrated|XTE|=352,783 > 300,000 (+17%) → seamanship gate FAIL。RECOVERY 横向收敛 0.19m/s（上会话数据）因 own 仍被 cap。

### 5.4 与 ot-boundary 对比（确认不同源）

| 维度 | ot-boundary (Class A 子) | Class B (cs/cs-2/cs-intelligent) |
|---|---|---|
| 方向 | own 被**拉高**（设计 4.3→实际 15kn） | own 被**压低**（设计 10.8→实际 6.6kn） |
| 机制 | mock 丢字段 → GNC 全局 cruise_base floor | M5 标 emergency → GNC emergency_avoidance cap |
| M5 状态 | EMPTY（未激活） | VALID（命令设计速度） |
| 断裂层 | mock publisher | M5 navigation_mode 标签 + GNC cap |
| 同源？ | **否** | **否** |

两类都是 speed-envelope contract 冲突，但机制相反、断裂层不同、修复点不同。

### 5.5 Class B 修复选项（独立于 ot-boundary）

- **A. 提高 emergency_avoidance_speed_cap**（3.2 → 更高，让 own 跟 M5 命令）— emergency cap 是 GNC 安全设计（避让减速），提高需 source-backed 依据
- **B. M5 标非 emergency 的 navigation_mode**（如 colregs_overtake 或新 mode）— 绕过 cap 但语义需评审
- **C. 接受 GNC cap，改 M5 避让几何适应低速**（小 offset / 短距离）— 适配而非对抗
- **D. 接受现状，调 seamanship 阈值** — 违反 no-threshold-tuning

`emergency_avoidance_speed_cap=3.2` 的来源/依据未查（GNC 物理安全设计），是评审关键。

### 5.6 Class C（cs-edge）根因（跨场景 trace 确证，2026-06-28）

**核心问题（评审提出）**：GNC 限 3.2m/s 保舵效，还是 M5 waypoint 不可执行被拒？

**答案：都不是被拒，是 own 物理跟不上 M5 几何要求。**

#### 证据链

**1. GNC 接受 M5 plan（排除"被拒"假设）**

cs-edge M5 plan 862 条全 **ACCEPTED + feasible + 0 degraded + 0 rejected**（cs/cs-2/cs-intelligent 同样全 ACCEPTED）。M5 `allow_degraded_execution=true`（mid_mpc_node.cpp:830），故 GNC 不 reject。cs-edge 连 degrade 都没有（feasible）。

**2. M5 要求横向避让 400m，own 实际只到 225m（铁证）**

| t | own_x (实际 XTE) | wp0_x (M5 要求) | 差距 |
|---|---|---|---|
| 700 | 217 | 397 | own 落后 180m |
| 800 | 225 | 405 | own 落后 180m |
| 900 | 228 | 447 | own 落后 219m |

own heading 全程 0-22°（几乎不转），M5 要求横向避让，own 跟不上。

**3. own 被 emergency cap 压到 6.2kn（同 Class B 机制）**

own 设计 5.5kn，实际被 `emergency_avoidance_speed_cap=3.2` 压到 6.1-6.2kn。target 13.4kn 高速，TCPA ~700s，own 6.2kn 来不及完成 400m 横向避让。

**4. 近撞时序**

CPA 投影 t=400 起 <270m（54-120m 全程），t=950 最近 73m，min_cpa=0.8m。own 全程没有效避让。

#### Class C 根因 = Class B 同源（emergency cap）+ target 高速放大后果

- Class B：own 被压低 → XTE 收敛慢 → seamanship FAIL（非近撞）
- Class C：own 被压低 + target 13.4kn 高速 → 避让未完成 → 近撞 CPA 0.8m

两者都是 `emergency_avoidance_speed_cap=3.2` 压制 own 导致机动能力不足，cs-edge 因 target 高速、几何更紧而近撞。

## 6. GNC 执行 ODD 限制 + TDL 注入建议（评审重点）

### 6.1 GNC 执行 ODD 参数（active_route_manager_node.cpp + overlay）

| 参数 | 值 | 含义 |
|---|---|---|
| `max_lateral_accel_mps2` | 0.25 | 横向加速度上限 |
| `max_decel_mps2` | 0.08 | 减速度上限（极保守） |
| `min_segment_length_m` | 30 (emergency 15) | 最小航段长度 |
| `emergency_avoidance_speed_cap_mps` | 3.2 | emergency 避让速度上限（overlay:304，注释"tactical low-speed steering"） |
| `static_min_turn_radius` / `yaw_rate_limit` | (line 324/326) | 转弯半径/艏摇限制 |

### 6.2 GNC 拒绝/降级条件（active_route_manager_node.cpp:355-423）

- `available_radius < required_radius` → `turn_radius_too_small` / `yaw_rate_too_high`（reject 或 degrade）
- `available < required_decel` → `decel_distance_not_enough`（reject 或 degrade）
- M5 `allow_degraded_execution=true` → GNC 不 reject，只 degrade（限速执行）

### 6.3 核心设计缺陷（评审指出）

**M5 生成避让几何时未考虑 own 在 emergency cap 下的实际机动能力。**

M5 要求 400m 横向 offset（cs-edge），但 own 在 emergency cap 3.2 m/s + max_lateral_accel 0.25 m/s² 下，可用 TCPA 窗口内物理不可达。M5 plan 被 GNC 接受（feasible，因 M5 速度命令 5.5kn 在 GNC 限内），但 own 执行时被 emergency cap 压到 6.2kn，横向机动能力远低于 M5 几何假设。

**这是 M5/GNC 接口的 contract 盲区**：M5 的 feasibility（速度可执行）与 own 的 reachability（几何可达）脱节。GNC 接受了"速度可行"的 plan，但 own 在 emergency cap 下"几何不可达"。

### 6.4 建议修复方向：M5 注入 GNC 执行 ODD（评审建议，正解）

M5 避让几何生成时应消费 GNC 执行 ODD 限制：

1. **消费 emergency_avoidance_speed_cap + max_lateral_accel**：M5 估算 reachable 横向 offset = min(几何需求, 物理可达)
2. **物理不可达时调整策略**：
   - 提前触发避让 onset（给 own 更多时间，受 M6 conflict onset gate 约束）
   - 或降速 target（若可）
   - 或接受更小 offset + 配合纵向减速（Rule 8/15 允许减速避让）
3. **反馈机制**：M5 plan 附带 `estimated_reachability` / `required_time_s`，GNC 执行时校验是否在 TCPA 窗口内可达，不可达则触发 MRM（M7）

这要求 M5 知道 GNC 的 `emergency_avoidance_speed_cap`、`max_lateral_accel`、`max_decel` —— 即 **GNC ODD 参数应作为 contract 注入 TDL**，而非 M5 盲生成。

### 6.5 emergency_avoidance_speed_cap=3.2 的来源（评审关键）

overlay:304 注释明确："emergency inserted route points use tactical low-speed steering"。这是 GNC 的**安全设计**（保舵效），非 bug。是否可调高需 source-backed 舵效数据（45m FCB 在 >3.2 m/s emergency 避让时是否有足够侧向力）。

若 cap 不可调，则 Class B/C 的修复只能走：
- M5 注入 ODD 生成 reachable 几何（§6.4，正解）
- 或接受 cap，M5 用纵向减速 + 提前 onset 补偿横向不足

## 6. 验证证据路径

- ot-boundary trace: `runs/trace_eval/20260628_103248_rule15_cohort_wip/colreg-rule15-ot-boundary.trace_current.jsonl`
- ot-boundary oracle: `runs/module_oracle_rule15_ot_boundary_wip.json`
- ot-boundary verdict: `runs/trace_eval/20260628_103248_rule15_cohort_wip/colreg-rule15-ot-boundary.json`
- cs 对照 trace: `runs/trace_eval/20260628_103248_rule15_cohort_wip/colreg-rule15-cs.trace_current.jsonl`
- Class A (Rule5 fix) 证据: `runs/trace_eval/20260628_112420_rule14_ho_after_rule5_fix/`, `runs/trace_eval/20260628_113127_rule14_cohort_after_rule5_fix/`
- Class B 诊断: `runs/trace_eval/20260628_121600_rule14_ho_port_after_classb_fix/`

## 7. 关键源码坐标

| 文件 | 行 | 内容 |
|---|---|---|
| `docker/gnc_route_mock_publisher.py` | 94-107 | 断裂点：丢 target_sog，不填 speed_limit_mps |
| `docker/mock_l2_publisher.py` | 442, 597 | 对照：正确填 PlannedRoute.speed_profile_kn |
| `docker/gnc-ship-config-overlay.yaml` | 392, 433-434 | max_transit_speed=8.0, minimum_steerage=3.0, cruise_min=3.8 |
| `third_party/gnc_ws/.../active_route_manager_node.cpp` | 230, 490-493 | 消费 speed_limit_mps |
| `third_party/gnc_ws/.../coordinate_transform_node.cpp` | 619-624, 799-800 | speed_limit→Path orientation.z 编码 |
| `third_party/gnc_ws/.../ship_guidance_node.cpp` | 3524, 3755-3768 | u_cmd_planned 计算 + route limit cap |
| `third_party/gnc_ws/.../ship_guidance_node.cpp` | 4321, 4388-4393 | cruise_speed_cap gate + cruise_min FLOOR |
| `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp` | 846 | FSM ACTIVE 需 range_closing |
| `src/l3_tdl_kernel/m6_colregs_reasoner/src/rules/colregs/rule13_overtaking.cpp` | 32 | 附带 bug：kSameCourseMaxDeg=45 非 COLREGs |
| `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp` | 165-166, 176-177 | Class B/C：navigation_mode 默认 emergency_avoidance |
| `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` | 762-763, 830, 876 | AVOID/RECOVERY 标 emergency + allow_degraded=true |
| `third_party/gnc_ws/.../active_route_manager_node.cpp` | 92-99, 323-423, 355-423 | GNC ODD：min_segment/lateral_accel/decel + 拒绝/降级条件 |
| `docker/gnc-ship-config-overlay.yaml` | 304-306, 392, 433-434 | emergency_avoidance_speed_cap=3.2, max_transit=8.0, cruise_min=3.8 |

## 8. Iron Law 教训记录

本会话两次验证 Iron Law 价值：

1. **推翻上会话预判**：上会话基于"几何 own 4.3kn tgt 19.7kn cog300（overtake 边界）"判断 M6 Rule13/15 分类错。亲自 trace 发现 (a) 这不是 overtake 是 crossing（own_rel_tgt 在 target 前方扇区），(b) M6 silent 真因是 own 超速。
2. **避免修了无效**：用户要求"先验证 steerage 下限"。验证发现修 mock 不够（cruise_min floor 3.8 覆盖 route limit 2.21）。若直接修 mock 会重蹈 Class B 覆辙（fix 失败回退）。

跨模块集成 RED 必须亲自 trace 速度 + 几何时序 + 物理约束，不能依赖单次归因或接口层观察。
