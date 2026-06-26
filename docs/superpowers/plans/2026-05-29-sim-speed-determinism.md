# Sim-Speed Determinism + 1x Calibration — Implementation Plan (Phase 1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix cross-speed (1x/10x/50x) trajectory non-determinism and RTF≠rate by (a) making the SIL clock emit a fixed `dt_tick` per tick instead of scaling by `sim_rate`, (b) migrating 7 L3 control nodes from `create_wall_timer` to `rclcpp::create_timer(get_clock(), …)`, and (c) adding a failing regression test that turns green after the fix.

**Architecture:** The lifecycle clock node is the only source of `/clock`; making its increment constant and throttling its emission rate by `sim_rate` ensures all downstream sim-time timers fire at consistent sim-time intervals regardless of speed multiplier. Control nodes converted to sim-time timers fire in lockstep with the physics nodes, eliminating the physics:control step-ratio drift.

**Tech Stack:** Python (rclpy), C++ (rclcpp), pytest, colcon build, Docker Compose stack (HTTPS:8000 orchestrator API, `docker exec mass-l3-tacticallayer-sil-nodes-1`), feat/ git branch.

---

## Pre-flight: Branch & Baseline

Before touching any code:

- [ ] **Step 0.1: Create feature branch**

  ```bash
  cd /Users/marine/Code/MASS-L3-Tactical\ Layer
  git checkout -b feat/sim-speed-determinism
  ```
  Expected: Branch `feat/sim-speed-determinism` checked out.

- [ ] **Step 0.2: Verify existing tests still pass (unit-level only)**

  ```bash
  cd /Users/marine/Code/MASS-L3-Tactical\ Layer
  python -m pytest src/sim_workbench/sil_lifecycle/test/ -v
  ```
  Expected: All 5 existing tests PASS (they test FSM transitions, not clock logic).

---

## Task 1: Failing Regression Test (Red Baseline)

Write the cross-speed determinism and RTF test first, run it, and confirm it is **red** before touching any implementation.

**Files:**
- Create: `tests/integration/sim_determinism/test_determinism.py`
- Create: `tests/integration/sim_determinism/capture_imazu.py`
- Create: `tests/integration/sim_determinism/__init__.py`
- Create: `tests/integration/sim_determinism/conftest.py`
- Create: `tests/integration/__init__.py`

### Task 1.1 — Write the capture helper

- [ ] **Step 1.1.1: Create `capture_imazu.py`**

  This script drives the orchestrator lifecycle and captures telemetry to a CSV. It is called by the test, not by humans directly.

  ```python
  # tests/integration/sim_determinism/capture_imazu.py
  """Drive SIL lifecycle and capture own-ship telemetry to a CSV file.

  Usage (inside sil-nodes container, or host with network_mode=host):
      python3 capture_imazu.py --rate 1.0 --duration 60 --output /tmp/run_1x.csv
  """
  from __future__ import annotations
  import argparse
  import csv
  import json
  import math
  import ssl
  import sys
  import threading
  import time
  import urllib.request
  import urllib.error

  import rclpy
  from rclpy.node import Node
  from rosgraph_msgs.msg import Clock as ClockMsg
  from sil_msgs.msg import OwnShipState
  from l3_msgs.msg import BehaviorPlan, COLREGsConstraint

  # Orchestrator HTTPS endpoint (self-signed cert, verification skipped)
  ORCHESTRATOR_URL = "https://localhost:8000"
  SSL_CTX = ssl.create_default_context()
  SSL_CTX.check_hostname = False
  SSL_CTX.verify_mode = ssl.CERT_NONE

  # Longitude of imazu-01-ho route meridian (for XTE calculation)
  ROUTE_LON = 10.38  # degrees


  def _api(path: str, payload: dict | None = None) -> dict:
      url = ORCHESTRATOR_URL + path
      data = json.dumps(payload).encode() if payload else None
      req = urllib.request.Request(
          url, data=data,
          headers={"Content-Type": "application/json"} if data else {}
      )
      with urllib.request.urlopen(req, context=SSL_CTX, timeout=10) as resp:
          return json.loads(resp.read())


  def _meters_easting(lon_deg: float, lat_deg: float) -> float:
      """Approximate easting distance from route meridian (lon=ROUTE_LON)."""
      return (lon_deg - ROUTE_LON) * math.pi / 180.0 * 6_371_000 * math.cos(
          lat_deg * math.pi / 180.0
      )


  class CaptureNode(Node):
      def __init__(self, rate: float, duration: float, output: str) -> None:
          super().__init__("capture_imazu")
          self.rate = rate
          self.duration = duration
          self.output = output
          self.rows: list[dict] = []
          self._last_behavior: str = ""
          self._last_conflict: int = 0
          self._sim_t: float = 0.0
          self._wall_start: float = time.time()
          self._running = True

          self.create_subscription(OwnShipState, "/sil/own_ship_state",
                                   self._on_oss, 10)
          self.create_subscription(BehaviorPlan, "/l3/m4/behavior_plan",
                                   self._on_bp, 10)
          self.create_subscription(COLREGsConstraint, "/l3/m6/colregs_constraint",
                                   self._on_cr, 10)
          self.create_subscription(ClockMsg, "/clock",
                                   self._on_clock, 10)

      def _on_clock(self, msg: ClockMsg) -> None:
          self._sim_t = msg.clock.sec + msg.clock.nanosec * 1e-9

      def _on_bp(self, msg: BehaviorPlan) -> None:
          self._last_behavior = str(msg.behavior)

      def _on_cr(self, msg: COLREGsConstraint) -> None:
          self._last_conflict = int(msg.conflict_detected)

      def _on_oss(self, msg: OwnShipState) -> None:
          if not self._running:
              return
          xte_m = _meters_easting(msg.lon, msg.lat)
          self.rows.append({
              "sim_t": round(self._sim_t, 3),
              "lat": msg.lat,
              "lon": msg.lon,
              "heading_deg": round(math.degrees(msg.heading), 4),
              "rudder_deg": round(math.degrees(getattr(msg, "rudder_angle", 0.0)), 4),
              "xte_m": round(xte_m, 2),
              "behavior": self._last_behavior,
              "conflict": self._last_conflict,
          })
          # Stop after wall-clock duration
          if time.time() - self._wall_start > self.duration:
              self._running = False

      def done(self) -> bool:
          return not self._running

      def save(self) -> None:
          if not self.rows:
              return
          fields = list(self.rows[0].keys())
          with open(self.output, "w", newline="") as f:
              writer = csv.DictWriter(f, fieldnames=fields)
              writer.writeheader()
              writer.writerows(self.rows)


  def run_capture(rate: float, duration: float, output: str) -> None:
      # Lifecycle: cleanup -> configure -> rate -> activate -> capture -> deactivate
      print(f"[capture] cleanup", flush=True)
      _api("/api/v1/lifecycle/cleanup")
      time.sleep(1.0)
      print(f"[capture] configure", flush=True)
      _api("/api/v1/lifecycle/configure", {"scenario_id": "imazu-01-ho"})
      time.sleep(2.0)
      print(f"[capture] rate={rate}", flush=True)
      _api("/api/v1/lifecycle/rate", {"rate": rate})
      time.sleep(0.5)
      print(f"[capture] activate", flush=True)
      _api("/api/v1/lifecycle/activate")
      time.sleep(0.5)

      rclpy.init()
      node = CaptureNode(rate=rate, duration=duration, output=output)
      while rclpy.ok() and not node.done():
          rclpy.spin_once(node, timeout_sec=0.1)

      node.save()
      print(f"[capture] captured {len(node.rows)} rows -> {output}", flush=True)
      node.destroy_node()
      rclpy.shutdown()

      print(f"[capture] deactivate", flush=True)
      _api("/api/v1/lifecycle/deactivate")
      time.sleep(1.0)


  if __name__ == "__main__":
      parser = argparse.ArgumentParser()
      parser.add_argument("--rate", type=float, default=1.0)
      parser.add_argument("--duration", type=float, default=120.0,
                          help="Wall-clock seconds to capture")
      parser.add_argument("--output", default="/tmp/capture.csv")
      args = parser.parse_args()
      run_capture(args.rate, args.duration, args.output)
  ```

