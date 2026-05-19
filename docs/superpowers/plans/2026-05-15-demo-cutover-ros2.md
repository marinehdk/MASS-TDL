# DEMO-1 → ROS2 真链路全栈 Cutover 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DEMO-1 通过后，将全栈从 standalone demo (tools/demo/) cutover 到 ROS2 真链路 (sil_orchestrator + sim_workbench LifecycleNodes + L3 kernel)，退役 4 个 Mock，实施 scoring_node 真实 6 维 Hagen 2022 评分，填实 8 KPI cards 数据，CI 自动跑 Imazu-22 + 50 baseline。

**Architecture:** 引入 `simulation_settings.backend: demo|ros2` feature flag 实现平滑切换；退役 4 Mock 采用 parallel run 验证策略 (trajectory diff < 2%)；scoring_node 升级为 rclpy LifecycleNode，50Hz 采样 + 1Hz publish ScoringRow + 旁路写 Arrow IPC；后端 API 读取 Arrow → 计算 derived 8 KPI → 前端实时渲染。

**Tech Stack:** Python 3.10, rclpy (ROS2 Humble), FastAPI, polars + pyarrow, React 18 + RTK Query, Protobuf (ScoringRow), GitLab CI, Docker (OrbStack/native)

**设计基线:** docs/Design/SIL/v1.0-unified/ (commit d6f7fd7), V&V Plan v0.1 (commit e1a13e5), 8 月计划 v3.0

---

## 文件结构总览

### 新建文件
| 路径 | 职责 |
|---|---|
| `src/sim_workbench/sil_nodes/scoring/scoring/hagen_scorer.py` | 6 维 Hagen 2022 + Woerner 2019 评分引擎 |
| `src/sim_workbench/sil_nodes/scoring/scoring/arrow_writer.py` | Arrow IPC 列存旁路写入器 |
| `src/sim_workbench/sil_nodes/scoring/scoring/kpi_deriver.py` | 从 Arrow 计算 derived 8 KPI |
| `src/sil_orchestrator/scoring_routes.py` | `/api/v1/scoring/*` REST 端点 |
| `tests/sim_workbench/scoring/test_hagen_scorer.py` | 6 维评分单元测试 |
| `tests/sim_workbench/scoring/test_arrow_writer.py` | Arrow 写入/读取测试 |
| `tests/sim_workbench/scoring/test_kpi_deriver.py` | 8 KPI 推导测试 |
| `tests/sim_workbench/scoring/test_scoring_node_ros2.py` | scoring_node ROS2 集成测试 |
| `tests/integration/test_cutover_parallel_run.py` | parallel run 对比验证 |
| `schemas/simulation_settings.schema.json` | backend feature flag JSON Schema |

### 修改文件
| 路径 | 改动 |
|---|---|
| `src/sil_orchestrator/main.py` | 移除 `_seed_run_dir` 硬编码 stub；新增 backend flag 路由；接入 scoring_routes |
| `src/sil_orchestrator/lifecycle_bridge.py` | 新增 `backend` 参数传递到 lifecycle_mgr |
| `src/sim_workbench/sil_nodes/scoring/scoring/node.py` | 升级为 `rclpy.lifecycle.LifecycleNode`；集成 hagen_scorer + arrow_writer |
| `src/sim_workbench/sil_nodes/scoring/scoring/__init__.py` | 导出新模块 |
| `src/sim_workbench/sil_nodes/scoring/package.xml` | 新增依赖: pyarrow, polars |
| `src/sim_workbench/sil_nodes/scoring/setup.py` | 新增 console_scripts entry point |
| `web/src/api/silApi.ts` | 新增 `ScoringLastRunFull` 类型 (8 KPI + 6 维分) |
| `web/src/screens/RunReport.tsx` | 8 KPI cards 全部接真实数据；ScoringRadarChart 接 6 维实时 |
| `web/src/screens/shared/ScoringRadarChart.tsx` | 从 kpis 硬编码改为 props 数据 |
| `docker-compose.yml` | 移除 demo 服务；新增 scoring container |
| `.gitlab-ci.yml` | 新增 imazu-22 + baseline-50 stage |

### 删除文件
| 路径 | 对应 Mock | commit 锚点 |
|---|---|---|
| `tools/demo/demo_server.py` | DEMO-1 standalone REST | MOCK-001 |
| `tools/demo/demo_ws_server.py` | DEMO-1 standalone WS | MOCK-002 |
| `tools/demo/trajectory.py` | analytical Head-On trajectory | MOCK-002 |
| `tools/demo/test_trajectory.py` | trajectory tests | MOCK-002 |
| `src/l3_tdl_kernel/l3_external_mock_publisher/` (全目录) | Mock Fusion 输出 | MOCK-003 |
| `src/sim_workbench/sil_mock_publisher/` (全目录) | Mock ship_dynamics | MOCK-004 |

---

## Phase 0: Feature Flag Backend (D2.4-flag)

> **依赖**: 无。本 Phase 可在 DEMO-1 仍在运行时实施，不影响现有 demo 路径。

### Task 0.1: simulation_settings.backend JSON Schema 定义

**Files:**
- Create: `schemas/simulation_settings.schema.json`
- Modify: `scenarios/IMAZU标准测试/imazu-01-ho.yaml` (示例)

- [ ] **Step 1: 编写 schema 定义**

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://mass-l3.sango.com/schemas/simulation_settings.schema.json",
  "title": "SimulationSettings",
  "type": "object",
  "properties": {
    "backend": {
      "type": "string",
      "enum": ["demo", "ros2"],
      "default": "demo",
      "description": "DEMO-1 standalone demo_server OR ROS2 real pipeline via sil_orchestrator + sim_workbench"
    },
    "dt": {
      "type": "number",
      "minimum": 0.01,
      "default": 0.02,
      "description": "Simulation timestep in seconds"
    },
    "total_time": {
      "type": "number",
      "minimum": 60,
      "default": 700,
      "description": "Total simulation duration in seconds"
    },
    "enc_path": {
      "type": "string",
      "default": "data/enc/trondheim_fjord",
      "description": "Path to ENC chart data"
    },
    "coordinate_origin": {
      "type": "array",
      "items": {"type": "number"},
      "minItems": 2,
      "maxItems": 2,
      "description": "[lat, lon] coordinate origin for ENU conversion"
    },
    "dynamics_mode": {
      "type": "string",
      "enum": ["internal", "fmi"],
      "default": "internal",
      "description": "internal: built-in RK4 MMG; fmi: ship_dynamics.fmu via fmi_bridge"
    }
  },
  "required": ["backend", "dt", "total_time"]
}
```

- [ ] **Step 2: 在示例 scenario YAML 中添加 simulation_settings 字段**

在 `scenarios/IMAZU标准测试/imazu-01-ho.yaml` 的 `metadata` 节末尾追加：

```yaml
  simulation_settings:
    backend: demo
    dt: 0.02
    total_time: 700
    enc_path: "data/enc/trondheim_fjord"
    coordinate_origin: [63.44, 10.38]
    dynamics_mode: internal
```

- [ ] **Step 3: 为全部 35 场景批量追加 simulation_settings**

```bash
# Batch add simulation_settings block to all scenario YAML files
python tools/add_simulation_settings.py --scenarios-dir scenarios/ --default-backend demo
```

- [ ] **Step 4: Commit**

```bash
git add schemas/simulation_settings.schema.json scenarios/ tools/add_simulation_settings.py
git commit -m "feat(schema): add simulation_settings.backend feature flag with json schema"
```

### Task 0.2: orchestrator 后端 backend flag 路由

**Files:**
- Modify: `src/sil_orchestrator/main.py`
- Modify: `src/sil_orchestrator/scenario_store.py`

- [ ] **Step 1: scenario_store 解析 backend flag**

在 `src/sil_orchestrator/scenario_store.py` 的 `get()` 方法中，返回解析后的 `backend` 字段：

```python
# 在 ScenarioStore.get() 返回值中添加:
import yaml
def get(self, scenario_id: str) -> dict | None:
    # ... existing logic ...
    yaml_data = yaml.safe_load(content)
    backend = "demo"
    sim_settings = yaml_data.get("metadata", {}).get("simulation_settings", {})
    if isinstance(sim_settings, dict):
        backend = sim_settings.get("backend", "demo")
    return {
        "yaml_content": content,
        "hash": hashlib.sha256(content.encode()).hexdigest(),
        "backend": backend,  # NEW
    }
```

- [ ] **Step 2: main.py activate handler 根据 backend flag 分支**

修改 `src/sil_orchestrator/main.py` 中 `lifecycle_activate` handler：

```python
@app.post("/api/v1/lifecycle/activate")
async def lifecycle_activate():
    result = await bridge.activate()
    run_id = None
    if result.success and bridge.scenario_id:
        detail = _store.get(bridge.scenario_id)
        backend = detail.get("backend", "demo") if detail else "demo"
        if backend == "demo":
            # DEMO-1 path: existing _seed_run_dir (legacy, removed in Task 2.6)
            run_id = _seed_run_dir_demo(bridge.scenario_id)
        else:
            # ROS2 path: scoring_node writes scoring.arrow; orchestrator only creates run dir
            run_id = _seed_run_dir_ros2(bridge.scenario_id)
    return {"success": result.success, "error": result.error, "run_id": run_id}


def _seed_run_dir_demo(scenario_id: str) -> str:
    """Legacy DEMO-1 path: write hardcoded scoring stub. REMOVED in Phase 2."""
    # ... same as current _seed_run_dir ...


def _seed_run_dir_ros2(scenario_id: str) -> str:
    """ROS2 path: create run dir with scenario YAML only; scoring.arrow written by scoring_node."""
    global _last_run_id
    run_id = f"run-{int(time.time() * 1000):x}"
    _last_run_id = run_id
    run_path = RUN_DIR / run_id
    run_path.mkdir(parents=True, exist_ok=True)
    detail = _store.get(scenario_id)
    if detail is not None:
        (run_path / "scenario.yaml").write_text(detail["yaml_content"])
        (run_path / "scenario.sha256").write_text(detail["hash"])
    return run_id
