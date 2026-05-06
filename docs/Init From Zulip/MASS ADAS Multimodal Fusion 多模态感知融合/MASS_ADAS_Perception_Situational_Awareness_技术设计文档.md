# MASS_ADAS Situational Awareness Model 态势感知模型技术设计文档

**文档编号：** SANGO-ADAS-SAW-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的态势感知三层模型
5. 计算流程总览
6. 步骤一：环境态势构建
7. 步骤二：交通态势构建
8. 步骤三：本船态势构建
9. 步骤四：综合态势图生成
10. 步骤五：态势变化检测与事件生成
11. 能见度估计
12. 海况估计
13. 交通密度评估
14. 感知覆盖盲区分析
15. 态势数据的岸基共享
16. 内部子模块分解
17. 数值算例
18. 参数总览表
19. 与其他模块的协作关系
20. 测试策略与验收标准
21. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Situational Awareness Model（态势感知模型）是 Perception 子系统的最高层模块。它接收 Multi-Sensor Fusion 输出的融合目标列表和 Data Preprocessing 输出的环境数据，构建一个统一的、结构化的"世界模型"——包括环境态势（能见度、海况、水域类型）、交通态势（周围目标的分布、密度、运动模式）和本船态势（传感器健康、操纵余量）。

这个模块对应的是船长在驾驶台上持续维持的"头脑中的世界图景"——他不仅知道每个目标在哪里，还理解整个态势的"意义"：现在是在交通密集区还是空旷海域？能见度在变好还是变差？我的传感器有没有盲区？前方有什么特殊的水域需要注意？

### 1.2 核心职责

- **环境态势构建：** 综合传感器数据估计当前能见度、海况等级、降水强度。
- **交通态势构建：** 评估周围的交通密度、交通分布模式、潜在冲突区域。
- **本船态势构建：** 评估传感器覆盖范围、盲区、系统健康状态。
- **综合态势图输出：** 以结构化数据形式输出完整的态势信息，供 L3 Tactical 层和 Shore Link 使用。
- **态势变化检测：** 检测态势的显著变化（如能见度突然下降、交通密度突然增加），生成事件通知。

### 1.3 设计原则

- **全局视角原则：** 不关注单个目标的细节（那是 Target Tracker 的事），关注的是"整体画面"。
- **预判原则：** 不仅描述当前态势，还预判近期可能的变化趋势。
- **可视化友好原则：** 输出的数据结构应便于在岸基监控站进行可视化显示。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 频率 |
|-------|------|------|
| 融合目标列表 | Multi-Sensor Fusion | 2 Hz |
| 本船状态 | Data Preprocessing | 50 Hz |
| 环境传感器数据（风、水深、温度） | Data Preprocessing | 1 Hz |
| 水域类型 | ENC Reader | 按需 |
| 传感器健康状态 | Data Preprocessing | 1 Hz |
| 摄像头图像（用于能见度估计） | Data Preprocessing | 1 Hz（降采样） |
| 雷达杂波水平 | Object Detection | 2 Hz |

### 2.2 输出