- [ ] **Step 1.1.2: Create `conftest.py`**

  ```python
  # tests/integration/sim_determinism/conftest.py
  """Pytest fixtures for determinism integration tests.

  These tests require the Docker SIL stack to be running.
  Mark with: pytest -m integration
  """
  import pytest


  def pytest_configure(config):
      config.addinivalue_line(
          "markers", "integration: mark test as requiring docker SIL stack"
      )
  ```

- [ ] **Step 1.1.3: Create `__init__.py` files**

  ```bash
  touch tests/integration/__init__.py
  touch tests/integration/sim_determinism/__init__.py
  ```

### Task 1.2 — Write the failing test

- [ ] **Step 1.2.1: Create `test_determinism.py`**

  ```python
  # tests/integration/sim_determinism/test_determinism.py
  """Cross-speed determinism regression test.

  Requires the docker SIL stack running (network_mode: host).
  Run with:
      pytest tests/integration/sim_determinism/ -m integration -v

  PASS criteria (post-fix):
    - RTF @ rate=1.0 in [0.95, 1.05]
    - Trajectory at same sim_t grid: position < 1 m, heading < 0.1 deg,
      behavior/conflict sequence identical between 1x and 10x runs.
  """
  from __future__ import annotations
  import csv
  import math
  import os
  import subprocess
  import sys
  import time
  from pathlib import Path

  import pytest

  # Path to capture script (relative to repo root)
  CAPTURE_SCRIPT = Path(__file__).parent / "capture_imazu.py"

  # Capture duration in wall seconds
  CAPTURE_WALL_S_1X = 130   # sim 60 s @ rate=1 -> need ~65 s wall (pre-fix RTF~1.74 -> ~35 s wall)
  CAPTURE_WALL_S_10X = 20   # sim 60 s @ rate=10 -> ~6 s wall

  # Alignment grid step [sim seconds]
  GRID_STEP_S = 2.0

  # Tolerances (post-fix)
  POS_TOL_M = 1.0
  HDG_TOL_DEG = 0.1

  # RTF tolerance
  RTF_LOW = 0.95
  RTF_HIGH = 1.05

  # Minimum sim-time coverage for alignment [s]
  MIN_SIM_COVERAGE_S = 30.0


  def _run_capture_in_container(rate: float, duration: float, output: str) -> None:
      """Run capture_imazu.py inside the sil-nodes container."""
      container = "mass-l3-tacticallayer-sil-nodes-1"
      # Copy script into container
      subprocess.run(
          ["docker", "cp", str(CAPTURE_SCRIPT), f"{container}:/tmp/capture_imazu.py"],
          check=True
      )
      subprocess.run(
          ["docker", "exec", container,
           "bash", "-c",
           f"source /opt/ros/*/setup.bash && "
           f"source /opt/ws/install/setup.bash && "
           f"python3 /tmp/capture_imazu.py "
           f"--rate {rate} --duration {duration} --output {output}"],
          check=True, timeout=int(duration) + 60
      )
      # Copy CSV back to host
      subprocess.run(
          ["docker", "cp", f"{container}:{output}", output],
          check=True
      )


  def _load_csv(path: str) -> list[dict]:
      with open(path, newline="") as f:
          return list(csv.DictReader(f))


  def _rows_by_sim_t(rows: list[dict]) -> dict[float, dict]:
      """Index rows by sim_t (float)."""
      return {float(r["sim_t"]): r for r in rows}


  def _haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
      R = 6_371_000.0
      dlat = math.radians(lat2 - lat1)
      dlon = math.radians(lon2 - lon1)
      a = (math.sin(dlat / 2) ** 2 +
           math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) *
           math.sin(dlon / 2) ** 2)
      return 2 * R * math.asin(math.sqrt(a))


  def _heading_diff(h1: float, h2: float) -> float:
      diff = abs(h1 - h2) % 360.0
      return diff if diff <= 180.0 else 360.0 - diff


  def _align_on_grid(
      rows_a: dict[float, dict],
      rows_b: dict[float, dict],
      grid_step: float,
  ) -> list[tuple[dict, dict]]:
      """Return paired (row_a, row_b) at each grid sim_t step."""
      if not rows_a or not rows_b:
          return []
      t_min = max(min(rows_a), min(rows_b))
      t_max = min(max(rows_a), max(rows_b))
      grid_t = t_min
      pairs: list[tuple[dict, dict]] = []
      while grid_t <= t_max:
          closest_a = min(rows_a, key=lambda t: abs(t - grid_t))
          closest_b = min(rows_b, key=lambda t: abs(t - grid_t))
          if abs(closest_a - grid_t) < grid_step and abs(closest_b - grid_t) < grid_step:
              pairs.append((rows_a[closest_a], rows_b[closest_b]))
          grid_t += grid_step
      return pairs


  @pytest.mark.integration
  class TestSimDeterminism:

      def test_rtf_at_rate_1x(self, tmp_path):
          """RTF @ rate=1.0 must be in [0.95, 1.05]."""
          out = str(tmp_path / "rtf_1x.csv")
          wall_start = time.time()
          _run_capture_in_container(rate=1.0, duration=30.0, output=out)
          wall_elapsed = time.time() - wall_start

          rows = _load_csv(out)
          assert rows, "No rows captured -- is the stack running?"

          sim_t_vals = [float(r["sim_t"]) for r in rows]
          sim_elapsed = max(sim_t_vals) - min(sim_t_vals)
          assert sim_elapsed > 5.0, f"Too little sim time captured: {sim_elapsed:.1f}s"

          # RTF = sim_elapsed / wall_elapsed_during_sim (subtract ~5s lifecycle overhead)
          rtf = sim_elapsed / max(1.0, wall_elapsed - 5.0)
          assert RTF_LOW <= rtf <= RTF_HIGH, (
              f"RTF={rtf:.3f} out of [{RTF_LOW}, {RTF_HIGH}]. "
              f"sim={sim_elapsed:.1f}s wall~{wall_elapsed:.1f}s"
          )

      def test_cross_speed_determinism_1x_vs_10x(self, tmp_path):
          """1x and 10x runs of imazu-01-ho must produce identical trajectories."""
          out_1x = str(tmp_path / "det_1x.csv")
          out_10x = str(tmp_path / "det_10x.csv")

          _run_capture_in_container(rate=1.0, duration=CAPTURE_WALL_S_1X, output=out_1x)
          time.sleep(3.0)  # Let stack settle between runs
          _run_capture_in_container(rate=10.0, duration=CAPTURE_WALL_S_10X, output=out_10x)

          rows_1x = _load_csv(out_1x)
          rows_10x = _load_csv(out_10x)
          assert rows_1x, "No rows in 1x capture"
          assert rows_10x, "No rows in 10x capture"

          by_t_1x = _rows_by_sim_t(rows_1x)
          by_t_10x = _rows_by_sim_t(rows_10x)

          pairs = _align_on_grid(by_t_1x, by_t_10x, GRID_STEP_S)
          assert len(pairs) >= int(MIN_SIM_COVERAGE_S / GRID_STEP_S), (
              f"Too few aligned pairs: {len(pairs)} (need >={MIN_SIM_COVERAGE_S/GRID_STEP_S:.0f})"
          )

          max_pos_err = 0.0
          max_hdg_err = 0.0
          behavior_mismatches = 0
          conflict_mismatches = 0

          for r1, r10 in pairs:
              pos_err = _haversine_m(
                  float(r1["lat"]), float(r1["lon"]),
                  float(r10["lat"]), float(r10["lon"])
              )
              hdg_err = _heading_diff(float(r1["heading_deg"]), float(r10["heading_deg"]))
              max_pos_err = max(max_pos_err, pos_err)
              max_hdg_err = max(max_hdg_err, hdg_err)
              if r1["behavior"] != r10["behavior"]:
                  behavior_mismatches += 1
              if r1["conflict"] != r10["conflict"]:
                  conflict_mismatches += 1

          assert max_pos_err < POS_TOL_M, (
              f"Max position error {max_pos_err:.2f} m exceeds {POS_TOL_M} m"
          )
          assert max_hdg_err < HDG_TOL_DEG, (
              f"Max heading error {max_hdg_err:.3f}deg exceeds {HDG_TOL_DEG}deg"
          )
          assert behavior_mismatches == 0, (
              f"{behavior_mismatches} behavior mismatches at aligned sim_t"
          )
          assert conflict_mismatches == 0, (
              f"{conflict_mismatches} conflict mismatches at aligned sim_t"
          )
  ```

