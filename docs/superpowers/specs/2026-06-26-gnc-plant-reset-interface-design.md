# GNC Plant 运行时 Reset 接口设计

## 日期
2026-06-26

## 背景

GNC profile（TDL + 同事 GNC stack 联调）下，连续跑多个 COLREGs scenario 会出现 **own-ship 物理状态跨 run 累积**，导致 run-to-run 不可复现：

- 冷启后第 1 个 run：own-ship 从 scenario 初始位出发，行为健康
- 第 2 个 run 起：own-ship 从上一 run 末态位置继续，几何全错，立即误触发避碰甚至碰撞

### 根因（代码 + 运行时双重证据）

1. `lifecycle_bridge.py::_filter_injection_params_for_runtime_profile()` 在 GNC profile 下显式删除 `ship_dynamics_node` 的 `ownShip.initial.position` 注入（L760-769）。
2. GNC plant（`ship_dynamics_node` + `coordinate_transform_node`）在独立 DDS domain 50，orchestrator（domain 42）的 lifecycle cleanup/deactivate 只对 8 个 L3 SIL lifecycle node 发 transition，**碰不到 GNC plant**。
3. `ship_dynamics_node` 的 `eta_`（NED 位置）只在构造函数 `initialize()` 读一次 `initial_position` 参数，之后纯 RK4 积分；无 `add_on_set_parameters_callback`，`reset_state()` 只清速度 `nu_` 不动位置 `eta_`。
4. `coordinate_transform_node` 的 `origin_lat_/origin_lon_` 在首条 route 到达时锁定（`origin_locked_=true`），**之后终身不变，无 unlock/重设接口**。
5. 运行时证据：冷启后连续 3 次 rule14-ho，run2/run3 的 own-ship 在 t=0.5s 位于 lat=63.4979（run1 末态，非 scenario 初始 63.44，差 6.4km），且 run2/run3 M2 数据逐字节相同（确定性泄漏）。

## 目标

orchestrator 每次 scenario configure 时，把 GNC plant 的 own-ship 物理状态 reset 到 **scenario 的 ownShip.initial.position 真实经纬度**，实现跨 scenario 可复现。

**关键设计原则**：reset 必须支持任意起点经纬度，不依赖"所有 scenario 首点一致"的假设。

**约束**：
- 纯新增，向后兼容（不发 reset = 行为完全不变）
- 不改现有 initialize/reset_state/route_callback/参数语义
- bridge 可改（Track A 自有代码，非 forbidden 语境）

## 设计

### own-ship 位置的物理构成

geo_position（= /sil/own_ship_state 的 lat/lon）由两个量决定：
```
geo_position = origin_latlon + NED_to_WGS84(ship_dynamics.eta_)
```
- `ship_dynamics_node.eta_`：相对 origin 的 NED 位移（米），纯积分
- `coordinate_transform_node.origin_lat_/origin_lon_`：地图原点经纬度，首条 route 锁定

要让 own-ship 出现在任意 scenario 起点 (lat_s, lon_s)，**必须同时**：
1. coordinate_transform 的 origin = (lat_s, lon_s)
2. ship_dynamics 的 eta_ = (0, 0)（相对 origin 的原点）

### 数据流

```
orchestrator lifecycle configure (domain 42)
  → 发 /l3/sim/reset_own_ship (含 scenario ownShip.initial 的 lat/lon/heading/sog)
  → gnc_bridge 转发 (新增第5条 L3→GNC)
     ├→ /ship/geo_origin_reset (domain 50) → coordinate_transform_node: 重设 origin
     └→ /ship/dynamics_reset  (domain 50) → ship_dynamics_node: reset eta_/nu_
```

两个 GNC node 各订阅一个 reset topic（或同一 topic，各自取需要的字段）。倾向**单一 reset topic + 两个订阅者**：消息含 lat/lon/heading/sog，coordinate_transform 取 lat/lon 重设 origin，ship_dynamics 取 heading/sog reset eta_=(0,0)+nu_。

### 坐标系：reset 消息传 WGS84 lat/lon

reset 消息传 **scenario ownShip.initial 的真实经纬度**：
- `latitude`：ownShip.initial.position.latitude（度）
- `longitude`：ownShip.initial.position.longitude（度）
- `heading_deg`：ownShip.initial.heading（度）
- `sog_kn`：ownShip.initial.sog（节）

coordinate_transform 收到 lat/lon → 重设 origin → ship_dynamics 的 eta_=(0,0) → geo_position = 新 origin = scenario 真实起点。支持任意起点。

## 四处改动

### 1. ship_interfaces 新增 ShipReset.msg（同事包）

文件：`third_party/gnc_ws/src/platform/ship_interfaces/msg/ShipReset.msg`

