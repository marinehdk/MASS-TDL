# MASS_ADAS Navigation Filter 导航滤波器技术设计文档

**文档编号：** SANGO-ADAS-NAV-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 航海长的导航决策思维模型
5. 计算流程总览
6. 状态向量与运动模型定义
7. EKF 预测步
8. EKF 更新步——GPS 测量
9. EKF 更新步——罗经测量
10. EKF 更新步——计程仪测量
11. EKF 更新步——IMU 辅助
12. 多定位源融合策略
13. 传感器冗余投票机制
14. 定位源质量监控（四重检测）
15. GPS 丢失时的航位推算模式
16. GPS 恢复时的平滑切换
17. GPS 欺骗检测
18. 输出——本船状态单一真值源
19. 自适应过程噪声调节
20. 浅水效应对模型的影响
21. 内部子模块分解
22. 数值算例
23. 参数总览表
24. 与其他模块的协作关系
25. 测试策略与验收标准
26. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Navigation Filter（导航滤波器）是 MASS_ADAS v3.0 架构中新增的核心模块，位于 Perception 子系统的输出端。它接收 Data Preprocessing 处理后的各导航传感器数据（GPS、罗经、IMU、计程仪、风速仪），通过扩展卡尔曼滤波器（EKF）融合为一个最优的本船状态估计。

在 v2.0 架构中，本船位置直接取 GPS 输出（经 Data Preprocessing 的基本噪声滤波），航向取罗经输出。这意味着 GPS 的跳变会直接传导到 L4 Guidance 引起舵角突变，GPS 丢失时系统立即失去位置信息。Navigation Filter 的引入解决了这两个关键问题。

从 v3.0 开始，MASS_ADAS 所有层级（L3~L5）消费的本船状态统一来自 Navigation Filter 的输出——它是**本船状态的唯一真值源（Single Source of Truth）**。

### 1.2 核心职责

- **多源融合：** 将 GPS、罗经、IMU、计程仪的数据最优融合，输出精度优于任何单一传感器的本船状态估计。
- **噪声滤波：** 滤除传感器噪声，特别是 GPS 跳变和罗经高频抖动。EKF 的状态预测提供了"物理合理的期望"，突然偏离期望的传感器读数被自动抑制。
- **航位推算（Dead Reckoning）：** GPS 丢失时，利用 IMU 和计程仪数据维持位置估计。
- **定位源质量监控：** 对每个定位源做实时质量检测（冻结、方差、预测、漂移），自动排除质量变差的传感器。
- **传感器冗余管理：** 多套同类传感器（如 3 套罗经）的投票或差值检测。

### 1.3 设计原则

- **物理模型约束原则：** EKF 使用船舶运动模型作为预测基础——船不可能瞬间跳变 100 米。这个物理约束自然地抑制了 GPS 跳变。
- **自适应权重原则：** 不同传感器在不同条件下精度不同。EKF 的卡尔曼增益自动调整各传感器权重。
- **最低依赖原则：** 即使只有 IMU 和计程仪工作，系统也能维持航位推算。
- **不信任任何单一传感器原则：** 每个传感器的数据在进入 EKF 前经过独立质量检查。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 频率 | 数据 |
|-------|------|------|------|
| GPS 位置/速度 | Data Preprocessing | 1~10 Hz | 纬度、经度、SOG、COG、HDOP |
| 罗经航向 | Data Preprocessing | 10~50 Hz | 真航向（度） |
| IMU 数据 | Data Preprocessing | 100~200 Hz | 三轴角速度、加速度、姿态角 |
| 计程仪速度 | Data Preprocessing | 1~5 Hz | 纵/横向对水速度 |
| 传感器冗余数据 | Data Preprocessing | 各传感器频率 | GPS×2~3, 罗经×2~3 |

### 2.2 输出

通过 `/nav/filtered_state` 话题发布（50 Hz）：

```
FilteredOwnShipState:
    Header header
    float64 latitude, longitude         # WGS84 滤波位置
    float64 sog, cog                    # 对地速度和航向
    float64 u, v                        # 对水速度
    float64 heading, yaw_rate           # 航向、角速度
    float64 roll, pitch                 # 姿态
    float64 position_uncertainty_m      # 位置不确定性（1σ）
    float64 heading_uncertainty_deg     # 航向不确定性
    string nav_mode                     # OPTIMAL/DR_SHORT/DR_LONG/DEGRADED
    string[] active_sensors             # 参与融合的传感器
    string[] rejected_sensors           # 被拒绝的传感器
    float64 dr_elapsed_s                # DR 已持续时间
    float64 current_speed, current_direction  # 海流估计（副产品）
```

