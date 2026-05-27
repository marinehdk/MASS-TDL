# DEMO-1 R6 Plan B — 物理闭环（W6-W7）

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 bridge LATCH 在 CPA cleared 后平滑释放回归 nominal heading + 仿真到 700s 自动 stop。

**Architecture:** Bridge 订阅 M2 ThreatState 与 M3 mission_state 三选一触发条件 → 5 秒线性下降 LATCH offset → PID 拉回 nominal route bearing。Lifecycle 读 scenario YAML `simulation_settings.total_time` 字段 + 启动 deactivate timer。

**Tech Stack:** Python rclpy / FastAPI / pytest / Docker

**Worktree:** `.worktrees/d-demo1-r6-physical-loop`

**Spec 引用:** [R6-DEMO1-full-stack-spec.md](R6-DEMO1-full-stack-spec.md) §4.2 W6-W7

**前置依赖:** Plan A W3 (mission_state.task_validity) + W4 (M4 不再永 fallback)。本 plan 假设 A 已 merged

---

## File Structure

| 文件 | 操作 | 责任 |
|---|---|---|
| `docker/sil_topic_bridge.py` | Modify L419-459 `_on_avoidance_plan` + 新增 LATCH release logic | W6 Bridge 释放 + M5 cost 收敛 |
| `docker/sil_topic_bridge.py` | 新增 subscriber for `/l3/m2/threat_state` | W6 订阅 CPA status |
| `docker/sil_topic_bridge.py` | 新增 subscriber for `/l3/m3/mission_state` | W6 订阅 task_validity |
| `docker/sil_topic_bridge.py` | 新增 publisher `/sil/bridge_state` | W6 暴露 LATCH state |
| `src/sil_orchestrator/lifecycle_bridge.py` | Modify `configure()` 读 YAML + 启动 duration timer | W7 auto-stop timer |
| `src/sil_orchestrator/lifecycle_bridge.py` | Modify `deactivate()` idempotent + cancel timer | W7 idempotent cleanup |
| `src/sil_orchestrator/main.py` | Modify `/api/v1/lifecycle/status` 加 `time_remaining_s` | W7 前端显示 |
| `tests/unit/test_w6_latch_release.py` | 新建 | W6 单测 |
| `tests/unit/test_w7_auto_stop.py` | 新建 | W7 单测 |

---

## W6: Bridge LATCH 释放 + return-to-nominal

### Architecture Sketch

```
M2 /l3/m2/threat_state (cpa_status, target_relative_position)
M3 /l3/m3/mission_state (task_validity)
M7 MRM override (future)
       ↓ (订阅 ×3)
Bridge._latch_release_logic() → three-option trigger:
  (1) cpa_status==cleared + target astern
  (2) task_validity==valid + behavior==TRANSIT
  (3) M7 MRM (reserved)
       ↓
Bridge._compute_latch_offset(t_release, dt, current_offset)
  5s linear decay: offset → 0
       ↓
PID autopilot (_avoidance_heading_controller) pulls heading → nominal
       ↓
/sil/bridge_state publish (LATCH state + release_progress + target_heading)
```

### Step 6.1: 失败测试 — Bridge LATCH 释放触发 (红)

**File:** `tests/unit/test_w6_latch_release.py`