```
# GNC plant 运行时 reset 指令。orchestrator 每次 scenario configure 时发送。
# 收到后：coordinate_transform 重设 origin 到 (latitude, longitude)，
# ship_dynamics reset eta_=(0,0) 并设航向/速度。
std_msgs/Header header
float64 latitude    # 新 origin 纬度（= scenario ownShip.initial.position.latitude）
float64 longitude   # 新 origin 经度（= scenario ownShip.initial.position.longitude）
float64 heading_deg # own-ship 初始航向（= scenario ownShip.initial.heading）
float64 sog_kn      # own-ship 初始速度（= scenario ownShip.initial.sog）
```

L3 侧镜像 msg：`l3_external_msgs/msg/ShipReset.msg`（同字段），bridge 做 translator（与现有 route/avoidance 同模式）。或两边共用 ship_interfaces 的 ShipReset.msg（L3 已 find_package ship_interfaces）—— 优先共用，减少 msg 数量。

### 2. ship_dynamics_node（third_party/gnc_ws）— reset eta_/nu_

文件：
- `third_party/gnc_ws/src/simulation/ship_dynamics/src/ship_dynamics_node.cpp`
- `third_party/gnc_ws/src/simulation/ship_dynamics/include/ship_dynamics/ship_dynamics_node.hpp`

改动：
- 新增订阅 `/ship/dynamics_reset`（ShipReset.msg，domain 50 内，depth=10）。
- 新增 `reset_to_origin(double yaw_rad, double u_mps)` 方法，重置（与 initialize() L124-151 等价，但位置恒回 origin、航向/速度来自消息）：
  - `eta_` = {0.0, 0.0, 0.0, yaw_rad}（x/y 恒为 0 = origin 点）
  - `nu_` = {u_mps, 0.0, 0.0, 0.0}
  - `psi_continuous_` = yaw_rad
  - `initial_x_` = 0.0, `initial_y_` = 0.0
  - `auto_initial_yaw_applied_` = false
  - `tau_env_` = {0,0,0,0}, `tau_thruster_` = {0,0,0,0}
  - `last_thruster_cmd_time_` = now()
  - `last_time_` / `start_time_` = now()
- 回调：heading_deg→rad，sog_kn→m/s，调 `reset_to_origin()`，加 mutex（update_dynamics 在 timer 线程，reset 在 subscriber 线程）。
- **不改** initialize()、reset_state()、update_dynamics() 现有逻辑。
- ship_dynamics 不取 lat/lon 字段（位置恒回 origin，绝对经纬度由 coordinate_transform 的 origin 决定）。

### 3. coordinate_transform_node（third_party/gnc_ws）— 重设 origin

文件：
- `third_party/gnc_ws/src/gnc/ship_guidance/src/coordinate_transform_node.cpp`
- `third_party/gnc_ws/src/gnc/ship_guidance/include/ship_guidance/coordinate_transform_node.hpp`

改动：
- 新增订阅 `/ship/geo_origin_reset`（ShipReset.msg，domain 50 内）。
- 提取现有 origin 锁定逻辑（L808-830）成 `set_origin(double lat, double lon)` 方法：
  - `origin_lat_` = lat, `origin_lon_` = lon
  - `origin_published_` = true
  - 重算 `lat0_rad_`、`N0_`、`M0_`（L812-822 的 WGS84 曲率半径 + projection_mode 分支）
  - `origin_locked_` = true
  - publish origin_msg（L825-829）
- reset 回调调用 `set_origin(latitude, longitude)`，加 mutex（route_callback/odom_callback 可能并发）。
- **不改** route_callback 的 first_route 逻辑（首条 route 仍正常锁 origin）；reset 是额外的运行时重设通道。
- coordinate_transform 不取 heading/sog 字段（只关心 lat/lon）。

### 4. gnc_bridge（src/sim_workbench/gnc_bridge）— 第5条转发

文件：
- `src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp`
- `src/sim_workbench/gnc_bridge/include/gnc_bridge/gnc_bridge_node.hpp`
- `src/sim_workbench/gnc_bridge/include/gnc_bridge/translators.hpp`

改动：
- L3→GNC 方向新增第3条转发（现有2条：avoidance_waypoints, planned_route）：
  - L3SideNode 订阅 `/l3/sim/reset_own_ship`（domain 42，ShipReset.msg）
  - GncSideNode 发布两次：`/ship/geo_origin_reset` + `/ship/dynamics_reset`（domain 50，ShipReset.msg）
    - 或单一 `/ship/reset_state`，两个 node 都订阅（更简洁，优先）
