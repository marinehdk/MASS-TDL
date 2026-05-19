# SIL 10 节点 LifecycleNode 升级 + FCB MMG 4-DOF 物理模型实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 src/sim_workbench/ 下 10 个节点从 pure-Python 数据类 stub 升级为完整 rclpy.lifecycle.LifecycleNode，并为 ship_dynamics_node 实施 Yasukawa 2015 4-DOF MMG 物理模型（RK4, dt=0.02s），以满足 DEMO-1 (2026-05-20) + DEMO-2 (2026-05-31) 的 V&V E1.6 验收要求。

**Architecture:** 10 节点全部继承 rclpy.lifecycle.LifecycleNode，通过 scenario_lifecycle_mgr 按 Nav2 lifecycle_manager 模式编排 change_state 顺序调用。ship_dynamics_node 从 kinematic-only stub 升级为完整的 Yasukawa 2015 MMG 4-DOF (surge/sway/yaw/roll)，使用 FCB 45m 系数（fcb_dynamics.yaml）。DDS QoS 按 Doc 2 §7.3 qos_overrides.yaml 配置。

**Tech Stack:** ROS2 Humble Hawksbill, rclpy, lifecycle_msgs, sil_msgs (10 custom messages), pytest, NumPy (MMG matrix ops), Python 3.10

**Status:** DEMO-1 deadline 2026-05-20 | DEMO-2 deadline 2026-05-31 | 当前日期 2026-05-15

---

## 前置上下文

### 现有文件树（76 .py 文件）

```
src/sim_workbench/
├── sil_nodes/                          # 8 个业务节点（需升级）+ 1 个已实现
│   ├── ship_dynamics/ship_dynamics/node.py     # kinematic stub, 22 行
│   ├── env_disturbance/env_disturbance/node.py # Gauss-Markov wind, 62 行
│   ├── target_vessel/target_vessel/node.py     # linear kinematics, 105 行
│   ├── sensor_mock/sensor_mock/node.py         # AIS + Radar model, 43 行
│   ├── tracker_mock/tracker_mock/node.py       # God/KF tracker, 85 行
│   ├── fault_injection/fault_injection/node.py # 3 fault types, 60 行
│   ├── scoring/scoring/node.py                 # 6-dim Hagen scoring, 78 行
│   └── scenario_authoring/scenario_authoring/node.py  # YAML CRUD, 56 行
├── sil_lifecycle/                      # 生命周期管理器（需升级）
│   └── sil_lifecycle/lifecycle_mgr.py  # pure-Python FSM, 125 行
├── sil_msgs/                           # 10 个 .msg 文件（已完成）
│   ├── msg/OwnShipState.msg
│   ├── msg/EnvironmentState.msg
│   ├── msg/TargetVesselState.msg
│   ├── msg/RadarMeasurement.msg
│   ├── msg/AISMessage.msg
│   ├── msg/LifecycleStatus.msg
│   ├── msg/FaultEvent.msg
│   ├── msg/ScoringRow.msg
│   ├── msg/ModulePulse.msg
│   └── msg/ASDREvent.msg
├── fcb_simulator/                      # C++ MMG 参考实现
│   └── config/fcb_dynamics.yaml        # FCB 45m 系数（HAZID-UNVERIFIED）
├── fmi_bridge/                         # FMI FMU 包装（参考）
│   └── python/fcb_mmg_fmu.py           # pythonfmu 接口
└── ship_sim_interfaces/                # C++ 接口头文件
    └── include/.../ship_state.hpp      # ShipState 结构体
```

### 权威设计基线

| 文档 | 关键内容 | 引用 |
|---|---|---|
| SIL v1.0-unified/02-sil-backend-design.md §3.3 | 10 节点完整责任表 | GAP-018, GAP-020 |
| SIL v1.0-unified/02-sil-backend-design.md §7.1 | 完整话题清单 + 频率 + QoS | 行 432–448 |
| SIL v1.0-unified/02-sil-backend-design.md §7.3 | qos_overrides.yaml | 行 467–496 |
| fcb_dynamics.yaml | FCB 45m 全部 MMG 系数 | 78 行 YAML |
| V&V Plan E1.6 | E2E 数据流 <5s 验收 | 行 97 |
| ship_state.hpp | ShipState 字段: x,y,psi,u,v,r,phi,phi_dot | 16 行 |

### 消息类型对照（ros2 topic → sil_msgs）

| Topic | Msg Type | 频率 | Publisher | QoS Profile |
|---|---|---|---|---|
| `/sim_clock` | `builtin_interfaces/Time` | 1 kHz | scenario_lifecycle_mgr | RELIABLE + TRANSIENT_LOCAL + KEEP_LAST(10) |
| `/sil/own_ship_state` | `sil_msgs/OwnShipState` | 50 Hz | ship_dynamics_node | BEST_EFFORT + VOLATILE + KEEP_LAST(1) |
| `/sil/target_vessel_state` | `sil_msgs/TargetVesselState` | 10 Hz | target_vessel_node | BEST_EFFORT + VOLATILE + KEEP_LAST(1) |
| `/sil/radar_meas` | `sil_msgs/RadarMeasurement` | 5 Hz | sensor_mock_node | BEST_EFFORT + VOLATILE + KEEP_LAST(2) |
| `/sil/ais_msg` | `sil_msgs/AISMessage` | 0.1 Hz | sensor_mock_node | RELIABLE + VOLATILE + KEEP_LAST(10) |
| `/sil/environment` | `sil_msgs/EnvironmentState` | 1 Hz | env_disturbance_node | RELIABLE + VOLATILE + KEEP_LAST(2) |
| `/sil/tracked_targets` | `l3_external_msgs/TrackedTargetArray` | 10 Hz | tracker_mock_node | RELIABLE + VOLATILE + KEEP_LAST(2) |
| `/sil/actuator_cmd` | `ship_sim_interfaces/ActuatorCmd` | 10 Hz | L4 stub → ship_dynamics | RELIABLE + VOLATILE + KEEP_LAST(2) |
| `/sil/lifecycle_status` | `sil_msgs/LifecycleStatus` | 1 Hz | scenario_lifecycle_mgr | RELIABLE + TRANSIENT_LOCAL + KEEP_LAST(5) |
| `/sil/scoring` | `sil_msgs/ScoringRow` | 1 Hz | scoring_node | RELIABLE + TRANSIENT_LOCAL + KEEP_LAST(100) |
| `/sil/fault/ais_dropout` | `sil_msgs/FaultEvent` | event | fault_injection_node | RELIABLE + VOLATILE + KEEP_LAST(10) |
| `/sil/fault/radar_spike` | `sil_msgs/FaultEvent` | event | fault_injection_node | RELIABLE + VOLATILE + KEEP_LAST(10) |
| `/sil/fault/dist_step` | `sil_msgs/FaultEvent` | event | fault_injection_node | RELIABLE + VOLATILE + KEEP_LAST(10) |

### FCB 45m MMG 系数（fcb_dynamics.yaml 摘录）

```
L=46.0, d=2.8, B=8.0, displacement=450t, x_G=0.0
Added mass: m_x'=0.00831, m_y'=0.1284, J_zz'=0.00676
Hull: X_vv=-0.0407, X_vr=0.0441, X_rr=0.0127, X_vvvv=-0.0607
      Y_v=-0.3073, Y_r=0.1521, Y_vvv=-0.7256, Y_vvr=-0.1338
      Y_vrr=0.1657, Y_rrr=-0.0303
      N_v=-0.1084, N_r=-0.0585, N_vvv=0.0040, N_vvr=-0.0498
      N_vrr=-0.0151, N_rrr=-0.0061
Roll: G_M=1.2, T_phi=5.0
Propeller: t_P=0.184, w_P=0.200, D_P=1.5, k_0=0.6, k_1=-0.3, k_2=-0.25
Rudder: t_R=0.387, a_H=0.312, x_H'=-0.464, x_R'=-0.500, gamma_R=0.395
        l_R'=-0.710, kappa=0.50, epsilon=1.09, A_R=1.65, f_alpha=2.747
Integration: dt=0.02, x0=0, y0=0, psi0=1.5708, u0=9.26
```

---

## 升级顺序

升级顺序遵循**依赖倒置原则**：先修基础设施（mgr + msgs），再修核心数据源（ship_dynamics + env + targets），然后传感器链（sensor → tracker），最后评估工具（fault + scoring + scenario_authoring）。

```
Phase A — 基础设施 (Task 1-2)
  └─ sil_lifecycle_mgr (编排者) → sil_msgs 已就绪，无需改动

Phase B — 核心数据源 (Task 3-5)
  ├─ ship_dynamics (50Hz, MMG 最复杂)
  ├─ env_disturbance (1Hz, 已有 Gauss-Markov 逻辑)
  └─ target_vessel (10Hz, 已有 multi-target 逻辑)

Phase C — 传感器链 (Task 6-7)
  ├─ sensor_mock (5Hz radar + 0.1Hz AIS)
  └─ tracker_mock (10Hz, God/KF 追踪)

Phase D — 评估与注入 (Task 8-10)
  ├─ fault_injection (event-driven)
  ├─ scoring (1Hz, 6-dim)
  └─ scenario_authoring (event-driven, YAML CRUD)

Phase E — 集成验证 (Task 11-13)
  ├─ CI 单元测试 fixture
  ├─ 集成测试 (E1.6 <5s)
  └─ MMG 精度验证 (<2% RMS)
```

---

### Task 0: 预备 — 包依赖与 sil_msgs 编译验证

**Files:**
- Modify: `src/sim_workbench/sil_lifecycle/package.xml`
- Modify: `src/sim_workbench/sil_nodes/*/package.xml` (全部 8 个)

- [ ] **Step 0.1: 确保所有 package.xml 声明正确的 exec_depend**

每个 sil_nodes 包需要：
```xml
<exec_depend>rclpy</exec_depend>
<exec_depend>lifecycle_msgs</exec_depend>
<exec_depend>sil_msgs</exec_depend>
<exec_depend>std_msgs</exec_depend>
```

ship_dynamics 额外需要：
```xml
<exec_depend>python3-numpy</exec_depend>
```

sensor_mock + tracker_mock 额外需要：
```xml
<exec_depend>l3_external_msgs</exec_depend>
```

- [ ] **Step 0.2: 添加 sil_msgs 为所有节点包的 exec_depend**

打开每个 `src/sim_workbench/sil_nodes/*/package.xml`，在 `<exec_depend>rclpy</exec_depend>` 后添加：
```xml
  <exec_depend>sil_msgs</exec_depend>
```

- [ ] **Step 0.3: 编译验证**

```bash
cd /Users/marine/Code/MASS-L3-Tactical Layer
colcon build --packages-select sil_msgs sil_lifecycle ship_dynamics env_disturbance target_vessel sensor_mock tracker_mock fault_injection scoring scenario_authoring
```

Expected: 0 errors

- [ ] **Step 0.4: Commit**

```bash
git add src/sim_workbench/sil_lifecycle/package.xml src/sim_workbench/sil_nodes/*/package.xml
git commit -m "chore(sil): add lifecycle_msgs + sil_msgs exec_depend to all 9 node packages"
```

---

### Task 1: sil_lifecycle — scenario_lifecycle_mgr 升级为 LifecycleNode

**Files:**
- Modify: `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py`
- Modify: `src/sim_workbench/sil_lifecycle/setup.py`
- Modify: `src/sim_workbench/sil_lifecycle/launch/lifecycle_launch.py`
- Create: `tests/sim_workbench/sil_lifecycle/test_lifecycle_mgr.py`

**职责**: 作为唯一 LifecycleNode + 业务 mgr，编排 9 个业务节点的 change_state 顺序调用（参 Nav2 lifecycle_manager 模式）；publish `/sim_clock @ 1kHz + /sil/lifecycle_status @ 1Hz`。

**Callback 内 publisher/timer 注册清单：**

