# DEMO-1 Closure Implementation Plan — 多场景 COLREGs 避碰与航路回归

> **For agentic workers:** Use superpowers:subagent-driven-development to implement task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 补齐 DEMO-1 (Skeleton Live, 6/15) 全部缺口：本船沿 L2 航路航行 → M6 COLREGs 推理 → M4 IvP 行为仲裁 → M5 BC-MPC 避碰 → 回归航路，支持 YAML 多场景切换。

**Architecture:** 4 条并行开发流（A: M4 C++ / B: Mock Python / C: 集成修复 / D: 文档），A 与 B 完全独立。

**Tech Stack:** C++17 + ROS2 Jazzy + Eigen3 + yaml-cpp + Python 3.10 (rclpy, PyYAML) + maritime-schema v2.0 + GTest + pytest

---

## 并行开发拓扑

```
Stream A: M4 Behavior Arbiter (C++)     Stream B: Mock Publisher (Python)
    |                                        |
    +- A1: Foundation + CMake                +- B1: Package scaffold
    +- A2: Behavior Dictionary               +- B2: ExternalMockPublisher
    +- A3: BehaviorArbiterNode               +- B3: Scenario loader + data
    +- A4: Tests + Launch                    +- B4: Launch + verify
         |                                        |
         +------------------+---------------------+
                            v
                    Stream C: Integration
                    +- C1: Topic remap fix
                    +- C2: Full-stack launch
                    +- C3: E2E integration test
                    +- C4: CI pipeline update

Stream D: Docs (fully independent)
                    +- D1: D1.4 Coding standards
                    +- D2: D1.5 V&V Plan v0.1
```

---

## 前置确认

- [x] `colcon build` 对现有 8 包 pass（D1.1 + D1.2 已关闭）
- [x] 35 条 .msg 与 v1.1.2 对齐
- [x] `archive/sil_v0/l3_external_mock_publisher/` 存在权威 mock 模板
- [x] `archive/sil_v0/sil_mock_publisher/` 存在轨迹计算模板
- [x] `runs/run-19e1fc6b7ae/scenario.yaml` 存在完整场景 YAML 模板
- [x] 已知 bug: M6 publishes `/l3/m6/colregs_constraint` while M5 subscribes `/m6/colregs_constraint`

---

## Stream A: M4 Behavior Arbiter 补全 (C++ ROS2)

### 现有代码基础
- `src/ivp_solver.cpp` — IvPSolver complete (145 lines)
- `src/ivp_combine.cpp` — WeightedSumCombination complete (15 lines)
- `include/.../ivp_solver.hpp` — IvPHardConstraints, IvPSolution, IvPSolver
- `include/.../ivp_combine.hpp` — IvPCombinationStrategy, WeightedFunction
- `test/unit/test_ivp_solver.cpp` — 6 GTest cases
- `README.md` — node topology, class list

### 命名空间与约定
- Namespace: `mass_l3::m4`
- ROS2 header: `COLREGsConstraint.msg` => `#include <l3_msgs/msg/colre_gs_constraint.hpp>`
- Executor: `rclcpp::executors::SingleThreadedExecutor`
- QoS (subs): `rclcpp::QoS(4).reliable().volatile()`
- QoS (pubs): `rclcpp::QoS(4).reliable()`

---

### Task A1: Foundation — IvP Function + Domain + CMake 配置

**Files:**
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/ivp_function.hpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/src/ivp_function.cpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/ivp_domain.hpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/src/ivp_domain.cpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/CMakeLists.txt`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/package.xml`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/test/CMakeLists.txt`

- [ ] **Step 1: Write ivp_function.hpp**

File: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/ivp_function.hpp`

Content:
```cpp
#pragma once
#include <array>
#include <cstddef>
#include <string>

namespace mass_l3::m4 {

struct Piece {
  double x_left;
  double x_right;
  double y_left;
  double y_right;
};

template <size_t Pieces>
class IvPFunction {
public:
  static constexpr size_t kMaxPieces = Pieces;

  void set_pieces(const std::array<Piece, Pieces>& pieces) { pieces_ = pieces; }
  const std::array<Piece, Pieces>& pieces() const { return pieces_; }

  double evaluate(double x) const {
    for (const auto& p : pieces_) {
      if (x >= p.x_left && x <= p.x_right) {
        if (p.x_right == p.x_left) return p.y_left;
        double t = (x - p.x_left) / (p.x_right - p.x_left);
        return p.y_left + t * (p.y_right - p.y_left);
      }
    }
    return 0.0;
  }

  double weight{1.0};
  std::string priority{"normal"};

private:
  std::array<Piece, Pieces> pieces_{};
};

using IvPFunctionDefault = IvPFunction<32>;

}  // namespace mass_l3::m4
```

- [ ] **Step 2: Write ivp_function.cpp**

```cpp
#include "m4_behavior_arbiter/ivp_function.hpp"
namespace mass_l3::m4 {
template class IvPFunction<32>;
}
```

- [ ] **Step 3: Write ivp_domain.hpp**

```cpp
#pragma once
#include <cmath>
#include <cstddef>
#include <vector>

namespace mass_l3::m4 {

class IvPHeadingDomain {
public:
  explicit IvPHeadingDomain(double resolution_deg = 1.0)
    : resolution_deg_(resolution_deg) {
    for (double h = 0.0; h < 360.0; h += resolution_deg_) {
      domain_.emplace_back(h);
    }
  }
  double at(size_t index) const { return domain_.at(index); }
  size_t size() const { return domain_.size(); }
  double resolution_deg() const { return resolution_deg_; }
  static double wrap(double deg) {
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0) deg += 360.0;
    return deg;
  }
private:
  double resolution_deg_;
  std::vector<double> domain_;
};

class IvPSpeedDomain {
public:
  IvPSpeedDomain(double min_kn, double max_kn, double resolution_kn)
    : min_kn_(min_kn), max_kn_(max_kn), resolution_kn_(resolution_kn) {
    for (double s = min_kn; s <= max_kn; s += resolution_kn_) {
      domain_.emplace_back(s);
    }
  }
  double at(size_t index) const { return domain_.at(index); }
  size_t size() const { return domain_.size(); }
  double min_kn() const { return min_kn_; }
  double max_kn() const { return max_kn_; }
  double resolution_kn() const { return resolution_kn_; }
private:
  double min_kn_, max_kn_, resolution_kn_;
  std::vector<double> domain_;
};

}  // namespace mass_l3::m4
```

- [ ] **Step 4: Write ivp_domain.cpp**

```cpp
#include "m4_behavior_arbiter/ivp_domain.hpp"
namespace mass_l3::m4 {}
```

- [ ] **Step 5: Write CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.22)
project(m4_behavior_arbiter)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(builtin_interfaces REQUIRED)

