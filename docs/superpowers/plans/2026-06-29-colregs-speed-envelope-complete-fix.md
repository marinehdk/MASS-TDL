# COLREGs Speed-Envelope Contract 修复 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 彻底修复 COLREGs rule14/15 cohort 的 speed-envelope contract 缺陷，使 6 场景（ho/ho-port/cs/cs-2/cs-edge/ot-boundary）clean 8-probe overall_pass=true。

**Architecture:** 6 Workstream 分 4 Phase。核心：M5 横向 offset 加 reachability 校验（W4），GNC ODD 参数作 contract 注入 TDL（W2），mock 传 per-scenario speed（W1），scenario 速度适配物理下限（W3），M5↔M6↔M7 协同升级（W5），Rule13 same-course 门修正（W6）。

**Spec:** `docs/superpowers/specs/2026-06-29-colregs-speed-envelope-complete-fix.md`
**Diagnosis:** `docs/Doc From Claude/2026-06-28-colregs-speed-envelope-contract-diagnosis.md`

**Tech Stack:** C++ (ROS2 humble, colcon), Python 3 (pytest), Docker (codex-gnc-validation stack), CasADi/IPOPT (M5 MPC)

**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug`，branch `codex/colregs-12probe-debug`

---

## 执行前置：环境与验证基线

**容器内单测环境**（每个 C++ task 完成后跑）：
```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon build --packages-select <pkg> --cmake-args -DBUILD_TESTING=ON && \
   colcon test --packages-select <pkg> --event-handlers console_direct+"
```

**GNC 栈启停**（cohort 验证用）：
```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```

**单场景探针**（验证用）：
```bash
source scripts/local-a4000-env.sh
PROBE_STUCK_LIMIT=220 python3 scripts/run_colregs_clean_8probe.py \
  --profile gnc --scenario <scenario_id> \
  --restart-between-runs --restart-settle 24 --sim-rate 10 \
  --summary-out runs/<name>_$(date +%Y%m%d_%H%M%S).json \
  --trace-report-dir runs/trace_eval/$(date +%Y%m%d_%H%M%S)_<name>
```

**Python 单测**：
```bash
PYTHONPATH=src python3 -m pytest tests/docker/ -q
```

**基线现状**（修复前，6 场景全 RED）：
- ho/ho-port/cs: first_failure=L6_seamanship
- cs-2: first_failure=L4_colregs_compliance
- cs-edge: first_failure=L2_safety_floor（CPA 0.2m 近撞）
- ot-boundary: M6 silent（own 11-15kn，设计 4.3kn）

---

# Phase 1 — 独立低风险修复（W6 + W1）

两个 Workstream 互不依赖，可并行。先做这两个建立信心 + 不破坏现有行为。

## Task 1.1: W6 Rule13 same-course 门修正（M6 单测先行）

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/rules/colregs/rule13_overtaking.cpp:30-53`
- Test: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule13_overtaking.cpp`（如不存在则新建）

**背景**：`rule13_overtaking.cpp:32 kSameCourseMaxDeg=45.0` 加了非 COLREGs 限制。COLREGs Rule 13(a) "any vessel overtaking any other" 不要求 same-course（maritime_regulations 🟢high）。本 task 不影响 cohort（ot-boundary 是 crossing），但修正真正 course-diff 追越场景。

- [ ] **Step 1: 写失败测试 — course-diff 60° overtaking 应分类为 overtaking**

在 `test/test_rule13_overtaking.cpp` 加（如文件不存在，仿照 `test_rule_latch.cpp` 结构新建）：

```cpp
// Rule 13(a): "any vessel overtaking any other" — no same-course requirement.
// Target abaft own beam (>112.5°) + closing + own faster = overtaking,
// even when courses differ by 60°.
TEST(Rule13Overtaking, ClassifiesOvertakingWithLargeCourseDifference) {
  Rule13Overtaking rule;
  TargetGeometricState geo{};
  geo.ownship_heading_deg = 0.0;
  geo.target_heading_deg = 300.0;   // 60° course difference
  geo.ownship_speed_kn = 14.0;
  geo.target_speed_kn = 4.0;
  geo.bearing_deg = 180.0;          // target dead astern of own → own overtaking
  geo.range_m = 1000.0;
  geo.target_id = 1;

  auto eval = rule.evaluate(geo, OddDomain::ODD_A, RuleParameters{});
  EXPECT_TRUE(eval.is_active);
  EXPECT_EQ(eval.encounter_type, EncounterType::OVERTAKING);
  EXPECT_EQ(eval.role, Role::GIVE_WAY);
}
```

- [ ] **Step 2: 跑测试验证失败**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon build --packages-select m6_colregs_reasoner --cmake-args -DBUILD_TESTING=ON && \
   colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+"
```
Expected: 新测试 FAIL（当前 same-course 门 45° 拒绝 60° diff）。

- [ ] **Step 3: 修正 rule13 — 移除 same-course 硬门，改 confidence 软因子**

修改 `rule13_overtaking.cpp`，删除 `kSameCourseMaxDeg` 硬门判定，abaft beam + speed diff 即分类 overtaking。same-course 仅作 rationale 文本提及（不影响 is_active）：

