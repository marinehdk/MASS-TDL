# GNC Plant 运行时 Reset 接口 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** GNC profile 下 orchestrator 每次 scenario configure 时 reset GNC plant 的 own-ship 到 scenario 真实起点（WGS84 lat/lon），实现跨 scenario 可复现。

**Architecture:** orchestrator（domain 42）发 ShipReset msg → gnc_bridge 第5条 L3→GNC 转发 → domain 50 内 ship_dynamics + coordinate_transform 各取所需字段 reset（eta_→0/0，origin→新 lat/lon）。四处改动，纯新增，向后兼容。

**Tech Stack:** ROS2 Humble (C++ rclcpp + Python rclpy), ship_interfaces/sil_msgs/l3_external_msgs (rosidl), colcon build, pytest.

**Spec:** `docs/superpowers/specs/2026-06-26-gnc-plant-reset-interface-design.md`

**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-gnc-debug`, branch `codex/colregs-gnc-debug`

**Build/run context:**
- GNC stack: `bash scripts/gnc-profile-start.sh up`（冷启 gnc-nodes + sil-nodes + bridge）
- 验证探针: `SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule14-ho --profile gnc --sim-rate 10`
- C++ 改 GNC 后需重建: `bash scripts/gnc-profile-start.sh --down && bash scripts/gnc-profile-start.sh up`（gnc-nodes 镜像 mass-l3-gnc:mpc_latest 需重建；bridge 镜像 codex-gnc-validation 自动重建）

---

## File Structure

| 文件 | 改动 | 责任 |
|------|------|------|
| `third_party/gnc_ws/src/platform/ship_interfaces/msg/ShipReset.msg` | 新增 | reset 消息定义（lat/lon/heading/sog） |
| `third_party/gnc_ws/src/platform/ship_interfaces/CMakeLists.txt` | 改 | 注册新 msg |
| `third_party/gnc_ws/src/simulation/ship_dynamics/include/ship_dynamics/ship_dynamics_node.hpp` | 改 | reset subscriber + reset_to_origin() 声明 |
| `third_party/gnc_ws/src/simulation/ship_dynamics/src/ship_dynamics_node.cpp` | 改 | reset 回调 + reset_to_origin() 实现 |
| `third_party/gnc_ws/src/simulation/ship_dynamics/CMakeLists.txt` | 改 | find_package ship_interfaces（若未依赖） |
| `third_party/gnc_ws/src/gnc/ship_guidance/include/ship_guidance/coordinate_transform_node.hpp` | 改 | reset subscriber + set_origin() 声明 + mutex |
| `third_party/gnc_ws/src/gnc/ship_guidance/src/coordinate_transform_node.cpp` | 改 | set_origin() 实现 + reset 回调 + 提取 origin 锁定逻辑 |
| `src/sim_workbench/gnc_bridge/include/gnc_bridge/gnc_bridge_node.hpp` | 改 | L3ToGnc 加 reset 字段 + sub/pub 声明 |
| `src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp` | 改 | reset 订阅/发布 + drain 分支 |
| `src/sim_workbench/gnc_bridge/CMakeLists.txt` | 改 | find_package（若需） |
| `src/sil_orchestrator/lifecycle_bridge.py` | 改 | reset publisher + configure 发 reset |
| `tests/scripts/test_gnc_reset_interface.py` | 新增 | orchestrator reset 发送的 Python 单元测试 |

---

## Task 1: ship_interfaces 新增 ShipReset.msg

**Files:**
- Create: `third_party/gnc_ws/src/platform/ship_interfaces/msg/ShipReset.msg`
- Modify: `third_party/gnc_ws/src/platform/ship_interfaces/CMakeLists.txt:13-25`

- [ ] **Step 1: 创建 ShipReset.msg**

```
# ShipReset.msg — GNC plant 运行时 reset 指令。
# orchestrator 每次 scenario configure 时经 gnc_bridge 发送。
# 收到后：coordinate_transform 重设 origin 到 (latitude, longitude)，
# ship_dynamics reset eta_=(0,0) 并设航向/速度到 (heading_deg, sog_kn)。
std_msgs/Header header
float64 latitude    # 新 origin 纬度（度），= scenario ownShip.initial.position.latitude
float64 longitude   # 新 origin 经度（度），= scenario ownShip.initial.position.longitude
float64 heading_deg # own-ship 初始航向（度），= scenario ownShip.initial.heading
float64 sog_kn      # own-ship 初始对地速度（节），= scenario ownShip.initial.sog
```

- [ ] **Step 2: 注册到 CMakeLists.txt**

在 `third_party/gnc_ws/src/platform/ship_interfaces/CMakeLists.txt` 的 rosidl 接口列表（L13-25 区域，"msg/GeoPosition.msg" 之后）加一行：

```
  "msg/ShipReset.msg"