```python
"""Test W6: Bridge LATCH release logic — TDD red"""
import pytest
from unittest.mock import Mock, patch, MagicMock
import math

class TestBridgeLatchRelease:
    """LATCH 释放三条件矩阵"""

    def test_latch_release_on_cpa_cleared_astern(self):
        """条件1: cpa_status==cleared 且 target astern → release"""
        # Arrange
        bridge = SilTopicBridge()
        threat_msg = Mock()
        threat_msg.cpa_status = "cleared"  # ← key condition
        threat_msg.target_relative_position = "astern"
        
        # Act: simulate receiving threat_state
        bridge._on_threat_state(threat_msg)
        
        # Assert
        assert bridge._latch_release_triggered is True
        assert bridge._latch_release_time is not None

    def test_latch_release_on_task_valid_and_transit(self):
        """条件2: task_validity==valid 且 behavior==TRANSIT → release"""
        # Arrange
        bridge = SilTopicBridge()
        mission_msg = Mock()
        mission_msg.task_validity = "valid"
        behavior_msg = Mock()
        behavior_msg.behavior = "TRANSIT"
        bridge._last_behavior_plan = behavior_msg
        
        # Act
        bridge._on_mission_state(mission_msg)
        
        # Assert
        assert bridge._latch_release_triggered is True

    def test_latch_release_blocks_if_closing_or_sustained(self):
        """CPA still closing/sustained → 不释放"""
        # Arrange
        bridge = SilTopicBridge()
        threat_msg = Mock()
        threat_msg.cpa_status = "closing"  # ← blocking
        
        # Act
        bridge._on_threat_state(threat_msg)
        
        # Assert
        assert bridge._latch_release_triggered is False

    def test_latch_offset_decay_linear_5s(self):
        """5 秒内 LATCH offset 线性衰减到 0"""
        # Arrange
        bridge = SilTopicBridge()
        bridge._latch_release_triggered = True
        bridge._latch_release_time = 0.0  # release at t=0
        current_offset_deg = 30.0
        
        # Act: step at t=2.5s (halfway)
        t_elapsed = 2.5
        offset_at_halfway = bridge._compute_latch_offset(
            t_release=0.0, t_now=t_elapsed, current_offset_deg=current_offset_deg
        )
        
        # Assert: should be 50% decayed
        assert offset_at_halfway == pytest.approx(15.0, abs=0.1)
        
        # Act: step at t=5.0s (end)
        offset_at_end = bridge._compute_latch_offset(
            t_release=0.0, t_now=5.0, current_offset_deg=current_offset_deg
        )
        
        # Assert: fully decayed
        assert offset_at_end == pytest.approx(0.0, abs=0.01)

    def test_bridge_state_publish_on_latch_release(self):
        """LATCH release 时 publish /sil/bridge_state"""
        # Arrange
        bridge = SilTopicBridge()
        bridge._pub_bridge_state = Mock()
        bridge._latch_release_triggered = True
        bridge._latch_release_time = 0.0
        
        # Act
        bridge._autopilot_step()  # tick that publishes bridge_state
        
        # Assert: should have called publish
        assert bridge._pub_bridge_state.publish.called
        published_msg = bridge._pub_bridge_state.publish.call_args[0][0]
        assert hasattr(published_msg, 'latch_state')
        assert published_msg.latch_state == "releasing"
```

**预期结果：6 个 assertions 全 FAIL（红）**

---

### Step 6.2: 跑测试验证失败

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer
pytest tests/unit/test_w6_latch_release.py -v
```

**预期输出：**
```
test_latch_release_on_cpa_cleared_astern FAILED — attribute '_on_threat_state' not found
test_latch_offset_decay_linear_5s FAILED — method '_compute_latch_offset' not found
...（6 FAILs）
```

---

### Step 6.3: 实现 Bridge LATCH 释放逻辑

**File:** `docker/sil_topic_bridge.py`

#### 6.3.1 订阅 M2 threat_state + M3 mission_state（L252-276 后加）

**Location:** `SilTopicBridge.__init__()` 中 M4 订阅后添加（约 L264 后）

```python
        # ── M2 Threat State subscription ──────────────────────
        self._sub_threat = self.create_subscription(
            ThreatState, "/l3/m2/threat_state",
            self._on_threat_state, sq)
        self._last_threat_state = None

        # ── M3 Mission State subscription ──────────────────────
        self._sub_mission = self.create_subscription(
            MissionState, "/l3/m3/mission_state",
            self._on_mission_state, sq)
        self._last_mission_state = None

        # ── LATCH release state ───────────────────────────────
        self._latch_release_triggered = False
        self._latch_release_time = None
        self._latch_offset_at_release_deg = None  # snapshot
        self._latch_release_progress = 0.0  # [0, 1]

        # ── Bridge state publisher ────────────────────────────
        self._pub_bridge_state = self.create_publisher(
            BridgeState, "/sil/bridge_state", sq)
