# D1.3c · OSP libcosim FMI Bridge + dds-fmu Mediator · 执行 Spec

**版本**：v1.0
**日期**：2026-05-11
**v3.1 D 编号**：D1.3c
**Owner**：**V&V 工程师 FTE**（全职 4w）+ 安全工程师外包（临时支援 1w）+ 架构师（设计审）
**完成日期**：Phase 1 6/15（DEMO-1）· Phase 2 7/15（DEMO-2）
**工时预算**：12-16 人周（Phase 1 ~3 周 · Phase 2 ~4 周）
**关闭 finding**：SIL P0 SIL-1 · V&V P0 E1 SIL latency budget

---

## §0 设计决策记录（brainstorming 锁定，D1.3c 内不再议）

| 决策 | 内容 | Rationale |
|---|---|---|
| **ROS2 发行版** | **Humble + Ubuntu 22.04 + Python 3.11**（容器内升 3.11）。修正现有 Dockerfile.ci（当前为 Jazzy + 24.04 + 3.12） | SIL 决策记录 §3 锁定；实船 OT 部署约束 FCB onboard Ubuntu 22.04 + PREEMPT_RT；工具链稳定性（libcosim/dds-fmu 已验证 Humble 兼容）。Dockerfile.ci 修正列入 Phase 1 T6 |
| **FMU 打包 Phase 1** | **pythonfmu** — pybind11 绑定 FCBSimulator::step()，pythonfmu 打包 `.fmu` | Phase 1 目标 DEMO-1 展示旋回参考解 + latency 测试，pythonfmu 开发速度优先。Phase 2 切换 C++ FMI wrapper 入认证证据（混合策略 C） |
| **FMU 打包 Phase 2** | C++ FMI Library（QTronic/jmodelica 或 Reference FMUs）— 纯 C++ shared lib + modelDescription.xml | 零 Python 依赖，认证路径干净。Phase 2 按 DEMO-2 时间窗切换 |
| **M7 FMI 边界验证** | **CI lint 静态检查** — `check-rl-isolation.sh` 风格 grep，确保 `src/l3_tdl_kernel/m7_safety_supervisor/` 不 import/link `libcosim` / `fmi_bridge` | 架构级编译时保证；M7 是 ROS2 native 节点不经过 FMI 调度循环，源码层不 link libcosim 即可保证。运行时检查多余 |
| **Phase 1 范围** | 单 FMU（MMG own-ship）+ dds-fmu 桥接 + latency 验证 + Dockerfile 升级 + tool confidence 报告。Multi-FMU/ncdm_vessel 留 Phase 2 | D1.3c v3.1 §0.4 闭环路径 #3：Phase 1 仅 own-ship FMU，target ships + L1 mock 仍 ROS2 native |

---

## §1 现有基线审计

### 1.1 可复用工件

| 工件 | 路径 | 能力 | D1.3c 消费方式 |
|---|---|---|---|
| `ShipMotionSimulator` 抽象 | `src/sim_workbench/ship_sim_interfaces/include/.../ship_motion_simulator.hpp` | 纯虚类 + `export_fmu_interface()` 签名 + `FmuInterfaceSpec`/`FmuVariableSpec` struct | D1.3c 读取 `export_fmu_interface()` 返回值生成 modelDescription.xml + dds-fmu mapping |
| `FCBSimulator` plugin | `src/sim_workbench/fcb_simulator/src/fcb_simulator_plugin.cpp` | MMG 4-DOF RK4 积分器 + 22 变量 FMU spec | pybind11 绑定 → pythonfmu 打包。22 变量清单：11 outputs + 2 control inputs + 4 disturbance inputs + 5 parameters |
| CI colcon pipeline | `.gitlab-ci.yml` | stage: lint / unit / static / integration / release | D1.3c 追加 `fmi_bridge` 到 build matrix；现有 39+ tests 零回归 |
| RL 隔离 lint | `tools/ci/check-rl-isolation.sh` | 跨组 import 检测 | D1.3c 追加 M7-FMI 边界检查规则 |
| `fcb_dynamics.yaml` | `src/sim_workbench/fcb_simulator/config/` | FCB 45m 参数（HAZID-UNVERIFIED 标注） | FMU 打包时读取作 modelDescription 中 parameter start values |

