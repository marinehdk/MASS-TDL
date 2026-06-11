# Bridge De-shadow Strict 8-Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct the first implementation batch so M6/M4/M5 follow the architecture authority chain, remove the current direction/magnitude drift, and prepare strict 8-probe verification while keeping Bridge as a temporary adapter.

**Architecture:** M6 owns COLREGs conflict/role/direction, M4 converts that directive into behavior and heading/speed windows, M5 builds the avoidance plan from explicit COLREGs directive fields, and Bridge remains a transitional transport/control adapter without new tactical authority. Full L4 extraction and final Bridge deletion are follow-up phases after strict 8-probe is green.

**Tech Stack:** ROS2 C++ packages (`m6_colregs_reasoner`, `m4_behavior_arbiter`, `m5_tactical_planner`), gtest via colcon, Python pytest for bridge/scoring regression tests, SIL scenario harness under `scripts/` and `scenarios/COLREGs测试/`.

---

## File Structure

- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp`
  - Responsibility: prevent the generic latch from re-onsetting a target already marked past-and-clear.
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp`
  - Responsibility: regression coverage for finally-past-and-clear onset suppression.
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/colregs_directive.hpp`
  - Responsibility: pure helper for parsing M6 `COLREGsConstraint` direction/min alteration and building M4 windows/ranges.
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/src/colregs_directive.cpp`
  - Responsibility: implementation of direction-aware M4 helper.
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_colregs_directive.cpp`
  - Responsibility: gtest coverage for `STARBOARD`, `PORT`, `REDUCE_SPEED`, and `HOLD`.
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/CMakeLists.txt`
  - Responsibility: compile/link the new helper and test.
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`
  - Responsibility: replace starboard-only extraction/fallback/ranges with `ColregsDirective`.
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp`
  - Responsibility: carry explicit COLREGs fallback direction/min alteration inside `MidMpcInput`.
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
  - Responsibility: populate M5 COLREGs directive fields and build fallback target heading from explicit direction.
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_geometric_fallback.cpp`
  - Responsibility: gtest coverage for starboard, port, reduce-speed, hold, and backward-compatible overload.
- Modify: `tests/docker/test_sil_topic_bridge.py`
  - Responsibility: add a bridge de-shadow regression that M5 plan arrival must not clear or override active M6 authority.

## Parallelization Notes

The implementation can be split into independent ownership lanes:

- Lane A: M6 latch guard and tests.
- Lane B: M4 `colregs_directive` helper and M4 node integration.
- Lane C: M5 explicit fallback helper and Mid-MPC integration.
- Lane D: Python bridge/scoring regression and verification harness checks.

In a shared worktree, run implementation tasks sequentially or with disjoint worker write sets only. Do not let two workers edit the same CMake or source file at the same time.

---

### Task 1: M6 Latch Past-And-Clear Onset Guard

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp`
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp`

- [ ] **Step 1: Write the failing test**

Append this test before the closing anonymous namespace in `test_rule_latch.cpp`:

```cpp
TEST(RuleLatch, DoesNotOnsetWhenAlreadyPastAndClear) {
  RuleLatch latch{1852.0, 1.5};
  RuleEvaluation onset{};
  onset.is_active = true;
  onset.role = Role::GIVE_WAY;
  onset.encounter_type = EncounterType::CROSSING;
  onset.preferred_direction = "STARBOARD";
  onset.min_alteration_deg = 15.0;

  EXPECT_FALSE(latch.update(/*rule_active=*/true, /*cpa_m=*/900.0,
                            /*range_closing=*/true,
                            /*past_and_clear=*/true, &onset));
  EXPECT_FALSE(latch.latched());
  EXPECT_FALSE(latch.has_onset());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ --ctest-args -R test_rule_latch
```

Expected: `RuleLatch.DoesNotOnsetWhenAlreadyPastAndClear` fails because current `RuleLatch::update()` latches without checking `past_and_clear` during onset.

- [ ] **Step 3: Implement minimal guard**

In `rule_latch.hpp`, change the onset condition inside `if (!latched_)` from:

```cpp
if (rule_active && cpa_m < cpa_safe_m_ && range_closing) {
```

to:

```cpp
if (rule_active && cpa_m < cpa_safe_m_ && range_closing && !past_and_clear) {
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ --ctest-args -R test_rule_latch
colcon test-result --verbose
```

Expected: all `test_rule_latch` tests pass.

- [ ] **Step 5: Commit task**

```bash
git add src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp \
        src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp
git commit -m "fix(m6): block past-clear latch re-onset"
```

---

### Task 2: M4 Direction-Aware COLREGs Directive Helper

**Files:**
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/colregs_directive.hpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/src/colregs_directive.cpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_colregs_directive.cpp`
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/CMakeLists.txt`

- [ ] **Step 1: Write the failing helper test**

Create `test/unit/test_colregs_directive.cpp`:

```cpp
#include <gtest/gtest.h>

#include "m4_behavior_arbiter/colregs_directive.hpp"

namespace mass_l3::m4 {
namespace {

l3_msgs::msg::COLREGsConstraint make_msg(const std::string& direction, double min_deg) {
  l3_msgs::msg::COLREGsConstraint msg;
  msg.conflict_detected = true;
  msg.primary_preferred_direction = direction;
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = min_deg;
  msg.constraints.push_back(c);
  return msg;
}

TEST(ColregsDirective, StarboardBuildsPositiveWindowAndAllowedRange) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 30.0));
  EXPECT_EQ(directive.direction, ColregsDirection::Starboard);
  EXPECT_DOUBLE_EQ(directive.min_alteration_deg, 30.0);

  const auto required = required_deviation_deg(directive, 3704.0);
  EXPECT_DOUBLE_EQ(required, 30.0);

  const auto window = directive_heading_window(0.0, directive, required);
  ASSERT_TRUE(window.has_value());
  EXPECT_DOUBLE_EQ(window->heading_min_deg, 15.0);
  EXPECT_DOUBLE_EQ(window->heading_max_deg, 45.0);

  const auto ranges = directive_allowed_ranges(0.0, directive, required);
  ASSERT_EQ(ranges.size(), 1u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 30.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 180.0);
}

TEST(ColregsDirective, PortBuildsNegativeWindowAndAllowedRange) {
  const auto directive = extract_colregs_directive(make_msg("PORT", 25.0));
  EXPECT_EQ(directive.direction, ColregsDirection::Port);

  const auto window = directive_heading_window(0.0, directive, 25.0);
  ASSERT_TRUE(window.has_value());
  EXPECT_DOUBLE_EQ(window->heading_min_deg, 320.0);
  EXPECT_DOUBLE_EQ(window->heading_max_deg, 350.0);

  const auto ranges = directive_allowed_ranges(0.0, directive, 25.0);
  ASSERT_EQ(ranges.size(), 1u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 180.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 335.0);
}

TEST(ColregsDirective, ReduceSpeedAndHoldDoNotCreateHeadingWindow) {
  const auto reduce = extract_colregs_directive(make_msg("REDUCE_SPEED", 15.0));
  EXPECT_EQ(reduce.direction, ColregsDirection::ReduceSpeed);
  EXPECT_FALSE(directive_heading_window(90.0, reduce, 15.0).has_value());
  EXPECT_TRUE(directive_allowed_ranges(90.0, reduce, 15.0).empty());

  const auto hold = extract_colregs_directive(make_msg("HOLD", 15.0));
  EXPECT_EQ(hold.direction, ColregsDirection::Hold);
  EXPECT_FALSE(directive_heading_window(90.0, hold, 15.0).has_value());
  EXPECT_TRUE(directive_allowed_ranges(90.0, hold, 15.0).empty());
}

TEST(ColregsDirective, NoConflictProducesInactiveHold) {
  auto msg = make_msg("STARBOARD", 30.0);
  msg.conflict_detected = false;

  const auto directive = extract_colregs_directive(msg);
  EXPECT_FALSE(directive.conflict_active);
  EXPECT_EQ(directive.direction, ColregsDirection::Hold);
  EXPECT_DOUBLE_EQ(directive.min_alteration_deg, 0.0);
}

}  // namespace
}  // namespace mass_l3::m4
```

- [ ] **Step 2: Wire the test target and verify RED**

Add `src/colregs_directive.cpp` to `m4_core` in `CMakeLists.txt`, and add:

```cmake
  ament_add_gtest(test_colregs_directive test/unit/test_colregs_directive.cpp)
  target_link_libraries(test_colregs_directive m4_core)
  target_include_directories(test_colregs_directive PRIVATE include)
