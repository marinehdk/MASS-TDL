# D-DEMO1-R6 — DEMO-1 全模块端到端落地 Spec

| 字段 | 值 |
|---|---|
| Spec ID | D-DEMO1-R6 |
| Owner | 主 agent (MASS-L3-Tactical Layer) |
| 起草日期 | 2026-05-27 |
| 目标 milestone | DEMO-1 Skeleton Live 6/15（不可取消） |
| 前置依赖 | R2/R3/R4/R5 已实施 commits (`fb015648` + 5 后续 commits) |
| 后续动作 | `superpowers:writing-plans` 拆出 4 组并行 plan |
| 关联文档 | [Demo-1场景.md](../../SIL/Demo-1场景.md)、[DEMO-1-评审报告.md](../../SIL/DEMO-1-评审报告.md)、[DEMO-1-四屏闭环实施计划.md](../../SIL/DEMO-1-四屏闭环实施计划.md)、[POST-IMPL-REVIEW-R2-to-R5.md](POST-IMPL-REVIEW-R2-to-R5.md) |

---

## 1. 目标与成功判据

### 1.1 业务目标

让 `imazu-01-ho` (Imazu 1987 Case 01 Head-On) 场景在真实 M1-M8 决策链下跑出 [Demo-1场景.md](../../SIL/Demo-1场景.md) 描述的五阶段物理过程：

```
§1 直航 (T+0~200s)  →  §2 探测分类 (T+200~300s)  →  §3 右转执行 (T+300~450s)
                                          ↓
            §5 自动评估 (T+600~700s)  ←  §4 驶过回归 (T+450~600s)
```

### 1.2 验收断言（可执行测试 oracle）

| ID | 断言 | 测量方式 | 阈值 |
|---|---|---|---|
| A-1 | §1 直航阶段无避碰 | `max(\|own_heading_rad\|) for t∈[0, 200]` | ≤ 0.087 rad (5°) |
| A-2 | §2 阶段触发 Rule 14 | `applicable_rule == "Rule 14"` at some t∈[180, 320] | True |
| A-3 | §3 阶段右舷转向（含上限防过避） | `max(own_heading_rad) for t∈[200, 500]` | 0.436 ~ 0.785 rad (25° ~ 45°) |
| A-4 | §4 安全 CPA | `min(cpa_nm) over full run × 1852` | ≥ 500 m |
| A-5 | §4 归航 | `\|own_heading_rad\| at t=650s` | ≤ 0.087 rad (5°) |
| A-6 | §5 仿真自动 stop | `lifecycle_state == "inactive" at wall_time of t=700s+10s` | True |
| A-7 | §5 scoring 端点返回 verdict | `GET /api/v1/scoring/last_run → kpis≠null AND scoring_dimensions≠null AND verdict in {pass,fail}` | True |
| A-8 | M3 ACTIVE 链路真打通 | `applicable_rule≠"" AND M4 plan.rationale 不含 "IvP infeasible"` 在 §3 期间 | True |
| A-9 | 无 demo dead-reckoning 路径残留 | `grep "demo" src/sil_orchestrator/main.py` 返回 0 行 | True |

**所有 9 条全 PASS = DEMO-1 落地完成。** 任何一条 FAIL → 回 Phase 1 重新分析（systematic-debugging Iron Law）。

---

## 2. 场景背景与根因（已经 Phase 1 验证）

### 2.1 剧本 B 真因链（CPA 时间序列已证）

```
mock_l2 publisher 未集成 imazu-01-ho scenario 启动流程
        ↓
/l2/planned_route 永不发布
        ↓
M3 永驻 AWAITING_ROUTE（Pub count=0）
        ↓
M4 IvP 无 mission goal → 永 infeasible → 走 R12.B 几何 starboard fallback
        ↓
M5 收到 M4 fallback rationale → 自己也走几何 fallback（turn_radius=50m）
        ↓
Bridge LATCH 取 M4 相对窗口 [own_hdg, own_hdg+30°] → target = own_hdg+25°
        ↓
ship 从 T+0 即开始右转，无 §1 直航阶段 ← H-G
        ↓
M4 window 跟随 own_heading 滑动 → ship 持续累积转向到 60.79° 后稳态
        ↓
Bridge LATCH 无 CPA-cleared 释放路径 → ship 永漂 NE 不归 0° ← H-F
        ↓
sim_time 8800s 仍在运行，scoring Arrow API 用错（open_stream vs open_file）→ verdict 无法取出
```