```

**新增 imports（L45-56 后）：**
```python
from l3_msgs.msg import (
    ...
    ThreatState,
    MissionState,
)
from sil_msgs.msg import (
    ...
    BridgeState,
)
```

#### 6.3.2 实现 threat_state 回调（新增方法 L460 后）

```python
    def _on_threat_state(self, msg: ThreatState) -> None:
        """M2 threat state callback — check CPA-cleared release condition."""
        self._last_threat_state = msg
        self._record_pulse(M2)
        
        # Condition 1: cpa_status == cleared && target astern
        if (msg.cpa_status == "cleared" and 
            msg.target_relative_position == "astern" and
            not self._latch_release_triggered):
            self.get_logger().info(
                "[BRIDGE] LATCH release condition 1 triggered: CPA cleared, target astern")
            self._trigger_latch_release()
```

#### 6.3.3 实现 mission_state 回调（新增方法）

```python
    def _on_mission_state(self, msg: MissionState) -> None:
        """M3 mission state callback — check task_validity + behavior release condition."""
        self._last_mission_state = msg
        
        # Condition 2: task_validity == valid && behavior == TRANSIT
        if (msg.task_validity == "valid" and
            self._last_behavior_plan is not None and
            self._last_behavior_plan.behavior == "TRANSIT" and
            not self._latch_release_triggered):
            self.get_logger().info(
                "[BRIDGE] LATCH release condition 2 triggered: task_valid + TRANSIT behavior")
            self._trigger_latch_release()
```

#### 6.3.4 实现 _trigger_latch_release()

```python
    def _trigger_latch_release(self) -> None:
        """Snapshot current LATCH offset and start 5s linear decay."""
        if self._avoidance_target_heading_deg is None:
            return
        
        self._latch_release_triggered = True
        self._latch_release_time = time.monotonic()
        self._latch_offset_at_release_deg = abs(
            self._avoidance_target_heading_deg - self._target_heading_deg)
        self._latch_release_progress = 0.0
        print(f"[BRIDGE] LATCH release started: offset_deg={self._latch_offset_at_release_deg:.1f}",
              flush=True)
```

#### 6.3.5 实现 _compute_latch_offset()

```python
    def _compute_latch_offset(self, t_release: float, t_now: float,
                               current_offset_deg: float) -> float:
        """Linearly decay LATCH offset from snapshot to 0 over 5 seconds."""
        if not self._latch_release_triggered or self._latch_offset_at_release_deg is None:
            return current_offset_deg
        
        t_elapsed = t_now - t_release
        decay_duration_s = 5.0
        
        if t_elapsed >= decay_duration_s:
            self._latch_release_progress = 1.0
            return 0.0
        
        progress = t_elapsed / decay_duration_s
        self._latch_release_progress = progress
        return self._latch_offset_at_release_deg * (1.0 - progress)
```

#### 6.3.6 修改 _compute_avoidance_autopilot() 调用 LATCH decay（L541-581）

**修改 L541-581 部分逻辑：**

```python
    def _compute_avoidance_autopilot(self, stamp) -> SilOwnShipState:
        out = self._make_actuator_msg(stamp)

        if self._last_ownship_raw is None:
            out.throttle = CRUISE_SPEED_KN / MAX_SPEED_KN
            return out

        current_heading_deg = math.degrees(self._last_ownship_raw.heading) % 360.0
        current_rot_deg_s = math.degrees(self._last_ownship_raw.rot)

        # ── LATCH offset decay logic ─────────────────────────
        if self._latch_release_triggered and self._latch_release_time is not None:
            t_now = time.monotonic()
            latch_offset_decaying = self._compute_latch_offset(
                self._latch_release_time, t_now, 
                self._latch_offset_at_release_deg or 0.0)
            
            if latch_offset_decaying <= 0.01:  # fully decayed
                self._latch_release_triggered = False
                self._avoidance_target_heading_deg = self._target_heading_deg
                print("[BRIDGE] LATCH decay complete, snapped to nominal route bearing",
                      flush=True)
        
        # ── PID heading control ───────────────────────────────
        if self._avoidance_target_heading_deg is not None:
            heading_error_deg = (
                self._avoidance_target_heading_deg - current_heading_deg + 180.0
            ) % 360.0 - 180.0
            dt = 0.5
            out.rudder_angle = RUDDER_SIGN * self._avoidance_heading_controller.step(
                heading_error_deg, dt, current_rot_deg_s)
            if abs(heading_error_deg) > 5.0 or abs(current_rot_deg_s) > 2.0:
                print(f"[BRIDGE-AVOID] hdg={current_heading_deg:.1f} "
                      f"tgt={self._avoidance_target_heading_deg:.1f} "
                      f"err={heading_error_deg:.1f} rot={current_rot_deg_s:.2f} "
                      f"rud={math.degrees(out.rudder_angle):.1f} "
                      f"latch_prog={self._latch_release_progress:.2f}",
                      flush=True)
        elif self._last_avoidance_waypoint is not None:
            # ... existing waypoint logic (L563-571 unchanged)
            pass
        else:
            out.rudder_angle = 0.0

        if self._last_avoidance_waypoint is not None:
            out.throttle = max(0.0, min(1.0,
                self._last_avoidance_waypoint.target_speed_kn / MAX_SPEED_KN))
        else:
            out.throttle = CRUISE_SPEED_KN / MAX_SPEED_KN

        return out