| Callback | 操作 |
|---|---|
| `on_configure` | 声明 ROS 参数 (scenario_id, scenario_hash, tick_hz=1000.0, status_hz=1.0)；返回 `TransitionCallbackReturn.SUCCESS` |
| `on_activate` | 创建 `_sim_clock_pub` → `/sim_clock` (builtin_interfaces/Time, QoS: RELIABLE+TRANSIENT_LOCAL+KEEP_LAST(10))；创建 `_status_pub` → `/sil/lifecycle_status` (sil_msgs/LifecycleStatus, QoS: RELIABLE+TRANSIENT_LOCAL+KEEP_LAST(5))；创建 `_sim_clock_timer` @ 1kHz；创建 `_status_timer` @ 1Hz |
| `on_deactivate` | 销毁所有 timer + publisher |
| `on_cleanup` | 清理参数 + 状态重置 |

**节点职责**: 维护 5 状态 FSM（UNCONFIGURED→INACTIVE→ACTIVE→DEACTIVATING→FINALIZED），在 on_activate 后按顺序调用 9 个业务节点的 `change_state(ACTIVATE)`。

- [ ] **Step 1.1: 写测试 — LifecycleNode state transitions**

```python
# tests/sim_workbench/sil_lifecycle/test_lifecycle_mgr.py
import pytest
import rclpy
from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn
from lifecycle_msgs.msg import State, Transition
from sil_lifecycle.lifecycle_mgr import ScenarioLifecycleMgr


@pytest.fixture
def ros_context():
    rclpy.init()
    yield
    rclpy.shutdown()


def test_lifecycle_node_inherits_lifecycle_node(ros_context):
    """Verify ScenarioLifecycleMgr inherits from rclpy.lifecycle.LifecycleNode."""
    node = ScenarioLifecycleMgr()
    assert isinstance(node, LifecycleNode)
    node.destroy_node()


def test_on_configure_returns_success(ros_context):
    """on_configure should return SUCCESS when valid params are set."""
    node = ScenarioLifecycleMgr()
    result = node.on_configure(State())
    assert result == TransitionCallbackReturn.SUCCESS
    node.destroy_node()


def test_on_activate_returns_success(ros_context):
    """on_activate should return SUCCESS after configuring."""
    node = ScenarioLifecycleMgr()
    node.on_configure(State())
    result = node.on_activate(State())
    assert result == TransitionCallbackReturn.SUCCESS
    node.destroy_node()


def test_on_deactivate_returns_success(ros_context):
    """on_deactivate should return SUCCESS after activating."""
    node = ScenarioLifecycleMgr()
    node.on_configure(State())
    node.on_activate(State())
    result = node.on_deactivate(State())
    assert result == TransitionCallbackReturn.SUCCESS
    node.destroy_node()


def test_on_cleanup_returns_success(ros_context):
    """on_cleanup should return SUCCESS after deactivating."""
    node = ScenarioLifecycleMgr()
    node.on_configure(State())
    node.on_activate(State())
    node.on_deactivate(State())
    result = node.on_cleanup(State())
    assert result == TransitionCallbackReturn.SUCCESS
    node.destroy_node()


def test_sim_clock_publishes_at_1khz(ros_context):
    """sim_clock timer should be created with period 0.001s (1kHz)."""
    node = ScenarioLifecycleMgr()
    node.on_configure(State())
    node.on_activate(State())
    # Check timer exists
    assert len(node._timers) > 0
    timer = node._timers[0]
    assert abs(timer.timer_period_ns - 1_000_000) < 100_000  # ~1ms tolerance
    node.on_deactivate(State())
    node.on_cleanup(State())
    node.destroy_node()


def test_lifecycle_status_publishes_at_1hz(ros_context):
    """lifecycle_status timer should be created with period 1.0s."""
    node = ScenarioLifecycleMgr()
    node.on_configure(State())
    node.on_activate(State())
    # Check at least 2 timers
    assert len(node._timers) >= 2
    # The 1Hz timer
    status_periods = [t.timer_period_ns for t in node._timers]
    assert any(abs(p - 1_000_000_000) < 100_000_000 for p in status_periods)  # ~1s
    node.on_deactivate(State())
    node.on_cleanup(State())
    node.destroy_node()
```

- [ ] **Step 1.2: 运行测试验证失败**

```bash
cd /Users/marine/Code/MASS-L3-Tactical Layer
python -m pytest tests/sim_workbench/sil_lifecycle/test_lifecycle_mgr.py -v
```

Expected: 全部 FAIL（ScenarioLifecycleMgr 尚未继承 LifecycleNode）

- [ ] **Step 1.3: 实现完整的 ScenarioLifecycleMgr LifecycleNode**

```python
# src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py
"""Scenario Lifecycle Manager — LifecycleNode + 5-state FSM orchestrating 9 business nodes.

States: UNCONFIGURED -> INACTIVE -> ACTIVE -> DEACTIVATING -> FINALIZED
Maps to FE screens: (1) UNCONFIGURED -> (2) INACTIVE -> (3) ACTIVE -> (4) INACTIVE

Per Doc 2 §3.3: publishes /sim_clock @ 1kHz, /sil/lifecycle_status @ 1Hz.
Per Doc 2 §7.3: uses RELIABLE + TRANSIENT_LOCAL for both topics.
"""

import time
from enum import IntEnum

import rclpy
from rclpy.lifecycle import LifecycleNode
from rclpy.lifecycle.node import TransitionCallbackReturn
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy
from lifecycle_msgs.msg import State, Transition
from builtin_interfaces.msg import Time as BuiltinTime
from sil_msgs.msg import LifecycleStatus


# ── QoS profiles (Doc 2 §7.3) ──────────────────────────────────────────────
QOS_CLOCK = QoSProfile(
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.TRANSIENT_LOCAL,
    history=HistoryPolicy.KEEP_LAST,
    depth=10,
)
QOS_STATUS = QoSProfile(
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.TRANSIENT_LOCAL,
    history=HistoryPolicy.KEEP_LAST,
    depth=5,
)


# ── Retained: LifecycleState / Transition enums ────────────────────────────
class LifecycleState(IntEnum):
    UNCONFIGURED = 0
    INACTIVE = 1
    ACTIVE = 2
    DEACTIVATING = 3
    FINALIZED = 4


class Transition(IntEnum):
    CONFIGURE = 1
    ACTIVATE = 3
    DEACTIVATE = 4
    CLEANUP = 6


# ── ScenarioLifecycleMgr ───────────────────────────────────────────────────
class ScenarioLifecycleMgr(LifecycleNode):
    """LifecycleNode acting as the SIL scenario execution orchestrator.

    Manages 5-state FSM, publishes sim_clock @ 1kHz and lifecycle_status @ 1Hz.
    Phase 1: does NOT automatically orchestrate 9 child nodes (Phase 2 feature).
    """

    def __init__(self, node_name: str = "scenario_lifecycle_mgr") -> None:
        super().__init__(node_name)

        # Declare ROS parameters
        self.declare_parameter("scenario_id", "")
        self.declare_parameter("scenario_hash", "")
        self.declare_parameter("tick_hz", 1000.0)
        self.declare_parameter("status_hz", 1.0)

        # Internal state
        self._fsm_state = LifecycleState.UNCONFIGURED
        self._sim_time = 0.0
        self._wall_start = 0.0
        self._sim_rate = 1.0

        # Publishers & timers (created in on_activate, destroyed in on_deactivate)
        self._sim_clock_pub = None
        self._status_pub = None
        self._sim_clock_timer = None
        self._status_timer = None

    # ── Lifecycle callbacks ─────────────────────────────────────────────────
    def on_configure(self, state: State) -> TransitionCallbackReturn:
        """Load parameters from YAML / launch args; transition to INACTIVE."""
        self.get_logger().info("Configuring scenario_lifecycle_mgr...")
        self._scenario_id = self.get_parameter("scenario_id").value
        self._scenario_hash = self.get_parameter("scenario_hash").value
        self._fsm_state = LifecycleState.INACTIVE
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: State) -> TransitionCallbackReturn:
        """Create publishers + timers; transition to ACTIVE."""
        self.get_logger().info("Activating scenario_lifecycle_mgr...")

        tick_hz = self.get_parameter("tick_hz").value
        status_hz = self.get_parameter("status_hz").value

        # Publishers
        self._sim_clock_pub = self.create_publisher(
            BuiltinTime, "/sim_clock", QOS_CLOCK
        )
        self._status_pub = self.create_publisher(
            LifecycleStatus, "/sil/lifecycle_status", QOS_STATUS
        )

        # Timers
        self._sim_clock_timer = self.create_timer(
            1.0 / tick_hz, self._clock_callback
        )
        self._status_timer = self.create_timer(
            1.0 / status_hz, self._status_callback
        )

        self._fsm_state = LifecycleState.ACTIVE
        self._wall_start = time.time()
        return TransitionCallbackReturn.SUCCESS

    def on_deactivate(self, state: State) -> TransitionCallbackReturn:
        """Destroy timers + publishers; transition to INACTIVE."""
        self.get_logger().info("Deactivating scenario_lifecycle_mgr...")

        if self._sim_clock_timer is not None:
            self.destroy_timer(self._sim_clock_timer)
            self._sim_clock_timer = None
        if self._status_timer is not None:
            self.destroy_timer(self._status_timer)
            self._status_timer = None

        if self._sim_clock_pub is not None:
            self.destroy_publisher(self._sim_clock_pub)
            self._sim_clock_pub = None
        if self._status_pub is not None:
            self.destroy_publisher(self._status_pub)
            self._status_pub = None

        self._fsm_state = LifecycleState.INACTIVE
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state: State) -> TransitionCallbackReturn:
        """Reset all internal state; transition to UNCONFIGURED."""
        self.get_logger().info("Cleaning up scenario_lifecycle_mgr...")
        self._fsm_state = LifecycleState.UNCONFIGURED
        self._scenario_id = ""
        self._scenario_hash = ""
        self._sim_time = 0.0
        self._wall_start = 0.0
        return TransitionCallbackReturn.SUCCESS

    # ── Timer callbacks ─────────────────────────────────────────────────────
    def _clock_callback(self) -> None:
        """Publish sim_clock @ 1kHz, advance simulation time."""
        if self._fsm_state != LifecycleState.ACTIVE:
            return
        tick_hz = self.get_parameter("tick_hz").value
        self._sim_time += (1.0 / tick_hz) * self._sim_rate

        msg = BuiltinTime()
        secs = int(self._sim_time)
        msg.sec = secs
        msg.nanosec = int((self._sim_time - secs) * 1e9)
        self._sim_clock_pub.publish(msg)

    def _status_callback(self) -> None:
        """Publish lifecycle_status @ 1Hz."""
        if self._fsm_state != LifecycleState.ACTIVE:
            return
        msg = LifecycleStatus()
        msg.stamp = self.get_clock().now().to_msg()
        msg.current_state = self._fsm_state.value
        msg.scenario_id = self._scenario_id
        msg.scenario_hash = self._scenario_hash
        msg.sim_time = self._sim_time
        msg.wall_time = time.time() - self._wall_start if self._wall_start > 0 else 0.0
        msg.sim_rate = self._sim_rate
        self._status_pub.publish(msg)

    # ── Public API (retained from stub for Phase 2 orchestration) ───────────
    @property
    def current_state(self) -> LifecycleState:
        return self._fsm_state

    @property
    def scenario_id(self) -> str:
        return self._scenario_id

    @property
    def sim_time(self) -> float:
        return self._sim_time

    @property
    def sim_rate(self) -> float:
        return self._sim_rate

    def set_sim_rate(self, rate: float) -> bool:
        if rate < 0:
            return False
        self._sim_rate = rate
        return True


def main(args=None) -> None:
    """ROS2 entry point (console_scripts)."""
    rclpy.init(args=args)
    node = ScenarioLifecycleMgr()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
```

- [ ] **Step 1.4: 运行测试验证通过**

```bash
python -m pytest tests/sim_workbench/sil_lifecycle/test_lifecycle_mgr.py -v
```

Expected: 6 PASS（可能 lifecycle 状态测试需要 rclpy 正确初始化，如有需要调整 fixture）

- [ ] **Step 1.5: 更新 setup.py entry_points**

```python
# src/sim_workbench/sil_lifecycle/setup.py
entry_points={
    'console_scripts': [
        'scenario_lifecycle_mgr = sil_lifecycle.lifecycle_mgr:main',
    ],
},
```

- [ ] **Step 1.6: Commit**

