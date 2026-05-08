# D1.1 ROS2 Workspace + IDL Messages Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `schema_version` field to all 27 top-level .msg files (16 l3_msgs + 11 l3_external_msgs), wire schema_version assignments into the mock publisher, and verify colcon build + topic frequencies against the D1.1 DoD.

**Architecture:** Both message packages already exist with correct CMakeLists.txt structure (l3_external_msgs correctly depends on l3_msgs via `find_package(l3_msgs REQUIRED)`). The change is additive: insert one field at the top of 27 .msg files and assign it in 11 mock publisher methods. Sub-messages (9 in l3_msgs + TimeWindow in l3_external_msgs) are fully exempt.

**Tech Stack:** ROS2 Jazzy, colcon, Python 3.11, rosidl_default_generators, ament_cmake

---

## Parallel Execution Map

```
Wave 1 (parallel):
  Task 1: l3_msgs IDL patch      ─┐
  Task 2: l3_external_msgs patch  ─┴─→ Wave 2

Wave 2 (parallel, after Wave 1):
  Task 3: colcon build verify    ─┐
  Task 4: mock publisher patch   ─┴─→ Wave 3

Wave 3 (sequential):
  Task 5: run verification + closure checklist
```

**DEPENDENCY NOTE for subagent dispatch:**
- Task 1 and Task 2: no mutual dependency → dispatch in parallel
- Task 3 and Task 4: both depend on Task 1 AND Task 2 completing → dispatch in parallel only after both Wave 1 tasks are done
- Task 5: depends on Task 3 AND Task 4 → dispatch after both Wave 2 tasks are done

---

## File Map

| File | Action | Task |
|---|---|---|
| `src/l3_msgs/msg/ASDRRecord.msg` | insert schema_version after comment block | Task 1 |
| `src/l3_msgs/msg/AvoidancePlan.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/BehaviorPlan.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/COLREGsConstraint.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/MissionGoal.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/ModeCmd.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/ODDState.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/OwnShipState.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/ReactiveOverrideCmd.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/RouteReplanRequest.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/SATData.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/SafetyAlert.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/ToRRequest.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/TrackedTarget.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/UIState.msg` | insert schema_version | Task 1 |
| `src/l3_msgs/msg/WorldState.msg` | insert schema_version | Task 1 |
| `src/l3_external_msgs/msg/CheckerVetoNotification.msg` | insert schema_version | Task 2 |
| `src/l3_external_msgs/msg/EmergencyCommand.msg` | insert schema_version | Task 2 |
| `src/l3_external_msgs/msg/EnvironmentState.msg` | insert schema_version | Task 2 |
| `src/l3_external_msgs/msg/FilteredOwnShipState.msg` | insert schema_version | Task 2 |
| `src/l3_external_msgs/msg/OverrideActiveSignal.msg` | insert schema_version | Task 2 |
| `src/l3_external_msgs/msg/PlannedRoute.msg` | insert schema_version | Task 2 |
| `src/l3_external_msgs/msg/ReflexActivationNotification.msg` | insert schema_version | Task 2 |
| `src/l3_external_msgs/msg/ReplanResponse.msg` | insert schema_version | Task 2 |
| `src/l3_external_msgs/msg/SpeedProfile.msg` | insert schema_version | Task 2 |
| `src/l3_external_msgs/msg/TrackedTargetArray.msg` | insert schema_version | Task 2 |
| `src/l3_external_msgs/msg/VoyageTask.msg` | insert schema_version | Task 2 |
| `src/l3_external_msgs/msg/TimeWindow.msg` | **EXEMPT** — pure sub-message | — |
| `src/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py` | add schema_version assignments in 11 publish methods | Task 4 |

**Exempt sub-messages (do NOT patch):**
- l3_msgs: `AvoidanceWaypoint.msg`, `Constraint.msg`, `EncounterClassification.msg`, `RuleActive.msg`, `SAT1Data.msg`, `SAT2Data.msg`, `SAT3Data.msg`, `SpeedSegment.msg`, `ZoneConstraint.msg`
- l3_external_msgs: `TimeWindow.msg`