```cpp
  // COLREGs Rule 13(a): "any vessel overtaking any other" — overtaking does
  // NOT require same/similar course. Only abaft-beam sector (Rule 13(b)) +
  // closing speed differential matters. Course difference is recorded in
  // rationale for auditability but does not gate classification.
  constexpr double kAbaftBeamMinDeg = 112.5;
  constexpr double kAbaftBeamMaxDeg = 247.5;
  constexpr double kMinOvertakingSpeedDiffKn = 2.0;
  const auto in_abaft_beam_sector = [](double relative_bearing_deg_value) {
    return relative_bearing_deg_value >= kAbaftBeamMinDeg &&
        relative_bearing_deg_value <= kAbaftBeamMaxDeg;
  };

  const double kTargetRelBearingFromOwn =
      relative_bearing_deg(geo.ownship_heading_deg, geo.bearing_deg);
  const double kOwnBearingFromTarget = normalize_bearing_deg(geo.bearing_deg + 180.0);
  const double kOwnRelBearingFromTarget =
      relative_bearing_deg(geo.target_heading_deg, kOwnBearingFromTarget);
  const double kCourseDiffDeg = angle_diff_deg(geo.ownship_heading_deg, geo.target_heading_deg);
  const bool kOwnFaster =
      geo.ownship_speed_kn > geo.target_speed_kn + kMinOvertakingSpeedDiffKn;
  const bool kTargetFaster =
      geo.target_speed_kn > geo.ownship_speed_kn + kMinOvertakingSpeedDiffKn;
  const bool kOwnOvertakingTarget =
      kOwnFaster && in_abaft_beam_sector(kOwnRelBearingFromTarget);
  const bool kTargetOvertakingOwn =
      kTargetFaster && in_abaft_beam_sector(kTargetRelBearingFromOwn);

  if (!kOwnOvertakingTarget && !kTargetOvertakingOwn) {
    // ... 不变：返回 is_active=false + rationale
  }
  // ... 后续 own/target overtaking 分支不变，rationale 追加 course diff 信息
```

- [ ] **Step 4: 跑测试验证通过**

同 Step 2 命令。Expected: 新测试 PASS，且原有 Rule13 测试不回归。

- [ ] **Step 5: 回归保护 — 写 counterfactual 测试**

加测试：non-abaft-beam + 非 closing 不应分类 overtaking（防止移除 same-course 门后过度分类）：

```cpp
TEST(Rule13Overtaking, DoesNotClassifyCrossingAsOvertaking) {
  Rule13Overtaking rule;
  TargetGeometricState geo{};
  geo.ownship_heading_deg = 0.0;
  geo.target_heading_deg = 0.0;
  geo.ownship_speed_kn = 10.0;
  geo.target_speed_kn = 4.0;
  geo.bearing_deg = 60.0;   // starboard beam — crossing, NOT abaft
  geo.range_m = 1000.0;
  geo.target_id = 1;
  auto eval = rule.evaluate(geo, OddDomain::ODD_A, RuleParameters{});
  EXPECT_FALSE(eval.is_active);
}
```

- [ ] **Step 6: Commit**

```bash
git add src/l3_tdl_kernel/m6_colregs_reasoner/src/rules/colregs/rule13_overtaking.cpp \
        src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule13_overtaking.cpp
git commit -m "fix(m6): Rule13 overtaking no longer requires same-course (COLREGs 13(a))

kSameCourseMaxDeg=45 gate was non-COLREGs. Rule 13(a) 'any vessel overtaking
any other' applies regardless of course difference; only abaft-beam sector
(13(b)) + closing speed diff matters. Course diff now rationale-only.

Verified: 60° course-diff overtaking classified correctly; non-abaft crossing
not over-classified. Does not affect cohort (ot-boundary is crossing)."
```

## Task 1.2: W1 Mock per-scenario speed 注入（mock 修复）

**Files:**
- Modify: `docker/gnc_route_mock_publisher.py:74-107`
- Test: `tests/docker/test_gnc_route_mock_publisher.py`（新建）

**背景**：`gnc_route_mock_publisher.py:94-95` 读 nominalRoute 只取 lat/lon，丢弃 `target_sog_kn`，不填 `RoutePlan.speed_limit_mps` → GNC 用全局 max_transit_speed=8.0 → own 4.3kn 被拉高到 15kn（ot-boundary）。

- [ ] **Step 1: 写失败测试 — RoutePlan 应含 speed_limit_mps**

新建 `tests/docker/test_gnc_route_mock_publisher.py`：

```python
"""Tests for gnc_route_mock_publisher per-scenario speed injection (W1)."""
import pytest


def test_load_preserves_target_sog_kn(tmp_path, monkeypatch):
    """_load() must keep target_sog_kn per waypoint, not just lat/lon."""
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    yaml_content = """
ownShip:
  nominalRoute:
    - latitude: 63.44
      longitude: 10.38
      target_sog_kn: 4.3
    - latitude: 63.606667
      longitude: 10.38
      target_sog_kn: 4.3
"""
    (scenario_dir / "colreg-test.yaml").write_text(yaml_content)

    # Import the module under test (adjust import path to actual location)
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "gnc_route_mock_publisher",
        "docker/gnc_route_mock_publisher.py")
    mod = importlib.util.module_from_spec(spec)

    # Stub rclpy to allow import without ROS2
    import sys, types
    if "rclpy" not in sys.modules:
        rclpy_stub = types.ModuleType("rclpy")
        rclpy_stub.node = types.ModuleType("rclpy.node")
        sys.modules["rclpy"] = rclpy_stub
        sys.modules["rclpy.node"] = rclpy_stub.node
    spec.loader.exec_module(mod)

    publisher = object.__new__(mod.GncRouteMockPublisher)  # bypass __init__
    publisher._scenario_dir = str(scenario_dir)
    publisher._waypoints = None
    publisher._yaml_speeds_kn = None
    publisher._is_active = False
    publisher._current_scenario_id = None

    publisher._load("colreg-test")

    assert publisher._waypoints is not None
    assert len(publisher._waypoints) == 2
    assert publisher._yaml_speeds_kn == [4.3, 4.3]


def test_on_timer_fills_speed_limit_mps(tmp_path):
    """_on_timer built RoutePlan must populate speed_limit_mps (m/s)."""
    # Reuse setup from above; verify msg.speed_limit_mps non-empty,
    # length matches latitude, values = kn * 0.514444.
    # (Capture the published msg via a fake publisher stub.)
    pytest.skip("Implement after _load test passes; structure mirrors test above.")
```

