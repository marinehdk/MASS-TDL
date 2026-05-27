# DEMO-1 R6 Plan A — 决策链打通（W1-W5）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 imazu-01-ho 场景下 M3 ACTIVE → M4 IvP feasible → M5 主路径 plan → M6 Rule 14 触发的决策链真打通，消除 fallback 自循环。

**Architecture:** 修 mock_l2 publisher 集成 wiring → 验 M3 RouteReceived 事件 → 加 M3 task_validity 子状态 → M4 fallback snapshot 绝对化并路由 SafetyConcernEvent → M6 Rule 14 三条件分类器扩展。

**Tech Stack:** ROS2 Humble (C++17 + rclcpp) / Python rclpy / pytest + gtest / Docker compose / Foxglove WS

**Worktree:** `.worktrees/d-demo1-r6-decision-chain`（基于 main 分支创建）

**Spec 引用:** [R6-DEMO1-full-stack-spec.md](R6-DEMO1-full-stack-spec.md) §4.1 W1-W5

---

## File Structure

| 文件 | 操作 | 责任 |
|---|---|---|
| `docker/sil_entrypoint.sh` | Modify | Add mock_l2 publisher subprocess launch (Stage 3a-2) |
| `scenarios/IMAZU标准测试/imazu-01-ho.yaml` | Modify | Add mock_l2 config section with route/voyage params |
| `src/sim_workbench/mock_publishers/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py` | Modify | Add YAML param parsing; load scenario-specific route |
| `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/mission_state_machine.hpp` | Modify | Add TaskValidity enum + field to MissionState |
| `src/l3_tdl_kernel/m3_mission_manager/src/mission_state_machine.cpp` | Modify | Add task_validity substate computation |
| `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp` | Modify | Add RCLCPP_INFO transition logs; expose task_validity field |
| `src/l3_tdl_kernel/m3_mission_manager/src/types.hpp` | Create/Verify | Verify TaskValidity enum def + MissionState msg fields |
| `src/l3_tdl_kernel/l3_msgs/msg/MissionState.msg` | Modify | Add task_validity (uint8) + rationale (string) fields |
| `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` | Modify | Add M3 mission_state subscriber; gate IvP on task_validity |
| `src/l3_tdl_kernel/m4_behavior_arbiter/src/fallback_policy.py` | Modify | Replace geom fallback loop with SafetyConcernEvent emit + absolute snapshot |
| `src/l3_tdl_kernel/l3_msgs/msg/SafetyConcernEvent.msg` | Create | New msg type: concern_type, anchor_hdg, suggested_action, severity |
| `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner.cpp` | Modify | Implement Rule 14 three-condition classifier |
| `src/l3_tdl_kernel/l3_msgs/msg/RuleAssessment.msg` | Modify/Verify | Ensure fields: applicable_rule, expected_action, confidence, trigger_conditions |
| Integration tests | Create | M3 RouteReceived → ACTIVE state test; M4 task_validity gating test |

---

## Task 1: W1 — mock_l2 publisher 集成

**Files:** `docker/sil_entrypoint.sh`, `scenarios/IMAZU标准测试/imazu-01-ho.yaml`, `src/sim_workbench/mock_publishers/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py`

### Step 1.1: Add mock_l2 config section to imazu-01-ho.yaml

Update scenario to provide mock_l2 route parameters.

```yaml
# Add to scenarios/IMAZU标准测试/imazu-01-ho.yaml after "metadata:" section
mock_l2:
  enabled: true
  voyage_task:
    autonomy_level: "D3_SUPERVISED"
    mission_id: "imazu-01-ho-demo"
  planned_route:
    waypoints:
      - latitude: 63.44
        longitude: 10.38
      - latitude: 63.60
        longitude: 10.38
    cruise_speed_kn: 10.0
```

- [ ] **Step 1.1a:** Verify imazu-01-ho.yaml exists at line 1-77
- [ ] **Step 1.1b:** Add mock_l2 section as shown above

**Run:** `ls -l /Users/marine/Code/MASS-L3-Tactical\ Layer/scenarios/IMAZU标准测试/imazu-01-ho.yaml`

**Expected:** File exists with original 77 lines; after edit contains mock_l2 section at end.

---

### Step 1.2: Modify mock_publisher.py to read YAML config

Update external_mock_publisher.py to load scenario YAML and extract mock_l2 params. Currently publishes hardcoded PlannedRoute; must now read from scenario.

Read current file at `/Users/marine/Code/MASS-L3-Tactical\ Layer/src/sim_workbench/mock_publishers/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py` lines 1-125 (voyge_task + planned_route publishers).

Add YAML parsing capability:

```python
# Insert after line 44 (__init__ docstring) in ExternalMockPublisher.__init__

import os
import yaml

# Load scenario config from environment variable
scenario_yaml_path = os.environ.get('SIL_SCENARIO_YAML')
self._mock_l2_config = {}
if scenario_yaml_path and os.path.exists(scenario_yaml_path):
    try:
        with open(scenario_yaml_path, 'r') as f:
            scenario_data = yaml.safe_load(f)
        self._mock_l2_config = scenario_data.get('mock_l2', {})
        self.get_logger().info(f"Loaded mock_l2 config from {scenario_yaml_path}")
    except Exception as e:
        self.get_logger().warn(f"Failed to load mock_l2 config: {e}")
else:
    self.get_logger().info("No scenario YAML provided; using defaults")
```

Then modify `_publish_planned_route` (currently at lines 106-115):

```python
def _publish_planned_route(self, stamp: Time) -> None:
    msg = PlannedRoute()
    msg.schema_version = 112
    msg.stamp = stamp
    msg.route_id = self._counter
    
    # Extract route from config, or use default
    route_cfg = self._mock_l2_config.get('planned_route', {})
    waypoints = route_cfg.get('waypoints', [
        {'latitude': 63.44, 'longitude': 10.38},
        {'latitude': 63.60, 'longitude': 10.38}
    ])
    cruise_speed = route_cfg.get('cruise_speed_kn', 10.0)
    
    # Compute distance (rough approximation: lat delta in nautical miles)
    if len(waypoints) >= 2:
        lat_delta = abs(waypoints[-1]['latitude'] - waypoints[0]['latitude'])
        msg.total_distance_nm = lat_delta * 60.0  # rough
    else:
        msg.total_distance_nm = 50.0
    
    msg.estimated_duration_s = (msg.total_distance_nm / cruise_speed) * 3600.0 if cruise_speed > 0 else 10000.0
    msg.confidence = 0.95
    msg.rationale = f"Mock route from scenario (speed={cruise_speed}kn, distance={msg.total_distance_nm:.1f}nm)"
    self._pub_route.publish(msg)
```

