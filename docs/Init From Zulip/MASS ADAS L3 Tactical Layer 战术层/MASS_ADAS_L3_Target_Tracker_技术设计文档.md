# MASS_ADAS Target Tracker 目标跟踪器技术设计文档

**文档编号：** SANGO-ADAS-TGT-001  
**版本：** 0.1 草案  
**日期：** 2026-04-25  
**编制：** 系统架构组  
**状态：** 开发参考

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的态势感知思维模型
5. 计算流程总览
6. 步骤一：目标筛选与预处理
7. 步骤二：相对运动计算
8. 步骤三：CPA/TCPA 精确计算
9. 步骤四：BCR 与船首穿越分析
10. 步骤五：目标运动预测
11. 步骤六：威胁评估与优先级排序
12. 目标状态管理与航迹维护
13. 传感器不一致处理
14. 坐标系与单位规范
15. 特殊目标处理
16. 性能要求
17. 内部子模块分解
18. 数值算例
19. 参数总览表
20. 与其他模块的协作关系
21. 测试策略与验收标准
22. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Target Tracker（目标跟踪器）是 MASS_ADAS 五层架构中 Layer 3（Tactical Layer，战术层）的第一个子模块，是整个避碰决策链的入口。它接收多模态感知融合子系统（Perception）输出的统一目标列表，为每个目标计算避碰决策所需的全部运动学指标，并按威胁等级排序输出结构化的"威胁列表"，供下游的 COLREGs Engine 消费。

Target Tracker 等价于船长在驾驶台上持续进行的"态势感知"工作——他不断扫视雷达屏幕、观察 AIS 信息、判断每个目标"有没有威胁、威胁多大、还有多少时间"。这个判断过程是所有避碰行动的前提。

### 1.2 核心职责

Target Tracker 的职责可以精确概括为六项：

- **相对运动计算：** 为每个目标计算相对速度、相对航向、相对运动线方向。
- **CPA/TCPA 计算：** 计算每个目标的最近会遇距离（CPA）和到达最近会遇点的时间（TCPA）。
- **BCR 计算：** 计算目标穿越本船船首前方时的距离（Bow Crossing Range），判断目标是否会从船首前方通过。
- **运动预测：** 基于目标当前状态预测其未来 1~10 分钟的位置轨迹，含预测不确定性估算。
- **威胁评估：** 综合 CPA、TCPA、距离、相对方位等指标，对每个目标进行威胁等级评分和排序。
- **航迹维护：** 管理目标的生命周期——新目标出现、目标状态更新、目标丢失/消失的处理。

### 1.3 设计原则

- **保守判断原则：** 对模糊目标或数据不确定性较大的目标，始终假设最危险的情况。
- **连续跟踪原则：** 目标的状态应在时间上保持连续性——避免因传感器数据跳变导致 CPA/TCPA 的剧烈波动，需要平滑滤波。
- **全目标评估原则：** 不预判哪些目标"肯定安全"而跳过计算。即使远距离的目标，如果是高速接近，TCPA 可能很短。
- **实时性原则：** 全部目标的计算必须在一个决策周期（2 秒）内完成。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 话题/接口 | 频率 | 说明 |
|-------|------|----------|------|------|
| 融合目标列表 | Perception | `/perception/targets` | 2 Hz | TrackedTargetArray 消息 |
| 环境态势 | Perception | `/perception/environment` | 0.2 Hz | 能见度、水域类型等 |
| 本船状态 | Nav Filter | `/nav/odom` | 50 Hz | 位置、速度、航向、角速度 |
| 本船地理位置 | Nav Filter | `/nav/geo_pose` | 10 Hz | 经纬度 |
| CPA 安全距离标准 | Parameter DB | 配置文件 | 启动时加载 | 各水域类型的 CPA_safe |
| 威胁评估参数 | Parameter DB | 配置文件 | 启动时加载 | 权重、阈值 |

### 2.2 输入消息结构详解

来自 Perception 的 TrackedTarget 消息（参见话题接口规范文档 7.1 节）包含以下关键字段：

```
TrackedTarget:
    uint32 track_id          # Perception 分配的唯一跟踪 ID
    uint32 mmsi              # AIS 关联的 MMSI（0 = 未关联）
    string classification    # "vessel" / "buoy" / "fishing_net" / "debris" / "unknown"
    float64 confidence       # 融合置信度 [0, 1]
    float64 latitude         # WGS84
    float64 longitude        # WGS84
    float64 cog              # 对地航向（度）
    float64 sog              # 对地速度（m/s）
    float64 heading          # 真航向（度，NaN = 未知）
    float64 length           # 船长（米，NaN = 未知）
    float64 beam             # 船宽（米，NaN = 未知）
    uint8[] source_sensors   # 数据来源：0=Radar, 1=AIS, 2=Camera, 3=LiDAR
    Time first_detected      # 首次检测时间
    Time last_updated        # 最后更新时间
```

### 2.3 输出

Target Tracker 通过内部接口向 COLREGs Engine 传递威胁列表。同时发布到 ROS2 话题 `/tactical/threat_list` 供 Shore Link 和调试工具使用。

**输出消息结构：**

```
ThreatList:
    Header header
    ThreatTarget[] targets          # 按 threat_score 降序排列
    uint32 total_tracked            # 当前跟踪的目标总数
    uint32 threat_count             # threat_level ≥ MEDIUM 的目标数

ThreatTarget:
    # ---- 目标身份 ----
    uint32 track_id                 # 跟踪 ID（与 Perception 一致）
    uint32 mmsi                     # AIS MMSI
    string classification           # 目标类型
    float64 confidence              # 数据置信度

    # ---- 当前绝对状态 ----
    float64 latitude                # WGS84
    float64 longitude               # WGS84
    float64 cog                     # 对地航向（度，真北）
    float64 sog                     # 对地速度（m/s）
    float64 heading                 # 真航向（度）
    float64 length                  # 船长（米）
    float64 beam                    # 船宽（米）

    # ---- 相对运动状态 ----
    float64 range                   # 距本船距离（米）
    float64 bearing_true            # 真方位（度，真北基准）
    float64 bearing_relative        # 相对方位（度，船首为 0°，顺时针）
    float64 rel_speed               # 相对速度（m/s）
    float64 rel_course              # 相对运动线方向（度，真北基准）
    float64 range_rate              # 距离变化率（m/s，负值=接近）

    # ---- 避碰核心指标 ----
    float64 cpa                     # 最近会遇距离（米）
    float64 tcpa                    # 到达 CPA 时间（秒，负值=已过）
    float64 dcpa                    # CPA 时目标在本船的左/右偏移（米，正=右舷）
    float64 bcr                     # 船首穿越距离（米，NaN=不穿越船首）
    float64 bcr_time                # 到达 BCR 时间（秒）
    bool crosses_bow                # 目标是否会穿越本船船首前方

    # ---- 运动预测 ----
    PredictedPosition pred_1min     # 1 分钟后预测
    PredictedPosition pred_3min     # 3 分钟后预测
    PredictedPosition pred_5min     # 5 分钟后预测
    PredictedPosition pred_10min    # 10 分钟后预测

    # ---- 威胁评估 ----
    string threat_level             # "CRITICAL"/"HIGH"/"MEDIUM"/"LOW"/"NONE"
    float64 threat_score            # 连续评分 [0, 100]
    string threat_trend             # "INCREASING"/"STABLE"/"DECREASING"

    # ---- 航迹状态 ----
    string track_status             # "TRACKING"/"LOST"/"NEW"
    float64 track_age_sec           # 跟踪持续时间（秒）
    float64 data_age_sec            # 距最后一次数据更新的时间（秒）

PredictedPosition:
    float64 latitude
    float64 longitude
    float64 uncertainty_radius_m    # 预测不确定性半径（米）
```

