# MASS ADAS L3 Tactical Decision Layer

Colcon-based ROS2 workspace for the L3 tactical decision layer of the MASS
(Maritime Autonomous Surface Ship) ADAS system.

## Overview

This workspace implements the **L3 Tactical Decision Layer (TDL)** which handles
COLREGs-compliant real-time decision-making at the seconds-to-minutes timescale.
It is part of a larger MASS architecture spanning L1 (Mission) through L5 (Control)
layers, with cross-cutting Multimodal Fusion, X-axis Deterministic Checker,
Y-axis Reflex Arc, and Z-bottom Hardware Override + ASDR.

The L3 TDL contains 8 modules organized in 3 responsibility tiers:

- **Envelope Control**: M1 ODD/Envelope Manager
- **Decision & Planning**: M2 World Model, M3 Mission Manager, M4 Behavior
  Arbiter, M5 Tactical Planner (Mid-MPC + BC-MPC), M6 COLREGs Reasoner
- **Safety & HMI**: M7 Safety Supervisor (independent path), M8 HMI/Transparency
  Bridge

## Repository Structure

```
src/                              ROS2 packages (colcon workspace)
  l3_msgs/                        Internal L3 message definitions (v1.1.2 §15.1)
  l3_external_msgs/               Cross-layer message definitions
  l3_external_mock_publisher/     Python mock publishers for cross-layer nodes
  m{1-8}_*/                       8 module packages (Wave 1 implementation)
tools/
  ci/                             CI scripts (5 check scripts)
  docker/                         Dockerfile.dev / Dockerfile.ci / compose.yml
  third-party/                    Vendored libraries (git submodules)
docs/
  Design/                         Architecture, detailed design, RFCs, HAZID
  Implementation/                 Wave 0 + Wave 1 spec files
```

## Design Documentation

The authoritative architecture document is
[`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`](docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_%E6%9E%B6%E6%9E%84%E8%AE%BE%E8%AE%A1%E6%8A%A5%E5%91%8A.md)
(v1.1.2 — interfaces locked across teams).

Key implementation specs:
- [Implementation Master Plan](docs/Implementation/00-implementation-master-plan.md)
- [Coding Standards (MISRA C++:2023)](docs/Implementation/coding-standards.md)
- [Static Analysis Policy](docs/Implementation/static-analysis-policy.md)
- [CI/CD Pipeline](docs/Implementation/ci-cd-pipeline.md)
- [ROS2 IDL Implementation Guide](docs/Implementation/ros2-idl-implementation-guide.md)
- [Third-Party Library Policy](docs/Implementation/third-party-library-policy.md)

## Build Prerequisites

| Tool         | Locked Version  | Notes |
|--------------|-----------------|-------|
| OS (CI base) | Ubuntu 22.04 (jammy) | ROS2 Jazzy 兼容 |
| ROS2         | Jazzy Jalisco   | EOL 2029-05-31 |
| GCC          | 14.3 LTS        | C++17 baseline; Clang 20.1.8 多样化路径 |
| CMake        | 3.22+           | Ubuntu 22.04 自带 |
| colcon       | 0.20.1          | Locked in CI image |
| Python       | 3.10            | M2 ENC 工具 + M8 后端；不允许 3.11+ |
| clang-tidy   | 20.1.8          | MISRA C++:2023 部分规则 |
| cppcheck     | OSS 2.20.0 / Premium 26.1.0 | Premium 提供 MISRA C++:2023 完整规则 |

## Quick Start

### Option A: Use the dev Docker image (recommended)

```bash
# Build and enter dev container (auto sources /opt/ros/jazzy/setup.bash)
docker build -t mass-l3/dev:jazzy-ubuntu22.04 -f tools/docker/Dockerfile.dev .
docker run --rm -it -v "$(pwd):/workspace" mass-l3/dev:jazzy-ubuntu22.04

# Inside container: init submodules + build
git submodule update --init --recursive
colcon build
source install/setup.bash
```

### Option B: Local Ubuntu 22.04

```bash
# Install system dependencies (Ubuntu 22.04 jammy)
sudo apt install ros-jazzy-desktop g++-14 \
                 clang-20 clang-tidy-20 clang-format-20 \
                 cmake ninja-build ccache \
                 python3-colcon-common-extensions \
                 lcov cppcheck

# Set up ROS2 environment
source /opt/ros/jazzy/setup.bash

# Initialize third-party submodules (one-time, large download ~2 GB)
git submodule update --init --recursive

# Build the workspace
colcon build

# Source the overlay
source install/setup.bash

# Run unit tests
colcon test
```

## Development Status

| Phase | Status | Reference |
|---|---|---|
| Architecture Design (v1.1.2) | ✅ Complete | `docs/Design/Architecture Design/` |
| Detailed Design (M1–M8) | ✅ Complete | `docs/Design/Detailed Design/` |
| Cross-Team RFCs (6 total) | ✅ Approved | `docs/Design/Cross-Team Alignment/RFC-decisions.md` |
| HAZID RUN-001 | ⏳ In progress (5/13–8/19) | `docs/Design/HAZID/RUN-001-kickoff.md` |
| Wave 0 Infrastructure | ⏳ Under review | `docs/Implementation/00-implementation-master-plan.md` §8.1 |
| Wave 1 Module Implementation | ⏳ Started (M1, M2 worktrees) | `docs/Implementation/M{1-8}/code-skeleton-spec.md` |
| FCB Sea Trial | 📅 2026-06 (≥ 50 nm + ≥ 100 h) | — |
| CCS i-Ship AIP Application | 📅 2026-08–09 | `docs/Certification/` (planned) |

## License

Proprietary. MASS ADAS project, SANGO Group.