### 2.2 已存在但**未跑主路径**的模块（来自 R6 审计）

| 模块 | 代码状态 | 跑没跑 | gap 类型 |
|---|---|---|---|
| M1 ODD Envelope | FULL（985 LOC + watchdog） | 跑了 | 缺 M3 ACTIVE 超时监控 |
| M2 World Model | FULL（CPA/TCPA 77 处引用） | 跑了 | 待 V3 验证 |
| M3 Mission Manager | FULL（state machine 完整） | **未跑主路径** | wiring 缺：mock_l2 → /l2/planned_route 链路断 |
| M4 Behavior Arbiter | FULL（IvP solver 完整） | **永 infeasible** | 级联 M3 + fallback 走自循环非 SafetyConcernEvent |
| M5 Tactical Planner | FULL（2933 LOC Mid+BC-MPC） | **永 fallback** | 级联 M4 + 等待 V1 验证 |
| M6 COLREGs Reasoner | MVP（head-on 1 行） | **永空** rule | 算法浅 + 触发条件未实现 |
| M7 Safety Supervisor | FULL（Doer-Checker 完整） | 跑了 | 待 V2 验证 |
| M8 HMI/Transparency | FULL（CMM pulse 完整） | 跑了 | OK |

**核心洞察**：底座代码大部分在，**集成 wiring 是断的**。修主链路后多数模块会自动跑起来；M6 和 return-to-nominal 有真实算法缺口。

---

## 3. 与已锁定架构/RFC 对齐

| 锁定决策来源 | 锁定内容 | 本 spec 如何对齐 |
|---|---|---|
| [架构 §7.1-7.3](../../Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) | M3 ACTIVE 由 L2 route ENC + L1 validity gate 触发 | W2 实现该 gate；W3 加 substate 暴露 gate 状态 |
| 架构 §10.3-10.4 | M5 BC-MPC 输出 k=7 离散绝对航向候选 | W4 fallback 也用绝对（snapshot），与 BC-MPC 语义一致 |
| 架构 §12 | M7 Doer-Checker 独立实现路径 | V2 验证；W4 路由 SafetyConcernEvent 不破坏独立性 |
| RFC-003 | M1 15s 滑窗看门狗监 M3 / M5 健康 | W9 实现 M3 ACTIVE 维度看门狗 |
| RFC-006 | mock_l2 → M3 接口契约（1Hz PlannedRoute） | W1 集成 mock_l2 publisher；W2 验证 M3 订阅契约 |
| `fallback_policy.py` 设计意图 | infeasible → emit SafetyConcernEvent → M7 仲裁 | W4 拆 M4 自循环，改走 SafetyConcernEvent |
| Demo-1场景.md §3 | 避碰偏角预计 ~35° | W4 fallback snapshot 30° 窗口（5/6 取值 → 25° 偏置），合理 |
| Demo-1场景.md §4 | 平滑回归 nominal | W6 实现 bridge LATCH 释放 + M5 cost 自然收敛 |

---

## 4. Scope —— 10 W-item + 3 V-item

### 4.1 A 组：决策链主路径打通

#### W1: mock_l2 publisher 集成 imazu-01-ho scenario

**问题**：`src/sim_workbench/mock_publishers/l3_external_mock_publisher/external_mock_publisher.py` 声明 1Hz 发布 `/l2/planned_route` + `/l1/voyage_task`，但 docker-compose / sil_entrypoint / scenario YAML 都未启动该节点。