```

- [ ] **Step 3: 重建 ship_interfaces 验证 msg 生成**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```

预期：gnc-nodes 镜像重建成功，无 msg 生成错误。验证：
```bash
docker exec codex-gnc-validation-gnc-gnc-nodes-1 ros2 interface show ship_interfaces/msg/ShipReset
```
预期输出含 4 个字段定义。

- [ ] **Step 4: Commit**

```bash
git add third_party/gnc_ws/src/platform/ship_interfaces/msg/ShipReset.msg third_party/gnc_ws/src/platform/ship_interfaces/CMakeLists.txt
git commit -m "feat(gnc): add ShipReset.msg to ship_interfaces

New message for runtime GNC plant reset: orchestrator sends scenario
ownShip.initial position/heading/sog, bridge forwards to domain 50,
ship_dynamics + coordinate_transform reset to scenario start point."
```

---

## Task 2: ship_dynamics_node 加 reset_to_origin()

**Files:**
- Modify: `third_party/gnc_ws/src/simulation/ship_dynamics/include/ship_dynamics/ship_dynamics_node.hpp`
- Modify: `third_party/gnc_ws/src/simulation/ship_dynamics/src/ship_dynamics_node.cpp`
- Modify: `third_party/gnc_ws/src/simulation/ship_dynamics/CMakeLists.txt`

**Context:**
- 现有 `data_mutex_`（hpp L118）保护 update_dynamics（cpp L625 `lock_guard`）。reset 回调必须用同一 mutex。
- initialize() 在 cpp L124-151 设 eta_/nu_/psi_continuous_/initial_x_/y_/auto_initial_yaw_applied_/tau_env_/tau_thruster_/last_thruster_cmd_time_。
- ship_dynamics 已 find_package 的包见 CMakeLists L8-14（geometry_msgs/nav_msgs/tf2_ros 等）；需确认是否已 find_package(ship_interfaces)。

- [ ] **Step 1: 确认 ship_interfaces 依赖**

读 `third_party/gnc_ws/src/simulation/ship_dynamics/CMakeLists.txt`。若已 `find_package(ship_interfaces REQUIRED)`，跳过 Step 2。若无，执行 Step 2。

- [ ] **Step 2: 加 ship_interfaces 依赖（若 Step 1 发现缺失）**

在 CMakeLists.txt 的 find_package 段（L10-14 区域）加：
```cmake
find_package(ship_interfaces REQUIRED)
```
在 ament_target_dependencies（或 target_link_libraries）的 ship_dynamics_node 依赖列表加 `ship_interfaces`。

- [ ] **Step 3: hpp 加 include + 成员声明**

在 `ship_dynamics_node.hpp` 的 include 段（geometry_msgs 之后）加：
```cpp
#include "ship_interfaces/msg/ship_reset.hpp"
```

在 private 成员区（data_mutex_ 附近，hpp L118 区域）加：
```cpp
    // Runtime reset: scenario configure 经 gnc_bridge 发 /ship/dynamics_reset。
    // 回调在 subscriber 线程，update_dynamics 在 timer 线程，共享 data_mutex_。
    rclcpp::Subscription<ship_interfaces::msg::ShipReset>::SharedPtr reset_sub_;
    void reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr msg);
    void reset_to_origin(double yaw_rad, double u_mps);
```

- [ ] **Step 4: cpp 加 reset_to_origin() 实现**

在 `ship_dynamics_node.cpp` 的 reset_state() 方法（L484-487）之后，加新方法：

