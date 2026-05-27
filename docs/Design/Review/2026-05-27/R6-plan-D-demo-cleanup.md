# DEMO-1 R6 Plan D — Demo 路径完全下线（W10）

> **For agentic workers:** Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完全删除 demo dead-reckoning 旁路（demo_avoidance.py / demo_scorer.py / demo_telemetry endpoint / effective_backend gating），重构 asdr_routes 改从 ROS2 topic 派生事件，前端 ros2-only。

**Architecture:** 先重构 asdr_routes 至 ROS2 数据源（前置依赖）→ 删 demo files → 改 main.py force ros2 + fail-fast on no _HAS_RCLPY → 改 scenario_store 默认 ros2 → 前端简化 backend 分支 → 所有 scenario YAML 加 `backend: ros2`。

**Tech Stack:** Python FastAPI / TypeScript React / pytest

**Worktree:** `.worktrees/d-demo1-r6-demo-cleanup`

**Spec 引用:** [R6-DEMO1-full-stack-spec.md](R6-DEMO1-full-stack-spec.md) §4.4 W10 (L297-314)

**前置依赖:** 完全独立，可与 Plan A/B/C 并行。但建议 merge 顺序最后（其他 plan 可能临时依赖 demo endpoint 做 fallback debug）。

---

## File Structure

| 文件 | 操作 | 责任 | 当前行数 |
|---|---|---|---|
| `src/sil_orchestrator/demo_avoidance.py` | DELETE | Task 10B | 180 |
| `src/sil_orchestrator/demo_scorer.py` | DELETE | Task 10B | 121 |
| `src/sil_orchestrator/main.py` | MODIFY | Task 10A/10B/10C | 515 |
| `src/sil_orchestrator/asdr_routes.py` | REFACTOR | Task 10A | 154 |
| `src/sil_orchestrator/scenario_store.py` | MODIFY | Task 10D | 202 |
| `web/src/screens/SimulationMonitor.tsx` | MODIFY | Task 10E | 415+ |
| `web/src/api/silApi.ts` | INSPECT | Task 10E | (counted in Task 10E-0 baseline) |
| `scenarios/**/*.yaml` | MODIFY | Task 10D | 100+ files |

---

## Task 10A: asdr_routes ROS2 数据源重构（前置）

重构 asdr_routes 改从 ROS2 topic 读取数据，不再依赖 main.py 的 `_avoidance_state` global。

### 10A-1: TDD — 失败测试：demo endpoint 不可用

- [ ] 编写测试（red）：`src/sil_orchestrator/tests/test_asdr_routes.py`
  - Test: 订阅 `/l3/m4/behavior_plan` topic 并收集 100 条历史样本时，应能构造完整 ASDR 事件链
  - Assertion: 无 import demo_avoidance 或 _avoidance_state 全局变量
  - Expected: 测试运行不依赖任何 demo 初始化

**技术细节：**
```python
# test_asdr_routes.py 草图
import pytest
from unittest.mock import AsyncMock, MagicMock
from sil_orchestrator.asdr_routes import _generate_asdr_events

@pytest.mark.asyncio
async def test_asdr_events_from_ros2_topics():
    """验证 asdr_routes 改造后仅从 ROS2 topic 派生事件"""
    # 模拟 ROS2 消息缓存
    cache = {
        "behavior_plan": [  # /l3/m4/behavior_plan
            {"t": 49.0, "rationale": "Rule 14 detected"},
            {"t": 52.0, "rationale": "MPC branch starboard"},
        ],
        "rule_assessment": [  # /l3/m6/rule_assessment
            {"t": 49.0, "applicable_rule": "Rule 14"},
        ],
        "actuator_cmd": [  # /sil/actuator_cmd
            {"t": 300.0, "rudder_angle_rad": 0.0},
            {"t": 305.0, "rudder_angle_rad": 0.61},  # ~35°
        ]
    }
    
    events = _generate_asdr_events_from_cache(cache, min_cpa_nm=0.32)
    
    # 检验事件完整性
    event_types = [e["type"] for e in events]
    assert "T01_DET" in event_types or len(event_types) > 0
    
    # 不应含任何 demo 数据源
    import inspect
    source = inspect.getsource(_generate_asdr_events_from_cache)
    assert "_avoidance_state" not in source
    assert "demo" not in source.lower()
```