**修改**：
- `docker/sil_entrypoint.sh` Stage 2-3 之间显式 `ros2 run sim_workbench l3_external_mock_publisher` 后台运行
- 节点参数从 scenario YAML 的 `mock_l2` 段读取（route waypoints、cruise speed、voyage_task fields）
- 若 scenario YAML 无 `mock_l2` 段则节点输出 default：nominal route 直线北行 + voyage_task autonomy_level=D3

**输出契约**：
```
/l1/voyage_task (sil_proto/VoyageTask)     @ 1Hz
/l2/planned_route (sil_proto/PlannedRoute) @ 1Hz
```

**imazu-01-ho.yaml 需加段**：
```yaml
mock_l2:
  voyage_task:
    autonomy_level: "D3_SUPERVISED"
    mission_id: "imazu-01-ho-demo"
  planned_route:
    waypoints:
      - {lat: 63.44, lon: 10.38}      # start
      - {lat: 63.60, lon: 10.38}      # north 9.5nm
    cruise_speed_kn: 10.0
```

**Effort**: S (0.3-0.4pw)

---

#### W2: M3 RouteReceived 事件 wiring 端到端验证

**问题**：`mission_state_machine.cpp:L85-86` 写了 `RouteReceived → ACTIVE`，但 W1 修通后 Pub count 是否真 ≥ 1 待验证。

**修改**：
- 读 `mission_manager_node.cpp:L368-388` RouteReceived 触发条件，确认 PlannedRoute 消息收到即 fire
- 验证 M3 ACTIVE 后 `/l3/m3/mission_state` Publisher count ≥ 1，且 1Hz 心跳
- 如发现 condition 漏判，**仅修该 condition**（surgical），不重写 state machine
- 加 RCLCPP_INFO 日志：每次 state transition 打 from/to/trigger

**输出契约**：
```
/l3/m3/mission_state (sil_proto/MissionState)
  fields: state (enum), task_validity (enum), rationale (string)
  rate: 1Hz when ACTIVE, 0.2Hz when AwaitingRoute
```

**Effort**: S (0.3pw)

---

#### W3: M3 `task_validity_pending` 子状态对外发布

**问题**：v1.1.3 §7 锁定 M3 ACTIVE 后内部应有 `task_validity_pending` 子状态，对外发布让 M4 决定是否激活仲裁。当前缺。

**修改**：
- `mission_state_machine.cpp` 引入子状态枚举 `TaskValidity ∈ {pending, valid, invalid, replanning}`
- ACTIVE 状态下，子状态在 `L1 task + L2 route + ENC check + autonomy level` 4 条件全过 → `valid`
- 任意失败 → `invalid` + `rationale` 字段说明原因
- `MissionState.msg` 加 `task_validity` 字段

**M4 联动**：
- M4 订阅 `/l3/m3/mission_state`，当 `task_validity != valid` 时 IvP 仲裁不激活（输出 `behavior_plan.behavior=IDLE` + `rationale="m3 not ready"`）
- M4 不再 emit "IvP infeasible" 自循环 fallback

**Effort**: M (0.6pw)

---

#### W4: M4 fallback 绝对 snapshot 化 + 走 SafetyConcernEvent

**问题**：当前 M4 IvP infeasible 时输出相对窗口 `[own_hdg, own_hdg+30°]`，正反馈导致 ship 持续右转累加。设计意图是 emit `SafetyConcernEvent` 给 M7 仲裁。

**修改**：
- 删除 M4 当前的 "geometric starboard fallback" 自循环路径
- 改为：IvP infeasible 时
  1. 首次触发瞬间 snapshot 当前 own_heading 为 `fallback_anchor_hdg`
  2. emit `SafetyConcernEvent{ concern: ivp_infeasible, anchor_hdg: fallback_anchor_hdg, suggested_action: starboard_30deg_absolute }` 给 M7
  3. behavior_plan 输出 `heading_window = [fallback_anchor_hdg, fallback_anchor_hdg + 30°]` **绝对值，不再更新**
  4. M7 收到 SafetyConcernEvent 后：若安全级别 OK 则放行 M4 absolute window 给 M5；若不 OK 则 emit MRM 命令 override
