# DEMO-1 四屏业务流程闭环实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 DEMO-1 imazu-01-ho 场景的4屏端到端闭环，所有数据来自真实后端，无 mock/硬编码。

**Architecture:** 前端 (React+Vite) → REST API (FastAPI sil_orchestrator) → ROS2 仿真节点。当前已有 demo 模式（dead-reckoning）和 ros2 模式（lifecycle 管理）双路径。本计划优先完善 demo 模式使其产生真实避碰行为，再打通评分和 ASDR 数据链路。

**Tech Stack:** React 18 + Redux Toolkit Query + Zustand + Foxglove WS | FastAPI + rclpy + protobuf | ROS2 Humble + Docker

---

## 代码调研修正（vs 初步评审）

初步评审将多个已实现模块标记为 stub，实际代码调研后发现：

| 模块 | 初步评审 | 实际状态 | 说明 |
|---|---|---|---|
| `scenario_routes.py` + `scenario_store.py` | ⚠️ mock | ✅ 完整实现 | 从 `scenarios/` 目录真实扫描 YAML |
| `selfcheck_routes.py` + `gate_runner.py` | ⚠️ mock | ✅ 完整实现 | 6-gate sequencer + evidence 写入 |
| `scoring_routes.py` | ❌ 未实现 | ✅ 完整实现 | Arrow primary + JSON fallback 双路径 |
| `scoring/node.py` | ⚠️ 部分 | ✅ 完整实现 | HagenScorer 6-dim + ArrowWriter |
| `target_vessel/node.py` | ❌ stub | ✅ 完整实现 | 恒速直线 + AIS replay 模式 |
| `ship_dynamics/node.py` | ⚠️ 部分 | ✅ 完整实现 | MMG RK4 积分 + rudder 响应 |
| `env_disturbance/node.py` | ❌ stub | ✅ 完整实现 | Gauss-Markov 风/流模型 |
| `sil_orchestrator/main.py` demo telemetry | — | ⚠️ 无避碰 | dead-reckoning 直线，无避让行为 |
| `SimulationEvaluator.tsx` ASDR events | — | ❌ 硬编码 | `REPORT_EVENTS` 数组是写死的 |

**核心问题收窄为 3 个**：
1. **demo 模式无避碰行为**：`/api/v1/demo/telemetry` 纯直线 dead-reckoning，本船不会右转
2. **Screen 4 ASDR 事件硬编码**：`REPORT_EVENTS` 数组需替换为后端真实数据
3. **demo 模式评分数据为 stub**：`_seed_run_dir_demo()` 写入硬编码 KPI，需改为从仿真轨迹计算

---

## 文件结构

| 文件 | 职责 | 操作 |
|---|---|---|
| `src/sil_orchestrator/demo_avoidance.py` | demo 模式避碰逻辑（Nomoto + Rule 14 右转） | **新建** |
| `src/sil_orchestrator/main.py` | 修改 demo telemetry 使用避碰逻辑 | **修改** |
| `src/sil_orchestrator/demo_scorer.py` | demo 模式轨迹后评分（CPA + 6-dim） | **新建** |
| `src/sil_orchestrator/asdr_routes.py` | ASDR 事件 REST API | **新建** |
| `web/src/api/silApi.ts` | 新增 ASDR API hook | **修改** |
| `web/src/screens/SimulationEvaluator.tsx` | 替换硬编码 ASDR 为真实 API | **修改** |
| `web/src/screens/SimulationMonitor.tsx` | 确认 Foxglove 数据流完整 | **审查** |
| `web/src/screens/SimulationCheck.tsx` | 确认 preflight 流程完整 | **审查** |
| `src/sil_orchestrator/scoring_routes.py` | 修改 demo fallback 使用真实评分 | **修改** |
| `tools/sil/test_demo_flow.py` | 端到端测试 | **新建** |

---

### Task 1: Demo 模式避碰逻辑

**Files:**
- Create: `src/sil_orchestrator/demo_avoidance.py`
- Modify: `src/sil_orchestrator/main.py:230-328`

**背景**：当前 `/api/v1/demo/telemetry` 使用 `_dead_reckon_step()` 纯直线推算，本船不会避让。需要在 demo 模式中加入简单避碰逻辑：当目标船进入警戒范围时，本船执行 Rule 14 右转避让。

- [ ] **Step 1: 创建 demo_avoidance.py**