---

## 3. 核心参数数据库

### 3.1 CPA 安全距离标准

CPA 安全距离是 Target Tracker 判断目标是否构成威胁的核心阈值。它随水域类型变化：

| 水域类型（zone_type） | CPA_safe (nm) | CPA_safe (m) | 说明 |
|---------------------|-------------|-------------|------|
| open_sea | 1.0 | 1852 | 大洋通用标准 |
| coastal | 0.5 | 926 | 沿岸空间受限 |
| narrow_channel | 0.3 | 556 | 狭水道 |
| tss_lane | 0.5 | 926 | TSS 分道内 |
| port_approach | 0.3 | 556 | 进港区域 |
| port_inner | 0.2 | 370 | 港内 |
| anchorage | 0.3 | 556 | 锚地 |

**动态调整：** 能见度不良时（< 2 nm），CPA_safe 增大 50%。

```
IF visibility < 2.0 nm:
    CPA_safe_effective = CPA_safe × 1.5
ELIF visibility < 5.0 nm:
    CPA_safe_effective = CPA_safe × 1.2
ELSE:
    CPA_safe_effective = CPA_safe
```

### 3.2 威胁评估权重参数

| 参数 | 符号 | 默认值 | 取值范围 | 说明 |
|------|------|-------|---------|------|
| CPA 权重 | w_cpa | 0.40 | 0.30~0.50 | CPA 越小威胁越大 |
| TCPA 权重 | w_tcpa | 0.30 | 0.20~0.40 | TCPA 越短越紧急 |
| 距离权重 | w_range | 0.20 | 0.10~0.25 | 当前距离越近越危险 |
| 方位权重 | w_aspect | 0.10 | 0.05~0.15 | 正前方来船比正后方更危险 |

### 3.3 威胁等级阈值

| 等级 | threat_score 区间 | 含义 |
|------|-----------------|------|
| CRITICAL | ≥ 80 | 紧急碰撞风险，需立即行动 |
| HIGH | [60, 80) | 需要启动避碰行动 |
| MEDIUM | [40, 60) | 需要持续关注，COLREGs Engine 开始评估 |
| LOW | [20, 40) | 存在但不紧迫 |
| NONE | < 20 | 无威胁 |

### 3.4 跟踪维护参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 新目标确认时间 | 30 秒 | 至少连续跟踪 30 秒才确认为有效目标 |
| 目标丢失超时 | 60 秒 | 超过 60 秒无更新则标记为 LOST |
| 目标删除超时 | 180 秒 | LOST 状态超过 180 秒则从列表删除 |
| CPA/TCPA 平滑滤波时间常数 | 10 秒 | 低通滤波防止跳变 |
| 最大跟踪目标数 | 100 | 超过时按威胁等级保留最高优先级目标 |

### 3.5 预测不确定性参数

| 参数 | 符号 | 值 | 说明 |
|------|------|-----|------|
| 基础位置不确定性 | σ_base | 20 m | Perception 融合后的典型精度 |
| 不确定性增长率 | k_uncertainty | 0.05 × V_target | 每秒增长目标速度的 5% |
| AIS 目标不确定性折减 | k_ais | 0.5 | AIS 关联的目标不确定性减半 |
| 仅雷达目标不确定性增大 | k_radar_only | 1.5 | 无 AIS 关联的目标不确定性增大 50% |

---

## 4. 船长的态势感知思维模型

### 4.1 船长如何"看"目标

一个经验丰富的船长在驾驶台上做态势感知时，不是逐个检查每个目标的 CPA 和 TCPA——他首先做的是**快速分类**："哪些目标需要关注，哪些可以忽略"。

他的快速分类依据是：

**距离和接近趋势。** 雷达上看到一个目标亮点，首先看它离我多远。如果在 12 nm 以外，通常不急。但如果亮点在扫描之间明显移动（距离在缩短），说明它在快速接近，即使距离远也需要关注。

**相对方位是否稳定。** 这是老船长最核心的判断技巧——如果一个目标的相对方位（Relative Bearing）随时间不变或变化很慢，说明存在碰撞风险（"方位不变距离近"定律）。如果方位在明显变化，通常不会碰撞。

**相对运动线的方向。** 在雷达的相对运动显示模式下，目标的运动轨迹（相对运动线, RDRML）如果指向或通过屏幕中心（本船位置），说明有碰撞风险。如果运动线偏过中心，目标会从某一侧安全通过。

**目标类型和行为模式。** 商船通常走固定航线、速度恒定，行为可预测。渔船的运动模式不规则——可能突然转向、突然停船，威胁更难评估。帆船受风影响大，航向可能频繁改变。

Target Tracker 的设计目标就是将以上这些直觉判断全部量化。

### 4.2 船长优先关注的指标

按船长的注意力优先级排序：

1. **CPA < CPA_safe 的目标**：这些是"必须处理"的目标。
2. **TCPA < 15 分钟且 CPA 接近 CPA_safe 的目标**：这些是"即将需要处理"的目标。
3. **相对方位变化率接近零的目标**（"方位不变"）：这些是经典的碰撞风险指示器，即使当前 CPA 计算结果看似安全，也需要额外关注。
4. **航向或航速突然变化的目标**：行为不可预测的目标，威胁评估需要频繁更新。
5. **距离在快速缩小的目标**（range_rate 绝对值很大）：高速接近的目标，决策时间窗口短。

### 4.3 船长不做的事情

船长不会为每个目标做精确的数学计算——他依赖直觉和雷达画面的视觉模式。但 Target Tracker 作为自动化系统，必须为每个目标提供精确的数值计算结果。精确计算的优势是可以发现人眼可能遗漏的目标（比如在杂波中的小目标），劣势是可能被传感器噪声误导（人眼反而能过滤噪声）。因此需要平滑滤波机制。

---

## 5. 计算流程总览

Target Tracker 每个决策周期（2 秒）执行以下完整流程：

```
输入：Perception 融合目标列表 + 本船状态
          │
          ▼
    ┌──────────────────────────┐
    │ 步骤一：目标筛选与预处理    │
    │ 过滤非船舶目标、低置信度目标│
    │ 更新航迹生命周期状态        │
    └────────────┬─────────────┘
                 │
                 ▼
    ┌──────────────────────────┐
    │ 步骤二：相对运动计算        │
    │ 相对位置、相对速度、        │
    │ 相对方位、距离变化率        │
    └────────────┬─────────────┘
                 │
                 ▼
    ┌──────────────────────────┐
    │ 步骤三：CPA/TCPA 精确计算   │
    │ 含 DCPA（左右舷判定）      │
    └────────────┬─────────────┘
                 │
                 ▼
    ┌──────────────────────────┐
    │ 步骤四：BCR 船首穿越分析    │
    │ 目标是否穿越本船船首？      │
    │ 穿越时的距离和时间          │
    └────────────┬─────────────┘
                 │
                 ▼
    ┌──────────────────────────┐
    │ 步骤五：目标运动预测        │
    │ 1/3/5/10 分钟预测位置     │
    │ 含预测不确定性半径          │
    └────────────┬─────────────┘
                 │
                 ▼
    ┌──────────────────────────┐
    │ 步骤六：威胁评估与排序      │
    │ 多因子加权评分              │
    │ 等级分类 + 趋势判定         │
    │ 按 threat_score 降序排列   │
    └────────────┬─────────────┘
                 │
                 ▼
输出：ThreatList（威胁目标列表）
```