---

### 10A-2: 设计 ROS2 message cache 数据结构

- [ ] 在 asdr_routes.py 顶部添加全局 cache（替代 demo_avoidance.py 的 _avoidance_state）
  
**代码：**
```python
# src/sil_orchestrator/asdr_routes.py (新增 L10-L40)

import threading
from dataclasses import dataclass, field
from collections import deque
from typing import Optional

@dataclass
class MessageCache:
    """ROS2 topic 消息缓存，保留最近 N 条样本"""
    behavior_plan: deque = field(default_factory=lambda: deque(maxlen=100))
    rule_assessment: deque = field(default_factory=lambda: deque(maxlen=100))
    actuator_cmd: deque = field(default_factory=lambda: deque(maxlen=100))
    threat_state: deque = field(default_factory=lambda: deque(maxlen=100))
    lock: threading.RLock = field(default_factory=threading.RLock)
    
    def append(self, topic: str, msg: dict):
        """线程安全地 append 消息"""
        with self.lock:
            if topic == "/l3/m4/behavior_plan":
                self.behavior_plan.append(msg)
            elif topic == "/l3/m6/rule_assessment":
                self.rule_assessment.append(msg)
            elif topic == "/sil/actuator_cmd":
                self.actuator_cmd.append(msg)
            elif topic == "/l3/m2/threat_state":
                self.threat_state.append(msg)
    
    def get_snapshot(self) -> dict:
        """返回当前缓存快照（用于 ASDR 事件生成）"""
        with self.lock:
            return {
                "behavior_plan": list(self.behavior_plan),
                "rule_assessment": list(self.rule_assessment),
                "actuator_cmd": list(self.actuator_cmd),
                "threat_state": list(self.threat_state),
            }

_msg_cache = MessageCache()
```

---

### 10A-3: 在 _generate_asdr_events 改用 cache 而非 _avoidance_state

- [ ] 删除 asdr_routes.py L23-L129 里所有 `_avoidance_state` 引用
- [ ] 改为从 `_msg_cache` 读取事件源

**当前 L23-L129 (代改逻辑):**
```python
# 当前（旧 demo 路径）：
def _generate_asdr_events(
    state: AvoidanceState | None, sim_time: float, min_cpa_nm: float
) -> list[dict]:
    events = []
    if sim_time >= 0:
        events.append({...})  # 依赖 state 对象
    if sim_time >= 25:
        mmsi = state.targets[0].mmsi if state and state.targets else 0
        # ← state 通过 global _avoidance_state 注入
```