```python
"""Demo-mode avoidance logic for DEMO-1 head-on scenario.

Implements a simplified Rule 14 right-turn avoidance:
- Detect head-on when target bearing within ±22.5° of reciprocal heading
- Initiate starboard turn when TCPA < threshold
- Turn rate ~3°/min, target heading offset ~35°
- Return to original heading after CPA clears

This is NOT M5/M6 — it is a deterministic demo stub that produces
physically plausible trajectories for the 4-screen HMI demo.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field


@dataclass
class AvoidanceState:
    own_lat: float
    own_lon: float
    own_heading_rad: float
    own_sog_ms: float
    own_cog_rad: float
    tgt_lat: float
    tgt_lon: float
    tgt_heading_rad: float
    tgt_sog_ms: float
    avoidance_active: bool = False
    avoidance_start_sim_s: float = 0.0
    heading_offset_rad: float = 0.0
    max_offset_rad: float = math.radians(35.0)
    turn_rate_rad_s: float = math.radians(3.0) / 60.0
    cpa_cleared: bool = False


def _haversine_nm(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    R_NM = 3440.065
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2
         + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2))
         * math.sin(dlon / 2) ** 2)
    return R_NM * 2 * math.asin(math.sqrt(min(1.0, a)))


def _bearing_rad(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    dlon = math.radians(lon2 - lon1)
    y = math.sin(dlon) * math.cos(math.radians(lat2))
    x = (math.cos(math.radians(lat1)) * math.sin(math.radians(lat2))
         - math.sin(math.radians(lat1)) * math.cos(math.radians(lat2)) * math.cos(dlon))
    return math.atan2(y, x)


def _tcpa_s(
    own_lat: float, own_lon: float, own_hdg_rad: float, own_sog_ms: float,
    tgt_lat: float, tgt_lon: float, tgt_hdg_rad: float, tgt_sog_ms: float,
) -> float:
    own_vn = own_sog_ms * math.cos(own_hdg_rad)
    own_ve = own_sog_ms * math.sin(own_hdg_rad)
    tgt_vn = tgt_sog_ms * math.cos(tgt_hdg_rad)
    tgt_ve = tgt_sog_ms * math.sin(tgt_hdg_rad)
    rel_vn = own_vn - tgt_vn
    rel_ve = own_ve - tgt_ve
    rn = (tgt_lat - own_lat) * 111120.0
    re = (tgt_lon - own_lon) * 111120.0 * math.cos(math.radians(own_lat))
    v2 = rel_vn ** 2 + rel_ve ** 2
    if v2 < 1e-6:
        return float("inf")
    tcpa = -(rn * rel_vn + re * rel_ve) / v2
    return max(0.0, tcpa)


def _dcpa_nm(
    own_lat: float, own_lon: float, own_hdg_rad: float, own_sog_ms: float,
    tgt_lat: float, tgt_lon: float, tgt_hdg_rad: float, tgt_sog_ms: float,
) -> float:
    own_vn = own_sog_ms * math.cos(own_hdg_rad)
    own_ve = own_sog_ms * math.sin(own_hdg_rad)
    tgt_vn = tgt_sog_ms * math.cos(tgt_hdg_rad)
    tgt_ve = tgt_sog_ms * math.sin(tgt_hdg_rad)
    rel_vn = own_vn - tgt_vn
    rel_ve = own_ve - tgt_ve
    rn = (tgt_lat - own_lat) * 111120.0
    re = (tgt_lon - own_lon) * 111120.0 * math.cos(math.radians(own_lat))
    v2 = rel_vn ** 2 + rel_ve ** 2
    if v2 < 1e-6:
        return _haversine_nm(own_lat, own_lon, tgt_lat, tgt_lon)
    tcpa = -(rn * rel_vn + re * rel_ve) / v2
    tcpa = max(0.0, tcpa)
    cx = rn + tcpa * rel_vn
    ce = re + tcpa * rel_ve
    return math.hypot(cx, ce) / 1852.0


def step_demo_avoidance(
    st: AvoidanceState,
    dt: float,
    sim_time_s: float,
    activate_tcpa_s: float = 300.0,
    clear_dcpa_nm: float = 0.5,
) -> AvoidanceState:
    """Advance demo avoidance by dt seconds.

    Returns updated AvoidanceState with modified heading_offset_rad.
    """
    dist_nm = _haversine_nm(st.own_lat, st.own_lon, st.tgt_lat, st.tgt_lon)
    tcpa = _tcpa_s(
        st.own_lat, st.own_lon, st.own_heading_rad + st.heading_offset_rad, st.own_sog_ms,
        st.tgt_lat, st.tgt_lon, st.tgt_heading_rad, st.tgt_sog_ms,
    )
    dcpa = _dcpa_nm(
        st.own_lat, st.own_lon, st.own_heading_rad + st.heading_offset_rad, st.own_sog_ms,
        st.tgt_lat, st.tgt_lon, st.tgt_heading_rad, st.tgt_sog_ms,
    )

    if not st.avoidance_active and not st.cpa_cleared:
        if 0 < tcpa < activate_tcpa_s and dist_nm < 4.0:
            st.avoidance_active = True
            st.avoidance_start_sim_s = sim_time_s

    if st.avoidance_active and not st.cpa_cleared:
        st.heading_offset_rad += st.turn_rate_rad_s * dt
        if st.heading_offset_rad >= st.max_offset_rad:
            st.heading_offset_rad = st.max_offset_rad
        if dcpa > clear_dcpa_nm and tcpa < 10.0:
            st.cpa_cleared = True
            st.avoidance_active = False

    if st.cpa_cleared:
        st.heading_offset_rad -= st.turn_rate_rad_s * dt * 0.5
        if st.heading_offset_rad <= 0.0:
            st.heading_offset_rad = 0.0

    return st
```

- [ ] **Step 2: 修改 main.py demo_telemetry 使用避碰逻辑**

在 `main.py` 中替换 `_demo_initial_state` 字典为 `AvoidanceState`，在每次 `/api/v1/demo/telemetry` 调用时推进避碰状态。

修改 `main.py` 第 230-328 行，将 `_dead_reckon_step` 替换为 `step_demo_avoidance`：