---

## 3. 核心参数数据库

### 3.1 EKF 模型参数

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 状态维度 | 15 | 见第 6 节 |
| EKF 预测频率 | 50 Hz | |
| 位置过程噪声 σ_q_pos | 0.1 m/s² | |
| 速度过程噪声 σ_q_vel | 0.3 m/s² | |
| 航向过程噪声 σ_q_hdg | 0.5 °/s | |
| 海流过程噪声 σ_q_current | 0.01 m/s² | |

### 3.2 传感器测量噪声

| 传感器 | 参数 | 默认值 |
|-------|------|-------|
| GPS 位置 | σ_gps | 2.0 m (标准) / 0.5 m (DGPS) / 0.02 m (RTK) |
| GPS 速度 | σ_gps_vel | 0.1 m/s |
| 罗经 | σ_compass | 0.5° |
| IMU 角速度 | σ_gyro | 0.01 °/s |
| 计程仪 | σ_log | 0.05 m/s |

### 3.3 质量监控参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 冻结检测窗口 | 10 次 | 连续 10 次相同 → 冻结 |
| 方差比阈值 | 3.0 | 实际/预期 > 3 → 异常 |
| 预测门控 | 99.9% χ² | 马氏距离检验 |
| 漂移 CUSUM 阈值 | 5.0 | |
| 恢复等待 | 30 秒 | |

---

## 4. 航海长的导航决策思维模型

航海长在驾驶台上持续维护"心中的船位"——不完全依赖 GPS 显示的数字，而是综合多种信息。GPS 告诉他经纬度但他知道有时不准；罗经告诉他航向通常很准但偶尔也有问题；计程仪告诉他对水速度，与 GPS SOG 的差就是海流。

他的"心中定位"本质上就是卡尔曼滤波：有一个基于船速和航向的"预期位置"（预测步），然后用 GPS 读数来修正（更新步）。如果 GPS 突然跳了 50 米但航向和速度没变，他不会相信 GPS 跳变——他的预期位置只移动了正常距离。

GPS 丢失时他用航位推算："最后可靠的 GPS 位置是 10 分钟前。从那以后以 8 节航向 045° 航行，海流约 1 节向东。所以现在应该在东北方约 1.3 海里处。"

---

## 5. 计算流程总览

```
各导航传感器 → 冗余投票 → 质量监控 → EKF 预测 → EKF 更新 → 输出 FilteredOwnShipState
```

---

## 6. 状态向量与运动模型定义

### 6.1 15 维状态向量

```
x = [x_n, x_e, ψ, u, v, r, V_cn, V_ce, b_ψ, b_u, b_v, φ, θ, p, q]ᵀ

x_n, x_e  = 北向/东向位置（米，ENU）
ψ         = 真航向（弧度）
u, v      = 纵/横向对水速度（m/s，船体系）
r         = 艏摇角速度（rad/s）
V_cn, V_ce= 海流北向/东向分量（m/s）
b_ψ       = 罗经零偏（弧度）
b_u, b_v  = 计程仪零偏（m/s）
φ, θ      = 横摇/纵摇角（弧度）
p, q      = 横摇/纵摇角速度（rad/s）
```

### 6.2 运动模型

```
FUNCTION predict_state(x, dt, imu_data):
    
    # 位置更新（对地速度 = 对水速度旋转到地理系 + 海流）
    u_gnd_n = x.u * cos(x.ψ) - x.v * sin(x.ψ) + x.V_cn
    u_gnd_e = x.u * sin(x.ψ) + x.v * cos(x.ψ) + x.V_ce
    x.x_n += u_gnd_n * dt
    x.x_e += u_gnd_e * dt
    
    # 航向更新
    x.ψ += x.r * dt
    x.ψ = normalize_0_2pi(x.ψ)
    
    # 速度更新（IMU 辅助）
    IF imu_data IS NOT NULL:
        x.u += (imu_data.accel_x - g * sin(x.θ)) * dt
        x.v += (imu_data.accel_y + g * sin(x.φ) * cos(x.θ)) * dt
        x.r = imu_data.gyro_z
        x.p = imu_data.gyro_x
        x.q = imu_data.gyro_y
    
    # 海流和偏差：假设缓慢变化（过程噪声允许漂移）
    # 姿态更新
    x.φ += x.p * dt
    x.θ += x.q * dt
    
    RETURN x
```