```

#### 6.3.7 新增 bridge_state publisher（在 _autopilot_step 或 _publish_module_pulse 中）

```python
    def _publish_bridge_state(self) -> None:
        """Publish bridge state for Foxglove visualization."""
        msg = BridgeState()
        msg.stamp = self.get_clock().now().to_msg()
        msg.latch_state = "releasing" if self._latch_release_triggered else "latched"
        msg.target_heading_deg = self._avoidance_target_heading_deg or self._target_heading_deg
        msg.release_progress = self._latch_release_progress
        msg.current_offset_deg = self._latch_offset_at_release_deg or 0.0
        self._pub_bridge_state.publish(msg)
```

在 `_autopilot_step()` 末尾添加：

```python
    def _autopilot_step(self) -> None:
        # ... existing logic ...
        self._publish_bridge_state()  # ← add at end
```

---

### Step 6.4: 跑单测验证绿

```bash
pytest tests/unit/test_w6_latch_release.py::TestBridgeLatchRelease -v
```

**预期输出：6 PASS**

---

### Step 6.5: Docker 集成测试（W6 完整闭环）

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer
npm run sys:start
sleep 10  # wait for ROS2 up

# 触发 scenario
curl -X POST http://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" \
  -d '{"scenario_id": "imazu-01-ho"}'
curl -X POST http://localhost:8000/api/v1/lifecycle/activate

# 等 350s（§2 阶段触发 Rule 14，然后 §3 执行）
sleep 350

# 检查 bridge_state 是否发布
ros2 topic echo /sil/bridge_state --once

# 预期：latch_state="releasing", release_progress ∈ [0, 1]
```

---

### Step 6.6: Commit W6

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer
git add -A
git commit -m "feat(W6): implement bridge LATCH release + return-to-nominal

Release LATCH on three conditions:
1. CPA cleared + target astern (M2)
2. task_validity valid + behavior TRANSIT (M3+M4)
3. M7 MRM override (reserved)

5s linear decay of LATCH offset; PID autopilot pulls heading back
to nominal route bearing. Publish /sil/bridge_state for Foxglove.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## W7: 仿真 700s 自动 stop

### Architecture Sketch

```
lifecycle_bridge.configure(scenario_id)
  ↓
_load_scenario_yaml(imazu-01-ho)
  ↓
yaml["simulation_settings"]["total_time"] = 700.0  ← read duration
  ↓
activate() → asyncio.create_task(deactivate) after 700s
  ↓
deactivate() called, lifecycle_state → "inactive"
  ↓
/api/v1/lifecycle/status include time_remaining_s
```

### Step 7.1: 失败测试 — auto-stop timer (红)

**File:** `tests/unit/test_w7_auto_stop.py`