### 1.2 须修正项

| 工件 | 当前状态 | 修正 |
|---|---|---|
| `Dockerfile.ci` | ROS2 Jazzy + Ubuntu 24.04 Noble + Python 3.12 | 回退到 Humble + 22.04 + deadsnakes Python 3.11 |
| `pytest.ini` | `pythonpath` 含 `src/l3_tdl_kernel` `src/sim_workbench` | 追加 `src/sim_workbench/fmi_bridge` |
| `.gitlab-ci.yml` | stage-2-unit-test matrix 无 fmi_bridge | 追加 `fmi_bridge` 行 |

### 1.3 环境确认

```
目标环境：  Ubuntu 22.04 + ROS2 Humble + Python 3.11 + PREEMPT_RT
DNV 工具链： libcosim (C++ MPL-2.0) + farn (MIT) + ospx (MIT) — pip install
FMU 工具链： pythonfmu (MIT, pip) + fmi-library (BSD, cmake FetchContent)
绑定层：    pybind11 (BSD, ROS2 Humble apt: python3-pybind11)
```

---

## §2 架构设计

### 2.1 FMI 边界拓扑

```
┌─ SIL CORE (Production ROS2 Humble, C++/MISRA, certified) ─────────────┐
│                                                                        │
│  [M1 ODD] [M2 World] [M3 Mission] [M4 Arbiter] [M6 COLREGs]          │
│       │        │           │            │             │                │
│       └────────┴───────────┴────────────┴─────────────┘                │
│                          │                                             │
│                   [M5 Tactical Planner]                                │
│                    Mid-MPC + BC-MPC                                    │
│                          │                                             │
│                          │ AvoidancePlanMsg @ 1-2 Hz                   │
│                          │ ReactiveOverrideCmd (event)                 │
│                          ▼                                             │
│  ┌──────────────────────────────────────────────────────────────┐     │
│  │  [M7 Safety Supervisor]  ← ROS2 NATIVE ONLY, NEVER VIA FMI   │     │
│  │  KPI < 10 ms end-to-end                                      │     │
│  │  CI lint enforces: zero libcosim/fmi_bridge import from M7   │     │
│  └──────────────────────────────────────────────────────────────┘     │
│                          │                                             │
└──────────────────────────┼─────────────────────────────────────────────┘
                           │ /cmd/setpoint (ψ_cmd, u_cmd)
                           ▼
┌─ FMI 2.0 / OSP libcosim BOUNDARY ────────────────────────────────────┐
│                                                                       │
│  ┌──────────────────────────────┐                                    │
│  │  dds-fmu mediator            │  DDS ↔ FMU variable mapping         │
│  │  /cmd/rudder → delta_cmd     │  YAML-configured signal routes      │
│  │  /cmd/thrust  → n_rps_cmd    │                                    │
│  │  /ship/state  ← u,v,r,x,y,ψ  │                                    │
│  └──────────────┬───────────────┘                                    │
│                 │ FMI 2.0 variable exchange                           │
│                 ▼                                                     │
│  ┌──────────────────────────────┐                                    │
│  │  libcosim co-simulation      │  fixed-step master (dt=0.02s)       │
│  │  async_slave: dds-fmu adapter│  synchronizes with ROS2 clock       │
│  └──────────────┬───────────────┘                                    │
│                 │                                                     │
│                 ▼                                                     │
│  ┌──────────────────────────────────────────────────────────────┐    │
│  │  fcb_mmg_4dof.fmu (FMI 2.0 Co-Simulation)                    │    │
│  │  Model: FCBShipDynamics (Yasukawa 2015 4-DOF MMG, RK4)       │    │
│  │  Inputs:  delta_cmd [rad], n_rps_cmd [rev/s],                │    │
│  │           wind_dir_deg, wind_speed_mps,                       │    │
│  │           current_dir_deg, current_speed_mps                   │    │
│  │  Outputs: u,v,r,x,y,psi,phi,p,delta,n,sog                     │    │
│  │  Parameters: L,B,d,m,Izz                                       │    │
│  └──────────────────────────────────────────────────────────────┘    │
│                                                                       │
│  Phase 1: own-ship only (1 FMU)                                       │
│  Phase 2: + ncdm_vessel.fmu (D2.4) + disturbance_field.fmu (D2.5)   │
└───────────────────────────────────────────────────────────────────────┘
```