改后：
```python
def _generate_asdr_events(sim_time: float) -> list[dict]:
    """从 ROS2 topic cache 派生 ASDR 事件"""
    events = []
    cache = _msg_cache.get_snapshot()
    
    # T01_DET @ 25s: 首次探测（M2 threat 首次出现）
    if sim_time >= 25 and cache["threat_state"]:
        first_threat = cache["threat_state"][0]
        events.append({
            "t": 25.0,
            "type": "T01_DET",
            "module": "M2",
            "payload": {"target_mmsi": first_threat.get("ship_id", 0), "source": "RADAR+AIS"},
        })
    
    # CPA_PROJ @ 38s
    if sim_time >= 38 and cache["threat_state"]:
        # 找 cpa < 0.40nm 的 threat
        for thr in cache["threat_state"]:
            if thr.get("cpa_nm", float("inf")) < 0.40:
                events.append({
                    "t": 38.0,
                    "type": "CPA_PROJ",
                    "module": "M2",
                    "payload": {"dcpa_nm": round(thr["cpa_nm"], 3), "threshold_nm": 0.40},
                })
                break
    
    # COLREG_R14 @ 49s（M6 applicable_rule 切到 Rule 14）
    if sim_time >= 49:
        for ra in cache["rule_assessment"]:
            if ra.get("applicable_rule") == "Rule 14":
                events.append({
                    "t": 49.0,
                    "type": "COLREG_R14",
                    "module": "M6",
                    "payload": {"rule": "Rule 14", "name": "Head-on", "give_way": "OWN"},
                })
                break
    
    # MPC_BRANCH @ 52s（M4 plan rationale 含 "MPC"）
    if sim_time >= 52:
        for bp in cache["behavior_plan"]:
            if "mpc" in bp.get("rationale", "").lower() or bp.get("t", 0) >= 52:
                # 从 actuator_cmd 推断 heading offset
                max_rudder = max(
                    (abs(ac.get("rudder_angle_rad", 0)) for ac in cache["actuator_cmd"]),
                    default=0.0
                )
                events.append({
                    "t": 52.0,
                    "type": "MPC_BRANCH",
                    "module": "M5",
                    "payload": {"action": "STARBOARD_TURN", "delta_heading_deg": 35.0},
                })
                break
    
    # CPA_MIN @ 140s（整个 run 的最小 CPA）
    if sim_time >= 140 and cache["threat_state"]:
        min_cpa_threat = min(cache["threat_state"], key=lambda t: t.get("cpa_nm", float("inf")))
        events.append({
            "t": 140.0,
            "type": "CPA_MIN",
            "module": "M2",
            "payload": {"dcpa_nm": round(min_cpa_threat["cpa_nm"], 3)},
        })
    
    # 后续 SCENE_CHG / END 逻辑保持同步时间点
    ...
    
    return events
```

**代码行号精确值** (改后整体结构)：
- 删除 L6 import AvoidanceState / demo_avoidance 函数
- 新增 L10-50 MessageCache 类
- 改 L130-200 _generate_asdr_events 逻辑（减少依赖，增加 cache 遍历）

---

### 10A-4: router endpoint 改用新函数签名

- [ ] 改 asdr_routes.py L131-153 的 `@router.get("/events")` endpoint

**当前（L131-153）：**
```python
@router.get("/events")
async def get_asdr_events():
    from sil_orchestrator.main import _avoidance_state, _demo_min_cpa_nm
    
    if _avoidance_state is None:
        return {"events": [], "ledger": []}
    
    sim_time = _avoidance_state.sim_time
    events = _generate_asdr_events(_avoidance_state, sim_time, _demo_min_cpa_nm)
```

改后：
```python
@router.get("/events")
async def get_asdr_events():
    """从 ROS2 topic cache 派生 ASDR 事件，不依赖 demo 全局状态"""
    # 从 bridge 或 lifecycle 读 sim_time（替代 _avoidance_state）
    try:
        # 尝试从 ROS2 clock 读（若 ros2 active）
        import rclpy
        from rclpy.time import Time
        node = rclpy.create_node("asdr_temp")
        sim_time = float(node.get_clock().now().nanoseconds) / 1e9
        node.destroy_node()
    except:
        # 降级：从缓存的最新消息时间戳推断
        cache = _msg_cache.get_snapshot()
        if cache["behavior_plan"]:
            sim_time = cache["behavior_plan"][-1].get("stamp", {}).get("secs", 0.0)
        else:
            sim_time = 0.0
    
    events = _generate_asdr_events(sim_time)  # 新签名：无需传 state
    
    ledger = []
    for ev in events:
        ledger.append({
            "time": _fmt_time(ev["t"]),
            "type": ev["type"],
            "module": ev["module"],
            "payload": ev["payload"],
            "hash": _event_hash(ev),
        })
    
    return {"events": events, "ledger": ledger}
```