```
EnvironmentState:
    Header header
    
    # ---- 能见度 ----
    float64 visibility_nm               # 估计能见度（海里）
    string visibility_category          # "good"(>5nm) / "moderate"(2~5nm) / "poor"(0.5~2nm) / "very_poor"(<0.5nm)
    float64 visibility_trend            # 能见度变化趋势（正=改善，负=恶化，nm/min）
    
    # ---- 海况 ----
    uint8 sea_state                     # 海况等级（0~9，Douglas 海况表）
    float64 significant_wave_height_m   # 有义波高（米）
    float64 dominant_wave_period_s      # 主波周期（秒）
    float64 dominant_wave_direction_deg # 主波方向（度）
    
    # ---- 降水 ----
    string precipitation                # "none" / "light_rain" / "moderate_rain" / "heavy_rain" / "snow"
    
    # ---- 交通 ----
    uint32 total_targets_count          # 视野内目标总数
    float64 traffic_density             # 交通密度（目标数/平方海里）
    string traffic_level                # "sparse"(<2) / "moderate"(2~5) / "dense"(5~10) / "very_dense"(>10)
    TrafficSector[] traffic_sectors     # 各扇区的交通分布
    
    # ---- 水域 ----
    string zone_type                    # 当前水域类型（来自 ENC Reader）
    float64 distance_to_shore_nm        # 距最近岸线距离
    bool in_tss                         # 是否在 TSS 内
    bool in_narrow_channel              # 是否在狭水道内
    
    # ---- 感知覆盖 ----
    SensorCoverage sensor_coverage      # 传感器覆盖评估
    BlindSector[] blind_sectors         # 感知盲区列表
    
    # ---- 系统健康 ----
    string overall_perception_status    # "nominal" / "degraded" / "critical"
    string[] degraded_sensors           # 降级的传感器列表

TrafficSector:
    float64 bearing_from                # 扇区起始方位（度）
    float64 bearing_to                  # 扇区终止方位（度）
    uint32 target_count                 # 该扇区内的目标数
    float64 nearest_target_range_nm     # 该扇区内最近目标的距离

SensorCoverage:
    float64 radar_effective_range_nm    # 雷达有效探测距离
    float64 camera_effective_range_nm   # 摄像头有效探测距离
    float64 overall_coverage_ratio      # 360° 中有效覆盖的比例 [0,1]

BlindSector:
    float64 bearing_from                # 盲区起始方位
    float64 bearing_to                  # 盲区终止方位
    string cause                        # "radar_shadow" / "camera_fov_gap" / "sensor_failure"
```

---

## 3. 核心参数数据库

| 参数 | 值 | 说明 |
|------|-----|------|
| 交通密度评估半径 | 6 nm | 在此半径内统计目标数 |
| 交通密度分级——稀疏 | < 2 targets/nm² | |
| 交通密度分级——中等 | 2~5 | |
| 交通密度分级——密集 | 5~10 | |
| 交通密度分级——极密集 | > 10 | |
| 交通扇区数量 | 12（每 30°） | 360° 分为 12 个扇区 |
| 能见度估计更新间隔 | 30 秒 | |
| 海况估计更新间隔 | 60 秒 | |
| 态势变化检测阈值——能见度 | ±1 nm/5min | |
| 态势变化检测阈值——交通密度 | ±50%/5min | |
| 能见度估计——雷达杂波法系数 | 经验标定 | |
| 能见度估计——摄像头对比度法参数 | 经验标定 | |

---

## 4. 船长的态势感知三层模型

Endsley (1995) 的态势感知理论将态势感知分为三个层次——这恰好对应船长在驾驶台上的认知过程：

**Level 1——感知（Perception）：** "有什么？" 船长知道周围有哪些目标、它们在哪里、当前能见度如何、海况如何。这是 Multi-Sensor Fusion 的输出。

**Level 2——理解（Comprehension）：** "意味着什么？" 船长理解这些信息的含义——"前方 5 海里有一艘对遇船，CPA 不足 1 海里"不仅仅是数字，他理解这意味着"需要避让"。交通密集、能见度下降意味着"需要降速、提高警惕"。

**Level 3——预判（Projection）：** "接下来会怎样？" 船长预判近期态势的变化——"这片雾看起来在加厚"、"前方渔区可能有大量渔船"、"进入 TSS 后交通会变密"。

Situational Awareness Model 主要负责 Level 2（理解）和部分 Level 3（预判）。Level 1（感知）由前序模块完成，Level 3 的决策预判由 L3 Tactical 层完成。

---

## 5. 计算流程总览