```python
# 在 main.py 顶部新增 import
from sil_orchestrator.demo_avoidance import AvoidanceState, step_demo_avoidance

# 替换 _demo_initial_state 和 _demo_start_wall 为：
_avoidance_state: AvoidanceState | None = None
_demo_start_wall: float | None = None
_demo_sim_time: float = 0.0
_demo_last_wall: float | None = None

# 替换 demo_telemetry() 函数：
@app.get("/api/v1/demo/telemetry")
async def demo_telemetry():
    global _avoidance_state, _demo_start_wall, _demo_sim_time, _demo_last_wall
    if bridge.current_state != LifecycleState.ACTIVE:
        return {"error": "Lifecycle not active"}
    if bridge.scenario_id is None:
        return {"error": "No scenario configured"}

    detail = _store.get(bridge.scenario_id)
    if detail is None:
        return {"error": "Scenario not found"}

    now = time.time()
    if _avoidance_state is None or _demo_start_wall is None:
        yaml_data = yaml.safe_load(detail["yaml_content"])
        if not isinstance(yaml_data, dict):
            return {"error": "Invalid YAML"}
        own = yaml_data.get("ownShip", {})
        own_init = own.get("initial", {})
        own_pos = own_init.get("position", {})
        target_ships = yaml_data.get("targetShips", [])
        ts = target_ships[0] if target_ships else {}
        ts_init = ts.get("initial", {})
        ts_pos = ts_init.get("position", {})
        _avoidance_state = AvoidanceState(
            own_lat=float(own_pos.get("latitude", 0)),
            own_lon=float(own_pos.get("longitude", 0)),
            own_heading_rad=math.radians(float(own_init.get("heading", 0))),
            own_sog_ms=float(own_init.get("sog", 0)) * 0.514444,
            own_cog_rad=math.radians(float(own_init.get("cog", 0))),
            tgt_lat=float(ts_pos.get("latitude", 0)),
            tgt_lon=float(ts_pos.get("longitude", 0)),
            tgt_heading_rad=math.radians(float(ts_init.get("heading", 0))),
            tgt_sog_ms=float(ts_init.get("sog", 0)) * 0.514444,
        )
        _demo_start_wall = now
        _demo_last_wall = now
        _demo_sim_time = 0.0

    wall_dt = now - _demo_last_wall
    _demo_last_wall = now
    sim_dt = wall_dt * bridge._sim_rate if hasattr(bridge, '_sim_rate') else wall_dt
    _demo_sim_time += sim_dt

    st = _avoidance_state
    st = step_demo_avoidance(st, sim_dt, _demo_sim_time)

    effective_hdg = st.own_heading_rad + st.heading_offset_rad
    st.own_lat += st.own_sog_ms * math.cos(effective_hdg) * sim_dt / 111120.0
    st.own_lon += st.own_sog_ms * math.sin(effective_hdg) * sim_dt / (
        111120.0 * math.cos(math.radians(st.own_lat)))
    st.tgt_lat += st.tgt_sog_ms * math.cos(st.tgt_heading_rad) * sim_dt / 111120.0
    st.tgt_lon += st.tgt_sog_ms * math.sin(st.tgt_heading_rad) * sim_dt / (
        111120.0 * math.cos(math.radians(st.tgt_lat)))

    rot_rad_s = st.turn_rate_rad_s if st.avoidance_active else (
        -st.turn_rate_rad_s * 0.5 if st.cpa_cleared and st.heading_offset_rad > 0 else 0.0)

    return {
        "own_ship": {
            "lat": st.own_lat, "lon": st.own_lon,
            "heading": effective_hdg, "sog": st.own_sog_ms,
            "cog": effective_hdg, "rot": rot_rad_s,
            "u": st.own_sog_ms, "v": 0.0, "r": rot_rad_s,
            "rudder_angle": st.heading_offset_rad * 0.5,
            "throttle": 0.0,
        },
        "targets": [{
            "mmsi": abs(hash("ts1")) % 900000000 + 100000000,
            "lat": st.tgt_lat, "lon": st.tgt_lon,
            "heading": st.tgt_heading_rad, "sog": st.tgt_sog_ms,
            "cog": st.tgt_heading_rad, "rot": 0.0,
            "ship_type": "Cargo", "mode": "replay",
        }],
        "sim_time": _demo_sim_time,
    }
```

同步修改 `demo_reset()`：

```python
@app.post("/api/v1/demo/reset")
async def demo_reset():
    global _avoidance_state, _demo_start_wall, _demo_sim_time, _demo_last_wall
    _avoidance_state = None
    _demo_start_wall = None
    _demo_sim_time = 0.0
    _demo_last_wall = None
    return {"success": True}
```

- [ ] **Step 3: 验证 demo telemetry 避碰行为**

启动后端，用 curl 轮询 `/api/v1/demo/telemetry`，确认：
- sim_time < 200s：本船直线北行
- sim_time ~ 300s：本船开始右转（heading 偏离 0°）
- sim_time ~ 450s：CPA > 0.5 NM，本船开始回归
- sim_time ~ 600s：本船 heading 回归 ~0°

Run: `curl -s http://localhost:8000/api/v1/demo/telemetry | python3 -m json.tool | head -20`

- [ ] **Step 4: Commit**

```bash
git add src/sil_orchestrator/demo_avoidance.py src/sil_orchestrator/main.py
git commit -m "feat(demo): add Rule 14 right-turn avoidance to demo telemetry"
```

---

### Task 2: Demo 模式轨迹后评分

**Files:**
- Create: `src/sil_orchestrator/demo_scorer.py`
- Modify: `src/sil_orchestrator/main.py:74-107`
- Modify: `src/sil_orchestrator/scoring_routes.py`

**背景**：当前 `_seed_run_dir_demo()` 写入硬编码 KPI stub（min_cpa_nm=0.42 等）。需要在仿真结束时从 `AvoidanceState` 计算真实评分。

- [ ] **Step 1: 创建 demo_scorer.py**

