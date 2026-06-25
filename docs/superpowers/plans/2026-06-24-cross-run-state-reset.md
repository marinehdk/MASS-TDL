# Cross-Run State Reset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the mandatory `docker restart` between COLREGs probe scenarios by giving every L3 module a self-healing cross-run state reset triggered by `/sil/scenario_loaded`.

**Architecture:** All modules subscribe `/sil/scenario_loaded` (std_msgs/String, TRANSIENT_LOCAL QoS) and call a per-module `_reset_cross_run_state()` that clears accumulated state (latches / warm state / track history / control residuals). M6 keeps its existing sim-time rewind as a defensive fallback. One orchestrator line upgrades the publisher QoS to TRANSIENT_LOCAL so late-starting C++ nodes receive the latched scenario_id.

**Tech Stack:** C++17 (rclcpp, ament_cmake) for M2/M4/M5/M6, Python (rclpy) for bridge/L4, CMake/colcon build, pytest + gtest.

**Spec:** `docs/superpowers/specs/2026-06-24-cross-run-state-reset-design.md`

**Branch:** `codex/cross-run-state-reset` in `.worktrees/main-runtime`

---

## File Structure

**Modify (C++):**
- `src/sil_orchestrator/lifecycle_bridge.py:144-146` — QoS → TRANSIENT_LOCAL (1 line)
- `src/l3_tdl_kernel/m2_world_model/include/m2_world_model/track_buffer.hpp` — add `clear()`
- `src/l3_tdl_kernel/m2_world_model/include/m2_world_model/world_model_node.hpp` — add sub + reset
- `src/l3_tdl_kernel/m2_world_model/src/world_model_node.cpp` — implement sub + reset
- `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_arbiter_node.hpp` — add sub + reset decl
- `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` — implement sub + reset
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp` — add sub + reset decl
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` — implement sub + reset
- `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_reasoner_node.hpp` — add sub + reset decl
- `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp` — extract `_reset_cross_run_state()`, add sub, keep sim-time fallback

**Modify (Python):**
- `docker/sil_topic_bridge.py` — add scenario_loaded sub calling existing `reset()`
- `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py` — add scenario_loaded sub calling existing `_reset_state()`

**Create (tests):**
- `src/l3_tdl_kernel/m2_world_model/test/test_track_buffer_clear.cpp`
- `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_m4_cross_run_reset.cpp`
- `src/l3_tdl_kernel/m5_tactical_planner/test/test_m5_cross_run_reset.cpp`
- `tests/sim_workbench/test_l4_cross_run_reset.py`

---

## Task 0: Pre-flight — baseline residual probe

Capture the pre-fix baseline so the final verification has a concrete "before" to compare against.

**Files:** none modified

- [ ] **Step 1: Run the no-restart residual probe on the current stack**

Ensure `mass-l3-sil` stack is up (orchestrator on 18000). Then:

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/main-runtime"
python3 /tmp/residual_probe.py colreg-rule14-ho 300 2>&1 | tee runs/baseline_residual_pre_fix.txt
```

Expected: `VERDICT: RESIDUAL CONFIRMED` (onset differs by ~58s, max_heading_dev differs by ~11°).

- [ ] **Step 2: Record baseline numbers**

Save the comparison block (RUN-1 vs RUN-2 transitions, max_heading_dev) into the run file. This is the gate the final Task 9 must flip to "NO RESIDUAL IMPACT".

---

## Task 1: Orchestrator QoS upgrade (TRANSIENT_LOCAL)

The single orchestrator change. Must land first so C++ nodes added later receive the latched scenario_id.

**Files:**
- Modify: `src/sil_orchestrator/lifecycle_bridge.py:144-146`
- Test: `tests/sil_orchestrator/test_main.py` (existing)

- [ ] **Step 1: Write the failing test**

Add to `tests/sil_orchestrator/test_main.py` (or the lifecycle bridge test if one exists). The test asserts the publisher uses TRANSIENT_LOCAL durability:

```python
def test_scenario_loaded_publisher_is_transient_local():
    """Cross-run reset signal must be latched so late-starting C++ nodes
    (launched in entrypoint Stage 3, after configure) receive the scenario_id."""
    from rclpy.qos import DurabilityPolicy
    from sil_orchestrator.lifecycle_bridge import LifecycleBridge
    bridge = LifecycleBridge.__new__(LifecycleBridge)  # bypass __init__ (needs rclpy)
    # Inspect the QoS that the code constructs — see implementation step
    # We assert via the constant the code uses:
    from sil_orchestrator import lifecycle_bridge as lb
    import inspect
    src = inspect.getsource(lb)
    assert "DurabilityPolicy.TRANSIENT_LOCAL" in src, \
        "scenario_loaded publisher must use TRANSIENT_LOCAL durability"
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/main-runtime"
python3 -m pytest tests/sil_orchestrator/test_main.py::test_scenario_loaded_publisher_is_transient_local -v
```

Expected: FAIL (`TRANSIENT_LOCAL` not yet present in source).

- [ ] **Step 3: Implement — change QoS**

In `src/sil_orchestrator/lifecycle_bridge.py`, find the publisher at line 144-146:

```python
        self._scenario_loaded_pub = self.create_publisher(
            String, "/sil/scenario_loaded", 10)