### 2.2 关键边界规则

| 规则 | 实施 |
|---|---|
| **M7 不过 FMI** | CI lint: `grep -rn 'libcosim\|fmi_bridge' src/l3_tdl_kernel/m7_safety_supervisor/` → 命中即 fail |
| **2-10ms exchange budget** | dds-fmu 单次 exchange P95 ≤ 10ms（Phase 1 `test_dds_fmu_latency.py` 实测验证） |
| **Phase 1 单 FMU** | 仅 own-ship MMG FMU；target ships 仍 ROS2 native topic（D1.3a 的 target_publisher.py） |
| **RL 隔离** | `src/rl_workbench/` 不进 colcon 主工作区；D1.3c 不触碰 rl_workbench |

---

## §3 包结构

```
src/sim_workbench/fmi_bridge/                     ← NEW colcon 包
├── package.xml                                    # ament_cmake + pybind11 + libcosim
├── CMakeLists.txt
├── include/fmi_bridge/
│   ├── libcosim_wrapper.hpp                       # libcosim async_slave C++ wrapper
│   ├── dds_fmu_bridge.hpp                         # DDS↔FMU signal mapping engine
│   └── fmu_utils.hpp                              # modelDescription.xml parser + validator
├── src/
│   ├── libcosim_wrapper.cpp
│   ├── dds_fmu_bridge.cpp
│   ├── dds_fmu_node.cpp                           # ROS2 node entry point
│   └── fmu_utils.cpp
├── python/
│   ├── fcb_mmg_fmu.py                             # pythonfmu: FCBSimulator → .fmu
│   └── build_fmu.py                               # CLI: generate .fmu + modelDescription.xml
├── config/
│   └── dds_fmu_mapping.yaml                       # DDS topic ↔ FMU variable signal map
├── launch/
│   └── fmi_bridge.launch.py
└── test/
    ├── test_fmu_turning_circle.py                 # 旋回机动参考解验证
    ├── test_dds_fmu_latency.py                    # DDS roundtrip latency < 10ms P95
    ├── test_fmu_model_description.py              # modelDescription.xml schema 验证
    └── test_m7_fmi_isolation.py                   # CI lint: M7 不 link libcosim

tools/ci/
└── check-m7-fmi-isolation.sh                     # NEW CI lint script (M7 FMI boundary)
```

**不改动文件**（回归边界）：
- `src/sim_workbench/ship_sim_interfaces/` — 接口签名已在 D1.3a v3.1 补丁冻结
- `src/sim_workbench/fcb_simulator/` — 零修改，仅通过 pybind11 绑定消费
- `src/l3_tdl_kernel/` — 全部零修改

---

## §4 pythonfmu FMU 打包

### 4.1 策略

Phase 1（5/25-6/15）：pythonfmu 快速验证链路。
Phase 2（6/16-7/15）：切换到 C++ FMI Library 纯 C++ wrapper，进认证证据链。

### 4.2 pybind11 绑定（消费 D1.3a FCBSimulator）

```cpp
// fmi_bridge/python/fcb_mmg_bindings.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "fcb_simulator/fcb_simulator_plugin.hpp"
#include "ship_sim_interfaces/ship_motion_simulator.hpp"

namespace py = pybind11;

PYBIND11_MODULE(fcb_mmg_py, m) {
    py::class_<ship_sim::ShipState>(m, "ShipState")
        .def(py::init<>())
        .def_readwrite("x", &ship_sim::ShipState::x)
        .def_readwrite("y", &ship_sim::ShipState::y)
        .def_readwrite("psi", &ship_sim::ShipState::psi)
        .def_readwrite("u", &ship_sim::ShipState::u)
        .def_readwrite("v", &ship_sim::ShipState::v)
        .def_readwrite("r", &ship_sim::ShipState::r)
        .def_readwrite("phi", &ship_sim::ShipState::phi)
        .def_readwrite("phi_dot", &ship_sim::ShipState::phi_dot);

    py::class_<fcb_sim::FCBSimulator>(m, "FCBSimulator")
        .def(py::init<>())
        .def("load_params", &fcb_sim::FCBSimulator::load_params)
        .def("step", &fcb_sim::FCBSimulator::step)
        .def("export_fmu_interface", &fcb_sim::FCBSimulator::export_fmu_interface);
}
```