add_library(m4_core STATIC
  src/ivp_solver.cpp src/ivp_combine.cpp
  src/ivp_function.cpp src/ivp_domain.cpp
  src/behavior_dictionary.cpp src/behavior_activation.cpp src/behavior_priority.cpp
)
ament_target_dependencies(m4_core l3_msgs l3_external_msgs Eigen3)
target_include_directories(m4_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

add_library(m4_node_lib STATIC src/behavior_arbiter_node.cpp)
ament_target_dependencies(m4_node_lib rclcpp l3_msgs l3_external_msgs spdlog)
target_include_directories(m4_node_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(m4_node_lib m4_core)

add_executable(m4_behavior_arbiter src/main.cpp)
ament_target_dependencies(m4_behavior_arbiter rclcpp l3_msgs)
target_link_libraries(m4_behavior_arbiter m4_node_lib m4_core)

install(TARGETS m4_behavior_arbiter RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)
install(DIRECTORY launch/ DESTINATION share/${PROJECT_NAME}/launch)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  add_subdirectory(test)
endif()

ament_package()
```

- [ ] **Step 6: Write package.xml**

```xml
<?xml version="1.0"?>
<package format="3">
  <name>m4_behavior_arbiter</name>
  <version>0.1.0</version>
  <description>M4 Behavior Arbiter: IvP multi-objective behavior arbitration for L3 TDL</description>
  <maintainer email="mass-l3@example.com">MASS L3 Team</maintainer>
  <license>Proprietary</license>
  <buildtool_depend>ament_cmake</buildtool_depend>
  <depend>rclcpp</depend>
  <depend>l3_msgs</depend>
  <depend>l3_external_msgs</depend>
  <depend>Eigen3</depend>
  <depend>spdlog</depend>
  <depend>yaml-cpp</depend>
  <depend>builtin_interfaces</depend>
  <test_depend>ament_cmake_gtest</test_depend>
  <export><build_type>ament_cmake</build_type></export>
</package>
```

- [ ] **Step 7: Write test/CMakeLists.txt**

```cmake
ament_add_gtest(test_ivp_solver unit/test_ivp_solver.cpp)
target_link_libraries(test_ivp_solver m4_core)

ament_add_gtest(test_ivp_function unit/test_ivp_function.cpp)
target_link_libraries(test_ivp_function m4_core)

ament_add_gtest(test_ivp_domain unit/test_ivp_domain.cpp)
target_link_libraries(test_ivp_domain m4_core)

ament_add_gtest(test_behavior_dictionary unit/test_behavior_dictionary.cpp)
target_link_libraries(test_behavior_dictionary m4_core)
ament_target_dependencies(test_behavior_dictionary yaml-cpp)

ament_add_gtest(test_behavior_activation unit/test_behavior_activation.cpp)
target_link_libraries(test_behavior_activation m4_core)

ament_add_gtest(test_m4_node_lifecycle integration/test_m4_node_lifecycle.cpp)
target_link_libraries(test_m4_node_lifecycle m4_node_lib m4_core)
ament_target_dependencies(test_m4_node_lifecycle rclcpp l3_msgs l3_external_msgs)
```

- [ ] **Step 8: Build and verify**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select m4_behavior_arbiter
```
Expected: `colcon build` passes, no errors.

- [ ] **Step 9: Write test_ivp_function.cpp**

```cpp
#include <gtest/gtest.h>
#include "m4_behavior_arbiter/ivp_function.hpp"

namespace mass_l3::m4::test {

TEST(IvPFunction, SetAndEvaluate) {
  IvPFunction<2> fn;
  std::array<Piece, 2> pieces = {{
    {0.0, 50.0, 0.0, 100.0},
    {50.0, 100.0, 100.0, 0.0},
  }};
  fn.set_pieces(pieces);
  EXPECT_NEAR(fn.evaluate(0.0), 0.0, 1e-9);
  EXPECT_NEAR(fn.evaluate(50.0), 100.0, 1e-9);
  EXPECT_NEAR(fn.evaluate(100.0), 0.0, 1e-9);
}

TEST(IvPFunction, OutOfRangeReturnsZero) {
  IvPFunction<1> fn;
  std::array<Piece, 1> pieces = {{{0.0, 10.0, 1.0, 1.0}}};
  fn.set_pieces(pieces);
  EXPECT_DOUBLE_EQ(fn.evaluate(20.0), 0.0);
}

TEST(IvPFunction, DefaultWeightAndPriority) {
  IvPFunctionDefault fn;
  EXPECT_DOUBLE_EQ(fn.weight, 1.0);
  EXPECT_EQ(fn.priority, "normal");
}

}  // namespace mass_l3::m4::test
```

- [ ] **Step 10: Run tests**

```bash
colcon test --packages-select m4_behavior_arbiter --event-handlers console_direct+
```
Expected: 9 tests PASS (6 existing solver tests + 3 new function tests).

- [ ] **Step 11: Commit**

```bash
git add src/l3_tdl_kernel/m4_behavior_arbiter/
git commit -m "feat(m4): add IvP Function + Domain + CMake + package scaffold"
```

---

### Task A2: Behavior Dictionary + Types + Errors

**Files:**
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/types.hpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/error.hpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_dictionary.hpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_dictionary.cpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_behavior_dictionary.cpp`

- [ ] **Step 1: Write types.hpp**

```cpp
#pragma once
#include <cstdint>
#include <l3_msgs/msg/behavior_plan.hpp>
#include <l3_msgs/msg/odd_state.hpp>
#include <l3_msgs/msg/world_state.hpp>
#include <l3_msgs/msg/mode_cmd.hpp>
#include <l3_msgs/msg/mission_goal.hpp>
#include <l3_msgs/msg/colre_gs_constraint.hpp>

namespace mass_l3::m4 {

using ODDStateMsg = l3_msgs::msg::ODDState;
using WorldStateMsg = l3_msgs::msg::WorldState;
using ModeCmdMsg = l3_msgs::msg::ModeCmd;
using MissionGoalMsg = l3_msgs::msg::MissionGoal;
using COLREGsConstraintMsg = l3_msgs::msg::COLREGsConstraint;
using BehaviorPlanMsg = l3_msgs::msg::BehaviorPlan;

}  // namespace mass_l3::m4
```

- [ ] **Step 2: Write error.hpp**

```cpp
#pragma once
#include <cstdint>
namespace mass_l3::m4 {
enum class ErrorCode : int32_t {
  kOk = 0,
  kNoActiveBehavior = 4001,
  kIvPInfeasible = 4002,
  kDictionaryLoadFailed = 4003,
  kPriorityConflict = 4004,
};
}  // namespace mass_l3::m4
```

- [ ] **Step 3: Write behavior_dictionary.hpp**

```cpp
#pragma once
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

enum class BehaviorType : uint8_t {
  Transit = 0,
  ColregAvoid = 1,
  DpHold = 2,
  Berth = 3,
  MrcDrift = 4,
  MrcAnchor = 5,
  MrcHeaveTo = 6,
};

struct BehaviorDescriptor {
  BehaviorType type;
  std::string name;
  double priority_weight;
  std::string activation_rule;
  std::string ivp_function_type;
};

class BehaviorDictionary {
public:
  BehaviorDictionary() = default;
  bool load(const std::string& yaml_path);
  std::vector<BehaviorDescriptor> get_active_subset(
      const ODDStateMsg& odd_state, const ModeCmdMsg& mode_cmd) const;
  const std::vector<BehaviorDescriptor>& all_behaviors() const { return behaviors_; }
  const BehaviorDescriptor* find(BehaviorType type) const;

private:
  std::vector<BehaviorDescriptor> behaviors_;
};

}  // namespace mass_l3::m4
```

- [ ] **Step 4: Write behavior_dictionary.cpp**

```cpp
#include "m4_behavior_arbiter/behavior_dictionary.hpp"
#include <spdlog/spdlog.h>

namespace mass_l3::m4 {

bool BehaviorDictionary::load(const std::string& yaml_path) {
  try {
    YAML::Node root = YAML::LoadFile(yaml_path);
    behaviors_.clear();
    for (const auto& node : root["behaviors"]) {
      BehaviorDescriptor b;
      b.type = static_cast<BehaviorType>(node["type"].as<uint8_t>());
      b.name = node["name"].as<std::string>();
      b.priority_weight = node["priority_weight"].as<double>();
      b.activation_rule = node["activation_rule"].as<std::string>();
      b.ivp_function_type = node["ivp_function_type"].as<std::string>();
      behaviors_.push_back(b);
    }
    return true;
  } catch (const YAML::Exception& e) {
    spdlog::error("BehaviorDictionary::load failed: {}", e.what());
    return false;
  }
}

std::vector<BehaviorDescriptor> BehaviorDictionary::get_active_subset(
    const ODDStateMsg& odd_state, const ModeCmdMsg& mode_cmd) const {
  std::vector<BehaviorDescriptor> active;
  // DEMO-1 simplified: always activate Transit + ColregAvoid when COLREGs constraint present
  for (const auto& b : behaviors_) {
    if (b.type == BehaviorType::Transit) {
      active.push_back(b);
    }
  }
  return active;
}

const BehaviorDescriptor* BehaviorDictionary::find(BehaviorType type) const {
  for (const auto& b : behaviors_) {
    if (b.type == type) return &b;
  }
  return nullptr;
}

}  // namespace mass_l3::m4
```

- [ ] **Step 5: Write test_behavior_dictionary.cpp**

```cpp
#include <gtest/gtest.h>
#include <fstream>
#include "m4_behavior_arbiter/behavior_dictionary.hpp"

namespace mass_l3::m4::test {

TEST(BehaviorDictionary, LoadValidYaml) {
  BehaviorDictionary dict;
  bool ok = dict.load(std::string(TEST_CONFIG_DIR) + "/behavior_definitions.yaml");
  EXPECT_TRUE(ok);
  EXPECT_EQ(dict.all_behaviors().size(), 3u);
}

TEST(BehaviorDictionary, FindExistingType) {
  BehaviorDictionary dict;
  dict.load(std::string(TEST_CONFIG_DIR) + "/behavior_definitions.yaml");
  const auto* b = dict.find(BehaviorType::Transit);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name, "Keep Lane / Route Following");
}

TEST(BehaviorDictionary, FindMissingTypeReturnsNull) {
  BehaviorDictionary dict;
  EXPECT_EQ(dict.find(BehaviorType::DpHold), nullptr);
}

}  // namespace mass_l3::m4::test
```

- [ ] **Step 6: Build + test + commit**

```bash
colcon build --packages-select m4_behavior_arbiter
colcon test --packages-select m4_behavior_arbiter --event-handlers console_direct+
git add -A && git commit -m "feat(m4): add BehaviorDictionary + types + error codes"
```

---

### Task A3: Behavior Activation + Priority + ROS2 Node

**Files:**
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_activation.hpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_activation.cpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_priority.hpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_priority.cpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_arbiter_node.hpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/src/main.cpp`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/config/m4_params.yaml`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/config/behavior_definitions.yaml`
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/launch/m4_only.launch.py`

- [ ] **Step 1: Write behavior_activation.hpp**

```cpp
#pragma once
#include <vector>
#include "m4_behavior_arbiter/types.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"