```

Run:

```bash
colcon test --packages-select m4_behavior_arbiter --event-handlers console_direct+ --ctest-args -R test_colregs_directive
```

Expected: compile fails because `m4_behavior_arbiter/colregs_directive.hpp` does not exist yet.

- [ ] **Step 3: Add helper header**

Create `include/m4_behavior_arbiter/colregs_directive.hpp`:

```cpp
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "l3_msgs/msg/colre_gs_constraint.hpp"

namespace mass_l3::m4 {

enum class ColregsDirection : std::uint8_t {
  Hold = 0u,
  Starboard = 1u,
  Port = 2u,
  ReduceSpeed = 3u,
};

struct ColregsDirective {
  bool conflict_active{false};
  ColregsDirection direction{ColregsDirection::Hold};
  double min_alteration_deg{0.0};
};

struct HeadingWindow {
  double heading_min_deg{0.0};
  double heading_max_deg{0.0};
};

[[nodiscard]] double wrap_heading_deg(double heading_deg);
[[nodiscard]] ColregsDirection parse_colregs_direction(const std::string& direction);
[[nodiscard]] ColregsDirective extract_colregs_directive(
    const l3_msgs::msg::COLREGsConstraint& msg);
[[nodiscard]] double required_deviation_deg(
    const ColregsDirective& directive,
    double nearest_target_range_m,
    double cpa_safe_m = 500.0,
    double boldness_factor = 2.5,
    double max_deviation_deg = 120.0);
[[nodiscard]] std::optional<HeadingWindow> directive_heading_window(
    double base_heading_deg,
    const ColregsDirective& directive,
    double required_deviation_deg,
    double half_width_deg = 15.0);
[[nodiscard]] std::vector<std::pair<double, double>> directive_allowed_ranges(
    double base_heading_deg,
    const ColregsDirective& directive,
    double required_deviation_deg);
[[nodiscard]] double signed_deviation_deg(
    const ColregsDirective& directive,
    double required_deviation_deg);

}  // namespace mass_l3::m4
```

- [ ] **Step 4: Add helper implementation**

Create `src/colregs_directive.cpp`:

```cpp
#include "m4_behavior_arbiter/colregs_directive.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace mass_l3::m4 {