### 4.3 pythonfmu 模型

```python
# fmi_bridge/python/fcb_mmg_fmu.py
from pythonfmu import Fmi2Slave, Fmi2Causality, Fmi2Variability, Real, Integer
from fcb_mmg_py import FCBSimulator, ShipState


class FCBMmgFmu(Fmi2Slave):
    """FMI 2.0 Co-Simulation slave wrapping FCB 4-DOF MMG model."""

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._sim = FCBSimulator()
        self._sim.load_params("config/fcb_dynamics.yaml")
        self._state = ShipState()

        # Register variables from export_fmu_interface() spec
        self.register_variable(Real("u", causality=Fmi2Causality.output))
        self.register_variable(Real("v", causality=Fmi2Causality.output))
        self.register_variable(Real("r", causality=Fmi2Causality.output))
        self.register_variable(Real("x", causality=Fmi2Causality.output))
        self.register_variable(Real("y", causality=Fmi2Causality.output))
        self.register_variable(Real("psi", causality=Fmi2Causality.output))
        self.register_variable(Real("delta_cmd", causality=Fmi2Causality.input))
        self.register_variable(Real("n_rps_cmd", causality=Fmi2Causality.input))
        # ... full 22-variable set per FmuInterfaceSpec

    def do_step(self, current_time, step_size):
        self._state = self._sim.step(
            self._state, self.delta_cmd, self.n_rps_cmd, step_size
        )
        self.u = self._state.u
        self.v = self._state.v
        self.r = self._state.r
        self.x = self._state.x
        self.y = self._state.y
        self.psi = self._state.psi
        return True
```

### 4.4 FMU 构建

```bash
# CLI build
python -m fmi_bridge.python.build_fmu \
    --model FCBShipDynamics \
    --config config/fcb_dynamics.yaml \
    --output fcb_mmg_4dof.fmu
```

产出：`fcb_mmg_4dof.fmu`（ZIP: modelDescription.xml + shared lib + resources/）

---

## §5 dds-fmu Mediator

### 5.1 DDS↔FMU 信号映射

YAML 配置文件 `config/dds_fmu_mapping.yaml`：

```yaml
# dds-fmu signal mapping: ROS2 topics ↔ FMU variables
mappings:
  # Control inputs: L4/L5 → FMU
  - topic: /cmd/rudder
    type: l3_msgs/msg/RudderCmd
    field: angle_rad
    fmu_variable: delta_cmd
    direction: to_fmu

  - topic: /cmd/thrust
    type: l3_msgs/msg/ThrustCmd
    field: n_rps
    fmu_variable: n_rps_cmd
    direction: to_fmu

  # Disturbance inputs: environment → FMU
  - topic: /disturbance/wind
    type: l3_msgs/msg/WindState
    field: dir_deg
    fmu_variable: wind_dir_deg
    direction: to_fmu
  - topic: /disturbance/wind
    type: l3_msgs/msg/WindState
    field: speed_mps
    fmu_variable: wind_speed_mps
    direction: to_fmu
  - topic: /disturbance/current
    type: l3_msgs/msg/CurrentState
    field: dir_deg
    fmu_variable: current_dir_deg
    direction: to_fmu
  - topic: /disturbance/current
    type: l3_msgs/msg/CurrentState
    field: speed_mps
    fmu_variable: current_speed_mps
    direction: to_fmu

  # Ship state: FMU → M2 World Model
  - topic: /ship/state
    type: l3_external_msgs/msg/FilteredOwnShipState
    field_map:
      u_mps: u
      v_mps: v
      r_rads: r
      x_m: x
      y_m: y
      psi_rad: psi
    direction: from_fmu
    frequency: 50  # Hz (matches FMU dt=0.02s)
```

### 5.2 DdsFmuNode 设计

