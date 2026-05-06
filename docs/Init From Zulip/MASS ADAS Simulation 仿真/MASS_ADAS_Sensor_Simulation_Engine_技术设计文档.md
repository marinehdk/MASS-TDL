# MASS_ADAS Sensor Simulation Engine 传感器仿真引擎技术设计文档

**文档编号：** SANGO-ADAS-SSE-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  
**状态：** 开发参考

---

## 目录

1. 概述与职责定义
2. 传感器仿真总体架构
3. 仿真数据生成的统一原理
4. GPS 仿真器
5. 罗经仿真器
6. IMU 仿真器
7. 计程仪仿真器
8. 测深仪仿真器
9. 风速风向仪仿真器
10. 舵角反馈仿真器
11. 主机转速反馈仿真器
12. AIS 仿真器
13. 雷达仿真器
14. LiDAR 仿真器
15. 摄像头仿真器
16. 传感器故障注入
17. 传感器噪声模型校准方法
18. 与仿真引擎其他子系统的时序关系
19. 与 MASS_ADAS 生产代码的接口协议
20. 内部子模块分解
21. 参数总览表
22. 测试策略与验收标准
23. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Sensor Simulation Engine（传感器仿真引擎）是 MASS_ADAS 仿真系统的关键子系统。它接收仿真引擎的"上帝视角"真值数据（本船精确状态、所有目标船精确状态、环境精确状态），通过物理推导和噪声模型，生成与真实传感器驱动输出格式完全一致的虚拟传感器数据。

在真实系统中，传感器是物理世界与软件世界之间的桥梁——它们把物理现实（电磁波、声波、光子、惯性力）转换为数字信号。在仿真系统中，Sensor Simulation Engine 替代了这座桥——它把仿真引擎计算的虚拟物理状态转换为与真实传感器相同格式的数字信号。

### 1.2 核心设计原则

**格式一致原则：** 每种仿真传感器的输出必须与对应的真实传感器驱动使用完全相同的数据格式——相同的 ROS2 话题名称、相同的消息类型、相同的 NMEA 语句格式。MASS_ADAS 的 Data Preprocessing 模块用完全相同的代码路径处理仿真数据和真实数据，不存在"if 仿真 then..."的分支。

**统计特性一致原则：** 仿真传感器的噪声统计特性（均值、方差、分布形状、时间相关性）应与真实传感器尽可能一致。理想情况下，对 MASS_ADAS 而言，仿真传感器数据与真实传感器数据在统计上不可区分。

**物理合理原则：** 仿真传感器不只是"真值 + 白噪声"。真实传感器有特定的物理特性——GPS 有多径效应导致的跳变，雷达有海杂波，AIS 有报告延迟，摄像头有能见度限制。仿真必须模拟这些物理效应，否则 MASS_ADAS 的数据预处理和异常检测算法无法被充分测试。

**独立于硬件原则：** 全部传感器数据从仿真引擎的状态中纯数学推导生成，不依赖任何真实硬件。仿真可以在普通笔记本电脑上运行。

### 1.3 传感器仿真与真实传感器的关系

```
真实系统：
  物理世界 → [传感器硬件] → 电信号 → [传感器驱动] → ROS2 消息 → MASS_ADAS

仿真系统：
  仿真引擎状态 → [传感器仿真引擎] → ROS2 消息 → MASS_ADAS
                  ↑ 本文档定义的内容 ↑
```

传感器仿真引擎替代了"物理世界 + 传感器硬件 + 传感器驱动"这三层——直接输出 ROS2 消息。

---

## 2. 传感器仿真总体架构

### 2.1 数据流

```
仿真引擎核心（Ship Dynamics + Environment + Traffic）
     │
     │  每个仿真步提供以下真值：
     │
     │  本船状态：x, y, ψ, u, v, r, roll, pitch, heave
     │  本船执行机构状态：actual_rudder, actual_rpm, actual_bow_thrust
     │  目标船状态列表：每艘的 x, y, course, speed, heading, mmsi, length, beam, height
     │  环境状态：wind_speed, wind_dir, wave_Hs, wave_Tp, wave_dir, visibility, rain, sea_state
     │  海图数据：水深场, 海岸线, 航标位置
     │
     ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Sensor Simulation Engine                      │
│                                                                 │
│  ┌──────────────────────────────────────────────────────┐       │
│  │ 导航传感器仿真组                                       │       │
│  │  GPS / 罗经 / IMU / 计程仪 / 测深仪 / 风速仪          │       │
│  │  舵角反馈 / RPM反馈                                    │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                 │
│  ┌──────────────────────────────────────────────────────┐       │
│  │ 目标检测传感器仿真组                                    │       │
│  │  雷达（ARPA/原始扫描）/ AIS / LiDAR / 摄像头           │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                 │
│  ┌──────────────────────────────────────────────────────┐       │
│  │ 故障注入器                                             │       │
│  │  数据丢失 / 数据冻结 / 噪声增大 / GPS 欺骗             │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                 │
└──────────────────────────┬──────────────────────────────────────┘
                           │
                           ▼
                   ROS2 Middleware (DDS)
                   标准话题，标准消息格式
                           │
                           ▼
                   MASS_ADAS Data Preprocessing
                   （用处理真实传感器完全相同的代码处理）
```

### 2.2 仿真传感器输出话题清单

| 仿真传感器 | ROS2 话题 | 消息格式 | 输出频率 |
|-----------|----------|---------|---------|
| GPS | `/sensors/gps/nmea` | String (NMEA $GPGGA, $GPRMC) | 1~10 Hz |
| 罗经 | `/sensors/compass/nmea` | String (NMEA $HEHDT) | 10~50 Hz |
| IMU | `/sensors/imu/data` | sensor_msgs/Imu | 100~200 Hz |
| 计程仪 | `/sensors/speed_log/nmea` | String (NMEA $VDVBW) | 1~5 Hz |
| 测深仪 | `/sensors/echo_sounder/nmea` | String (NMEA $SDDBT) | 1~5 Hz |
| 风速仪 | `/sensors/wind/nmea` | String (NMEA $WIMWV) | 1 Hz |
| 舵角反馈 | `/sensors/rudder/feedback` | Float64 | 50 Hz |
| RPM 反馈 | `/sensors/engine/rpm` | Float64 | 10 Hz |
| AIS | `/sensors/ais/nmea` | String (NMEA !AIVDM) | 按 IMO 间隔 |
| 雷达 ARPA | `/sensors/radar/arpa_targets` | RadarTargetArray | 2~4 Hz |
| 雷达扫描 | `/sensors/radar/spoke` | RadarSpoke | 20~50 Hz |
| LiDAR | `/sensors/lidar/points` | sensor_msgs/PointCloud2 | 10~20 Hz |
| 摄像头 | `/sensors/camera_X/image` | sensor_msgs/Image | 10~30 Hz |

---

## 3. 仿真数据生成的统一原理

所有传感器仿真遵循同一个三步模型：