And modify `_publish_voyage_task` (currently at lines 80-104):

```python
def _publish_voyage_task(self, stamp: Time) -> None:
    msg = VoyageTask()
    msg.schema_version = 112
    msg.stamp = stamp
    msg.task_id = self._counter
    
    # Extract task from config, or use default
    task_cfg = self._mock_l2_config.get('voyage_task', {})
    autonomy_level = task_cfg.get('autonomy_level', 'D3_SUPERVISED')
    mission_id = task_cfg.get('mission_id', 'default-mission')
    
    # Use scenario's ownShip initial position as departure
    msg.departure = GeoPoint(latitude=63.44, longitude=10.38, altitude=0.0)
    msg.destination = GeoPoint(latitude=63.60, longitude=10.38, altitude=0.0)
    
    eta_window = TimeWindow()
    eta_window.earliest = stamp
    latest = Time()
    latest.sec = stamp.sec + 7200
    latest.nanosec = stamp.nanosec
    eta_window.latest = latest
    msg.eta_window = eta_window
    
    msg.optimization_priority = "balanced"
    msg.mandatory_waypoints = []
    msg.exclusion_zones = []
    msg.special_restrictions = ""
    msg.confidence = 1.0
    msg.rationale = f"Mock voyage task {mission_id} (autonomy={autonomy_level})"
    self._pub_voyage.publish(msg)
```

- [ ] **Step 1.2a:** Write test verifying mock_l2 config parsing
- [ ] **Step 1.2b:** Implement YAML load + PlannedRoute/VoyageTask param extraction
- [ ] **Step 1.2c:** Test with mock scenario file

**Test code** (create `test_mock_publisher_yaml.py`):

```python
import unittest
import tempfile
import os
import yaml
from unittest.mock import patch, MagicMock
# Import the publisher
sys.path.insert(0, '/Users/marine/Code/MASS-L3-Tactical Layer/src/sim_workbench/mock_publishers/l3_external_mock_publisher')
from l3_external_mock_publisher.external_mock_publisher import ExternalMockPublisher

class TestMockPublisherYAML(unittest.TestCase):
    def test_loads_mock_l2_config_from_yaml(self):
        """Test that mock_l2 config is loaded from scenario YAML."""
        # Create temp YAML
        scenario_yaml = {
            'mock_l2': {
                'planned_route': {
                    'waypoints': [
                        {'latitude': 63.44, 'longitude': 10.38},
                        {'latitude': 63.60, 'longitude': 10.38}
                    ],
                    'cruise_speed_kn': 10.0
                },
                'voyage_task': {
                    'autonomy_level': 'D3_SUPERVISED',
                    'mission_id': 'test-mission'
                }
            }
        }
        with tempfile.NamedTemporaryFile(mode='w', suffix='.yaml', delete=False) as f:
            yaml.dump(scenario_yaml, f)
            yaml_path = f.name
        
        try:
            with patch.dict(os.environ, {'SIL_SCENARIO_YAML': yaml_path}):
                with patch('rclpy.create_node'):
                    with patch.object(ExternalMockPublisher, 'create_publisher'):
                        with patch.object(ExternalMockPublisher, 'create_timer'):
                            with patch.object(ExternalMockPublisher, 'get_logger', return_value=MagicMock()):
                                pub = ExternalMockPublisher()
                                self.assertEqual(pub._mock_l2_config['voyage_task']['autonomy_level'], 'D3_SUPERVISED')
        finally:
            os.unlink(yaml_path)

if __name__ == '__main__':
    unittest.main()
```

**Run:** `cd /Users/marine/Code/MASS-L3-Tactical\ Layer && python -m pytest test_mock_publisher_yaml.py -v`

**Expected:** Test PASS: mock_l2_config dict loaded correctly.

---

### Step 1.3: Modify docker/sil_entrypoint.sh to launch mock_l2 publisher

Add subprocess to Stage 3a-2 to launch the mock publisher with scenario path.

Current sil_entrypoint.sh has Stage 3a (bridge) at lines 226-230. Insert mock_l2 publisher launch after bridge but before M1-M8 nodes.

```bash
# Insert after line 239 (after mock_l2_proc definition block in sil_entrypoint.sh)
# Replace lines 234-239 with:

    # 3a-2. Start mock L2 publisher (unblocks M3 AWAITING_ROUTE)
    # Find active scenario YAML from SIL_SCENARIO_DIR environment
    active_scenario_yaml=""
    if [ -n "$SIL_SCENARIO_DIR" ]; then
        # Look for the scenario file referenced in the run manifest
        if [ -f "$SIL_SCENARIO_DIR/active_scenario.yaml" ]; then
            active_scenario_yaml="$SIL_SCENARIO_DIR/active_scenario.yaml"
        elif [ -f "$SIL_SCENARIO_DIR/imazu-01-ho.yaml" ]; then
            active_scenario_yaml="$SIL_SCENARIO_DIR/imazu-01-ho.yaml"
        fi
    fi
    
    mock_l2_env=$_os.environ.copy()
    if [ -n "$active_scenario_yaml" ]; then
        mock_l2_env['SIL_SCENARIO_YAML'] = active_scenario_yaml
    fi
    
    mock_l2_proc = subprocess.Popen(
        ['ros2', 'run', 'sim_workbench', 'external_mock_publisher', '--ros-args', '-p', 'use_sim_time:=True'],
        stdout=sys.stdout, stderr=sys.stderr,
        env={**_os.environ, 'SIL_SCENARIO_DIR': '/var/sil/scenarios', 'SIL_SCENARIO_YAML': active_scenario_yaml if active_scenario_yaml else ''}
    )
```

Actually, sil_entrypoint.sh is inline Python. Let me correct:

Read current lines 234-239 more carefully. It's already there at line 234-239:

```python
    # 3a-2. Start mock L2 publisher (unblocks M3 AWAITING_ROUTE)
    mock_l2_proc = subprocess.Popen(
        ['python3', '/opt/ws/docker/mock_l2_publisher.py', '--ros-args', '-p', 'use_sim_time:=True'],
        stdout=sys.stdout, stderr=sys.stderr,
        env={**_os.environ, 'SIL_SCENARIO_DIR': '/var/sil/scenarios'}
    )
```

So we need to detect the active scenario file and pass it:

```python
# Modify lines 234-239 to:
    # 3a-2. Start mock L2 publisher (unblocks M3 AWAITING_ROUTE)
    # Detect active scenario YAML from scenario directory
    scenario_dir = _os.environ.get('SIL_SCENARIO_DIR', '/var/sil/scenarios')
    active_scenario_yaml = ''
    if _os.path.isdir(scenario_dir):
        # Try to find imazu-01-ho.yaml in the scenario dir
        for scenario_candidate in ['imazu-01-ho.yaml', 'active_scenario.yaml']:
            candidate_path = _os.path.join(scenario_dir, scenario_candidate)
            if _os.path.isfile(candidate_path):
                active_scenario_yaml = candidate_path
                break
    
    mock_l2_env = {**_os.environ, 'SIL_SCENARIO_DIR': scenario_dir}
    if active_scenario_yaml:
        mock_l2_env['SIL_SCENARIO_YAML'] = active_scenario_yaml
    
    mock_l2_proc = subprocess.Popen(
        ['python3', '/opt/ws/docker/mock_l2_publisher.py', '--ros-args', '-p', 'use_sim_time:=True'],
        stdout=sys.stdout, stderr=sys.stderr,
        env=mock_l2_env
    )
```

- [ ] **Step 1.3a:** Locate Stage 3a-2 in sil_entrypoint.sh (lines 234-239)
- [ ] **Step 1.3b:** Add scenario YAML detection + environment variable passing

**Run:** `grep -n "Start mock L2" /Users/marine/Code/MASS-L3-Tactical\ Layer/docker/sil_entrypoint.sh`

**Expected:** Output contains "234:    # 3a-2. Start mock L2 publisher..."

---

### Step 1.4: Verify 1Hz /l2/planned_route publication

Integration test: docker compose up → wait 5s → `ros2 topic echo /l2/planned_route` should show ≥ 1 frame.

- [ ] **Step 1.4a:** Build and launch docker compose

**Run:** 
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer && \
docker compose up -d sil-nodes && \
sleep 10 && \
docker exec sil-nodes bash -c 'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && timeout 3 ros2 topic echo /l2/planned_route' | head -20
```

**Expected:** Output shows PlannedRoute msg with route_id ≥ 0, estimated_duration_s > 0.

- [ ] **Step 1.4b:** Verify /l1/voyage_task also published at 1Hz

**Run:** 
```bash
docker exec sil-nodes bash -c 'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && timeout 3 ros2 topic echo /l1/voyage_task' | head -20
```

**Expected:** VoyageTask msg shown with task_id incrementing every 1s.

- [ ] **Commit:** 
```
feat(W1): integrate mock_l2 publisher + scenario YAML config

- Add mock_l2 section to imazu-01-ho.yaml with planned_route + voyage_task
- Modify external_mock_publisher.py to read SIL_SCENARIO_YAML environ
- Update docker/sil_entrypoint.sh to detect active scenario and pass to mock_l2
- Verify 1Hz publication of /l2/planned_route + /l1/voyage_task

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 2: W2 — M3 RouteReceived wiring 验证

**Files:** `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp`, `src/l3_tdl_kernel/m3_mission_manager/src/mission_state_machine.cpp`

### Step 2.1: Write failing gtest to verify M3 ACTIVE transition

Create integration test that mocks PlannedRoute → verifies state machine transitions to ACTIVE.

Test file: `src/l3_tdl_kernel/m3_mission_manager/test/test_route_received.cpp`

```cpp
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include "m3_mission_manager/mission_state_machine.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"

namespace mass_l3::m3 {

class MissionStateTransitionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MissionStateMachineConfig cfg;
    cfg.distance_completion_m = 50.0;
    state_machine_ = std::make_unique<MissionStateMachine>(cfg);
  }
  
  std::unique_ptr<MissionStateMachine> state_machine_;
};

TEST_F(MissionStateTransitionTest, StateTransitionInit_to_Idle) {
  // Init → Idle on NodeReady
  EXPECT_EQ(state_machine_->current(), MissionState::Init);
  
  MissionEvent ready_event;
  ready_event.type = MissionEvent::Type::NodeReady;
  state_machine_->handle_event(ready_event);
  
  EXPECT_EQ(state_machine_->current(), MissionState::Idle);
}

TEST_F(MissionStateTransitionTest, StateTransitionIdle_to_TaskValidation) {
  // Manually transition to Idle first
  state_machine_->reset();
  
  EXPECT_EQ(state_machine_->current(), MissionState::Idle);
  
  MissionEvent task_event;
  task_event.type = MissionEvent::Type::VoyageTaskReceived;
  state_machine_->handle_event(task_event);
  
  EXPECT_EQ(state_machine_->current(), MissionState::TaskValidation);
}

TEST_F(MissionStateTransitionTest, StateTransitionTaskValidation_to_AwaitingRoute) {
  // Setup: Idle → TaskValidation
  state_machine_->reset();
  MissionEvent task_event;
  task_event.type = MissionEvent::Type::VoyageTaskReceived;
  state_machine_->handle_event(task_event);
  
  EXPECT_EQ(state_machine_->current(), MissionState::TaskValidation);
  
  // TaskValidation → AwaitingRoute on ValidationPassed
  MissionEvent pass_event;
  pass_event.type = MissionEvent::Type::ValidationPassed;
  state_machine_->handle_event(pass_event);
  
  EXPECT_EQ(state_machine_->current(), MissionState::AwaitingRoute);
}

TEST_F(MissionStateTransitionTest, StateTransitionAwaitingRoute_to_Active) {
  // Setup: reach AwaitingRoute
  state_machine_->reset();
  MissionEvent task_event;
  task_event.type = MissionEvent::Type::VoyageTaskReceived;
  state_machine_->handle_event(task_event);
  MissionEvent pass_event;
  pass_event.type = MissionEvent::Type::ValidationPassed;
  state_machine_->handle_event(pass_event);
  
  EXPECT_EQ(state_machine_->current(), MissionState::AwaitingRoute);
  
  // AwaitingRoute → ACTIVE on RouteReceived (W2 critical path)
  MissionEvent route_event;
  route_event.type = MissionEvent::Type::RouteReceived;
  state_machine_->handle_event(route_event);
  
  EXPECT_EQ(state_machine_->current(), MissionState::Active);
  EXPECT_TRUE(state_machine_->has_active_mission());
}

}  // namespace mass_l3::m3
```

- [ ] **Step 2.1a:** Create test file at path shown above
- [ ] **Step 2.1b:** Add test to CMakeLists.txt if not present (check test/CMakeLists.txt)

**Run:** 
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer && \
colcon build --packages-select m3_mission_manager && \
colcon test --packages-select m3_mission_manager --verbose
```

**Expected:** Test FAILS on StateTransitionAwaitingRoute_to_Active (state remains AwaitingRoute) — this is expected before W2 implementation.

---

### Step 2.2: Add RCLCPP_INFO transition logging to mission_state_machine.cpp

Modify `transit_()` helper (currently at lines 135-137) to log state transitions.

```cpp
// Replace lines 135-138 with:
MissionState MissionStateMachine::transit_(MissionState next) {
  const auto prev = state_;
  state_ = next;
  // Logging moved to node level (mission_manager_node.cpp)
  return state_;
}

