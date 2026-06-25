# Sensor Fusion 传感器融合模块设计说明（Shadow Mode）

> 位置：`src/gnc/sensor_fusion`  
> 当前阶段：旁路观测 / Shadow Mode  
> 重要边界：当前模块**不接管主闭环**，不改变 `/ship/odometry`，不影响制导、控制、动力学和推力分配。主链路仍然使用原始 `/ship/odometry`。

## 1. 模块目标

`sensor_fusion_node` 的目标是把本船导航相关传感器融合成一份更可信的状态估计，并对传感器异常进行检测和记录。

当前阶段已经完成：

- 8 维 EKF 状态估计。
- GNSS 跳变检测与拒绝。
- GNSS 中断后的 dead-reckoning / degraded 状态输出。
- 融合状态 JSON 输出。
- 可视化页面和 CSV 日志旁路记录。

当前阶段暂不做：

- 不把 `/ship/odom_filtered` 接入 `ship_guidance_node`、`ship_control_node`、`thrust_allocation_node`。
- 不替换现有 `/ship/odometry`。
- 不做真实船舶最终导航认证。
- 不处理真实传感器驱动，只定义接入契约和预留接口。

## 2. 当前文件结构

| 文件 | 作用 |
|---|---|
| `include/sensor_fusion/sensor_fusion_node.hpp` | EKF 状态、参数、订阅/发布接口声明 |
| `src/sensor_fusion_node.cpp` | EKF 预测、更新、FDE、状态输出实现 |
| `CMakeLists.txt` | ROS2 C++ 节点构建配置 |
| `package.xml` | ROS2 包依赖声明 |
| `SENSOR_FUSION_DESIGN_CN.md` | 当前说明文档 |

## 3. 当前 ROS 输入输出

### 3.1 当前订阅话题

| Topic | 类型 | 当前用途 | 是否必需 | 备注 |
|---|---|---|---|---|
| `/ship/odometry` | `nav_msgs/msg/Odometry` | 可选输入，仿真中作为完整本船状态观测 | 可选 | Shadow live 模式下用于观察，不改变主链路 |
| `/mock/gnss/odometry` | `nav_msgs/msg/Odometry` | GNSS mock 位置输入 | 可选 | 正式 mock 场景 `007/008` 使用 |
| `/mock/imu` | `sensor_msgs/msg/Imu` | IMU 姿态/角速度输入 | 可选 | 当前使用 roll、p、r；yaw 默认不用 |
| `/mock/heading` | `std_msgs/msg/Float64` | 航向输入，单位 rad | 可选 | mock 罗经/航向仪 |
| `/cmd_tau` | `geometry_msgs/msg/WrenchStamped` | 控制期望广义力，供预测模型使用 | 可选 | 未来建议改用真实执行器反馈 |

### 3.2 当前发布话题

| Topic | 类型 | 作用 | 是否进入主链路 |
|---|---|---|---|
| `/ship/odom_filtered` | `nav_msgs/msg/Odometry` | EKF 输出的滤波本船状态 | 否，当前仅 shadow |
| `/navigation/fusion_status` | `std_msgs/msg/String` | JSON 格式融合健康状态 | 否，当前用于可视化/日志 |

### 3.3 当前可视化/日志

`tools/ship_viz/passive_ship_viz_server.py` 已订阅 `/navigation/fusion_status`，页面显示 `Fusion Shadow` 面板，并将融合状态写入 CSV。

默认日志示例：

- `logs/fusion_status_viz_YYYYMMDD_HHMMSS.csv`

该日志字段包括：

- `initialized`
- `degraded`
- `fault_detected`
- `state_dim`
- `x / y / roll_deg / yaw_deg / speed_mps`
- `q_scale`
- `gnss_updates / gnss_rejects / gnss_timed_out / gnss_fault / gnss_age_s`
- `imu_updates / imu_rejects / imu_timed_out`
- `heading_updates / heading_rejects / heading_timed_out`
- `ship_odom_updates / ship_odom_rejects / ship_odom_timed_out`

## 4. 当前 EKF 状态定义

当前 EKF 状态为 8 维：

```text
X = [x, y, phi, psi, u, v, p, r]^T
```