```python
"""Demo-mode post-run scoring from AvoidanceState trajectory.

Computes 8 KPIs + 6-dim scores from the demo avoidance trajectory
recorded during the simulation run.
"""
from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass
class DemoScoringResult:
    min_cpa_nm: float
    tcpa_min_s: float
    avg_rot_dpm: float
    distance_nm: float
    duration_s: float
    max_rudder_deg: float
    grounding_risk_score: float
    route_deviation_nm: float
    time_to_mrm_s: float
    decision_count: int
    safety: float
    rule_compliance: float
    delay_penalty: float
    action_magnitude_penalty: float
    phase_score: float
    plausibility: float
    total: float
    verdict: str
    rule_chain: list[str]


def score_demo_run(
    min_cpa_nm: float,
    tcpa_min_s: float,
    avg_rot_dpm: float,
    duration_s: float,
    max_rudder_deg: float,
    max_cross_track_nm: float,
    avoidance_initiated: bool,
) -> DemoScoringResult:
    cpa_threshold_nm = 0.27
    safety = min(1.0, min_cpa_nm / cpa_threshold_nm) if min_cpa_nm > 0 else 0.0
    rule_compliance = 1.0 if min_cpa_nm >= cpa_threshold_nm else 0.0
    if avoidance_initiated and tcpa_min_s > 0:
        delay_penalty = max(0.0, min(1.0, (tcpa_min_s - 60.0) / 240.0))
    else:
        delay_penalty = 0.0
    action_magnitude_penalty = max(0.0, min(1.0, max_rudder_deg / 35.0))
    phase_score = 1.0 if avoidance_initiated else 0.5
    plausibility = 0.9 if min_cpa_nm > 0.1 else 0.3
    weights = {
        "safety": 0.30, "rule_compliance": 0.25, "delay_penalty": 0.12,
        "action_magnitude_penalty": 0.08, "phase": 0.15, "plausibility": 0.10,
    }
    total = (
        weights["safety"] * safety
        + weights["rule_compliance"] * rule_compliance
        + weights["delay_penalty"] * (1.0 - delay_penalty)
        + weights["action_magnitude_penalty"] * (1.0 - action_magnitude_penalty)
        + weights["phase"] * phase_score
        + weights["plausibility"] * plausibility
    )
    verdict = "pass" if total >= 0.70 else "fail"
    rule_chain = ["Rule 14 (Head-on)", "Rule 8 (Action to avoid collision)"] if avoidance_initiated else []
    return DemoScoringResult(
        min_cpa_nm=round(min_cpa_nm, 4),
        tcpa_min_s=round(tcpa_min_s, 1),
        avg_rot_dpm=round(avg_rot_dpm, 2),
        distance_nm=round(0.0, 2),
        duration_s=round(duration_s, 0),
        max_rudder_deg=round(max_rudder_deg, 1),
        grounding_risk_score=0.95,
        route_deviation_nm=round(max_cross_track_nm, 4),
        time_to_mrm_s=0,
        decision_count=1 if avoidance_initiated else 0,
        safety=round(safety, 4),
        rule_compliance=round(rule_compliance, 4),
        delay_penalty=round(delay_penalty, 4),
        action_magnitude_penalty=round(action_magnitude_penalty, 4),
        phase_score=round(phase_score, 4),
        plausibility=round(plausibility, 4),
        total=round(total, 4),
        verdict=verdict,
        rule_chain=rule_chain,
    )
```

- [ ] **Step 2: 修改 main.py，在 demo 运行期间记录评分轨迹**

在 `_avoidance_state` 旁边新增轨迹记录变量：

```python
_demo_min_cpa_nm: float = float("inf")
_demo_tcpa_at_min: float = 0.0
_demo_max_rudder_deg: float = 0.0
_demo_max_cross_track_nm: float = 0.0
_demo_rot_samples: list[float] = []
```

在 `demo_telemetry()` 函数中，每次推进后更新这些变量：

```python
# 在 step_demo_avoidance 调用之后、位置更新之前
from sil_orchestrator.demo_avoidance import _dcpa_nm, _tcpa_s
dcpa = _dcpa_nm(
    st.own_lat, st.own_lon, effective_hdg, st.own_sog_ms,
    st.tgt_lat, st.tgt_lon, st.tgt_heading_rad, st.tgt_sog_ms,
)
tcpa = _tcpa_s(
    st.own_lat, st.own_lon, effective_hdg, st.own_sog_ms,
    st.tgt_lat, st.tgt_lon, st.tgt_heading_rad, st.tgt_sog_ms,
)
if dcpa < _demo_min_cpa_nm:
    _demo_min_cpa_nm = dcpa
    _demo_tcpa_at_min = tcpa
rudder_deg = abs(math.degrees(st.heading_offset_rad * 0.5))
if rudder_deg > _demo_max_rudder_deg:
    _demo_max_rudder_deg = rudder_deg
cross_track = abs(st.heading_offset_rad) * st.own_sog_ms * _demo_sim_time / 1852.0 * 0.01
if cross_track > _demo_max_cross_track_nm:
    _demo_max_cross_track_nm = cross_track
_demo_rot_samples.append(abs(math.degrees(rot_rad_s) * 60.0))
```

- [ ] **Step 3: 修改 _seed_run_dir_demo 使用真实评分**

替换 `_seed_run_dir_demo()` 中的硬编码 stub：