---

## Task 1: l3_msgs IDL Patch

**WAVE 1 — run in parallel with Task 2**

**Files:**
- Modify: `src/l3_msgs/msg/*.msg` (16 top-level files listed in File Map above)

- [ ] **Step 1: Verify current state fails DoD (pre-check)**

```bash
grep -rl "schema_version" src/l3_msgs/msg/*.msg | wc -l
```

Expected output: `0` (no files currently have schema_version)

Also verify sub-messages will NOT be patched by checking they exist:
```bash
ls src/l3_msgs/msg/AvoidanceWaypoint.msg src/l3_msgs/msg/Constraint.msg src/l3_msgs/msg/EncounterClassification.msg src/l3_msgs/msg/RuleActive.msg src/l3_msgs/msg/SAT1Data.msg src/l3_msgs/msg/SAT2Data.msg src/l3_msgs/msg/SAT3Data.msg src/l3_msgs/msg/SpeedSegment.msg src/l3_msgs/msg/ZoneConstraint.msg
```

Expected: all 9 files listed (they exist and must remain unpatchd)

- [ ] **Step 2: Write the patch script**

Create file `scripts/patch_schema_version.py`:

```python
#!/usr/bin/env python3
"""Insert schema_version field into top-level ROS2 .msg files.

Usage: python3 scripts/patch_schema_version.py <msg_file> [<msg_file> ...]

Inserts: string schema_version  # default: "v1.1.2"
Position: after the last leading comment line, before the first field.
"""
import sys
from pathlib import Path


def patch_msg_file(path: Path) -> bool:
    """Return True if file was modified, False if already patched."""
    text = path.read_text(encoding="utf-8")
    if 'schema_version' in text:
        print(f"  SKIP (already patched): {path}")
        return False

    lines = text.splitlines(keepends=True)
    # Find insertion point: first line that is not a comment and not blank
    insert_at = len(lines)  # fallback: append
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped and not stripped.startswith('#'):
            insert_at = i
            break

    schema_line = 'string schema_version  # default: "v1.1.2"\n'
    lines.insert(insert_at, schema_line)
    path.write_text(''.join(lines), encoding="utf-8")
    print(f"  PATCHED: {path} (inserted at line {insert_at + 1})")
    return True


def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: patch_schema_version.py <msg_file> [...]")
        sys.exit(1)

    patched = 0
    for arg in sys.argv[1:]:
        p = Path(arg)
        if not p.exists():
            print(f"  ERROR: {p} not found", file=sys.stderr)
            sys.exit(1)
        if patch_msg_file(p):
            patched += 1

    print(f"\nDone. {patched}/{len(sys.argv) - 1} files patched.")


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Run patch on all 16 top-level l3_msgs**

```bash
python3 scripts/patch_schema_version.py \
  src/l3_msgs/msg/ASDRRecord.msg \
  src/l3_msgs/msg/AvoidancePlan.msg \
  src/l3_msgs/msg/BehaviorPlan.msg \
  src/l3_msgs/msg/COLREGsConstraint.msg \
  src/l3_msgs/msg/MissionGoal.msg \
  src/l3_msgs/msg/ModeCmd.msg \
  src/l3_msgs/msg/ODDState.msg \
  src/l3_msgs/msg/OwnShipState.msg \
  src/l3_msgs/msg/ReactiveOverrideCmd.msg \
  src/l3_msgs/msg/RouteReplanRequest.msg \
  src/l3_msgs/msg/SATData.msg \
  src/l3_msgs/msg/SafetyAlert.msg \
  src/l3_msgs/msg/ToRRequest.msg \
  src/l3_msgs/msg/TrackedTarget.msg \
  src/l3_msgs/msg/UIState.msg \
  src/l3_msgs/msg/WorldState.msg