- 释放条件：M3 task_validity 切回 valid → 清 snapshot → 重新走主路径

**输出契约**：
```
M4: /l3/m4/behavior_plan
  fields: heading_min_deg, heading_max_deg (absolute when fallback),
          fallback_anchor_hdg (optional), rationale
  rate: 4Hz
M4→M7: /l3/safety/concern (sil_proto/SafetyConcernEvent)
  fields: concern_type, anchor_hdg, suggested_action, severity, stamp
```

**Effort**: M (0.8pw)

---

#### W5: M6 Rule 14 head-on 分类器扩展

**问题**：当前 M6 head-on 检测只有 1 行逻辑（per audit）。需扩展到能在 imazu-01-ho 场景 §2 阶段（T+200~300s）真触发 Rule 14。

**修改**：
- M6 head-on 触发三条件（COLREGs Rule 14 标准）：
  1. **互逆 heading**：`|heading_diff - 180°| < 22.5°`（IMO 标准 head-on 扇区）
  2. **bearing rate 近 0**：`|d(bearing)/dt| < 0.5°/min`（持续 30s）
  3. **range 闭合中**：`d(range)/dt < 0`（持续 30s）
- 满足三条件后发布 `applicable_rule="Rule 14"` + `expected_action="turn_starboard"` + `confidence=0.91`
- M4 订阅 M6 输出，COLREG_AVOIDANCE behavior 权重 +0.6

**输出契约**：
```
/l3/m6/rule_assessment (sil_proto/RuleAssessment)
  fields: applicable_rule (string), expected_action (string),
          confidence (float), trigger_conditions (list[string]),
          stamp, target_mmsi
  rate: 2Hz
```

**Effort**: M (0.8pw)

---

### 4.2 B 组：物理闭环

#### W6: Bridge LATCH 释放 + return-to-nominal

**问题**：当前 `sil_topic_bridge.py` LATCH 仅在 `has_valid_plan = False` 时释放，无 CPA-cleared 出口；ship 永漂。

**修改**：
- Bridge 订阅 M2 `/l3/m2/threat_state`（含 `cpa_status ∈ {closing, sustained, cleared}`）
- LATCH 释放条件三选一：
  1. `cpa_status == cleared` 且 `target_relative_position == astern`（M2 已判定通过）
  2. `M3 task_validity == valid` 且 `M4 behavior == TRANSIT`（M4 重新主路径）
  3. 紧急：M7 MRM override（绝对优先）
- 释放方式：5 秒线性下降 LATCH offset 到 0，让 PID 自然把 heading 拉回 nominal route bearing
- M5 cost function 不需要改（架构 §10.4 隐式收敛已在）

**输出契约**：
- 订阅：`/l3/m2/threat_state.cpa_status`, `/l3/m3/mission_state.task_validity`
- LATCH state 暴露到 `/sil/bridge_state` 便于 Foxglove 观察

**Effort**: M (0.6pw)

---

#### W7: 仿真 700s 自动 stop

**问题**：当前 imazu-01-ho 跑了 8800s 不 stop。scenario YAML `simulation.duration_s=700` 字段未被消费。

**修改**：
- `src/sil_orchestrator/lifecycle_bridge.py` 启动 scenario 时读 YAML `simulation.duration_s`
- 启动定时器：到时调用 `/api/v1/lifecycle/deactivate` 内部函数 + 提交 scoring 计算
- deactivate 后 lifecycle_state → "inactive"，但保留 run-* 目录数据
- 加 `/api/v1/lifecycle/status` 字段 `time_remaining_s` 便于前端显示

**Effort**: S (0.3pw)

---

### 4.3 C 组：评分与可观测性

#### W8: scoring Arrow API 修复

**问题**：`scoring_routes.py` 用 `pa.ipc.open_stream()` 读 scoring.arrow，但 ArrowWriter 写的是 file (footer) 格式 → `InvalidFooter`。已在容器实测 `open_file()` 可读 10323 行。

