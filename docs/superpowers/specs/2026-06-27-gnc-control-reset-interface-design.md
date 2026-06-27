# GNC 控制层 Reset 接口扩展设计

## 日期
2026-06-27

## 背景

前置工作（2026-06-26）给 GNC plant 的 `ship_dynamics_node` + `coordinate_transform_node` 加了 runtime reset 接口（订阅 ShipReset msg），解决了 own-ship 起点跨 run 累积。但稳定性测试发现：reset 生效后（两次都从 scenario 起点出发），连续 2 次 rule14-ho 仍**严重不可复现**——AVOID onset 差 4700s（t=60 vs t=4763），避让方向相反（Starboard vs Port）。

对照实验：`docker restart gnc-nodes gnc-bridge`（同步）后跑 → **10/10 指标完全一致**（AVOID onset t=1469.3/t=1469.3，steer Starboard/Starboard）。

结论：**reset 接口只清了 plant，控制层节点（ship_control/ship_guidance）的积分器/状态跨 run 残留才是不可复现的根因。** restart 重建进程才把积分器归零。

## 根因（代码证据，两路 Explore 调研）

### P0 根因：积分器跨 run 累积

**ship_control_node**（`third_party/gnc_ws/src/gnc/ship_control/`）：
- 4 个 PID 积分项 `integral_surge_/integral_sway_/integral_yaw_/integral_speed_`（hpp:100，构造置 0，永不重置）
- NDO 非线性干扰观测器积分状态 `z_/d_hat_/tau_last_`（hpp:89-91，每步 `z_ += z_dot*dt` 纯积分）
- 误差/微分历史 `prev_error_*/prev_deriv_*/previous_u_/last_time_`
- 清积分逻辑**只绑定模式切换**（DP↔AUTOPILOT），不绑定新 scenario。`target_callback` 甚至用 `tau_last_` 反喂积分（把残留带进新 run）

**ship_guidance_node**（`third_party/gnc_ws/src/gnc/ship_guidance/`）：
- ILOS 积分项 `integral_e_`（hpp:334）+ 侧滑估计 `beta_hat_`（hpp:330）+ `psi_cmd_prev_/init_psi_`（hpp:336-337）
- path signature latch `has_last_path_signature_/last_path_signature_/last_path_size_`（hpp:323-325）—— 相同 scenario 重跑可能因签名相同丢弃新 path
- 多个 rejoin/gate latch：`far_xte_rejoin_active_/heading_align_active_/corridor_hold_active_/cruise_recovery_gate_cleared_` 等未被 path_callback 清零

### P1：route 守卫状态残留（coordinate_transform 补漏）

`coordinate_transform_node` 的 reset_callback（已加）**只清 origin**，漏清：
- `has_last_route_/last_accepted_route_time_/last_feedback_path_`（route 更新守卫）—— reset 后新 route 可能被 `min_route_update_interval_s_=10s` 时间守卫误 REJECT，导致 ship_guidance 收不到新 path

### P2：thrust_allocation 执行器状态残留（次要）

`thrust_allocation_node`：`states_[i].last_thrust_N/last_angle_rad`（QP 热启动初值 + 角速率限制基准）+ `is_maneuvering_mode_` + `tau_env_` 跨 run 残留。影响启动段 transient，稳态被每步 QP 消化。

### 无残留（无需处理）
- `active_route_manager_node`：仅 60s 自过期 latch，无累积
- `propulsion_policy_node`：纯无状态评估
- `ship_dynamics_node`：已清
- `coordinate_transform` origin：已清

## 目标

给 ship_control/ship_guidance/thrust_allocation 加 ShipReset 订阅，补全 coordinate_transform 的 reset_callback。使 reset 接口覆盖全部跨 run 状态残留，达到与 restart 等效的可复现性，无需每 scenario restart（省 ~25s/scenario）。

**约束**：
- 纯新增，向后兼容（不发 reset = 行为不变）
- 复用已有 ShipReset msg + bridge 转发（bridge 零改动）
- 不改现有 control_loop/guidance 逻辑，只在 reset 回调里清状态

## 设计

### 复用现有 reset topic（bridge 零改动）

gnc_bridge 已转发 ShipReset 到 domain 50 的两个 topic：
- `/ship/geo_origin_reset`（coordinate_transform 订阅）
- `/ship/dynamics_reset`（ship_dynamics 订阅）

新增 3 个节点各订阅**其中一个**（任选，消息内容相同）：
- ship_control_node → 订阅 `/ship/dynamics_reset`
- ship_guidance_node → 订阅 `/ship/dynamics_reset`
- thrust_allocation_node → 订阅 `/ship/dynamics_reset`

bridge 的 `pub_dynamics_reset_` 已存在，多订阅者自动广播，无需改 bridge。

### 四处改动

#### 1. ship_control_node（P0）— 清积分器 + 误差历史

文件：`third_party/gnc_ws/src/gnc/ship_control/src/ship_control_node.cpp` + `include/ship_control/ship_control_node.hpp`