| 状态 | 含义 | 单位 | 坐标系 |
|---|---|---:|---|
| `x` | 北向位置 | m | 本地 NED / map，北为正 |
| `y` | 东向位置 | m | 本地 NED / map，东为正 |
| `phi` | 横摇角 roll | rad | 船体姿态 |
| `psi` | 艏向角 yaw | rad | 真北顺时针/仿真 yaw 约定 |
| `u` | 纵荡速度 surge | m/s | 船体坐标系，向前为正 |
| `v` | 横荡速度 sway | m/s | 船体坐标系，向右为正 |
| `p` | 横摇角速度 roll rate | rad/s | 船体坐标系 |
| `r` | 艏摇角速度 yaw rate | rad/s | 船体坐标系 |

为什么当前认为 8 维是合格起步版：

- 船舶主运动至少需要 `x/y/psi/u/v/r` 六个状态。
- 当前动力学和反馈中已经包含横摇 `roll` 和 `roll_rate`。
- 用户要求未来把横摇纳入保护边界，因此 EKF 不应继续停留在 6 维。
- 当前 8 维可以支撑静水、GNSS 跳变、GNSS 中断、横摇观测和可视化诊断。

## 5. 运动学与预测模型公式

### 5.1 坐标运动学

船体速度到地图坐标的转换：

```text
x_dot = u cos(psi) - v sin(psi)
y_dot = u sin(psi) + v cos(psi)
phi_dot = p
psi_dot = r
```

离散预测使用一阶欧拉积分：

```text
x_k   = x_{k-1}   + (u cos(psi) - v sin(psi)) dt
y_k   = y_{k-1}   + (u sin(psi) + v cos(psi)) dt
phi_k = wrap(phi_{k-1} + p dt)
psi_k = wrap(psi_{k-1} + r dt)
```

其中 `wrap()` 表示归一化到 `[-pi, pi]`。

### 5.2 名义动力学预测

当前预测模型使用简化的一阶名义质量/阻尼模型：

```text
u_dot = (Fx + D_x u) / M_x
v_dot = (Fy + D_y v) / M_y
p_dot = (D_phi p) / M_phi
r_dot = (Mz + D_psi r) / M_psi
```

离散形式：

```text
u_k = u_{k-1} + u_dot dt
v_k = v_{k-1} + v_dot dt
p_k = p_{k-1} + p_dot dt
r_k = r_{k-1} + r_dot dt
```

当前输入：

```text
tau_cmd = [Fx, Fy, Mz]^T
```

注意：

- 当前 roll 方向暂无真实恢复力矩模型，只用 `D_phi p / M_phi` 做占位阻尼预测。
- 未来若要做实船级横摇估计，应加入 GM、横摇惯量、横摇阻尼、波浪激励或至少使用更完整的 roll 子模型。
- 当前预测使用 `/cmd_tau`，它是控制命令，不是真实执行器反馈；真实接船时应优先使用推进器/RPM/舵角反馈重构实际作用力。

### 5.3 雅可比矩阵 F 的核心项

EKF 预测协方差传播：

```text
P_k^- = F P_{k-1} F^T + Q
```

主要非线性项来自坐标转换：

```text
∂x_dot/∂psi = -u sin(psi) - v cos(psi)
∂y_dot/∂psi =  u cos(psi) - v sin(psi)
∂x_dot/∂u   =  cos(psi)
∂y_dot/∂u   =  sin(psi)
∂x_dot/∂v   = -sin(psi)
∂y_dot/∂v   =  cos(psi)
∂phi_dot/∂p = 1
∂psi_dot/∂r = 1
```

离散后对应项乘以 `dt`。

速度/角速度阻尼项：

```text
F(u,u) = 1 + (D_x / M_x) dt
F(v,v) = 1 + (D_y / M_y) dt
F(p,p) = 1 + (D_phi / M_phi) dt
F(r,r) = 1 + (D_psi / M_psi) dt
```

## 6. 观测模型公式

通用观测方程：

```text
z = H X + noise
```

残差：

```text
y = z - H X
```

角度残差需要 wrap：

```text
y_angle = wrap(y_angle)
```

创新协方差：

```text
S = H P H^T + R
```

卡尔曼增益：

```text
K = P H^T S^{-1}
```

状态更新：

```text
X = X + K y
```

