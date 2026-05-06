# MASS_ADAS Object Detection & Classification 目标检测与分类模块技术设计文档

**文档编号：** SANGO-ADAS-ODC-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  
**状态：** 开发参考

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 了望员的目标识别思维模型
5. 计算流程总览
6. 步骤一：雷达目标检测（CFAR 检测器）
7. 步骤二：摄像头目标检测（深度学习）
8. 步骤三：LiDAR 目标检测（点云聚类）
9. 步骤四：目标分类
10. 步骤五：目标属性估算
11. 检测模型的训练策略
12. 海事场景的特殊挑战
13. 夜间与恶劣天气的检测策略
14. 小目标检测增强
15. 检测性能评估指标
16. 推理加速与边缘部署
17. 在线学习与模型更新
18. 内部子模块分解
19. 数值算例
20. 参数总览表
21. 与其他模块的协作关系
22. 测试策略与验收标准
23. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Object Detection & Classification（目标检测与分类）是 Perception 子系统的第二个模块。它接收 Data Preprocessing 输出的预处理传感器数据，从中检测并分类海上目标——船舶、浮标、渔网、漂流物、岸线结构等。

在传统船舶上，这项工作由了望员（Lookout）和操船者（OOW）共同完成——他们用眼睛和雷达发现目标，用经验判断"那是什么"。在 MASS_ADAS 中，这项工作由 AI 算法完成。

### 1.2 核心职责

- **雷达目标检测：** 从雷达扫描数据中提取目标回波，区分真实目标与杂波。
- **视觉目标检测：** 从摄像头图像中检测船舶和其他海上物体。
- **LiDAR 目标检测：** 从 3D 点云中提取目标（如有 LiDAR）。
- **目标分类：** 将检测到的目标分类为：大型船舶、小型船舶、帆船、渔船、浮标/航标、渔网/渔具、漂流物/集装箱、岸线结构、未知物体。
- **目标属性估算：** 估算目标的尺度（长度、宽度）、朝向。

### 1.3 设计原则

- **宁可多检不可漏检原则：** 在海上，漏检一个目标的后果是碰撞。误检（把杂波当目标）的后果只是不必要的避让——代价远低于漏检。因此检测器的阈值应偏向灵敏（高召回率），允许一定的误检率。
- **多传感器互补原则：** 雷达在远距离和恶劣天气下有优势，摄像头在目标分类和近距离细节识别上有优势。两者互补而非替代。
- **实时性原则：** 检测算法必须在传感器数据周期内完成——雷达检测 < 100ms/scan，摄像头检测 < 100ms/frame。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 频率 | 说明 |
|-------|------|------|------|
| 预处理后的雷达扫描/ARPA 目标 | Data Preprocessing | 2~4 Hz | 扫描图像或 ARPA 目标列表 |
| 预处理后的摄像头帧 | Data Preprocessing | 10~30 Hz | 去畸变、增强后的图像 |
| 预处理后的 LiDAR 点云 | Data Preprocessing | 10~20 Hz | 运动补偿后的点云 |
| 本船状态 | Data Preprocessing | 50 Hz | 用于运动补偿和坐标转换 |

### 2.2 输出

```
DetectedObjectArray:
    Header header
    DetectedObject[] objects

DetectedObject:
    uint32 detection_id                 # 唯一检测 ID（帧内唯一）
    string sensor_source                # "radar"/"camera"/"lidar"/"camera+lidar"
    
    # ---- 空间信息 ----
    float64 range_m                     # 距离（米）
    float64 bearing_deg                 # 相对方位（度）
    float64 x_body, y_body              # 船体坐标系
    float64 latitude, longitude         # WGS84（如可计算）
    BoundingBox2D bbox_image            # 图像中的检测框（仅摄像头）
    BoundingBox3D bbox_3d               # 3D 包围盒（仅 LiDAR）
    
    # ---- 分类 ----
    string classification               # "vessel_large"/"vessel_small"/"sailing"/"fishing"/
                                        # "buoy"/"fishing_gear"/"debris"/"shore_structure"/"unknown"
    float64 classification_confidence   # 分类置信度 [0,1]
    float64[] class_probabilities       # 各类别的概率分布
    
    # ---- 目标属性 ----
    float64 estimated_length_m          # 估计长度（米，NaN=未知）
    float64 estimated_heading_deg       # 估计朝向（度，NaN=未知）
    float64 rcs_dbsm                    # 雷达截面积（dBsm，仅雷达）
    
    # ---- 检测质量 ----
    float64 detection_confidence        # 检测置信度 [0,1]
    bool is_confirmed                   # 是否被多帧确认
    uint32 confirmation_count           # 连续检测次数
```

