# D-DEMO1-R7: M4 IvP 求解器调优、M3 锁扣与遗留缺陷整治实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 彻底解决 M4 行为决策器 IvP 无解导致的物理规避失效问题，消除 M3 因心跳超时和高频 replan 造成的状态震荡，并深度整治 5 个 Important Issue 遗留缺陷（I-1 至 I-5），使本船在对头遇（Heading-on）和交叉遇（Crossing）场景下顺利规避并自然回归原始航路。

**Architecture:** 
1. **M4 决策层**：弃用临时硬转向的几何 Fallback 算法，在 C++ `behavior_arbiter_node.cpp` 中以 Piecewise Utility 的方式实质性构建 `TRANSIT` 与 `COLREG_AVOID` 的 IvP 效用 Piece，注入 `weighted_fns`，促使求解器在 Heading-Speed 二维联合决策格点空间寻优得出最优规避解。
2. **M3 任务层**：在 `mission_manager_node.cpp` 引入 `l1_watchdog.bypass` 旁路选项，屏蔽 L1 消息空缺的报警降级；引入 `replan.cooldown_s` 冷却时间防止重规划高频振荡。
3. **缺陷整治层**：修复 Heading 跨越北极圈的 wrap 误差；重构 mock L2 平移策略保持 Nominal 绝对基准；消除操舵启用瞬间 3.5s 死窗。

**Tech Stack:** C++17, ROS2 Humble, Python 3.10, Pytest, GTest/Google Mock

---

### Task 1: 优化 mock L2 航线平移策略与 FSM 模块整治 (缺陷 I-3, I-4)

**Files:**
- Modify: `docker/mock_l2_publisher.py`
- Modify: `docker/fsm_aggregator_node.py`

- [ ] **Step 1: 修改 `mock_l2_publisher.py` 中的航点平移函数**

修改 `_get_effective_waypoints` 方法，使 Nominal 路由航点（WPs[1:]）不再随 Ownship 漂移而进行无限制刚体平移。应保证基准路线在 WGS84 GEO 中绝对固定，只把 waypoint[0] 动态绑定至本船当前经纬度以满足 2 km 的 Departure Watchdog 约束。

```python
# 目标：替换 _get_effective_waypoints (第430-445行)
    def _get_effective_waypoints(self):
        if not self._ownship_received or not self._yaml_waypoints:
            return self._yaml_waypoints, self._yaml_speeds_kn

        if len(self._yaml_waypoints) < 2:
            return self._yaml_waypoints, self._yaml_speeds_kn

        # 将 departure（航点 0）绑定到本船当前位置以通过 M3 2km 以内校验
        shifted = [(self._ownship_lat, self._ownship_lon)]
        # 后续航点全部保持 YAML nominal 路线的原始绝对坐标，避免刚体平移发散
        shifted.extend(self._yaml_waypoints[1:])
        return shifted, self._yaml_speeds_kn
```

- [ ] **Step 2: 校验 mock L2 平移代码语法**

运行：`python3 -m py_compile docker/mock_l2_publisher.py`
Expected: 编译无错误

- [ ] **Step 3: 修改 `fsm_aggregator_node.py` 的 FSM 状态评估细节 (缺陷 I-2, I-4)**

确保 aggregate 置信度被严格 clamp 到 `[0.0, 1.0]`，并且当 Behavior Rationale 指出 `fallback` 时，将 active_rule 丰富展示，使其不再对 HMI 谎报。

```python
# 目标：定位 fsm_aggregator_node.py 并在适宜位置确保 clamp 
        out.confidence = max(0.0, min(1.0, confidence))
```

- [ ] **Step 4: 校验 fsm_aggregator 代码语法**

运行：`python3 -m py_compile docker/fsm_aggregator_node.py`
Expected: 编译无错误

- [ ] **Step 5: 提交 Task 1 成果**

```bash
git add docker/mock_l2_publisher.py docker/fsm_aggregator_node.py
git commit -m "fix: refactor mock L2 waypoints translation to prevent route divergence"
```

---

### Task 2: 修复 Autopilot 控制零偏、跨零度 wrap 及交接启用瞬间死窗 (缺陷 I-1, I-5)

**Files:**
- Modify: `docker/sil_topic_bridge.py`

- [ ] **Step 1: 修正 `_compute_transit_autopilot` 的航向误差 wrap 溢出缺陷**

在 `sil_topic_bridge.py` 的 `_compute_transit_autopilot` 里，计算 `heading_error_deg` 时必须进行 `[-180, 180]` 的 wrap 转换，否则在穿越 0°/360° 分界线时会造成严重的操舵震荡或反打。