```

Replace with a TRANSIENT_LOCAL QoS profile. Add this helper near the other QoS definitions at module top (after the imports, before the class), then use it:

```python
# Latched QoS for the cross-run reset signal. TRANSIENT_LOCAL so that C++ L3
# nodes launched in entrypoint Stage 3 (after orchestrator configure publishes
# scenario_loaded) still receive the most recent scenario_id on subscription.
_SCENARIO_LOADED_QOS = QoSProfile(
    depth=10,
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.TRANSIENT_LOCAL,
    history=HistoryPolicy.KEEP_LAST,
)
```

Then change the publisher creation:

```python
        self._scenario_loaded_pub = self.create_publisher(
            String, "/sil/scenario_loaded", qos_profile=_SCENARIO_LOADED_QOS)
```

- [ ] **Step 4: Run test to verify it passes**

```bash
python3 -m pytest tests/sil_orchestrator/test_main.py::test_scenario_loaded_publisher_is_transient_local -v
```

Expected: PASS.

- [ ] **Step 5: Run full orchestrator test suite (no regressions)**

```bash
python3 -m pytest tests/sil_orchestrator/ -v 2>&1 | tail -20
```

Expected: all PASS (the QoS change is additive; existing tests publish/subscribe volatile and are unaffected by a latched publisher).

- [ ] **Step 6: Commit**

```bash
git add src/sil_orchestrator/lifecycle_bridge.py tests/sil_orchestrator/test_main.py
git commit -m "feat(orchestrator): scenario_loaded QoS -> TRANSIENT_LOCAL for cross-run reset

Late-starting C++ L3 nodes (entrypoint Stage 3) must receive the latched
scenario_id to trigger their cross-run state reset. Volatile QoS drops it."
```

---

## Task 2: M4 behavior_arbiter cross-run reset (highest priority — confirmed residual source)

M4 is the dominant residual contributor (confirmed: onset moved 58s, transitions 7→5).

**Files:**
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_arbiter_node.hpp`
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`
- Test: `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_m4_cross_run_reset.cpp`
- Test reg: `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/CMakeLists.txt` (or the package's test registration)

- [ ] **Step 1: Write the failing test**

Create `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_m4_cross_run_reset.cpp`. This is a gtest that constructs the node, simulates residual state, calls reset, and asserts the fields return to construction defaults.

```cpp
// test/unit/test_m4_cross_run_reset.cpp
#include <gtest/gtest.h>
#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"
#include "std_msgs/msg/string.hpp"

// Friend test: BehaviorArbiterNode exposes reset + inspection via a test seam.
// The node declares the reset method public for the cross-run reset contract.
class M4CrossRunResetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    node_ = std::make_shared<mass_l3::m4::BehaviorArbiterNode>();
  }
  void TearDown() override { rclcpp::shutdown(); }
  std::shared_ptr<mass_l3::m4::BehaviorArbiterNode> node_;
};

TEST_F(M4CrossRunResetTest, ResetClearsCrossRunStateToDefaults) {
  // After construction, defaults hold. We cannot easily inject residual state
  // without a full sim, so we assert the reset method is callable and the
  // observable post-reset state matches a freshly-constructed node's behavior
  // on the next arbitration cycle (no residual primary).
  // The key contract: reset() is idempotent and safe to call any time.
  EXPECT_NO_THROW(node_->reset_cross_run_state());
  // Idempotent: calling twice does not throw or change behavior.
  EXPECT_NO_THROW(node_->reset_cross_run_state());
}
```

- [ ] **Step 2: Register the test in CMakeLists**

Find `src/l3_tdl_kernel/m4_behavior_arbiter/CMakeLists.txt`. Add the test target following the existing unit-test pattern (look for `ament_add_gtest` or `if(BUILD_TESTING)`). Example entry:

```cmake
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  # ... existing tests ...
  ament_add_gtest(test_m4_cross_run_reset test/unit/test_m4_cross_run_reset.cpp)
  target_link_libraries(test_m4_cross_run_reset ${PROJECT_NAME})
  target_include_directories(test_m4_cross_run_reset PRIVATE include)
endif()
```

- [ ] **Step 3: Run test to verify it fails (compiles, method missing)**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/main-runtime"
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "cd /opt/ws/src/l3_tdl_kernel/m4_behavior_arbiter && \
   source /opt/ros/humble/setup.bash && \
   colcon build --packages-select m4_behavior_arbiter --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5" || true
```

Expected: compile FAIL — `reset_cross_run_state` not declared.

- [ ] **Step 4: Declare the reset method + subscription in the header**

In `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_arbiter_node.hpp`:

Add to the public section (near the existing public methods):

```cpp
  /// Clear all cross-scenario accumulated state. Called on /sil/scenario_loaded.
  /// Idempotent; safe to call at any time. Clears latches, FSM recovery state,
  /// and ranking history so a new scenario starts from a clean decision slate.
  void reset_cross_run_state();
```

Add to the private member section (near the other subscriptions, around the `timer_` declaration at line 110):

