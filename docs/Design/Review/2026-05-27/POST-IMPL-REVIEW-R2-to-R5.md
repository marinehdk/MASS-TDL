---
title: D-DEMO1 R2-R5 实施后审查报告（post-implementation review）
date: 2026-05-27
auditor: Claude Opus 4.7 (main agent) + superpowers:code-reviewer subagent + 独立活体复测
git_range: 463f43a0..086f0ea2 (6 commits, 3243+/33-, 23 files)
verdict: 代码可合并；DEMO-1 不可宣告完成
---

# 1. 用户 R6 自评表 vs 独立复测

| 验收项 | 用户报告 | 独立复测（imazu 已激活） | 判定 |
|---|---|---|---|
| /l1/voyage_task | 0.5 Hz PASS | 1.0 Hz | ✅ 实际更好 |
| /l2/planned_route | 1.0 Hz | 2.0 Hz | ✅ 实际更好 |
| /l3/m3/mission_goal | 0.5 Hz PASS | **🔴 SILENT** (Pub=1 但不发数据) | ❌ 虚报 |
| /l3/fsm_state | 10.0 Hz | 19.9 Hz | ✅ |
| /sil/actuator_cmd | 1.0 Hz | 1.0 Hz | ✅ |

附加：/l3/m3/mission_state Pub=0（R1 原阻断未修复）；`m3_mission_manager` 节点 `ros2 node list` 找不到。

# 2. 代码 review（subagent code-reviewer）

🟢 Critical = 0；两 audit P0 修复正确：
- bridge autopilot 用 `envelope_state == ODDState.ENVELOPE_IN` ✓
- frontend 用 `wss://127.0.0.1:8765` 与 docker-compose foxglove TLS 配置一致 ✓

🟡 Important = 5：
- **I-1** sil_topic_bridge.py:393-421 `_compute_transit_autopilot` 后置 zeroing 重复
- **I-2** fsm_aggregator_node.py:183-186 不检查 M4 rationale → fallback 也显示 COLREG_AVOIDANCE（HMI 说谎）
- **I-3** mock_l2_publisher.py `_get_effective_waypoints` 整条路由跟 ownship 平移
- **I-4** fsm_aggregator_node.py:196 confidence 没 clamp 到 [0,1]
- **I-5** sil_topic_bridge.py autopilot 启用瞬间不立即发，最多 3.5s 死操舵窗口

⚪ Minor = 7（emoji 一致性 / py 版本注解 / dead code 等，详见 reviewer 报告附录）

# 3. 三个"已知遗留"判定

| Remnant | 用户判定 | 我的判定（基于实测） |
|---|---|---|
| (a) M1 envelope_state=3 MRC_PREP | 非阻断 | 🟢 **用户对** — 实测 FSM 在 imazu 下因 M4 behavior 优先级 > M1 envelope 显示 COLREG_AVOIDANCE |
| (b) lifecycle cleanup-timeout 绕过 | 非阻断 | 🟡 **单次 demo OK** — 多次需 docker compose restart |
| (c) M4 IvP solver stub fallback | 非阻断（"速度已修复 22kn"） | 🔴 **用户错** — 实测 30s 观察 ship 全程 rudder=0、lat 单调北上、heading 不变 → **根本没避碰转向** |

**用户漏报第 4 项 P0 阻断**：M3 完全没启动（mission_goal/mission_state 都 SILENT；节点 list 找不到 m3_mission_manager）。R4 mock L2"解锁 M3"目标未达成。

# 4. 综合判决

| 维度 | 判决 |
|---|---|
| 代码合并到 main | 🟢 可以（P0 audit fix 全到位、Critical = 0、跨 plan 对齐 OK） |
| DEMO-1 验收宣告通过 | 🔴 不可以 — 物理避碰未发生，min CPA / 右转 35° 无法证实 |
| 用户 R6 表准确性 | 🟡 4/5 真，1/5 虚报 |
| 3 个 remnant 判定 | 🟡 2/3 合理 (a, b)，1/3 不合理 (c) |
| 隐藏 P0 | 🔴 M3 链路实际未解锁 |

# 5. 跟进 D-task 建议

| ID | 内容 | 优先级 | 预计 |
|---|---|---|---|
| D-DEMO1-R7 | 修 I-1..I-5（5 个 Important issue） | P1 | 0.5 pw |
| D-DEMO1-R8 | 修 M3 mission_goal silent（找 m3_mission_manager 节点为何不启动 / 不发数据） | **P0** | 1.0 pw |
| D-DEMO1-R9 | 修 M4 fallback：在 fallback 时仍要产出真避碰 heading（不需要完整 IvP solver） | **P0** | 1.5 pw |
| D2.1 (Phase 2) | M1 MRC_PREP 调参；ODD sensor 降级阈值 | P2 | 在原计划内 |
| D3.1 (Phase 2) | M4 真 IvP solver 实现 | P2 | 在原计划内 |
| D-DEMO1-R10 | 写真自动 CI 验收：跑 700s imazu → 拉 trajectory CSV → 算 min CPA + 最大 heading 偏转 → 自动 PASS/FAIL | P1 | 0.5 pw |

# 6. 实测数据 dump（证据存档）

## 6.1 M4 behavior_plan（imazu 激活后）
schema_version: 113
behavior: 1
heading_min_deg: 270.00, heading_max_deg: 90.00
speed_max_kn: 22.0 ✓ R3 修复验证
confidence: 0.30
rationale: "IvP infeasible fallback" ← 永远 fallback

## 6.2 M1 odd_state
envelope_state: 3 (MRC_PREP)
auto_level: 3, health: 1
conformance_score: 0.64
zone_reason: 'radar_degraded; comm_degraded; tmr_unavailable;'
rationale: "M7 safety alert — MrC_PREP (Checker VETO > Doer)"

## 6.3 FSM state
current_state: 1 (COLREG_AVOIDANCE)
active_rule: "Rule 14 head-on"
rationale: "state=COLREG_AVOIDANCE rule='Rule 14 head-on' odd_env=MRC_PREP beh=COLREG_AVOID safety_sev=WARNING avoid_wp=4"
confidence: 0.30

## 6.4 actuator_cmd
throttle: 0.40 ✓（10kn / 25kn）
rudder_angle: 0.0 ← 全程 0，没转向

## 6.5 own_ship 30s 内位移
T+0:  (63.4487, 10.3800), heading=0
T+30: (63.4567, 10.3800), heading=0 ← 单纯北上 ~890m，零横向

## 6.6 M3 link 拓扑
/l3/m3/mission_state: Publisher count: **0** (subscribers: 1)
/l3/m3/mission_goal:  Publisher count: 1 / subscribers: 3 / **SILENT** (no data flow)
/l3/m3/route_replan_request: Publisher count: 1 / subscribers: 0
ros2 node list | grep -i m3 → /m3_mission_manager ... lifecycle get → "Node not found"

# 7. 签名
- 主 agent: Claude Opus 4.7
- code-reviewer subagent: claude (sonnet, via superpowers:code-reviewer)
- 独立活体验证：docker exec mass-l3-tacticallayer-sil-nodes-1 ros2 topic hz/echo（imazu-01-ho 激活后）
- 完成时间: 2026-05-27