namespace mass_l3::m4 {

struct ArbitrationInputs {
  ODDStateMsg odd_state;
  WorldStateMsg world_state;
  COLREGsConstraintMsg colregs_constraint;
  ModeCmdMsg mode_cmd;
  MissionGoalMsg mission_goal;
};

class BehaviorActivationCondition {
public:
  static std::vector<BehaviorDescriptor> compute_active_set(
      const ArbitrationInputs& inputs,
      const BehaviorDictionary& dictionary);
  static bool is_transit_applicable(const ArbitrationInputs& inputs);
  static bool is_colreg_avoid_applicable(const ArbitrationInputs& inputs);
};

}  // namespace mass_l3::m4
```

- [ ] **Step 2: Write behavior_activation.cpp**

```cpp
#include "m4_behavior_arbiter/behavior_activation.hpp"

namespace mass_l3::m4 {

std::vector<BehaviorDescriptor> BehaviorActivationCondition::compute_active_set(
    const ArbitrationInputs& inputs, const BehaviorDictionary& dictionary) {
  std::vector<BehaviorDescriptor> active;
  if (is_transit_applicable(inputs)) {
    const auto* b = dictionary.find(BehaviorType::Transit);
    if (b) active.push_back(*b);
  }
  if (is_colreg_avoid_applicable(inputs)) {
    const auto* b = dictionary.find(BehaviorType::ColregAvoid);
    if (b) active.push_back(*b);
  }
  return active;
}

bool BehaviorActivationCondition::is_transit_applicable(const ArbitrationInputs& inputs) {
  return inputs.odd_state.envelope_state == l3_msgs::msg::ODDState::ENVELOPE_IN
      || inputs.odd_state.envelope_state == l3_msgs::msg::ODDState::EDGE;
}

bool BehaviorActivationCondition::is_colreg_avoid_applicable(const ArbitrationInputs& inputs) {
  return !inputs.colregs_constraint.active_rules.empty()
      && inputs.colregs_constraint.conflict_detected;
}

}  // namespace mass_l3::m4
```

- [ ] **Step 3: Write behavior_priority.hpp**

```cpp
#pragma once
#include <vector>
#include "m4_behavior_arbiter/behavior_dictionary.hpp"

namespace mass_l3::m4 {

class BehaviorPriority {
public:
  static BehaviorDescriptor select_primary(
      const std::vector<BehaviorDescriptor>& active_set);
  static bool has_mrc(const std::vector<BehaviorDescriptor>& active_set);
};

}  // namespace mass_l3::m4
```

- [ ] **Step 4: Write behavior_priority.cpp**

```cpp
#include "m4_behavior_arbiter/behavior_priority.hpp"
#include <algorithm>

namespace mass_l3::m4 {

BehaviorDescriptor BehaviorPriority::select_primary(
    const std::vector<BehaviorDescriptor>& active_set) {
  return *std::max_element(active_set.begin(), active_set.end(),
      [](const auto& a, const auto& b) { return a.priority_weight < b.priority_weight; });
}

bool BehaviorPriority::has_mrc(const std::vector<BehaviorDescriptor>& active_set) {
  return std::any_of(active_set.begin(), active_set.end(),
      [](const auto& b) {
        return b.type == BehaviorType::MrcDrift
            || b.type == BehaviorType::MrcAnchor
            || b.type == BehaviorType::MrcHeaveTo;
      });
}

}  // namespace mass_l3::m4
```

- [ ] **Step 5: Write behavior_arbiter_node.hpp**

```cpp
#pragma once
#include <rclcpp/rclcpp.hpp>
#include <l3_msgs/msg/behavior_plan.hpp>
#include <l3_msgs/msg/asdr_record.hpp>
#include <chrono>
#include <memory>
#include "m4_behavior_arbiter/types.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"
#include "m4_behavior_arbiter/behavior_activation.hpp"
#include "m4_behavior_arbiter/behavior_priority.hpp"
#include "m4_behavior_arbiter/ivp_solver.hpp"
#include "m4_behavior_arbiter/ivp_function.hpp"
#include "m4_behavior_arbiter/ivp_domain.hpp"
#include "m4_behavior_arbiter/ivp_combine.hpp"

namespace mass_l3::m4 {

class BehaviorArbiterNode : public rclcpp::Node {
public:
  explicit BehaviorArbiterNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

private:
  void on_odd_state(ODDStateMsg::ConstSharedPtr msg);
  void on_world_state(WorldStateMsg::ConstSharedPtr msg);
  void on_mode_cmd(ModeCmdMsg::ConstSharedPtr msg);
  void on_mission_goal(MissionGoalMsg::ConstSharedPtr msg);
  void on_colregs_constraint(COLREGsConstraintMsg::ConstSharedPtr msg);
  void arbitration_timer_callback();

  // Subscribers
  rclcpp::Subscription<ODDStateMsg>::SharedPtr sub_odd_state_;
  rclcpp::Subscription<WorldStateMsg>::SharedPtr sub_world_state_;
  rclcpp::Subscription<ModeCmdMsg>::SharedPtr sub_mode_cmd_;
  rclcpp::Subscription<MissionGoalMsg>::SharedPtr sub_mission_goal_;
  rclcpp::Subscription<COLREGsConstraintMsg>::SharedPtr sub_colregs_constraint_;