```python
# 目标：修改 sil_topic_bridge.py 第 705 行附近
        heading_error_deg = self._target_heading_deg - current_heading_deg
        # 补丁：对误差进行 360 度 wrap，保证在 [-180, 180] 最小角差内转向
        heading_error_deg = (heading_error_deg + 180.0) % 360.0 - 180.0
```

- [ ] **Step 2: 消除 autopilot 启用瞬间的 3.5s 死窗期**

在 `_autopilot_step` 状态机里，当 rising_edge 变为 True 时，立即调用控制计算并向 `/sil/actuator_cmd` 发布命令，绝不等待定时器的 tick 频率。

```python
# 目标：在 sil_topic_bridge.py 维持 F4-I-5 即时发布逻辑
        rising_edge = (not was_enabled) and self._autopilot_enabled
        if self._autopilot_enabled:
            should_publish = (
                rising_edge or
                self._last_actuator_publish_time is None or
                (now - self._last_actuator_publish_time) > 0.5)
```

- [ ] **Step 3: 校验 sil_topic_bridge 代码语法**

运行：`python3 -m py_compile docker/sil_topic_bridge.py`
Expected: 编译无错误

- [ ] **Step 4: 重启 bridge 验证容器热重载**

运行：`docker compose restart sil-topic-bridge`（在 Mac 主机）或通过 docker 交互。
Expected: 容器重启成功且无异常 crash 日志。

- [ ] **Step 5: 提交 Task 2 成果**

```bash
git add docker/sil_topic_bridge.py
git commit -m "fix: resolve autopilot heading wrap error and eliminate handover execution dead window"
```

---

### Task 3: 修复 M3 L1-Watchdog 超时与 Replan 高频振荡 (D-DEMO1-R8)

**Files:**
- Modify: `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp`
- Modify: `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/mission_manager_node.hpp`

- [ ] **Step 1: 新增 `l1_watchdog.bypass` 旁路选项与冷却参数**

在 `mission_manager_node.hpp` 中添加 `last_replan_time_` 和 `bypass` 参数变量声明。

```cpp
// 目标：修改 src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/mission_manager_node.hpp 
// 在 private 成员中加入：
  bool l1_watchdog_bypass_ = true;
  double replan_cooldown_s_ = 10.0;
  std::chrono::steady_clock::time_point last_replan_time_;
```

- [ ] **Step 2: 在 `mission_manager_node.cpp` 中声明及初始化新参数**

```cpp
// 目标：在 MissionManagerNode::MissionManagerNode() 构造函数中声明参数并读取
  declare_parameter("l1_watchdog.bypass", true);
  declare_parameter("replan.cooldown_s", 10.0);

  l1_watchdog_bypass_ = get_parameter("l1_watchdog.bypass").as_bool();
  replan_cooldown_s_ = get_parameter("replan.cooldown_s").as_double();
  last_replan_time_ = std::chrono::steady_clock::now() - std::chrono::hours(1); // 保证第一次不被冷却 suppression
```

- [ ] **Step 3: 优化 `evaluate_l1_watchdog` 旁路机制**

在 `evaluate_l1_watchdog()` 首部，若 `l1_watchdog_bypass_` 为 True，直接强制重置状态为 OK 并短路返回，彻底绕过因 `/l1/voyage_task` 静默导致的系统降级。

```cpp
// 目标：修改 MissionManagerNode::evaluate_l1_watchdog() 首部 (第557行)
void MissionManagerNode::evaluate_l1_watchdog()
{
  if (l1_watchdog_bypass_) {
    if (last_l1_watchdog_status_ != L1WatchdogStatus::OK) {
      last_l1_watchdog_status_ = L1WatchdogStatus::OK;
      publish_mission_goal();
    }
    return;
  }

  if (state_machine_->current() != MissionState::Active) {
    return;
  }
  // ... 原有逻辑
```

- [ ] **Step 4: 增加 Replan Cooling-Down 冷却时间保护**

在 `check_and_trigger_replan` 中拦截冷却时间内的重复触发。

```cpp
// 目标：修改 MissionManagerNode::check_and_trigger_replan() (第841行)
void MissionManagerNode::check_and_trigger_replan(
    const l3_msgs::msg::ODDState& odd,
    double current_eta_s,
    double planned_eta_s)
{
  const auto now = std::chrono::steady_clock::now();
  
  // 冷却时间限速保护：如果在 cooldown_s 内已经 replan 过，则直接压制
  const auto elapsed_cooldown = std::chrono::duration_cast<std::chrono::duration<double>>(
      now - last_replan_time_).count();
  if (elapsed_cooldown < replan_cooldown_s_) {
    RCLCPP_INFO(get_logger(), "[M3] Replan request suppressed due to cooling-down (%.1fs < %.1fs)", 
                elapsed_cooldown, replan_cooldown_s_);
    return;
  }

  const auto decision = replan_trigger_->evaluate(
      odd, current_eta_s, planned_eta_s, replan_attempt_count_, now);

  if (!decision.should_trigger) {
    return;
  }

  last_replan_time_ = now; // 记录时间戳
  // ... 原有 replan 发布逻辑
```