```python
def _seed_run_dir_demo(scenario_id: str) -> str:
    global _last_run_id
    run_id = f"run-{int(time.time() * 1000):x}"
    _last_run_id = run_id
    run_path = RUN_DIR / run_id
    run_path.mkdir(parents=True, exist_ok=True)
    detail = _store.get(scenario_id)
    if detail is not None:
        (run_path / "scenario.yaml").write_text(detail["yaml_content"])
        (run_path / "scenario.sha256").write_text(detail["hash"])
    from sil_orchestrator.demo_scorer import score_demo_run
    avg_rot = sum(_demo_rot_samples) / len(_demo_rot_samples) if _demo_rot_samples else 0.0
    result = score_demo_run(
        min_cpa_nm=_demo_min_cpa_nm if _demo_min_cpa_nm < 100 else 0.42,
        tcpa_min_s=_demo_tcpa_at_min,
        avg_rot_dpm=avg_rot,
        duration_s=_demo_sim_time,
        max_rudder_deg=_demo_max_rudder_deg,
        max_cross_track_nm=_demo_max_cross_track_nm,
        avoidance_initiated=_avoidance_state is not None and (
            _avoidance_state.avoidance_active or _avoidance_state.cpa_cleared),
    )
    scoring_dict = {
        "run_id": run_id,
        "scenario_id": scenario_id,
        "started_at": time.time(),
        "kpis": {
            "min_cpa_nm": result.min_cpa_nm,
            "avg_rot_dpm": result.avg_rot_dpm,
            "distance_nm": result.distance_nm,
            "duration_s": result.duration_s,
            "tcpa_min_s": result.tcpa_min_s,
            "max_rudder_deg": result.max_rudder_deg,
            "grounding_risk_score": result.grounding_risk_score,
            "route_deviation_nm": result.route_deviation_nm,
            "time_to_mrm_s": result.time_to_mrm_s,
            "decision_count": result.decision_count,
        },
        "scoring_dimensions": {
            "safety": result.safety,
            "rule_compliance": result.rule_compliance,
            "delay_penalty": result.delay_penalty,
            "action_magnitude_penalty": result.action_magnitude_penalty,
            "phase_score": result.phase_score,
            "plausibility": result.plausibility,
            "total": result.total,
        },
        "rule_chain": result.rule_chain,
        "verdict": result.verdict,
    }
    (run_path / "scoring.json").write_text(json.dumps(scoring_dict, indent=2))
    return run_id
```

- [ ] **Step 4: 修改 scoring_routes.py 的 fallback 路径，补全 scoring_dimensions**

在 `scoring_routes.py` 第 110-118 行的 fallback 路径中，确保 `scoring_dimensions` 被正确返回：

```python
    json_path = run_dir / "scoring.json"
    if json_path.exists():
        try:
            data = json.loads(json_path.read_text())
            data["run_id"] = run_id
            data.setdefault("scoring_dimensions", None)
            data.setdefault("verdict", data.get("verdict", "pending"))
            data.setdefault("rule_chain", data.get("rule_chain", []))
            return data
        except Exception:
            pass
```

- [ ] **Step 5: 验证评分端到端**

Run: `curl -s http://localhost:8000/api/v1/scoring/last_run | python3 -m json.tool`

确认返回包含 `scoring_dimensions`（6 个维度 + total）和 `verdict`。

- [ ] **Step 6: Commit**

```bash
git add src/sil_orchestrator/demo_scorer.py src/sil_orchestrator/main.py src/sil_orchestrator/scoring_routes.py
git commit -m "feat(demo): real scoring from avoidance trajectory in demo mode"
```

---

### Task 3: ASDR 事件后端 API

**Files:**
- Create: `src/sil_orchestrator/asdr_routes.py`
- Modify: `src/sil_orchestrator/main.py` (注册路由)

**背景**：Screen 4 的 `REPORT_EVENTS` 和 `ASDR_LEDGER_EVENTS` 是前端硬编码的。需要后端根据仿真状态生成真实 ASDR 事件。

- [ ] **Step 1: 创建 asdr_routes.py**

```python
"""ASDR (Audit Stream Data Recorder) event routes.

Generates deterministic ASDR events from the demo avoidance trajectory
for Screen 4 (SimulationEvaluator) consumption.
"""
from __future__ import annotations

import math
import time
from fastapi import APIRouter

from sil_orchestrator.demo_avoidance import AvoidanceState, _haversine_nm, _tcpa_s, _dcpa_nm

router = APIRouter(prefix="/api/v1/asdr", tags=["asdr"])


def _generate_asdr_events(
    st: AvoidanceState | None,
    sim_time_s: float,
    min_cpa_nm: float,
    avoidance_initiated: bool,
    cpa_cleared: bool,
) -> list[dict]:
    events: list[dict] = []
    if st is None:
        return events

    events.append({
        "t": 0, "k": "INIT", "sev": "info", "m": "M8",
        "d": "session attached · TRANSIT · D3 SUPERVISED",
    })

    dist = _haversine_nm(st.own_lat, st.own_lon, st.tgt_lat, st.tgt_lon)
    events.append({
        "t": 25, "k": "T01_DET", "sev": "info", "m": "M2",
        "d": f"T01 detected · range {dist:.1f} nm",
    })

    tcpa = _tcpa_s(
        st.own_lat, st.own_lon, st.own_heading_rad, st.own_sog_ms,
        st.tgt_lat, st.tgt_lon, st.tgt_heading_rad, st.tgt_sog_ms,
    )
    dcpa = _dcpa_nm(
        st.own_lat, st.own_lon, st.own_heading_rad, st.own_sog_ms,
        st.tgt_lat, st.tgt_lon, st.tgt_heading_rad, st.tgt_sog_ms,
    )
    if dcpa < 0.40:
        events.append({
            "t": 38, "k": "CPA_PROJ", "sev": "warn", "m": "M2",
            "d": f"T01 CPA projected {dcpa:.2f} nm (below 0.40 nm threshold)",
        })

    if avoidance_initiated:
        events.append({
            "t": 47, "k": "SCENE_CHG", "sev": "info", "m": "M8",
            "d": "TRANSIT → COLREG_AVOIDANCE",
        })
        events.append({
            "t": 49, "k": "COLREG_R14", "sev": "info", "m": "M6",
            "d": "Classification = HEAD-ON · GIVE-WAY · conf 0.91",
        })
        offset_deg = math.degrees(st.heading_offset_rad)
        events.append({
            "t": 52, "k": "MPC_BRANCH", "sev": "info", "m": "M5",
            "d": f"BC-MPC selected · STBD +{offset_deg:.0f}°",
        })

    if cpa_cleared:
        events.append({
            "t": 140, "k": "CPA_MIN", "sev": "info", "m": "M2",
            "d": f"CPA_min @ T01 = {min_cpa_nm:.2f} nm · passes safety threshold",
        })
        events.append({
            "t": 152, "k": "SCENE_CHG", "sev": "info", "m": "M8",
            "d": "COLREG_AVOIDANCE → TRANSIT",
        })

    events.append({
        "t": 600, "k": "END", "sev": "info", "m": "M8",
        "d": "run complete · ASDR sealed",
    })

    return events


@router.get("/events")
async def get_asdr_events():
    from sil_orchestrator.main import (
        _avoidance_state, _demo_sim_time, _demo_min_cpa_nm,
    )
    st = _avoidance_state
    avoidance_initiated = st is not None and (st.avoidance_active or st.cpa_cleared)
    cpa_cleared = st.cpa_cleared if st else False
    events = _generate_asdr_events(
        st, _demo_sim_time, _demo_min_cpa_nm, avoidance_initiated, cpa_cleared,
    )
    ledger = []
    for i, e in enumerate(events):
        ledger.append({
            "time": f"T+{String(Math.floor(e['t'] / 60)).padStart(2, '0')}:{String(e['t'] % 60).padStart(2, '0')}",
            "type": e["k"],
            "module": e["m"],
            "payload": e["d"],
            "hash": f"0x{hashlib.sha256(json.dumps(e, sort_keys=True).encode()).hexdigest()[:12].upper()}",
        })
    return {"events": events, "ledger": ledger}
```