double wrap_heading_deg(double heading_deg) {
  double wrapped = std::fmod(heading_deg, 360.0);
  if (wrapped < 0.0) {
    wrapped += 360.0;
  }
  return wrapped;
}

ColregsDirection parse_colregs_direction(const std::string& direction) {
  if (direction == "STARBOARD") {
    return ColregsDirection::Starboard;
  }
  if (direction == "PORT") {
    return ColregsDirection::Port;
  }
  if (direction == "REDUCE_SPEED") {
    return ColregsDirection::ReduceSpeed;
  }
  return ColregsDirection::Hold;
}

ColregsDirective extract_colregs_directive(const l3_msgs::msg::COLREGsConstraint& msg) {
  ColregsDirective out;
  out.conflict_active = msg.conflict_detected;
  if (!out.conflict_active) {
    return out;
  }

  out.direction = parse_colregs_direction(msg.primary_preferred_direction);
  for (const auto& c : msg.constraints) {
    if (c.constraint_type == "colregs" && c.unit == "deg" && c.numeric_value > 0.0) {
      out.min_alteration_deg = std::max(out.min_alteration_deg, c.numeric_value);
    }
  }
  return out;
}

double required_deviation_deg(
    const ColregsDirective& directive,
    double nearest_target_range_m,
    double cpa_safe_m,
    double boldness_factor,
    double max_deviation_deg) {
  if (!directive.conflict_active ||
      (directive.direction != ColregsDirection::Starboard &&
       directive.direction != ColregsDirection::Port)) {
    return 0.0;
  }

  double required = directive.min_alteration_deg;
  if (nearest_target_range_m > cpa_safe_m) {
    const double raw_deg = std::asin(cpa_safe_m / nearest_target_range_m) * 180.0 / M_PI;
    required = std::max(required, raw_deg * boldness_factor);
  } else if (nearest_target_range_m > 0.0) {
    required = max_deviation_deg;
  }
  return std::min(max_deviation_deg, required);
}