---

## 7. EKF 预测步

```
FUNCTION ekf_predict(state, P, Q, dt, imu_data):
    state_predicted = predict_state(state, dt, imu_data)
    F = compute_jacobian(state, dt)    # 15×15 状态转移雅可比
    P_predicted = F * P * Fᵀ + Q
    RETURN state_predicted, P_predicted
```

---

## 8. EKF 更新步——GPS 测量

```
FUNCTION ekf_update_gps(state, P, gps):
    
    z = [gps.x_n, gps.x_e, gps.v_n, gps.v_e]    # GPS 测量
    
    h = [state.x_n, state.x_e,
         state.u*cos(state.ψ) - state.v*sin(state.ψ) + state.V_cn,
         state.u*sin(state.ψ) + state.v*cos(state.ψ) + state.V_ce]
    
    H = jacobian_of_h(state)    # 4×15
    σ_pos = gps.hdop * UERE
    R = diag([σ_pos², σ_pos², σ_gps_vel², σ_gps_vel²])
    
    y = z - h    # 创新
    S = H * P * Hᵀ + R
    
    # 创新门控（马氏距离）
    mahalanobis = yᵀ * S⁻¹ * y
    IF mahalanobis > CHI2_4DOF:    # 18.47 (99.9%, 4df)
        log_event("gps_rejected", {mahalanobis})
        RETURN state, P    # 拒绝此测量
    
    K = P * Hᵀ * S⁻¹
    state = state + K * y
    P = (I - K * H) * P
    RETURN state, P
```

---

## 9. EKF 更新步——罗经测量

```
FUNCTION ekf_update_compass(state, P, compass_heading_rad):
    
    z = [compass_heading_rad]
    h = [state.ψ + state.b_ψ]    # 观测 = 真航向 + 零偏
    
    H = zeros(1, 15)
    H[0][2] = 1; H[0][8] = 1
    R = [σ_compass²]
    
    y = [normalize_pm_pi(z[0] - h[0])]    # 环形角度处理
    
    S = H * P * Hᵀ + R
    IF y[0]² / S[0][0] > CHI2_1DOF:    # 10.83
        RETURN state, P
    
    K = P * Hᵀ * S⁻¹
    state = state + K * y
    state.ψ = normalize_0_2pi(state.ψ)
    P = (I - K * H) * P
    RETURN state, P
```

---

## 10. EKF 更新步——计程仪测量

```
FUNCTION ekf_update_speed_log(state, P, stw_long, stw_trans):
    
    z = [stw_long, stw_trans]
    h = [state.u + state.b_u, state.v + state.b_v]
    
    H = zeros(2, 15)
    H[0][3] = 1; H[0][9] = 1    # u + b_u
    H[1][4] = 1; H[1][10] = 1   # v + b_v
    R = diag([σ_log², σ_log²])
    
    y = z - h
    S = H * P * Hᵀ + R
    IF yᵀ * S⁻¹ * y > CHI2_2DOF:    # 13.82
        RETURN state, P
    
    K = P * Hᵀ * S⁻¹
    state = state + K * y
    P = (I - K * H) * P
    RETURN state, P
```

---

## 11. EKF 更新步——IMU 辅助

IMU 主要辅助**预测步**——100~200 Hz 的高频数据在 GPS 更新之间维持精确预测。姿态（roll/pitch）作为独立更新：

```
FUNCTION ekf_update_imu_attitude(state, P, imu_roll, imu_pitch):
    z = [imu_roll, imu_pitch]
    h = [state.φ, state.θ]
    H = zeros(2, 15); H[0][11] = 1; H[1][12] = 1
    R = diag([σ_imu_att², σ_imu_att²])
    
    y = z - h
    K = P * Hᵀ * (H * P * Hᵀ + R)⁻¹
    state = state + K * y
    P = (I - K * H) * P
    RETURN state, P
```

---

