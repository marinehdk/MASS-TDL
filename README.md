# MASS ADAS L3 Tactical Decision Layer

Colcon-based ROS2 workspace for the L3 tactical decision layer of the MASS
(Maritime Autonomous Surface Ship) ADAS system.

## Overview

This workspace contains the implementation of the L3 Tactical Decision Layer
(TDL), which handles COLREGs-compliant real-time decision-making at the
seconds-to-minutes timescale. It is part of a larger MASS architecture spanning
L1 (Mission) through L5 (Control) layers.

## Repository Structure

```
src/                     -- ROS2 packages (colcon workspace)
  l3_msgs/               -- Internal message definitions (Task 2)
  l3_external_msgs/      -- Cross-layer message definitions (Task 3)
  m1_odd_envelope_manager/
  m2_world_model/
  m3_mission_manager/
  m4_behavior_arbiter/
  m5_tactical_planner/
  m6_colregs_reasoner/
  m7_safety_supervisor/
  m8_hmi_transparency_bridge/
tools/                   -- Build support, CI, Docker, third-party deps
docs/                    -- Architecture and design documentation
```

## Design Documentation

All architecture and design documentation lives in `docs/`. See
`docs/Design/Architecture Design/README.md` for the index.

The authoritative architecture document is:
`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` (v1.1.2).

## Build Prerequisites

| Tool        | Minimum Version |
|-------------|-----------------|
| ROS2        | Jazzy           |
| GCC         | 14.3            |
| colcon      | 0.20.1          |
| CMake       | 3.22+           |
| Python      | 3.12+           |

## Quick Start

```bash
# Install system dependencies (Ubuntu 24.04)
sudo apt install ros-jazzy-desktop g++-14 cmake python3-colcon-common-extensions

# Set up ROS2 environment
source /opt/ros/jazzy/setup.bash

# Build the workspace
colcon build

# Source the overlay
source install/setup.bash
```

## License

Proprietary. MASS ADAS project.