double signed_deviation_deg(
    const ColregsDirective& directive,
    double required_deviation) {
  if (directive.direction == ColregsDirection::Starboard) {
    return required_deviation;
  }
  if (directive.direction == ColregsDirection::Port) {
    return -required_deviation;
  }
  return 0.0;
}

std::optional<HeadingWindow> directive_heading_window(
    double base_heading_deg,
    const ColregsDirective& directive,
    double required_deviation,
    double half_width_deg) {
  const double signed_dev = signed_deviation_deg(directive, required_deviation);
  if (signed_dev == 0.0) {
    return std::nullopt;
  }

  const double centre = wrap_heading_deg(base_heading_deg + signed_dev);
  return HeadingWindow{
      wrap_heading_deg(centre - half_width_deg),
      wrap_heading_deg(centre + half_width_deg)};
}

std::vector<std::pair<double, double>> directive_allowed_ranges(
    double base_heading_deg,
    const ColregsDirective& directive,
    double required_deviation) {
  if (directive.direction == ColregsDirection::Starboard && required_deviation > 0.0) {
    return {{wrap_heading_deg(base_heading_deg + required_deviation),
             wrap_heading_deg(base_heading_deg + 180.0)}};
  }
  if (directive.direction == ColregsDirection::Port && required_deviation > 0.0) {
    return {{wrap_heading_deg(base_heading_deg - 180.0),
             wrap_heading_deg(base_heading_deg - required_deviation)}};
  }
  return {};
}

}  // namespace mass_l3::m4
```

- [ ] **Step 5: Run helper test**

Run:

```bash
colcon test --packages-select m4_behavior_arbiter --event-handlers console_direct+ --ctest-args -R test_colregs_directive
```

Expected: `test_colregs_directive` passes.

- [ ] **Step 6: Commit task**

```bash
git add src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/colregs_directive.hpp \
        src/l3_tdl_kernel/m4_behavior_arbiter/src/colregs_directive.cpp \
        src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_colregs_directive.cpp \
        src/l3_tdl_kernel/m4_behavior_arbiter/CMakeLists.txt
git commit -m "feat(m4): add direction-aware COLREGs directive helper"
```

---

### Task 3: Integrate Direction-Aware Directive Into M4 Node

**Files:**
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_m4_node_lifecycle.cpp`

- [ ] **Step 1: Write node regression tests**

Add this helper method inside `BehaviorArbiterTest`:

```cpp
bool anchor_is_set(std::shared_ptr<BehaviorArbiterNode> node) {
  return node->fallback_anchor_set_;
}
```

Append this test before the closing namespace:

```cpp
TEST_F(BehaviorArbiterTest, PortDirectiveDoesNotUseStarboardFallbackAnchor) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = 0;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_preferred_direction = "PORT";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 25.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);

  EXPECT_TRUE(anchor_is_set(node));
  EXPECT_DOUBLE_EQ(get_fallback_anchor_hdg(node), 0.0);
}
```

- [ ] **Step 2: Run test to verify current behavior is wrong**

Run:

```bash
colcon test --packages-select m4_behavior_arbiter --event-handlers console_direct+ --ctest-args -R test_m4_node_lifecycle
```

Expected: the new test fails before integration because the node ignores `primary_preferred_direction="PORT"` and treats the numeric COLREGs value as starboard-only.

- [ ] **Step 3: Include helper and replace directive extraction**

In `behavior_arbiter_node.cpp`, add:

```cpp
#include "m4_behavior_arbiter/colregs_directive.hpp"
```

Inside `arbitration_timer_callback()`, immediately before COLREGs avoidance function construction, compute:

```cpp
ColregsDirective colregs_directive;
if (colregs_received_ && latest_colregs_) {
  colregs_directive = extract_colregs_directive(*latest_colregs_);
}

double nearest_target_range_m = std::numeric_limits<double>::max();
if (latest_world_) {
  for (const auto& tgt : latest_world_->targets) {
    if (tgt.rng_m > 0.0 && tgt.rng_m < nearest_target_range_m) {
      nearest_target_range_m = tgt.rng_m;
    }
  }
}
const double required_dev_deg = required_deviation_deg(
    colregs_directive, nearest_target_range_m);
```