## 12. 多定位源融合策略

多种定位系统按精度排序，精度最低的先更新（顺序更新等价于联合更新但数值更稳定）：

```
FUNCTION fuse_position_sources(sources):
    sources.sort(by=noise_variance, descending=true)
    FOR EACH source IN sources:
        IF source.quality_status == "ACCEPTED":
            state, P = ekf_update_position(state, P, source.measurement, source.R)
    RETURN state, P
```

---

## 13. 传感器冗余投票机制

### 13.1 三余度投票

```
FUNCTION triple_sensor_vote(v1, v2, v3, threshold):
    median_val = median([v1, v2, v3])
    rejected = [i for i, v in enumerate([v1,v2,v3]) if abs(v - median_val) > threshold]
    IF len(rejected) == 0:
        RETURN median_val, "ALL_AGREE"
    ELIF len(rejected) == 1:
        valid = [v for i, v in enumerate([v1,v2,v3]) if i not in rejected]
        RETURN mean(valid), "ONE_REJECTED"
    ELSE:
        RETURN median_val, "MAJORITY_FAILED"
```

### 13.2 双余度差值检测

```
FUNCTION dual_sensor_diff(v1, v2, threshold):
    IF abs(v1 - v2) < threshold:
        RETURN mean([v1, v2]), "CONSISTENT"
    ELSE:
        log_event("dual_divergence", {diff: abs(v1-v2)})
        RETURN mean([v1, v2]), "DIVERGENT"
```

---

## 14. 定位源质量监控（四重检测）

### 14.1 冻结检测

```
FUNCTION freeze_test(sensor_id, value, history):
    history[sensor_id].append(value)
    IF len(history[sensor_id]) >= 10:
        IF all(v == history[sensor_id][0] for v in history[sensor_id][-10:]):
            RETURN "FROZEN"
    RETURN "OK"
```

### 14.2 方差检测

```
FUNCTION variance_test(innovations, expected_var):
    actual_var = var(innovations[-60s:])
    IF actual_var / expected_var > 3.0:
        RETURN "HIGH_VARIANCE"
    RETURN "OK"
```

### 14.3 预测检测

已在 EKF 更新步中通过创新门控（马氏距离）实现。

### 14.4 漂移检测（CUSUM）

```
FUNCTION drift_test(innovations):
    cusum_pos = max(0, cusum_pos + mean(innovations[-30s:]) - 0.5)
    cusum_neg = max(0, cusum_neg - mean(innovations[-30s:]) - 0.5)
    IF cusum_pos > 5.0 OR cusum_neg > 5.0:
        RETURN "DRIFTING"
    RETURN "OK"
```

### 14.5 综合判定

```
FUNCTION assess_quality(sensor_id, value, innovations, expected_var):
    tests = [freeze_test(...), variance_test(...), drift_test(...)]
    IF any(t != "OK" for t in tests):
        sensor_status[sensor_id] = "REJECTED"
        RETURN "REJECTED"
    # 被拒绝后需持续 30 秒正常才恢复
    IF sensor_status[sensor_id] == "REJECTED" AND stable_for(30s):
        sensor_status[sensor_id] = "ACCEPTED"
    RETURN sensor_status[sensor_id]
```

---

## 15. GPS 丢失时的航位推算模式

```
FUNCTION handle_gps_loss(state, P, dt):
    IF time_since_last_gps > 5.0:
        IF nav_mode != "DR_SHORT" AND nav_mode != "DR_LONG":
            nav_mode = "DR_SHORT"
            dr_start = now()
            notify_risk_monitor("GPS_LOST")
        
        dr_elapsed = now() - dr_start
        IF dr_elapsed > 300:    # 5 分钟
            nav_mode = "DR_LONG"
            notify_risk_monitor("DR_LONG", {elapsed: dr_elapsed})
        
        # DR 模式：EKF 只做预测步，无 GPS 更新步
        # 位置精度随时间漂移：
        #   30s: ~10m    3min: ~80m    10min: ~300m
        
        # 增大位置过程噪声
        Q[0][0] *= 3.0; Q[1][1] *= 3.0
    
    RETURN state, P
```

---

## 16. GPS 恢复时的平滑切换

