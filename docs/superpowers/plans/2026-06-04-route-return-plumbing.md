# Route-Return Plumbing Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make own-ship, after a COLREG avoidance, actively cut back onto its *fixed original route line* via a controlled cross-track (XTE) intercept — instead of merely aiming at the next far waypoint along a route that rolls with the ship.

**Architecture:** Four independent breaks in the L2→L3 route plumbing must be fixed in dependency order. (1) The orchestrator must propagate `scenario_id` so `mock_l2_publisher` can load the scenario's route. (2) `mock_l2` auto-detect must search recursively. (3) `mock_l2` must publish a *stable* planned route (stop rebinding waypoint-0 to the ship's moving position) while keeping the VoyageTask departure ship-bound for M3. (4) The SIL bridge must actually populate `_route_wps` and apply the XTE correction with a feasible intercept gain.

**Tech Stack:** ROS2 (rclpy), Python; SIL runs in Docker on the A4000 server (`ssh a4000`, repo `~/Code/mass-l3`, branch `fix/m5-colreg-cost-formula`). `src/` and `docker/` are bind-mounted into the `sil-nodes` and `sil-orchestrator` containers. Python node changes under `docker/` take effect on `docker compose restart sil-nodes`; changes under `src/sil_orchestrator/` and `src/sim_workbench/` are baked into the orchestrator image and need `docker compose build sil-orchestrator && docker compose up -d sil-orchestrator`.

---

## Background — confirmed root causes (2026-06-04)

See memory `l3-route-return-plumbing-4-breaks`. Evidence from A4000 trace of `colreg-rule14-ho`:
- Ship heading after avoidance = `354.6°` = exactly `atan2(Δeast, Δnorth)` bearing to the next waypoint, with **zero XTE correction** even at ~900–1500 m east offset → bridge `_route_wps` empty.
- `/sil/lifecycle_status.scenario_id` = `''` → `mock_l2` logs `Lifecycle ACTIVE (no scenario_id) — auto-detecting` → non-recursive `glob` finds nothing in subdirs → `default_generation` hardcoded route.
- `mock_l2._get_effective_waypoints` rebinds WP0 to the live ship position → the route line rolls with the ship.

## Pre-flight (run once before Task 1)

- [ ] **Confirm clean baseline on A4000**

```bash
ssh a4000 'cd ~/Code/mass-l3 && git status -s && git rev-parse --abbrev-ref HEAD'
```
Expected: branch `fix/m5-colreg-cost-formula`; the circling fix commit `be0d99ff` present in `git log --oneline -3`.

---

## Task 1: Orchestrator propagates `scenario_id` (Break #1 — keystone)

**Files:**
- Modify: `src/sil_orchestrator/lifecycle_bridge.py` (method `configure`, ~lines 335–387)
- Reference (read-only): `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py:480-500` (`on_configure` reads `get_parameter("scenario_id")`), `:980-985` (LifecycleStatus broadcast sets `msg.scenario_id = self._fsm.scenario_id`)
- Test (integration, on A4000): echo of `/sil/lifecycle_status`

- [ ] **Step 1: Confirm the exact failure mechanism (instrument, do NOT guess)**

Add a temporary diagnostic log in `src/sil_orchestrator/lifecycle_bridge.py::configure`, immediately after `injection_map = _extract_injection_params(yaml_data)` (~line 362):

```python
        injection_map = _extract_injection_params(yaml_data)
        _log.warning("DIAG configure: scenario_id_arg=%r metadata_sid=%r inject_keys=%s",
                     scenario_id,
                     (yaml_data.get("metadata", {}) or {}).get("scenario_id"),
                     list(injection_map.keys()))
        _print_injection_summary(injection_map)
```

- [ ] **Step 2: Deploy + observe the diagnostic**