  // Publishers
  rclcpp::Publisher<BehaviorPlanMsg>::SharedPtr pub_behavior_plan_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr pub_asdr_;

  // Timer
  rclcpp::TimerBase::SharedPtr arbitration_timer_;

  // Internal state
  ODDStateMsg latest_odd_;
  WorldStateMsg latest_world_;
  ModeCmdMsg latest_mode_;
  MissionGoalMsg latest_mission_;
  COLREGsConstraintMsg latest_colregs_;
  bool odd_received_{false}, world_received_{false}, colregs_received_{false};
  bool mode_received_{false}, mission_received_{false};

  // Components
  BehaviorDictionary dictionary_;
  IvPHeadingDomain heading_domain_;
  IvPSpeedDomain speed_domain_{0.0, 22.0, 0.5};
  std::unique_ptr<IvPCombinationStrategy> combination_strategy_;
};

}  // namespace mass_l3::m4
```

- [ ] **Step 6: Write behavior_arbiter_node.cpp**

```cpp
#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"
#include <spdlog/spdlog.h>

namespace mass_l3::m4 {

BehaviorArbiterNode::BehaviorArbiterNode(const rclcpp::NodeOptions& options)
    : Node("behavior_arbiter", options),
      heading_domain_(1.0),
      combination_strategy_(std::make_unique<WeightedSumCombination>()) {

  auto qos_sub = rclcpp::QoS(4).reliable().volatile();
  auto qos_pub = rclcpp::QoS(4).reliable();

  sub_odd_state_ = create_subscription<ODDStateMsg>(
      "/l3/m1/odd_state", qos_sub,
      [this](ODDStateMsg::ConstSharedPtr m) { on_odd_state(std::move(m)); });
  sub_world_state_ = create_subscription<WorldStateMsg>(
      "/l3/m2/world_state", qos_sub,
      [this](WorldStateMsg::ConstSharedPtr m) { on_world_state(std::move(m)); });
  sub_mode_cmd_ = create_subscription<ModeCmdMsg>(
      "/l3/m1/mode_cmd", qos_sub,
      [this](ModeCmdMsg::ConstSharedPtr m) { on_mode_cmd(std::move(m)); });
  sub_mission_goal_ = create_subscription<MissionGoalMsg>(
      "/l3/m3/mission_goal", qos_sub,
      [this](MissionGoalMsg::ConstSharedPtr m) { on_mission_goal(std::move(m)); });
  sub_colregs_constraint_ = create_subscription<COLREGsConstraintMsg>(
      "/l3/m6/colregs_constraint", qos_sub,
      [this](COLREGsConstraintMsg::ConstSharedPtr m) { on_colregs_constraint(std::move(m)); });

  pub_behavior_plan_ = create_publisher<BehaviorPlanMsg>("/l3/m4/behavior_plan", qos_pub);
  pub_asdr_ = create_publisher<l3_msgs::msg::ASDRRecord>("/l3/asdr/record", qos_pub);

  std::string config_dir;
  declare_parameter<std::string>("config_dir", "");
  get_parameter("config_dir", config_dir);
  if (!config_dir.empty()) {
    dictionary_.load(config_dir + "/behavior_definitions.yaml");
  }

  arbitration_timer_ = create_wall_timer(
      std::chrono::milliseconds(500),
      [this]() { arbitration_timer_callback(); });

  spdlog::info("M4 BehaviorArbiterNode initialized");
}

void BehaviorArbiterNode::on_odd_state(ODDStateMsg::ConstSharedPtr msg) {
  latest_odd_ = *msg; odd_received_ = true;
}
void BehaviorArbiterNode::on_world_state(WorldStateMsg::ConstSharedPtr msg) {
  latest_world_ = *msg; world_received_ = true;
}
void BehaviorArbiterNode::on_mode_cmd(ModeCmdMsg::ConstSharedPtr msg) {
  latest_mode_ = *msg; mode_received_ = true;
}
void BehaviorArbiterNode::on_mission_goal(MissionGoalMsg::ConstSharedPtr msg) {
  latest_mission_ = *msg; mission_received_ = true;
}
void BehaviorArbiterNode::on_colregs_constraint(COLREGsConstraintMsg::ConstSharedPtr msg) {
  latest_colregs_ = *msg; colregs_received_ = true;
}

void BehaviorArbiterNode::arbitration_timer_callback() {
  if (!odd_received_ || !world_received_) return;

  ArbitrationInputs inputs{latest_odd_, latest_world_, latest_colregs_,
                           latest_mode_, latest_mission_};
  auto active = BehaviorActivationCondition::compute_active_set(inputs, dictionary_);

  BehaviorPlanMsg plan;
  plan.schema_version = "v1.1.2";
  plan.stamp = now();
  plan.confidence = 0.9f;

  if (active.empty()) {
    plan.behavior = BehaviorPlanMsg::BEHAVIOR_TRANSIT;
    plan.heading_min_deg = 0.0f;
    plan.heading_max_deg = 360.0f;
    plan.speed_min_kn = 0.0f;
    plan.speed_max_kn = 0.0f;
    plan.rationale = "no active behavior, fallback transit";
    pub_behavior_plan_->publish(plan);
    return;
  }

  auto primary = BehaviorPriority::select_primary(active);
  plan.behavior = static_cast<uint8_t>(primary.type);
  plan.heading_min_deg = 0.0f;
  plan.heading_max_deg = 360.0f;
  plan.speed_min_kn = 0.0f;
  plan.speed_max_kn = static_cast<float>(speed_domain_.max_kn());
  plan.rationale = "primary behavior: " + primary.name;

  pub_behavior_plan_->publish(plan);
}

}  // namespace mass_l3::m4
```

- [ ] **Step 7: Write main.cpp**

```cpp
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<mass_l3::m4::BehaviorArbiterNode>();
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
```

- [ ] **Step 8: Write config/m4_params.yaml**

```yaml
m4:
  arbitration:
    interval_ms: 500
    heading_domain_resolution_deg: 1.0
    speed_domain_min_kn: 0.0
    speed_domain_max_kn: 22.0
    speed_domain_resolution_kn: 0.5
    ivp_timeout_ms: 50
  config_dir: ""
```

- [ ] **Step 9: Write config/behavior_definitions.yaml**

```yaml
behaviors:
  - type: 0
    name: "Keep Lane / Route Following"
    priority_weight: 1.0
    activation_rule: "odd_in_or_edge"
    ivp_function_type: "transit"
  - type: 1
    name: "COLREGs-Compliant Collision Avoidance"
    priority_weight: 10.0
    activation_rule: "colregs_conflict_detected"
    ivp_function_type: "colreg_avoid"
  - type: 2
    name: "Dynamic Positioning Hold"
    priority_weight: 100.0
    activation_rule: "mrc_requested"
    ivp_function_type: "dp_hold"
```

- [ ] **Step 10: Write launch/m4_only.launch.py**

```python
import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    config_dir = os.path.join(
        get_package_share_directory("m4_behavior_arbiter"), "config")
    return LaunchDescription([
        Node(
            package="m4_behavior_arbiter",
            executable="m4_behavior_arbiter",
            name="behavior_arbiter",
            output="screen",
            parameters=[os.path.join(config_dir, "m4_params.yaml"),
                        {"config_dir": config_dir}],
        ),
    ])