```
FUNCTION handle_gps_recovery(state, P, gps):
    IF nav_mode.startswith("DR"):
        dr_gps_diff = distance(state.position, gps.position)
        
        IF dr_gps_diff < 50:    # 小偏差——直接更新
            state, P = ekf_update_gps(state, P, gps)
            nav_mode = "OPTIMAL"
        ELSE:    # 大偏差——渐进过渡
            R_inflated = R_gps * 10    # 膨胀 GPS 噪声
            state, P = ekf_update_gps_with_R(state, P, gps, R_inflated)
            # 后续逐步减小膨胀系数直到恢复正常
    RETURN state, P
```

---

## 17. GPS 欺骗检测

```
FUNCTION detect_gps_spoofing(gps, state, speed_log):
    score = 0
    
    # 位置偏离 EKF 预测
    IF distance(gps, state.predicted) > 100:
        score += 30
    
    # GPS 速度 vs 计程仪+海流
    IF abs(gps.sog - (speed_log.stw + current)) > 3.0:
        score += 25
    
    # GPS COG vs 罗经（高速时）
    IF state.u > 3 AND abs(gps.cog - state.heading) > 30:
        score += 25
    
    # 双 GPS 同源偏移
    IF gps_count >= 2 AND both_shifted_same_direction:
        score += 20
    
    IF score >= 50:
        nav_mode = "DR_SHORT"    # 不信任 GPS
        notify_risk_monitor("GPS_SPOOF_ALERT")
        RETURN "SPOOFING_SUSPECTED"
    RETURN "OK"
```

---

## 18. 输出——本船状态单一真值源

```
FUNCTION publish_filtered_state(state, P, nav_mode, sensor_status):
    msg = FilteredOwnShipState()
    msg.latitude, msg.longitude = enu_to_wgs84(state.x_n, state.x_e)
    msg.u = state.u; msg.v = state.v
    msg.sog = compute_sog(state)
    msg.cog = compute_cog(state)
    msg.heading = state.ψ * 180/π
    msg.yaw_rate = state.r * 180/π
    msg.roll = state.φ * 180/π; msg.pitch = state.θ * 180/π
    msg.position_uncertainty_m = sqrt(P[0][0] + P[1][1])
    msg.heading_uncertainty_deg = sqrt(P[2][2]) * 180/π
    msg.nav_mode = nav_mode
    msg.active_sensors = [s for s in status if status[s] == "ACCEPTED"]
    msg.rejected_sensors = [s for s in status if status[s] == "REJECTED"]
    msg.current_speed = sqrt(state.V_cn² + state.V_ce²)
    msg.current_direction = atan2(state.V_ce, state.V_cn) * 180/π
    filtered_state_publisher.publish(msg)
```

---

## 19. 自适应过程噪声调节

```
FUNCTION adapt_process_noise(Q, state, environment, nav_mode):
    # 机动时：增大航向和速度噪声
    IF abs(state.r) > 0.02:    # > 1°/s
        Q[2][2] *= 3.0; Q[3][3] *= 2.0; Q[4][4] *= 2.0
    
    # 大浪时：增大速度和姿态噪声
    IF environment.sea_state >= 4:
        Q[3][3] *= 2.0; Q[11][11] *= 3.0
    
    # DR 模式：增大位置噪声
    IF nav_mode.startswith("DR"):
        Q[0][0] *= 3.0; Q[1][1] *= 3.0
    
    RETURN Q
```

---

## 20. 浅水效应对模型的影响

```
FUNCTION adjust_shallow_water(Q, depth, draft):
    ukc = depth - draft
    IF ukc < draft * 1.5:
        factor = 1 + (draft * 1.5 - ukc) / (draft * 1.5)
        Q[3][3] *= (1 + factor)    # 纵向速度不确定性增大
        Q[4][4] *= (1 + factor)    # 横向速度不确定性增大
        Q[5][5] *= (1 + factor * 0.5)
    RETURN Q
```

---

## 21. 内部子模块分解