```cpp
// fmi_bridge/include/fmi_bridge/dds_fmu_bridge.hpp
// Core class: manages DDS↔FMU variable exchange at each libcosim step

class DdsFmuBridge {
public:
    void load_mapping(const std::string& yaml_path);

    // Called by libcosim at each co-simulation step
    void write_inputs_to_fmu(double time, FmuHandle fmu);
    void read_outputs_from_fmu(double time, FmuHandle fmu);

    // ROS2 publishers/subscribers initialized from mapping YAML
    void init_ros2(rclcpp::Node* node);

private:
    std::vector<SignalMapping> mappings_;
    rclcpp::Node* node_;
    // Subscribers (DDS → FMU inputs)
    // Publishers  (FMU outputs → DDS)
};
```

---

## §6 libcosim async_slave 集成

### 6.1 libcosimWrapper

```cpp
// fmi_bridge/include/fmi_bridge/libcosim_wrapper.hpp
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/execution.hpp>
#include <cosim/observer/file_observer.hpp>

class LibcosimWrapper {
public:
    LibcosimWrapper(double step_size = 0.02);

    // Load FMU from .fmu file
    void load_fmu(const std::string& fmu_path, const std::string& instance_name);

    // Run co-simulation for N steps
    void run_for(double duration_s);

    // Access FMU variables by name
    double get_real(const std::string& fmu_instance, const std::string& var_name);
    void set_real(const std::string& fmu_instance, const std::string& var_name, double value);

private:
    std::shared_ptr<cosim::execution> execution_;
    std::shared_ptr<cosim::fixed_step_algorithm> algorithm_;
    double step_size_;
    double current_time_ = 0.0;
};
```

### 6.2 async_slave 接口

```cpp
// libcosim async_slave: the dds-fmu node acts as an asynchronous
// participant that libcosim synchronizes at each fixed step.
// Implementation in dds_fmu_node.cpp:

class DdsFmuAsyncSlave : public cosim::async_slave {
public:
    void do_step(double current_time, double step_size) override {
        // 1. Write ROS2 subscriber values → FMU inputs
        bridge_.write_inputs_to_fmu(current_time, fmu_handle_);
        // 2. Read FMU outputs → ROS2 publisher topics
        bridge_.read_outputs_from_fmu(current_time, fmu_handle_);
    }
private:
    DdsFmuBridge bridge_;
    cosim::slave_handle fmu_handle_;
};
```

---

## §7 CI 变更

### 7.1 Dockerfile.ci 修正

```dockerfile
# 当前: ros:jazzy-ros-base-noble (Ubuntu 24.04)
# 修正: ros:humble-ros-base (Ubuntu 22.04) — per SIL decision §3
FROM ros:humble-ros-base@${ROS_BASE_DIGEST}

# Python 3.11 via deadsnakes PPA
RUN apt-get update && apt-get install -y --no-install-recommends \
    software-properties-common \
    && add-apt-repository ppa:deadsnakes/ppa \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
    python3.11 python3.11-venv python3.11-dev \
    python3-pip \
    && update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.11 1

# DNV toolchain (MUST per SIL decision §2)
RUN pip3 install farn ospx

# FMI toolchain
RUN pip3 install pythonfmu

# libcosim C++ — build from source or binary package
# (OSP provides .deb for Ubuntu 22.04)
RUN apt-get install -y libcosim-dev
```

### 7.2 M7 FMI 隔离 lint

```bash
#!/bin/bash
# tools/ci/check-m7-fmi-isolation.sh
# M7 Safety Supervisor must NEVER import/link libcosim or fmi_bridge.
# Architecture v1.1.3-pre-stub §F.2: M7 KPI < 10ms, cannot cross FMI boundary.

set -euo pipefail
errors=0

# C++ include scan
if grep -rEn '#include.*(cosim|fmi_bridge)' \
    src/l3_tdl_kernel/m7_safety_supervisor/ \
    --include='*.hpp' --include='*.cpp' --include='*.h'; then
    echo "FAIL: M7 includes libcosim/fmi_bridge header" >&2
    errors=$((errors + 1))
fi

# CMake dependency scan
if grep -rEn '(libcosim|fmi_bridge)' \
    src/l3_tdl_kernel/m7_safety_supervisor/CMakeLists.txt; then
    echo "FAIL: M7 CMakeLists.txt references libcosim/fmi_bridge" >&2
    errors=$((errors + 1))
fi

if [[ $errors -gt 0 ]]; then
    echo "M7-FMI isolation violated: $errors error(s)." >&2
    echo "Architecture §F.2: M7 is ROS2 native, never crosses FMI boundary." >&2
    exit 1
fi

echo "PASS: M7-FMI isolation check clean."
```

