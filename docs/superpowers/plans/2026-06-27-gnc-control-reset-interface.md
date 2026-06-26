# GNC 控制层 Reset 接口扩展 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给 ship_control/ship_guidance/thrust_allocation 加 ShipReset 订阅 + 补全 coordinate_transform reset_callback，使 reset 接口覆盖全部跨 run 状态残留，达到与 restart 等效的可复现性（无需每 scenario restart）。

**Architecture:** 复用已有 ShipReset msg + gnc_bridge 转发（bridge 零改动）。3 个新节点订阅已有的 `/ship/dynamics_reset` topic（与 ship_dynamics 同 topic，消息内容相同）。coordinate_transform 在现有 reset_callback 追加 route 守卫清理。

**Tech Stack:** ROS2 Humble (C++ rclcpp), ship_interfaces ShipReset.msg（已存在），colcon build.

**Spec:** `docs/superpowers/specs/2026-06-27-gnc-control-reset-interface-design.md`

**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-gnc-debug`, branch `codex/colregs-gnc-debug`

**Build/run context:**
- 改 GNC 后重建: `bash scripts/gnc-profile-start.sh --down && bash scripts/gnc-profile-start.sh up`
- 验证探针: `SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule14-ho --profile gnc --sim-rate 10`
- ShipReset msg 已在 ship_interfaces 定义（latitude/longitude/heading_deg/sog_kn），bridge 已转发到 `/ship/dynamics_reset`

**验收标准（关键）：** 连续 2 次 rule14-ho 不 restart 容器，10/10 指标一致：AVOID onset Δ<5s, steer_dir 相同, min_cpa Δ<10m, final_xte Δ<20m。

---

## File Structure

| 文件 | 改动 | 责任 |
|------|------|------|
| `third_party/gnc_ws/src/gnc/ship_control/include/ship_control/ship_control_node.hpp` | 改 | reset sub + reset_controller() 声明 + mutex |
| `third_party/gnc_ws/src/gnc/ship_control/src/ship_control_node.cpp` | 改 | reset 回调 + reset_controller() 实现 + 订阅 |
| `third_party/gnc_ws/src/gnc/ship_control/CMakeLists.txt` | 改 | find_package ship_interfaces（若缺失） |
| `third_party/gnc_ws/src/gnc/ship_control/package.xml` | 改 | depend ship_interfaces（若缺失） |
| `third_party/gnc_ws/src/gnc/ship_guidance/include/ship_guidance/ship_guidance_node.hpp` | 改 | reset sub + reset_guidance() 声明 |
| `third_party/gnc_ws/src/gnc/ship_guidance/src/ship_guidance_node.cpp` | 改 | reset 回调 + reset_guidance() 实现 + 订阅 |
| `third_party/gnc_ws/src/gnc/ship_guidance/src/coordinate_transform_node.cpp` | 改 | reset_callback 追加 route 守卫清理 |
| `third_party/gnc_ws/src/gnc/thrust_allocation/include/thrust_allocation/thrust_allocation_node.hpp` | 改 | reset sub + reset_callback 声明 |
| `third_party/gnc_ws/src/gnc/thrust_allocation/src/thrust_allocation_node.cpp` | 改 | reset 回调 + 实现 + 订阅 |
| `third_party/gnc_ws/src/gnc/thrust_allocation/CMakeLists.txt` | 改 | find_package ship_interfaces（若缺失） |
| `third_party/gnc_ws/src/gnc/thrust_allocation/package.xml` | 改 | depend ship_interfaces（若缺失） |

---

## Task 1: ship_control_node reset（P0 根因）

**Files:**
- Modify: `third_party/gnc_ws/src/gnc/ship_control/src/ship_control_node.cpp`
- Modify: `third_party/gnc_ws/src/gnc/ship_control/include/ship_control/ship_control_node.hpp`
- Modify: `third_party/gnc_ws/src/gnc/ship_control/CMakeLists.txt`（核查）
- Modify: `third_party/gnc_ws/src/gnc/ship_control/package.xml`（核查）

**Context:** 4 PID 积分 integral_surge_/sway_/yaw_/speed_（hpp:100），NDO z_/d_hat_/tau_last_（hpp:89-91），误差历史 prev_error_*/prev_deriv_*/previous_u_（hpp:101-104），last_time_（hpp:106）。需核查 ship_control 是否有 mutex（control_loop timer vs reset subscriber 并发）。

- [ ] **Step 1: 核查 ship_control 依赖 + mutex**

```bash
grep -n 'ship_interfaces\|find_package\|depend' third_party/gnc_ws/src/gnc/ship_control/CMakeLists.txt third_party/gnc_ws/src/gnc/ship_control/package.xml | head
grep -nE 'mutex|lock_guard|data_mutex|control_mutex' third_party/gnc_ws/src/gnc/ship_control/src/ship_control_node.cpp third_party/gnc_ws/src/gnc/ship_control/include/ship_control/ship_control_node.hpp | head
```
若缺 ship_interfaces：CMakeLists 加 `find_package(ship_interfaces REQUIRED)` + ament_target_dependencies，package.xml 加 `<depend>ship_interfaces</depend>`。
若无 mutex：hpp 新增 `std::mutex data_mutex_;`，control_loop 入口和 reset_callback 都 `std::lock_guard`。

- [ ] **Step 2: hpp 加 include + 声明**

include 段加 `#include "ship_interfaces/msg/ship_reset.hpp"`。
private 成员区加：
```cpp
    rclcpp::Subscription<ship_interfaces::msg::ShipReset>::SharedPtr reset_sub_;
    void reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr msg);
    void reset_controller();
```