| 子模块 | 职责 | 建议语言 | 频率 |
|-------|------|---------|------|
| EKF Core | 15 状态 EKF 预测/更新 | C++ | 50 Hz |
| GPS Update | GPS 测量更新 + HDOP 自适应 | C++ | 1~10 Hz |
| Compass Update | 罗经更新 + 环形角度 | C++ | 10~50 Hz |
| Speed Log Update | 计程仪更新 + 零偏估计 | C++ | 1~5 Hz |
| IMU Integrator | IMU 辅助预测 + 姿态更新 | C++ | 100~200 Hz |
| Sensor Vote Manager | 三余度投票/双余度差值 | C++ | 各传感器 |
| Quality Monitor | 四重检测 | C++ | 1 Hz |
| DR Manager | GPS 丢失/恢复管理 | C++ | 1 Hz |
| GPS Spoof Detector | 欺骗检测 | C++ | 1 Hz |
| Adaptive Tuner | 自适应噪声+浅水修正 | C++ | 1 Hz |
| Output Publisher | 状态构建和发布 | C++ | 50 Hz |

---

## 22. 数值算例

### 算例一：GPS 跳变抑制

```
场景：船以 8 m/s、航向 045° 稳定航行。GPS 突然跳变 50m。

EKF 预测位置增量 = 8 × 0.1 = 0.8m（100ms 内）
GPS 测量偏移 = 50m
创新 = 50m

马氏距离 = 50² / (P_predicted + R_gps) = 50² / 5 = 500 >> 18.47 (χ² 阈值)
→ 测量被拒绝！EKF 保持预测位置。
→ 下一周期 GPS 恢复正常，正常更新。

结果：MASS_ADAS 感知到的位置只移动了 0.8m，而非 50m。
```

### 算例二：GPS 丢失 3 分钟

```
GPS 丢失时：位置 (31.2345°N, 121.4567°E), 速度 8 m/s, 航向 045°
海流估计：0.5 m/s, 090°

DR 180 秒后：
  北向 = (8*cos45° + 0) * 180 = 1019m
  东向 = (8*sin45° + 0.5) * 180 = 1109m
  
  位置不确定性 ≈ 0.3 * 180 * 1.5 = 81m

GPS 恢复时偏差约 20m < 50m → 平滑过渡。
```

---

## 23. 参数总览表

| 类别 | 参数 | 默认值 |
|------|------|-------|
| **EKF** | 状态维度 | 15 |
| | 预测频率 | 50 Hz |
| | 位置过程噪声 | 0.1 m/s² |
| | 速度过程噪声 | 0.3 m/s² |
| | 海流过程噪声 | 0.01 m/s² |
| **GPS** | 位置噪声（标准） | 2.0 m |
| | 位置噪声（DGPS） | 0.5 m |
| | 丢失超时 | 5 秒 |
| **罗经** | 噪声 | 0.5° |
| **计程仪** | 噪声 | 0.05 m/s |
| **质量监控** | 冻结窗口 | 10 次 |
| | 方差比阈值 | 3.0 |
| | 漂移 CUSUM 阈值 | 5.0 |
| | 恢复等待 | 30 秒 |
| **DR** | 长期阈值 | 300 秒 |
| | 噪声膨胀 | ×3.0 |
| **GPS 欺骗** | 位置阈值 | 100 m |
| | 速度阈值 | 3.0 m/s |

---

## 24. 与其他模块的协作关系

| 交互方 | 方向 | 数据 |
|-------|------|------|
| Data Preprocessing → Nav Filter | 输入 | GPS, 罗经, IMU, 计程仪原始数据 |
| Nav Filter → L3 Target Tracker | 输出 | 本船位置/速度（CPA/TCPA 计算） |
| Nav Filter → L4 LOS Guidance | 输出 | 本船位置/航向（航线跟踪） |
| Nav Filter → L4 CTE Calculator | 输出 | 本船位置（横向偏差计算） |
| Nav Filter → L4 Drift Correction | 输出 | 海流估计（偏流补偿） |
| Nav Filter → L5 Heading Controller | 输出 | heading, yaw_rate |
| Nav Filter → L5 Speed Controller | 输出 | u（对水速度反馈） |
| Nav Filter → Risk Monitor | 输出 | nav_mode, position_uncertainty |
| Nav Filter → ASDR | 输出 | 全部模式切换事件 |

---