```cpp
void ShipDynamicsNode::reset_to_origin(double yaw_rad, double u_mps) {
    // 等价于 initialize() L124-151 的位置/速度设置，但位置恒回 origin (0,0)，
    // 航向/速度来自 reset 消息。由 reset_callback 调用，调用方持 data_mutex_。
    eta_ = {0.0, 0.0, 0.0, yaw_rad};
    nu_ = {u_mps, 0.0, 0.0, 0.0};
    psi_continuous_ = yaw_rad;
    initial_x_ = 0.0;
    initial_y_ = 0.0;
    auto_initial_yaw_applied_ = false;
    tau_env_ = {0.0, 0.0, 0.0, 0.0};
    tau_thruster_ = {0.0, 0.0, 0.0, 0.0};
    last_thruster_cmd_time_ = this->now();
    last_time_ = this->now();
    start_time_ = this->now();
    RCLCPP_INFO(this->get_logger(),
                "reset_to_origin: eta=(0,0,%.3frad), u=%.3f m/s", yaw_rad, u_mps);
}
```

- [ ] **Step 5: cpp 加 reset_callback + 订阅**

在构造函数末尾（所有现有 create_subscription 之后）加订阅：

```cpp
    reset_sub_ = this->create_subscription<ship_interfaces::msg::ShipReset>(
        "/ship/dynamics_reset", 10,
        std::bind(&ShipDynamicsNode::reset_callback, this, std::placeholders::_1));
```

加回调实现（reset_to_origin 之后）：

```cpp
void ShipDynamicsNode::reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr msg) {
    // 收到 reset：位置恒回 origin (0,0)；航向/速度来自消息。
    // 绝对经纬度由 coordinate_transform 的 origin 重设决定（另一个订阅者）。
    const double yaw_rad = msg->heading_deg * M_PI / 180.0;
    const double u_mps = msg->sog_kn * 0.514444;  // kn -> m/s
    std::lock_guard<std::mutex> lock(data_mutex_);
    reset_to_origin(yaw_rad, u_mps);
}
```

- [ ] **Step 6: 重建验证编译通过**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```
预期：gnc-nodes 镜像重建成功，无编译错误。若 ship_dynamics_node 启动报找不到 topic 正常（此时 bridge 还没加转发，Task 4 后才连通）。

- [ ] **Step 7: Commit**

```bash
git add third_party/gnc_ws/src/simulation/ship_dynamics/
git commit -m "feat(gnc): ship_dynamics_node reset_to_origin on /ship/dynamics_reset

New subscriber resets eta_ to origin (0,0 NED) + sets heading/speed from
ShipReset msg. Reuses existing data_mutex_ for thread safety. Backward
compatible: no reset msg = no behavior change."
```

---

## Task 3: coordinate_transform_node 加 set_origin()

**Files:**
- Modify: `third_party/gnc_ws/src/gnc/ship_guidance/include/ship_guidance/coordinate_transform_node.hpp`
- Modify: `third_party/gnc_ws/src/gnc/ship_guidance/src/coordinate_transform_node.cpp`

**Context:**
- origin 锁定逻辑在 route_callback（cpp L808-830）：设 origin_lat_/lon_、origin_published_=true、重算 lat0_rad_/N0_/M0_（L812-822 含 projection_mode 分支）、origin_locked_=true、publish origin_msg。
- coordinate_transform **无 mutex**（grep 确认）。route_callback/odom_callback 在 executor 线程，reset 回调也在 executor 线程（同 Node 同 executor 默认单线程），但仍加 mutex 防御 MultiThreadedExecutor。
- ship_guidance 已 find_package(ship_interfaces)（CMakeLists L16）。

- [ ] **Step 1: hpp 加 include + 成员声明**

在 `coordinate_transform_node.hpp` include 段加：
```cpp
#include "ship_interfaces/msg/ship_reset.hpp"
#include <mutex>
```

在 private 成员区加：
```cpp
    // Runtime origin reset: scenario configure 经 gnc_bridge 发 /ship/geo_origin_reset。
    std::mutex origin_reset_mutex_;
    rclcpp::Subscription<ship_interfaces::msg::ShipReset>::SharedPtr reset_sub_;
    void reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr msg);
    void set_origin(double lat, double lon);