- 复用现有 `CrossDomainHandoff` queue + 非阻塞 `try_pop_*` + 20Hz drain timer 模式。
- 若 L3/GNC 共用 ShipReset.msg 则透传，无需 translator。

### 5. lifecycle_bridge.py（src/sil_orchestrator）— configure 时发 reset

文件：
- `src/sil_orchestrator/lifecycle_bridge.py`

改动：
- 新增 publisher `/l3/sim/reset_own_ship`（ShipReset.msg），与 `/sil/scenario_loaded` publisher 同模式（L154-155）。
- configure 流程中，**GNC profile 下**，构造 ShipReset 消息（用 scenario ownShip.initial.position/heading/sog）并发布。
- 时序：configure 之后、activate 之前发 reset，确保 plant 在 sim 启动前回到真实起点。
- SIL profile 不发（SIL ship_dynamics 由现有注入 + cross-run-reset 覆盖）。

## 向后兼容

- ship_dynamics：不发 `/ship/reset_state` 时，订阅永不触发，行为完全不变。
- coordinate_transform：不发 reset 时，set_origin 永不被调用，origin 锁定逻辑不变。
- bridge：L3 侧不发 `/l3/sim/reset_own_ship` 时，handoff queue 空，drain timer 无事可做。
- orchestrator：SIL profile 不发 reset（只 GNC profile 发）。
- 同事侧：拿到改动后，若不调用 reset，行为与现在完全一致。

## 已知边界

1. **reset 与 route 到达的时序**：configure 时 orchestrator 同时发 reset 和新 route。若 route 先到，coordinate_transform 的 first_route 逻辑会用 route 首点锁 origin；随后 reset 到达会用 ownShip.initial 重设 origin。两者应一致（ownShip.initial = route 首点），但若有 scenario 两者不同，reset 覆盖 route 锁定值（reset 在后）。需确认 reset 在 route 之后到达，或在 reset 后不依赖 route 的 origin 锁定。
2. **ship_interfaces ShipReset.msg 是同事包**：新增 msg 需同事 colcon rebuild ship_interfaces。改完同步给同事。
3. **mutex 正确性**：ship_dynamics 的 update_dynamics（timer 线程）与 reset 回调（subscriber 线程）并发访问 eta_/nu_ 等，需 mutex。coordinate_transform 的 route_callback/odom_callback 与 set_origin 并发同理。现有代码是否已有 mutex 需核查。

## 不在范围

- "plant position 应从外部 GPS 注入而非自积分"的架构重构（用户确认为更大话题，单独开）。
- SIL profile 的 reset（SIL ship_dynamics 由现有注入 + lifecycle 覆盖，不需要）。
- reset 消息的反馈/确认机制（fire-and-forget，靠后续 geo_position 验证）。

## 测试计划

- 单元测试（C++ gtest）：
  - ship_dynamics `reset_to_origin()` 后，eta_/nu_/psi_continuous_ 等成员等于预期值。
  - coordinate_transform `set_origin()` 后，origin_lat_/lon_/N0_/M0_ 等于预期，geo_position 换算正确。
- 集成测试：GNC profile 下连续跑 2 个 rule14-ho，验证第 2 个 run 的 own-ship 从 ownShip.initial.position (63.44,10.38) 出发（非上一 run 末态），AVOID onset 时序与第 1 个 run 一致（可复现）。
- 不同起点测试：构造一个 route 首点不同的测试 scenario，验证 reset 到新 origin 后 geo_position 正确。
- 回归：SIL profile 跑现有探针，确认行为不变（reset 不触发）。

## 关键文件

- `third_party/gnc_ws/src/platform/ship_interfaces/msg/ShipReset.msg`（新增）
- `third_party/gnc_ws/src/simulation/ship_dynamics/src/ship_dynamics_node.cpp`（initialize L113-210；reset_state L484-487）
- `third_party/gnc_ws/src/simulation/ship_dynamics/include/ship_dynamics/ship_dynamics_node.hpp`（成员 L88-154）
- `third_party/gnc_ws/src/gnc/ship_guidance/src/coordinate_transform_node.cpp`（origin 锁定 L629-631、L808-830；geo_position 计算 L1235-1241）
- `third_party/gnc_ws/src/gnc/ship_guidance/include/ship_guidance/coordinate_transform_node.hpp`
- `src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp`（L3→GNC 转发 L52-75）
- `src/sim_workbench/gnc_bridge/include/gnc_bridge/gnc_bridge_node.hpp`
- `src/sim_workbench/gnc_bridge/include/gnc_bridge/translators.hpp`
- `src/sil_orchestrator/lifecycle_bridge.py`（configure L389-455；scenario_loaded publisher L154-155）
- `l3_external_msgs/msg/`（若需镜像 ShipReset.msg）