```
步骤一：物理推导
  从仿真引擎的真值状态出发，用物理/几何关系推导出
  "如果有一个理想传感器在这里，它应该测量到什么值"

步骤二：传感器效应叠加
  在理想值上叠加该传感器特有的物理效应：
  - 测量噪声（高斯/均匀/泊松）
  - 系统性偏差（零偏、标度因子误差）
  - 动态特性（响应延迟、带宽限制）
  - 环境影响（海杂波、能见度、降水衰减）
  - 几何效应（安装偏差、遮挡、盲区）
  - 间歇性失效（丢帧、跳变、冻结）

步骤三：格式编码
  将处理后的值编码为与真实传感器驱动完全一致的输出格式：
  - NMEA 0183 语句（GPS、罗经、AIS 等）
  - ROS2 标准消息（IMU、PointCloud2、Image）
  - 自定义消息（雷达目标列表）
```

---

## 4. GPS 仿真器

### 4.1 物理推导

GPS 测量的是船上 GPS 天线的 WGS84 地理坐标。仿真引擎已知船的精确位置——ENU 坐标 (x, y) 通过坐标转换得到经纬度。

```
FUNCTION simulate_gps(true_state, gps_config, sim_time):
    
    # 步骤一：精确位置（仿真引擎的真值）
    true_lat, true_lon = enu_to_wgs84(true_state.x, true_state.y, origin)
    true_alt = 0.0    # 海面
    true_sog = sqrt(true_state.u² + true_state.v²)    # 对地速度
    true_cog = atan2(true_state.u × sin(true_state.psi) + true_state.v × cos(true_state.psi),
                     true_state.u × cos(true_state.psi) - true_state.v × sin(true_state.psi))
```

### 4.2 传感器效应叠加

```
    # ---- 测量噪声 ----
    # GPS 位置噪声与 HDOP 相关
    hdop = gps_config.base_hdop + abs(random.gauss(0, 0.3))
    position_sigma = hdop × gps_config.uere    # UERE ≈ 1.5m（User Equivalent Range Error）
    
    noise_north = random.gauss(0, position_sigma)    # 北向噪声（米）
    noise_east = random.gauss(0, position_sigma)     # 东向噪声（米）
    
    gps_lat = true_lat + noise_north / 111111
    gps_lon = true_lon + noise_east / (111111 × cos(radians(true_lat)))
    
    # ---- GPS 跳变模拟 ----
    # 真实 GPS 偶尔因多径效应出现位置跳变（瞬间偏移数十米）
    IF random.random() < gps_config.jump_probability:    # 典型 0.001（0.1%）
        jump_magnitude = random.uniform(20, 100)    # 20~100 米
        jump_direction = random.uniform(0, 2π)
        gps_lat += jump_magnitude × cos(jump_direction) / 111111
        gps_lon += jump_magnitude × sin(jump_direction) / (111111 × cos(radians(true_lat)))
    
    # ---- 速度噪声 ----
    gps_sog = true_sog + random.gauss(0, 0.1)    # ±0.1 m/s
    gps_cog = true_cog × 180/π + random.gauss(0, 0.5)    # ±0.5°
    
    # ---- 低速时 COG 不可靠 ----
    IF true_sog < 0.5:
        gps_cog = random.uniform(0, 360)    # 低速时 COG 随机
    
    # ---- 卫星数量模拟 ----
    num_sats = int(clip(random.gauss(10, 2), 4, 14))
    
    # ---- 定位质量 ----
    fix_quality = 1    # 1=标准GPS, 2=DGPS, 4=RTK
    IF gps_config.mode == "dgps":
        fix_quality = 2
        position_sigma *= 0.3    # DGPS 精度更高
    ELIF gps_config.mode == "rtk":
        fix_quality = 4
        position_sigma *= 0.01   # RTK 厘米级
```

### 4.3 格式编码

```
    # ---- 输出 NMEA GGA 语句 ----
    utc_str = format_utc_hhmmss(sim_time)
    lat_str = format_nmea_lat(gps_lat)
    lon_str = format_nmea_lon(gps_lon)
    
    gga = f"$GPGGA,{utc_str},{lat_str},N,{lon_str},E,{fix_quality},{num_sats:02d},{hdop:.1f},{true_alt:.1f},M,,,,*"
    gga += compute_nmea_checksum(gga)
    
    # ---- 输出 NMEA RMC 语句 ----
    sog_knots = gps_sog / 0.5144
    date_str = format_utc_ddmmyy(sim_time)
    
    rmc = f"$GPRMC,{utc_str},A,{lat_str},N,{lon_str},E,{sog_knots:.1f},{gps_cog:.1f},{date_str},,,A*"
    rmc += compute_nmea_checksum(rmc)
    
    RETURN {gga, rmc}
```

### 4.4 GPS 噪声参数

| 参数 | 默认值 | 说明 |
|------|-------|------|
| UERE（用户等效距离误差） | 1.5 m | 标准 GPS |
| 基础 HDOP | 1.0 | |
| 跳变概率 | 0.001 | 每次测量 0.1% 概率跳变 |
| 跳变幅度 | 20~100 m | |
| SOG 噪声标准差 | 0.1 m/s | |
| COG 噪声标准差 | 0.5° | |
| COG 不可靠速度阈值 | 0.5 m/s | |
| 输出频率 | 1~10 Hz | |

---

## 5. 罗经仿真器

```
FUNCTION simulate_compass(true_heading_deg, compass_config):
    
    # 步骤一：真值
    # 仿真引擎输出的 ψ 就是真航向
    
    # 步骤二：传感器效应
    
    # 随机噪声
    noise = random.gauss(0, compass_config.noise_std)    # ±0.5°
    
    # 罗经系统性偏差（零偏——安装时未完全校准）
    bias = compass_config.bias    # 典型 ±0.2°，启动时随机生成，整个仿真期间恒定
    
    # 罗经动态延迟（罗经的输出滞后于实际航向变化）
    # 用一阶滤波模拟
    compass_output = α × (true_heading_deg + noise + bias) + (1-α) × compass_prev
    # α = dt / (τ_compass + dt), τ_compass ≈ 0.5s
    
    # 步骤三：NMEA 编码
    heading_str = f"{compass_output:.1f}"
    nmea = f"$HEHDT,{heading_str},T*"
    nmea += compute_nmea_checksum(nmea)
    
    RETURN nmea
```

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 噪声标准差 | 0.5° | 卫星罗经 |
| 系统性偏差 | ±0.2° | 每次仿真随机 |
| 动态时间常数 | 0.5 s | 罗经响应延迟 |
| 输出频率 | 10~50 Hz | |

---

## 6. IMU 仿真器