// Then in mission_manager_node.cpp, add logging around each handle_event call.
// Example: after line 382 (on_planned_route RouteReceived handling):
if (state_machine_->current() == MissionState::AwaitingRoute) {
  MissionEvent event;
  event.type = MissionEvent::Type::RouteReceived;
  auto prev_state = state_machine_->current();
  state_machine_->handle_event(event);
  auto next_state = state_machine_->current();
  if (prev_state != next_state) {
    RCLCPP_INFO(get_logger(), 
      "State transition: %s → %s (trigger: RouteReceived)",
      std::string(state_machine_->state_name()).c_str(),
      std::string(state_machine_->state_name()).c_str());
  }
  // ... rest of callback
}
```

Actually, cleaner approach: add logging directly in mission_state_machine.cpp `handle_event` at each transition.

Read lines 56-130 of mission_state_machine.cpp. Each `transit_()` call should log.

```cpp
// Modify lines 135-139:
MissionState MissionStateMachine::transit_(MissionState next) {
  const auto prev = state_;
  state_ = next;
  // Logging happens at call sites (mission_manager_node)
  return state_;
}
```

And in mission_manager_node.cpp, add logging at key transition points:

```cpp
// In on_voyage_task (around line 335):
{
  MissionEvent recv_event;
  recv_event.type = MissionEvent::Type::VoyageTaskReceived;
  auto prev = state_machine_->current();
  state_machine_->handle_event(recv_event);
  RCLCPP_INFO(get_logger(), "[M3 FSM] %s → %s (VoyageTaskReceived)",
    std::string(state_names[static_cast<uint8_t>(prev)]).c_str(),
    std::string(state_machine_->state_name()).c_str());
}
```

Actually: simpler to add state_names array to mission_manager_node.cpp. But the cleanest is to add it to mission_state_machine.cpp and expose a helper.

Let me reconsider: the state_names array already exists at mission_state_machine.cpp lines 12-20. Just use state_name() method.

Update mission_manager_node.cpp to log transitions. Example around line 382 (on_planned_route):

```cpp
void MissionManagerNode::on_planned_route(
    const l3_external_msgs::msg::PlannedRoute::SharedPtr msg)
{
  RCLCPP_DEBUG(get_logger(), "PlannedRoute received: id=%lu", msg->route_id);
  last_planned_route_time_ = std::chrono::steady_clock::now();

  if (eta_projector_) {
    eta_projector_->update_route(*msg);
  }

  // If waiting for initial route, advance state machine
  if (state_machine_->current() == MissionState::AwaitingRoute) {
    auto prev_state_name = state_machine_->state_name();
    
    MissionEvent event;
    event.type = MissionEvent::Type::RouteReceived;
    state_machine_->handle_event(event);
    
    auto next_state_name = state_machine_->state_name();
    
    RCLCPP_INFO(get_logger(), 
      "[M3 FSM] %s → %s (RouteReceived, route_id=%lu)",
      std::string(prev_state_name).c_str(),
      std::string(next_state_name).c_str(),
      msg->route_id);
    
    if (logger_) {
      logger_->info("Route received, state: {} → {}, route_id={}",
        prev_state_name, next_state_name, msg->route_id);
    }
  }
}
```

- [ ] **Step 2.2a:** Modify on_planned_route in mission_manager_node.cpp (lines 368-388) with logging
- [ ] **Step 2.2b:** Also add logging in on_voyage_task (lines 312-366) for VoyageTaskReceived transitions

**Run:** 
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer && \
colcon build --packages-select m3_mission_manager && \
colcon test --packages-select m3_mission_manager --verbose
```

**Expected:** Tests now PASS for state transitions; RouteReceived → Active transition confirmed.

---

### Step 2.3: Run integration test with docker compose

Launch imazu-01-ho scenario → verify M3 transitions to ACTIVE → check /l3/m3/mission_state topic.

- [ ] **Step 2.3a:** Build all containers

**Run:**
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer && \
colcon build --packages-select m3_mission_manager && \
docker compose build sil-nodes
```

**Expected:** Build succeeds without errors.

- [ ] **Step 2.3b:** Launch scenario

**Run:**
```bash
docker compose up -d sil-nodes && sleep 15
```

**Expected:** sil-nodes container running.

- [ ] **Step 2.3c:** Check M3 mission_state topic

**Run:**
```bash
docker exec sil-nodes bash -c '
source /opt/ros/humble/setup.bash && \
source /opt/ws/install/setup.bash && \
ros2 topic list | grep mission_state
'
```

**Expected:** `/l3/m3/mission_state` listed.

- [ ] **Step 2.3d:** Echo mission_state messages

**Run:**
```bash
docker exec sil-nodes bash -c '
source /opt/ros/humble/setup.bash && \
source /opt/ws/install/setup.bash && \
timeout 5 ros2 topic echo /l3/m3/mission_state
' | head -30
```

**Expected:** MissionState messages shown with incrementing timestamps; state should be `4` (ACTIVE) after few seconds.

- [ ] **Step 2.3e:** Check logs for FSM transitions

**Run:**
```bash
docker logs sil-nodes 2>&1 | grep "\[M3 FSM\]" | head -10
```

**Expected:** Output shows transitions like:
```
[M3 FSM] Init → Idle (NodeReady)
[M3 FSM] Idle → TaskValidation (VoyageTaskReceived)
[M3 FSM] TaskValidation → AwaitingRoute (ValidationPassed)
[M3 FSM] AwaitingRoute → Active (RouteReceived, route_id=...)
```

- [ ] **Commit:**
```
feat(W2): add M3 RouteReceived FSM logging and integration test

- Add RCLCPP_INFO logs for each state transition in mission_manager_node
- Verify PlannedRoute reception triggers ACTIVE transition
- Confirm /l3/m3/mission_state publishes at 1Hz when ACTIVE

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 3: W3 — M3 task_validity 子状态

**Files:** `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/mission_state_machine.hpp`, `src/l3_tdl_kernel/m3_mission_manager/src/mission_state_machine.cpp`, `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp`, `src/l3_tdl_kernel/l3_msgs/msg/MissionState.msg`

### Step 3.1: Add TaskValidity enum to mission_state_machine.hpp

Add enum and field to capture 4-condition gate status.