- [ ] **Step 1.2.2: Run the test and confirm it is RED**

  ```bash
  cd /Users/marine/Code/MASS-L3-Tactical\ Layer
  pytest tests/integration/sim_determinism/ -m integration -v -k "test_rtf" 2>&1 | tail -20
  ```
  Expected: FAILED with RTF out of [0.95, 1.05] (currently ~1.74).

- [ ] **Step 1.2.3: Commit the failing test**

  ```bash
  git add tests/integration/
  git commit -m "test: add failing cross-speed determinism + RTF regression tests"
  ```

---

## Task 2: Fix `lifecycle_mgr.py` — Fixed-Increment Clock

**Files:**
- Modify: `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py`
- Modify: `src/sim_workbench/sil_lifecycle/test/test_lifecycle_mgr.py`

### Task 2.1 — Write new unit tests for the fixed-increment clock

- [ ] **Step 2.1.1: Append failing unit tests to `test_lifecycle_mgr.py`**

  Append to `src/sim_workbench/sil_lifecycle/test/test_lifecycle_mgr.py`:

  ```python
  def test_tick_advances_by_dt_tick_not_sim_rate():
      """tick() must advance sim_time by dt_tick regardless of sim_rate."""
      mgr = ScenarioLifecycleMgr(tick_hz=250.0)
      assert mgr.configure("s1")
      assert mgr.activate()
      # Rate=10 must NOT multiply the increment
      mgr.set_sim_rate(10.0)
      mgr.tick()
      expected_dt = 1.0 / 250.0
      assert abs(mgr.sim_time - expected_dt) < 1e-9, (
          f"sim_time={mgr.sim_time}, expected {expected_dt}"
      )


  def test_set_sim_rate_does_not_change_dt_tick():
      """set_sim_rate() must not affect the tick increment."""
      mgr = ScenarioLifecycleMgr(tick_hz=100.0)
      assert mgr.configure("s1")
      assert mgr.activate()
      mgr.set_sim_rate(50.0)
      for _ in range(10):
          mgr.tick()
      expected = 10.0 / 100.0
      assert abs(mgr.sim_time - expected) < 1e-9, (
          f"sim_time={mgr.sim_time}, expected {expected}"
      )


  def test_tick_does_not_advance_when_inactive():
      """tick() must not advance sim_time when FSM is not ACTIVE."""
      mgr = ScenarioLifecycleMgr(tick_hz=250.0)
      mgr.configure("s1")
      # Not yet activated
      mgr.tick()
      assert mgr.sim_time == 0.0
  ```

- [ ] **Step 2.1.2: Run and confirm new tests FAIL**

  ```bash
  python -m pytest src/sim_workbench/sil_lifecycle/test/test_lifecycle_mgr.py \
    -k "test_tick_advances" -v 2>&1 | tail -10
  ```
  Expected: FAILED (old `tick()` scales by `sim_rate`).

### Task 2.2 — Implement the fixed-increment clock

- [ ] **Step 2.2.1: Fix `ScenarioLifecycleMgr.tick()` — remove `sim_rate` scaling**

  In `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py`, lines 158–161:

  **Before:**
  ```python
      def tick(self) -> None:
          """Advance simulation time by one tick (called at tick_hz)."""
          if self._state == LifecycleState.ACTIVE:
              self._sim_time += (1.0 / self._tick_hz) * self._sim_rate
  ```

  **After:**
  ```python
      def tick(self) -> None:
          """Advance simulation time by one fixed tick (called at tick_hz).

          dt_tick = 1/tick_hz is always constant.
          sim_rate only affects the wall-clock emission frequency of the timer
          callback, not the tick increment.  This ensures cross-speed determinism.
          """
          if self._state == LifecycleState.ACTIVE:
              self._sim_time += 1.0 / self._tick_hz
  ```

- [ ] **Step 2.2.2: Run unit tests — must now PASS**

  ```bash
  python -m pytest src/sim_workbench/sil_lifecycle/test/test_lifecycle_mgr.py -v
  ```
  Expected: All 8 tests PASS (5 original + 3 new).

### Task 2.3 — Implement the wall-paced throttle loop in `LifecycleManagerNode`

- [ ] **Step 2.3.1: Add `run_start_wall` property to `ScenarioLifecycleMgr`**

  The field `_wall_start` already exists (set in `activate()`, line 133). Add a property after `wall_time` (after line 118) in `lifecycle_mgr.py`:

  ```python
      @property
      def run_start_wall(self) -> float:
          """Wall-clock time.time() at activation start."""
          return self._wall_start
  ```

- [ ] **Step 2.3.2: Add pacing-state attributes in `LifecycleManagerNode.on_activate()`**

  In `on_activate` (line ~301), after `self._fsm.activate()`:

  ```python
          self._fsm.activate()
          self._run_start_wall: float = time.time()
  ```