```
融合目标列表 + 环境数据 + 本船状态
          │
          ▼
    ┌──────────────────────────────────┐
    │ 步骤一：环境态势构建              │
    │ 能见度估计、海况估计、降水判定     │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤二：交通态势构建              │
    │ 交通密度、扇区分布、冲突区域      │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤三：本船态势构建              │
    │ 传感器覆盖、盲区分析              │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤四：综合态势图生成             │
    │ 封装 EnvironmentState 消息        │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤五：态势变化检测              │
    │ 能见度下降、交通激增等事件         │
    └────────────┬─────────────────────┘
                 │
                 ▼
输出：EnvironmentState (0.2 Hz) + 态势变化事件
```

---

## 6. 步骤一：环境态势构建

### 6.1 能见度估计

能见度不能直接测量（除非安装专用能见度仪）。需要从间接传感器数据估计。

**方法一：雷达杂波水平法**

雾和雨增加大气衰减，导致雷达远距离回波减弱——表现为有效探测距离缩短。

```
FUNCTION estimate_visibility_radar(radar_data):
    
    # 统计雷达回波在不同距离段的强度衰减
    near_echo_avg = mean_echo_strength(radar_data, 0~2nm)
    mid_echo_avg = mean_echo_strength(radar_data, 2~6nm)
    far_echo_avg = mean_echo_strength(radar_data, 6~12nm)
    
    # 正常情况下远距离的已知目标应有一定强度
    # 如果远距离回波显著弱于预期——可能有降水或雾
    
    attenuation_ratio = far_echo_avg / near_echo_avg
    
    # 经验映射（需要标定）
    IF attenuation_ratio > 0.5:
        estimated_visibility = 10    # nm，良好
    ELIF attenuation_ratio > 0.3:
        estimated_visibility = 5
    ELIF attenuation_ratio > 0.1:
        estimated_visibility = 2
    ELSE:
        estimated_visibility = 0.5
    
    RETURN estimated_visibility
```

**方法二：摄像头图像对比度法**

雾降低图像的对比度和清晰度。

```
FUNCTION estimate_visibility_camera(image):
    
    # 1. 提取图像的全局对比度
    gray = cvtColor(image, BGR2GRAY)
    contrast = std(gray) / mean(gray)    # 对比度指标
    
    # 2. 检测地平线区域的清晰度
    horizon_region = gray[height//3 : height//2, :]
    edge_density = count_edges(horizon_region)    # 边缘像素密度
    
    # 3. 暗通道先验（DCP）——雾浓度估计
    dark_channel = compute_dark_channel(image, patch_size=15)
    fog_density = mean(dark_channel)    # 暗通道值越高雾越浓
    
    # 4. 综合估计能见度
    IF fog_density < 50:
        estimated_visibility = 10    # nm
    ELIF fog_density < 100:
        estimated_visibility = 5
    ELIF fog_density < 150:
        estimated_visibility = 2
    ELIF fog_density < 200:
        estimated_visibility = 1
    ELSE:
        estimated_visibility = 0.5
    
    RETURN estimated_visibility
```

**融合两种方法：**

```
FUNCTION estimate_visibility_fused(radar_vis, camera_vis):
    
    IF camera_available AND radar_available:
        # 取保守值（较低的那个）
        RETURN min(radar_vis, camera_vis)
    ELIF camera_available:
        RETURN camera_vis
    ELIF radar_available:
        RETURN radar_vis
    ELSE:
        RETURN NaN    # 无法估计
```

### 6.2 海况估计

```
FUNCTION estimate_sea_state(imu_data, radar_sea_clutter):
    
    # 方法一：从 IMU 的垂直加速度估计波高
    # 有义波高 H_s ≈ 4 × std(vertical_acceleration) / ω²
    # 简化：从横摇幅度估计
    roll_amplitude = compute_rms(imu_data.roll_history_60s)
    
    # 方法二：从雷达海杂波强度估计
    sea_clutter_level = radar_sea_clutter.average_level
    
    # 映射到 Douglas 海况表
    IF roll_amplitude < 1:
        sea_state = 1
    ELIF roll_amplitude < 3:
        sea_state = 2
    ELIF roll_amplitude < 5:
        sea_state = 3
    ELIF roll_amplitude < 8:
        sea_state = 4
    ELIF roll_amplitude < 12:
        sea_state = 5
    ELSE:
        sea_state = min(int(roll_amplitude / 2), 9)
    
    # 波浪周期从 IMU 频谱分析获取
    wave_period = estimate_dominant_period(imu_data.roll_history_60s)
    
    RETURN {sea_state, wave_period, roll_amplitude}
```