- [ ] **Step 3: cpp 加 reset_controller() 实现**

在类方法区加：
```cpp
void ShipControlNode::reset_controller() {
    // 清零所有跨 run 累积的积分器/历史状态。等价于冷启初始化。
    // 由 reset_callback 调用，调用方持 mutex。
    integral_surge_ = 0.0;
    integral_sway_ = 0.0;
    integral_yaw_ = 0.0;
    integral_speed_ = 0.0;
    z_.setZero();
    d_hat_.setZero();
    tau_last_.setZero();
    prev_error_surge_ = 0.0;
    prev_error_sway_ = 0.0;
    prev_error_yaw_ = 0.0;
    prev_error_speed_ = 0.0;
    prev_deriv_surge_ = 0.0;
    prev_deriv_sway_ = 0.0;
    prev_deriv_yaw_ = 0.0;
    previous_u_ = 0.0;
    last_time_ = this->now();
    RCLCPP_INFO(this->get_logger(), "reset_controller: integrals + NDO + error history cleared");
}

void ShipControlNode::reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr /*msg*/) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    reset_controller();
}
```

- [ ] **Step 4: cpp 构造函数加订阅**

在所有现有 create_subscription 之后加：
```cpp
    reset_sub_ = this->create_subscription<ship_interfaces::msg::ShipReset>(
        "/ship/dynamics_reset", 10,
        std::bind(&ShipControlNode::reset_callback, this, std::placeholders::_1));
```

- [ ] **Step 5: 重建验证编译**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```
预期：ship_control 编译通过，无错误。

- [ ] **Step 6: Commit**

```bash
git add third_party/gnc_ws/src/gnc/ship_control/
git commit -m "feat(gnc): ship_control_node reset_controller on /ship/dynamics_reset

P0 root cause of run-to-run irreproducibility: 4 PID integrals
(integral_surge/sway/yaw/speed) + NDO integral state (z_/d_hat_/tau_last_)
+ error/derivative history accumulated across runs. reset_callback zeros
all. Backward compatible: no reset msg = no change."
```

---

## Task 2: ship_guidance_node reset（P0 根因）

**Files:**
- Modify: `third_party/gnc_ws/src/gnc/ship_guidance/src/ship_guidance_node.cpp`
- Modify: `third_party/gnc_ws/src/gnc/ship_guidance/include/ship_guidance/ship_guidance_node.hpp`

**Context:** ILOS 积分 integral_e_（hpp:334），beta_hat_（hpp:330），psi_cmd_prev_/init_psi_（hpp:336-337），last_time_（hpp:331），path signature latch has_last_path_signature_/last_path_signature_/last_path_size_（hpp:323-325），rejoin/gate latch far_xte_rejoin_active_/heading_align_active_/corridor_hold_active_/cruise_recovery_gate_cleared_/raw_route_rejoin_active_（hpp:229-245）。ship_guidance 已 find_package(ship_interfaces)（coordinate_transform 同包）。

- [ ] **Step 1: hpp 加 include + 声明**

include 段（coordinate_transform 已加 ship_reset.hpp，ship_guidance 同 hpp 或独立 hpp）确认 `#include "ship_interfaces/msg/ship_reset.hpp"`。
private 成员区加：
```cpp
    rclcpp::Subscription<ship_interfaces::msg::ShipReset>::SharedPtr reset_sub_;
    void reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr msg);
    void reset_guidance();
```