---

## 6. 步骤一：目标筛选与预处理

### 6.1 输入过滤

并非 Perception 输出的所有目标都需要进入避碰计算。以下类型的目标在 Target Tracker 中做不同处理：

```
FUNCTION filter_targets(perception_targets):
    
    active_targets = []
    
    FOR EACH target IN perception_targets:
        
        # 过滤 1：置信度过低
        IF target.confidence < CONFIDENCE_THRESHOLD:
            mark_as_uncertain(target)
            CONTINUE    # 暂不纳入威胁计算，但记录以防后续置信度上升
        
        # 过滤 2：非移动目标（浮标、渔网、漂流物）
        IF target.classification IN ["buoy", "fishing_net", "debris"]:
            IF target.sog < 0.5:    # 几乎静止
                mark_as_static_obstacle(target)
                CONTINUE    # 交给 L2/L4 处理，不纳入 L3 避碰
        
        # 过滤 3：数据过期
        IF (now - target.last_updated) > DATA_STALENESS_THRESHOLD:
            mark_as_stale(target)
            # 不跳过——过期数据仍然需要保守评估
        
        active_targets.append(target)
    
    RETURN active_targets

CONFIDENCE_THRESHOLD = 0.3          # 低于此值不计算（太不可靠）
DATA_STALENESS_THRESHOLD = 30 秒    # 超过此值标记为过期但仍计算
```

### 6.2 航迹生命周期管理

Target Tracker 维护一个内部的目标航迹表（Track Table），管理每个目标的生命周期：

```
FUNCTION update_track_lifecycle(track_table, perception_targets):
    
    # 1. 匹配：将 Perception 输出的目标与现有航迹关联
    FOR EACH perc_target IN perception_targets:
        matched_track = find_matching_track(track_table, perc_target.track_id)
        
        IF matched_track:
            # 更新现有航迹
            matched_track.update(perc_target)
            matched_track.data_age_sec = 0
            matched_track.track_status = "TRACKING"
        ELSE:
            # 新目标——创建新航迹
            new_track = create_track(perc_target)
            new_track.track_status = "NEW"
            new_track.track_age_sec = 0
            track_table.add(new_track)
    
    # 2. 老化：检查未更新的航迹
    FOR EACH track IN track_table:
        track.data_age_sec += dt    # dt = 决策周期（2秒）
        track.track_age_sec += dt
        
        IF track.data_age_sec > TARGET_LOST_TIMEOUT:
            track.track_status = "LOST"
        
        IF track.track_status == "LOST" AND track.data_age_sec > TARGET_DELETE_TIMEOUT:
            track_table.remove(track)
    
    # 3. 新目标确认
    FOR EACH track IN track_table:
        IF track.track_status == "NEW" AND track.track_age_sec > NEW_TARGET_CONFIRM_TIME:
            track.track_status = "TRACKING"
```

### 6.3 数据平滑

Perception 输出的目标位置和速度可能在帧间有跳变（特别是雷达跟踪目标）。Target Tracker 使用一阶指数平滑滤波器：

```
FUNCTION smooth_target_state(track, new_data, α):
    
    track.latitude  = α × new_data.latitude  + (1 - α) × track.latitude
    track.longitude = α × new_data.longitude + (1 - α) × track.longitude
    track.sog       = α × new_data.sog       + (1 - α) × track.sog
    track.cog       = circular_smooth(track.cog, new_data.cog, α)   # 角度平滑需要特殊处理
    
    # α 取值：
    # AIS 关联目标（数据准确）：α = 0.7（快速跟随）
    # 仅雷达目标（可能有噪声）：α = 0.3（强平滑）
    # 仅摄像头目标（精度较低）：α = 0.2（强平滑）

FUNCTION circular_smooth(old_angle, new_angle, α):
    # 角度平滑需要处理 0°/360° 跳变
    diff = normalize_angle(new_angle - old_angle)   # 归一化至 [-180, 180]
    RETURN normalize_angle(old_angle + α × diff)
```

---

## 7. 步骤二：相对运动计算

### 7.1 坐标转换

所有计算在本地 ENU（东-北-上）坐标系中进行。以本船当前位置为原点。

```
FUNCTION geo_to_enu(lat, lon, lat_ref, lon_ref):
    # 简化的平面近似（适用于距离 < 50 nm）
    R = 6371000    # 地球平均半径（米）
    x_east  = (lon - lon_ref) × R × cos(lat_ref × π/180) × π/180
    y_north = (lat - lat_ref) × R × π/180
    RETURN (x_east, y_north)
```

对于距离超过 50 nm 的目标，应使用 Vincenty 公式或 UTM 投影。但在避碰计算中，通常只关注 20 nm 以内的目标，平面近似足够。

### 7.2 相对位置

```
FUNCTION compute_relative_position(own_ship, target):
    
    # ENU 坐标中的相对位置
    (Δx, Δy) = geo_to_enu(target.lat, target.lon, own.lat, own.lon)
    
    # 距离
    range = sqrt(Δx² + Δy²)
    
    # 真方位（从正北顺时针）
    bearing_true = atan2(Δx, Δy) × 180/π
    IF bearing_true < 0: bearing_true += 360
    
    # 相对方位（从船首顺时针）
    bearing_relative = normalize_0_360(bearing_true - own.heading)
    
    RETURN {Δx, Δy, range, bearing_true, bearing_relative}
```

### 7.3 相对速度

```
FUNCTION compute_relative_velocity(own_ship, target):
    
    # 本船速度分量（ENU）
    own_vx = own.sog × sin(own.cog × π/180)    # 东向分量
    own_vy = own.sog × cos(own.cog × π/180)    # 北向分量
    
    # 目标速度分量（ENU）
    tgt_vx = target.sog × sin(target.cog × π/180)
    tgt_vy = target.sog × cos(target.cog × π/180)
    
    # 相对速度
    Δvx = tgt_vx - own_vx
    Δvy = tgt_vy - own_vy
    
    # 相对速度大小
    rel_speed = sqrt(Δvx² + Δvy²)
    
    # 相对运动线方向（RDRML 方向，真北基准）
    rel_course = atan2(Δvx, Δvy) × 180/π
    IF rel_course < 0: rel_course += 360
    
    RETURN {Δvx, Δvy, rel_speed, rel_course}
```

### 7.4 距离变化率（Range Rate）

```
FUNCTION compute_range_rate(Δx, Δy, Δvx, Δvy, range):
    
    IF range < 1.0:    # 避免除以零
        RETURN 0
    
    range_rate = (Δx × Δvx + Δy × Δvy) / range
    # 负值 = 距离在缩小（接近中）
    # 正值 = 距离在增大（远离中）
    
    RETURN range_rate
```

### 7.5 相对方位变化率

这是船长"方位不变"判断的量化指标：

```
FUNCTION compute_bearing_rate(track):
    
    IF track.has_previous_bearing:
        Δbearing = normalize_pm180(track.bearing_true - track.previous_bearing_true)
        Δt = track.time_since_last_update
        
        IF Δt > 0:
            bearing_rate = Δbearing / Δt    # 度/秒
        ELSE:
            bearing_rate = 0
    ELSE:
        bearing_rate = NaN
    
    # 保存当前值供下次使用
    track.previous_bearing_true = track.bearing_true
    
    RETURN bearing_rate
```