---

## 3. 核心参数数据库

### 3.1 检测阈值

| 传感器 | 参数 | 默认值 | 说明 |
|-------|------|-------|------|
| 雷达 CFAR | 虚警率 P_fa | 10⁻⁴ | 每个距离单元的虚警概率 |
| 雷达 CFAR | 保护单元数 | 2 | CFAR 保护窗口 |
| 雷达 CFAR | 参考单元数 | 16 | CFAR 参考窗口（每侧 8 个） |
| 摄像头 DNN | 检测置信度阈值 | 0.3 | NMS 前的置信度下限 |
| 摄像头 DNN | NMS IoU 阈值 | 0.5 | 非极大值抑制重叠阈值 |
| LiDAR 聚类 | 最小点数 | 5 | 一个聚类的最少点数 |
| LiDAR 聚类 | 聚类距离 | 1.0 m | DBSCAN 邻域半径 |

### 3.2 目标分类类别

| 类别 ID | 类别名称 | 说明 | 典型 RCS(dBsm) | 典型图像特征 |
|---------|---------|------|---------------|-------------|
| 0 | vessel_large | 大型船舶（> 50m） | 30~50 | 明显的上层建筑、大面积船体 |
| 1 | vessel_small | 小型船舶（< 50m） | 10~30 | 较小船体、少量上层建筑 |
| 2 | sailing | 帆船 | 5~20 | 帆、桅杆 |
| 3 | fishing | 渔船 | 10~25 | 渔具、起重臂、拖网 |
| 4 | buoy | 浮标/航标 | 0~10 | 小型圆柱/圆锥形物体 |
| 5 | fishing_gear | 渔网/渔具/渔排 | -5~10 | 水面漂浮物集群 |
| 6 | debris | 漂流物（集装箱等） | 5~20 | 不规则形状、低矮 |
| 7 | shore_structure | 岸线结构（桥梁、码头等） | 30~60 | 固定、大面积 |
| 8 | unknown | 未知目标 | — | 无法匹配已知类别 |

### 3.3 多帧确认参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 确认所需连续检测次数 | 3 | 连续 3 帧/扫描检测到才确认 |
| 确认窗口时间 | 10 秒 | 在 10 秒内出现 3 次 |
| 关联距离阈值 | 50 m | 帧间目标关联的最大位移 |

---

## 4. 了望员的目标识别思维模型

### 4.1 了望员如何发现和识别目标

**第一步——发现：** 了望员扫视海面时，他首先注意到的是与背景不同的"异常"——海平线上的一个突起（远距离船舶）、水面上的一个反光点（近距离小船）、雷达屏幕上的一个持续亮点。发现的关键不是"那是什么"，而是"那里有东西"。

**第二步——跟踪：** 发现后，他会持续关注那个目标几秒钟——确认它确实存在（不是海浪的一次性反射）、它在移动还是静止、它在什么方位和大概什么距离。这对应检测模块的多帧确认机制。

**第三步——分类：** 确认目标存在后，他开始判断"那是什么"——是商船、渔船、帆船、浮标还是漂流物。他的判断依据是：目标大小（大的可能是商船）、形状（有帆的是帆船）、运动模式（速度恒定走直线的可能是商船，速度低且方向不定的可能是渔船）、灯光（夜间看灯光颜色和闪烁模式识别目标类型和朝向）。

**第四步——报告：** 他向驾驶台报告："右舷 30 度，约 5 海里，一艘大型货船，航向大致 270，可能穿越我们的航路。" 这个报告包含了方位、距离、类型、航向——正是 DetectedObject 的核心字段。

### 4.2 AI 检测与人眼检测的差异

| 方面 | 人眼 | AI |
|------|------|-----|
| 远距离检测 | 有限（< 6nm 目视，< 20nm 雷达辅助） | 雷达 20+nm，摄像头 3~8nm |
| 恶劣天气 | 严重退化（雨、雾中近乎失明） | 雷达不受影响，红外有一定穿透 |
| 分类准确性 | 极高（经验丰富时） | 中等（需要大量训练数据） |
| 小目标检测 | 优秀（人眼对运动目标极敏感） | 有挑战（小目标在图像中只有几个像素） |
| 夜间 | 只能看灯光 | 红外摄像头可工作，雷达不受影响 |
| 持续性 | 会疲劳 | 不疲劳 |
| 一致性 | 因人而异 | 确定性（同样输入同样输出） |

---

## 5. 计算流程总览