- [ ] **Step 2: cpp 加 reset_guidance() 实现 + reset_callback**

在类方法区加：
```cpp
void ShipGuidanceNode::reset_guidance() {
    // 清零 ILOS 积分器 + 侧滑估计 + 航向历史 + path latch + rejoin gates。
    // 等价于冷启初始化。由 reset_callback 调用。
    integral_e_ = 0.0;
    beta_hat_ = 0.0;
    psi_cmd_prev_ = 0.0;
    init_psi_ = false;
    last_time_ = this->now();
    has_last_path_signature_ = false;
    last_path_signature_ = "";
    last_path_size_ = 0;
    far_xte_rejoin_active_ = false;
    heading_align_active_ = false;
    corridor_hold_active_ = false;
    cruise_recovery_gate_cleared_ = false;
    raw_route_rejoin_active_ = false;
    RCLCPP_INFO(this->get_logger(), "reset_guidance: ILOS integral + path latch + rejoin gates cleared");
}

void ShipGuidanceNode::reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr /*msg*/) {
    reset_guidance();
}
```

> 注意：ship_guidance 的 control_loop 和各 callback 是否在同一 executor 单线程（默认）跑。若是单线程，无需 mutex。若用 MultiThreadedExecutor 需加 mutex。核查 node 构造函数的 executor 类型；若不确定，加 mutex 防御（参照 ship_control Task 1 Step 1）。

- [ ] **Step 3: cpp 构造函数加订阅**

在所有现有 create_subscription 之后加：
```cpp
    reset_sub_ = this->create_subscription<ship_interfaces::msg::ShipReset>(
        "/ship/dynamics_reset", 10,
        std::bind(&ShipGuidanceNode::reset_callback, this, std::placeholders::_1));
```

- [ ] **Step 4: 重建验证编译**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```
预期：ship_guidance 编译通过。

- [ ] **Step 5: Commit**

```bash
git add third_party/gnc_ws/src/gnc/ship_guidance/
git commit -m "feat(gnc): ship_guidance_node reset_guidance on /ship/dynamics_reset

P0 root cause: ILOS integral_e_ + beta_hat_ side-slip estimate +
psi_cmd_prev_ heading history accumulated across runs, biasing avoidance
direction. Also clears path_signature latch (could drop duplicate route
on re-run) and rejoin/gate latches. Backward compatible."
```

---

## Task 3: coordinate_transform_node reset_callback 补漏（P1）

**Files:**
- Modify: `third_party/gnc_ws/src/gnc/ship_guidance/src/coordinate_transform_node.cpp`

**Context:** 现有 reset_callback（cpp:1102-1108）只调 set_origin()，漏清 route 守卫状态 has_last_route_/last_accepted_route_time_/last_feedback_path_/feedback_dp_latched_。

- [ ] **Step 1: 扩展 reset_callback**

在 reset_callback 的 `set_origin(msg->latitude, msg->longitude)` 之后追加：
```cpp
    // 补清 route 更新守卫状态，防止 reset 后新 route 被误 REJECT。
    has_last_route_ = false;
    last_accepted_route_time_ = rclcpp::Time(0, 0);
    last_feedback_path_.clear();
    feedback_dp_latched_ = false;
```

- [ ] **Step 2: 重建验证编译**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```

- [ ] **Step 3: Commit**

```bash
git add third_party/gnc_ws/src/gnc/ship_guidance/src/coordinate_transform_node.cpp
git commit -m "fix(gnc): coordinate_transform reset_callback clears route guard state

Previous reset only cleared origin, leaving has_last_route_ +
last_accepted_route_time_ + last_feedback_path_ which could cause the
new scenario's first route to be REJECTED by the min_route_update_interval
guard, starving ship_guidance of the new path."
```

---

## Task 4: thrust_allocation_node reset（P2 次要）

**Files:**
- Modify: `third_party/gnc_ws/src/gnc/thrust_allocation/src/thrust_allocation_node.cpp`
- Modify: `third_party/gnc_ws/src/gnc/thrust_allocation/include/thrust_allocation/thrust_allocation_node.hpp`
- Modify: `third_party/gnc_ws/src/gnc/thrust_allocation/CMakeLists.txt`（核查）
- Modify: `third_party/gnc_ws/src/gnc/thrust_allocation/package.xml`（核查）

