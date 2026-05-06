# MASS_ADAS Multi-Sensor Fusion 多传感器融合模块技术设计文档

**文档编号：** SANGO-ADAS-MSF-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的信息融合思维模型
5. 计算流程总览
6. 步骤一：目标关联（Association）
7. 步骤二：航迹初始化与管理
8. 步骤三：状态估计与预测（卡尔曼滤波）
9. 步骤四：传感器数据融合更新
10. 步骤五：分类信息融合
11. 步骤六：航迹质量评估与维护
12. AIS-雷达关联详解
13. 摄像头-雷达关联详解
14. 三传感器联合关联
15. 航迹分裂与合并处理
16. 假目标抑制与确认逻辑
17. 融合输出——统一目标列表
18. 内部子模块分解
19. 数值算例
20. 参数总览表
21. 与其他模块的协作关系
22. 测试策略与验收标准
23. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Multi-Sensor Fusion（多传感器融合）是 Perception 子系统的核心模块。它接收 Object Detection 模块从雷达、摄像头、LiDAR 各自独立检测到的目标，以及 Data Preprocessing 提供的 AIS 目标，将来自不同传感器的同一物理目标的观测数据关联在一起，使用卡尔曼滤波器融合为一个最优的目标状态估计（位置、速度、航向），并维护一个统一的目标航迹列表。

这个模块对应的是经验丰富的航海人员在脑中自动完成的"信息整合"——他看到雷达上 3 海里处有个目标、AIS 显示那个位置有一艘 MMSI 为 123456789 的货船、目视看到远处有一个黑点——他在脑中自动将这三个信息关联为同一个目标，并综合三者的信息形成对该目标最完整的认知。

### 1.2 核心职责

- **目标关联（Association）：** 判定来自不同传感器的哪些检测对应同一个物理目标。
- **航迹初始化：** 当一个新目标首次被检测到时，创建一个新的融合航迹。
- **状态融合：** 使用卡尔曼滤波器或扩展卡尔曼滤波器融合多个传感器的测量值，输出最优状态估计。
- **分类融合：** 综合多个传感器的分类结果，输出最可靠的目标类型判定。
- **航迹维护：** 管理航迹的生命周期——确认、跟踪、丢失、删除。

### 1.3 设计原则

- **信息互补原则：** 每个传感器有其优势——雷达精确测距、AIS 提供身份、摄像头提供分类。融合的目标是让各传感器扬长避短。
- **鲁棒关联原则：** 关联算法必须对传感器噪声和时间偏差鲁棒——不能因为雷达和 AIS 的位置差了 100 米就认为是两个目标。
- **航迹连续性原则：** 融合后的目标航迹应在时间上保持连续——即使某个传感器暂时丢失目标（如摄像头被波浪遮挡），只要其他传感器仍在跟踪，航迹就不应中断。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 频率 | 说明 |
|-------|------|------|------|
| 雷达检测目标列表 | Object Detection (Radar) | 2~4 Hz | RadarDetectedObjectArray |
| 摄像头检测目标列表 | Object Detection (Camera) | 10~30 Hz | CameraDetectedObjectArray |
| LiDAR 检测目标列表 | Object Detection (LiDAR) | 10~20 Hz | LidarDetectedObjectArray |
| AIS 目标列表 | Data Preprocessing | 按接收频率 | AisTargetArray |
| 本船状态 | Data Preprocessing | 50 Hz | OwnShipState |

### 2.2 输出

通过 `/perception/targets` 话题发布 TrackedTargetArray（参见话题接口规范文档 7.1 节）。