```
FUNCTION simulate_imu(true_state, imu_config, dt):
    
    # IMU 输出三轴加速度和三轴角速度
    
    # ---- 角速度（陀螺仪）----
    # 仿真引擎已知 p(横摇角速度), q(纵摇角速度), r(艏摇角速度)
    gyro_x = true_state.p + random.gauss(0, imu_config.gyro_noise) + imu_config.gyro_bias_x
    gyro_y = true_state.q + random.gauss(0, imu_config.gyro_noise) + imu_config.gyro_bias_y
    gyro_z = true_state.r + random.gauss(0, imu_config.gyro_noise) + imu_config.gyro_bias_z
    
    # 陀螺仪零偏漂移（缓慢变化的偏移）
    imu_config.gyro_bias_x += random.gauss(0, imu_config.bias_drift_rate) × dt
    imu_config.gyro_bias_y += random.gauss(0, imu_config.bias_drift_rate) × dt
    imu_config.gyro_bias_z += random.gauss(0, imu_config.bias_drift_rate) × dt
    
    # ---- 加速度（加速度计）----
    # 线性加速度 + 重力分量（取决于横摇和纵摇姿态）
    # 在船体坐标系中：
    accel_x = true_state.ax + g × sin(true_state.pitch) + random.gauss(0, imu_config.accel_noise)
    accel_y = true_state.ay - g × sin(true_state.roll) × cos(true_state.pitch) + random.gauss(0, imu_config.accel_noise)
    accel_z = true_state.az - g × cos(true_state.roll) × cos(true_state.pitch) + random.gauss(0, imu_config.accel_noise)
    
    # ---- 姿态角（由 IMU 内部 AHRS 算法解算）----
    roll = true_state.roll × 180/π + random.gauss(0, 0.1)     # ±0.1°
    pitch = true_state.pitch × 180/π + random.gauss(0, 0.1)
    
    # 步骤三：ROS2 Imu 消息
    imu_msg = Imu()
    imu_msg.angular_velocity.x = gyro_x
    imu_msg.angular_velocity.y = gyro_y
    imu_msg.angular_velocity.z = gyro_z
    imu_msg.linear_acceleration.x = accel_x
    imu_msg.linear_acceleration.y = accel_y
    imu_msg.linear_acceleration.z = accel_z
    imu_msg.orientation = quaternion_from_euler(roll, pitch, true_state.psi)
    
    RETURN imu_msg
```

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 陀螺仪噪声密度 | 0.01 °/s/√Hz | MEMS IMU 典型值 |
| 陀螺仪零偏不稳定性 | 10 °/hr | |
| 陀螺仪零偏漂移率 | 0.001 °/s² | |
| 加速度计噪声密度 | 0.002 m/s²/√Hz | |
| 姿态角噪声 | 0.1° | AHRS 输出 |
| 输出频率 | 100~200 Hz | |

---

## 7. 计程仪仿真器

```
FUNCTION simulate_speed_log(true_u, true_v, speed_log_config):
    
    # 关键物理事实：计程仪测量的是对水速度（STW），不是对地速度（SOG）
    # 仿真引擎水动力学模型输出的 u, v 已经是对水速度
    # （因为运动方程在水体参考系中建立，海流通过环境力项处理）
    
    # 纵向对水速度
    stw_long = true_u + random.gauss(0, speed_log_config.noise_std)    # ±0.05 m/s
    
    # 横向对水速度（双轴计程仪）
    stw_trans = true_v + random.gauss(0, speed_log_config.noise_std)
    
    # 计程仪在低速时精度下降
    IF abs(true_u) < 0.5:
        stw_long += random.gauss(0, 0.2)    # 低速噪声增大
    
    # 计程仪在浅水时可能受海底反射干扰
    IF water_depth < 5:
        stw_long += random.gauss(0, 0.3)    # 浅水噪声增大
    
    # NMEA 编码（VBW 语句——对水速度）
    stw_long_knots = stw_long / 0.5144
    stw_trans_knots = stw_trans / 0.5144
    nmea = f"$VDVBW,{stw_long_knots:.1f},{stw_trans_knots:.1f},A,,,A*"
    nmea += compute_nmea_checksum(nmea)
    
    RETURN nmea
```

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 噪声标准差 | 0.05 m/s | 正常精度 |
| 低速附加噪声 | 0.2 m/s | < 0.5 m/s 时 |
| 浅水附加噪声 | 0.3 m/s | 水深 < 5m 时 |
| 输出频率 | 1~5 Hz | |

---

## 8. 测深仪仿真器

```
FUNCTION simulate_echo_sounder(own_x, own_y, virtual_enc, tide_model, sim_time, config):
    
    # 步骤一：真实水深 = 海图水深 + 潮高
    chart_depth = virtual_enc.get_depth(own_x, own_y)    # 从虚拟海图查表
    tide_height = tide_model.get_tide(sim_time)
    true_depth = chart_depth + tide_height
    
    # 步骤二：传感器效应
    
    # 深度依赖的噪声——浅水绝对噪声，深水相对噪声
    IF true_depth < 20:
        noise = random.gauss(0, 0.1)    # 浅水 ±0.1m
    ELSE:
        noise = random.gauss(0, true_depth × 0.01)    # 深水 ±1%
    
    measured_depth = max(0, true_depth + noise)
    
    # 间歇性信号丢失（气泡、鱼群、海底坡度过大）
    IF random.random() < config.dropout_rate:    # 2%
        RETURN None    # 本周期无数据
    
    # 虚假浅水回波（鱼群、温跃层反射）
    IF random.random() < config.false_shallow_rate:    # 0.5%
        measured_depth = measured_depth × random.uniform(0.3, 0.7)    # 虚假浅值
    
    # 步骤三：NMEA 编码
    depth_feet = measured_depth × 3.281
    depth_fathoms = measured_depth × 0.5468
    nmea = f"$SDDBT,{depth_feet:.1f},f,{measured_depth:.1f},M,{depth_fathoms:.1f},F*"
    nmea += compute_nmea_checksum(nmea)
    
    RETURN nmea
```

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 浅水噪声 | ±0.1 m | 水深 < 20m |
| 深水噪声 | ±1% | 水深 ≥ 20m |
| 信号丢失率 | 2% | |
| 虚假浅水率 | 0.5% | |
| 输出频率 | 1~5 Hz | |

---

## 9. 风速风向仪仿真器

### 9.1 视风与真风的物理关系

风速仪安装在船上，它测量的是"视风"（Apparent Wind），不是气象预报中的"真风"（True Wind）。视风是真风矢量与船速反向矢量的合成。