### 7.3 CI pipeline 追加

```yaml
# .gitlab-ci.yml 追加
fmi_bridge_build:
  stage: build
  needs: ["fcb_simulator_build"]
  script:
    - colcon build --packages-select fmi_bridge
    - colcon test --packages-select fmi_bridge

m7_fmi_isolation_check:
  stage: lint
  needs: []
  script:
    - bash tools/ci/check-m7-fmi-isolation.sh
  allow_failure: false
```

---

## §8 测试设计

### 8.1 旋回测试（`test_fmu_turning_circle.py`）

```python
"""Verify FMU turning circle against D1.3a analytical reference."""

def test_turning_circle_advance(fmu_path, fcb_config_yaml):
    """35° rudder turning circle: advance ≈ 3L ± 5% (FCB L=45m)."""
    from fmi_bridge.python.fcb_mmg_fmu import FCBMmgFmu

    fmu = FCBMmgFmu.load(fmu_path, config=fcb_config_yaml)
    fmu.setup_simple(world=None)

    # Set controls for standard turning circle
    fmu.n_rps_cmd = 3.5     # ~12 kn approach speed
    fmu.delta_cmd = 0.0
    fmu.do_step(0.0, 600.0)  # approach straight

    fmu.delta_cmd = 0.6109   # 35° rudder
    trajectory = []
    for _ in range(1500):     # 300s @ 0.2s step
        fmu.do_step(0.0, 0.2)
        trajectory.append((fmu.x, fmu.y, fmu.psi))

    # Compute advance and transfer from trajectory
    advance, transfer = compute_turning_metrics(trajectory)
    L = 45.0  # FCB length

    assert abs(advance - 3 * L) < 0.15 * L   # advance ≈ 135m ± 6.75m
    assert abs(transfer - 0.7 * L) < 0.05 * L # transfer ≈ 31.5m ± 2.25m
```

### 8.2 DDS latency 测试（`test_dds_fmu_latency.py`）

```python
"""dds-fmu exchange latency: P95 must be < 10ms."""

def test_dds_roundtrip_latency_p95():
    """DDS loopback (local host, no FMU): roundtrip P95 < 10ms."""
    import time
    import rclpy
    from fmi_bridge.dds_fmu_bridge import DdsFmuBridge

    rclpy.init()
    node = rclpy.create_node("latency_test")
    bridge = DdsFmuBridge()
    bridge.init_ros2(node)

    latencies = []
    for _ in range(1000):
        t0 = time.perf_counter()
        bridge.write_inputs_to_fmu(0.0, None)   # topic publish
        rclpy.spin_once(node, timeout_sec=0.01)
        bridge.read_outputs_from_fmu(0.0, None)  # topic receive
        t1 = time.perf_counter()
        latencies.append((t1 - t0) * 1000)

    p95 = sorted(latencies)[int(len(latencies) * 0.95)]
    assert p95 < 10.0, f"DDS latency P95={p95:.1f}ms > 10ms budget"

    node.destroy_node()
    rclpy.shutdown()
```

---

## §9 Phase 1 DoD 闭口判据（6/15 EOD）