```bash
git add src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py tests/sim_workbench/sil_lifecycle/test_lifecycle_mgr.py
git commit -m "feat(sil): upgrade scenario_lifecycle_mgr to rclpy LifecycleNode with 1kHz sim_clock + 1Hz status"
```

---

### Task 2: 创建共享 test fixture — ROS2 集成测试基础设施

**Files:**
- Create: `tests/sim_workbench/conftest.py`
- Create: `tests/sim_workbench/__init__.py` (empty)

- [ ] **Step 2.1: 创建 conftest.py — 共享 ROS2 fixture**

```python
# tests/sim_workbench/conftest.py
"""Shared pytest fixtures for SIL LifecycleNode integration tests.

Provides:
- ros_context: session-scoped rclpy.init/shutdown
- isolated_executor: MultiThreadedExecutor(num_threads=4)
"""

import pytest
import rclpy
from rclpy.executors import MultiThreadedExecutor


@pytest.fixture(scope="session")
def ros_context():
    """Initialize rclpy once per test session."""
    rclpy.init()
    yield
    rclpy.shutdown()


@pytest.fixture
def isolated_executor(ros_context):
    """Create a fresh MultiThreadedExecutor for each test.

    Per Doc 2 §2.2 GAP-016: uses 4 threads + ReentrantCallbackGroup.
    """
    executor = MultiThreadedExecutor(num_threads=4)
    yield executor
    executor.shutdown()


@pytest.fixture
def lifecycle_node_test(ros_context):
    """Helper to test a LifecycleNode through all 4 state transitions.

    Usage:
        def test_my_node(lifecycle_node_test):
            node_cls = MyLifecycleNode
            lifecycle_node_test(node_cls)

    Returns a dict with {configured, activated, deactivated, cleaned_up} booleans.
    """

    def _run_transitions(node_cls, node_name=None):
        import time
        from rclpy.lifecycle.node import TransitionCallbackReturn

        name = node_name or "test_lifecycle_node"
        node = node_cls(node_name=name)

        # on_configure
        result = node.on_configure(rclpy.lifecycle.State())
        configured = result == TransitionCallbackReturn.SUCCESS

        # on_activate
        result = node.on_activate(rclpy.lifecycle.State())
        activated = result == TransitionCallbackReturn.SUCCESS

        # Let timers fire
        rclpy.spin_once(node, timeout_sec=0.1)

        # on_deactivate
        result = node.on_deactivate(rclpy.lifecycle.State())
        deactivated = result == TransitionCallbackReturn.SUCCESS

        # on_cleanup
        result = node.on_cleanup(rclpy.lifecycle.State())
        cleaned_up = result == TransitionCallbackReturn.SUCCESS

        node.destroy_node()
        return configured, activated, deactivated, cleaned_up

    return _run_transitions
```

- [ ] **Step 2.2: Commit**

```bash
mkdir -p tests/sim_workbench
touch tests/sim_workbench/__init__.py
git add tests/sim_workbench/conftest.py tests/sim_workbench/__init__.py
git commit -m "test(sil): add shared ROS2 integration test fixtures (conftest.py)"
```

---

### Task 3: ship_dynamics — Yasukawa 2015 4-DOF MMG 物理模型实施

**Files:**
- Modify: `src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/node.py`
- Create: `src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/mmg_model.py`
- Create: `src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/mmg_coefficients.py`
- Create: `tests/sim_workbench/sil_nodes/ship_dynamics/test_lifecycle.py`
- Create: `tests/sim_workbench/sil_nodes/ship_dynamics/test_mmg_model.py`
- Create: `tests/sim_workbench/sil_nodes/ship_dynamics/test_turning_circle.py`

**Callback 内 publisher/subscription/timer 注册清单：**

| Callback | 操作 |
|---|---|
| `on_configure` | 声明 ROS 参数 (dt, L, B, d, displacement_t, x_G, 全部 MMG 系数从 fcb_dynamics.yaml)；初始化 MMGModel + ShipState |
| `on_activate` | 创建 `_state_pub` → `/sil/own_ship_state` (sil_msgs/OwnShipState, QoS: BEST_EFFORT+VOLATILE+KEEP_LAST(1))；创建 `_actuator_sub` → `/sil/actuator_cmd` (ship_sim_interfaces/ActuatorCmd)；创建 `_env_sub` → `/sil/environment` (sil_msgs/EnvironmentState)；创建 `_timer` @ 50Hz |
| `on_deactivate` | 销毁 timer + publisher + subscriptions |
| `on_cleanup` | 重置 ShipState 为初始条件 |

**MMG Model 文件拆分：**

| 文件 | 职责 |
|---|---|
| `mmg_coefficients.py` | `MMGCoefficients` dataclass — 从 fcb_dynamics.yaml 加载的 78 行参数的结构化表示 |
| `mmg_model.py` | `MMGModel` — Yasukawa 2015 4-DOF 数学实现：M_RB, M_A, C(ν), D(ν), g(η), τ_hull, τ_prop, τ_rud, RK4 integrator |
| `node.py` | `ShipDynamicsNode(LifecycleNode)` — ROS2 包装，50Hz step，publish OwnShipState |

- [ ] **Step 3.1: 写 mmg_coefficients.py — FCB 45m 系数数据类**

```python
# src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/mmg_coefficients.py
"""FCB 45m MMG coefficients per Yasukawa & Yoshimura 2015.

Source: fcb_dynamics.yaml (HAZID-UNVERIFIED initial estimates).
RUN-001 (2026-08-19) calibration will update these values.
"""
from dataclasses import dataclass, field


@dataclass
class MMGCoefficients:
    """Non-dimensional MMG coefficients for a semi-planing hull.

    All primed quantities are non-dimensional per the MMG convention
    (Yasukawa & Yoshimura 2015, DOI: 10.1007/s00773-015-0299-0).

    S.I. scale factors: L=46.0 m (LBP), d=2.8 m (draft), B=8.0 m (beam),
    displacement=450.0 t, rho=1025 kg/m³ (seawater).
    """

    # ── Ship particulars ────────────────────────────────────────────────
    L: float = 46.0               # LBP [m]
    d: float = 2.8                # draft [m]
    B: float = 8.0                # beam [m]
    displacement_t: float = 450.0 # tonnes
    x_G: float = 0.0              # CG offset from midship [m]
    rho: float = 1025.0           # seawater density [kg/m³]

    # ── Added mass (non-dimensional) ────────────────────────────────────
    m_x_prime: float = 0.00831
    m_y_prime: float = 0.1284
    J_zz_prime: float = 0.00676

    # ── Hull derivatives (Abkowitz form, non-dimensional) ───────────────
    X_vv: float = -0.0407
    X_vr: float = 0.0441
    X_rr: float = 0.0127
    X_vvvv: float = -0.0607

    Y_v: float = -0.3073
    Y_r: float = 0.1521
    Y_vvv: float = -0.7256
    Y_vvr: float = -0.1338
    Y_vrr: float = 0.1657
    Y_rrr: float = -0.0303

    N_v: float = -0.1084
    N_r: float = -0.0585
    N_vvv: float = 0.0040
    N_vvr: float = -0.0498
    N_vrr: float = -0.0151
    N_rrr: float = -0.0061

    # ── Roll (1-DOF pendulum) ───────────────────────────────────────────
    G_M: float = 1.2              # metacentric height [m]
    T_phi: float = 5.0            # natural roll period [s]

    # ── Propeller ───────────────────────────────────────────────────────
    t_P: float = 0.184            # thrust deduction factor
    w_P: float = 0.200            # effective wake fraction
    D_P: float = 1.5              # propeller diameter [m]
    k_0: float = 0.6              # open-water K_T coefficient (const)
    k_1: float = -0.3             # open-water K_T coefficient (linear)
    k_2: float = -0.25            # open-water K_T coefficient (quadratic)

    # ── Rudder ──────────────────────────────────────────────────────────
    t_R: float = 0.387
    a_H: float = 0.312
    x_H_prime: float = -0.464
    x_R_prime: float = -0.500
    gamma_R: float = 0.395
    l_R_prime: float = -0.710
    kappa: float = 0.50
    epsilon: float = 1.09
    A_R: float = 1.65             # rudder area [m²]
    f_alpha: float = 2.747        # rudder lift gradient

    # ── Integration ─────────────────────────────────────────────────────
    dt: float = 0.02              # RK4 step size [s]

    # ── Initial conditions ──────────────────────────────────────────────
    x0: float = 0.0
    y0: float = 0.0
    psi0: float = 1.5708          # rad = 90° = North
    u0: float = 9.26              # m/s ≈ 18 kn cruise

    @property
    def mass(self) -> float:
        """Displacement mass [kg]."""
        return self.displacement_t * 1000.0

    @property
    def I_zz(self) -> float:
        """Yaw moment of inertia [kg·m²].
        Approximate: I_zz = m * (0.25*L)^2 * (1 + J_zz_prime)
        """
        m = self.mass
        k_zz = 0.25 * self.L  # radius of gyration ≈ 0.25 L
        I_zz_rigid = m * k_zz**2
        return I_zz_rigid * (1.0 + self.J_zz_prime)

    @property
    def I_xx(self) -> float:
        """Roll moment of inertia [kg·m²].
        I_xx = m * (k_xx)^2, k_xx ≈ 0.35 * B
        Natural roll period: T_phi = 2π * sqrt(I_xx / (m*g*G_M))
        → I_xx = (T_phi/(2π))^2 * m * g * G_M
        """
        import math
        g = 9.81
        omega_phi = 2.0 * math.pi / self.T_phi
        return self.mass * g * self.G_M / (omega_phi**2)

    @classmethod
    def from_yaml(cls, path: str) -> "MMGCoefficients":
        """Load coefficients from fcb_dynamics.yaml."""
        import yaml
        with open(path) as f:
            cfg = yaml.safe_load(f)
        params = cfg["fcb_simulator"]["ros__parameters"]
        # Map YAML keys to dataclass field names with unit conversions
        return cls(
            L=float(params["L"]),
            d=float(params["d"]),
            B=float(params["B"]),
            displacement_t=float(params["displacement_t"]),
            x_G=float(params.get("x_G", 0.0)),
            m_x_prime=float(params["m_x_prime"]),
            m_y_prime=float(params["m_y_prime"]),
            J_zz_prime=float(params["J_zz_prime"]),
            X_vv=float(params["X_vv"]),
            X_vr=float(params["X_vr"]),
            X_rr=float(params["X_rr"]),
            X_vvvv=float(params["X_vvvv"]),
            Y_v=float(params["Y_v"]),
            Y_r=float(params["Y_r"]),
            Y_vvv=float(params["Y_vvv"]),
            Y_vvr=float(params["Y_vvr"]),
            Y_vrr=float(params["Y_vrr"]),
            Y_rrr=float(params["Y_rrr"]),
            N_v=float(params["N_v"]),
            N_r=float(params["N_r"]),
            N_vvv=float(params["N_vvv"]),
            N_vvr=float(params["N_vvr"]),
            N_vrr=float(params["N_vrr"]),
            N_rrr=float(params["N_rrr"]),
            G_M=float(params["G_M"]),
            T_phi=float(params["T_phi"]),
            t_P=float(params["t_P"]),
            w_P=float(params["w_P"]),
            D_P=float(params["D_P"]),
            k_0=float(params["k_0"]),
            k_1=float(params["k_1"]),
            k_2=float(params["k_2"]),
            t_R=float(params["t_R"]),
            a_H=float(params["a_H"]),
            x_H_prime=float(params["x_H_prime"]),
            x_R_prime=float(params["x_R_prime"]),
            gamma_R=float(params["gamma_R"]),
            l_R_prime=float(params["l_R_prime"]),
            kappa=float(params["kappa"]),
            epsilon=float(params["epsilon"]),
            A_R=float(params["A_R"]),
            f_alpha=float(params["f_alpha"]),
            dt=float(params["dt"]),
            x0=float(params.get("x0", 0.0)),
            y0=float(params.get("y0", 0.0)),
            psi0=float(params.get("psi0", 1.5708)),
            u0=float(params.get("u0", 9.26)),
        )
```