```
FUNCTION simulate_anemometer(true_wind_speed, true_wind_dir, own_speed, own_heading, config):
    
    # 步骤一：真风 → 视风变换
    
    # 真风矢量分解（气象惯例：风从哪里来）
    tw_east = -true_wind_speed × sin(radians(true_wind_dir))
    tw_north = -true_wind_speed × cos(radians(true_wind_dir))
    
    # 船速矢量（船在运动，产生的迎面"风"）
    ship_east = own_speed × sin(radians(own_heading))
    ship_north = own_speed × cos(radians(own_heading))
    
    # 视风矢量 = 真风 - 船速（在地球坐标系中）
    aw_east = tw_east - ship_east
    aw_north = tw_north - ship_north
    
    apparent_speed = sqrt(aw_east² + aw_north²)
    apparent_dir_true = (atan2(-aw_east, -aw_north) × 180/π) % 360
    
    # 转为相对风向（相对于船首，0°=船首正前方，顺时针）
    relative_dir = (apparent_dir_true - own_heading) % 360
    
    # 步骤二：传感器效应
    apparent_speed += random.gauss(0, config.speed_noise)    # ±0.5 m/s
    relative_dir += random.gauss(0, config.dir_noise)         # ±3°
    
    apparent_speed = max(0, apparent_speed)
    relative_dir = relative_dir % 360
    
    # 步骤三：NMEA 编码（MWV 语句——相对风）
    speed_mps = apparent_speed
    nmea = f"$WIMWV,{relative_dir:.0f},R,{speed_mps:.1f},M,A*"
    nmea += compute_nmea_checksum(nmea)
    
    RETURN nmea
```

### 9.2 视风→真风反算的验证意义

Data Preprocessing 收到视风数据后需要反算出真风。仿真器先做了真风→视风的正算，然后 Data Preprocessing 做视风→真风的反算——如果反算正确，应该得到原来的真风值。这个"正算→反算"闭环本身就是一个有效的测试。

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 风速噪声 | ±0.5 m/s | |
| 风向噪声 | ±3° | |
| 输出频率 | 1 Hz | |

---

## 10. 舵角反馈仿真器

```
FUNCTION simulate_rudder_feedback(actual_rudder_angle, config):
    
    # actual_rudder_angle 来自仿真引擎的执行机构响应模型（步骤⑥）
    # 它不等于 Thrust Allocator 下令的指令舵角——包含了响应延迟和速率限制
    
    # 传感器噪声
    measured_angle = actual_rudder_angle + random.gauss(0, config.noise_std)    # ±0.3°
    
    # 量化误差（舵角传感器分辨率通常 0.1°）
    measured_angle = round(measured_angle × 10) / 10
    
    # 偶尔的传感器跳变（电位器接触不良）
    IF random.random() < config.glitch_rate:    # 0.1%
        measured_angle += random.choice([-5, 5])    # 突然跳 5°
    
    RETURN RudderFeedback(angle_deg=measured_angle)
```

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 噪声标准差 | 0.3° | |
| 量化分辨率 | 0.1° | |
| 传感器跳变率 | 0.1% | |
| 输出频率 | 50 Hz | |

---

## 11. 主机转速反馈仿真器

```
FUNCTION simulate_rpm_feedback(actual_rpm, config):
    
    # actual_rpm 来自仿真引擎的执行机构响应模型
    # 包含了主机的加速/减速动态特性
    
    measured_rpm = actual_rpm + random.gauss(0, config.noise_std)    # ±5 RPM
    measured_rpm = max(0, round(measured_rpm))    # 整数，非负
    
    RETURN RpmFeedback(rpm=measured_rpm)
```

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 噪声标准差 | 5 RPM | |
| 输出频率 | 10 Hz | |

---

## 12. AIS 仿真器

### 12.1 AIS 报告间隔

AIS 的报告间隔由 IMO 规定，取决于目标船的导航状态和速度：

```
FUNCTION get_ais_report_interval(nav_status, sog_knots, turning):
    IF nav_status IN [1, 5, 6]:    # 锚泊/系泊
        RETURN 180    # 3 分钟
    IF turning:
        IF sog_knots < 14: RETURN 3.33
        RETURN 2
    IF sog_knots < 3: RETURN 10
    IF sog_knots < 14: RETURN 6
    IF sog_knots < 23: RETURN 6
    RETURN 2
```

### 12.2 AIS 消息生成

```
FUNCTION simulate_ais(target_ships, sim_time, ais_config):
    
    messages = []
    
    FOR EACH target IN target_ships:
        
        # 检查是否有 AIS（部分小船/渔船没有）
        IF NOT target.has_ais:
            CONTINUE
        
        # 检查报告间隔
        interval = get_ais_report_interval(target.nav_status, target.speed_knots, target.turning)
        IF sim_time - ais_state[target.mmsi].last_report < interval:
            CONTINUE
        ais_state[target.mmsi].last_report = sim_time
        
        # 步骤一：从交通引擎已知的目标精确状态出发
        true_lat = target.latitude
        true_lon = target.longitude
        true_sog = target.speed_knots
        true_cog = target.course_deg
        true_heading = target.heading_deg
        true_rot = target.rot
        
        # 步骤二：叠加目标船自身的 GPS 噪声
        # （目标船的 AIS 位置来自目标船自己的 GPS，也有噪声）
        ais_lat = true_lat + random.gauss(0, 5.0 / 111111)     # ±5m
        ais_lon = true_lon + random.gauss(0, 5.0 / (111111 × cos(radians(true_lat))))
        ais_sog = true_sog + random.gauss(0, 0.2)              # ±0.2 节
        ais_cog = true_cog + random.gauss(0, 0.5)              # ±0.5°
        
        # 航向处理（511 = 不可用）
        IF target.heading_available:
            ais_heading = int(true_heading + random.gauss(0, 1.0)) % 360
        ELSE:
            ais_heading = 511
        
        # ROT（转向率）
        IF abs(true_rot) > 0.5:
            ais_rot = int(true_rot)
        ELSE:
            ais_rot = 0
        
        # 步骤三：AIS 6-bit 编码
        payload = encode_ais_message_type1(
            mmsi=target.mmsi,
            nav_status=target.nav_status,
            rot=ais_rot,
            sog=int(ais_sog × 10),                    # 0.1 节单位
            longitude=int(ais_lon × 600000),           # 1/10000 分单位
            latitude=int(ais_lat × 600000),
            cog=int(ais_cog × 10),                     # 0.1° 单位
            heading=ais_heading,
            timestamp=int(sim_time) % 60
        )
        
        armored = encode_6bit_ascii(payload)
        
        nmea = f"!AIVDM,1,1,,A,{armored},0*"
        nmea += compute_nmea_checksum(nmea)
        
        messages.append(nmea)
    
    # 为某些目标偶尔生成静态数据消息（消息类型 5——船名、尺度等）
    FOR EACH target IN target_ships:
        IF target.has_ais AND sim_time - ais_state[target.mmsi].last_static > 360:
            ais_state[target.mmsi].last_static = sim_time
            msg5 = generate_ais_type5(target)
            messages.append(msg5)
    
    RETURN messages
```

### 12.3 AIS 传输延迟