```

- [ ] **Step 11: Build + test + commit**

```bash
colcon build --packages-select m4_behavior_arbiter
colcon test --packages-select m4_behavior_arbiter --event-handlers console_direct+
git add -A && git commit -m "feat(m4): add BehaviorArbiterNode + activation + priority + launch"
```

---

### Task A4: M4 Integration Tests + Verify Chain

**Files:**
- Create: `src/l3_tdl_kernel/m4_behavior_arbiter/test/integration/test_m4_node_lifecycle.cpp`
- Create: `tests/integration/test_int_009_m4_arbitration_chain.cpp`

- [ ] **Step 1: Write test_m4_node_lifecycle.cpp**

```cpp
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <l3_msgs/msg/odd_state.hpp>
#include <l3_msgs/msg/world_state.hpp>
#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

TEST(M4NodeLifecycle, ConstructAndSpin) {
  auto node = std::make_shared<mass_l3::m4::BehaviorArbiterNode>(
      rclcpp::NodeOptions().parameter_overrides(
          std::vector<rclcpp::Parameter>{rclcpp::Parameter("use_sim_time", true)}));
  EXPECT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0);

  auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  executor->add_node(node);
  executor->spin_some(std::chrono::milliseconds(100));
}

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 2: Write test_int_009_m4_arbitration_chain.cpp**

```cpp
// INT-009: M1→M4→M5 chain — verify M4 publishes BehaviorPlan when M1 ODD+World+COLREGs present
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <l3_msgs/msg/odd_state.hpp>
#include <l3_msgs/msg/world_state.hpp>
#include <l3_msgs/msg/colre_gs_constraint.hpp>
#include <l3_msgs/msg/behavior_plan.hpp>

TEST(INT009_M4, PublishesBehaviorPlanWhenInputsPresent) {
  auto node = rclcpp::Node::make_shared("test_int_009");
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);

  bool received = false;
  auto sub = node->create_subscription<l3_msgs::msg::BehaviorPlan>(
      "/l3/m4/behavior_plan", 4,
      [&](l3_msgs::msg::BehaviorPlan::ConstSharedPtr) { received = true; });

  auto pub_odd = node->create_publisher<l3_msgs::msg::ODDState>("/l3/m1/odd_state", 4);
  auto pub_world = node->create_publisher<l3_msgs::msg::WorldState>("/l3/m2/world_state", 4);
  auto pub_colregs = node->create_publisher<l3_msgs::msg::COLREGsConstraint>("/l3/m6/colregs_constraint", 4);

  l3_msgs::msg::ODDState odd;
  odd.envelope_state = l3_msgs::msg::ODDState::ENVELOPE_IN;
  odd.auto_level = l3_msgs::msg::ODDState::AUTO_LEVEL_D3;
  pub_odd->publish(odd);

  l3_msgs::msg::WorldState world;
  pub_world->publish(world);

  l3_msgs::msg::COLREGsConstraint colregs;
  colregs.conflict_detected = true;
  l3_msgs::msg::RuleActive rule;
  rule.rule_id = 14;
  colregs.active_rules.push_back(rule);
  pub_colregs->publish(colregs);

  exec.spin_some(std::chrono::milliseconds(2000));
  EXPECT_TRUE(received) << "M4 should publish BehaviorPlan within 2s";
}

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

- [ ] **Step 3: Build + test + commit**

```bash
colcon build
colcon test --packages-select m4_behavior_arbiter --event-handlers console_direct+
git add -A && git commit -m "test(m4): add integration tests for M4 arbitration chain"
```

---

## Stream B: External Mock Publisher + 多场景支持 (Python)

### 关键约定
- Package path: `src/l3_tdl_kernel/l3_external_mock_publisher/`
- 权威模板: `archive/sil_v0/l3_external_mock_publisher/`
- 话题命名: 严格遵循 v1.1.2 §15.2
- Python 版本: 3.10 (ROS2 Jazzy 默认)
- 构建类型: `ament_python`

---

### Task B1: Package Scaffold

**Files:**
- Create: `src/l3_tdl_kernel/l3_external_mock_publisher/package.xml`
- Create: `src/l3_tdl_kernel/l3_external_mock_publisher/setup.py`
- Create: `src/l3_tdl_kernel/l3_external_mock_publisher/resource/l3_external_mock_publisher`
- Create: `src/l3_tdl_kernel/l3_external_mock_publisher/l3_external_mock_publisher/__init__.py`
- Create: `src/l3_tdl_kernel/l3_external_mock_publisher/launch/external_mock.launch.py`

- [ ] **Step 1: Write package.xml**

```xml
<?xml version="1.0"?>
<package format="3">
  <name>l3_external_mock_publisher</name>
  <version>0.1.0</version>
  <description>Mock publishers for all external (cross-layer) ROS2 topics consumed by L3 TDL</description>
  <maintainer email="mass-l3@example.com">MASS L3 Team</maintainer>
  <license>Proprietary</license>
  <exec_depend>rclpy</exec_depend>
  <exec_depend>l3_msgs</exec_depend>
  <exec_depend>l3_external_msgs</exec_depend>
  <exec_depend>builtin_interfaces</exec_depend>
  <exec_depend>geographic_msgs</exec_depend>
  <exec_depend>python3-pyyaml</exec_depend>
  <export><build_type>ament_python</build_type></export>
</package>
```

- [ ] **Step 2: Write setup.py**

```python
from setuptools import find_packages, setup

package_name = "l3_external_mock_publisher"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages",
         ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
        ("share/" + package_name + "/launch", ["launch/external_mock.launch.py"]),
    ],
    install_requires=["setuptools", "PyYAML>=6.0"],
    entry_points={
        "console_scripts": [
            "external_mock_publisher = l3_external_mock_publisher.external_mock_publisher:main",
        ],
    },
)
```

- [ ] **Step 3: Write resource marker file**

```bash
touch src/l3_tdl_kernel/l3_external_mock_publisher/resource/l3_external_mock_publisher
```

- [ ] **Step 4: Write __init__.py**

```python
"""l3_external_mock_publisher — mock all external ROS2 topics for L3 TDL."""
```

- [ ] **Step 5: Write launch/external_mock.launch.py**

```python
import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(
            package="l3_external_mock_publisher",
            executable="external_mock_publisher",
            name="l3_external_mock_publisher",
            output="screen",
            parameters=[{"scenario_path": os.path.join(
                os.path.dirname(__file__), "../../../../../scenarios/colregs/colreg-rule14-ho-001.yaml"
            )}],
        ),
    ])
```

- [ ] **Step 6: Build verify**

```bash
colcon build --packages-select l3_external_mock_publisher
```
Expected: `colcon build` passes.

- [ ] **Step 7: Commit**

```bash
git add src/l3_tdl_kernel/l3_external_mock_publisher/
git commit -m "feat(mock): add l3_external_mock_publisher package scaffold"
```

---

### Task B2: ExternalMockPublisher Node

**Files:**
- Create: `src/l3_tdl_kernel/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py`

- [ ] **Step 1: Write external_mock_publisher.py**

Based on `archive/sil_v0/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py` (295 lines) — copy the complete file and add scenario loading support. Key structure:

```python
#!/usr/bin/env python3
"""External mock publisher for L3 TDL — publishes all cross-layer topics."""

import math
import time
import rclpy
from rclpy.node import Node
from builtin_interfaces.msg import Time as RosTime
from geographic_msgs.msg import GeoPoint, GeoPath

from l3_msgs.msg import TrackedTarget, EncounterClassification, OwnShipState, ZoneConstraint
from l3_msgs.msg import ODDState, SATData, ASDRRecord, WorldState
from l3_external_msgs.msg import (
    TrackedTargetArray, FilteredOwnShipState, EnvironmentState,
    PlannedRoute, SpeedProfile, VoyageTask, ReplanResponse,
    CheckerVetoNotification, ReflexActivationNotification,
    OverrideActiveSignal, EmergencyCommand,
)