协方差使用 Joseph form，保证数值稳定：

```text
P = (I - K H) P (I - K H)^T + K R K^T
```

## 7. 当前各传感器观测内容

### 7.1 `/ship/odometry`

类型：`nav_msgs/msg/Odometry`

当前可观测状态：

```text
z_ship_odom = [x, y, phi, psi, u, v, p, r]^T
```

字段映射：

| 状态 | Odometry 字段 |
|---|---|
| `x` | `pose.pose.position.x` |
| `y` | `pose.pose.position.y` |
| `phi` | `pose.pose.orientation` 解算 roll |
| `psi` | `pose.pose.orientation` 解算 yaw |
| `u` | `twist.twist.linear.x` |
| `v` | `twist.twist.linear.y` |
| `p` | `twist.twist.angular.x` |
| `r` | `twist.twist.angular.z` |

### 7.2 `/mock/gnss/odometry`

类型：`nav_msgs/msg/Odometry`

默认观测：

```text
z_gnss = [x, y]^T
```

可选观测：

```text
z_gnss += [psi]      # use_gnss_heading=true
z_gnss += [u, v]     # use_gnss_velocity=true
```

当前建议：

- 默认只用 GNSS 位置。
- 不建议默认使用 GNSS heading，除非明确是双天线 GNSS 或高置信 COG/heading。
- 不建议默认使用 GNSS velocity 作为船体速度，除非适配器已明确完成对地速度到船体速度的转换。

### 7.3 `/mock/imu`

类型：`sensor_msgs/msg/Imu`

默认观测：

```text
z_imu = [phi, p, r]^T
```

字段映射：

| 状态 | IMU 字段 |
|---|---|
| `phi` | `orientation` 解算 roll |
| `p` | `angular_velocity.x` |
| `r` | `angular_velocity.z` |

可选：

```text
z_imu += [psi]       # use_imu_yaw=true
```

当前建议：

- IMU yaw 不默认启用，因为普通 IMU yaw 可能存在漂移。
- 若 IMU 内部已经融合磁罗盘/卫导航向，必须在适配器里明确其置信度和来源。

### 7.4 `/mock/heading`

类型：`std_msgs/msg/Float64`

默认观测：

```text
z_heading = [psi]^T
```

当前约定：

- 单位：rad。
- 来源：mock 罗经/航向仪。

未来真实接入时建议改为带时间戳和健康状态的消息，例如：

- `sensor_msgs/msg/Imu` 的 orientation。
- 或自定义 `Heading.msg`，包含 `heading_rad`、`variance`、`source`、`healthy`、`timestamp`。

## 8. FDE 故障检测逻辑

### 8.1 马氏距离检测

每次观测更新前计算：

```text
d^2 = y^T S^{-1} y
```

如果：

```text
d^2 > chi2_threshold
```

则认为该观测异常，可拒绝本次更新，并记录对应传感器的 `reject_count`。

当前分传感器阈值：

| 参数 | 默认含义 |
|---|---|
| `odom_chi2_threshold` | `/ship/odometry` 完整观测门限 |
| `gnss_chi2_threshold` | GNSS 位置观测门限 |
| `imu_chi2_threshold` | IMU 观测门限 |
| `heading_chi2_threshold` | 航向观测门限 |

### 8.2 GNSS 跳变硬门限

正式 `007_gnss_jump` 测试暴露出一个问题：只靠马氏距离门限，在协方差变大时，GNSS 跳变第一帧可能被 EKF 接收。

因此增加连续 GNSS 流硬门限：

```text
position_jump = sqrt((z_x - x)^2 + (z_y - y)^2)
```

如果 GNSS 最近仍然在线，且：

```text
position_jump > gnss_position_jump_gate_m
```

则直接拒绝该 GNSS 位置观测。

默认参数：

```text
gnss_position_jump_gate_m = 20.0
```

注意：

- 该硬门限只在 GNSS 连续在线时启用。
- 如果 GNSS 已经 timeout，再恢复时允许重新捕获，避免中断恢复后永远无法接回 GNSS。

### 8.3 传感器超时

每个传感器都有超时门限：

| 参数 | 默认用途 |
|---|---|
| `ship_odom_timeout_s` | `/ship/odometry` 超时判定 |
| `gnss_timeout_s` | GNSS 超时判定 |
| `imu_timeout_s` | IMU 超时判定 |
| `heading_timeout_s` | 航向超时判定 |