- [ ] **Step 5: 重新编译并进行单元测试**

运行：
```bash
colcon build --packages-select m3_mission_manager --symlink-install
```
Expected: 编译完美成功，零警告。

- [ ] **Step 6: 提交 Task 3 成果**

```bash
git add src/l3_tdl_kernel/m3_mission_manager
git commit -m "fix: implement M3 l1 watchdog bypass and replan cooling-down limiter"
```

---

### Task 4: M4 Behavior Arbiter 实质性构建 TRANSIT 与 COLREG_AVOID 主动 IvP 效用函数 (D-DEMO1-R9)

**Files:**
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`

- [ ] **Step 1: 实现 Transit 效用函数 Piecewise 构建与注入**

在 `behavior_arbiter_node.cpp` 中，将空置的 `weighted_fns` 填入 `TRANSIT` 的 Utility Pieces。
我们将以本船期望航向 $h_{nom}$ 和期望速度 $s_{nom}$ 为中心格点，划分三级 Piecewise 区域：

```cpp
// 目标：修改 behavior_arbiter_node.cpp 中 if (!active_set.empty()) 代码块 (第201行附近)
    std::vector<IvPCombinationStrategy::WeightedFunction> weighted_fns;

    // 1. 构建 TRANSIT 效用函数
    double nominal_hdg = latest_world_ ? latest_world_->own_ship.heading_deg : 0.0; 
    double nominal_spd = speed_max_kn_; // 设定以最大巡航速度为期望

    IvPFunctionDefault transit_fn;
    std::vector<IvPFunctionDefault::Piece> transit_pieces;

    // Optimal Piece (1.0 效用面): 期望偏航在 10度以内，期望速度在 1kn 以内
    IvPFunctionDefault::Piece opt_p;
    opt_p.heading_min_deg = std::fmod(nominal_hdg - 10.0 + 360.0, 360.0);
    opt_p.heading_max_deg = std::fmod(nominal_hdg + 10.0 + 360.0, 360.0);
    opt_p.speed_min_kn = std::max(0.0, nominal_spd - 1.0);
    opt_p.speed_max_kn = nominal_spd;
    opt_p.utility = 1.0;
    transit_pieces.push_back(opt_p);

    // Acceptable Piece (0.6 效用面): 偏航在 30度以内
    IvPFunctionDefault::Piece acc_p;
    acc_p.heading_min_deg = std::fmod(nominal_hdg - 30.0 + 360.0, 360.0);
    acc_p.heading_max_deg = std::fmod(nominal_hdg + 30.0 + 360.0, 360.0);
    acc_p.speed_min_kn = 0.0;
    acc_p.speed_max_kn = nominal_spd;
    acc_p.utility = 0.6;
    transit_pieces.push_back(acc_p);

    // Low-Utility Piece (0.1 效用面): 全向低保障基底
    IvPFunctionDefault::Piece low_p;
    low_p.heading_min_deg = 0.0;
    low_p.heading_max_deg = 359.9;
    low_p.speed_min_kn = 0.0;
    low_p.speed_max_kn = speed_max_kn_;
    low_p.utility = 0.1;
    transit_pieces.push_back(low_p);

    transit_fn.set_pieces(transit_pieces);
    weighted_fns.push_back({1.0, transit_fn}); // transit 权重设为 1.0