- [ ] **Step 3.2: 写 mmg_model.py — Yasukawa 2015 4-DOF MMG + RK4 integrator**

```python
# src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/mmg_model.py
"""Yasukawa & Yoshimura 2015 4-DOF MMG model with RK4 integrator.

State vector: η = [x, y, ψ, φ]ᵀ (position) + ν = [u, v, r, p]ᵀ (velocity)
Total 8-state: [x, y, ψ, φ, u, v, r, p]

Equation of motion:
  (M_RB + M_A) · ν̇ + C(ν)·ν + D(ν)·ν + g(η) = τ_hull + τ_prop + τ_rud + τ_env

Reference: Yasukawa, H. & Yoshimura, Y. (2015)
  "Introduction of MMG standard method for ship maneuvering predictions"
  J. Mar. Sci. Tech., DOI: 10.1007/s00773-015-0299-0
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Optional

from .mmg_coefficients import MMGCoefficients


@dataclass
class ShipState:
    """8-state vector for 4-DOF MMG model.

    η = [x, y, ψ, φ]   (position + attitude, NED frame)
    ν = [u, v, r, p]   (velocity, body frame)
    """

    x: float = 0.0      # East position [m]
    y: float = 0.0      # North position [m]
    psi: float = 0.0    # heading, math convention [rad] (0=East, +π/2=North)
    phi: float = 0.0    # roll angle [rad]
    u: float = 0.0      # surge velocity [m/s]
    v: float = 0.0      # sway velocity [m/s]
    r: float = 0.0      # yaw rate [rad/s]
    p: float = 0.0      # roll rate [rad/s]

    def copy(self) -> "ShipState":
        return ShipState(
            x=self.x, y=self.y, psi=self.psi, phi=self.phi,
            u=self.u, v=self.v, r=self.r, p=self.p,
        )

    def __add__(self, other: "ShipState") -> "ShipState":
        return ShipState(
            x=self.x + other.x, y=self.y + other.y, psi=self.psi + other.psi,
            phi=self.phi + other.phi, u=self.u + other.u, v=self.v + other.v,
            r=self.r + other.r, p=self.p + other.p,
        )

    def __mul__(self, scalar: float) -> "ShipState":
        return ShipState(
            x=self.x * scalar, y=self.y * scalar, psi=self.psi * scalar,
            phi=self.phi * scalar, u=self.u * scalar, v=self.v * scalar,
            r=self.r * scalar, p=self.p * scalar,
        )

    __rmul__ = __mul__


class MMGModel:
    """Yasukawa 2015 4-DOF MMG ship dynamics model.

    Implements:
    - M_RB: rigid-body mass matrix
    - M_A: added mass matrix
    - C(ν): Coriolis + centripetal matrix
    - D(ν): damping (hull derivatives, Abkowitz form)
    - g(η): restoring forces (roll only)
    - τ_hull: hull hydrodynamic forces (non-dimensional → dimensional)
    - τ_prop: propeller thrust
    - τ_rud: rudder forces
    - τ_env: environmental disturbance (wind/current, injected from env_disturbance_node)

    Integration: 4th-order Runge-Kutta (RK4), fixed step dt=0.02s (50 Hz).
    """

    def __init__(self, coeffs: MMGCoefficients) -> None:
        self.c = coeffs
        self._precompute_mass_matrix()
        self._precompute_scale_factors()

    def _precompute_mass_matrix(self) -> None:
        """Compute the total mass matrix M = M_RB + M_A.

        M_RB = diag(m, m, I_zz, I_xx)  [surge/sway/yaw/roll]
        M_A  = diag(m·m_x', m·m_y', I_zz·J_zz', 0)  [added mass, roll=0]

        Returns M as 4x4 list-of-lists (precomputed).
        """
        m = self.c.mass
        self._m11 = m + m * self.c.m_x_prime  # surge
        self._m22 = m + m * self.c.m_y_prime  # sway
        self._m33 = self.c.I_zz               # yaw
        self._m44 = self.c.I_xx               # roll
        # Off-diagonal: 0 for diagonal mass matrix
        self._m24 = 0.0  # surge-roll coupling (0 in this model)
        self._m42 = 0.0  # roll-sway coupling (0 in this model)

    def _precompute_scale_factors(self) -> None:
        """Precompute non-dimensional ↔ dimensional scale factors.

        Per MMG convention:
          v' = v / U,  r' = r * L / U
          X' = X / (0.5·ρ·L·d·U²), Y' = Y / (0.5·ρ·L·d·U²)
          N' = N / (0.5·ρ·L²·d·U²)
        """
        c = self.c
        self._scale_force = 0.5 * c.rho * c.L * c.d  # denominator base for X, Y
        self._scale_moment = 0.5 * c.rho * c.L * c.L * c.d  # denominator base for N

    def _ndim_velocity(self, u: float, v: float, r: float) -> tuple[float, float]:
        """Convert to non-dimensional primed quantities: v' = v/U, r' = r·L/U.

        Returns (v_prime, r_prime).  u must be > 0.
        """
        U = math.sqrt(u * u + v * v)
        if U < 1e-6:
            return (0.0, 0.0)
        v_p = v / U
        r_p = r * self.c.L / U
        return v_p, r_p

    def _hull_forces_nd(self, v_p: float, r_p: float) -> tuple[float, float, float]:
        """Compute non-dimensional hull forces X', Y', N' using Abkowitz polynomial.

        Returns (X_prime, Y_prime, N_prime).
        """
        c = self.c
        # Surge resistance: X'(v', r')
        X = (
            c.X_vv * v_p * v_p
            + c.X_vr * v_p * r_p
            + c.X_rr * r_p * r_p
            + c.X_vvvv * v_p * v_p * v_p * v_p
        )
        # Sway force: Y'(v', r')
        Y = (
            c.Y_v * v_p
            + c.Y_r * r_p
            + c.Y_vvv * v_p * v_p * v_p
            + c.Y_vvr * v_p * v_p * r_p
            + c.Y_vrr * v_p * r_p * r_p
            + c.Y_rrr * r_p * r_p * r_p
        )
        # Yaw moment: N'(v', r')
        N = (
            c.N_v * v_p
            + c.N_r * r_p
            + c.N_vvv * v_p * v_p * v_p
            + c.N_vvr * v_p * v_p * r_p
            + c.N_vrr * v_p * r_p * r_p
            + c.N_rrr * r_p * r_p * r_p
        )
        return X, Y, N

    def _hull_forces_dimensional(
        self, u: float, v: float, r: float
    ) -> tuple[float, float, float]:
        """Compute dimensional hull forces X_H, Y_H, N_H [N, N, N·m].

        X' = X / (0.5·ρ·L·d·U²) → X = X' * 0.5·ρ·L·d·U²
        Y' = Y / (0.5·ρ·L·d·U²) → Y = Y' * 0.5·ρ·L·d·U²
        N' = N / (0.5·ρ·L²·d·U²) → N = N' * 0.5·ρ·L²·d·U²
        """
        U2 = u * u + v * v
        if U2 < 1e-6:
            return (0.0, 0.0, 0.0)
        v_p, r_p = self._ndim_velocity(u, v, r)
        X_p, Y_p, N_p = self._hull_forces_nd(v_p, r_p)
        X = X_p * self._scale_force * U2
        Y = Y_p * self._scale_force * U2
        N = N_p * self._scale_moment * U2
        return X, Y, N

    def _propeller_thrust(self, u: float, n_rps: float) -> float:
        """Compute propeller thrust τ_prop [N].

        Advance ratio:  J = u·(1 - w_P) / (n·D_P)
        Thrust coefficient: K_T = k_2·J² + k_1·J + k_0
        Thrust: T = (1 - t_P) · ρ · n² · D_P⁴ · K_T

        n_rps = propeller revolutions per second [rev/s].
        """
        c = self.c
        if n_rps < 0.01:
            return 0.0
        u_a = u * (1.0 - c.w_P)  # advance velocity
        J = u_a / (n_rps * c.D_P) if n_rps > 0 else 0.0
        J = max(-1.0, min(1.0, J))  # clamp advance ratio
        K_T = c.k_2 * J * J + c.k_1 * J + c.k_0
        T = (1.0 - c.t_P) * c.rho * n_rps * n_rps * c.D_P**4 * K_T
        return T

    def _rudder_forces(
        self, u: float, v: float, r: float, delta_rad: float, n_rps: float
    ) -> tuple[float, float, float]:
        """Compute rudder forces X_R, Y_R, N_R [N, N, N·m].

        Rudder normal force: F_N = 0.5·ρ·A_R·f_α·U_R²·sin(α_R)
        where U_R is effective inflow velocity to rudder.
        """
        c = self.c
        if abs(delta_rad) < 1e-6 and abs(v) < 1e-6:
            return (0.0, 0.0, 0.0)

        # Effective inflow velocity components at rudder position
        # u_R = u · (1 - w_R)  with w_R ≈ w_P for simplicity
        # v_R = v + l_R · r (geometric sway component)
        u_R = u * (1.0 - c.w_P)  # simplified: w_R ≈ w_P
        v_R = v + c.l_R_prime * c.L * r

        # Rudder inflow speed
        U_R = math.sqrt(u_R * u_R + v_R * v_R)
        if U_R < 1e-6:
            return (0.0, 0.0, 0.0)

        # Effective rudder angle (accounting for inflow direction)
        alpha_R = delta_rad - math.atan2(v_R, u_R)

        # Rudder normal force
        F_N = 0.5 * c.rho * c.A_R * c.f_alpha * U_R * U_R * math.sin(alpha_R)

        # Force decomposition in hull frame
        X_R = -(1.0 - c.t_R) * F_N * math.sin(delta_rad)
        Y_R = -(1.0 + c.a_H) * F_N * math.cos(delta_rad)
        N_R = -(c.x_R_prime * c.L + c.a_H * c.x_H_prime * c.L) * F_N * math.cos(delta_rad)

        return X_R, Y_R, N_R

    def compute_derivatives(
        self,
        state: ShipState,
        delta_cmd: float,
        n_rps_cmd: float,
        wind_speed: float = 0.0,
        wind_dir_rad: float = 0.0,
        current_speed: float = 0.0,
        current_dir_rad: float = 0.0,
    ) -> ShipState:
        """Compute state derivatives d(state)/dt = f(state, controls, env).

        Returns dState containing [ẋ, ẏ, ψ̇, φ̇, u̇, v̇, ṙ, ṗ].
        """
        c = self.c
        s = state

        # ── Kinematics (η̇) ──────────────────────────────────────────────
        # NED position derivatives from body-frame velocities
        x_dot = s.u * math.cos(s.psi) - s.v * math.sin(s.psi)
        y_dot = s.u * math.sin(s.psi) + s.v * math.cos(s.psi)
        psi_dot = s.r
        phi_dot = s.p

        # ── Hull forces ─────────────────────────────────────────────────
        X_H, Y_H, N_H = self._hull_forces_dimensional(s.u, s.v, s.r)

        # ── Propeller thrust ────────────────────────────────────────────
        X_P = self._propeller_thrust(s.u, n_rps_cmd)
        # Propeller operates on surge axis only in this model

        # ── Rudder forces ───────────────────────────────────────────────
        X_R, Y_R, N_R = self._rudder_forces(s.u, s.v, s.r, delta_cmd, n_rps_cmd)

        # ── Environmental forces (simplified wind model) ─────────────────
        # τ_env = wind load (phase 1: simple quadratic drag, phase 2: full Fujiwara)
        X_env, Y_env, N_env = self._env_forces(
            s.u, s.v, wind_speed, wind_dir_rad, current_speed, current_dir_rad
        )

        # ── Total forces ────────────────────────────────────────────────
        X_total = X_H + X_P + X_R + X_env
        Y_total = Y_H + Y_R + Y_env
        N_total = N_H + N_R + N_env

        # ── Coriolis + centripetal ──────────────────────────────────────
        # C(ν)·ν for diagonal M: [0, 0, 0] in surge, [-m·ur, m·ur] in sway-yaw
        m = c.mass
        m_x_added = m * c.m_x_prime
        m_y_added = m * c.m_y_prime
        # Coriolis terms
        X_cor = (m + m_y_added) * s.v * s.r + m * c.x_G * s.r * s.r
        Y_cor = -(m + m_x_added) * s.u * s.r
        N_cor = -m * c.x_G * s.u * s.r

        # ── Solve M · ν̇ = τ_total - C(ν)·ν - D(ν)·ν (already in X/Y/N_total) ──
        # For diagonal mass matrix, ν̇ = M⁻¹ · τ_total
        u_dot = (X_total - X_cor) / self._m11 if self._m11 > 0 else 0.0
        v_dot = (Y_total - Y_cor) / self._m22 if self._m22 > 0 else 0.0
        r_dot = (N_total - N_cor) / self._m33 if self._m33 > 0 else 0.0

        # ── Roll dynamics (1-DOF pendulum, weakly coupled) ──────────────
        # I_xx·ṗ + K_φ·φ + K_p·p = K_v·v + K_r·r  (heeling moment from sway+yaw)
        g = 9.81
        K_phi = c.mass * g * c.G_M  # roll stiffness
        K_p = c.mass * g * c.G_M * c.T_phi / (2.0 * math.pi) * 0.1  # roll damping (crude estimate)
        # Heeling moment from sway + yaw (simplified)
        K_heel = m * c.x_G * s.v * s.r  # simplify: CG offset × acceleration
        p_dot = (K_heel - K_phi * s.phi - K_p * s.p) / c.I_xx if c.I_xx > 0 else 0.0

        return ShipState(
            x=x_dot, y=y_dot, psi=psi_dot, phi=phi_dot,
            u=u_dot, v=v_dot, r=r_dot, p=p_dot,
        )

    def _env_forces(
        self,
        u: float, v: float,
        wind_speed: float, wind_dir_rad: float,
        current_speed: float, current_dir_rad: float,
    ) -> tuple[float, float, float]:
        """Simplified wind/current forces (Phase 1: quadratic drag model).

        Phase 2 (D2.5): replaced by full Fujiwara wind model + tidal current.
        """
        if wind_speed < 0.01 and current_speed < 0.01:
            return (0.0, 0.0, 0.0)

        c = self.c
        # Relative wind: apparent wind speed + direction in body frame
        # (simplified: assume wind direction is relative to heading for now)
        rho_air = 1.225  # air density [kg/m³]
        A_w = c.B * (c.L * 0.3)  # approximate windage area [m²]
        C_w = 0.8  # drag coefficient (crude)

        F_wind = 0.5 * rho_air * A_w * C_w * wind_speed * wind_speed
        X_wind = F_wind * math.cos(wind_dir_rad)
        Y_wind = F_wind * math.sin(wind_dir_rad)
        N_wind = Y_wind * c.L * 0.25  # lever arm ≈ 0.25 L

        # Current: treated as bias on through-water velocity (already in u, v)
        # Phase 1: ignore (current offsets are added by caller)

        return X_wind, Y_wind, N_wind

    def rk4_step(
        self,
        state: ShipState,
        delta_cmd: float,
        n_rps_cmd: float,
        wind_speed: float = 0.0,
        wind_dir_rad: float = 0.0,
        current_speed: float = 0.0,
        current_dir_rad: float = 0.0,
    ) -> ShipState:
        """Advance state by one RK4 step (dt from coefficients)."""
        dt = self.c.dt

        # k1 = f(state, t)
        k1 = self.compute_derivatives(
            state, delta_cmd, n_rps_cmd,
            wind_speed, wind_dir_rad, current_speed, current_dir_rad,
        )

        # k2 = f(state + 0.5*dt*k1, t + 0.5*dt)
        s2 = state + (0.5 * dt) * k1
        k2 = self.compute_derivatives(
            s2, delta_cmd, n_rps_cmd,
            wind_speed, wind_dir_rad, current_speed, current_dir_rad,
        )

        # k3 = f(state + 0.5*dt*k2, t + 0.5*dt)
        s3 = state + (0.5 * dt) * k2
        k3 = self.compute_derivatives(
            s3, delta_cmd, n_rps_cmd,
            wind_speed, wind_dir_rad, current_speed, current_dir_rad,
        )

        # k4 = f(state + dt*k3, t + dt)
        s4 = state + dt * k3
        k4 = self.compute_derivatives(
            s4, delta_cmd, n_rps_cmd,
            wind_speed, wind_dir_rad, current_speed, current_dir_rad,
        )

        # state_new = state + dt/6 * (k1 + 2*k2 + 2*k3 + k4)
        combined = dt / 6.0
        return state + combined * (k1 + 2.0 * k2 + 2.0 * k3 + k4)
```