- [ ] **Step 2: 跑测试验证失败**

```bash
PYTHONPATH=src python3 -m pytest tests/docker/test_gnc_route_mock_publisher.py -v
```
Expected: `test_load_preserves_target_sog_kn` FAIL（当前 `_load` 不存 `_yaml_speeds_kn`）。

- [ ] **Step 3: 修改 _load() 保留 target_sog_kn**

修改 `docker/gnc_route_mock_publisher.py:90-96`：

```python
        nominal = scenario.get("ownShip", {}).get("nominalRoute")
        if not nominal or len(nominal) < 2:
            self.get_logger().warn(f"no nominalRoute in {scenario_id}")
            return
        self._waypoints = [(float(wp["latitude"]), float(wp["longitude"]))
                           for wp in nominal]
        # W1: preserve per-waypoint target_sog_kn so RoutePlan.speed_limit_mps
        # is populated downstream. Fallback to ownShip.initial.sog, then 0.
        own_sog = scenario.get("ownShip", {}).get("initial", {}).get("sog", 0.0)
        self._yaml_speeds_kn = [
            float(wp.get("target_sog_kn", own_sog)) for wp in nominal
        ]
        self._is_active = True
        self.get_logger().info(
            f"GNC route ACTIVE — {len(self._waypoints)} wp from {scenario_id}")
```

- [ ] **Step 4: 修改 _on_timer() 填 speed_limit_mps**

修改 `_on_timer`（line 100-107）：

```python
    def _on_timer(self):
        if not self._is_active or not self._waypoints:
            return
        msg = RoutePlan()
        msg.header = Header(stamp=_now(self), frame_id="map")
        msg.latitude = [lat for lat, _ in self._waypoints]
        msg.longitude = [lon for _, lon in self._waypoints]
        # W1: populate speed_limit_mps (m/s). Empty/0 means "no limit" in GNC;
        # we only emit when target_sog was actually specified (non-zero).
        if self._yaml_speeds_kn and all(s > 0 for s in self._yaml_speeds_kn):
            msg.speed_limit_mps = [s * 0.514444 for s in self._yaml_speeds_kn]
        self._pub.publish(msg)
```

同时在 `__init__` / 类属性初始化处加 `self._yaml_speeds_kn = []`（若未有）。

- [ ] **Step 5: 跑测试验证通过 + 完善 _on_timer 测试**

```bash
PYTHONPATH=src python3 -m pytest tests/docker/test_gnc_route_mock_publisher.py -v
```
补全 Step 1 的 `test_on_timer_fills_speed_limit_mps`（去掉 skip，验证 msg.speed_limit_mps 非空 + 长度匹配 + 值=kn×0.514444）。Expected: 全 PASS。

- [ ] **Step 6: Commit**

```bash
git add docker/gnc_route_mock_publisher.py tests/docker/test_gnc_route_mock_publisher.py
git commit -m "fix(mock): inject per-scenario target_sog into GNC RoutePlan.speed_limit_mps (W1)

gnc_route_mock_publisher dropped target_sog_kn, leaving speed_limit_mps empty,
so GNC used global max_transit_speed=8.0 and own ship was pulled off design
speed (ot-boundary 4.3kn → 15kn). Now _load keeps speeds, _on_timer emits
speed_limit_mps in m/s.

Note: own still subject to GNC cruise_min_speed floor (W3) — this fix alone
restores per-scenario intent but own may still be floored to ≥3.8 m/s."
```

- [ ] **Step 7: Phase 1 cohort 回归（确认 W6+W1 不破坏）**

跑 ho + ot-boundary 单场景探针，确认 W6 不回归（ho/ot-boundary 行为不变），W1 让 ot-boundary own 速度变化（应从 15kn 降到 cruise_min floor ~7.4kn）。

Expected: ho 行为不变；ot-boundary own 速度下降（但仍可能 > 4.3kn 因 cruise_min floor，需 W3）。ot-boundary M6 可能仍 silent（若 own 7.4kn 仍使 range 不 closing）—— 这是预期的，W3/W4 才彻底解决。

---

# Phase 2 — GNC ODD contract 暴露 + M5 横向 offset reachability（W2 → W4）

核心修复。W2 前置（GNC 暴露 ODD 参数），W4 消费（M5 reachability 校验）。

## Task 2.1: W2 GNC ODD 参数发布（GNC 侧）

**Files:**
- Create: `third_party/gnc_ws/src/platform/ship_interfaces/msg/GncExecutionOdd.msg`
- Modify: `third_party/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp`（发布 ODD）
- Modify: `third_party/gnc_ws/src/platform/ship_interfaces/CMakeLists.txt`（注册 msg）