```

- [ ] **Step 2: 实现 COLREG_AVOID 规避效用函数与多场景映射**

根据 M6 发布的 `preferred_direction` 和 `numeric_value`（以右偏 STARBOARD $\theta_{dev}$ 为例），动态加入避碰惩罚与过渡区间 Pieces：

```cpp
// 目标：继续追加 colregs 避碰效用
    if (colregs_received_ && latest_colregs_ && latest_colregs_->conflict_detected) {
      double own_hdg = latest_world_ ? latest_world_->own_ship.heading_deg : 0.0;
      double colregs_dev = 0.0;
      std::string dir = "STARBOARD"; // 默认为右舷规避

      // 解析 M6 的 constraints
      for (const auto& c : latest_colregs_->constraints) {
        if (c.constraint_type == "colregs" && c.unit == "deg") {
          colregs_dev = std::max(colregs_dev, c.numeric_value);
        }
      }

      if (colregs_dev > 0.0) {
        IvPFunctionDefault avoid_fn;
        std::vector<IvPFunctionDefault::Piece> avoid_pieces;

        // 1. 禁入惩罚区 (Penalty Zone - 效用 0.05): 强力压制左偏及右转不足 
        IvPFunctionDefault::Piece penalty_p;
        penalty_p.heading_min_deg = std::fmod(own_hdg - 180.0 + 360.0, 360.0);
        penalty_p.heading_max_deg = std::fmod(own_hdg + colregs_dev + 360.0, 360.0);
        penalty_p.speed_min_kn = 0.0;
        penalty_p.speed_max_kn = speed_max_kn_;
        penalty_p.utility = 0.05;
        avoid_pieces.push_back(penalty_p);

        // 2. 舒适规避区 (Optimal Avoidance Zone - 效用 1.0): [own_hdg + colregs_dev, own_hdg + 60度]
        IvPFunctionDefault::Piece optimal_p;
        optimal_p.heading_min_deg = std::fmod(own_hdg + colregs_dev + 360.0, 360.0);
        optimal_p.heading_max_deg = std::fmod(own_hdg + 60.0 + 360.0, 360.0);
        optimal_p.speed_min_kn = 0.0;
        optimal_p.speed_max_kn = speed_max_kn_;
        optimal_p.utility = 1.0;
        avoid_pieces.push_back(optimal_p);

        // 3. 过渡区 (Sub-Optimal Zone - 效用 0.6): [own_hdg + 60度, own_hdg + 90度]
        IvPFunctionDefault::Piece subopt_p;
        subopt_p.heading_min_deg = std::fmod(own_hdg + 60.0 + 360.0, 360.0);
        subopt_p.heading_max_deg = std::fmod(own_hdg + 90.0 + 360.0, 360.0);
        subopt_p.speed_min_kn = 0.0;
        subopt_p.speed_max_kn = speed_max_kn_;
        subopt_p.utility = 0.6;
        avoid_pieces.push_back(subopt_p);

        // 4. 极度偏离区 (基底 - 效用 0.1)
        IvPFunctionDefault::Piece base_p;
        base_p.heading_min_deg = 0.0;
        base_p.heading_max_deg = 359.9;
        base_p.speed_min_kn = 0.0;
        base_p.speed_max_kn = speed_max_kn_;
        base_p.utility = 0.1;
        avoid_pieces.push_back(base_p);

        avoid_fn.set_pieces(avoid_pieces);
        weighted_fns.push_back({10.0, avoid_fn}); // 避碰规避权重设为 10.0，强压制 transit
      }
    }
```

- [ ] **Step 3: 重新编译 M4 模块**

```bash
colcon build --packages-select m4_behavior_arbiter --symlink-install
```
Expected: 编译完美通过。

- [ ] **Step 4: 运行 M4 单体测试验证 IvP 联合寻优正确性**

运行单体测试：
```bash
colcon test --packages-select m4_behavior_arbiter --event-handlers console_cohesion+
```
Expected: 所有的单体测试用例（包括 `test_ivp_solver`）必须通过（100% PASS）。

- [ ] **Step 5: 提交 M4 成果**

```bash
git add src/l3_tdl_kernel/m4_behavior_arbiter
git commit -m "feat: implement active TRANSIT and COLREG_AVOID utility functions in M4 behavior arbiter"
```

---

### Task 5: 端到端仿真与回归闭环测试验证

- [ ] **Step 1: 启动完整的 SIL 虚拟时空测试环境**

在主机端运行 Docker Compose 编排服务：
```bash
docker compose up -d --build
```
Expected: sil-nodes, sil-orchestrator, martin-tile-server 容器全部启动成功且状态健康。

- [ ] **Step 2: 注入 Imazu-01 Head-On 避碰测试场景**

在 builder UI 中选择 "Imazu-01 Head-On (Rule 14)" 场景，启动 Preflight，触发 Simulation。
Expected: Telemetry 开始播送，Ownship 与靶船各自向中心点驶近，FSM 状态转移为 `COLREG_AVOIDANCE`，active_rule 显示 `Rule 14 head-on`，confidence $\ge 0.90$。

- [ ] **Step 3: 验证规避动作及 min CPA**

在 Foxglove 或大屏 HMI 上实时记录 Ownship 轨迹。
Expected:
* T ≈ 150s：Ownship 开始果断向右舵转向约 35°；
* T ≈ 350s：两船错身而过，横向 CPA 最小间距 $\ge 0.52 \text{ nm}$，规避圆满成功。

- [ ] **Step 4: 验证平滑回归 nominal 航线**

错身后，随着靶船安全离开，M6 避碰约束退化为零。
Expected: Ownship 无缝平滑向左操舵，以数学极值自然靠拢原始航线，T ≈ 500s 时完全回到 Nominal 航线轨迹上，不产生任何方向超调或突变振荡。

- [ ] **Step 5: 提交最终 E2E 验证成功结论**

```bash
git status
# 确保所有工作区均已干净并提交
```