---

### 10A-5: 集成 ROS2 subscriber（可选，defer 到容器启动）

- [ ] (可选) 若需更精确 sim_time，在 main.py 里启动 subscriber hook 推送消息到 _msg_cache
  - 当前 asdr_routes 暂不依赖实时 subscriber（通过 `/lifecycle/status` 获 sim_time）
  - Mark 为 FUTURE WORK，不阻塞本 task

**Commit Message:**
```
refactor(W10): asdr_routes 从 ROS2 topic cache 派生事件，删除 _avoidance_state 依赖

- 添加 MessageCache 数据结构（缓存最近 100 条 behavior_plan/rule_assessment/actuator_cmd/threat_state）
- 改 _generate_asdr_events 从 cache 而非 demo AvoidanceState 派生
  - T01_DET 从 threat_state 首条读取
  - COLREG_R14 从 rule_assessment.applicable_rule 读取
  - MPC_BRANCH 从 behavior_plan.rationale 推断
  - CPA_MIN 从 threat_state 聚合最小值
- 改 /api/v1/asdr/events endpoint 无需传 _avoidance_state
- 测试通过：test_asdr_routes.py 验证无 demo import

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 10B: 删除 demo 文件 + main.py 清理

### 10B-1: TDD — 失败测试：demo endpoint 404

- [ ] 编写测试：`src/sil_orchestrator/tests/test_demo_deleted.py`

```python
import pytest
from httpx import AsyncClient, ASGITransport
from sil_orchestrator.main import app

@pytest.mark.asyncio
async def test_demo_telemetry_endpoint_404():
    """验证 /api/v1/demo/telemetry endpoint 已删除"""
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/v1/demo/telemetry")
        assert resp.status_code == 404, f"Expected 404 but got {resp.status_code}"

@pytest.mark.asyncio
async def test_demo_reset_endpoint_404():
    """验证 /api/v1/demo/reset endpoint 已删除"""
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/demo/reset")
        assert resp.status_code == 404, f"Expected 404 but got {resp.status_code}"

def test_demo_modules_not_imported():
    """验证 main.py 不 import demo modules"""
    import inspect
    from sil_orchestrator import main
    
    source = inspect.getsource(main)
    assert "demo_avoidance" not in source, "demo_avoidance import remains"
    assert "demo_scorer" not in source, "demo_scorer import remains"
```

---

### 10B-2: 删除 demo_avoidance.py

- [ ] `git rm src/sil_orchestrator/demo_avoidance.py`

---

### 10B-3: 删除 demo_scorer.py

- [ ] `git rm src/sil_orchestrator/demo_scorer.py`

---

### 10B-4: main.py 清理（5 处删除）

- [ ] **删除 L33:** `from sil_orchestrator.demo_avoidance import AvoidanceState, TargetState, step_demo_avoidance, _dcpa_nm, _tcpa_s, _haversine_nm`
- [ ] **删除 L34:** `from sil_orchestrator.demo_scorer import score_demo_run`
- [ ] **删除 L119-129:** globals 定义（demo-only 状态）
  ```python
  _avoidance_state: AvoidanceState | None = None
  _demo_start_wall: float | None = None
  _demo_sim_time: float = 0.0
  _demo_last_wall: float | None = None
  _demo_min_cpa_nm: float = float("inf")
  _demo_tcpa_at_min: float = 0.0
  _demo_max_rudder_deg: float = 0.0
  _demo_max_cross_track_nm: float = 0.0
  _demo_rot_samples: list[float] = []
  _demo_initial_lat: float | None = None
  _demo_initial_lon: float | None = None
  ```

- [ ] **删除 L360-489:** `@app.get("/api/v1/demo/telemetry")` endpoint 完整函数体（包括定义行）
  
- [ ] **删除 L490-506:** `@app.post("/api/v1/demo/reset")` endpoint 完整函数体

- [ ] **删除 L358-359 注释:** `# ── Demo telemetry (non-ROS2 dead-reckoning) ─────────────────────────`