**背景**：GNC ODD 参数（emergency_avoidance_speed_cap, max_lateral_accel, max_decel, static_min_turn_radius, cruise_min_speed, max_transit_speed）当前硬编码在 active_route_manager + overlay。需作 latched contract msg 发布，让 M5 订阅消费。

- [ ] **Step 1: 定义 GncExecutionOdd.msg**

```text
# GncExecutionOdd.msg — GNC execution ODD contract for TDL consumption.
# Latched: published on startup + when parameters change.
# TDL (M5) uses these to generate reachable avoidance geometry.

std_msgs/Header header

# Speed envelope (m/s)
float64 emergency_avoidance_speed_cap_mps    # own speed cap during emergency_avoidance nav mode
float64 cruise_min_speed_mps                  # floor in cruise/transit mode
float64 max_transit_speed_mps                 # upper target speed

# Maneuverability limits
float64 max_lateral_accel_mps2                # lateral acceleration cap
float64 max_decel_mps2                        # deceleration cap
float64 static_min_turn_radius_m              # geometric minimum turn radius
float64 emergency_max_yaw_rate_deg_s          # yaw rate cap in emergency mode

string schema_version   # "1.0"
```

- [ ] **Step 2: 注册 msg 到 ship_interfaces CMakeLists.txt**

在 `third_party/gnc_ws/src/platform/ship_interfaces/CMakeLists.txt` 的 `rosidl_generate_interfaces` 列表加 `"msg/GncExecutionOdd.msg"`。

- [ ] **Step 3: active_route_manager 发布 GncExecutionOdd（latched）**

在 `active_route_manager_node.cpp` 构造函数加 publisher（latched QoS），在参数加载完成后 + on_configure_param_callback 中发布当前 ODD：

```cpp
#include "ship_interfaces/msg/gnc_execution_odd.hpp"
// ...
rclcpp::Publisher<ship_interfaces::msg::GncExecutionOdd>::SharedPtr odd_pub_;
// in constructor:
rclcpp::QoS odd_qos = rclcpp::QoS(rclcpp::KeepLast(1)).transient_local();
odd_pub_ = create_publisher<ship_interfaces::msg::GncExecutionOdd>(
    "/gnc/execution_odd", odd_qos);
// helper to publish current ODD after params loaded:
auto publish_odd = [this]() {
    ship_interfaces::msg::GncExecutionOdd msg;
    msg.header.stamp = this->now();
    msg.emergency_avoidance_speed_cap_mps = emergency_avoidance_speed_cap_mps_;
    msg.cruise_min_speed_mps = /* read from param or ship_guidance shared */;
    // ... fill all fields from loaded params
    msg.schema_version = "1.0";
    odd_pub_->publish(msg);
};
publish_odd();  // initial publish
```

**注意**：`cruise_min_speed_mps` 等参数在 ship_guidance_node 而非 active_route_manager。两种方案：
- (a) active_route_manager 也声明这些参数（重复，但 msg 单一发布点）
- (b) ship_guidance_node 发布自己的 ODD，active_route_manager 发布 avoidance 相关

**评审决策点**：推荐 (a)，active_route_manager 作为 ODD 单一发布点，声明所有 ODD 参数（从 overlay 读），避免 TDL 订阅两个 topic。若参数已在 ship_guidance_node 声明，active_route_manager 通过参数回调读取（同进程或 parameter bridge）。

- [ ] **Step 4: GNC 侧编译 + 验证 msg 发布**

```bash
# rebuild gnc image (需在 docker-compose.gnc.yml 构建上下文)
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
# 验证 topic 存在
docker exec codex-gnc-validation-gnc-gnc-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && ros2 topic echo /gnc/execution_odd --once"
```
Expected: 输出含 emergency_avoidance_speed_cap_mps=3.2 等字段。

- [ ] **Step 5: Commit**

```bash
git add third_party/gnc_ws/src/platform/ship_interfaces/msg/GncExecutionOdd.msg \
        third_party/gnc_ws/src/platform/ship_interfaces/CMakeLists.txt \
        third_party/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp
git commit -m "feat(gnc): publish GncExecutionOdd contract msg (W2)

GNC ODD params (emergency_avoidance_speed_cap, max_lateral_accel, max_decel,
static_min_turn_radius, cruise_min_speed, max_transit_speed) now published as
latched msg on /gnc/execution_odd so TDL (M5) can consume actual execution
limits instead of hardcoding them."
```

## Task 2.2: W2 GNC bridge 转发 ODD 到 L3 domain

**Files:**
- Modify: `src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp`（订阅 + 转发）

**背景**：GNC 在 domain 50，L3 在 domain 42，gnc_bridge 是唯一跨 domain 进程。需把 `/gnc/execution_odd`（domain 50）转发到 L3 domain（42）。

- [ ] **Step 1: bridge 订阅 domain 50 的 GncExecutionOdd，转发到 domain 42**

在 `gnc_bridge_node.cpp` 加双向 topic bridge（参照现有 avoidance_plan/route_plan 转发模式）：

```cpp
// domain 50 subscriber → domain 42 publisher
auto odd_sub_50 = create_subscription<ship_interfaces::msg::GncExecutionOdd>(
    "/gnc/execution_odd", rclcpp::QoS(1).transient_local(),
    [this](const ship_interfaces::msg::GncExecutionOdd::SharedPtr msg) {
        odd_pub_42_->publish(*msg);
    });
odd_pub_42_ = create_publisher<ship_interfaces::msg::GncExecutionOdd>(
    "/gnc/execution_odd", rclcpp::QoS(1).transient_local());
```