**方位变化率的意义：**

| bearing_rate | 含义 | 风险 |
|-------------|------|------|
| |rate| < 0.05°/s | 方位几乎不变 | **高碰撞风险**（方位不变距离近定律） |
| 0.05 ≤ |rate| < 0.2°/s | 方位缓慢变化 | 中等风险，需持续关注 |
| |rate| ≥ 0.2°/s | 方位明显变化 | 通常安全通过 |

---

## 8. 步骤三：CPA/TCPA 精确计算

### 8.1 基本公式推导

假设本船和目标船在当前时刻的相对位置为 (Δx, Δy)，相对速度为 (Δvx, Δvy)。如果双方都保持当前航向和航速不变，t 秒后的相对位置为：

```
Δx(t) = Δx + Δvx × t
Δy(t) = Δy + Δvy × t

距离的平方：
D²(t) = Δx(t)² + Δy(t)²
      = (Δx + Δvx×t)² + (Δy + Δvy×t)²
      = (Δvx² + Δvy²)×t² + 2×(Δx×Δvx + Δy×Δvy)×t + (Δx² + Δy²)

这是关于 t 的二次函数。最小值出现在：
dD²/dt = 0
2×(Δvx² + Δvy²)×t + 2×(Δx×Δvx + Δy×Δvy) = 0
```

### 8.2 TCPA 计算

```
FUNCTION compute_tcpa(Δx, Δy, Δvx, Δvy):
    
    dv_sq = Δvx² + Δvy²    # 相对速度的平方
    
    IF dv_sq < 1e-6:
        # 相对速度接近零——两船近似并行等速
        RETURN +∞    # CPA 永远不会到来（或已经是当前距离）
    
    TCPA = -(Δx × Δvx + Δy × Δvy) / dv_sq
    
    RETURN TCPA
    # TCPA > 0: CPA 在未来
    # TCPA < 0: CPA 已过（目标正在远离）
    # TCPA = 0: CPA 就是现在
```

### 8.3 CPA 计算

```
FUNCTION compute_cpa(Δx, Δy, Δvx, Δvy, TCPA):
    
    IF TCPA < 0:
        # CPA 已过，返回当前距离
        RETURN sqrt(Δx² + Δy²)
    
    # CPA 位置的相对坐标
    cpa_x = Δx + Δvx × TCPA
    cpa_y = Δy + Δvy × TCPA
    
    CPA = sqrt(cpa_x² + cpa_y²)
    
    RETURN CPA
```

### 8.4 DCPA 计算（左右舷判定）

DCPA 告诉你在 CPA 时刻，目标在本船的左舷还是右舷。这对 COLREGs Engine 的会遇分类至关重要。

```
FUNCTION compute_dcpa(Δx, Δy, Δvx, Δvy, TCPA, own_heading):
    
    # CPA 位置的相对坐标（ENU）
    cpa_x = Δx + Δvx × TCPA
    cpa_y = Δy + Δvy × TCPA
    
    # 转换到船体坐标系
    hdg_rad = own_heading × π/180
    cpa_along = cpa_x × sin(hdg_rad) + cpa_y × cos(hdg_rad)     # 沿航向分量
    cpa_cross = cpa_x × cos(hdg_rad) - cpa_y × sin(hdg_rad)     # 垂直航向分量
    
    DCPA = cpa_cross
    # DCPA > 0: 目标在右舷侧通过
    # DCPA < 0: 目标在左舷侧通过
    
    RETURN DCPA
```

### 8.5 CPA/TCPA 平滑滤波

原始的 CPA 和 TCPA 计算基于瞬时的位置和速度数据，可能因传感器噪声而在帧间跳变。特别是当两船近似平行航行时，微小的速度变化会导致 TCPA 从 +∞ 跳到 -∞。

```
FUNCTION smooth_cpa_tcpa(track, cpa_raw, tcpa_raw):
    
    τ = CPA_SMOOTH_TIME_CONSTANT    # 10 秒
    α = dt / (τ + dt)               # dt = 更新周期（2秒），α ≈ 0.167
    
    # CPA 用一阶低通滤波
    track.cpa_smoothed = α × cpa_raw + (1 - α) × track.cpa_smoothed
    
    # TCPA 的滤波需要特殊处理（可能出现符号翻转）
    IF sign(tcpa_raw) != sign(track.tcpa_smoothed) AND abs(tcpa_raw) > 60:
        # TCPA 符号翻转且值较大——可能是正常的从"接近"变为"远离"
        track.tcpa_smoothed = tcpa_raw    # 直接替换，不滤波
    ELSE:
        track.tcpa_smoothed = α × tcpa_raw + (1 - α) × track.tcpa_smoothed
    
    RETURN (track.cpa_smoothed, track.tcpa_smoothed)
```

### 8.6 CPA/TCPA 计算的边界情况

| 情况 | TCPA | CPA | 处理方式 |
|------|------|-----|---------|
| 目标正在远离 | < 0 | 当前距离 | threat_level 降为 LOW/NONE |
| 两船平行同向同速 | +∞ | 当前距离 | 视为安全，除非距离太近 |
| 两船平行同向不同速 | 很大 | 当前距离 | 如果速度差很小，TCPA 可能不稳定 |
| 目标静止（锚泊） | > 0 | 计算值 | 按静止目标处理，CPA = 通过距离 |
| 目标速度未知 | — | — | 假设目标以最危险的方向和速度运动 |

---

## 9. 步骤四：BCR 与船首穿越分析

### 9.1 BCR 的物理含义

BCR（Bow Crossing Range）是目标穿越本船船首正前方时与本船的距离。当一个目标从右舷接近并将要从本船船首前方通过时，BCR 告诉你"它通过我的船首时离我多远"。

如果 BCR 很小（比如 < 500m），即使 CPA 本身可能在安全范围内，但目标从我的船首前方很近处横穿，这让船长非常不舒服——因为如果目标突然减速或本船突然加速，就可能碰撞。

### 9.2 BCR 计算

```
FUNCTION compute_bcr(Δx, Δy, Δvx, Δvy, own_heading):
    
    # 将相对位置和速度转换到船体坐标系
    hdg_rad = own_heading × π/180
    
    # 沿航向的相对位置和速度
    along_x = Δx × sin(hdg_rad) + Δy × cos(hdg_rad)
    along_vx = Δvx × sin(hdg_rad) + Δvy × cos(hdg_rad)
    
    # 垂直航向的相对位置和速度
    cross_x = Δx × cos(hdg_rad) - Δy × sin(hdg_rad)
    cross_vx = Δvx × cos(hdg_rad) - Δvy × sin(hdg_rad)
    
    # 目标穿越船首正前方（along_x = 0）的时间
    IF abs(along_vx) < 0.01:
        # 目标不会穿越船首（沿航向分量的相对速度接近零）
        RETURN {crosses_bow: false, bcr: NaN, bcr_time: NaN}
    
    t_cross = -along_x / along_vx
    
    IF t_cross < 0:
        # 穿越时间为负——目标已经通过船首
        RETURN {crosses_bow: false, bcr: NaN, bcr_time: NaN}
    
    # 穿越时的横向距离
    bcr = abs(cross_x + cross_vx × t_cross)
    
    # 判断是否从船首前方通过（along_x > 0 表示目标在前方）
    crosses_bow = (along_x > 0 OR t_cross < 60)   # 目标在前方或即将到达前方
    
    RETURN {crosses_bow: crosses_bow, bcr: bcr, bcr_time: t_cross}
```