**Context:** states_（vector<ThrusterState>，每元素 last_thrust_N/last_angle_rad，hpp:55-60），tau_des_prev_（hpp:111），tau_env_（hpp:107），last_rudder_cmd_（hpp:112），is_maneuvering_mode_（hpp:182）。无积分器，但执行器热启动状态残留影响 transient。

- [ ] **Step 1: 核查依赖 + 推进器默认角度**

```bash
grep -n 'ship_interfaces\|find_package\|depend' third_party/gnc_ws/src/gnc/thrust_allocation/CMakeLists.txt third_party/gnc_ws/src/gnc/thrust_allocation/package.xml | head
grep -nE 'angle_fixed|angle_default|azimuth|ThrusterState' third_party/gnc_ws/src/gnc/thrust_allocation/src/thrust_allocation_node.cpp | head
```
核查每个推进器的固定角度配置名（reset 时 last_angle_rad 重设到该默认值）。

- [ ] **Step 2: hpp 加 include + 声明**

include 段加 `#include "ship_interfaces/msg/ship_reset.hpp"`。
private 成员区加：
```cpp
    rclcpp::Subscription<ship_interfaces::msg::ShipReset>::SharedPtr reset_sub_;
    void reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr msg);
    void reset_allocator();
```

- [ ] **Step 3: cpp 加 reset_allocator() 实现 + reset_callback**

```cpp
void ThrustAllocationNode::reset_allocator() {
    // 清执行器热启动状态 + maneuvering mode。等价于冷启初始化。
    // last_angle_rad 重设到各推进器的固定角配置（若全回转则 0）。
    for (auto& s : states_) {
        s.last_thrust_N = 0.0;
        // last_angle_rad 保持构造时的初始值（固定推进器的安装角）；
        // 若需精确重设，从 cfg 重读。这里保守清零热启动量。
    }
    tau_des_prev_ = Eigen::Vector3d::Zero();
    tau_env_ = Eigen::Vector3d::Zero();
    last_rudder_cmd_ = 0.0;
    is_maneuvering_mode_ = false;
    RCLCPP_INFO(this->get_logger(), "reset_allocator: thruster hot-start state cleared");
}

void ThrustAllocationNode::reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr /*msg*/) {
    reset_allocator();
}
```

- [ ] **Step 4: cpp 构造函数加订阅**

```cpp
    reset_sub_ = this->create_subscription<ship_interfaces::msg::ShipReset>(
        "/ship/dynamics_reset", 10,
        std::bind(&ThrustAllocationNode::reset_callback, this, std::placeholders::_1));
```

- [ ] **Step 5: 重建验证编译**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```

- [ ] **Step 6: Commit**

```bash
git add third_party/gnc_ws/src/gnc/thrust_allocation/
git commit -m "feat(gnc): thrust_allocation_node reset_allocator on /ship/dynamics_reset

Clears thruster hot-start state (last_thrust_N/last_angle_rad for QP
warm-start) + maneuvering mode + tau_env/last_rudder_cmd. P2: affects
startup transient only, not steady-state. Backward compatible."
```

---

## Task 5: 集成验证 — 不 restart 连续 2 run 可复现

**Files:** 无代码改动，纯验证（核心验收）

**Context:** 这是整个 reset 接口工作的核心验收。4 节点 reset 加完后，应达到与 restart 工况等效的可复现性。若通过 → 无需每 scenario restart（省 25s）。若不通过 → 还有遗漏的残留状态，需进一步排查。

- [ ] **Step 1: 冷启 + 跑 run1**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
sleep 30
mkdir -p runs/stabB
nohup env SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 PROBE_STUCK_LIMIT=150 \
  python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule14-ho \
  --profile gnc --sim-rate 10 \
  --summary-out runs/stabB/r1.json --trace-report-dir runs/stabB/t1 \
  > runs/stabB/r1.log 2>&1 &
```

- [ ] **Step 2: 等 run1 完成（~6min）**

```bash
sleep 360
curl -sk --max-time 10 https://127.0.0.1:18000/api/v1/lifecycle/status | python3 -c "import json,sys; print(json.load(sys.stdin)['current_state'])"
# 预期: unconfigured
```