```
# 真实 AIS 消息从目标船发射到本船接收有传输延迟
# 在仿真中通过延迟队列模拟

FUNCTION apply_ais_delay(message, target_range):
    # VHF 传播延迟可忽略（光速）
    # 主要延迟来自 AIS 的 TDMA 时隙分配（0~26.67ms）
    # 以及接收器的解码延迟（~50ms）
    
    tdma_delay = random.uniform(0, 0.02667)    # TDMA 时隙延迟
    decode_delay = 0.05                          # 解码固定延迟
    
    total_delay = tdma_delay + decode_delay
    
    # 远距离 AIS 信号可能丢失
    IF target_range > AIS_MAX_RANGE:
        RETURN None    # 超出 AIS 接收范围
    
    RETURN delayed(message, total_delay)

AIS_MAX_RANGE = 40 × 1852    # 约 40 海里
```

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 目标 GPS 噪声 | ±5 m | 目标船的 GPS 精度 |
| SOG 噪声 | ±0.2 节 | |
| COG 噪声 | ±0.5° | |
| 最大接收距离 | 40 nm | VHF 视距 |
| TDMA 时隙延迟 | 0~26.67 ms | |
| 解码延迟 | 50 ms | |
| 无 AIS 的目标比例 | 按场景配置 | 渔船约 50% |

---

## 13. 雷达仿真器

### 13.1 ARPA 目标仿真（模式 A）

```
FUNCTION simulate_radar_arpa(own_ship, all_targets, environment, radar_config):
    
    arpa_targets = []
    
    FOR EACH target IN all_targets:
        
        # 步骤一：纯几何计算——距离和方位
        dx = target.x - own_ship.x
        dy = target.y - own_ship.y
        true_range = sqrt(dx² + dy²)
        true_bearing = (atan2(dx, dy) × 180/π) % 360
        
        # 超出量程
        IF true_range > radar_config.max_range:
            CONTINUE
        
        # 近距离盲区
        IF true_range < radar_config.min_range:
            CONTINUE
        
        # 雷达阴影区（船体结构遮挡）
        relative_bearing = (true_bearing - own_ship.heading × 180/π) % 360
        IF is_in_shadow_sector(relative_bearing, radar_config.shadow_sectors):
            CONTINUE
        
        # 步骤二：检测概率（基于简化雷达方程）
        rcs_linear = 10 ** (target.rcs_dbsm / 10)
        
        # 接收功率 ∝ RCS / R⁴
        snr = radar_config.system_constant × rcs_linear / (true_range ** 4)
        
        # 降水衰减
        IF environment.precipitation == "moderate_rain":
            snr *= 0.6
        ELIF environment.precipitation == "heavy_rain":
            snr *= 0.3
        
        # 检测概率（Swerling I 模型简化）
        P_detect = 1 - exp(-snr / radar_config.detection_threshold)
        P_detect = clip(P_detect, 0, 1)
        
        IF random.random() > P_detect:
            CONTINUE    # 未检测到
        
        # 步骤三：测量噪声
        measured_range = true_range + random.gauss(0, radar_config.range_noise)
        measured_bearing = true_bearing + random.gauss(0, radar_config.bearing_noise)
        
        # 距离量化（雷达有距离分辨率）
        measured_range = round(measured_range / radar_config.range_resolution) × radar_config.range_resolution
        
        # 方位量化（雷达有方位分辨率 ≈ 波束宽度）
        measured_bearing = round(measured_bearing / radar_config.beam_width) × radar_config.beam_width
        
        # ARPA 跟踪的速度和航向（从多扫描推导，有延迟和额外噪声）
        IF target.mmsi IN arpa_track_history:
            measured_speed = target.speed + random.gauss(0, 0.5)
            measured_course = target.course + random.gauss(0, 3.0)
        ELSE:
            measured_speed = NaN    # 新目标尚未建立跟踪
            measured_course = NaN
        arpa_track_history[target.id] = (measured_range, measured_bearing, sim_time)
        
        arpa_targets.append(RadarTarget(
            range_m=max(0, measured_range),
            bearing_deg=measured_bearing % 360,
            speed_mps=measured_speed,
            course_deg=measured_course,
            rcs_dbsm=target.rcs_dbsm
        ))
    
    # 步骤四：生成海杂波（假目标）
    IF environment.sea_state >= 2:
        clutter_count = environment.sea_state × radar_config.clutter_density
        clutter_max_range = SEA_CLUTTER_RANGE[min(environment.sea_state, 5)]
        
        FOR i IN range(clutter_count):
            arpa_targets.append(RadarTarget(
                range_m=random.uniform(radar_config.min_range, clutter_max_range),
                bearing_deg=random.uniform(0, 360),
                speed_mps=random.uniform(-0.5, 0.5),
                course_deg=random.uniform(0, 360),
                rcs_dbsm=random.uniform(-15, 0),
                is_simulated_clutter=True    # 仿真内部标记，不传递给 MASS_ADAS
            ))
    
    RETURN arpa_targets
```

### 13.2 海杂波范围

```
SEA_CLUTTER_RANGE = {
    0: 0,       # 海况 0：无杂波
    1: 500,     # 海况 1：500m
    2: 1000,    # 海况 2：1km
    3: 2000,    # 海况 3：2km
    4: 3000,    # 海况 4：3km
    5: 5000     # 海况 5：5km
}
```

### 13.3 雷达参数

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 最大量程 | 24 nm（44.4 km） | X-band 海事雷达 |
| 最小量程 | 30 m | 近距离盲区 |
| 距离分辨率 | 10 m | |
| 波束宽度 | 1.0° | |
| 距离噪声 | ±20 m | |
| 方位噪声 | ±1.0° | |
| 扫描周期 | 2.5 s | |
| 杂波密度 | 5 个/海况等级 | |
| 降水衰减（中雨） | 0.6 | |
| 降水衰减（大雨） | 0.3 | |

---

## 14. LiDAR 仿真器

### 14.1 射线投射原理

LiDAR 仿真的核心是射线-包围盒相交测试（Ray-AABB Intersection）。仿真引擎知道每个目标的 3D 包围盒——LiDAR 的每一条扫描线做一次相交测试就知道是否碰到目标以及碰到的距离。