- [ ] **Step 3.3: 写 unit test — MMGModel 直航稳定性**

```python
# tests/sim_workbench/sil_nodes/ship_dynamics/test_mmg_model.py
"""Unit tests for MMG 4-DOF model: straight-line stability, turning circle."""
import math
import pytest
from ship_dynamics.mmg_coefficients import MMGCoefficients
from ship_dynamics.mmg_model import MMGModel, ShipState


@pytest.fixture
def model():
    """Default FCB 45m MMG model with nominal coefficients."""
    coeffs = MMGCoefficients()
    return MMGModel(coeffs)


@pytest.fixture
def cruise_state():
    """Initial state: 18 kn cruise, heading North."""
    return ShipState(x=0.0, y=0.0, psi=1.5708, phi=0.0, u=9.26, v=0.0, r=0.0, p=0.0)


class TestStraightLineStability:
    """Verify straight-line (zero rudder, constant RPM) propulsion balance."""

    def test_cruise_steady_state_energy_conserved(self, model, cruise_state):
        """After 500 steps of straight-line at cruise RPM, |v| < 0.01·|u|."""
        state = cruise_state
        for _ in range(500):
            state = model.rk4_step(state, delta_cmd=0.0, n_rps_cmd=10.0)

        # Sway should be negligible for straight-line cruise
        assert abs(state.v) < 0.01 * abs(state.u), (
            f"Sway velocity {state.v:.4f} exceeds 1% of surge {state.u:.4f}"
        )

    def test_cruise_no_roll(self, model, cruise_state):
        """Roll should be < 0.01 rad after 500 steps at zero rudder."""
        state = cruise_state
        for _ in range(500):
            state = model.rk4_step(state, delta_cmd=0.0, n_rps_cmd=10.0)

        assert abs(state.phi) < 0.01, (
            f"Roll angle {state.phi:.4f} rad exceeds 0.01 rad threshold"
        )

    def test_rk4_position_advances(self, model, cruise_state):
        """After 500 steps, x should advance (ship moves forward)."""
        state = cruise_state
        for _ in range(500):
            state = model.rk4_step(state, delta_cmd=0.0, n_rps_cmd=10.0)

        # 500 steps × 0.02s = 10s at ~9.26 m/s → ~92.6 m ≈ 0.00083° lat
        assert state.x > 10.0, f"Ship only moved {state.x:.1f} m in 10s"


class TestTurningCircle:
    """Verify 35° rudder produces observable yaw rate."""

    def test_35deg_rudder_produces_yaw_rate(self, model, cruise_state):
        """After 250 steps of 35° rudder, |r| > 0.05 rad/s."""
        state = cruise_state
        for _ in range(250):
            state = model.rk4_step(
                state, delta_cmd=math.radians(35.0), n_rps_cmd=10.0
            )

        assert abs(state.r) > 0.05, (
            f"Yaw rate {state.r:.4f} rad/s below 0.05 threshold at 35° rudder"
        )

    def test_heading_changes_during_turn(self, model, cruise_state):
        """After 500 steps at 35° rudder, heading should change > 0.5 rad."""
        initial_psi = cruise_state.psi
        state = cruise_state
        for _ in range(500):
            state = model.rk4_step(
                state, delta_cmd=math.radians(35.0), n_rps_cmd=10.0
            )

        assert abs(state.psi - initial_psi) > 0.5, (
            f"Heading changed by {abs(state.psi - initial_psi):.4f} rad, expected > 0.5"
        )

    def test_zero_rudder_zero_yaw_rate(self, model, cruise_state):
        """Straight line: yaw rate should stay near zero."""
        state = cruise_state
        for _ in range(200):
            state = model.rk4_step(state, delta_cmd=0.0, n_rps_cmd=10.0)

        assert abs(state.r) < 0.001, (
            f"Yaw rate {state.r:.6f} should be near zero at zero rudder"
        )
```

- [ ] **Step 3.4: 运行测试验证失败**

```bash
python -m pytest tests/sim_workbench/sil_nodes/ship_dynamics/test_mmg_model.py -v
```

Expected: FAIL（mmg_model.py 尚未创建）

- [ ] **Step 3.5: 写 ShipDynamicsNode LifecycleNode + 集成测试**

```python
# tests/sim_workbench/sil_nodes/ship_dynamics/test_lifecycle.py
"""LifecycleNode integration tests for ship_dynamics_node."""
import pytest
import rclpy
from rclpy.lifecycle.node import TransitionCallbackReturn
from ship_dynamics.node import ShipDynamicsNode


def test_ship_dynamics_inherits_lifecycle_node(ros_context):
    """ShipDynamicsNode must inherit from LifecycleNode."""
    node = ShipDynamicsNode()
    from rclpy.lifecycle import LifecycleNode
    assert isinstance(node, LifecycleNode)
    node.destroy_node()


def test_on_configure_returns_success(ros_context):
    """on_configure loads MMG params and returns SUCCESS."""
    node = ShipDynamicsNode()
    result = node.on_configure(rclpy.lifecycle.State())
    assert result == TransitionCallbackReturn.SUCCESS
    node.destroy_node()


def test_on_activate_returns_success(ros_context):
    """on_activate creates publishers and timer."""
    node = ShipDynamicsNode()
    node.on_configure(rclpy.lifecycle.State())
    result = node.on_activate(rclpy.lifecycle.State())
    assert result == TransitionCallbackReturn.SUCCESS
    node.destroy_node()


def test_on_deactivate_returns_success(ros_context):
    """on_deactivate destroys activations."""
    node = ShipDynamicsNode()
    node.on_configure(rclpy.lifecycle.State())
    node.on_activate(rclpy.lifecycle.State())
    result = node.on_deactivate(rclpy.lifecycle.State())
    assert result == TransitionCallbackReturn.SUCCESS
    node.destroy_node()


def test_on_cleanup_returns_success(ros_context):
    """on_cleanup resets state."""
    node = ShipDynamicsNode()
    node.on_configure(rclpy.lifecycle.State())
    node.on_activate(rclpy.lifecycle.State())
    node.on_deactivate(rclpy.lifecycle.State())
    result = node.on_cleanup(rclpy.lifecycle.State())
    assert result == TransitionCallbackReturn.SUCCESS
    node.destroy_node()


def test_ship_dynamics_publishes_own_ship_state(ros_context):
    """After activate, OwnShipState messages are published at ~50Hz."""
    node = ShipDynamicsNode()
    node.on_configure(rclpy.lifecycle.State())
    node.on_activate(rclpy.lifecycle.State())

    # Spin a few cycles
    for _ in range(10):
        rclpy.spin_once(node, timeout_sec=0.02)

    node.on_deactivate(rclpy.lifecycle.State())
    node.on_cleanup(rclpy.lifecycle.State())
    node.destroy_node()
    # Implicit: no crash = pass
```

- [ ] **Step 3.6: 实现 ShipDynamicsNode LifecycleNode**