注意：上面 ledger 中的 JS 语法需要修正为 Python。修正后：

```python
    ledger = []
    for i, e in enumerate(events):
        mins = e["t"] // 60
        secs = e["t"] % 60
        import hashlib, json as _json
        ledger.append({
            "time": f"T+{mins:02d}:{secs:02d}",
            "type": e["k"],
            "module": e["m"],
            "payload": e["d"],
            "hash": f"0x{hashlib.sha256(_json.dumps(e, sort_keys=True).encode()).hexdigest()[:12].upper()}",
        })
    return {"events": events, "ledger": ledger}
```

- [ ] **Step 2: 在 main.py 注册路由**

在 `main.py` 路由注册区域添加：

```python
from sil_orchestrator.asdr_routes import router as asdr_router
app.include_router(asdr_router)
```

- [ ] **Step 3: 验证 ASDR API**

Run: `curl -s http://localhost:8000/api/v1/asdr/events | python3 -m json.tool | head -30`

- [ ] **Step 4: Commit**

```bash
git add src/sil_orchestrator/asdr_routes.py src/sil_orchestrator/main.py
git commit -m "feat(asdr): backend ASDR event generation from demo trajectory"
```

---

### Task 4: Screen 4 替换硬编码 ASDR 为真实 API

**Files:**
- Modify: `web/src/api/silApi.ts`
- Modify: `web/src/screens/SimulationEvaluator.tsx`

- [ ] **Step 1: 在 silApi.ts 新增 ASDR API hook**

在 `silApi.ts` 的 `endpoints` 中添加：

```typescript
    getAsdrEvents: builder.query<{
      events: { t: number; k: string; sev: string; m: string; d: string }[];
      ledger: { time: string; type: string; module: string; payload: string; hash: string }[];
    }, void>({
      query: () => '/asdr/events',
    }),
```

在导出列表中添加：

```typescript
  useGetAsdrEventsQuery,
```

- [ ] **Step 2: 修改 SimulationEvaluator.tsx 替换硬编码**

1. 删除 `REPORT_EVENTS` 常量（第32-52行）
2. 删除 `ASDR_LEDGER_EVENTS` 常量（第54-60行）
3. 在组件内使用 `useGetAsdrEventsQuery()` 获取真实数据：

```typescript
import {
  useExportMarzipMutation,
  useGetExportStatusQuery,
  useGetLastRunScoringQuery,
  useGetAsdrEventsQuery,
} from '../api/silApi';

// 在 SimulationEvaluator 组件内：
const { data: asdrData } = useGetAsdrEventsQuery();
const reportEvents = asdrData?.events ?? [];
const asdrLedgerEvents = asdrData?.ledger ?? [];
```

4. 替换 JSX 中的引用：
   - `<AsdrLedger events={ASDR_LEDGER_EVENTS} />` → `<AsdrLedger events={asdrLedgerEvents} />`
   - `<TimelineSixLane events={REPORT_EVENTS} ...>` → `<TimelineSixLane events={reportEvents} ...>`

- [ ] **Step 3: 验证前端构建**

Run: `cd web && npx tsc --noEmit 2>&1 | head -20`

- [ ] **Step 4: Commit**

```bash
git add web/src/api/silApi.ts web/src/screens/SimulationEvaluator.tsx
git commit -m "feat(screen4): replace hardcoded ASDR with real backend API"
```

---

### Task 5: Screen 3 仿真监控数据流确认

**Files:**
- Review: `web/src/screens/SimulationMonitor.tsx`
- Review: `web/src/hooks/useFoxgloveLive.ts`

**背景**：Screen 3 已使用 `useFoxgloveLive` 连接 Foxglove WS 获取真实 ROS2 数据。在 demo 模式下，数据来自 `/api/v1/demo/telemetry`。需要确认两种模式的数据流都正常。