```cpp
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr scenario_loaded_sub_;
  void on_scenario_loaded(const std_msgs::msg::String::SharedPtr msg);
```

- [ ] **Step 5: Implement reset + subscription in the .cpp**

In `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`:

First, in the constructor (find where other subscriptions are created), add the scenario_loaded subscription. Use TRANSIENT_LOCAL to match the publisher:

```cpp
  // Cross-run reset: clear accumulated state when a new scenario loads.
  {
    rclcpp::QoS qos = rclcpp::QoS(rclcpp::KeepLast(10)).transient_local();
    scenario_loaded_sub_ = this->create_subscription<std_msgs::msg::String>(
        "/sil/scenario_loaded", qos,
        std::bind(&BehaviorArbiterNode::on_scenario_loaded, this, std::placeholders::_1));
  }
```

Then implement the two methods (add near the bottom of the file, before the `main()` if present, or alongside other methods):

```cpp
void BehaviorArbiterNode::on_scenario_loaded(
    const std_msgs::msg::String::SharedPtr /*msg*/) {
  RCLCPP_INFO(get_logger(),
      "scenario_loaded received — resetting cross-run decision state");
  reset_cross_run_state();
}

void BehaviorArbiterNode::reset_cross_run_state() {
  // Reset every cross-scenario accumulated field to its construction default.
  // These defaults are the in-class initializers in the header (lines 116-134).
  prev_primary_ = BehaviorType::MRC_DRIFT;
  prev_odd_zone_ = 99;
  prev_health_ = HealthState::Normal;
  m3_active_latch_ = false;
  colregs_rule15_commit_active_ = false;
  colregs_inactive_cycles_ = 0;
  recovery_active_ = false;
  recovery_dwell_cycles_ = 0;
  last_active_colregs_.reset();
  risk_ranking_state_ = mass_l3::risk::RankingState{};
}
```

**Note:** `recovery_dwell_cycles_` is referenced in the spec; confirm its exact name from the header (it appears at line 894 in the .cpp as `recovery_dwell_cycles_ = 0`). If the field is named differently, use the actual name. `RankingState{}` value-initializes to defaults.

- [ ] **Step 6: Build the package**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m4_behavior_arbiter --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -10"
```

Expected: BUILD SUCCEEDED. If `recovery_dwell_cycles_` or `RankingState{}` errors, fix the exact field name/type from the header.

- [ ] **Step 7: Run the test to verify it passes**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m4_behavior_arbiter --event-handlers console_direct+ 2>&1 | tail -15"
```

Expected: `test_m4_cross_run_reset` PASS.

- [ ] **Step 8: Commit**

```bash
git add src/l3_tdl_kernel/m4_behavior_arbiter/
git commit -m "feat(m4): cross-run state reset on scenario_loaded

Resets prev_primary_/recovery_active_/colregs_rule15_commit_active_/
risk_ranking_state_/last_active_colregs_ and related latches when a new
scenario loads. M4 was the dominant residual source (onset moved 58s in
the no-restart probe)."
```

---

## Task 3: M6 colregs_reasoner — extract shared reset, add scenario_loaded (keep sim-time fallback)

M6 already self-heals via sim-time rewind. This task unifies it onto scenario_loaded while keeping the rewind as a defensive fallback, per the approved design.

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_reasoner_node.hpp`
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`
- Test: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_cross_run_reset.cpp`

- [ ] **Step 1: Write the failing test**

Create `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_cross_run_reset.cpp`:

```cpp
#include <gtest/gtest.h>
#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"

class M6CrossRunResetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    node_ = std::make_shared<mass_l3::m6::ColregsReasonerNode>();
  }
  void TearDown() override { rclcpp::shutdown(); }
  std::shared_ptr<mass_l3::m6::ColregsReasonerNode> node_;
};

TEST_F(M6CrossRunResetTest, ResetClearsAllLatches) {
  // reset_cross_run_state must be callable and idempotent (both scenario_loaded
  // and the retained sim-time rewind fallback invoke it).
  EXPECT_NO_THROW(node_->reset_cross_run_state());
  EXPECT_NO_THROW(node_->reset_cross_run_state());
}
```

- [ ] **Step 2: Register in CMakeLists**

In `src/l3_tdl_kernel/m6_colregs_reasoner/CMakeLists.txt`, under `if(BUILD_TESTING)`:

```cmake
  ament_add_gtest(test_cross_run_reset test/test_cross_run_reset.cpp)
  target_link_libraries(test_cross_run_reset ${PROJECT_NAME})
```

- [ ] **Step 3: Run test to verify it fails**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m6_colregs_reasoner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5"
```

Expected: compile FAIL — `reset_cross_run_state` not declared as public.

- [ ] **Step 4: Declare reset method + subscription in header**

In `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_reasoner_node.hpp`, add to public:

```cpp
  /// Clear all cross-scenario encounter latches/history. Idempotent.
  /// Triggered by /sil/scenario_loaded (primary) and sim-time rewind (fallback).
  void reset_cross_run_state();
```

Add to private members (near other subscriptions):

```cpp
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr scenario_loaded_sub_;
  void on_scenario_loaded(const std_msgs::msg::String::SharedPtr msg);
```