```
预处理后的传感器数据
    │
    ├── 雷达扫描数据 ──→ 步骤一：雷达目标检测（CFAR）
    │                         │
    ├── 摄像头帧 ──────→ 步骤二：视觉目标检测（DNN）
    │                         │
    ├── LiDAR 点云 ───→ 步骤三：LiDAR 目标检测（聚类）
    │                         │
    └──────────────────────────┤
                               ▼
                    ┌─────────────────────┐
                    │ 步骤四：目标分类       │
                    │ 综合多传感器特征分类    │
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │ 步骤五：目标属性估算   │
                    │ 尺度、朝向估算         │
                    └──────────┬──────────┘
                               │
                               ▼
                    输出：DetectedObjectArray
```

---

## 6. 步骤一：雷达目标检测（CFAR 检测器）

### 6.1 CFAR 原理

恒虚警率（Constant False Alarm Rate, CFAR）检测器是雷达目标检测的标准方法。原理：对于每个距离-方位单元，用其邻近单元的回波强度估计当前背景杂波水平，设定一个自适应阈值——只有回波强度超过阈值的单元才被检测为目标。

```
FUNCTION cfar_detect(radar_scan, cfar_params):
    
    detections = []
    
    FOR EACH azimuth_bin IN radar_scan:
        FOR EACH range_bin IN azimuth_bin.echo_data:
            
            # 跳过保护单元
            guard_start = range_bin - cfar_params.guard_cells
            guard_end = range_bin + cfar_params.guard_cells
            
            # 参考单元（保护单元两侧各 N 个单元）
            ref_leading = echo_data[range_bin - cfar_params.guard_cells - cfar_params.ref_cells : guard_start]
            ref_lagging = echo_data[guard_end : range_bin + cfar_params.guard_cells + cfar_params.ref_cells]
            
            # 估计背景杂波功率
            # CA-CFAR（Cell Averaging）：取参考单元的平均值
            noise_estimate = mean(ref_leading + ref_lagging)
            
            # 阈值 = 噪声估计 × 阈值因子
            threshold = noise_estimate × cfar_params.threshold_factor
            
            # 检测判定
            IF echo_data[range_bin] > threshold:
                detections.append({
                    range: range_bin × range_resolution,
                    azimuth: azimuth_bin.angle,
                    echo_strength: echo_data[range_bin],
                    snr: echo_data[range_bin] / noise_estimate
                })
    
    # 聚合相邻检测单元为一个目标
    targets = cluster_detections(detections)
    
    RETURN targets
```

### 6.2 阈值因子计算

```
FUNCTION compute_threshold_factor(P_fa, N_ref):
    # CA-CFAR 的阈值因子与虚警率和参考单元数的关系：
    # T = N × (P_fa^(-1/N) - 1)
    
    T = N_ref × (P_fa ** (-1.0 / N_ref) - 1)
    
    RETURN T

# P_fa = 10^-4, N_ref = 16:
# T = 16 × (10^(4/16) - 1) = 16 × (10^0.25 - 1) = 16 × 0.778 = 12.45
# 即回波需要比背景噪声强 12.45 倍才被检测
```

### 6.3 CFAR 变体选择

| CFAR 变体 | 适用场景 | 说明 |
|-----------|---------|------|
| CA-CFAR（Cell Averaging） | 均匀杂波 | 标准选择 |
| GO-CFAR（Greatest Of） | 杂波边缘 | 取两侧参考单元中较大的——防止在杂波边缘产生虚警 |
| SO-CFAR（Smallest Of） | 多目标环境 | 取较小的——防止大目标遮蔽小目标 |
| OS-CFAR（Ordered Statistics） | 非均匀杂波 | 排序后取第 k 大值——最鲁棒 |

**建议：** 默认使用 CA-CFAR。在海杂波边缘区域自动切换到 GO-CFAR。在密集目标区域切换到 SO-CFAR。

### 6.4 雷达检测后处理

```
FUNCTION postprocess_radar_detections(raw_detections):
    
    # 1. 聚类——将相邻的检测单元合并为一个目标
    clusters = dbscan_cluster(raw_detections, eps=3_cells, min_points=2)
    
    FOR EACH cluster:
        # 2. 计算目标中心位置（加权平均）
        center_range = weighted_mean(cluster.ranges, weights=cluster.strengths)
        center_azimuth = weighted_circular_mean(cluster.azimuths, weights=cluster.strengths)
        
        # 3. 估计目标展宽（方位展宽 ≈ 目标角尺寸）
        azimuth_extent = max(cluster.azimuths) - min(cluster.azimuths)
        range_extent = max(cluster.ranges) - min(cluster.ranges)
        
        # 4. 估计 RCS
        rcs = compute_rcs(max(cluster.strengths), center_range, radar_params)
        
        # 5. 构建 RadarDetection
        detection = {
            range: center_range,
            bearing: center_azimuth,
            azimuth_extent: azimuth_extent,
            range_extent: range_extent,
            rcs_dbsm: 10 × log10(rcs),
            peak_strength: max(cluster.strengths),
            detection_confidence: compute_radar_confidence(cluster)
        }
    
    RETURN detections
```

