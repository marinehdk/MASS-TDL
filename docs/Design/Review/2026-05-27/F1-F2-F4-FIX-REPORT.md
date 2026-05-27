---
title: D-DEMO1 F1-F2-F4 修复进度报告（post POST-IMPL-REVIEW）
date: 2026-05-27
author: Claude Opus 4.7 (main agent)
parent: docs/Design/Review/2026-05-27/POST-IMPL-REVIEW-R2-to-R5.md
session_scope: 单 session（约 2 小时）— 修复 POST-IMPL-REVIEW 列出的 P0 阻断中可在 session 内闭口的部分
---

# 1. 已完成修复

## F1a — mock_l2_publisher 增加 RouteReplanRequest 处理 → M3 解锁

**问题**：M3 收 M7 SafetyAlert(MRC_REQUIRED) → 触发 RouteReplanRequest → 进 REPLAN_WAIT 等 L2 响应 → mock L2 不订阅 → M3 永远不发 mission_goal。

**修复**：`docker/mock_l2_publisher.py`
- 新增 `/l2/replan_response` publisher（ReplanResponse IDL，schema=112，RFC-006）
- 新增 `/l3/m3/route_replan_request` subscription
- 回调：收到 replan request 后 <100ms 发 STATUS_SUCCESS + 强制 republish 当前路由

**验证**：
- /l3/m3/mission_goal 从 SILENT → **1.5 Hz** ✓
- M3 从 REPLAN_WAIT 推进到 publishing 状态

## F1b — 新建 diagnostic_mock_publisher → M1 sensor 健康

**问题**：M1 OddEnvelopeManagerNode 订阅 `/l3/diagnostics`（不是标准 `/diagnostics`），无 publisher 时默认所有 sensor degraded → zone_reason='radar_degraded; comm_degraded; tmr_unavailable;' → conformance 0.64 → envelope_state 趋向 MRC_PREP。

**修复**：`docker/diagnostic_mock_publisher.py` (新文件)
- 2 Hz 发布 healthy DiagnosticArray to `/l3/diagnostics`
- 3 个 status entries: sil_mock/radar_health, sil_mock/comm_link (with delay_s=0.05), sil_mock/tmr_voting
- 全部 level=OK，message="operational (SIL mock)"
- 配套修改：`docker/sil_entrypoint.sh` Stage 3a-4 启动 + cleanup；`docker/sil_nodes.Dockerfile` COPY

**验证**：
- /l3/diagnostics Subscription count: 1（M1 已订阅）✓
- M1 conformance_score: **0.64 → 0.9999993** ✓
- M1 zone_reason: 'radar_degraded; comm_degraded; tmr_unavailable;' → **'All dimensions nominal'** ✓

## F2 — M4 fallback 输出几何 starboard 偏置 heading window

**问题**：M4 IvP solver stub 总返回 nullopt → fallback 输出对称 [own_hdg-90°, own_hdg+90°]（180° 宽，无约束）→ M5 据此无法计算 turn。

**修复**：`src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` lines 212-275
- 检查 latest_colregs_ 中 `constraint_type=="colregs"` 且 `numeric_value > 0` 的 starboard deviation
- 若存在 starboard 要求：emit heading window **center = own_hdg + dev_deg ± 15°**（30° 宽，强偏右舷）
- confidence 0.55（vs 原 fallback 0.30）
- rationale 含具体 dev 和窗口值
- ASDR log 区分 "geometric_starboard" vs "cascading"

**验证**：
- M4 rationale: "IvP infeasible — **geometric starboard fallback (dev=15deg, window=359.997→29.9974)**" ✓
- M4 heading_min_deg=359.997, heading_max_deg=29.997 (30° 宽窗口，居中 +15°) ✓
- M4 behavior=1 (COLREG_AVOID), speed_max_kn=22 ✓

## F4 partial — autopilot envelope 放宽 + rising edge force publish

**问题**（来自 reviewer I-1/I-5）：
- 原 autopilot 只在 ENVELOPE_IN 激活；M7 SOTIF 任何 WARNING 把 M1 推到 MRC_PREP 后 autopilot 直接 disabled，ship 失控
- autopilot 启用瞬间最多有 1.5s 死操舵窗口

**修复**：`docker/sil_topic_bridge.py` lines 449-485
- 激活条件改为 `env_state in {ENVELOPE_IN, ENVELOPE_EDGE, ENVELOPE_MRC_PREP}`（排除 OUT 和 MRC_ACTIVE）
- 加 rising_edge 检测，新激活瞬间立刻 publish 不等 timer

**验证**：autopilot 现在在 M1 MRC_PREP 下也能维持 ship（throttle=0.4 = 10kn/25kn 持续输出）

---

# 2. 残留 P0（超出 session 范围，需独立 D-task）

## D-DEMO1-R11 — M7 SOTIF "single assumption violated" 误报根因

**症状**：
- M7 SafetyAlert(severity=WARNING, "SOTIF: single assumption violated — monitoring")
- M1 把这个 WARNING **interpreted 为 MRC_PREP**（"M7 safety alert — MrC_PREP (Checker VETO > Doer)"）