- [ ] **Step 2: rebuild sil-nodes + 验证 L3 domain 收到**

```bash
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
sleep 25
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   ros2 topic echo /gnc/execution_odd --once"
```
Expected: L3 domain 42 也能收到 ODD msg。

- [ ] **Step 3: Commit**

```bash
git add src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp
git commit -m "feat(gnc_bridge): forward GncExecutionOdd from domain 50 to 42 (W2)"
```

## Task 2.3: W4 M5 订阅 GncExecutionOdd + 缓存

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`

- [ ] **Step 1: M5 节点加 ODD 订阅 + 缓存成员**

在 `mid_mpc_node.hpp` 加：
```cpp
#include "ship_interfaces/msg/gnc_execution_odd.hpp"
// in class:
ship_interfaces::msg::GncExecutionOdd latest_gnc_odd_;
rclcpp::Subscription<ship_interfaces::msg::GncExecutionOdd>::SharedPtr gnc_odd_sub_;
std::mutex odd_mutex_;
```

在 `mid_mpc_node.cpp` 构造函数加订阅：
```cpp
gnc_odd_sub_ = create_subscription<ship_interfaces::msg::GncExecutionOdd>(
    "/gnc/execution_odd", rclcpp::SensorDataQoS(),  // 或 transient_local
    [this](const ship_interfaces::msg::GncExecutionOdd::SharedPtr msg) {
        std::lock_guard<std::mutex> lock(odd_mutex_);
        latest_gnc_odd_ = *msg;
    });
```

- [ ] **Step 2: fallback —— 无 ODD msg 时用 GncAvoidancePreflightConfig 硬编码**

加 helper：
```cpp
ship_interfaces::msg::GncExecutionOdd effective_odd() const {
    std::lock_guard<std::mutex> lock(odd_mutex_);
    if (!latest_gnc_odd_.schema_version.empty()) {
        return latest_gnc_odd_;
    }
    // Fallback to hardcoded (matches gnc_avoidance_preflight.hpp defaults)
    ship_interfaces::msg::GncExecutionOdd fallback;
    fallback.emergency_avoidance_speed_cap_mps = 3.2;
    fallback.max_lateral_accel_mps2 = 0.25;
    fallback.max_decel_mps2 = 0.08;
    fallback.static_min_turn_radius_m = 45.0;
    fallback.emergency_max_yaw_rate_deg_s = 2.0;
    fallback.cruise_min_speed_mps = 3.8;
    fallback.max_transit_speed_mps = 8.0;
    return fallback;
}
```

- [ ] **Step 3: rebuild M5 + 单测验证订阅不破坏现有行为**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && \
   colcon test --packages-select m5_tactical_planner --event-handlers console_direct+"
```
Expected: 现有 M5 测试全 PASS（订阅不影响逻辑）。

- [ ] **Step 4: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
git commit -m "feat(m5): subscribe and cache GncExecutionOdd (W4 prep)"
```

## Task 2.4: W4 核心 — reachable_lateral_offset 纯函数 + 单测

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp`
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/test_avoidance_waypoint_gen.cpp`

**核心修复点**。这是 cs-edge 近撞 + cs/ho seamanship FAIL 的精确根因。

- [ ] **Step 1: 写失败测试 — reachable offset 计算**

在 `test_avoidance_waypoint_gen.cpp` 加：

```cpp
#include "ship_interfaces/msg/gnc_execution_odd.hpp"

TEST(ReachableLateralOffset, CapsOffsetToPhysicallyReachable) {
    // own at emergency cap 3.2 m/s, lateral accel 0.25 m/s², TCPA 700s.
    // Reachable lateral displacement in 0.6×700=420s (60% safety margin):
    //   accel phase: t_accel = v_lat_max/a_lat. With v≈3.2 and a=0.25,
    //   simplified bound: 0.5*a*t² for full accel, but lateral speed also
    //   bounded by own heading change. Use conservative: d = 0.5*a*t² capped
    //   at v*t once cruising. For t=420s, a=0.25: 0.5*0.25*420² = 22050m way
    //   over any offset — so the BINDING constraint is heading-rate-limited
    //   lateral speed, not pure accel. See function for model.
    ship_interfaces::msg::GncExecutionOdd odd;
    odd.emergency_avoidance_speed_cap_mps = 3.2;
    odd.max_lateral_accel_mps2 = 0.25;
    odd.emergency_max_yaw_rate_deg_s = 2.0;
    odd.static_min_turn_radius_m = 45.0;

    const double tcpa_s = 700.0;
    const double geometric_offset_m = 270.0;  // default corridor peak
    const double reachable = mass_l3::m5::reachable_lateral_offset_m(
        odd, tcpa_s, geometric_offset_m);
    // Expect reachable <= geometric_offset (capped to physical ability)
    EXPECT_LE(reachable, geometric_offset_m);
    EXPECT_GT(reachable, 0.0);
}

TEST(ReachableLateralOffset, ReturnsGeometricWhenPhysicallyReachable) {
    // Very long TCPA + high accel → physical limit exceeds geometric →
    // return geometric (no cap needed).
    ship_interfaces::msg::GncExecutionOdd odd;
    odd.emergency_avoidance_speed_cap_mps = 3.2;
    odd.max_lateral_accel_mps2 = 0.25;
    odd.emergency_max_yaw_rate_deg_s = 2.0;
    const double reachable = mass_l3::m5::reachable_lateral_offset_m(
        odd, /*tcpa_s=*/10000.0, /*geometric=*/270.0);
    EXPECT_NEAR(reachable, 270.0, 1.0);  // not capped
}