```

Expected: `Done. 16/16 files patched.`

- [ ] **Step 4: Verify patch correctness**

```bash
# Count: must be 16
grep -rl "schema_version" src/l3_msgs/msg/*.msg | wc -l
```
Expected: `16`

```bash
# Verify field appears correctly in sample file
head -5 src/l3_msgs/msg/ODDState.msg
```
Expected output (schema_version inserted after last # comment line):
```
# Per v1.1.2 §15.1 ODD_StateMsg + §3.5 state machine + §3.6 Conformance_Score
# Frequency: 1 Hz + event-driven补发 (EDGE→OUT 不等周期)
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp
```

```bash
# Verify ModeCmd (2-line comment block)
head -4 src/l3_msgs/msg/ModeCmd.msg
```
Expected:
```
# M1 ODD/Envelope Manager → M4 Behavior Arbiter (event-driven)
# Communicates the commanded operational mode based on ODD state
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp
```

```bash
# Verify sub-messages are NOT patched (must exit 1)
grep "schema_version" src/l3_msgs/msg/AvoidanceWaypoint.msg
echo "exit code: $?"
```
Expected: output empty, `exit code: 1`

```bash
grep "schema_version" src/l3_msgs/msg/ZoneConstraint.msg
echo "exit code: $?"
```
Expected: output empty, `exit code: 1`

```bash
# ASDRRecord: schema_version present (not exempt — only confidence is exempt)
grep "schema_version" src/l3_msgs/msg/ASDRRecord.msg
```
Expected: `string schema_version  # default: "v1.1.2"`

- [ ] **Step 5: Commit**

```bash
git add src/l3_msgs/msg/ASDRRecord.msg \
        src/l3_msgs/msg/AvoidancePlan.msg \
        src/l3_msgs/msg/BehaviorPlan.msg \
        src/l3_msgs/msg/COLREGsConstraint.msg \
        src/l3_msgs/msg/MissionGoal.msg \
        src/l3_msgs/msg/ModeCmd.msg \
        src/l3_msgs/msg/ODDState.msg \
        src/l3_msgs/msg/OwnShipState.msg \
        src/l3_msgs/msg/ReactiveOverrideCmd.msg \
        src/l3_msgs/msg/RouteReplanRequest.msg \
        src/l3_msgs/msg/SATData.msg \
        src/l3_msgs/msg/SafetyAlert.msg \
        src/l3_msgs/msg/ToRRequest.msg \
        src/l3_msgs/msg/TrackedTarget.msg \
        src/l3_msgs/msg/UIState.msg \
        src/l3_msgs/msg/WorldState.msg \
        scripts/patch_schema_version.py
git commit -m "feat(d1.1): add schema_version to 16 top-level l3_msgs (F P1-F-04)"
```

---

## Task 2: l3_external_msgs IDL Patch

**WAVE 1 — run in parallel with Task 1**

**Files:**
- Modify: `src/l3_external_msgs/msg/*.msg` (11 top-level files; TimeWindow exempt)

- [ ] **Step 1: Verify current state fails DoD (pre-check)**

```bash
grep -rl "schema_version" src/l3_external_msgs/msg/*.msg | wc -l
```
Expected: `0`

```bash
# TimeWindow must be exempt — verify it exists
ls src/l3_external_msgs/msg/TimeWindow.msg
```
Expected: file listed (exists, must NOT be patched)

- [ ] **Step 2: Confirm patch script exists**

The `scripts/patch_schema_version.py` is created by Task 1. If running this task before Task 1 completes, create the script from Task 1 Step 2 first.

```bash
ls scripts/patch_schema_version.py
```
Expected: file exists. If not, create it now from the code in Task 1 Step 2.

- [ ] **Step 3: Run patch on all 11 top-level l3_external_msgs**

```bash
python3 scripts/patch_schema_version.py \
  src/l3_external_msgs/msg/CheckerVetoNotification.msg \
  src/l3_external_msgs/msg/EmergencyCommand.msg \
  src/l3_external_msgs/msg/EnvironmentState.msg \
  src/l3_external_msgs/msg/FilteredOwnShipState.msg \
  src/l3_external_msgs/msg/OverrideActiveSignal.msg \
  src/l3_external_msgs/msg/PlannedRoute.msg \
  src/l3_external_msgs/msg/ReflexActivationNotification.msg \
  src/l3_external_msgs/msg/ReplanResponse.msg \
  src/l3_external_msgs/msg/SpeedProfile.msg \
  src/l3_external_msgs/msg/TrackedTargetArray.msg \
  src/l3_external_msgs/msg/VoyageTask.msg
```
Expected: `Done. 11/11 files patched.`

- [ ] **Step 4: Verify patch correctness**

```bash
# Count: must be 11
grep -rl "schema_version" src/l3_external_msgs/msg/*.msg | wc -l
```
Expected: `11`

```bash
# Verify FilteredOwnShipState (50 Hz, 3-line comment block)
head -5 src/l3_external_msgs/msg/FilteredOwnShipState.msg
```
Expected:
```
# Multimodal Fusion Nav Filter → M2 World Model (50 Hz)
# Per architecture v1.1.2 §15.1 + §6 + RFC-002 resolution
# Wave 0 fix: F-IMP-B-008 — added nav_mode (RFC-002 required), current_speed/direction
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp
```

```bash
# Verify ReflexActivationNotification (4-line comment block with continuation)
head -6 src/l3_external_msgs/msg/ReflexActivationNotification.msg
```
Expected:
```
# Y-axis Reflex Arc → M1 ODD/Envelope Manager (event-driven)
# Per architecture v1.1.2 §15.1 ReflexActivationNotification + RFC-005 resolution
# Wave 0 fix: F-CRIT-B-005 — fields rewritten to match v1.1.2 IDL
#                            critical: l3_freeze_required is required by M1 state machine (§11.8)
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time activation_time
```

```bash
# TimeWindow MUST NOT be patched (exit 1)
grep "schema_version" src/l3_external_msgs/msg/TimeWindow.msg
echo "exit code: $?"
```
Expected: output empty, `exit code: 1`

- [ ] **Step 5: Commit**

```bash
git add \
  src/l3_external_msgs/msg/CheckerVetoNotification.msg \
  src/l3_external_msgs/msg/EmergencyCommand.msg \
  src/l3_external_msgs/msg/EnvironmentState.msg \
  src/l3_external_msgs/msg/FilteredOwnShipState.msg \
  src/l3_external_msgs/msg/OverrideActiveSignal.msg \
  src/l3_external_msgs/msg/PlannedRoute.msg \
  src/l3_external_msgs/msg/ReflexActivationNotification.msg \
  src/l3_external_msgs/msg/ReplanResponse.msg \
  src/l3_external_msgs/msg/SpeedProfile.msg \
  src/l3_external_msgs/msg/TrackedTargetArray.msg \
  src/l3_external_msgs/msg/VoyageTask.msg
git commit -m "feat(d1.1): add schema_version to 11 top-level l3_external_msgs (F P1-F-04)"
```

---

## Task 3: Colcon Build Verification

**WAVE 2 — run in parallel with Task 4. Depends on Task 1 AND Task 2 complete.**

**Files:**
- Read: `src/l3_msgs/CMakeLists.txt`
- Read: `src/l3_external_msgs/CMakeLists.txt`

> **RISK R1.1 (medium):** `geographic_msgs` package name may differ on ROS2 Jazzy. Run Step 1 before anything.
> **RISK R1.2 (medium):** build order may fail if l3_msgs not installed before l3_external_msgs. Use `--packages-up-to` form.

- [ ] **Step 1: Verify ROS environment and package names**

```bash
echo $ROS_DISTRO
```
Expected: `jazzy`

```bash
apt-cache show ros-jazzy-geographic-msgs 2>/dev/null | grep "Package:" || echo "NOT_FOUND_check_apt"
```
If NOT_FOUND: `sudo apt install ros-jazzy-geographic-msgs` then re-run.

```bash
source /opt/ros/jazzy/setup.bash
ros2 pkg list | grep geographic
```
Expected: `geographic_msgs` is listed.

- [ ] **Step 2: Inspect CMakeLists.txt for correctness (no edits — read-only)**

```bash
grep -n "find_package\|ament_export_dependencies\|rosidl_generate" src/l3_msgs/CMakeLists.txt
```
Expected output must contain:
```
find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(geographic_msgs REQUIRED)
```
And:
```
rosidl_generate_interfaces(${PROJECT_NAME}
```
And:
```
ament_export_dependencies(rosidl_default_runtime)
```

```bash
grep -n "find_package\|ament_export_dependencies\|rosidl_generate\|l3_msgs" src/l3_external_msgs/CMakeLists.txt
```
Expected output must contain:
- `find_package(l3_msgs REQUIRED)` — critical dependency
- `DEPENDENCIES builtin_interfaces geometry_msgs geographic_msgs l3_msgs` in rosidl_generate_interfaces
- `ament_export_dependencies(rosidl_default_runtime l3_msgs)` — exports l3_msgs downstream

If any of these are missing, report the gap and stop — do not modify CMakeLists.txt without explicit direction (this task is verification only).

- [ ] **Step 3: Build l3_msgs alone**

```bash
source /opt/ros/jazzy/setup.bash
cd $(git rev-parse --show-toplevel)
colcon build --packages-select l3_msgs 2>&1 | tee /tmp/colcon_l3_msgs.log
echo "EXIT: $?"
```
Expected: `EXIT: 0` and log shows `Finished <<< l3_msgs`

```bash
# No warnings in stderr
grep -i "warning" /tmp/colcon_l3_msgs.log
```
Expected: no output (no warnings).

If build fails with `geographic_msgs not found`: install with `sudo apt install ros-jazzy-geographic-msgs` and retry.

- [ ] **Step 4: Build l3_external_msgs (pulls l3_msgs automatically)**

```bash
source /opt/ros/jazzy/setup.bash
cd $(git rev-parse --show-toplevel)
colcon build --packages-up-to l3_external_msgs 2>&1 | tee /tmp/colcon_l3_external.log
echo "EXIT: $?"
```
Expected: `EXIT: 0` and log shows both packages finishing.

```bash
grep -i "warning" /tmp/colcon_l3_external.log
```
Expected: no output.

- [ ] **Step 5: Run combined DoD colcon build**

```bash
source /opt/ros/jazzy/setup.bash
cd $(git rev-parse --show-toplevel)
colcon build --packages-select l3_msgs l3_external_msgs 2>&1 | tee /tmp/colcon_final.log
echo "EXIT: $?"
```
Expected: `EXIT: 0`

```bash
grep -i "warning" /tmp/colcon_final.log
```
Expected: empty.

- [ ] **Step 6: D0 CI grep protection (must NOT find vessel-specific constants)**

```bash
grep -rE "(\bFCB\b|45\s*m\b)" src/l3_msgs/ src/l3_external_msgs/
echo "exit: $?"
```
Expected: no output printed, `exit: 1` — the grep should find nothing (exit 1 means no match).

- [ ] **Step 7: Create evidence directory and save build log**

```bash
mkdir -p "docs/Design/Detailed Design/D1.1-ros2-workspace/evidence"
cp /tmp/colcon_final.log "docs/Design/Detailed Design/D1.1-ros2-workspace/evidence/colcon-build-ok.log"
```

Note: `colcon-build-ok.png` in the spec refers to a screenshot. If running headlessly, the log file is the equivalent evidence. Capture a screenshot manually if a GUI is available.

- [ ] **Step 8: Commit evidence**

```bash
git add "docs/Design/Detailed Design/D1.1-ros2-workspace/evidence/"
git commit -m "chore(d1.1): colcon build verified exit 0 — evidence saved (F P1-F-06)"
```

---

## Task 4: Mock Publisher schema_version Patch

**WAVE 2 — run in parallel with Task 3. Depends on Task 1 AND Task 2 complete.**

**Files:**
- Modify: `src/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py`

> **RISK R1.4 (medium):** stale `__pycache__` bytecode from an older Python version can cause import errors. Clean it in Step 1.

- [ ] **Step 1: Clean stale bytecode**

```bash
find src/l3_external_mock_publisher -name "*.pyc" -delete
find src/l3_external_mock_publisher -name "__pycache__" -type d
```
Expected: no directories printed after the find -delete pass.

- [ ] **Step 2: Verify current state fails DoD (pre-check)**

```bash
grep -c 'schema_version = "v1.1.2"' src/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py
```
Expected: `0`

- [ ] **Step 3: Apply schema_version assignments to all 11 publish methods**

Edit `src/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py`.

In each `_publish_*` method, add `msg.schema_version = "v1.1.2"` immediately after the line `msg = MsgType()` (the object instantiation). Below is the complete set of changes:

**`_publish_voyage_task`** — add after `msg = VoyageTask()`:
```python
    def _publish_voyage_task(self, stamp: Time) -> None:
        msg = VoyageTask()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.task_id = self._counter
        # ... rest unchanged
```

**`_publish_planned_route`** — add after `msg = PlannedRoute()`:
```python
    def _publish_planned_route(self, stamp: Time) -> None:
        msg = PlannedRoute()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        # ... rest unchanged
```

**`_publish_speed_profile`** — add after `msg = SpeedProfile()`:
```python
    def _publish_speed_profile(self, stamp: Time) -> None:
        msg = SpeedProfile()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        # ... rest unchanged
```

**`_publish_targets`** — add after `msg = TrackedTargetArray()` (the container; individual `TrackedTarget` objects inside do NOT get schema_version — they are from l3_msgs and l3_msgs.TrackedTarget already has schema_version as a field but the publisher does not need to assign it since TrackedTarget is embedded):
```python
    def _publish_targets(self, stamp: Time) -> None:
        msg = TrackedTargetArray()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        # ... rest unchanged (the inner TrackedTarget loop is unchanged)
```

**`_publish_ownship`** — add after `msg = FilteredOwnShipState()`:
```python
    def _publish_ownship(self, stamp: Time) -> None:
        msg = FilteredOwnShipState()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        # ... rest unchanged
```

**`_publish_environment`** — add after `msg = EnvironmentState()`:
```python
    def _publish_environment(self, stamp: Time) -> None:
        msg = EnvironmentState()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        # ... rest unchanged
```

**`_publish_replan_response`** — add after `msg = ReplanResponse()`:
```python
    def _publish_replan_response(self, stamp: Time) -> None:
        msg = ReplanResponse()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        # ... rest unchanged
```

**`_publish_veto`** — add after `msg = CheckerVetoNotification()`:
```python
    def _publish_veto(self, stamp: Time) -> None:
        msg = CheckerVetoNotification()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        # ... rest unchanged
```

**`_publish_reflex_state`** — add after `msg = ReflexActivationNotification()`:
```python
    def _publish_reflex_state(self, stamp: Time) -> None:
        msg = ReflexActivationNotification()
        msg.schema_version = "v1.1.2"
        msg.activation_time = stamp
        # ... rest unchanged
```

**`_publish_override_signal`** — add after `msg = OverrideActiveSignal()`:
```python
    def _publish_override_signal(self, stamp: Time) -> None:
        msg = OverrideActiveSignal()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        # ... rest unchanged
```

**`_publish_emergency_command`** — add after `msg = EmergencyCommand()`:
```python
    def _publish_emergency_command(self, stamp: Time) -> None:
        msg = EmergencyCommand()
        msg.schema_version = "v1.1.2"
        msg.trigger_time = stamp
        # ... rest unchanged
```

- [ ] **Step 4: Verify all 11 assignments are present**

```bash
grep -c 'schema_version = "v1.1.2"' src/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py
```
Expected: `11`

```bash
# Quick sanity: each publish method has the assignment
grep -A2 "def _publish_" src/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py | grep "schema_version" | wc -l
```
Expected: `11`

- [ ] **Step 5: Static syntax check**

```bash
python3 -m py_compile src/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py
echo "syntax OK: $?"
```
Expected: `syntax OK: 0`

- [ ] **Step 6: Commit**

```bash
git add src/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py
git commit -m "feat(d1.1): assign schema_version='v1.1.2' in all 11 mock publisher methods"
```

---

## Task 5: Mock Publisher Run Verification + Closure Checklist

**WAVE 3 — sequential. Depends on Task 3 AND Task 4 complete.**

**Files:**
- Read/modify: `docs/Design/Detailed Design/D1.1-ros2-workspace/01-spec.md` (fill closure checklist §10)

> **Prerequisites:** colcon build completed (Task 3) and schema_version assignments applied (Task 4).

- [ ] **Step 1: Source workspace and run pre-flight checks**

```bash
source /opt/ros/jazzy/setup.bash
cd $(git rev-parse --show-toplevel)
source install/setup.bash
```

```bash
# Verify the mock publisher package built correctly
ros2 pkg list | grep l3_external_mock_publisher
```
Expected: `l3_external_mock_publisher` listed.

If not listed: run `colcon build --packages-select l3_external_mock_publisher` first (requires Task 3's colcon build to have run).

- [ ] **Step 2: Launch mock publisher in background**

```bash
ros2 run l3_external_mock_publisher external_mock_publisher &
MOCK_PID=$!
sleep 3  # allow timers to warm up
echo "Mock publisher running, PID=$MOCK_PID"
```

- [ ] **Step 3: Verify topic list**

```bash
ros2 topic list | grep -E "/fusion/|/l1/|/l2/|/checker/|/reflex/|/override/"
```
Expected output (all 11 external topics):
```
/checker/veto_notification
/fusion/environment_state
/fusion/own_ship_state
/fusion/tracked_targets
/l1/voyage_task
/l2/planned_route
/l2/replan_response
/l2/speed_profile
/override/active_signal
/reflex/activation_notification
/reflex/emergency_command
```

- [ ] **Step 4: Verify 50 Hz (FilteredOwnShipState)**

```bash
timeout 12 ros2 topic hz /fusion/own_ship_state 2>&1 | tail -5
```
Expected output contains line like: `average rate: 50.0XX` (acceptable range: 47.5–52.5 Hz)

- [ ] **Step 5: Verify 2 Hz (TrackedTargetArray)**

```bash
timeout 8 ros2 topic hz /fusion/tracked_targets 2>&1 | tail -5
```
Expected: `average rate: 2.0XX` (acceptable: 1.9–2.1 Hz)

- [ ] **Step 6: Verify 1 Hz (VoyageTask)**

```bash
timeout 8 ros2 topic hz /l1/voyage_task 2>&1 | tail -5
```
Expected: `average rate: 1.0XX` (acceptable: 0.95–1.05 Hz)

- [ ] **Step 7: Verify 0.2 Hz (EnvironmentState)**

```bash
timeout 30 ros2 topic hz /fusion/environment_state 2>&1 | tail -5
```
Expected: `average rate: 0.2XX` (acceptable: 0.19–0.21 Hz; takes ~25 s to stabilize)

- [ ] **Step 8: Verify schema_version in published message**

```bash
ros2 topic echo /fusion/own_ship_state --once 2>/dev/null | grep schema_version
```
Expected: `schema_version: v1.1.2`

```bash
ros2 topic echo /l1/voyage_task --once 2>/dev/null | grep schema_version
```
Expected: `schema_version: v1.1.2`

- [ ] **Step 9: Stop mock publisher**

```bash
kill $MOCK_PID 2>/dev/null
```

- [ ] **Step 10: Final combined DoD verification pass**

```bash
# DoD 1: colcon build (already verified in Task 3, confirm log exists)
ls "docs/Design/Detailed Design/D1.1-ros2-workspace/evidence/colcon-build-ok.log"
```
Expected: file exists.

```bash
# DoD 2: 27 top-level .msg files have schema_version (16 l3_msgs + 11 l3_external_msgs)
grep -rl "schema_version" src/l3_msgs/msg src/l3_external_msgs/msg | wc -l
```
Expected: `27`

```bash
# DoD 3: TimeWindow exempt
grep "schema_version" src/l3_external_msgs/msg/TimeWindow.msg
echo "exit: $?"
```
Expected: `exit: 1`

```bash
# DoD 4: sub-messages exempt (spot check)
grep "schema_version" src/l3_msgs/msg/AvoidanceWaypoint.msg src/l3_msgs/msg/SAT1Data.msg
echo "exit: $?"
```
Expected: no output, `exit: 1`

```bash
# DoD 5: mock publisher 50 Hz (already verified step 4 — confirm string in log or re-run)
# If evidence log was captured in step 4, reference it. Otherwise re-run:
timeout 12 ros2 topic hz /fusion/own_ship_state 2>&1 | grep "average rate"
```
Expected: `average rate: 50.0XX`

```bash
# DoD 6: D0 CI grep protection
grep -rE "(\bFCB\b|45\s*m\b)" src/l3_msgs/ src/l3_external_msgs/
echo "exit: $?"
```
Expected: `exit: 1`

- [ ] **Step 11: Fill closure checklist in spec**

Edit `docs/Design/Detailed Design/D1.1-ros2-workspace/01-spec.md` — update §10 Closure Checklist:

Change each `⬜` to `✅` for completed items and add evidence paths:

```markdown
| colcon build exit 0 无 warning | 截图路径：`docs/Design/Detailed Design/D1.1-ros2-workspace/evidence/colcon-build-ok.log` | ✅ |
| 25 + 11 条顶层 .msg 含 schema_version | `grep -rl "schema_version" src/l3_msgs/msg src/l3_external_msgs/msg \| wc -l` = 27 | ✅ |
| TimeWindow 豁免确认 | `grep "schema_version" src/l3_external_msgs/msg/TimeWindow.msg` → exit 1 | ✅ |
| mock publisher 50 Hz | `ros2 topic hz /fusion/own_ship_state` 输出 average rate: ~50 Hz | ✅ |
| D0 CI job 不 break | 本地 `grep -rE "(\bFCB\b|45\s*m\b)" src/l3_msgs/ src/l3_external_msgs/` → exit 1 | ✅ |
| F P1-F-04 关闭证据 | 本 spec §3.1 + T2a/T2b 完成 — schema_version 字段选型 string "v1.1.2" | ✅ |
| F P1-F-06 关闭证据 | Task 3 colcon build evidence log | ✅ |
| ARCH-GAP 9 条完整记录 | 本 spec §4 | ✅ |
```

- [ ] **Step 12: Final commit**

```bash
git add "docs/Design/Detailed Design/D1.1-ros2-workspace/01-spec.md"
git commit -m "chore(d1.1): close D1.1 — all DoD items verified, closure checklist filled"
```

---

## Self-Review Against Spec

**Spec coverage check:**

| Spec DoD item | Plan task | Status |
|---|---|---|
| `colcon build --packages-select l3_msgs l3_external_msgs` exit 0, stderr 无 warning | Task 3 Steps 5+6 | ✅ |
| 25+11 顶层 .msg 全含 schema_version (实为 16+11=27) | Task 1+2 | ✅ |
| ARCH-GAP 001–004 + M4 完整记录于 §4 | Already in spec §4 (pre-existing) | ✅ |
| mock publisher 50 Hz | Task 5 Step 4 | ✅ |
| D0 CI 2 job 验证不 break | Task 3 Step 6 | ✅ |
| F P1-F-04 + F P1-F-06 closure checklist §7 | Task 5 Step 11 | ✅ |

**Risks addressed:**
- R1.1 (geographic_msgs Jazzy): Task 3 Step 1 verifies before build
- R1.2 (build order): Task 3 uses `--packages-up-to` form
- R1.3 (ruff violations): no .msg patch touches ruff exclude rules; .ruff.toml untouched
- R1.4 (stale bytecode): Task 4 Step 1 cleans before running
- R1.5 (DDS overhead): accepted, no action

**Placeholder scan:** None found. All steps contain exact commands, expected outputs, and exact code.

**Type consistency:** `schema_version` field name used consistently as `string schema_version` in .msg files and `msg.schema_version = "v1.1.2"` in publisher.