```
TrackedTargetArray:
    Header header
    TrackedTarget[] targets
    uint32 total_tracked              # 当前跟踪的总目标数
    uint32 confirmed_count            # 已确认的目标数

TrackedTarget:
    uint32 track_id                   # 融合后的唯一跟踪 ID
    uint32 mmsi                       # AIS MMSI（0 = 未关联 AIS）
    string classification             # 融合后的最佳分类
    float64 confidence                # 融合置信度 [0,1]
    
    # 融合后的状态估计（最优估计）
    float64 latitude, longitude       # WGS84
    float64 cog                       # 对地航向（度）
    float64 sog                       # 对地速度（m/s）
    float64 heading                   # 真航向（度，NaN=未知）
    float64 rot                       # 转向率（度/分钟，NaN=未知）
    float64 length, beam              # 尺度（米，NaN=未知）
    
    # 状态估计不确定性
    float64[4] position_covariance    # 位置协方差 [σ_xx, σ_xy, σ_yx, σ_yy]
    float64[4] velocity_covariance    # 速度协方差
    
    # 数据来源
    uint8[] source_sensors            # 数据来源传感器列表
    Time first_detected               # 首次检测时间
    Time last_updated                 # 最后更新时间
    string track_status               # "tentative"/"confirmed"/"lost"
    uint32 update_count               # 总更新次数
```

---

## 3. 核心参数数据库

### 3.1 关联参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 雷达-AIS 关联距离阈值 | 500 m | 超过此距离不关联 |
| 雷达-AIS 关联速度阈值 | 3.0 m/s | 速度差超过此值降低关联置信度 |
| 摄像头-雷达关联方位阈值 | 5° | 方位差超过此值不关联 |
| 摄像头-雷达关联距离阈值（如有距离） | 200 m | |
| LiDAR-雷达关联距离阈值 | 100 m | |
| 关联置信度最低要求 | 0.5 | 低于此不关联 |

### 3.2 卡尔曼滤波参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 过程噪声（位置） | σ_q_pos = 1.0 m/s² | 目标加速度不确定性 |
| 过程噪声（速度） | σ_q_vel = 0.5 m/s² | |
| 雷达测量噪声（距离） | σ_r_range = 20 m | |
| 雷达测量噪声（方位） | σ_r_bearing = 1.0° | |
| AIS 测量噪声（位置） | σ_ais_pos = 30 m | GPS 精度 + 延迟 |
| AIS 测量噪声（速度） | σ_ais_vel = 0.5 m/s | |
| 摄像头测量噪声（方位） | σ_cam_bearing = 2.0° | |
| LiDAR 测量噪声（距离） | σ_lidar_range = 0.5 m | |
| LiDAR 测量噪声（方位） | σ_lidar_bearing = 0.5° | |

### 3.3 航迹管理参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 确认所需检测次数 | 3/5（3 out of 5 scans） | M/N 逻辑 |
| 航迹丢失超时 | 30 秒 | 无更新超过此时间标记为 lost |
| 航迹删除超时 | 120 秒 | lost 状态超过此时间删除 |
| 最大同时航迹数 | 200 | |

---

## 4. 船长的信息融合思维模型

船长在脑中做的融合过程：

**第一步——关联：** "雷达上这个目标和 AIS 上的那艘船是同一个吗？" 他的判断依据是位置是否接近、运动方向是否一致、大小是否匹配。

**第二步——取精华：** 关联后他取各源的最佳信息——位置用雷达的（实时性好），身份用 AIS 的（MMSI、船名），类型用目视判断（最可靠），速度用 AIS 的（精度高）。

**第三步——填空白：** 如果雷达看到一个目标但 AIS 上没有对应的——可能是小船没装 AIS，或者关闭了 AIS。船长会标记这是一个"未识别目标"，提高警惕。

---

## 5. 计算流程总览

```
各传感器检测列表
    │
    ├── 雷达目标 ───┐
    ├── AIS 目标  ──┤
    ├── 摄像头目标 ──┤
    └── LiDAR 目标 ─┤
                     ▼
    ┌────────────────────────────┐
    │ 步骤一：目标关联             │
    │ 计算关联矩阵 → 匈牙利算法   │
    └────────────┬───────────────┘
                 │
                 ▼
    ┌────────────────────────────┐
    │ 步骤二：航迹初始化/匹配      │
    │ 新目标 → 创建航迹            │
    │ 已知目标 → 更新航迹          │
    └────────────┬───────────────┘
                 │
                 ▼
    ┌────────────────────────────┐
    │ 步骤三：卡尔曼预测步         │
    │ 用运动模型预测到当前时刻      │
    └────────────┬───────────────┘
                 │
                 ▼
    ┌────────────────────────────┐
    │ 步骤四：卡尔曼更新步         │
    │ 用测量数据修正预测           │
    └────────────┬───────────────┘
                 │
                 ▼
    ┌────────────────────────────┐
    │ 步骤五：分类信息融合         │
    │ 多源分类结果投票/加权         │
    └────────────┬───────────────┘
                 │
                 ▼
    ┌────────────────────────────┐
    │ 步骤六：航迹质量评估         │
    │ 确认/丢失/删除               │
    └────────────┬───────────────┘
                 │
                 ▼
    输出：TrackedTargetArray
```