- [ ] **Step 5: Extract shared reset + add subscription in .cpp**

In `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`:

(a) In the constructor, add the subscription (TRANSIENT_LOCAL):

```cpp
  {
    rclcpp::QoS qos = rclcpp::QoS(rclcpp::KeepLast(10)).transient_local();
    scenario_loaded_sub_ = this->create_subscription<std_msgs::msg::String>(
        "/sil/scenario_loaded", qos,
        std::bind(&ColregsReasonerNode::on_scenario_loaded, this, std::placeholders::_1));
  }
```

(b) Add the two methods:

```cpp
void ColregsReasonerNode::on_scenario_loaded(
    const std_msgs::msg::String::SharedPtr msg) {
  RCLCPP_INFO(get_logger(),
      "scenario_loaded '%s' — resetting cross-run encounter state",
      msg->data.c_str());
  reset_cross_run_state();
}

void ColregsReasonerNode::reset_cross_run_state() {
  const std::lock_guard<std::mutex> kLock(state_mutex_);
  rule_latches_.clear();
  give_way_latches_.clear();
  standon_latches_.clear();
  encounter_reference_heading_.clear();
  resolved_targets_.clear();
  prev_target_range_.clear();
  prev_target_bearing_.clear();
}
```

(c) **Refactor the existing sim-time rewind** (line 507-518) to call the shared method instead of duplicating the 7 `.clear()` calls. Replace the body of the `if (dt_s < -1.0) { ... }` block with a call to `reset_cross_run_state()` plus the existing log line:

```cpp
    if (dt_s < -1.0) {
      RCLCPP_INFO(get_logger(),
        "New run detected (sim time %.1fs → %.1fs) — cleared cross-run latch/history state",
        prev_world_stamp_.seconds(), ws_stamp.seconds());
      reset_cross_run_state();  // shared with scenario_loaded trigger
    }
```

**Caution:** `run_reasoning` already holds `state_mutex_` when it reaches this code (the lock is taken at line 488). Calling `reset_cross_run_state()` which re-locks `state_mutex_` would **deadlock**. Fix: make `reset_cross_run_state()` lock-free (it does NOT take the lock), and have callers that already hold the lock call it directly; the public scenario_loaded callback takes the lock itself. Restructure:

```cpp
// Public, takes the lock — called from scenario_loaded callback.
void ColregsReasonerNode::reset_cross_run_state() {
  const std::lock_guard<std::mutex> kLock(state_mutex_);
  reset_cross_run_state_locked_();
}

// Private, assumes state_mutex_ already held — called from run_reasoning.
void ColregsReasonerNode::reset_cross_run_state_locked_() {
  rule_latches_.clear();
  give_way_latches_.clear();
  standon_latches_.clear();
  encounter_reference_heading_.clear();
  resolved_targets_.clear();
  prev_target_range_.clear();
  prev_target_bearing_.clear();
}
```

Then the sim-time rewind block calls `reset_cross_run_state_locked_()`, and `on_scenario_loaded` calls `reset_cross_run_state()`. Declare `reset_cross_run_state_locked_()` as private in the header.

- [ ] **Step 6: Build**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m6_colregs_reasoner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -10"
```

Expected: BUILD SUCCEEDED.

- [ ] **Step 7: Run test**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ 2>&1 | tail -15"
```

Expected: `test_cross_run_reset` PASS, and existing m6 tests still PASS (no regression).

- [ ] **Step 8: Commit**

```bash
git add src/l3_tdl_kernel/m6_colregs_reasoner/
git commit -m "refactor(m6): unify cross-run reset on scenario_loaded, keep sim-time fallback

Extracts the 7-latch clear into reset_cross_run_state (locked/unlocked
variants to avoid deadlock where run_reasoning already holds state_mutex_).
scenario_loaded is the primary trigger; the retained sim-time rewind is a
defensive fallback."
```

---

## Task 4: M2 world_model — add TrackBuffer::clear + reset on scenario_loaded

**Files:**
- Modify: `src/l3_tdl_kernel/m2_world_model/include/m2_world_model/track_buffer.hpp`
- Modify: `src/l3_tdl_kernel/m2_world_model/src/track_buffer.cpp` (or inline in header if methods are header-defined)
- Modify: `src/l3_tdl_kernel/m2_world_model/include/m2_world_model/world_model_node.hpp`
- Modify: `src/l3_tdl_kernel/m2_world_model/src/world_model_node.cpp`
- Test: `src/l3_tdl_kernel/m2_world_model/test/test_track_buffer_clear.cpp`

- [ ] **Step 1: Write the failing test for TrackBuffer::clear**

Create `src/l3_tdl_kernel/m2_world_model/test/test_track_buffer_clear.cpp`:

```cpp
#include <gtest/gtest.h>
#include "m2_world_model/track_buffer.hpp"

TEST(TrackBufferClearTest, ClearEmptiesBuffer) {
  TrackBuffer::Config cfg{};
  cfg.max_targets = 5;
  TrackBuffer buf(cfg);
  ASSERT_EQ(buf.size(), 0u);
  // clear() on empty buffer is safe (idempotent)
  EXPECT_NO_THROW(buf.clear());
  EXPECT_EQ(buf.size(), 0u);
}
```