当主要位置源不可用时，`/navigation/fusion_status` 中 `degraded=true`。

## 9. `/navigation/fusion_status` JSON 输出结构

类型：`std_msgs/msg/String`

示例：

```json
{
  "initialized": true,
  "degraded": false,
  "fault_detected": false,
  "state_dim": 8,
  "x": 8251.447,
  "y": -2.935,
  "roll_deg": 0.0,
  "yaw_deg": -0.31,
  "speed_mps": 0.006,
  "q_scale": 1.0,
  "sensors": {
    "gnss": {
      "updates": 0,
      "rejects": 0,
      "timed_out": false,
      "fault": false,
      "age_s": -1.0,
      "mahalanobis": 0.0
    },
    "imu": {
      "updates": 0,
      "rejects": 0,
      "timed_out": false,
      "fault": false,
      "age_s": -1.0,
      "mahalanobis": 0.0
    },
    "heading": {
      "updates": 0,
      "rejects": 0,
      "timed_out": false,
      "fault": false,
      "age_s": -1.0,
      "mahalanobis": 0.0
    },
    "ship_odom": {
      "updates": 100,
      "rejects": 0,
      "timed_out": false,
      "fault": false,
      "age_s": 0.008,
      "mahalanobis": 0.0
    }
  }
}
```

字段解释：

| 字段 | 含义 |
|---|---|
| `initialized` | EKF 是否已初始化 |
| `degraded` | 是否处于降级导航状态 |
| `fault_detected` | 是否检测到任意传感器异常或超时 |
| `state_dim` | 当前 EKF 状态维度，应为 8 |
| `x/y` | 滤波位置 |
| `roll_deg/yaw_deg` | 滤波横摇角/艏向角 |
| `speed_mps` | 滤波合速度 |
| `q_scale` | 自适应过程噪声缩放系数 |
| `sensors.*.updates` | 该传感器接收并接受的观测次数 |
| `sensors.*.rejects` | 该传感器被 FDE 拒绝的次数 |
| `sensors.*.timed_out` | 该传感器是否超时 |
| `sensors.*.fault` | 该传感器是否曾触发故障状态 |
| `sensors.*.age_s` | 距离最近该传感器消息的时间 |
| `sensors.*.mahalanobis` | 最近一次马氏距离或跳变幅值 |

未来建议：

- JSON 适合当前调试阶段。
- 工业化阶段应改为自定义强类型消息，例如 `NavigationFusionStatus.msg`。

## 10. `/ship/odom_filtered` 输出结构

类型：`nav_msgs/msg/Odometry`

字段映射：

| 输出字段 | 含义 |
|---|---|
| `header.frame_id = "odom"` | 地图/本地坐标系 |
| `child_frame_id = "base_link"` | 船体坐标系 |
| `pose.pose.position.x` | 北向位置 `x` |
| `pose.pose.position.y` | 东向位置 `y` |
| `pose.pose.orientation` | roll + yaw 四元数，pitch 当前为 0 |
| `twist.twist.linear.x` | 纵荡速度 `u` |
| `twist.twist.linear.y` | 横荡速度 `v` |
| `twist.twist.angular.x` | 横摇角速度 `p` |
| `twist.twist.angular.z` | 艏摇角速度 `r` |
| `pose.covariance` | 位置/姿态估计方差 |
| `twist.covariance` | 速度/角速度估计方差 |

当前约束：

- 该输出只用于 shadow 观察。
- 主链路尚未消费该 topic。
- 若未来接入主链路，必须通过 launch 参数或配置开关逐步切换，不能直接替换。

## 11. 未来真实传感器接入规划

### 11.1 GNSS / RTK

推荐真实输入结构：

| 方案 | ROS 类型 | 说明 |
|---|---|---|
| 标准 GNSS | `sensor_msgs/msg/NavSatFix` | 原始经纬度、高度、状态、协方差 |
| 已转换本地坐标 | `nav_msgs/msg/Odometry` | 由 adapter 转换成本地 `x/y` |
| 双天线 GNSS | 自定义或 `Odometry` | 可额外提供航向 |

必须提供或标定：