---

## 7. 步骤二：交通态势构建

### 7.1 交通密度计算

```
FUNCTION compute_traffic_density(targets, own_position, radius_nm):
    
    radius_m = radius_nm × 1852
    area_nm2 = π × radius_nm²
    
    targets_in_range = [t for t in targets if t.range < radius_m]
    count = len(targets_in_range)
    
    density = count / area_nm2    # 目标数/平方海里
    
    # 分级
    IF density < 2:
        level = "sparse"
    ELIF density < 5:
        level = "moderate"
    ELIF density < 10:
        level = "dense"
    ELSE:
        level = "very_dense"
    
    RETURN {count, density, level}
```

### 7.2 交通扇区分布

```
FUNCTION compute_traffic_sectors(targets, n_sectors=12):
    
    sector_size = 360 / n_sectors
    sectors = []
    
    FOR i = 0 TO n_sectors - 1:
        bearing_from = i × sector_size
        bearing_to = (i + 1) × sector_size
        
        sector_targets = [t for t in targets 
                          if bearing_from ≤ t.bearing_relative < bearing_to]
        
        nearest = min(sector_targets, key=lambda t: t.range) IF sector_targets ELSE NULL
        
        sectors.append({
            bearing_from: bearing_from,
            bearing_to: bearing_to,
            target_count: len(sector_targets),
            nearest_target_range_nm: nearest.range / 1852 IF nearest ELSE NaN
        })
    
    RETURN sectors
```

### 7.3 交通模式识别

```
FUNCTION identify_traffic_patterns(targets, zone_type):
    
    patterns = []
    
    # 模式 1：对向交通流（两组目标航向相差约 180°）
    heading_groups = cluster_headings(targets, n_clusters=2)
    IF len(heading_groups) == 2:
        diff = abs(heading_groups[0].mean_heading - heading_groups[1].mean_heading)
        IF 160 < diff < 200:
            patterns.append("opposing_traffic_flow")
    
    # 模式 2：交叉交通（两组航向差约 90°）
    IF len(heading_groups) == 2:
        IF 70 < diff < 110:
            patterns.append("crossing_traffic")
    
    # 模式 3：渔船群（多个低速、不规则运动的目标聚集）
    slow_targets = [t for t in targets if t.sog < 3.0]    # < 3 m/s
    IF len(slow_targets) > 3:
        # 检查是否聚集
        centroid = mean_position(slow_targets)
        spread = max(distance(t, centroid) for t in slow_targets)
        IF spread < 2 × 1852:    # < 2 nm 范围内
            patterns.append("fishing_fleet")
    
    # 模式 4：锚泊区（多个静止目标）
    stationary = [t for t in targets if t.sog < 0.5]
    IF len(stationary) > 3:
        patterns.append("anchorage_area")
    
    RETURN patterns
```

---

## 8. 步骤三：本船态势构建

### 8.1 传感器覆盖评估