### 9.3 BCR 的安全标准

| BCR 值 | 风险等级 | 说明 |
|--------|---------|------|
| BCR < 0.5 × CPA_safe | CRITICAL | 目标极近距离穿越船首 |
| BCR < CPA_safe | HIGH | 穿越距离偏近 |
| BCR < 2 × CPA_safe | MEDIUM | 可接受但需监控 |
| BCR ≥ 2 × CPA_safe | LOW | 安全穿越 |

### 9.4 BCR 在避碰决策中的作用

COLREGs 第十五条（交叉相遇）规定让路船应避免从对方船首前方横越。这意味着如果我是让路船，我的避让动作不能导致我从对方船首前方近距离通过。BCR 正是用来检验这个约束的指标。

---

## 10. 步骤五：目标运动预测

### 10.1 匀速直线预测（默认模型）

```
FUNCTION predict_linear(target, t_predict):
    
    # 对地速度分量
    vx = target.sog × sin(target.cog × π/180)
    vy = target.sog × cos(target.cog × π/180)
    
    # 预测位置（ENU）
    pred_x = target.x + vx × t_predict
    pred_y = target.y + vy × t_predict
    
    # 转回经纬度
    pred_lat, pred_lon = enu_to_geo(pred_x, pred_y, ref_lat, ref_lon)
    
    # 预测不确定性
    σ = compute_uncertainty(target, t_predict)
    
    RETURN PredictedPosition(pred_lat, pred_lon, uncertainty_radius_m=σ)
```

### 10.2 转向预测（当 ROT 显著时）

```
FUNCTION predict_with_turn(target, t_predict):
    
    IF abs(target.rot) < ROT_THRESHOLD:    # ROT < 1°/min
        RETURN predict_linear(target, t_predict)
    
    # 圆弧预测
    ω = target.rot × π / (180 × 60)    # 转换为 rad/s
    R = target.sog / abs(ω)            # 旋回半径
    
    # 圆弧运动方程
    θ_0 = target.cog × π/180
    pred_x = target.x + R × (sin(θ_0 + ω × t_predict) - sin(θ_0))
    pred_y = target.y + R × (cos(θ_0) - cos(θ_0 + ω × t_predict))
    
    pred_lat, pred_lon = enu_to_geo(pred_x, pred_y, ref_lat, ref_lon)
    
    # 转向预测的不确定性更大
    σ = compute_uncertainty(target, t_predict) × 1.5
    
    RETURN PredictedPosition(pred_lat, pred_lon, uncertainty_radius_m=σ)

ROT_THRESHOLD = 1.0    # 度/分钟
```

### 10.3 预测不确定性计算

```
FUNCTION compute_uncertainty(target, t_predict):
    
    # 基础不确定性（来自传感器精度）
    σ_base = SIGMA_BASE    # 20m
    
    # 按数据来源调整
    IF "AIS" IN target.source_sensors:
        σ_base *= K_AIS_UNCERTAINTY    # × 0.5（AIS 定位较准确）
    ELIF only "RADAR" IN target.source_sensors:
        σ_base *= K_RADAR_ONLY_UNCERTAINTY    # × 1.5（仅雷达，方位精度有限）
    
    # 随时间增长
    σ_growth = K_UNCERTAINTY × target.sog × t_predict
    # 每秒增长速度的 5%
    
    # 如果目标有 ROT，不确定性增长更快
    IF abs(target.rot) > ROT_THRESHOLD:
        σ_growth *= 2.0
    
    # 如果数据已过期，不确定性进一步增大
    IF target.data_age_sec > 10:
        σ_growth += target.sog × target.data_age_sec × 0.10
    
    σ_total = sqrt(σ_base² + σ_growth²)
    
    RETURN σ_total
```

### 10.4 多预测时间点

Target Tracker 为每个目标生成 4 个标准预测时间点的位置：

| 预测时间 | 用途 |
|---------|------|
| 1 分钟 | 短期态势评估，用于紧急避碰判断 |
| 3 分钟 | 中期态势评估，用于标准避碰决策 |
| 5 分钟 | 让路船行动规划的参考时间窗口 |
| 10 分钟 | 长期态势监控，早期预警 |

---

## 11. 步骤六：威胁评估与优先级排序

### 11.1 威胁评分函数

威胁评分是一个多因子加权评分模型：

```
threat_score = w_cpa × f_cpa(CPA)
             + w_tcpa × f_tcpa(TCPA)
             + w_range × f_range(range)
             + w_aspect × f_aspect(bearing_relative)
```

### 11.2 各因子评分函数

**f_cpa(CPA)——CPA 评分：**

```
FUNCTION f_cpa(cpa, cpa_safe):
    
    IF cpa ≤ 0:
        RETURN 100    # CPA = 0 意味着碰撞
    ELIF cpa < cpa_safe × 0.5:
        RETURN 100    # 远小于安全距离——极度危险
    ELIF cpa < cpa_safe:
        # 在 0.5×CPA_safe ~ CPA_safe 之间线性从 100 降到 70
        RETURN 100 - 30 × (cpa - 0.5 × cpa_safe) / (0.5 × cpa_safe)
    ELIF cpa < cpa_safe × 2:
        # 在 CPA_safe ~ 2×CPA_safe 之间线性从 70 降到 20
        RETURN 70 - 50 × (cpa - cpa_safe) / cpa_safe
    ELIF cpa < cpa_safe × 3:
        # 在 2×CPA_safe ~ 3×CPA_safe 之间线性从 20 降到 0
        RETURN 20 - 20 × (cpa - 2 × cpa_safe) / cpa_safe
    ELSE:
        RETURN 0    # CPA 远大于安全距离
```

**f_tcpa(TCPA)——TCPA 评分：**

```
FUNCTION f_tcpa(tcpa):
    
    IF tcpa < 0:
        RETURN 0     # CPA 已过，目标正在远离
    ELIF tcpa < 2 × 60:      # < 2 分钟
        RETURN 100
    ELIF tcpa < 5 × 60:      # < 5 分钟
        RETURN 100 - 30 × (tcpa - 120) / 180    # 100→70
    ELIF tcpa < 10 × 60:     # < 10 分钟
        RETURN 70 - 40 × (tcpa - 300) / 300     # 70→30
    ELIF tcpa < 20 × 60:     # < 20 分钟
        RETURN 30 - 30 × (tcpa - 600) / 600     # 30→0
    ELSE:
        RETURN 0
```

**f_range(range)——距离评分：**

```
FUNCTION f_range(range):
    
    range_nm = range / 1852
    
    IF range_nm < 0.5:
        RETURN 100
    ELIF range_nm < 1.0:
        RETURN 100 - 40 × (range_nm - 0.5) / 0.5    # 100→60
    ELIF range_nm < 3.0:
        RETURN 60 - 40 × (range_nm - 1.0) / 2.0     # 60→20
    ELIF range_nm < 6.0:
        RETURN 20 - 20 × (range_nm - 3.0) / 3.0     # 20→0
    ELSE:
        RETURN 0
```

**f_aspect(bearing_relative)——方位评分：**

```
FUNCTION f_aspect(bearing_relative):
    
    # 正前方（0°）来船最危险，正后方（180°）最不危险
    # 使用余弦函数：前方 = 100，后方 = 0
    
    bearing_rad = bearing_relative × π / 180
    score = 50 × (1 + cos(bearing_rad))    # 范围 [0, 100]
    
    RETURN score
```