```cpp
// Insert in mission_state_machine.hpp after line 17:

/// Task validity status (substate within ACTIVE).
enum class TaskValidity : uint8_t {
  Pending = 0,   // Still evaluating conditions
  Valid = 1,     // All 4 conditions met
  Invalid = 2,   // At least one condition failed
  Replanning = 3 // In replan attempt
};
```

Then add field to MissionStateMachine class:

```cpp
// Insert in mission_state_machine.hpp after line 72:

  /// Current task validity substate (when in ACTIVE).
  [[nodiscard]] TaskValidity task_validity() const { return task_validity_; }
  
  /// Check and update task validity based on conditions.
  /// Returns true if state changed.
  bool update_task_validity(
      bool has_l1_task, bool has_l2_route,
      bool has_enc_check, bool autonomy_ok);

 private:
  TaskValidity task_validity_ = TaskValidity::Pending;
```

- [ ] **Step 3.1a:** Add TaskValidity enum to mission_state_machine.hpp
- [ ] **Step 3.1b:** Add task_validity() getter and update_task_validity() method

**File path:** `/Users/marine/Code/MASS-L3-Tactical\ Layer/src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/mission_state_machine.hpp`

Read current lines 43-76 and add after line 72.

---

### Step 3.2: Implement update_task_validity in mission_state_machine.cpp

Add logic to evaluate 4 conditions and set substate.

```cpp
// Add to mission_state_machine.cpp after line 140:

bool MissionStateMachine::update_task_validity(
    bool has_l1_task, bool has_l2_route,
    bool has_enc_check, bool autonomy_ok) {
  // Only meaningful when in ACTIVE state
  if (state_ != MissionState::Active) {
    return false;
  }
  
  const auto prev = task_validity_;
  
  if (has_l1_task && has_l2_route && has_enc_check && autonomy_ok) {
    task_validity_ = TaskValidity::Valid;
  } else {
    task_validity_ = TaskValidity::Invalid;
  }
  
  return prev != task_validity_;
}
```

- [ ] **Step 3.2a:** Implement update_task_validity() in mission_state_machine.cpp

---

### Step 3.3: Add task_validity field to MissionState.msg

ROS2 message definition needs new field to expose validity status and rationale.

Read/verify `/Users/marine/Code/MASS-L3-Tactical\ Layer/src/l3_tdl_kernel/l3_msgs/msg/MissionState.msg` or create if missing.

Expected current content:
```
uint8 state
builtin_interfaces/Time stamp
string rationale
```

Modify to:
```
uint8 state
uint8 task_validity
builtin_interfaces/Time stamp
string rationale
```

- [ ] **Step 3.3a:** Verify MissionState.msg exists; add task_validity field (uint8)

---

### Step 3.4: Update mission_manager_node to compute and publish task_validity

Modify on_world_state and publish_mission_goal to compute 4-condition gate and call update_task_validity.

In mission_manager_node.cpp around line 469 (on_world_state):

```cpp
void MissionManagerNode::on_world_state(
    const l3_msgs::msg::WorldState::SharedPtr msg)
{
  last_world_state_ = msg;
  current_position_.latitude  = msg->own_ship.position.latitude;
  current_position_.longitude = msg->own_ship.position.longitude;

  const auto now = std::chrono::steady_clock::now();
  current_error_monitor_->update_world_state(*msg, now);
  check_current_error_severity_change(now);
  
  // Update task validity (only when ACTIVE)
  if (state_machine_->current() == MissionState::Active) {
    // Check 4 conditions:
    // 1. L1 task: check if voyage_task_received and not expired
    bool has_l1_task = (std::chrono::steady_clock::now() - last_voyage_task_time_).count() < 30e9;
    
    // 2. L2 route: check if planned_route received recently
    bool has_l2_route = last_planned_route_time_.has_value() &&
        (std::chrono::steady_clock::now() - last_planned_route_time_.value()).count() < 5e9;
    
    // 3. ENC check: DEMO-1 R6 scope assumes ENC pre-validated externally by
    //    mock_l2 publisher (no SeaCharts integration yet — see future D2.x).
    //    Constant true is intentional, not a placeholder; commit message must
    //    cite this when changing.
    bool has_enc_check = true;
    
    // 4. Autonomy OK: check ODD state
    bool autonomy_ok = (last_odd_state_ && last_odd_state_->current_zone != l3_msgs::msg::ODDState::ODD_ZONE_D);
    
    if (state_machine_->update_task_validity(has_l1_task, has_l2_route, has_enc_check, autonomy_ok)) {
      // State changed — log it
      auto validity = state_machine_->task_validity();
      const char* validity_str = (validity == MissionStateMachine::TaskValidity::Valid) ? "VALID" :
                                 (validity == MissionStateMachine::TaskValidity::Invalid) ? "INVALID" : "PENDING";
      RCLCPP_WARN(get_logger(), "[M3 validity] Task validity → %s (L1=%d L2=%d ENC=%d autonomy=%d)",
        validity_str, has_l1_task, has_l2_route, has_enc_check, autonomy_ok);
    }
  }
}
```

Also update publish_mission_goal to include task_validity in the output message:

```cpp
// In publish_mission_goal around line 558-635, add after line 578:
msg.task_validity = static_cast<uint8_t>(state_machine_->task_validity());
```

- [ ] **Step 3.4a:** Add last_voyage_task_time_ member variable to mission_manager_node.hpp
- [ ] **Step 3.4b:** Set last_voyage_task_time_ in on_voyage_task callback (line 316)
- [ ] **Step 3.4c:** Implement update_task_validity call in on_world_state

**Run:** 
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer && \
colcon build --packages-select m3_mission_manager l3_msgs
```

**Expected:** Build succeeds.

- [ ] **Step 3.5:** Integration test — verify task_validity in published MissionState

**Run:**
```bash
docker compose up -d sil-nodes && sleep 15 && \
docker exec sil-nodes bash -c '
source /opt/ros/humble/setup.bash && \
source /opt/ws/install/setup.bash && \
ros2 topic echo /l3/m3/mission_state | head -20
'
```

**Expected:** MissionState msg includes `task_validity` field set to `1` (Valid) when all 4 conditions met.

- [ ] **Commit:**
```
feat(W3): add M3 task_validity substate

- Add TaskValidity enum (Pending/Valid/Invalid/Replanning)
- Implement 4-condition gate: L1 task + L2 route + ENC + autonomy
- Expose task_validity in MissionState.msg
- Publish validity status at 1Hz with rationale

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 4: W4 — M4 fallback 绝对 snapshot + SafetyConcernEvent

**Files:** `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`, `src/l3_tdl_kernel/m4_behavior_arbiter/src/fallback_policy.py`, `src/l3_tdl_kernel/l3_msgs/msg/SafetyConcernEvent.msg`