**待查**（按可能性排序）：
1. `src/l3_tdl_kernel/m7_safety_supervisor/src/sotif/assumption_monitor.cpp` evaluate() 6 个 assumption 中哪个返回 true：
   - AIS-radar consistency（world.confidence）
   - Motion predictability（world.confidence）
   - Perception coverage（blind_zone_fraction > max_blind_zone_fraction）
   - COLREGs solvability（colregs.confidence < kColregsConfidenceFailThreshold）
   - Comm link（rtt > threshold OR packet_loss > threshold）
   - Checker veto rate
2. 阈值配置过严（imazu clean scenario 不应触发任何）
3. M1 对 M7 WARNING-severity SafetyAlert 的处理是否应直接 MRC_PREP？— 可能 M1 的安全决策矩阵需要细化

**预估**：1 pw

## D-DEMO1-R12 — M5 mid_mpc solver stub → 真 MPC 实现

**症状**：M5 接收 M4 starboard heading window [0°, 30°] 但 solver 返回 turn_radius=0 解 → wp_gen 出原地踏步 waypoints → bridge 推 rudder=0。

**当前实现** `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:218-227`：
```cpp
const MidMpcSolution sol = solver_.solve(input, warm);
last_solution_ = sol;
const auto plan = wp_gen_.generate(sol, lat, lon);
```
`solver_.solve()` 是 stub（D3.1 待实现，Phase 3 计划）。

**两条路径**：
- A. 实现真 IvP MPC（D3.1 完整版，估 3-5 pw）
- B. 加几何 fallback：检测 M4 heading window 偏置 → wp_gen_ 生成 starboard arc waypoints with turn_radius from rot_max（估 0.5 pw，可作 R12.1 临时桥接）

**预估**：A=3-5 pw，B=0.5 pw

## D-DEMO1-R13 — F4 剩余 3 个 I 项

**未修**：
- I-1：bridge `_compute_transit_autopilot` 后置 zeroing 重复（cosmetic）
- I-2：FSM aggregator 不检查 M4 rationale "fallback" → HMI "lying"
- I-3：mock_l2 `_get_effective_waypoints` 整条路由跟 ownship 平移（destination drift）
- I-4：FSM aggregator confidence 没 clamp 到 [0,1]

**预估**：0.5 pw

---

# 3. 实测数据（修复后，imazu 激活）

```
=== M1 odd_state ===
envelope_state: 3 (MRC_PREP, 因 M7 SOTIF — D-DEMO1-R11 待修)
conformance_score: 0.9999993 ✓ (was 0.64)
zone_reason: 'All dimensions nominal' ✓ (was sensor degraded)

=== M3 mission_goal ===
rate: 1.5 Hz ✓ (was SILENT)

=== M4 behavior_plan ===
behavior: 1 (COLREG_AVOID)
heading_min_deg: 359.997
heading_max_deg: 29.997  ← 30° 右舷窗口 ✓
speed_max_kn: 22.0
rationale: "IvP infeasible — geometric starboard fallback (dev=15deg, window=...)"

=== M5 avoidance_plan ===
target_speed_kn: 10.0 ✓
turn_radius_m: 0.0 🔴 ← stub solver 未消费 M4 heading window
waypoints: ownship 当前位置附近原地踏步

=== ship_dynamics ===
SOG: 10.0 kn ✓ (autopilot 维持)
heading: 0° ✓
rudder: 0 🔴 (因 M5 plan turn_radius=0 → bridge rudder=0)
throttle: 0.40 ✓ (= 10kn/25kn)

=== FSM ===
current_state: 1 (COLREG_AVOIDANCE) ✓
active_rule: "Rule 14 head-on" ✓
```

---

# 4. 综合判决

| 维度 | 状态 |
|---|---|
| M3 解锁（F1a） | ✅ 完成 |
| M1 sensor 健康（F1b） | ✅ 完成 |
| M4 starboard 偏置（F2） | ✅ 完成 |
| Autopilot 鲁棒性（F4 partial） | ✅ 完成 |
| Ship 物理避碰 | 🔴 **未达** — M5 NLP 是 stub，输出 turn_radius=0 |
| DEMO-1 可宣告通过 | 🔴 **不可以** — 物理避碰未发生 |

**净进展**：5 个 P0 中修了 3 个；剩 2 个（M7 SOTIF + M5 stub）需独立 D-task R11/R12。

---

# 5. 推荐下一步

按用户"不允许保留阻断"原则：

1. **优先 R12.B** — M5 几何 fallback（0.5 pw）— 是单 session 范围内可达，能让 ship 物理上真转
2. R11 — M7 SOTIF 调参（1 pw）— 让 M1 恢复 ENVELOPE_IN
3. R13 — 3 个剩余 I 项（0.5 pw）— polish
4. R12.A — 真 D3.1 IvP MPC（3-5 pw）— Phase 3 计划内