- [ ] `src/sim_workbench/fmi_bridge/` colcon 包存在，`colcon build` 通过
- [ ] `fcb_mmg_4dof.fmu` 生成（pythonfmu 打包，含 modelDescription.xml + shared lib）
- [ ] `dds_fmu_node.cpp` ROS2 节点：订阅 `/cmd/rudder` `/cmd/thrust` → FMU input / FMU output → 发布 `/ship/state` @ 50Hz
- [ ] `test_fmu_turning_circle.py` PASS：advance ≈ 3L±5%，transfer ≈ 0.7L±5%
- [ ] `test_dds_fmu_latency.py` PASS：DDS roundtrip P95 < 10ms
- [ ] `test_m7_fmi_isolation.py` PASS：M7 不 link libcosim/fmi_bridge
- [ ] Dockerfile.ci：Humble + Ubuntu 22.04 + Python 3.11 + libcosim-dev + farn + ospx + pythonfmu
- [ ] CI：`colcon build fmi_bridge` + `colcon test fmi_bridge` 全绿
- [ ] 全量回归：现有 39+ tests 零回归
- [ ] `tools/ci/check-m7-fmi-isolation.sh` 脚本存在 + CI job 绿勾
- [ ] tool confidence 报告：`docs/Design/Detailed Design/D1.3c-fmi-bridge/phase1-confidence-report.md`（farn 10-cell sweep + ospx system composition validation）
- [ ] 关闭 finding：SIL P0 SIL-1（v3.1 NEW）+ V&V P0 E1 SIL latency budget

---

## §10 Phase 2 预留接口（7/15 前，不进 Phase 1 DoD）

| 接口 | 用途 | D1.3c 预留 |
|---|---|---|
| `FmuRegistry::load_multi()` | 加载多个 FMU（MMG + ncdm_vessel + disturbance） | libcosim_wrapper.hpp 声明，Phase 2 实现 |
| `NcdmVesselFmu` | D2.4 NTNU 神经网络船模 → mlfmu → FMU | target_modes.py 的 `ncdm_vessel` stub 消费 |
| `farn::CaseFolder::run(n=100)` | farn 100-cell LHS sweep | libcosim_wrapper 的 `run_batch()` 接口 |
| C++ FMI wrapper | 替代 pythonfmu，纯 C++ FMU shared lib | fcb_mmg_fmu.cpp（Phase 2 新增） |

---

## §11 Risk + Contingency

| ID | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| **R-P1** | libcosim .deb 在 Ubuntu 22.04 不可用（OSP 仅发布 24.04 包） | 中 | 高（阻塞 D1.3c 全部） | Phase 1 提前验证：`apt-cache search libcosim`；若不可用，cmake FetchContent 从 GitHub 源码构建 |
| **R-P2** | dds-fmu latency P95 > 10ms（Humble DDS loopback 实测超 budget） | 中 | 中 | 降级方案：P99 < 15ms 接受（per 计划 v3.1 D1.5 SIL latency budget P99 目标）；若仍超，切 zero-copy loaned messages 或 IPC 替代 DDS loopback |
| **R-P3** | pythonfmu 打包的 .fmu 在 libcosim 中加载失败 | 低 | 高 | pythonfmu 是 OSP 社区推荐的 FMI 2.0 打包工具，已知与 libcosim 兼容。若失败，Phase 1 改手动打包（ZIP + modelDescription.xml 手写 + pybind11 .so） |
| **R-P4** | Dockerfile Humble + Python 3.11 deadsnakes PPA 在 CI 环境不可用 | 低 | 中 | 备选：源码编译 Python 3.11（~15min Docker build time）；或降级接受 Humble 出厂 3.10（farn 最低要求 3.10，ospx 需确认） |
| **R-P5** | pybind11 绑定 FCBSimulator 时 ABI 不兼容（GCC 版本差） | 低 | 中 | Dockerfile 统一 GCC 14.3；CMake 验证 C++17 ABI；Phase 1 测试旋回通过即 ABI 已验证 |
| **R-P6** | 现有 39 tests 在 docker image 重建后出现回归（路径/Python 版本变化） | 中 | 中 | Dockerfile 变更在单独 commit，CI 全量 run；若回归 > 2h 修复窗口，Dockerfile 变更 split 为独立 PR（先合 Dockerfile → 等 CI 绿 → 再合 D1.3c 代码） |

---

## §12 Task 拆分（Phase 1，~15d）