---

## 7. 步骤二：摄像头目标检测（深度学习）

### 7.1 检测模型选择

海事目标检测推荐使用以下架构：

| 模型 | 精度 | 速度 | 推荐度 | 说明 |
|------|------|------|--------|------|
| YOLOv8/v9 | 高 | 快 | 推荐 | 实时单阶段检测器，适合边缘部署 |
| RT-DETR | 很高 | 中等 | 备选 | 端到端 Transformer 检测器，无 NMS |
| Faster R-CNN | 很高 | 慢 | 不推荐 | 两阶段检测器，精度高但速度不满足实时要求 |

**推荐：YOLOv8-m 或 YOLOv8-l，配合 TensorRT 部署到 NVIDIA Jetson/GPU 设备。**

### 7.2 检测推理流程

```
FUNCTION detect_objects_camera(frame, model, params):
    
    # 1. 图像预处理（适配模型输入）
    input_tensor = preprocess_image(frame, 
                                     target_size=model.input_size,  # 如 640×640
                                     normalize=True,
                                     format="NCHW")
    
    # 2. 模型推理
    raw_outputs = model.infer(input_tensor)    # GPU 推理
    
    # 3. 后处理
    # 3a. 置信度过滤
    candidates = [det for det in raw_outputs if det.confidence > params.conf_threshold]
    
    # 3b. NMS（非极大值抑制）
    final_detections = non_max_suppression(candidates, iou_threshold=params.nms_iou)
    
    # 4. 构建输出
    objects = []
    FOR EACH det IN final_detections:
        obj = DetectedObject()
        obj.sensor_source = "camera"
        obj.bbox_image = det.bbox    # [x1, y1, x2, y2]
        obj.classification = CLASS_NAMES[det.class_id]
        obj.classification_confidence = det.confidence
        obj.class_probabilities = det.class_probs
        
        # 方位角估算（从像素坐标转换）
        center_x = (det.bbox.x1 + det.bbox.x2) / 2
        obj.bearing_deg = pixel_to_bearing(center_x, frame.width, camera_config)
        
        # 距离暂不可知（单目摄像头）
        obj.range_m = NaN
        
        # 尺度估算（从 bbox 大小粗估）
        obj.estimated_length_m = estimate_length_from_bbox(det.bbox, camera_config, estimated_range=NaN)
        
        objects.append(obj)
    
    RETURN objects
```

### 7.3 海事目标检测数据集

训练海事目标检测模型需要专门的海上数据集：

| 数据集 | 规模 | 类别 | 来源 |
|-------|------|------|------|
| SeaShips | 31,455 张 | 6 类船舶 | 公开数据集 |
| SMD (Singapore Maritime Dataset) | 8 万帧视频 | 多类 | 新加坡海事数据集 |
| MCShips | 10 万+ | 多类船舶 | 公开 |
| 自采数据 | 需自建 | 根据实际海域 | **必须补充** |

**关键：** 公开数据集不够——必须在 SANGO USV 的实际运营海域（中国沿海）采集标注数据，特别是渔船、小型渔排、渔网等在中国沿海特有的目标类型。

### 7.4 数据增强策略

```
# 海事场景特有的数据增强
augmentations = [
    RandomFog(p=0.3, severity=[1,3]),           # 雾天模拟
    RandomRain(p=0.2),                           # 雨天模拟
    RandomSunGlare(p=0.2),                       # 阳光眩光
    RandomWaveOcclusion(p=0.3),                  # 波浪遮挡（目标在浪谷中部分隐藏）
    RandomHorizonShift(p=0.2),                   # 地平线位置变化（横摇）
    RandomBrightnessContrast(p=0.4),             # 亮度对比度
    Mosaic(p=0.5),                               # 马赛克增强
    MixUp(p=0.2),                                # 混合增强
    SmallObjectCopyPaste(p=0.3, max_copies=5),   # 小目标复制粘贴（增加小目标样本）
]
```

---

## 8. 步骤三：LiDAR 目标检测（点云聚类）

### 8.1 水面上方点云提取