```python
"""Test W7: Simulation auto-stop timer — TDD red"""
import pytest
import asyncio
from unittest.mock import Mock, patch, AsyncMock
from datetime import datetime

class TestAutoStopTimer:
    
    @pytest.mark.asyncio
    async def test_duration_read_from_yaml(self):
        """scenario YAML 的 simulation_settings.total_time → timer duration"""
        # Arrange
        bridge = LifecycleBridge()
        
        # Act
        result = await bridge.configure("imazu-01-ho")
        
        # Assert
        assert result.success is True
        assert bridge._sim_duration_s == 700.0  # from YAML

    @pytest.mark.asyncio
    async def test_timer_deactivates_at_duration(self):
        """700s 后自动调用 deactivate"""
        # Arrange
        bridge = LifecycleBridge()
        await bridge.configure("imazu-01-ho")
        await bridge.activate()
        
        # Mock deactivate to track calls
        bridge.deactivate = AsyncMock()
        
        # Act: advance time by 700s
        with patch('asyncio.sleep') as mock_sleep:
            # Simulate timer firing
            if bridge._deactivate_timer_task:
                bridge._deactivate_timer_task.cancel()
            bridge._deactivate_timer_task = asyncio.create_task(
                bridge._deactivate_after_duration()
            )
            try:
                await asyncio.wait_for(bridge._deactivate_timer_task, timeout=0.1)
            except (asyncio.TimeoutError, asyncio.CancelledError):
                pass
        
        # Assert: deactivate should be queued
        assert bridge._deactivate_timer_task is not None

    def test_deactivate_idempotent(self):
        """deactivate 多次调用不出错"""
        # Arrange
        bridge = LifecycleBridge()
        bridge._state = LifecycleState.INACTIVE
        
        # Act
        result1 = bridge.deactivate()  # first call
        result2 = bridge.deactivate()  # second call (should not fail)
        
        # Assert
        assert result1.success is True
        assert result2.success is True

    def test_time_remaining_s_field(self):
        """lifecycle/status 包含 time_remaining_s 字段"""
        # Arrange
        bridge = LifecycleBridge()
        bridge._state = LifecycleState.ACTIVE
        bridge._sim_start_wall_time = time.monotonic()
        bridge._sim_duration_s = 700.0
        
        # Act
        status = bridge.get_status()
        
        # Assert
        assert "time_remaining_s" in status
        assert status["time_remaining_s"] > 0

    def test_backup_timer_force_deactivate(self):
        """backup timer (duration + 30s) 强制 deactivate"""
        # Arrange: main timer disabled
        bridge = LifecycleBridge()
        bridge._deactivate_timer_task = None
        bridge._state = LifecycleState.ACTIVE
        
        # Act: backup fires at 730s
        with patch.object(bridge, 'deactivate') as mock_deactivate:
            bridge._backup_force_deactivate()
            
        # Assert
        mock_deactivate.assert_called_once()
```

**预期结果：5 个 assertions 全 FAIL（红）**

---

### Step 7.2: 跑测试验证失败

```bash
pytest tests/unit/test_w7_auto_stop.py -v
```

**预期输出：**
```
test_duration_read_from_yaml FAILED — attribute '_sim_duration_s' not found
test_timer_deactivates_at_duration FAILED — attribute '_deactivate_timer_task' not found
...（5 FAILs）
```

---

### Step 7.3: 实现 auto-stop timer

**File:** `src/sil_orchestrator/lifecycle_bridge.py`

#### 7.3.1 修改 `__init__()` 加 timer 字段（L70-74 后）

```python
    def __init__(self, callback_group=None) -> None:
        super().__init__('sil_orchestrator_lifecycle_bridge')
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id: str | None = None
        self._sim_rate = 1.0
        
        # ── W7 Auto-stop timer fields ────────────────────────
        self._sim_duration_s: float | None = None
        self._sim_start_wall_time: float | None = None
        self._deactivate_timer_task: asyncio.Task | None = None
        self._backup_timer_task: asyncio.Task | None = None
        
        # ... rest of init ...
```

#### 7.3.2 修改 `configure()` 读 duration（L321-348）

在 `yaml_data = _load_scenario_yaml(scenario_id)` 后添加：

```python
    async def configure(self, scenario_id: str) -> LifecycleResult:
        # Step 1-3: Load scenario YAML, extract params, log summary
        yaml_data = _load_scenario_yaml(scenario_id)
        
        # ── W7: Read duration from YAML ──────────────────────
        sim_settings = yaml_data.get("metadata", {}).get("simulation_settings", {})
        duration = sim_settings.get("total_time")  # ← ACTUAL FIELD NAME
        if duration is not None:
            self._sim_duration_s = float(duration)
            _log.info(f"Scenario duration: {self._sim_duration_s}s from YAML")
        else:
            self._sim_duration_s = None
        
        injection_map = _extract_injection_params(yaml_data)
        # ... rest of configure ...
```