- [ ] **Step 4: Replace the starboard-only COLREGs IvP block**

Replace the `if (colregs_received_ && latest_colregs_ && latest_colregs_->conflict_detected)` COLREGs block with logic using `colregs_directive.direction`.

For turn directives:

```cpp
if (colregs_directive.conflict_active && required_dev_deg > 0.0) {
  IvPFunctionDefault avoid_fn;
  std::vector<IvPFunctionDefault::Piece> avoid_pieces;
  const double signed_dev = signed_deviation_deg(colregs_directive, required_dev_deg);
  const double comfort_upper_deg = std::min(120.0, required_dev_deg + 15.0);
  const double signed_upper =
      colregs_directive.direction == ColregsDirection::Port
          ? -comfort_upper_deg
          : comfort_upper_deg;

  IvPFunctionDefault::Piece penalty_ap;
  penalty_ap.heading_min_deg = wrap_hdg(nominal_hdg - 180.0);
  penalty_ap.heading_max_deg = wrap_hdg(nominal_hdg + signed_dev);
  penalty_ap.speed_min_kn = 0.0;
  penalty_ap.speed_max_kn = speed_max_kn_;
  penalty_ap.utility = 0.05;
  avoid_pieces.push_back(penalty_ap);

  IvPFunctionDefault::Piece optimal_ap;
  optimal_ap.heading_min_deg = wrap_hdg(nominal_hdg + signed_dev);
  optimal_ap.heading_max_deg = wrap_hdg(nominal_hdg + signed_upper);
  optimal_ap.speed_min_kn = 0.0;
  optimal_ap.speed_max_kn = speed_max_kn_;
  optimal_ap.utility = 1.0;
  avoid_pieces.push_back(optimal_ap);

  IvPFunctionDefault::Piece base_ap;
  base_ap.heading_min_deg = 0.0;
  base_ap.heading_max_deg = 359.9;
  base_ap.speed_min_kn = 0.0;
  base_ap.speed_max_kn = speed_max_kn_;
  base_ap.utility = 0.1;
  avoid_pieces.push_back(base_ap);

  avoid_fn.set_pieces(avoid_pieces);
  weighted_fns.push_back({10.0, avoid_fn});
}
```

For `REDUCE_SPEED`, cap the avoidance speed without creating a turn:

```cpp
if (colregs_directive.conflict_active &&
    colregs_directive.direction == ColregsDirection::ReduceSpeed) {
  s_max = std::min(s_max, std::max(0.0, nominal_spd * 0.6));
}
```

- [ ] **Step 5: Replace hard constraints and fallback window**

Replace hard-constraint injection with:

```cpp
if (colregs_directive.conflict_active) {
  const double own_hdg = latest_world_ ? latest_world_->own_ship.heading_deg : 0.0;
  const auto ranges = directive_allowed_ranges(own_hdg, colregs_directive, required_dev_deg);
  constraints.heading_allowed_ranges_deg.insert(
      constraints.heading_allowed_ranges_deg.end(), ranges.begin(), ranges.end());
}
```

In the `sol == nullopt` fallback, replace `starboard_dev_deg` with:

```cpp
const double signed_dev = signed_deviation_deg(colregs_directive, required_dev_deg);
```

Use `std::abs(signed_dev) > 0.0` as the turn condition, use `fallback_anchor_hdg_ + signed_dev`, and set `suggested_action` to:

```cpp
concern.suggested_action =
    colregs_directive.direction == ColregsDirection::Port
        ? "turn_port_absolute"
        : "turn_starboard_absolute";
```

Set the ASDR field name to `colregs_dev_deg` and include the signed value.

- [ ] **Step 6: Run M4 tests**

Run:

```bash
colcon test --packages-select m4_behavior_arbiter --event-handlers console_direct+
colcon test-result --verbose
```

Expected: all M4 tests pass.

- [ ] **Step 7: Commit task**

```bash
git add src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp \
        src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_m4_node_lifecycle.cpp
git commit -m "fix(m4): consume M6 preferred direction in arbitration"
```

---