**修改**：
- `scoring_routes.py` 单行改：`pa.ipc.open_stream(src)` → `pa.ipc.open_file(src)`
- 加 try-except：若仍失败 fallback 到 `scoring.json`（保留兼容旧 run）
- 同时修 KPIs 聚合：当前可能只 expose `min_cpa_nm`，需 aggregate 6-dim scoring + verdict

**Effort**: XS (0.1pw)

---

#### W9: M1 15s 看门狗监 M3 ACTIVE

**问题**：W1-3 修通后仍可能因网络/timing 偶发失败。需安全网。

**修改**：
- `odd_envelope_manager_node.cpp` 加滑窗：`m3_active_since_last_route_received_s`
- 若 M3 处于 ACTIVE 但子状态 `task_validity != valid` 持续 > 15s → 触发 `M7 SOTIF{ category: m3_route_stale, severity: WARNING }`
- M7 收到 SOTIF 切 ODD envelope_state → DEGRADED → bridge 进 hold-station 模式（不再 fallback runaway）

**Effort**: M (0.6pw)

---

### 4.4 D 组：清理

#### W10: demo dead-reckoning 路径完全下线

**问题**：`demo_avoidance.py`、`demo_scorer.py`、`main.py` 的 `/api/v1/demo/*` endpoint 仍存。`effective_backend` gating 仍写 `"demo" if not _HAS_RCLPY`。`asdr_routes.py` 反向依赖 `_avoidance_state` global。

**修改**：
- 删除 `src/sil_orchestrator/demo_avoidance.py`
- 删除 `src/sil_orchestrator/demo_scorer.py`
- `src/sil_orchestrator/main.py`：
  - 删 L119-129 globals
  - 删 L360-489 `demo_telemetry` endpoint
  - 删 L490+ `demo_reset` endpoint
  - 4 处 `effective_backend = "demo" if not _HAS_RCLPY else backend` 改为 force `"ros2"`，启动期断言 `_HAS_RCLPY=True` 否则 fail-fast
- `src/sil_orchestrator/asdr_routes.py`：**重构** L6/L133-139 改从 `/l3/m4/behavior_plan.rationale` + `/l3/m6/rule_assessment` + `/sil/actuator_cmd` 派生 ASDR events，不再读 `_avoidance_state`
- `src/sil_orchestrator/scenario_store.py` L104/L112：默认 backend → `"ros2"`
- 前端 `web/src/screens/SimulationMonitor.tsx` L273-280：移除 backend 分支（仅 ros2）
- 所有 scenario YAML 加 `backend: ros2` 字段或确认 store 默认行为

**Effort**: M (0.6pw)

---

### 4.5 验证项（无新代码）

#### V1: M5 Mid-MPC 真跑主路径验证

**做**：W1-9 落地后实测：
- `ros2 topic echo /l3/m5/avoidance_plan` 的 `rationale` 字段在 §3 期间不应含 "M5 geometric starboard fallback"，而是包含 "Mid-MPC" / "BC-MPC" 等真实算法名
- M5 waypoint 数量 ≥ 5（geometric fallback 只输出 2-3 个）
- `turn_radius_m` 反映 MPC 输出（非硬编码 50.0）

**失败处置**：开 D-task `D-DEMO1-R7` 专修 M5 Mid-MPC 集成。

---

#### V2: M7 Doer-Checker 独立性

**做**：
- `grep` 验证 M7 代码不 import M5 / M4 的算法实现（仅订阅 topic）
- 静态独立性审计：M7 输出 MRM 命令时若 M4 同时 emit conflicting plan，bridge 应 honor M7
- 写 unit test：mock M4 plan + M7 MRM 同 tick 到达 → bridge 输出应为 MRM

---

#### V3: M2 World Model 真发布 threats

**做**：
- `ros2 topic echo /l3/m2/threats` 应在 imazu-01-ho 期间含 TS1 entry
- Foxglove 应能可视化 threats（cpa_nm, tcpa_s, ship_id 字段齐）