### 11.3 修正因子

基础评分之上叠加修正因子：

```
FUNCTION apply_modifiers(base_score, track):
    
    score = base_score
    
    # 修正 1：方位变化率接近零 → 增大威胁
    IF abs(track.bearing_rate) < 0.05 AND track.range_rate < 0:
        score = min(100, score × 1.2)    # 增大 20%
    
    # 修正 2：目标行为不可预测（COG/SOG 突然变化）→ 增大威胁
    IF track.behavior_unpredictable:
        score = min(100, score × 1.15)   # 增大 15%
    
    # 修正 3：数据过期或置信度低 → 增大威胁（保守处理）
    IF track.data_age_sec > 15 OR track.confidence < 0.5:
        score = min(100, score × 1.10)   # 增大 10%
    
    # 修正 4：目标尺寸大（大船操纵性差，碰撞后果严重）→ 增大威胁
    IF track.length > 100:    # 大型船舶
        score = min(100, score × 1.10)
    
    # 修正 5：能见度不良 → 增大威胁
    IF visibility < 2.0:
        score = min(100, score × 1.20)
    
    RETURN score
```

### 11.4 威胁等级分类

```
FUNCTION classify_threat(score):
    IF score >= 80: RETURN "CRITICAL"
    IF score >= 60: RETURN "HIGH"
    IF score >= 40: RETURN "MEDIUM"
    IF score >= 20: RETURN "LOW"
    RETURN "NONE"
```

### 11.5 威胁趋势判定

```
FUNCTION determine_trend(track):
    
    # 比较当前评分与最近 30 秒内的历史评分
    recent_scores = track.score_history[-15:]    # 最近 15 个周期（30 秒）
    
    IF len(recent_scores) < 5:
        RETURN "STABLE"
    
    avg_old = mean(recent_scores[:5])
    avg_new = mean(recent_scores[-5:])
    
    IF avg_new > avg_old + 5:
        RETURN "INCREASING"    # 威胁在增大
    ELIF avg_new < avg_old - 5:
        RETURN "DECREASING"    # 威胁在减小
    ELSE:
        RETURN "STABLE"
```

### 11.6 排序输出

```
FUNCTION sort_and_output(tracks):
    
    # 按 threat_score 降序排列
    tracks.sort(key=lambda t: -t.threat_score)
    
    # 构建 ThreatList 消息
    threat_list = ThreatList()
    threat_list.total_tracked = len(tracks)
    threat_list.threat_count = sum(1 for t in tracks if t.threat_level >= "MEDIUM")
    threat_list.targets = tracks
    
    RETURN threat_list
```

---

## 12. 目标状态管理与航迹维护

### 12.1 航迹表数据结构

```
struct TrackEntry {
    # ---- 从 Perception 获取的原始数据 ----
    uint32 track_id;
    uint32 mmsi;
    string classification;
    float64 confidence;
    float64 latitude, longitude;
    float64 cog, sog, heading;
    float64 length, beam;
    uint8[] source_sensors;
    
    # ---- Target Tracker 计算的派生数据 ----
    float64 range, bearing_true, bearing_relative;
    float64 rel_speed, rel_course, range_rate;
    float64 bearing_rate;
    float64 cpa, tcpa, dcpa;
    float64 bcr, bcr_time;
    bool crosses_bow;
    PredictedPosition pred_1min, pred_3min, pred_5min, pred_10min;
    float64 threat_score;
    string threat_level;
    string threat_trend;
    
    # ---- 平滑滤波状态 ----
    float64 cpa_smoothed, tcpa_smoothed;
    float64 previous_bearing_true;
    deque<float64> score_history;       # 最近 30 秒的评分历史
    
    # ---- 航迹生命周期状态 ----
    string track_status;                # "NEW" / "TRACKING" / "LOST"
    float64 track_age_sec;
    float64 data_age_sec;
    Time first_detected;
    Time last_updated;
};
```

### 12.2 LOST 目标的保守处理

当一个目标进入 LOST 状态（超过 60 秒无更新），Target Tracker 不会立即删除它。而是：

```
IF track.track_status == "LOST":
    # 继续用最后已知的速度和航向做线性预测
    track.latitude += track.vx × dt / R_earth × 180/π
    track.longitude += track.vy × dt / (R_earth × cos(track.latitude × π/180)) × 180/π
    
    # 但大幅增加不确定性
    track.uncertainty *= 1.5    # 每个周期增加 50%
    
    # 威胁评分不降低——保守原则
    # 如果目标在丢失前是 HIGH 威胁，在恢复跟踪或确认安全之前保持 HIGH
    track.threat_score = max(track.threat_score, track.threat_score_at_loss × 0.9)
```

### 12.3 行为异常检测

```
FUNCTION detect_anomalous_behavior(track, new_data):
    
    # 航向突变：如果航向变化 > 30° 在一个更新周期内
    heading_change = abs(normalize_pm180(new_data.cog - track.cog))
    IF heading_change > 30:
        track.behavior_unpredictable = true
        track.anomaly_type = "sudden_turn"
    
    # 速度突变：加速或减速 > 50%
    speed_change_ratio = abs(new_data.sog - track.sog) / max(track.sog, 0.1)
    IF speed_change_ratio > 0.5:
        track.behavior_unpredictable = true
        track.anomaly_type = "sudden_speed_change"
    
    # 如果连续 30 秒无异常行为，清除标记
    IF track.time_since_last_anomaly > 30:
        track.behavior_unpredictable = false
```

---

## 13. 传感器不一致处理

### 13.1 AIS 与雷达数据冲突

Perception 融合子系统通常已经处理了传感器关联，但 Target Tracker 仍需关注以下不一致情况：

```
# AIS 报告目标速度 12 节，但雷达跟踪显示速度仅 8 节
IF abs(ais_sog - radar_sog) > 2.0 m/s:
    # 可能原因：AIS 信息更新延迟、GPS 故障、AIS 欺骗
    track.confidence *= 0.7    # 降低置信度
    track.sog = min(ais_sog, radar_sog)    # 取保守值（按更危险的情况处理）

# AIS 报告航向 090°，但雷达跟踪显示航向 120°
IF abs(normalize_pm180(ais_cog - radar_cog)) > 15°:
    track.confidence *= 0.7
    # 使用雷达数据（雷达是实测运动，AIS 可能延迟或 GPS 漂移）
    track.cog = radar_cog
```

### 13.2 AIS 欺骗检测

```
FUNCTION detect_ais_spoofing(track):
    
    # 指标 1：AIS 位置与雷达位置严重偏离
    IF distance(ais_position, radar_position) > 1000m:
        RETURN {spoofing_suspected: true, confidence_reduction: 0.5}
    
    # 指标 2：多个不同目标使用相同 MMSI
    IF count_tracks_with_mmsi(track.mmsi) > 1:
        RETURN {spoofing_suspected: true, type: "duplicate_mmsi"}
    
    # 指标 3：AIS 报告的船舶尺寸与雷达回波不匹配
    IF ais_length > 200 AND radar_rcs_small:
        RETURN {spoofing_suspected: true, type: "size_mismatch"}
    
    RETURN {spoofing_suspected: false}
```

---

## 14. 坐标系与单位规范

### 14.1 坐标系定义