加 ShipReset 订阅（订阅 `/ship/dynamics_reset`）+ `reset_callback` + `reset_controller()` 方法。回调清零：
- 积分项：`integral_surge_/integral_sway_/integral_yaw_/integral_speed_` = 0
- NDO 积分：`z_.setZero()` / `d_hat_.setZero()` / `tau_last_.setZero()`
- 误差历史：`prev_error_surge_/sway_/yaw_/speed_` = 0
- 微分历史：`prev_deriv_surge_/sway_/yaw_` = 0
- `previous_u_` = 0
- `last_time_` = now()（重置 dt 锚点）
- 回调用 `data_mutex_`（需核查 ship_control 是否已有 mutex；若无则新增）

#### 2. ship_guidance_node（P0）— 清 ILOS 积分 + path latch

文件：`third_party/gnc_ws/src/gnc/ship_guidance/src/ship_guidance_node.cpp` + `include/ship_guidance/ship_guidance_node.hpp`

加 ShipReset 订阅（`/ship/dynamics_reset`）+ `reset_callback` + `reset_guidance()`。回调清零：
- ILOS 积分：`integral_e_` = 0
- 侧滑估计：`beta_hat_` = 0
- 航向历史：`psi_cmd_prev_` = 0, `init_psi_` = false
- `last_time_` = now()
- path latch：`has_last_path_signature_` = false, `last_path_signature_` = "", `last_path_size_` = 0
- rejoin/gate latch：`far_xte_rejoin_active_/heading_align_active_/corridor_hold_active_/cruise_recovery_gate_cleared_/raw_route_rejoin_active_` = false

#### 3. coordinate_transform_node（P1）— 补 reset_callback route 守卫清理

文件：`third_party/gnc_ws/src/gnc/ship_guidance/src/coordinate_transform_node.cpp`

现有 `reset_callback`（只调 `set_origin`）追加清 route 守卫状态：
- `has_last_route_` = false
- `last_accepted_route_time_` = rclcpp::Time(0, 0)
- `last_feedback_path_`.clear()
- `feedback_dp_latched_` = false

#### 4. thrust_allocation_node（P2）— 清执行器热启动状态

文件：`third_party/gnc_ws/src/gnc/thrust_allocation/src/thrust_allocation_node.cpp` + `include/thrust_allocation/thrust_allocation_node.hpp`

加 ShipReset 订阅（`/ship/dynamics_reset`）+ `reset_callback`。回调清：
- 每个 `states_[i]`：`last_thrust_N` = 0, `last_angle_rad` = 对应 cfg 的 `angle_fixed`（核查默认值）
- `tau_des_prev_` = 0, `tau_env_` = 0, `last_rudder_cmd_` = 0
- `is_maneuvering_mode_` = false

## 向后兼容

- 4 节点：不发 ShipReset 时，新订阅永不触发，行为完全不变
- bridge：零改动（现有 pub_dynamics_reset_ 自动广播给新订阅者）
- orchestrator：零改动（已有 ShipReset 发布）

## 验收标准

连续 2 次 rule14-ho（不 restart 容器，只靠 reset 接口），10/10 指标一致（AVOID onset Δ<5s, steer_dir 相同, min_cpa Δ<10m, final_xte Δ<20m）。达到与 restart 工况等效的可复现性。

## 已知边界

1. **ship_control 的 mutex**：control_loop（timer）与 reset_callback（subscriber）并发访问积分器。需核查 ship_control 是否已有 mutex，无则新增。ship_guidance 同理（control_loop + path_callback 并发）。
2. **thrust_allocation 的 states_ 初始角度**：reset 时 `last_angle_rad` 应重设到推进器配置的固定角（如全回转=0°，固定推进器=安装角）。需读 cfg 结构确认默认值。
3. **ship_guidance control_loop 频率**：清积分后，下一拍 control_loop 用清零的 integral_e_ 重算 ILOS，首拍行为等价于冷启。

## 不在范围

- ship_control 的模式状态机重置（active_mode_ 等）—— 模式由外部 topic 驱动，reset 后首拍会被正常 callback 覆盖，不强制清
- propulsion_policy_node / active_route_manager_node —— 无残留，无需 reset
- SIL profile —— SIL stack 由现有 lifecycle 覆盖

## 关键文件

- `third_party/gnc_ws/src/gnc/ship_control/src/ship_control_node.cpp`（积分更新 cpp:29,319-342,462-547,687-714,963-1091）
- `third_party/gnc_ws/src/gnc/ship_control/include/ship_control/ship_control_node.hpp`（成员 hpp:57-106）
- `third_party/gnc_ws/src/gnc/ship_guidance/src/ship_guidance_node.cpp`（ILOS cpp:3093,3134,3388,3955；path_callback cpp:1607-1751；control_loop cpp:3145）
- `third_party/gnc_ws/src/gnc/ship_guidance/include/ship_guidance/ship_guidance_node.hpp`（成员 hpp:229-369）
- `third_party/gnc_ws/src/gnc/ship_guidance/src/coordinate_transform_node.cpp`（reset_callback cpp:1102-1108）
- `third_party/gnc_ws/src/gnc/thrust_allocation/src/thrust_allocation_node.cpp`（热启动 cpp:895-916；角速率 cpp:1059-1066）
- `third_party/gnc_ws/src/gnc/thrust_allocation/include/thrust_allocation/thrust_allocation_node.hpp`（成员 hpp:55-60,107,111-112,182）