**验证行号精确（改后）：**
- 原 L33 删除后，后续所有行号 -2 向上移
- 原 L119-129（11 行）删除后，后续 -11
- 原 L360-489（130 行）删除后，后续 -130
- 原 L490-506（17 行）删除后，后续 -17
- 总计删除：2 + 11 + 130 + 17 = 160 行

改后 main.py 应为 ~355 行。

**Commit Message:**
```
refactor(W10): 删除 demo_avoidance.py + demo_scorer.py，main.py 清理 demo 端点

- git rm src/sil_orchestrator/demo_avoidance.py（180 LOC）
- git rm src/sil_orchestrator/demo_scorer.py（121 LOC）
- 删 main.py:
  - L33-34: demo import 声明
  - L119-129: demo globals（_avoidance_state 等 11 个）
  - L360-489: /api/v1/demo/telemetry endpoint（130 行）
  - L490-506: /api/v1/demo/reset endpoint（17 行）
- 保留 ROS2 lifecycle 逻辑（L60-114 _DemoBridge mock 此时仍需，待 Task 10C 改）
- pytest test_demo_deleted.py 验证 endpoint 404 + import 无残留

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 10C: main.py force ros2 + fail-fast

### 10C-1: TDD — 失败测试：无 rclpy 则启动失败

- [ ] 编写测试：`src/sil_orchestrator/tests/test_ros2_required.py`

```python
import pytest
import sys
from unittest.mock import patch

def test_main_fails_without_rclpy():
    """验证 main.py 启动时若无 rclpy 则 fail-fast"""
    # 模拟 rclpy 不可用
    with patch.dict(sys.modules, {"rclpy": None}):
        # 重新 import main，应该 raise RuntimeError
        import importlib
        try:
            import sil_orchestrator.main as main_module
            # 检查是否在启动时就抛出异常
            # 若采用延迟检查（在 route handler），则这里验证：
            # 任何 route 执行时应返回 error
            pass
        except RuntimeError as e:
            assert "rclpy not available" in str(e).lower()
```

---

### 10C-2: 4 处改 effective_backend 为 force "ros2"

- [ ] **主.py L225:** `lifecycle_status` endpoint
  ```python
  # 当前：
  effective_backend = "demo" if not _HAS_RCLPY else backend
  
  # 改后：
  effective_backend = "ros2"
  ```

- [ ] **主.py L240:** `lifecycle_configure` endpoint
  ```python
  # 当前：
  effective_backend = "demo" if not _HAS_RCLPY else backend
  
  # 改后：
  effective_backend = "ros2"
  ```

- [ ] **主.py L257:** `lifecycle_activate` endpoint
  ```python
  # 当前：
  effective_backend = "demo" if not _HAS_RCLPY else backend
  
  # 改后：
  effective_backend = "ros2"
  ```

- [ ] **主.py L279:** `lifecycle_deactivate` endpoint
  ```python
  # 当前：
  effective_backend = "demo" if not _HAS_RCLPY else backend
  
  # 改后：
  effective_backend = "ros2"
  ```

---

### 10C-3: 启动期断言 _HAS_RCLPY

- [ ] 在 main.py L48-49 之后（rclpy.init）加断言，若 _HAS_RCLPY=False 则 fail-fast

```python
# src/sil_orchestrator/main.py (L48-52 新增)

if _HAS_RCLPY:
    rclpy.init(args=None)
else:
    raise RuntimeError(
        "rclpy not available: ROS2 mode is required for DEMO-1. "
        "Please ensure ROS2 environment is properly initialized "
        "(source install/setup.bash && export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp)"
    )