---

## 5. 错误处理与降级模式

| 失败场景 | 检测者 | 响应 | 结果 |
|---|---|---|---|
| mock_l2 节点未启动 | M3 (无 route 超 15s) | M1 watchdog → M7 SOTIF | ODD → DEGRADED，bridge hold-station |
| M3 收到无效 route（waypoint 不合法） | M3 自身 validity check | task_validity=invalid + rationale | M4 不仲裁，等待重 plan |
| M4 IvP infeasible（即使 W3 完成） | M4 | snapshot + SafetyConcernEvent → M7 | M7 仲裁后放行 absolute window 或下 MRM |
| M5 Mid-MPC 求解失败 | M5 | BC-MPC fallback；若 BC-MPC 也失败 emit SafetyConcernEvent | M7 仲裁；最差进 MRM |
| Bridge LATCH 释放后 PID 不收敛回 nominal | M2 watchdog（持续 30s heading error > 30°）| emit threat | bridge 重 LATCH 到 nominal route bearing |
| Sim 自动 stop 失败（W7 没触发） | sil_orchestrator 备份定时器（duration_s + 30s） | 强制 deactivate | 记 warning 但 run 不丢 |
| scoring Arrow 仍读失败 | scoring_routes | fallback 到 scoring.json | KPIs partial 但 endpoint 不 500 |

---

## 6. 测试策略

### 6.1 端到端失败测试（先写，TDD red 启动）

文件：`tools/sil/test_demo1_imazu01ho_e2e.py`

```python
"""DEMO-1 imazu-01-ho 端到端物理验收测试。

成功条件：A-1 ~ A-9 全 PASS。任何一项 FAIL → DEMO-1 未达标。
"""
import json, math, time
import urllib.request

BASE = "https://localhost:8000/api/v1"
SCENARIO = "imazu-01-ho"

def setup_module():
    _post("/lifecycle/cleanup")
    _post("/lifecycle/configure", {"scenario_id": SCENARIO})
    _post("/lifecycle/activate")

def teardown_module():
    _post("/lifecycle/cleanup")

def test_A1_transit_straight():
    """§1: T+0~200s heading 不偏离北向 >5°"""
    samples = _collect_topic("/sil/own_ship_state", from_t=0, to_t=200, n=10)
    max_dev_rad = max(abs(s["heading"]) for s in samples)
    assert max_dev_rad <= math.radians(5.0), f"transit heading drift {math.degrees(max_dev_rad):.1f}°"

def test_A2_rule14_triggered():
    """§2: applicable_rule 在 t∈[180, 320] 切到 Rule 14"""
    samples = _collect_topic("/l3/m6/rule_assessment", from_t=180, to_t=320, n=20)
    assert any(s["applicable_rule"] == "Rule 14" for s in samples), "Rule 14 never triggered"

def test_A3_starboard_turn():
    """§3: heading 右偏到 25-40° 之间"""
    samples = _collect_topic("/sil/own_ship_state", from_t=200, to_t=500, n=30)
    max_hdg = max(s["heading"] for s in samples)
    assert math.radians(25) <= max_hdg <= math.radians(45), \
        f"turn magnitude {math.degrees(max_hdg):.1f}° outside [25,45]"

def test_A4_safe_cpa():
    """§4: min CPA >= 500m"""
    s = _get("/scoring/last_run")
    min_cpa_m = s["kpis"]["min_cpa_nm"] * 1852.0
    assert min_cpa_m >= 500.0, f"min_cpa={min_cpa_m:.0f}m < 500m"

def test_A5_return_to_nominal():
    """§4 末: t=650s heading 回归 0° ±5°"""
    sample = _collect_topic("/sil/own_ship_state", from_t=650, to_t=655, n=1)[0]
    assert abs(sample["heading"]) <= math.radians(5.0), \
        f"return heading {math.degrees(sample['heading']):.1f}° > 5°"

def test_A6_auto_stop():
    """§5: 到 710s 时 lifecycle 已 inactive"""
    _wait_until_wall_s(710)
    status = _get("/lifecycle/status")
    assert status["current_state"] == "inactive", \
        f"lifecycle still {status['current_state']} at t=710s"

def test_A7_scoring_complete():
    """§5: scoring 返回 verdict + dimensions"""
    s = _get("/scoring/last_run")
    assert s.get("kpis") is not None
    assert s.get("scoring_dimensions") is not None
    assert s.get("verdict") in {"pass", "fail"}

def test_A8_decision_chain_real():
    """§3 期间 M4 不在 IvP infeasible fallback"""
    samples = _collect_topic("/l3/m4/behavior_plan", from_t=300, to_t=400, n=10)
    for s in samples:
        assert "IvP infeasible" not in s["rationale"], \
            f"M4 still in fallback at t={s['stamp']}: {s['rationale']}"

def test_A9_no_demo_path():
    """demo dead-reckoning 已下线"""
    import subprocess
    r = subprocess.run(
        ["grep", "-c", "demo", "src/sil_orchestrator/main.py"],
        capture_output=True, text=True,
    )
    assert int(r.stdout.strip() or "0") == 0, "demo references remain in main.py"
```