```
FUNCTION extract_above_water_points(point_cloud, sea_surface_model):
    
    # 估计海面高度（基于 IMU 横摇/纵摇和传感器高度）
    sea_level_z = -sensor_height + roll_compensation + pitch_compensation
    
    # 提取水面以上的点
    above_water = [p for p in point_cloud if p.z > sea_level_z + WATER_MARGIN]
    
    RETURN above_water

WATER_MARGIN = 0.5    # 米——海面以上 0.5m 以内的点可能是浪花
```

### 8.2 DBSCAN 聚类

```
FUNCTION cluster_lidar_points(points, eps, min_points):
    
    # DBSCAN 聚类——适合任意形状、自动确定簇数
    clusters = DBSCAN(eps=eps, min_samples=min_points).fit(points.xyz)
    
    objects = []
    FOR EACH cluster_id IN unique(clusters.labels):
        IF cluster_id == -1:
            CONTINUE    # 噪声点
        
        cluster_points = points[clusters.labels == cluster_id]
        
        # 计算包围盒
        bbox_3d = compute_3d_bounding_box(cluster_points)
        
        # 计算中心
        center = mean(cluster_points.xyz, axis=0)
        
        # 计算距离和方位
        range_m = sqrt(center.x² + center.y²)
        bearing_deg = atan2(center.y, center.x) × 180/π
        
        objects.append({
            range_m: range_m,
            bearing_deg: bearing_deg,
            bbox_3d: bbox_3d,
            point_count: len(cluster_points),
            estimated_length: bbox_3d.length,
            estimated_width: bbox_3d.width,
            estimated_height: bbox_3d.height,
            detection_confidence: compute_lidar_confidence(cluster_points)
        })
    
    RETURN objects
```

---

## 9. 步骤四：目标分类

### 9.1 多特征分类

对于已检测到的目标，综合多种特征进行分类：

```
FUNCTION classify_target(detection, sensor_source):
    
    IF sensor_source == "camera":
        # 摄像头检测已经包含了 DNN 分类结果
        RETURN detection.classification, detection.classification_confidence
    
    IF sensor_source == "radar":
        # 雷达分类基于 RCS 和运动特征
        RETURN classify_by_radar_features(detection)
    
    IF sensor_source == "lidar":
        # LiDAR 分类基于 3D 形状
        RETURN classify_by_3d_shape(detection)

FUNCTION classify_by_radar_features(detection):
    
    rcs = detection.rcs_dbsm
    azimuth_extent = detection.azimuth_extent
    
    # 基于 RCS 的粗分类
    IF rcs > 35:
        RETURN "vessel_large", 0.7
    ELIF rcs > 15:
        RETURN "vessel_small", 0.5    # 可能是小船或大浮标
    ELIF rcs > 5:
        RETURN "buoy", 0.4            # 可能是浮标或渔船
    ELIF rcs > -5:
        RETURN "fishing_gear", 0.3    # 小 RCS，可能是渔网
    ELSE:
        RETURN "unknown", 0.2

FUNCTION classify_by_3d_shape(detection):
    
    l = detection.estimated_length
    w = detection.estimated_width
    h = detection.estimated_height
    
    aspect_ratio = l / max(w, 0.1)
    
    IF l > 30 AND aspect_ratio > 3:
        RETURN "vessel_large", 0.6
    ELIF l > 5 AND aspect_ratio > 2:
        RETURN "vessel_small", 0.5
    ELIF h > 3 AND l < 5:
        RETURN "buoy", 0.5
    ELIF h < 1 AND l > 3:
        RETURN "fishing_gear", 0.4
    ELSE:
        RETURN "unknown", 0.3
```

### 9.2 分类置信度融合

当同一目标被多个传感器检测时，分类结果可能不同。置信度融合在 Multi-Sensor Fusion 模块中处理。

---

## 10. 步骤五：目标属性估算

### 10.1 从摄像头估算目标长度

```
FUNCTION estimate_length_from_camera(bbox, camera_config, estimated_range):
    
    IF isnan(estimated_range):
        RETURN NaN    # 无距离信息无法估算
    
    # 目标在图像中的像素宽度
    pixel_width = bbox.x2 - bbox.x1
    
    # 每像素对应的角度
    degrees_per_pixel = camera_config.fov_horizontal / camera_config.resolution_w
    
    # 目标的角展宽
    angular_width_deg = pixel_width × degrees_per_pixel
    angular_width_rad = angular_width_deg × π / 180
    
    # 由距离和角宽度估算物理长度
    estimated_length = estimated_range × angular_width_rad
    
    RETURN estimated_length
```

### 10.2 从雷达估算目标尺度