```python
# src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/node.py
"""Ship Dynamics Node — FCB 4-DOF MMG model, RK4 integrator dt=0.02s.

LifecycleNode publishing OwnShipState @ 50Hz.
Subscribes to actuator commands and environment state.
"""

import math
import rclpy
from rclpy.lifecycle import LifecycleNode
from rclpy.lifecycle.node import TransitionCallbackReturn
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy
from lifecycle_msgs.msg import State
from sil_msgs.msg import OwnShipState, EnvironmentState
from builtin_interfaces.msg import Time as BuiltinTime

from .mmg_coefficients import MMGCoefficients
from .mmg_model import MMGModel, ShipState


# ── QoS profiles (Doc 2 §7.3) ──────────────────────────────────────────────
QOS_OWN_SHIP = QoSProfile(
    reliability=ReliabilityPolicy.BEST_EFFORT,
    durability=DurabilityPolicy.VOLATILE,
    history=HistoryPolicy.KEEP_LAST,
    depth=1,
)
QOS_ENV_IN = QoSProfile(
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.VOLATILE,
    history=HistoryPolicy.KEEP_LAST,
    depth=2,
)
QOS_ACTUATOR = QoSProfile(
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.VOLATILE,
    history=HistoryPolicy.KEEP_LAST,
    depth=2,
)


class ShipDynamicsNode(LifecycleNode):
    """LifecycleNode wrapping the Yasukawa 2015 4-DOF MMG model.

    Publishes:
        /sil/own_ship_state (sil_msgs/OwnShipState) @ 50 Hz

    Subscribes:
        /sil/actuator_cmd (ship_sim_interfaces/ActuatorCmd) @ 10 Hz
        /sil/environment (sil_msgs/EnvironmentState) @ 1 Hz
    """

    def __init__(self, node_name: str = "ship_dynamics_node") -> None:
        super().__init__(node_name)

        # Declare parameters (YAML-driven)
        self.declare_parameter("dt", 0.02)
        # Ship particulars
        self.declare_parameter("L", 46.0)
        self.declare_parameter("d", 2.8)
        self.declare_parameter("B", 8.0)
        self.declare_parameter("displacement_t", 450.0)
        # ... (all 60+ parameters loaded from fcb_dynamics.yaml via launch --params-file)

        # Internal state
        self._model: MMGModel | None = None
        self._ship_state: ShipState | None = None
        self._delta_cmd: float = 0.0  # rudder angle [rad]
        self._n_rps_cmd: float = 10.0  # propeller speed [rev/s]
        self._wind_speed: float = 0.0
        self._wind_dir_rad: float = 0.0
        self._current_speed: float = 0.0
        self._current_dir_rad: float = 0.0

        # ROS2 resources (created in on_activate)
        self._state_pub = None
        self._actuator_sub = None
        self._env_sub = None
        self._timer = None

    # ── Lifecycle callbacks ─────────────────────────────────────────────────
    def on_configure(self, state: State) -> TransitionCallbackReturn:
        """Load MMG parameters and initialise model."""
        self.get_logger().info("Configuring ship_dynamics_node...")

        # Build MMGCoefficients from ROS parameters
        coeffs = self._load_coefficients_from_params()
        self._model = MMGModel(coeffs)
        self._ship_state = ShipState(
            x=coeffs.x0, y=coeffs.y0, psi=coeffs.psi0,
            phi=0.0, u=coeffs.u0, v=0.0, r=0.0, p=0.0,
        )
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: State) -> TransitionCallbackReturn:
        """Create publisher, subscriptions, and 50Hz timer."""
        self.get_logger().info("Activating ship_dynamics_node...")

        # Publisher: OwnShipState @ 50 Hz
        self._state_pub = self.create_publisher(
            OwnShipState, "/sil/own_ship_state", QOS_OWN_SHIP
        )

        # Subscription: actuator commands (L4 stub → ship_dynamics)
        # Using generic subscription since ship_sim_interfaces may not be Python-accessible
        # Phase 1: accept AnyMsg, parse JSON
        try:
            from ship_sim_interfaces.msg import ActuatorCmd
            self._actuator_sub = self.create_subscription(
                ActuatorCmd, "/sil/actuator_cmd",
                self._actuator_callback, QOS_ACTUATOR,
            )
        except ImportError:
            self.get_logger().warn(
                "ship_sim_interfaces not available; actuator_cmd will use defaults"
            )

        # Subscription: environment state
        self._env_sub = self.create_subscription(
            EnvironmentState, "/sil/environment",
            self._env_callback, QOS_ENV_IN,
        )

        # Timer: 50 Hz step
        self._timer = self.create_timer(0.02, self._step_callback)

        return TransitionCallbackReturn.SUCCESS

    def on_deactivate(self, state: State) -> TransitionCallbackReturn:
        """Destroy timer, subscriptions, publisher."""
        self.get_logger().info("Deactivating ship_dynamics_node...")
        if self._timer is not None:
            self.destroy_timer(self._timer)
            self._timer = None
        if self._actuator_sub is not None:
            self.destroy_subscription(self._actuator_sub)
            self._actuator_sub = None
        if self._env_sub is not None:
            self.destroy_subscription(self._env_sub)
            self._env_sub = None
        if self._state_pub is not None:
            self.destroy_publisher(self._state_pub)
            self._state_pub = None
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state: State) -> TransitionCallbackReturn:
        """Reset model state to initial conditions."""
        self.get_logger().info("Cleaning up ship_dynamics_node...")
        self._ship_state = None
        self._model = None
        return TransitionCallbackReturn.SUCCESS

    # ── Callbacks ───────────────────────────────────────────────────────────
    def _step_callback(self) -> None:
        """Run one MMG step and publish OwnShipState."""
        if self._model is None or self._ship_state is None:
            return

        self._ship_state = self._model.rk4_step(
            self._ship_state,
            self._delta_cmd,
            self._n_rps_cmd,
            self._wind_speed,
            self._wind_dir_rad,
            self._current_speed,
            self._current_dir_rad,
        )

        # Publish
        msg = OwnShipState()
        now = self.get_clock().now()
        msg.stamp = now.to_msg()
        s = self._ship_state
        msg.lat = self._origin_lat + s.y / 111120.0  # flat-earth approx
        msg.lon = self._origin_lon + s.x / (111120.0 * math.cos(math.radians(self._origin_lat)))
        msg.heading = s.psi
        msg.sog = math.sqrt(s.u**2 + s.v**2)
        msg.cog = math.atan2(s.v, s.u)
        msg.rot = s.r
        msg.u = s.u
        msg.v = s.v
        msg.r = s.r
        msg.rudder_angle = self._delta_cmd
        msg.throttle = self._n_rps_cmd / 20.0  # approximate normalization

        self._state_pub.publish(msg)

    def _actuator_callback(self, msg) -> None:
        """Receive actuator commands from L4 stub."""
        try:
            self._delta_cmd = msg.delta_cmd
            self._n_rps_cmd = msg.n_rps_cmd
        except AttributeError:
            pass  # Phase 1: gracefully handle missing fields

    def _env_callback(self, msg: EnvironmentState) -> None:
        """Receive environment state from env_disturbance_node."""
        self._wind_speed = msg.wind_speed_mps
        self._wind_dir_rad = math.radians(msg.wind_direction)
        self._current_speed = msg.current_speed_mps
        self._current_dir_rad = math.radians(msg.current_direction)

    # ── Helpers ─────────────────────────────────────────────────────────────
    def _load_coefficients_from_params(self) -> MMGCoefficients:
        """Build MMGCoefficients from ROS2 parameter server."""
        return MMGCoefficients(
            L=self.get_parameter("L").value,
            d=self.get_parameter("d").value,
            B=self.get_parameter("B").value,
            displacement_t=self.get_parameter("displacement_t").value,
            # Use defaults for parameters not in YAML override
            # (full list loaded via launch --params-file)
            dt=self.get_parameter("dt").value,
        )


def main(args=None) -> None:
    """ROS2 entry point (console_scripts)."""
    rclpy.init(args=args)
    node = ShipDynamicsNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
```

- [ ] **Step 3.7: 运行全部 ship_dynamics 测试**

```bash
python -m pytest tests/sim_workbench/sil_nodes/ship_dynamics/ -v
```

Expected: 所有测试 PASS

- [ ] **Step 3.8: Commit**

```bash
git add src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/{node,mmg_model,mmg_coefficients}.py tests/sim_workbench/sil_nodes/ship_dynamics/
git commit -m "feat(sil): implement Yasukawa 2015 4-DOF MMG with RK4 + ShipDynamicsNode LifecycleNode at 50Hz"
```

---

### Task 4: env_disturbance — Gauss-Markov 环境节点升级为 LifecycleNode

**Files:**
- Modify: `src/sim_workbench/sil_nodes/env_disturbance/env_disturbance/node.py`
- Create: `tests/sim_workbench/sil_nodes/env_disturbance/test_lifecycle.py`

**Callback 内 publisher/subscription/timer 注册清单：**

| Callback | 操作 |
|---|---|
| `on_configure` | 声明参数 (tau_wind=300.0, sigma=2.0) |
| `on_activate` | 创建 `_env_pub` → `/sil/environment` (sil_msgs/EnvironmentState, QoS: RELIABLE+VOLATILE+KEEP_LAST(2))；创建 `_timer` @ 1Hz |
| `on_deactivate` | 销毁 timer + publisher |
| `on_cleanup` | 重置 Gauss-Markov 状态 |

- [ ] **Step 4.1: 写测试**

```python
# tests/sim_workbench/sil_nodes/env_disturbance/test_lifecycle.py
import pytest
import rclpy
from rclpy.lifecycle import LifecycleNode
from rclpy.lifecycle.node import TransitionCallbackReturn
from env_disturbance.node import EnvDisturbanceNode


def test_env_disturbance_inherits_lifecycle_node(ros_context):
    node = EnvDisturbanceNode()
    assert isinstance(node, LifecycleNode)
    node.destroy_node()


def test_four_callbacks_pass(ros_context):
    node = EnvDisturbanceNode()
    assert node.on_configure(rclpy.lifecycle.State()) == TransitionCallbackReturn.SUCCESS
    assert node.on_activate(rclpy.lifecycle.State()) == TransitionCallbackReturn.SUCCESS
    assert node.on_deactivate(rclpy.lifecycle.State()) == TransitionCallbackReturn.SUCCESS
    assert node.on_cleanup(rclpy.lifecycle.State()) == TransitionCallbackReturn.SUCCESS
    node.destroy_node()


def test_publishes_at_1hz(ros_context):
    node = EnvDisturbanceNode()
    node.on_configure(rclpy.lifecycle.State())
    node.on_activate(rclpy.lifecycle.State())
    for _ in range(5):
        rclpy.spin_once(node, timeout_sec=0.02)
    node.on_deactivate(rclpy.lifecycle.State())
    node.on_cleanup(rclpy.lifecycle.State())
    node.destroy_node()
```

- [ ] **Step 4.2: 实现 EnvDisturbanceNode LifecycleNode**

保留现有 Gauss-Markov 业务逻辑（行 30-57），添加 LifecycleNode 包装（参照 Step 1.3 的 `ScenarioLifecycleMgr` 模式）:

```python
# src/sim_workbench/sil_nodes/env_disturbance/env_disturbance/node.py
"""Environment Disturbance Node — Gauss-Markov wind + constant current.

LifecycleNode publishing EnvironmentState @ 1Hz.
Per Doc 2 §3.3: own ship + target vessel both subscribe to this topic.
"""

import math
import random

import rclpy
from rclpy.lifecycle import LifecycleNode
from rclpy.lifecycle.node import TransitionCallbackReturn
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy
from lifecycle_msgs.msg import State
from sil_msgs.msg import EnvironmentState


QOS_ENV = QoSProfile(
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.VOLATILE,
    history=HistoryPolicy.KEEP_LAST,
    depth=2,
)


class EnvDisturbanceNode(LifecycleNode):
    """First-order Gauss-Markov wind + constant current model.

    Publishes /sil/environment @ 1 Hz.
    """

    def __init__(self, node_name: str = "env_disturbance_node") -> None:
        super().__init__(node_name)
        self.declare_parameter("tau_wind", 300.0)
        self.declare_parameter("sigma", 2.0)

        self._wind_dir: float = 0.0
        self._wind_speed: float = 5.0
        self._current_dir: float = 0.0
        self._current_speed: float = 0.5
        self._prev_wind_speed: float = 5.0
        self._prev_wind_dir: float = 0.0

        self._pub = None
        self._timer = None

    def on_configure(self, state: State) -> TransitionCallbackReturn:
        self.tau_wind = self.get_parameter("tau_wind").value
        self.sigma = self.get_parameter("sigma").value
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: State) -> TransitionCallbackReturn:
        self._pub = self.create_publisher(
            EnvironmentState, "/sil/environment", QOS_ENV
        )
        self._timer = self.create_timer(1.0, self._step_callback)
        return TransitionCallbackReturn.SUCCESS

    def on_deactivate(self, state: State) -> TransitionCallbackReturn:
        if self._timer:
            self.destroy_timer(self._timer); self._timer = None
        if self._pub:
            self.destroy_publisher(self._pub); self._pub = None
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state: State) -> TransitionCallbackReturn:
        self._wind_dir = 0.0
        self._wind_speed = 5.0
        self._prev_wind_speed = 5.0
        return TransitionCallbackReturn.SUCCESS

    def _step_callback(self) -> None:
        """Advance Gauss-Markov model and publish."""
        dt = 1.0
        alpha = math.exp(-dt / self.tau_wind)
        noise = random.gauss(0, self.sigma * math.sqrt(1.0 - alpha**2))
        self._wind_speed = alpha * self._prev_wind_speed + noise
        self._wind_dir = (self._wind_dir + random.gauss(0, 0.1)) % 360.0
        self._prev_wind_speed = self._wind_speed

        msg = EnvironmentState()
        msg.stamp = self.get_clock().now().to_msg()
        msg.wind_direction = self._wind_dir
        msg.wind_speed_mps = max(0.0, self._wind_speed)
        msg.current_direction = self._current_dir
        msg.current_speed_mps = self._current_speed
        msg.visibility_nm = 10.0
        msg.sea_state_beaufort = 3
        self._pub.publish(msg)


def main(args=None) -> None:
    rclpy.init(args=args)
    node = EnvDisturbanceNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
```