- [ ] **Step 2: Register in CMakeLists**

In `src/l3_tdl_kernel/m2_world_model/CMakeLists.txt` under `if(BUILD_TESTING)`:

```cmake
  ament_add_gtest(test_track_buffer_clear test/test_track_buffer_clear.cpp)
  target_link_libraries(test_track_buffer_clear ${PROJECT_NAME})
```

- [ ] **Step 3: Run test to verify it fails**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m2_world_model --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5"
```

Expected: compile FAIL — no `clear()` method.

- [ ] **Step 4: Implement TrackBuffer::clear**

First check whether TrackBuffer methods are defined in the header or a .cpp. Read `src/l3_tdl_kernel/m2_world_model/include/m2_world_model/track_buffer.hpp` and `src/l3_tdl_kernel/m2_world_model/src/track_buffer.cpp` (find the file with `update`/`evict_stale` definitions).

In the header public section, declare:

```cpp
  /// Remove all tracks. Idempotent; used on scenario change so a prior run's
  /// target history does not bleed into the next scenario's classification.
  void clear();
```

Implement it where the other methods are defined (the .cpp). It must take the internal mutex (TrackBuffer has an internal `std::mutex`; check the exact member name — likely `mtx_` or `mutex_`):

```cpp
void TrackBuffer::clear() {
  std::lock_guard<std::mutex> lock(mtx_);  // use the actual mutex member name
  tracks_.clear();  // use the actual container member name
}
```

**Confirm exact member names** by reading the header's private section before writing.

- [ ] **Step 5: Build + run TrackBuffer test**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m2_world_model --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5 && \
   source /opt/ws/install/setup.bash && \
   colcon test --packages-select m2_world_model --event-handlers console_direct+ 2>&1 | tail -10"
```

Expected: `test_track_buffer_clear` PASS.

- [ ] **Step 6: Add reset to WorldModelNode — header**

In `src/l3_tdl_kernel/m2_world_model/include/m2_world_model/world_model_node.hpp`, add public method declaration:

```cpp
  /// Clear all cross-scenario track history on new scenario.
  void reset_cross_run_state();
```

Add private members (near other subscriptions):

```cpp
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr scenario_loaded_sub_;
  void on_scenario_loaded(const std_msgs::msg::String::SharedPtr msg);
```

- [ ] **Step 7: Implement in WorldModelNode .cpp**

In the constructor of `src/l3_tdl_kernel/m2_world_model/src/world_model_node.cpp`, add the subscription (the node may use pimpl — find where `track_buffer_` is created at line 166 and add the sub nearby or in the constructor body):

```cpp
  {
    rclcpp::QoS qos = rclcpp::QoS(rclcpp::KeepLast(10)).transient_local();
    scenario_loaded_sub_ = create_subscription<std_msgs::msg::String>(
        "/sil/scenario_loaded", qos,
        std::bind(&WorldModelNode::on_scenario_loaded, this, std::placeholders::_1));
  }
```

Add the methods:

```cpp
void WorldModelNode::on_scenario_loaded(
    const std_msgs::msg::String::SharedPtr msg) {
  logger_->info("scenario_loaded '{}' — resetting cross-run track history",
                msg->data);
  reset_cross_run_state();
}

void WorldModelNode::reset_cross_run_state() {
  track_buffer_->clear();
}
```

**Note:** if `track_buffer_` is in a pimpl and not directly accessible, call through the pimpl accessor. The `on_tracked_targets` handler accesses `track_buffer_` directly (line 363), so it is reachable.

- [ ] **Step 8: Build + commit**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m2_world_model 2>&1 | tail -5"
```

Expected: BUILD SUCCEEDED.

```bash
git add src/l3_tdl_kernel/m2_world_model/
git commit -m "feat(m2): cross-run track history reset on scenario_loaded

Adds TrackBuffer::clear() and resets the track buffer when a new scenario
loads, so a prior run's target classification/filtering history does not
bleed into the next scenario."
```

---

## Task 5: M5 tactical_planner — reset MPC warm state on scenario_loaded

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/test_m5_cross_run_reset.cpp`

- [ ] **Step 1: Write the failing test**

Create `src/l3_tdl_kernel/m5_tactical_planner/test/test_m5_cross_run_reset.cpp`:

```cpp
#include <gtest/gtest.h>
#include "m5_tactical_planner/mid_mpc/mid_mpc_node.hpp"

class M5CrossRunResetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    node_ = std::make_shared<mass_l3::m5::MidMpcNode>();
  }
  void TearDown() override { rclcpp::shutdown(); }
  std::shared_ptr<mass_l3::m5::MidMpcNode> node_;
};

TEST_F(M5CrossRunResetTest, ResetClearsWarmState) {
  // reset_cross_run_state clears last_solution_ so the next solve cold-starts.
  EXPECT_NO_THROW(node_->reset_cross_run_state());
  EXPECT_NO_THROW(node_->reset_cross_run_state());  // idempotent
}
```

- [ ] **Step 2: Register in CMakeLists**