---

## 6. 步骤一：目标关联（Association）

### 6.1 关联代价矩阵

对于每对（已有航迹 i，新检测 j），计算关联代价：

```
FUNCTION compute_association_cost(track_i, detection_j):
    
    # 位置距离代价
    d_pos = geo_distance(track_i.predicted_position, detection_j.position)
    c_pos = d_pos / GATE_DISTANCE    # 归一化到 [0,1]
    
    # 速度一致性代价
    IF NOT isnan(detection_j.speed) AND NOT isnan(track_i.speed):
        d_vel = abs(track_i.speed - detection_j.speed)
        c_vel = d_vel / GATE_VELOCITY
    ELSE:
        c_vel = 0    # 无速度信息不惩罚
    
    # 航向一致性代价
    IF NOT isnan(detection_j.course) AND NOT isnan(track_i.course):
        d_hdg = abs(normalize_pm180(track_i.course - detection_j.course))
        c_hdg = d_hdg / GATE_HEADING
    ELSE:
        c_hdg = 0
    
    # 分类一致性代价
    IF track_i.classification == detection_j.classification:
        c_cls = 0
    ELSE:
        c_cls = 0.3    # 分类不一致有小惩罚
    
    # 加权总代价
    cost = W_POS × c_pos + W_VEL × c_vel + W_HDG × c_hdg + W_CLS × c_cls
    
    # 门控：超过阈值的不关联
    IF d_pos > GATE_DISTANCE:
        cost = INFINITY
    
    RETURN cost

W_POS = 0.5    # 位置权重（最重要）
W_VEL = 0.2    # 速度权重
W_HDG = 0.2    # 航向权重
W_CLS = 0.1    # 分类权重
GATE_DISTANCE = 500    # 米（关联门限）
GATE_VELOCITY = 5.0    # m/s
GATE_HEADING = 30      # 度
```

### 6.2 匈牙利算法求解最优关联

```
FUNCTION solve_association(tracks, detections):
    
    # 构建代价矩阵 C[i][j]
    n_tracks = len(tracks)
    n_detections = len(detections)
    C = matrix(n_tracks, n_detections)
    
    FOR i IN range(n_tracks):
        FOR j IN range(n_detections):
            C[i][j] = compute_association_cost(tracks[i], detections[j])
    
    # 匈牙利算法求解最小总代价的一对一匹配
    matches = hungarian_algorithm(C)
    
    # 分类结果
    matched_pairs = []      # (track_index, detection_index)
    unmatched_tracks = []   # 已有航迹无匹配检测（可能目标暂时丢失）
    unmatched_detections = [] # 新检测无匹配航迹（可能是新目标）
    
    FOR EACH (i, j) IN matches:
        IF C[i][j] < COST_THRESHOLD:
            matched_pairs.append((i, j))
        ELSE:
            unmatched_tracks.append(i)
            unmatched_detections.append(j)
    
    # 未被匹配的
    FOR i NOT IN matched_indices:
        unmatched_tracks.append(i)
    FOR j NOT IN matched_indices:
        unmatched_detections.append(j)
    
    RETURN {matched_pairs, unmatched_tracks, unmatched_detections}

COST_THRESHOLD = 0.8    # 代价超过此值即使匹配也拒绝
```

---

## 7. 步骤二：航迹初始化与管理

### 7.1 新航迹创建