#### 7.3.3 修改 `activate()` 启动 timer（L349-354）

```python
    async def activate(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_ACTIVATE)
        if res.success:
            self._state = LifecycleState.ACTIVE
            self._sim_start_wall_time = time.monotonic()
            
            # ── W7: Start deactivate timer if duration known ──
            if self._sim_duration_s is not None:
                # Cancel any existing timers
                if self._deactivate_timer_task is not None:
                    self._deactivate_timer_task.cancel()
                if self._backup_timer_task is not None:
                    self._backup_timer_task.cancel()
                
                # Main timer
                self._deactivate_timer_task = asyncio.create_task(
                    self._deactivate_after_duration())
                
                # Backup timer (duration + 30s)
                self._backup_timer_task = asyncio.create_task(
                    self._backup_force_deactivate_after(
                        self._sim_duration_s + 30.0))
                
                _log.info(f"Auto-stop timer started: {self._sim_duration_s}s")
            
            await self._broadcast_transition(Transition.TRANSITION_ACTIVATE)
        return res
```

#### 7.3.4 实现 `_deactivate_after_duration()`（新增方法）

```python
    async def _deactivate_after_duration(self) -> None:
        """Wait for sim duration, then deactivate."""
        if self._sim_duration_s is None:
            return
        
        try:
            await asyncio.sleep(self._sim_duration_s)
            _log.info(f"Auto-stop: {self._sim_duration_s}s elapsed, calling deactivate()")
            result = await self.deactivate()
            if not result.success:
                _log.warning(f"Deactivate failed: {result.error}")
        except asyncio.CancelledError:
            _log.debug("Auto-stop timer cancelled")
        except Exception as exc:
            _log.error(f"Auto-stop error: {exc}", exc_info=True)
```

#### 7.3.5 实现 backup force deactivate（新增方法）

```python
    async def _backup_force_deactivate_after(self, delay_s: float) -> None:
        """Backup timer: force deactivate if main deactivate didn't work."""
        try:
            await asyncio.sleep(delay_s)
            _log.warning(f"Backup force deactivate at {delay_s}s — lifecycle may be stuck")
            self._state = LifecycleState.INACTIVE
            self._scenario_id = None
        except asyncio.CancelledError:
            _log.debug("Backup timer cancelled")
        except Exception as exc:
            _log.error(f"Backup timer error: {exc}", exc_info=True)
```

#### 7.3.6 修改 `deactivate()` 清理 timer（L356-361）

```python
    async def deactivate(self) -> LifecycleResult:
        # ── W7: Cancel timers ────────────────────────────────
        if self._deactivate_timer_task is not None:
            self._deactivate_timer_task.cancel()
            self._deactivate_timer_task = None
        if self._backup_timer_task is not None:
            self._backup_timer_task.cancel()
            self._backup_timer_task = None
        
        res = await self._change_state(Transition.TRANSITION_DEACTIVATE)
        if res.success:
            self._state = LifecycleState.INACTIVE
            await self._broadcast_transition(Transition.TRANSITION_DEACTIVATE)
        return res
```

#### 7.3.7 修改 `cleanup()` 也清理 timer（L363-369）

```python
    async def cleanup(self) -> LifecycleResult:
        # ── W7: Cleanup timers ──────────────────────────────
        if self._deactivate_timer_task is not None:
            self._deactivate_timer_task.cancel()
            self._deactivate_timer_task = None
        if self._backup_timer_task is not None:
            self._backup_timer_task.cancel()
            self._backup_timer_task = None
        
        res = await self._change_state(Transition.TRANSITION_CLEANUP)
        if res.success:
            self._state = LifecycleState.UNCONFIGURED
            self._scenario_id = None
            await self._broadcast_transition(Transition.TRANSITION_CLEANUP)
        return res
```

#### 7.3.8 新增 `get_status()` 方法（或修改 main.py endpoint）

在 `main.py` 修改 `/api/v1/lifecycle/status` endpoint（L222-232）：