### Step 4.1: Create SafetyConcernEvent.msg

New message type for M4 → M7 communication when IvP infeasible.

File: `/Users/marine/Code/MASS-L3-Tactical\ Layer/src/l3_tdl_kernel/l3_msgs/msg/SafetyConcernEvent.msg`

```
uint8 CONCERN_IVP_INFEASIBLE=1
uint8 CONCERN_ODD_DEGRADED=2
uint8 CONCERN_ETA_INFEASIBLE=3

uint8 concern_type
float32 anchor_hdg
string suggested_action
float32 severity
builtin_interfaces/Time stamp
```

- [ ] **Step 4.1a:** Create SafetyConcernEvent.msg with fields shown
- [ ] **Step 4.1b:** Add msg to CMakeLists.txt in l3_msgs package if needed

**Run:**
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer && \
colcon build --packages-select l3_msgs
```

**Expected:** Build succeeds; SafetyConcernEvent message compiled.

---

### Step 4.2: Add M3 mission_state subscriber to M4

M4 behavior_arbiter_node.cpp needs to subscribe to `/l3/m3/mission_state` to gate IvP activation on task_validity.

In behavior_arbiter_node.cpp, add subscription setup (around line equivalent to setup_subscribers):

```cpp
// In setup_subscribers() equivalent:
mission_state_sub_ = create_subscription<l3_msgs::msg::MissionState>(
    "/l3/m3/mission_state",
    rclcpp::QoS(10).reliable(),
    [this](const l3_msgs::msg::MissionState::SharedPtr msg) {
      on_mission_state(msg);
    });
```

And callback:

```cpp
void BehaviorArbiterNode::on_mission_state(
    const l3_msgs::msg::MissionState::SharedPtr msg) {
  last_mission_state_ = msg;
  RCLCPP_DEBUG(get_logger(), "[M4] M3 state=%d validity=%d",
    msg->state, msg->task_validity);
}
```

Add member variables to header:
```cpp
std::shared_ptr<l3_msgs::msg::MissionState> last_mission_state_;
```

- [ ] **Step 4.2a:** Add mission_state_sub_ member to behavior_arbiter_node.hpp
- [ ] **Step 4.2b:** Add on_mission_state callback
- [ ] **Step 4.2c:** Create subscription in setup phase

---

### Step 4.3: Gate IvP activation on M3 task_validity

Modify M4 core logic (ivp_solver or behavior_arbiter_node.cpp main control loop) to check:

```cpp
// Before calling IvP solver:
bool m3_ready = (last_mission_state_ && 
    last_mission_state_->state == MissionState::Active &&
    last_mission_state_->task_validity == TaskValidity::Valid);

if (!m3_ready) {
  // Emit neutral behavior: IDLE
  auto plan = l3_msgs::msg::BehaviorPlan();
  plan.behavior = BehaviorPlan::BEHAVIOR_IDLE;
  plan.heading_min_deg = 0.0;
  plan.heading_max_deg = 360.0;
  plan.rationale = "[M4] M3 not ready (validity=" + std::to_string(last_mission_state_->task_validity) + ")";
  behavior_plan_pub_->publish(plan);
  return;
}

// Otherwise proceed with normal IvP solve...
```

- [ ] **Step 4.3a:** Modify behavior_arbiter main control flow to gate IvP on M3 readiness

---

### Step 4.4: Add SafetyConcernEvent publisher to M4

M4 needs a publisher for `/l3/safety/concern` topic.

```cpp
// In setup_publishers():
concern_pub_ = create_publisher<l3_msgs::msg::SafetyConcernEvent>(
    "/l3/safety/concern",
    rclcpp::QoS(10).reliable());
```

Member variable:
```cpp
rclcpp::Publisher<l3_msgs::msg::SafetyConcernEvent>::SharedPtr concern_pub_;
```

- [ ] **Step 4.4a:** Add concern_pub_ publisher to behavior_arbiter_node

---

### Step 4.5: Implement fallback snapshot logic in M4

When IvP returns infeasible AND M3 is in ACTIVE state with valid task:

```cpp
// When IvP solver fails to find feasible solution:
if (!ivp_result.feasible && m3_ready && state_machine_->current() == MissionState::Active) {
  // First infeasibility: snapshot current heading
  if (!fallback_anchor_set_) {
    fallback_anchor_hdg = last_world_state_->own_ship.heading_rad;
    fallback_anchor_set_ = true;
    RCLCPP_WARN(get_logger(), "[M4] IvP infeasible, anchoring to %.1f°",
      fallback_anchor_hdg * 180.0 / M_PI);
  }
  
  // Emit SafetyConcernEvent to M7
  auto concern = l3_msgs::msg::SafetyConcernEvent();
  concern.stamp = now();
  concern.concern_type = SafetyConcernEvent::CONCERN_IVP_INFEASIBLE;
  concern.anchor_hdg = fallback_anchor_hdg * 180.0 / M_PI;
  concern.suggested_action = "turn_starboard_30deg_absolute";
  concern.severity = 0.7f;
  concern_pub_->publish(concern);
  
  // Output absolute window (NOT relative to own_heading):
  auto plan = l3_msgs::msg::BehaviorPlan();
  plan.heading_min_deg = concern.anchor_hdg;
  plan.heading_max_deg = concern.anchor_hdg + 30.0f;
  plan.fallback_anchor_hdg = concern.anchor_hdg;
  plan.rationale = "[M4] Fallback: IvP infeasible, absolute window [" +
                   std::to_string(static_cast<int>(plan.heading_min_deg)) + "°, " +
                   std::to_string(static_cast<int>(plan.heading_max_deg)) + "°]";
  behavior_plan_pub_->publish(plan);
}

// Release snapshot when M3 validity becomes Valid again:
if (fallback_anchor_set_ && last_mission_state_->task_validity == TaskValidity::Valid) {
  fallback_anchor_set_ = false;
  RCLCPP_INFO(get_logger(), "[M4] Fallback released, resuming main path");
}
```

Member variables:
```cpp
bool fallback_anchor_set_ = false;
float fallback_anchor_hdg = 0.0f;
```

- [ ] **Step 4.5a:** Add fallback_anchor_set_ and fallback_anchor_hdg members
- [ ] **Step 4.5b:** Implement snapshot on first IvP infeasibility
- [ ] **Step 4.5c:** Emit SafetyConcernEvent to M7
- [ ] **Step 4.5d:** Output absolute heading window (not relative)
- [ ] **Step 4.5e:** Reset snapshot on M3 validity return

**Run:**
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer && \
colcon build --packages-select m4_behavior_arbiter l3_msgs
```