```
FUNCTION create_new_track(detection):
    
    track = FusedTrack()
    track.track_id = generate_unique_id()
    track.track_status = "tentative"    # 待确认
    track.first_detected = now()
    track.last_updated = now()
    track.update_count = 1
    track.confirmation_count = 1
    track.miss_count = 0
    
    # 初始状态
    track.state = [detection.x, detection.y, detection.vx, detection.vy]
    track.covariance = initial_covariance(detection.sensor_type)
    
    # 分类
    track.classification = detection.classification
    track.classification_confidence = detection.classification_confidence
    track.classification_votes = {detection.classification: 1}
    
    # AIS 关联
    IF detection.sensor_type == "ais":
        track.mmsi = detection.mmsi
    ELSE:
        track.mmsi = 0
    
    # 数据来源
    track.source_sensors = [detection.sensor_type]
    
    RETURN track
```

### 7.2 M/N 确认逻辑

```
FUNCTION update_confirmation(track, detected_this_cycle):
    
    IF detected_this_cycle:
        track.confirmation_count += 1
        track.miss_count = 0
    ELSE:
        track.miss_count += 1
    
    # M out of N 确认
    IF track.track_status == "tentative":
        IF track.confirmation_count >= M_CONFIRM:
            track.track_status = "confirmed"
            log_event("track_confirmed", {id: track.track_id})
    
    # 丢失判定
    IF track.track_status == "confirmed":
        IF track.miss_count > 0 AND (now() - track.last_updated) > TRACK_LOST_TIMEOUT:
            track.track_status = "lost"
    
    # 删除判定
    IF track.track_status == "lost":
        IF (now() - track.last_updated) > TRACK_DELETE_TIMEOUT:
            track.track_status = "deleted"
    
    IF track.track_status == "tentative":
        IF track.miss_count > N_CONFIRM - M_CONFIRM:
            # 在 N 次扫描内没能积累到 M 次检测——删除
            track.track_status = "deleted"

M_CONFIRM = 3    # 需要 3 次检测
N_CONFIRM = 5    # 在 5 次扫描窗口内
```

---

## 8. 步骤三：状态估计与预测（卡尔曼滤波）

### 8.1 状态向量和运动模型

```
状态向量：x = [x, y, vx, vy]ᵀ

匀速运动模型：
x_{k+1} = F × x_k + w_k

F = [[1, 0, dt, 0 ],
     [0, 1, 0,  dt],
     [0, 0, 1,  0 ],
     [0, 0, 0,  1 ]]

过程噪声协方差：
Q = σ_q² × [[dt⁴/4, 0,      dt³/2, 0     ],
             [0,      dt⁴/4, 0,      dt³/2],
             [dt³/2, 0,      dt²,   0     ],
             [0,      dt³/2, 0,      dt²  ]]
```

### 8.2 预测步

```
FUNCTION kalman_predict(track, dt):
    
    F = compute_state_transition(dt)
    Q = compute_process_noise(dt, σ_q)
    
    track.state = F × track.state
    track.covariance = F × track.covariance × Fᵀ + Q
    
    RETURN track
```

### 8.3 更新步（融合测量）

```
FUNCTION kalman_update(track, measurement, H, R):
    
    # 创新（测量残差）
    y = measurement - H × track.state
    
    # 创新协方差
    S = H × track.covariance × Hᵀ + R
    
    # 卡尔曼增益
    K = track.covariance × Hᵀ × S⁻¹
    
    # 状态更新
    track.state = track.state + K × y
    
    # 协方差更新
    I = identity(4)
    track.covariance = (I - K × H) × track.covariance
    
    track.last_updated = now()
    track.update_count += 1
    
    RETURN track
```

### 8.4 各传感器的观测矩阵和测量噪声

**雷达（测量距离和方位）：**

```
H_radar = 非线性——使用 EKF 的雅可比矩阵
h(x) = [sqrt((x-x_own)² + (y-y_own)²), atan2(y-y_own, x-x_own)]
H_radar = ∂h/∂x（雅可比矩阵）

R_radar = [[σ_r_range², 0], [0, σ_r_bearing²]]
```

**AIS（测量位置和速度）：**