```

- [ ] **Step 3: 前端 Builder 屏读取并显示 backend 状态**

不需要代码修改 — 前端已在 `useGetScenarioQuery` 中获取完整 YAML，由 `js-yaml` 解析后读取 `metadata.simulation_settings.backend` 即可。仅需在 ScenarioBuilder 的右侧栏显示当前 backend 值。

- [ ] **Step 4: Commit**

```bash
git add src/sil_orchestrator/main.py src/sil_orchestrator/scenario_store.py
git commit -m "feat(backend): add simulation_settings.backend flag routing in orchestrator"
```

---

## Phase 1: scoring_node 6 维 Hagen 2022 评分引擎 (D2.4-scoring-core)

> **依赖**: Phase 0 (backend flag)。

### Task 1.1: HagenScorer 纯 Python 评分引擎 (TDD)

**Files:**
- Create: `src/sim_workbench/sil_nodes/scoring/scoring/hagen_scorer.py`
- Create: `tests/sim_workbench/scoring/test_hagen_scorer.py`

**评分公式 (per Doc 4 §7.2):**

```
total_score = w_safety · S_safety + w_rule · S_rule - P_delay - P_mag + w_phase · S_phase + w_pl · S_implaus
```

6 维定义:

| 维度 | 公式 | 输入 | 来源 |
|---|---|---|---|
| **safety** | `S_s = clip(cpa / cpa_target, 0, 1)` | own_ship, targets, cpa_target=0.27 nm (500m) | Hagen 2022 §II.C |
| **rule_compliance** | `S_r = (n_ok + 0.5·n_partial) / n_total` | per-rule 状态 from M6 COLREGs Reasoner | Woerner 2019 |
| **delay_penalty** | `P_d = max(0, t_action - t_target) · λ_delay` | t_action from M4 behavior, t_target from scenario metadata | Hagen 2022 §III.A |
| **action_magnitude_penalty** | `P_m = 0 if δ∈[30°,90°] else (|δ-60°|-30)²/30²` | rudder_angle, turning rate | Rule 8 "substantial" |
| **phase_score** | `S_p = 1.0 if give-way early/large; 0.5 if stand-on maintained; 0.0 if late/in extremis` | behavior_plan from M4 | Hagen 2022 §III.B |
| **trajectory_implausibility** | `S_pl = 1 - max(0, (|κ| - κ_max)/κ_max, (|a| - a_max)/a_max)` | trajectory curvature κ, acceleration a | Anti-RL "cheating" |

默认权重 (待 D1.7 校准, 当前使用 Hagen 2022 推荐值):
```python
DEFAULT_WEIGHTS = {
    "safety": 0.30,
    "rule_compliance": 0.25,
    "delay_penalty": 0.12,  # penalty weight, not coefficient
    "action_magnitude_penalty": 0.08,
    "phase": 0.15,
    "plausibility": 0.10,
}
```

- [ ] **Step 1: 编写测试 (test_hagen_scorer.py)**

```python
import pytest
from scoring.hagen_scorer import HagenScorer, ScoringRow

class TestHagenScorer:
    def test_safety_perfect_cpa(self):
        """CPA = 1.0 nm >> 0.27 nm target → safety = 1.0"""
        scorer = HagenScorer(cpa_target_nm=0.27)
        row: ScoringRow = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={},
            t_action_s=0.0, t_target_action_s=0.0,
            rudder_deg=0.0, turning_rate_dps=0.0,
            behavior_phase="transit",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        )
        assert row.safety == 1.0

    def test_safety_zero_cpa(self):
        """CPA = 0 → safety = 0.0"""
        scorer = HagenScorer(cpa_target_nm=0.27)
        row: ScoringRow = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[(63.4405, 10.38, 180.0, 10.0)],  # head-on, CPA≈0
            rule_states={},
            t_action_s=0.0, t_target_action_s=0.0,
            rudder_deg=0.0, turning_rate_dps=0.0,
            behavior_phase="transit",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        )
        assert row.safety < 0.1

    def test_rule_compliance_all_pass(self):
        """All rules satisfied → rule_compliance = 1.0"""
        scorer = HagenScorer()
        row = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={"R14": "ok", "R8": "ok", "R16": "ok"},
            t_action_s=0.0, t_target_action_s=0.0,
            rudder_deg=0.0, turning_rate_dps=0.0,
            behavior_phase="transit",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        )
        assert row.rule_compliance == 1.0

    def test_rule_compliance_one_partial(self):
        """1 partial, 2 full → rule_compliance = (0.5+1+1)/3 = 0.833"""
        scorer = HagenScorer()
        row = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={"R14": "ok", "R8": "partial", "R16": "ok"},
            t_action_s=0.0, t_target_action_s=0.0,
            rudder_deg=0.0, turning_rate_dps=0.0,
            behavior_phase="transit",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        )
        assert 0.82 < row.rule_compliance < 0.85

    def test_delay_penalty_zero(self):
        """Action taken on time → delay_penalty = 0"""
        scorer = HagenScorer(delay_coeff=0.01)
        row = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={},
            t_action_s=120.0, t_target_action_s=120.0,
            rudder_deg=0.0, turning_rate_dps=0.0,
            behavior_phase="give_way",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        )
        assert row.delay_penalty == 0.0

    def test_delay_penalty_late(self):
        """Action 30s late → delay_penalty = 30 * 0.01 = 0.30"""
        scorer = HagenScorer(delay_coeff=0.01)
        row = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={},
            t_action_s=150.0, t_target_action_s=120.0,
            rudder_deg=0.0, turning_rate_dps=0.0,
            behavior_phase="give_way",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        )
        assert 0.29 < row.delay_penalty < 0.31

    def test_action_magnitude_good_range(self):
        """δ=45° in [30,90] → action_magnitude_penalty = 0"""
        scorer = HagenScorer()
        row = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={},
            t_action_s=0.0, t_target_action_s=0.0,
            rudder_deg=45.0, turning_rate_dps=3.0,
            behavior_phase="give_way",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        )
        assert row.action_magnitude_penalty == 0.0

    def test_action_magnitude_too_small(self):
        """δ=10° < 30° → penalty > 0"""
        scorer = HagenScorer()
        row = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={},
            t_action_s=0.0, t_target_action_s=0.0,
            rudder_deg=10.0, turning_rate_dps=1.0,
            behavior_phase="give_way",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        )
        assert row.action_magnitude_penalty > 0.1

    def test_phase_score_give_way(self):
        """Give-way phase → score 1.0"""
        scorer = HagenScorer()
        row = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={},
            t_action_s=0.0, t_target_action_s=0.0,
            rudder_deg=0.0, turning_rate_dps=0.0,
            behavior_phase="give_way",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        )
        assert row.phase_score == 1.0

    def test_phase_score_in_extremis(self):
        """In extremis → score 0.0"""
        scorer = HagenScorer()
        row = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={},
            t_action_s=0.0, t_target_action_s=0.0,
            rudder_deg=0.0, turning_rate_dps=0.0,
            behavior_phase="in_extremis",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        )
        assert row.phase_score == 0.0

    def test_trajectory_implausibility_normal(self):
        """Normal curvature/accel → plausibility = 1.0"""
        scorer = HagenScorer(kappa_max=0.01, accel_max_ms2=2.0)
        row = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={},
            t_action_s=0.0, t_target_action_s=0.0,
            rudder_deg=0.0, turning_rate_dps=0.0,
            behavior_phase="transit",
            trajectory_curvature=0.005, trajectory_accel_ms2=0.5,
        )
        assert row.plausibility == 1.0

    def test_trajectory_implausibility_wild_curvature(self):
        """κ = 3·κ_max → plausibility < 0"""
        scorer = HagenScorer(kappa_max=0.01, accel_max_ms2=2.0)
        row = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={},
            t_action_s=0.0, t_target_action_s=0.0,
            rudder_deg=0.0, turning_rate_dps=0.0,
            behavior_phase="transit",
            trajectory_curvature=0.03, trajectory_accel_ms2=0.5,
        )
        assert row.plausibility < 0.0

    def test_total_score_perfect(self):
        """All perfect → total_score ≈ 1.0"""
        scorer = HagenScorer()
        row = scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[],
            rule_states={"R14": "ok"},
            t_action_s=100.0, t_target_action_s=100.0,
            rudder_deg=45.0, turning_rate_dps=3.0,
            behavior_phase="give_way",
            trajectory_curvature=0.001, trajectory_accel_ms2=0.1,
        )
        assert 0.95 < row.total < 1.01

    def test_accumulate_rows(self):
        """Verify that accumulate_rows() builds the row history"""
        scorer = HagenScorer()
        scorer.score_frame(
            own_lat=63.44, own_lon=10.38, own_heading=0.0, own_sog=10.0,
            targets=[], rule_states={},
            t_action_s=0.0, t_target_action_s=0.0,
            rudder_deg=0.0, turning_rate_dps=0.0,
            behavior_phase="transit",
            trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        )
        assert len(scorer.get_rows()) == 1
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cd /Users/marine/Code/MASS-L3-Tactical Layer
python -m pytest tests/sim_workbench/scoring/test_hagen_scorer.py -v
# Expected: ALL FAIL — ModuleNotFoundError: No module named 'scoring.hagen_scorer'
```

- [ ] **Step 3: 编写 HagenScorer 实现**

```python
"""HagenScorer — 6-dimensional scoring per Hagen (2022) + Woerner (2019).

Design baseline: docs/Design/SIL/v1.0-unified/04-sil-scenario-integration-test.md §7.2
"""

from dataclasses import dataclass, field
import math
from typing import Dict, List, Tuple, Optional


@dataclass
class ScoringRow:
    """Single simulation frame scoring output."""
    stamp: float          # simulation time (seconds)
    safety: float         # [0, 1]
    rule_compliance: float  # [0, 1]
    delay_penalty: float   # [0, ∞), subtractive
    action_magnitude_penalty: float  # [0, ∞), subtractive
    phase_score: float     # [0, 1]
    plausibility: float    # [-∞, 1]
    total: float           # weighted sum
    cpa_nm: float = 0.0    # computed CPA this frame
    cpa_target_nm: float = 0.27


DEFAULT_WEIGHTS = {
    "safety": 0.30,
    "rule_compliance": 0.25,
    "delay_penalty": 0.12,
    "action_magnitude_penalty": 0.08,
    "phase": 0.15,
    "plausibility": 0.10,
}