```
FUNCTION evaluate_sensor_coverage(sensor_status, visibility):
    
    # 雷达有效距离（不受能见度影响，但受降水影响）
    radar_range_nm = RADAR_MAX_RANGE
    IF precipitation IN ["moderate_rain", "heavy_rain"]:
        radar_range_nm *= 0.7    # 降水衰减
    
    # 摄像头有效距离（强烈依赖能见度）
    camera_range_nm = min(visibility, CAMERA_MAX_RANGE)
    
    # 360° 覆盖率
    covered_sectors = 0
    total_sectors = 360
    
    # 检查雷达覆盖（通常 360°，除了阴影区）
    radar_shadows = get_radar_shadow_sectors()
    radar_coverage = 360 - sum(s.width for s in radar_shadows)
    
    # 检查摄像头覆盖（取决于摄像头数量和布局）
    camera_coverage = sum(cam.fov_horizontal for cam in active_cameras)
    camera_coverage = min(camera_coverage, 360)
    
    # 综合覆盖率
    overall_coverage = radar_coverage / 360    # 雷达几乎全覆盖
    
    RETURN {
        radar_effective_range_nm: radar_range_nm,
        camera_effective_range_nm: camera_range_nm,
        overall_coverage_ratio: overall_coverage
    }
```

### 8.2 盲区分析

```
FUNCTION analyze_blind_sectors(sensor_status, radar_shadows, camera_config):
    
    blind_sectors = []
    
    # 雷达阴影区
    FOR EACH shadow IN radar_shadows:
        blind_sectors.append({
            bearing_from: shadow.start_deg,
            bearing_to: shadow.end_deg,
            cause: "radar_shadow",
            severity: "LOW"    # 摄像头可能仍覆盖
        })
    
    # 摄像头间隙
    camera_gaps = compute_camera_gaps(camera_config)
    FOR EACH gap IN camera_gaps:
        blind_sectors.append({
            bearing_from: gap.start_deg,
            bearing_to: gap.end_deg,
            cause: "camera_fov_gap",
            severity: "LOW"    # 雷达仍覆盖
        })
    
    # 传感器故障导致的盲区
    FOR EACH sensor IN sensor_status:
        IF sensor.status == "OFFLINE":
            IF sensor.type == "radar":
                blind_sectors.append({
                    bearing_from: 0, bearing_to: 360,
                    cause: "radar_failure",
                    severity: "CRITICAL"    # 雷达是主传感器
                })
            IF sensor.type == "camera" AND sensor.facing == "forward":
                blind_sectors.append({
                    bearing_from: 330, bearing_to: 30,
                    cause: "forward_camera_failure",
                    severity: "HIGH"
                })
    
    RETURN blind_sectors
```

---

## 9. 步骤四：综合态势图生成

将以上各步骤的结果封装为 EnvironmentState 消息并发布。

```
FUNCTION build_environment_state():
    
    state = EnvironmentState()
    state.header.stamp = now()
    
    # 能见度
    state.visibility_nm = visibility_estimate
    state.visibility_category = categorize_visibility(visibility_estimate)
    state.visibility_trend = compute_visibility_trend()
    
    # 海况
    state.sea_state = sea_state_estimate
    state.significant_wave_height_m = wave_height
    state.dominant_wave_period_s = wave_period
    
    # 降水
    state.precipitation = precipitation_estimate
    
    # 交通
    state.total_targets_count = traffic.count
    state.traffic_density = traffic.density
    state.traffic_level = traffic.level
    state.traffic_sectors = traffic_sectors
    
    # 水域
    state.zone_type = current_zone.zone_type
    state.distance_to_shore_nm = current_zone.distance_to_shore / 1852
    state.in_tss = current_zone.zone_type == "tss_lane"
    state.in_narrow_channel = current_zone.zone_type == "narrow_channel"
    
    # 覆盖
    state.sensor_coverage = coverage
    state.blind_sectors = blind_sectors
    
    # 系统健康
    state.overall_perception_status = compute_perception_status()
    state.degraded_sensors = get_degraded_sensor_list()
    
    RETURN state
```

---

## 10. 步骤五：态势变化检测与事件生成