| 坐标系 | 原点 | X 方向 | Y 方向 | 用途 |
|-------|------|--------|--------|------|
| WGS84 (EPSG:4326) | — | 经度（东正） | 纬度（北正） | 输入/输出地理位置 |
| ENU（本地切平面） | 本船当前位置 | 东 | 北 | 内部计算 |
| 船体坐标系 | 本船重心 | 船首方向 | 左舷方向 | BCR/DCPA 计算 |

### 14.2 单位规范

| 物理量 | 内部计算单位 | 输出单位 | 说明 |
|-------|------------|---------|------|
| 位置 | 米（ENU 坐标） | 度（经纬度） | 内部用 ENU，输出转回经纬度 |
| 距离 | 米 | 米 | CPA、BCR、range 均为米 |
| 速度 | m/s | m/s | SOG、相对速度 |
| 航向/方位 | 度（真北，[0, 360)） | 度 | COG、bearing |
| 角速度 | 度/秒 | 度/分钟（ROT） | 内部用度/秒，ROT 输出用度/分钟 |
| 时间 | 秒 | 秒 | TCPA、BCR_time |

### 14.3 角度归一化函数

```
FUNCTION normalize_0_360(angle):
    WHILE angle < 0: angle += 360
    WHILE angle >= 360: angle -= 360
    RETURN angle

FUNCTION normalize_pm180(angle):
    WHILE angle < -180: angle += 360
    WHILE angle >= 180: angle -= 360
    RETURN angle
```

---

## 15. 特殊目标处理

### 15.1 静止目标（锚泊船）

```
IF target.sog < 0.5 m/s AND target.nav_status == "at_anchor":
    # 锚泊船不会主动避让，视为固定障碍物
    # CPA 简化为：本船通过该位置时的最近距离
    # TCPA = 本船到达最近点的时间
    
    CPA = perpendicular_distance(own_track, target.position)
    TCPA = along_track_distance(own_position, closest_point) / own_speed
```

### 15.2 高速目标（快艇、气垫船）

```
IF target.sog > 15 m/s (约 30 节):
    # 高速目标的决策时间窗口极短
    # 增大威胁评分权重
    score_modifier *= 1.3
    
    # 增大预测不确定性（高速目标的航向变化影响更大）
    uncertainty_modifier *= 1.5
    
    # 缩短行动建议时间
    action_deadline_modifier *= 0.5
```

### 15.3 渔船群

```
IF multiple targets classified as "fishing" within 1nm radius:
    # 渔船群视为一个整体障碍区域
    # 计算群体的包围盒或凸包
    # CPA 按整个群体的边界计算
    
    fishing_fleet_boundary = compute_convex_hull(fishing_targets)
    CPA = distance_to_polygon(own_track, fishing_fleet_boundary)
```

### 15.4 AIS B 类目标（小型船舶）

AIS B 类设备通常安装在小型游艇和渔船上，报告间隔更长（30 秒~3 分钟），位置精度更低。

```
IF target.ais_class == "B":
    confidence *= 0.8
    uncertainty *= 1.3
```

### 15.5 无 AIS 信号的雷达目标

```
IF target.mmsi == 0 AND target.source_sensors == [RADAR]:
    # 可能是小型船舶（未安装 AIS）或故意关闭 AIS 的船舶
    # 按最保守方式处理
    
    IF target.length 未知:
        target.assumed_length = 50m    # 假设中等大小船舶
    
    confidence *= 0.7
    uncertainty *= 1.5
    
    # 如果在密集交通区，可能需要提高威胁评分
    IF zone_type IN ["tss_lane", "narrow_channel"]:
        score_modifier *= 1.2
```

---

## 16. 性能要求

| 指标 | 要求 | 说明 |
|------|------|------|
| 全部目标计算时延 | < 100 ms（100 个目标） | 每个决策周期内完成 |
| 单目标 CPA/TCPA 计算 | < 0.5 ms | 核心计算路径 |
| 单目标运动预测 | < 0.2 ms（匀速线性）/ < 1 ms（圆弧） | |
| 内存占用 | < 50 MB（100 个目标） | 含历史数据 |
| 最大同时跟踪目标数 | 100 | 超过时按威胁排序丢弃最低优先级 |

---

## 17. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Track Manager | 航迹生命周期管理（创建/更新/丢失/删除） | C++ |
| Data Smoother | 输入数据平滑滤波 | C++ |
| Relative Motion Calc | 相对位置/速度/方位计算 | C++ |
| CPA/TCPA Engine | CPA/TCPA/DCPA 计算 + 平滑 | C++ |
| BCR Calculator | 船首穿越分析 | C++ |
| Motion Predictor | 目标运动预测（线性/圆弧）+ 不确定性 | C++ |
| Threat Assessor | 威胁评分 + 修正 + 等级分类 + 趋势 | C++ |
| Anomaly Detector | 行为异常检测 + AIS 欺骗检测 | C++ |

---

## 18. 数值算例

### 18.1 算例一：典型交叉相遇

**条件：**

```
本船：位置 (0, 0), 航向 000°（正北）, 速度 8 m/s
目标：位置 (3000, 2000) [东 3000m, 北 2000m], 航向 270°（正西）, 速度 6 m/s
```

**计算：**

```
相对位置：Δx = 3000, Δy = 2000
距离：range = sqrt(3000² + 2000²) = 3606 m

真方位：bearing_true = atan2(3000, 2000) × 180/π = 56.3°
相对方位：bearing_relative = 56.3° - 0° = 56.3°（右舷前方）

本船速度分量：own_vx = 8 × sin(0) = 0, own_vy = 8 × cos(0) = 8
目标速度分量：tgt_vx = 6 × sin(270° × π/180) = -6, tgt_vy = 6 × cos(270° × π/180) = 0

相对速度：Δvx = -6 - 0 = -6, Δvy = 0 - 8 = -8
相对速度大小：rel_speed = sqrt(36 + 64) = 10 m/s

TCPA = -(3000×(-6) + 2000×(-8)) / (36 + 64) = -(-18000 - 16000) / 100 = 34000/100 = 340 秒（5.67 分钟）

CPA 位置：cpa_x = 3000 + (-6)×340 = 3000 - 2040 = 960
           cpa_y = 2000 + (-8)×340 = 2000 - 2720 = -720
CPA = sqrt(960² + 720²) = sqrt(921600 + 518400) = sqrt(1440000) = 1200 m

DCPA（船体坐标系，航向 000°）：
  cpa_cross = 960 × cos(0) - (-720) × sin(0) = 960（右舷）
  → 目标从右舷侧通过

distance: range = 3606 m = 1.95 nm
```

**威胁评估（CPA_safe = 1852m，开阔水域）：**

```
f_cpa(1200) = 70 - 50 × (1200 - 1852) / 1852 ← CPA < CPA_safe
  实际 CPA = 1200 < CPA_safe = 1852，在 0.5×CPA ~ CPA 区间
  f_cpa = 100 - 30 × (1200 - 926) / 926 = 100 - 30 × 0.296 = 91.1

f_tcpa(340) = 在 5~10 分钟区间
  f_tcpa = 70 - 40 × (340 - 300) / 300 = 70 - 5.3 = 64.7

f_range(3606m = 1.95nm) = 在 1~3 nm 区间
  f_range = 60 - 40 × (1.95 - 1.0) / 2.0 = 60 - 19 = 41.0

f_aspect(56.3°) = 50 × (1 + cos(56.3° × π/180)) = 50 × (1 + 0.555) = 77.8

threat_score = 0.40×91.1 + 0.30×64.7 + 0.20×41.0 + 0.10×77.8
             = 36.4 + 19.4 + 8.2 + 7.8 = 71.8

threat_level = "HIGH"（在 60~80 区间）
```