```
FUNCTION estimate_length_from_radar(detection, radar_params):
    
    # 方位展宽（扣除雷达波束宽度）
    target_angular_width = max(0, detection.azimuth_extent - radar_params.beam_width)
    
    IF target_angular_width > 0:
        length_estimate = detection.range × target_angular_width × π / 180
    ELSE:
        length_estimate = NaN    # 目标小于一个波束宽度，无法估算
    
    RETURN length_estimate
```

### 10.3 目标朝向估算

```
# 摄像头：从目标外观推断朝向（需要专门的 AI 模型）
# 雷达：从多扫描的运动轨迹推断（COG 近似等于朝向）
# LiDAR：从 3D 包围盒的长轴方向推断

# V1 版本：目标朝向设为 NaN，由 Multi-Sensor Fusion 用 AIS 数据补充
```

---

## 11. 检测模型的训练策略

### 11.1 训练数据需求

| 目标类别 | 最少标注图像数 | 说明 |
|---------|-------------|------|
| vessel_large | 5,000 | 各角度、各距离 |
| vessel_small | 10,000 | 变化多，需要更多样本 |
| sailing | 3,000 | 帆船形态多样 |
| fishing | 8,000 | 中国沿海渔船种类繁多 |
| buoy | 3,000 | 各种航标 |
| fishing_gear | 5,000 | 渔排、渔网、渔笼 |
| debris | 2,000 | 漂流物稀少但重要 |

### 11.2 训练流程

```
# 1. 数据采集：在实际海域航行中录制视频和雷达数据
# 2. 数据标注：使用 LabelImg 或 CVAT 标注目标
# 3. 数据划分：训练 70% / 验证 15% / 测试 15%
# 4. 模型训练：YOLOv8，300 epochs，SGD 优化器
# 5. 模型评估：计算 mAP@50, mAP@50:95
# 6. 模型量化：FP16 或 INT8 量化（TensorRT）
# 7. 边缘部署：导出 TensorRT engine 部署到 Jetson
# 8. 在线验证：实际航行中持续收集误检/漏检样本
# 9. 模型迭代：用新数据重新训练，定期更新
```

---

## 12. 海事场景的特殊挑战

### 12.1 海面杂波

海面的波浪产生大量的雷达反射（海杂波）和视觉噪声（波光粼粼、白浪花），这是海事目标检测最大的挑战之一。

```
# 雷达应对：CFAR 检测器的参考单元自动适应杂波水平
# 摄像头应对：DNN 模型需要在包含海面的数据上训练
# 关键：训练数据必须包含各种海况（平静→大浪）的样本
```

### 12.2 远距离小目标

远距离的小型船舶在摄像头图像中可能只有几个像素，在雷达上可能只有一个距离单元。检测这些目标是极大的挑战。

```
# 应对策略：
# 1. 多尺度检测（YOLOv8 的 FPN 结构天然支持）
# 2. 裁剪-放大策略：将远距离区域（图像上部）裁剪并放大后再检测
# 3. 小目标专用检测头：在 YOLO 中添加更大特征图的检测头
# 4. 注意力机制：让模型关注海平线附近区域
```

### 12.3 目标遮挡

波浪可能部分遮挡目标（目标在浪谷中只露出上部），多个目标可能互相遮挡。

```
# 应对策略：
# 1. 数据增强中加入遮挡模拟
# 2. 使用支持部分可见目标的检测器
# 3. 多帧融合——单帧被遮挡但多帧中总有完整可见的时刻
```

---

## 13. 夜间与恶劣天气的检测策略

### 13.1 夜间检测

```
FUNCTION select_night_detection_strategy(time_of_day, cameras):
    
    IF time_of_day == "night":
        # 可见光摄像头近乎无用（除灯光检测）
        # 切换到：雷达为主 + 红外摄像头 + 灯光检测
        
        detection_sources = ["radar"]
        
        IF cameras.has_infrared:
            detection_sources.append("ir_camera")
            # 红外摄像头可以检测船舶的热辐射
        
        IF cameras.has_low_light:
            detection_sources.append("low_light_camera")
            # 低照度摄像头可以检测航行灯
        
        RETURN detection_sources
```

### 13.2 雾天检测

```
# 雾天：摄像头几乎失效，雷达不受影响
# 策略：完全依赖雷达检测，降低摄像头检测结果的权重
IF visibility < 1.0:    # nm
    camera_weight = 0.1
    radar_weight = 0.9
ELIF visibility < 3.0:
    camera_weight = 0.3
    radar_weight = 0.7
```

### 13.3 大雨检测