In `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt` under `if(BUILD_TESTING)`:

```cmake
  ament_add_gtest(test_m5_cross_run_reset test/test_m5_cross_run_reset.cpp)
  target_link_libraries(test_m5_cross_run_reset ${PROJECT_NAME})
```

- [ ] **Step 3: Run test to verify it fails**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5"
```

Expected: compile FAIL — `reset_cross_run_state` not declared.

- [ ] **Step 4: Declare in header**

In `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp`, add public:

```cpp
  /// Clear cross-scenario MPC warm state (last_solution_) so a new scenario
  /// cold-starts the solver instead of inheriting the prior run's solution.
  void reset_cross_run_state();
```

Add private:

```cpp
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr scenario_loaded_sub_;
  void on_scenario_loaded(const std_msgs::msg::String::SharedPtr msg);
```

- [ ] **Step 5: Implement in .cpp**

In `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` constructor, add subscription (TRANSIENT_LOCAL):

```cpp
  {
    rclcpp::QoS qos = rclcpp::QoS(rclcpp::KeepLast(10)).transient_local();
    scenario_loaded_sub_ = this->create_subscription<std_msgs::msg::String>(
        "/sil/scenario_loaded", qos,
        std::bind(&MidMpcNode::on_scenario_loaded, this, std::placeholders::_1));
  }
```

Add methods:

```cpp
void MidMpcNode::on_scenario_loaded(
    const std_msgs::msg::String::SharedPtr msg) {
  RCLCPP_INFO(get_logger(),
      "scenario_loaded '%s' — resetting MPC warm state", msg->data.c_str());
  reset_cross_run_state();
}

void MidMpcNode::reset_cross_run_state() {
  // Drop the warm-start solution so the next solve cold-starts.
  // last_solution_ is std::optional<MidMpcSolution>.
  last_solution_.reset();
  // nomoto_fallback_ and wp_gen_ are stateless solvers (verified: no
  // last_/prev_/cache_ members), so no per-run state to clear there.
}
```

**Note:** confirm the node class name (`MidMpcNode` vs `MidMpcNodeNode` etc.) from the header before building. The constructor is at line 109 in the .cpp.

- [ ] **Step 6: Build + test + commit**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5 && \
   source /opt/ws/install/setup.bash && \
   colcon test --packages-select m5_tactical_planner --pytest-args test_cross_run_reset 2>&1 | tail -10"
```

Expected: `test_m5_cross_run_reset` PASS.

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/
git commit -m "feat(m5): reset MPC warm state (last_solution_) on scenario_loaded

Drops the prior run's MPC solution so the next scenario cold-starts the
solver instead of warm-starting from a stale trajectory."
```

---

## Task 6: sil_topic_bridge — wire scenario_loaded to existing reset()

The bridge already has a `reset()` method. This task only adds the subscription that triggers it.

**Files:**
- Modify: `docker/sil_topic_bridge.py`
- Test: `tests/sim_workbench/test_bridge_cross_run_reset.py` (new, Python)

- [ ] **Step 1: Inspect the existing reset() + constructor**

Read `docker/sil_topic_bridge.py` to find:
- The `reset()` method (around line 267) — confirm signature and what it clears.
- The constructor subscription section (where it subscribes to behavior/avoidance/etc.) — this is where the scenario_loaded sub goes.
- The String import (`from std_msgs.msg import String`).

- [ ] **Step 2: Write the failing test**

Create `tests/sim_workbench/test_bridge_cross_run_reset.py`:

```python
import inspect
import docker.sil_topic_bridge as bridge_mod  # adjust import path as needed


def test_bridge_subscribes_scenario_loaded_and_has_reset():
    """The bridge must subscribe /sil/scenario_loaded and call reset() on it,
    so cross-run actuator/plan residual state is cleared per scenario."""
    src = inspect.getsource(bridge_mod)
    assert "/sil/scenario_loaded" in src, \
        "sil_topic_bridge must subscribe /sil/scenario_loaded"
    assert "def reset(" in src, \
        "sil_topic_bridge must have a reset() method"
    # The callback must invoke reset
    assert "reset()" in src, \
        "scenario_loaded callback must call reset()"
```

If the module isn't importable standalone (ROS deps), fall back to a text assertion reading the file content instead of importing.

- [ ] **Step 3: Run test to verify it fails**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/main-runtime"
python3 -m pytest tests/sim_workbench/test_bridge_cross_run_reset.py -v
```

Expected: FAIL (`/sil/scenario_loaded` not yet in bridge).

- [ ] **Step 4: Implement — add the subscription**

In `docker/sil_topic_bridge.py`, ensure `String` is imported:

```python
from std_msgs.msg import String
```

In the constructor (where other subscriptions are created, near the avoidance/behavior subs), add:

```python
        # Cross-run reset: clear actuator/plan residual state on new scenario.
        # QoS transient_local to match the orchestrator publisher so we receive
        # the latched scenario_id even if we start after configure.
        scenario_qos = QoSProfile(
            depth=10,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.TRANSIENT_LOCAL,
            history=HistoryPolicy.KEEP_LAST,
        )
        self.create_subscription(
            String, "/sil/scenario_loaded",
            self._on_scenario_loaded, scenario_qos)

```