```

- [ ] **Step 2: cpp 提取 set_origin() 方法**

在 `coordinate_transform_node.cpp`，提取 L808-830 的 origin 设置逻辑成 `set_origin()` 方法。在 route_callback 之外（类方法区）加：

```cpp
void CoordinateTransformNode::set_origin(double lat, double lon) {
    // 提取自 route_callback L808-830 的 origin 锁定逻辑，供运行时 reset 复用。
    origin_lat_ = lat;
    origin_lon_ = lon;
    origin_published_ = true;
    lat0_rad_ = origin_lat_ * M_PI / 180.0;
    double sin_lat0 = std::sin(lat0_rad_);
    N0_ = earth_a_ / std::sqrt(1.0 - earth_e2_ * sin_lat0 * sin_lat0);
    M0_ = earth_a_ * (1.0 - earth_e2_) /
          std::pow(1.0 - earth_e2_ * sin_lat0 * sin_lat0, 1.5);
    if (projection_mode_ == "planner_equirectangular" ||
        projection_mode_ == "equirectangular") {
        const double meters_per_radian = planner_meters_per_degree_ * 180.0 / M_PI;
        M0_ = meters_per_radian;
        N0_ = meters_per_radian;
    }
    origin_locked_ = true;

    geometry_msgs::msg::Point origin_msg;
    origin_msg.x = origin_lat_;
    origin_msg.y = origin_lon_;
    origin_msg.z = 0.0;
    origin_pub_->publish(origin_msg);

    RCLCPP_INFO(this->get_logger(),
                "set_origin: lat=%.6f lon=%.6f (runtime reset)", lat, lon);
}
```

- [ ] **Step 3: route_callback 改为调用 set_origin()**

将 route_callback 的 L808-830（`if (first_route) { ... }` 块内的 origin 设置代码）替换为调用 set_origin：

```cpp
    if (first_route) {
        set_origin(lat0, lon0);
    }
```

保留 `if (first_route)` 判断（route 首次到达才锁 origin，后续 route 不改）。L832-834 的 last_lats_/lons_/speed_limits_ 更新保留不变。

> **注意**：projection_mode_、earth_a_、earth_e2_、planner_meters_per_degree_ 必须在 set_origin 调用前已初始化（它们在构造函数参数读取时设置）。route_callback 首次触发时已初始化，reset_callback 触发时也已初始化（构造函数先于任何回调）。核查这些成员在 hpp 中的声明与构造函数初始化顺序。

- [ ] **Step 4: cpp 加 reset_callback + 订阅**

在构造函数末尾（所有现有 create_subscription 之后）加：

```cpp
    reset_sub_ = this->create_subscription<ship_interfaces::msg::ShipReset>(
        "/ship/geo_origin_reset", 10,
        std::bind(&CoordinateTransformNode::reset_callback, this, std::placeholders::_1));
```

加回调实现：

```cpp
void CoordinateTransformNode::reset_callback(
    const ship_interfaces::msg::ShipReset::SharedPtr msg) {
    // 运行时重设 origin 到 scenario ownShip.initial.position。
    // ship_dynamics 同时 reset eta_=(0,0)，故 geo_position = 新 origin = scenario 起点。
    std::lock_guard<std::mutex> lock(origin_reset_mutex_);
    set_origin(msg->latitude, msg->longitude);
}
```

- [ ] **Step 5: 重建验证编译通过**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```
预期：gnc-nodes 镜像重建成功，coordinate_transform_node 启动正常。

- [ ] **Step 6: Commit**

```bash
git add third_party/gnc_ws/src/gnc/ship_guidance/
git commit -m "feat(gnc): coordinate_transform set_origin on /ship/geo_origin_reset

Extract origin-locking logic into set_origin(), reuse for runtime reset.
New subscriber resets origin to scenario lat/lon. route_callback first-route
path now calls set_origin(). Backward compatible: no reset = no change."
```

---

## Task 4: gnc_bridge 加第5条转发

**Files:**
- Modify: `src/sim_workbench/gnc_bridge/include/gnc_bridge/gnc_bridge_node.hpp`
- Modify: `src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp`
- Modify: `src/sim_workbench/gnc_bridge/CMakeLists.txt`（若 ship_interfaces 未在依赖）