- 天线安装位置到船体参考点/CG 的杆臂。
- WGS84 到本地 NED 的原点和投影规则。
- 位置协方差或 HDOP/RTK fix 状态。
- 更新率。
- 延迟时间戳。
- 跳变、失锁、重捕获状态。

建议 adapter 输出：

```text
/mock/gnss/odometry       # 仿真阶段
/sensors/gnss/odometry    # 真实阶段建议
```

### 11.2 IMU

推荐输入结构：

```text
sensor_msgs/msg/Imu
```

关键字段：

| 字段 | 含义 |
|---|---|
| `orientation` | 姿态四元数，如果 IMU 提供姿态解算 |
| `angular_velocity.x` | 横摇角速度 p |
| `angular_velocity.y` | 俯仰角速度 q，当前暂未使用 |
| `angular_velocity.z` | 艏摇角速度 r |
| `linear_acceleration` | 三轴加速度，未来可用于更高阶 INS |
| `*_covariance` | 协方差 |

必须提供或标定：

- IMU 安装角。
- IMU 安装位置杆臂。
- 陀螺噪声。
- 加计噪声。
- gyro bias。
- accel bias。
- bias random walk。
- 时间同步误差。

### 11.3 罗经 / 航向仪 / 双天线航向

当前 mock 输入：

```text
std_msgs/msg/Float64    # heading rad
```

未来建议结构：

```text
HeadingStamped.msg
```

建议字段：

```text
std_msgs/Header header
float64 heading_rad
float64 variance
string source        # gyrocompass / dual_gnss / magnetic / fused
bool healthy
uint8 quality
```

必须明确：

- 是真北还是磁北。
- 单位是 rad 还是 deg。
- 是否已经做磁偏角修正。
- 航向角正方向。
- 健康状态和置信度。

### 11.4 计程仪 / DVL / Doppler Log

推荐输入结构：

```text
geometry_msgs/msg/TwistWithCovarianceStamped
```

或：

```text
nav_msgs/msg/Odometry
```

关键问题：

- 速度是对水速度还是对地速度。
- 坐标系是船体系还是世界系。
- 是否受流影响。
- 低速时是否可靠。
- 是否有底跟踪/水跟踪状态。

建议用途：

- 对 `u/v` 进行直接观测。
- GNSS 中断时帮助 dead-reckoning。
- 有流时区分船体速度和对地速度。

### 11.5 推进器 / 舵 / 执行器反馈

当前预测使用：

```text
/cmd_tau    # 控制层期望广义力
```

但真实系统中应优先使用执行器实际反馈：

| 数据 | 建议结构 | 用途 |
|---|---|---|
| 主机 RPM | 自定义或 `Float64MultiArray` | 估计实际纵向推力 |
| 螺距/桨距 | 自定义 | 估计推力曲线 |
| 舵角 | 自定义 | 估计艏摇力矩和横向力 |
| 侧推转速/推力 | 自定义 | 低速 DP 预测 |
| 推进器健康状态 | 当前已有 `/thruster/health_status` | 判断执行器能力下降 |

建议未来新增：

```text
/actuator/feedback
```

或拆分为：

```text
/thruster/states
/rudder/states
/engine/states
```

预测模型中应逐步从：

```text
commanded tau
```

过渡到：

```text
estimated actual tau from actuator feedback
```

### 11.6 风、浪、流与环境估计

环境不是传统导航传感器，但对船舶状态估计非常重要。

当前已有相关环境话题：

- `/env/wind_load`
- `/env/current_load`
- `/env/wave/raw_load`
- `/env/total_load`

未来可考虑：

- 把流速作为扩展状态估计。
- 把风流扰动作为过程模型输入。
- 在 GNSS 中断时，结合水速计和流估计减少漂移。

## 12. 未来状态维度扩展建议

### 12.1 当前 8 维：合格起步版

```text
[x, y, phi, psi, u, v, p, r]
```

适合：

- 静水仿真。
- GNSS 跳变检测。
- GNSS 短时中断。
- 横摇观测与日志。

### 12.2 11~12 维：实船导航初级版

建议加入 IMU bias：

```text
[x, y, phi, psi, u, v, p, r, b_p, b_r, b_a]
```

或至少：