```
# 大雨：雷达受雨杂波影响，摄像头视野受限
# 策略：雷达使用 FTC 杂波抑制，摄像头使用去雨算法
IF rain_intensity > "heavy":
    radar_mode = "FTC_HIGH"
    camera_preprocess = "derain"
```

---

## 14. 小目标检测增强

### 14.1 多分辨率检测策略

```
FUNCTION multi_resolution_detect(frame, model, camera_config):
    
    # 策略：将图像分为多个区域，对远距离区域做放大检测
    
    detections_full = model.detect(frame, size=640)    # 全图检测
    
    # 远距离区域裁剪放大（图像上部 1/3 是远距离海面）
    horizon_region = frame[0:frame.height//3, :]
    horizon_upscaled = resize(horizon_region, scale=2.0)
    detections_horizon = model.detect(horizon_upscaled, size=640)
    
    # 将放大区域的检测结果映射回原图坐标
    FOR EACH det IN detections_horizon:
        det.bbox.y1 = det.bbox.y1 / 2.0
        det.bbox.y2 = det.bbox.y2 / 2.0
        det.bbox.x1 = det.bbox.x1 / 2.0
        det.bbox.x2 = det.bbox.x2 / 2.0
    
    # 合并并去重
    all_detections = merge_and_nms(detections_full + detections_horizon)
    
    RETURN all_detections
```

---

## 15. 检测性能评估指标

| 指标 | 定义 | 目标值 | 说明 |
|------|------|-------|------|
| mAP@50 | 50% IoU 阈值下的平均精度 | > 0.70 | 整体检测质量 |
| mAP@50:95 | 50%~95% IoU 的平均 mAP | > 0.45 | 更严格的质量指标 |
| 召回率（vessel） | 船舶类目标的检出率 | > 0.95 | **最关键——不漏检船** |
| 精确率（vessel） | 船舶检测的正确率 | > 0.80 | 允许 20% 误检 |
| 召回率（small_target） | 小目标（< 20px）检出率 | > 0.70 | 远距离小目标 |
| 推理时间 | 单帧处理时间 | < 50 ms | 满足实时要求 |

---

## 16. 推理加速与边缘部署

### 16.1 部署平台

| 平台 | 推理速度 | 功耗 | 推荐度 |
|------|---------|------|--------|
| NVIDIA Jetson AGX Orin | YOLOv8-m @ 30fps FP16 | 30W | 强烈推荐 |
| NVIDIA Jetson Xavier NX | YOLOv8-s @ 30fps FP16 | 15W | 推荐 |
| Intel NUC + OpenVINO | YOLOv8-s @ 15fps | 25W | 备选 |

### 16.2 模型优化

```
# 1. TensorRT 加速
model_trt = torch2trt(model, input_shape=[1,3,640,640], fp16=True)

# 2. INT8 量化（进一步加速，精度略降）
model_int8 = torch2trt(model, int8=True, calibration_dataset=cal_data)

# 3. 模型剪枝（减小模型大小）
model_pruned = prune_model(model, amount=0.3)    # 剪掉 30% 的权重

# 4. 知识蒸馏（用大模型指导小模型训练）
student = YOLOv8-s()
teacher = YOLOv8-x()  # 预训练的大模型
student = distill(student, teacher, train_data)
```

---

## 17. 在线学习与模型更新

### 17.1 持续数据采集

```
FUNCTION collect_online_samples(detections, ground_truth_from_ais):
    
    # 利用 AIS 数据作为"弱标签"——AIS 报告的目标位置和类型
    # 可以自动标注部分摄像头检测结果
    
    FOR EACH ais_target:
        # 将 AIS 目标投影到摄像头图像中
        pixel_x, pixel_y = project_to_camera(ais_target.position, camera_config)
        
        # 在该位置附近搜索摄像头检测
        matched_detection = find_nearest_detection(pixel_x, pixel_y, detections, max_dist=50px)
        
        IF matched_detection:
            # 自动标注样本
            sample = {
                image: current_frame,
                bbox: matched_detection.bbox,
                label: map_ais_type_to_class(ais_target.ship_type),
                source: "auto_label_ais"
            }
            online_dataset.append(sample)
        ELSE:
            # AIS 有目标但摄像头没检测到——潜在的漏检样本
            missed_sample = {
                image: current_frame,
                expected_position: (pixel_x, pixel_y),
                ais_type: ais_target.ship_type,
                source: "missed_detection"
            }
            missed_dataset.append(missed_sample)
```

### 17.2 定期模型更新