```bash
ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh >/dev/null 2>&1 && docker compose build sil-orchestrator && docker compose up -d sil-orchestrator && sleep 12'
ssh a4000 'cd ~/Code/mass-l3 && export ORCH_URL=https://127.0.0.1:18000 && python3 - <<PY
import json,ssl,urllib.request,time
B="https://127.0.0.1:18000/api/v1";C=ssl.create_default_context();C.check_hostname=False;C.verify_mode=ssl.CERT_NONE
req=lambda m,p,b=None: json.loads(urllib.request.urlopen(urllib.request.Request(B+p,data=(json.dumps(b).encode() if b else None),method=m,headers={"Content-Type":"application/json"}),context=C,timeout=20).read())
req("POST","/lifecycle/cleanup");time.sleep(2);print(req("POST","/lifecycle/configure",{"scenario_id":"colreg-rule14-ho"}))
PY'
ssh a4000 'docker logs --since 30s mass-l3-sil-sil-orchestrator-1 2>&1 | grep -iE "DIAG configure|inject /scenario_lifecycle_mgr" | tail'
```
Expected: `DIAG configure: scenario_id_arg='colreg-rule14-ho' metadata_sid='colreg-rule14-ho-001-v1.0' inject_keys=[... 'scenario_lifecycle_mgr' ...]`. This proves the injection map is built correctly and isolates the loss to the **inject-before-reset ordering** (the injected param is wiped by `_reset_to_unconfigured()` / re-declared to `""` by `on_configure`).

- [ ] **Step 3: Apply the fix — inject params AFTER reset, before CONFIGURE**

In `src/sil_orchestrator/lifecycle_bridge.py::configure`, move the parameter-injection block so it runs *after* `_reset_to_unconfigured()` and *before* `_change_state(TRANSITION_CONFIGURE)`. Replace the current ordering:

```python
        # Step 4: Inject params in parallel — fail-loud on first error
        if injection_map:
            tasks = [
                self._inject_params_to_node(node_name, params)
                for node_name, params in injection_map.items()
            ]
            results = await asyncio.gather(*tasks, return_exceptions=True)
            for result in results:
                if isinstance(result, ScenarioInjectionError):
                    raise result

        # Step 5-7: Original flow — reset, configure transition, broadcast
        reset = await self._reset_to_unconfigured()
        if not reset.success:
            return reset
        res = await self._change_state(Transition.TRANSITION_CONFIGURE)
```

with:

```python
        # Step 5: reset to UNCONFIGURED FIRST so the node's parameter store is a
        # clean slate, THEN inject (allow_undeclared_parameters=True lets us set
        # scenario_id before on_configure declares it; on_configure then sees
        # has_parameter(...)=True and keeps the injected value instead of the
        # "" default). Injecting before the reset let the cleanup wipe the param,
        # so the LifecycleStatus broadcast carried scenario_id="" and mock_l2
        # could never load the scenario route (route-return Break #1).
        reset = await self._reset_to_unconfigured()
        if not reset.success:
            return reset

        # Step 6: Inject params — fail-loud on first error
        if injection_map:
            tasks = [
                self._inject_params_to_node(node_name, params)
                for node_name, params in injection_map.items()
            ]
            results = await asyncio.gather(*tasks, return_exceptions=True)
            for result in results:
                if isinstance(result, ScenarioInjectionError):
                    raise result

        # Step 7: configure transition + broadcast
        res = await self._change_state(Transition.TRANSITION_CONFIGURE)
```

- [ ] **Step 4: Remove the temporary DIAG log from Step 1**

Delete the `_log.warning("DIAG configure: ...")` line added in Step 1.

- [ ] **Step 5: Rebuild orchestrator + verify scenario_id propagates**

```bash
ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh >/dev/null 2>&1 && docker compose build sil-orchestrator && docker compose up -d sil-orchestrator && sleep 12'
ssh a4000 'cd ~/Code/mass-l3 && export ORCH_URL=https://127.0.0.1:18000 && python3 - <<PY
import json,ssl,urllib.request,time
B="https://127.0.0.1:18000/api/v1";C=ssl.create_default_context();C.check_hostname=False;C.verify_mode=ssl.CERT_NONE
req=lambda m,p,b=None: json.loads(urllib.request.urlopen(urllib.request.Request(B+p,data=(json.dumps(b).encode() if b else None),method=m,headers={"Content-Type":"application/json"}),context=C,timeout=20).read())
req("POST","/lifecycle/cleanup");time.sleep(2);req("POST","/lifecycle/configure",{"scenario_id":"colreg-rule14-ho"});time.sleep(1);req("POST","/lifecycle/activate");time.sleep(4)
PY'
ssh a4000 'docker exec mass-l3-sil-sil-nodes-1 bash -lc "source /opt/ros/*/setup.bash 2>/dev/null; source /opt/ws/install/setup.bash 2>/dev/null; timeout 5 ros2 topic echo --once /sil/lifecycle_status 2>&1 | grep -iE \"current_state|scenario_id\""'
```
Expected: `current_state: 3` and **`scenario_id: colreg-rule14-ho-001-v1.0`** (no longer empty).