- [ ] **Step 2.3.3: Rewrite `_clock_callback` with MAX_CATCHUP_TICKS loop**

  Replace `_clock_callback` (lines 336–352) entirely:

  ```python
      # Maximum dt_tick steps to emit per single callback invocation.
      _MAX_CATCHUP_TICKS = 10

      def _clock_callback(self) -> None:
          """Wall-clock-paced /clock emitter.

          Design (spec §4.1):
            wall_elapsed = now_wall - run_start_wall
            target_sim   = wall_elapsed * sim_rate
            Emit dt_tick steps until sim_time reaches target_sim,
            capped at _MAX_CATCHUP_TICKS per callback.
            Publish one /clock + /sim_clock per dt_tick so that
            sim-time timers fire on every tick (no skipped steps).
          """
          if not hasattr(self, "_run_start_wall"):
              return

          sim_rate = self._fsm.sim_rate
          dt_tick = 1.0 / self._fsm._tick_hz
          wall_elapsed = time.time() - self._run_start_wall
          target_sim = wall_elapsed * sim_rate

          emitted = 0
          while self._fsm.sim_time < target_sim and emitted < self._MAX_CATCHUP_TICKS:
              self._fsm.tick()
              sim_t = self._fsm.sim_time

              time_msg = TimeMsg()
              time_msg.sec = int(sim_t)
              time_msg.nanosec = int((sim_t - time_msg.sec) * 1e9)
              if self._sim_clock_pub is not None:
                  self._sim_clock_pub.publish(time_msg)

              if self._clock_pub is not None:
                  clock_msg = ClockMsg()
                  clock_msg.clock = time_msg
                  self._clock_pub.publish(clock_msg)

              emitted += 1

          if emitted == self._MAX_CATCHUP_TICKS and self._fsm.sim_time < target_sim:
              self.get_logger().warn(
                  f"[lifecycle_mgr] Clock catchup capped at {self._MAX_CATCHUP_TICKS} ticks; "
                  f"sim_time={self._fsm.sim_time:.3f} target={target_sim:.3f}. "
                  f"RTF may be < sim_rate temporarily."
              )
  ```

- [ ] **Step 2.3.4: Reset `_run_start_wall` on deactivate**

  In `on_deactivate` (line ~310), after `self._fsm.deactivate()`:

  ```python
          self._fsm.deactivate()
          if hasattr(self, "_run_start_wall"):
              del self._run_start_wall
  ```

- [ ] **Step 2.3.5: Run all unit tests — all must PASS**

  ```bash
  python -m pytest src/sim_workbench/sil_lifecycle/test/ -v
  ```
  Expected: All 8 tests PASS.

- [ ] **Step 2.3.6: Commit**

  ```bash
  git add src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py \
          src/sim_workbench/sil_lifecycle/test/test_lifecycle_mgr.py
  git commit -m "fix(lifecycle_mgr): fixed-increment clock tick + wall-paced catchup loop"
  ```

---

## Task 3: C++ Control Nodes — `create_wall_timer` → sim-time

**Pattern for all changes:** Replace `create_wall_timer(period, cb[, cbgroup])` with the free function:
```cpp
rclcpp::create_timer(
    get_node_base_interface(),
    get_node_timers_interface(),
    get_clock(),
    period,
    cb
    [, cbgroup]   // include only if original had a callback group
);
```
Reference: M2 `world_model_node.cpp` lines 297–323.

> **watchdog/heartbeat timers (M3:324,328 and M7:247):** Per spec §4.4, convert to sim-time (default). Semantics in SIL = "did upstream send within N simulated seconds?" — deterministic under sim-time. No exception needed.

> **m4_behavior_arbiter/.salvage-d3.1/: DO NOT MODIFY.**

### Task 3.1 — M1: `odd_envelope_manager_node.cpp`

**File:** `src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp`
**Lines:** 473–488 (`initialize_timers()`)

- [ ] **Step 3.1.1: Replace 4 `create_wall_timer` calls in M1**

  ```cpp
  void OddEnvelopeManagerNode::initialize_timers() {
    main_loop_timer_ = rclcpp::create_timer(
        get_node_base_interface(),
        get_node_timers_interface(),
        get_clock(),
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(kMainLoopPeriodS)),
        [this]() { on_main_loop_tick(); });

    odd_publish_timer_ = rclcpp::create_timer(
        get_node_base_interface(),
        get_node_timers_interface(),
        get_clock(),
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(kOddPublishPeriodS)),
        [this]() { on_odd_state_publish_tick(); });

    asdr_periodic_timer_ = rclcpp::create_timer(
        get_node_base_interface(),
        get_node_timers_interface(),
        get_clock(),
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(kAsdrPeriodicPeriodS)),
        [this]() { on_asdr_record_periodic_tick(); });

    sat_timer_ = rclcpp::create_timer(
        get_node_base_interface(),
        get_node_timers_interface(),
        get_clock(),
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(kSatPeriodS)),
        [this]() { on_sat_data_publish_tick(); });
  }
  ```

- [ ] **Step 3.1.2: Build M1**

  ```bash
  docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c \
    "cd /opt/ws && colcon build --packages-select m1_odd_envelope_manager 2>&1 | tail -5"
  ```
  Expected: `Summary: 1 packages finished`.

- [ ] **Step 3.1.3: Commit**

  ```bash
  git add src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp
  git commit -m "fix(M1): create_wall_timer -> rclcpp::create_timer(get_clock()) for all 4 timers"
  ```

### Task 3.2 — M3: `mission_manager_node.cpp`

**File:** `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp`
**Lines:** 310–330 (`setup_timers()`)

- [ ] **Step 3.2.1: Replace 5 `create_wall_timer` calls in M3**

  ```cpp
  void MissionManagerNode::setup_timers()
  {
    const double goal_hz = get_parameter("mission_goal.publish_rate_hz").as_double();
    const auto goal_period = std::chrono::duration<double>(1.0 / goal_hz);
    mission_goal_timer_ = rclcpp::create_timer(
        get_node_base_interface(),
        get_node_timers_interface(),
        get_clock(),
        std::chrono::duration_cast<std::chrono::nanoseconds>(goal_period),
        [this]() { publish_mission_goal(); });

    const double asdr_hz = get_parameter("asdr.heartbeat_rate_hz").as_double();
    const auto asdr_period = std::chrono::duration<double>(1.0 / asdr_hz);
    asdr_timer_ = rclcpp::create_timer(
        get_node_base_interface(),
        get_node_timers_interface(),
        get_clock(),
        std::chrono::duration_cast<std::chrono::nanoseconds>(asdr_period),
        [this]() { publish_asdr_snapshot(); });

    replan_deadline_timer_ = rclcpp::create_timer(
        get_node_base_interface(),
        get_node_timers_interface(),
        get_clock(),
        std::chrono::seconds(1),
        [this]() { check_replan_deadline(); });

    heartbeat_timer_ = rclcpp::create_timer(
        get_node_base_interface(),
        get_node_timers_interface(),
        get_clock(),
        std::chrono::seconds(1),
        [this]() { log_heartbeat(); });

    l1_watchdog_timer_ = rclcpp::create_timer(
        get_node_base_interface(),
        get_node_timers_interface(),
        get_clock(),
        std::chrono::seconds(1),
        [this]() { evaluate_l1_watchdog(); });
  }
  ```

- [ ] **Step 3.2.2: Build M3**

  ```bash
  docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c \
    "cd /opt/ws && colcon build --packages-select m3_mission_manager 2>&1 | tail -5"
  ```
  Expected: `Summary: 1 packages finished`.

- [ ] **Step 3.2.3: Commit**

  ```bash
  git add src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp
  git commit -m "fix(M3): create_wall_timer -> rclcpp::create_timer(get_clock()) for all 5 timers"
  ```