```
# 每月或每积累 N 个新标注样本后，重新训练模型
# 训练在岸基服务器上进行，更新后的模型通过 OTA 推送到船端

update_cycle:
    1. 收集最近一个月的在线标注数据
    2. 人工审核自动标注质量（抽查 10%）
    3. 将新数据加入训练集
    4. 重新训练模型（微调 fine-tune，50 epochs）
    5. 在测试集上评估——确保 mAP 不退化
    6. 部署新模型到船端
```

---

## 18. 内部子模块分解

| 子模块 | 职责 | 建议语言 | 硬件 |
|-------|------|---------|------|
| Radar CFAR Detector | CFAR 检测 + 后处理 | C++ | CPU |
| Camera DNN Detector | YOLOv8 推理 + NMS | C++/Python | GPU (Jetson) |
| LiDAR Clusterer | 点云聚类 + 包围盒 | C++ | CPU |
| Target Classifier | 多特征分类融合 | C++/Python | CPU |
| Attribute Estimator | 尺度/朝向估算 | C++ | CPU |
| Multi-Res Detector | 多分辨率检测策略 | Python | GPU |
| Online Sampler | 在线样本采集 | Python | CPU |
| Night Mode Handler | 夜间/恶劣天气策略切换 | C++ | CPU |

---

## 19. 数值算例

### 19.1 CFAR 检测算例

```
雷达参数：量程 12nm，距离分辨率 10m，方位分辨率 1°
CFAR 参数：P_fa=10⁻⁴, N_ref=16, guard=2

某距离单元回波强度 = 850（数字量）
相邻参考单元平均强度 = 120

阈值因子 T = 16 × (10^(4/16) - 1) = 12.45
阈值 = 120 × 12.45 = 1494

850 < 1494 → 未检测到（回波不够强）

另一个单元回波强度 = 2500，参考平均 = 130
阈值 = 130 × 12.45 = 1618
2500 > 1618 → 检测到目标！
SNR = 2500/130 = 19.2 = 12.8 dB
```

---

## 20. 参数总览表

| 类别 | 参数 | 默认值 |
|------|------|-------|
| **雷达 CFAR** | P_fa | 10⁻⁴ |
| | 保护单元 | 2 |
| | 参考单元 | 16（每侧 8） |
| **摄像头 DNN** | 检测置信度阈值 | 0.3 |
| | NMS IoU 阈值 | 0.5 |
| | 输入分辨率 | 640×640 |
| | 推理模式 | FP16 (TensorRT) |
| **LiDAR** | DBSCAN eps | 1.0 m |
| | DBSCAN min_points | 5 |
| **多帧确认** | 确认次数 | 3 |
| | 确认窗口 | 10 s |
| | 关联距离 | 50 m |

---

## 21. 与其他模块的协作关系

| 交互方 | 方向 | 数据 | 频率 |
|-------|------|------|------|
| Data Preprocessing → Detection | 输入 | 预处理后的传感器数据 | 各传感器频率 |
| Detection → Multi-Sensor Fusion | 输出 | DetectedObjectArray | 各传感器频率 |
| Detection → Online Sampler → Shore | 输出 | 在线标注样本 | 异步 |

---

## 22. 测试策略与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| ODC-001 | 良好天气大型船舶（3nm） | 检出率 > 99% |
| ODC-002 | 良好天气小型船舶（1nm） | 检出率 > 95% |
| ODC-003 | 远距离目标（6nm，雷达） | 检出率 > 90% |
| ODC-004 | 小目标（图像 < 20px） | 检出率 > 70% |
| ODC-005 | 夜间（雷达 + 红外） | 检出率 > 90% |
| ODC-006 | 雾天（仅雷达） | 检出率 > 85% |
| ODC-007 | 大浪（海况 5）雷达杂波 | 虚警率 < 5% |
| ODC-008 | 渔船群（10+ 目标） | 逐个检出率 > 80% |
| ODC-009 | 浮标/航标 | 分类准确率 > 80% |
| ODC-010 | 推理速度 | < 50ms/frame (GPU) |

---

## 23. 参考文献与标准

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Jocher, G. "YOLOv8" Ultralytics, 2023 | YOLO 系列目标检测器 |
| [2] | Richards, M.A. "Fundamentals of Radar Signal Processing" 2014 | CFAR 检测理论 |
| [3] | Shao, Z. et al. "SeaShips: A Large-scale Precisely Annotated Dataset for Ship Detection" IEEE TIP, 2018 | 海事目标检测数据集 |
| [4] | IEC 62388 | 海事雷达性能标准 |
| [5] | NVIDIA TensorRT Documentation | 模型优化与边缘部署 |

---

**文档结束**