```
H_ais = [[1, 0, 0, 0],
         [0, 1, 0, 0],
         [0, 0, 1, 0],
         [0, 0, 0, 1]]

R_ais = diag([σ_ais_pos², σ_ais_pos², σ_ais_vel², σ_ais_vel²])
```

**摄像头（仅测量方位）：**

```
h_cam(x) = atan2(y - y_own, x - x_own)
H_cam = ∂h_cam/∂x    # 1×4 雅可比

R_cam = [σ_cam_bearing²]
```

---

## 9. 步骤四：传感器数据融合更新

### 9.1 顺序更新

当同一目标在一个周期内被多个传感器检测到时，依次用各传感器的测量做更新：

```
FUNCTION fuse_measurements(track, measurements):
    
    # 先预测到当前时刻
    dt = now() - track.last_predict_time
    kalman_predict(track, dt)
    
    # 按传感器精度从低到高依次更新（精度最高的最后更新）
    measurements.sort(by=sensor_accuracy, ascending=true)
    
    FOR EACH meas IN measurements:
        H, R = get_observation_model(meas.sensor_type)
        z = meas.to_measurement_vector()
        kalman_update(track, z, H, R)
    
    RETURN track
```

### 9.2 信息权重

不同传感器对不同状态量的贡献不同：

| 状态量 | 雷达贡献 | AIS 贡献 | 摄像头贡献 | LiDAR 贡献 |
|-------|---------|---------|----------|----------|
| 位置 (x, y) | 高（精确测距） | 中（GPS 精度有限） | 低（无距离） | 高（精确测距） |
| 速度 (vx, vy) | 中（多扫描推导） | 高（直接报告） | 无 | 中（多帧推导） |
| 航向 | 低（从运动推导） | 高（直接报告） | 中（从外观推断） | 中（从形状推断） |
| 分类 | 低（仅靠 RCS） | 高（AIS 船舶类型） | 高（AI 视觉） | 中（3D 形状） |
| 身份 (MMSI) | 无 | **唯一来源** | 无 | 无 |

---

## 10. 步骤五：分类信息融合

```
FUNCTION fuse_classification(track, new_classification, new_confidence, sensor_type):
    
    # 传感器分类权重
    sensor_class_weight = {
        "ais": 0.9,       # AIS 报告的船舶类型最可靠
        "camera": 0.7,    # 视觉分类较可靠
        "lidar": 0.4,     # 3D 形状分类中等
        "radar": 0.2      # 仅靠 RCS 分类不可靠
    }[sensor_type]
    
    weighted_confidence = new_confidence × sensor_class_weight
    
    # 投票累加
    IF new_classification NOT IN track.classification_votes:
        track.classification_votes[new_classification] = 0
    track.classification_votes[new_classification] += weighted_confidence
    
    # 取加权投票最高的类别
    best_class = max(track.classification_votes, key=lambda k: track.classification_votes[k])
    total_votes = sum(track.classification_votes.values())
    best_ratio = track.classification_votes[best_class] / total_votes
    
    track.classification = best_class
    track.classification_confidence = best_ratio
    
    RETURN track
```

---

## 11. 步骤六：航迹质量评估与维护

### 11.1 融合置信度计算

```
FUNCTION compute_track_confidence(track):
    
    confidence = 0.0
    
    # 因子 1：数据来源数量（多传感器确认更可靠）
    n_sources = len(set(track.source_sensors))
    confidence += 0.3 × min(n_sources / 3.0, 1.0)
    
    # 因子 2：更新频率（频繁更新更可靠）
    update_rate = track.update_count / max((now() - track.first_detected), 1.0)
    confidence += 0.2 × min(update_rate / 2.0, 1.0)    # 2 Hz 为满分
    
    # 因子 3：位置协方差（不确定性越小越好）
    pos_uncertainty = sqrt(track.covariance[0][0] + track.covariance[1][1])
    confidence += 0.2 × max(0, 1.0 - pos_uncertainty / 500.0)
    
    # 因子 4：AIS 关联（有 AIS 更可靠）
    IF track.mmsi != 0:
        confidence += 0.2
    
    # 因子 5：确认状态
    IF track.track_status == "confirmed":
        confidence += 0.1
    
    RETURN clamp(confidence, 0, 1)
```