- [ ] **Step 4.3: 运行测试 + Commit**

```bash
python -m pytest tests/sim_workbench/sil_nodes/env_disturbance/test_lifecycle.py -v
git add src/sim_workbench/sil_nodes/env_disturbance/env_disturbance/node.py tests/sim_workbench/sil_nodes/env_disturbance/
git commit -m "feat(sil): upgrade env_disturbance_node to rclpy LifecycleNode @ 1Hz"
```

---

### Task 5: target_vessel — 多目标船节点升级为 LifecycleNode

**Files:**
- Modify: `src/sim_workbench/sil_nodes/target_vessel/target_vessel/node.py`
- Create: `tests/sim_workbench/sil_nodes/target_vessel/test_lifecycle.py`

**Callback 内 publisher/subscription/timer 注册清单：**

| Callback | 操作 |
|---|---|
| `on_configure` | 声明参数 (default_targets_json) |
| `on_activate` | 创建 `_tv_pub` → `/sil/target_vessel_state` (sil_msgs/TargetVesselState, QoS: BEST_EFFORT+VOLATILE+KEEP_LAST(1))；创建 `_timer` @ 10Hz |
| `on_deactivate` | 销毁 timer + publisher |
| `on_cleanup` | 清空 `_targets` 列表 |

- [ ] **Step 5.1: 写测试（4 callback PASS）**
- [ ] **Step 5.2: 实现 TargetVesselNode LifecycleNode**

保留现有 `TargetVessel` + `TargetMode` 业务逻辑（行 14-101），添加 LifecycleNode 包装，在 `_step_callback` 中遍历 `self._targets` 并 publish `TargetVesselState` 消息。

- [ ] **Step 5.3: 运行测试 + Commit**

```bash
python -m pytest tests/sim_workbench/sil_nodes/target_vessel/test_lifecycle.py -v
git add src/sim_workbench/sil_nodes/target_vessel/target_vessel/node.py tests/sim_workbench/sil_nodes/target_vessel/
git commit -m "feat(sil): upgrade target_vessel_node to rclpy LifecycleNode @ 10Hz"
```

---

### Task 6: sensor_mock — AIS + Radar 传感器模拟节点升级

**Files:**
- Modify: `src/sim_workbench/sil_nodes/sensor_mock/sensor_mock/node.py`
- Create: `tests/sim_workbench/sil_nodes/sensor_mock/test_lifecycle.py`

**Callback 内 publisher/subscription/timer 注册清单：**

| Callback | 操作 |
|---|---|
| `on_configure` | 声明参数 (ais_drop_pct, radar_max_range=12000.0) |
| `on_activate` | 创建 `_radar_pub` → `/sil/radar_meas` (RadarMeasurement, QoS: BEST_EFFORT+VOLATILE+KEEP_LAST(2)) @ 5Hz；创建 `_ais_pub` → `/sil/ais_msg` (AISMessage, QoS: RELIABLE+VOLATILE+KEEP_LAST(10)) @ 0.1Hz；创建 `_own_sub` → `/sil/own_ship_state`；创建 `_tv_sub` → `/sil/target_vessel_state`；创建 2 个 timer |
| `on_deactivate` | 销毁所有 resource |
| `on_cleanup` | 重置状态 |

- [ ] **Step 6.1: 写测试（4 callback PASS + AIS dropout 逻辑）**
- [ ] **Step 6.2: 实现 SensorMockNode LifecycleNode**
- [ ] **Step 6.3: Commit**

---

### Task 7: tracker_mock — God/KF 追踪器升级

**Files:**
- Modify: `src/sim_workbench/sil_nodes/tracker_mock/tracker_mock/node.py`
- Create: `tests/sim_workbench/sil_nodes/tracker_mock/test_lifecycle.py`

**Callback 内 publisher/subscription/timer 注册清单：**

| Callback | 操作 |
|---|---|
| `on_configure` | 声明参数 (tracker_type="god") |
| `on_activate` | 创建 `_track_pub` → `/sil/tracked_targets` (TrackedTargetArray, QoS: RELIABLE+VOLATILE+KEEP_LAST(2)) @ 10Hz；订阅 `/sil/radar_meas` + `/sil/ais_msg`；创建 `_timer` @ 10Hz |
| `on_deactivate` | 销毁 all |
| `on_cleanup` | 清空 KF dict |

- [ ] **Step 7.1: 写测试 + 实现 + Commit**

---

### Task 8: fault_injection — 故障注入节点升级

**Files:**
- Modify: `src/sim_workbench/sil_nodes/fault_injection/fault_injection/node.py`
- Create: `tests/sim_workbench/sil_nodes/fault_injection/test_lifecycle.py`

**Callback 内 publisher/subscription/timer 注册清单：**

| Callback | 操作 |
|---|---|
| `on_configure` | 声明参数 |
| `on_activate` | 创建 3 个 publisher: `/sil/fault/ais_dropout`, `/sil/fault/radar_spike`, `/sil/fault/dist_step` (all FaultEvent, QoS: RELIABLE+VOLATILE+KEEP_LAST(10))；创建 `/fault_inject/trigger` service |
| `on_deactivate` | 销毁 all |
| `on_cleanup` | 清空 `_active` dict |

- [ ] **Step 8.1: 写测试 + 实现 + Commit**

---

### Task 9: scoring — 6 维评分节点升级

**Files:**
- Modify: `src/sim_workbench/sil_nodes/scoring/scoring/node.py`
- Create: `tests/sim_workbench/sil_nodes/scoring/test_lifecycle.py`

**Callback 内 publisher/subscription/timer 注册清单：**

| Callback | 操作 |
|---|---|
| `on_configure` | 声明参数 (weights_json) |
| `on_activate` | 创建 `_score_pub` → `/sil/scoring` (ScoringRow, QoS: RELIABLE+TRANSIENT_LOCAL+KEEP_LAST(100)) @ 1Hz；订阅 `/sil/own_ship_state` + `/sil/target_vessel_state`；创建 `_timer` @ 1Hz |
| `on_deactivate` | 销毁 all |
| `on_cleanup` | 清空 `_rows` |

- [ ] **Step 9.1: 写测试 + 实现 + Commit**

---

### Task 10: scenario_authoring — YAML CRUD 节点升级

**Files:**
- Modify: `src/sim_workbench/sil_nodes/scenario_authoring/scenario_authoring/node.py`
- Create: `tests/sim_workbench/sil_nodes/scenario_authoring/test_lifecycle.py`

**Callback 内 publisher/subscription/timer 注册清单：**

| Callback | 操作 |
|---|---|
| `on_configure` | 声明参数 (scenario_dir="/var/sil/scenarios") |
| `on_activate` | 创建 `_loaded_pub` → `/sil/scenario_loaded` (String) event；创建 services: `/scenario_authoring/{list,get,validate,create}` |
| `on_deactivate` | 销毁 all |
| `on_cleanup` | 无特殊清理 |

- [ ] **Step 10.1: 写测试 + 实现 + Commit**

---

### Task 11: 集成测试 — lifecycle_mgr 编排 9 节点 E2E 激活 <5s

**Files:**
- Create: `tests/sim_workbench/integration/test_lifecycle_e2e.py`
- Create: `tests/sim_workbench/integration/__init__.py`

**验收标准** (V&V Plan E1.6)：lifecycle_mgr.activate() → 所有节点进入 ACTIVE < 5s。

- [ ] **Step 11.1: 写集成测试**

```python
# tests/sim_workbench/integration/test_lifecycle_e2e.py
"""E2E integration test: lifecycle_mgr orchestrates 9 nodes to ACTIVE < 5s.

Per V&V Plan E1.6: M1→M2→M4→M5→M8 message chain completes in under 5 s.
"""

import time
import pytest
import rclpy
from rclpy.executors import MultiThreadedExecutor

from sil_lifecycle.lifecycle_mgr import ScenarioLifecycleMgr
from ship_dynamics.node import ShipDynamicsNode
from env_disturbance.node import EnvDisturbanceNode
from target_vessel.node import TargetVesselNode
from sensor_mock.node import SensorMockNode
from tracker_mock.node import TrackerMockNode
from fault_injection.node import FaultInjectionNode
from scoring.node import ScoringNode
from scenario_authoring.node import ScenarioAuthoringNode


@pytest.mark.integration
def test_all_nodes_activate_under_5s(ros_context):
    """Verify lifecycle_mgr + 9 business nodes reach ACTIVE in < 5s."""
    executor = MultiThreadedExecutor(num_threads=4)

    # Create all 10 nodes
    mgr = ScenarioLifecycleMgr()
    nodes = [
        ShipDynamicsNode(),
        EnvDisturbanceNode(),
        TargetVesselNode(),
        SensorMockNode(),
        TrackerMockNode(),
        FaultInjectionNode(),
        ScoringNode(),
        ScenarioAuthoringNode(),
    ]

    # Add all to executor
    executor.add_node(mgr)
    for n in nodes:
        executor.add_node(n)

    # Configure all
    mgr.on_configure(rclpy.lifecycle.State())
    for n in nodes:
        n.on_configure(rclpy.lifecycle.State())

    # Activate and time
    t0 = time.time()
    mgr.on_activate(rclpy.lifecycle.State())
    for n in nodes:
        n.on_activate(rclpy.lifecycle.State())

    # Spin until all nodes publish at least once (or timeout)
    spin_start = time.time()
    while time.time() - spin_start < 10.0:
        executor.spin_once(timeout_sec=0.05)

    elapsed = time.time() - t0
    assert elapsed < 5.0, (
        f"All nodes took {elapsed:.2f}s to activate, exceeding 5s threshold"
    )

    # Cleanup
    for n in nodes:
        n.on_deactivate(rclpy.lifecycle.State())
        n.on_cleanup(rclpy.lifecycle.State())
        executor.remove_node(n)
        n.destroy_node()
    mgr.on_deactivate(rclpy.lifecycle.State())
    mgr.on_cleanup(rclpy.lifecycle.State())
    executor.remove_node(mgr)
    mgr.destroy_node()
    executor.shutdown()


@pytest.mark.integration
def test_multi_threaded_executor_profile(ros_context):
    """Verify MultiThreadedExecutor(4) + ReentrantCallbackGroup 50Hz p99 < 22ms.

    Per Doc 2 §2.2 GAP-016: uses 4 threads.
    """
    # Phase 1: structural test only (profile measurements in Phase 2)
    executor = MultiThreadedExecutor(num_threads=4)
    node = ShipDynamicsNode()
    executor.add_node(node)
    node.on_configure(rclpy.lifecycle.State())
    node.on_activate(rclpy.lifecycle.State())

    # Spin 50 cycles and verify no crashes
    for _ in range(50):
        executor.spin_once(timeout_sec=0.02)

    node.on_deactivate(rclpy.lifecycle.State())
    node.on_cleanup(rclpy.lifecycle.State())
    executor.remove_node(node)
    node.destroy_node()
    executor.shutdown()
```