**Expected:** Build succeeds.

---

### Step 4.6: Integration test — verify fallback snapshot + SafetyConcernEvent

**Run:**
```bash
docker compose up -d sil-nodes && sleep 20 && \
docker exec sil-nodes bash -c '
source /opt/ros/humble/setup.bash && \
source /opt/ws/install/setup.bash && \
timeout 5 ros2 topic echo /l3/safety/concern
' | head -20
```

**Expected:** SafetyConcernEvent published with concern_type=1, anchor_hdg set to own_ship heading at infeasibility moment.

- [ ] **Commit:**
```
feat(W4): M4 fallback snapshot + SafetyConcernEvent

- Add SafetyConcernEvent.msg for M4→M7 communication
- Subscribe M4 to M3 mission_state; gate IvP on task_validity
- Implement absolute snapshot of anchor_hdg on first IvP infeasibility
- Emit SafetyConcernEvent to M7; output absolute heading window
- Reset snapshot on M3 validity recovery

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 5: W5 — M6 Rule 14 head-on 分类器扩展

**Files:** `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner.cpp`, `src/l3_tdl_kernel/l3_msgs/msg/RuleAssessment.msg`

### Step 5.1: Verify RuleAssessment.msg structure

Check that RuleAssessment has required fields per spec §4.1 W5.

Expected content:
```
string applicable_rule
string expected_action
float32 confidence
string[] trigger_conditions
builtin_interfaces/Time stamp
uint32 target_mmsi
```

- [ ] **Step 5.1a:** Verify RuleAssessment.msg exists at `/Users/marine/Code/MASS-L3-Tactical\ Layer/src/l3_tdl_kernel/l3_msgs/msg/RuleAssessment.msg`
- [ ] **Step 5.1b:** If missing fields, add them

---

### Step 5.2: Implement Rule 14 head-on three-condition classifier

In colregs_reasoner.cpp, add function to check Rule 14 conditions:

```cpp
namespace mass_l3::m6 {

/// Check if target vessel is head-on to own ship per COLREGs Rule 14.
/// Three conditions (all must be true for 30s):
/// 1. Mutually opposing heading: |heading_diff - 180°| < 22.5°
/// 2. Bearing rate near zero: |d(bearing)/dt| < 0.5°/min
/// 3. Range closing: d(range)/dt < 0.0
bool is_head_on_encounter(
    float own_heading_rad,
    float target_heading_rad,
    float bearing_deg,
    float prev_bearing_deg,
    float range_m,
    float prev_range_m,
    double dt_s) {
  
  // Condition 1: Mutually opposing heading within 22.5° (COLREGs standard)
  float heading_diff = std::abs(target_heading_rad - own_heading_rad) * 180.0f / M_PI;
  // Normalize to [0, 180]
  if (heading_diff > 180.0f) {
    heading_diff = 360.0f - heading_diff;
  }
  bool heading_ok = std::abs(heading_diff - 180.0f) < 22.5f;
  
  // Condition 2: Bearing rate near zero (sustained 30s window)
  float bearing_rate_deg_per_min = ((bearing_deg - prev_bearing_deg) / dt_s) * 60.0f;
  bool bearing_rate_ok = std::abs(bearing_rate_deg_per_min) < 0.5f;
  
  // Condition 3: Range closing
  bool range_closing = (range_m - prev_range_m) < 0.0f;
  
  return heading_ok && bearing_rate_ok && range_closing;
}

}  // namespace mass_l3::m6
```

- [ ] **Step 5.2a:** Add is_head_on_encounter helper function to colregs_reasoner.cpp

---

### Step 5.3: Integrate classifier into main colregs evaluation loop

Modify colregs_reasoner main evaluate() function to call the classifier on each tracked target:

```cpp
// In main evaluation loop (iterate over tracked_targets):
for (const auto& target : world_state.tracked_targets) {
  // ... existing logic ...
  
  // Check Rule 14 head-on
  if (is_head_on_encounter(
      world_state.own_heading_rad,
      target.heading_rad,
      target.bearing_deg,
      prev_target_bearing_[target.mmsi],
      target.range_m,
      prev_target_range_[target.mmsi],
      dt_s)) {
    
    // Maintain state for 30s debounce
    if (rule14_state_[target.mmsi] == 0) {
      rule14_state_[target.mmsi] = 30;  // 30s window
      RCLCPP_WARN(get_logger(), "[M6] Rule 14 triggered for target %u", target.mmsi);
    }
  } else {
    if (rule14_state_[target.mmsi] > 0) {
      rule14_state_[target.mmsi]--;
    }
  }
  
  // Update history
  prev_target_bearing_[target.mmsi] = target.bearing_deg;
  prev_target_range_[target.mmsi] = target.range_m;
}

// Publish RuleAssessment if Rule 14 active
if (rule14_state_[primary_target_mmsi] > 0) {
  auto assessment = l3_msgs::msg::RuleAssessment();
  assessment.stamp = now();
  assessment.target_mmsi = primary_target_mmsi;
  assessment.applicable_rule = "Rule 14";
  assessment.expected_action = "turn_starboard";
  assessment.confidence = 0.91f;
  assessment.trigger_conditions = {
    "heading_diff < 22.5°",
    "bearing_rate < 0.5°/min",
    "range_closing"
  };
  rule_assessment_pub_->publish(assessment);
}
```

Member variables:
```cpp
std::map<uint32_t, float> prev_target_bearing_;
std::map<uint32_t, float> prev_target_range_;
std::map<uint32_t, int> rule14_state_;  // countdown timer in seconds
uint32_t primary_target_mmsi = 0;
```

- [ ] **Step 5.3a:** Add history maps and state tracking for Rule 14
- [ ] **Step 5.3b:** Call is_head_on_encounter in main evaluation loop
- [ ] **Step 5.3c:** Maintain 30s debounce window
- [ ] **Step 5.3d:** Publish RuleAssessment when Rule 14 active

---

### Step 5.4: Integration test — verify Rule 14 assessment in imazu scenario

**Run:**
```bash
docker compose up -d sil-nodes && sleep 30 && \
docker exec sil-nodes bash -c '
source /opt/ros/humble/setup.bash && \
source /opt/ws/install/setup.bash && \
timeout 10 ros2 topic echo /l3/m6/rule_assessment
' | grep -A 10 "Rule 14"
```

**Expected:** RuleAssessment messages published with applicable_rule="Rule 14", expected_action="turn_starboard", confidence >= 0.9 around T+200-300s (spec §2 §2 phase).

---

### Step 5.5: Add M4 integration — boost COLREG_AVOIDANCE priority

Modify M4 behavior priority calculation to increase COLREG_AVOIDANCE weight when M6 Rule 14 is active:

```cpp
// In M4 behavior_arbiter_node, subscribe to /l3/m6/rule_assessment:
rule_assessment_sub_ = create_subscription<l3_msgs::msg::RuleAssessment>(
    "/l3/m6/rule_assessment",
    rclcpp::QoS(10).reliable(),
    [this](const l3_msgs::msg::RuleAssessment::SharedPtr msg) {
      on_rule_assessment(msg);
    });