```text
[x, y, phi, psi, u, v, p, r, b_p, b_r]
```

适合：

- IMU 漂移建模。
- GNSS 短时中断更稳定。
- 罗经/IMU 互相校验。

### 12.3 14~16 维：更完整实船版

可加入：

```text
current_north, current_east
accel_bias_x, accel_bias_y
GNSS position bias
clock/time delay state
```

适合：

- 有流环境。
- 长时间 GNSS 中断。
- 多传感器延迟不同步。
- 实船级融合验证。

当前阶段不建议一步到位做 16 维，因为缺少真实传感器参数和标定数据，过早复杂化会导致参数不可解释。

## 13. 当前主要参数

| 参数 | 含义 |
|---|---|
| `q_pos` | 位置过程噪声 |
| `q_roll` | 横摇角过程噪声 |
| `q_psi` | 艏向角过程噪声 |
| `q_vel` | 速度过程噪声 |
| `q_p` | 横摇角速度过程噪声 |
| `q_r` | 艏摇角速度过程噪声 |
| `r_pos` | `/ship/odometry` 位置测量噪声 |
| `r_gnss_pos` | GNSS 位置测量噪声 |
| `r_roll` | 横摇测量噪声 |
| `r_psi` | 航向测量噪声 |
| `r_vel` | 速度测量噪声 |
| `r_p` | 横摇角速度测量噪声 |
| `r_r` | 艏摇角速度测量噪声 |
| `nom_mass_x` | 纵荡名义质量 |
| `nom_mass_y` | 横荡名义质量 |
| `nom_mass_roll` | 横摇名义惯量/等效质量 |
| `nom_mass_psi` | 艏摇名义惯量 |
| `nom_damp_x` | 纵荡名义阻尼 |
| `nom_damp_y` | 横荡名义阻尼 |
| `nom_damp_roll` | 横摇名义阻尼 |
| `nom_damp_psi` | 艏摇名义阻尼 |
| `gnss_position_jump_gate_m` | 连续 GNSS 跳变硬门限 |
| `reject_outliers` | 是否拒绝异常观测 |
| `predict_rate_hz` | 预测发布频率 |
| `status_rate_hz` | 状态 JSON 发布频率 |

## 14. 已完成测试

### 14.1 `007_gnss_jump`

目的：GNSS 位置跳变检测。

结果：PASS。

关键指标：

- `fault_detected_during_fault = true`
- `max_gnss_rejects = 80`
- `max_filter_truth_error_m = 7.596`
- `max_gnss_truth_error_m = 41.899`

说明：

- 第一轮测试发现只靠马氏距离不够。
- 增加 `gnss_position_jump_gate_m` 后通过。

### 14.2 `008_gnss_outage`

目的：GNSS 中断时进入 degraded，恢复后重新接收 GNSS。

结果：PASS。

关键指标：

- `degraded_during_fault = true`
- `recovered_after_fault = true`
- `max_filter_truth_error_m = 32.937`
- `max_gnss_truth_error_m = 179.819`

## 15. 当前已知不足

1. 当前使用 JSON 字符串作为状态输出，不适合最终工业化接口。
2. 当前预测仍使用 `/cmd_tau`，不是真实执行器反馈。
3. 当前没有 IMU bias 状态。
4. 当前没有使用 GNSS `NavSatFix` 原始经纬度，真实接入需要 adapter。
5. 当前没有处理多 GNSS 天线、杆臂、延迟补偿。
6. 当前 roll 模型仍是占位阻尼模型，未引入真实横摇恢复力矩。
7. 当前没有对融合输出接管主链路做安全切换，只适合 shadow 验证。

## 16. 下一阶段建议

在不接入主链路前，建议继续做三件事：

1. 固化 `NavigationFusionStatus.msg`，替代 JSON 字符串。
2. 增加真实传感器 adapter 层，把 GNSS/IMU/罗经统一转换成融合节点可用话题。
3. 增加自动化场景回归，把 `007_gnss_jump`、`008_gnss_outage` 加入一键测试报告。

主链路接入应放在下一大阶段：

- 先提供 launch 参数：`state_source:=raw|filtered`。
- 默认仍为 raw。
- 只有 shadow 测试、故障测试、长航线测试都通过后，才允许在测试分支启用 filtered。