class ExternalMockPublisher(Node):
    def __init__(self):
        super().__init__("l3_external_mock_publisher")
        self._counter = 0
        self._start_time = time.time()

        # --- Create publishers (11 total) ---
        qos_50hz = rclpy.qos.QoSProfile(depth=10, reliability=rclpy.qos.ReliabilityPolicy.BEST_EFFORT)
        qos_2hz = rclpy.qos.QoSProfile(depth=10, reliability=rclpy.qos.ReliabilityPolicy.RELIABLE)
        qos_event = rclpy.qos.QoSProfile(depth=10, reliability=rclpy.qos.ReliabilityPolicy.RELIABLE)

        self._pub_own_ship = self.create_publisher(FilteredOwnShipState, "/fusion/own_ship_state", qos_50hz)
        self._pub_tracks = self.create_publisher(TrackedTargetArray, "/fusion/tracked_targets", qos_2hz)
        self._pub_env = self.create_publisher(EnvironmentState, "/fusion/environment_state", qos_2hz)
        self._pub_route = self.create_publisher(PlannedRoute, "/l2/planned_route", qos_2hz)
        self._pub_speed = self.create_publisher(SpeedProfile, "/l2/speed_profile", qos_2hz)
        self._pub_voyage = self.create_publisher(VoyageTask, "/l1/voyage_task", qos_event)
        self._pub_replan = self.create_publisher(ReplanResponse, "/l2/replan_response", qos_event)
        self._pub_veto = self.create_publisher(CheckerVetoNotification, "/checker/veto_notification", qos_event)
        self._pub_reflex_act = self.create_publisher(ReflexActivationNotification, "/reflex/activation_notification", qos_event)
        self._pub_override = self.create_publisher(OverrideActiveSignal, "/override/active_signal", qos_event)
        self._pub_emergency = self.create_publisher(EmergencyCommand, "/reflex/emergency_command", qos_event)

        # --- Create timers ---
        self.create_timer(0.02, self._publish_50hz_callback)
        self.create_timer(0.5, self._publish_2hz_callback)
        self.create_timer(1.0, self._publish_1hz_callback)
        self.create_timer(5.0, self._publish_5s_callback)
        self.create_timer(30.0, self._publish_30s_callback)

        self._scenario_targets = []

    def load_scenario(self, scenario_path: str):
        """Load maritime-schema v2.0 YAML scenario and extract target ship data."""
        import yaml
        with open(scenario_path, "r") as f:
            data = yaml.safe_load(f)
        self._own_ship_init = data.get("own_ship", {}).get("initial", {})
        self._scenario_targets = data.get("target_ships", [])

    def _now(self) -> RosTime:
        return self.get_clock().now().to_msg()

    def _publish_50hz_callback(self):
        stamp = self._now()
        self._publish_own_ship_state(stamp)

    def _publish_2hz_callback(self):
        stamp = self._now()
        self._publish_tracked_targets(stamp)
        self._publish_environment(stamp)
        self._publish_planned_route(stamp)
        self._publish_speed_profile(stamp)

    def _publish_1hz_callback(self):
        stamp = self._now()
        self._publish_voyage_task(stamp)

    def _publish_5s_callback(self):
        stamp = self._now()
        self._publish_replan_response(stamp)

    def _publish_30s_callback(self):
        stamp = self._now()
        self._publish_veto_notification(stamp)
        self._publish_reflex_activation(stamp)
        self._publish_override_signal(stamp)
        self._publish_emergency_command(stamp)

    def _publish_own_ship_state(self, stamp: RosTime):
        msg = FilteredOwnShipState()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        init = self._own_ship_init
        if init:
            msg.position.latitude = init.get("position", {}).get("latitude", 63.435)
            msg.position.longitude = init.get("position", {}).get("longitude", 10.395)
            msg.cog_deg = init.get("cog", 0.0)
            msg.sog_kn = init.get("sog", 18.0)
            msg.heading_deg = init.get("heading", 0.0)
        else:
            msg.position.latitude = 63.435
            msg.position.longitude = 10.395
            msg.cog_deg = 0.0
            msg.sog_kn = 18.0
            msg.heading_deg = 0.0
        msg.nav_mode = "autonomous_d3"
        msg.confidence = 0.95
        msg.rationale = "FCB own-ship state from simulator/scenario"
        self._pub_own_ship.publish(msg)

    def _publish_tracked_targets(self, stamp: RosTime):
        msg = TrackedTargetArray()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.confidence = 0.9
        msg.rationale = f"{len(self._scenario_targets)} target(s) from scenario"
        for ts in self._scenario_targets:
            target = TrackedTarget()
            init = ts.get("initial", {})
            pos = init.get("position", {})
            target.target_id = ts.get("mmsi", 100000000)
            target.position.latitude = pos.get("latitude", 0.0)
            target.position.longitude = pos.get("longitude", 0.0)
            target.sog_kn = init.get("sog", 10.0)
            target.cog_deg = init.get("cog", 180.0)
            target.heading_deg = init.get("heading", 180.0)
            target.classification = "cargo"
            target.classification_confidence = 0.85
            target.source_sensor = "radar_ais_fusion"
            target.cpa_m = 500.0
            target.tcpa_s = 300.0
            target.encounter.encounter_type = EncounterClassification.HEAD_ON
            target.confidence = 0.9
            msg.targets.append(target)
        self._pub_tracks.publish(msg)

    def _publish_environment(self, stamp: RosTime):
        msg = EnvironmentState()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.wind_speed_kn = 5.0
        msg.visibility_range_nm = 10.0
        msg.confidence = 0.8
        msg.rationale = "clear weather, moderate wind"
        self._pub_env.publish(msg)

    def _publish_planned_route(self, stamp: RosTime):
        msg = PlannedRoute()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.route_id = 1
        # Route: own_ship position heading north for ~10nm
        wp_n = GeoPoint(latitude=63.526, longitude=10.395)
        wp_s = GeoPoint(latitude=63.435, longitude=10.395)
        msg.route = GeoPath(poses=[], points=[wp_s, wp_n])
        msg.total_distance_nm = 10.0
        msg.confidence = 0.95
        msg.rationale = "R14 head-on route: south-to-north"
        self._pub_route.publish(msg)

    def _publish_speed_profile(self, stamp: RosTime):
        msg = SpeedProfile()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.profile_id = 1
        msg.segment_start_distances_m = [0.0]
        msg.segment_end_distances_m = [18520.0]
        msg.target_speeds_kn = [18.0]
        msg.confidence = 0.95
        msg.rationale = "constant 18kn speed profile"
        self._pub_speed.publish(msg)

    def _publish_voyage_task(self, stamp: RosTime):
        msg = VoyageTask()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.task_id = self._counter
        msg.departure = GeoPoint(latitude=63.435, longitude=10.395)
        msg.destination = GeoPoint(latitude=63.526, longitude=10.395)
        msg.confidence = 0.95
        msg.rationale = "R14 test voyage"
        self._pub_voyage.publish(msg)
        self._counter += 1

    def _publish_replan_response(self, stamp: RosTime):
        msg = ReplanResponse()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.status = 0  # ACCEPTED
        msg.confidence = 0.9
        msg.rationale = "mock replan accepted"
        self._pub_replan.publish(msg)

    def _publish_veto_notification(self, stamp: RosTime):
        msg = CheckerVetoNotification()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.active = False
        msg.rationale = "no veto active"
        self._pub_veto.publish(msg)

    def _publish_reflex_activation(self, stamp: RosTime):
        msg = ReflexActivationNotification()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.active = False
        msg.rationale = "reflex arc inactive"
        self._pub_reflex_act.publish(msg)

    def _publish_override_signal(self, stamp: RosTime):
        msg = OverrideActiveSignal()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.active = False
        msg.rationale = "no hardware override"
        self._pub_override.publish(msg)

    def _publish_emergency_command(self, stamp: RosTime):
        msg = EmergencyCommand()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.command = 0  # NONE
        msg.rationale = "no emergency"
        self._pub_emergency.publish(msg)