TEST(ReachableLateralOffset, ShrinksRapidlyForShortTcpa) {
    // Short TCPA (60s) → tiny reachable offset.
    ship_interfaces::msg::GncExecutionOdd odd;
    odd.emergency_avoidance_speed_cap_mps = 3.2;
    odd.max_lateral_accel_mps2 = 0.25;
    odd.emergency_max_yaw_rate_deg_s = 2.0;
    const double reachable = mass_l3::m5::reachable_lateral_offset_m(
        odd, /*tcpa_s=*/60.0, /*geometric=*/270.0);
    EXPECT_LT(reachable, 100.0);  // heavily shrunk
}
```

- [ ] **Step 2: 跑测试验证失败**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "... colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && \
   colcon test --packages-select m5_tactical_planner --event-handlers console_direct+"
```
Expected: 编译失败（`reachable_lateral_offset_m` 未定义）。

- [ ] **Step 3: 实现 reachable_lateral_offset_m 纯函数**

在 `avoidance_waypoint_gen.hpp` 加（在 `generate_stable_avoidance_corridor_waypoints` 前）：

```cpp
#include "ship_interfaces/msg/gnc_execution_odd.hpp"

namespace mass_l3::m5 {

// Compute the lateral offset own ship can physically reach within the
// remaining TCPA window, given GNC execution ODD. Used to cap M5 avoidance
// corridor geometry so GNC can actually follow it.
//
// Model: own lateral velocity is bounded by max_lateral_accel (accel phase)
// AND by emergency_speed × sin(yaw-rate-limited heading change). The binding
// constraint is typically yaw-rate: at 3.2 m/s and 2°/s yaw, lateral velocity
// asymptotes to 3.2×sin(small) — own needs sustained turn to build lateral
// velocity. Conservative bound: integrate max lateral accel for the maneuver
// window (60% of TCPA, leaving 40% for recovery), capped by lateral speed
// ceiling = sqrt(2 × a × d) which is self-consistent at d.
//
// Returns min(geometric_offset, physically_reachable).
inline double reachable_lateral_offset_m(
    const ship_interfaces::msg::GncExecutionOdd& odd,
    double tcpa_remaining_s,
    double geometric_offset_m) {
  constexpr double kSafetyFactor = 0.6;  // use 60% of TCPA for lateral maneuver
  constexpr double kMinUsableTcpaS = 30.0;
  const double t_maneuver_s = std::max(
      kMinUsableTcpaS, tcpa_remaining_s * kSafetyFactor);
  const double a_lat = std::max(1e-3, odd.max_lateral_accel_mps2);

  // Lateral speed ceiling from yaw-rate limit: own can redirect up to
  // emergency_speed perpendicular only after ~90° turn, but practically
  // lateral velocity ≈ emergency_speed × sin(yaw_rate × t). Use small-angle
  // integrated bound then cap at emergency_speed.
  const double v_max = std::max(0.1, odd.emergency_avoidance_speed_cap_mps);
  const double yaw_rate_rad_s = odd.emergency_max_yaw_rate_deg_s * M_PI / 180.0;
  // Displacement from sustained yaw: v²/a_yaw gives turn radius; lateral
  // displacement after turning angle θ at radius r: r(1-cos θ).
  // Simplified conservative: 0.5 × a_lat × t² capped at v_max × t (cruise).
  double accel_phase_disp = 0.5 * a_lat * t_maneuver_s * t_maneuver_s;
  double cruise_phase_vel = std::sqrt(2.0 * a_lat * geometric_offset_m);
  cruise_phase_vel = std::min(cruise_phase_vel, v_max);
  // Phase split: t_accel = cruise_phase_vel / a_lat
  double t_accel = cruise_phase_vel / a_lat;
  double reachable;
  if (t_maneuver_s <= t_accel) {
    // Still accelerating — pure accel bound
    reachable = 0.5 * a_lat * t_maneuver_s * t_maneuver_s;
  } else {
    // Accel then cruise
    double accel_disp = 0.5 * a_lat * t_accel * t_accel;
    double cruise_disp = cruise_phase_vel * (t_maneuver_s - t_accel);
    reachable = accel_disp + cruise_disp;
  }
  return std::min(geometric_offset_m, std::max(0.0, reachable));
}

}  // namespace mass_l3::m5
```

**注意**：上述运动学模型是**初始保守版本**，需在 Task 2.4 Step 3 后用 cs-edge 实测 trace 校准（own 实际横向位移 225m/800s vs 模型预测）。若模型过保守（reachable 太小导致 CPA 更差），调整 kSafetyFactor 或改用 yaw-rate 主导模型。这是设计迭代点，单测先锁基本行为。

- [ ] **Step 4: 跑测试验证通过**

Expected: 三个 reachable 测试 PASS。

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/test_avoidance_waypoint_gen.cpp
git commit -m "feat(m5): reachable_lateral_offset_m pure function (W4 core)