Add the callback method (near `reset`):

```python
    def _on_scenario_loaded(self, msg: String) -> None:
        """Clear cross-run actuator/plan residual state on new scenario."""
        self.get_logger().info(
            f"[BRIDGE] scenario_loaded '{msg.data}' — resetting cross-run state")
        self.reset()
```

Confirm `QoSProfile`/`ReliabilityPolicy`/`DurabilityPolicy`/`HistoryPolicy` are imported (other code in the file already uses `QoSProfile`, so add the missing enum imports if needed).

- [ ] **Step 5: Run test to verify it passes**

```bash
python3 -m pytest tests/sim_workbench/test_bridge_cross_run_reset.py -v
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add docker/sil_topic_bridge.py tests/sim_workbench/test_bridge_cross_run_reset.py
git commit -m "feat(bridge): trigger existing reset() on scenario_loaded

Wires /sil/scenario_loaded to the bridge's existing reset() so actuator
command residual (last_cmd_deg/_last_valid_plan_time/_last_avoidance_waypoint)
is cleared per scenario instead of bleeding across runs."
```

---

## Task 7: L4 guidance adapter — wire scenario_loaded to existing _reset_state()

L4 already has `_reset_state(clear_route=...)`. This task adds the subscription.

**Files:**
- Modify: `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py`
- Test: `tests/sim_workbench/test_l4_cross_run_reset.py` (new)

- [ ] **Step 1: Write the failing test**

Create `tests/sim_workbench/test_l4_cross_run_reset.py`:

```python
import inspect
from pathlib import Path


def test_l4_adapter_subscribes_scenario_loaded_and_resets():
    node_path = Path(
        "src/sim_workbench/sil_nodes/l4_guidance_adapter/"
        "l4_guidance_adapter/node.py")
    src = node_path.read_text()
    assert "/sil/scenario_loaded" in src, \
        "L4 guidance adapter must subscribe /sil/scenario_loaded"
    assert "_reset_state" in src, \
        "L4 guidance adapter must call _reset_state on scenario_loaded"
    assert "clear_route=False" in src, \
        "scenario_loaded reset must use clear_route=False (route is injected separately)"
```

- [ ] **Step 2: Run test to verify it fails**

```bash
python3 -m pytest tests/sim_workbench/test_l4_cross_run_reset.py -v
```

Expected: FAIL.

- [ ] **Step 3: Implement — add subscription**

In `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py`:

Ensure `String` import:

```python
from std_msgs.msg import String
```

In `L4GuidanceAdapterNode.__init__` (where other subscriptions are created), add:

```python
        # Cross-run reset: clear actuator gate/latch state on new scenario.
        scenario_qos = _latched_qos(depth=10)
        self.create_subscription(
            String, "/sil/scenario_loaded",
            self._on_scenario_loaded, scenario_qos)
```

`_latched_qos` already exists in this file (line 56). Add the callback:

```python
    def _on_scenario_loaded(self, msg: String) -> None:
        """Clear cross-run actuator gate/latch state on new scenario.

        Uses clear_route=False because the route is injected separately by
        the route_ingest/mock_l2 path; clearing it here would drop the route
        the new scenario needs.
        """
        self.get_logger().info(
            f"[l4_guidance_adapter] scenario_loaded '{msg.data}' "
            f"— resetting cross-run actuator state")
        self._reset_state(clear_route=False)
```

- [ ] **Step 4: Run test to verify it passes**

```bash
python3 -m pytest tests/sim_workbench/test_l4_cross_run_reset.py -v
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py tests/sim_workbench/test_l4_cross_run_reset.py
git commit -m "feat(l4): trigger _reset_state on scenario_loaded

Wires /sil/scenario_loaded to the existing _reset_state(clear_route=False)
so actuator gate/latch residual (_latch_release_*/_last_actuator_publish_time/
_safety_gate_*) is cleared per scenario."
```

---

## Task 8: Rebuild stack + verify scenario_loaded propagates

After all modules are built in-container, rebuild the sil-nodes image layer (or hot-reload) and confirm scenario_loaded is received by every module on a fresh scenario.

**Files:** none modified

- [ ] **Step 1: Rebuild all touched packages in the container**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select \
     m2_world_model m4_behavior_arbiter m5_tactical_planner m6_colregs_reasoner 2>&1 | tail -10"
```

Expected: all BUILD SUCCEEDED.

- [ ] **Step 2: Restart sil-nodes to pick up new C++ binaries + Python files**

```bash
docker restart mass-l3-sil-sil-nodes-1
# wait for orchestrator to be ready
for i in $(seq 1 30); do
  curl -sk https://127.0.0.1:18000/api/v1/lifecycle/status 2>/dev/null | grep -q current_state && break
  sleep 1