- [ ] **Step 6: Commit**

```bash
ssh a4000 'cd ~/Code/mass-l3 && git add src/sil_orchestrator/lifecycle_bridge.py && git commit -m "fix(orchestrator): inject scenario_id param after reset so LifecycleStatus broadcasts it

configure() injected node params before _reset_to_unconfigured(), so the
cleanup wiped the scenario_id param on scenario_lifecycle_mgr and on_configure
re-declared it to \"\". LifecycleStatus then broadcast scenario_id=\"\" and
mock_l2_publisher could never load the scenario route (route-return Break #1).
Reorder: reset → inject → CONFIGURE.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"'
```

---

## Task 2: mock_l2 auto-detect searches recursively (Break #2 — defense)

**Files:**
- Modify: `docker/mock_l2_publisher.py` (`_auto_detect_scenario`, ~line 338–354)

> With Task 1 done, mock_l2 takes the targeted `_load_scenario(sid)` path (already recursive `os.walk`). This task hardens the fallback so a missing scenario_id degrades to *finding* a YAML instead of the hardcoded default. Note: recursive auto-detect still picks the first scenario alphabetically — acceptable only as a last-resort fallback.

- [ ] **Step 1: Make the glob recursive**

In `docker/mock_l2_publisher.py::_auto_detect_scenario`, replace:

```python
    def _auto_detect_scenario(self):
        import glob as _glob
        yaml_files = sorted(_glob.glob(os.path.join(self._scenario_dir, "*.yaml")))
```

with:

```python
    def _auto_detect_scenario(self):
        import glob as _glob
        # Recursive: scenario YAMLs live in subdirs (COLREGs测试/, IMAZU标准测试/...).
        # A flat glob found nothing → fell back to the hardcoded default route.
        yaml_files = sorted(
            f for f in _glob.glob(os.path.join(self._scenario_dir, "**", "*.yaml"),
                                  recursive=True)
            if not os.path.basename(f).startswith(".")
            and os.path.basename(f) != "schema.yaml"
        )
```

- [ ] **Step 2: Deploy + verify no regression**

```bash
ssh a4000 'cd ~/Code/mass-l3 && docker compose restart sil-nodes && sleep 10'
```
Expected: container restarts clean (Task 5 does the behavioural verification).

- [ ] **Step 3: Commit**

```bash
ssh a4000 'cd ~/Code/mass-l3 && git add docker/mock_l2_publisher.py && git commit -m "fix(mock_l2): recursive scenario auto-detect glob (subdir YAMLs)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"'
```

---

## Task 3: mock_l2 publishes a stable route line (Break #3)

**Files:**
- Modify: `docker/mock_l2_publisher.py` (`_publish_planned_route` ~line 516–558; leave `_get_effective_waypoints` and `_publish_voyage_task` as-is)
- Reference (read-only): M3 mission manager route/departure validation — confirm whether M3 validates `PlannedRoute.route.poses[0]` against ownship before changing this. Search: `grep -rn "departure\|planned_route\|2.?km\|2000" src/l3_tdl_kernel/m3_*/`

- [ ] **Step 1: Confirm M3 does NOT reject a fixed-WP0 planned route**

```bash
grep -rn "departure\|2000\|2 km\|within\|planned_route\|poses\[0\]" src/l3_tdl_kernel/*m3* 2>/dev/null | head -30
```
Decision gate:
- If M3 only validates `VoyageTask.departure` (which stays ship-bound via `_publish_voyage_task`, untouched) → safe to proceed.
- If M3 *also* validates `PlannedRoute.route.poses[0]` against ownship → do NOT change WP0; instead skip to Task 4 (XTE) and revisit Break #3 with a different approach (e.g. emit the fixed track on a separate field). Record the finding and stop this task.

- [ ] **Step 2: Make planned_route use the fixed nominal waypoints (not the ship-rebound set)**

In `docker/mock_l2_publisher.py::_publish_planned_route`, replace the line that fetches the effective (rebound) waypoints:

```python
        waypoints, speeds = self._get_effective_waypoints()
```

with:

```python
        # Break #3 fix: the PlannedRoute must be the STABLE original track so the
        # L3 bridge can compute a meaningful cross-track error and steer back onto
        # it. _get_effective_waypoints() rebinds WP0 to the live ship position
        # (needed only for the VoyageTask departure check, handled separately in
        # _publish_voyage_task), which made the route line roll with the ship and
        # collapsed XTE to ~0. Publish the fixed nominal/default waypoints here.
        if self._yaml_waypoints and len(self._yaml_waypoints) >= 2:
            waypoints, speeds = self._yaml_waypoints, self._yaml_speeds_kn
        else:
            waypoints, speeds = self._get_effective_waypoints()
```

- [ ] **Step 3: Deploy + verify the published route is fixed (WP0 = scenario start, not ship pos)**

```bash
ssh a4000 'cd ~/Code/mass-l3 && docker compose restart sil-nodes && sleep 10 && export ORCH_URL=https://127.0.0.1:18000 && python3 - <<PY
import json,ssl,urllib.request,time
B="https://127.0.0.1:18000/api/v1";C=ssl.create_default_context();C.check_hostname=False;C.verify_mode=ssl.CERT_NONE
req=lambda m,p,b=None: json.loads(urllib.request.urlopen(urllib.request.Request(B+p,data=(json.dumps(b).encode() if b else None),method=m,headers={"Content-Type":"application/json"}),context=C,timeout=20).read())
req("POST","/lifecycle/cleanup");time.sleep(2);req("POST","/lifecycle/configure",{"scenario_id":"colreg-rule14-ho"});time.sleep(1);req("POST","/lifecycle/activate");time.sleep(5)
PY'
ssh a4000 'docker exec mass-l3-sil-sil-nodes-1 bash -lc "source /opt/ros/*/setup.bash 2>/dev/null; source /opt/ws/install/setup.bash 2>/dev/null; timeout 6 ros2 topic echo --once --qos-durability transient_local --qos-reliability reliable /l2/planned_route 2>&1 | grep -iE \"latitude|longitude|rationale\""'
```
Expected: `rationale: 'SIL_MOCK: YAML nominalRoute'`, WP0 = `(63.44, 10.38)` (the fixed start, NOT the ship's current lat/lon), WP1 = `(63.49, 10.38)`.

- [ ] **Step 4: Commit**

```bash
ssh a4000 'cd ~/Code/mass-l3 && git add docker/mock_l2_publisher.py && git commit -m "fix(mock_l2): publish fixed nominal route line, stop WP0 ship-rebind rolling

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"'
```

---

## Task 4: Bridge populates `_route_wps` and applies XTE intercept (Break #4)

**Files:**
- Modify: `docker/sil_topic_bridge.py` (`_on_planned_route` ~line 1255–1263; XTE gain in `_compute_transit_autopilot` ~line 1121–1124)
- Test: `tests/docker/test_sil_topic_bridge.py`

- [ ] **Step 1: Confirm WHY `_route_wps` is empty (remove the silent except)**

Temporarily replace the silent `except` in `docker/sil_topic_bridge.py::_on_planned_route`:

```python
    def _on_planned_route(self, msg: PlannedRoute) -> None:
        """Cache planned-route waypoints (lat, lon) for geometric cross-track error."""
        try:
            self._route_wps = [
                (float(p.pose.position.latitude), float(p.pose.position.longitude))
                for p in msg.route.poses
            ]
        except Exception:
            pass
```

with a logging version:

```python
    def _on_planned_route(self, msg: PlannedRoute) -> None:
        """Cache planned-route waypoints (lat, lon) for geometric cross-track error."""
        try:
            self._route_wps = [
                (float(p.pose.position.latitude), float(p.pose.position.longitude))
                for p in msg.route.poses
            ]
            self.get_logger().info(
                f"[BRIDGE] planned route cached: {len(self._route_wps)} wps "
                f"first={self._route_wps[0] if self._route_wps else None}")
        except Exception as exc:
            self.get_logger().warn(f"[BRIDGE] planned route parse failed: {exc!r}")
```

```bash
ssh a4000 'cd ~/Code/mass-l3 && docker compose restart sil-nodes && sleep 10 && export ORCH_URL=https://127.0.0.1:18000 && python3 - <<PY
import json,ssl,urllib.request,time
B="https://127.0.0.1:18000/api/v1";C=ssl.create_default_context();C.check_hostname=False;C.verify_mode=ssl.CERT_NONE
req=lambda m,p,b=None: json.loads(urllib.request.urlopen(urllib.request.Request(B+p,data=(json.dumps(b).encode() if b else None),method=m,headers={"Content-Type":"application/json"}),context=C,timeout=20).read())
req("POST","/lifecycle/cleanup");time.sleep(2);req("POST","/lifecycle/configure",{"scenario_id":"colreg-rule14-ho"});time.sleep(1);req("POST","/lifecycle/activate");time.sleep(5)
PY'
ssh a4000 'docker logs --since 25s mass-l3-sil-sil-nodes-1 2>&1 | grep -iE "planned route cached|planned route parse failed" | tail'
```
- If you see `parse failed: ...` → the field path is wrong; read the actual message type (`ros2 interface show geographic_msgs/msg/GeoPath` and `.../GeoPoseStamped`) and correct the `p.pose.position.latitude` access in the next step.
- If you see `cached: 2 wps first=(63.44, 10.38)` → parse is fine; `_route_wps` populates once Break #1/#3 deliver the route. The remaining issue is gain (Step 3).

- [ ] **Step 2: Keep the diagnostic log (it is low-volume and useful); fix the field path only if Step 1 showed a parse failure**

If Step 1 showed a parse failure, change `p.pose.position.latitude/longitude` to the correct path discovered from `ros2 interface show`. Otherwise leave the parse body unchanged (the logging added in Step 1 stays).

- [ ] **Step 3: Add a failing unit test for the XTE intercept gain**

In `tests/docker/test_sil_topic_bridge.py`, add:

```python
def test_xte_intercept_steers_back_to_route_line(monkeypatch):
    """A ship offset east of a due-north route line must steer WEST of the
    waypoint bearing (controlled intercept), saturating for large offsets."""
    bridge = _load_bridge(monkeypatch)
    from unittest.mock import Mock
    fake_self = SimpleNamespace(
        _last_ownship_raw=SimpleNamespace(heading=0.0, sog=12.0, rot=0.0,
                                          lat=63.46, lon=10.41),  # ~1.5 km east of lon 10.38
        _target_heading_deg=0.0, _target_sog_kn=12.0,
        _current_target_wp_lat=63.49, _current_target_wp_lon=10.38,
        _route_wps=[(63.44, 10.38), (63.49, 10.38)],
        _heading_controller=Mock(step=lambda err, dt, rot: err),  # passthrough = effective heading error
        _speed_controller=Mock(step=lambda err, dt: 0.5),
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        _signed_xte_m=lambda lat, lon: bridge.SilTopicBridge._signed_xte_m(fake_self, lat, lon),
        _great_circle_bearing=staticmethod(bridge.SilTopicBridge._great_circle_bearing),
    )
    out = bridge.SilTopicBridge._compute_transit_autopilot(fake_self, SimpleNamespace(sec=0))
    # heading_controller passthrough returns the effective heading error (target - current=0).
    # For a ship east of the line, effective target heading must be < 360 by MORE than the
    # bare waypoint bearing, i.e. a hard port (west) correction. With xte≈-1500 m and gain
    # 0.10 the correction saturates at -30°, so effective error ≈ bearing(-~5°) + (-30°).
    eff_err = out.rudder_angle  # RUDDER_SIGN * passthrough(heading_error)
    assert eff_err < -25.0, f"expected strong westward intercept, got {eff_err}"
```

- [ ] **Step 4: Run it — expect FAIL (current gain 0.10 already saturates, but confirm the wiring)**

```bash
ssh a4000 'cd ~/Code/mass-l3 && python3 -m pytest tests/docker/test_sil_topic_bridge.py::test_xte_intercept_steers_back_to_route_line -o addopts="" -q'
```
Expected: FAIL or PASS — if PASS, the gain is already adequate and Break #4 is purely the empty-`_route_wps` issue (resolved by Tasks 1/3). If FAIL because correction is too weak, proceed to Step 5. (RUDDER_SIGN handling may flip the sign; adjust the assertion to match the bridge's convention discovered while reading `_compute_transit_autopilot`.)

- [ ] **Step 5: (If needed) steepen the intercept gain within rudder feasibility**

In `docker/sil_topic_bridge.py::_compute_transit_autopilot`, the current correction is:

```python
            xte_correction = max(-30.0, min(30.0, xte_m * 0.10))
```
If Step 4 shows the ship rejoins too slowly in the Task 5 retest, raise the clamp to allow a steeper intercept (still rudder-feasible — the `_heading_controller` rate-limits the rudder):

```python
            # Steeper intercept so the ship returns to the original track promptly
            # (rudder rate limit in _heading_controller keeps it feasible).
            xte_correction = max(-45.0, min(45.0, xte_m * 0.15))
```
Only apply this if Task 5's trajectory shows an unacceptably shallow return; otherwise keep 0.10/±30°.

- [ ] **Step 6: Run unit tests (all green)**

```bash
ssh a4000 'cd ~/Code/mass-l3 && python3 -m pytest tests/docker/test_sil_topic_bridge.py -o addopts="" -q'
```
Expected: all pass (the 6 existing + the new intercept test).

- [ ] **Step 7: Commit**

```bash
ssh a4000 'cd ~/Code/mass-l3 && git add docker/sil_topic_bridge.py tests/docker/test_sil_topic_bridge.py && git commit -m "fix(bridge): log+confirm planned-route caching, XTE intercept back to route line

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"'
```

---

## Task 5: Integration retest — full route return on A4000

**Files:**
- Reference: `scripts/_retest_spinfix.py` (already on A4000; extend its trajectory print)

- [ ] **Step 1: Clean full restart so the L2 route handshake is healthy**

```bash
ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh >/dev/null 2>&1 && docker compose restart sil-nodes sil-orchestrator && sleep 25'
```

- [ ] **Step 2: Run one 15-min (sim) cycle and dump the lon trajectory**

```bash
ssh a4000 'cd ~/Code/mass-l3 && export ORCH_URL=https://127.0.0.1:18000 && RUN_WALL=150 python3 scripts/_retest_spinfix.py 2>&1 | tail -15'
ssh a4000 'cd ~/Code/mass-l3 && python3 - <<PY
import json
osh=[json.loads(l) for l in open("runs/trace_current.jsonl") if l.strip()]
osh=[r for r in osh if r.get("topic")=="/sil/own_ship_state" and r.get("heading_deg") is not None]
for r in osh[::max(1,len(osh)//16)]:
    print("t=%.0f lon=%.5f hdg=%.1f off_m=%.0f"%(r["sim_t"],r["lon"],r["heading_deg"],(r["lon"]-10.38)*111320*0.448))
PY'
```

- [ ] **Step 3: Verify acceptance criteria**

Expected from the trajectory:
- During avoidance: lon drifts east (offset grows to ~1–1.5 km), heading reaches ~60°.
- After avoidance: heading goes **below 354°** (a clear westward intercept, not just ~355°), and `off_m` shrinks back toward **< 100 m within the 15-min run** (vs the old behaviour where it took ~50 min sim to reach 153 m).
- `final avoidance_active: False`, `|loops| < 1` (no circling — Task-independent regression guard).

- [ ] **Step 4: Sync the branch (three ends) per CLAUDE.md §13 — only after user confirms**

```bash
# Surface the commits to the user; do NOT push without explicit go-ahead.
ssh a4000 'cd ~/Code/mass-l3 && git log --oneline -6'
```

---

## Self-review notes

- Spec coverage: Break #1 → Task 1; #2 → Task 2; #3 → Task 3; #4 → Task 4; integration → Task 5. All four covered.
- Dependency order respected: #1 (scenario_id) gates the route loading that #3/#4 rely on; #2 is defense; verification is end-to-end in Task 5.
- Decision gates included where behaviour is not yet 100% confirmed (Task 1 Step 2 mechanism check; Task 3 Step 1 M3 coupling; Task 4 Step 1 parse-vs-gain split) — execution must honour these and stop/adjust rather than guess.
- Deploy mechanics: `docker/` changes → `restart sil-nodes`; `src/sil_orchestrator/` changes → `build sil-orchestrator && up -d`. Stated per task.