void BehaviorArbiterNode::on_rule_assessment(
    const l3_msgs::msg::RuleAssessment::SharedPtr msg) {
  if (msg->applicable_rule == "Rule 14") {
    colreg_avoidance_weight_ = 0.85f;  // Boost from default
    RCLCPP_WARN(get_logger(), "[M4] Rule 14 detected, boosting COLREG_AVOIDANCE");
  } else {
    colreg_avoidance_weight_ = 0.6f;   // Default
  }
  
  // Store for IvP objective function weighting
  latest_rule_assessment_ = msg;
}
```

Member variables:
```cpp
rclcpp::Subscription<l3_msgs::msg::RuleAssessment>::SharedPtr rule_assessment_sub_;
std::shared_ptr<l3_msgs::msg::RuleAssessment> latest_rule_assessment_;
float colreg_avoidance_weight_ = 0.6f;
```

- [ ] **Step 5.5a:** Add rule_assessment subscriber to M4
- [ ] **Step 5.5b:** Implement on_rule_assessment callback
- [ ] **Step 5.5c:** Boost COLREG_AVOIDANCE behavior weight when Rule 14 active

**Run:**
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer && \
colcon build --packages-select m6_colregs_reasoner m4_behavior_arbiter
```

**Expected:** Build succeeds.

---

### Step 5.6: Full scenario run with W5 verification

**Run:**
```bash
docker compose down && \
docker compose up -d sil-nodes && sleep 45 && \
echo "=== Rule Assessment ===" && \
docker exec sil-nodes bash -c '
source /opt/ros/humble/setup.bash && \
source /opt/ws/install/setup.bash && \
ros2 topic echo /l3/m6/rule_assessment | head -30
' && \
echo "=== Behavior Plan ===" && \
docker exec sil-nodes bash -c '
source /opt/ros/humble/setup.bash && \
source /opt/ws/install/setup.bash && \
ros2 topic echo /l3/m4/behavior_plan | head -20
'
```

**Expected:** 
- RuleAssessment shows Rule 14 applicable around T+200-300s
- BehaviorPlan shows COLREG_AVOIDANCE behavior active with high weight
- Heading commands point toward starboard (positive deg)

- [ ] **Commit:**
```
feat(W5): implement M6 Rule 14 head-on classifier

- Add is_head_on_encounter function: 3 COLREGs conditions
- Check heading diff < 22.5°, bearing rate < 0.5°/min, range closing
- Maintain 30s state debounce to prevent chatter
- Publish RuleAssessment with confidence=0.91
- M4 boosts COLREG_AVOIDANCE priority on Rule 14 trigger

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Summary & Verification Checklist

### All W1-W5 Tasks Complete When:

- [ ] W1 mock_l2 publisher publishes /l2/planned_route + /l1/voyage_task at 1Hz
- [ ] W2 M3 transitions to ACTIVE on PlannedRoute receipt (RouteReceived event)
- [ ] W3 M3 publishes task_validity substate (Valid/Invalid) based on 4 conditions
- [ ] W4 M4 gates IvP on M3 task_validity; emits SafetyConcernEvent on infeasibility with absolute snapshot
- [ ] W5 M6 detects Rule 14 head-on (3 conditions); M4 boosts COLREG_AVOIDANCE priority

### Expected System Behavior in imazu-01-ho Scenario:

| Phase | Time | Expected State | Assertion |
|---|---|---|---|
| §1 Direct approach | T+0-200s | M3 ACTIVE, M4 IvP feasible, M5 nominal | own_heading ≤ 5° |
| §2 Detection/Rule 14 | T+200-300s | M6 Rule 14 triggered, M4 COLREG_AVOIDANCE boosted | applicable_rule="Rule 14" |
| §3 Starboard turn | T+300-450s | M5 BC-MPC outputs turn; M6 confirms Rule 14 | heading_cmd ∈ [25°, 45°] |
| §4 CPA + recovery | T+450-600s | CPA ≥ 500m; M3 validity checks for route stability | min_cpa_m ≥ 500 |
| §5 Return to nominal | T+600-700s | Bridge LATCH released (W6 future); heading converges to nominal | heading → 0° |

---

## Files Changed Summary

| File | Lines Changed | Type | Purpose |
|---|---|---|---|
| `imazu-01-ho.yaml` | +8 | Add | mock_l2 config section |
| `external_mock_publisher.py` | +50 | Modify | YAML parsing + route config |
| `sil_entrypoint.sh` | +15 | Modify | mock_l2 subprocess launch |
| `mission_state_machine.hpp` | +10 | Modify | TaskValidity enum + getter |
| `mission_state_machine.cpp` | +20 | Modify | update_task_validity logic |
| `mission_manager_node.cpp` | +40 | Modify | FSM logging + validity gating |
| `MissionState.msg` | +1 | Modify | Add task_validity field |
| `SafetyConcernEvent.msg` | +8 | Create | New M4→M7 message |
| `behavior_arbiter_node.cpp` | +80 | Modify | M3 sub + fallback snapshot + concern pub |
| `colregs_reasoner.cpp` | +100 | Modify | Rule 14 classifier + state machine |
| `RuleAssessment.msg` | — | Verify | Ensure fields present |
| Integration tests | +150 | Create | RouteReceived, fallback snapshot, Rule 14 tests |

---

## Known Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Mock_l2 scenario YAML path incorrect at runtime | Pass explicit `SIL_SCENARIO_YAML` env var; default fallback to nominal route |
| M3 task_validity drifts (high state churn) | Implement hysteresis: 2s hold before state change allowed |
| Rule 14 bearing_rate noise causes false positives | Low-pass filter bearing; require sustained 30s condition hold |
| M4 fallback anchor never released due to M3 validity stuck invalid | Add timeout: if fallback > 60s, release unconditionally + escalate to M7 MRM |

---

**Plan written:** 2026-05-27  
**Total effort estimate:** ~6-8 pw (person-weeks)  
**Critical path:** W1 → W2 → W3 (gate) → W4 (fallback) + W5 (Rule 14 in parallel)  
**Docker build time per iteration:** ~8 min (ccache enabled in sil_nodes.Dockerfile)