---

## 12. AIS-雷达关联详解

AIS 和雷达的关联是最常见也是最重要的关联类型。

```
FUNCTION associate_ais_radar(ais_target, radar_target, own_ship):
    
    # 位置距离
    d_pos = geo_distance(ais_target.position, radar_target.position)
    
    IF d_pos > AIS_RADAR_GATE_DISTANCE:
        RETURN {associated: false}
    
    # 速度差
    IF NOT isnan(ais_target.sog) AND NOT isnan(radar_target.speed):
        d_vel = abs(ais_target.sog - radar_target.speed)
    ELSE:
        d_vel = 0
    
    # 航向差
    IF NOT isnan(ais_target.cog) AND NOT isnan(radar_target.course):
        d_hdg = abs(normalize_pm180(ais_target.cog - radar_target.course))
    ELSE:
        d_hdg = 0
    
    # 综合关联评分
    score = 1.0
    score -= 0.4 × (d_pos / AIS_RADAR_GATE_DISTANCE)
    score -= 0.3 × min(d_vel / 5.0, 1.0)
    score -= 0.3 × min(d_hdg / 30.0, 1.0)
    
    IF score > ASSOCIATION_MIN_SCORE:
        RETURN {associated: true, score: score}
    ELSE:
        RETURN {associated: false}

AIS_RADAR_GATE_DISTANCE = 500    # 米
ASSOCIATION_MIN_SCORE = 0.5
```

---

## 13. 摄像头-雷达关联详解

摄像头只有方位信息（无距离），关联主要基于方位一致性：

```
FUNCTION associate_camera_radar(camera_det, radar_det):
    
    # 方位差
    d_bearing = abs(normalize_pm180(camera_det.bearing - radar_det.bearing))
    
    IF d_bearing > CAMERA_RADAR_GATE_BEARING:
        RETURN {associated: false}
    
    # 如果摄像头有距离估算（从目标大小）
    IF NOT isnan(camera_det.estimated_range):
        d_range = abs(camera_det.estimated_range - radar_det.range)
        IF d_range > CAMERA_RADAR_GATE_RANGE:
            RETURN {associated: false}
    
    score = 1.0 - d_bearing / CAMERA_RADAR_GATE_BEARING
    
    RETURN {associated: true, score: score}

CAMERA_RADAR_GATE_BEARING = 5.0    # 度
CAMERA_RADAR_GATE_RANGE = 200      # 米
```

---

## 14. 三传感器联合关联

当同一区域有雷达、AIS 和摄像头三者同时检测到目标时：

```
FUNCTION three_way_association(radar_dets, ais_dets, camera_dets):
    
    # 策略：先做雷达-AIS 关联（最可靠），再将结果与摄像头关联
    
    # Step 1: 雷达-AIS
    radar_ais_pairs = solve_association(radar_dets, ais_dets, cost_func=ais_radar_cost)
    
    # Step 2: 合并已关联的雷达-AIS 对，再与摄像头关联
    fused_targets = []
    FOR EACH (r, a) IN radar_ais_pairs.matched:
        fused = merge_radar_ais(radar_dets[r], ais_dets[a])
        fused_targets.append(fused)
    
    # 加入未关联的雷达目标
    FOR EACH r IN radar_ais_pairs.unmatched_radar:
        fused_targets.append(radar_dets[r])
    
    # Step 3: fused_targets 与摄像头关联
    final_pairs = solve_association(fused_targets, camera_dets, cost_func=bearing_cost)
    
    FOR EACH (f, c) IN final_pairs.matched:
        # 补充摄像头的分类信息
        fused_targets[f].classification = camera_dets[c].classification
        fused_targets[f].source_sensors.append("camera")
    
    RETURN fused_targets
```

---

## 15. 航迹分裂与合并处理

### 15.1 航迹分裂

当一个融合航迹对应的物理目标实际上是两个紧挨的目标时（初始被当作一个），传感器分辨率提高（距离缩短）后可能检测到两个。