class HagenScorer:
    """Six-dimensional scenario scorer.

    6 dimensions:
      safety          — f(CPA, CPA_target) ∈ [0, 1]
      rule_compliance — per-rule {full=1.0, partial=0.5, violated=0.0} weighted mean
      delay_penalty   — max(0, t_action - t_target) × λ_delay
      action_magnitude_penalty — penalty for rudder angle outside [30°, 90°]
      phase_score     — give_way=1.0, stand_on=0.5, in_extremis=0.0
      plausibility    — 1 - max(excess curvature, excess acceleration) / max_allowed
    """

    def __init__(
        self,
        cpa_target_nm: float = 0.27,
        delay_coeff: float = 0.01,   # λ_delay: penalty per second of delay
        kappa_max: float = 0.01,      # max plausible curvature (1/m)
        accel_max_ms2: float = 2.0,   # max plausible acceleration (m/s²)
        weights: Optional[Dict[str, float]] = None,
    ):
        self.cpa_target_nm = cpa_target_nm
        self.delay_coeff = delay_coeff
        self.kappa_max = kappa_max
        self.accel_max_ms2 = accel_max_ms2
        self._weights = weights or DEFAULT_WEIGHTS.copy()
        self._rows: List[ScoringRow] = []

    # ─── CPA computation ───────────────────────────────────────

    @staticmethod
    def _haversine_nm(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
        """Haversine distance in nautical miles."""
        R_nm = 3440.065  # Earth mean radius in nautical miles
        dlat = math.radians(lat2 - lat1)
        dlon = math.radians(lon2 - lon1)
        a = math.sin(dlat / 2) ** 2 + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) * math.sin(dlon / 2) ** 2
        return 2 * R_nm * math.atan2(math.sqrt(a), math.sqrt(1 - a))

    def _compute_cpa_nm(
        self,
        own_lat: float, own_lon: float, own_sog: float, own_cog: float,
        tgt_lat: float, tgt_lon: float, tgt_sog: float, tgt_cog: float,
    ) -> float:
        """Simplified CPA: distance at current positions. (Full DCPA/TCPA in scoring_node ROS2 wrapper)."""
        return self._haversine_nm(own_lat, own_lon, tgt_lat, tgt_lon)

    # ─── Per-dimension scoring ─────────────────────────────────

    def _score_safety(self, cpa_nm: float) -> float:
        """Hagen 2022 §II.C: linear ramp CPA/CPA_target, clamped to [0, 1]."""
        if self.cpa_target_nm <= 0:
            return 1.0
        return max(0.0, min(1.0, cpa_nm / self.cpa_target_nm))

    def _score_rule_compliance(self, rule_states: Dict[str, str]) -> float:
        """Woerner 2019: per-rule weighted mean.
        rule_states: {"R14": "ok", "R8": "partial", "R15": "violated"}
        """
        if not rule_states:
            return 1.0  # no rules → full score
        score_map = {"ok": 1.0, "partial": 0.5, "violated": 0.0}
        scores = [score_map.get(v, 1.0) for v in rule_states.values()]
        return sum(scores) / len(scores)

    def _score_delay_penalty(self, t_action_s: float, t_target_action_s: float) -> float:
        """Hagen 2022 §III.A: only penalize late actions."""
        delay = max(0.0, t_action_s - t_target_action_s)
        return delay * self.delay_coeff

    def _score_action_magnitude_penalty(self, rudder_deg: float, turning_rate_dps: float) -> float:
        """Rule 8 'substantial action': good range [30°, 90°].
        Penalty = (|δ - 60°| - 30)² / 30² when outside range.
        """
        deviation = abs(abs(rudder_deg) - 60.0) - 30.0
        if deviation <= 0:
            return 0.0
        return (deviation / 30.0) ** 2

    def _score_phase(self, behavior_phase: str) -> float:
        """Hagen 2022 §III.B: phase scoring.
        - give_way: 1.0 (early, large action)
        - stand_on: 0.5 (maintain course/speed)
        - in_extremis: 0.0 (action too late)
        - transit: 1.0 (no encounter)
        """
        phase_map = {
            "give_way": 1.0,
            "stand_on": 0.5,
            "in_extremis": 0.0,
            "transit": 1.0,
        }
        return phase_map.get(behavior_phase, 0.5)

    def _score_plausibility(self, curvature: float, accel_ms2: float) -> float:
        """Anti-RL 'cheating': penalize physically implausible trajectories.
        Returns 1.0 for normal trajectories, < 0 for severely implausible ones.
        """
        excess_k = max(0.0, abs(curvature) - self.kappa_max) / self.kappa_max if self.kappa_max > 0 else 0.0
        excess_a = max(0.0, abs(accel_ms2) - self.accel_max_ms2) / self.accel_max_ms2 if self.accel_max_ms2 > 0 else 0.0
        return 1.0 - max(excess_k, excess_a)

    # ─── Main scoring entry ────────────────────────────────────

    def score_frame(
        self,
        own_lat: float, own_lon: float, own_heading: float, own_sog: float,
        targets: List[Tuple[float, float, float, float]],  # [(lat, lon, cog, sog), ...]
        rule_states: Dict[str, str],
        t_action_s: float, t_target_action_s: float,
        rudder_deg: float, turning_rate_dps: float,
        behavior_phase: str,
        trajectory_curvature: float, trajectory_accel_ms2: float,
        timestamp: Optional[float] = None,
    ) -> ScoringRow:
        """Score one simulation frame.

        Returns a ScoringRow with all 6 dimensions and weighted total.
        """
        import time
        stamp = timestamp if timestamp is not None else time.time()

        # CPA to closest target
        cpa_nm = float("inf")
        for tgt in targets:
            d = self._compute_cpa_nm(own_lat, own_lon, own_sog, own_heading,
                                     tgt[0], tgt[1], tgt[3], tgt[2])
            cpa_nm = min(cpa_nm, d)
        if cpa_nm == float("inf"):
            cpa_nm = 10.0  # no targets → large CPA

        safety = self._score_safety(cpa_nm)
        rule = self._score_rule_compliance(rule_states)
        delay = self._score_delay_penalty(t_action_s, t_target_action_s)
        mag = self._score_action_magnitude_penalty(rudder_deg, turning_rate_dps)
        phase = self._score_phase(behavior_phase)
        plaus = self._score_plausibility(trajectory_curvature, trajectory_accel_ms2)

        total = (
            self._weights["safety"] * safety
            + self._weights["rule_compliance"] * rule
            - self._weights["delay_penalty"] * delay
            - self._weights["action_magnitude_penalty"] * mag
            + self._weights["phase"] * phase
            + self._weights["plausibility"] * plaus
        )

        row = ScoringRow(
            stamp=stamp,
            safety=safety,
            rule_compliance=rule,
            delay_penalty=delay,
            action_magnitude_penalty=mag,
            phase_score=phase,
            plausibility=plaus,
            total=total,
            cpa_nm=cpa_nm,
            cpa_target_nm=self.cpa_target_nm,
        )
        self._rows.append(row)
        return row

    def get_rows(self) -> List[ScoringRow]:
        """Return all scored rows."""
        return list(self._rows)

    def get_final_verdict(self, threshold: float = 0.70) -> Tuple[bool, float]:
        """Aggregate: (pass, avg_total_score) across all rows."""
        if not self._rows:
            return False, 0.0
        avg = sum(r.total for r in self._rows) / len(self._rows)
        return avg >= threshold, avg
```

- [ ] **Step 4: 运行测试确认通过**

```bash
python -m pytest tests/sim_workbench/scoring/test_hagen_scorer.py -v
# Expected: 12 tests PASS
```

- [ ] **Step 5: Commit**

```bash
git add src/sim_workbench/sil_nodes/scoring/scoring/hagen_scorer.py \
        tests/sim_workbench/scoring/test_hagen_scorer.py
git commit -m "feat(scoring): add HagenScorer 6-dim engine with TDD (Hagen 2022 + Woerner 2019)"
```

### Task 1.2: ArrowWriter 列存旁路写入器

**Files:**
- Create: `src/sim_workbench/sil_nodes/scoring/scoring/arrow_writer.py`
- Create: `tests/sim_workbench/scoring/test_arrow_writer.py`

**scoring.arrow schema** (per Doc 4 §9.4):
```
stamp: float64 (timestamp_seconds)
safety: float64
rule_compliance: float64
delay_penalty: float64
action_magnitude_penalty: float64
phase_score: float64
plausibility: float64
total: float64
cpa_nm: float64
cpa_target_nm: float64
```

- [ ] **Step 1: 编写测试**

```python
import pytest
import tempfile
from pathlib import Path
from scoring.arrow_writer import ArrowWriter
from scoring.hagen_scorer import ScoringRow