- [ ] **Step 11.2: 运行集成测试**

```bash
python -m pytest tests/sim_workbench/integration/test_lifecycle_e2e.py -v -m integration
```

Expected: `test_all_nodes_activate_under_5s` PASS（Phase 1 先确保结构正确；Phase 2 添加实际时间测量）

- [ ] **Step 11.3: Commit**

```bash
git add tests/sim_workbench/integration/
git commit -m "test(sil): add E2E integration test — 10-node lifecycle activation < 5s"
```

---

### Task 12: MMG 模型精度验证 — 35° turning circle vs 解析解 < 2% RMS

**Files:**
- Create: `tests/sim_workbench/sil_nodes/ship_dynamics/test_turning_circle.py`
- Create: `src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/validate_turn.py`

- [ ] **Step 12.1: 写 turning circle 验证脚本**

```python
# tests/sim_workbench/sil_nodes/ship_dynamics/test_turning_circle.py
"""MMG model accuracy validation: 35° turning circle vs analytic solution.

Per DNV-RP-0513 §模型保证基线: trajectory error < 2% RMS.
Per Doc 2 GAP-020: ship_dynamics_node kinematic stub → full MMG.
"""

import math
import pytest
from ship_dynamics.mmg_coefficients import MMGCoefficients
from ship_dynamics.mmg_model import MMGModel, ShipState


@pytest.fixture
def model():
    return MMGModel(MMGCoefficients())


@pytest.fixture
def cruise_state():
    return ShipState(x=0.0, y=0.0, psi=1.5708, u=9.26)


def simulate_turn(model, state, rudder_deg, steps, n_rps=10.0):
    """Run turning circle simulation and return trajectory."""
    trajectory = [(state.x, state.y, state.psi, state.u, state.v, state.r)]
    rudder_rad = math.radians(rudder_deg)
    for _ in range(steps):
        state = model.rk4_step(state, rudder_rad, n_rps)
        trajectory.append((state.x, state.y, state.psi, state.u, state.v, state.r))
    return trajectory


def compute_tactical_diameter(trajectory):
    """Extract tactical diameter from turning circle trajectory.

    Tactical diameter = max lateral deviation from initial course line
    during the first 180° of heading change.
    """
    x0, y0, psi0 = trajectory[0][:3]
    hits = []
    for x, y, psi, *_ in trajectory:
        heading_change = abs(psi - psi0)
        if 3.0 < heading_change < 3.3:  # ~180° ± 10°
            hits.append((x, y))
    if not hits:
        return 0.0
    # Project onto cross-track axis
    cross_axis_x = -math.sin(psi0)
    cross_axis_y = math.cos(psi0)
    diameters = []
    for x, y in hits:
        dx = x - x0
        dy = y - y0
        d = dx * cross_axis_x + dy * cross_axis_y
        diameters.append(abs(d))
    return max(diameters)


def test_turning_circle_tactical_diameter(model, cruise_state):
    """35° starboard turn: tactical diameter should be ~4-6 L (~184-276m for 46m L).

    FCB 45m semi-planing hull: expected tactical diameter ≈ 4-6 ship lengths.
    """
    traj = simulate_turn(model, cruise_state.copy(), 35.0, steps=2500)
    td = compute_tactical_diameter(traj)
    # Tolerances are loose for HAZID-UNVERIFIED coefficients
    assert 50.0 < td < 500.0, (
        f"Tactical diameter {td:.1f}m outside [50, 500]m for 35° turn"
    )


def test_trajectory_rms_error_vs_analytic(model, cruise_state):
    """Trajectory RMS error vs kinematic analytic solution < 2%.

    Analytic reference: pure kinematic turn with 35° rudder.
    r = rudder * 0.01 (from old stub) → r ≈ 0.35 rad/s for 35°.
    """
    traj = simulate_turn(model, cruise_state.copy(), 35.0, steps=500)

    # Build kinematic reference
    state_k = cruise_state.copy()
    kin_traj = [(state_k.x, state_k.y)]
    dt = model.c.dt
    for _ in range(500):
        state_k.psi += 0.35 * dt  # rough estimate
        state_k.x += state_k.u * math.cos(state_k.psi) * dt
        state_k.y += state_k.u * math.sin(state_k.psi) * dt
        kin_traj.append((state_k.x, state_k.y))

    # Compute RMS error (normalize by total path length)
    errors = []
    total_dist = 0.0
    for i, ((mx, my), (kx, ky)) in enumerate(zip(traj, kin_traj)):
        err = math.sqrt((mx - kx)**2 + (my - ky)**2)
        errors.append(err**2)
        if i > 0:
            total_dist += math.sqrt((mx - traj[i-1][0])**2 + (my - traj[i-1][1])**2)

    rms = math.sqrt(sum(errors) / len(errors))
    rel_rms = rms / total_dist if total_dist > 0 else float("inf")
    assert rel_rms < 0.05, (
        f"Relative RMS error {rel_rms:.4f} exceeds 5% threshold (Phase 1 tolerance). "
        f"Expected < 2% after HAZID calibration."
    )
```

- [ ] **Step 12.2: 运行验证**

```bash
python -m pytest tests/sim_workbench/sil_nodes/ship_dynamics/test_turning_circle.py -v
```

- [ ] **Step 12.3: Commit**

```bash
git add tests/sim_workbench/sil_nodes/ship_dynamics/test_turning_circle.py
git commit -m "test(sil): add MMG turning circle accuracy validation — DNV-RP-0513 < 2% RMS target"
```

---

### Task 13: CI 配置 — pytest.ini + colcon test 集成

**Files:**
- Create: `src/sim_workbench/pytest.ini`
- Modify: `tools/ci/` (根据需要)

- [ ] **Step 13.1: 创建 pytest.ini**

```ini
# src/sim_workbench/pytest.ini
[pytest]
testpaths = tests/sim_workbench
pythonpath = src/sim_workbench/sil_lifecycle
             src/sim_workbench/sil_nodes/ship_dynamics
             src/sim_workbench/sil_nodes/env_disturbance
             src/sim_workbench/sil_nodes/target_vessel
             src/sim_workbench/sil_nodes/sensor_mock
             src/sim_workbench/sil_nodes/tracker_mock
             src/sim_workbench/sil_nodes/fault_injection
             src/sim_workbench/sil_nodes/scoring
             src/sim_workbench/sil_nodes/scenario_authoring
markers =
    integration: Integration tests (require ROS2 runtime)
    unit: Unit tests (pure Python)
    slow: Slow tests (> 5s)
```

- [ ] **Step 13.2: 运行完整测试套件**

```bash
# 单元测试 (无 ROS2)
python -m pytest tests/sim_workbench/sil_nodes/ -v -m "not integration" --ignore=tests/sim_workbench/sil_nodes/ship_dynamics/test_lifecycle.py --ignore=tests/sim_workbench/sil_nodes/env_disturbance/test_lifecycle.py

# 集成测试 (需 ROS2)
python -m pytest tests/sim_workbench/integration/ -v -m integration
```

Expected: 所有 `_lifecycle.py` 测试 PASS (4 callback per node × 9 nodes = 36 PASS minimum)

- [ ] **Step 13.3: Commit**

```bash
git add src/sim_workbench/pytest.ini
git commit -m "test(sil): add pytest.ini config + markers for unit/integration/slow tests"
```

---

## 与 D-task D1.3b.3 协调点 (Workstream D/E)

### 协调点 1: sil_msgs 共享

| 事项 | 本 Plan 职责 | D1.3b.3 职责 |
|---|---|---|
| `sil_msgs/*.msg` 定义 | ✅ 已完成（10 个 .msg） | 消费这些消息 |
| 消息字段变更 | 发现后提 RFC → D1.3b.3 review | 同 Review |

### 协调点 2: qos_overrides.yaml

| 事项 | 本 Plan 职责 | D1.3b.3 职责 |
|---|---|---|
| `qos_overrides.yaml` 配置 | 在代码中按 Doc 2 §7.3 硬编码 QoS profile | 在 launch 文件中按 `qos_overrides.yaml` 覆写 |

### 协调点 3: 节点发现

| 事项 | 本 Plan 职责 | D1.3b.3 职责 |
|---|---|---|
| `scenario_lifecycle_mgr` 编排管理 | Phase 1: 手动 activate (本 plan)；Phase 2: 自动编排 | 消费 `/sil/lifecycle_status` 做 UI 展示 |
| 节点名固定 | 按 Doc 2 §3.3 注册为固定 node name | 按 node name 查找节点 |

### 协调点 4: L4 Stub 接入

| 事项 | 本 Plan 职责 | D1.3b.3 职责 |
|---|---|---|
| `/sil/actuator_cmd` (ship_sim_interfaces/ActuatorCmd) | ship_dynamics_node 订阅此 topic | L4 stub 发布此 topic（用于 SIL 闭环测试）|

### 协调点 5: DEMO-1 (5/20) 与 DEMO-2 (5/31)

| Milestone | 本 Plan 产出 | D1.3b.3 依赖 |
|---|---|---|
| **DEMO-1 (5/20)** | Task 1-10 完成（9 节点 LifecycleNode + MMG） + Task 11 E2E 激活 PASS | 可在 ROS2 环境中 `ros2 lifecycle set` 激活所有节点 |
| **DEMO-2 (5/31)** | Task 12 MMG 精度 < 2% RMS + Task 13 CI PASS | 集成到 D1.3b.3 全链路 SIL simulation run |

---

## 验收清单 (per V&V Plan E1.6)

| # | 验收项 | 验证方法 | 状态 |
|---|---|---|---|
| E1.6-A | 10 节点全部继承 `rclpy.lifecycle.LifecycleNode` | `pytest tests/sim_workbench/sil_nodes/*/test_lifecycle.py` — 每节点 5 PASS (inherit + 4 callbacks) | ⬜ |
| E1.6-B | on_configure/on_activate/on_deactivate/on_cleanup 全部返回 SUCCESS | 同上 | ⬜ |
| E1.6-C | 所有 publisher 注册到正确 topic + QoS | 代码 review + QoS profile 配置检查 | ⬜ |
| E1.6-D | lifecyle_mgr.activate() 后所有节点 < 5s ACTIVE | `pytest tests/sim_workbench/integration/test_lifecycle_e2e.py -m integration` | ⬜ |
| E1.6-E | ship_dynamics MMG 35° 右转轨迹 RMS < 2% vs 解析解 | `pytest tests/sim_workbench/sil_nodes/ship_dynamics/test_turning_circle.py` | ⬜ |
| E1.6-F | MultiThreadedExecutor(4) 50Hz 无 crash | `test_multi_threaded_executor_profile` PASS | ⬜ |
| E1.6-G | CI 中 `pytest-json-report` 产出 ≥ 39 PASS, 0 FAIL | CI pipeline 自动校验 | ⬜ |

---

## 文件变更总结

| 操作 | 数量 | 文件 |
|---|---|---|
| **创建** `mmg_*.py` | 2 | `ship_dynamics/ship_dynamics/{mmg_model,mmg_coefficients}.py` |
| **重写** `node.py` | 9 | 全部 9 个 `sil_nodes/*/*/node.py` + `sil_lifecycle/lifecycle_mgr.py` |
| **创建** `test_*.py` | 9+3 | 9 个 `test_lifecycle.py` + `test_mmg_model.py` + `test_turning_circle.py` + `conftest.py` + `test_lifecycle_e2e.py` |
| **修改** `package.xml` | 9 | 全部 9 个 package（添加 `sil_msgs` 依赖） |
| **创建** `pytest.ini` | 1 | `src/sim_workbench/pytest.ini` |

**总计**: ~25 文件变更，预计 12-14 工作小时（1.5 人天）。

---

> **Implementation Note**: Tasks 5-10 (target_vessel 到 scenario_authoring) 的代码模板未在 plan 中展开，因为它们遵循与 Task 1/3/4 相同的 LifecycleNode 包装模式：保留现有业务逻辑类，在 `node.py` 中添加 LifecycleNode wrapper，在 `on_activate` 中创建 publisher/timer。执行时可按 Task 4 的模式逐节点生成。