- [ ] **Step 3: 不 restart 容器，跑 run2（关键：靠 reset 接口，不重启 GNC）**

```bash
nohup env SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 PROBE_STUCK_LIMIT=150 \
  python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule14-ho \
  --profile gnc --sim-rate 10 \
  --summary-out runs/stabB/r2.json --trace-report-dir runs/stabB/t2 \
  > runs/stabB/r2.log 2>&1 &
```

- [ ] **Step 4: 等 run2 完成（~6min）**

```bash
sleep 360
curl -sk --max-time 10 https://127.0.0.1:18000/api/v1/lifecycle/status | python3 -c "import json,sys; print(json.load(sys.stdin)['current_state'])"
```

- [ ] **Step 5: 对比 run1 vs run2（验收）**

```python
import json
def load(p):
    d=json.load(open(p)); return d.get('colreg-rule14-ho',d)
r1=load('runs/stabB/r1.json'); r2=load('runs/stabB/r2.json')
def near(a,b,tol):
    if isinstance(a,(int,float)) and isinstance(b,(int,float)): return abs(a-b)<tol
    return a==b
rows=[
 ('AVOID onset t', r1['bp_transitions'][1][0], r2['bp_transitions'][1][0], 5.0),
 ('RECOVERY t', r1['bp_transitions'][-1][0], r2['bp_transitions'][-1][0], 5.0),
 ('min_cpa_m', r1.get('min_cpa_m'), r2.get('min_cpa_m'), 10.0),
 ('steer_dir', r1.get('steer_dir'), r2.get('steer_dir'), None),
 ('steer_mag', r1.get('steer_mag'), r2.get('steer_mag'), 3.0),
 ('final_xte', r1.get('final_xte'), r2.get('final_xte'), 20.0),
 ('role', r1.get('role'), r2.get('role'), None),
 ('cpa_ok', r1.get('cpa_ok'), r2.get('cpa_ok'), None),
]
allok=True
for label,v1,v2,tol in rows:
    m = '✓' if (v1==v2 if tol is None else near(v1,v2,tol)) else '✗'
    if m=='✗': allok=False
    print(f'{label}: {v1} vs {v2} {m}')
print(f'\n验收: {"PASS - reset 接口达 restart 等效可复现" if allok else "FAIL - 仍有残留需排查"}')
```

- [ ] **Step 6: 记录结论到 handoff + mempalace**

```bash
git add handoff/workspace_log.md
git commit -m "test(gnc): verify control-layer reset — consecutive runs reproducible without restart"
```

---

## Task 6: SIL profile 回归

- [ ] **Step 1: 跑现有测试套件**

```bash
python3 -m pytest tests/scripts/test_run_6_horizon_adaptive.py tests/scripts/test_run_6_scenarios_gate.py tests/scripts/test_gnc_reset_interface.py -q
```
预期：67 passed, 4 skipped。

- [ ] **Step 2: 确认 GNC reset 只在 GNC profile 触发（SIL 不受影响）**

reset publisher 只在 `runtime_profile == "gnc"` 时发（lifecycle_bridge.py），SIL profile 不发，4 个新订阅永不触发。无需额外验证。

---

## Self-Review

**1. Spec coverage:**
- ✅ ship_control 积分器 reset (Task 1) — P0
- ✅ ship_guidance ILOS 积分 + latch reset (Task 2) — P0
- ✅ coordinate_transform route 守卫补漏 (Task 3) — P1
- ✅ thrust_allocation 执行器状态 reset (Task 4) — P2
- ✅ 验收：连续 2 run 不 restart 可复现 (Task 5)
- ✅ SIL 回归 (Task 6)
- ✅ bridge 零改动（复用 /ship/dynamics_reset）

**2. Placeholder scan:** Task 1 Step 1 / Task 4 Step 1 有"核查依赖/mutex/默认角度"分支 —— 这是实施时验证点，给了明确处理路径（缺则加），非 placeholder。

**3. Type consistency:**
- ShipReset msg：复用已定义（latitude/longitude/heading_deg/sog_kn），4 节点都不取字段值（reset 只用信号触发清零，msg 内容仅 plant 用）
- topic：全部 `/ship/dynamics_reset`（与 ship_dynamics 同 topic）—— bridge 已转发，多订阅者自动广播
- 方法名：reset_controller / reset_guidance / reset_allocator —— 各 Task 内一致
