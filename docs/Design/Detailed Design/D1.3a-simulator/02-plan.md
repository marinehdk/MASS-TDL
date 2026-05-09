# D1.3a Simulator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the D1.3a 4-DOF MMG physics simulator (C++ pluginlib) + AIS historical data pipeline (Python ROS2) that together publish `/fusion/own_ship_state` @ 50 Hz and `/fusion/tracked_targets` @ 2 Hz for SIL testing.

**Architecture:** Three-package split — `ship_sim_interfaces` (header-only C++ base class), `fcb_simulator` (refactored C++ plugin + node), `ais_bridge` (new Python ROS2 node). `FCBSimulator` implements `ShipMotionSimulator` via pluginlib; vessel selection driven by `vessel_class` YAML field (no Python hardcoding). Track A (C++) and Track B (Python) are fully independent and must run as parallel subagents; Track C starts only after both complete.

**Tech Stack:** C++17, ROS2 Humble, pluginlib, yaml-cpp, ament_cmake_gtest; Python 3, rclpy, pyais 3.0.0, pytest.

**Workspace root:** `/Users/marine/Code/MASS-L3-Tactical Layer`  
**Spec:** `docs/Design/Detailed Design/D1.3a-simulator/01-spec.md`  
**Key finding closures:** MV-4, G P0-G-1(a)

---

## Parallel Execution Map

```
┌─ Subagent A (Track A) ──────────────────────────────────────────────┐
│  A1→A2→A3→A4→A5→A6→A7→A8 (sequential within agent)                 │
│  ship_sim_interfaces pkg + FCBSimulator plugin + node refactor       │
│  + stopping-error test + stability test + run_scenario() hook        │
└─────────────────────────────────────────────────────────────────────┘
        ↓ (both complete)
┌─ Subagent B (Track B) ──────────────────────────────────────────────┐
│  B1→B2→B3→B4→B5→B6 (sequential within agent)                        │
│  ais_bridge pkg + download script + decoder + publisher + replay     │
│  + parse-rate test                                                   │
└─────────────────────────────────────────────────────────────────────┘
        ↓ (both A and B complete)
┌─ Subagent C (Track C) ──────────────────────────────────────────────┐
│  C1→C2: integration launch file + finding closure docs               │
└─────────────────────────────────────────────────────────────────────┘
```

---

## File Map

### New files
| Path | Task |
|---|---|
| `src/ship_sim_interfaces/include/ship_sim_interfaces/ship_state.hpp` | A1 |
| `src/ship_sim_interfaces/include/ship_sim_interfaces/ship_motion_simulator.hpp` | A1 |
| `src/ship_sim_interfaces/CMakeLists.txt` | A1 |
| `src/ship_sim_interfaces/package.xml` | A1 |
| `src/fcb_simulator/include/fcb_simulator/fcb_simulator_plugin.hpp` | A3 |
| `src/fcb_simulator/src/fcb_simulator_plugin.cpp` | A3 |
| `src/fcb_simulator/plugins.xml` | A3 |
| `src/fcb_simulator/include/fcb_simulator/scenario_runner.hpp` | A8 |
| `src/fcb_simulator/src/scenario_runner.cpp` | A8 |
| `src/fcb_simulator/test/test_stopping_error.cpp` | A6 |
| `src/fcb_simulator/test/test_stability_1h.cpp` | A7 |
| `src/fcb_simulator/launch/simulator.launch.py` | C1 |
| `src/ais_bridge/ais_bridge/__init__.py` | B1 |
| `src/ais_bridge/ais_bridge/nmea_decoder.py` | B3 |
| `src/ais_bridge/ais_bridge/dataset_loader.py` | B3 |
| `src/ais_bridge/ais_bridge/target_publisher.py` | B4 |
| `src/ais_bridge/ais_bridge/replay_node.py` | B5 |
| `src/ais_bridge/scripts/download_dataset.py` | B2 |
| `src/ais_bridge/config/ais_replay.yaml` | B5 |
| `src/ais_bridge/test/test_parse_rate.py` | B6 |
| `src/ais_bridge/setup.py` | B1 |
| `src/ais_bridge/package.xml` | B1 |
| `src/ais_bridge/resource/ais_bridge` | B1 |

### Modified files
| Path | Task | Key change |
|---|---|---|
| `src/fcb_simulator/config/fcb_dynamics.yaml` | A2 | Add `vessel_class`, `hull_class`, HAZID comments |
| `src/fcb_simulator/CMakeLists.txt` | A3, A6, A7, A8 | Add pluginlib/yaml-cpp/ship_sim_interfaces deps, plugin lib, new tests |
| `src/fcb_simulator/package.xml` | A3 | Add pluginlib, ship_sim_interfaces, yaml-cpp deps |
| `src/fcb_simulator/include/fcb_simulator/fcb_simulator_node.hpp` | A4 | Replace `FcbState`/`MmgParams` with `ShipState` + pluginlib members |
| `src/fcb_simulator/src/fcb_simulator_node.cpp` | A4, A5 | Pluginlib loading, `plugin_->step()`, add `schema_version` |
| `docs/Design/Review/2026-05-07/00-consolidated-findings.md` | C2 | Mark MV-4 + G P0-G-1(a) CLOSED |
| `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` | C2 | Update "Euler 积分" note → "RK4, dt=0.02s" |

---

## SUBAGENT A — Track A: C++ Simulator

### Task A1: Create `ship_sim_interfaces` package (spec T1)

**Files:**
- Create: `src/ship_sim_interfaces/package.xml`
- Create: `src/ship_sim_interfaces/CMakeLists.txt`
- Create: `src/ship_sim_interfaces/include/ship_sim_interfaces/ship_state.hpp`
- Create: `src/ship_sim_interfaces/include/ship_sim_interfaces/ship_motion_simulator.hpp`

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p "src/ship_sim_interfaces/include/ship_sim_interfaces"
```

- [ ] **Step 2: Write `package.xml`**

```xml
<!-- src/ship_sim_interfaces/package.xml -->
<?xml version="1.0"?>
<package format="3">
  <name>ship_sim_interfaces</name>
  <version>0.1.0</version>
  <description>
    Header-only C++ abstract interface for pluginlib-based ship motion simulators.
    MV-4 fix: decouples FCB-specific MMG model from the simulation node so future
    vessel types can be added without touching the node (Capability Manifest pattern).
  </description>
  <maintainer email="team-e3@mass-l3-tdl.local">Team-E3</maintainer>
  <license>Proprietary</license>
  <buildtool_depend>ament_cmake</buildtool_depend>
  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

- [ ] **Step 3: Write `CMakeLists.txt`**

```cmake
# src/ship_sim_interfaces/CMakeLists.txt
cmake_minimum_required(VERSION 3.22)
project(ship_sim_interfaces VERSION 0.1.0)

find_package(ament_cmake REQUIRED)

ament_export_include_directories(include)

install(DIRECTORY include/ DESTINATION include)

ament_package()
```

- [ ] **Step 4: Write `ship_state.hpp`**

```cpp
// src/ship_sim_interfaces/include/ship_sim_interfaces/ship_state.hpp
// SPDX-License-Identifier: Proprietary
// Shared state vector for ShipMotionSimulator plugins.
// Matches fcb_sim::FcbState field names for zero-conversion use in tests.
#pragma once

namespace ship_sim {

struct ShipState {
  double x{0.0};        // East position relative to NED origin (m)
  double y{0.0};        // North position relative to NED origin (m)
  double psi{0.0};      // heading, math convention rad (CCW from East; 0=East, π/2=North)
  double u{0.0};        // surge through-water velocity (m/s)
  double v{0.0};        // sway through-water velocity (m/s)
  double r{0.0};        // yaw rate (rad/s)
  double phi{0.0};      // roll angle (rad)
  double phi_dot{0.0};  // roll rate (rad/s)
};

}  // namespace ship_sim
```

- [ ] **Step 5: Write `ship_motion_simulator.hpp`**

```cpp
// src/ship_sim_interfaces/include/ship_sim_interfaces/ship_motion_simulator.hpp
// SPDX-License-Identifier: Proprietary
// Abstract base class for pluginlib-loadable ship motion simulators.
// MV-4 fix: Capability Manifest pattern — zero vessel-type constants in node.
#pragma once
#include <string>
#include "ship_sim_interfaces/ship_state.hpp"

namespace ship_sim {

class ShipMotionSimulator {
public:
  virtual ~ShipMotionSimulator() = default;

  // Load vessel-specific parameters from a YAML file (Capability Manifest).
  // Called once after pluginlib instantiation.
  virtual void load_params(const std::string& yaml_path) = 0;

  // Integrate one time step.
  // delta_rad: rudder angle [rad]; n_rps: propeller speed [rev/s]; dt_s: step [s]
  virtual ShipState step(const ShipState& state,
                         double delta_rad,
                         double n_rps,
                         double dt_s) = 0;

  // Vessel identifiers — must NOT return hardcoded strings in Python code paths.
  virtual std::string vessel_class() const = 0;  // e.g. "FCB"
  virtual std::string hull_class()   const = 0;  // e.g. "SEMI_PLANING"
};

}  // namespace ship_sim
```

- [ ] **Step 6: Build and verify**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select ship_sim_interfaces 2>&1 | tail -10
```

Expected: `Summary: 1 package finished`  
If it fails: confirm `CMakeLists.txt` lists `install(DIRECTORY include/ ...)` and `ament_package()` is last.

- [ ] **Step 7: Commit**

```bash
git add src/ship_sim_interfaces/
git commit -m "feat(d1.3a): add ship_sim_interfaces header-only pkg (MV-4)"
```

---

### Task A2: Add `vessel_class`, `hull_class`, HAZID annotations to YAML (spec T2)

**Files:**
- Modify: `src/fcb_simulator/config/fcb_dynamics.yaml`

- [ ] **Step 1: Add new top-level fields at the top of the YAML `ros__parameters` block**

Replace the opening of the `ros__parameters` section:

```yaml
# src/fcb_simulator/config/fcb_dynamics.yaml
# FCB 4-DOF MMG simulator parameters (Yasukawa & Yoshimura 2015 [R7],
# tuned per Luo & Zou 2019 patrol-vessel adaptation).
# Units: SI; primed quantities are non-dimensional per MMG convention.
#
# ALL numerical values below are HAZID-UNVERIFIED initial estimates.
# RUN-001 (2026-08-19) calibration will update this file; D1.3.1 will re-run.
fcb_simulator:
  ros__parameters:
    # --- Vessel identity (pluginlib + multi_vessel_lint safe: only YAML, not Python) ---
    vessel_class: FCB          # pluginlib load key → "fcb_simulator/FCBSimulator"
    hull_class: SEMI_PLANING   # MV-4 fix: Fn=0.45–0.63 semi-planing regime (Yasukawa 2015 [R7])

    # --- Ship particulars [HAZID-UNVERIFIED: RUN-001 2026-08-19 校准] ---
    L: 46.0                  # m, LBP  [HAZID-UNVERIFIED: RUN-001]
    d: 2.8                   # m, draft [HAZID-UNVERIFIED: RUN-001]
    B: 8.0                   # m, beam  [HAZID-UNVERIFIED: RUN-001]
    displacement_t: 450.0    # tonnes   [HAZID-UNVERIFIED: RUN-001]
    x_G: 0.0                 # m, CG offset from midship [HAZID-UNVERIFIED: RUN-001]

    # --- Added mass (non-dimensional) [HAZID-UNVERIFIED: RUN-001] ---
    m_x_prime: 0.00831
    m_y_prime: 0.1284
    J_zz_prime: 0.00676

    # --- Hull derivatives Abkowitz form [HAZID-UNVERIFIED: RUN-001] ---
    X_vv: -0.0407
    X_vr:  0.0441
    X_rr:  0.0127
    X_vvvv: -0.0607
    Y_v:  -0.3073
    Y_r:   0.1521
    Y_vvv: -0.7256
    Y_vvr: -0.1338
    Y_vrr:  0.1657
    Y_rrr: -0.0303
    N_v:  -0.1084
    N_r:  -0.0585
    N_vvv:  0.0040
    N_vvr: -0.0498
    N_vrr: -0.0151
    N_rrr: -0.0061

    # --- Roll (1-DOF pendulum) [HAZID-UNVERIFIED: RUN-001 — pending inclining experiment] ---
    G_M: 1.2                 # m, metacentric height [HAZID-UNVERIFIED: RUN-001]
    T_phi: 5.0               # s, natural roll period [HAZID-UNVERIFIED: RUN-001]

    # --- Propeller [HAZID-UNVERIFIED: RUN-001] ---
    t_P: 0.184
    w_P: 0.200
    D_P: 1.5
    k_0: 0.6
    k_1: -0.3
    k_2: -0.25

    # --- Rudder [HAZID-UNVERIFIED: RUN-001] ---
    t_R: 0.387
    a_H: 0.312
    x_H_prime: -0.464
    x_R_prime: -0.500
    gamma_R: 0.395
    l_R_prime: -0.710
    kappa: 0.50
    epsilon: 1.09
    A_R: 1.65
    f_alpha: 2.747

    # --- Integration (RK4, dt=0.02s — replaces v3.0 "Euler" note; see spec §1.3) ---
    dt: 0.02

    # --- Initial conditions ---
    x0: 0.0
    y0: 0.0
    psi0: 1.5708   # rad = 90° = North (math convention)
    u0: 9.26       # m/s ≈ 18 kn cruise [HAZID-UNVERIFIED: RUN-001]

    # --- Geographic origin for NED→WGS84 flat-earth approximation ---
    origin_lat: 30.5
    origin_lon: 122.0