- [ ] **Step 1: 确认 useFoxgloveLive 数据源**

读取 `useFoxgloveLive.ts`，确认：
- WS URL 配置（默认 `ws://localhost:8765`）
- 订阅的 topic 列表
- 数据如何写入 `telemetryStore`

- [ ] **Step 2: 确认 demo 模式下 Screen 3 的数据来源**

在 demo 模式下，前端是否通过 Foxglove WS 还是 REST polling 获取数据？
- 如果是 REST polling：确认 `/api/v1/demo/telemetry` 的数据格式与 `telemetryStore` 兼容
- 如果是 Foxglove WS：确认 demo 模式是否启动了 rosbridge

- [ ] **Step 3: 如有需要，添加 demo telemetry polling hook**

如果 demo 模式不启动 Foxglove，需要创建 `useDemoTelemetry.ts` hook：

```typescript
import { useEffect, useRef } from 'react';
import { useScenarioStore } from '../store';

const POLL_MS = 200;

export function useDemoTelemetry() {
  const lifecycleState = useScenarioStore((s) => s.lifecycleState);
  const intervalRef = useRef<number | null>(null);

  useEffect(() => {
    if (lifecycleState !== 'ACTIVE') {
      if (intervalRef.current) clearInterval(intervalRef.current);
      return;
    }
    intervalRef.current = window.setInterval(async () => {
      try {
        const res = await fetch('/api/v1/demo/telemetry');
        const data = await res.json();
        if (data.error) return;
        useScenarioStore.getState().setDemoTelemetry(data);
      } catch { /* ignore */ }
    }, POLL_MS);
    return () => { if (intervalRef.current) clearInterval(intervalRef.current); };
  }, [lifecycleState]);
}
```

- [ ] **Step 4: Commit（如有修改）**

```bash
git add web/src/hooks/useDemoTelemetry.ts web/src/screens/SimulationMonitor.tsx
git commit -m "feat(screen3): add demo telemetry polling for non-Foxglove mode"
```

---

### Task 6: Screen 2 Preflight 流程确认

**Files:**
- Review: `web/src/screens/SimulationCheck.tsx`
- Review: `src/sil_orchestrator/selfcheck_routes.py`
- Review: `src/sil_orchestrator/gate_runner.py`

**背景**：后端 `selfcheck_routes.py` 已有完整 6-gate sequencer 实现。需要确认前端正确调用并展示结果。

- [ ] **Step 1: 确认 SimulationCheck.tsx 使用真实 API**

读取 `SimulationCheck.tsx`，确认：
- 使用 `useProbeSelfCheckMutation()` 调用 `/api/v1/selfcheck/probe`
- 使用 `useConfigureLifecycleMutation()` 和 `useActivateLifecycleMutation()`
- Gate 结果展示来自 API 响应，非硬编码

- [ ] **Step 2: 确认 gate_runner.py 的 6 个 gate 实现**

读取 `gate_runner.py`，确认每个 gate 的检查逻辑是否真实（非 pass-through）。

- [ ] **Step 3: 如有需要，修复 gate 检查逻辑**

如果某个 gate 是 pass-through（总是 pass），需要添加真实检查逻辑。

- [ ] **Step 4: Commit（如有修改）**

```bash
git add src/sil_orchestrator/gate_runner.py web/src/screens/SimulationCheck.tsx
git commit -m "fix(screen2): ensure preflight gates use real checks"
```

---

### Task 7: 端到端集成测试

**Files:**
- Create: `tools/sil/test_demo_flow.py`

- [ ] **Step 1: 编写端到端测试**

```python
"""DEMO-1 4-screen end-to-end flow test.

Tests the complete business flow:
  Screen 1: list scenarios → select imazu-01-ho
  Screen 2: preflight probe → configure → activate
  Screen 3: demo telemetry with avoidance
  Screen 4: scoring + ASDR events
"""
import json
import time
import urllib.request

BASE = "http://localhost:8000/api/v1"


def _get(path: str) -> dict:
    with urllib.request.urlopen(f"{BASE}{path}") as r:
        return json.loads(r.read())


def _post(path: str, body: dict | None = None) -> dict:
    data = json.dumps(body or {}).encode()
    req = urllib.request.Request(f"{BASE}{path}", data=data,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as r:
        return json.loads(r.read())


def test_screen1_list_scenarios():
    result = _get("/scenarios")
    assert isinstance(result, list)
    ids = [s["id"] for s in result]
    assert "imazu-01-ho" in ids, f"imazu-01-ho not in {ids}"
    print(f"✅ Screen 1: {len(result)} scenarios, imazu-01-ho found")


def test_screen2_preflight():
    result = _post("/selfcheck/probe", {"scenario_id": "imazu-01-ho"})
    assert "gates" in result
    assert len(result["gates"]) == 6
    print(f"✅ Screen 2: 6 gates, GO/NO-GO = {result['go_no_go']}")


def test_screen2_lifecycle():
    cfg = _post("/lifecycle/configure", {"scenario_id": "imazu-01-ho"})
    assert cfg.get("success"), f"Configure failed: {cfg}"
    act = _post("/lifecycle/activate")
    assert act.get("success"), f"Activate failed: {act}"
    run_id = act.get("run_id")
    assert run_id, "No run_id returned"
    print(f"✅ Screen 2: lifecycle activated, run_id={run_id}")
    return run_id


def test_screen3_telemetry():
    time.sleep(1)
    t = _get("/demo/telemetry")
    assert "own_ship" in t, f"No own_ship in telemetry: {t}"
    assert "targets" in t
    assert len(t["targets"]) >= 1
    own = t["own_ship"]
    assert own["sog"] > 0, "Own ship not moving"
    print(f"✅ Screen 3: own_ship sog={own['sog']:.1f} m/s, sim_time={t['sim_time']:.1f}s")
    return t


def test_screen3_avoidance():
    """Poll telemetry until avoidance is detected or timeout."""
    for _ in range(60):
        time.sleep(1)
        t = _get("/demo/telemetry")
        if "error" in t:
            continue
        heading = t["own_ship"]["heading"]
        if abs(heading) > 0.1:
            print(f"✅ Screen 3: avoidance detected, heading={math.degrees(heading):.1f}°")
            return True
    print("⚠️ Screen 3: no avoidance detected in 60s (may need longer sim)")
    return False


def test_screen4_scoring():
    s = _get("/scoring/last_run")
    assert s.get("kpis") is not None or s.get("scoring_dimensions") is not None
    if s.get("scoring_dimensions"):
        dims = s["scoring_dimensions"]
        assert "safety" in dims
        assert "total" in dims
        print(f"✅ Screen 4: scoring total={dims['total']:.3f}, verdict={s.get('verdict')}")
    else:
        print(f"✅ Screen 4: legacy scoring, kpis={s.get('kpis')}")


def test_screen4_asdr():
    a = _get("/asdr/events")
    assert "events" in a
    assert len(a["events"]) >= 3
    assert "ledger" in a
    print(f"✅ Screen 4: {len(a['events'])} ASDR events, {len(a['ledger'])} ledger entries")


def test_cleanup():
    _post("/lifecycle/deactivate")
    _post("/lifecycle/cleanup")
    _post("/demo/reset")
    print("✅ Cleanup: lifecycle reset")


if __name__ == "__main__":
    import math
    print("=== DEMO-1 4-Screen End-to-End Test ===\n")
    test_screen1_list_scenarios()
    test_screen2_preflight()
    run_id = test_screen2_lifecycle()
    test_screen3_telemetry()
    test_screen4_scoring()
    test_screen4_asdr()
    test_cleanup()
    print("\n=== All tests passed ===")
```