### Task 4: M5 Explicit COLREGs Fallback Direction

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_geometric_fallback.cpp`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`

- [ ] **Step 1: Write failing helper tests**

Append to `test_geometric_fallback.cpp`:

```cpp
TEST(GeometricFallback, PortDirectionSubtractsMinAlteration) {
  const double route_brg = 0.0;
  const double h_min = -40.0 * M_PI / 180.0;
  const double h_max = 0.0;
  const double min_alt = 25.0 * M_PI / 180.0;

  const double target = fallback_target_heading(
      route_brg, h_min, h_max, min_alt, ColregsPreferredDirection::Port);

  EXPECT_NEAR(target, -25.0 * M_PI / 180.0, 1e-3);
}

TEST(GeometricFallback, ReduceSpeedKeepsRouteBearing) {
  const double route_brg = 10.0 * M_PI / 180.0;
  const double h_min = -40.0 * M_PI / 180.0;
  const double h_max = 40.0 * M_PI / 180.0;

  const double target = fallback_target_heading(
      route_brg, h_min, h_max, 30.0 * M_PI / 180.0,
      ColregsPreferredDirection::ReduceSpeed);

  EXPECT_NEAR(target, route_brg, 1e-3);
}

TEST(GeometricFallback, HoldKeepsRouteBearing) {
  const double route_brg = -5.0 * M_PI / 180.0;
  const double h_min = -40.0 * M_PI / 180.0;
  const double h_max = 40.0 * M_PI / 180.0;

  const double target = fallback_target_heading(
      route_brg, h_min, h_max, 30.0 * M_PI / 180.0,
      ColregsPreferredDirection::Hold);

  EXPECT_NEAR(target, route_brg, 1e-3);
}
```

- [ ] **Step 2: Run M5 helper tests to verify RED**

Run:

```bash
colcon test --packages-select m5_tactical_planner --event-handlers console_direct+ --ctest-args -R test_geometric_fallback
```

Expected: compile fails because `ColregsPreferredDirection` and the 5-argument overload do not exist.

- [ ] **Step 3: Add explicit direction fields and overload**

In `common/types.hpp`, add before `MidMpcInput`:

```cpp
enum class ColregsPreferredDirection : std::uint8_t {
  Hold = 0u,
  Starboard = 1u,
  Port = 2u,
  ReduceSpeed = 3u,
};

inline ColregsPreferredDirection parse_colregs_preferred_direction(const std::string& direction) {
  if (direction == "STARBOARD") {
    return ColregsPreferredDirection::Starboard;
  }
  if (direction == "PORT") {
    return ColregsPreferredDirection::Port;
  }
  if (direction == "REDUCE_SPEED") {
    return ColregsPreferredDirection::ReduceSpeed;
  }
  return ColregsPreferredDirection::Hold;
}
```

Add to `MidMpcInput`:

```cpp
bool colregs_conflict_active{false};
ColregsPreferredDirection colregs_preferred_direction{ColregsPreferredDirection::Hold};
double colregs_min_alteration_rad{0.0};
```

Replace the current `fallback_target_heading` helper with:

```cpp
inline double clamp_heading_window(double target, double h_min, double h_max) {
  return std::min(std::max(target, h_min), h_max);
}

inline double fallback_target_heading(
    double route_brg,
    double h_min,
    double h_max,
    double min_alt_rad,
    ColregsPreferredDirection direction) {
  double target = route_brg;
  if (direction == ColregsPreferredDirection::Starboard) {
    target = route_brg + min_alt_rad;
  } else if (direction == ColregsPreferredDirection::Port) {
    target = route_brg - min_alt_rad;
  }
  return clamp_heading_window(target, h_min, h_max);
}

inline double fallback_target_heading(
    double route_brg, double h_min, double h_max, double min_alt_rad) {
  return fallback_target_heading(
      route_brg, h_min, h_max, min_alt_rad,
      ColregsPreferredDirection::Starboard);
}
```

- [ ] **Step 4: Populate M5 directive from COLREGsConstraint**

In `mid_mpc_node.cpp::assemble_input_()`, after setting `own_ship_psi_rad`, add:

```cpp
inp.colregs_conflict_active =
    colregs_constraint_ != nullptr && colregs_constraint_->conflict_detected;
if (colregs_constraint_ != nullptr) {
  inp.colregs_preferred_direction = mass_l3::m5::parse_colregs_preferred_direction(
      colregs_constraint_->primary_preferred_direction);
  double min_alt_deg = 0.0;
  for (const auto& c : colregs_constraint_->constraints) {
    if (c.constraint_type == "colregs" && c.unit == "deg" && c.numeric_value > 0.0) {
      min_alt_deg = std::max(min_alt_deg, c.numeric_value);
    }
  }
  inp.colregs_min_alteration_rad = min_alt_deg * units::kRadPerDeg;
}
```

Change CPA inflation condition from:

```cpp
if (colregs_constraint_ != nullptr && !colregs_constraint_->active_rules.empty()) {
```

to:

```cpp
if (inp.colregs_conflict_active) {
```

- [ ] **Step 5: Use explicit direction in geometric fallback**

In `build_geometric_fallback_plan_()`, replace:

```cpp
const double min_alt_rad = std::min(std::abs(h_max - route_brg), std::abs(route_brg - h_min));
double target_psi = mass_l3::m5::fallback_target_heading(route_brg, h_min, h_max, min_alt_rad);
```

with:

```cpp
double min_alt_rad = input.colregs_min_alteration_rad;
if (min_alt_rad <= 0.0) {
  min_alt_rad = std::min(std::abs(h_max - route_brg), std::abs(route_brg - h_min));
}
double target_psi = mass_l3::m5::fallback_target_heading(
    route_brg, h_min, h_max, min_alt_rad, input.colregs_preferred_direction);
```

Update the rationale prefix from:

```cpp
plan.rationale = "M5 geometric starboard fallback (" + reason + ")"
```

to:

```cpp
plan.rationale = "M5 geometric COLREG fallback (" + reason + ")"
```

- [ ] **Step 6: Run M5 tests**

Run:

```bash
colcon test --packages-select m5_tactical_planner --event-handlers console_direct+ --ctest-args -R test_geometric_fallback
colcon test-result --verbose
```

Expected: `test_geometric_fallback` passes.

- [ ] **Step 7: Commit task**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_geometric_fallback.cpp
git commit -m "fix(m5): use explicit COLREGs direction in fallback"
```

---

### Task 5: Bridge Regression Guard And Fast Local Verification

**Files:**
- Modify: `tests/docker/test_sil_topic_bridge.py`

- [ ] **Step 1: Add a bridge de-shadow regression**

Append this test near existing M6 conflict tests:

```python
def test_m5_empty_plan_does_not_release_while_m6_conflict_active(monkeypatch):
    """Bridge must not clear active avoidance from an empty M5 plan while M6 owns conflict."""
    import docker.sil_topic_bridge as bridge

    fake_self = types.SimpleNamespace(
        _avoidance_active=True,
        _m6_conflict_active=True,
        _avoidance_plan_last_t=10.0,
        _avoidance_last_plan_t=10.0,
        _avoidance_plan_wps=[object()],
        _last_mission_goal=None,
        _last_threat_state=None,
        _avoidance_target_heading_deg=20.0,
        _last_behavior=1,
        _trace=lambda *args, **kwargs: None,
        get_clock=lambda: types.SimpleNamespace(now=lambda: types.SimpleNamespace(nanoseconds=11_000_000_000)),
        get_logger=lambda: types.SimpleNamespace(info=lambda *a, **k: None, warn=lambda *a, **k: None),
    )

    empty_plan = types.SimpleNamespace(
        waypoints=[],
        speed_adjustments=[],
        confidence=1.0,
        status="NORMAL",
        rationale="M4 TRANSIT - no avoidance required",
    )

    bridge.SilTopicBridge._on_avoidance_plan(fake_self, empty_plan)

    assert fake_self._avoidance_active is True
    assert fake_self._avoidance_plan_wps == [object()] or fake_self._avoidance_plan_wps