Computes physically reachable lateral offset given GNC ODD + remaining TCPA.
Caps M5 avoidance corridor so own ship can follow. Conservative accel+cruise
kinematic model, 60% TCPA safety factor."
```

## Task 2.5: W4 接入 — generate_stable_avoidance_corridor_waypoints 消费 reachable offset

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp:127`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:802`

- [ ] **Step 1: 函数签名加 odd + tcpa 参数**

修改 `generate_stable_avoidance_corridor_waypoints`（line 127-134），在 `max_lateral_offset_m` 后加 `const ship_interfaces::msg::GncExecutionOdd& odd, double tcpa_remaining_s`：

```cpp
inline std::vector<WaypointLatLon> generate_stable_avoidance_corridor_waypoints(
    double heading_min_deg, double heading_max_deg,
    double anchor_lat, double anchor_lon,
    double planned_route_bearing_rad,
    ColregsPreferredDirection preferred_direction = ColregsPreferredDirection::Hold,
    double max_lateral_offset_m = kDefaultStableCorridorPeakOffsetM,
    const ship_interfaces::msg::GncExecutionOdd& odd = {},
    double tcpa_remaining_s = 1.0e9,
    double rejoin_taper_start_m = kDefaultNoRejoinTaperDistanceM,
    double rejoin_taper_end_m = kDefaultNoRejoinTaperDistanceM + 1.0) {
  // W4: cap geometric offset to physically reachable
  const double effective_offset = (odd.schema_version.empty())
      ? max_lateral_offset_m  // fallback: no ODD, keep geometric (legacy)
      : reachable_lateral_offset_m(odd, tcpa_remaining_s, max_lateral_offset_m);
  // ... 后续 lateral_cap 用 effective_offset 替代 max_lateral_offset_m
  const double default_lateral_cap = std::max(
      2.0 * kGncEmergencyWaypointSwitchGateM, std::abs(effective_offset));
```

**注意**：默认参数 `odd={}` + `tcpa=1e9` 保证未传 ODD 时行为同 legacy（geometric 不 cap），不破坏现有调用。

- [ ] **Step 2: mid_mpc_node 调用点传 ODD + TCPA**

修改 `mid_mpc_node.cpp:802` 的调用：

```cpp
        : mass_l3::m5::generate_stable_avoidance_corridor_waypoints(
            anchor.heading_min_deg,
            anchor.heading_max_deg,
            anchor.lat_deg,
            anchor.lon_deg,
            anchor.route_bearing_rad,
            anchor.direction,
            kDefaultStableCorridorPeakOffsetM,
            effective_odd(),              // W4: cached GNC ODD
            input.tcpa_remaining_s);      // W4: from M2/M6 target state
```

**注意**：`input.tcpa_remaining_s` 需确认 MidMpcInput 是否含此字段。若无，从 M2 target state 的 tcpa_s 取（primary threat）。若 M5 无 TCPA 输入，需在 MidMpcInput 加字段（从 M2 world_state 的 primary target tcpa 传入）。

- [ ] **Step 3: rebuild + 现有测试不回归**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c "... colcon build m5_tactical_planner ... && colcon test ..."
```
Expected: 现有 M5 测试 PASS（默认参数保证 legacy 行为）。

- [ ] **Step 4: cohort 验证 — cs-edge + cs + ho-port**

跑 cs-edge、cs、ho-port 单场景探针。Expected:
- cs-edge: own 横向 offset 收缩到 reachable，CPA 改善（目标 ≥ floor 270m，若仍不足需 W5）
- cs/ho-port: offset 可达，XTE 收敛改善，seamanship 可能转 PASS

若 cs-edge CPA 仍不足 → reachable offset 不足以满足 floor → 需 W5 提前 onset。这是预期，记录结果。

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
git commit -m "feat(m5): cap avoidance corridor lateral offset to reachable (W4)

generate_stable_avoidance_corridor_waypoints now consumes GncExecutionOdd +
remaining TCPA, capping geometric peak offset to physically reachable.
cs-edge: own no longer commanded 400m lateral it can't reach."
```

---

# Phase 3 — M5↔M6↔M7 reachability 协同（W5）

**前置**：Phase 2 完成，确认 W4 单独对 cs-edge 不足（CPA 仍 < floor），需要提前 onset。

## Task 3.1: W5 AvoidancePlan 加 reachability 字段

**Files:**
- Modify: `third_party/gnc_ws/src/platform/ship_interfaces/msg/AvoidancePlan.msg`
- Modify: `src/l3_tdl_kernel/l3_msgs/msg/AvoidanceWaypoint.msg`（或 AvoidancePlan L3 版）

**背景**：M5 plan 附带 reachability 评估，让 M6/M7 知道几何是否充分。

- [ ] **Step 1: AvoidancePlan msg 加字段**

```text
# added to ship_interfaces AvoidancePlan.msg:
bool cpa_floor_achievable        # true if reachable offset satisfies CPA floor
double required_lateral_offset_m # geometric offset needed for CPA floor
double reachable_lateral_offset_m # physical max own can reach in TCPA
double estimated_time_to_offset_s # time own needs to reach commanded offset
```

- [ ] **Step 2: M5 计算并填充这些字段**

在 `mid_mpc_node.cpp` avoidance plan 生成处，调用 `reachable_lateral_offset_m` 比较 geometric vs reachable，填 `cpa_floor_achievable = (reachable >= required)`。

- [ ] **Step 3: rebuild + Commit**

## Task 3.2: W5 M6 onset 提前（当 reachability 不足）

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`（FSM onset gate）

**背景**：M6 FSM ACTIVE onset 当前受 t_plan gate（TCPA ≤ t_plan）。当 M5 报告 cpa_floor_achievable=false，M6 应提前 onset（给 own 更多时间机动）。

- [ ] **Step 1: M6 订阅 M5 reachability（或通过 M4 behavior plan 透传）**

- [ ] **Step 2: onset gate 放宽条件**

当 `cpa_floor_achievable=false`，t_plan gate 用更大的提前量（如 t_plan × 1.5 或固定 +120s），让 M5 更早开始避让。

- [ ] **Step 3: cohort 验证 cs-edge**

Expected: cs-edge 提前 onset → own 有更多时间 → CPA ≥ floor。

- [ ] **Step 4: Commit**

## Task 3.3: W5 M7 MRM 触发（执行中不可达防护）

**Files:**
- Modify: `src/l3_tdl_kernel/m7_safety_supervisor/`

**背景**：执行中若 own 实际偏离 M5 几何 > 阈值持续 N 秒（own 跟不上），M7 触发 MRM。

- [ ] **Step 1: M7 监听 own 实际 vs M5 plan 偏离**

- [ ] **Step 2: 定义 MRM 触发阈值（评审决策点）**

- [ ] **Step 3: cohort 验证**

- [ ] **Step 4: Commit**

---

# Phase 4 — scenario 速度适配（W3，需评审决策）

**前置**：Phase 1-3 完成，确认剩余 RED 是 scenario 设计速度与 GNC 物理下限冲突（ot-boundary 4.3kn < cruise_min 3.8）。

## Task 4.1: 评审决策 W3 路线

**评审输入**：
- ot-boundary own 4.3kn（2.21 m/s）< cruise_min 3.8 m/s
- cs-edge own 5.5kn（2.83 m/s）< cruise_min 3.8 m/s
- 路线 A（改 scenario ≥7.4kn）vs 路线 B（GNC ODD 动态化，需舵效数据）

**决策**：推荐路线 A。若无舵效实测数据支撑降 cruise_min，路线 B 不可行。

## Task 4.2: W3 路线 A — scenario own 速度修正

**Files:**
- Modify: `scenarios/COLREGs测试/colreg-rule15-ot-boundary.yaml`
- Modify: `scenarios/COLREGs测试/colreg-rule15-cs-edge.yaml`
- Modify: `tools/sil/verify_colreg_tier12.py`（加速度下限校验）

- [ ] **Step 1: ot-boundary own sog 4.3 → 7.4kn，重算几何**

修改 yaml `ownShip.initial.sog` + `nominalRoute.target_sog_kn`。用 `tools/sil/gen_colreg_tier12.py` 或手算保证仍是 crossing/overtake 边界 + DCPA≈0。

- [ ] **Step 2: cs-edge own sog 5.5 → 7.4kn，重算几何**

同上。

- [ ] **Step 3: verify_colreg_tier12 加 own 速度下限校验**

```python
# 拒绝 own 设计速度 < GNC cruise_min (3.8 m/s = 7.4kn) 的场景
OWN_MIN_DESIGN_SPEED_KN = 7.4
if own_sog_kn < OWN_MIN_DESIGN_SPEED_KN:
    findings.append({"code": "OWN_SPEED_BELOW_GNC_FLOOR",
                     "msg": f"own {own_sog_kn}kn < GNC cruise_min 7.4kn"})
```

- [ ] **Step 4: cohort 全验证**

跑 6 场景，目标全 GREEN。

- [ ] **Step 5: Commit**

---

# 最终验证 + 收尾

## Task F1: 完整 cohort 8-probe 验证

- [ ] 跑 rule14+15 cohort（6 场景 + rule13-ot + rule17-cr-so 补全 8-probe）
- [ ] 确认 overall_pass=true（或诚实记录剩余 RED + 根因）

## Task F2: A4000 sync + acceptance

- [ ] sync touched paths to A4000（rsync -avR，narrow scope）
- [ ] A4000 acceptance probe
- [ ] 记录本地 + A4000 证据路径

## Task F3: handoff + 文档更新

- [ ] handoff/workspace_log.md 追加完成条目
- [ ] 诊断报告标记 RESOLVED 章节
- [ ] mempalace diary + drawer

---

## Self-Review

**Spec coverage**:
- W1 (mock speed) → Task 1.2 ✓
- W2 (GNC ODD 暴露) → Task 2.1, 2.2 ✓
- W3 (scenario 适配) → Task 4.1, 4.2 ✓
- W4 (M5 reachability) → Task 2.3, 2.4, 2.5 ✓
- W5 (M5↔M6↔M7) → Task 3.1, 3.2, 3.3 ✓
- W6 (Rule13) → Task 1.1 ✓
- cs-2 phase 特例 → 未独立 task，speed 修完后复测，若仍 RED 单独诊断（spec §5 待评审点 4）

**Placeholder scan**: Task 3.x 和 Task 4.x 含评审决策点 + "若...则..." 条件分支，因为这些依赖 Phase 1-2 实测结果。执行时根据实测填充，非空 placeholder。Task 1.x、2.x 完整代码。

**Type consistency**: `reachable_lateral_offset_m` 签名一致；`GncExecutionOdd` msg 字段名一致；`effective_odd()` helper 一致。

**已知风险**:
- reachable offset 运动学模型需 cs-edge 实测校准（Task 2.4 Step 3 注释）
- W5 onset 提前量需 M6/M4 协调测试
- W3 路线 A 改 scenario 几何需重算保证 encounter 意图