## 25. 测试策略与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| NAV-001 | 正常航行 | 位置精度 < 2m RMS |
| NAV-002 | GPS 跳变 50m | 滤波后偏移 < 2m |
| NAV-003 | GPS 连续异常 | 自动排除 + 切换 DR |
| NAV-004 | GPS 丢失 30s | DR 漂移 < 20m |
| NAV-005 | GPS 丢失 3min | DR 漂移 < 100m |
| NAV-006 | GPS 丢失 10min | DR 漂移 < 500m + 降级通知 |
| NAV-007 | GPS 恢复小偏差 | 平滑过渡，L4 无阶跃 |
| NAV-008 | GPS 恢复大偏差 | 渐进过渡 5s 内收敛 |
| NAV-009 | GPS 欺骗 100m | ≤10s 检测 + 切 DR |
| NAV-010 | 罗经冻结 | ≤5s 检测 + 排除 |
| NAV-011 | 三罗经投票 | 正确排除异常台 |
| NAV-012 | 海流估计 | 偏差 < 0.3 m/s |
| NAV-013 | 50Hz 延迟 | < 5ms |

---

## 26. 参考文献与标准

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Fossen 2021, Ch.11-12 | 船舶导航滤波器理论 |
| [2] | Farrell 2008 | GPS/INS 融合 |
| [3] | Bar-Shalom 2001 | EKF 和创新门控 |
| [4] | Kongsberg K-Pos | 工业级 DP 导航滤波器参考 |
| [5] | IMO A.915(22) | 导航精度要求 |
| [6] | IALA Guideline 1129 | GNSS 漏洞缓解 |

---

## v3.1 架构升级：近场相对定位源支持 + IJS 降级通知

### A. 近场相对定位系统（Position Reference Systems）

在靠泊平台、抵近风电基座等 GNSS 信号受多径干扰的场景下，需要独立于卫星的近场高精度定位。Navigation Filter 的 EKF 架构天然支持增加新的定位源——只需增加一个 EKF 更新步。

#### A.1 支持的近场定位源

| 系统 | 原理 | 精度 | 作用距离 | 适用天气 |
|------|------|------|---------|---------|
| SpotTrack（激光测距） | 激光反射棱镜 | ±0.01m | 500m | 受雾影响 |
| RADius（微波雷达） | C-band 微波 | ±0.1m | 1000m | 全天候 |
| LiDAR 结构匹配 | 点云与已知结构匹配 | ±0.1m | 200m | 受雾影响 |
| 视觉 SLAM | 摄像头特征匹配 | ±0.5m | 500m | 受光照/雾影响 |

#### A.2 EKF 更新步——相对定位源

```
FUNCTION ekf_update_relative_position(state, P, prs_measurement):
    
    # 相对定位测量的是"本船相对于目标结构物的距离和方位"
    # 而非绝对经纬度
    
    z = [prs_measurement.range, prs_measurement.bearing]
    
    # 如果已知目标结构物的绝对位置（海图上的平台坐标）
    # 可以转换为绝对位置更新
    IF prs_measurement.target_position_known:
        target_pos = prs_measurement.target_position    # 已知的平台坐标
        
        # 从相对测量反算本船绝对位置
        own_x = target_pos.x - prs_measurement.range * sin(prs_measurement.bearing)
        own_y = target_pos.y - prs_measurement.range * cos(prs_measurement.bearing)
        
        z_abs = [own_x, own_y]
        h_abs = [state.x_n, state.x_e]
        
        H = zeros(2, 15)
        H[0][0] = 1; H[1][1] = 1
        
        # 测量噪声——比 GPS 精度高很多
        R_prs = diag([prs_measurement.range_noise², prs_measurement.range_noise²])
        
        # 标准 EKF 更新（与 GPS 更新同结构，但噪声更小）
        y = z_abs - h_abs
        S = H * P * Hᵀ + R_prs
        
        # 创新门控
        mahalanobis = yᵀ * S⁻¹ * y
        IF mahalanobis > CHI2_2DOF:
            RETURN state, P    # 拒绝（可能是目标识别错误）
        
        K = P * Hᵀ * S⁻¹
        state = state + K * y
        P = (I - K * H) * P
    
    RETURN state, P
```

#### A.3 多定位源自动权重

在近场操作中，多个定位源同时可用时，EKF 自动通过卡尔曼增益分配权重——精度高的定位源（SpotTrack ±0.01m）自然获得比 GPS（±2m）更大的权重。无需手动切换。