```
FUNCTION detect_track_split(track, detections):
    
    # 如果一个已有航迹在一个周期内匹配到两个不同位置的检测
    matched = [d for d in detections if association_cost(track, d) < COST_THRESHOLD]
    
    IF len(matched) >= 2:
        # 两个检测之间的距离
        d_between = geo_distance(matched[0].position, matched[1].position)
        
        IF d_between > SPLIT_DISTANCE_THRESHOLD:
            # 航迹分裂——创建一个新航迹
            new_track = create_new_track(matched[1])
            log_event("track_split", {original: track.track_id, new: new_track.track_id})
            RETURN new_track
    
    RETURN NULL

SPLIT_DISTANCE_THRESHOLD = 200    # 米
```

### 15.2 航迹合并

```
FUNCTION detect_track_merge(track_a, track_b):
    
    d = geo_distance(track_a.predicted_position, track_b.predicted_position)
    
    IF d < MERGE_DISTANCE_THRESHOLD:
        # 两条航迹实际上是同一个目标——合并
        merged = merge_tracks(track_a, track_b)
        log_event("track_merge", {a: track_a.track_id, b: track_b.track_id, merged: merged.track_id})
        RETURN merged
    
    RETURN NULL

MERGE_DISTANCE_THRESHOLD = 100    # 米
```

---

## 16. 假目标抑制与确认逻辑

```
FUNCTION suppress_false_targets(tracks):
    
    FOR EACH track IN tracks:
        IF track.track_status == "tentative":
            # 检查是否可能是假目标
            
            # 假目标特征 1：只被雷达检测且 RCS 很小
            IF track.source_sensors == ["radar"] AND track.rcs_avg < 0:
                track.false_target_likelihood += 0.1
            
            # 假目标特征 2：位置随机跳动
            IF track.position_variance > HIGH_VARIANCE_THRESHOLD:
                track.false_target_likelihood += 0.2
            
            # 假目标特征 3：只出现一次就消失
            IF track.update_count == 1 AND track.miss_count > 2:
                track.false_target_likelihood += 0.3
            
            IF track.false_target_likelihood > 0.7:
                track.track_status = "deleted"
                log_event("false_target_suppressed", {id: track.track_id})
```

---

## 17. 融合输出——统一目标列表

```
FUNCTION build_tracked_target_array(tracks):
    
    output = TrackedTargetArray()
    output.header.stamp = now()
    
    FOR EACH track IN tracks:
        IF track.track_status == "deleted":
            CONTINUE
        
        target = TrackedTarget()
        target.track_id = track.track_id
        target.mmsi = track.mmsi
        target.classification = track.classification
        target.confidence = compute_track_confidence(track)
        
        # 从状态向量提取位置和速度
        target.latitude, target.longitude = enu_to_geo(track.state[0], track.state[1], own_ship)
        target.sog = sqrt(track.state[2]² + track.state[3]²)
        target.cog = atan2(track.state[2], track.state[3]) × 180 / π
        target.heading = track.heading IF NOT NaN ELSE target.cog
        target.rot = track.rot
        target.length = track.length
        target.beam = track.beam
        
        target.position_covariance = [track.covariance[0][0], track.covariance[0][1],
                                       track.covariance[1][0], track.covariance[1][1]]
        target.source_sensors = track.source_sensors
        target.first_detected = track.first_detected
        target.last_updated = track.last_updated
        target.track_status = track.track_status
        target.update_count = track.update_count
        
        output.targets.append(target)
    
    output.total_tracked = len(output.targets)
    output.confirmed_count = sum(1 for t in output.targets if t.track_status == "confirmed")
    
    RETURN output
```

---

## 18. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Association Engine | 代价矩阵计算 + 匈牙利算法 | C++ |
| Track Manager | 航迹创建/确认/丢失/删除 | C++ |
| Kalman Filter Core | EKF 预测/更新 | C++ |
| AIS-Radar Associator | AIS-雷达专用关联逻辑 | C++ |
| Camera-Radar Associator | 摄像头-雷达方位关联 | C++ |
| Classification Fuser | 多源分类投票融合 | C++ |
| Track Quality Assessor | 航迹质量和置信度计算 | C++ |
| Split-Merge Handler | 航迹分裂和合并检测 | C++ |
| False Target Suppressor | 假目标抑制 | C++ |