```
FUNCTION simulate_lidar(own_ship, all_targets, wave_model, lidar_config, sim_time):
    
    point_cloud = []
    
    # LiDAR 安装位置（船体坐标→世界坐标）
    lidar_pos = body_to_world(lidar_config.mount, own_ship)
    
    FOR EACH v_angle IN lidar_config.vertical_angles:    # 16 线
        FOR h_angle_step IN range(0, 3600, lidar_config.h_resolution_x10):    # 0.2° 步进
            
            h_angle = h_angle_step / 10.0    # 度
            
            # 射线方向（世界坐标系）
            beam_heading = (own_ship.heading_deg + h_angle) % 360
            dx = cos(radians(v_angle)) × sin(radians(beam_heading))
            dy = cos(radians(v_angle)) × cos(radians(beam_heading))
            dz = sin(radians(v_angle))
            direction = (dx, dy, dz)
            
            # 对所有目标做射线-包围盒相交测试
            nearest_dist = lidar_config.max_range
            nearest_hit = None
            
            FOR EACH target IN all_targets:
                bbox = build_target_3d_bbox(target)
                hit, dist = ray_aabb_intersection(lidar_pos, direction, bbox)
                
                IF hit AND dist < nearest_dist AND dist > lidar_config.min_range:
                    nearest_dist = dist
                    nearest_hit = target
            
            # 水面反射检测
            water_dist = ray_water_intersection(lidar_pos, direction, wave_model, sim_time)
            IF water_dist AND water_dist < nearest_dist:
                nearest_dist = water_dist
                nearest_hit = "water"
            
            IF nearest_hit IS None:
                CONTINUE    # 射线未命中任何物体
            
            # 反射率和检测概率
            IF nearest_hit == "water":
                reflectivity = 0.1    # 水面反射弱且不稳定
                P_detect = 0.3 × (1 - nearest_dist / lidar_config.max_range)
            ELSE:
                reflectivity = nearest_hit.surface_reflectivity    # 0.3~0.9
                P_detect = reflectivity × (1 - (nearest_dist / lidar_config.max_range) ** 2)
            
            IF random.random() > P_detect:
                CONTINUE
            
            # 距离噪声
            measured_dist = nearest_dist + random.gauss(0, lidar_config.range_noise)
            
            # 转为 LiDAR 本体坐标系的点
            px = measured_dist × cos(radians(v_angle)) × sin(radians(h_angle))
            py = measured_dist × cos(radians(v_angle)) × cos(radians(h_angle))
            pz = measured_dist × sin(radians(v_angle))
            
            point_cloud.append(LidarPoint(
                x=px, y=py, z=pz,
                intensity=reflectivity,
                ring=v_angle_index
            ))
    
    # 转为 ROS2 PointCloud2 消息
    RETURN to_pointcloud2(point_cloud, frame_id="lidar_link")
```

### 14.2 射线-AABB 相交算法

```
FUNCTION ray_aabb_intersection(origin, direction, box_min, box_max):
    
    # Slab method——计算射线进出包围盒的参数 t
    inv_dir = 1.0 / direction    # 各分量取倒数（方向为零时需处理）
    
    t1 = (box_min - origin) × inv_dir
    t2 = (box_max - origin) × inv_dir
    
    t_near = min(t1, t2)    # 各分量取小值
    t_far = max(t1, t2)     # 各分量取大值
    
    t_enter = max(t_near.x, t_near.y, t_near.z)    # 三个面中最后进入的
    t_exit = min(t_far.x, t_far.y, t_far.z)         # 三个面中最先离开的
    
    IF t_enter > t_exit OR t_exit < 0:
        RETURN (False, None)    # 未相交
    
    RETURN (True, max(t_enter, 0))    # 返回碰撞距离
```

### 14.3 目标 3D 包围盒构建

```
FUNCTION build_target_3d_bbox(target):
    
    # 交通仿真引擎已知目标的尺寸和姿态
    half_l = target.length / 2
    half_w = target.beam / 2
    height = target.height_above_water
    
    # 8 个角点（船体坐标系）
    corners_local = [
        (-half_l, -half_w, 0), (half_l, -half_w, 0),
        (half_l, half_w, 0), (-half_l, half_w, 0),
        (-half_l, -half_w, height), (half_l, -half_w, height),
        (half_l, half_w, height), (-half_l, half_w, height)
    ]
    
    # 旋转（按目标航向）+ 平移（到目标位置）
    corners_world = [rotate_translate(c, target.heading, target.x, target.y) 
                     for c in corners_local]
    
    # 轴对齐包围盒（AABB）
    box_min = (min(c[0] for c in corners_world),
               min(c[1] for c in corners_world),
               min(c[2] for c in corners_world))
    box_max = (max(c[0] for c in corners_world),
               max(c[1] for c in corners_world),
               max(c[2] for c in corners_world))
    
    RETURN (box_min, box_max)
```

### 14.4 水面反射

```
FUNCTION ray_water_intersection(origin, direction, wave_model, t):
    
    IF direction[2] >= 0:
        RETURN None    # 射线朝上，不会碰到水面
    
    # 与 z=0 平面的交点
    t_hit = -origin[2] / direction[2]
    hit_x = origin[0] + direction[0] × t_hit
    hit_y = origin[1] + direction[1] × t_hit
    
    # 查询该点的波浪高度
    wave_z = wave_model.get_wave_elevation(hit_x, hit_y, t)
    
    # 精确交点（考虑波浪起伏）
    t_corrected = -(origin[2] - wave_z) / direction[2]
    
    IF t_corrected > 0 AND t_corrected < LIDAR_MAX_RANGE:
        RETURN t_corrected
    
    RETURN None
```

### 14.5 LiDAR 参数

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 最大量程 | 200 m | 海事 LiDAR |
| 最小量程 | 0.5 m | |
| 垂直通道数 | 16 | |
| 垂直角度范围 | -15° ~ +15° | |
| 水平分辨率 | 0.2° | |
| 距离噪声 | ±0.02 m | 2cm |
| 水面反射检测概率 | 0.3 | |
| 扫描频率 | 10~20 Hz | |

---

## 15. 摄像头仿真器

### 15.1 简化模式（日常开发用）

```
FUNCTION simulate_camera_simplified(own_ship, targets, environment, camera_config):
    
    detections = []
    
    FOR EACH target IN targets:
        
        # 步骤一：几何计算
        bearing = compute_bearing(own_ship, target)
        relative_bearing = (bearing - own_ship.heading_deg) % 360
        distance = compute_distance(own_ship, target)
        
        # 视场角检查
        half_fov = camera_config.fov_horizontal / 2
        IF relative_bearing > half_fov AND relative_bearing < (360 - half_fov):
            CONTINUE
        
        # 能见度限制
        IF distance / 1852 > environment.visibility_nm:
            CONTINUE
        
        # 距离限制
        IF distance > camera_config.max_detection_range:
            CONTINUE
        
        # 步骤二：检测概率
        # 近距离高，远距离低
        P_base = clip(1.0 - (distance / camera_config.max_detection_range) ** 2, 0, 1)
        
        # 目标越大越容易检测
        angular_size = target.length / distance    # 弧度
        IF angular_size < 0.001:    # < 0.06° ≈ 几个像素
            P_base *= 0.3
        
        # 夜间大幅降低（除非有灯光）
        IF environment.is_night:
            IF target.has_lights:
                P_base *= 0.6
            ELSE:
                P_base *= 0.05    # 夜间无灯目标几乎不可见
        
        # 雾天降低
        IF environment.visibility_nm < 2:
            P_base *= environment.visibility_nm / 2
        
        IF random.random() > P_base:
            CONTINUE
        
        # 步骤三：生成检测框
        pixel_x = bearing_to_pixel_x(relative_bearing, camera_config)
        pixel_width = target.length / distance × camera_config.focal_length_px
        pixel_height = target.height / distance × camera_config.focal_length_px
        
        center_y = camera_config.horizon_pixel_y    # 目标大致在地平线附近
        
        bbox = BoundingBox(
            x1=pixel_x - pixel_width / 2,
            y1=center_y - pixel_height,
            x2=pixel_x + pixel_width / 2,
            y2=center_y
        )
        
        # 分类结果（直接从交通引擎的目标类型映射）
        classification = map_ship_type_to_class(target.ship_type)
        confidence = P_base × 0.9 + random.gauss(0, 0.05)
        
        detections.append(CameraDetection(
            bbox=bbox,
            classification=classification,
            confidence=clip(confidence, 0.3, 0.99),
            bearing_deg=bearing
        ))
    
    RETURN detections
```