def main(args=None):
    rclpy.init(args=args)
    node = ExternalMockPublisher()
    # Load scenario from param
    node.declare_parameter("scenario_path", "")
    scenario_path = node.get_parameter("scenario_path").get_parameter_value().string_value
    if scenario_path:
        node.load_scenario(scenario_path)
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Build + verify import**

```bash
colcon build --packages-select l3_external_mock_publisher
source install/setup.bash
python3 -c "from l3_external_mock_publisher.external_mock_publisher import ExternalMockPublisher; print('OK')"
```
Expected: `OK` printed, no errors.

- [ ] **Step 3: Commit**

```bash
git add src/l3_tdl_kernel/l3_external_mock_publisher/
git commit -m "feat(mock): implement ExternalMockPublisher with 11 topic publishers + YAML scenario loader"
```

---

### Task B3: Multi-Scenario Data Files

**Files:**
- Create: `scenarios/colregs/colreg-rule14-ho-001.yaml` (R14 head-on)
- Create: `scenarios/colregs/colreg-rule15-cross-001.yaml` (R15 crossing)
- Create: `scenarios/colregs/colreg-rule13-ot-001.yaml` (R13 overtaking)

- [ ] **Step 1: Create R14 Head-On scenario**

Based on `runs/run-19e1fc6b7ae/scenario.yaml` template:

```yaml
title: "Head-On Encounter — DEMO-1 R14"
description: "FCB own-ship heading 0° at 18kn, target ship heading 180° at 10kn, Trondheim Fjord"
start_time: "2026-01-01T00:00:00Z"
own_ship:
  id: "os"
  nav_status: 0
  mmsi: 123456789
  initial:
    position:
      latitude: 63.435
      longitude: 10.395
    cog: 0.0
    sog: 18.0
    heading: 0.0
target_ships:
  - id: "ts1"
    nav_status: 0
    mmsi: 100000001
    initial:
      position:
        latitude: 63.480
        longitude: 10.395
      cog: 180.0
      sog: 10.0
      heading: 180.0
metadata:
  schema_version: "2.0"
  scenario_id: "demo1-r14-ho-v1.0"
  scenario_source: "demo1"
  vessel_class: "FCB"
  odd_zone: "A"
  geo_origin:
    latitude: 63.435
    longitude: 10.395
  encounter:
    rule: "Rule14"
    give_way_vessel: "both"
    expected_own_action: "turn_starboard"
  simulation:
    duration_s: 600
    dt_s: 0.02
  prng_seed: 42
```

- [ ] **Step 2: Create R15 Crossing scenario**

```yaml
title: "Crossing Encounter — DEMO-1 R15"
description: "FCB own-ship heading 0° at 18kn, target ship crossing from starboard"
start_time: "2026-01-01T00:00:00Z"
own_ship:
  id: "os"
  nav_status: 0
  mmsi: 123456789
  initial:
    position:
      latitude: 63.435
      longitude: 10.395
    cog: 0.0
    sog: 18.0
    heading: 0.0
target_ships:
  - id: "ts1"
    nav_status: 0
    mmsi: 100000001
    initial:
      position:
        latitude: 63.460
        longitude: 10.420
      cog: 270.0
      sog: 12.0
      heading: 270.0
metadata:
  schema_version: "2.0"
  scenario_id: "demo1-r15-cross-v1.0"
  scenario_source: "demo1"
  vessel_class: "FCB"
  odd_zone: "A"
  geo_origin:
    latitude: 63.435
    longitude: 10.395
  encounter:
    rule: "Rule15"
    give_way_vessel: "own"
    expected_own_action: "turn_starboard"
  simulation:
    duration_s: 600
    dt_s: 0.02
  prng_seed: 42
```

- [ ] **Step 3: Create R13 Overtaking scenario**

```yaml
title: "Overtaking Encounter — DEMO-1 R13"
description: "FCB own-ship heading 0° at 18kn overtaking slower target ahead"
start_time: "2026-01-01T00:00:00Z"
own_ship:
  id: "os"
  nav_status: 0
  mmsi: 123456789
  initial:
    position:
      latitude: 63.435
      longitude: 10.395
    cog: 0.0
    sog: 18.0
    heading: 0.0
target_ships:
  - id: "ts1"
    nav_status: 0
    mmsi: 100000001
    initial:
      position:
        latitude: 63.450
        longitude: 10.395
      cog: 0.0
      sog: 8.0
      heading: 0.0
metadata:
  schema_version: "2.0"
  scenario_id: "demo1-r13-ot-v1.0"
  scenario_source: "demo1"
  vessel_class: "FCB"
  odd_zone: "A"
  geo_origin:
    latitude: 63.435
    longitude: 10.395
  encounter:
    rule: "Rule13"
    give_way_vessel: "own"
    expected_own_action: "turn_port_or_starboard"
  simulation:
    duration_s: 600
    dt_s: 0.02
  prng_seed: 42
```

- [ ] **Step 4: Commit**

```bash
git add scenarios/colregs/
git commit -m "feat(scenarios): add DEMO-1 R13/R14/R15 YAML scenario files"
```

---

### Task B4: Mock Publisher Launch + Verify

- [ ] **Step 1: Build and verify publish**

```bash
colcon build --packages-select l3_external_mock_publisher
source install/setup.bash
# In terminal 1: launch mock
ros2 launch l3_external_mock_publisher external_mock.launch.py &
sleep 2
# In terminal 2: verify topics
ros2 topic list | grep -E "fusion|l1|l2|checker|reflex|override"
```
Expected: 11 topics visible.

- [ ] **Step 2: Verify topic frequency**

```bash
ros2 topic hz /fusion/own_ship_state --window 100
```
Expected: ~50 Hz.

- [ ] **Step 3: Commit scenario launch update**

```bash
git add -A && git commit -m "feat(mock): verify external mock publisher topics + frequencies"
```

---

## Stream C: Integration Fixes + Full-Stack Wiring

### Task C1: Fix M6 → M5 Topic Remapping

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/launch/m5_standalone.launch.py` (or create if missing)

- [ ] **Step 1: Create or modify M5 launch to add remap**

Find or create the launch file. If it doesn't exist, create it:

```python
import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    config_dir = os.path.join(
        get_package_share_directory("m5_tactical_planner"), "config")
    return LaunchDescription([
        Node(
            package="m5_tactical_planner",
            executable="mid_mpc_node",
            name="mid_mpc",
            output="screen",
            remappings=[("/m6/colregs_constraint", "/l3/m6/colregs_constraint")],
            parameters=[os.path.join(config_dir, "m5_params.yaml")],
        ),
    ])
```

- [ ] **Step 2: Verify topic match**

```bash
colcon build
source install/setup.bash
# Launch M6 and M5, verify ros2 topic info /l3/m6/colregs_constraint shows M5 as subscriber
```

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "fix(m5): add M6->M5 colregs_constraint topic remapping"
```

---

### Task C2: Full-Stack Launch File

**Files:**
- Create: `tools/ci/full_l3_stack_demo1.launch.py`

- [ ] **Step 1: Write full-stack launch for DEMO-1**