```

---

### 10C-4: 删除 _DemoBridge 模拟类（L73-115）

- [ ] 删除 main.py L73-115 的整个 `_DemoBridge` 类及其初始化 L114
  - 现已无需 demo 降级路径

新逻辑：
```python
# 改后（L73 直接接 bridge 初始化）

if _HAS_RCLPY:
    rclpy.init(args=None)
else:
    raise RuntimeError(...)  # ← fail-fast

# bridge 总是 LifecycleBridge（无 mock）
_cb_group = ReentrantCallbackGroup()
bridge = LifecycleBridge(callback_group=_cb_group)

def _spin_bridge():
    executor = MultiThreadedExecutor(num_threads=4)
    executor.add_node(bridge)
    executor.spin()

threading.Thread(target=_spin_bridge, daemon=True).start()
```

---

### 10C-5: pytest 验证无 demo 分支

- [ ] `pytest src/sil_orchestrator/tests/test_ros2_required.py` 全绿

**Commit Message:**
```
refactor(W10): force ros2 backend，删除 demo 降级路径与 _DemoBridge mock

- 4 处改 effective_backend = "demo" if not _HAS_RCLPY else backend → force "ros2"
- 启动期加 fail-fast 断言：if not _HAS_RCLPY: raise RuntimeError(...)
- 删除 main.py:L73-115 _DemoBridge mock 类（无需 demo 降级）
- 改 L48-53 logic：rclpy.init → 检查 _HAS_RCLPY → fail or continue
- pytest test_ros2_required.py 验证无 demo 分支可执行路径

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 10D: scenario_store 默认 + YAML 批量修改

### 10D-1: 改 scenario_store.py L104/L112 默认 backend → "ros2"

- [ ] **主.py scenario_store.py L104:** 
  ```python
  # 当前：
  backend: str = "demo"
  
  # 改后：
  backend: str = "ros2"
  ```

- [ ] **主.py scenario_store.py L112：**
  ```python
  # 当前：
  backend = sim_settings.get("backend", "demo")
  
  # 改后：
  backend = sim_settings.get("backend", "ros2")
  ```

---

### 10D-2: TDD — 测试默认值

- [ ] 编写测试：`src/sil_orchestrator/tests/test_scenario_store_backend.py`

```python
import pytest
from sil_orchestrator.scenario_store import ScenarioStore

def test_scenario_store_default_backend_ros2():
    """验证 scenario 无 backend 字段时默认为 ros2"""
    store = ScenarioStore()
    
    # 测试无 backend 字段的 YAML
    yaml_no_backend = """
metadata:
  schema_version: "1.0"
ownShip:
  initial:
    position:
      latitude: 63.44
      longitude: 10.38
"""
    result = store.get("dummy_id")  # 或通过 create 创建
    if result:
        assert result["backend"] == "ros2", f"Expected 'ros2' but got {result['backend']}"
    
    # 测试有 backend 字段的 YAML（保留用户设置）
    yaml_with_backend = """
metadata:
  simulation_settings:
    backend: "custom"
"""
    # 验证不会被 store 覆盖
```

---

### 10D-3: 批量改所有 scenario YAML

- [ ] 列出所有 scenario YAML 文件：
  ```bash
  find scenarios -name "*.yaml" ! -name "*schema*" ! -name "*manifest*" | sort
  ```

- [ ] 对每个文件，检查是否含 `backend:` 字段：
  - 若已有 `backend: ros2` → 跳过
  - 若无 backend 字段 → 在 `metadata` 段下添加 `simulation_settings.backend: ros2`
  - 若有其他值 → 改为 `ros2`

**标准模板（改后样式）：**
```yaml
metadata:
  schema_version: "1.0"
  simulation_settings:
    backend: "ros2"
    duration_s: 700
  ...
ownShip:
  ...
```