```

If `object()` identity makes the final assert brittle in this test file, use a named sentinel:

```python
sentinel = object()
fake_self._avoidance_plan_wps = [sentinel]
...
assert fake_self._avoidance_plan_wps == [sentinel]
```

- [ ] **Step 2: Run test to verify current behavior**

Run:

```bash
pytest tests/docker/test_sil_topic_bridge.py::test_m5_empty_plan_does_not_release_while_m6_conflict_active -q
```

Expected: if current bridge releases on empty plan while M6 conflict is active, the test fails. If current bridge already preserves state, the test passes and becomes a guard.

- [ ] **Step 3: Implement minimal bridge fix only if RED**

If the test fails, update `_on_avoidance_plan()` in `docker/sil_topic_bridge.py` so the empty-plan release branch returns early when `_m6_conflict_active` is true:

```python
if not has_valid_plan and getattr(self, "_m6_conflict_active", False):
    self._trace("bridge.m5_empty_plan_ignored_m6_active", {
        "rationale": getattr(msg, "rationale", ""),
    })
    return
```

Place this before any assignment that clears `_avoidance_active` or `_avoidance_plan_wps`.

- [ ] **Step 4: Run local Python regressions**

Run:

```bash
pytest tests/docker/test_sil_topic_bridge.py tests/sim_workbench/scoring/test_stability_scorer.py -q
```

Expected: all selected Python tests pass.

- [ ] **Step 5: Commit task**

```bash
git add tests/docker/test_sil_topic_bridge.py docker/sil_topic_bridge.py
git commit -m "test(bridge): guard M6 authority during empty M5 plan"
```

If no bridge production code changed, omit `docker/sil_topic_bridge.py` from `git add`.

---

### Task 6: Integrated Verification

**Files:**
- No source changes unless verification exposes a regression in touched files.

- [ ] **Step 1: Run package tests**

Run:

```bash
colcon test --packages-select m6_colregs_reasoner m4_behavior_arbiter m5_tactical_planner --event-handlers console_direct+
colcon test-result --verbose
```

Expected: all selected package tests pass.

- [ ] **Step 2: Run local Python tests**

Run:

```bash
pytest tests/docker/test_sil_topic_bridge.py tests/sim_workbench/scoring/test_stability_scorer.py -q
```

Expected: all selected Python tests pass.

- [ ] **Step 3: Run strict 8-probe on SIL host**

Run the project’s authoritative clean-restart harness for:

```text
scenarios/COLREGs测试/
```

Expected:

- 8/8 strict probes pass.
- `conflict_toggles <= 2` for all scenarios.
- `behavior_toggles <= 2` for all scenarios.
- CPA floors meet each scenario YAML.
- No circling, U-turn, or no-action behavior in trace review.

- [ ] **Step 4: Capture final evidence**

Record final evidence in the handoff log:

```text
handoff/workspace_log.md
```

Use this exact summary shape:

```markdown
### 2026-06-10 — bridge de-shadow strict 8-probe implementation

- Changed: M6 latch past-clear onset guard; M4 direction-aware COLREGs directive; M5 explicit COLREGs fallback direction; Bridge M6-authority regression guard.
- Local tests:
  - `colcon test --packages-select m6_colregs_reasoner m4_behavior_arbiter m5_tactical_planner --event-handlers console_direct+`: PASS
  - `pytest tests/docker/test_sil_topic_bridge.py tests/sim_workbench/scoring/test_stability_scorer.py -q`: PASS
- Strict 8-probe:
  - `<command>`: PASS 8/8
- Remaining staged work: move Bridge controllers into L4 SIL guidance adapter, then delete Bridge tactical lifecycle paths.
```

- [ ] **Step 5: Final commit**

```bash
git add handoff/workspace_log.md
git commit -m "docs(handoff): record bridge de-shadow strict 8-probe evidence"
```

---

## Self-Review

- Spec coverage: P1 M6 authority is covered by Task 1; P2 M4 direction awareness is covered by Tasks 2-3; P3 M5 explicit fallback is covered by Task 4; Bridge transitional guard is covered by Task 5; strict verification is covered by Task 6.
- Scope: P4 L4 adapter and P5/P6 full frontend/Bridge deletion are intentionally excluded from this first implementation batch because the user requested final Bridge removal slowly after behavior is normal.
- Placeholder scan: no bare implementation placeholders are present. Existing calibration markers in source are not introduced by this plan.
- Type consistency: `ColregsDirection` is scoped to M4; `ColregsPreferredDirection` is scoped to M5; helper overload keeps existing M5 tests and call sites compatible.