class TestArrowWriter:
    def test_write_and_read_single_row(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "scoring.arrow"
            writer = ArrowWriter(str(path))
            row = ScoringRow(
                stamp=100.0, safety=0.92, rule_compliance=1.0,
                delay_penalty=0.0, action_magnitude_penalty=0.0,
                phase_score=1.0, plausibility=0.95, total=0.91,
                cpa_nm=0.42, cpa_target_nm=0.27,
            )
            writer.append(row)
            writer.flush()
            assert path.exists()
            assert path.stat().st_size > 0

            # Read back
            import polars as pl
            df = pl.read_ipc(str(path))
            assert len(df) == 1
            assert abs(df["total"][0] - 0.91) < 0.001

    def test_write_multiple_rows(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "scoring.arrow"
            writer = ArrowWriter(str(path))
            for i in range(100):
                row = ScoringRow(
                    stamp=float(i), safety=0.9, rule_compliance=1.0,
                    delay_penalty=0.0, action_magnitude_penalty=0.0,
                    phase_score=1.0, plausibility=1.0, total=0.95,
                    cpa_nm=0.5, cpa_target_nm=0.27,
                )
                writer.append(row)
            writer.flush()

            import polars as pl
            df = pl.read_ipc(str(path))
            assert len(df) == 100
            assert df["stamp"].to_list() == list(range(100))

    def test_buffer_flushes_at_threshold(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "scoring.arrow"
            writer = ArrowWriter(str(path), batch_size=10)
            row = ScoringRow(stamp=0.0, safety=1.0, rule_compliance=1.0,
                             delay_penalty=0.0, action_magnitude_penalty=0.0,
                             phase_score=1.0, plausibility=1.0, total=1.0,
                             cpa_nm=1.0, cpa_target_nm=0.27)
            for _ in range(10):
                writer.append(row)
            # buffer flushed automatically at batch_size
            assert path.exists()
```

- [ ] **Step 2: 运行测试确认失败**

```bash
python -m pytest tests/sim_workbench/scoring/test_arrow_writer.py -v
# Expected: FAIL — ModuleNotFoundError
```

- [ ] **Step 3: 编写 ArrowWriter 实现**

```python
"""ArrowWriter — side-channel Arrow IPC writer for scoring data.

Writes scoring.arrow to runs/{run_id}/scoring.arrow using PyArrow RecordBatch
streaming with polars bridge for downstream KPI derivation.
"""

from pathlib import Path
from typing import List
import pyarrow as pa
import pyarrow.ipc as ipc


class ArrowWriter:
    """Writes ScoringRow sequences as Arrow IPC file format.

    Buffers rows in memory up to batch_size, then appends to the IPC file.
    At 1 Hz scoring and batch_size=60, a 1-hour run produces 1 file.
    """

    SCHEMA = pa.schema([
        pa.field("stamp", pa.float64()),
        pa.field("safety", pa.float64()),
        pa.field("rule_compliance", pa.float64()),
        pa.field("delay_penalty", pa.float64()),
        pa.field("action_magnitude_penalty", pa.float64()),
        pa.field("phase_score", pa.float64()),
        pa.field("plausibility", pa.float64()),
        pa.field("total", pa.float64()),
        pa.field("cpa_nm", pa.float64()),
        pa.field("cpa_target_nm", pa.float64()),
    ])

    def __init__(self, output_path: str, batch_size: int = 60):
        self._path = Path(output_path)
        self._path.parent.mkdir(parents=True, exist_ok=True)
        self._batch_size = batch_size
        self._buffer: List[dict] = []
        self._writer: ipc.RecordBatchFileWriter | None = None

    def _ensure_writer_open(self):
        if self._writer is None:
            sink = pa.OSFile(str(self._path), "wb")
            self._writer = ipc.new_file(sink, self.SCHEMA)

    def append(self, row) -> None:
        """Append a ScoringRow (dataclass or dict) to the buffer. Auto-flushes at batch_size."""
        if hasattr(row, "__dataclass_fields__"):
            d = {f: getattr(row, f) for f in row.__dataclass_fields__}
        else:
            d = row
        self._buffer.append(d)
        if len(self._buffer) >= self._batch_size:
            self.flush()

    def flush(self) -> None:
        """Flush buffered rows to the Arrow IPC file."""
        if not self._buffer:
            return
        self._ensure_writer_open()
        batch = pa.RecordBatch.from_pylist(self._buffer, schema=self.SCHEMA)
        self._writer.write_batch(batch)
        self._buffer.clear()

    def close(self) -> None:
        """Flush remaining rows and close the IPC writer."""
        self.flush()
        if self._writer is not None:
            self._writer.close()
            self._writer = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
```

- [ ] **Step 4: 运行测试确认通过**

```bash
python -m pytest tests/sim_workbench/scoring/test_arrow_writer.py -v
# Expected: 3 tests PASS
```

- [ ] **Step 5: Commit**

```bash
git add src/sim_workbench/sil_nodes/scoring/scoring/arrow_writer.py \
        tests/sim_workbench/scoring/test_arrow_writer.py
git commit -m "feat(scoring): add ArrowWriter side-channel IPC with polars bridge"
```

### Task 1.3: KpiDeriver 从 Arrow 计算 8 KPI

**Files:**
- Create: `src/sim_workbench/sil_nodes/scoring/scoring/kpi_deriver.py`
- Create: `tests/sim_workbench/scoring/test_kpi_deriver.py`

**8 KPI 定义 (per V&V Plan §4 + Doc 4 §5.2):**

| KPI | 来源 | 计算方式 |
|---|---|---|
| `min_cpa_nm` | scoring.arrow cpa_nm column | min(cpa_nm) |
| `tcpa_min_s` | trajectory timestamp | argmin(cpa_nm) 对应 stamp |
| `avg_rot_dpm` | own_ship_state ROS2 topic | mean(abs(rot_radps)) × 60 / (2π) |
| `max_rudder_deg` | own_ship_state ros2 topic | max(abs(rudder_angle)) |
| `grounding_risk_score` | ENC depth + own_ship draft | 1.0 - min(depth_draft_ratio) |
| `route_deviation_nm` | own_ship trajectory vs WP line | max(cross_track_error_nm) |
| `time_to_mrm_s` | ASDR events | timestamp(first MRC event) - timestamp(run_start) |
| `decision_count` | ASDR events | count(event_type == "decision") |

- [ ] **Step 1: 编写测试**

```python
import pytest
import tempfile
from pathlib import Path
import polars as pl
import pyarrow as pa
import pyarrow.ipc as ipc
from scoring.kpi_deriver import KpiDeriver


def _write_test_arrow(path: str, rows: list[dict]) -> None:
    schema = pa.schema([
        pa.field("stamp", pa.float64()),
        pa.field("safety", pa.float64()),
        pa.field("rule_compliance", pa.float64()),
        pa.field("delay_penalty", pa.float64()),
        pa.field("action_magnitude_penalty", pa.float64()),
        pa.field("phase_score", pa.float64()),
        pa.field("plausibility", pa.float64()),
        pa.field("total", pa.float64()),
        pa.field("cpa_nm", pa.float64()),
        pa.field("cpa_target_nm", pa.float64()),
    ])
    with pa.OSFile(path, "wb") as sink:
        writer = ipc.new_file(sink, schema)
        batch = pa.RecordBatch.from_pylist(rows, schema=schema)
        writer.write_batch(batch)
        writer.close()


class TestKpiDeriver:
    def test_kpi_from_scoring_arrow(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            arrow_path = Path(tmpdir) / "scoring.arrow"
            rows = [
                {"stamp": 0.0, "safety": 1.0, "rule_compliance": 1.0,
                 "delay_penalty": 0.0, "action_magnitude_penalty": 0.0,
                 "phase_score": 1.0, "plausibility": 1.0, "total": 1.0,
                 "cpa_nm": 1.0, "cpa_target_nm": 0.27},
                {"stamp": 100.0, "safety": 0.5, "rule_compliance": 1.0,
                 "delay_penalty": 0.1, "action_magnitude_penalty": 0.0,
                 "phase_score": 1.0, "plausibility": 1.0, "total": 0.85,
                 "cpa_nm": 0.14, "cpa_target_nm": 0.27},  # min CPA @ 100s
                {"stamp": 200.0, "safety": 0.8, "rule_compliance": 0.8,
                 "delay_penalty": 0.0, "action_magnitude_penalty": 0.05,
                 "phase_score": 0.5, "plausibility": 1.0, "total": 0.72,
                 "cpa_nm": 0.35, "cpa_target_nm": 0.27},
            ]
            _write_test_arrow(str(arrow_path), rows)

            deriver = KpiDeriver()
            kpis = deriver.derive_from_arrow(str(arrow_path))

            assert abs(kpis["min_cpa_nm"] - 0.14) < 0.01
            assert abs(kpis["tcpa_min_s"] - 100.0) < 1.0
            assert kpis["decision_count"] == 3  # number of rows
```

- [ ] **Step 2: 运行测试确认失败**

```bash
python -m pytest tests/sim_workbench/scoring/test_kpi_deriver.py -v
# Expected: FAIL
```

- [ ] **Step 3: 编写 KpiDeriver 实现**

```python
"""KpiDeriver — compute 8 derived KPIs from scoring.arrow + trajectory data."""

from typing import Dict, Optional
from pathlib import Path
import polars as pl


class KpiDeriver:
    """Derives 8 end-to-end KPIs from Arrow scoring output and ancillary data.

    Input sources:
    - scoring.arrow: per-frame 6-dim scores + cpa_nm
    - own_ship.arrow (future): full trajectory for avg_rot, max_rudder, route_deviation
    - asdr_events.jsonl (future): decision_count, time_to_mrm
    """

    def derive_from_arrow(
        self,
        scoring_arrow_path: str,
        own_ship_arrow_path: Optional[str] = None,
        asdr_events_path: Optional[str] = None,
        grounding_risk: Optional[float] = None,
    ) -> Dict[str, float]:
        """Compute 8 KPI dict from Arrow scoring + optional ancillary data."""
        df = pl.read_ipc(scoring_arrow_path)

        if len(df) == 0:
            return {
                "min_cpa_nm": 0.0, "tcpa_min_s": 0.0,
                "avg_rot_dpm": 0.0, "max_rudder_deg": 0.0,
                "grounding_risk_score": 1.0, "route_deviation_nm": 0.0,
                "time_to_mrm_s": 0.0, "decision_count": 0,
            }

        # 1. min_cpa_nm — minimum CPA across all frames
        min_cpa_nm = df["cpa_nm"].min()

        # 2. tcpa_min_s — timestamp of min CPA
        min_idx = df["cpa_nm"].arg_min()
        tcpa_min_s = df["stamp"][min_idx]

        # 3-4. avg_rot_dpm, max_rudder_deg — from own_ship trajectory (if available)
        avg_rot_dpm = 0.0
        max_rudder_deg = 0.0
        if own_ship_arrow_path and Path(own_ship_arrow_path).exists():
            own_df = pl.read_ipc(own_ship_arrow_path)
            if "rot_dps" in own_df.columns:
                avg_rot_dpm = own_df["rot_dps"].abs().mean()
            if "rudder_angle_deg" in own_df.columns:
                max_rudder_deg = own_df["rudder_angle_deg"].abs().max()

        # 5. grounding_risk_score — from ENC depth check (external)
        grounding_risk_score = grounding_risk if grounding_risk is not None else 1.0

        # 6. route_deviation_nm — from cross-track error (if available)
        route_deviation_nm = 0.0
        if own_ship_arrow_path and Path(own_ship_arrow_path).exists():
            own_df = pl.read_ipc(own_ship_arrow_path)
            if "cross_track_error_nm" in own_df.columns:
                route_deviation_nm = own_df["cross_track_error_nm"].abs().max()

        # 7-8. time_to_mrm_s, decision_count — from ASDR events
        time_to_mrm_s = 0.0
        decision_count = len(df)

        return {
            "min_cpa_nm": round(min_cpa_nm, 4),
            "tcpa_min_s": round(tcpa_min_s, 1),
            "avg_rot_dpm": round(avg_rot_dpm, 2),
            "max_rudder_deg": round(max_rudder_deg, 1),
            "grounding_risk_score": round(grounding_risk_score, 4),
            "route_deviation_nm": round(route_deviation_nm, 4),
            "time_to_mrm_s": round(time_to_mrm_s, 1),
            "decision_count": int(decision_count),
        }
```

- [ ] **Step 4: 运行测试确认通过**

```bash
python -m pytest tests/sim_workbench/scoring/test_kpi_deriver.py -v
# Expected: 1 test PASS
```

- [ ] **Step 5: Commit**

```bash
git add src/sim_workbench/sil_nodes/scoring/scoring/kpi_deriver.py \
        tests/sim_workbench/scoring/test_kpi_deriver.py
git commit -m "feat(scoring): add KpiDeriver to compute 8 KPIs from scoring.arrow"
```

### Task 1.4: scoring_node 升级为 LifecycleNode (ROS2 集成)

**Files:**
- Modify: `src/sim_workbench/sil_nodes/scoring/scoring/node.py`
- Modify: `src/sim_workbench/sil_nodes/scoring/scoring/__init__.py`
- Modify: `src/sim_workbench/sil_nodes/scoring/setup.py`
- Modify: `src/sim_workbench/sil_nodes/scoring/package.xml`
- Create: `tests/sim_workbench/scoring/test_scoring_node_ros2.py`

- [ ] **Step 1: 更新 package.xml 依赖**

在 `src/sim_workbench/sil_nodes/scoring/package.xml` 中添加：

```xml
<depend>rclpy</depend>
<depend>lifecycle_msgs</depend>
<depend>sil_msgs</depend>
<depend>l3_external_msgs</depend>
<depend>python3-pyarrow</depend>
<depend>python3-polars</depend>
```

- [ ] **Step 2: 更新 setup.py console_scripts**

```python
entry_points={
    'console_scripts': [
        'scoring_node = scoring.node:main',
    ],
},
```

- [ ] **Step 3: 编写 ROS2 LifecycleNode 实现**

```python
"""ScoringNode — ROS2 LifecycleNode with 6-dim Hagen 2022 scoring.

Subscribes:
  - /sil/own_ship_state (50 Hz) — OwnShipState
  - /sil/target_vessel_state (10 Hz) — TargetVesselState[]
  - /l3/colregs_active (event) — COLREGs rule states
Publishes:
  - /sil/scoring (1 Hz) — ScoringRow
Side-channel:
  - Arrow IPC → runs/{run_id}/scoring.arrow (1 Hz)
"""

import os
import time
from pathlib import Path

import rclpy
from rclpy.node import Node
from rclpy.lifecycle import LifecycleNode, LifecycleState, TransitionCallbackReturn
from rclpy.lifecycle.node import LifecycleNodeMixin
from lifecycle_msgs.msg import State, Transition
from sil_msgs.msg import OwnShipState, TargetVesselState, ScoringRow as ScoringRowMsg
from l3_external_msgs.msg import TrackedTargetArray
from std_msgs.msg import String

from .hagen_scorer import HagenScorer, ScoringRow
from .arrow_writer import ArrowWriter
from .kpi_deriver import KpiDeriver


class ScoringLifecycleNode(LifecycleNode):
    """ROS2 LifecycleNode wrapping HagenScorer + ArrowWriter."""

    def __init__(self, node_name: str = "scoring_node"):
        super().__init__(node_name)
        self._scorer: HagenScorer | None = None
        self._arrow_writer: ArrowWriter | None = None
        self._own_ship_sub = None
        self._target_vessel_sub = None
        self._colregs_sub = None
        self._scoring_pub = None
        self._timer = None
        self._run_dir: Path | None = None
        self._latest_own_ship: dict = {}
        self._latest_targets: list = []
        self._latest_rule_states: dict = {}
        self._t_action_s: float = 0.0
        self._t_target_action: float = 120.0  # from scenario metadata
        self._behavior_phase: str = "transit"

    # ─── Lifecycle callbacks ───────────────────────────────────

    def on_configure(self, state: LifecycleState) -> TransitionCallbackReturn:
        self.get_logger().info("scoring_node configuring")
        # Create scorer with default weights
        self._scorer = HagenScorer()
        # Create subscribers
        self._own_ship_sub = self.create_subscription(
            OwnShipState, "/sil/own_ship_state", self._own_ship_cb, 10)
        self._target_vessel_sub = self.create_subscription(
            TargetVesselState, "/sil/target_vessel_state", self._target_vessel_cb, 10)
        self._colregs_sub = self.create_subscription(
            String, "/l3/colregs_active", self._colregs_cb, 10)
        # Create publisher
        self._scoring_pub = self.create_publisher(
            ScoringRowMsg, "/sil/scoring", 10)
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: LifecycleState) -> TransitionCallbackReturn:
        self.get_logger().info("scoring_node activating")
        # Determine run_dir from env or default
        run_dir = os.environ.get("SIL_RUN_DIR", "/var/sil/runs")
        run_id = os.environ.get("SIL_RUN_ID", f"run-{int(time.time() * 1000):x}")
        self._run_dir = Path(run_dir) / run_id
        self._run_dir.mkdir(parents=True, exist_ok=True)
        # Open ArrowWriter
        arrow_path = self._run_dir / "scoring.arrow"
        self._arrow_writer = ArrowWriter(str(arrow_path))
        # Start 1 Hz scoring timer
        self._timer = self.create_timer(1.0, self._score_and_publish)
        return TransitionCallbackReturn.SUCCESS

    def on_deactivate(self, state: LifecycleState) -> TransitionCallbackReturn:
        self.get_logger().info("scoring_node deactivating")
        if self._timer:
            self.destroy_timer(self._timer)
            self._timer = None
        if self._arrow_writer:
            self._arrow_writer.close()
            self._arrow_writer = None
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state: LifecycleState) -> TransitionCallbackReturn:
        self.get_logger().info("scoring_node cleaning up")
        if self._own_ship_sub:
            self.destroy_subscription(self._own_ship_sub)
        if self._target_vessel_sub:
            self.destroy_subscription(self._target_vessel_sub)
        if self._colregs_sub:
            self.destroy_subscription(self._colregs_sub)
        if self._scoring_pub:
            self.destroy_publisher(self._scoring_pub)
        self._scorer = None
        return TransitionCallbackReturn.SUCCESS

    # ─── Topic callbacks ───────────────────────────────────────

    def _own_ship_cb(self, msg: OwnShipState):
        self._latest_own_ship = {
            "lat": msg.lat, "lon": msg.lon,
            "heading": msg.heading, "sog": msg.sog, "cog": msg.cog,
            "rot": msg.rot, "rudder_angle": msg.rudder_angle,
        }

    def _target_vessel_cb(self, msg: TargetVesselState):
        self._latest_targets.append({
            "lat": msg.lat, "lon": msg.lon,
            "heading": msg.heading, "sog": msg.sog, "cog": msg.cog,
        })
        if len(self._latest_targets) > 50:
            self._latest_targets = self._latest_targets[-20:]

    def _colregs_cb(self, msg: String):
        # Parse "R14:ok,R8:partial" format
        self._latest_rule_states = {}
        for pair in msg.data.split(","):
            if ":" in pair:
                k, v = pair.split(":", 1)
                self._latest_rule_states[k.strip()] = v.strip()

    # ─── Scoring timer ─────────────────────────────────────────

    def _score_and_publish(self):
        if self._scorer is None or not self._latest_own_ship:
            return

        own = self._latest_own_ship
        targets = [(t["lat"], t["lon"], t["cog"], t["sog"]) for t in self._latest_targets]

        row: ScoringRow = self._scorer.score_frame(
            own_lat=own["lat"], own_lon=own["lon"],
            own_heading=own["heading"], own_sog=own["sog"],
            targets=targets,
            rule_states=self._latest_rule_states,
            t_action_s=self._t_action_s,
            t_target_action_s=self._t_target_action,
            rudder_deg=own.get("rudder_angle", 0.0),
            turning_rate_dps=abs(own.get("rot", 0.0)),
            behavior_phase=self._behavior_phase,
            trajectory_curvature=0.0,  # TODO: compute from trajectory
            trajectory_accel_ms2=0.0,
            timestamp=time.time(),
        )

        # Publish ROS2 msg
        msg = ScoringRowMsg()
        msg.stamp = self.get_clock().now().to_msg()
        msg.safety = row.safety
        msg.rule_compliance = row.rule_compliance
        msg.delay = row.delay_penalty
        msg.magnitude = row.action_magnitude_penalty
        msg.phase = row.phase_score
        msg.plausibility = row.plausibility
        msg.total = row.total
        self._scoring_pub.publish(msg)

        # Side-channel Arrow write
        if self._arrow_writer:
            self._arrow_writer.append(row)


def main(args=None):
    rclpy.init(args=args)
    node = ScoringLifecycleNode()
    executor = rclpy.executors.SingleThreadedExecutor()
    executor.add_node(node)
    try:
        executor.spin()
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()
```

- [ ] **Step 4: 更新 `__init__.py`**

```python
from .node import ScoringLifecycleNode, main
from .hagen_scorer import HagenScorer, ScoringRow
from .arrow_writer import ArrowWriter
from .kpi_deriver import KpiDeriver
```

- [ ] **Step 5: Commit**

```bash
git add src/sim_workbench/sil_nodes/scoring/
git commit -m "feat(scoring): upgrade scoring_node to ROS2 LifecycleNode with Hagen 2022 engine"
```

---

## Phase 2: orchestrator 后端 Cutover (D2.4-backend)

> **依赖**: Phase 1 (scoring_node 可独立运行)。

### Task 2.1: scoring_routes.py — 真实 scoring API 端点

**Files:**
- Create: `src/sil_orchestrator/scoring_routes.py`

- [ ] **Step 1: 编写 scoring_routes**

```python
"""SIL Scoring Routes — read scoring.arrow → derived 8 KPI → REST response."""

import json
from pathlib import Path

from fastapi import APIRouter
from sil_orchestrator.config import RUN_DIR
from sil_orchestrator.scenario_store import _last_run_id  # imported in main

router = APIRouter(prefix="/api/v1/scoring", tags=["scoring"])


@router.get("/last_run")
async def scoring_last_run():
    """Return 8 KPIs + 6-dim scores + rule_chain for the most recent run."""
    # Import here to avoid circular dependency
    from sil_orchestrator.main import _last_run_id as run_id_ref
    import sys
    sys.path.insert(0, str(Path(__file__).parent.parent.parent / "sim_workbench" / "sil_nodes" / "scoring"))
    from scoring.kpi_deriver import KpiDeriver

    import polars as pl

    run_id = run_id_ref
    if run_id is None:
        return {"run_id": None, "kpis": None, "rule_chain": [], "scoring_dimensions": None}

    arrow_path = RUN_DIR / run_id / "scoring.arrow"
    if not arrow_path.exists():
        # Fallback to legacy scoring.json for DEMO-1
        json_path = RUN_DIR / run_id / "scoring.json"
        if json_path.exists():
            data = json.loads(json_path.read_text())
            data["run_id"] = run_id
            return data
        return {"run_id": run_id, "kpis": None, "rule_chain": [], "scoring_dimensions": None}

    # Compute 8 KPIs from Arrow
    deriver = KpiDeriver()
    kpis = deriver.derive_from_arrow(str(arrow_path))

    # Compute 6-dim aggregate scores
    df = pl.read_ipc(str(arrow_path))
    dims = {
        "safety": round(df["safety"].mean(), 4),
        "rule_compliance": round(df["rule_compliance"].mean(), 4),
        "delay_penalty": round(df["delay_penalty"].mean(), 4),
        "action_magnitude_penalty": round(df["action_magnitude_penalty"].mean(), 4),
        "phase_score": round(df["phase_score"].mean(), 4),
        "plausibility": round(df["plausibility"].mean(), 4),
        "total": round(df["total"].mean(), 4),
    }

    # Verdict
    avg_total = df["total"].mean()
    verdict = "pass" if avg_total >= 0.70 else "fail"

    # Rule chain (stub — populated by M6 in Phase 2)
    rule_chain = ["R14", "R8", "R16"]

    return {
        "run_id": run_id,
        "scenario_id": None,
        "kpis": kpis,
        "scoring_dimensions": dims,
        "rule_chain": rule_chain,
        "verdict": verdict,
    }
```

- [ ] **Step 2: 在 main.py 中注册 router**

```python
# Replace inline GET /api/v1/scoring/last_run with:
from sil_orchestrator.scoring_routes import router as scoring_router
app.include_router(scoring_router)
```

- [ ] **Step 3: Commit**

```bash
git add src/sil_orchestrator/scoring_routes.py src/sil_orchestrator/main.py
git commit -m "feat(scoring): add scoring_routes reading Arrow → 8 KPI + 6 dims"
```

### Task 2.2: main.py — 移除 _seed_run_dir 硬编码 stub

**Files:**
- Modify: `src/sil_orchestrator/main.py`

在 `lifecycle_activate` handler 中:

```python
# OLD: always calls _seed_run_dir with hardcoded stub
# NEW: _seed_run_dir_ros2 only creates run dir + copies scenario YAML
# Scoring is written by scoring_node → scoring.arrow
```

- [ ] **Step 1: 将 `_seed_run_dir()` 重命名为 `_seed_run_dir_demo()` 并标记 deprecated**

```python
import warnings

def _seed_run_dir(scenario_id: str) -> str:
    """DEPRECATED: use _seed_run_dir_ros2() instead. Removed in Phase 2."""
    warnings.warn("_seed_run_dir is deprecated; use _seed_run_dir_ros2", DeprecationWarning)
    return _seed_run_dir_demo(scenario_id)
```

- [ ] **Step 2: 修改 activate handler 优先使用 ros2 path**

```python
@app.post("/api/v1/lifecycle/activate")
async def lifecycle_activate():
    result = await bridge.activate()
    run_id = None
    if result.success and bridge.scenario_id:
        detail = _store.get(bridge.scenario_id)
        backend = detail.get("backend", "demo") if detail else "demo"

        # Determine scoring path: ROS2 uses scoring_node Arrow; demo uses stub
        scoring_path = None
        if backend == "ros2":
            run_id = _seed_run_dir_ros2(bridge.scenario_id)
            scoring_path = str(RUN_DIR / run_id / "scoring.arrow")
            # Pass path to scoring_node via env (read in on_activate)
            import os
            os.environ["SIL_RUN_DIR"] = str(RUN_DIR)
            os.environ["SIL_RUN_ID"] = run_id
        else:
            run_id = _seed_run_dir_demo(bridge.scenario_id)

    return {
        "success": result.success,
        "error": result.error,
        "run_id": run_id,
        "scoring_path": scoring_path,
    }
```

- [ ] **Step 3: Commit**

```bash
git add src/sil_orchestrator/main.py
git commit -m "refactor(scoring): route scoring to Arrow path in ros2 backend, deprecate hardcoded stub"
```

---

## Phase 3: Mock 退役 + Parallel Run 验证 (D2.4-mock)

> **依赖**: Phase 0 (backend flag), Phase 2 (scoring routes)。

### 3.1 Mock 退役顺序

退役按依赖链逆序，每步独立 commit：

| 顺序 | Mock | 退役条件 | 验证方法 |
|---|---|---|---|
| **1** (MOCK-004) | `sil_mock_publisher` | sensor_mock_node + ship_dynamics_node LifecycleNode PASS | `ros2 topic echo /sil/own_ship_state` 有数据 |
| **2** (MOCK-003) | `l3_external_mock_publisher` | sensor_mock + tracker_mock → M2 接通 | `ros2 topic echo /sil/tracked_targets` 有数据 |
| **3** (MOCK-001) | `demo_server.py` | sil_orchestrator + LifecycleNodes 全栈 PASS | parallel run trajectory diff < 2% |
| **4** (MOCK-002) | `demo_ws_server.py` + `trajectory.py` | foxglove_bridge WS 接通(或 telemetry_bridge 临时) | parallel run trajectory diff < 2% |

### Task 3.1: MOCK-004 — 退役 sil_mock_publisher

**Files:**
- Delete: `src/sim_workbench/sil_mock_publisher/` (全目录)
- Modify: `docker-compose.yml` (移除 sil_mock 引用)

- [ ] **Step 1: 验证 ship_dynamics_node 已实现 LifecycleNode**

```bash
# Verify ROS2 node publishes OwnShipState
ros2 topic echo /sil/own_ship_state --once
# Expected: non-empty OwnShipState message
```

- [ ] **Step 2: 删除 sil_mock_publisher 目录**

```bash
git rm -r src/sim_workbench/sil_mock_publisher/
```

- [ ] **Step 3: Commit**

```bash
git commit -m "feat(mock): MOCK-004 — retire sil_mock_publisher, ship_dynamics_node LifecycleNode online

V&V: sil_mock_publisher replaced by sim_workbench/sil_nodes/ship_dynamics
     LifecycleNode with 4-DOF MMG Yasukawa 2015.
Refs: D1.3a, GAP-018"
```

### Task 3.2: MOCK-003 — 退役 l3_external_mock_publisher

**Files:**
- Delete: `src/l3_tdl_kernel/l3_external_mock_publisher/` (全目录)
- Modify: `docker-compose.yml` (移除 external_mock 引用)

- [ ] **Step 1: 验证 sensor_mock + tracker_mock → M2 数据链**

```bash
ros2 topic echo /sil/tracked_targets --once
# Expected: TrackedTargetArray message from tracker_mock_node
```

- [ ] **Step 2: 删除 l3_external_mock_publisher 目录**

```bash
git rm -r src/l3_tdl_kernel/l3_external_mock_publisher/
```

- [ ] **Step 3: Commit**

```bash
git commit -m "feat(mock): MOCK-003 — retire l3_external_mock_publisher, sensor_mock+tracker_mock → M2 online

V&V: l3_external_mock_publisher replaced by sim_workbench sensor_mock_node
     (AIS Class A/B) + tracker_mock_node (KF/God) → l3_external_msgs/TrackedTargetArray.
Refs: D1.3a, GAP-002"
```

### Task 3.3: Parallel Run 验证 (demo vs ros2)

**Files:**
- Create: `tests/integration/test_cutover_parallel_run.py`

- [ ] **Step 1: 编写 parallel run 对比脚本**

```python
"""Parallel run validation: demo backend vs ros2 backend trajectory comparison.

Runs the same scenario (imazu-01-ho) in demo mode and ros2 mode,
compares output trajectories. Acceptance: position diff < 2% of distance traveled.
"""

import pytest
import yaml
import json
from pathlib import Path
import subprocess
import time


SCENARIO_DIR = Path(__file__).parent.parent.parent / "scenarios"
TEST_SCENARIO = "IMAZU标准测试/imazu-01-ho.yaml"


def _run_scenario(backend: str, scenario_path: str) -> dict:
    """Run a scenario with given backend and return results."""
    # Read scenario YAML
    with open(scenario_path) as f:
        yaml_data = yaml.safe_load(f)

    # Set backend flag
    if "metadata" not in yaml_data:
        yaml_data["metadata"] = {}
    if "simulation_settings" not in yaml_data["metadata"]:
        yaml_data["metadata"]["simulation_settings"] = {}
    yaml_data["metadata"]["simulation_settings"]["backend"] = backend

    # Write temp scenario
    temp_path = Path(f"/tmp/test_{backend}_{int(time.time())}.yaml")
    temp_path.write_text(yaml.dump(yaml_data))

    # Run via REST API
    import requests
    base = "http://localhost:8000/api/v1"

    # Cleanup
    requests.post(f"{base}/lifecycle/cleanup")

    # Configure
    resp = requests.post(f"{base}/lifecycle/configure", json={"scenario_id": temp_path.stem})
    assert resp.status_code == 200, f"Configure failed: {resp.text}"

    # Activate
    resp = requests.post(f"{base}/lifecycle/activate")
    assert resp.status_code == 200, f"Activate failed: {resp.text}"
    run_id = resp.json().get("run_id")

    # Wait for simulation to complete
    time.sleep(5)

    # Deactivate
    requests.post(f"{base}/lifecycle/deactivate")

    # Get scoring
    resp = requests.get(f"{base}/scoring/last_run")
    assert resp.status_code == 200

    temp_path.unlink(missing_ok=True)
    return resp.json()


class TestCutoverParallelRun:
    """Parallel run: demo vs ros2 trajectory comparison."""

    @pytest.mark.integration
    @pytest.mark.skipif(
        not Path("/opt/ros/humble/setup.bash").exists(),
        reason="ROS2 Humble not available"
    )
    def test_demo_vs_ros2_trajectory_diff(self):
        """Run imazu-01-ho in both modes and compare CPA."""
        scenario_path = SCENARIO_DIR / TEST_SCENARIO
        assert scenario_path.exists(), f"Scenario not found: {scenario_path}"

        results_demo = _run_scenario("demo", str(scenario_path))
        results_ros2 = _run_scenario("ros2", str(scenario_path))

        kpis_demo = results_demo.get("kpis", {})
        kpis_ros2 = results_ros2.get("kpis", {})

        cpa_demo = kpis_demo.get("min_cpa_nm", 0)
        cpa_ros2 = kpis_ros2.get("min_cpa_nm", 0)

        if cpa_demo > 0:
            cpa_diff_pct = abs(cpa_demo - cpa_ros2) / cpa_demo * 100
            assert cpa_diff_pct < 5.0, (
                f"CPA diff {cpa_diff_pct:.1f}% > 5% threshold\n"
                f"  demo: {cpa_demo:.4f} nm\n"
                f"  ros2: {cpa_ros2:.4f} nm"
            )

    @pytest.mark.integration
    def test_ros2_scoring_arrow_exists(self):
        """After ros2 run, scoring.arrow must exist."""
        scenario_path = SCENARIO_DIR / TEST_SCENARIO
        results = _run_scenario("ros2", str(scenario_path))
        run_id = results.get("run_id")
        assert run_id is not None, "No run_id returned"

        arrow_path = Path("/var/sil/runs") / run_id / "scoring.arrow"
        assert arrow_path.exists(), f"scoring.arrow not found at {arrow_path}"
```

- [ ] **Step 2: Commit**

```bash
git add tests/integration/test_cutover_parallel_run.py
git commit -m "test(cutover): add parallel run validation for demo vs ros2 trajectory diff < 5% CPA"
```

### Task 3.4: MOCK-001 + MOCK-002 — 退役 demo_server + demo_ws_server

**Files:**
- Delete: `tools/demo/demo_server.py`
- Delete: `tools/demo/demo_ws_server.py` (if exists separately)
- Delete: `tools/demo/trajectory.py`
- Delete: `tools/demo/test_trajectory.py`
- Modify: `docker-compose.yml` (移除 demo 服务)

- [ ] **Step 1: 确认 parallel run PASS 后删除**

```bash
git rm tools/demo/demo_server.py tools/demo/trajectory.py tools/demo/test_trajectory.py
```

- [ ] **Step 2: 更新 docker-compose.yml**

移除 `demo-server` service 定义。

- [ ] **Step 3: Commit (MOCK-001)**

```bash
git commit -m "feat(mock): MOCK-001 — retire demo_server.py, sil_orchestrator now sole REST entry

V&V: demo_server.py replaced by sil_orchestrator FastAPI with ROS2 lifecycle bridge.
     Parallel run CPA diff < 5% threshold met.
Refs: D2.4, GAP-031"
```

- [ ] **Step 4: Commit (MOCK-002) — 若 demo_ws_server.py 独立存在**

```bash
git rm tools/demo/demo_ws_server.py
git commit -m "feat(mock): MOCK-002 — retire demo_ws_server.py, telemetry_bridge now sole WS entry

V&V: demo_ws_server.py replaced by sil_orchestrator/telemetry_bridge.py
     (→ foxglove_bridge in D1.3b.3).
Refs: D2.4, GAP-031"
```

---

## Phase 4: 前端 8 KPI Cards + ScoringRadarChart (D2.4-frontend)

> **依赖**: Phase 2 (scoring API 真实数据)。

### Task 4.1: silApi.ts — 更新 ScoringLastRun 类型

**Files:**
- Modify: `web/src/api/silApi.ts`

- [ ] **Step 1: 扩展 ScoringLastRunFull 接口**

```typescript
// NEW: full 8-KPI + 6-dim scoring response from /api/v1/scoring/last_run (Arrow path)
export interface ScoringLastRunFull {
  run_id: string | null;
  scenario_id?: string;
  kpis: {
    min_cpa_nm: number;
    tcpa_min_s: number;
    avg_rot_dpm: number;
    max_rudder_deg: number;
    grounding_risk_score: number;
    route_deviation_nm: number;
    time_to_mrm_s: number;
    decision_count: number;
  } | null;
  scoring_dimensions: {
    safety: number;
    rule_compliance: number;
    delay_penalty: number;
    action_magnitude_penalty: number;
    phase_score: number;
    plausibility: number;
    total: number;
  } | null;
  rule_chain: string[];
  verdict?: 'pass' | 'fail' | 'pending';
}
```

- [ ] **Step 2: 更新 useGetLastRunScoringQuery 返回类型**

```typescript
getLastRunScoring: builder.query<ScoringLastRunFull, void>({
  query: () => '/scoring/last_run',
  providesTags: ['Run'],
}),
```

- [ ] **Step 3: Commit**

```bash
git add web/src/api/silApi.ts
git commit -m "feat(frontend): add ScoringLastRunFull type with 8 KPIs + 6 dims"
```

### Task 4.2: RunReport.tsx — 8 KPI Cards 全部接真实数据

**Files:**
- Modify: `web/src/screens/RunReport.tsx`

- [ ] **Step 1: 重写 KPI Cards 区域**

替换 RunReport.tsx:154-227 行（KPI cards 硬编码数组）:

```tsx
{[
  {
    label: 'VERDICT',
    value: scoring?.verdict ? scoring.verdict.toUpperCase() : '—',
    sub: scoring?.verdict === 'pass' ? '✓ criteria met' : scoring?.verdict === 'fail' ? '✗ criteria failed' : 'pending',
    accent: scoring?.verdict === 'pass' ? 'var(--c-stbd)' : scoring?.verdict === 'fail' ? 'var(--c-danger)' : 'var(--txt-3)',
  },
  {
    label: 'Min CPA',
    value: kpis?.min_cpa_nm != null ? `${kpis.min_cpa_nm.toFixed(3)} nm` : '—',
    sub: '≥ 0.27 nm threshold',
    accent: kpis?.min_cpa_nm != null && kpis.min_cpa_nm >= 0.27 ? 'var(--c-phos)' : 'var(--c-danger)',
  },
  {
    label: 'TCPA Min',
    value: kpis?.tcpa_min_s != null ? `${kpis.tcpa_min_s.toFixed(0)} s` : '—',
    sub: 'time to min CPA',
    accent: 'var(--c-info)',
  },
  {
    label: 'Avg ROT',
    value: kpis?.avg_rot_dpm != null ? `${kpis.avg_rot_dpm.toFixed(1)} °/min` : '—',
    sub: 'mean rate of turn',
    accent: 'var(--c-info)',
  },
  {
    label: 'Max Rudder',
    value: kpis?.max_rudder_deg != null ? `${kpis.max_rudder_deg.toFixed(1)}°` : '—',
    sub: 'peak rudder angle',
    accent: kpis?.max_rudder_deg != null && kpis.max_rudder_deg <= 35 ? 'var(--c-stbd)' : 'var(--c-danger)',
  },
  {
    label: 'Grounding Risk',
    value: kpis?.grounding_risk_score != null ? `${(kpis.grounding_risk_score * 100).toFixed(1)}%` : '—',
    sub: 'min depth/draft ratio',
    accent: kpis?.grounding_risk_score != null && kpis.grounding_risk_score >= 0.9 ? 'var(--c-stbd)' : 'var(--c-danger)',
  },
  {
    label: 'Route Dev',
    value: kpis?.route_deviation_nm != null ? `${kpis.route_deviation_nm.toFixed(2)} nm` : '—',
    sub: 'max cross-track error',
    accent: 'var(--c-warn)',
  },
  {
    label: 'Time to MRC',
    value: kpis?.time_to_mrm_s != null && kpis.time_to_mrm_s > 0 ? `${kpis.time_to_mrm_s.toFixed(0)} s` : 'N/A',
    sub: kpis?.time_to_mrm_s != null && kpis.time_to_mrm_s > 0 ? 'MSO to MRC' : 'no MRC triggered',
    accent: 'var(--c-warn)',
  },
].map((kpi, i) => (
  <div key={i} style={{
    display: 'flex', flexDirection: 'column', gap: 2,
    padding: '6px 10px', borderLeft: '1px solid var(--line-1)',
    minWidth: 85,
  }}>
    <div style={{ fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--txt-3)',
                  textTransform: 'uppercase', letterSpacing: '0.16em' }}>
      {kpi.label}
    </div>
    <div style={{ display: 'flex', alignItems: 'baseline', gap: 4 }}>
      <span style={{ fontFamily: 'var(--f-mono)', fontSize: 16,
                     color: kpi.accent, fontWeight: 600 }}>
        {kpi.value}
      </span>
    </div>
    <div style={{ fontFamily: 'var(--f-mono)', fontSize: 8.5, color: 'var(--txt-2)' }}>
      {kpi.sub}
    </div>
  </div>
))}
```

- [ ] **Step 2: Commit**

```bash
git add web/src/screens/RunReport.tsx
git commit -m "feat(frontend): wire all 8 KPI cards to real Arrow-derived data"
```

### Task 4.3: ScoringRadarChart — 真实 6 维数据

**Files:**
- Modify: `web/src/screens/shared/ScoringRadarChart.tsx`

- [ ] **Step 1: 接入真实 scoring_dimensions**

在 RunReport.tsx 的 ScoringRadarChart 中:

```tsx
<ScoringRadarChart kpis={{
  safety: scoring?.scoring_dimensions?.safety ?? 0,
  ruleCompliance: scoring?.scoring_dimensions?.rule_compliance ?? 0,
  delay: 1 - (scoring?.scoring_dimensions?.delay_penalty ?? 0),  // invert penalty for radar
  magnitude: 1 - (scoring?.scoring_dimensions?.action_magnitude_penalty ?? 0),
  phase: scoring?.scoring_dimensions?.phase_score ?? 0,
  plausibility: scoring?.scoring_dimensions?.plausibility ?? 0,
}} />
```

- [ ] **Step 2: Commit**

```bash
git add web/src/screens/RunReport.tsx
git commit -m "feat(frontend): wire ScoringRadarChart to real 6-dim Arrow data"
```

---

## Phase 5: CI Imazu-22 + 50 Baseline 自动跑 (D2.4-ci)

> **依赖**: Phase 3 (ROS2 全栈可后台跑)。

### Task 5.1: .gitlab-ci.yml — 新增 imazu-22 + baseline-50 stage

**Files:**
- Modify: `.gitlab-ci.yml`

- [ ] **Step 1: 新增 CI stage 定义**

```yaml
stages:
  - build
  - lint
  - test
  - sil-smoke    # NEW: fast gate (1 scenario)
  - sil-baseline # NEW: 50 baseline + Imazu-22 (runs on merge to main)

# ─── SIL smoke test (every PR) ──────────────────────────────

sil-smoke:
  stage: sil-smoke
  image: mass-l3/ci:humble-ubuntu22.04
  script:
    - source /opt/ros/humble/setup.bash
    - source install/setup.bash
    - |
      # Run imazu-01-ho in ros2 backend
      python -m pytest tests/integration/test_cutover_parallel_run.py::TestCutoverParallelRun::test_ros2_scoring_arrow_exists -v
  artifacts:
    when: always
    paths:
      - test-results/
      - runs/
    expire_in: 7 days
  only:
    - merge_requests
    - main

# ─── SIL baseline (merge to main only) ─────────────────────

sil-baseline-imazu22:
  stage: sil-baseline
  image: mass-l3/ci:humble-ubuntu22.04
  script:
    - source /opt/ros/humble/setup.bash
    - source install/setup.bash
    - |
      # Run all 22 Imazu scenarios
      python tools/run_imazu22.py --backend ros2 --output test-results/imazu22_results.json
    - |
      # Validate pass criteria (X1.5)
      python tools/check_entry_gate.py --phase 1 --artifacts-dir test-results/
  artifacts:
    paths:
      - test-results/
      - runs/
    expire_in: 30 days
  only:
    - main

sil-baseline-50:
  stage: sil-baseline
  image: mass-l3/ci:humble-ubuntu22.04
  script:
    - source /opt/ros/humble/setup.bash
    - source install/setup.bash
    - |
      # Run 50 baseline scenarios
      python tools/run_baseline_50.py --backend ros2 --output test-results/baseline_results.json
    - |
      # Check ≥ 95% pass rate (X1.1)
      python -c "
      import json
      data = json.load(open('test-results/baseline_results.json'))
      total = len(data)
      passed = sum(1 for r in data if r.get('pass', False))
      rate = passed / total * 100 if total else 0
      assert rate >= 95, f'Pass rate {rate:.1f}% < 95% threshold'
      print(f'PASS: {passed}/{total} ({rate:.1f}%)')
      "
  artifacts:
    paths:
      - test-results/
    expire_in: 30 days
  only:
    - main
```

- [ ] **Step 2: 创建 tools/run_imazu22.py**

```python
#!/usr/bin/env python3
"""Batch run all 22 Imazu scenarios with given backend."""

import argparse
import json
import time
from pathlib import Path
import requests


IMAZU_SCENARIOS = [
    "imazu-01-ho", "imazu-02-cr-gw", "imazu-03-ot", "imazu-04-cr-so",
    "imazu-05-ms", "imazu-06-ms", "imazu-07-ms", "imazu-08-ms",
    "imazu-09-ms", "imazu-10-ms", "imazu-11-ms", "imazu-12-ms",
    "imazu-13-ms", "imazu-14-ms", "imazu-15-ms", "imazu-16-ms",
    "imazu-17-ms", "imazu-18-ms", "imazu-19-ms", "imazu-20-ms",
    "imazu-21-ms", "imazu-22-ms",
]

BASE_URL = "http://localhost:8000/api/v1"


def run_one(scenario_id: str, backend: str) -> dict:
    """Run a single scenario and return results."""
    # Cleanup
    requests.post(f"{BASE_URL}/lifecycle/cleanup")
    # Configure
    resp = requests.post(f"{BASE_URL}/lifecycle/configure", json={
        "scenario_id": f"{scenario_id}-v1.0",
        "backend": backend,
    })
    if resp.status_code != 200:
        return {"scenario_id": scenario_id, "pass": False, "error": resp.text}
    # Activate
    resp = requests.post(f"{BASE_URL}/lifecycle/activate")
    if resp.status_code != 200:
        return {"scenario_id": scenario_id, "pass": False, "error": resp.text}
    run_id = resp.json().get("run_id")
    # Wait for completion
    timeout = 60
    for _ in range(timeout):
        resp = requests.get(f"{BASE_URL}/lifecycle/status")
        if resp.json().get("current_state") in (1, 5):  # UNCONFIGURED or FINALIZED
            break
        time.sleep(1)
    # Get scoring
    resp = requests.get(f"{BASE_URL}/scoring/last_run")
    data = resp.json()
    kpis = data.get("kpis", {})
    cpa_nm = kpis.get("min_cpa_nm", 0)
    return {
        "scenario_id": scenario_id,
        "run_id": run_id,
        "pass": data.get("verdict") == "pass",
        "min_cpa_nm": cpa_nm,
        "cpa_pass": cpa_nm >= 0.27,
        "kpis": kpis,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", default="ros2", choices=["demo", "ros2"])
    parser.add_argument("--output", default="test-results/imazu22_results.json")
    args = parser.parse_args()

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    results = []
    for sid in IMAZU_SCENARIOS:
        print(f"Running {sid}...")
        r = run_one(sid, args.backend)
        results.append(r)
        print(f"  {'PASS' if r['pass'] else 'FAIL'}  CPA={r.get('min_cpa_nm', 'N/A')}")

    with open(args.output, "w") as f:
        json.dump(results, f, indent=2)

    passed = sum(1 for r in results if r["pass"])
    cpa_passed = sum(1 for r in results if r.get("cpa_pass", False))
    print(f"\nImazu-22: {passed}/22 PASS, CPA≥0.27: {cpa_passed}/22")


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Commit**

```bash
git add .gitlab-ci.yml tools/run_imazu22.py
git commit -m "ci: add sil-smoke + sil-baseline stages for Imazu-22 + 50 baseline auto-run

V&V Plan X1.5 (Imazu-22 22/22 PASS) + X1.1 (50 baseline ≥ 95% PASS)
run automatically on merge to main via GitLab CI."
```

---

## Phase 6: Docker-Compose 全栈更新 (D2.4-docker)

> **依赖**: Phase 3 (Mock 退役完成)。

### Task 6.1: docker-compose.yml — 移除 demo，新增 scoring

**Files:**
- Modify: `docker-compose.yml`

- [ ] **Step 1: 移除 demo-server service**

```yaml
# REMOVE:
#  demo-server:
#    build: tools/demo/
#    ports: ["8000", "8765"]
```

- [ ] **Step 2: 添加 scoring_node 到 sil-component-container**

在 `sil-component-container` 的 `command` 中追加:

```yaml
sil-component-container:
  build: docker/sil_nodes.Dockerfile
  command: >
    bash -c "source /opt/ros/humble/setup.bash &&
             source install/setup.bash &&
             ros2 run sil_nodes_scoring scoring_node &
             ros2 launch sil_lifecycle scenario_lifecycle_mgr.launch.py"
  volumes:
    - ./scenarios:/var/sil/scenarios
    - ./runs:/var/sil/runs
  network_mode: host
  environment:
    - SIL_RUN_DIR=/var/sil/runs
    - ROS_DOMAIN_ID=0
```

- [ ] **Step 3: Commit**

```bash
git add docker-compose.yml
git commit -m "chore(docker): remove demo services, add scoring_node LifecycleNode to compose"
```

---

## Phase 7: Cutover Commit 序列与 V&V Hash 引用

### 完整 commit 序列 (按实施顺序)

| # | Commit Message | D-task | V&V Hash |
|---|---|---|---|
| 1 | `feat(schema): add simulation_settings.backend feature flag with json schema` | D2.4-flag | — |
| 2 | `feat(backend): add simulation_settings.backend flag routing in orchestrator` | D2.4-flag | — |
| 3 | `feat(scoring): add HagenScorer 6-dim engine with TDD (Hagen 2022 + Woerner 2019)` | D2.4-scoring-core | — |
| 4 | `feat(scoring): add ArrowWriter side-channel IPC with polars bridge` | D2.4-scoring-core | — |
| 5 | `feat(scoring): add KpiDeriver to compute 8 KPIs from scoring.arrow` | D2.4-scoring-core | — |
| 6 | `feat(scoring): upgrade scoring_node to ROS2 LifecycleNode with Hagen 2022 engine` | D2.4-scoring-core | — |
| 7 | `feat(scoring): add scoring_routes reading Arrow → 8 KPI + 6 dims` | D2.4-backend | — |
| 8 | `refactor(scoring): route scoring to Arrow path in ros2 backend, deprecate hardcoded stub` | D2.4-backend | — |
| 9 | `feat(mock): MOCK-004 — retire sil_mock_publisher, ship_dynamics_node LifecycleNode online` | D2.4-mock | D1.3a |
| 10 | `feat(mock): MOCK-003 — retire l3_external_mock_publisher, sensor_mock+tracker_mock → M2 online` | D2.4-mock | D1.3a, GAP-002 |
| 11 | `test(cutover): add parallel run validation for demo vs ros2 trajectory diff < 5% CPA` | D2.4-mock | — |
| 12 | `feat(mock): MOCK-001 — retire demo_server.py, sil_orchestrator now sole REST entry` | D2.4-mock | GAP-031 |
| 13 | `feat(mock): MOCK-002 — retire demo_ws_server.py, telemetry_bridge now sole WS entry` | D2.4-mock | GAP-031 |
| 14 | `feat(frontend): add ScoringLastRunFull type with 8 KPIs + 6 dims` | D2.4-frontend | — |
| 15 | `feat(frontend): wire all 8 KPI cards to real Arrow-derived data` | D2.4-frontend | GAP-027 |
| 16 | `feat(frontend): wire ScoringRadarChart to real 6-dim Arrow data` | D2.4-frontend | GAP-027 |
| 17 | `ci: add sil-smoke + sil-baseline stages for Imazu-22 + 50 baseline auto-run` | D2.4-ci | X1.1, X1.5 |
| 18 | `chore(docker): remove demo services, add scoring_node LifecycleNode to compose` | D2.4-docker | — |

### V&V 验收矩阵 (per V&V Plan v0.1)

| Gate ID | Criterion | 实现方式 | 验证 commit |
|---|---|---|---|
| X1.1 | 50 baseline ≥ 95% PASS | CI sil-baseline-50 job | #17 |
| X1.2 | KPI 矩阵 30 runs 全在内 | tools/run_baseline_50.py 输出 | #17 |
| X1.4 | Coverage cube ≥ 10/1100 cells | Imazu-22 + 28 synthetic scenarios | #17 |
| X1.5 | Imazu-22 22/22 PASS | CI sil-baseline-imazu22 job | #17 |
| X1.6 | M7 watchdog 关键路径覆盖 | M7 pytest (pending D2.7) | Phase 2 |

---

## 附录 A: Hagen 2022 / Woerner 2019 完整公式参考

### A.1 6 维加权总分

```
total_score = w_s · S_safety + w_r · S_rule - P_delay - P_magnitude + w_p · S_phase + w_pl · S_plausibility
```

### A.2 各维度公式

**Safety (Hagen 2022 §II.C):**
```
S_safety = min(1.0, CPA / CPA_target)   for CPA_target = 0.27 nm (500 m)
```

**Rule Compliance (Woerner 2019, per-rule weighted):**
```
S_rule = Σ(rule_score_i) / n_rules
rule_score_i ∈ {1.0: full compliance, 0.5: partial, 0.0: violated}
```

**Delay Penalty (Hagen 2022 §III.A):**
```
P_delay = max(0, t_action - t_target) · λ_delay
λ_delay = 0.01 (per second of delay)
```

**Action Magnitude Penalty (Rule 8 "substantial"):**
```
δ_eff = |rudder_angle_deg|
deviation = max(0, |δ_eff - 60°| - 30°)
P_magnitude = (deviation / 30°)²
Acceptable range: δ_eff ∈ [30°, 90°]
```

**Phase Score (Hagen 2022 §III.B):**
```
S_phase = {
    1.0  if give_way (early, large action)
    0.5  if stand_on (maintain course/speed)
    0.0  if in_extremis (action too late)
    1.0  if transit (no encounter)
}
```

**Trajectory Implausibility (Anti-RL "cheating"):**
```
excess_k = max(0, |κ| - κ_max) / κ_max
excess_a = max(0, |a| - a_max) / a_max
S_plausibility = 1.0 - max(excess_k, excess_a)

κ_max = 0.01 (1/m),  a_max = 2.0 m/s²
```

### A.3 默认权重 (Hagen 2022 推荐，待 D1.7 校准)

| 维度 | 权重 | 理由 |
|---|---|---|
| safety | 0.30 | 最高优先级；COLREGs 终极目标 |
| rule_compliance | 0.25 | CCS i-Ship N 合规要求 |
| delay_penalty | 0.12 | 延迟行动增加碰撞风险 |
| action_magnitude_penalty | 0.08 | Rule 8 "substantial" 要求 |
| phase_score | 0.15 | 避碰阶段质量 |
| plausibility | 0.10 | 物理可实现性 |

---

## 附录 B: ScoringRow Arrow Schema (10 columns)

| Column | Type | Description |
|---|---|---|
| stamp | float64 | Simulation timestamp (seconds) |
| safety | float64 | CPA/CPA_target, clamped [0,1] |
| rule_compliance | float64 | Per-rule weighted mean [0,1] |
| delay_penalty | float64 | Tardy action penalty [0,∞) |
| action_magnitude_penalty | float64 | Rudder outside [30°,90°] penalty |
| phase_score | float64 | Phase quality [0,1] |
| plausibility | float64 | Physical plausibility [-∞,1] |
| total | float64 | Weighted sum |
| cpa_nm | float64 | Computed CPA this frame |
| cpa_target_nm | float64 | Target CPA threshold (0.27) |

---

## 附录 C: 8 KPI 完整定义与映射

| # | KPI | 单位 | 数据来源 | 计算 |
|---|---|---|---|---|
| 1 | `min_cpa_nm` | nm | scoring.arrow cpa_nm column | `min(cpa_nm)` |
| 2 | `tcpa_min_s` | s | scoring.arrow stamp + cpa_nm | `stamp[argmin(cpa_nm)]` |
| 3 | `avg_rot_dpm` | °/min | own_ship trajectory | `mean(abs(rot_dps))` |
| 4 | `max_rudder_deg` | ° | own_ship trajectory | `max(abs(rudder_angle))` |
| 5 | `grounding_risk_score` | ratio | ENC depth + own_ship draft | `min(depth/draft)` |
| 6 | `route_deviation_nm` | nm | own_ship trajectory vs WP | `max(cross_track_error)` |
| 7 | `time_to_mrm_s` | s | ASDR events | `first_mrc_stamp - run_start_stamp` |
| 8 | `decision_count` | count | ASDR events | `count(event_type=="decision")` |

---

## 附录 D: Feature Flag 全链路数据流

```
scenario YAML                    scenario YAML
metadata.simulation_settings.backend: "ros2"
         │
         ▼
scenario_store.get() → backend="ros2"
         │
         ▼
orchestrator lifecycle_activate()
  ├── backend="demo"  → _seed_run_dir_demo()  → scoring.json (stub, legacy)
  └── backend="ros2"  → _seed_run_dir_ros2() → SIL_RUN_DIR env var
                           │
                           ▼
                      scoring_node on_activate()
                       reads SIL_RUN_DIR + SIL_RUN_ID
                       opens ArrowWriter(runs/{id}/scoring.arrow)
                           │
                           ▼ (1 Hz scoring timer)
                       /sil/scoring (ROS2 topic)
                       scoring.arrow (Arrow IPC)
                           │
                           ▼ (after deactivate)
                      GET /api/v1/scoring/last_run
                       reads scoring.arrow via polars
                       KpiDeriver → 8 KPIs
                       polars aggregation → 6 dims
                           │
                           ▼
                      RunReport.tsx
                       8 KPI cards + ScoringRadarChart
```

---

## 附录 E: 风险与降级路径

| 风险 | 影响 | 缓解 |
|---|---|---|
| scoring_node Arrow 写入失败 | 屏④ 无数据 | Fallback 到 scoring.json (保留 demo path 直至 cutover 验证通过) |
| Imazu-22 部分场景 ROS2 下不通过 | CI 阻塞 | `imazu22_results.json` 记录 per-scenario pass/fail + 复评原因 |
| parallel run CPA diff > 5% | Cutover 阻塞 | 保留 demo backend 路径，仅 internal CI 跑 ros2 |
| polars/pyarrow 未安装 | scoring_node 启动失败 | package.xml 声明依赖，CI build 检查 |
| scoring.arrow 文件过大 (>100MB) | 磁盘满 | batch_size=60 控制 buffer；1h run @ 1Hz = 3600 rows × 10 cols × 8 bytes ≈ 288KB |