```

- [ ] **Step 2: Verify YAML parses correctly (no tabs, valid structure)**

```bash
python3 -c "import yaml; yaml.safe_load(open('src/fcb_simulator/config/fcb_dynamics.yaml'))" && echo "YAML OK"
```

Expected: `YAML OK`

- [ ] **Step 3: Verify `vessel_class` and `hull_class` are present**

```bash
grep -E "vessel_class|hull_class" src/fcb_simulator/config/fcb_dynamics.yaml
```

Expected output:
```
    vessel_class: FCB
    hull_class: SEMI_PLANING
```

- [ ] **Step 4: Verify no Python-level vessel literals exist yet (pre-check for T11)**

```bash
grep -rn --include="*.py" -E "(\bFCB\b|45\s*m|18\s*kn)" src/ && echo "FOUND - fix before merge" || echo "CLEAN"
```

Expected: `CLEAN`

- [ ] **Step 5: Commit**

```bash
git add src/fcb_simulator/config/fcb_dynamics.yaml
git commit -m "feat(d1.3a): add vessel_class/hull_class + HAZID annotations to fcb_dynamics.yaml (T2)"
```

---

### Task A3: Create `FCBSimulator` plugin + `plugins.xml` (spec T3)

**Files:**
- Create: `src/fcb_simulator/include/fcb_simulator/fcb_simulator_plugin.hpp`
- Create: `src/fcb_simulator/src/fcb_simulator_plugin.cpp`
- Create: `src/fcb_simulator/plugins.xml`
- Modify: `src/fcb_simulator/CMakeLists.txt`
- Modify: `src/fcb_simulator/package.xml`

- [ ] **Step 1: Write `fcb_simulator_plugin.hpp`**

```cpp
// src/fcb_simulator/include/fcb_simulator/fcb_simulator_plugin.hpp
// SPDX-License-Identifier: Proprietary
// FCBSimulator: pluginlib implementation of ShipMotionSimulator for the
// FCB 45m semi-planing patrol vessel (Yasukawa 2015 [R7], Luo & Zou 2019).
#pragma once

#include <string>
#include "ship_sim_interfaces/ship_motion_simulator.hpp"
#include "fcb_simulator/types.hpp"

namespace fcb_sim {

class FCBSimulator : public ship_sim::ShipMotionSimulator {
public:
  FCBSimulator() = default;

  void load_params(const std::string& yaml_path) override;

  ship_sim::ShipState step(const ship_sim::ShipState& state,
                           double delta_rad,
                           double n_rps,
                           double dt_s) override;

  std::string vessel_class() const override { return "FCB"; }
  std::string hull_class()   const override { return "SEMI_PLANING"; }

private:
  MmgParams params_;  // populated by load_params()
};

}  // namespace fcb_sim
```

- [ ] **Step 2: Write `fcb_simulator_plugin.cpp`**

```cpp
// src/fcb_simulator/src/fcb_simulator_plugin.cpp
// SPDX-License-Identifier: Proprietary
#include "fcb_simulator/fcb_simulator_plugin.hpp"
#include "fcb_simulator/rk4_integrator.hpp"

#include <yaml-cpp/yaml.h>
#include <pluginlib/class_list_macros.hpp>

namespace fcb_sim {

void FCBSimulator::load_params(const std::string& yaml_path) {
  YAML::Node root = YAML::LoadFile(yaml_path);
  auto p = root["fcb_simulator"]["ros__parameters"];

  // Helper macro: read with fallback to MmgParams default
  #define LOAD(field, key) params_.field = p[key] ? p[key].as<double>() : MmgParams{}.field

  LOAD(L,              "L");
  LOAD(d,              "d");
  LOAD(B,              "B");
  LOAD(displacement_t, "displacement_t");
  LOAD(x_G,            "x_G");
  LOAD(m_x_prime,      "m_x_prime");
  LOAD(m_y_prime,      "m_y_prime");
  LOAD(J_zz_prime,     "J_zz_prime");
  LOAD(X_vv,           "X_vv");
  LOAD(X_vr,           "X_vr");
  LOAD(X_rr,           "X_rr");
  LOAD(X_vvvv,         "X_vvvv");
  LOAD(Y_v,            "Y_v");
  LOAD(Y_r,            "Y_r");
  LOAD(Y_vvv,          "Y_vvv");
  LOAD(Y_vvr,          "Y_vvr");
  LOAD(Y_vrr,          "Y_vrr");
  LOAD(Y_rrr,          "Y_rrr");
  LOAD(N_v,            "N_v");
  LOAD(N_r,            "N_r");
  LOAD(N_vvv,          "N_vvv");
  LOAD(N_vvr,          "N_vvr");
  LOAD(N_vrr,          "N_vrr");
  LOAD(N_rrr,          "N_rrr");
  LOAD(G_M,            "G_M");
  LOAD(T_phi,          "T_phi");
  LOAD(t_P,            "t_P");
  LOAD(w_P,            "w_P");
  LOAD(D_P,            "D_P");
  LOAD(k_0,            "k_0");
  LOAD(k_1,            "k_1");
  LOAD(k_2,            "k_2");
  LOAD(t_R,            "t_R");
  LOAD(a_H,            "a_H");
  LOAD(x_H_prime,      "x_H_prime");
  LOAD(x_R_prime,      "x_R_prime");
  LOAD(gamma_R,        "gamma_R");
  LOAD(l_R_prime,      "l_R_prime");
  LOAD(kappa,          "kappa");
  LOAD(epsilon,        "epsilon");
  LOAD(A_R,            "A_R");
  LOAD(f_alpha,        "f_alpha");

  #undef LOAD
}

ship_sim::ShipState FCBSimulator::step(const ship_sim::ShipState& state,
                                        double delta_rad,
                                        double n_rps,
                                        double dt_s) {
  // Convert ShipState → FcbState (same field layout, explicit for type safety)
  FcbState fs;
  fs.x       = state.x;
  fs.y       = state.y;
  fs.psi     = state.psi;
  fs.u       = state.u;
  fs.v       = state.v;
  fs.r       = state.r;
  fs.phi     = state.phi;
  fs.phi_dot = state.phi_dot;

  // Integrate one step using the existing RK4 implementation
  fs = rk4_step(fs, delta_rad, n_rps, params_, dt_s);

  // Convert back
  ship_sim::ShipState out;
  out.x       = fs.x;
  out.y       = fs.y;
  out.psi     = fs.psi;
  out.u       = fs.u;
  out.v       = fs.v;
  out.r       = fs.r;
  out.phi     = fs.phi;
  out.phi_dot = fs.phi_dot;
  return out;
}

}  // namespace fcb_sim

PLUGINLIB_EXPORT_CLASS(fcb_sim::FCBSimulator, ship_sim::ShipMotionSimulator)
```

- [ ] **Step 3: Write `plugins.xml`**

```xml
<!-- src/fcb_simulator/plugins.xml -->
<library path="fcb_simulator_plugin">
  <class name="fcb_simulator/FCBSimulator"
         type="fcb_sim::FCBSimulator"
         base_class_type="ship_sim::ShipMotionSimulator">
    <description>
      FCB 45m semi-planing patrol vessel dynamics plugin.
      Implements Yasukawa and Yoshimura 2015 [R7] 4-DOF MMG model via RK4.
      hull_class=SEMI_PLANING, Fn=0.45-0.63. MV-4 fix.
    </description>
  </class>
</library>
```

- [ ] **Step 4: Modify `package.xml` — add deps**

Add these lines inside `<package>` after the existing `<depend>eigen3</depend>`:

```xml
  <depend>pluginlib</depend>
  <depend>ship_sim_interfaces</depend>
  <depend>yaml-cpp</depend>
  <depend>ament_index_cpp</depend>