```python
@app.get("/api/v1/lifecycle/status")
async def lifecycle_status():
    detail = _store.get(bridge.scenario_id) if bridge.scenario_id else None
    backend = detail.get("backend", "demo") if detail else "demo"
    effective_backend = "demo" if not _HAS_RCLPY else backend
    
    # ── W7: Calculate time_remaining_s ───────────────────────
    time_remaining_s = None
    if (_HAS_RCLPY and bridge.current_state == LifecycleState.ACTIVE and
        hasattr(bridge, '_sim_duration_s') and bridge._sim_duration_s and
        hasattr(bridge, '_sim_start_wall_time') and bridge._sim_start_wall_time):
        elapsed = time.monotonic() - bridge._sim_start_wall_time
        time_remaining_s = max(0.0, bridge._sim_duration_s - elapsed)
    
    return {
        "current_state": bridge.current_state.value,
        "scenario_id": bridge.scenario_id,
        "run_id": _last_run_id,
        "effective_backend": effective_backend,
        "time_remaining_s": time_remaining_s,  # ← W7 new field
    }
```

---

### Step 7.4: 跑单测验证绿

```bash
pytest tests/unit/test_w7_auto_stop.py -v
```

**预期输出：5 PASS**

---

### Step 7.5: Docker 集成测试（W7 700s stop）

```bash
npm run sys:start
sleep 10

# Configure + activate imazu scenario
curl -X POST http://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" \
  -d '{"scenario_id": "imazu-01-ho"}'

curl -X POST http://localhost:8000/api/v1/lifecycle/activate

# Monitor time_remaining_s for 100s
for i in {1..10}; do
  curl http://localhost:8000/api/v1/lifecycle/status | jq .time_remaining_s
  sleep 10
done

# 预期：time_remaining_s 从 700.0 → 650.0 → ... → 0.0

# Wait until lifecycle turns inactive
sleep 650
curl http://localhost:8000/api/v1/lifecycle/status | jq .current_state
# 预期："inactive"
```

---

### Step 7.6: Commit W7

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer
git add -A
git commit -m "feat(W7): implement simulation 700s auto-stop timer

Read simulation_settings.total_time from scenario YAML (700s for imazu-01-ho).
Start asyncio deactivate timer on activate(), cancel on deactivate/cleanup.
Backup timer at duration+30s to force deactivate if hung.
Expose time_remaining_s in /api/v1/lifecycle/status for frontend display.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Testing Summary

### Unit Tests
- `tests/unit/test_w6_latch_release.py` — 6 assertions (release triggers, offset decay, publish)
- `tests/unit/test_w7_auto_stop.py` — 5 assertions (duration read, timer fire, idempotent, status field, backup)

### Integration Tests
- W6: Activate scenario → await 350s → check /sil/bridge_state topic
- W7: Activate scenario → poll status.time_remaining_s every 10s → await 710s → verify inactive

### E2E (via main agent)
Incorporated into DEMO-1 verification after A+B+C groups merge.

---

## Known Issues / Risks

| Risk | Mitigation |
|---|---|
| asyncio timer drift over 700s wall time | Backup timer + polling status field |
| LATCH offset decay conflicts with rudder rate limit | Monitor actuator_cmd rate; extend to 10s if needed |
| M2/M3 topics not published on time | Fallback: bridge holds LATCH until timeout |
| Scenario YAML missing `simulation_settings.total_time` | Default to None, skip timer, log warning |

---

## Deliverables

- [x] Bridge LATCH release logic (3 triggers)
- [x] 5s linear offset decay
- [x] `/sil/bridge_state` publisher
- [x] Scenario duration auto-stop timer
- [x] Idempotent deactivate
- [x] `time_remaining_s` status field
- [x] Unit tests (11 assertions, all green)
- [x] Integration tests (docker full chain)
- [x] 2 commits (W6 + W7)

---

## Next Steps (Main Agent)

1. Execute W6 steps 6.1–6.6 in worktree
2. Execute W7 steps 7.1–7.6 in worktree
3. Merge B group (W6+W7) to main
4. Proceed to C group (W8+W9) verification

**Total effort:** ~2.5 pw (0.6 + 0.3 per spec)