---

## 19. 数值算例

### 算例：雷达-AIS 融合

```
雷达检测：距离 3200m, 方位 045°, RCS 25dBsm
AIS 报告：位置 (31.235°N, 121.457°E), SOG 12kn, COG 270°, MMSI 123456789

本船位置：(31.230°N, 121.450°E)

将 AIS 位置转到 ENU：Δx=617m(东), Δy=556m(北)
AIS 目标距离 = sqrt(617²+556²) = 830m... 嗯这跟雷达 3200m 差很多

实际上需要用经纬度精确计算——假设两者位置差 d_pos = 150m（在门限 500m 内）
速度差：AIS 12kn = 6.17 m/s, 雷达未直接报告速度 → d_vel = 0
航向差：AIS COG 270°, 雷达无直接航向 → d_hdg = 0

关联评分 = 1.0 - 0.4×(150/500) = 1.0 - 0.12 = 0.88 > 0.5 → 关联成功！

卡尔曼更新后：
  位置 = 雷达和 AIS 的加权平均（雷达距离精度高→权重大）
  速度 = 主要来自 AIS（AIS 直接报告速度）
  MMSI = 来自 AIS
  分类 = 待摄像头补充
```

---

## 20. 参数总览表

| 类别 | 参数 | 默认值 |
|------|------|-------|
| **关联** | AIS-雷达距离门限 | 500 m |
| | 摄像头-雷达方位门限 | 5° |
| | 关联最低评分 | 0.5 |
| | 代价阈值 | 0.8 |
| **卡尔曼** | 过程噪声 σ_q | 1.0 m/s² |
| | 雷达距离噪声 | 20 m |
| | 雷达方位噪声 | 1.0° |
| | AIS 位置噪声 | 30 m |
| | AIS 速度噪声 | 0.5 m/s |
| | 摄像头方位噪声 | 2.0° |
| **航迹管理** | 确认次数 M/N | 3/5 |
| | 丢失超时 | 30 s |
| | 删除超时 | 120 s |
| | 最大航迹数 | 200 |
| **分裂合并** | 分裂距离 | 200 m |
| | 合并距离 | 100 m |

---

## 21. 与其他模块的协作

| 交互方 | 方向 | 数据 |
|-------|------|------|
| Object Detection → Fusion | 输入 | 各传感器检测列表 |
| Data Preprocessing → Fusion | 输入 | AIS 目标 |
| Fusion → Situational Awareness | 输出 | TrackedTargetArray |
| Fusion → Target Tracker (L3) | 输出 | TrackedTargetArray（通过话题） |

---

## 22. 测试策略与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| MSF-001 | 雷达+AIS 同一目标关联 | 关联成功率 > 95% |
| MSF-002 | 雷达+摄像头方位关联 | 关联成功率 > 85% |
| MSF-003 | 三传感器联合关联 | 三者一致性 > 80% |
| MSF-004 | 卡尔曼滤波位置精度 | 融合后精度优于任一单传感器 |
| MSF-005 | AIS 无雷达目标（仅 AIS） | 航迹正确创建 |
| MSF-006 | 雷达无 AIS 目标（仅雷达） | 航迹正确创建，标记无 AIS |
| MSF-007 | 目标暂时丢失一个传感器 | 航迹不中断 |
| MSF-008 | 假目标抑制 | 单次闪烁目标不确认 |
| MSF-009 | 航迹分裂（两船分离） | 正确分裂 |
| MSF-010 | 200 个同时目标 | 处理延迟 < 100ms |

---

## 23. 参考文献

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Bar-Shalom, Y. "Estimation with Applications to Tracking and Navigation" 2001 | 多目标跟踪理论 |
| [2] | Blackman, S. "Multiple-Target Tracking with Radar Applications" 1986 | 雷达目标跟踪 |
| [3] | Kuhn, H.W. "The Hungarian Method" 1955 | 关联优化算法 |
| [4] | IMO MSC.192(79) | INS 综合导航系统标准 |

---

**文档结束**