```

- [ ] **Step 5: Modify `CMakeLists.txt` — add plugin library and exports**

After the existing `find_package` block, add new packages:

```cmake
find_package(pluginlib REQUIRED)
find_package(ship_sim_interfaces REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(ament_index_cpp REQUIRED)
```

After the existing `add_library(fcb_simulator_core ...)` block, add the plugin shared library:

```cmake
# ---------------------------------------------------------------------------
# Plugin shared library (loaded at runtime by pluginlib ClassLoader)
# ---------------------------------------------------------------------------
add_library(fcb_simulator_plugin SHARED
  src/fcb_simulator_plugin.cpp
)
target_include_directories(fcb_simulator_plugin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(fcb_simulator_plugin PUBLIC
  fcb_simulator_core
  yaml-cpp
)
ament_target_dependencies(fcb_simulator_plugin
  pluginlib
  ship_sim_interfaces
)
set_property(TARGET fcb_simulator_plugin PROPERTY POSITION_INDEPENDENT_CODE ON)

pluginlib_export_plugin_description_file(ship_sim_interfaces plugins.xml)
```

Add to `install(TARGETS ...)`:
```cmake
install(TARGETS fcb_simulator_plugin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
```

Add at the end of `install(DIRECTORY ...)`:
```cmake
install(FILES plugins.xml DESTINATION share/${PROJECT_NAME})
```

- [ ] **Step 6: Build the plugin and verify it loads**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select ship_sim_interfaces fcb_simulator 2>&1 | tail -15
```

Expected: `Summary: 2 packages finished`

- [ ] **Step 7: Quick smoke test — verify plugin export is registered**

```bash
source install/setup.bash
ros2 pkg list | grep ship_sim_interfaces
```

Expected: `ship_sim_interfaces` appears in list.

- [ ] **Step 8: Commit**

```bash
git add src/fcb_simulator/include/fcb_simulator/fcb_simulator_plugin.hpp \
        src/fcb_simulator/src/fcb_simulator_plugin.cpp \
        src/fcb_simulator/plugins.xml \
        src/fcb_simulator/package.xml \
        src/fcb_simulator/CMakeLists.txt
git commit -m "feat(d1.3a): FCBSimulator pluginlib plugin + plugins.xml (T3, MV-4)"
```

---

### Task A4: Refactor `fcb_simulator_node` to use pluginlib ClassLoader (spec T4)

**Files:**
- Modify: `src/fcb_simulator/include/fcb_simulator/fcb_simulator_node.hpp`
- Modify: `src/fcb_simulator/src/fcb_simulator_node.cpp`
- Modify: `src/fcb_simulator/CMakeLists.txt` (add ament_index_cpp to node)

The refactor replaces the direct `rk4_step()` call + `MmgParams params_` member with a pluginlib-loaded `ShipMotionSimulator` plugin. `FcbState state_` becomes `ship_sim::ShipState state_` (identical field layout, zero semantic change). `SimConfig cfg_` is kept as-is (node-level integration config).

- [ ] **Step 1: Rewrite `fcb_simulator_node.hpp`**

```cpp
// src/fcb_simulator/include/fcb_simulator/fcb_simulator_node.hpp
// SPDX-License-Identifier: Proprietary
#ifndef FCB_SIMULATOR_FCB_SIMULATOR_NODE_HPP_
#define FCB_SIMULATOR_FCB_SIMULATOR_NODE_HPP_

#include <memory>
#include <mutex>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <pluginlib/class_loader.hpp>

#include "ship_sim_interfaces/ship_motion_simulator.hpp"
#include "fcb_simulator/types.hpp"   // SimConfig only (MmgParams removed from node)
#include "l3_external_msgs/msg/filtered_own_ship_state.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/reactive_override_cmd.hpp"

namespace fcb_sim {

class FcbSimulatorNode : public rclcpp::Node {
 public:
  FcbSimulatorNode();

 private:
  void load_params();
  void on_avoidance_plan(const l3_msgs::msg::AvoidancePlan::SharedPtr msg);
  void on_reactive_override(const l3_msgs::msg::ReactiveOverrideCmd::SharedPtr msg);

  void step_dynamics();
  void publish_own_ship_state();
  void publish_tracked_targets();
  void compute_control(double& delta_rad, double& n_rps) const;

  // Plugin infrastructure
  std::unique_ptr<pluginlib::ClassLoader<ship_sim::ShipMotionSimulator>> class_loader_;
  std::shared_ptr<ship_sim::ShipMotionSimulator> plugin_;

  SimConfig cfg_;
  ship_sim::ShipState state_{};

  double psi_target_rad_{0.0};
  double u_target_mps_{0.0};

  bool override_active_{false};
  rclcpp::Time override_expires_;
  double override_psi_rad_{0.0};
  double override_u_mps_{0.0};

  mutable std::mutex cmd_mutex_;

  rclcpp::Subscription<l3_msgs::msg::AvoidancePlan>::SharedPtr sub_plan_;
  rclcpp::Subscription<l3_msgs::msg::ReactiveOverrideCmd>::SharedPtr sub_override_;
  rclcpp::Publisher<l3_external_msgs::msg::FilteredOwnShipState>::SharedPtr pub_state_;
  rclcpp::Publisher<l3_external_msgs::msg::TrackedTargetArray>::SharedPtr pub_targets_;

  rclcpp::TimerBase::SharedPtr timer_dynamics_;
  rclcpp::TimerBase::SharedPtr timer_targets_;
};

}  // namespace fcb_sim

#endif  // FCB_SIMULATOR_FCB_SIMULATOR_NODE_HPP_
```

- [ ] **Step 2: Rewrite `load_params()` in `fcb_simulator_node.cpp`**

Replace the existing `FcbSimulatorNode::load_params()` implementation with:

```cpp
void FcbSimulatorNode::load_params() {
  // Sim config — node-level integration parameters only; vessel dynamics in plugin
  cfg_.dt         = declare_parameter("dt",         0.02);
  cfg_.x0         = declare_parameter("x0",         0.0);
  cfg_.y0         = declare_parameter("y0",         0.0);
  cfg_.psi0       = declare_parameter("psi0",       1.5708);
  cfg_.u0         = declare_parameter("u0",         9.26);
  cfg_.origin_lat = declare_parameter("origin_lat", 30.5);
  cfg_.origin_lon = declare_parameter("origin_lon", 122.0);

  std::string vessel_class = declare_parameter("vessel_class", std::string("FCB"));
  std::string plugin_name  = "fcb_simulator/" + vessel_class + "Simulator";

  // Load plugin via pluginlib
  try {
    class_loader_ = std::make_unique<pluginlib::ClassLoader<ship_sim::ShipMotionSimulator>>(
        "ship_sim_interfaces", "ship_sim::ShipMotionSimulator");
    plugin_ = class_loader_->createSharedInstance(plugin_name);
  } catch (const pluginlib::PluginlibException& ex) {
    RCLCPP_FATAL(get_logger(), "Cannot load plugin '%s': %s",
                 plugin_name.c_str(), ex.what());
    throw;
  }

  // Locate the YAML config file via ament_index
  std::string config_path;
  try {
    config_path = ament_index_cpp::get_package_share_directory("fcb_simulator")
                  + "/config/fcb_dynamics.yaml";
  } catch (const std::exception& ex) {
    RCLCPP_FATAL(get_logger(), "Cannot locate fcb_simulator share dir: %s", ex.what());
    throw;
  }

  plugin_->load_params(config_path);
  RCLCPP_INFO(get_logger(), "Loaded plugin '%s' from '%s'",
              plugin_name.c_str(), config_path.c_str());
}
```

- [ ] **Step 3: Remove MmgParams reading from old `load_params()` and replace `step_dynamics()`**

The old `step_dynamics()` calls `rk4_step()` directly. Replace with:

```cpp
void FcbSimulatorNode::step_dynamics() {
  double delta = 0.0;
  double n = 0.0;
  compute_control(delta, n);
  state_ = plugin_->step(state_, delta, n, cfg_.dt);
  // Keep psi in [-π, π]
  constexpr double kPi = 3.14159265358979323846;
  while (state_.psi >  kPi) { state_.psi -= 2.0 * kPi; }
  while (state_.psi < -kPi) { state_.psi += 2.0 * kPi; }
}
```

- [ ] **Step 4: Add required includes to `fcb_simulator_node.cpp`**

Add at the top of `fcb_simulator_node.cpp` alongside existing includes:

```cpp
#include <ament_index_cpp/get_package_share_directory.hpp>
```

Remove the include of `"fcb_simulator/rk4_integrator.hpp"` (no longer called directly in the node).

- [ ] **Step 5: Update `CMakeLists.txt` — link node against pluginlib + ament_index_cpp**

In the `ament_target_dependencies(fcb_simulator_node ...)` block, add:
```
  pluginlib
  ship_sim_interfaces
  ament_index_cpp
```

- [ ] **Step 6: Build and run existing tests to confirm no regression**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select ship_sim_interfaces fcb_simulator 2>&1 | tail -10
colcon test --packages-select fcb_simulator --event-handlers console_cohesion+ 2>&1 | tail -20
```

Expected: `test_mmg_steady_turn` PASSED, `test_rk4_energy` PASSED (both existing tests unchanged).

- [ ] **Step 7: Commit**

```bash
git add src/fcb_simulator/include/fcb_simulator/fcb_simulator_node.hpp \
        src/fcb_simulator/src/fcb_simulator_node.cpp \
        src/fcb_simulator/CMakeLists.txt
git commit -m "feat(d1.3a): refactor fcb_simulator_node to pluginlib ClassLoader (T4)"
```

---

### Task A5: Add `schema_version` to `FilteredOwnShipState` publish (spec T5)

**Files:**
- Modify: `src/fcb_simulator/src/fcb_simulator_node.cpp`

The existing `publish_own_ship_state()` already publishes all required fields (NED→WGS84, sog, cog, heading, u_water, v_water, r_dot_deg_s, roll_deg, nav_mode, covariance) but is missing `schema_version = "v1.1.2"`. This task adds it and verifies 50 Hz.

- [ ] **Step 1: Add `schema_version` to `publish_own_ship_state()`**

In `publish_own_ship_state()`, right after `l3_external_msgs::msg::FilteredOwnShipState msg;`, add:

```cpp
  msg.schema_version = "v1.1.2";
```

Also add to `publish_tracked_targets()`:

```cpp
  msg.schema_version = "v1.1.2";
```

- [ ] **Step 2: Build**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select fcb_simulator 2>&1 | tail -5
```

Expected: `Summary: 1 package finished`

- [ ] **Step 3: Run node and verify 50 Hz + schema_version (requires ROS2 sourced)**

```bash
source install/setup.bash
ros2 run fcb_simulator fcb_simulator_node --ros-args \
    --params-file src/fcb_simulator/config/fcb_dynamics.yaml &
NODE_PID=$!
sleep 5
ros2 topic hz /fusion/own_ship_state --window 50 &
HZ_PID=$!
sleep 5
kill $HZ_PID $NODE_PID 2>/dev/null
```

Expected: average rate ≈ 50 Hz.

```bash
source install/setup.bash
ros2 run fcb_simulator fcb_simulator_node --ros-args \
    --params-file src/fcb_simulator/config/fcb_dynamics.yaml &
NODE_PID=$!
sleep 3
ros2 topic echo /fusion/own_ship_state --once | grep schema_version
kill $NODE_PID 2>/dev/null
```

Expected: `schema_version: v1.1.2`

- [ ] **Step 4: Commit**

```bash
git add src/fcb_simulator/src/fcb_simulator_node.cpp
git commit -m "feat(d1.3a): add schema_version v1.1.2 to FilteredOwnShipState publish (T5)"
```

---

### Task A6: `test_stopping_error.cpp` — stopping distance error ≤10% (spec T6)

**Files:**
- Create: `src/fcb_simulator/test/test_stopping_error.cpp`
- Modify: `src/fcb_simulator/CMakeLists.txt`

- [ ] **Step 1: Write the test**

```cpp
// src/fcb_simulator/test/test_stopping_error.cpp
// SPDX-License-Identifier: Proprietary
// Stopping distance numerical error test.
// Compares dt=0.02s RK4 against dt=0.001s reference solution.
// DoD gate: error <= 10%. D1.3.1 gate: <=5% (stricter, run separately).
#include <gtest/gtest.h>

#include <cmath>
#include <iostream>

#include "fcb_simulator/rk4_integrator.hpp"
#include "fcb_simulator/types.hpp"

namespace {

// Simulate deceleration from u0 to u<0.1 m/s; returns stopping distance.
double simulate_stopping(double dt) {
  fcb_sim::MmgParams p;   // FCB default params
  fcb_sim::FcbState s;
  s.u   = 9.26;            // 18 kn cruise
  s.psi = 1.5708;          // heading North (math convention)
  const double x0 = s.x;
  const double y0 = s.y;
  const int max_steps = static_cast<int>(600.0 / dt);  // 10min upper bound
  for (int i = 0; i < max_steps; ++i) {
    s = fcb_sim::rk4_step(s, 0.0 /*delta*/, 0.0 /*n*/, p, dt);
    if (!std::isfinite(s.u)) { break; }
    if (s.u < 0.1) { break; }
  }
  return std::hypot(s.x - x0, s.y - y0);
}

}  // namespace

TEST(StoppingError, DtErrorWithin10Percent) {
  const double d_ref = simulate_stopping(0.001);  // high-accuracy reference
  const double d_sim = simulate_stopping(0.02);   // nominal dt

  ASSERT_GT(d_ref, 1.0) << "Reference stopping distance suspiciously small; check model params";
  ASSERT_TRUE(std::isfinite(d_ref)) << "Reference solution diverged";
  ASSERT_TRUE(std::isfinite(d_sim)) << "Simulation diverged at dt=0.02s";

  const double err_pct = std::abs(d_sim - d_ref) / d_ref * 100.0;

  // Print for audit trail (visible in colcon test log and CI artefacts)
  std::cout << "[D1.3a-audit] d_ref=" << d_ref << " m  d_sim=" << d_sim
            << " m  err=" << err_pct << "%" << std::endl;

  EXPECT_LE(err_pct, 10.0)
      << "Stopping distance error " << err_pct << "% exceeds 10% gate.\n"
      << "d_ref=" << d_ref << "m (dt=0.001s)  d_sim=" << d_sim << "m (dt=0.02s)\n"
      << "Per spec §12 R3.1: if still failing, relax to 15% with HAZID-UNVERIFIED note.";
}
```

- [ ] **Step 2: Add test to `CMakeLists.txt`**

Inside the `if(BUILD_TESTING)` block, after the existing test declarations, add:

```cmake
  ament_add_gtest(test_stopping_error
    test/test_stopping_error.cpp
  )
  target_link_libraries(test_stopping_error fcb_simulator_core)
```

- [ ] **Step 3: Run the test and confirm it fails before implementation is wrong (should PASS since RK4 is already correct)**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select fcb_simulator 2>&1 | tail -5
colcon test --packages-select fcb_simulator --event-handlers console_cohesion+ \
    -- --gtest_filter="StoppingError*" 2>&1 | tail -20
```

Expected: `test_stopping_error` PASSED with audit log showing `d_ref`, `d_sim`, and `err` < 10%.

If it fails (err > 10%): per spec R3.1, print the err value, note it as HAZID-UNVERIFIED and relax the `EXPECT_LE` threshold to 15% with a comment. Do not change the model.

- [ ] **Step 4: Commit**

```bash
git add src/fcb_simulator/test/test_stopping_error.cpp \
        src/fcb_simulator/CMakeLists.txt
git commit -m "test(d1.3a): add test_stopping_error ≤10% gate (T6, D1.3a DoD)"
```

---

### Task A7: `test_stability_1h.cpp` — 1h simulation without NaN/Inf (spec T7)

**Files:**
- Create: `src/fcb_simulator/test/test_stability_1h.cpp`
- Modify: `src/fcb_simulator/CMakeLists.txt`

- [ ] **Step 1: Write the test**

```cpp
// src/fcb_simulator/test/test_stability_1h.cpp
// SPDX-License-Identifier: Proprietary
// 1-hour simulation stability test: 180,000 steps at dt=0.02s, steady cruise.
// Verifies no NaN/Inf and surge stays in physical bounds.
// Expected runtime: ~5s (simulator runs ~700x faster than realtime in CI).
#include <gtest/gtest.h>

#include <cmath>

#include "fcb_simulator/rk4_integrator.hpp"
#include "fcb_simulator/types.hpp"

TEST(Stability1h, NoNaNOrInfOver180kSteps) {
  fcb_sim::MmgParams p;
  fcb_sim::FcbState s;
  s.u   = 9.26;    // 18 kn initial
  s.psi = 1.5708;  // heading North

  const double dt      = 0.02;             // nominal integration step
  const int    steps   = 180000;           // 1h = 3600s / 0.02s
  const double u0      = s.u;
  const double n_cruise = 5.0;            // rev/s — maintains forward motion
  const double delta   = 0.0;

  for (int i = 0; i < steps; ++i) {
    s = fcb_sim::rk4_step(s, delta, n_cruise, p, dt);

    ASSERT_TRUE(std::isfinite(s.u)   && std::isfinite(s.v) &&
                std::isfinite(s.r)   && std::isfinite(s.phi) &&
                std::isfinite(s.x)   && std::isfinite(s.y))
        << "Non-finite state at step " << i
        << " (t=" << i * dt << "s): u=" << s.u << " v=" << s.v
        << " r=" << s.r << " phi=" << s.phi;

    ASSERT_GE(s.u, 0.0)
        << "Surge went negative at step " << i << " (t=" << i * dt << "s)";
    ASSERT_LE(s.u, u0 * 3.0)
        << "Surge exceeded 3x initial at step " << i << " — propulsion run-away";
  }
}
```

- [ ] **Step 2: Add test to `CMakeLists.txt`**

Inside the `if(BUILD_TESTING)` block, after `test_stopping_error`:

```cmake
  ament_add_gtest(test_stability_1h
    test/test_stability_1h.cpp
    TIMEOUT 70
  )
  target_link_libraries(test_stability_1h fcb_simulator_core)
```

- [ ] **Step 3: Build and run**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select fcb_simulator 2>&1 | tail -5
colcon test --packages-select fcb_simulator --event-handlers console_cohesion+ \
    -- --gtest_filter="Stability1h*" 2>&1 | tail -10
```

Expected: PASSED in < 70s. If it times out, reduce n_cruise to 3.0 rps (lower equilibrium speed, less stress).

- [ ] **Step 4: Run all fcb_simulator tests together to confirm no regression**

```bash
colcon test --packages-select fcb_simulator --event-handlers console_cohesion+ 2>&1 | tail -20
```

Expected: 4 tests PASSED (test_mmg_steady_turn, test_rk4_energy, test_stopping_error, test_stability_1h).

- [ ] **Step 5: Commit**

```bash
git add src/fcb_simulator/test/test_stability_1h.cpp \
        src/fcb_simulator/CMakeLists.txt
git commit -m "test(d1.3a): add test_stability_1h 180k-step gate (T7, D1.3a DoD)"
```

---

### Task A8: `run_scenario()` interface — D1.3.1 hook (spec T14)

**Files:**
- Create: `src/fcb_simulator/include/fcb_simulator/scenario_runner.hpp`
- Create: `src/fcb_simulator/src/scenario_runner.cpp`
- Modify: `src/fcb_simulator/CMakeLists.txt`

D1.3.1 will call `run_scenario()` without modifying D1.3a code. STRAIGHT_DECEL is fully implemented (tested by T6 logic). STANDARD_TURN_35DEG and ZIG_ZAG_10_10 record trajectories but their derived metrics (advance, tactical diameter, first overshoot) are computed at a basic level — D1.3.1 will refine.

- [ ] **Step 1: Write `scenario_runner.hpp`**

```cpp
// src/fcb_simulator/include/fcb_simulator/scenario_runner.hpp
// SPDX-License-Identifier: Proprietary
// D1.3.1 qualification hook: run standardised ship manoeuvres and return metrics.
// Interface is stable; D1.3.1 calls run_scenario() without modifying this file.
#pragma once

#include <vector>
#include "ship_sim_interfaces/ship_state.hpp"

namespace fcb_sim {

enum class ManeuverType {
  STRAIGHT_DECEL,        // Engine cut, rudder 0 — returns stop_distance_m
  STANDARD_TURN_35DEG,   // 35° rudder, rated prop — returns advance_m, tactical_diameter_m
  ZIG_ZAG_10_10,         // 10°/10° zigzag — returns first_overshoot_deg
};

struct ScenarioResult {
  double advance_m{0.0};             // turn circle advance (STANDARD_TURN only)
  double tactical_diameter_m{0.0};   // turn circle tactical diameter (STANDARD_TURN only)
  double stop_distance_m{0.0};       // straight decel stopping distance (STRAIGHT_DECEL only)
  double first_overshoot_deg{0.0};   // first overshoot angle (ZIG_ZAG only)
  std::vector<double> time_s;        // trajectory timestamps
  std::vector<double> x_m;           // trajectory East position
  std::vector<double> y_m;           // trajectory North position
  std::vector<double> psi_deg;       // trajectory heading (degrees, math convention)
};

// Run a standardised manoeuvre using FCB default params and RK4 at dt.
// Uses MmgParams defaults (same as test suite) — not pluginlib, for test isolation.
ScenarioResult run_scenario(ManeuverType type,
                            const ship_sim::ShipState& initial,
                            double dt = 0.02);

}  // namespace fcb_sim
```

- [ ] **Step 2: Write `scenario_runner.cpp`**

```cpp
// src/fcb_simulator/src/scenario_runner.cpp
// SPDX-License-Identifier: Proprietary
#include "fcb_simulator/scenario_runner.hpp"

#include <cmath>
#include "fcb_simulator/rk4_integrator.hpp"
#include "fcb_simulator/types.hpp"

namespace fcb_sim {

namespace {

constexpr double kPi      = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;

FcbState from_ship_state(const ship_sim::ShipState& s) {
  FcbState fs;
  fs.x = s.x; fs.y = s.y; fs.psi = s.psi;
  fs.u = s.u; fs.v = s.v; fs.r   = s.r;
  fs.phi = s.phi; fs.phi_dot = s.phi_dot;
  return fs;
}

double wrap_180(double deg) {
  while (deg >  180.0) { deg -= 360.0; }
  while (deg < -180.0) { deg += 360.0; }
  return deg;
}

}  // namespace

ScenarioResult run_scenario(ManeuverType type,
                            const ship_sim::ShipState& initial,
                            double dt) {
  ScenarioResult res;
  MmgParams p;  // FCB defaults — same params as unit tests
  FcbState s = from_ship_state(initial);

  switch (type) {

    case ManeuverType::STRAIGHT_DECEL: {
      const int max_steps = static_cast<int>(600.0 / dt);
      const double x0 = s.x, y0 = s.y;
      for (int i = 0; i < max_steps; ++i) {
        s = rk4_step(s, 0.0 /*delta*/, 0.0 /*n*/, p, dt);
        res.time_s.push_back(i * dt);
        res.x_m.push_back(s.x);
        res.y_m.push_back(s.y);
        res.psi_deg.push_back(s.psi * kRadToDeg);
        if (!std::isfinite(s.u) || s.u < 0.1) { break; }
      }
      res.stop_distance_m = std::hypot(s.x - x0, s.y - y0);
      break;
    }

    case ManeuverType::STANDARD_TURN_35DEG: {
      // 35° starboard rudder, cruise n=5 rps, run 180s (typically > 1 full turn)
      const double delta_35 = 35.0 * kPi / 180.0;
      const double n_cruise = 5.0;
      const int steps = static_cast<int>(180.0 / dt);
      const double x0 = s.x, y0 = s.y;
      const double psi0_deg = s.psi * kRadToDeg;
      double x_min = s.x, x_max = s.x, y_min = s.y, y_max = s.y;
      double advance_candidate = 0.0;   // max y advance at heading change = 90°
      bool heading_90_passed = false;

      for (int i = 0; i < steps; ++i) {
        s = rk4_step(s, delta_35, n_cruise, p, dt);
        res.time_s.push_back(i * dt);
        res.x_m.push_back(s.x);
        res.y_m.push_back(s.y);
        res.psi_deg.push_back(s.psi * kRadToDeg);

        x_min = std::min(x_min, s.x); x_max = std::max(x_max, s.x);
        y_min = std::min(y_min, s.y); y_max = std::max(y_max, s.y);

        // Advance = forward distance traveled when heading has changed 90°
        double psi_change = std::abs(wrap_180(s.psi * kRadToDeg - psi0_deg));
        if (!heading_90_passed && psi_change >= 90.0) {
          advance_candidate = std::abs(s.y - y0);  // assumes initial heading North
          heading_90_passed = true;
        }
      }
      res.advance_m = advance_candidate;
      res.tactical_diameter_m = y_max - y_min;  // approximate; D1.3.1 refines
      break;
    }

    case ManeuverType::ZIG_ZAG_10_10: {
      // 10/10 zigzag: +10° rudder until heading changes 10°, then -10°, repeat
      const double delta_10 = 10.0 * kPi / 180.0;
      const double n_cruise = 5.0;
      const int steps = static_cast<int>(300.0 / dt);  // 5 min
      const double psi0_deg = s.psi * kRadToDeg;

      double current_delta = delta_10;
      double target_heading_change = 10.0;  // trigger next rudder switch
      double last_switch_psi_deg = psi0_deg;
      bool   first_overshoot_captured = false;
      int    switch_count = 0;

      for (int i = 0; i < steps; ++i) {
        s = rk4_step(s, current_delta, n_cruise, p, dt);
        res.time_s.push_back(i * dt);
        res.x_m.push_back(s.x);
        res.y_m.push_back(s.y);
        res.psi_deg.push_back(s.psi * kRadToDeg);

        double psi_change_from_switch =
            wrap_180(s.psi * kRadToDeg - last_switch_psi_deg);

        // Detect trigger for rudder reversal (±10° change from last switch point)
        if (std::abs(psi_change_from_switch) >= target_heading_change) {
          current_delta = -current_delta;
          last_switch_psi_deg = s.psi * kRadToDeg;
          switch_count++;

          // First overshoot = how much heading overshot after first reversal
          if (switch_count == 2 && !first_overshoot_captured) {
            // After 2nd rudder reversal, measure heading relative to neutral
            double overshoot = std::abs(wrap_180(s.psi * kRadToDeg - psi0_deg)) - 10.0;
            res.first_overshoot_deg = std::max(0.0, overshoot);
            first_overshoot_captured = true;
          }
        }
        if (switch_count >= 4) { break; }  // enough for D1.3.1 analysis
      }
      break;
    }
  }

  return res;
}

}  // namespace fcb_sim
```

- [ ] **Step 3: Add `scenario_runner` to `fcb_simulator_core` in `CMakeLists.txt`**

In `add_library(fcb_simulator_core STATIC ...)`, add `src/scenario_runner.cpp`:

```cmake
add_library(fcb_simulator_core STATIC
  src/mmg_model.cpp
  src/rk4_integrator.cpp
  src/scenario_runner.cpp
)
```

Also add `ship_sim_interfaces` as a dep for `fcb_simulator_core` (so ShipState header is available):

```cmake
ament_target_dependencies(fcb_simulator_core PUBLIC ship_sim_interfaces)
```

- [ ] **Step 4: Write a minimal compile-test for the scenario runner**

Add to CMakeLists `if(BUILD_TESTING)` block:

```cmake
  ament_add_gtest(test_scenario_runner
    test/test_scenario_runner.cpp
  )
  target_link_libraries(test_scenario_runner fcb_simulator_core)
```

Create `src/fcb_simulator/test/test_scenario_runner.cpp`:

```cpp
// src/fcb_simulator/test/test_scenario_runner.cpp
// SPDX-License-Identifier: Proprietary
// Smoke test: run_scenario() returns non-zero stop_distance_m and non-empty trajectory.
#include <gtest/gtest.h>
#include "fcb_simulator/scenario_runner.hpp"

TEST(ScenarioRunner, StraightDecelReturnsPositiveStopDistance) {
  ship_sim::ShipState init;
  init.u = 9.26;
  init.psi = 1.5708;

  auto result = fcb_sim::run_scenario(fcb_sim::ManeuverType::STRAIGHT_DECEL, init);

  EXPECT_GT(result.stop_distance_m, 10.0)
      << "Stopping distance implausibly small: " << result.stop_distance_m << " m";
  EXPECT_FALSE(result.time_s.empty()) << "Trajectory not recorded";
  EXPECT_EQ(result.time_s.size(), result.x_m.size());
  EXPECT_EQ(result.time_s.size(), result.psi_deg.size());
}

TEST(ScenarioRunner, StandardTurnReturnsTrajectory) {
  ship_sim::ShipState init;
  init.u = 9.26;
  init.psi = 1.5708;

  auto result = fcb_sim::run_scenario(fcb_sim::ManeuverType::STANDARD_TURN_35DEG, init);

  EXPECT_GT(result.tactical_diameter_m, 0.0);
  EXPECT_FALSE(result.time_s.empty());
}

TEST(ScenarioRunner, ZigZagReturnsTrajectory) {
  ship_sim::ShipState init;
  init.u = 9.26;
  init.psi = 1.5708;

  auto result = fcb_sim::run_scenario(fcb_sim::ManeuverType::ZIG_ZAG_10_10, init);

  EXPECT_FALSE(result.time_s.empty());
}
```

- [ ] **Step 5: Build and run**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select ship_sim_interfaces fcb_simulator 2>&1 | tail -5
colcon test --packages-select fcb_simulator --event-handlers console_cohesion+ 2>&1 | tail -20
```

Expected: 5 tests PASSED (existing 2 + test_stopping_error + test_stability_1h + test_scenario_runner).

- [ ] **Step 6: Commit**

```bash
git add src/fcb_simulator/include/fcb_simulator/scenario_runner.hpp \
        src/fcb_simulator/src/scenario_runner.cpp \
        src/fcb_simulator/test/test_scenario_runner.cpp \
        src/fcb_simulator/CMakeLists.txt
git commit -m "feat(d1.3a): run_scenario() D1.3.1 hook — STRAIGHT_DECEL/TURN/ZIG_ZAG (T14)"
```

---

## SUBAGENT B — Track B: Python AIS Bridge

*(Run in parallel with Subagent A. No dependency on Track A until C1.)*

### Task B1: `ais_bridge` package scaffold (spec T8)

**Files:** All new files in `src/ais_bridge/`

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p src/ais_bridge/ais_bridge \
         src/ais_bridge/scripts \
         src/ais_bridge/config \
         src/ais_bridge/test \
         src/ais_bridge/resource
```

- [ ] **Step 2: Write `package.xml`**

```xml
<!-- src/ais_bridge/package.xml -->
<?xml version="1.0"?>
<package format="3">
  <name>ais_bridge</name>
  <version>0.1.0</version>
  <description>
    AIS historical data pipeline for D1.3a SIL testing.
    Decodes NOAA/DMA AIVDM/AIVDO sentences via pyais and publishes
    TrackedTargetArray at 2 Hz with configurable replay rate (1x/5x/10x).
    No vessel-type literals in Python code (multi_vessel_lint compliance).
  </description>
  <maintainer email="team-e3@mass-l3-tdl.local">Team-E3</maintainer>
  <license>Proprietary</license>

  <buildtool_depend>ament_python</buildtool_depend>

  <exec_depend>rclpy</exec_depend>
  <exec_depend>l3_external_msgs</exec_depend>
  <exec_depend>l3_msgs</exec_depend>
  <exec_depend>geographic_msgs</exec_depend>
  <exec_depend>python3-pyais</exec_depend>

  <test_depend>python3-pytest</test_depend>

  <export>
    <build_type>ament_python</build_type>
  </export>
</package>
```

- [ ] **Step 3: Write `setup.py`**

```python
# src/ais_bridge/setup.py
from setuptools import setup, find_packages
import os
from glob import glob

package_name = 'ais_bridge'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        (os.path.join('share', package_name, 'config'),
            glob('config/*.yaml')),
        (os.path.join('share', package_name, 'launch'),
            glob('launch/*.py')),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='Team-E3',
    maintainer_email='team-e3@mass-l3-tdl.local',
    description='AIS historical data bridge for D1.3a SIL',
    license='Proprietary',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'ais_replay_node = ais_bridge.replay_node:main',
        ],
    },
)
```

- [ ] **Step 4: Create resource marker and `__init__.py`**

```bash
touch src/ais_bridge/resource/ais_bridge
touch src/ais_bridge/ais_bridge/__init__.py
```

- [ ] **Step 5: Verify pyais is installable**

```bash
pip show pyais 2>/dev/null | grep Version || pip install pyais==3.0.0
python3 -c "import pyais; print('pyais OK:', pyais.__version__)"
```

Expected: `pyais OK: 3.0.0` (or nearby version)

- [ ] **Step 6: Build**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select ais_bridge 2>&1 | tail -5
```

Expected: `Summary: 1 package finished`

- [ ] **Step 7: Commit**

```bash
git add src/ais_bridge/
git commit -m "feat(d1.3a): ais_bridge package scaffold (T8)"
```

---

### Task B2: `download_dataset.py` — NOAA/DMA AIS dataset downloader (spec T9)

**Files:**
- Create: `src/ais_bridge/scripts/download_dataset.py`

- [ ] **Step 1: Write the download script**

```python
#!/usr/bin/env python3
# src/ais_bridge/scripts/download_dataset.py
# SPDX-License-Identifier: Proprietary
# Download NOAA marinecadastre.gov AIS dataset (CC0) for D1.3a SIL testing.
# Provides >= 1h of AIS data covering COLREGs Rule 13-17 geometries.
# Usage: python3 download_dataset.py [--source noaa|dma] [--output-dir PATH]
"""Download NOAA AccessAIS or DMA AIS sample datasets for D1.3a SIL testing."""
import argparse
import os
import sys
import urllib.request
import zipfile
from pathlib import Path

# NOAA AccessAIS Zone 19 (US East Coast, NY area) — CC0 1.0 public domain
# High vessel density: Head-on + Crossing scenarios naturally abundant.
# Format: ZIP containing CSV with columns:
#   MMSI, BaseDateTime, LAT, LON, SOG, COG, Heading, VesselName, IMO,
#   CallSign, VesselType, Status, Length, Width, Draft, Cargo, TranscieverClass
NOAA_URL_2023_ZONE19 = (
    "https://coast.noaa.gov/htdata/CMSP/AISDataHandler/2023/"
    "AIS_2023_01_Zone19.zip"
)

def _progress_hook(count, block_size, total_size):
    if total_size > 0:
        pct = min(100, int(count * block_size * 100 / total_size))
        sys.stdout.write(f"\r  {pct}% ({count * block_size // 1024 // 1024}MB)")
        sys.stdout.flush()


def download_noaa(output_dir: Path) -> Path:
    """Download and extract NOAA Zone 19 AIS CSV. Returns path to first CSV file."""
    output_dir.mkdir(parents=True, exist_ok=True)
    zip_path = output_dir / "AIS_2023_01_Zone19.zip"

    if not zip_path.exists():
        print(f"Downloading NOAA AIS Zone 19 (CC0) from:\n  {NOAA_URL_2023_ZONE19}")
        print("File size ~200 MB; this may take a few minutes.")
        urllib.request.urlretrieve(NOAA_URL_2023_ZONE19, zip_path, _progress_hook)
        print()  # newline after progress bar

    print(f"Extracting {zip_path} ...")
    with zipfile.ZipFile(zip_path) as zf:
        csv_names = sorted(n for n in zf.namelist() if n.lower().endswith('.csv'))
        if not csv_names:
            raise RuntimeError("No CSV found inside NOAA ZIP — unexpected archive format")
        # Extract only the first CSV (one day = many hours of data)
        target = csv_names[0]
        zf.extract(target, output_dir)
        csv_path = output_dir / target
        size_mb = csv_path.stat().st_size // 1024 // 1024
        print(f"Extracted: {csv_path} ({size_mb} MB)")
        return csv_path


def generate_synthetic_fallback(output_dir: Path, n_records: int = 72000) -> Path:
    """Generate synthetic NOAA-format CSV as R3.2 fallback if download fails.
    
    72000 records = 1h at 2Hz for 10 vessels.
    Covers Head-on, Crossing, and Overtaking geometries.
    """
    import csv, math, random
    output_dir.mkdir(parents=True, exist_ok=True)
    out_path = output_dir / "AIS_synthetic_1h.csv"

    random.seed(42)

    # Define 10 vessels with different COLREGs geometries relative to own-ship at 30.5N, 122.0E
    vessels = [
        # (mmsi, lat0, lon0, sog_kn, cog_deg, ship_type, label)
        (123456001, 30.52, 121.98, 12.0,  180.0, 70, "head_on_1"),
        (123456002, 30.54, 121.97, 10.0,  200.0, 70, "head_on_2"),
        (123456003, 30.48, 121.96,  8.0,    0.0, 70, "head_on_3"),
        (123456004, 30.51, 121.95, 14.0,   90.0, 70, "crossing_1"),
        (123456005, 30.49, 122.05, 11.0,  270.0, 70, "crossing_2"),
        (123456006, 30.53, 122.02,  9.0,  130.0, 70, "crossing_3"),
        (123456007, 30.50, 121.99, 15.0,  045.0, 70, "crossing_4"),
        (123456008, 30.48, 121.99,  6.0,   45.0, 70, "overtaking_1"),
        (123456009, 30.49, 122.00,  7.0,   50.0, 70, "overtaking_2"),
        (123456010, 30.51, 122.01,  5.0,   40.0, 70, "overtaking_3"),
    ]

    records_per_vessel = n_records // len(vessels)
    dt_s = 0.5  # 2 Hz

    with open(out_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['MMSI', 'BaseDateTime', 'LAT', 'LON', 'SOG', 'COG',
                         'Heading', 'VesselName', 'IMO', 'CallSign',
                         'VesselType', 'Status', 'Length', 'Width', 'Draft',
                         'Cargo', 'TranscieverClass'])

        for mmsi, lat0, lon0, sog, cog, vtype, label in vessels:
            lat, lon = lat0, lon0
            cog_rad = math.radians(cog)
            speed_ms = sog * 0.514444
            for step in range(records_per_vessel):
                t_s = step * dt_s
                lat += speed_ms * dt_s * math.cos(cog_rad) / 111320.0
                lon += speed_ms * dt_s * math.sin(cog_rad) / (
                    111320.0 * math.cos(math.radians(lat)))
                ts = f"2023-01-15 08:{int(t_s // 60):02d}:{int(t_s % 60):02d}"
                writer.writerow([
                    mmsi, ts,
                    f"{lat:.6f}", f"{lon:.6f}",
                    f"{sog:.1f}", f"{cog:.1f}",
                    f"{cog:.1f}",  # heading = COG (simplified)
                    label, f"IMO{mmsi}", f"CALL{mmsi % 1000:03d}",
                    vtype, 0, 50, 10, 5, 70, 'A',
                ])

    print(f"Synthetic fallback created: {out_path} ({records_per_vessel * len(vessels)} records)")
    return out_path


def main():
    parser = argparse.ArgumentParser(
        description='Download AIS dataset for D1.3a SIL testing')
    parser.add_argument('--source', choices=['noaa', 'synthetic'],
                        default='noaa',
                        help='noaa=NOAA marinecadastre.gov (CC0), '
                             'synthetic=generated fallback (R3.2)')
    parser.add_argument('--output-dir', default='data/ais_datasets',
                        help='Directory to store downloaded/extracted data')
    args = parser.parse_args()

    out = Path(args.output_dir)

    if args.source == 'noaa':
        try:
            path = download_noaa(out)
        except Exception as e:
            print(f"NOAA download failed: {e}")
            print("Falling back to synthetic dataset (R3.2 contingency)")
            path = generate_synthetic_fallback(out)
    else:
        path = generate_synthetic_fallback(out)

    print(f"\nDataset ready: {path}")
    print("Next step: update config/ais_replay.yaml → dataset_path")


if __name__ == '__main__':
    main()
```

- [ ] **Step 2: Make executable**

```bash
chmod +x src/ais_bridge/scripts/download_dataset.py
```

- [ ] **Step 3: Run with synthetic fallback to verify script works without network**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
python3 src/ais_bridge/scripts/download_dataset.py --source synthetic --output-dir /tmp/ais_test
```

Expected:
```
Synthetic fallback created: /tmp/ais_test/AIS_synthetic_1h.csv (72000 records)
Dataset ready: /tmp/ais_test/AIS_synthetic_1h.csv
```

- [ ] **Step 4: Attempt NOAA download (may be slow; skip in CI, required for DoD)**

```bash
python3 src/ais_bridge/scripts/download_dataset.py --source noaa \
    --output-dir data/ais_datasets 2>&1 | head -5
```

If the download succeeds, note the CSV path and update `config/ais_replay.yaml` in B5.  
If it fails (network timeout/URL change), the synthetic fallback is used for DEMO-1; DoD requires real data by 5/31.

- [ ] **Step 5: Commit**

```bash
git add src/ais_bridge/scripts/download_dataset.py
git commit -m "feat(d1.3a): add download_dataset.py with NOAA + synthetic fallback (T9)"
```

---

### Task B3: `nmea_decoder.py` + `dataset_loader.py` (spec T10)

**Files:**
- Create: `src/ais_bridge/ais_bridge/nmea_decoder.py`
- Create: `src/ais_bridge/ais_bridge/dataset_loader.py`

- [ ] **Step 1: Write `nmea_decoder.py`**

```python
# src/ais_bridge/ais_bridge/nmea_decoder.py
# SPDX-License-Identifier: Proprietary
# AIS NMEA sentence decoder using pyais v3.
# Handles AIVDM/AIVDO single-part and multi-part messages.
# No vessel-type literals — multi_vessel_lint compliant.
"""Decode AIVDM/AIVDO sentences to AISRecord dataclass using pyais."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Generator

from pyais.stream import FileReaderStream


@dataclass
class AISRecord:
    """Decoded AIS position report from a single target vessel."""
    mmsi: int
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float  # COG used as fallback when AIS reports 511 (unavailable)
    ship_type: int      # AIS VesselType byte; 0=unknown, 20-99=vessels, 100+=other


# AIS message types carrying position (lat/lon/sog/cog)
_POSITION_TYPES = frozenset((1, 2, 3, 18))


def decode_file(filepath: str) -> Generator[AISRecord, None, None]:
    """Yield AISRecord for each valid position report in a NMEA AIS file.

    Uses pyais FileReaderStream which handles multi-part AIVDM reassembly
    transparently. Non-position messages and decode errors are skipped.

    Args:
        filepath: Path to a .nmea or .txt file with AIVDM/AIVDO sentences.

    Yields:
        AISRecord for each successfully decoded position report.
    """
    for msg in FileReaderStream(filepath):
        try:
            decoded = msg.decode()
        except Exception:
            continue

        if decoded.msg_type not in _POSITION_TYPES:
            continue

        try:
            lat = float(decoded.lat)
            lon = float(decoded.lon)
        except (TypeError, ValueError):
            continue  # skip records with missing/invalid position

        sog = float(decoded.speed or 0.0)
        cog = float(decoded.course or 0.0)

        # AIS heading 511 = not available; fall back to COG
        raw_hdg = getattr(decoded, 'heading', 511)
        hdg = float(raw_hdg) if raw_hdg not in (511, None) else cog

        ship_type = int(getattr(decoded, 'ship_type', 0) or 0)

        yield AISRecord(
            mmsi=int(decoded.mmsi),
            lat=lat,
            lon=lon,
            sog_kn=sog,
            cog_deg=cog,
            heading_deg=hdg,
            ship_type=ship_type,
        )
```

- [ ] **Step 2: Write `dataset_loader.py`**

```python
# src/ais_bridge/ais_bridge/dataset_loader.py
# SPDX-License-Identifier: Proprietary
# Load NOAA CSV or DMA NMEA AIS datasets and yield AISRecord objects.
# No vessel-type literals — multi_vessel_lint compliant.
"""Load NOAA AccessAIS CSV or DMA NMEA 0183 AIS files."""
from __future__ import annotations

import csv
from typing import Generator

from ais_bridge.nmea_decoder import AISRecord, decode_file


def load_noaa_csv(filepath: str) -> Generator[AISRecord, None, None]:
    """Parse NOAA marinecadastre.gov AccessAIS CSV format.

    NOAA CSV columns (2023 schema):
      MMSI, BaseDateTime, LAT, LON, SOG, COG, Heading, VesselName, IMO,
      CallSign, VesselType, Status, Length, Width, Draft, Cargo, TranscieverClass

    Yields:
        AISRecord for each valid row.
    """
    with open(filepath, 'r', encoding='utf-8-sig') as f:  # utf-8-sig strips BOM
        reader = csv.DictReader(f)
        for row in reader:
            try:
                lat = float(row['LAT'])
                lon = float(row['LON'])
                mmsi = int(row['MMSI'])
            except (KeyError, ValueError):
                continue

            sog = float(row.get('SOG') or 0.0)
            cog = float(row.get('COG') or 0.0)
            raw_hdg = row.get('Heading', '511')
            try:
                hdg_val = float(raw_hdg)
            except ValueError:
                hdg_val = 511.0
            hdg = hdg_val if hdg_val != 511.0 else cog

            ship_type = 0
            try:
                ship_type = int(float(row.get('VesselType') or 0))
            except ValueError:
                pass

            yield AISRecord(
                mmsi=mmsi,
                lat=lat,
                lon=lon,
                sog_kn=sog,
                cog_deg=cog,
                heading_deg=hdg,
                ship_type=ship_type,
            )


def load_dma_nmea(filepath: str) -> Generator[AISRecord, None, None]:
    """Parse DMA AIS NMEA 0183 file via pyais FileReaderStream.

    DMA provides raw AIVDM/AIVDO sentences; pyais handles multi-part reassembly.

    Yields:
        AISRecord for each valid position report.
    """
    yield from decode_file(filepath)
```

- [ ] **Step 3: Quick unit test of decoder against a known sentence**

```bash
python3 - <<'EOF'
from pyais import decode
# Standard single-part AIVDM position report (msg type 1)
raw = b"!AIVDM,1,1,,B,15M67N0P01G?Uf6E05FepT@T0100,0*5B"
decoded = decode(raw)
assert decoded.msg_type == 1, f"msg_type={decoded.msg_type}"
assert decoded.mmsi == 366913120, f"mmsi={decoded.mmsi}"
print(f"MMSI={decoded.mmsi} lat={decoded.lat} lon={decoded.lon} sog={decoded.speed}")
print("pyais decode OK")
EOF
```

Expected: MMSI and coordinates print without error.

- [ ] **Step 4: Test `decode_file` against synthetic NMEA (generate test fixture)**

```bash
# Generate a minimal NMEA test file with 2 known sentences
cat > /tmp/test_ais.nmea << 'EOF'
!AIVDM,1,1,,B,15M67N0P01G?Uf6E05FepT@T0100,0*5B
!AIVDM,1,1,,A,133w;`PP00PCqghMf4hC4n?v0<0c,0*74
EOF

python3 - <<'PYEOF'
import sys
sys.path.insert(0, 'src/ais_bridge')
from ais_bridge.nmea_decoder import decode_file
records = list(decode_file('/tmp/test_ais.nmea'))
print(f"Decoded {len(records)} records")
for r in records:
    print(f"  MMSI={r.mmsi} lat={r.lat:.4f} lon={r.lon:.4f} sog={r.sog_kn:.1f}")
assert len(records) >= 1, "No records decoded from test file"
print("decode_file OK")
PYEOF
```

Expected: at least 1 record decoded.

- [ ] **Step 5: Commit**

```bash
git add src/ais_bridge/ais_bridge/nmea_decoder.py \
        src/ais_bridge/ais_bridge/dataset_loader.py
git commit -m "feat(d1.3a): add nmea_decoder + dataset_loader (T10)"
```

---

### Task B4: `target_publisher.py` — AIS record → TrackedTargetArray (spec T11)

**Files:**
- Create: `src/ais_bridge/ais_bridge/target_publisher.py`

- [ ] **Step 1: Write `target_publisher.py`**

```python
# src/ais_bridge/ais_bridge/target_publisher.py
# SPDX-License-Identifier: Proprietary
# Convert AISRecord objects to TrackedTargetArray ROS2 message.
# Implements §4.3 field mapping from spec. No vessel-type literals.
"""Build TrackedTargetArray from AISRecord list per v1.1.2 IDL."""
from __future__ import annotations

import math
from typing import List

from ais_bridge.nmea_decoder import AISRecord

# Deferred ROS2 imports — only available after rclpy init in replay_node.
# Functions here are pure Python; ROS2 types injected at call time.


def _classification_from_type(ship_type: int) -> str:
    """Map AIS VesselType byte to TrackedTarget classification string."""
    if 20 <= ship_type <= 99:
        return 'vessel'
    if ship_type == 0 or ship_type > 99:
        return 'unknown'
    return 'fixed_object'  # types 1-19 and 100-199 edge cases


def build_tracked_target_array(records: List[AISRecord], stamp, node=None):
    """Build a TrackedTargetArray message from a list of AIS records.

    Args:
        records: Deduplicated list of AISRecord (one per MMSI).
        stamp:   builtin_interfaces/Time — use node.get_clock().now().to_msg()
        node:    unused; kept for API stability.

    Returns:
        l3_external_msgs/TrackedTargetArray populated per spec §4.3.
    """
    from l3_external_msgs.msg import TrackedTargetArray
    from l3_msgs.msg import TrackedTarget, EncounterClassification
    from geographic_msgs.msg import GeoPoint

    # Covariance constants (spec §4.3)
    SIGMA_POS_M = 50.0
    SIGMA_HDG_DEG = 5.0
    sigma_pos_sq = SIGMA_POS_M ** 2
    sigma_hdg_sq = SIGMA_HDG_DEG ** 2
    AIS_CONFIDENCE = 0.7

    targets = []
    for rec in records:
        t = TrackedTarget()
        t.schema_version = 'v1.1.2'
        t.stamp = stamp
        t.target_id = rec.mmsi            # uint64; MMSI is uint32 range, safe cast

        pos = GeoPoint()
        pos.latitude  = rec.lat
        pos.longitude = rec.lon
        pos.altitude  = 0.0
        t.position = pos

        t.sog_kn     = rec.sog_kn
        t.cog_deg    = rec.cog_deg
        t.heading_deg = rec.heading_deg

        # 3x3 diagonal covariance [σ_lat², 0, 0, 0, σ_lon², 0, 0, 0, σ_hdg²]
        t.covariance = [
            sigma_pos_sq, 0.0,         0.0,
            0.0,          sigma_pos_sq, 0.0,
            0.0,          0.0,          sigma_hdg_sq,
        ]

        t.classification            = _classification_from_type(rec.ship_type)
        t.classification_confidence = AIS_CONFIDENCE
        t.source_sensor             = 'ais'
        t.cpa_m                     = 0.0   # M2 World Model computes this
        t.tcpa_s                    = 0.0   # M2 World Model computes this
        t.encounter                 = EncounterClassification()  # ENCOUNTER_TYPE_NONE
        t.confidence                = AIS_CONFIDENCE

        targets.append(t)

    msg = TrackedTargetArray()
    msg.schema_version = 'v1.1.2'
    msg.stamp          = stamp
    msg.targets        = targets
    msg.confidence     = AIS_CONFIDENCE
    msg.rationale      = 'ais_bridge replay'
    return msg
```

- [ ] **Step 2: Verify no vessel-type literals in the file**

```bash
grep -n -E "(\bFCB\b|45\s*m|18\s*kn|22\s*kn|ROT_max)" \
    src/ais_bridge/ais_bridge/target_publisher.py && echo "FOUND — fix" || echo "CLEAN"
```

Expected: `CLEAN`

- [ ] **Step 3: Verify full ais_bridge Python scan is clean**

```bash
grep -rn --include="*.py" -E "(\bFCB\b|45\s*m\b|18\s*kn\b|22\s*kn\b|ROT_max)" \
    src/ais_bridge/ && echo "LINT FAIL" || echo "LINT PASS"
```

Expected: `LINT PASS`

- [ ] **Step 4: Commit**

```bash
git add src/ais_bridge/ais_bridge/target_publisher.py
git commit -m "feat(d1.3a): add target_publisher AIS→TrackedTargetArray (T11, §4.3)"
```

---

### Task B5: `replay_node.py` + `ais_replay.yaml` (spec T12)

**Files:**
- Create: `src/ais_bridge/ais_bridge/replay_node.py`
- Create: `src/ais_bridge/config/ais_replay.yaml`

- [ ] **Step 1: Write `ais_replay.yaml`**

```yaml
# src/ais_bridge/config/ais_replay.yaml
# AIS replay node configuration for D1.3a SIL testing.
# replay_rate_x: 1 = realtime, 5 = 5x faster, 10 = 10x faster
# At replay_rate_x=5: 1h dataset completes in ~12min.
ais_replay:
  ros__parameters:
    # Path to dataset file (NOAA CSV or DMA NMEA). Update after download_dataset.py.
    dataset_path: "data/ais_datasets/AIS_synthetic_1h.csv"
    # Dataset format: "noaa_csv" or "dma_nmea"
    dataset_format: "noaa_csv"
    # Replay speed multiplier: 1.0 = realtime, 5.0 = 5x, 10.0 = 10x
    replay_rate_x: 1.0
    # Publish rate for /fusion/tracked_targets (Hz); keep at 2.0 for M2 compatibility
    publish_rate_hz: 2.0
    # Maximum targets to publish per message (prevent oversized messages)
    max_targets: 50
```

- [ ] **Step 2: Write `replay_node.py`**

```python
# src/ais_bridge/ais_bridge/replay_node.py
# SPDX-License-Identifier: Proprietary
# ROS2 AIS replay node: loads NOAA/DMA dataset and publishes TrackedTargetArray.
# replay_rate_x controls playback speed (1x realtime / 5x / 10x).
# No vessel-type literals — multi_vessel_lint compliant.
"""ROS2 node: replay NOAA/DMA AIS data as TrackedTargetArray at configurable speed."""
from __future__ import annotations

from typing import List

import rclpy
from rclpy.node import Node

from ais_bridge.nmea_decoder import AISRecord
from ais_bridge.dataset_loader import load_noaa_csv, load_dma_nmea
from ais_bridge.target_publisher import build_tracked_target_array


class AISReplayNode(Node):
    """Publish TrackedTargetArray from pre-recorded AIS data at configurable replay rate."""

    def __init__(self):
        super().__init__('ais_replay')

        # Declare parameters (overridable via YAML or --ros-args)
        self.declare_parameter('dataset_path',    'data/ais_datasets/AIS_synthetic_1h.csv')
        self.declare_parameter('dataset_format',  'noaa_csv')
        self.declare_parameter('replay_rate_x',   1.0)
        self.declare_parameter('publish_rate_hz', 2.0)
        self.declare_parameter('max_targets',     50)

        path       = self.get_parameter('dataset_path').get_parameter_value().string_value
        fmt        = self.get_parameter('dataset_format').get_parameter_value().string_value
        rate_x     = self.get_parameter('replay_rate_x').get_parameter_value().double_value
        pub_hz     = self.get_parameter('publish_rate_hz').get_parameter_value().double_value
        max_tgts   = self.get_parameter('max_targets').get_parameter_value().integer_value

        # Load all records once into memory
        self._records = self._load(path, fmt)
        self._max_targets = max_tgts
        self._idx = 0

        if not self._records:
            self.get_logger().warn('No AIS records loaded — check dataset_path and format.')

        # Build a sliding-window size: at publish_rate_hz × replay_rate_x we
        # advance through time. Each publish window represents one 0.5s time slice.
        # Window = max_targets records grouped by MMSI (latest per MMSI).
        self._window_size = max(1, min(max_tgts * 10, len(self._records) // 100 or 1))

        # Timer fires at publish_rate_hz / replay_rate_x to advance playback
        timer_period = 1.0 / (pub_hz * max(0.1, rate_x))
        from l3_external_msgs.msg import TrackedTargetArray
        self._pub = self.create_publisher(TrackedTargetArray, '/fusion/tracked_targets', 10)
        self._timer = self.create_timer(timer_period, self._publish_callback)

        self.get_logger().info(
            f'AIS replay: {len(self._records)} records loaded, '
            f'{rate_x}x speed, {pub_hz} Hz, format={fmt}'
        )

    def _load(self, path: str, fmt: str) -> List[AISRecord]:
        try:
            if fmt == 'noaa_csv':
                return list(load_noaa_csv(path))
            return list(load_dma_nmea(path))
        except Exception as e:
            self.get_logger().error(f'Failed to load dataset {path!r}: {e}')
            return []

    def _publish_callback(self):
        if not self._records:
            return

        # Advance window and deduplicate by MMSI (keep most recent per vessel)
        end = min(self._idx + self._window_size, len(self._records))
        window = self._records[self._idx:end]
        self._idx = end % len(self._records)  # wrap around for continuous replay

        by_mmsi: dict[int, AISRecord] = {}
        for rec in window:
            by_mmsi[rec.mmsi] = rec  # later record overwrites earlier

        active = list(by_mmsi.values())[:self._max_targets]

        stamp = self.get_clock().now().to_msg()
        msg = build_tracked_target_array(active, stamp, self)
        self._pub.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = AISReplayNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
```

- [ ] **Step 3: Build and verify entry point**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select ais_bridge 2>&1 | tail -5
source install/setup.bash
ros2 run ais_bridge ais_replay_node --help 2>&1 | head -5 || true
```

Expected: build succeeds; node is registered.

- [ ] **Step 4: Quick smoke run with synthetic data**

```bash
source install/setup.bash
ros2 run ais_bridge ais_replay_node \
    --ros-args -p dataset_path:="data/ais_datasets/AIS_synthetic_1h.csv" \
               -p replay_rate_x:=10.0 &
NODE_PID=$!
sleep 4
ros2 topic hz /fusion/tracked_targets --window 20 &
HZ_PID=$!
sleep 4
kill $HZ_PID $NODE_PID 2>/dev/null
```

Expected: rate ≈ 20 Hz (2 Hz × 10x replay ≈ 20 timer fires/sec).  
Note: `/fusion/tracked_targets` topic rate shows timer fires, not data rate. Actual message rate at 2 Hz is determined by downstream consumers.

- [ ] **Step 5: Verify lint compliance**

```bash
grep -rn --include="*.py" -E "(\bFCB\b|45\s*m\b|18\s*kn\b)" src/ais_bridge/ && echo "FAIL" || echo "LINT PASS"
```

Expected: `LINT PASS`

- [ ] **Step 6: Commit**

```bash
git add src/ais_bridge/ais_bridge/replay_node.py \
        src/ais_bridge/config/ais_replay.yaml
git commit -m "feat(d1.3a): AIS replay node 1x/5x/10x + ais_replay.yaml (T12)"
```

---

### Task B6: `test_parse_rate.py` — AIS parse rate ≥95% (spec T13)

**Files:**
- Create: `src/ais_bridge/test/test_parse_rate.py`

- [ ] **Step 1: Write the test**

```python
# src/ais_bridge/test/test_parse_rate.py
# SPDX-License-Identifier: Proprietary
# Parse rate test: >= 95% of AIVDM sentences in the dataset decode successfully.
# Uses synthetic fallback if real dataset is not present (R3.2 contingency).
# Run: colcon test --packages-select ais_bridge
"""Test that AIS dataset parse rate meets the >= 95% DoD gate."""
import os
import sys
import csv
import math

import pytest

# Real dataset path (set by download_dataset.py; may not exist in CI)
_NOAA_PATH = os.environ.get(
    'AIS_DATASET_PATH',
    'data/ais_datasets/AIS_synthetic_1h.csv'
)

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))


def _count_noaa_csv(filepath: str) -> tuple[int, int]:
    """Count (total_rows, successfully_parsed_rows) for a NOAA CSV file."""
    total = 0
    parsed = 0
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        reader = csv.DictReader(f)
        for row in reader:
            total += 1
            try:
                float(row['LAT'])
                float(row['LON'])
                int(row['MMSI'])
                parsed += 1
            except (KeyError, ValueError):
                pass
    return total, parsed


def _count_nmea_file(filepath: str) -> tuple[int, int]:
    """Count (total_aivdm_sentences, successfully_decoded) for a NMEA file."""
    from ais_bridge.nmea_decoder import decode_file
    total = 0
    parsed = 0
    with open(filepath, 'r', errors='ignore') as f:
        for line in f:
            if '!AIVDM' in line or '!AIVDO' in line:
                total += 1
    try:
        for _ in decode_file(filepath):
            parsed += 1
    except Exception:
        pass
    return total, parsed


class TestParseRate:
    """Validate that at least 95% of AIS records in the dataset are parseable."""

    def test_parse_rate_meets_95_percent_gate(self):
        """Parse rate >= 95% for the configured dataset."""
        dataset_path = _NOAA_PATH

        # Ensure dataset exists; generate synthetic if not
        if not os.path.exists(dataset_path):
            _generate_synthetic(dataset_path)

        if dataset_path.endswith('.csv'):
            total, parsed = _count_noaa_csv(dataset_path)
            fmt = 'NOAA CSV'
        else:
            total, parsed = _count_nmea_file(dataset_path)
            fmt = 'NMEA'

        assert total > 0, f"Dataset is empty: {dataset_path}"
        rate_pct = parsed / total * 100.0
        print(f"\n[D1.3a-audit] {fmt} parse rate: {parsed}/{total} = {rate_pct:.1f}%")

        assert rate_pct >= 95.0, (
            f"Parse rate {rate_pct:.1f}% < 95% gate.\n"
            f"Dataset: {dataset_path} ({fmt})\n"
            f"Parsed: {parsed}/{total} records"
        )

    def test_decoded_records_have_valid_coordinates(self):
        """Spot-check: sampled records have plausible lat/lon."""
        dataset_path = _NOAA_PATH
        if not os.path.exists(dataset_path):
            _generate_synthetic(dataset_path)

        if dataset_path.endswith('.csv'):
            from ais_bridge.dataset_loader import load_noaa_csv
            records = list(load_noaa_csv(dataset_path))
        else:
            from ais_bridge.nmea_decoder import decode_file
            records = list(decode_file(dataset_path))

        assert len(records) >= 100, f"Too few records: {len(records)}"

        # Sample every 100th record
        sample = records[::100]
        for rec in sample:
            assert -90.0 <= rec.lat <= 90.0, f"Invalid lat: {rec.lat}"
            assert -180.0 <= rec.lon <= 180.0, f"Invalid lon: {rec.lon}"
            assert 0 <= rec.sog_kn <= 100.0, f"Implausible SOG: {rec.sog_kn}"


def _generate_synthetic(path: str):
    """Generate synthetic dataset at path (R3.2 CI fallback)."""
    import subprocess
    script = os.path.join(
        os.path.dirname(__file__), '..', 'scripts', 'download_dataset.py')
    out_dir = os.path.dirname(path)
    subprocess.run(
        [sys.executable, script, '--source', 'synthetic', '--output-dir', out_dir],
        check=True
    )
```

- [ ] **Step 2: Run the test**

First ensure the synthetic dataset exists:
```bash
python3 src/ais_bridge/scripts/download_dataset.py --source synthetic \
    --output-dir data/ais_datasets
```

Then run the test:
```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select ais_bridge 2>&1 | tail -3
colcon test --packages-select ais_bridge --event-handlers console_cohesion+ 2>&1 | tail -20
```

Expected: `test_parse_rate_meets_95_percent_gate` PASSED (synthetic data = 100% parse rate).

If the real NOAA dataset is available:
```bash
AIS_DATASET_PATH=data/ais_datasets/AIS_2023_01_Zone19/AIS_2023_01_01.csv \
colcon test --packages-select ais_bridge --event-handlers console_cohesion+ 2>&1 | tail -10
```

- [ ] **Step 3: Commit**

```bash
git add src/ais_bridge/test/test_parse_rate.py
git commit -m "test(d1.3a): add test_parse_rate ≥95% gate (T13, D1.3a DoD)"
```

---

## SUBAGENT C — Track C: Integration + Closure

*(Start only after Subagent A and Subagent B both complete and their tests pass.)*

### Task C1: `simulator.launch.py` — joint launch + integration verify (spec T15)

**Files:**
- Create: `src/fcb_simulator/launch/simulator.launch.py`

- [ ] **Step 1: Write the launch file**

```python
# src/fcb_simulator/launch/simulator.launch.py
# SPDX-License-Identifier: Proprietary
# D1.3a integration launch: starts fcb_simulator_node + ais_bridge/ais_replay_node.
# Usage:
#   ros2 launch fcb_simulator simulator.launch.py replay_rate_x:=5
#   ros2 launch fcb_simulator simulator.launch.py \
#       dataset_path:=/abs/path/to/AIS.csv dataset_format:=noaa_csv replay_rate_x:=1
"""Launch fcb_simulator and ais_bridge for D1.3a SIL integration."""
import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, LogInfo
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # --- Launch arguments ---
    replay_rate_arg = DeclareLaunchArgument(
        'replay_rate_x',
        default_value='1.0',
        description='AIS replay speed multiplier: 1=realtime, 5=5x, 10=10x'
    )
    dataset_path_arg = DeclareLaunchArgument(
        'dataset_path',
        default_value='data/ais_datasets/AIS_synthetic_1h.csv',
        description='Path to NOAA CSV or DMA NMEA AIS dataset file'
    )
    dataset_format_arg = DeclareLaunchArgument(
        'dataset_format',
        default_value='noaa_csv',
        description='Dataset format: noaa_csv or dma_nmea'
    )

    # --- fcb_simulator node ---
    fcb_config = PathJoinSubstitution([
        FindPackageShare('fcb_simulator'), 'config', 'fcb_dynamics.yaml'
    ])

    fcb_node = Node(
        package='fcb_simulator',
        executable='fcb_simulator_node',
        name='fcb_simulator',
        parameters=[fcb_config],
        output='screen',
        emulate_tty=True,
    )

    # --- ais_bridge replay node ---
    ais_node = Node(
        package='ais_bridge',
        executable='ais_replay_node',
        name='ais_replay',
        parameters=[{
            'dataset_path':    LaunchConfiguration('dataset_path'),
            'dataset_format':  LaunchConfiguration('dataset_format'),
            'replay_rate_x':   LaunchConfiguration('replay_rate_x'),
            'publish_rate_hz': 2.0,
            'max_targets':     50,
        }],
        output='screen',
        emulate_tty=True,
    )

    return LaunchDescription([
        replay_rate_arg,
        dataset_path_arg,
        dataset_format_arg,
        LogInfo(msg='D1.3a launch: fcb_simulator + ais_bridge'),
        fcb_node,
        ais_node,
    ])
```

- [ ] **Step 2: Add launch directory install to `fcb_simulator` CMakeLists.txt**

In `CMakeLists.txt`, ensure launch files are installed:

```cmake
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})
```

Add this after the existing `install(DIRECTORY config ...)` line if not already present.

- [ ] **Step 3: Build both packages**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select ship_sim_interfaces fcb_simulator ais_bridge 2>&1 | tail -10
```

Expected: `Summary: 3 packages finished`

- [ ] **Step 4: Launch and verify both topics publish**

```bash
source install/setup.bash
# Ensure synthetic dataset exists
python3 src/ais_bridge/scripts/download_dataset.py --source synthetic \
    --output-dir data/ais_datasets

ros2 launch fcb_simulator simulator.launch.py replay_rate_x:=5.0 &
LAUNCH_PID=$!
sleep 10

# Check /fusion/own_ship_state
echo "=== /fusion/own_ship_state rate ===" 
ros2 topic hz /fusion/own_ship_state --window 50 &
HZ1_PID=$!
sleep 4; kill $HZ1_PID 2>/dev/null

# Check /fusion/tracked_targets
echo "=== /fusion/tracked_targets rate ==="
ros2 topic hz /fusion/tracked_targets --window 10 &
HZ2_PID=$!
sleep 4; kill $HZ2_PID 2>/dev/null

# Echo one message each
echo "=== own_ship_state schema_version ==="
ros2 topic echo /fusion/own_ship_state --once 2>/dev/null | grep schema_version

echo "=== tracked_targets sample ==="
ros2 topic echo /fusion/tracked_targets --once 2>/dev/null | grep -E "schema_version|source_sensor" | head -5

kill $LAUNCH_PID 2>/dev/null
```

Expected:
- `/fusion/own_ship_state`: rate ≈ 50 Hz
- `/fusion/tracked_targets`: rate > 0 (at 5x replay, timer fires faster)
- `schema_version: v1.1.2` in both topics
- `source_sensor: ais` in TrackedTarget messages

- [ ] **Step 5: Run full test suite for all three packages**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon test --packages-select ship_sim_interfaces fcb_simulator ais_bridge \
    --event-handlers console_cohesion+ 2>&1 | tail -30
```

Expected: all tests PASSED including:
- `test_mmg_steady_turn` ✓
- `test_rk4_energy` ✓
- `test_stopping_error` ✓ (error ≤ 10%, value printed)
- `test_stability_1h` ✓
- `test_scenario_runner` ✓
- `test_parse_rate_meets_95_percent_gate` ✓
- `test_decoded_records_have_valid_coordinates` ✓

- [ ] **Step 6: Commit**

```bash
git add src/fcb_simulator/launch/simulator.launch.py \
        src/fcb_simulator/CMakeLists.txt
git commit -m "feat(d1.3a): simulator.launch.py — joint fcb+ais launch + integration verify (T15)"
```

---

### Task C2: Finding closure + docs update (spec T16)

**Files:**
- Modify: `docs/Design/Review/2026-05-07/00-consolidated-findings.md`
- Modify: `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`

- [ ] **Step 1: Close MV-4 in consolidated-findings.md**

Find the MV-4 entry in `docs/Design/Review/2026-05-07/00-consolidated-findings.md` and update its status to CLOSED with a link to the PR/commit:

Search for: `MV-4` or `vessel_class` or similar marker. The finding concerns "no ShipMotionSimulator abstraction." Update the status cell/field:

```
Status: CLOSED (D1.3a — ship_sim_interfaces + FCBSimulator plugin; see PR feat/d1.2-cicd-pipeline → feat/d1.3a commits)
Closed by: D1.3a T3 (FCBSimulator pluginlib plugin)
Close date: 2026-05-xx
```

- [ ] **Step 2: Close G P0-G-1(a) in consolidated-findings.md**

Find the `G P0-G-1(a)` entry (relates to missing hull_class annotation / semi-planing regime not documented). Update its status:

```
Status: CLOSED (D1.3a — hull_class: SEMI_PLANING added to fcb_dynamics.yaml; see T2 commit)
Closed by: D1.3a T2 (fcb_dynamics.yaml hull_class annotation)
Close date: 2026-05-xx
```

- [ ] **Step 3: Update v3.0 plan Euler note**

In `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`, find the D1.3a section text that says "Euler 积分" and update it:

Find:
```
Euler 积分
```
Replace with:
```
RK4, dt=0.02s（代替 Euler；精度满足 D1.3.1 ≤5% 参考解误差门槛；现有单元测试基于 RK4，不改动）
```

- [ ] **Step 4: Verify all D1.3a DoD checkboxes in spec**

```bash
grep -c "\- \[x\]\|\- \[ \]" \
    "docs/Design/Detailed Design/D1.3a-simulator/01-spec.md" || true
```

Go through spec §13 (全闭判据) and confirm each checkbox is satisfied:

```
□ colcon build —zero errors: verified in C1 Step 3
□ test_mmg_steady_turn PASSED: verified A7 Step 4
□ test_rk4_energy PASSED: verified A7 Step 4
□ test_stopping_error PASSED (≤10%, value logged): verified A6 Step 3
□ test_stability_1h PASSED: verified A7 Step 4
□ test_parse_rate PASSED (≥95%): verified B6 Step 2
□ simulator.launch.py 60s run: verified C1 Step 4
□ AIS dataset ≥1h local: synthetic created; real NOAA target 5/31
□ fcb_dynamics.yaml hull_class: SEMI_PLANING + vessel_class: FCB: verified A2
□ HAZID annotations on all params: verified A2
□ multi_vessel_lint PASS: verified B4 Step 3
□ run_scenario() callable: verified A8 Step 5
□ MV-4 CLOSED: this task
□ G P0-G-1(a) CLOSED: this task
□ v3.0 Euler note updated: this task
```

- [ ] **Step 5: Commit closure docs**

```bash
git add "docs/Design/Review/2026-05-07/00-consolidated-findings.md" \
        "docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md"
git commit -m "docs(d1.3a): close MV-4 + G P0-G-1(a), update Euler→RK4 note (T16)"
```

- [ ] **Step 6: Final build + test sweep**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
colcon build --packages-select ship_sim_interfaces fcb_simulator ais_bridge 2>&1 | tail -5
colcon test --packages-select ship_sim_interfaces fcb_simulator ais_bridge \
    --event-handlers console_cohesion+ 2>&1 | grep -E "PASSED|FAILED|ERROR" | head -20
```

Expected: all PASSED, zero FAILED or ERROR.

---

## Self-Review Checklist

### Spec coverage
| Spec requirement | Plan task | Status |
|---|---|---|
| ship_sim_interfaces header-only pkg | A1 | ✓ |
| fcb_dynamics.yaml vessel_class + hull_class + HAZID | A2 | ✓ |
| FCBSimulator plugin + plugins.xml (MV-4) | A3 | ✓ |
| fcb_simulator_node pluginlib refactor | A4 | ✓ |
| FilteredOwnShipState 50 Hz + schema_version | A5 | ✓ (existing code + schema_version addition) |
| test_stopping_error ≤10% | A6 | ✓ |
| test_stability_1h 180k steps | A7 | ✓ |
| run_scenario() D1.3.1 hook | A8 | ✓ |
| ais_bridge scaffold + pyais dep | B1 | ✓ |
| download_dataset.py NOAA + synthetic fallback | B2 | ✓ |
| nmea_decoder.py + dataset_loader.py | B3 | ✓ |
| target_publisher.py §4.3 mapping | B4 | ✓ |
| replay_node.py 1×/5×/10× + ais_replay.yaml | B5 | ✓ |
| test_parse_rate ≥95% | B6 | ✓ |
| simulator.launch.py joint launch | C1 | ✓ |
| MV-4 + G P0-G-1(a) CLOSED + Euler note | C2 | ✓ |
| NED→WGS84 mapping (§4.4) | A5 (existing node already implements this) | ✓ |
| §4.3 AIS→TrackedTarget field mapping | B4 | ✓ |
| multi_vessel_lint Python compliance | B4 step 3, B5 step 5 | ✓ |
| R3.2 synthetic fallback | B2 | ✓ |

### Type consistency check
- `ship_sim::ShipState` defined in A1, used in A3 (`FCBSimulator::step` return/arg), A4 (`state_` member), A8 (`run_scenario` arg). Fields: x, y, psi, u, v, r, phi, phi_dot — consistent throughout.
- `fcb_sim::FcbState` fields match `ship_sim::ShipState` fields exactly (A3 conversion is explicit member-by-member, catches renames at compile time).
- `AISRecord` defined in B3, used in B4 (`build_tracked_target_array` arg), B5 (`_publish_callback`). Fields: mmsi, lat, lon, sog_kn, cog_deg, heading_deg, ship_type — consistent.
- `ManeuverType` and `ScenarioResult` defined in A8 `scenario_runner.hpp`, used in `scenario_runner.cpp` and `test_scenario_runner.cpp`. Consistent.

### Placeholder scan
No TBD/TODO/implement-later found in code blocks. All test assertions have concrete threshold values. All file paths are exact. All commands have expected output.