**结论：** 目标从右舷前方 56° 方位接近，CPA 1200m 小于安全距离 1852m，TCPA 约 5.7 分钟。威胁等级 HIGH，COLREGs Engine 需要介入——这是一个交叉相遇态势，目标在我右舷，我是让路船。

### 18.2 算例二：对遇

```
本船：(0, 0), 航向 000°, 速度 8 m/s
目标：(100, 8000), 航向 182°, 速度 6 m/s

相对方位：atan2(100, 8000) = 0.7°（几乎正前方）
航向差：182° - 0° = 182°（几乎正对相向）

TCPA = -(100×(-0.21) + 8000×(-13.98)) / (0.21² + 13.98²)
     ≈ 112000/195.7 ≈ 572 秒（9.5 分钟）

CPA ≈ 93 m（极小，几乎碰撞）

threat_score ≈ 95+ → CRITICAL
→ 对遇态势，双方应向右转
```

---

## 19. 参数总览表

| 类别 | 参数 | 默认值 | 可配置 |
|------|------|-------|--------|
| **安全距离** | CPA_safe（开阔水域） | 1852 m (1 nm) | 是 |
| | CPA_safe（沿岸） | 926 m (0.5 nm) | 是 |
| | CPA_safe（狭水道） | 556 m (0.3 nm) | 是 |
| | CPA_safe（港内） | 370 m (0.2 nm) | 是 |
| | 能见度不良增大系数 | ×1.5 | 是 |
| **威胁评估** | w_cpa | 0.40 | 是 |
| | w_tcpa | 0.30 | 是 |
| | w_range | 0.20 | 是 |
| | w_aspect | 0.10 | 是 |
| | CRITICAL 阈值 | ≥ 80 | 是 |
| | HIGH 阈值 | ≥ 60 | 是 |
| | MEDIUM 阈值 | ≥ 40 | 是 |
| **航迹管理** | 置信度过滤阈值 | 0.3 | 是 |
| | 新目标确认时间 | 30 秒 | 是 |
| | 目标丢失超时 | 60 秒 | 是 |
| | 目标删除超时 | 180 秒 | 是 |
| | CPA/TCPA 平滑时间常数 | 10 秒 | 是 |
| | 最大跟踪目标数 | 100 | 是 |
| **预测** | 基础位置不确定性 σ_base | 20 m | 是 |
| | 不确定性增长率 | 0.05 × V_target | 是 |
| | AIS 不确定性折减 | ×0.5 | 是 |
| | 仅雷达不确定性增大 | ×1.5 | 是 |
| | ROT 阈值 | 1.0 °/min | 是 |
| **数据平滑** | AIS 目标 α | 0.7 | 是 |
| | 仅雷达目标 α | 0.3 | 是 |
| | 仅摄像头目标 α | 0.2 | 是 |

---

## 20. 与其他模块的协作关系

| 交互方 | 方向 | 数据内容 | 频率 |
|-------|------|---------|------|
| Perception → Target Tracker | 输入 | TrackedTargetArray（融合目标列表） | 2 Hz |
| Perception → Target Tracker | 输入 | EnvironmentState（能见度、水域类型） | 0.2 Hz |
| Nav Filter → Target Tracker | 输入 | 本船位置/速度/航向 | 50 Hz |
| Target Tracker → COLREGs Engine | 输出 | ThreatList（威胁目标列表，按评分排序） | 2 Hz |
| Target Tracker → Shore Link | 输出 | 态势报告（供岸基远程监控） | 1 Hz |
| Target Tracker → Risk Monitor | 输出 | 航迹状态变化通知（目标丢失/异常行为） | 事件驱动 |
| ENC Reader → Target Tracker | 查询 | ZoneClassification（获取当前水域的 CPA_safe） | 按需 |

---

## 21. 测试策略与验收标准

### 21.1 测试场景

| 场景编号 | 描述 | 验证目标 |
|---------|------|---------|
| TGT-001 | 单目标匀速直线接近 | CPA/TCPA 计算精度 |
| TGT-002 | 单目标匀速直线远离 | TCPA < 0 正确处理 |
| TGT-003 | 两船平行同向同速 | TCPA = ∞ 正确处理 |
| TGT-004 | 对遇态势（正前方） | CPA ≈ 0, 高威胁评分 |
| TGT-005 | 交叉相遇（右舷 60°） | DCPA 正值（右舷通过） |
| TGT-006 | 交叉相遇（左舷 300°） | DCPA 负值（左舷通过） |
| TGT-007 | 目标穿越船首 | BCR 正确计算 |
| TGT-008 | 目标不穿越船首 | crosses_bow = false |
| TGT-009 | 目标突然转向 | 异常检测 + 预测更新 |
| TGT-010 | 目标信号丢失 60 秒 | LOST 状态 + 保守预测 |
| TGT-011 | 目标信号恢复 | 从 LOST 恢复到 TRACKING |
| TGT-012 | 100 个同时目标 | 性能 < 100ms |
| TGT-013 | 低置信度目标 | 正确过滤但记录 |
| TGT-014 | 静止目标（锚泊船） | 按固定障碍物处理 |
| TGT-015 | 高速目标（30+ 节） | 增大威胁评分 |
| TGT-016 | 渔船群（5 艘聚集） | 群体边界 CPA 计算 |
| TGT-017 | AIS 与雷达数据冲突 | 保守值取用 + 降低置信度 |
| TGT-018 | AIS 欺骗（重复 MMSI） | 检测并标记 |
| TGT-019 | 能见度不良（< 2nm） | CPA_safe 增大 50% |
| TGT-020 | 方位不变距离近 | bearing_rate ≈ 0 修正生效 |

### 21.2 验收标准

| 指标 | 标准 |
|------|------|
| CPA 计算精度 | 与理论值误差 < 1%（匀速直线目标） |
| TCPA 计算精度 | 与理论值误差 < 1%（匀速直线目标） |
| BCR 计算精度 | 与理论值误差 < 5% |
| 威胁等级分类 | 与人工判断一致率 > 90%（对照 20 个标准场景） |
| CPA/TCPA 平滑后稳定性 | 连续帧间变化 < 5%（稳态目标） |
| 异常行为检测率 | 航向突变 > 30° 的检出率 100% |
| LOST 目标保守处理 | 威胁评分不低于丢失前的 90% |
| 全量计算时延（100 目标） | < 100 ms |
| 内存占用（100 目标） | < 50 MB |

---

## 22. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | IMO COLREGs (COLREG 1972, as amended) | 国际海上避碰规则 |
| [2] | Lenart, A.S. "Collision Threat Parameters for a New Radar Display" JSN, 1983 | CPA/TCPA 计算理论 |
| [3] | Szlapczynski, R. "A Unified Measure of Collision Risk" JNAOE, 2006 | 威胁评分模型 |
| [4] | IMO Resolution A.823(19) | 雷达目标跟踪性能标准 |
| [5] | IMO MSC.192(79) | 综合导航系统性能标准 |
| [6] | ITU-R M.1371 | AIS 技术标准（报文格式、更新率） |
| [7] | Fossen, T.I. "Handbook of Marine Craft Hydrodynamics" 2021 | 坐标系与运动学 |
| [8] | Bar-Shalom, Y. "Estimation with Applications to Tracking and Navigation" 2001 | 目标跟踪理论 |

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-25 | 架构组 | 初始版本 |

---

**文档结束**