### 15.2 渲染模式（AI 训练用）

```
FUNCTION simulate_camera_rendered(own_ship, targets, environment, camera_config, render_engine):
    
    # 使用 Unity/Unreal/Open3D 渲染真正的图像
    # 步骤一：设置场景
    render_engine.set_camera_pose(
        position=body_to_world(camera_config.mount, own_ship),
        orientation=own_ship.heading + camera_config.mount.heading_offset,
        fov=camera_config.fov_horizontal
    )
    
    # 步骤二：放置目标船 3D 模型
    FOR EACH target IN targets:
        render_engine.place_ship_model(
            model=target.ship_type,
            position=(target.x, target.y, 0),
            heading=target.heading,
            length=target.length
        )
    
    # 步骤三：设置环境
    render_engine.set_ocean(
        wave_height=environment.wave_Hs,
        wave_period=environment.wave_Tp,
        wave_direction=environment.wave_dir
    )
    render_engine.set_weather(
        visibility=environment.visibility_nm,
        rain=environment.precipitation,
        time_of_day=environment.time_of_day
    )
    
    # 步骤四：渲染
    image = render_engine.render()    # 返回 RGB 像素矩阵
    
    # 步骤五：输出 ROS2 Image 消息
    RETURN to_ros_image(image, camera_config.encoding)
```

### 15.3 两种模式的注入点差异

```
简化模式：
  仿真器 → 检测框列表 → 注入到 Multi-Sensor Fusion 输入端
  （跳过 Data Preprocessing 和 Object Detection）
  适用于测试 L3~L5 的决策和控制逻辑

渲染模式：
  仿真器 → 像素图像 → 注入到 Data Preprocessing 输入端
  → Object Detection (YOLOv8) 处理
  适用于测试完整的感知管线和训练 AI 模型
```

---

## 16. 传感器故障注入

### 16.1 故障类型

```
FUNCTION inject_sensor_fault(sensor_output, fault_config, sim_time):
    
    IF NOT fault_config.active:
        RETURN sensor_output    # 无故障
    
    # 检查故障时间窗口
    IF sim_time < fault_config.start_time OR sim_time > fault_config.end_time:
        RETURN sensor_output
    
    SWITCH fault_config.fault_type:
        
        CASE "data_loss":
            # 传感器完全停止输出
            RETURN None
        
        CASE "data_freeze":
            # 传感器输出冻结——持续输出最后一个有效值
            IF NOT hasattr(fault_config, "frozen_value"):
                fault_config.frozen_value = sensor_output
            RETURN fault_config.frozen_value
        
        CASE "noise_increase":
            # 噪声增大（传感器老化或环境干扰）
            original_noise = sensor_output.noise_component
            sensor_output.value += original_noise × fault_config.noise_multiplier
            RETURN sensor_output
        
        CASE "bias_drift":
            # 渐变偏移（传感器校准失效）
            elapsed = sim_time - fault_config.start_time
            drift = fault_config.drift_rate × elapsed
            sensor_output.value += drift
            RETURN sensor_output
        
        CASE "intermittent":
            # 间歇性失效——随机丢包
            IF random.random() < fault_config.dropout_rate:
                RETURN None
            RETURN sensor_output
        
        CASE "gps_spoofing":
            # GPS 欺骗——位置被人为偏移
            sensor_output.latitude += fault_config.spoof_offset_lat
            sensor_output.longitude += fault_config.spoof_offset_lon
            RETURN sensor_output
        
        CASE "ais_spoofing":
            # AIS 欺骗——插入虚假 AIS 目标
            fake_targets = generate_fake_ais_targets(fault_config.fake_count)
            sensor_output.extend(fake_targets)
            RETURN sensor_output
```

### 16.2 故障场景配置

```yaml
# scenario_emr_001_sensor_cascade.yaml
faults:
  - sensor: "radar"
    fault_type: "data_loss"
    start_time: 60
    end_time: 300
    
  - sensor: "gps"
    fault_type: "noise_increase"
    start_time: 120
    end_time: 240
    noise_multiplier: 5.0    # 噪声增大 5 倍
    
  - sensor: "camera_front"
    fault_type: "data_loss"
    start_time: 180
    end_time: 300
    
  - sensor: "gps"
    fault_type: "gps_spoofing"
    start_time: 300
    end_time: 420
    spoof_offset_lat: 0.001    # 约 111m 偏移
    spoof_offset_lon: 0.001
```

---

## 17. 传感器噪声模型校准方法

### 17.1 从真实数据校准

```
# 在真实船舶上采集传感器数据，与高精度参考（如 RTK GPS）对比
# 提取噪声统计特性

FUNCTION calibrate_sensor_noise(real_data, reference_data):
    
    errors = real_data - reference_data    # 真实传感器与参考值的差
    
    noise_model = {
        mean: mean(errors),            # 系统性偏差
        std: std(errors),              # 噪声标准差
        distribution: fit_distribution(errors),    # 分布形状（高斯/拉普拉斯/...）
        autocorrelation: compute_acf(errors),      # 时间相关性
        outlier_rate: count(|errors| > 3σ) / len(errors)    # 异常值比率
    }
    
    RETURN noise_model
```

### 17.2 无真实数据时的默认值

本文档中各传感器仿真器使用的噪声参数是基于传感器厂商规格书和海事行业文献的经验默认值。首次海试后应使用实测数据校准。

---

## 18. 与仿真引擎其他子系统的时序关系

```
仿真主循环（每步 dt = 0.02s）：

1. 时钟步进：sim_time += dt
2. 环境更新：wind_model.update(), wave_model.update(), current_model.update()
3. 交通更新：FOR EACH target: target.behavior.step(dt)
4. MASS_ADAS 上一步输出的控制指令读取：RPM_cmd, rudder_cmd, bow_cmd
5. 执行机构响应模型更新：actual_rudder, actual_rpm, actual_thrust
6. 推进力计算：propulsion_forces = compute(actual_rudder, actual_rpm, ...)
7. 环境力计算：env_forces = compute(wind, wave, current, own_ship)
8. 水动力学积分：own_ship.state = integrate(propulsion + env_forces, dt)
   
9. ★ 传感器仿真引擎调用 ★
   9a. simulate_gps(own_ship.state)           → 发布 /sensors/gps/nmea
   9b. simulate_compass(own_ship.heading)      → 发布 /sensors/compass/nmea
   9c. simulate_imu(own_ship.state)            → 发布 /sensors/imu/data
   9d. simulate_speed_log(own_ship.u, own_ship.v) → 发布 /sensors/speed_log/nmea
   9e. simulate_echo_sounder(own_ship.pos)     → 发布 /sensors/echo_sounder/nmea
   9f. simulate_anemometer(wind, own_ship)     → 发布 /sensors/wind/nmea
   9g. simulate_rudder_feedback(actual_rudder) → 发布 /sensors/rudder/feedback
   9h. simulate_rpm_feedback(actual_rpm)       → 发布 /sensors/engine/rpm
   9i. simulate_ais(target_ships, sim_time)    → 发布 /sensors/ais/nmea
   9j. simulate_radar(own_ship, targets, env)  → 发布 /sensors/radar/arpa_targets
   9k. simulate_lidar(own_ship, targets, wave) → 发布 /sensors/lidar/points
   9l. simulate_camera(own_ship, targets, env) → 发布 /sensors/camera_X/image

10. ROS2 spin —— MASS_ADAS 的 Data Preprocessing 接收到以上传感器数据
    → 完整感知管线 → L3 决策 → L4 引导 → L5 控制 → 输出新的控制指令
    → 回到步骤 4（下一个仿真步）
```