**测试启动条件**：W1-9 全部 land 后；测试通过 = DEMO-1 verdict=pass。

### 6.2 单元测试覆盖要求

| W-item | 单测目标 |
|---|---|
| W2 | M3 RouteReceived 触发逻辑（mock PlannedRoute 各种 shape） |
| W3 | `task_validity` 4 条件矩阵（每条件 pass/fail × 16 组合 → 抽样 8） |
| W4 | M4 fallback snapshot 不再 update + SafetyConcernEvent emit |
| W5 | M6 Rule 14 三条件矩阵（各条件 pass/fail × 8 组合） |
| W6 | Bridge LATCH 释放三种触发器单独验证 |
| W7 | scenario duration 字段读取 + auto-stop timer |
| W8 | Arrow open_file path + JSON fallback path |
| W9 | M1 watchdog 15s 阈值精度 + SOTIF emit |
| W10 | scenario_store 默认 backend；asdr_routes 不依赖 _avoidance_state |

---

## 7. 拆解建议（writing-plans 输入）

按 worktree 独立性分 4 组，可并行：

| 组 | W-items | worktree | 串行依赖 |
|---|---|---|---|
| **A** 决策链 | W1, W2, W3, W4, W5 | `.worktrees/d-demo1-r6-decision-chain` | W1→W2→W3→W4；W5 与 W3-4 并行 |
| **B** 物理闭环 | W6, W7 | `.worktrees/d-demo1-r6-physical-loop` | 依赖 A 组 W3 完成（订阅 mission_state） |
| **C** 评分与可观测 | W8, W9 | `.worktrees/d-demo1-r6-scoring-watchdog` | W8 独立；W9 需 A 组 W3 完成 |
| **D** 清理 | W10 | `.worktrees/d-demo1-r6-demo-cleanup` | 完全独立 |
| **V** 验证 | V1, V2, V3 | 主 worktree | 依赖 A+B+C 全部 merge |
| **E2E** | 端到端测试运行 | 主 worktree | 依赖 V1-3 通过 |

**推荐执行顺序**：
1. D 组 (W10) + C 组 W8 — 完全独立，先 land 减小后续认知负载
2. A 组 W1-W5 — 主链路，按顺序 land
3. B 组 W6-W7 + C 组 W9 — 依赖 A 组完成
4. V1-V3 — 验证 module landing 真实性
5. E2E test 跑通 = DEMO-1 验收

---

## 8. 真正排除（限 spec 体量，非走捷径）

下面 3 项已**评估**且**不进本 spec**，理由明确：