**Context:**
- CrossDomainHandoff::L3ToGnc（hpp L38-43）是 variant struct，现有 avoidance_plan + route_plan 两个 has_* 标志。加第三个 reset。
- L3 侧消息类型：l3_external_msgs 还是直接 ship_interfaces？L3 已 find_package(ship_interfaces)（gnc_bridge_node.hpp L24-27 已 include ship_interfaces/*）。**L3 侧也用 ship_interfaces::msg::ShipReset**，两边共用，无需 translator。
- domain 50 内发布**两个 topic**：`/ship/geo_origin_reset`（coordinate_transform 订阅）+ `/ship/dynamics_reset`（ship_dynamics 订阅），都发同一个 ShipReset msg。

- [ ] **Step 1: hpp 加 ShipReset include + L3ToGnc 字段 + sub/pub 声明**

在 `gnc_bridge_node.hpp` 的 include 段（ship_interfaces includes 之后，L27 区域）加：
```cpp
#include "ship_interfaces/msg/ship_reset.hpp"
```

修改 `L3ToGnc` struct（L38-43）加第三个字段：
```cpp
  struct L3ToGnc {
    ship_interfaces::msg::AvoidancePlan avoidance_plan;
    bool has_avoidance{false};
    ship_interfaces::msg::RoutePlan route_plan;
    bool has_route{false};
    ship_interfaces::msg::ShipReset ship_reset;
    bool has_reset{false};
  };
```

L3SideNode private 区（L128-129 附近）加订阅：
```cpp
  rclcpp::Subscription<ship_interfaces::msg::ShipReset>::SharedPtr sub_reset_;
```

GncSideNode private 区（L143-144 附近）加两个 publisher（domain 50 内两个 topic）：
```cpp
  rclcpp::Publisher<ship_interfaces::msg::ShipReset>::SharedPtr pub_geo_reset_;
  rclcpp::Publisher<ship_interfaces::msg::ShipReset>::SharedPtr pub_dynamics_reset_;
```

- [ ] **Step 2: cpp L3SideNode 加订阅 + 回调**

在 `gnc_bridge_node.cpp` 的 L3SideNode 构造函数（sub_avoidance_/sub_route_ 创建之后，约 L30 区域），加 reset 订阅：

```cpp
  sub_reset_ = create_subscription<ship_interfaces::msg::ShipReset>(
      "/l3/sim/reset_own_ship", 10,
      [this](const ship_interfaces::msg::ShipReset::SharedPtr msg) {
        CrossDomainHandoff::L3ToGnc item;
        item.ship_reset = *msg;
        item.has_reset = true;
        handoff_->push_l3_to_gnc(std::move(item));
      });
```

更新 L3SideNode 的 ready 日志（L30）加 `/l3/sim/reset_own_ship`。

- [ ] **Step 3: cpp GncSideNode 加 publisher + drain 分支**

在 GncSideNode 构造函数（pub_route_ 创建之后，L54-56 区域），加两个 publisher：

```cpp
  pub_geo_reset_ = create_publisher<ship_interfaces::msg::ShipReset>(
      "/ship/geo_origin_reset", 10);
  pub_dynamics_reset_ = create_publisher<ship_interfaces::msg::ShipReset>(
      "/ship/dynamics_reset", 10);
```

在 drain_timer_ lambda（L60-71）的 while 循环内，加 reset 分支：

```cpp
        if (item.has_reset) {
          pub_geo_reset_->publish(item.ship_reset);
          pub_dynamics_reset_->publish(item.ship_reset);
        }
```

更新 GncSideNode ready 日志（L72-74）加两个 reset topic。

- [ ] **Step 4: 重建验证编译通过 + bridge 转发连通**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```
预期：bridge 镜像（mass-l3-sil-sil-nodes:codex-gnc-validation）重建成功。验证 bridge 启动日志含 reset topic：
```bash
docker logs codex-gnc-validation-gnc-gnc-bridge-1 2>&1 | grep -i reset
```
预期含 `/l3/sim/reset_own_ship`（L3 side）和 `/ship/geo_origin_reset` `/ship/dynamics_reset`（GNC side）。

- [ ] **Step 5: Commit**

```bash
git add src/sim_workbench/gnc_bridge/
git commit -m "feat(bridge): forward /l3/sim/reset_own_ship to domain 50 reset topics

5th cross-domain channel: L3 reset -> /ship/geo_origin_reset +
/ship/dynamics_reset (domain 50). Reuses CrossDomainHandoff queue +
drain timer pattern. Both GNC nodes subscribe one ShipReset msg each."
```

---

## Task 5: lifecycle_bridge.py configure 时发 reset

**Files:**
- Modify: `src/sil_orchestrator/lifecycle_bridge.py`
- Create: `tests/scripts/test_gnc_reset_interface.py`

**Context:**
- `/sil/scenario_loaded` publisher 在 L154-155（同模式参考）。
- configure() 在 L389-455，含 _extract_injection_params + _filter_injection_params_for_runtime_profile + _inject_params_to_node + CONFIGURE transition。
- scenario ownShip.initial.position 在 scenario_data（YAML 已 parse）。读 `scen_data["ownShip"]["initial"]["position"]["latitude/longitude"]`、`["heading"]`、`["sog"]`。
- runtime_profile 判断：self.runtime_profile 或参数，参考 _filter_injection_params_for_runtime_profile 的 profile 来源。
- ShipReset msg 在 L3 侧用 ship_interfaces::msg::ShipReset（Python: `from ship_interfaces.msg import ShipReset`）。需确认 sil_orchestrator 容器能 import ship_interfaces（gnc-nodes 镜像有，sil-orchestrator 镜像需核查依赖）。

- [ ] **Step 1: 确认 sil_orchestrator 能 import ship_interfaces**

```bash
docker exec codex-gnc-validation-sil-orchestrator-1 python3 -c "from ship_interfaces.msg import ShipReset; print('ok')"
```
若失败：sil_orchestrator Dockerfile / 镜像需加 ship_interfaces 依赖。先查 `src/sil_orchestrator/` 的 Dockerfile 或 requirements，加 ship_interfaces 到 PYTHONPATH / install。若 ship_interfaces 未在 sil-orchestrator 镜像内，改用 l3_external_msgs 镜像 msg（需在 l3_external_msgs 加 ShipReset.msg 镜像，bridge translator 翻译）。

> **分支决策**：若 orchestrator 不能 import ship_interfaces，Task 1/4 需调整：L3 侧用 l3_external_msgs::msg::ShipReset（新增镜像 msg），bridge 做 translator。本 plan 假设 orchestrator 能用 ship_interfaces（gnc stack 已 build 进 sil-nodes 镜像，orchestrator 与 sil-nodes 共享 install 或 overlay）。实施时先验证，失败则走镜像 msg 分支。

- [ ] **Step 2: 写失败测试**

`tests/scripts/test_gnc_reset_interface.py`:

```python
"""GNC reset interface: orchestrator emits ShipReset on configure."""
import pytest


@pytest.fixture
def runner():
    import sys
    sys.path.insert(0, ".")
    from src.sil_orchestrator import lifecycle_bridge
    return lifecycle_bridge


def test_build_ship_reset_from_ownship_initial(runner):
    """_build_ship_reset extracts lat/lon/heading/sog from scenario ownShip.initial."""
    scen = {
        "ownShip": {
            "initial": {
                "position": {"latitude": 63.44, "longitude": 10.38},
                "heading": 0.0,
                "sog": 6.0,
            }
        }
    }
    msg = runner._build_ship_reset(scen)
    assert msg.latitude == pytest.approx(63.44)
    assert msg.longitude == pytest.approx(10.38)
    assert msg.heading_deg == pytest.approx(0.0)
    assert msg.sog_kn == pytest.approx(6.0)


def test_build_ship_reset_none_when_no_ownship(runner):
    """Missing ownShip.initial returns None (no reset emitted)."""
    msg = runner._build_ship_reset({})
    assert msg is None
```

- [ ] **Step 3: 运行测试验证失败**

```bash
python3 -m pytest tests/scripts/test_gnc_reset_interface.py -v
```
预期：FAIL（`_build_ship_reset` 不存在）。

- [ ] **Step 4: 实现 _build_ship_reset()**

在 `lifecycle_bridge.py` 加纯函数（在类外或静态方法）：

```python
def _build_ship_reset(scen_data):
    """Build a ShipReset msg from scenario ownShip.initial, or None if absent."""
    own = scen_data.get("ownShip", {}).get("initial", {})
    pos = own.get("position", {})
    lat = pos.get("latitude")
    lon = pos.get("longitude")
    if lat is None or lon is None:
        return None
    from ship_interfaces.msg import ShipReset
    msg = ShipReset()
    msg.latitude = float(lat)
    msg.longitude = float(lon)
    msg.heading_deg = float(own.get("heading", 0.0))
    msg.sog_kn = float(own.get("sog", 0.0))
    return msg
```

- [ ] **Step 5: 运行测试验证通过**

```bash
python3 -m pytest tests/scripts/test_gnc_reset_interface.py -v
```
预期：PASS（2 tests）。若 ship_interfaces import 在本机失败（非容器），测试需 mock 或在容器内跑。备选：用 try/except import + 跳过。

- [ ] **Step 6: lifecycle_bridge 加 reset publisher + configure 发送**

在 lifecycle_bridge.py 的 rclpy Node 初始化区（`/sil/scenario_loaded` publisher 创建处，L154-155 附近），加 reset publisher：

```python
        self._reset_pub = self._node.create_publisher(
            ShipReset, "/l3/sim/reset_own_ship", 10)
```
（需在文件顶部 `from ship_interfaces.msg import ShipReset`，或延迟 import 避免非 GNC 环境报错）

在 configure() 方法（L389-455），CONFIGURE transition 之前、参数注入之后，加 reset 发送（仅 GNC profile）：

```python
        if self.runtime_profile == "gnc":
            reset_msg = _build_ship_reset(scen_data)
            if reset_msg is not None:
                reset_msg.header.stamp = self._node.get_clock().now().to_msg()
                self._reset_pub.publish(reset_msg)
                logger.info(
                    "GNC reset emitted: lat=%.6f lon=%.6f heading=%.1f sog=%.1f",
                    reset_msg.latitude, reset_msg.longitude,
                    reset_msg.heading_deg, reset_msg.sog_kn)
```

> 确认 `self.runtime_profile` 的实际属性名（grep lifecycle_bridge.py 的 runtime_profile / _runtime_profile）。

- [ ] **Step 7: 重建 orchestrator + 验证 reset 发送**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```
验证 orchestrator 启动后，跑一次探针，查 orchestrator 日志含 "GNC reset emitted"：
```bash
docker logs codex-gnc-validation-sil-orchestrator-1 2>&1 | grep "GNC reset emitted"
```

- [ ] **Step 8: Commit**

```bash
git add src/sil_orchestrator/lifecycle_bridge.py tests/scripts/test_gnc_reset_interface.py
git commit -m "feat(orchestrator): emit ShipReset on GNC profile configure

lifecycle_bridge builds ShipReset from scenario ownShip.initial and
publishes /l3/sim/reset_own_ship before activate (GNC profile only).
SIL profile unchanged. +2 unit tests for _build_ship_reset."
```

---

## Task 6: 集成验证 — GNC profile 连续2 run 可复现

**Files:**
- 无代码改动，纯验证

**Context:** 这是 spec 的核心验收条件。reset 接口连通后，连续 2 次 rule14-ho 应该都从 ownShip.initial (63.44, 10.38) 出发，行为可复现（AVOID onset 时序一致）。

- [ ] **Step 1: 冷启 + 跑第 1 次 rule14-ho**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
sleep 30
mkdir -p runs/gnc_reset_verify
SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 PROBE_STUCK_LIMIT=150 \
  python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule14-ho \
  --profile gnc --sim-rate 10 \
  --summary-out runs/gnc_reset_verify/run1_summary.json \
  --trace-report-dir runs/gnc_reset_verify/trace_run1
```

- [ ] **Step 2: 记录 run1 起点 + AVOID onset**

```bash
python3 -c "
import json, collections
f='runs/gnc_reset_verify/trace_run1/colreg-rule14-ho.trace_current.jsonl'
tc=collections.Counter(); beh_seq=[]
with open(f) as fh:
    for line in fh:
        r=json.loads(line)
        if r.get('topic')=='/l3/m2/world_state':
            tc[r.get('target_count')]+=1
        elif r.get('topic')=='/sil/own_ship_state':
            t=r.get('sim_t',0)
            if t<3: print(f'  OWN t={t:.2f} lat={r.get(\"lat\")} lon={r.get(\"lon\")}')
print('target_count:', dict(tc))
"
```
预期：OWN lat≈63.44 lon≈10.38（scenario 起点，非上一 run 末态）。

- [ ] **Step 3: 不重启容器，跑第 2 次 rule14-ho**

```bash
SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 PROBE_STUCK_LIMIT=150 \
  python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule14-ho \
  --profile gnc --sim-rate 10 \
  --summary-out runs/gnc_reset_verify/run2_summary.json \
  --trace-report-dir runs/gnc_reset_verify/trace_run2
```

- [ ] **Step 4: 验证 run2 起点与 run1 一致（核心验收）**

```bash
python3 -c "
import json
for label,p in [('run1','runs/gnc_reset_verify/trace_run1/colreg-rule14-ho.trace_current.jsonl'),('run2','runs/gnc_reset_verify/trace_run2/colreg-rule14-ho.trace_current.jsonl')]:
    with open(p) as fh:
        for line in fh:
            r=json.loads(line)
            if r.get('topic')=='/sil/own_ship_state' and r.get('sim_t',0)<3:
                print(f'{label}: OWN t={r[\"sim_t\"]:.2f} lat={r[\"lat\"]:.5f} lon={r[\"lon\"]:.5f}')
                break
"
```
**验收标准**：run2 的 OWN lat/lon ≈ run1（都从 63.44,10.38 出发），而非 run1 末态。这是 reset 接口生效的证据。

- [ ] **Step 5: 验证 AVOID onset 时序一致**

对比 run1/run2 的 behavior transitions（bp_transitions），应时序接近（可复现）。若有差异记录幅度。

- [ ] **Step 6: 记录验证结论**

在 `handoff/workspace_log.md` 追加验证结果（run1/run2 起点、AVOID onset、是否可复现）。

---

## Task 7: SIL profile 回归

**Files:**
- 无代码改动，纯验证

**Context:** reset 只在 GNC profile 发，SIL profile 必须行为不变。

- [ ] **Step 1: 跑 SIL profile 现有测试**

```bash
python3 -m pytest tests/scripts/test_run_6_horizon_adaptive.py tests/scripts/test_run_6_scenarios_gate.py tests/scripts/test_gnc_reset_interface.py -v
```
预期：全 PASS（含之前的 recovery_stalled 修复测试）。

- [ ] **Step 2: （可选）SIL stack 跑一次 rule14-ho 确认无 reset 干扰**

若 SIL stack 可用，跑一次确认 reset publisher 不影响 SIL 行为（reset 只在 runtime_profile=="gnc" 时发）。

- [ ] **Step 3: 最终 commit（若 Task 6 有日志改动）**

```bash
git add handoff/workspace_log.md
git commit -m "test(gnc): verify reset interface — consecutive runs reproducible"
```

---

## Self-Review

**1. Spec coverage:**
- ✅ ShipReset.msg (Task 1)
- ✅ ship_dynamics reset_to_origin (Task 2)
- ✅ coordinate_transform set_origin (Task 3)
- ✅ bridge 第5条转发 (Task 4)
- ✅ lifecycle_bridge configure 发 reset (Task 5)
- ✅ 连续2 run 可复现验证 (Task 6)
- ✅ SIL 回归 (Task 7)
- ✅ WGS84 lat/lon reset（非 NED (0,0) 假设）—— Task 1 msg 含 lat/lon，Task 3 set_origin 用 lat/lon
- ✅ 向后兼容（不发 reset = 无变化）—— 各 Task 均纯新增
- ⚠️ mutex：ship_dynamics 复用 data_mutex_（Task 2），coordinate_transform 新增 origin_reset_mutex_（Task 3）—— 已覆盖

**2. Placeholder scan:** 无 TBD/TODO。Task 5 Step 1 有分支决策（orchestrator 能否 import ship_interfaces）—— 这是实施时验证点，非 placeholder，给了明确的失败处理路径。

**3. Type consistency:**
- ShipReset msg 字段：latitude/longitude/heading_deg/sog_kn —— 全 Task 一致
- ship_dynamics: reset_to_origin(yaw_rad, u_mps) —— Task 2 内一致
- coordinate_transform: set_origin(lat, lon) —— Task 3 内一致
- bridge: L3ToGnc.ship_reset + has_reset —— Task 4 hpp/cpp 一致
- topic 名：/l3/sim/reset_own_ship（L3）/ship/geo_origin_reset + /ship/dynamics_reset（GNC）—— 全 Task 一致