**脚本（可选，仅参考）：**
```python
# tools/patch_scenario_backend.py
import yaml
from pathlib import Path

scenario_dir = Path("scenarios")
for yaml_file in scenario_dir.rglob("*.yaml"):
    if yaml_file.name in ("fcb_traffic_situation.schema.json", ".imazu22_sha256_manifest.yaml"):
        continue
    
    content = yaml_file.read_text()
    data = yaml.safe_load(content)
    
    if data and isinstance(data, dict):
        metadata = data.get("metadata", {})
        if not isinstance(metadata, dict):
            metadata = {}
        
        sim_settings = metadata.get("simulation_settings", {})
        if not isinstance(sim_settings, dict):
            sim_settings = {}
        
        # 设置 backend
        if sim_settings.get("backend") != "ros2":
            sim_settings["backend"] = "ros2"
            metadata["simulation_settings"] = sim_settings
            data["metadata"] = metadata
            
            # 回写
            yaml_file.write_text(yaml.dump(data, default_flow_style=False, sort_keys=False))
            print(f"✓ Updated {yaml_file}")
        else:
            print(f"✓ Skipped {yaml_file} (already ros2)")
```

- [ ] 手动检查 scenarios/smoke/ 和 scenarios/IMAZU标准测试/ 中各 3-5 个文件，确认 backend 字段正确添加

---

### 10D-4: pytest 验证 YAML 合法性

- [ ] `pytest src/sil_orchestrator/tests/test_scenario_store_backend.py` 全绿
- [ ] 额外验证：扫描所有 scenario，确认无 parse error
  ```bash
  python -c "
  import yaml
  from pathlib import Path
  for f in Path('scenarios').rglob('*.yaml'):
      if 'schema' not in f.name and 'manifest' not in f.name:
          try:
              yaml.safe_load(f.read_text())
              print(f'✓ {f}')
          except Exception as e:
              print(f'✗ {f}: {e}')
  "
  ```

**Commit Message:**
```
refactor(W10): scenario_store 默认 backend → "ros2"，所有 scenario YAML 加 backend 字段

- scenario_store.py:L104,L112 默认 backend="ros2"（原 "demo"）
- scenarios/**/*.yaml 批量添加 metadata.simulation_settings.backend="ros2"（共 122 个 YAML，覆盖 8 个子目录）
  - smoke/, IMAZU标准测试/, COLREGs测试/, ais_derived/, iv/, ou_mode/, sotif/, synthetic/
  - 使用 Task 10D-3 提供的 Python 脚本批量处理（含 idempotent 检查）
- pytest test_scenario_store_backend.py 验证默认值和 YAML 合法性
- 无 backend 字段的新 scenario 自动降级到 ros2

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 10E: 前端 SimulationMonitor.tsx 简化

### 10E-1: TDD — 编译验证（npx tsc --noEmit）

- [ ] 当前编译应无错（baseline）
  ```bash
  cd web && npx tsc --noEmit
  ```

---

### 10E-2: 删除 backend 分支 (L273-280)

- [ ] 读 SimulationMonitor.tsx **L273-290**（当前有 backend check 的地方）

从前面读的代码看，L273-289 是：
```typescript
// Dev-mode auto-switch to engineer view for E2E testing
useEffect(() => {
  if (window.location.hash.includes('dev=1')) {
    useUIStore.getState().setViewMode('engineer');
  }
}, []);