| ID | Task | 工时 | 前置 | AC |
|---|---|---|---|---|
| **T1** | fmi_bridge 包脚手架（package.xml + CMakeLists.txt + 目录结构） | 1d | — | `colcon build --packages-select fmi_bridge` 成功 |
| **T2** | pybind11 绑定 FCBSimulator（`fcb_mmg_bindings.cpp`） | 2d | T1 | `python3 -c "import fcb_mmg_py; s = fcb_mmg_py.FCBSimulator()"` 无报错 |
| **T3** | pythonfmu FCBMmgFmu 类 + `build_fmu.py` CLI | 2d | T2 | `python -m fmi_bridge.python.build_fmu --output fcb_mmg_4dof.fmu` 生成有效 .fmu |
| **T4** | `test_fmu_turning_circle.py` — 旋回参考解验证 | 1d | T3 | advance ≈ 135m ± 6.75m；transfer ≈ 31.5m ± 2.25m |
| **T5** | libcosim_wrapper C++ 实现 + dds_fmu_bridge 信号映射引擎 | 2d | T3 | `colcon test --packages-select fmi_bridge` 单元测试通过 |
| **T6** | dds_fmu_node ROS2 节点 + `dds_fmu_mapping.yaml` | 1d | T5 | `/ship/state` topic 有 50Hz 数据输出 |
| **T7** | `test_dds_fmu_latency.py` — DDS roundtrip P95 < 10ms | 1d | T6 | P95 < 10ms（或 P99 < 15ms contingency） |
| **T8** | Dockerfile.ci 修正：Humble + 22.04 + Python 3.11 + libcosim-dev + farn + ospx + pythonfmu | 2d | T1 | CI pipeline 全绿；39+ tests 零回归 |
| **T9** | `tools/ci/check-m7-fmi-isolation.sh` + CI job | 0.5d | T1 | CI lint job 绿勾 |
| **T10** | tool confidence 报告：farn 10-cell sweep + ospx validation | 1d | T3, T5 | `phase1-confidence-report.md` commit |
| **T11** | CI 全量回归 + colcon test 全包 | 1d | T1-T10 | 全绿；现有 tests 零回归 |
| **T12** | 文档 + finding 关闭 | 0.5d | T11 | SIL P0 SIL-1 + V&V P0 E1 标 CLOSED |

---

## §13 依赖图

```
T1 (scaffold)
 ├─ T2 (pybind11 bindings)
 │   └─ T3 (pythonfmu build)
 │       ├─ T4 (旋回测试)
 │       └─ T5 (libcosim_wrapper + dds_fmu_bridge)
 │           └─ T6 (dds_fmu_node)
 │               └─ T7 (latency 测试)
 ├─ T8 (Dockerfile 修正 — 可并行 1d 后启动)
 ├─ T9 (M7-FMI lint — 可并行)
 └─ T10 (tool confidence — 依赖 T3, T5)

T4 + T7 + T8 + T9 + T10 ──→ T11 (全量回归) ──→ T12 (finding 关闭)
```

---

## §14 来源与置信度

| 断言 | 来源 | 置信度 |
|---|---|---|
| libcosim async_slave 是 OSP FMI 2.0 co-sim 的 C++ API | NLM silhil_platform notebook (98 sources) + OSP 官方文档 | 🟢 High |
| dds-fmu mediator 是 DNV 的 DDS↔FMI 桥接开源工具 | NLM silhil_platform notebook + DNV GitHub | 🟢 High |
| pythonfmu 与 libcosim 兼容（OSP 社区推荐） | DNV OSP 文档 + pythonfmu PyPI | 🟢 High |
| farn v0.4.2 + ospx Python 3.11+ 要求 | SIL decision §2 + GitHub Releases | 🟢 High |
| ROS2 Humble + Ubuntu 22.04 锁定 | SIL decision §3 + 用户拍板 2026-05-09 | 🟢 High |
| M7 <10ms KPI 不越 FMI 边界铁律 | SIL decision §2 + 架构报告附录 F.2 | 🟢 High |
| Yasukawa 2015 MMG 旋回 advance ≈ 3L for FCB | D1.3a spec §5 + Yasukawa 2015 [R7] | 🟡 Medium（HAZID 8/19 校准后升 🟢） |
| dds-fmu single exchange 2-10ms budget | SIL decision §2.4 + NLM E3 引用 | 🟡 Medium（实测 T7 验证） |

---

*文档版本 v1.0 · 2026-05-11 · brainstorming 产出，待用户评审后走 /writing-plans → /executing-plans*