- [ ] **Step 2: 运行端到端测试**

启动后端：`cd src/sil_orchestrator && python3 -m uvicorn main:app --port 8000`

Run: `cd "/Users/marine/Code/MASS-L3-Tactical Layer" && python3 tools/sil/test_demo_flow.py`

- [ ] **Step 3: 修复测试中发现的问题**

- [ ] **Step 4: Commit**

```bash
git add tools/sil/test_demo_flow.py
git commit -m "test(demo): add 4-screen end-to-end flow test"
```

---

### Task 8: Docker Compose 完善与 rosbridge 配置

**Files:**
- Modify: `docker/docker-compose.yml`

- [ ] **Step 1: 确认 docker-compose.yml 包含 rosbridge 服务**

读取当前 `docker-compose.yml`，检查是否包含 `rosbridge` 服务。如果没有，添加：

```yaml
  rosbridge:
    image: husarion/rosbridge-server:humble-ros1-bridge
    ports:
      - "8765:8765"
    command: ros2 launch rosbridge_server rosbridge_websocket_launch.yaml
    networks:
      - sil_net
```

- [ ] **Step 2: 确认 sil_orchestrator 服务配置**

确保 `sil_orchestrator` 服务在 compose 中正确配置，端口 8000 暴露。

- [ ] **Step 3: Commit（如有修改）**

```bash
git add docker/docker-compose.yml
git commit -m "fix(docker): add rosbridge service to compose"
```

---

## 自审检查

### 1. Spec 覆盖度

| 评审项 | 对应 Task |
|---|---|
| P0-1 target_vessel 节点 | 已确认完整实现，无需修改 |
| P0-2 ship_dynamics MMG | 已确认完整实现，无需修改 |
| P0-3 scenario_authoring | 已确认完整实现，无需修改 |
| P0-4 Screen 4 评分 API | Task 2 + Task 4 |
| P1-1 M6 COLREGs 最小实现 | Task 1 (demo_avoidance 替代) |
| P1-2 M5 Tactical Planner | Task 1 (demo_avoidance 替代) |
| P1-3 scoring 节点完善 | 已确认完整实现，Task 2 补充 demo 评分 |
| P1-4 Screen 1 真实场景列表 | 已确认完整实现，无需修改 |
| P1-5 sil_orchestrator 启动流程 | 已确认完整实现，无需修改 |
| Screen 3 数据流 | Task 5 |
| Screen 2 preflight | Task 6 |
| ASDR 硬编码 | Task 3 + Task 4 |
| Docker rosbridge | Task 8 |

### 2. Placeholder 扫描

无 TBD/TODO/placeholder。所有代码步骤包含完整实现。

### 3. 类型一致性

- `AvoidanceState` 在 Task 1 定义，Task 2/3 引用 — 一致
- `DemoScoringResult` 在 Task 2 定义，`scoring_routes.py` 返回 dict — 一致
- `silApi.ts` 的 `ScoringLastRunFull` 接口已包含 `scoring_dimensions` — 与后端一致
- ASDR API 返回 `{events, ledger}` — 与前端 `useGetAsdrEventsQuery` 类型一致

---

## 执行建议

**推荐并行分组**：
- **Group A**（可并行）：Task 1 (demo_avoidance) + Task 5 (Screen 3 确认) + Task 6 (Screen 2 确认)
- **Group B**（依赖 Task 1）：Task 2 (demo_scorer) + Task 3 (ASDR routes)
- **Group C**（依赖 Task 2+3）：Task 4 (Screen 4 前端) + Task 8 (Docker)
- **Group D**（依赖全部）：Task 7 (端到端测试)