```
近场操作时的典型权重分配：
  SpotTrack:  85%（精度最高）
  GPS:        10%（作为粗略参考和一致性检查）
  计程仪+IMU:  5%（短期动态补充）

远场航行时的典型权重分配：
  GPS:        75%
  计程仪+IMU: 20%
  SpotTrack:   0%（超出作用距离，不可用）
```

### B. IJS 降级时的 Navigation Filter 行为

当系统降级到 IJS_EMERGENCY 模式（主控计算机不可用），Navigation Filter 也随之停止工作。IJS 的 MCU 只使用原始 GPS 位置（如可用）或航位推算——没有 EKF 融合。

```
# IJS 模式下的导航信息来源（降级）：
# 1. GPS 直接读取（无 EKF 滤波——有跳变风险但能用）
# 2. 罗经直接读取（无偏差补偿）
# 3. 无 Nav Filter 的融合精度和 DR 能力
# 这是可接受的降级——IJS 是紧急模式，不追求精度

# IJS MCU 通过独立的 NMEA 串口直接读取 GPS 和罗经
# 不经过 OT 网络或 ROS2
```

---

### C. 泊位相对坐标变换（离靠港操作支持）

靠泊操作需要的不是绝对经纬度，而是"距离泊位面还有多少米、沿泊位面偏了多少、航向差多少度"。Navigation Filter 增加泊位相对坐标输出。

```
FUNCTION transform_to_berth_frame(filtered_state, berth_definition):
    
    # 泊位定义（由 L2 Berth Planner 提供）：
    #   berth_position: 泊位中心点的 ENU 坐标
    #   berth_heading:  泊位面的法线方向（船应该面对的方向）
    #   berth_length:   泊位长度
    
    # 本船到泊位中心的 ENU 偏移
    dx = filtered_state.x_n - berth_definition.position.x_n
    dy = filtered_state.x_e - berth_definition.position.x_e
    
    # 旋转到泊位坐标系
    cos_b = cos(berth_definition.heading)
    sin_b = sin(berth_definition.heading)
    
    d_perp = dx * cos_b + dy * sin_b     # 垂直于泊位面的距离（正=需要接近）
    d_along = -dx * sin_b + dy * cos_b   # 沿泊位面方向的偏移（正=需要右移）
    
    # 航向偏差
    heading_error = normalize_pm_pi(filtered_state.heading - berth_definition.heading)
    
    # 接近速度（垂直于泊位面方向的速度分量）
    approach_speed = filtered_state.u * cos(heading_error) + filtered_state.v * sin(heading_error)
    
    # 横移速度
    lateral_speed = -filtered_state.u * sin(heading_error) + filtered_state.v * cos(heading_error)
    
    berth_relative = BerthRelativeState()
    berth_relative.d_perp = d_perp                    # 到泊位面距离（米）
    berth_relative.d_along = d_along                  # 沿泊位面偏移（米）
    berth_relative.heading_error = heading_error       # 航向偏差（弧度）
    berth_relative.approach_speed = approach_speed      # 接近速度（m/s）
    berth_relative.lateral_speed = lateral_speed        # 横移速度（m/s）
    berth_relative.position_uncertainty = filtered_state.position_uncertainty_m
    
    RETURN berth_relative

# 发布话题：/nav/berth_relative（仅在 DP_APPROACH 模式下发布）
# 频率：50 Hz（与 filtered_state 同步）
```

### D. 近场模式下的 Navigation Filter 精度要求

```
靠泊各阶段对 Navigation Filter 的精度要求：

进近段（50~200m）：位置精度 < 1.0m → GNSS + 近场传感器混合
精确段（5~50m）：  位置精度 < 0.3m → 近场传感器为主，GNSS 辅助
接触段（0~5m）：   位置精度 < 0.1m → 近场传感器为主 + LiDAR 点云

如果精度不满足要求：
  precision_degraded → Risk Monitor 收到通知 → 
  DP_APPROACH 降低接近速度或暂停接近（原地 DP_STATION）
```

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-26 | 架构组 | 初始版本 v3.0 新增模块 |
| 0.2 | 2026-04-26 | 架构组 | v3.1 升级：近场定位源 + IJS 降级 |
| 0.3 | 2026-04-26 | 架构组 | v3.1 补充：增加泊位相对坐标变换（BerthRelativeState）；增加近场各阶段精度要求；支持 DP_APPROACH 模式的定位需求 |

---

**文档结束**