### Task 3.3 — M4: `behavior_arbiter_node.cpp`

**File:** `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`
**Line:** 74

- [ ] **Step 3.3.1: Replace `timer_` in M4 constructor**

  ```cpp
  timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::milliseconds(interval_ms_),
      [this]() { arbitration_timer_callback(); });
  ```

- [ ] **Step 3.3.2: Build M4**

  ```bash
  docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c \
    "cd /opt/ws && colcon build --packages-select m4_behavior_arbiter 2>&1 | tail -5"
  ```
  Expected: `Summary: 1 packages finished`.

- [ ] **Step 3.3.3: Commit**

  ```bash
  git add src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp
  git commit -m "fix(M4): create_wall_timer -> rclcpp::create_timer(get_clock())"
  ```

### Task 3.4 — M5 mid_mpc: `mid_mpc_node.cpp`

**File:** `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
**Lines:** 87–89

- [ ] **Step 3.4.1: Replace `solve_timer_` in M5 mid_mpc**

  ```cpp
  solve_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::seconds(1),
      [this]() { on_solve_cycle_(); });
  ```

- [ ] **Step 3.4.2: Build M5**

  ```bash
  docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c \
    "cd /opt/ws && colcon build --packages-select m5_tactical_planner 2>&1 | tail -5"
  ```
  Expected: `Summary: 1 packages finished`.

- [ ] **Step 3.4.3: Commit**

  ```bash
  git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
  git commit -m "fix(M5 mid_mpc): create_wall_timer -> rclcpp::create_timer(get_clock())"
  ```

### Task 3.5 — M5 bc_mpc: `bc_mpc_node.cpp`

**File:** `src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp`
**Lines:** 51–54

- [ ] **Step 3.5.1: Replace `validity_timer_` in M5 bc_mpc**

  ```cpp
  validity_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::duration<double>(kTickInterval_s)),
      [this]() { on_validity_tick_(); });
  ```

- [ ] **Step 3.5.2: Build M5 (same package as mid_mpc — already done in 3.4.2)**

  If not already done:
  ```bash
  docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c \
    "cd /opt/ws && colcon build --packages-select m5_tactical_planner 2>&1 | tail -5"
  ```

- [ ] **Step 3.5.3: Commit**

  ```bash
  git add src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp
  git commit -m "fix(M5 bc_mpc): create_wall_timer -> rclcpp::create_timer(get_clock())"
  ```

### Task 3.6 — M6: `colregs_reasoner_node.cpp`

**File:** `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`
**Lines:** 383–390 (`setup_timers()`)

- [ ] **Step 3.6.1: Replace 4 `create_wall_timer` calls in M6**

  ```cpp
  void ColregsReasonerNode::setup_timers() {
    const auto kReasoningPeriod = std::chrono::milliseconds(
      get_parameter("reasoning_period_ms").as_int());
    const auto kHealthPeriod = std::chrono::milliseconds(
      get_parameter("health_check_period_ms").as_int());
    const auto kAsdrPeriod = std::chrono::milliseconds(
      get_parameter("asdr_snapshot_period_ms").as_int());
    const auto kSatPeriod = std::chrono::milliseconds(
      get_parameter("sat_publish_period_ms").as_int());

    reasoning_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      kReasoningPeriod,
      [this]() { run_reasoning(); });
    health_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      kHealthPeriod,
      [this]() { check_health(); });
    asdr_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      kAsdrPeriod,
      [this]() { publish_asdr_snapshot(); });
    sat_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      kSatPeriod,
      [this]() { publish_sat_data(); });
  }
  ```

- [ ] **Step 3.6.2: Build M6**

  ```bash
  docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c \
    "cd /opt/ws && colcon build --packages-select m6_colregs_reasoner 2>&1 | tail -5"
  ```
  Expected: `Summary: 1 packages finished`.

- [ ] **Step 3.6.3: Commit**

  ```bash
  git add src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp
  git commit -m "fix(M6): create_wall_timer -> rclcpp::create_timer(get_clock()) for all 4 timers"
  ```

### Task 3.7 — M7: `safety_supervisor_node.cpp`

**File:** `src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp`
**Lines:** 232–250 (`setup_timers()`)

M7 passes `cb_group_main_` as a 6th argument to `rclcpp::create_timer`.

- [ ] **Step 3.7.1: Replace 4 `create_wall_timer` calls in M7**

  ```cpp
  void SafetySupervisorNode::setup_timers() noexcept
  {
    timer_main_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::milliseconds{250},
      [this]() { on_main_loop_tick(); },
      cb_group_main_);

    timer_sat_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::milliseconds{100},
      [this]() { on_sat_tick(); },
      cb_group_main_);

    timer_asdr_periodic_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::milliseconds{500},
      [this]() { on_asdr_periodic_tick(); },
      cb_group_main_);

    timer_heartbeat_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::milliseconds{100},
      [this]() { on_heartbeat_tick(); },
      cb_group_main_);
  }
  ```

- [ ] **Step 3.7.2: Build M7**

  ```bash
  docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c \
    "cd /opt/ws && colcon build --packages-select m7_safety_supervisor 2>&1 | tail -5"
  ```
  Expected: `Summary: 1 packages finished`.

- [ ] **Step 3.7.3: Commit**

  ```bash
  git add src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp
  git commit -m "fix(M7): create_wall_timer -> rclcpp::create_timer(get_clock()) for all 4 timers (incl. watchdog/heartbeat)"
  ```

### Task 3.8 — M8: emit-side timer migration + consumer-side sim-time fix

**Rationale:** M7's `timer_heartbeat_` publishes `/l3/m7/heartbeat` using `get_clock()->now()` (sim-time after Task 3.7). M8 currently records receipt time as `SatAggregator::Clock::now()` (= `std::chrono::steady_clock::now()`, wall clock) and compares against `steady_clock::now()` at query time. With M7 on sim-time and M8 on wall-clock, the age comparison is clock-domain mismatch: at rate=10 M7 fires at 100 Hz wall-time so M8 sees the heartbeat as always fresh, but at rate<1 (or during pause) M8 will spuriously time out. The safety sequence `is_m7_timed_out → forced D2 mode` will differ across sim speeds — exactly the non-determinism we are eliminating.

**Fix:** Change `ModuleHealthMonitor` to use `double` (sim-time seconds) throughout. Callers extract the sim-time from the message `stamp` (M7 heartbeat `hb.stamp.sec + hb.stamp.nanosec * 1e-9`) or from `get_clock()->now().seconds()`. The threshold values (e.g. `m7_timeout_s = 2.0`) remain in sim-seconds, so semantics are preserved: "M7 failed to send a heartbeat within 2 simulated seconds."

**Files affected:**
- `src/l3_tdl_kernel/m8_hmi_transparency_bridge/include/m8_hmi_transparency_bridge/module_health_monitor.hpp`
- `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/module_health_monitor.cpp`
- `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp`
- `src/l3_tdl_kernel/m8_hmi_transparency_bridge/test/test_module_health_monitor.cpp`

#### Task 3.8A — M8 emit-side: timer migration

- [ ] **Step 3.8.1: Replace 5 `create_wall_timer` calls in `hmi_transparency_bridge_node.cpp` (lines 126–131)**

  ```cpp
  void HmiTransparencyBridgeNode::init_timers()
  {
    using namespace std::chrono_literals;
    timer_ui_ = rclcpp::create_timer(
        get_node_base_interface(), get_node_timers_interface(), get_clock(),
        20ms, [this] { on_ui_publish_tick(); });
    timer_tor_ = rclcpp::create_timer(
        get_node_base_interface(), get_node_timers_interface(), get_clock(),
        500ms, [this] { on_tor_tick(); });
    timer_health_ = rclcpp::create_timer(
        get_node_base_interface(), get_node_timers_interface(), get_clock(),
        1000ms, [this] { on_health_check_tick(); });
    timer_asdr_snapshot_ = rclcpp::create_timer(
        get_node_base_interface(), get_node_timers_interface(), get_clock(),
        500ms, [this] { on_asdr_snapshot_tick(); });
    timer_sil_stub_ = rclcpp::create_timer(
        get_node_base_interface(), get_node_timers_interface(), get_clock(),
        1000ms, [this] { on_sil_stub_tick(); });
  }
  ```

#### Task 3.8B — M8 consumer-side: ModuleHealthMonitor → sim-time

- [ ] **Step 3.8.2: Rewrite `module_health_monitor.hpp` — replace `Clock/TimePoint` with `double`**

  **Before (lines 5–21):**
  ```cpp
  #include <chrono>
  // ...
  class ModuleHealthMonitor final {
   public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
  ```

  **After:**
  ```cpp
  // <chrono> no longer needed for TimePoint; keep for internal use only if needed
  // ...
  class ModuleHealthMonitor final {
   public:
    /// Sim-time in seconds (from rclcpp::Clock or message stamp).
    /// Replacing std::chrono::steady_clock::time_point to make health
    /// judgments deterministic across simulation speeds.
    using SimTimeS = double;
  ```

  Replace ALL occurrences of `TimePoint` in the header with `SimTimeS`:

  ```cpp
  // Full updated header:
  #ifndef MASS_L3_M8_MODULE_HEALTH_MONITOR_HPP_
  #define MASS_L3_M8_MODULE_HEALTH_MONITOR_HPP_

  #include <map>
  #include <mutex>
  #include <string>

  #include "m8_hmi_transparency_bridge/sat_aggregator.hpp"

  namespace mass_l3::m8 {

  /// Heartbeat monitor for upstream M1-M7 modules.
  ///
  /// All time values are sim-time seconds (from rclcpp::Clock or message stamp),
  /// ensuring health judgments are deterministic across simulation speeds.
  class ModuleHealthMonitor final {
   public:
    /// Sim-time seconds.  Pass get_clock()->now().seconds() or
    /// msg->stamp.sec + msg->stamp.nanosec * 1e-9 at call sites.
    using SimTimeS = double;

    struct Thresholds {
      double m1_timeout_s{2.0};
      double m2_timeout_s{1.0};
      double m4_timeout_s{1.0};
      double m6_timeout_s{1.0};
      double m7_timeout_s{2.0};
    };

    ModuleHealthMonitor() noexcept = default;
    explicit ModuleHealthMonitor(Thresholds t) noexcept : thresholds_(t) {}

    /// Update heartbeat for a module.
    /// @param now  Current sim-time seconds.
    void record_heartbeat(SatAggregator::SourceModule src, SimTimeS now) noexcept;

    /// True if module has not sent a message within its timeout window.
    [[nodiscard]] bool is_timed_out(
        SatAggregator::SourceModule src, SimTimeS now) const noexcept;

    /// Check if M7 specifically has timed out (critical -- triggers forced D2).
    [[nodiscard]] bool is_m7_timed_out(SimTimeS now) const noexcept;

    /// Check any module is timed out (DEGRADED condition).
    [[nodiscard]] bool any_module_timed_out(SimTimeS now) const noexcept;

   private:
    Thresholds thresholds_;
    mutable std::mutex mutex_;
    std::map<SatAggregator::SourceModule, SimTimeS> last_heartbeat_{};

    [[nodiscard]] double timeout_for(SatAggregator::SourceModule src) const noexcept;

    static constexpr double kDefaultTimeoutS{1.0};
  };

  }  // namespace mass_l3::m8

  #endif  // MASS_L3_M8_MODULE_HEALTH_MONITOR_HPP_
  ```

- [ ] **Step 3.8.3: Rewrite `module_health_monitor.cpp` — use `double` throughout**

  **Full replacement:**
  ```cpp
  #include "m8_hmi_transparency_bridge/module_health_monitor.hpp"

  #include <mutex>

  namespace mass_l3::m8 {

  void ModuleHealthMonitor::record_heartbeat(
      SatAggregator::SourceModule src, SimTimeS now) noexcept
  {
      std::lock_guard<std::mutex> lock{mutex_};
      last_heartbeat_[src] = now;
  }

  bool ModuleHealthMonitor::is_timed_out(
      SatAggregator::SourceModule src, SimTimeS now) const noexcept
  {
      std::lock_guard<std::mutex> lock{mutex_};
      auto it = last_heartbeat_.find(src);
      if (it == last_heartbeat_.end()) {
          return true;  // never heard from = timed out
      }
      double age = now - it->second;
      return age > timeout_for(src);
  }

  bool ModuleHealthMonitor::is_m7_timed_out(SimTimeS now) const noexcept
  {
      // Inline to avoid re-locking (std::mutex is not recursive)
      std::lock_guard<std::mutex> lock{mutex_};
      auto it = last_heartbeat_.find(SatAggregator::SourceModule::kM7);
      if (it == last_heartbeat_.end()) {
          return true;
      }
      double age = now - it->second;
      return age > thresholds_.m7_timeout_s;
  }

  bool ModuleHealthMonitor::any_module_timed_out(SimTimeS now) const noexcept
  {
      std::lock_guard<std::mutex> lock{mutex_};
      for (uint8_t i = 0U; i < static_cast<uint8_t>(SatAggregator::SourceModule::kCount); ++i) {
          auto src = static_cast<SatAggregator::SourceModule>(i);
          auto it = last_heartbeat_.find(src);
          if (it == last_heartbeat_.end()) {
              return true;
          }
          double age = now - it->second;
          if (age > timeout_for(src)) {
              return true;
          }
      }
      return false;
  }

  double ModuleHealthMonitor::timeout_for(
      SatAggregator::SourceModule src) const noexcept
  {
      switch (src) {
          case SatAggregator::SourceModule::kM1: return thresholds_.m1_timeout_s;
          case SatAggregator::SourceModule::kM2: return thresholds_.m2_timeout_s;
          case SatAggregator::SourceModule::kM4: return thresholds_.m4_timeout_s;
          case SatAggregator::SourceModule::kM6: return thresholds_.m6_timeout_s;
          case SatAggregator::SourceModule::kM7: return thresholds_.m7_timeout_s;
          default: return kDefaultTimeoutS;
      }
  }

  }  // namespace mass_l3::m8
  ```

- [ ] **Step 3.8.4: Update call sites in `hmi_transparency_bridge_node.cpp`**

  Three locations need updating:

  **A. `on_sat_data` (line 138–146) — SAT data heartbeat via `SatAggregator` module ID:**

  ```cpp
  void HmiTransparencyBridgeNode::on_sat_data(const l3_msgs::msg::SATData::SharedPtr msg)
  {
    auto now = SatAggregator::Clock::now();
    // SatAggregator uses wall-clock receive_time for freshness (separate from health monitor).
    sat_aggregator_->ingest(*msg, now);
    auto src = SatAggregator::from_string(msg->source_module);
    if (src.has_value()) {
      // Extract sim-time from message stamp for deterministic health judgment.
      const double sim_now_s =
          static_cast<double>(msg->stamp.sec) + msg->stamp.nanosec * 1e-9;
      health_monitor_->record_heartbeat(*src, sim_now_s);
    }
  }
  ```

  **B. `on_m7_heartbeat` (line 194–199) — M7 heartbeat stamp:**

  ```cpp
  void HmiTransparencyBridgeNode::on_m7_heartbeat(const std_msgs::msg::Header::SharedPtr msg)
  {
    // Use message stamp (sim-time, stamped by M7 with get_clock()->now())
    // rather than local wall-clock receive time.
    const double sim_now_s =
        static_cast<double>(msg->stamp.sec) + msg->stamp.nanosec * 1e-9;
    health_monitor_->record_heartbeat(
        SatAggregator::SourceModule::kM7,
        sim_now_s);
  }
  ```

  **C. All `is_m7_timed_out` and `any_module_timed_out` query sites (lines 289–290, 408) — pass sim-time now:**

  Change all:
  ```cpp
  // BEFORE:
  auto now = SatAggregator::Clock::now();
  if (health_monitor_->is_m7_timed_out(now)) {
  ```
  to:
  ```cpp
  // AFTER:
  const double sim_now_s = get_clock()->now().seconds();
  if (health_monitor_->is_m7_timed_out(sim_now_s)) {
  ```

  And line 408 (`on_sil_stub_tick`):
  ```cpp
  // BEFORE:
  bool m7_active = health_monitor_ && !health_monitor_->is_m7_timed_out(SatAggregator::Clock::now());
  // AFTER:
  bool m7_active = health_monitor_ && !health_monitor_->is_m7_timed_out(get_clock()->now().seconds());
  ```

- [ ] **Step 3.8.5: Update `test_module_health_monitor.cpp` — replace `TimePoint` with `SimTimeS` (double)**

  The existing tests use `Clock::now()` and `offset(base, seconds)` helpers. Replace with plain `double` arithmetic:

  **Remove:**
  ```cpp
  ModuleHealthMonitor::TimePoint epoch()
  {
      return ModuleHealthMonitor::Clock::now();
  }

  ModuleHealthMonitor::TimePoint offset(
      ModuleHealthMonitor::TimePoint base, double seconds)
  {
      using namespace std::chrono;
      return base + duration_cast<steady_clock::duration>(duration<double>(seconds));
  }
  ```

  **Replace with:**
  ```cpp
  // Sim-time helpers for test (arbitrary epoch at 1000.0s)
  static constexpr double kEpoch = 1000.0;

  ModuleHealthMonitor::SimTimeS epoch()
  {
      return kEpoch;
  }

  ModuleHealthMonitor::SimTimeS offset(
      ModuleHealthMonitor::SimTimeS base, double seconds)
  {
      return base + seconds;
  }
  ```

  All test bodies continue to compile with no further changes (they call `epoch()` and `offset()` which now return `double`), except remove the `using namespace std::chrono;` line that is no longer needed.

  **Add one new test** for the cross-rate determinism property:
  ```cpp
  // ---------------------------------------------------------------------------
  // Test 9: health judgment is rate-independent (sim-time semantics)
  // ---------------------------------------------------------------------------
  TEST(ModuleHealthMonitor, HealthJudgment_RateIndependent)
  {
      // Heartbeat recorded at sim_t=1000s, timeout=2s.
      // At sim_t=1001s (1s elapsed): NOT timed out.
      // At sim_t=1003s (3s elapsed): timed out.
      // This must hold regardless of wall-clock speed ratio.
      auto mon = make_monitor();
      const double t_hb = epoch();                  // 1000.0
      const double t_ok = offset(t_hb, 1.0);       // 1001.0
      const double t_expired = offset(t_hb, 3.0);  // 1003.0

      mon.record_heartbeat(Src::kM7, t_hb);

      EXPECT_FALSE(mon.is_m7_timed_out(t_ok));
      EXPECT_TRUE(mon.is_m7_timed_out(t_expired));
  }
  ```

- [ ] **Step 3.8.6: Build and run M8 unit tests**

  ```bash
  docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c \
    "cd /opt/ws && colcon build --packages-select m8_hmi_transparency_bridge \
     && colcon test --packages-select m8_hmi_transparency_bridge \
     && colcon test-result --verbose 2>&1 | tail -30"
  ```
  Expected: All 9 unit tests PASS (`test_module_health_monitor` 9/9).

- [ ] **Step 3.8.7: Commit M8 consumer-side fix**

  ```bash
  git add \
    src/l3_tdl_kernel/m8_hmi_transparency_bridge/include/m8_hmi_transparency_bridge/module_health_monitor.hpp \
    src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/module_health_monitor.cpp \
    src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp \
    src/l3_tdl_kernel/m8_hmi_transparency_bridge/test/test_module_health_monitor.cpp
  git commit -m "fix(M8): ModuleHealthMonitor -> sim-time seconds; record from msg stamp not wall clock"
  ```

---

## Task 4: Audit docker Python nodes + TOR scope decision

### Task 4.1 — Docker Python node audit (no code changes)

The 4 docker Python nodes (`sil_topic_bridge`, `mock_l2_publisher`, `fsm_aggregator_node`, `diagnostic_mock_publisher`) use `create_timer` (not `create_wall_timer`) and are launched with `use_sim_time:=True`. The `sil_topic_bridge.py` uses `time.monotonic()` only for internal autopilot debounce. **No code changes needed.**

- [ ] **Step 4.1.1: Confirm no `create_wall_timer` in docker Python nodes**

  ```bash
  grep -rn "create_wall_timer" \
    /Users/marine/Code/MASS-L3-Tactical\ Layer/docker/*.py
  ```
  Expected: **No output**.

- [ ] **Step 4.1.2: Confirm all 4 nodes launch with `use_sim_time:=True`**

  ```bash
  grep "use_sim_time" \
    /Users/marine/Code/MASS-L3-Tactical\ Layer/docker/sil_entrypoint.sh | head -10
  ```
  Expected: Lines for all 4 `.py` nodes show `use_sim_time:=True`.

### Task 4.2 — TOR protocol: explicit scope decision (wall-clock RETAINED)

`TorProtocol` uses `std::chrono::steady_clock` for its `deadline_s = 60.0` countdown. This is **intentionally NOT converted** in Phase 1 for the following reason:

- **TOR is a human-factors deadline, not a simulation-physics deadline.** The 60-second Transfer-of-Responsibility window (IMO MASS Code C §12.4) measures real operator response time, not simulated time. If rate=10, an operator IRL has only 6 simulated seconds to acknowledge — that is a fundamentally different cognitive load and does not reproduce a valid safety scenario.
- Scope: `tor_protocol.cpp` / `tor_protocol.hpp` / `TorProtocol::TimePoint` remain `std::chrono::steady_clock` in Phase 1.
- Phase 2 decision: If TOR must be tested at accelerated rates (e.g. scenario regression without human operator), add a `sim_deadline_s` field separate from the real-time `deadline_s` and pick based on `use_sim_time`. Track as separate issue.

Similarly, `SatAggregator::Clock` (used for `last_receive_time` / `age_seconds` / `is_stale`) remains wall-clock in Phase 1. The SAT freshness check in `AdaptiveSatTrigger` is a UI responsiveness guard, not a deterministic control decision. Phase 2 scope.

- [ ] **Step 4.2.1: Document TOR and SAT scope decision (no code change)**

  Verify the two files are NOT changed:
  ```bash
  git diff HEAD -- \
    src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/tor_protocol.cpp \
    src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/sat_aggregator.cpp
  ```
  Expected: No output (no modifications).

- [ ] **Step 4.2.2: Commit audit + scope decision**

  ```bash
  git commit --allow-empty \
    -m "chore: audit docker/*.py + TOR scope decision -- wall-clock retained for human-factors deadline"
  ```

---

## Task 5: Full colcon Build Verification

- [ ] **Step 5.1: Full workspace build (preserves Docker BuildKit cache)**

  ```bash
  docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c \
    "cd /opt/ws && colcon build 2>&1 | tail -20"
  ```
  Expected: `Summary: N packages finished` — 0 new errors.

- [ ] **Step 5.2: Unit tests still pass**

  ```bash
  python -m pytest src/sim_workbench/sil_lifecycle/test/ -v
  ```
  Expected: All 8 tests PASS.

---

## Task 6: Integration Test — Confirm Green

- [ ] **Step 6.1: Restart sil-nodes container with new binaries**

  ```bash
  docker compose restart sil-nodes
  ```
  Wait ~10 seconds.

- [ ] **Step 6.2: Run RTF test**

  ```bash
  cd /Users/marine/Code/MASS-L3-Tactical\ Layer
  pytest tests/integration/sim_determinism/test_determinism.py \
    -m integration -v -k "test_rtf" 2>&1 | tail -20
  ```
  Expected: PASSED with RTF in [0.95, 1.05].

- [ ] **Step 6.3: Run cross-speed determinism test**

  ```bash
  pytest tests/integration/sim_determinism/test_determinism.py \
    -m integration -v -k "test_cross_speed" 2>&1 | tail -30
  ```
  Expected: PASSED — max pos < 1 m, max hdg < 0.1 deg, 0 mismatches.

- [ ] **Step 6.4: Qualitative 1x sanity check**

  Verify in docker logs that no new ERROR/FATAL lines appear:
  ```bash
  docker logs mass-l3-tacticallayer-sil-nodes-1 --since 5m 2>&1 | \
    grep -i "error\|fatal\|crash" | head -20
  ```
  Expected: No new errors related to timer setup or sim-time.

  Qualitative verification via Foxglove (if accessible): own-ship heading should not stay frozen at 0°.

- [ ] **Step 6.5: Final commit**

  ```bash
  git add -A
  git commit -m "test: integration tests GREEN -- determinism + RTF verified"
  ```

---

## Task 7: Pre-PR Verification

- [ ] **Step 7.1: Confirm no changes to forbidden paths**

  ```bash
  git diff main -- \
    '*/m5_tactical_planner/src/mid_mpc/mid_mpc_formulation*' \
    | head -5
  ```
  Expected: No output.

- [ ] **Step 7.2: Confirm backup salvage directory is unchanged**

  ```bash
  git diff main -- '*/m4_behavior_arbiter/.salvage-d3.1/*'
  ```
  Expected: No output.

- [ ] **Step 7.3: Review diff summary**

  ```bash
  git diff main --stat
  ```
  Expected changes confined to:
  - `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py`
  - `src/sim_workbench/sil_lifecycle/test/test_lifecycle_mgr.py`
  - `src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp`
  - `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp`
  - `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`
  - `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
  - `src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp`
  - `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`
  - `src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp`
  - `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp`
  - `src/l3_tdl_kernel/m8_hmi_transparency_bridge/include/m8_hmi_transparency_bridge/module_health_monitor.hpp` (modified)
  - `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/module_health_monitor.cpp` (modified)
  - `src/l3_tdl_kernel/m8_hmi_transparency_bridge/test/test_module_health_monitor.cpp` (modified)
  - `tests/integration/sim_determinism/` (new)
  - **NOT modified:** `tor_protocol.{hpp,cpp}`, `sat_aggregator.{hpp,cpp}` (wall-clock retained, see Task 4.2)

---

## Completion Criteria (spec §6)

| Criterion | Verification |
|---|---|
| Determinism test GREEN | Task 6.3 PASS |
| RTF(rate=1.0) in [0.95, 1.05] | Task 6.2 PASS |
| 1x qualitative no regression | Task 6.4 |
| M7 health state sequence identical at same sim_t across speeds | Task 6.3 (M7 timeout behavior captured in behavior/conflict columns) |
| M8 unit tests pass (9/9 incl. new rate-independence test) | Task 3.8.6 |
| Existing unit tests unbroken | Tasks 0.2, 5.2 |
| colcon build passes | Task 5.1 |
| Forbidden paths untouched | Task 7.1–7.2 |
| TOR + SAT aggregator scope documented | Task 4.2 |

---

## Decisions Made During Planning

1. **`/clock` publish frequency:** One publish per `dt_tick` step (inside the while loop), not one per callback. Ensures sim-time timers fire every tick without skipping at high sim_rate. At rate=10, clock publishes at 10×250=2500 msg/s — within LAN DDS capacity.

2. **`MAX_CATCHUP_TICKS = 10`:** Prevents catch-up storms. If the process lags > 10 ticks (40 ms simulated per 4 ms wall burst), it logs WARN and accepts brief RTF shortfall.

3. **M7 heartbeat + M8 health monitor: full sim-time on both ends.** M7 `timer_heartbeat_` converted to sim-time (Task 3.7). M8 `ModuleHealthMonitor` changed from `std::chrono::steady_clock::time_point` to `double` sim-seconds (Task 3.8B). Call sites record from `msg->stamp` (sim-time stamped by M7) and query with `get_clock()->now().seconds()`. Rationale: safety liveness checking IS a simulation behavior — if M7 crashes, `/clock` (independent node) keeps advancing, M8 computes age from the last sim-time stamp and correctly fires timeout. At rate=10 wall-clock heartbeat arrives at 100 Hz but sim-time advances at 10 Hz, so the sim-age judgment is identical to 1x. Half-fixing (only M7 emit, not M8 consume) would create clock-domain mismatch causing non-deterministic health sequences across speeds.

4. **TOR deadline: wall-clock RETAINED.** `TorProtocol::deadline_s = 60.0` measures real operator response time (IMO MASS Code C §12.4). Converting to sim-time would mean a rate=10 operator has 6 real seconds to respond — invalid safety scenario. Phase 2 work: add separate `sim_deadline_s` override for automated testing.

5. **`SatAggregator` freshness (age_seconds/is_stale): wall-clock RETAINED.** Used only for UI responsiveness checks in `AdaptiveSatTrigger` — not on the deterministic control path. Phase 2 scope.

6. **`sil_topic_bridge.py` `time.monotonic()` usage:** Kept. Used only for internal autopilot debounce (`_last_actuator_publish_time`) and pulse tracking — not for any ROS publish timing. Both ROS `create_timer` calls already use sim-time.