```python
"""full_l3_stack_demo1.launch.py — DEMO-1 full pipeline launch"""
import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, TimerAction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

def generate_launch_description():
    scenario_path = LaunchConfiguration("scenario_path", default="")

    external_mock = Node(
        package="l3_external_mock_publisher",
        executable="external_mock_publisher",
        name="l3_external_mock_publisher",
        output="screen",
        parameters=[{"scenario_path": scenario_path}],
    )

    m1_node = Node(package="m1_odd_envelope_manager", executable="m1_odd_envelope_manager",
                   name="m1_odd", output="screen")
    m2_node = Node(package="m2_world_model", executable="m2_world_model",
                   name="m2_world", output="screen")
    m3_node = Node(package="m3_mission_manager", executable="m3_mission_manager",
                   name="m3_mission", output="screen")
    m4_node = TimerAction(period=1.0, actions=[
        Node(package="m4_behavior_arbiter", executable="m4_behavior_arbiter",
             name="m4_arbiter", output="screen")])
    m5_node = TimerAction(period=2.0, actions=[
        Node(package="m5_tactical_planner", executable="mid_mpc_node",
             name="m5_mid_mpc", output="screen",
             remappings=[("/m6/colregs_constraint", "/l3/m6/colregs_constraint")])])
    m6_node = TimerAction(period=1.5, actions=[
        Node(package="m6_colregs_reasoner", executable="m6_colregs_reasoner",
             name="m6_colregs", output="screen")])
    m7_node = TimerAction(period=3.0, actions=[
        Node(package="m7_safety_supervisor", executable="m7_safety_supervisor",
             name="m7_safety", output="screen")])

    return LaunchDescription([
        DeclareLaunchArgument("scenario_path", default_value="",
                              description="Path to scenario YAML file"),
        external_mock,
        m1_node, m2_node, m3_node, m4_node, m5_node, m6_node, m7_node,
    ])
```

- [ ] **Step 2: Commit**

```bash
git add tools/ci/full_l3_stack_demo1.launch.py
git commit -m "feat(ci): add full L3 stack launch file for DEMO-1"
```

---

### Task C3: End-to-End Integration Test

**Files:**
- Create: `tests/e2e/test_demo1_head_on_full_chain.py`

- [ ] **Step 1: Write E2E test**

```python
#!/usr/bin/env python3
"""E2E test: DEMO-1 R14 head-on full chain — external mock → M1→M2→M3→M6→M4→M5."""
import pytest
import rclpy
from rclpy.node import Node
from l3_msgs.msg import BehaviorPlan, AvoidancePlan, WorldState, COLREGsConstraint

class Demo1E2ETester(Node):
    def __init__(self):
        super().__init__("demo1_e2e_tester")
        self.behavior_received = False
        self.avoidance_received = False
        self.world_received = False
        self.colregs_received = False
        self.create_subscription(BehaviorPlan, "/l3/m4/behavior_plan",
            lambda m: setattr(self, "behavior_received", True), 4)
        self.create_subscription(AvoidancePlan, "/l3/m5/avoidance_plan",
            lambda m: setattr(self, "avoidance_received", True), 4)
        self.create_subscription(WorldState, "/l3/m2/world_state",
            lambda m: setattr(self, "world_received", True), 4)
        self.create_subscription(COLREGsConstraint, "/l3/m6/colregs_constraint",
            lambda m: setattr(self, "colregs_received", True), 4)

@pytest.mark.e2e
def test_demo1_full_chain():
    rclpy.init()
    tester = Demo1E2ETester()
    executor = rclpy.executors.SingleThreadedExecutor()
    executor.add_node(tester)
    # Spin for 10 seconds to allow full chain to activate
    for _ in range(100):
        executor.spin_once(timeout_sec=0.1)
    rclpy.shutdown()
    assert tester.behavior_received, "M4 BehaviorPlan not received"
    assert tester.avoidance_received, "M5 AvoidancePlan not received"
    assert tester.world_received, "M2 WorldState not received"
    assert tester.colregs_received, "M6 COLREGsConstraint not received"
```

- [ ] **Step 2: Commit**

```bash
git add tests/e2e/test_demo1_head_on_full_chain.py
git commit -m "test(e2e): add DEMO-1 R14 head-on full chain E2E test"
```

---

### Task C4: CI Pipeline Update

**Files:**
- Modify: `.gitlab-ci.yml` (add m4 + mock build stages)

- [ ] **Step 1: Add m4 and mock to CI build matrix**

In `.gitlab-ci.yml`, add entries for m4_behavior_arbiter and l3_external_mock_publisher packages in the build, unit-test, and integration-test stages following the existing pattern.

- [ ] **Step 2: Commit**

```bash
git add .gitlab-ci.yml
git commit -m "ci: add m4 + mock publisher to CI pipeline"
```

---

## Stream D: Documentation

### Task D1: D1.4 Coding Standards Document

**Files:**
- Create: `docs/Implementation/coding-standards.md`

Write a concise document covering:
- ROS2 node conventions (namespace `mass_l3::mX`, executor pattern)
- Message publishing patterns (schema_version, stamp, confidence, rationale)
- Test conventions (GTest for C++, pytest for Python)
- MISRA C++:2023 PATH-D / PATH-S allocation
- Naming conventions (snake_case for files, CamelCase for classes)

- [ ] **Step 1: Write + commit**

```bash
git add docs/Implementation/coding-standards.md
git commit -m "docs: add D1.4 coding standards document"
```

---

### Task D2: D1.5 V&V Plan v0.1

**Files:**
- Create: `docs/Design/V&V_Plan/00-vv-strategy-v0.1.md`

Write a structured document covering:
- SIL → HIL → sea trial entry/exit gates
- End-to-end KPI matrix (AvoidancePlan P95 ≤ 1.0s, etc.)
- Functional × performance × failure-response coverage
- SIL latency budget
- RL artefact rebound path
- DNV toolchain entry conditions

- [ ] **Step 1: Write + commit**

```bash
git add docs/Design/V&V_Plan/00-vv-strategy-v0.1.md
git commit -m "docs: add D1.5 V&V Plan v0.1"
```

---

## DEMO-1 验证清单

完成所有 Stream A-D 任务后，执行以下验证：

```bash
# 1. Full build
colcon build
# Expected: ALL packages pass

# 2. Full test
colcon test --event-handlers console_direct+
# Expected: ALL tests pass (M1-M8 unit + integration + E2E)

# 3. Launch full stack
source install/setup.bash
ros2 launch full_l3_stack_demo1 full_l3_stack_demo1.launch.py \
    scenario_path:=scenarios/colregs/colreg-rule14-ho-001.yaml

# 4. Verify data flow
ros2 topic list | wc -l         # > 25 topics
ros2 topic hz /l3/m4/behavior_plan   # ~2 Hz
ros2 topic hz /l3/m5/avoidance_plan  # ~1 Hz

# 5. Verify collision avoidance chain
ros2 topic echo /l3/m6/colregs_constraint --once  # conflict_detected=true
ros2 topic echo /l3/m4/behavior_plan --once       # behavior=1 (ColregAvoid)
ros2 topic echo /l3/m5/avoidance_plan --once      # waypoints with avoidance maneuver

# 6. Run DEMO-1 E2E test
pytest tests/e2e/test_demo1_head_on_full_chain.py -v
# Expected: 4 assertions PASS

# 7. Switch scenarios
ros2 launch full_l3_stack_demo1 full_l3_stack_demo1.launch.py \
    scenario_path:=scenarios/colregs/colreg-rule15-cross-001.yaml
# Expected: chain works with different scenario (crossing instead of head-on)
```

---

## 并行执行建议

```
Worktree 1: Stream A (M4 C++)          → feat/demo1-stream-a-m4
Worktree 2: Stream B (Mock Python)     → feat/demo1-stream-b-mock
Worktree 3: Stream D (Docs)            → feat/demo1-stream-d-docs

After A+B complete:
Worktree 1: Stream C (Integration)     → feat/demo1-stream-c-integration
```