---

## 19. 与 MASS_ADAS 生产代码的接口协议

**接口原则：** 话题名称和消息类型完全一致。唯一的区别是 ROS2 的 `use_sim_time` 参数。

```bash
# 仿真模式启动
ros2 launch mass_adas bringup.launch.py use_sim_time:=true

# 真实模式启动
ros2 launch mass_adas bringup.launch.py use_sim_time:=false

# MASS_ADAS 的代码中没有任何 "if sim_time then..." 的分支
# Data Preprocessing 用完全相同的代码处理仿真数据和真实数据
```

---

## 20. 内部子模块分解

| 子模块 | 职责 | 建议语言 | 频率 |
|-------|------|---------|------|
| GPS Simulator | GPS NMEA 生成（含跳变模拟） | Python/C++ | 1~10 Hz |
| Compass Simulator | 罗经 NMEA 生成（含延迟模型） | Python/C++ | 10~50 Hz |
| IMU Simulator | IMU 消息生成（含零偏漂移） | C++ | 100~200 Hz |
| Speed Log Simulator | 计程仪 NMEA 生成 | Python | 1~5 Hz |
| Echo Sounder Simulator | 测深仪 NMEA 生成（查海图） | Python | 1~5 Hz |
| Anemometer Simulator | 风仪 NMEA 生成（真风→视风变换） | Python | 1 Hz |
| Rudder Feedback Sim | 舵角反馈生成 | C++ | 50 Hz |
| RPM Feedback Sim | 转速反馈生成 | C++ | 10 Hz |
| AIS Simulator | AIS NMEA 生成（6-bit 编码 + 报告间隔） | Python/C++ | 按 IMO 间隔 |
| Radar Simulator | ARPA 目标生成（检测概率 + 海杂波） | C++ | 2~4 Hz |
| LiDAR Simulator | 点云生成（射线投射 + 水面反射） | C++ | 10~20 Hz |
| Camera Simulator | 检测框生成（简化）或图像渲染（Unity） | Python / Unity | 10~30 Hz |
| Fault Injector | 传感器故障注入 | Python | 按场景配置 |
| NMEA Encoder | NMEA 0183 校验和和格式化 | C++ | 通用 |

---

## 21. 参数总览表

| 传感器 | 关键噪声参数 | 默认值 | 输出频率 |
|-------|-----------|-------|---------|
| GPS | 位置 σ | 2.0 m | 1~10 Hz |
| | 跳变概率 | 0.1% | |
| 罗经 | 航向 σ | 0.5° | 10~50 Hz |
| IMU | 陀螺仪 σ | 0.01 °/s/√Hz | 100~200 Hz |
| | 加速度计 σ | 0.002 m/s²/√Hz | |
| 计程仪 | 速度 σ | 0.05 m/s | 1~5 Hz |
| 测深仪 | 深度 σ | ±0.1m (浅) / ±1% (深) | 1~5 Hz |
| 风仪 | 风速 σ | 0.5 m/s | 1 Hz |
| | 风向 σ | 3° | |
| 舵角反馈 | 角度 σ | 0.3° | 50 Hz |
| RPM 反馈 | 转速 σ | 5 RPM | 10 Hz |
| AIS | 位置 σ | 5.0 m | 按 IMO 间隔 |
| | SOG σ | 0.2 节 | |
| 雷达 | 距离 σ | 20 m | 2~4 Hz |
| | 方位 σ | 1.0° | |
| LiDAR | 距离 σ | 0.02 m | 10~20 Hz |
| 摄像头 | 检测概率 | 距离相关 | 10~30 Hz |

---

## 22. 测试策略与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| SSE-001 | GPS 仿真 — 噪声统计 | 位置误差分布与指定 σ 一致（χ² 检验 p > 0.05） |
| SSE-002 | GPS 仿真 — 跳变注入 | Data Preprocessing 的跳变检测算法能识别 |
| SSE-003 | AIS 仿真 — 报告间隔 | 符合 IMO 规定的间隔（±10%） |
| SSE-004 | AIS 仿真 — NMEA 格式 | Data Preprocessing 100% 成功解析 |
| SSE-005 | 雷达仿真 — 检测概率 | 远距离目标检测概率随距离四次方衰减 |
| SSE-006 | 雷达仿真 — 海杂波 | Object Detection 能区分真目标和杂波 |
| SSE-007 | LiDAR 仿真 — 点云密度 | 与真实 LiDAR 在相同距离的点密度比 0.8~1.2 |
| SSE-008 | LiDAR 仿真 — 水面反射 | Data Preprocessing 能过滤水面杂点 |
| SSE-009 | 风仪仿真 — 视风变换 | Data Preprocessing 反算真风误差 < 1 m/s |
| SSE-010 | 全传感器闭环 | SIL 运行 10 分钟稳定无发散 |
| SSE-011 | 故障注入 — GPS 丢失 | MASS_ADAS 正确切换到 DR 模式 |
| SSE-012 | 故障注入 — 雷达故障 | Risk Monitor 正确报告传感器降级 |
| SSE-013 | 故障注入 — GPS 欺骗 | Data Preprocessing 的交叉验证能检测 |
| SSE-014 | 多传感器时间同步 | 所有传感器数据时间戳偏差 < 10ms |
| SSE-015 | 100 目标性能 | 100 个目标时全部传感器仿真 < 10ms/步 |

---

## 23. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | IEC 61162-1 | NMEA 0183 语句格式标准 |
| [2] | ITU-R M.1371-5 | AIS 消息编码规范 |
| [3] | IEC 62388 | 海事雷达性能标准（检测概率要求） |
| [4] | Skolnik, M. "Introduction to Radar Systems" | 雷达方程和检测理论 |
| [5] | IEEE Std 1278 (DIS) | 分布式交互仿真标准 |
| [6] | ROS2 sensor_msgs | ROS2 标准传感器消息定义 |
| [7] | Hasselmann, K. "JONSWAP" 1973 | 波浪谱模型（水面反射仿真用） |
| [8] | Titterton & Weston "Strapdown Inertial Navigation Technology" | IMU 误差模型 |

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-26 | 架构组 | 初始版本 |

---

**文档结束**