```
FUNCTION detect_situation_changes(current_state, previous_state, dt):
    
    events = []
    
    # 能见度显著下降
    IF current_state.visibility_nm < previous_state.visibility_nm - 1.0:
        events.append({
            type: "visibility_decrease",
            severity: "WARNING",
            details: f"能见度从 {previous_state.visibility_nm:.1f}nm 降至 {current_state.visibility_nm:.1f}nm"
        })
    
    # 能见度进入"不良"
    IF current_state.visibility_category == "poor" AND previous_state.visibility_category IN ["good", "moderate"]:
        events.append({
            type: "visibility_poor",
            severity: "HIGH",
            details: "能见度降至不良水平，应启用 Rule 19"
        })
    
    # 交通密度显著增加
    IF current_state.traffic_density > previous_state.traffic_density × 1.5:
        events.append({
            type: "traffic_increase",
            severity: "INFO",
            details: f"交通密度增加至 {current_state.traffic_density:.1f} targets/nm²"
        })
    
    # 进入特殊水域
    IF current_state.zone_type != previous_state.zone_type:
        events.append({
            type: "zone_change",
            severity: "INFO",
            details: f"进入 {current_state.zone_type} 水域"
        })
    
    # 海况恶化
    IF current_state.sea_state > previous_state.sea_state + 1:
        events.append({
            type: "sea_state_increase",
            severity: "WARNING",
            details: f"海况从 {previous_state.sea_state} 升至 {current_state.sea_state}"
        })
    
    # 传感器降级
    IF current_state.overall_perception_status == "degraded" AND previous_state.overall_perception_status == "nominal":
        events.append({
            type: "perception_degraded",
            severity: "HIGH",
            details: f"感知系统降级，受影响传感器：{current_state.degraded_sensors}"
        })
    
    RETURN events
```

---

## 11. 能见度估计详解

（详细的雷达杂波法和摄像头暗通道先验法已在第 6.1 节中给出。）

### 能见度分级

| 等级 | 范围 | 对应气象条件 | 系统响应 |
|------|------|------------|---------|
| good | > 5 nm | 晴朗或少云 | 正常模式 |
| moderate | 2~5 nm | 薄雾或阵雨 | 提高警惕 |
| poor | 0.5~2 nm | 中度雾或持续降雨 | 启用 Rule 19，降速 |
| very_poor | < 0.5 nm | 浓雾或暴雨 | 考虑停航或锚泊 |

---

## 12. 海况估计详解

### Douglas 海况表（简化）

| 等级 | H_s (m) | 描述 | 对 USV 的影响 |
|------|---------|------|-------------|
| 0 | 0 | 平静如镜 | 无影响 |
| 1 | 0~0.1 | 微澜 | 无影响 |
| 2 | 0.1~0.5 | 小浪 | 轻微横摇 |
| 3 | 0.5~1.25 | 中浪 | 中度横摇，开始影响摄像头稳定性 |
| 4 | 1.25~2.5 | 大浪 | 明显横摇，雷达杂波增加 |
| 5 | 2.5~4.0 | 巨浪 | 影响操纵性，考虑降速 |
| 6+ | > 4.0 | 猛浪+ | 对 12m USV 可能超出安全操纵范围 |

---

## 13. 交通密度评估详解

（详细算法已在第 7.1~7.3 节中给出。）

---

## 14. 感知覆盖盲区分析详解

（详细算法已在第 8.2 节中给出。）

---

## 15. 态势数据的岸基共享

```
FUNCTION share_situation_to_shore(environment_state):
    
    # 压缩态势数据（卫星通信带宽有限）
    compressed = {
        timestamp: state.header.stamp,
        visibility: round(state.visibility_nm, 1),
        sea_state: state.sea_state,
        traffic_count: state.total_targets_count,
        traffic_level: state.traffic_level,
        zone: state.zone_type,
        perception_status: state.overall_perception_status,
        events: recent_events    # 最近的变化事件
    }
    
    shore_link.publish(compressed)
    
    # 每分钟发送一次完整态势
    # 有重要事件时立即发送
    IF has_high_severity_events:
        shore_link.publish_urgent(compressed)
```

---