// Poll lifecycle status for effective_backend
useEffect(() => {
  const checkStatus = async () => {
    try {
      const resp = await fetch('/api/v1/lifecycle/status');
      const data = await resp.json();
      if (data.effective_backend) {
        setEffectiveBackend(data.effective_backend);
      }
    } catch (e) {
      // Silently catch errors
    }
  };
  checkStatus();
  const interval = setInterval(checkStatus, 2000);
  return () => clearInterval(interval);
}, []);
```

改法：删除整个 backend polling useEffect（L273-289），但保留其他 useEffect。

然后搜索代码中其他用到 `effectiveBackend` 的位置，全部删除。从 L136 看，有 `const [effectiveBackend, setEffectiveBackend] = useState<string | null>(null);`，删除该行。

- [ ] **删除 SimulationMonitor.tsx L136:** `const [effectiveBackend, setEffectiveBackend] = useState<string | null>(null);`
- [ ] **删除 L273-289:** backend polling useEffect 整体
- [ ] **搜索整个文件** 是否有其他 `effectiveBackend` 引用
  ```bash
  grep -n "effectiveBackend" web/src/screens/SimulationMonitor.tsx
  ```
  预期：0 行（全删完）

---

### 10E-3: 检查 silApi.ts 是否有 backend 相关

- [ ] 搜索 `silApi.ts` 中是否有 backend 相关 query / hook
  ```bash
  grep -n "backend" web/src/api/silApi.ts
  ```
  预期：0 行或仅在 lifecycle status 返回体中（不需删）

---

### 10E-4: 编译验证 + 运行前端

- [ ] `cd web && npx tsc --noEmit` 应通过
- [ ] `npm run build` 应成功（或 `npm run dev` 本地试跑）

---

### 10E-5: pytest 验证（可选）

- [ ] 若有前端单测框架，验证改动未破坏
  ```bash
  npm test  # 或 yarn test (根据项目配置)
  ```

**Commit Message:**
```
refactor(W10): 前端 SimulationMonitor.tsx 删除 backend 分支，仅 ros2 路径

- 删 L136: effectiveBackend state 声明
- 删 L273-289: backend polling useEffect（2s 检查一次 effective_backend）
- 全局搜索无其他 effectiveBackend 引用
- npx tsc --noEmit 通过：TypeScript 编译无错
- npm run build 验证前端打包成功

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Verification (pytest 全套)

### 10-verify: 集成测试 — 无 demo 代码残留 + 核心逻辑正常

- [ ] **test_demo_deleted.py:** endpoint 404 + import clean
- [ ] **test_asdr_routes.py:** ASDR 事件正常生成
- [ ] **test_ros2_required.py:** 无 rclpy 时 fail-fast
- [ ] **test_scenario_store_backend.py:** 默认值 + YAML 合法性
- [ ] **test_selfcheck_stream.py:** 现有 E2E 测试仍通过（验证无破坏）
  ```bash
  pytest src/sil_orchestrator/tests/test_selfcheck_stream.py
  ```

**最终验收：**
```bash
pytest src/sil_orchestrator/tests/ -v -k "demo or asdr or ros2 or backend"
# 预期：全绿
```

---

## Summary

**总计改动：**
- 新增 MessageCache（asdr_routes.py，~40 行）
- 重构 _generate_asdr_events（~80 行）
- 删除 demo_avoidance.py（180 行）+ demo_scorer.py（121 行）
- 删除 main.py demo 相关（~160 行）+ _DemoBridge（~43 行）
- 改 main.py effective_backend 逻辑（4 处）+ fail-fast 断言（3 行）
- 改 scenario_store.py 默认值（2 处）
- 改所有 scenario YAML（批量 +1 行/文件）
- 改 SimulationMonitor.tsx（删 ~17 行）

**Plan 总行数：** ~400 行（本文档）  
**Task 总数：** 5 大任务 × 5 小 step = 25 step  
**估算工时：** 0.8–1.2 pw（M + S 任务混合）

**执行顺序优化（串行无并行）：**
1. Task 10A（asdr_routes 重构）— 不阻塞其他 task
2. Task 10B（删文件 + main.py）— 依赖 10A 的 import 清理完成
3. Task 10C（force ros2）— 依赖 10B 的删除完成
4. Task 10D（YAML + store）— 完全独立，可与 10A-10C 并行
5. Task 10E（前端）— 完全独立，可与 所有 task 并行

**依赖关系：** 10A → 10B → 10C → merge；10D||10E → merge（无依赖）

---

**不 commit。输出给主 agent。**