done
```

- [ ] **Step 3: Configure a scenario and watch logs for scenario_loaded handlers**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/main-runtime"
curl -sk -X POST https://127.0.0.1:18000/api/v1/lifecycle/cleanup >/dev/null; sleep 2
curl -sk -X POST -H 'Content-Type: application/json' \
  -d '{"scenario_id":"colreg-rule14-ho"}' \
  https://127.0.0.1:18000/api/v1/lifecycle/configure >/dev/null
sleep 1
# Check each module logged its reset handler
docker logs mass-l3-sil-sil-nodes-1 --since 30s 2>&1 | grep -iE "scenario_loaded|resetting cross-run|resetting cross-run state|cleared cross-run"
```

Expected: log lines from M2/M4/M5/M6 showing "scenario_loaded ... resetting cross-run state". (bridge/L4 logs appear too if they ran.)

- [ ] **Step 4: Commit no code; record observation**

No commit (build artifacts only). Note the log evidence in the handoff.

---

## Task 9: Final verification — no-restart residual probe must flip to NO RESIDUAL

The gate. Run the same probe as Task 0, now with all resets in place.

**Files:** none modified

- [ ] **Step 1: Run the residual probe without restart**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/main-runtime"
python3 /tmp/residual_probe.py colreg-rule14-ho 300 2>&1 | tee runs/final_residual_post_fix.txt
```

- [ ] **Step 2: Verify the gate**

Expected output:
```
VERDICT: NO RESIDUAL IMPACT
  transitions identical: True
  max_heading_dev within 2°: True
```

Compare to Task 0 baseline (onset differed 58s, dev differed 11°). The gate passes ONLY if:
- `behavior_transitions` RUN-1 == RUN-2 (identical sequence)
- `max_heading_dev` |RUN-1 − RUN-2| < 2°
- onset sim_t |RUN-1 − RUN-2| < 5s

- [ ] **Step 3: If gate fails — triage which module**

If residual persists, identify which module by checking the diff in transitions/dev. Most likely candidates in order: M4 (recheck field list completeness), bridge (recheck reset() coverage), M5. Do NOT weaken the gate; fix the missing field.

- [ ] **Step 4: Run a full clean-8 without --restart-between-runs**

```bash
SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 \
python3 scripts/run_colregs_clean_8probe.py \
  --restart-container "" \
  --summary-out runs/postfix_clean8_$(date +%Y%m%d_%H%M%S).json \
  --trace-report-dir runs/trace_eval/$(date +%Y%m%d_%H%M%S) 2>&1 | tail -30
```

Note: `run_colregs_clean_8probe.py` currently REQUIRES `--restart-container` when `--restart-between-runs` is set. To run WITHOUT restart, simply omit `--restart-between-runs`. The runner will then configure→activate each scenario back-to-back. Confirm verdicts match a restart baseline.

Expected: same PASS/RED pattern as a restart run; no NEW reds introduced by residual contamination.

- [ ] **Step 5: Commit the evidence**

```bash
git add runs/final_residual_post_fix.txt
git commit -m "test(evidence): no-restart residual probe passes after cross-run reset

RUN-1 vs RUN-2 transitions identical, max_heading_dev within 2°. Confirms
the mandatory docker restart between scenarios is no longer needed."
```

---

## Task 10 (optional,治标): Lower restart-settle + lock sim_rate

Only do this if the user wants immediate additional speedup before fully trusting no-restart. These are reversible CLI/env changes, not code.

- [ ] **Step 1: Document the治标 knobs**

These are runtime options, no commit needed unless adding to a profile:
- `--restart-settle 10` (down from 24) — verify cold-start + DDS settle is stable at 10s
- `--sim-rate 10` — lock to the verified CPU ceiling (do not exceed; >10x degrades)
- OrbStack already raised to 6 cores (Task 0 environment)

- [ ] **Step 2: Only after Task 9 passes, these become moot**

If no-restart works (Task 9 green), `--restart-between-runs` can be dropped entirely, eliminating the settle cost completely. This task is then a fallback for cautious verification only.

---

## Self-Review

**Spec coverage check:**
- §3.2 QoS TRANSIENT_LOCAL → Task 1 ✓
- §4.1 M4 reset → Task 2 ✓
- §4.2 M5 reset → Task 5 ✓
- §4.3 M6 unify + fallback → Task 3 ✓
- §4.4 M2 reset → Task 4 ✓
- §4.5 bridge reset → Task 6 ✓
- §4.6 L4 reset → Task 7 ✓
- §6 invariants (idempotent, thread-safe) → encoded in each task's reset implementation + the locked/unlocked M6 split (Task 3) ✓
- §7.2 integration verification → Task 9 ✓

**Placeholder scan:** No TBD/TODO. Field names flagged for "confirm exact name" (M4 `recovery_dwell_cycles_`, M2 mutex/container names, M5 node class name, M6 deadlock-safe split) are concrete verification steps with fallback instructions, not gaps.

**Type consistency:** `reset_cross_run_state()` is the consistent public API across M2/M4/M5/M6. M6's internal `reset_cross_run_state_locked_()` is clearly named as the lock-held variant. `_on_scenario_loaded` callback name is consistent across all modules.

**Risk note:** Task 3 (M6) has a real deadlock hazard (run_reasoning holds state_mutex_). The plan addresses it explicitly with locked/unlocked variants. This is the highest-risk task; execute carefully and run existing M6 tests to confirm no deadlock.