## 16. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Visibility Estimator | 能见度估计（雷达+摄像头融合） | C++/Python |
| Sea State Estimator | 海况估计（IMU+雷达杂波） | C++ |
| Traffic Analyzer | 交通密度和模式分析 | C++ |
| Coverage Analyzer | 传感器覆盖和盲区分析 | C++ |
| Situation Builder | 综合态势图构建 | C++ |
| Change Detector | 态势变化检测和事件生成 | C++ |
| Shore Sharer | 态势数据压缩和岸基共享 | Python |

---

## 17. 数值算例

```
当前态势：
  融合目标数 = 15
  评估半径 6nm 面积 = π × 36 = 113 nm²
  交通密度 = 15/113 = 0.13 targets/nm² → "sparse"
  
  雷达杂波衰减比 = 0.25 → 估计能见度 ≈ 2~3 nm
  摄像头暗通道均值 = 120 → 估计能见度 ≈ 3~4 nm
  融合能见度 = min(2.5, 3.5) = 2.5 nm → "moderate"
  
  IMU 横摇 RMS = 4° → 海况 3
  
  水域类型 = "coastal"
  
  感知状态 = "nominal"（全部传感器正常）
```

---

## 18. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 交通密度评估半径 | 6 nm | |
| 交通扇区数 | 12 | |
| 能见度更新间隔 | 30 s | |
| 海况更新间隔 | 60 s | |
| 能见度"不良"阈值 | 2.0 nm | |
| 能见度"极差"阈值 | 0.5 nm | |
| 态势变化检测——能见度 | Δ > 1 nm | |
| 态势变化检测——交通密度 | Δ > 50% | |
| 态势变化检测——海况 | Δ > 1 级 | |

---

## 19. 与其他模块的协作

| 交互方 | 方向 | 数据 |
|-------|------|------|
| Multi-Sensor Fusion → SA | 输入 | TrackedTargetArray |
| Data Preprocessing → SA | 输入 | OwnShipState, 环境传感器 |
| ENC Reader → SA | 输入 | ZoneClassification |
| SA → Target Tracker (L3) | 输出 | EnvironmentState（能见度→CPA_safe 调整） |
| SA → COLREGs Engine (L3) | 输出 | EnvironmentState（能见度→Rule 19 切换） |
| SA → Heading Controller (L5) | 输出 | sea_state（增益调度） |
| SA → Speed Profiler (L2) | 输出 | visibility, sea_state（速度约束） |
| SA → Shore Link | 输出 | 压缩态势报告 |
| SA → Risk Monitor (L3) | 输出 | perception_status（传感器降级） |

---

## 20. 测试策略与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| SAW-001 | 良好天气开阔水域 | visibility="good", sea_state≤2, traffic="sparse" |
| SAW-002 | 雾天（能见度 1nm） | visibility="poor" 正确检测 |
| SAW-003 | 大雨 | precipitation 正确识别 |
| SAW-004 | 交通密集区（> 10 目标/nm²） | traffic="very_dense" 正确分级 |
| SAW-005 | 进入 TSS | zone_change 事件触发 |
| SAW-006 | 能见度突降 | visibility_decrease 事件 ≤ 30s 内触发 |
| SAW-007 | 雷达故障 | blind_sector 正确标注 + perception="degraded" |
| SAW-008 | 渔船群检测 | traffic_pattern="fishing_fleet" |
| SAW-009 | 海况从 2 升到 5 | sea_state_increase 事件触发 |
| SAW-010 | 岸基共享数据完整性 | 压缩数据包含全部关键字段 |

---

## 21. 参考文献与标准

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Endsley, M.R. "Toward a Theory of Situation Awareness" Human Factors, 1995 | 态势感知三层模型 |
| [2] | IMO MSC.192(79) | INS 综合导航系统性能标准 |
| [3] | He, K. et al. "Single Image Haze Removal Using Dark Channel Prior" CVPR, 2009 | 暗通道先验（能见度估计） |
| [4] | WMO Manual on Codes | Douglas 海况表 |
| [5] | IMO COLREGs Rule 5 | 了望要求（态势感知的法律基础） |

---

**文档结束**