| 排除项 | 理由 | 何时做 |
|---|---|---|
| **M5 Mid-MPC 算法本体改动** | V1 验证若通过即认为已 land；现有 2933 LOC 已实现 Mid-MPC + BC-MPC 双层，仅缺集成 | V1 fail → 开 `D-DEMO1-R7` |
| **M7 MRM-02/03 真实执行 + ship_dynamics 接收 MRM 命令** | DEMO-1 §1-§5 不应触发 MRM（CPA 3.48nm 远超 0.27nm 阈值），DEMO-2/3 才需 | D2.x 阶段 |
| **前端 Screen 4 评分 6-dim 完整可视化** | W8 让数据流通即可；前端美化不影响后端验收 | D1.7（HMI 完善阶段） |

---

## 9. Open Questions / 风险

| Q/R ID | 内容 | 应对 |
|---|---|---|
| **Q-1** | mock_l2 publisher 节点是否需在 SIL 之外用真 L2 voyage_planner 替换？ | DEMO-1 用 mock；DEMO-3 接真 L2 时另开 D-task |
| **Q-2** | M4 fallback `fallback_anchor_hdg` snapshot 时机：first IvP infeasible 还是首次进入 ACTIVE 后第一次 infeasible？ | 推荐"首次进入 ACTIVE 后第一次 infeasible"，W4 spec 化时定 |
| **Q-3** | Bridge LATCH 5 秒线性下降是否足够平滑（rudder rate 限制）？ | W6 实现时测量 actuator_cmd rate；若 rate > 限制则改 10 秒 |
| **R-1** | V1 (M5 Mid-MPC 真跑) 失败概率：中。Mid-MPC 集成可能有隐藏 bug，CCS 评估前需暴露 | 留预算 0.8pw 给 R7（若需要）|
| **R-2** | W5 (M6 Rule 14) bearing-rate 阈值在 imazu-01-ho 头对头场景可能极敏感（接近 head-on 时 bearing 数学上不定义） | spec implementation 时加 range-dependent 阈值放宽 |
| **R-3** | W10 删 demo 后若 sil-nodes container 启动失败（无 _HAS_RCLPY），整个系统 fail-fast，无 demo 兜底 | 接受：fail-fast 是设计意图；容器健康检查在 sil_entrypoint 已就位 |
| **R-4** | sim auto-stop (W7) 与 lifecycle 现有 deactivate 路径可能冲突 | spec 时审 lifecycle_bridge.py 现有 deactivate 全路径，确保 idempotent |

---

## 10. 验收 Definition of Done

- [ ] W1-W10 全部 commit 到 main（每 W-item 一个 PR / 一次 review）
- [ ] V1-V3 验证全部 pass
- [ ] `tools/sil/test_demo1_imazu01ho_e2e.py` 9 个 assertions 全绿
- [ ] `npm run sys:start` → 等 60s → `pytest tools/sil/test_demo1_imazu01ho_e2e.py` 一键通过
- [ ] Phase 1 progress 文档更新（`docs/Design/Phase 1/00-overview.md` + 各 M{n}-progress.md）
- [ ] DEMO-1 演示视频录制（≥ 5min，覆盖五阶段）

---

## 11. 引用

- [Demo-1场景.md](../../SIL/Demo-1场景.md)：五阶段物理过程权威定义
- [DEMO-1-评审报告.md](../../SIL/DEMO-1-评审报告.md)：P0-P1-P2 修复清单
- [DEMO-1-四屏闭环实施计划.md](../../SIL/DEMO-1-四屏闭环实施计划.md)：四屏数据流闭环（与本 spec 互补）
- [POST-IMPL-REVIEW-R2-to-R5.md](POST-IMPL-REVIEW-R2-to-R5.md)：R2-R5 实施后的 gap 报告（含 H-A/H-F 假设来源）
- [架构设计报告 v1.1.3-pre-stub](../../Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) §7, §10, §12
- RFC-003（M1 watchdog） / RFC-006（mock_l2 contract）— in `docs/Design/Cross-Team Alignment/RFC-decisions.md`
- Phase 1 hypothesis verification（H-A 至 H-G）：本 session 主对话历史 + CPA 时间序列证据
